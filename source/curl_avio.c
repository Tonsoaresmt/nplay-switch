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
#include "diag.h"
#include "hls_manifest.h"
#include "subtitle_limits.h"
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
#define HLS_META_INITIAL (64 * 1024)
// Teto por recurso textual (playlist, VTT). Legendas de anime com karaoke
// convertido de ASS passam de 1 MB e podem chegar perto de 4 MB; acima do teto
// o download falhava e a legenda nunca aparecia. A memoria so cresce sob demanda.
#define HLS_META_MAX     SUBTITLE_DOWNLOAD_MAX
#define HLS_MEDIA_RINGCAP (4 * 1024 * 1024)

static SDL_atomic_t g_active_contexts = {0};
static SDL_atomic_t g_reserved_kb = {0};
static SDL_atomic_t g_resource_sequence = {0};
static SDL_atomic_t g_startup_deadline = {0};
static SDL_atomic_t g_media_requests = {0};
static SDL_atomic_t g_media_slow_first = {0};
static SDL_atomic_t g_media_worst_first = {0};
static SDL_atomic_t g_media_new_connections = {0};
static SDL_atomic_t g_media_failures = {0};
static SDL_atomic_t g_media_first_reads = {0};
static SDL_atomic_t g_media_ready_first = {0};
static SDL_atomic_t g_media_young_first = {0};
static SDL_atomic_t g_media_old_empty_first = {0};
static int (*g_abort_check)(void *) = NULL;
static void *g_abort_userdata = NULL;

void nplay_curl_avio_set_abort_check(int (*check)(void *), void *userdata) {
    g_abort_check = check;
    g_abort_userdata = userdata;
}

static int abort_requested(void) {
    return g_abort_check && g_abort_check(g_abort_userdata);
}

void nplay_curl_avio_quality_reset(void) {
    SDL_AtomicSet(&g_media_requests, 0);
    SDL_AtomicSet(&g_media_slow_first, 0);
    SDL_AtomicSet(&g_media_worst_first, 0);
    SDL_AtomicSet(&g_media_new_connections, 0);
    SDL_AtomicSet(&g_media_failures, 0);
    SDL_AtomicSet(&g_media_first_reads, 0);
    SDL_AtomicSet(&g_media_ready_first, 0);
    SDL_AtomicSet(&g_media_young_first, 0);
    SDL_AtomicSet(&g_media_old_empty_first, 0);
}

void nplay_curl_avio_quality_get(NplayCurlAvioQuality *out) {
    if (!out) return;
    out->requests = SDL_AtomicGet(&g_media_requests);
    out->slow_first_bytes = SDL_AtomicGet(&g_media_slow_first);
    out->worst_first_ms = SDL_AtomicGet(&g_media_worst_first);
    out->new_connections = SDL_AtomicGet(&g_media_new_connections);
    out->failures = SDL_AtomicGet(&g_media_failures);
    out->first_reads = SDL_AtomicGet(&g_media_first_reads);
    out->ready_first_reads = SDL_AtomicGet(&g_media_ready_first);
    out->young_first_reads = SDL_AtomicGet(&g_media_young_first);
    out->old_empty_first_reads = SDL_AtomicGet(&g_media_old_empty_first);
}

static void atomic_raise_max(SDL_atomic_t *target, int value) {
    int old = SDL_AtomicGet(target);
    while (value > old && !SDL_AtomicCAS(target, old, value))
        old = SDL_AtomicGet(target);
}

void nplay_curl_avio_set_startup_window(unsigned timeout_ms) {
    SDL_AtomicSet(&g_startup_deadline,
                  timeout_ms ? (int)(SDL_GetTicks() + timeout_ms) : 0);
}

static int startup_deadline_expired(void) {
    int deadline = SDL_AtomicGet(&g_startup_deadline);
    return deadline != 0 && (Sint32)(SDL_GetTicks() - (Uint32)deadline) >= 0;
}

// O demuxer HLS fecha um AVIO ao terminar cada segmento e abre outro para o
// seguinte. Manter o easy handle ocioso preserva a conexao TCP/TLS sem usar
// CURLSH entre as threads de audio e video (inseguro no libcurl do Switch).
#define HLS_IDLE_HANDLES 4
static CURL *g_hls_idle[HLS_IDLE_HANDLES];
static int g_hls_idle_count = 0;
static SDL_SpinLock g_hls_idle_lock = 0;

static CURL *hls_easy_take(void) {
    CURL *easy = NULL;
    SDL_AtomicLock(&g_hls_idle_lock);
    if (g_hls_idle_count > 0) easy = g_hls_idle[--g_hls_idle_count];
    SDL_AtomicUnlock(&g_hls_idle_lock);
    if (easy) curl_easy_reset(easy); // conserva conexoes e cache TLS/DNS
    return easy ? easy : curl_easy_init();
}

static void hls_easy_return(CURL *easy) {
    if (!easy) return;
    int kept = 0;
    SDL_AtomicLock(&g_hls_idle_lock);
    if (g_hls_idle_count < HLS_IDLE_HANDLES) {
        g_hls_idle[g_hls_idle_count++] = easy;
        kept = 1;
    }
    SDL_AtomicUnlock(&g_hls_idle_lock);
    if (!kept) curl_easy_cleanup(easy);
}

// ---- Abertura posicionada e cache de metadados HLS ----------------------
// O seek interno do FFmpeg 7.1 corrompe o demuxer mov das renditions fMP4.
// Seek, retomada e troca de audio reabrem a fonte com cada playlist de midia
// comecando no segmento do ponto pedido (hls_media_playlist_trim). Para que a
// reabertura nao pague de novo master/playlists na rede, os textos originais
// ficam num cache curto, limitado e descartado ao sair do player.
#define HLS_META_CACHE_SLOTS 12
#define HLS_META_CACHE_BYTES (2 * 1024 * 1024)
#define HLS_META_CACHE_TTL_MS (10u * 60u * 1000u)
typedef struct {
    char *url;
    char effective_url[2048];
    unsigned char *data;
    size_t len;
    Uint32 stored;
} HlsMetaCacheEntry;
static HlsMetaCacheEntry g_meta_cache[HLS_META_CACHE_SLOTS];
static size_t g_meta_cache_bytes = 0;
static SDL_SpinLock g_meta_cache_lock = 0;
static double g_hls_start = 0;
static double g_hls_full_duration = 0;
static int g_hls_positioned_count = 0;
static SDL_SpinLock g_hls_start_lock = 0;

void nplay_curl_avio_set_hls_start(double seconds) {
    SDL_AtomicLock(&g_hls_start_lock);
    g_hls_start = seconds > 0 ? seconds : 0;
    if (g_hls_start > 0) { g_hls_full_duration = 0; g_hls_positioned_count = 0; }
    SDL_AtomicUnlock(&g_hls_start_lock);
}

