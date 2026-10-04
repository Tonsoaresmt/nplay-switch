#include "api.h"
#include "net.h"
#include "diag.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
const char *BASE = "https://nplay.test";
char g_token[640] = "test-token";
static int calls, destructive_calls, resolve_calls, cancel_on_get, have_alternative = 1;
static long resolve_code = 200;
static long variants_code = 200;
static const char *variants_error = "{\"error\":\"Expirado\",\"reason\":\"expired\"}";
static const char *resolved = "{\"source_id\":8,\"session_id\":16,\"container\":\"m3u8\",\"delivery\":\"r2\",\"play_url\":\"/api/play/42\"}";
Uint32 SDL_GetTicks(void) { return 100u; }
int SDL_AtomicGet(SDL_atomic_t *value) { return value->value; }
void diag_network_event(const char *method, const char *path, long code, unsigned ms, size_t bytes) {
    (void)method; (void)path; (void)code; (void)ms; (void)bytes;
}
long net_request_timeout_cancel(const char *url, const char *method, const char *body,
    const char *bearer, struct membuf *out, const char **err,
    long connect_timeout, long total_timeout, SDL_atomic_t *cancel) {
    assert(cancel && !cancel->value);
    assert(!strcmp(bearer, "test-token"));
    assert(connect_timeout > 0 && total_timeout <= 8L);
    calls++;
    const char *answer;
    long code = 200;
    if (!strcmp(method, "GET")) {
        assert(!strcmp(url, "https://nplay.test/api/stream/42/variants?sid=15"));
        answer = have_alternative ? "{\"variants\":[{\"src_id\":7,\"container\":\"m3u8\",\"play_url\":\"/old\"},{\"src_id\":9,\"container\":\"embed\",\"play_url\":\"/web\"},{\"src_id\":8,\"container\":\"mp4\",\"play_url\":\"/new\"}]}" : "{\"variants\":[]}";
        code = variants_code;
        if (code != 200) answer = variants_error;
        if (cancel_on_get) cancel->value = 1;
    } else if (strstr(url, "/fail")) {
        destructive_calls++;
        answer = "{\"source_id\":8,\"play_url\":\"/new\"}";
    } else {
        assert(!strcmp(url, "https://nplay.test/api/stream/42"));
        assert(!strcmp(method, "POST"));
        cJSON *json = cJSON_Parse(body); assert(json);
        cJSON *excluded = cJSON_GetObjectItemCaseSensitive(json, "exclude_source_ids");
        if (!cJSON_IsArray(excluded) || cJSON_GetArraySize(excluded) != 1 ||
            !cJSON_IsNumber(cJSON_GetArrayItem(excluded, 0)) ||
            cJSON_GetArrayItem(excluded, 0)->valueint != 7) {
            puts("FAIL: resolver did not exclude the failed source");
            code = 400;
        }
        assert(!strcmp(jstr(json, "quality"), "720p"));
        cJSON_Delete(json);
        resolve_calls++;
        answer = resolved;
        if (code == 200) code = resolve_code;
    }
    *err = NULL;
    out->len = strlen(answer); out->data = malloc(out->len + 1); assert(out->data);
    memcpy(out->data, answer, out->len + 1);
    return code;
}
long net_request_timeout(const char *url, const char *method, const char *body,
    const char *bearer, struct membuf *out, const char **err,
    long connect_timeout, long total_timeout) {
    SDL_atomic_t cancel = {0};
    return net_request_timeout_cancel(url, method, body, bearer, out, err, connect_timeout, total_timeout, &cancel);
}
void membuf_free(struct membuf *m) { free(m->data); m->data = NULL; }
int main(void) {
    PlaybackSource current = {.item_id=42, .session_id=15, .source_id=7}, out = {0};
    strcpy(current.quality, "720p");
    SDL_atomic_t cancel = {0};
    int rc = api_fail_playback_cancel(&current, &cancel, &out);
    if (destructive_calls || rc != 0) {
        printf("FAIL: fallback rc=%d destructive=%d\n", rc, destructive_calls); return 1;
    }
    assert(out.source_id == 8 && out.delivery == DELIVERY_R2 && !strcmp(out.container, "m3u8"));
    assert(calls == 2 && resolve_calls == 1);
    PlaybackSource sentinel = {.item_id=999}; out = sentinel;
    resolved = "{\"source_id\":7,\"session_id\":15,\"container\":\"m3u8\",\"play_url\":\"/old\"}";
    assert(api_fail_playback_cancel(&current, &cancel, &out) != 0 && out.item_id == 999);
    resolved = "{\"source_id\":8,\"session_id\":15,\"container\":\"torrent\",\"play_url\":\"/new\"}";
    assert(api_fail_playback_cancel(&current, &cancel, &out) != 0 && out.item_id == 999);
    resolve_code = 409; resolved = "{\"error\":\"Nenhuma fonte ativa\"}";
    assert(api_fail_playback_cancel(&current, &cancel, &out) != 0 && out.item_id == 999);
    resolve_code = 401; resolved = "{\"error\":\"Expirado\",\"reason\":\"expired\"}";
    assert(api_fail_playback_cancel(&current, &cancel, &out) != 0 && api_last_error_access_expired());
    int before = resolve_calls;
    variants_code = 401;
    assert(api_fail_playback_cancel(&current, &cancel, &out) != 0 && api_last_error_access_expired());
    assert(resolve_calls == before && strstr(api_last_error(), "Expirado"));
    variants_code = 502; variants_error = "{\"error\":\"Indisponivel temporariamente\"}";
    assert(api_fail_playback_cancel(&current, &cancel, &out) != 0 && !api_last_error_access_expired());
    assert(resolve_calls == before && strstr(api_last_error(), "HTTP 502") &&
           strstr(api_last_error(), "Indisponivel temporariamente"));
    variants_code = 200;
    have_alternative = 0;
    assert(api_fail_playback_cancel(&current, &cancel, &out) != 0 && resolve_calls == before);
    have_alternative = 1; cancel_on_get = 1;
    assert(api_fail_playback_cancel(&current, &cancel, &out) != 0 && resolve_calls == before);
    assert(!destructive_calls);
    puts("SAFE FALLBACK OK: no global /fail, session exclusion, complete descriptor, ignored exclusion, incompatible format, 409/expiry/cancel, preserved output");
    return 0;
}
