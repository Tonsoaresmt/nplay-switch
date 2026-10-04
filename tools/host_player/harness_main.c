// Harness host: roda o player_run REAL do NRO (FFmpeg 7.1, libcurl, SDL2) no
// Linux, com video offscreen e audio "dummy" em tempo real. Botoes sao
// injetados por roteiro e cada quadro/evento e registrado com o tempo.
#include <SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "player.h"
#include "player_ui.h"
#include "text.h"
#include "net.h"
#include "ui.h"
#include <libavutil/log.h>

const char *BASE = "http://127.0.0.1:8765";
char g_token[640] = "";
SDL_Renderer *gRen = NULL;
Uint32 g_toast_until = 0;
char g_toast[160] = "";

static Uint32 g_t0;
static volatile double g_first_video_mark = -1;
static double now_s(void) { return (SDL_GetTicks() - g_t0) / 1000.0; }

// ---- roteiro de botoes: "1500:A,4000:R,4200:A" (ms desde player_run)
typedef struct { Uint32 at; int button; char name[8]; } Step;
static Step g_steps[64];
static int g_nsteps;
static SDL_atomic_t g_last_inject_ms;
static char g_last_inject_name[16];

static int button_code(const char *n) {
    static const struct { const char *n; int b; } map[] = {
        {"A",0},{"B",1},{"X",2},{"Y",3},{"L",6},{"R",7},{"ZL",8},{"ZR",9},
        {"PLUS",10},{"MINUS",11},{"LEFT",12},{"UP",13},{"RIGHT",14},{"DOWN",15}};
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) if (!strcmp(n, map[i].n)) return map[i].b;
    return -1;
}
static void parse_script(const char *s) {
    char buf[2048]; snprintf(buf, sizeof(buf), "%s", s ? s : "");
    for (char *tok = strtok(buf, ","); tok && g_nsteps < 64; tok = strtok(NULL, ",")) {
        char name[8] = ""; unsigned at = 0;
        if (sscanf(tok, "%u:%7s", &at, name) == 2 && button_code(name) >= 0) {
            g_steps[g_nsteps].at = at; g_steps[g_nsteps].button = button_code(name);
            snprintf(g_steps[g_nsteps].name, sizeof(g_steps[g_nsteps].name), "%s", name);
            g_nsteps++;
        }
    }
}
static int script_thread(void *u) {
    (void)u;
    Uint32 base = 0;
    if (getenv("SCRIPT_AFTER_FRAME")) {   // tempos relativos ao primeiro quadro
        while (g_first_video_mark < 0 && SDL_GetTicks() - g_t0 < 60000) SDL_Delay(5);
        base = (Uint32)(g_first_video_mark * 1000);
    }
    for (int i = 0; i < g_nsteps; i++) {
        while (SDL_GetTicks() - g_t0 < base + g_steps[i].at) SDL_Delay(2);
        SDL_Event e; memset(&e, 0, sizeof(e));
        e.type = SDL_JOYBUTTONDOWN; e.jbutton.button = (Uint8)g_steps[i].button; e.jbutton.state = SDL_PRESSED;
        snprintf(g_last_inject_name, sizeof(g_last_inject_name), "%s", g_steps[i].name);
        SDL_AtomicSet(&g_last_inject_ms, (int)(SDL_GetTicks() - g_t0));
        SDL_PushEvent(&e);
        printf("%8.3f INJECT %s\n", now_s(), g_steps[i].name);
        fflush(stdout);
    }
    return 0;
}