int nplay_curl_avio_hls_positioned_count(void) {
    SDL_AtomicLock(&g_hls_start_lock);
    int value = g_hls_positioned_count;
    SDL_AtomicUnlock(&g_hls_start_lock);
    return value;
}

double nplay_curl_avio_hls_full_duration(void) {
    SDL_AtomicLock(&g_hls_start_lock);
    double value = g_hls_full_duration;
    SDL_AtomicUnlock(&g_hls_start_lock);
    return value;
}

static void meta_cache_drop(HlsMetaCacheEntry *entry) {
    g_meta_cache_bytes -= entry->len;
    free(entry->url);
    free(entry->data);
    memset(entry, 0, sizeof(*entry));
}

static void meta_cache_clear(void) {
    SDL_AtomicLock(&g_meta_cache_lock);
    for (int i = 0; i < HLS_META_CACHE_SLOTS; i++)
        if (g_meta_cache[i].url) meta_cache_drop(&g_meta_cache[i]);
    g_meta_cache_bytes = 0;
    SDL_AtomicUnlock(&g_meta_cache_lock);
}

// Copia o corpo em cache para um buffer novo do chamador (malloc).
static int meta_cache_get(const char *url, unsigned char **data, size_t *len,
                          char *effective_url, size_t effective_url_size) {
    int found = 0;
    Uint32 now = SDL_GetTicks();
    SDL_AtomicLock(&g_meta_cache_lock);
    for (int i = 0; i < HLS_META_CACHE_SLOTS && !found; i++) {
        HlsMetaCacheEntry *entry = &g_meta_cache[i];
        if (!entry->url || strcmp(entry->url, url)) continue;
        if (now - entry->stored > HLS_META_CACHE_TTL_MS) { meta_cache_drop(entry); break; }
        unsigned char *copy = (unsigned char *)malloc(entry->len + 1);
        if (copy) {
            memcpy(copy, entry->data, entry->len);
            copy[entry->len] = 0;
            *data = copy;
            *len = entry->len;
            snprintf(effective_url, effective_url_size, "%s", entry->effective_url);
            found = 1;
        }
    }
    SDL_AtomicUnlock(&g_meta_cache_lock);
    return found;
}

static void meta_cache_put(const char *url, const char *effective_url,
                           const unsigned char *data, size_t len) {
    if (!url || !data || len == 0 || len > HLS_META_CACHE_BYTES / 2) return;
    char *url_copy = strdup(url);
    unsigned char *copy = (unsigned char *)malloc(len);
    if (!url_copy || !copy) { free(url_copy); free(copy); return; }
    memcpy(copy, data, len);
    SDL_AtomicLock(&g_meta_cache_lock);
    int slot = -1;
    for (int i = 0; i < HLS_META_CACHE_SLOTS; i++)
        if (g_meta_cache[i].url && !strcmp(g_meta_cache[i].url, url)) meta_cache_drop(&g_meta_cache[i]);
    // Libera os mais antigos ate caber; no pior caso o cache fica vazio.
    while (1) {
        int empty = -1, oldest = -1;
        for (int i = 0; i < HLS_META_CACHE_SLOTS; i++) {
            if (!g_meta_cache[i].url) { if (empty < 0) empty = i; continue; }
            if (oldest < 0 || g_meta_cache[i].stored < g_meta_cache[oldest].stored) oldest = i;
        }
        if (empty >= 0 && g_meta_cache_bytes + len <= HLS_META_CACHE_BYTES) { slot = empty; break; }
        if (oldest < 0) break;
        meta_cache_drop(&g_meta_cache[oldest]);
    }
    if (slot >= 0) {
        HlsMetaCacheEntry *entry = &g_meta_cache[slot];
        entry->url = url_copy; url_copy = NULL;
        entry->data = copy; copy = NULL;
        entry->len = len;
        entry->stored = SDL_GetTicks();
        snprintf(entry->effective_url, sizeof(entry->effective_url), "%s",
                 effective_url && effective_url[0] ? effective_url : url);
        g_meta_cache_bytes += len;
    }
    SDL_AtomicUnlock(&g_meta_cache_lock);
    free(url_copy);
    free(copy);
}

void nplay_curl_avio_pool_clear(void) {
    meta_cache_clear();
    CURL *idle[HLS_IDLE_HANDLES];
    int count;
    SDL_AtomicLock(&g_hls_idle_lock);
    count = g_hls_idle_count;
    memcpy(idle, g_hls_idle, (size_t)count * sizeof(CURL *));
    g_hls_idle_count = 0;
    SDL_AtomicUnlock(&g_hls_idle_lock);
    for (int i = 0; i < count; i++) curl_easy_cleanup(idle[i]);
}

typedef struct {
    CURL *easy;
    char *url;
    unsigned char *ring;
    size_t ring_cap;
    size_t block_size, tmp_cap, tmp_limit;
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
    int write_overflow;
    int accounted, reserved_kb;
    int static_data, synchronous, streaming, pooled;
    size_t static_pos, static_len;
    int resource_id, first_http_logged;
    Uint32 first_read_tick;
    Uint32 opened_tick;
    int first_read_started;
    int startup_timeout_logged;
    unsigned fetch_count, slow_fetch_count, empty_waits;
    unsigned worst_fetch_ms, worst_first_byte_ms, last_first_byte_ms, stream_started_tick;
    Uint32 last_body_tick; // producer only; excludes time blocked by our full ring
    unsigned long long downloaded_bytes;
    int64_t produced_offset, request_start;
    size_t stream_len;
    long response_code;
    char effective_url[2048];
    char profile[8];
    SDL_atomic_t *cancel_flag; // cancelamento da thread que abriu (busca/legenda)
} CurlIO;

// Threads auxiliares (busca paralela, legenda em segundo plano) nao podem
// usar o callback de B da thread de render. Cada uma registra sua propria
// flag; todo recurso aberto nela herda a flag e a transferencia e abortada.
static _Thread_local SDL_atomic_t *t_cancel_flag = NULL;
void nplay_curl_avio_set_thread_cancel(SDL_atomic_t *cancel) { t_cancel_flag = cancel; }

