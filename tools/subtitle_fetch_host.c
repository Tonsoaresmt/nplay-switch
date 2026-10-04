#define _DEFAULT_SOURCE
#include <SDL.h>
#include "subtitle_retry.h"
#include "subtitle_limits.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#define HLS_MANIFEST_URI_MAX 1024
typedef struct { int *text; } ExternalSubtitleStore;
struct SDL_Thread { pthread_t thread; int (*run)(void *); void *data; };
static SDL_atomic_t ready, video_abort;
static int create_fail, joins, load_result;
static _Thread_local SDL_atomic_t *own_cancel;
int SDL_AtomicGet(SDL_atomic_t *v) { return __atomic_load_n(&v->value, __ATOMIC_SEQ_CST); }
int SDL_AtomicSet(SDL_atomic_t *v, int value) { return __atomic_exchange_n(&v->value, value, __ATOMIC_SEQ_CST); }
Uint32 SDL_GetTicks(void) { return 100; }
static void *entry(void *opaque) { SDL_Thread *t = opaque; t->run(t->data); return NULL; }
SDL_Thread *SDL_CreateThread(int (*fn)(void *), const char *name, void *data) {
    (void)name;
    if (create_fail) return NULL;
    SDL_Thread *t = calloc(1, sizeof(*t)); assert(t); t->run = fn; t->data = data;
    assert(!pthread_create(&t->thread, NULL, entry, t)); return t;
}
void SDL_WaitThread(SDL_Thread *t, int *status) {
    (void)status; assert(!pthread_join(t->thread, NULL)); joins++; free(t);
}
static void nplay_curl_avio_set_thread_cancel(SDL_atomic_t *cancel) {
    assert(cancel != &video_abort); own_cancel = cancel;
}
static void external_subtitle_clear(ExternalSubtitleStore *store) { free(store->text); store->text = NULL; }
static int load_external_subtitle(const char *url, ExternalSubtitleStore *store, int direct, SDL_atomic_t *cancel) {
    (void)url; assert(!direct && own_cancel == cancel);
    while (!SDL_AtomicGet(&ready) && !SDL_AtomicGet(cancel)) usleep(1000);
    if (SDL_AtomicGet(cancel)) return -1;
    store->text = malloc(sizeof(int)); assert(store->text); *store->text = 70;
    return load_result;
}
#include "subtitle_fetch.inc"
static void finish(SubtitleFetch *fetch) {
    SDL_AtomicSet(&ready, 1);
    for (int i = 0; i < 1000 && !SDL_AtomicGet(&fetch->done); i++) usleep(1000);
    assert(SDL_AtomicGet(&fetch->done));
}
static void reset_load(void) { SDL_AtomicSet(&ready, 0); load_result = 0; }
int main(void) {
    char first[1024], second[1024];
    subtitle_session_key("https://test/sub.vtt?track=pt", first, sizeof(first));
    subtitle_session_key("https://test/sub.vtt?track=en", second, sizeof(second));
    assert(strcmp(first, second)); // Query can select a different subtitle track.
    subtitle_session_key("https://test/sub.vtt?track=pt#fragment", second, sizeof(second));
    assert(!strcmp(first, second));
    char too_long[1100]; memset(too_long, 'a', sizeof(too_long)); too_long[1099] = 0;
    subtitle_session_key(too_long, first, sizeof(first)); assert(!first[0]);
    subtitle_session_key(NULL, first, sizeof(first)); assert(!first[0]);
    SubtitleRetry retry;
    subtitle_retry_select(&retry, 2); // applied remains 0, desired is 2.
    const unsigned delays[] = {2000, 5000, 10000, 20000};
    uint32_t now = 100;
    for (int i = 0; i < 4; i++) {
        assert(subtitle_retry_failed(&retry, 2, now));
        assert(subtitle_retry_due(&retry, now + delays[i] - 1) == -1);
        now += delays[i]; assert(subtitle_retry_due(&retry, now) == 2);
    }
    assert(!subtitle_retry_failed(&retry, 2, now));
    subtitle_retry_select(&retry, 1); assert(subtitle_retry_failed(&retry, 1, now));
    subtitle_retry_select(&retry, 3); assert(subtitle_retry_due(&retry, now + 30000) == -1);
    assert(!subtitle_retry_failed(&retry, 1, now));
    assert(subtitle_retry_failed(&retry, 3, now)); subtitle_retry_succeeded(&retry, 3);
    assert(!retry.attempts && !retry.pending);
    subtitle_retry_select(&retry, -1); assert(!subtitle_retry_failed(&retry, 3, now));
    subtitle_retry_select(&retry, 0); now = UINT32_MAX - 1000;
    assert(subtitle_retry_failed(&retry, 0, now));
    assert(subtitle_retry_due(&retry, 998) == -1 && subtitle_retry_due(&retry, 999) == 0);
    assert(SUBTITLE_DOWNLOAD_MAX == 8u * 1024u * 1024u);
    ExternalSubtitleStore applied = {.text = malloc(sizeof(int))}; *applied.text = 10;
    SubtitleFetch fetch = {0};
    reset_load(); assert(!subtitle_fetch_start(&fetch, "https://test/sub.vtt", 2));
    assert(!subtitle_fetch_poll(&fetch, &applied, 2) && *applied.text == 10);
    finish(&fetch); assert(subtitle_fetch_poll(&fetch, &applied, 2) == 1);
    assert(!fetch.thread && *applied.text == 70 && joins == 1);
    reset_load(); load_result = -1;
    assert(!subtitle_fetch_start(&fetch, "https://test/sub.vtt", 3)); finish(&fetch);
    assert(subtitle_fetch_poll(&fetch, &applied, 3) == -1 && *applied.text == 70);
    reset_load(); assert(!subtitle_fetch_start(&fetch, "https://test/sub.vtt", 1)); finish(&fetch);
    *applied.text = 80;
    assert(!subtitle_fetch_poll(&fetch, &applied, 2) && *applied.text == 80);
    reset_load(); assert(!subtitle_fetch_start(&fetch, "https://test/sub.vtt", 3));
    subtitle_fetch_stop(&fetch); assert(!fetch.thread && *applied.text == 80);
    assert(!SDL_AtomicGet(&video_abort));
    create_fail = 1; assert(subtitle_fetch_start(&fetch, "https://test/sub.vtt", 3) < 0);
    assert(!fetch.thread && *applied.text == 80);
    external_subtitle_clear(&applied);
    assert(joins == 4);
    puts("SUBTITLE FETCH OK: query-safe cache key, actual pthread worker/join, failure preserves applied track, stale completion discarded, independent cancel, thread creation failure; desired-track retry 2/5/10/20s, replacement/off/success/tick wrap");
    return 0;
}
