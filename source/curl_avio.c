// curl_avio.c - I/O do ffmpeg via libcurl (https com seek por Range) COM
// PREFETCH em thread de fundo, pra reproducao fluida (sem travar o decode).
//
// Motivo: o switch-ffmpeg nao tem TLS; o libcurl tem (mbedtls). Antes a leitura
// era bloqueante (cada bloco travava o player esperando a rede -> engasgos nos
// animes). Agora uma THREAD produtora baixa blocos a frente e enche um ring
// buffer; o ffmpeg (consumidor) le do buffer sem esperar a rede. Seek reposiciona
// a thread. Usa a interface EASY do curl (a MULTI falhava no Switch).
#include "curl_avio.h"
#include "net.h"
#include <curl/curl.h>
#include <SDL.h>
#include <libavutil/mem.h>
#include <libavutil/error.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

#define FILE_BLOCK   (512 * 1024)
#define FILE_RINGCAP (16 * 1024 * 1024)
#define HLS_BLOCK    (256 * 1024)
#define HLS_RINGCAP  (2 * 1024 * 1024)

typedef struct {
    CURL *easy;
    char  url[2048];
    unsigned char *ring;
    size_t ring_cap;
    size_t block_size, tmp_cap;
    size_t head, count;                // head = 1o byte disponivel; count = bytes no ring
    volatile int64_t base;             // offset (arquivo) de ring[head] = pos do consumidor
    volatile int64_t size;             // total (-1 desconhecido)
    volatile int64_t seek_req;         // pedido de seek (-1 = nenhum)
    volatile int running, eof, err;
    int delivered, fetch_complete;
    int64_t response_length, range_total;
    SDL_mutex *mtx;
    SDL_cond  *c_data, *c_space;
    SDL_Thread *th;
    unsigned char *tmp; size_t tmp_len;
} CurlIO;