static size_t wr_tmp(char *ptr, size_t sz, size_t nm, void *ud) {
    CurlIO *c = (CurlIO *)ud;
    if (sz != 0 && nm > SIZE_MAX / sz) { c->write_overflow = 1; return 0; }
    size_t n = sz * nm;
    if (n > c->tmp_cap - c->tmp_len) {
        if (!c->synchronous || n > c->tmp_limit - c->tmp_len) {
            c->write_overflow = 1;
            return 0;
        }
        size_t next = c->tmp_cap;
        while (n > next - c->tmp_len) {
            if (next >= c->tmp_limit / 2) { next = c->tmp_limit; break; }
            next *= 2;
        }
        unsigned char *grown = (unsigned char *)realloc(c->tmp, next);
        if (!grown) { c->write_overflow = 1; return 0; }
        c->tmp = grown;
        if (c->accounted) {
            int extra_kb = (int)((next - c->tmp_cap) / 1024);
            c->reserved_kb += extra_kb;
            SDL_AtomicAdd(&g_reserved_kb, extra_kb);
        }
        c->tmp_cap = next;
    }
    memcpy(c->tmp + c->tmp_len, ptr, n); c->tmp_len += n; return n;
}
static size_t hdr_size(char *ptr, size_t sz, size_t nm, void *ud) {
    CurlIO *c = (CurlIO *)ud;
    if (sz != 0 && nm > SIZE_MAX / sz) return 0;
    size_t n = sz * nm;
    char line[160];
    size_t copy = n < sizeof(line) - 1 ? n : sizeof(line) - 1;
    memcpy(line, ptr, copy);
    line[copy] = '\0';
    // Redirecionamentos entregam mais de um bloco de cabecalhos. Ao encontrar
    // uma nova linha de status, descarte os comprimentos da resposta anterior.
    if (copy > 5 && !strncasecmp(line, "HTTP/", 5)) {
        c->response_length = -1;
        c->range_total = -1;
        const char *space = strchr(line, ' ');
        c->response_code = space ? atol(space + 1) : 0;
    } else if (copy > 14 && !strncasecmp(line, "Content-Range:", 14)) {
        for (size_t i = 0; i + 1 < copy; i++) if (line[i] == '/') {
            long long total = atoll(line + i + 1);
            if (total > 0) {
                c->range_total = total;
                if (c->streaming) {
                    SDL_LockMutex(c->mtx);
                    c->size = total;
                    SDL_CondSignal(c->c_data);
                    SDL_UnlockMutex(c->mtx);
                }
            }
            break;
        }
    } else if (copy > 15 && !strncasecmp(line, "Content-Length:", 15)) {
        long long length = atoll(line + 15);
        if (length >= 0) {
            c->response_length = length;
            if (c->streaming && c->response_code == 200 && c->request_start == 0) {
                SDL_LockMutex(c->mtx);
                c->size = length;
                SDL_CondSignal(c->c_data);
                SDL_UnlockMutex(c->mtx);
            }
        }
    }
    return n;
}
// aborta o transfer em andamento quando fecha ou pede seek (deixa o close/seek rapidos)
static int xfer_cb(void *ud, curl_off_t a, curl_off_t b, curl_off_t d, curl_off_t e) {
    (void)a; (void)d; (void)e;
    CurlIO *c = (CurlIO *)ud;
    if (c->cancel_flag && SDL_AtomicGet(c->cancel_flag)) return 1;
    if (!c->running || c->seek_req >= 0 || startup_deadline_expired() ||
        (c->synchronous && abort_requested())) return 1;
    // Backpressure also exists BETWEEN writer callbacks. A previous callback
    // can fill the ring without blocking; libcurl still calls this callback
    // while the consumer is paused. Do not count that local wait as body idle.
    if (c->streaming && c->stream_len > 0) {
        SDL_LockMutex(c->mtx);
        int full = c->count == c->ring_cap;
        SDL_UnlockMutex(c->mtx);
        if (full) {
            c->last_body_tick = SDL_GetTicks();
            return 0;
        }
    }
    // Uma conexao persistente parada nao deve consumir os 30 s do low-speed
    // antes de tentar outro socket. Este limite vale somente ANTES do corpo;
    // ring cheio/backpressure e transferencia em andamento nao sao abortados.
    if (c->streaming && b == 0 &&
        SDL_GetTicks() - c->stream_started_tick >= 8000u) return 1;
    if (c->streaming && c->stream_len > 0 &&
        SDL_GetTicks() - c->last_body_tick >= 8000u) return 1;
    return 0;
}

