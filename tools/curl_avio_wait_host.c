#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef unsigned Uint32;
typedef void CURL;
typedef int64_t curl_off_t;
typedef int SDL_mutex;
typedef int SDL_cond;
typedef int SDL_Thread;
typedef struct { int value; } SDL_atomic_t;
#define AVERROR(x) (-(x))
#define AVERROR_EOF (-10001)
#define AVERROR_EXIT (-10002)
#include "curl_avio_read_test.inc"
static SDL_atomic_t g_media_first_reads, g_media_ready_first, g_media_young_first, g_media_old_empty_first;
static Uint32 ticks, deliver_at, cancel_at, drain_at;
static CurlIO *current;
static int deadline;
static int locked;
static Uint32 SDL_GetTicks(void) { return ticks; }
static int SDL_AtomicAdd(SDL_atomic_t *a, int value) { int old = a->value; a->value += value; return old; }
static int SDL_AtomicGet(SDL_atomic_t *a) { return a->value; }
static void SDL_LockMutex(SDL_mutex *m) { (void)m; assert(!locked); locked = 1; }
static void SDL_UnlockMutex(SDL_mutex *m) { (void)m; assert(locked); locked = 0; }
static void SDL_CondSignal(SDL_cond *c) { (void)c; }
static void SDL_CondWaitTimeout(SDL_cond *c, SDL_mutex *m, unsigned ms) {
    (void)c; (void)m; assert(locked); ticks += ms;
    if (deliver_at && ticks >= deliver_at) { current->ring[current->head] = 42; current->count = 1; deliver_at = 0; }
    if (drain_at && ticks >= drain_at) { current->count = 0; drain_at = 0; }
}
static int abort_requested(void) { return cancel_at && ticks >= cancel_at; }
static int startup_deadline_expired(void) { return deadline && ticks >= (Uint32)deadline; }
static void diag_player_event(const char *a, const char *b, const char *fmt, ...) { (void)a; (void)b; (void)fmt; }
#include "curl_avio_read_function.inc"
#include "curl_avio_progress_function.inc"
#include "curl_avio_ring_function.inc"
static void init(CurlIO *c, unsigned char *ring) {
    memset(c, 0, sizeof(*c)); c->ring = ring; c->ring_cap = 8;
    c->running = 1; c->streaming = 1; c->delivered = 1; c->seek_req = -1;
    current = c; ticks = 0; deliver_at = cancel_at = drain_at = 0; deadline = 0; locked = 0;
}
int main(void) {
    CurlIO c; unsigned char ring[8], out[8];
    init(&c, ring); deliver_at = 1500;
    assert(cio_read(&c, out, 8) == 1 && out[0] == 42 && ticks == 1500);
    deliver_at = 4300;
    assert(cio_read(&c, out, 8) == 1 && out[0] == 42 && ticks == 4300);
    // Already-buffered bytes never wait or get thrown away.
    init(&c, ring); c.count = 1; ring[0] = 7;
    assert(cio_read(&c, out, 8) == 1 && out[0] == 7 && ticks == 0);
    init(&c, ring); c.head = 7; c.count = 2; ring[7] = 8; ring[0] = 9;
    assert(cio_read(&c, out, 8) == 2 && out[0] == 8 && out[1] == 9);
    init(&c, ring); cancel_at = 600;
    assert(cio_read(&c, out, 8) == AVERROR_EXIT && ticks == 600);
    init(&c, ring); c.delivered = 0;
    assert(cio_read(&c, out, 8) == AVERROR(ETIMEDOUT) && ticks == 20000);
    init(&c, ring); c.eof = 1; assert(cio_read(&c, out, 8) == AVERROR_EOF);
    init(&c, ring); c.err = 1; assert(cio_read(&c, out, 8) == AVERROR(EIO));
    init(&c, ring); ticks = 7999; assert(xfer_cb(&c, 0, 0, 0, 0) == 0);
    ticks = 8000; assert(xfer_cb(&c, 0, 0, 0, 0) == 1);
    // Receiving a body / ring backpressure must not trigger first-byte abort.
    ticks = 30000; assert(xfer_cb(&c, 0, 1, 0, 0) == 0);
    c.running = 0; assert(xfer_cb(&c, 0, 1, 0, 0) == 1);
    init(&c, ring); c.seek_req = 100; assert(xfer_cb(&c, 0, 1, 0, 0) == 1);
    init(&c, ring); c.streaming = 0; ticks = 30000;
    assert(xfer_cb(&c, 0, 0, 0, 0) == 0);
    // Real writer callback: a full ring waits 60 s locally, then body idle
    // timer must be rearmed rather than treating pause as a broken network.
    init(&c, ring); c.count = 8; c.response_code = 200; drain_at = 60000;
    assert(wr_ring("abcd", 1, 4, &c) == 4 && ticks == 60000);
    assert(c.count == 4 && c.stream_len == 4);
    assert(xfer_cb(&c, 0, 4, 0, 0) == 0);
#ifdef TRANSPORT_IDLE_GUARD
    ticks += 7999; assert(xfer_cb(&c, 0, 4, 0, 0) == 0);
    ticks++; assert(xfer_cb(&c, 0, 4, 0, 0) == 1);
#endif
    puts("CURL AVIO WAIT OK: gap resumes, buffered bytes, cancel, EOF/error, first-byte deadlines, no backpressure abort");
    return 0;
}
