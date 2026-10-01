#pragma once
#include "api.h"
// Modal owns input until the joined worker finishes: no profile/context change
// can race the mutation. Cancelling is not proof the server did not apply it.
long ui_request_send(SDL_Renderer *ren, const char *path, const char *method,
                     const char *body, int *running);
cJSON *ui_request_get(SDL_Renderer *ren, const char *path, int *running,
                       int *cancelled);