static size_t wr_tmp(char *ptr, size_t sz, size_t nm, void *ud) {
    CurlIO *c = (CurlIO *)ud; size_t n = sz * nm;
    if (c->tmp_len + n > c->tmp_cap) return 0;
    memcpy(c->tmp + c->tmp_len, ptr, n); c->tmp_len += n; return n;
}
static size_t hdr_size(char *ptr, size_t sz, size_t nm, void *ud) {
    CurlIO *c = (CurlIO *)ud; size_t n = sz * nm;
    // Redirecionamentos entregam mais de um bloco de cabecalhos. Ao encontrar
    // uma nova linha de status, descarte os comprimentos da resposta anterior.
    if (n > 5 && !strncasecmp(ptr, "HTTP/", 5)) {
        c->response_length = -1;
        c->range_total = -1;
    } else if (n > 14 && !strncasecmp(ptr, "Content-Range:", 14)) {
        for (size_t i = 0; i + 1 < n; i++) if (ptr[i] == '/') {
            long long total = atoll(ptr + i + 1);
            if (total > 0) c->range_total = total;
            break;
        }
    } else if (n > 15 && !strncasecmp(ptr, "Content-Length:", 15)) {
        long long length = atoll(ptr + 15);
        if (length >= 0) c->response_length = length;
    }
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
    c->fetch_complete = 0;
    c->response_length = -1;
    c->range_total = -1;
    char range[64];
    snprintf(range, sizeof(range), "%lld-%lld", (long long)start,
             (long long)(start + (int64_t)c->block_size - 1));
    curl_easy_setopt(c->easy, CURLOPT_RANGE, range);
    CURLcode r = curl_easy_perform(c->easy);
    long code = 0; curl_easy_getinfo(c->easy, CURLINFO_RESPONSE_CODE, &code);
    if (c->range_total > 0) c->size = c->range_total;
    else if (code == 200 && start == 0 && c->response_length > 0) c->size = c->response_length;
    // Em uma queda depois de receber parte do range, preserve esses bytes e
    // retome exatamente do offset seguinte. Antes todo o trecho parcial era
    // descartado, causando o ciclo "carrega/toca/trava" em fontes lentas.
    // 4xx permanentes nao melhoram com repeticao. Timeout/rate limit continuam
    // transitorios; 5xx e falhas do curl tambem podem se recuperar.
    // Range exatamente depois do ultimo byte e EOF normal, nao uma fonte morta.
    if (code == 416 && c->size >= 0 && start >= c->size) {
        c->fetch_complete = 1;
        return 0;
    }
    if (code >= 400 && code < 500 && code != 408 && code != 429) return -2;
    if (code != 200 && code != 206) return -1;
    if (start > 0 && code != 206) return -1; // servidor ignorou Range: seek inseguro
    if (r != CURLE_OK && c->tmp_len == 0) return -1;
    c->fetch_complete = (r == CURLE_OK);
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
    Uint32 fail_since = 0;
    SDL_LockMutex(c->mtx); prod = c->base; SDL_UnlockMutex(c->mtx);
    while (1) {
        SDL_LockMutex(c->mtx);
        if (!c->running) { SDL_UnlockMutex(c->mtx); break; }
        if (c->seek_req >= 0) { prod = c->seek_req; c->base = c->seek_req; c->head = c->count = 0; c->seek_req = -1; c->eof = 0; }
        if (c->size >= 0 && prod >= c->size) { c->eof = 1; SDL_CondSignal(c->c_data); SDL_CondWaitTimeout(c->c_space, c->mtx, 200); SDL_UnlockMutex(c->mtx); continue; }
        if (c->count + c->block_size > c->ring_cap) { SDL_CondWaitTimeout(c->c_space, c->mtx, 200); SDL_UnlockMutex(c->mtx); continue; }
        int64_t start = prod;
        SDL_UnlockMutex(c->mtx);

        int got = fetch_block(c, start);            // rede, fora do lock

        SDL_LockMutex(c->mtx);
        if (!c->running) { SDL_UnlockMutex(c->mtx); break; }
        if (c->seek_req >= 0) { SDL_UnlockMutex(c->mtx); continue; }   // seek durante o fetch -> descarta
        if (got < 0) {                                                 // falha de rede: tenta de novo (bufferiza)
            Uint32 now = SDL_GetTicks();
            if (!fail_since) fail_since = now;
            // HTTP definitivo encerra logo; falha transitoria recebe ate dois
            // minutos para reconectar enquanto a UI continua responsiva ao B.
            if (got == -2 || now - fail_since >= 120000) {
                c->err = 1;
                SDL_CondSignal(c->c_data);
            }
            SDL_UnlockMutex(c->mtx);
            SDL_Delay(300);
            continue;
        }
        c->err = 0;
        // Sucesso sem corpo so e fim quando o tamanho confirma a posicao. Tratar
        // qualquer 200 vazio como EOF fazia o anime morrer durante a abertura.
        if (got == 0) {
            if (c->size >= 0 && prod >= c->size) {
                c->eof = 1;
                SDL_CondSignal(c->c_data);
            } else {
                Uint32 now = SDL_GetTicks();
                if (!fail_since) fail_since = now;
                else if (now - fail_since >= 120000) {
                    c->err = 1;
                    SDL_CondSignal(c->c_data);
                }
            }
            SDL_UnlockMutex(c->mtx);
            SDL_Delay(100);
            continue;
        }
        fail_since = 0;
        ring_put(c, c->tmp, (size_t)got);
        prod += got;
        if (c->fetch_complete && got > 0 && (size_t)got < c->block_size && c->size < 0) c->size = prod;
        SDL_CondSignal(c->c_data);
        SDL_UnlockMutex(c->mtx);
    }
    return 0;
}

