// curl_avio.c - I/O do ffmpeg via libcurl (https com seek por Range) COM
// PREFETCH em thread de fundo, pra reproducao fluida (sem travar o decode).
//
// Motivo: o switch-ffmpeg nao tem TLS; o libcurl tem (mbedtls). Antes a leitura
// era bloqueante (cada bloco travava o player esperando a rede -> engasgos nos
// animes). Agora uma THREAD produtora baixa blocos a frente e enche um ring
// buffer; o ffmpeg (consumidor) le do buffer sem esperar a rede. Seek reposiciona
// a thread. Usa a interface EASY do curl (a MULTI falhava no Switch).
#include "curl_avio.h"
#include <curl/curl.h>
#include <SDL.h>
#include <libavutil/mem.h>
#include <libavutil/error.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

#define BLOCK   (4 * 1024 * 1024)      // cada busca da thread (aumentado para 4MB)
#define TMPCAP  (BLOCK + 65536)
#define RINGCAP (32 * 1024 * 1024)     // ~32MB de leitura antecipada

typedef struct {
    CURL *easy;
    char  url[2048];
    unsigned char *ring;
    size_t ring_cap;
    size_t head, count;                // head = 1o byte disponivel; count = bytes no ring
    volatile int64_t base;             // offset (arquivo) de ring[head] = pos do consumidor
    volatile int64_t size;             // total (-1 desconhecido)
    volatile int64_t seek_req;         // pedido de seek (-1 = nenhum)
    volatile int running, eof, err;
    SDL_mutex *mtx;
    SDL_cond  *c_data, *c_space;
    SDL_Thread *th;
    unsigned char *tmp; size_t tmp_len;
} CurlIO;

static size_t wr_tmp(char *ptr, size_t sz, size_t nm, void *ud) {
    CurlIO *c = (CurlIO *)ud; size_t n = sz * nm;
    if (c->tmp_len + n > TMPCAP) return 0;
    memcpy(c->tmp + c->tmp_len, ptr, n); c->tmp_len += n; return n;
}
static size_t hdr_size(char *ptr, size_t sz, size_t nm, void *ud) {
    CurlIO *c = (CurlIO *)ud; size_t n = sz * nm;
    if (n > 14 && strncasecmp(ptr, "Content-Range:", 14) == 0)
        for (size_t i = 0; i + 1 < n; i++) if (ptr[i] == '/') { long long t = atoll(ptr + i + 1); if (t > 0) c->size = t; break; }
    return n;
}
// aborta o transfer em andamento quando fecha ou pede seek (deixa o close/seek rapidos)
static int xfer_cb(void *ud, curl_off_t a, curl_off_t b, curl_off_t d, curl_off_t e) {
    (void)a; (void)b; (void)d; (void)e;
    CurlIO *c = (CurlIO *)ud;
    return (!c->running || c->seek_req >= 0) ? 1 : 0;
}

static int fetch_block(CurlIO *c, int64_t start) {
    c->tmp_len = 0;
    char range[64];
    snprintf(range, sizeof(range), "%lld-%lld", (long long)start, (long long)(start + BLOCK - 1));
    curl_easy_setopt(c->easy, CURLOPT_RANGE, range);
    CURLcode r = curl_easy_perform(c->easy);
    if (r != CURLE_OK) return -1;
    long code = 0; curl_easy_getinfo(c->easy, CURLINFO_RESPONSE_CODE, &code);
    if (code != 200 && code != 206) return -1;
    return (int)c->tmp_len;
}
// Encerra a thread e libera tudo do CurlIO.
static void free_cio(CurlIO *c) {
    if (!c) return;
    if (c->th) { SDL_LockMutex(c->mtx); c->running = 0; SDL_CondSignal(c->c_space); SDL_CondSignal(c->c_data); SDL_UnlockMutex(c->mtx); SDL_WaitThread(c->th, NULL); }
    if (c->easy) curl_easy_cleanup(c->easy);
    if (c->mtx) SDL_DestroyMutex(c->mtx);
    if (c->c_data) SDL_DestroyCond(c->c_data);
    if (c->c_space) SDL_DestroyCond(c->c_space);
    free(c->ring); free(c->tmp); free(c);
}
static void ring_put(CurlIO *c, const unsigned char *src, size_t n) {
    size_t tail = (c->head + c->count) % c->ring_cap;
    size_t first = c->ring_cap - tail; if (first > n) first = n;
    memcpy(c->ring + tail, src, first);
    if (n > first) memcpy(c->ring, src + first, n - first);
    c->count += n;
}

