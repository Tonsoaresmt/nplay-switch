#include "connection_test.h"
#include "net.h"
#include <curl/curl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    SDL_atomic_t *cancel, *progress;
    Uint32 started;
    size_t bytes, limit;
    int step;
} ConnectionTransfer;

static size_t connection_discard(char *data, size_t size, size_t count, void *opaque) {
    (void)data;
    ConnectionTransfer *transfer = opaque;
    if ((size && count > SIZE_MAX / size) || SDL_AtomicGet(transfer->cancel) ||
        SDL_GetTicks() - transfer->started >= CONNECTION_TEST_MAX_MS) return 0;
    size_t bytes = size * count;
    if (transfer->bytes > transfer->limit || bytes > transfer->limit - transfer->bytes) return 0;
    transfer->bytes += bytes;
    if (transfer->limit == CONNECTION_DOWNLOAD_BYTES) {
        unsigned part = (unsigned)(transfer->bytes * 100u / transfer->limit);
        SDL_AtomicSet(transfer->progress, (transfer->step * 100 + (int)part) / 9);
    }
    return bytes;
}

static int connection_progress(void *opaque, curl_off_t total, curl_off_t received,
                               curl_off_t uploaded_total, curl_off_t uploaded) {
    (void)total; (void)received; (void)uploaded_total; (void)uploaded;
    ConnectionTransfer *transfer = opaque;
    return SDL_AtomicGet(transfer->cancel) ||
           SDL_GetTicks() - transfer->started >= CONNECTION_TEST_MAX_MS;
}

static double connection_time(CURL *curl, CURLINFO info) {
    double value = 0;
    if (curl_easy_getinfo(curl, info, &value) != CURLE_OK || !isfinite(value) || value < 0) return 0;
    return value;
}

static unsigned connection_ms(double value) {
    return value >= 4294967.295 ? 0xffffffffu : (unsigned)(value * 1000.0);
}

static int connection_type(const char *type, const char *expected) {
    size_t length = strlen(expected);
    return type && !strncmp(type, expected, length) &&
           (!type[length] || type[length] == ';');
}

static void connection_measure(CURL *curl, const char *url, int download,
                               ConnectionTransfer *transfer, ConnectionSample *sample) {
    unsigned elapsed = SDL_GetTicks() - transfer->started;
    if (SDL_AtomicGet(transfer->cancel) || elapsed >= CONNECTION_TEST_MAX_MS) return;
    long timeout_ms = download ? 6000L : 2000L;
    unsigned remaining = CONNECTION_TEST_MAX_MS - elapsed;
    if (remaining < (unsigned)timeout_ms) timeout_ms = (long)remaining;
    transfer->limit = download ? CONNECTION_DOWNLOAD_BYTES : CONNECTION_PROBE_MAX;
    transfer->bytes = 0;
    // reset preserves this worker's connection cache, never shares easy/CURLSH.
    curl_easy_reset(curl);
    net_configure_curl_isolated(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Nplay-Connection-Test");
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1500L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)transfer->limit);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "identity");
    curl_easy_setopt(curl, CURLOPT_HTTP_CONTENT_DECODING, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, connection_discard);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, transfer);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, connection_progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, transfer);
    // No bearer, profile, cookie, result upload or automatic API replay.
    sample->transport_code = (int)curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &sample->http_code);
    sample->bytes = transfer->bytes;
    double pre = connection_time(curl, CURLINFO_PRETRANSFER_TIME);
    double first = connection_time(curl, CURLINFO_STARTTRANSFER_TIME);
    double total = connection_time(curl, CURLINFO_TOTAL_TIME);
    char *type = NULL;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &type);
    const char *expected_type = transfer->step >= 4 && transfer->step < 7 ?
                                "application/json" : "application/octet-stream";
    sample->response_ms = first >= pre ? connection_ms(first - pre) : 0;
    sample->response_valid = !sample->transport_code && sample->http_code == 200 &&
                             connection_type(type, expected_type) && first > 0 && first >= pre && total >= first;
    if (download) {
        double body_time = total >= first ? total - first : 0;
        sample->body_ms = connection_ms(body_time);
        int full = !sample->transport_code && sample->bytes == CONNECTION_DOWNLOAD_BYTES;
        int partial = sample->transport_code == CURLE_OPERATION_TIMEDOUT &&
                      sample->bytes >= 256u * 1024u;
        sample->bandwidth_valid = sample->http_code == 200 && connection_type(type, "application/octet-stream") &&
                                   (full || partial) && body_time >= 0.1;
        sample->partial = sample->bandwidth_valid && partial;
        if (sample->bandwidth_valid)
            sample->mbps = (double)sample->bytes * 8.0 / body_time / 1000000.0;
    }
}