static int cio_read(void *opaque, uint8_t *out, int want) {
    CurlIO *c = (CurlIO *)opaque;
    if (want <= 0) return 0;
    SDL_LockMutex(c->mtx);
    // Na abertura, FFmpeg ainda nao sabe lidar bem com EAGAIN: aguarda o
    // primeiro byte. Depois disso, devolve o controle a cada 300 ms para a UI
    // poder desenhar o estado CARREGANDO e continuar recebendo comandos.
    int waits = c->delivered ? 1 : 67; // ate ~20 s apenas no primeiro acesso
    while (c->count == 0 && !c->eof && !c->err && c->running && waits-- > 0)
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
    c->delivered = 1;
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

static AVIOContext *curl_avio_open_profile(const char *url, int64_t expected_size,
                                           size_t block_size, size_t ring_cap) {
    if (!url || !url[0]) return NULL;
    CurlIO *c = (CurlIO *)calloc(1, sizeof(CurlIO));
    if (!c) return NULL;
    snprintf(c->url, sizeof(c->url), "%s", url);
    c->size = expected_size > 0 ? expected_size : -1;
    c->response_length = c->range_total = -1;
    c->seek_req = -1; c->base = 0; c->running = 1;
    c->block_size = block_size;
    c->ring_cap = ring_cap;
    c->tmp_cap = block_size + 65536;
    c->ring = (unsigned char *)malloc(c->ring_cap);
    c->tmp  = (unsigned char *)malloc(c->tmp_cap);
    c->easy = curl_easy_init();
    c->mtx = SDL_CreateMutex();
    c->c_data = SDL_CreateCond();
    c->c_space = SDL_CreateCond();
    if (!c->ring || !c->tmp || !c->easy || !c->mtx || !c->c_data || !c->c_space) { free_cio(c); return NULL; }
    curl_easy_setopt(c->easy, CURLOPT_URL, c->url);
    curl_easy_setopt(c->easy, CURLOPT_USERAGENT, "Nplay-Switch/1.0");
    curl_easy_setopt(c->easy, CURLOPT_FOLLOWLOCATION, 1L);
    // Byte ranges de MP4 precisam se referir aos bytes originais. Nao permita
    // gzip/brotli nem decodificacao transparente mudar offsets e tamanho.
    curl_easy_setopt(c->easy, CURLOPT_ACCEPT_ENCODING, "identity");
    curl_easy_setopt(c->easy, CURLOPT_HTTP_CONTENT_DECODING, 0L);
    // O backend TLS do libcurl usa a PKI interna do sistema do Switch.
    // MP4 direto carrega URLs assinadas e deve validar CA e hostname como a API.
    curl_easy_setopt(c->easy, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c->easy, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c->easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c->easy, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(c->easy, CURLOPT_TCP_KEEPIDLE, 120L);
    curl_easy_setopt(c->easy, CURLOPT_TCP_KEEPINTVL, 60L);
    curl_easy_setopt(c->easy, CURLOPT_CONNECTTIMEOUT, 20L);
    // Nao use timeout total: um servidor valido pode levar mais de 60 s para
    // um episodio longo. Detecte apenas conexao realmente parada.
    curl_easy_setopt(c->easy, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(c->easy, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(c->easy, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c->easy, CURLOPT_WRITEFUNCTION, wr_tmp);
    curl_easy_setopt(c->easy, CURLOPT_WRITEDATA, c);
    curl_easy_setopt(c->easy, CURLOPT_HEADERFUNCTION, hdr_size);
    curl_easy_setopt(c->easy, CURLOPT_HEADERDATA, c);
    curl_easy_setopt(c->easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c->easy, CURLOPT_XFERINFOFUNCTION, xfer_cb);
    curl_easy_setopt(c->easy, CURLOPT_XFERINFODATA, c);
    // Cada rendition HLS tem uma thread/handle de longa duracao. Nao coloque
    // essas conexoes no CURLSH 7.69 usado por requests curtos da UI; compartilhar
    // o cache de conexoes entre produtores simultaneos causou crash no hardware.
    net_configure_curl_isolated(c->easy);

    unsigned char *avio_buf = (unsigned char *)av_malloc(65536);
    if (!avio_buf) { free_cio(c); return NULL; }
    c->th = SDL_CreateThread(producer, "cavio", c);
    if (!c->th) { av_free(avio_buf); free_cio(c); return NULL; }
    AVIOContext *ctx = avio_alloc_context(avio_buf, 65536, 0, c, cio_read, NULL, cio_seek);
    if (!ctx) { av_free(avio_buf); free_cio(c); return NULL; }
    return ctx;
}

AVIOContext *nplay_curl_avio_open(const char *url, int64_t expected_size) {
    return curl_avio_open_profile(url, expected_size, FILE_BLOCK, FILE_RINGCAP);
}

AVIOContext *nplay_curl_avio_open_hls(const char *url) {
    return curl_avio_open_profile(url, -1, HLS_BLOCK, HLS_RINGCAP);
}

void nplay_curl_avio_close(AVIOContext *ctx) {
    if (!ctx) return;
    free_cio((CurlIO *)ctx->opaque);
    av_freep(&ctx->buffer);
    avio_context_free(&ctx);
}