static int fetch_block(CurlIO *c, int64_t start) {
    c->tmp_len = 0;
    c->write_overflow = 0;
    c->fetch_complete = 0;
    c->response_length = -1;
    c->range_total = -1;
    char range[64];
    if (c->synchronous) {
        // Manifestos reescritos pelo Worker nao possuem offsets estaveis.
        // Um Range do objeto original devolve uma playlist incompleta.
        curl_easy_setopt(c->easy, CURLOPT_RANGE, NULL);
    } else {
        snprintf(range, sizeof(range), "%lld-%lld", (long long)start,
                 (long long)(start + (int64_t)c->block_size - 1));
        curl_easy_setopt(c->easy, CURLOPT_RANGE, range);
    }
    Uint32 fetch_started = SDL_GetTicks();
    CURLcode r = curl_easy_perform(c->easy);
    Uint32 fetch_ms = SDL_GetTicks() - fetch_started;
    long code = 0; curl_easy_getinfo(c->easy, CURLINFO_RESPONSE_CODE, &code);
    char *effective = NULL;
    if (curl_easy_getinfo(c->easy, CURLINFO_EFFECTIVE_URL, &effective) == CURLE_OK &&
        effective && effective[0])
        snprintf(c->effective_url, sizeof(c->effective_url), "%s", effective);
    long new_connections = 0;
    curl_easy_getinfo(c->easy, CURLINFO_NUM_CONNECTS, &new_connections);
    c->fetch_count++;
    c->downloaded_bytes += c->tmp_len;
    if (fetch_ms > c->worst_fetch_ms) c->worst_fetch_ms = fetch_ms;
    if (fetch_ms >= 1000) c->slow_fetch_count++;
    if (!c->first_http_logged || r != CURLE_OK || code < 200 || code >= 400 || c->write_overflow) {
        diag_player_event("avio", "http",
                          "id=%d %s code=%ld curl=%d conn=%ld ms=%u off=%lld got=%u ov=%d",
                          c->resource_id, c->profile, code, (int)r,
                          new_connections, fetch_ms, (long long)start,
                          (unsigned)c->tmp_len, c->write_overflow);
        c->first_http_logged = 1;
    }
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
    // Nunca entregue um bloco truncado ao demuxer. Alguns hosts ignoram Range
    // e devolvem o segmento inteiro; a versao anterior aceitava o prefixo ate
    // encher tmp e podia alimentar o FFmpeg com fMP4 corrompido.
    if (c->write_overflow) return -2;
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
    int trace_close = !c->streaming || c->empty_waits >= 4 ||
                      c->worst_first_byte_ms >= 1000 ||
                      c->fetch_count > 1;
    if (c->accounted && trace_close)
        diag_player_event("avio", "close",
                          "id=%d %s req=%u first=%ums empty=%u bytes=%llu mem=%dKB",
                          c->resource_id, c->profile, c->fetch_count,
                          c->worst_first_byte_ms, c->empty_waits,
                          c->downloaded_bytes, SDL_AtomicGet(&g_reserved_kb));
    if (c->pooled) hls_easy_return(c->easy);
    else if (c->easy) curl_easy_cleanup(c->easy);
    if (c->mtx) SDL_DestroyMutex(c->mtx);
    if (c->c_data) SDL_DestroyCond(c->c_data);
    if (c->c_space) SDL_DestroyCond(c->c_space);
    if (c->accounted) {
        SDL_AtomicAdd(&g_active_contexts, -1);
        SDL_AtomicAdd(&g_reserved_kb, -c->reserved_kb);
    }
    free(c->url); free(c->ring); free(c->tmp); free(c);
}
static void ring_put(CurlIO *c, const unsigned char *src, size_t n) {
    size_t tail = (c->head + c->count) % c->ring_cap;
    size_t first = c->ring_cap - tail; if (first > n) first = n;
    memcpy(c->ring + tail, src, first);
    if (n > first) memcpy(c->ring, src + first, n - first);
    c->count += n;
}
// Segmentos HLS chegam ao buffer conforme a rede entrega bytes. Nao espere
// completar cada bloco antes de acordar o demuxer, nem abra varias conexoes
// Range para o mesmo segmento. O ring aplica backpressure ao libcurl.
static size_t wr_ring(char *ptr, size_t sz, size_t nm, void *ud) {
    CurlIO *c = (CurlIO *)ud;
    if (sz != 0 && nm > SIZE_MAX / sz) return 0;
    size_t n = sz * nm;
    if (c->response_code >= 300 && c->response_code < 400) return n;
    if (c->request_start > 0 && c->response_code != 206) return 0;
    if (c->response_code < 200 || c->response_code >= 300) return n;
    if (n && !c->stream_len) {
        unsigned first_ms = SDL_GetTicks() - c->stream_started_tick;
        c->last_first_byte_ms = first_ms;
        if (first_ms > c->worst_first_byte_ms) c->worst_first_byte_ms = first_ms;
        if (first_ms >= 1000) c->slow_fetch_count++;
    }
    size_t off = 0;
    while (off < n) {
        SDL_LockMutex(c->mtx);
        while (c->running && c->seek_req < 0 && c->count == c->ring_cap)
            SDL_CondWaitTimeout(c->c_space, c->mtx, 200);
        if (!c->running || c->seek_req >= 0) {
            SDL_UnlockMutex(c->mtx);
            return 0;
        }
        size_t take = c->ring_cap - c->count;
        if (take > n - off) take = n - off;
        ring_put(c, (const unsigned char *)ptr + off, take);
        c->produced_offset += take;
        c->stream_len += take;
        off += take;
        c->last_body_tick = SDL_GetTicks();
        SDL_CondSignal(c->c_data);
        SDL_UnlockMutex(c->mtx);
    }
    return n;
}

static int fetch_stream(CurlIO *c, int64_t start) {
    c->fetch_complete = 0;
    c->response_length = c->range_total = -1;
    c->response_code = 0;
    c->request_start = start;
    c->stream_len = 0;
    c->last_first_byte_ms = 0;
    char range[64];
    if (start == 0) curl_easy_setopt(c->easy, CURLOPT_RANGE, NULL);
    else {
        snprintf(range, sizeof(range), "%lld-", (long long)start);
        curl_easy_setopt(c->easy, CURLOPT_RANGE, range);
    }
    Uint32 started = SDL_GetTicks();
    c->stream_started_tick = started;
    c->last_body_tick = started;
    CURLcode rc = curl_easy_perform(c->easy);
    Uint32 took = SDL_GetTicks() - started;
    long code = 0;
    curl_easy_getinfo(c->easy, CURLINFO_RESPONSE_CODE, &code);
    long new_connections = 0;
    curl_easy_getinfo(c->easy, CURLINFO_NUM_CONNECTS, &new_connections);
    c->fetch_count++;
    c->downloaded_bytes += c->stream_len;
    if (took > c->worst_fetch_ms) c->worst_fetch_ms = took;
    SDL_AtomicAdd(&g_media_requests, 1);
    if (c->last_first_byte_ms >= 250) SDL_AtomicAdd(&g_media_slow_first, 1);
    atomic_raise_max(&g_media_worst_first, (int)c->last_first_byte_ms);
    if (new_connections > 0)
        SDL_AtomicAdd(&g_media_new_connections, (int)new_connections);
    if (c->running && (rc != CURLE_OK || code < 200 || code >= 400))
        SDL_AtomicAdd(&g_media_failures, 1);
    // Sucesso normal nao deve aguardar escrita no cartao SD antes de sinalizar
    // EOF ao demuxer. Preserve apenas falhas e atrasos realmente longos.
    if ((c->running && (rc != CURLE_OK || code < 200 || code >= 400)) ||
        c->last_first_byte_ms >= 1000) {
        diag_player_event("avio", "http", "id=%d media code=%ld curl=%d conn=%ld first=%u ms=%u idle=%u bytes=%zu",
                          c->resource_id, code, (int)rc, new_connections,
                          c->last_first_byte_ms, took,
                          SDL_GetTicks() - c->last_body_tick, c->stream_len);
        c->first_http_logged = 1;
    }
    if (code == 416 && c->size >= 0 && start >= c->size) {
        c->fetch_complete = 1;
        return 0;
    }
    if (code >= 400 && code < 500 && code != 408 && code != 429) return -2;
    if (code != 200 && code != 206) return -1;
    if (start > 0 && code != 206) return -2;
    if (rc != CURLE_OK && c->stream_len == 0) return -1;
    c->fetch_complete = rc == CURLE_OK;
    return c->stream_len ? 1 : 0;
}

