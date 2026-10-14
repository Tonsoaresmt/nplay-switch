#pragma once
#include <SDL.h>
#include <stddef.h>

#define CONNECTION_LATENCY_SAMPLES 4
#define CONNECTION_SERVICE_SAMPLES 3
#define CONNECTION_DOWNLOAD_SAMPLES 2
#define CONNECTION_DOWNLOAD_BYTES (4u * 1024u * 1024u)
#define CONNECTION_PROBE_MAX (64u * 1024u)
#define CONNECTION_TEST_MAX_MS 30000u

typedef struct {
    long http_code;
    int transport_code;
    unsigned response_ms, body_ms;
    size_t bytes;
    int response_valid, bandwidth_valid, partial;
    double mbps;
} ConnectionSample;

typedef struct {
    ConnectionSample internet[CONNECTION_LATENCY_SAMPLES];
    ConnectionSample service[CONNECTION_SERVICE_SAMPLES];
    ConnectionSample downloads[CONNECTION_DOWNLOAD_SAMPLES];
    unsigned elapsed_ms;
    size_t bytes;
    int cancelled, local_error;
} ConnectionReport;

typedef enum {
    CONNECTION_NO_RESULT, CONNECTION_LOCAL_ERROR, CONNECTION_TLS_ERROR, CONNECTION_EXTERNAL_UNAVAILABLE,
    CONNECTION_SERVICE_UNAVAILABLE, CONNECTION_SERVICE_SLOW, CONNECTION_LOW_MARGIN,
    CONNECTION_VARIABLE, CONNECTION_PARTIAL, CONNECTION_OK
} ConnectionAssessment;

typedef struct {
    ConnectionAssessment assessment;
    int internet_ok, service_ok, download_ok, partial_downloads;
    unsigned internet_ms, variation_ms, service_ms;
    double mbps;
} ConnectionSummary;

// Worker only. No credentials, SD writes, API mutations or renderer calls.
// phase/progress are the ONLY live fields read by the UI before done+join.
void connection_test_run(const char *base, SDL_atomic_t *cancel,
                         SDL_atomic_t *phase, SDL_atomic_t *progress,
                         ConnectionReport *report);
void connection_test_summary(const ConnectionReport *report, ConnectionSummary *summary);
const char *connection_test_advice(ConnectionAssessment assessment);
