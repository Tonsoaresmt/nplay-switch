#include "api.h"
#include "net.h"
#include "diag.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *BASE = "https://nplay.test";
char g_token[640] = "test-token";
static const char *fixture;
static int calls;
Uint32 SDL_GetTicks(void) { return 100; }
int SDL_AtomicGet(SDL_atomic_t *value) { return value->value; }
void diag_network_event(const char *method, const char *path, long code,
                        unsigned ms, size_t bytes) {
    assert(!strcmp(method, "POST") && !strcmp(path, "/api/stream/42"));
    assert(code == 200 && bytes == strlen(fixture)); (void)ms;
}
long net_request_timeout_cancel(const char *url, const char *method,
                                const char *body, const char *bearer,
                                struct membuf *out, const char **err,
                                long connect_timeout, long total_timeout,
                                SDL_atomic_t *cancel) {
    assert(!strcmp(url, "https://nplay.test/api/stream/42"));
    assert(!strcmp(method, "POST") && !strcmp(bearer, "test-token"));
    assert(connect_timeout == 8 && total_timeout == 20 && !cancel);
    // The NRO never forces an embed/provider or excludes the ready R2 source.
    assert(!strcmp(body, "{}"));
    out->len = strlen(fixture); out->data = malloc(out->len + 1);
    assert(out->data); memcpy(out->data, fixture, out->len + 1);
    *err = NULL; calls++; return 200;
}
void membuf_free(struct membuf *m) { free(m->data); m->data = NULL; }
long net_request_timeout(const char *url, const char *method,
                         const char *body, const char *bearer,
                         struct membuf *out, const char **err,
                         long connect_timeout, long total_timeout) {
    return net_request_timeout_cancel(url, method, body, bearer, out, err,
                                      connect_timeout, total_timeout, NULL);
}

int main(void) {
    PlaybackSource source = {0};
    fixture = "{\"kind\":\"episode\",\"section\":\"anime\",\"container\":\"m3u8\","
              "\"delivery\":\"r2\",\"play_url\":\"/api/play/42\","
              "\"session_id\":9,\"source_id\":7,\"season\":1,\"episode\":2}";
    assert(!api_resolve_playback(42, NULL, &source));
    assert(source.delivery == DELIVERY_R2 && !strcmp(source.container, "m3u8"));
    assert(!strcmp(source.section, "anime") && source.source_id == 7);
    assert(!strcmp(source.play_url, "https://nplay.test/api/play/42"));
    assert(source.session_id == 9 && source.season == 1 && source.episode == 2);
    assert(!source.source_provider[0]);
    fixture = "{\"kind\":\"episode\",\"section\":\"anime\",\"container\":\"mp4\","
              "\"source_provider\":\"animesdrive\",\"play_url\":\"https://cdn.test/video.mp4\"}";
    assert(!api_resolve_playback(42, NULL, &source));
    assert(source.delivery == DELIVERY_UNKNOWN && !strcmp(source.container, "mp4"));
    assert(!strcmp(source.source_provider, "animesdrive") && source.session_id == 0);
    fixture = "{\"delivery\":\"r2\",\"container\":\"m3u8\",\"play_url\":\"/api/play/42\","
              "\"source_provider\":\"https://secret.test/token\"}";
    assert(!api_resolve_playback(42, NULL, &source));
    assert(source.delivery == DELIVERY_R2 && !source.source_provider[0]);
    assert(calls == 3);
    puts("anime playback descriptor OK: R2 respected, provider distinguished, no stale source or secret label");
}