static int producer_stream(void *arg) {
    CurlIO *c = (CurlIO *)arg;
    int64_t prod = 0;
    Uint32 fail_since = 0;
    while (1) {
        SDL_LockMutex(c->mtx);
        if (!c->running) { SDL_UnlockMutex(c->mtx); break; }
        if (c->seek_req >= 0) {
            prod = c->seek_req;
            c->base = c->produced_offset = prod;
            c->head = c->count = 0;
            c->seek_req = -1;
            c->eof = c->err = 0;
        }
        if (c->size >= 0 && prod >= c->size) {
            c->eof = 1;
            SDL_CondSignal(c->c_data);
            SDL_CondWaitTimeout(c->c_space, c->mtx, 200);
            SDL_UnlockMutex(c->mtx);
            continue;
        }
        SDL_UnlockMutex(c->mtx);

        // A primeira tentativa reusa TLS/TCP. Falha sem corpo tenta um socket
        // novo; bytes ja recebidos continuam preservados por produced_offset.
        curl_easy_setopt(c->easy, CURLOPT_FRESH_CONNECT, fail_since ? 1L : 0L);
        int got = fetch_stream(c, prod);
        SDL_LockMutex(c->mtx);
        if (!c->running) { SDL_UnlockMutex(c->mtx); break; }
        if (c->seek_req >= 0) { SDL_UnlockMutex(c->mtx); continue; }
        prod = c->produced_offset;
        if (got < 0 || (got == 0 && (!c->fetch_complete ||
            (c->size > 0 && prod < c->size)))) {
            Uint32 now = SDL_GetTicks();
            if (!fail_since) fail_since = now;
            if (got == -2 || now - fail_since >= 120000) {
                c->err = 1;
                SDL_CondSignal(c->c_data);
            }
            SDL_UnlockMutex(c->mtx);
            SDL_Delay(300);
            continue;
        }
        fail_since = 0;
        c->err = 0;
        if (c->fetch_complete) {
            if (c->size < 0) c->size = prod;
            if (prod >= c->size) c->eof = 1;
            SDL_CondSignal(c->c_data);
        }
        SDL_UnlockMutex(c->mtx);
    }
    return 0;
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
    // Playlists e legendas sao baixadas por inteiro antes de retornar ao FFmpeg.
    // Esse caminho nao possui thread produtora e permite seeks seguros no buffer.
    if (c->static_data) {
        if (c->static_pos >= c->static_len) return AVERROR_EOF;
        size_t available = c->static_len - c->static_pos;
        size_t n = available < (size_t)want ? available : (size_t)want;
        memcpy(out, c->ring + c->static_pos, n);
        c->static_pos += n;
        c->delivered = 1;
        return (int)n;
    }
    SDL_LockMutex(c->mtx);
    if (!c->delivered && !c->first_read_started) {
        c->first_read_tick = SDL_GetTicks();
        c->first_read_started = 1;
        if (c->streaming) {
            Uint32 age_ms = c->first_read_tick - c->opened_tick;
            SDL_AtomicAdd(&g_media_first_reads, 1);
            if (c->count > 0) SDL_AtomicAdd(&g_media_ready_first, 1);
            if (age_ms < 250) SDL_AtomicAdd(&g_media_young_first, 1);
            if (age_ms >= 250 && c->count == 0)
                SDL_AtomicAdd(&g_media_old_empty_first, 1);
        }
    }
    // Segmentos sao lidos pelo worker demux, separado da UI. Nunca propagar
    // EAGAIN ao parser HLS/fMP4: fill_buffer do FFmpeg marca EOF+error e uma
    // falta temporaria no meio de um box pode deixar a leitura presa. Esperar
    // aqui conserva o parser; UI/controles continuam vivos e abort_requested
    // interrompe a espera em ate 100 ms. O supervisor limita stalls prolongados.
    int waits = c->delivered ? 3 : 200; // ate 300 ms; 20 s no primeiro acesso
    while (c->count == 0 && !c->eof && !c->err && c->running &&
           (c->streaming || waits-- > 0) &&
           !startup_deadline_expired() &&
           (c->delivered || SDL_GetTicks() - c->first_read_tick < 20000u)) {
        SDL_CondWaitTimeout(c->c_data, c->mtx, 100);
        // FFmpeg nao consulta interrupt_callback em todas as leituras AVIO.
        // A checagem corre no worker de leitura para B continuar responsivo.
        SDL_UnlockMutex(c->mtx);
        int cancelled = abort_requested();
        SDL_LockMutex(c->mtx);
        if (cancelled) {
            SDL_UnlockMutex(c->mtx);
            return AVERROR_EXIT;
        }
    }
    if (c->count == 0) {
        if (startup_deadline_expired() ||
            (!c->delivered && SDL_GetTicks() - c->first_read_tick >= 20000u)) {
            int log_timeout = !c->startup_timeout_logged;
            unsigned first_ms = c->last_first_byte_ms;
            unsigned empty_waits = c->empty_waits;
            int resource_id = c->resource_id;
            char profile[sizeof(c->profile)];
            snprintf(profile, sizeof(profile), "%s", c->profile);
            c->startup_timeout_logged = 1;
            c->err = 1;
            SDL_UnlockMutex(c->mtx);
            // Escrever o diagnostico na microSD fora do mutex de transporte:
            // o produtor precisa dele para encerrar e liberar o AVIO.
            if (log_timeout)
                diag_player_event("avio", "first-byte-timeout",
                                  "id=%d %s first=%u empty=%u",
                                  resource_id, profile, first_ms, empty_waits);
            return AVERROR(ETIMEDOUT);
        }
        if (c->running && !c->err && !c->eof) {
            c->empty_waits++;
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
    if (c->static_data) {
        if (whence == AVSEEK_SIZE) return (int64_t)c->static_len;
        int64_t next = whence == SEEK_SET ? off :
                       whence == SEEK_CUR ? (int64_t)c->static_pos + off :
                       whence == SEEK_END ? (int64_t)c->static_len + off : -1;
        if (next < 0 || next > (int64_t)c->static_len) return AVERROR(EINVAL);
        c->static_pos = (size_t)next;
        return next;
    }
    if (whence == AVSEEK_SIZE) {
        SDL_LockMutex(c->mtx);
        int64_t s = c->size;
        SDL_UnlockMutex(c->mtx);
        // AVSEEK_SIZE permite ENOSYS quando o Content-Length nao chegou.
        // Esperar ate 15 s aqui nao produz bytes e ocorre dentro da abertura
        // sincrona do HLS, podendo se repetir para cada segmento/rendition.
        // O tamanho conhecido continua disponivel imediatamente; quando a
        // resposta e chunked, o demuxer prossegue como stream de tamanho
        // desconhecido em vez de congelar a tela de preparacao.
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

static void cio_setup_easy(CurlIO *c) {
    curl_easy_setopt(c->easy, CURLOPT_URL, c->url);
    curl_easy_setopt(c->easy, CURLOPT_USERAGENT, "Nplay-Switch/1.0");
    curl_easy_setopt(c->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c->easy, CURLOPT_MAXREDIRS, 8L);
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
    // Metadados HLS sao transferencias pequenas e sincronas na abertura: um
    // servidor que nunca responde nao pode manter "Preparando video" sem fim.
    // Segmentos longos continuam sem timeout total e usam deteccao de queda.
    curl_easy_setopt(c->easy, CURLOPT_TIMEOUT, c->synchronous ? 20L : 0L);
    // wr_ring deliberately blocks when playback pauses / buffers are full.
    // Curl's average-speed timer includes this consumer delay. For media use
    // actual body idle (xfer_cb), rearmed when wr_ring gets space again.
    curl_easy_setopt(c->easy, CURLOPT_LOW_SPEED_LIMIT, c->streaming ? 0L : 1024L);
    curl_easy_setopt(c->easy, CURLOPT_LOW_SPEED_TIME, c->streaming ? 0L : 30L);
    curl_easy_setopt(c->easy, CURLOPT_WRITEFUNCTION, c->streaming ? wr_ring : wr_tmp);
    curl_easy_setopt(c->easy, CURLOPT_WRITEDATA, c);
    curl_easy_setopt(c->easy, CURLOPT_HEADERFUNCTION, hdr_size);
    curl_easy_setopt(c->easy, CURLOPT_HEADERDATA, c);
    curl_easy_setopt(c->easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c->easy, CURLOPT_XFERINFOFUNCTION, xfer_cb);
    curl_easy_setopt(c->easy, CURLOPT_XFERINFODATA, c);
    // O pool entrega um handle por vez a cada recurso HLS, inclusive playlists.
    // Isso permite reutilizar TLS/DNS nas leituras sequenciais de metadados.
    // Nao use o CURLSH 7.69 da UI: compartilhar conexoes entre produtores
    // simultaneos causou crash no hardware.
    net_configure_curl_isolated(c->easy);
}

static AVIOContext *curl_avio_open_profile(const char *url, int64_t expected_size,
                                           size_t block_size, size_t ring_cap,
                                           int avio_buffer_size, const char *profile,
                                           int synchronous) {
    if (!url || !url[0]) return NULL;
    CurlIO *c = (CurlIO *)calloc(1, sizeof(CurlIO));
    if (!c) return NULL;
    size_t url_len = strlen(url);
    c->url = (char *)malloc(url_len + 1);
    if (!c->url) { free_cio(c); return NULL; }
    memcpy(c->url, url, url_len + 1);
    c->size = expected_size > 0 ? expected_size : -1;
    c->resource_id = SDL_AtomicAdd(&g_resource_sequence, 1) + 1;
    snprintf(c->profile, sizeof(c->profile), "%s", profile ? profile : "file");
    c->response_length = c->range_total = -1;
    c->seek_req = -1; c->base = 0; c->running = 1;
    c->block_size = block_size;
    c->synchronous = synchronous;
    c->streaming = profile && !strcmp(profile, "media");
    c->opened_tick = SDL_GetTicks();
    c->cancel_flag = t_cancel_flag;
    c->pooled = profile && (!strcmp(profile, "media") || !strcmp(profile, "meta"));
    c->ring_cap = synchronous ? 0 : ring_cap;
    c->tmp_cap = synchronous ? HLS_META_INITIAL : c->streaming ? 0 : block_size;
    c->tmp_limit = synchronous ? HLS_META_MAX : c->streaming ? 0 : block_size;
    c->ring = synchronous ? NULL : (unsigned char *)malloc(c->ring_cap);
    c->tmp  = c->tmp_cap ? (unsigned char *)malloc(c->tmp_cap) : NULL;
    c->easy = c->pooled ? hls_easy_take() : curl_easy_init();
    c->mtx = SDL_CreateMutex();
    c->c_data = SDL_CreateCond();
    c->c_space = SDL_CreateCond();
    if ((!synchronous && !c->ring) || (c->tmp_cap && !c->tmp) || !c->easy || !c->mtx || !c->c_data || !c->c_space) { free_cio(c); return NULL; }
    cio_setup_easy(c);

    unsigned char *avio_buf = (unsigned char *)av_malloc((size_t)avio_buffer_size);
    if (!avio_buf) { free_cio(c); return NULL; }
    c->reserved_kb = (int)((c->ring_cap + c->tmp_cap + (size_t)avio_buffer_size + 1023) / 1024);
    c->accounted = 1;
    SDL_AtomicAdd(&g_active_contexts, 1);
    SDL_AtomicAdd(&g_reserved_kb, c->reserved_kb);
    if (!c->streaming)
        diag_player_event("avio", "allocated", "id=%d %s ring=%uKB active=%d total=%dKB",
                          c->resource_id, c->profile, (unsigned)(c->ring_cap / 1024),
                          SDL_AtomicGet(&g_active_contexts), SDL_AtomicGet(&g_reserved_kb));
    if (synchronous) {
        // O crash real da 0.9.8 ocorreu entre a primeira resposta HTTP e o
        // retorno de avformat_open_input. Para recursos pequenos, nao existe
        // beneficio em entregar ao demuxer enquanto outra thread ainda altera
        // o mesmo contexto. Baixe, valide e congele o payload primeiro.
        unsigned char *cached = NULL;
        size_t cached_len = 0;
        int got;
        if (meta_cache_get(c->url, &cached, &cached_len, c->effective_url,
                           sizeof(c->effective_url))) {
            free(c->tmp);
            c->tmp = cached;
            c->tmp_cap = cached_len + 1;
            got = (int)cached_len;
            c->fetch_complete = 1;
            c->size = got;
        } else {
            got = fetch_block(c, 0);
            if (got > 0 && c->fetch_complete && (c->size <= 0 || c->size == got))
                meta_cache_put(c->url, c->effective_url, c->tmp, (size_t)got);
        }
        if (got <= 0 || !c->fetch_complete ||
            (c->size > 0 && c->size != got)) {
            diag_player_event("avio", "metadata-invalid",
                              "id=%d got=%d total=%lld", c->resource_id, got,
                              (long long)c->size);
            av_free(avio_buf); free_cio(c); return NULL;
        }
        // Reutiliza a alocacao que acabou de receber o corpo; nao duplica
        // playlists grandes na memoria limitada do console.
        c->ring = c->tmp;
        c->ring_cap = c->tmp_cap;
        c->tmp = NULL;
        c->tmp_cap = 0;
        c->static_data = 1;
        c->static_len = (size_t)got;
        c->static_pos = 0;
        c->size = got;
        c->eof = 1;
        SDL_AtomicLock(&g_hls_start_lock);
        double start = g_hls_start;
        SDL_AtomicUnlock(&g_hls_start_lock);
        if (start > 0) {
            char *trimmed = NULL;
            size_t trimmed_len = 0;
            double segment_start = 0, total = 0;
            if (hls_media_playlist_trim((const char *)c->ring, c->static_len, start,
                                        &trimmed, &trimmed_len, &segment_start, &total)) {
                free(c->ring);
                c->ring = (unsigned char *)trimmed;
                c->ring_cap = trimmed_len + 1;
                c->static_len = trimmed_len;
                c->size = (int64_t)trimmed_len;
                diag_player_event("avio", "playlist-positioned", "id=%d start=%.2f segment=%.2f total=%.1f",
                                  c->resource_id, start, segment_start, total);
            }
            SDL_AtomicLock(&g_hls_start_lock);
            if (total > g_hls_full_duration) g_hls_full_duration = total;
            if (trimmed) g_hls_positioned_count++;
            SDL_AtomicUnlock(&g_hls_start_lock);
        }
        diag_player_event("avio", "metadata-ready", "id=%d bytes=%d", c->resource_id, got);
    } else {
        c->th = SDL_CreateThread(c->streaming ? producer_stream : producer, "cavio", c);
        if (!c->th) { av_free(avio_buf); free_cio(c); return NULL; }
    }
    AVIOContext *ctx = avio_alloc_context(avio_buf, avio_buffer_size, 0, c, cio_read, NULL, cio_seek);
    if (!ctx) { av_free(avio_buf); free_cio(c); return NULL; }
    return ctx;
}

AVIOContext *nplay_curl_avio_open(const char *url, int64_t expected_size) {
    return curl_avio_open_profile(url, expected_size, FILE_BLOCK, FILE_RINGCAP, 65536, "file", 0);
}

static int hls_is_metadata_url(const char *url) {
    if (!url) return 0;
    const char *query = strchr(url, '?');
    size_t path_len = query ? (size_t)(query - url) : strlen(url);
    const char *suffixes[] = { ".m3u8", ".vtt", ".srt", ".webvtt", ".key" };
    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
        size_t suffix_len = strlen(suffixes[i]);
        if (path_len >= suffix_len &&
            !strncasecmp(url + path_len - suffix_len, suffixes[i], suffix_len)) return 1;
    }
    return strstr(url, "/api/play/") != NULL || strstr(url, "/api/hls") != NULL;
}

// Secao de inicializacao fMP4 (EXT-X-MAP): poucos KB, imutavel e reaberta em
// toda abertura posicionada (seek/troca de audio). Tratada como metadado ela
// fica no cache curto e cada salto economiza uma ida e volta por rendition.
static int hls_is_init_section_url(const char *url) {
    if (!url) return 0;
    const char *query = strchr(url, '?');
    size_t path_len = query ? (size_t)(query - url) : strlen(url);
    const char *name = url;
    for (size_t i = 0; i < path_len; i++) if (url[i] == '/') name = url + i + 1;
    size_t name_len = path_len - (size_t)(name - url);
    return name_len > 8 && !strncasecmp(name, "init", 4) &&
           !strncasecmp(url + path_len - 4, ".mp4", 4);
}

AVIOContext *nplay_curl_avio_open_hls(const char *url) {
    if (hls_is_metadata_url(url) || hls_is_init_section_url(url))
        return curl_avio_open_profile(url, -1, HLS_META_INITIAL, 0, 32768, "meta", 1);
    return curl_avio_open_profile(url, -1, 0, HLS_MEDIA_RINGCAP, 65536, "media", 0);
}

int nplay_curl_avio_hls_media_counts(AVIOContext *ctx, int *audio, int *subtitles) {
    if (audio) *audio = 0;
    if (subtitles) *subtitles = 0;
    if (!ctx || !ctx->opaque) return 0;
    CurlIO *c = (CurlIO *)ctx->opaque;
    if (!c->static_data || !c->ring || c->static_len == 0) return 0;
    return hls_manifest_media_counts((const char *)c->ring, c->static_len,
                                     audio, subtitles);
}

int nplay_curl_avio_metadata(AVIOContext *ctx, const unsigned char **data,
                             size_t *length, char *effective_url,
                             size_t effective_url_size) {
    if (data) *data = NULL;
    if (length) *length = 0;
    if (effective_url && effective_url_size) effective_url[0] = 0;
    if (!ctx || !ctx->opaque) return 0;
    CurlIO *c = (CurlIO *)ctx->opaque;
    if (!c->static_data || !c->ring || c->static_len == 0) return 0;
    if (data) *data = c->ring;
    if (length) *length = c->static_len;
    if (effective_url && effective_url_size)
        snprintf(effective_url, effective_url_size, "%s",
                 c->effective_url[0] ? c->effective_url : c->url);
    return 1;
}

int nplay_curl_avio_replace_metadata(AVIOContext *ctx, char *body, size_t len) {
    if (!ctx || !ctx->opaque || !body || len == 0) return 0;
    CurlIO *c = (CurlIO *)ctx->opaque;
    // Somente antes de o demuxer ler: o buffer do AVIO ainda esta vazio.
    if (!c->static_data || c->static_pos != 0 || ctx->pos != 0 ||
        ctx->buf_ptr != ctx->buf_end) return 0;
    free(c->ring);
    c->ring = (unsigned char *)body;
    c->ring_cap = len + 1;
    c->static_len = len;
    c->size = (int64_t)len;
    return 1;
}

// ---- Busca paralela de metadados HLS ------------------------------------
// O demuxer HLS do FFmpeg pede master, playlists e secoes init uma depois da
// outra, cada uma pagando uma ida e volta inteira. Medido no host com Wi-Fi
// ruim (500 ms): 4,9 s dos 5,7 s ate o primeiro quadro eram essas esperas.
// Antes de avformat_open_input, estes recursos pequenos sao baixados em
// paralelo para o cache curto; o FFmpeg continua pedindo-os na mesma ordem,
// mas cada pedido sai do cache sem rede. Os handles voltam aquecidos ao pool.
#define HLS_PREFETCH_WORKERS 4
#define HLS_PREFETCH_MAX 16
// Cada onda cabe junto com o master nos 12 slots do cache sem expulsar nada
// que a propria abertura ainda vai pedir.
#define HLS_PREFETCH_WAVE 5
typedef struct {
    char (*urls)[HLS_MANIFEST_URI_MAX];
    int count;
    SDL_atomic_t next, cancel, stored, failed, active;
} PrefetchJob;

static int meta_cache_has(const char *url) {
    int found = 0;
    Uint32 now = SDL_GetTicks();
    SDL_AtomicLock(&g_meta_cache_lock);
    for (int i = 0; i < HLS_META_CACHE_SLOTS && !found; i++) {
        HlsMetaCacheEntry *entry = &g_meta_cache[i];
        found = entry->url && !strcmp(entry->url, url) &&
                now - entry->stored <= HLS_META_CACHE_TTL_MS;
    }
    SDL_AtomicUnlock(&g_meta_cache_lock);
    return found;
}

static int prefetch_one(const char *url, SDL_atomic_t *cancel) {
    CurlIO *c = (CurlIO *)calloc(1, sizeof(CurlIO));
    if (!c) return -1;
    c->url = strdup(url);
    c->synchronous = 1;
    c->running = 1;
    c->seek_req = -1;
    c->size = c->response_length = c->range_total = -1;
    c->first_http_logged = 1; // erros continuam no trace; sucesso nao grava SD
    c->cancel_flag = cancel;
    c->tmp_cap = HLS_META_INITIAL;
    c->tmp_limit = HLS_META_MAX;
    c->tmp = (unsigned char *)malloc(c->tmp_cap);
    c->easy = hls_easy_take();
    snprintf(c->profile, sizeof(c->profile), "pre");
    int rc = -1;
    if (c->url && c->tmp && c->easy) {
        cio_setup_easy(c);
        int got = fetch_block(c, 0);
        if (got > 0 && c->fetch_complete && (c->size <= 0 || c->size == got)) {
            meta_cache_put(c->url, c->effective_url, c->tmp, (size_t)got);
            rc = 0;
        }
    }
    // Um handle que falhou pode estar com a conexao quebrada: nao volta ao pool.
    if (c->easy) {
        if (rc == 0) hls_easy_return(c->easy);
        else curl_easy_cleanup(c->easy);
    }
    free(c->url); free(c->tmp); free(c);
    return rc;
}

static int prefetch_worker(void *arg) {
    PrefetchJob *job = (PrefetchJob *)arg;
    for (;;) {
        int i = SDL_AtomicAdd(&job->next, 1);
        if (i >= job->count || SDL_AtomicGet(&job->cancel)) break;
        if (meta_cache_has(job->urls[i])) continue;
        if (prefetch_one(job->urls[i], &job->cancel) == 0) SDL_AtomicAdd(&job->stored, 1);
        else SDL_AtomicAdd(&job->failed, 1);
    }
    SDL_AtomicAdd(&job->active, -1);
    return 0;
}

// Uma onda de downloads paralelos. poll roda na thread chamadora (render),
// que continua desenhando e lendo B; retornar !=0 cancela a onda.
static int prefetch_wave(PrefetchJob *job, int (*poll)(void *), void *userdata) {
    if (job->count <= 0) return 0;
    int workers = job->count < HLS_PREFETCH_WORKERS ? job->count : HLS_PREFETCH_WORKERS;
    SDL_Thread *threads[HLS_PREFETCH_WORKERS] = {0};
    SDL_AtomicSet(&job->active, workers);
    for (int i = 0; i < workers; i++) {
        threads[i] = SDL_CreateThread(prefetch_worker, "hls-prefetch", job);
        if (!threads[i]) SDL_AtomicAdd(&job->active, -1);
    }
    while (SDL_AtomicGet(&job->active) > 0) {
        if (!SDL_AtomicGet(&job->cancel) && poll && poll(userdata))
            SDL_AtomicSet(&job->cancel, 1);
        SDL_Delay(4);
    }
    for (int i = 0; i < workers; i++) if (threads[i]) SDL_WaitThread(threads[i], NULL);
    return SDL_AtomicGet(&job->cancel) ? -1 : 0;
}

int nplay_curl_avio_hls_prefetch(const char *master_url, const char *body, size_t len,
                                 int (*poll)(void *), void *userdata) {
    if (!master_url || !body || len == 0) return 0;
    Uint32 started = SDL_GetTicks();
    char (*raw)[HLS_MANIFEST_URI_MAX] = malloc(sizeof(*raw) * HLS_PREFETCH_MAX);
    char (*urls)[HLS_MANIFEST_URI_MAX] = malloc(sizeof(*urls) * HLS_PREFETCH_MAX);
    if (!raw || !urls) { free(raw); free(urls); return 0; }
    int found = hls_manifest_playlist_uris(body, len, raw, HLS_PREFETCH_MAX), count = 0;
    for (int i = 0; i < found && count < HLS_PREFETCH_WAVE; i++)
        if (hls_manifest_resolve_like_ffmpeg(master_url, raw[i], urls[count], HLS_MANIFEST_URI_MAX))
            count++;
    PrefetchJob playlists = { .urls = urls, .count = count };
    int rc = prefetch_wave(&playlists, poll, userdata);
    // Segunda onda: secoes init (EXT-X-MAP) das playlists ja em cache. A base
    // e a URL da playlist como o FFmpeg a pediu, sem query herdada.
    int inits = 0;
    for (int i = 0; rc == 0 && i < count && inits < HLS_PREFETCH_WAVE; i++) {
        unsigned char *data = NULL; size_t data_len = 0;
        char effective[HLS_MANIFEST_URI_MAX];
        if (!meta_cache_get(urls[i], &data, &data_len, effective, sizeof(effective))) continue;
        char map[HLS_MANIFEST_URI_MAX];
        if (hls_media_playlist_map_uri((const char *)data, data_len, map, sizeof(map)) &&
            hls_manifest_resolve_like_ffmpeg(urls[i], map, raw[inits], HLS_MANIFEST_URI_MAX)) {
            int duplicate = 0;
            for (int k = 0; k < inits && !duplicate; k++) duplicate = !strcmp(raw[k], raw[inits]);
            if (!duplicate) inits++;
        }
        free(data);
    }
    PrefetchJob sections = { .urls = raw, .count = inits };
    if (rc == 0) rc = prefetch_wave(&sections, poll, userdata);
    int stored = SDL_AtomicGet(&playlists.stored) + SDL_AtomicGet(&sections.stored);
    diag_player_event("avio", "meta-prefetch", "playlists=%d inits=%d stored=%d failed=%d cancel=%d ms=%u",
                      count, inits, stored,
                      SDL_AtomicGet(&playlists.failed) + SDL_AtomicGet(&sections.failed),
                      rc < 0, SDL_GetTicks() - started);
    free(raw); free(urls);
    return rc < 0 ? -1 : stored;
}

void nplay_curl_avio_stats(int *active_contexts, int *reserved_kb) {
    if (active_contexts) *active_contexts = SDL_AtomicGet(&g_active_contexts);
    if (reserved_kb) *reserved_kb = SDL_AtomicGet(&g_reserved_kb);
}

void nplay_curl_avio_close(AVIOContext *ctx) {
    if (!ctx) return;
    free_cio((CurlIO *)ctx->opaque);
    av_freep(&ctx->buffer);
    avio_context_free(&ctx);
}
