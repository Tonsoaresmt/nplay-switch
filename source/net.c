// net.c - implementacao da camada de HTTP (libcurl).
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <SDL.h>

// User-Agent de navegador: o Cloudflare do servidor bloqueia UAs "de bot".
// TODO: trocar por "Meruem-Switch/x" + regra de allowlist no Cloudflare.
#define USER_AGENT "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " \
                   "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"

void membuf_free(struct membuf *m) {
    if (!m) return;
    free(m->data);
    m->data = NULL;
    m->len = 0;
}

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t add = size * nmemb;
    struct membuf *m = (struct membuf *)userdata;
    char *np = realloc(m->data, m->len + add + 1);
    if (!np) return 0;                 // sem memoria -> aborta o download
    m->data = np;
    memcpy(m->data + m->len, ptr, add);
    m->len += add;
    m->data[m->len] = '\0';
    return add;
}

static size_t file_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    FILE *f = (FILE *)userdata;
    return fwrite(ptr, size, nmemb, f);
}

// ---- conexao compartilhada (CURLSH) -----------------------------------------
// Cada request usa seu proprio handle (thread-safe: capas/paginas baixam em
// threads), mas TODOS compartilham o cache de conexao/DNS/sessao-TLS via CURLSH.
// Assim a conexao TCP+TLS aberta e reaproveitada entre fetches (keepalive),
// evitando o handshake TLS (caro) a cada chamada. Os locks por mutex tornam o
// compartilhamento seguro entre as threads.
static CURLSH *g_share = NULL;
static SDL_mutex *g_share_mtx[CURL_LOCK_DATA_LAST];

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
}

static void net_apply_shared(CURL *curl) {
    if (g_share) curl_easy_setopt(curl, CURLOPT_SHARE, g_share);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);   // mantem a conexao viva
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
    // TODO (seguranca): verificar com cacert.pem no romfs em vez de desligar.
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    net_apply_shared(curl);

    if (method && strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body ? body : "");
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
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 256L * 1024L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
    net_apply_shared(curl);

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