// thread produtora: baixa blocos a frente e enche o ring buffer
static int producer(void *arg) {
    CurlIO *c = (CurlIO *)arg;
    int64_t prod;
    int fails = 0;
    SDL_LockMutex(c->mtx); prod = c->base; SDL_UnlockMutex(c->mtx);
    while (1) {
        SDL_LockMutex(c->mtx);
        if (!c->running) { SDL_UnlockMutex(c->mtx); break; }
        if (c->seek_req >= 0) { prod = c->seek_req; c->base = c->seek_req; c->head = c->count = 0; c->seek_req = -1; c->eof = 0; }
        if (c->size >= 0 && prod >= c->size) { c->eof = 1; SDL_CondSignal(c->c_data); SDL_CondWaitTimeout(c->c_space, c->mtx, 200); SDL_UnlockMutex(c->mtx); continue; }
        if (c->count + BLOCK > c->ring_cap) { SDL_CondWaitTimeout(c->c_space, c->mtx, 200); SDL_UnlockMutex(c->mtx); continue; }
        int64_t start = prod;
        SDL_UnlockMutex(c->mtx);

        int got = fetch_block(c, start);            // rede, fora do lock

        SDL_LockMutex(c->mtx);
        if (!c->running) { SDL_UnlockMutex(c->mtx); break; }
        if (c->seek_req >= 0) { SDL_UnlockMutex(c->mtx); continue; }   // seek durante o fetch -> descarta
        if (got < 0) {                                                 // falha de rede: tenta de novo (bufferiza)
            if (++fails >= 6) { c->err = 1; SDL_CondSignal(c->c_data); }
            SDL_UnlockMutex(c->mtx);
            SDL_Delay(300);
            continue;
        }
        fails = 0;
        ring_put(c, c->tmp, (size_t)got);
        prod += got;
        if (got < BLOCK && c->size < 0) c->size = prod;
        SDL_CondSignal(c->c_data);
        SDL_UnlockMutex(c->mtx);
    }
    return 0;
}

static int cio_read(void *opaque, uint8_t *out, int want) {
    CurlIO *c = (CurlIO *)opaque;
    if (want <= 0) return 0;
    SDL_LockMutex(c->mtx);
    while (c->count == 0 && !c->eof && !c->err && c->running)
        SDL_CondWaitTimeout(c->c_data, c->mtx, 300);
    if (c->count == 0) {
        if (c->running && !c->err && !c->eof) {
            SDL_UnlockMutex(c->mtx);
            return AVERROR(EAGAIN);
        }
        int eof = c->eof;
        SDL_UnlockMutex(c->mtx);
        return eof ? AVERROR_EOF : AVERROR(EIO);
    }
    size_t n = c->count < (size_t)want ? c->count : (size_t)want;
    size_t first = c->ring_cap - c->head; if (first > n) first = n;
    memcpy(out, c->ring + c->head, first);
    if (n > first) memcpy(out + first, c->ring, n - first);
    c->head = (c->head + n) % c->ring_cap;
    c->count -= n;
    c->base += n;
    SDL_CondSignal(c->c_space);
    SDL_UnlockMutex(c->mtx);
    return (int)n;
}

