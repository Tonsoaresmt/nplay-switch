#define _POSIX_C_SOURCE 200809L
#include <curl/curl.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
typedef unsigned Uint32;
typedef int SDL_mutex;
typedef int SDL_cond;
typedef int SDL_Thread;
typedef struct { int value; } SDL_atomic_t;
#include "curl_avio_read_test.inc"
static Uint32 SDL_GetTicks(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (Uint32)((uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000); }
static int startup_deadline_expired(void) { return 0; }
static int abort_requested(void) { return 0; }
static int SDL_AtomicGet(SDL_atomic_t *a) { return a->value; }
#include "curl_avio_progress_function.inc"
static int first = 1;
static size_t received;
static size_t hold(char *data, size_t size, size_t count, void *ud) {
    (void)data; CurlIO *c = ud;
    if (first) { struct timespec t = {4, 0}; first = 0; nanosleep(&t, NULL); }
    received += size * count;
    c->stream_len += size * count;
    c->last_body_tick = SDL_GetTicks();
    return size * count;
}
int main(int argc, char **argv) {
    assert(argc == 3);
    CURL *easy = curl_easy_init(); assert(easy);
    CurlIO c = {0}; c.running = c.streaming = 1; c.seek_req = -1;
    c.stream_started_tick = c.last_body_tick = SDL_GetTicks();
    if (!strcmp(argv[2], "idle")) first = 0;
    curl_easy_setopt(easy, CURLOPT_URL, argv[1]);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, hold);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &c);
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, atol(argv[2]));
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, 15L);
    if (atol(argv[2]) == 0) {
        curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, xfer_cb);
        curl_easy_setopt(easy, CURLOPT_XFERINFODATA, &c);
    }
    CURLcode rc = curl_easy_perform(easy);
    printf("curl=%d bytes=%zu\n", (int)rc, received);
    curl_easy_cleanup(easy);
    return rc == CURLE_OK ? 0 : 1;
}
