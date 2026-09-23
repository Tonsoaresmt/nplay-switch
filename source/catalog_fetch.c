#include "catalog_fetch.h"

#include <stdio.h>
#include <string.h>

#include "api.h"
#include "diag.h"
#include "net.h"

static int fetch_worker(void *userdata) {
    CatalogFetch *fetch = (CatalogFetch *)userdata;
    char url[1024];
    struct membuf response = {0};
    const char *transport_error = NULL;
    int length = snprintf(url, sizeof(url), "%s%s", BASE, fetch->path);
    if (length < 0 || length >= (int)sizeof(url)) {
        snprintf(fetch->error, sizeof(fetch->error), "Endereco do catalogo muito longo");
    } else {
        Uint32 started = SDL_GetTicks();
        long code = net_request_timeout_cancel(url, "GET", NULL,
                                               fetch->bearer[0] ? fetch->bearer : NULL,
                                               &response, &transport_error, 6L, 25L,
                                               &fetch->cancel);
        diag_network_event("GET", fetch->path, code, SDL_GetTicks() - started, response.len);
        if (code == 200 && response.data) fetch->result = cJSON_Parse(response.data);
        if (!fetch->result) {
            if (code == 200) snprintf(fetch->error, sizeof(fetch->error), "Resposta invalida do catalogo");
            else if (transport_error && transport_error[0])
                snprintf(fetch->error, sizeof(fetch->error), "Rede: %.160s", transport_error);
            else snprintf(fetch->error, sizeof(fetch->error), "Servidor respondeu HTTP %ld", code);
        }
    }
    membuf_free(&response);
    memset(fetch->bearer, 0, sizeof(fetch->bearer));
    SDL_AtomicSet(&fetch->done, 1);
    return 0;
}

int catalog_fetch_start(CatalogFetch *fetch, const char *path, const char *bearer) {
    if (!fetch || fetch->thread || !path || !path[0] || strlen(path) >= sizeof(fetch->path)) return -1;
    if (fetch->result) { cJSON_Delete(fetch->result); fetch->result = NULL; }
    snprintf(fetch->path, sizeof(fetch->path), "%s", path);
    snprintf(fetch->bearer, sizeof(fetch->bearer), "%s", bearer ? bearer : "");
    fetch->error[0] = '\0';
    SDL_AtomicSet(&fetch->done, 0);
    SDL_AtomicSet(&fetch->cancel, 0);
    fetch->thread = SDL_CreateThread(fetch_worker, "catalog-detail", fetch);
    if (!fetch->thread) {
        memset(fetch->bearer, 0, sizeof(fetch->bearer));
        return -1;
    }
    return 0;
}

void catalog_fetch_cancel(CatalogFetch *fetch) {
    if (fetch && fetch->thread) SDL_AtomicSet(&fetch->cancel, 1);
}

int catalog_fetch_take(CatalogFetch *fetch, cJSON **result, char *error, size_t error_cap) {
    if (!fetch || !fetch->thread || !SDL_AtomicGet(&fetch->done)) return 0;
    SDL_WaitThread(fetch->thread, NULL);
    fetch->thread = NULL;
    if (result) { *result = fetch->result; fetch->result = NULL; }
    else if (fetch->result) { cJSON_Delete(fetch->result); fetch->result = NULL; }
    if (error && error_cap) snprintf(error, error_cap, "%s", fetch->error);
    return 1;
}

void catalog_fetch_dispose(CatalogFetch *fetch) {
    if (!fetch) return;
    catalog_fetch_cancel(fetch);
    if (fetch->thread) { SDL_WaitThread(fetch->thread, NULL); fetch->thread = NULL; }
    if (fetch->result) { cJSON_Delete(fetch->result); fetch->result = NULL; }
    memset(fetch->bearer, 0, sizeof(fetch->bearer));
}
