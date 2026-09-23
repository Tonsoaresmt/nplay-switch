#pragma once

#include <stddef.h>
#include <SDL.h>
#include "cJSON.h"

// One bounded catalog request. The worker owns network and JSON parsing;
// the SDL thread takes ownership of the result after catalog_fetch_take().
typedef struct {
    SDL_Thread *thread;
    SDL_atomic_t done;
    SDL_atomic_t cancel;
    char path[512];
    char bearer[640];
    char error[192];
    cJSON *result;
} CatalogFetch;

int catalog_fetch_start(CatalogFetch *fetch, const char *path, const char *bearer);
int catalog_fetch_take(CatalogFetch *fetch, cJSON **result, char *error, size_t error_cap);
void catalog_fetch_cancel(CatalogFetch *fetch);
void catalog_fetch_dispose(CatalogFetch *fetch);
