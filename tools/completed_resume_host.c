#include "cJSON.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#if __has_include("playback_resume.h")
#include "playback_resume.h"
#endif
static cJSON *fixture;
static int prompts, restart_prompts, decision = 1, cancelled_request, g_running = 1;
static void *gRen;
static int jint(cJSON *obj, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return v ? v->valueint : 0;
}
static cJSON *ui_request_get(void *ren, const char *path, int *running, int *cancelled) {
    (void)ren; (void)path; (void)running;
    *cancelled = cancelled_request;
    return cJSON_Duplicate(fixture, 1);
}
static int prompt_resume_playback(const char *title, int position) {
    (void)title; (void)position; prompts++; return decision;
}
static int prompt_sequential_restart(const char *title) {
    (void)title; restart_prompts++; return decision;
}
static void api_stop_playback(int item_id) { (void)item_id; }
static double direct(int itemId) {
    const char *stable_title = "The Big Bang Theory";
#include "completed_resume_direct.inc"
    return start;
}
static double resolved(int sequential) {
    int itemId = 17;
    const char *stable_title = "The Big Bang Theory";
    struct { int sequential_stream, session_id; } src = {sequential, 7};
    double start = 0;
    int completed = 0;
    cJSON *pr = cJSON_Duplicate(fixture, 1);
#include "completed_resume_resolved.inc"
    return start;
}
int main(void) {
    const char *cases[] = {
      "{\"progress\":{\"position_seconds\":1320,\"duration_seconds\":1320,\"completed\":1}}",
      "{\"progress\":{\"position_seconds\":1250,\"duration_seconds\":1320,\"completed\":true}}",
      "{\"progress\":{\"position_seconds\":600,\"duration_seconds\":1320,\"completed\":0}}",
      "{\"progress\":{\"position_seconds\":0,\"duration_seconds\":0,\"completed\":1}}",
      "{\"progress\":{\"position_seconds\":-10,\"completed\":0}}",
      "{\"progress\":null}"
    };
    for (int i = 0; i < 6; i++) {
        fixture = cJSON_Parse(cases[i]); assert(fixture);
        for (int path = 0; path < 3; path++) {
            prompts = restart_prompts = 0; decision = 1;
            double start = path == 0 ? direct(17) : resolved(path == 2);
            double expected = i == 2 && path != 2 ? 600 : 0;
            if (start != expected) {
                printf("FAIL case=%d path=%d expected=%.0f actual=%.0f\n", i, path, expected, start);
                return 1;
            }
            assert(prompts == (i == 2 && path != 2));
            assert(restart_prompts == (i == 2 && path == 2));
        }
        cJSON_Delete(fixture);
    }
    fixture = cJSON_Parse(cases[2]);
    decision = 0; assert(direct(17) == 0 && resolved(0) == 0);
    decision = -1; assert(direct(17) == 0 && resolved(0) == 0);
    cJSON_Delete(fixture);
#if __has_include("playback_resume.h")
    assert(playback_resume_position(NAN, 0) == 0);
    assert(playback_resume_position(INFINITY, 0) == 0);
    assert(playback_resume_position(600, 0) == 600);
    assert(playback_resume_position(600, 1) == 0);
#endif
    puts("COMPLETED REPLAY OK: actual direct/R2/sequential decisions, numeric/boolean completion, partial resume, restart/cancel, invalid progress");
    return 0;
}