// ---- instrumentacao por --wrap
static int g_video_updates, g_presents;
static double g_last_video_update = -1, g_max_video_gap, g_first_video = -1;
static int g_gaps_over_250;
static void video_frame(void) {
    double t = now_s();
    if (g_first_video < 0) { g_first_video = t; g_first_video_mark = t; printf("%8.3f VIDEO first frame\n", t); }
    if (g_last_video_update >= 0) {
        double gap = t - g_last_video_update;
        if (gap > g_max_video_gap) g_max_video_gap = gap;
        if (gap > 0.25) {
            g_gaps_over_250++;
            printf("%8.3f VIDEO gap %.0f ms (ultimo botao %s @%.3f)\n", t, gap * 1000,
                   g_last_inject_name, SDL_AtomicGet(&g_last_inject_ms) / 1000.0);
        }
    }
    g_last_video_update = t;
    g_video_updates++;
}
int __real_SDL_UpdateYUVTexture(SDL_Texture *, const SDL_Rect *, const Uint8 *, int, const Uint8 *, int, const Uint8 *, int);
int __wrap_SDL_UpdateYUVTexture(SDL_Texture *t, const SDL_Rect *r, const Uint8 *y, int yp, const Uint8 *u, int up, const Uint8 *v, int vp) {
    video_frame(); return __real_SDL_UpdateYUVTexture(t, r, y, yp, u, up, v, vp);
}
int __real_SDL_UpdateNVTexture(SDL_Texture *, const SDL_Rect *, const Uint8 *, int, const Uint8 *, int);
int __wrap_SDL_UpdateNVTexture(SDL_Texture *t, const SDL_Rect *r, const Uint8 *y, int yp, const Uint8 *uv, int uvp) {
    video_frame(); return __real_SDL_UpdateNVTexture(t, r, y, yp, uv, uvp);
}
int __real_SDL_UpdateTexture(SDL_Texture *, const SDL_Rect *, const void *, int);
int __wrap_SDL_UpdateTexture(SDL_Texture *t, const SDL_Rect *r, const void *p, int pitch) {
    return __real_SDL_UpdateTexture(t, r, p, pitch);
}
void __real_SDL_RenderPresent(SDL_Renderer *);
static const char *g_snap_dir;
static double g_snaps[32]; static int g_nsnaps, g_snap_next;
void __wrap_SDL_RenderPresent(SDL_Renderer *r) {
    g_presents++;
    if (g_snap_dir && g_snap_next < g_nsnaps && now_s() >= g_snaps[g_snap_next]) {
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_ARGB8888);
        if (s && SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0) {
            char path[512]; snprintf(path, sizeof(path), "%s/snap_%02d_%05.1fs.bmp", g_snap_dir, g_snap_next, now_s());
            SDL_SaveBMP(s, path);
            printf("%8.3f SNAP %s\n", now_s(), path);
        }
        if (s) SDL_FreeSurface(s);
        g_snap_next++;
    }
    __real_SDL_RenderPresent(r);
}
// Legenda realmente entregue ao HUD: registra cada mudanca de texto. E assim
// que os testes provam se uma fala aparece (ou some) no tempo certo.
void __real_pui_draw(SDL_Renderer *, const PlayerHud *, Uint32);
static char g_last_sub[1100];
void __wrap_pui_draw(SDL_Renderer *r, const PlayerHud *h, Uint32 now) {
    const char *text = h && h->subtitle_text ? h->subtitle_text : "";
    if (strcmp(text, g_last_sub)) {
        snprintf(g_last_sub, sizeof(g_last_sub), "%s", text);
        char flat[1100]; size_t k = 0;
        for (const char *p = text; *p && k + 4 < sizeof(flat); p++) {
            if (*p == '\n') { memcpy(flat + k, " / ", 3); k += 3; } else flat[k++] = *p;
        }
        flat[k] = 0;
        printf("%8.3f SUB pos=%.2f [%s]\n", now_s(), h ? h->pos : 0, flat);
    }
    __real_pui_draw(r, h, now);
}
void __real_SDL_PauseAudioDevice(SDL_AudioDeviceID, int);
void __wrap_SDL_PauseAudioDevice(SDL_AudioDeviceID d, int pause) {
    printf("%8.3f AUDIO %s\n", now_s(), pause ? "pause" : "play");
    __real_SDL_PauseAudioDevice(d, pause);
}
void __wrap_diag_player_event(const char *component, const char *event, const char *fmt, ...) {
    char msg[512] = "";
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof(msg), fmt ? fmt : "", ap); va_end(ap);
    printf("%8.3f TRACE %s/%s %s\n", now_s(), component, event, msg);
}
void __wrap_diag_player_begin(int item_id, int session_id, int source_id, const char *container, const char *delivery) {
    printf("%8.3f TRACE begin item=%d container=%s delivery=%s\n", now_s(), item_id, container, delivery);
    (void)session_id; (void)source_id;
}

