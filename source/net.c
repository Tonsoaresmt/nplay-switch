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

// ---- conexao compartilhada (CURLSH) -----------------------------------------
// Cada request usa seu proprio handle (thread-safe: capas/paginas baixam em
// threads), mas TODOS compartilham o cache de conexao/DNS/sessao-TLS via CURLSH.
// Assim a conexao TCP+TLS aberta e reaproveitada entre fetches (keepalive),
// evitando o handshake TLS (caro) a cada chamada. Os locks por mutex tornam o
// compartilhamento seguro entre as threads.
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
    for (int i = 0; i < CURL_LOCK_DATA_LAST; i++) g_share_mtx[i] = SDL_CreateMutex();
    g_share = curl_share_init();
    if (g_share) {
        curl_share_setopt(g_share, CURLSHOPT_LOCKFUNC, share_lock);
        curl_share_setopt(g_share, CURLSHOPT_UNLOCKFUNC, share_unlock);
        curl_share_setopt(g_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
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
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);   // mantem a conexao viva
    if (g_ca_ready) curl_easy_setopt(curl, CURLOPT_CAINFO, g_ca_path);
}

void net_configure_curl(CURL *curl) {
    if (!curl) return;
    if (g_share) curl_easy_setopt(curl, CURLOPT_SHARE, g_share);
    net_configure_curl_isolated(curl);
}

long net_request_timeout(const char *url, const char *method,
                         const char *body, const char *bearer,
                         struct membuf *out, const char **err,
                         long connect_timeout, long total_timeout) {
    if (err) *err = NULL;

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

long net_request(const char *url, const char *method,
                 const char *body, const char *bearer,
                 struct membuf *out, const char **err) {
    return net_request_timeout(url, method, body, bearer, out, err, 15L, 45L);
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
