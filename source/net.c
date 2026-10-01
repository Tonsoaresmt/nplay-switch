// net.c - implementacao da camada de HTTP (libcurl).
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <curl/curl.h>
#include <SDL.h>
#include "cacert_bin.h"

// User-Agent de navegador: o Cloudflare do servidor bloqueia UAs "de bot".
// TODO: trocar por "Meruem-Switch/x" + regra de allowlist no Cloudflare.
#define USER_AGENT "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " \
                   "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"

static SDL_atomic_t g_profile_id;
void net_set_profile_id(int profile_id) { SDL_AtomicSet(&g_profile_id, profile_id > 0 ? profile_id : 0); }
int net_get_profile_id(void) { return SDL_AtomicGet(&g_profile_id); }

static struct curl_slist *append_profile_header(struct curl_slist *headers,
                                                const char *url, const char *bearer) {
    extern const char *BASE;
    int profile_id = net_get_profile_id();
    size_t base_len = strlen(BASE);
    if (profile_id <= 0 || !bearer || !bearer[0] || !url ||
        strncmp(url, BASE, base_len) || strncmp(url + base_len, "/api/", 5)) return headers;
    char header[64];
    snprintf(header, sizeof(header), "X-Profile-Id: %d", profile_id);
    return curl_slist_append(headers, header);
}

void membuf_free(struct membuf *m) {
    if (!m) return;
    free(m->data);
    m->data = NULL;
    m->len = 0;
    m->cap = 0;
}

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    if (size != 0 && nmemb > SIZE_MAX / size) return 0;
    size_t add = size * nmemb;
    struct membuf *m = (struct membuf *)userdata;
    if (m->len == SIZE_MAX || add > SIZE_MAX - m->len - 1) return 0;
    size_t need = m->len + add + 1;
    if (need > m->cap) {
        size_t cap = m->cap ? m->cap : 4096;
        while (cap < need) {
            if (cap > SIZE_MAX / 2) { cap = need; break; }
            cap *= 2;
        }
        char *np = realloc(m->data, cap);
        if (!np) return 0;             // sem memoria -> aborta o download
        m->data = np;
        m->cap = cap;
    }
    memcpy(m->data + m->len, ptr, add);
    m->len += add;
    m->data[m->len] = '\0';
    return add;
}

struct file_download_ctx { FILE *file; net_progress_cb progress; void *userdata; };

static size_t file_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    struct file_download_ctx *ctx = (struct file_download_ctx *)userdata;
    return fwrite(ptr, size, nmemb, ctx->file);
}

static int file_progress_cb(void *userdata, curl_off_t dltotal, curl_off_t dlnow,
                            curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal; (void)ulnow;
    struct file_download_ctx *ctx = (struct file_download_ctx *)userdata;
    return (ctx && ctx->progress) ? ctx->progress((long long)dlnow, (long long)dltotal, ctx->userdata) : 0;
}

// ---- cache DNS e sessao TLS (CURLSH) ----------------------------------------
// Cada request possui seu handle. Somente DNS e sessoes TLS sao compartilhados
// com locks; pools de conexao concorrentes nao sao suportados pelo libcurl.
static CURLSH *g_share = NULL;
static SDL_mutex *g_share_mtx[CURL_LOCK_DATA_LAST];
static int g_ca_ready = 0;
static const char *g_ca_path = "sdmc:/switch/.nplay-ca.pem";

static int ca_file_matches(void) {
    unsigned char buffer[8192];
    size_t offset = 0, count;
    FILE *file = fopen(g_ca_path, "rb");
    if (!file) return 0;
    while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (offset + count > cacert_bin_size ||
            memcmp(buffer, cacert_bin + offset, count) != 0) {
            fclose(file);
            return 0;
        }
        offset += count;
    }
    int matches = !ferror(file) && offset == cacert_bin_size;
    fclose(file);
    return matches;
}

static int provision_ca_bundle(void) {
    static const char *tmp_path = "sdmc:/switch/.nplay-ca.pem.new";
    if (ca_file_matches()) return 0;
    FILE *file = fopen(tmp_path, "wb");
    if (!file) return -1;
    size_t written = fwrite(cacert_bin, 1, cacert_bin_size, file);
    int failed = written != cacert_bin_size || fflush(file) != 0 || ferror(file);
    if (fclose(file) != 0) failed = 1;
    if (failed) { remove(tmp_path); return -1; }
    remove(g_ca_path);
    if (rename(tmp_path, g_ca_path) != 0) { remove(tmp_path); return -1; }
    return ca_file_matches() ? 0 : -1;
}

