#include "ui_request.h"
#include "ui.h"
#include "text.h"
#include <string.h>

typedef struct {
    const char *path, *method, *body;
    SDL_atomic_t cancel, done;
    long code;
    cJSON *json;
} UiRequest;

static int request_worker(void *opaque) {
    UiRequest *request = opaque;
    if (request->method)
        request->code = api_send_cancel(request->path, request->method,
                                        request->body, &request->cancel);
    else
        request->json = api_get_timeout_cancel(request->path, 2L, 5L,
                                               &request->cancel);
    SDL_AtomicSet(&request->done, 1);
    return 0;
}

static int request_wait(SDL_Renderer *ren, UiRequest *request, int *running) {
    SDL_Thread *thread = SDL_CreateThread(request_worker, "ui-request", request);
    if (!thread) return -1; // Never fall back to blocking network on renderer.
    while (!SDL_AtomicGet(&request->done)) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                if (running) *running = 0;
                SDL_AtomicSet(&request->cancel, 1);
            } else if ((event.type == SDL_JOYBUTTONDOWN &&
                       (event.jbutton.button == JOY_B || event.jbutton.button == JOY_MINUS)) ||
                       (event.type == SDL_FINGERDOWN &&
                        ui_header_action_hit((int)(event.tfinger.x * WIN_W),
                                             (int)(event.tfinger.y * WIN_H)))) {
                SDL_AtomicSet(&request->cancel, 1);
            }
        }
        SDL_SetRenderDrawColor(ren, 8, 10, 15, 255);
        SDL_RenderClear(ren);
        ui_header("NPLAY", request->method ? "Atualizando sua conta" : "Retomando seu video",
                   "B Cancelar");
        ui_popcorn_draw(ren, 640, 220, 96);
        text_center_at(SDL_AtomicGet(&request->cancel) ? "Encerrando pedido..." : "So um instante...",
                       0, WIN_W, 370, C_TEXT, 0);
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
    // Worker still owns borrowed path/body, JSON and stack state until joined.
    SDL_WaitThread(thread, NULL);
    return SDL_AtomicGet(&request->cancel);
}

long ui_request_send(SDL_Renderer *ren, const char *path, const char *method,
                     const char *body, int *running) {
    UiRequest request = { .path = path, .method = method, .body = body };
    if (request_wait(ren, &request, running) < 0) return -1;
    // A successful response is authoritative even if B raced completion.
    // No automatic retry of a mutation: a lost response can be ambiguous.
    return request.code;
}

cJSON *ui_request_get(SDL_Renderer *ren, const char *path, int *running,
                       int *cancelled) {
    UiRequest request = { .path = path };
    int result = request_wait(ren, &request, running);
    *cancelled = result != 0;
    if (*cancelled) { cJSON_Delete(request.json); return NULL; }
    return request.json;
}
