#include "intro_skip.h"
#include "api.h"
#include "net.h"
#include "diag.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define INTRO_BODY_MAX (32u * 1024u)

static int valid_duration(double d) { return isfinite(d) && d > 60 && d <= 14400; }
static int valid_range(int type, double s, double e, double d) {
    if (!valid_duration(d) || !isfinite(s) || !isfinite(e) || s < 0 || e - s < 5) return 0;
    if (type == 1) return e - s <= 240 && s <= fmin(900, d * .6) && e < d - 10;
    return type == 2 && e - s <= 300 && s >= d - fmin(300, d * .25) && e <= d;
}
static int json_number(cJSON *root, const char *key, double *out) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble)) return 0;
    *out = v->valuedouble;
    return 1;
}
static int json_id(cJSON *root, const char *key, int zero, int *out) {
    double v;
    if (!json_number(root, key, &v) || v < (zero ? 0 : 1) || v > INT_MAX || floor(v) != v) return 0;
    *out = (int)v;
    return 1;
}
static int parse_ranges(cJSON *array, double duration, int provider, IntroSegments *out) {
    memset(out, 0, sizeof(*out));
    if (!valid_duration(duration) || !cJSON_IsArray(array) || cJSON_GetArraySize(array) > 32) return 0;
    IntroSegment found[2] = {{0}};
    int conflict[2] = {0};
    cJSON *entry;
    cJSON_ArrayForEach(entry, array) {
        const char *kind = jstr(entry, "type");
        int type = provider ? 1 : kind && !strcmp(kind, "intro") ? 1 : kind && !strcmp(kind, "outro") ? 2 : 0;
        double start, end;
        if (!type || !json_number(entry, provider ? "start_ms" : "start", &start) ||
            !json_number(entry, provider ? "end_ms" : "end", &end)) continue;
        if (provider) { start /= 1000; end /= 1000; }
        if (!valid_range(type, start, end, duration)) continue;
        int i = type - 1;
        if (found[i].type && (fabs(found[i].start-start) > .001 || fabs(found[i].end-end) > .001)) conflict[i] = 1;
        else found[i] = (IntroSegment){type, start, end};
    }
    for (int i = 0; i < 2; i++) if (found[i].type && !conflict[i]) out->ranges[out->count++] = found[i];
    return out->count;
}
int intro_skip_parse(const char *json, double duration, IntroSegments *out) {
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    if (root) parse_ranges(cJSON_GetObjectItemCaseSensitive(root, "segments"), duration, 0, out);
    cJSON_Delete(root);
    return out->count;
}