// ---- callbacks da API
static int cb_progress(int item, int pos, int dur, SDL_atomic_t *c, void *u) {
    (void)c; (void)u; printf("%8.3f API progress item=%d pos=%d dur=%d\n", now_s(), item, pos, dur); return 0;
}
static int cb_renew(const PlaybackSource *cur, PlaybackSource *out, SDL_atomic_t *c, void *u) {
    (void)c; (void)u; printf("%8.3f API renew\n", now_s()); *out = *cur; return 0;
}
static int cb_heartbeat(int s, SDL_atomic_t *c, void *u) { (void)s; (void)c; (void)u; return 0; }
static int cb_stop(int i, int s, SDL_atomic_t *c, void *u) { (void)i; (void)s; (void)c; (void)u; printf("%8.3f API stop\n", now_s()); return 0; }

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "uso: %s URL [container]\n", argv[0]); return 2; }
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (getenv("AVDEBUG")) av_log_set_level(atoi(getenv("AVDEBUG")));
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1;
    }
    SDL_Window *win = SDL_CreateWindow("nplay-harness", 0, 0, 1280, 720, 0);
    gRen = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!gRen) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
    text_init();
    net_init();
    if (getenv("EXTRA_CA")) {   // CA do servidor de teste local (somente harness)
        FILE *in = fopen(getenv("EXTRA_CA"), "rb"), *out = fopen("sdmc:/switch/.nplay-ca.pem", "ab");
        char buf[4096]; size_t n;
        while (in && out && (n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
        if (in) fclose(in); if (out) fclose(out);
    }
    if (getenv("BASE_URL")) BASE = getenv("BASE_URL");
    parse_script(getenv("SCRIPT"));
    g_snap_dir = getenv("SNAPDIR");
    if (getenv("SNAPS")) {
        char b[512]; snprintf(b, sizeof(b), "%s", getenv("SNAPS"));
        for (char *t = strtok(b, ","); t && g_nsnaps < 32; t = strtok(NULL, ",")) g_snaps[g_nsnaps++] = atof(t);
    }

    PlayerRequest req; memset(&req, 0, sizeof(req));
    const char *container = argc > 2 ? argv[2] : "m3u8";
    snprintf(req.playback.play_url, sizeof(req.playback.play_url), "%s", argv[1]);
    snprintf(req.playback.container, sizeof(req.playback.container), "%s", container);
    snprintf(req.playback.delivery_str, sizeof(req.playback.delivery_str), "%s",
             getenv("DELIVERY") ? getenv("DELIVERY") : (!strcmp(container, "m3u8") ? "r2" : "upstream"));
    req.playback.delivery = !strcmp(req.playback.delivery_str, "r2") ? DELIVERY_R2 : DELIVERY_UPSTREAM;
    req.playback.item_id = 4242; req.playback.session_id = 77; req.playback.source_id = 9;
    req.playback.sequential_stream = getenv("SEQUENTIAL") ? 1 : 0;
    if (getenv("HOT_SID")) snprintf(req.playback.hot_session_id, sizeof(req.playback.hot_session_id), "%s", getenv("HOT_SID"));
    snprintf(req.playback.section, sizeof(req.playback.section), "%s", getenv("SECTION") ? getenv("SECTION") : "anime");
    req.item_id = 4242; req.session_id = 77; req.source_id = 9;
    req.delivery = req.playback.delivery;
    req.title = "Harness"; req.subtitle = "T1 E1 - Teste";
    req.has_next = getenv("HAS_NEXT") ? 1 : 0; req.next_title = req.has_next ? "T1 E2 - Proximo" : NULL;
    req.container = container; req.url = req.playback.play_url; req.section = req.playback.section;
    req.start_sec = getenv("START") ? atof(getenv("START")) : 0;
    req.audio_pref = getenv("AUDIO_PREF") ? atoi(getenv("AUDIO_PREF")) : 0;
#ifndef HARNESS_LEGACY_044   // 0.12.44 ainda nao tinha o painel Episodios
    static PlayerEpisode eps[64];
    if (getenv("EPISODES")) {
        int n = atoi(getenv("EPISODES")); if (n > 64) n = 64;
        for (int i = 0; i < n; i++) { eps[i].item_id = 5000 + i; eps[i].watched = i < 2;
            snprintf(eps[i].label, sizeof(eps[i].label), "T1 E%d  Episodio numero %d", i + 1, i + 1); }
        req.episodes = eps; req.episode_count = n; req.episode_current = 2;
    }
#endif
    req.progress_cb = cb_progress; req.renew_cb = cb_renew; req.fallback_cb = NULL;
    req.heartbeat_cb = cb_heartbeat; req.stop_cb = cb_stop;

    g_t0 = SDL_GetTicks();
    SDL_Thread *st = SDL_CreateThread(script_thread, "script", NULL);
    PlayerResult res; memset(&res, 0, sizeof(res));
    int rc = player_run(gRen, NULL, &req, &res);
    printf("%8.3f RESULT chosen=%d rc=%d reason=%d pos=%.1f dur=%.1f presented=%d recov=%d audio=%d(%s) sub=%d err='%s'\n",
#ifdef HARNESS_LEGACY_044
           now_s(), 0, rc,
#else
           now_s(), res.chosen_item_id, rc,
#endif
           res.reason, res.position, res.duration, res.presented_frame, res.recovery_count,
#ifdef HARNESS_LEGACY_044
           res.audio_index, res.audio_language, res.subtitle_index, "");
#else
           res.audio_index, res.audio_language, res.subtitle_index, player_last_error());
#endif
    printf("SUMMARY video_frames=%d presents=%d first_video=%.3f max_video_gap_ms=%.0f gaps_over_250=%d\n",
           g_video_updates, g_presents, g_first_video, g_max_video_gap * 1000, g_gaps_over_250);
    SDL_WaitThread(st, NULL);
    return rc < 0 ? 1 : 0;
}
