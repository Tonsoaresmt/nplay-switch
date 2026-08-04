// curl_avio.c - I/O do ffmpeg via libcurl (https com seek por Range).
// Motivo: o switch-ffmpeg nao tem TLS; o libcurl tem (mbedtls). Ver curl_avio.h.
//
// Estrategia: interface EASY BLOQUEANTE (a mesma do net.c, que comprovadamente
// funciona no Switch). Le em CHUNKS: cada leitura fora do chunk atual dispara um
// GET com CURLOPT_RANGE "inicio-fim". Seek so muda a posicao logica; o proximo
// read busca o chunk certo. O tamanho total sai do header Content-Range.
// (A interface MULTI foi tentada antes e falhava no Switch -> voltamos p/ EASY.)
#include "curl_avio.h"
#include <curl/curl.h>
#include <libavutil/mem.h>
#include <libavutil/error.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

#define CHUNK (2 * 1024 * 1024)   // 2MB por requisicao

typedef struct {
    CURL   *easy;
    char    url[2048];
    unsigned char *buf;    // chunk atual em memoria
    size_t  buf_len;       // bytes validos no buf
    size_t  buf_cap;
    int64_t buf_start;     // offset (no arquivo) de buf[0]
    int64_t size;          // tamanho total (-1 = desconhecido)
    int64_t pos;           // posicao logica de leitura
} CurlIO;

// write: acumula os bytes do chunk atual
static size_t wr_cb(char *ptr, size_t sz, size_t nm, void *ud) {
    CurlIO *c = (CurlIO *)ud;
    size_t n = sz * nm;
    if (c->buf_len + n > c->buf_cap) {
        size_t ncap = c->buf_cap ? c->buf_cap : CHUNK;
        while (ncap < c->buf_len + n) ncap *= 2;
        unsigned char *nb = (unsigned char *)realloc(c->buf, ncap);
        if (!nb) return 0;
        c->buf = nb; c->buf_cap = ncap;
    }
    memcpy(c->buf + c->buf_len, ptr, n);
    c->buf_len += n;
    return n;
}

// header: pega o tamanho total de "Content-Range: bytes X-Y/TOTAL"
static size_t hdr_cb(char *ptr, size_t sz, size_t nm, void *ud) {
    CurlIO *c = (CurlIO *)ud;
    size_t n = sz * nm;
    if (n > 14 && strncasecmp(ptr, "Content-Range:", 14) == 0) {
        for (size_t i = 0; i + 1 < n; i++) {
            if (ptr[i] == '/') { long long t = atoll(ptr + i + 1); if (t > 0) c->size = t; break; }
        }
    }
    return n;
}

// Busca um chunk de CHUNK bytes a partir de 'start'. Retorna 0 ok, -1 erro.
static int fetch_chunk(CurlIO *c, int64_t start) {
    if (c->size >= 0 && start >= c->size) { c->buf_start = start; c->buf_len = 0; return 0; }
    c->buf_len = 0;
    c->buf_start = start;
    int64_t end = start + CHUNK - 1;
    char range[64];
    snprintf(range, sizeof(range), "%lld-%lld", (long long)start, (long long)end);
    curl_easy_setopt(c->easy, CURLOPT_RANGE, range);
    CURLcode r = curl_easy_perform(c->easy);
    if (r != CURLE_OK) return -1;
    long code = 0;
    curl_easy_getinfo(c->easy, CURLINFO_RESPONSE_CODE, &code);
    if (code != 200 && code != 206) return -1;
    if (code == 200) {                 // servidor ignorou o Range: veio o arquivo todo do 0
        c->buf_start = 0;
        if (c->size < 0) c->size = (int64_t)c->buf_len;
    }
    return 0;
}

static int cio_read(void *opaque, uint8_t *out, int want) {
    CurlIO *c = (CurlIO *)opaque;
    if (want <= 0) return 0;
    if (c->size >= 0 && c->pos >= c->size) return AVERROR_EOF;
    // fora do chunk carregado? busca um novo a partir de pos.
    if (c->buf_len == 0 || c->pos < c->buf_start || c->pos >= c->buf_start + (int64_t)c->buf_len) {
        if (fetch_chunk(c, c->pos) < 0) return AVERROR(EIO);
        if (c->buf_len == 0) return AVERROR_EOF;
        if (c->pos < c->buf_start || c->pos >= c->buf_start + (int64_t)c->buf_len) return AVERROR_EOF;
    }
    size_t off = (size_t)(c->pos - c->buf_start);
    size_t avail = c->buf_len - off;
    size_t n = avail < (size_t)want ? avail : (size_t)want;
    memcpy(out, c->buf + off, n);
    c->pos += n;
    return (int)n;
}

static int64_t cio_seek(void *opaque, int64_t off, int whence) {
    CurlIO *c = (CurlIO *)opaque;
    if (whence == AVSEEK_SIZE) {
        if (c->size < 0) fetch_chunk(c, 0);      // aprende o tamanho pelo header
        return c->size >= 0 ? c->size : (int64_t)AVERROR(ENOSYS);
    }
    int64_t np;
    if (whence == SEEK_SET) np = off;
    else if (whence == SEEK_CUR) np = c->pos + off;
    else if (whence == SEEK_END) {
        if (c->size < 0) fetch_chunk(c, 0);
        if (c->size < 0) return AVERROR(ENOSYS);
        np = c->size + off;
    } else return AVERROR(EINVAL);
    if (np < 0) return AVERROR(EINVAL);
    c->pos = np;
    return np;
}

AVIOContext *nplay_curl_avio_open(const char *url) {
    if (!url || !url[0]) return NULL;
    CurlIO *c = (CurlIO *)calloc(1, sizeof(CurlIO));
    if (!c) return NULL;
    snprintf(c->url, sizeof(c->url), "%s", url);
    c->size = -1; c->pos = 0; c->buf_start = -1; c->buf_len = 0;
    c->easy = curl_easy_init();
    if (!c->easy) { free(c); return NULL; }
    // opcoes fixas (o Range muda a cada fetch_chunk)
    curl_easy_setopt(c->easy, CURLOPT_URL, c->url);
    curl_easy_setopt(c->easy, CURLOPT_USERAGENT, "Nplay-Switch/1.0");
    curl_easy_setopt(c->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c->easy, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(c->easy, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(c->easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c->easy, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c->easy, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(c->easy, CURLOPT_WRITEFUNCTION, wr_cb);
    curl_easy_setopt(c->easy, CURLOPT_WRITEDATA, c);
    curl_easy_setopt(c->easy, CURLOPT_HEADERFUNCTION, hdr_cb);
    curl_easy_setopt(c->easy, CURLOPT_HEADERDATA, c);

    const int avio_bufsz = 65536;
    unsigned char *avio_buf = (unsigned char *)av_malloc(avio_bufsz);
    if (!avio_buf) { curl_easy_cleanup(c->easy); free(c); return NULL; }
    AVIOContext *ctx = avio_alloc_context(avio_buf, avio_bufsz, 0, c, cio_read, NULL, cio_seek);
    if (!ctx) { av_free(avio_buf); curl_easy_cleanup(c->easy); free(c); return NULL; }
    return ctx;
}

void nplay_curl_avio_close(AVIOContext *ctx) {
    if (!ctx) return;
    CurlIO *c = (CurlIO *)ctx->opaque;
    if (c) {
        if (c->easy) curl_easy_cleanup(c->easy);
        free(c->buf);
        free(c);
    }
    av_freep(&ctx->buffer);
    avio_context_free(&ctx);
}
