#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "player_recovery.h"
#include "player_sync.h"
typedef unsigned Uint32;
typedef int SDL_Renderer;
typedef int SDL_Joystick;
typedef int SDL_Thread;
typedef struct { int value; } SDL_atomic_t;
typedef struct { int type; struct { int button; } jbutton; } SDL_Event;
#define SDL_QUIT 1
#define SDL_JOYBUTTONDOWN 2
#define JOY_B 1
#define JOY_MINUS 2
#define PLAYER_RESTART_SEEK 2
#define PLAYER_RESTART_TRACK 3
#define PLAYER_REQUEST_NEXT 4
#include "player_supervisor_types.inc"
static int g_player_last_access_expired, g_player_audio_index, g_player_subtitle_index;
static double g_hls_timeline_origin;
static int g_hls_timeline_origin_valid, g_player_chosen_item;
static char g_reopen_headline[64];
static void carry_frame_release(void) { g_reopen_headline[0] = 0; }
static void subtitle_session_clear(void) {}
static char g_player_audio_language[8], g_player_last_error[192];
static unsigned tick;
static void SDL_AtomicSet(SDL_atomic_t *a, int v) { a->value = v; }
static int SDL_AtomicGet(SDL_atomic_t *a) { return a->value; }
static Uint32 SDL_GetTicks(void) { return tick; }
static void SDL_Delay(unsigned ms) { tick += ms; }
static void SDL_FlushEvent(int type) { (void)type; }
static int SDL_PollEvent(SDL_Event *e) { (void)e; return 0; }
static int playback_heartbeat_thread(void *ud) { (void)ud; return 0; }
static SDL_Thread *SDL_CreateThread(int (*fn)(void *), const char *name, void *ud) { (void)fn; (void)name; (void)ud; return NULL; }
static void SDL_WaitThread(SDL_Thread *t, int *rc) { (void)t; (void)rc; }
static void SDL_RenderPresent(SDL_Renderer *r) { (void)r; }
static void pui_draw_loading(SDL_Renderer *r, ...) { (void)r; }
static void diag_player_event(const char *a, const char *b, const char *fmt, ...) { (void)a; (void)b; (void)fmt; }
static void diag_player_begin(int a, int b, int c, const char *d, const char *e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
static void diag_player_finish(int rc) { (void)rc; }
static void nplay_curl_avio_set_abort_check(void *a, void *b) { (void)a; (void)b; }
static void nplay_curl_avio_set_startup_window(unsigned ms) { (void)ms; }
static void nplay_curl_avio_pool_clear(void) {}
static void ui_popcorn_release(void) {}
static void player_error_message(const char *msg) { snprintf(g_player_last_error, sizeof(g_player_last_error), "%s", msg); }
typedef struct { double expected_start, out; int rc, presented, seeked; } Step;
static Step steps[8];
static int count, index_step, cancel_renew;
static int renew(const PlaybackSource *a, PlaybackSource *b, SDL_atomic_t *c, void *u) { (void)c; (void)u; *b = *a; return 0; }
static int player_recovery_call(SDL_Renderer *r, SDL_Joystick *j, const char *t, const char *h, const char *d,
                                PlayerRenewCallback cb, const PlaybackSource *a, PlaybackSource *b, void *u) {
    (void)r; (void)j; (void)t; (void)h; (void)d;
    if (cancel_renew) return 1;
    return cb(a, b, NULL, u);
}
static int player_play_internal(SDL_Renderer *r, SDL_Joystick *j, PlayerRequest *req, PlaybackHeartbeat *hb,
                                 double start, double *pos, double *dur, int *seeked, int *presented) {
    (void)r; (void)j; (void)req; (void)hb;
    assert(index_step < count);
    Step *s = &steps[index_step++];
    assert(start == s->expected_start);
    *pos = s->out; *dur = 7200; *seeked = s->seeked; *presented = s->presented;
    return s->rc;
}
#include "player_supervisor_function.inc"
static void setup(void) { memset(steps, 0, sizeof(steps)); count = index_step = cancel_renew = 0; tick = 0; }
int main(void) {
    PlayerRequest r = {0}; PlayerResult out; r.start_sec = 3000; r.title = "Fixture";
    r.playback.delivery = DELIVERY_R2; strcpy(r.playback.container, "m3u8");
    strcpy(r.playback.play_url, "https://example.invalid/fixture"); r.renew_cb = r.fallback_cb = renew;
    setup(); count = 4;
    steps[0] = (Step){3000, 3060, PLAYER_RESTART_SEEK, 1, 0};
    steps[1] = (Step){3060, 0, -5, 0, 1};
    steps[2] = (Step){3060, 0, -5, 0, 1};
    steps[3] = (Step){3060, 3061, 0, 1, 1};
    assert(player_run(NULL, NULL, &r, &out) == 0 && out.position == 3061 && index_step == count);
    setup(); count = 2;
    steps[0] = (Step){3000, 3002, -5, 1, 0};
    steps[1] = (Step){3002, 3003, 0, 1, 1};
    assert(player_run(NULL, NULL, &r, &out) == 0 && out.position == 3003);
    setup(); count = 2;
    steps[0] = (Step){3000, 0, PLAYER_RESTART_SEEK, 1, 0};
    steps[1] = (Step){0, 1, 0, 1, 0};
    assert(player_run(NULL, NULL, &r, &out) == 0 && out.position == 1);
    setup(); count = 2;
    steps[0] = (Step){3000, 0, PLAYER_RESTART_TRACK, 1, 0};
    steps[1] = (Step){3000, 3001, 0, 1, 1};
    assert(player_run(NULL, NULL, &r, &out) == 0 && out.position == 3001);
    setup(); count = 3;
    for (int i = 0; i < count; i++) steps[i] = (Step){3000, 0, -5, 0, 1};
    assert(player_run(NULL, NULL, &r, &out) == -5 && out.position == 3000 && out.reason == EXIT_REASON_ERROR);
    setup(); count = 1; cancel_renew = 1;
    steps[0] = (Step){3000, 0, -5, 0, 1};
    assert(player_run(NULL, NULL, &r, &out) == 0 && out.position == 3000 && out.reason == EXIT_REASON_USER);
    puts("PLAYER SUPERVISOR OK: actual player_run, seek/renew failure preserves position, pause recovery, explicit zero, cancel");
    return 0;
}