static void share_lock(CURL *h, curl_lock_data data, curl_lock_access acc, void *u) {
    (void)h; (void)acc; (void)u;
    if ((int)data >= 0 && data < CURL_LOCK_DATA_LAST && g_share_mtx[data]) SDL_LockMutex(g_share_mtx[data]);
}
static void share_unlock(CURL *h, curl_lock_data data, void *u) {
    (void)h; (void)u;
    if ((int)data >= 0 && data < CURL_LOCK_DATA_LAST && g_share_mtx[data]) SDL_UnlockMutex(g_share_mtx[data]);
}

int net_init(void) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) return -1;
    g_ca_ready = provision_ca_bundle() == 0;
    int locks_ready = 1;
    for (int i = 0; i < CURL_LOCK_DATA_LAST; i++) {
        g_share_mtx[i] = SDL_CreateMutex();
        if (!g_share_mtx[i]) locks_ready = 0;
    }
    g_share = locks_ready ? curl_share_init() : NULL;
    if (g_share) {
        curl_share_setopt(g_share, CURLSHOPT_LOCKFUNC, share_lock);
        curl_share_setopt(g_share, CURLSHOPT_UNLOCKFUNC, share_unlock);
        // libcurl explicitly forbids sharing connection pools between concurrent
        // threads, even with lock callbacks. DNS/TLS session sharing is supported.
        curl_share_setopt(g_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        curl_share_setopt(g_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
    }
    return 0;   // segue mesmo sem share (so perde o reuso de conexao)
}

void net_exit(void) {
    if (g_share) { curl_share_cleanup(g_share); g_share = NULL; }
    for (int i = 0; i < CURL_LOCK_DATA_LAST; i++) {
        if (g_share_mtx[i]) { SDL_DestroyMutex(g_share_mtx[i]); g_share_mtx[i] = NULL; }
    }
    curl_global_cleanup();
    g_ca_ready = 0;
}

void net_configure_curl_isolated(CURL *curl) {
    if (!curl) return;
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);   // mantem a conexao viva
    if (g_ca_ready) curl_easy_setopt(curl, CURLOPT_CAINFO, g_ca_path);
}

void net_configure_curl(CURL *curl) {
    if (!curl) return;
    if (g_share) curl_easy_setopt(curl, CURLOPT_SHARE, g_share);
    net_configure_curl_isolated(curl);
}

static int request_cancel_cb(void *userdata, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t ultotal, curl_off_t ulnow) {
    (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
    return SDL_AtomicGet((SDL_atomic_t *)userdata) ? 1 : 0;
}

long net_request_timeout_cancel(const char *url, const char *method,
                                const char *body, const char *bearer,
                                struct membuf *out, const char **err,
                                long connect_timeout, long total_timeout,
                                SDL_atomic_t *cancel) {
    if (err) *err = NULL;
    if (cancel && SDL_AtomicGet(cancel)) return -(long)CURLE_ABORTED_BY_CALLBACK;

    CURL *curl = curl_easy_init();
    if (!curl) { if (err) *err = "curl_easy_init falhou"; return -1; }

    // Sem "Accept" fixo: assim o mesmo helper serve para JSON e para imagens.
    struct curl_slist *headers = NULL;
    if (body) headers = curl_slist_append(headers, "Content-Type: application/json");

    char authbuf[1024];
    if (bearer && bearer[0]) {
        snprintf(authbuf, sizeof(authbuf), "Authorization: Bearer %s", bearer);
        headers = curl_slist_append(headers, authbuf);
    }
    headers = append_profile_header(headers, url, bearer);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, total_timeout);
    // Validação TLS reativada conforme política de segurança.
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    if (cancel) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, request_cancel_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancel);
    }
    net_configure_curl(curl);

    if (method && strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body ? body : "");
    } else if (method && strcmp(method, "GET") != 0) {
        // DELETE/PUT/etc: metodo custom. Se veio corpo, manda como campos.
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
        if (body) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    }

    CURLcode res = curl_easy_perform(curl);
    long code;
    if (res != CURLE_OK) {
        if (err) *err = curl_easy_strerror(res);
        code = -(long)res;
    } else {
        code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);   // conexao volta pro cache compartilhado (nao fecha)
    return code;
}

