#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "player_recovery.h"
#include "player_sync.h"
#include "subtitle_choice.h"
typedef unsigned Uint32;
typedef int SDL_Renderer;
typedef int SDL_Joystick;
typedef int SDL_Thread;
typedef struct { int value; } SDL_atomic_t;
static void SDL_AtomicLock(int *v) { assert(!*v); *v=1; }
static void SDL_AtomicUnlock(int *v) { assert(*v); *v=0; }
typedef struct { int type; struct { int button; } jbutton; struct { float x,y; } tfinger; } SDL_Event;
#define SDL_QUIT 1
#define SDL_JOYBUTTONDOWN 2
#define SDL_FINGERDOWN 3
#define JOY_B 1
#define JOY_MINUS 2
#define PLAYER_RESTART_SEEK 2
#define PLAYER_RESTART_TRACK 3
#define PLAYER_REQUEST_NEXT 4
#define PLAYER_RESTART_RESUME 6
#include "player_supervisor_types.inc"
static int g_player_last_access_expired, g_player_audio_index, g_player_subtitle_index;
static int g_player_pause_intent;
static SubtitleChoice g_player_subtitle_choice;
static double g_hls_timeline_origin;
static int g_hls_timeline_origin_valid, g_player_chosen_item;
static char g_reopen_headline[64];
static void carry_frame_release(void) { g_reopen_headline[0] = 0; }
static void subtitle_session_clear(void) {}
static char g_player_audio_language[8], g_player_last_error[192];
static unsigned tick;
static int lifecycle_pending, lifecycle_begins, lifecycle_ends, renew_calls, fallback_calls;
static int forced_recovery_rc;
static int recovery_calls;
static int changed_source;
static void player_lifecycle_begin(void) { lifecycle_begins++; lifecycle_pending=0; }
static void player_lifecycle_end(void) { lifecycle_ends++; lifecycle_pending=0; }
static int player_lifecycle_pending(void) { return lifecycle_pending; }
static void player_lifecycle_acknowledge(void) { lifecycle_pending=0; }
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
void player_boot_stage(const char *stage) { (void)stage; }
static void player_error_message(const char *msg) { snprintf(g_player_last_error, sizeof(g_player_last_error), "%s", msg); }
typedef struct { double expected_start, out; int rc, presented, seeked; } Step;
static Step steps[8];
static int count, index_step, cancel_renew;
static int scripted_pause = -1, pause_seen[8];
static int renew(const PlaybackSource *a, PlaybackSource *b, SDL_atomic_t *c, void *u) { (void)c; (void)u; renew_calls++; *b = *a; b->session_id=78; if(changed_source)b->source_id=8; return 0; }
static int fallback(const PlaybackSource *a, PlaybackSource *b, SDL_atomic_t *c, void *u) { fallback_calls++; return renew(a,b,c,u); }
static int player_recovery_call(SDL_Renderer *r, SDL_Joystick *j, const char *t, const char *h, const char *d,
                                PlayerRenewCallback cb, const PlaybackSource *a, PlaybackSource *b, void *u) {
    (void)r; (void)j; (void)t; (void)h; (void)d;
    recovery_calls++;
    if (cancel_renew) return 1;
    if (forced_recovery_rc) return forced_recovery_rc;
    return cb(a, b, NULL, u);
}
static int player_play_internal(SDL_Renderer *r, SDL_Joystick *j, PlayerRequest *req, PlaybackHeartbeat *hb,
                                 double start, double *pos, double *dur, int *seeked, int *presented) {
    (void)r; (void)j; (void)req; (void)hb;
    assert(index_step < count);
    pause_seen[index_step] = req->start_paused;
    if (index_step == 0 && scripted_pause >= 0) g_player_pause_intent = scripted_pause;
    if (index_step == 1 && steps[0].rc == PLAYER_RESTART_RESUME) {
        assert(req->session_id==78 && req->audio_hint==2 && req->subtitle_choice && req->subtitle_choice->valid);
    }
    if (changed_source && index_step==2) {
        assert(req->audio_hint_priority==1&&!strcmp(req->audio_hint_language,"pt"));
    }
    Step *s = &steps[index_step++];
    assert(start == s->expected_start);
    *pos = s->out; *dur = 7200; *seeked = s->seeked; *presented = s->presented;
    if (s->rc==PLAYER_RESTART_RESUME) {
        lifecycle_pending=1; g_player_audio_index=2;
        strcpy(g_player_audio_language,"pt");
        g_player_subtitle_choice.valid=1; g_player_subtitle_choice.index=1;
    }
    if (s->rc==PLAYER_RESTART_TRACK && changed_source) {
        g_player_audio_index=2;strcpy(g_player_audio_language,"pt");
    }
    return s->rc;
}
#include "player_supervisor_function.inc"
static void setup(void) { (void)fallback; memset(steps, 0, sizeof(steps)); count = index_step = cancel_renew = 0; tick = 0; scripted_pause=-1; changed_source=0; forced_recovery_rc=renew_calls=fallback_calls=recovery_calls=0; memset(pause_seen,0,sizeof(pause_seen)); }
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
    setup(); count=2; r.start_paused=1;
    steps[0]=(Step){3000,3060,PLAYER_RESTART_SEEK,1,0};steps[1]=(Step){3060,3061,0,1,1};
    assert(player_run(NULL,NULL,&r,&out)==0&&pause_seen[0]==1&&pause_seen[1]==1);
    setup();count=2;r.start_paused=0;scripted_pause=1;
    steps[0]=(Step){3000,3002,PLAYER_RESTART_TRACK,1,0};steps[1]=(Step){3002,3003,0,1,1};
    assert(player_run(NULL,NULL,&r,&out)==0&&!pause_seen[0]&&pause_seen[1]);
    setup();count=2;scripted_pause=1;
    steps[0]=(Step){3000,3002,-5,1,0};steps[1]=(Step){3002,3003,0,1,1};
    assert(player_run(NULL,NULL,&r,&out)==0&&pause_seen[1]);
    setup();count=1;steps[0]=(Step){3000,3001,0,1,0};
    assert(player_run(NULL,NULL,&r,&out)==0&&!pause_seen[0]);
    puts("PASS actual supervisor: paused seek/track/recovery preserves intent; new playback does not inherit pause.");
    r.fallback_cb=fallback;
    setup();count=2;scripted_pause=1;
    steps[0]=(Step){3000,3002,PLAYER_RESTART_RESUME,1,0};steps[1]=(Step){3002,3003,0,1,1};
    assert(player_run(NULL,NULL,&r,&out)==0&&pause_seen[1]&&renew_calls==1&&!fallback_calls);
    assert(out.position==3003&&out.subtitle_index==2);
    setup();count=1;forced_recovery_rc=-3;
    steps[0]=(Step){3000,3002,PLAYER_RESTART_RESUME,1,0};
    assert(player_run(NULL,NULL,&r,&out)==-5&&index_step==1&&out.position==3002&&recovery_calls==1);
    setup();count=1;cancel_renew=1;
    steps[0]=(Step){3000,3002,PLAYER_RESTART_RESUME,1,0};
    assert(player_run(NULL,NULL,&r,&out)==0&&out.reason==EXIT_REASON_USER&&out.position==3002);
    setup();count=3;
    steps[0]=(Step){3000,3002,PLAYER_RESTART_RESUME,1,0};
    steps[1]=(Step){3002,3003,PLAYER_RESTART_RESUME,1,1};steps[2]=(Step){3003,3004,0,1,1};
    assert(player_run(NULL,NULL,&r,&out)==0&&out.position==3004&&renew_calls==2&&!fallback_calls);
    setup();count=3;changed_source=1;
    steps[0]=(Step){3000,3002,PLAYER_RESTART_TRACK,1,0};
    steps[1]=(Step){3002,3003,PLAYER_RESTART_RESUME,1,1};steps[2]=(Step){3003,3004,0,1,1};
    assert(player_run(NULL,NULL,&r,&out)==0&&out.position==3004&&!strcmp(out.audio_language,"pt"));
    setup();count=2;
    steps[0]=(Step){3000,0,PLAYER_RESTART_RESUME,0,0};steps[1]=(Step){3000,3001,0,1,1};
    assert(player_run(NULL,NULL,&r,&out)==0&&out.position==3001);
    setup();count=3;
    steps[0]=(Step){3000,3002,PLAYER_RESTART_RESUME,1,0};
    steps[1]=(Step){3002,0,-5,0,1};steps[2]=(Step){3002,0,-5,0,1};
    assert(player_run(NULL,NULL,&r,&out)==-5&&out.position==3002&&!fallback_calls&&renew_calls==2);
    setup();count=1;r.playback.sequential_stream=1;
    steps[0]=(Step){3000,3002,PLAYER_RESTART_RESUME,1,0};
    assert(player_run(NULL,NULL,&r,&out)==-5&&out.position==3002&&!renew_calls);
    r.playback.sequential_stream=0;
    setup();count=2;r.playback.sequential_stream=1;r.playback.restart_seek=1;scripted_pause=1;
    steps[0]=(Step){3000,3002,PLAYER_RESTART_RESUME,1,0};steps[1]=(Step){3002,3003,0,1,1};
    assert(player_run(NULL,NULL,&r,&out)==0&&out.position==3003&&renew_calls==1&&pause_seen[1]);
    setup();count=2;
    steps[0]=(Step){3000,1800,PLAYER_RESTART_SEEK,1,0};steps[1]=(Step){1800,1801,0,1,1};
    assert(player_run(NULL,NULL,&r,&out)==0&&out.position==1801&&!renew_calls);
    r.playback.sequential_stream=r.playback.restart_seek=0;
    assert(lifecycle_begins==lifecycle_ends&&!lifecycle_pending);
    puts("PASS HOME/sleep supervisor: position/pause/tracks retained, readmission, no fallback, terminal denial, sequential safe exit, hook teardown.");
    puts("PLAYER SUPERVISOR OK: actual player_run, seek/renew failure preserves position, pause recovery, explicit zero, cancel");
    return 0;
}
