// curl_avio.c - I/O do ffmpeg via libcurl (streaming https com seek por Range).
// Motivo: o switch-ffmpeg nao tem TLS; o libcurl tem (mbedtls). Ver curl_avio.h.
//
// Modelo: uma transferencia curl (interface MULTI) alimenta um buffer; o ffmpeg
// puxa via cio_read. Em seek, a transferencia e reiniciada com CURLOPT_RANGE a
// partir da nova posicao. O tamanho total sai do header Content-Range.
#include "curl_avio.h"
#include <curl/curl.h>
#include <libavutil/mem.h>
#include <libavutil/error.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

typedef struct {
    CURLM *multi;
    CURL  *easy;
    char   url[2048];
    unsigned char *buf;   // bytes recebidos e ainda nao entregues (buf_off..buf_len)
    size_t buf_off;
    size_t buf_len;
    size_t buf_cap;
    int64_t size;         // content-length total (-1 = desconhecido)
    int64_t pos;          // proxima posicao logica a entregar
    int64_t stream_start; // offset onde a transferencia atual comecou
    int started;          // easy adicionado ao multi
    int running;          // transfers ativos
} CurlIO;

// --- write callback: acumula os bytes recebidos ---
static size_t cio_write_cb(char *ptr, size_t sz, size_t nm, void *ud) {
    CurlIO *c = (CurlIO *)ud;
    size_t n = sz * nm;
    if (n == 0) return 0;
    if (c->buf_len + n > c->buf_cap) {
        if (c->buf_off > 0) {                       // compacta o ja consumido
            memmove(c->buf, c->buf + c->buf_off, c->buf_len - c->buf_off);
            c->buf_len -= c->buf_off; c->buf_off = 0;
        }
        if (c->buf_len + n > c->buf_cap) {
            size_t ncap = c->buf_cap ? c->buf_cap : 131072;
            while (ncap < c->buf_len + n) ncap *= 2;
            unsigned char *nb = (unsigned char *)realloc(c->buf, ncap);
            if (!nb) return 0;                      // aborta a transferencia
            c->buf = nb; c->buf_cap = ncap;
        }
    }
    memcpy(c->buf + c->buf_len, ptr, n);
    c->buf_len += n;
    return n;
}

// --- header callback: captura o tamanho total (Content-Range: .../TOTAL) ---
static size_t cio_hdr_cb(char *ptr, size_t sz, size_t nm, void *ud) {
    CurlIO *c = (CurlIO *)ud;
    size_t n = sz * nm;
    if (n > 14 && strncasecmp(ptr, "Content-Range:", 14) == 0) {
        for (size_t i = 0; i + 1 < n; i++) {
            if (ptr[i] == '/') { long long t = atoll(ptr + i + 1); if (t > 0) c->size = t; break; }
        }
    } else if (n > 15 && c->size < 0 && c->stream_start == 0 &&
               strncasecmp(ptr, "Content-Length:", 15) == 0) {
        long long len = atoll(ptr + 15);
        if (len > 0) c->size = len;                 // so vale quando comecamos do 0
    }
    return n;
}

