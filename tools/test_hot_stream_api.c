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
static int started_mode;
static int normal_mode, r2_followup;

Uint32 SDL_GetTicks(void) { return 100u; }
int SDL_AtomicGet(SDL_atomic_t *value) { return value->value; }
void diag_network_event(const char *method, const char *path, long code,
                        unsigned ms, size_t bytes) {
    assert(!strcmp(method, "POST"));
    assert(!strcmp(path, normal_mode ? "/api/stream/42" : started_mode ? "/api/stream/hot/request/91/started" : "/api/stream/hot/42"));
    assert(code == fixture_code);
    assert(bytes == strlen(fixture));
    (void)ms;
}
long net_request_timeout_cancel(const char *url, const char *method,
                                const char *body, const char *bearer,
                                struct membuf *out, const char **err,
                                long connect_timeout, long total_timeout,
                                SDL_atomic_t *cancel) {
    if (r2_followup && !strcmp(url,"https://nplay.test/api/stream/42")) {
        normal_mode=1;
        fixture="{\"delivery\":\"r2\",\"container\":\"m3u8\",\"play_url\":\"/api/play/42?token=test\",\"session_id\":52,\"source_id\":14}";
    }
    assert(!strcmp(url, normal_mode ? "https://nplay.test/api/stream/42" : started_mode ? "https://nplay.test/api/stream/hot/request/91/started" : "https://nplay.test/api/stream/hot/42"));
    assert(!strcmp(method, "POST"));
    if (normal_mode) assert(!strcmp(body,"{}"));
    else if (started_mode) {
        cJSON *parsed = cJSON_Parse(body); assert(parsed);
        assert(!strcmp(jstr(parsed, "session_id"), "1234567890abcdef_safe-session"));
        assert(!strcmp(jstr(parsed, "delivery"), "debrid"));
        cJSON_Delete(parsed);
    } else assert(!strcmp(body, "{\"source_id\":7}"));
    assert(!strcmp(bearer, "test-token"));
    assert(connect_timeout == (normal_mode ? 4L : started_mode ? 3L : 5L) && total_timeout == (normal_mode ? 8L : started_mode ? 5L : 30L));
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
    assert(r.source.session_id == 0 && !r.source.hot_session_id[0]);
    r = run("{\"status\":\"streaming\",\"delivery\":\"hot\","
            "\"session_id\":\"1234567890abcdef_safe-session\",\"play_url\":\"/api/media/hot/x/video\"}", 200, 0);
    assert(!strcmp(r.source.hot_session_id, "1234567890abcdef_safe-session"));
    assert(r.source.session_id == 0); // Must not become a heartbeat id.
    r = run("{\"status\":\"streaming\",\"session_id\":\"1234567890abcdef/../bad\","
            "\"play_url\":\"/api/media/hot/x/video\"}", 200, 0);
    assert(!r.source.hot_session_id[0]);
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
    assert(calls == 8);
    PlaybackSource route={0}; assert(!api_playback_needs_hot_source(NULL));
    snprintf(route.container,sizeof(route.container),"embed"); assert(api_playback_needs_hot_source(&route));
    snprintf(route.container,sizeof(route.container),"torrent"); assert(api_playback_needs_hot_source(&route));
    snprintf(route.container,sizeof(route.container),"m3u8"); assert(!api_playback_needs_hot_source(&route));
    snprintf(route.container,sizeof(route.container),"mp4"); assert(!api_playback_needs_hot_source(&route));
    r = run("{\"status\":\"streaming\",\"delivery\":\"debrid\",\"seekable\":true,\"seek_mode\":\"range\","
            "\"file_name\":\"movie.mp4\",\"play_url\":\"/api/media/debrid/x/video?token=test\","
            "\"session_id\":\"1234567890abcdef_safe-session\",\"source_id\":7,\"request_id\":91,\"file_size\":10737418240}",200,0);
    assert(!r.source.sequential_stream && r.source.request_id == 91 && r.source.source_bytes == 10737418240LL);
    assert(!r.source.session_id);
    PlaybackSource catalog = {.item_id=42,.session_id=77,.season=2,.episode=3};
    snprintf(catalog.kind,sizeof(catalog.kind),"episode");
    snprintf(catalog.section,sizeof(catalog.section),"anime");
    api_hot_inherit_context(&r.source,&catalog);
    assert(!strcmp(r.source.kind,"episode") && !strcmp(r.source.section,"anime"));
    assert(r.source.season==2 && r.source.episode==3 && r.source.session_id==0);
    catalog.item_id=99; catalog.episode=5; api_hot_inherit_context(&r.source,&catalog);
    assert(r.source.episode==3);
    PlaybackSource started_source=r.source;
    r = run("{\"status\":\"streaming\",\"delivery\":\"debrid\",\"seekable\":true,"
            "\"file_name\":\"movie.mp4\",\"play_url\":\"/api/media/debrid/x/video\"}",200,0);
    assert(!r.source.sequential_stream);
    r = run("{\"status\":\"streaming\",\"delivery\":\"debrid\",\"seek_mode\":\"restart\",\"seekable\":false,"
            "\"file_name\":\"movie.mkv\",\"play_url\":\"/api/media/debrid/x/video\",\"request_id\":1.5}",200,0);
    assert(r.source.sequential_stream && !r.source.request_id);
    r = run("{\"status\":\"streaming\",\"delivery\":\"debrid\",\"file_name\":\"movie.mp4\","
            "\"play_url\":\"/api/media/debrid/x/video\"}",200,0);
    assert(!r.source.sequential_stream); // Older server with a known MP4 filename.
    r = run("{\"status\":\"streaming\",\"delivery\":\"debrid\",\"seek_mode\":\"range\","
            "\"container\":\"hls\",\"file_name\":\"movie.mkv\",\"play_url\":\"/api/media/debrid/x/hls/index.m3u8?token=test\"}",200,0);
    assert(!r.source.sequential_stream && !strcmp(r.source.container,"m3u8"));
    SDL_atomic_t cancel={0};
    fixture="{\"status\":\"streaming\",\"delivery\":\"debrid\",\"source_id\":7,\"seek_mode\":\"range\",\"file_name\":\"movie.mp4\",\"play_url\":\"/api/media/debrid/new/video?token=test\"}";
    fixture_code=200; PlaybackSource renewed={0};
    assert(!api_hot_refresh_range_cancel(&started_source,&cancel,&renewed));
    assert(renewed.item_id==42 && renewed.episode==3 && !renewed.session_id && !renewed.sequential_stream);
    PlaybackSource unchanged={.item_id=99}; renewed=unchanged;
    fixture="{\"status\":\"preparing\"}";
    assert(api_hot_refresh_range_cancel(&started_source,&cancel,&renewed)==-1 && renewed.item_id==99);
    fixture="{\"status\":\"streaming\",\"delivery\":\"debrid\",\"source_id\":7,\"seek_mode\":\"restart\",\"file_name\":\"movie.mkv\",\"play_url\":\"/api/media/debrid/new/video\"}";
    assert(api_hot_refresh_range_cancel(&started_source,&cancel,&renewed)==-1 && renewed.item_id==99);
    fixture="{\"status\":\"streaming\",\"delivery\":\"debrid\",\"source_id\":8,\"file_name\":\"movie.mp4\",\"play_url\":\"/api/media/debrid/new/video\"}";
    assert(api_hot_refresh_range_cancel(&started_source,&cancel,&renewed)==-1 && renewed.item_id==99);
    fixture="{\"error\":\"expired\",\"reason\":\"expired\"}"; fixture_code=401;
    assert(api_hot_refresh_range_cancel(&started_source,&cancel,&renewed)==-1 && api_last_error_access_expired());
    fixture="{\"status\":\"r2_ready\"}"; fixture_code=200;r2_followup=1;
    assert(!api_hot_refresh_range_cancel(&started_source,&cancel,&renewed));
    assert(renewed.delivery==DELIVERY_R2 && renewed.session_id==52 && !strcmp(renewed.container,"m3u8"));
    assert(!api_last_error_access_expired()); // Previous failed call cannot contaminate success.
    normal_mode=r2_followup=0;
    started_mode=1; fixture="{\"ok\":true}"; fixture_code=200;
    assert(!api_hot_playback_started_cancel(&started_source,&cancel));
    fixture_code=503; assert(api_hot_playback_started_cancel(&started_source,&cancel)==-1);
    int before=calls; cancel.value=1;
    assert(api_hot_playback_started_cancel(&started_source,&cancel)==-1 && calls==before);
    cancel.value=0; started_source.request_id=0;
    assert(api_hot_playback_started_cancel(&started_source,&cancel)==-1 && calls==before);
    started_source.request_id=91; snprintf(started_source.hot_session_id,sizeof(started_source.hot_session_id),"invalid/../sid");
    assert(api_hot_playback_started_cancel(&started_source,&cancel)==-1 && calls==before);
    puts("HOT API CONTRACT OK: remux, MP4, preparo, R2 e erros");
    return 0;
}