long net_request_timeout(const char *url, const char *method,
                         const char *body, const char *bearer,
                         struct membuf *out, const char **err,
                         long connect_timeout, long total_timeout) {
    return net_request_timeout_cancel(url, method, body, bearer, out, err,
                                      connect_timeout, total_timeout, NULL);
}

typedef struct { struct membuf *out; size_t limit; SDL_atomic_t *cancel; } LimitedText;
static size_t limited_text_write(char *ptr, size_t size, size_t count, void *opaque) {
    LimitedText *ctx = opaque;
    if ((ctx->cancel && SDL_AtomicGet(ctx->cancel)) ||
        (size && count > SIZE_MAX / size)) return 0;
    size_t bytes = size * count;
    if (ctx->out->len > ctx->limit || bytes > ctx->limit - ctx->out->len) return 0;
    size_t need = ctx->out->len + bytes + 1;
    if (need > ctx->out->cap) {
        size_t cap = ctx->out->cap ? ctx->out->cap : 4096;
        while (cap < need) cap *= 2; // limit <= 4 MiB, so no overflow.
        if (cap > ctx->limit + 1) cap = ctx->limit + 1;
        char *grown = realloc(ctx->out->data, cap);
        if (!grown) return 0;
        ctx->out->data = grown; ctx->out->cap = cap;
    }
    memcpy(ctx->out->data + ctx->out->len, ptr, bytes);
    ctx->out->len += bytes; ctx->out->data[ctx->out->len] = 0;
    return bytes;
}

long net_get_text_limited(const char *url, size_t limit, long timeout_ms,
                           SDL_atomic_t *cancel, struct membuf *out) {
    if (!out || out->data || !url || strncmp(url, "https://", 8) ||
        !limit || limit > 4u * 1024u * 1024u || timeout_ms <= 0 ||
        (cancel && SDL_AtomicGet(cancel))) return -1;
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    LimitedText ctx = {out, limit, cancel};
    net_configure_curl_isolated(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 2500L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)limit);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, limited_text_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    if (cancel) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, request_cancel_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancel);
    }
    CURLcode rc = curl_easy_perform(curl);
    long code = 0;
    if (rc == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    else code = -(long)rc;
    curl_easy_cleanup(curl);
    if (code != 200 || (cancel && SDL_AtomicGet(cancel))) {
        membuf_free(out);
        if (code == 200) code = -1;
    }
    return code;
}

long net_request(const char *url, const char *method,
                 const char *body, const char *bearer,
                 struct membuf *out, const char **err) {
    return net_request_timeout(url, method, body, bearer, out, err, 15L, 45L);
}

typedef struct {
    CURL *curl; size_t received, limit; Uint32 last_byte;
    SDL_atomic_t *cancel; net_text_chunk_cb callback; void *userdata;
} TextStream;
static size_t text_stream_write(char *data,size_t size,size_t count,void *opaque) {
    TextStream *s=opaque;
    if((size&&count>SIZE_MAX/size)||SDL_AtomicGet(s->cancel))return 0;
    size_t bytes=size*count;
    if(bytes>s->limit-s->received)return 0;
    long status=0;curl_easy_getinfo(s->curl,CURLINFO_RESPONSE_CODE,&status);
    // Preserve the HTTP status for the caller.  Aborting the body on a 404/503
    // turns it into CURLE_WRITE_ERROR and hides the actionable server result.
    if(status&&status!=200)return bytes;
    if(status!=200)return 0;
    s->last_byte=SDL_GetTicks();s->received+=bytes;
    return s->callback(data,bytes,s->userdata)?bytes:0;
}
static int text_stream_progress(void *opaque,curl_off_t a,curl_off_t b,curl_off_t c,curl_off_t d) {
    (void)a;(void)b;(void)c;(void)d;TextStream *s=opaque;
    return SDL_AtomicGet(s->cancel)||SDL_GetTicks()-s->last_byte>90000u;
}
long net_stream_text(const char *url,size_t limit,SDL_atomic_t *cancel,
                      net_text_chunk_cb callback,void *userdata) {
    if(!url||strncmp(url,"https://",8)||!cancel||!callback||!limit||limit>4u*1024u*1024u||SDL_AtomicGet(cancel))return -1;
    CURL *curl=curl_easy_init();if(!curl)return -1;
    TextStream stream={.curl=curl,.limit=limit,.last_byte=SDL_GetTicks(),.cancel=cancel,.callback=callback,.userdata=userdata};
    net_configure_curl_isolated(curl);
    curl_easy_setopt(curl,CURLOPT_URL,url);
    curl_easy_setopt(curl,CURLOPT_USERAGENT,USER_AGENT);
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L);
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT_MS,2500L);
    curl_easy_setopt(curl,CURLOPT_MAXFILESIZE_LARGE,(curl_off_t)limit);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,text_stream_write);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&stream);
    curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);
    curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,text_stream_progress);
    curl_easy_setopt(curl,CURLOPT_XFERINFODATA,&stream);
    CURLcode rc=curl_easy_perform(curl);long code=0;
    curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&code);
    curl_easy_cleanup(curl);
    if(rc!=CURLE_OK)return -(long)rc;
    return code;
}

