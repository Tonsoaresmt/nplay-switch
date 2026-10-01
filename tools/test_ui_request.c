#include "ui_request.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

const SDL_Color C_TEXT = {255, 255, 255, 255};
struct SDL_Thread { int (*function)(void *); void *data; };
static struct SDL_Thread thread;
static int scenario, frames, joined, api_calls, quit;
int SDL_AtomicSet(SDL_atomic_t *a, int v) { int old = a->value; a->value = v; return old; }
int SDL_AtomicGet(SDL_atomic_t *a) { return a->value; }
Uint32 SDL_GetTicks(void) { return (Uint32)frames * 16; }
SDL_Thread *SDL_CreateThread(int (*function)(void *), const char *name, void *data) {
    assert(!strcmp(name, "ui-request"));
    if (scenario == 5) return NULL;
    thread.function = function; thread.data = data; return &thread;
}
void SDL_WaitThread(SDL_Thread *t, int *status) { (void)status; assert(t == &thread && api_calls == 1); joined++; }
void SDL_Delay(Uint32 ms) { assert(ms == 16); frames++; if (frames == 4) thread.function(thread.data); }
int SDL_PollEvent(SDL_Event *event) {
    memset(event, 0, sizeof(*event));
    if (frames != 2 || quit || scenario == 0 || scenario == 5) return 0;
    quit = 1;
    if (scenario == 2) { event->type = SDL_FINGERDOWN; event->tfinger.x = 0.92f; event->tfinger.y = 0.05f; }
    else if (scenario == 3) event->type = SDL_QUIT;
    else { event->type = SDL_JOYBUTTONDOWN; event->jbutton.button = 1; }
    return 1;
}
int SDL_SetRenderDrawColor(SDL_Renderer *r, unsigned char a, unsigned char b, unsigned char c, unsigned char d) { (void)r; (void)a; (void)b; (void)c; (void)d; return 0; }
int SDL_RenderClear(SDL_Renderer *r) { (void)r; return 0; }
void SDL_RenderPresent(SDL_Renderer *r) { (void)r; }
void ui_header(const char *a, const char *b, const char *c) { (void)a; (void)b; assert(!strcmp(c, "B Cancelar")); }
int ui_header_action_hit(int x, int y) { return x > 1100 && y < 80; }
void ui_popcorn_draw(SDL_Renderer *r, int x, int y, int size) { (void)r; assert(x == 640 && y == 220 && size == 96); }
int text_center_at(const char *s, int x, int w, int y, SDL_Color c, int big) { (void)s; (void)c; (void)big; assert(x == 0 && w == 1280 && y == 370); return 0; }
long api_send_cancel(const char *path, const char *method, const char *body, SDL_atomic_t *cancel) {
    assert(frames == 4 && !strcmp(path, "/test") && !strcmp(method, "POST") && !strcmp(body, "owned-until-join"));
    api_calls++;
    return cancel->value && scenario != 4 ? 0 : 200;
}
cJSON *api_get_timeout_cancel(const char *path, long connect, long total, SDL_atomic_t *cancel) {
    assert(!strcmp(path, "/test") && connect == 2 && total == 5 && frames == 4);
    api_calls++; (void)cancel; return cJSON_CreateObject();
}
static void reset(int mode) { scenario = mode; frames = joined = api_calls = quit = 0; }
int main(void) {
    for (int mode = 0; mode <= 5; mode++) {
        reset(mode); int running = 1;
        long rc = ui_request_send(NULL, "/test", "POST", "owned-until-join", &running);
        assert(rc == ((mode == 0 || mode == 4) ? 200 : mode == 5 ? -1 : 0));
        assert(joined == (mode == 5 ? 0 : 1));
        assert(running == (mode == 3 ? 0 : 1));
        reset(mode); running = 1; int cancelled = 0;
        cJSON *json = ui_request_get(NULL, "/test", &running, &cancelled);
        assert(cancelled == (mode != 0));
        assert((json != NULL) == (mode == 0));
        cJSON_Delete(json);
    }
    puts("OK UI request: render while pending, button/touch/quit, ambiguous success, join, creation failure");
    return 0;
}