void connection_test_run(const char *base, SDL_atomic_t *cancel,
                         SDL_atomic_t *phase, SDL_atomic_t *progress,
                         ConnectionReport *report) {
    if (!report) return;
    memset(report, 0, sizeof(*report));
    if (!base || strncmp(base, "https://", 8) || !cancel || !phase || !progress) return;
    char service[1024];
    int length = snprintf(service, sizeof(service), "%s/api/health", base);
    if (length < 0 || (size_t)length >= sizeof(service)) return;
    ConnectionTransfer transfer = {.cancel=cancel, .progress=progress, .started=SDL_GetTicks()};
    SDL_AtomicSet(progress, 0);
    CURL *curl = SDL_AtomicGet(cancel) ? NULL : curl_easy_init();
    if (!curl && !SDL_AtomicGet(cancel)) report->local_error = 1;
    if (curl) {
        for (int step = 0; step < 9; step++) {
            if (SDL_AtomicGet(cancel) || SDL_GetTicks() - transfer.started >= CONNECTION_TEST_MAX_MS) break;
            transfer.step = step;
            SDL_AtomicSet(phase, step < 4 ? 1 : step < 7 ? 2 : 3);
            ConnectionSample *sample;
            const char *url;
            char download_url[128];
            if (step < 4) {
                sample = &report->internet[step];
                url = "https://speed.cloudflare.com/__down?bytes=0";
            } else if (step < 7) {
                sample = &report->service[step - 4]; url = service;
            } else {
                sample = &report->downloads[step - 7];
                snprintf(download_url, sizeof(download_url),
                         "https://speed.cloudflare.com/__down?bytes=%u", CONNECTION_DOWNLOAD_BYTES);
                url = download_url;
            }
            connection_measure(curl, url, step >= 7, &transfer, sample);
            report->bytes += sample->bytes;
            SDL_AtomicSet(progress, (step + 1) * 100 / 9);
        }
        curl_easy_cleanup(curl);
    }
    report->elapsed_ms = SDL_GetTicks() - transfer.started;
    report->cancelled = SDL_AtomicGet(cancel) != 0;
    SDL_AtomicSet(phase, 4);
}

static unsigned connection_median(const ConnectionSample *samples, int count,
                                   int *successes, unsigned *range) {
    unsigned values[CONNECTION_LATENCY_SAMPLES];
    *successes = 0;
    for (int i = 0; i < count; i++) if (samples[i].response_valid) {
        unsigned value = samples[i].response_ms;
        int j = (*successes)++;
        while (j > 0 && values[j - 1] > value) { values[j] = values[j - 1]; j--; }
        values[j] = value;
    }
    if (range) *range = *successes > 1 ? values[*successes - 1] - values[0] : 0;
    if (!*successes) return 0;
    return *successes % 2 ? values[*successes / 2] :
        (unsigned)(((uint64_t)values[*successes / 2 - 1] + values[*successes / 2]) / 2);
}

