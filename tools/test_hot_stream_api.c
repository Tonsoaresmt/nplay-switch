#include "api.h"
#include "net.h"
#include "diag.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

const char *BASE = "https://nplay.test";
char g_token[640] = "test-token";
static const char *fixture;
static long fixture_code;
static int calls;

Uint32 SDL_GetTicks(void) { return 100u; }
void diag_network_event(const char *method, const char *path, long code,
                        unsigned ms, size_t bytes) {
    assert(!strcmp(method, "POST"));
    assert(!strcmp(path, "/api/stream/hot/42"));
    assert(code == fixture_code);
    assert(bytes == strlen(fixture));
    (void)ms;
}
long net_request_timeout_cancel(const char *url, const char *method,
                                const char *body, const char *bearer,
                                struct membuf *out, const char **err,
                                long connect_timeout, long total_timeout,
                                SDL_atomic_t *cancel) {
    assert(!strcmp(url, "https://nplay.test/api/stream/hot/42"));
    assert(!strcmp(method, "POST"));
    assert(!strcmp(body, "{\"source_id\":7}"));
    assert(!strcmp(bearer, "test-token"));
    assert(connect_timeout == 5L && total_timeout == 30L);
    assert(cancel && cancel->value == 0);
    *err = NULL;
    out->len = strlen(fixture);
    out->data = malloc(out->len + 1);
    assert(out->data);
    memcpy(out->data, fixture, out->len + 1);
    calls++;
    return fixture_code;
}
long net_request_timeout(const char *url, const char *method,
                         const char *body, const char *bearer,
                         struct membuf *out, const char **err,
                         long connect_timeout, long total_timeout) {
    SDL_atomic_t cancel = {0};
    return net_request_timeout_cancel(url, method, body, bearer, out, err,
                                      connect_timeout, total_timeout, &cancel);
}
void membuf_free(struct membuf *m) { free(m->data); m->data = NULL; }

static HotStreamResult run(const char *json, long code, int expected_rc) {
    fixture = json; fixture_code = code;
    HotStreamResult result = {0};
    SDL_atomic_t cancel = {0};
    assert(api_hot_stream_attempt(42, 7, &cancel, &result) == expected_rc);
    return result;
}

int main(void) {
    HotStreamResult r;
    r = run("{\"status\":\"streaming\",\"delivery\":\"debrid\","
            "\"file_name\":\"movie.mkv\",\"play_url\":\"/api/media/debrid/secret/video\","
            "\"source_id\":7}", 200, 0);
    assert(r.status == HOT_STREAMING && r.source.sequential_stream);
    assert(!strcmp(r.source.play_url, "https://nplay.test/api/media/debrid/secret/video"));
    assert(r.source.source_id == 7 && !strcmp(r.source.container, "mp4"));
    r = run("{\"status\":\"streaming\",\"delivery\":\"debrid\","
            "\"file_name\":\"movie.mp4\",\"play_url\":\"https://cdn.test/movie.mp4\"}", 200, 0);
    assert(r.status == HOT_STREAMING && !r.source.sequential_stream);
    assert(!strcmp(r.source.play_url, "https://cdn.test/movie.mp4"));
    r = run("{\"status\":\"preparing\",\"poll_after_ms\":2000,"
            "\"target_bytes\":1000,\"stream_ready_bytes\":300}", 200, 0);
    assert(r.status == HOT_PREPARING && r.progress == 30 && r.poll_after_ms == 2000);
    r = run("{\"status\":\"r2_ready\",\"delivery\":\"r2\"}", 200, 0);
    assert(r.status == HOT_R2_READY);
    run("{\"status\":\"streaming\"}", 200, -1);
    run("{\"error\":\"sem fonte\"}", 409, -1);
    assert(strstr(api_last_error(), "HTTP 409"));
    assert(calls == 6);
    puts("HOT API CONTRACT OK: remux, MP4, preparo, R2 e erros");
    return 0;
}