static int64_t cio_seek(void *opaque, int64_t off, int whence) {
    CurlIO *c = (CurlIO *)opaque;
    if (whence == AVSEEK_SIZE) {
        SDL_LockMutex(c->mtx);
        int guard = 0;
        while (c->size < 0 && c->running && !c->err && guard++ < 150) SDL_CondWaitTimeout(c->c_data, c->mtx, 100);
        int64_t s = c->size;
        SDL_UnlockMutex(c->mtx);
        return s >= 0 ? s : (int64_t)AVERROR(ENOSYS);
    }
    SDL_LockMutex(c->mtx);
    int64_t pos = c->base, sz = c->size;
    SDL_UnlockMutex(c->mtx);
    int64_t np;
    if (whence == SEEK_SET) np = off;
    else if (whence == SEEK_CUR) np = pos + off;
    else if (whence == SEEK_END) { if (sz < 0) return AVERROR(ENOSYS); np = sz + off; }
    else return AVERROR(EINVAL);
    if (np < 0) return AVERROR(EINVAL);

    SDL_LockMutex(c->mtx);
    if (np >= c->base && np < c->base + (int64_t)c->count) {   // ja no buffer: so avanca
        size_t drop = (size_t)(np - c->base);
        c->head = (c->head + drop) % c->ring_cap;
        c->count -= drop; c->base = np;
    } else {                                                   // fora: limpa e reposiciona a thread
        c->head = c->count = 0; c->base = np; c->seek_req = np; c->eof = 0;
        SDL_CondSignal(c->c_space);
    }
    SDL_UnlockMutex(c->mtx);
    return np;
}

AVIOContext *nplay_curl_avio_open(const char *url) {
    if (!url || !url[0]) return NULL;
    CurlIO *c = (CurlIO *)calloc(1, sizeof(CurlIO));
    if (!c) return NULL;
    snprintf(c->url, sizeof(c->url), "%s", url);
    c->size = -1; c->seek_req = -1; c->base = 0; c->running = 1; c->ring_cap = RINGCAP;
    c->ring = (unsigned char *)malloc(c->ring_cap);
    c->tmp  = (unsigned char *)malloc(TMPCAP);
    c->easy = curl_easy_init();
    c->mtx = SDL_CreateMutex();
    c->c_data = SDL_CreateCond();
    c->c_space = SDL_CreateCond();
    if (!c->ring || !c->tmp || !c->easy || !c->mtx || !c->c_data || !c->c_space) { free_cio(c); return NULL; }
    curl_easy_setopt(c->easy, CURLOPT_URL, c->url);
    curl_easy_setopt(c->easy, CURLOPT_USERAGENT, "Nplay-Switch/1.0");
    curl_easy_setopt(c->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c->easy, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(c->easy, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(c->easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c->easy, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(c->easy, CURLOPT_TCP_KEEPIDLE, 120L);
    curl_easy_setopt(c->easy, CURLOPT_TCP_KEEPINTVL, 60L);
    curl_easy_setopt(c->easy, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c->easy, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(c->easy, CURLOPT_WRITEFUNCTION, wr_tmp);
    curl_easy_setopt(c->easy, CURLOPT_WRITEDATA, c);
    curl_easy_setopt(c->easy, CURLOPT_HEADERFUNCTION, hdr_size);
    curl_easy_setopt(c->easy, CURLOPT_HEADERDATA, c);
    curl_easy_setopt(c->easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c->easy, CURLOPT_XFERINFOFUNCTION, xfer_cb);
    curl_easy_setopt(c->easy, CURLOPT_XFERINFODATA, c);

    unsigned char *avio_buf = (unsigned char *)av_malloc(65536);
    if (!avio_buf) { free_cio(c); return NULL; }
    c->th = SDL_CreateThread(producer, "cavio", c);
    if (!c->th) { av_free(avio_buf); free_cio(c); return NULL; }
    AVIOContext *ctx = avio_alloc_context(avio_buf, 65536, 0, c, cio_read, NULL, cio_seek);
    if (!ctx) { av_free(avio_buf); free_cio(c); return NULL; }
    return ctx;
}

void nplay_curl_avio_close(AVIOContext *ctx) {
    if (!ctx) return;
    free_cio((CurlIO *)ctx->opaque);
    av_freep(&ctx->buffer);
    avio_context_free(&ctx);
}