static int intro_worker(void *userdata) {
    IntroSkip *job = userdata;
    char path[128], url[1024];
    snprintf(path, sizeof(path), "/api/stream/%d/segments", job->item_id);
    snprintf(url, sizeof(url), "%s%s?duration=%.3f&source_id=%d", BASE, path, job->duration, job->source_id);
    struct membuf body = {.limit=INTRO_BODY_MAX};
    const char *error = NULL;
    Uint32 start = SDL_GetTicks();
    long code = net_request_timeout_cancel(url, "GET", NULL, job->bearer, &body, &error, 2L, 5L, &job->cancel);
    (void)error;
    memset(job->bearer, 0, sizeof(job->bearer));
    diag_network_event("GET", path, code, SDL_GetTicks() - start, body.len);
    cJSON *root = code == 200 && !SDL_AtomicGet(&job->cancel) && body.len <= INTRO_BODY_MAX && body.data
        ? cJSON_Parse(body.data) : NULL;
    if (root) {
        parse_ranges(cJSON_GetObjectItemCaseSensitive(root, "segments"), job->duration, 0, &job->pending);
        cJSON *fallback = cJSON_GetObjectItemCaseSensitive(root, "fallback_lookup");
        const char *provider = jstr(fallback, "provider");
        int id=0, season=0, episode=0;
        if (!job->pending.count && provider && !strcmp(provider, "theintrodb") &&
            json_id(fallback, "tmdb_id", 0, &id) && json_id(fallback, "season", 1, &season) &&
            json_id(fallback, "episode", 0, &episode) && !SDL_AtomicGet(&job->cancel)) {
            // The site uses this same public fallback when its VPS cannot reach
            // the provider. Fixed host, no bearer/cookies, no redirects, TLS on.
            Uint32 elapsed = SDL_GetTicks() - start;
            if (elapsed < 8000u) {
                snprintf(url, sizeof(url), "https://api.theintrodb.org/v3/media?tmdb_id=%d&season=%d&episode=%d&duration_ms=%.0f",
                         id, season, episode, job->duration * 1000);
                struct membuf external = {0};
                long remaining = (long)(8000u - elapsed);
                if (remaining > 3000L) remaining = 3000L;
                long remote_code = net_get_text_limited(url, INTRO_BODY_MAX, remaining, &job->cancel, &external);
                cJSON *remote = remote_code == 200 && external.len <= INTRO_BODY_MAX && external.data && !SDL_AtomicGet(&job->cancel)
                    ? cJSON_Parse(external.data) : NULL;
                const char *type = jstr(remote, "type");
                int rid=0, rs=0, re=0;
                if (type && !strcmp(type, "tv") && json_id(remote, "tmdb_id", 0, &rid) && rid == id &&
                    json_id(remote, "season", 1, &rs) && rs == season &&
                    json_id(remote, "episode", 0, &re) && re == episode)
                    parse_ranges(cJSON_GetObjectItemCaseSensitive(remote, "intro"), job->duration, 1, &job->pending);
                cJSON_Delete(remote);
                membuf_free(&external);
            }
        }
    }
    cJSON_Delete(root);
    membuf_free(&body);
    SDL_AtomicSet(&job->done, 1);
    return 0;
}

void intro_skip_poll(IntroSkip *job, int item_id, int source_id, double duration, const char *bearer) {
    if (!job) return;
    if (job->thread && SDL_AtomicGet(&job->done)) {
        SDL_WaitThread(job->thread, NULL);
        job->thread = NULL;
        if (!SDL_AtomicGet(&job->cancel) && job->item_id == item_id && job->source_id == source_id &&
            valid_duration(duration) && fabs(duration - job->duration) <= 1)
            job->ready = job->pending;
    }
    if (job->attempted || job->thread || item_id <= 0 || source_id < 0 || !valid_duration(duration) ||
        !bearer || !bearer[0] || strlen(bearer) >= sizeof(job->bearer)) return;
    job->attempted = 1;
    job->item_id = item_id; job->source_id = source_id; job->duration = duration;
    snprintf(job->bearer, sizeof(job->bearer), "%s", bearer);
    SDL_AtomicSet(&job->cancel, 0); SDL_AtomicSet(&job->done, 0);
    job->thread = SDL_CreateThread(intro_worker, "intro-metadata", job);
    if (!job->thread) memset(job->bearer, 0, sizeof(job->bearer));
}
double intro_skip_target(const IntroSegment *range, double duration) {
    if (!range || !valid_range(range->type, range->start, range->end, duration)) return -1;
    // Opening a native pipeline exactly at EOF cannot present a first frame.
    // Leave the final second to finish normally; never fake completion/autoplay.
    return fmin(range->end, duration - 1);
}
const IntroSegment *intro_skip_current(const IntroSkip *job, double position, double duration, int enabled) {
    if (!job || !enabled || !isfinite(position) || !valid_duration(duration) || fabs(duration-job->duration) > 1) return NULL;
    for (int i=0; i < job->ready.count && i < 2; i++) {
        const IntroSegment *r = &job->ready.ranges[i];
        double target = intro_skip_target(r, duration);
        if (target > 0 && position >= r->start && position < target - .4) return r;
    }
    return NULL;
}
void intro_skip_stop(IntroSkip *job) {
    if (!job) return;
    SDL_AtomicSet(&job->cancel, 1);
    if (job->thread) SDL_WaitThread(job->thread, NULL);
    memset(job, 0, sizeof(*job));
}