long net_download_file_timeout(const char *url, const char *bearer,
                               const char *path, const char **err,
                               long connect_timeout, long total_timeout) {
    CURL *curl;
    CURLcode res;
    struct curl_slist *headers = NULL;
    char authbuf[1024];
    FILE *f;
    struct file_download_ctx dlctx = {0};
    long code = -1;

    if (err) *err = NULL;
    if (!path) {
        if (err) *err = "path invalido";
        return -1;
    }

    f = fopen(path, "wb");
    if (!f) {
        if (err) *err = "fopen falhou";
        return -1;
    }
    dlctx.file = f;

    curl = curl_easy_init();
    if (!curl) {
        fclose(f);
        if (err) *err = "curl_easy_init falhou";
        return -1;
    }

    if (bearer && bearer[0]) {
        snprintf(authbuf, sizeof(authbuf), "Authorization: Bearer %s", bearer);
        headers = curl_slist_append(headers, authbuf);
    }
    headers = append_profile_header(headers, url, bearer);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, total_timeout);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 256L * 1024L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dlctx);
    net_configure_curl(curl);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        if (err) *err = curl_easy_strerror(res);
        code = -(long)res;
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    fclose(f);

    if (code != 200) remove(path);
    return code;
}

long net_download_file_progress(const char *url, const char *bearer,
                                const char *path, const char **err,
                                net_progress_cb progress, void *userdata) {
    CURL *curl;
    CURLcode res;
    struct curl_slist *headers = NULL;
    char authbuf[1024];
    long code = -1;
    FILE *f = NULL;
    struct file_download_ctx dlctx = {0};
    if (err) *err = NULL;
    if (!url || !path) { if (err) *err = "parametro invalido"; return -1; }
    f = fopen(path, "wb");
    if (!f) { if (err) *err = "nao foi possivel criar arquivo na microSD"; return -1; }
    dlctx.file = f; dlctx.progress = progress; dlctx.userdata = userdata;
    curl = curl_easy_init();
    if (!curl) { fclose(f); if (err) *err = "curl_easy_init falhou"; return -1; }
    if (bearer && bearer[0]) {
        snprintf(authbuf, sizeof(authbuf), "Authorization: Bearer %s", bearer);
        headers = curl_slist_append(headers, authbuf);
    }
    headers = append_profile_header(headers, url, bearer);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 45L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 256L * 1024L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dlctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, file_progress_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &dlctx);
    net_configure_curl(curl);
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) { if (err) *err = curl_easy_strerror(res); code = -(long)res; }
    else curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    fclose(f);
    if (code != 200) remove(path);
    return code;
}

long net_download_file(const char *url, const char *bearer,
                       const char *path, const char **err) {
    return net_download_file_timeout(url, bearer, path, err, 15L, 180L);
}

void net_urlencode(const char *in, char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!in || !in[0]) return;
    char *enc = curl_easy_escape(NULL, in, 0);
    if (enc) {
        snprintf(out, cap, "%s", enc);
        curl_free(enc);
    }
}