// Configura o easy handle e o coloca no multi comecando de c->pos.
static void cio_start(CurlIO *c) {
    if (c->started) return;
    char range[48];
    snprintf(range, sizeof(range), "%lld-", (long long)c->pos);
    c->stream_start = c->pos;
    curl_easy_setopt(c->easy, CURLOPT_URL, c->url);
    curl_easy_setopt(c->easy, CURLOPT_USERAGENT, "Nplay-Switch/1.0");
    curl_easy_setopt(c->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c->easy, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(c->easy, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(c->easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c->easy, CURLOPT_FAILONERROR, 1L);     // 4xx/5xx = falha
    curl_easy_setopt(c->easy, CURLOPT_RANGE, range);
    curl_easy_setopt(c->easy, CURLOPT_WRITEFUNCTION, cio_write_cb);
    curl_easy_setopt(c->easy, CURLOPT_WRITEDATA, c);
    curl_easy_setopt(c->easy, CURLOPT_HEADERFUNCTION, cio_hdr_cb);
    curl_easy_setopt(c->easy, CURLOPT_HEADERDATA, c);
    curl_multi_add_handle(c->multi, c->easy);
    c->started = 1;
    c->running = 1;
}

static void cio_stop(CurlIO *c) {
    if (c->started) { curl_multi_remove_handle(c->multi, c->easy); c->started = 0; }
    c->buf_off = c->buf_len = 0;
}

// --- read: entrega ao ffmpeg o que houver (short reads sao ok) ---
static int cio_read(void *opaque, uint8_t *out, int want) {
    CurlIO *c = (CurlIO *)opaque;
    if (want <= 0) return 0;
    if (!c->started) cio_start(c);

    int stall = 0;                                  // guarda contra travamento
    while (c->buf_len - c->buf_off == 0) {
        if (!c->running) return AVERROR_EOF;
        int before = (int)(c->buf_len - c->buf_off);
        CURLMcode mc = curl_multi_perform(c->multi, &c->running);
        if (mc != CURLM_OK) return AVERROR(EIO);
        if (c->buf_len - c->buf_off > 0) break;
        if (!c->running) {
            if (c->buf_len - c->buf_off > 0) break;
            return AVERROR_EOF;
        }
        int numfds = 0;
        curl_multi_poll(c->multi, NULL, 0, 200, &numfds);
        if ((int)(c->buf_len - c->buf_off) == before) {
            if (++stall > 150) return AVERROR(ETIMEDOUT);  // ~30s sem progresso
        } else stall = 0;
    }
    size_t avail = c->buf_len - c->buf_off;
    size_t n = avail < (size_t)want ? avail : (size_t)want;
    memcpy(out, c->buf + c->buf_off, n);
    c->buf_off += n;
    c->pos += n;
    if (c->buf_off == c->buf_len) { c->buf_off = c->buf_len = 0; }
    else if (c->buf_off > c->buf_cap / 2) {
        memmove(c->buf, c->buf + c->buf_off, c->buf_len - c->buf_off);
        c->buf_len -= c->buf_off; c->buf_off = 0;
    }
    return (int)n;
}

// --- seek: reinicia a transferencia no novo offset (Range) ---
static int64_t cio_seek(void *opaque, int64_t off, int whence) {
    CurlIO *c = (CurlIO *)opaque;
    if (whence == AVSEEK_SIZE) {
        if (c->size >= 0) return c->size;
        if (!c->started) cio_start(c);
        int guard = 0;
        while (c->size < 0 && c->running && guard++ < 300) {
            curl_multi_perform(c->multi, &c->running);
            if (c->size >= 0) break;
            int nf = 0; curl_multi_poll(c->multi, NULL, 0, 50, &nf);
        }
        return c->size >= 0 ? c->size : (int64_t)AVERROR(ENOSYS);
    }
    int64_t np;
    if (whence == SEEK_SET) np = off;
    else if (whence == SEEK_CUR) np = c->pos + off;
    else if (whence == SEEK_END) { if (c->size < 0) return AVERROR(ENOSYS); np = c->size + off; }
    else return AVERROR(EINVAL);
    if (np < 0) return AVERROR(EINVAL);
    if (np == c->pos && c->started) return c->pos;

    cio_stop(c);
    c->pos = np;
    cio_start(c);
    return np;
}

AVIOContext *nplay_curl_avio_open(const char *url) {
    if (!url || !url[0]) return NULL;
    CurlIO *c = (CurlIO *)calloc(1, sizeof(CurlIO));
    if (!c) return NULL;
    snprintf(c->url, sizeof(c->url), "%s", url);
    c->size = -1; c->pos = 0;
    c->multi = curl_multi_init();
    c->easy = curl_easy_init();
    if (!c->multi || !c->easy) {
        if (c->easy) curl_easy_cleanup(c->easy);
        if (c->multi) curl_multi_cleanup(c->multi);
        free(c);
        return NULL;
    }
    const int avio_bufsz = 65536;
    unsigned char *avio_buf = (unsigned char *)av_malloc(avio_bufsz);
    if (!avio_buf) { curl_easy_cleanup(c->easy); curl_multi_cleanup(c->multi); free(c); return NULL; }
    AVIOContext *ctx = avio_alloc_context(avio_buf, avio_bufsz, 0, c, cio_read, NULL, cio_seek);
    if (!ctx) { av_free(avio_buf); curl_easy_cleanup(c->easy); curl_multi_cleanup(c->multi); free(c); return NULL; }
    return ctx;
}

void nplay_curl_avio_close(AVIOContext *ctx) {
    if (!ctx) return;
    CurlIO *c = (CurlIO *)ctx->opaque;
    if (c) {
        if (c->started) curl_multi_remove_handle(c->multi, c->easy);
        if (c->easy) curl_easy_cleanup(c->easy);
        if (c->multi) curl_multi_cleanup(c->multi);
        free(c->buf);
        free(c);
    }
    av_freep(&ctx->buffer);
    avio_context_free(&ctx);
}