void connection_test_summary(const ConnectionReport *report, ConnectionSummary *summary) {
    if (!summary) return;
    memset(summary, 0, sizeof(*summary));
    if (!report || report->cancelled) return;
    summary->internet_ms = connection_median(report->internet, CONNECTION_LATENCY_SAMPLES,
                                             &summary->internet_ok, &summary->variation_ms);
    summary->service_ms = connection_median(report->service, CONNECTION_SERVICE_SAMPLES,
                                            &summary->service_ok, NULL);
    double bytes = 0, body_ms = 0, minimum = 0, maximum = 0;
    int tls_error = 0, partial = 0, local_error = report->local_error;
    for (int i = 0; i < 9; i++) {
        const ConnectionSample *sample = i < 4 ? &report->internet[i] :
            i < 7 ? &report->service[i - 4] : &report->downloads[i - 7];
        if (sample->transport_code == CURLE_PEER_FAILED_VERIFICATION ||
            sample->transport_code == CURLE_SSL_CACERT_BADFILE) tls_error = 1;
        if (sample->transport_code == CURLE_OUT_OF_MEMORY) local_error = 1;
    }
    for (int i = 0; i < CONNECTION_DOWNLOAD_SAMPLES; i++) {
        const ConnectionSample *sample = &report->downloads[i];
        if (!sample->bandwidth_valid || !isfinite(sample->mbps) || sample->mbps <= 0 || !sample->body_ms) continue;
        summary->download_ok++;
        bytes += (double)sample->bytes; body_ms += sample->body_ms;
        if (!minimum || sample->mbps < minimum) minimum = sample->mbps;
        if (sample->mbps > maximum) maximum = sample->mbps;
        partial |= sample->partial;
        if (sample->partial) summary->partial_downloads++;
    }
    if (body_ms) summary->mbps = bytes * 8.0 / body_ms / 1000.0;
    summary->assessment = local_error ? CONNECTION_LOCAL_ERROR : tls_error ? CONNECTION_TLS_ERROR :
        !summary->internet_ok && !summary->service_ok && !summary->download_ok ? CONNECTION_NO_RESULT :
        summary->internet_ok < 3 || !summary->download_ok ? CONNECTION_EXTERNAL_UNAVAILABLE :
        summary->service_ok < 2 ? CONNECTION_SERVICE_UNAVAILABLE :
        summary->service_ms > 1000 && summary->service_ms > (uint64_t)summary->internet_ms * 3 ? CONNECTION_SERVICE_SLOW :
        summary->mbps < 8.0 ? CONNECTION_LOW_MARGIN :
        summary->internet_ok < 4 || summary->variation_ms > 120 ||
            (minimum && maximum > minimum * 2) ? CONNECTION_VARIABLE :
        partial || summary->download_ok < 2 ? CONNECTION_PARTIAL : CONNECTION_OK;
}

const char *connection_test_advice(ConnectionAssessment assessment) {
    switch (assessment) {
    case CONNECTION_LOCAL_ERROR: return "O aplicativo nao conseguiu executar o teste. Nao e uma falha do Wi-Fi.";
    case CONNECTION_TLS_ERROR: return "Falha de certificado HTTPS. Confira data/hora e a versao do app.";
    case CONNECTION_EXTERNAL_UNAVAILABLE: return "Teste externo incompleto. Isso nao prova que seu Wi-Fi esta ruim.";
    case CONNECTION_SERVICE_UNAVAILABLE: return "O Nplay nao respondeu bem; nao e prova de problema no seu Wi-Fi.";
    case CONNECTION_SERVICE_SLOW: return "O Nplay respondeu mais devagar neste teste; a rede pode nao ser a causa.";
    case CONNECTION_LOW_MARGIN: return "Pouca folga para video. Compare outro comodo e pause outros downloads.";
    case CONNECTION_VARIABLE: return "As respostas oscilaram. Repita perto do roteador para comparar.";
    case CONNECTION_PARTIAL: return "Download parcialmente medido. Repita o teste para confirmar.";
    case CONNECTION_OK: return "Boa folga neste teste. Fonte do video e player tambem influenciam.";
    default: return "Nao houve respostas suficientes. Verifique a conexao e tente de novo.";
    }
}
