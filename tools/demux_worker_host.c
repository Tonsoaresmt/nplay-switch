#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "player_buffer.h"

typedef unsigned Uint32;
typedef struct { atomic_int value; } SDL_atomic_t;
typedef pthread_mutex_t SDL_mutex;
typedef pthread_cond_t SDL_cond;
typedef struct { pthread_t id; int (*fn)(void *); void *arg; } SDL_Thread;
typedef struct { SDL_atomic_t demux_abort; } PlayerOpenDeadline;
typedef struct { int size, stream_index; int64_t pts; } AVPacket;
typedef struct { int codec_type; } AVCodecParameters;
typedef struct { AVCodecParameters *codecpar; double time_base; } AVStream;
typedef struct { AVStream **streams; } AVFormatContext;
#define AVERROR(x) (-(x))
#define AV_NOPTS_VALUE INT64_MIN
#define AVMEDIA_TYPE_VIDEO 1
static double av_q2d(double time_base) { return time_base; }
static int SDL_AtomicSet(SDL_atomic_t *v, int n) { return atomic_exchange(&v->value, n); }
static Uint32 SDL_GetTicks(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (Uint32)((uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000); }
static void SDL_Delay(Uint32 ms) { struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000 }; nanosleep(&t, NULL); }
static SDL_mutex *SDL_CreateMutex(void) { SDL_mutex *m = malloc(sizeof(*m)); assert(m && !pthread_mutex_init(m, NULL)); return m; }
static SDL_cond *SDL_CreateCond(void) { SDL_cond *c = malloc(sizeof(*c)); assert(c && !pthread_cond_init(c, NULL)); return c; }
static void SDL_LockMutex(SDL_mutex *m) { assert(!pthread_mutex_lock(m)); }
static void SDL_UnlockMutex(SDL_mutex *m) { assert(!pthread_mutex_unlock(m)); }
static void SDL_CondWait(SDL_cond *c, SDL_mutex *m) { assert(!pthread_cond_wait(c, m)); }
static void SDL_CondBroadcast(SDL_cond *c) { assert(!pthread_cond_broadcast(c)); }
static void SDL_CondSignal(SDL_cond *c) { assert(!pthread_cond_signal(c)); }
static void SDL_DestroyMutex(SDL_mutex *m) { assert(!pthread_mutex_destroy(m)); free(m); }
static void SDL_DestroyCond(SDL_cond *c) { assert(!pthread_cond_destroy(c)); free(c); }
static void *entry(void *arg) { SDL_Thread *t = arg; t->fn(t->arg); return NULL; }
static SDL_Thread *SDL_CreateThread(int (*fn)(void *), const char *name, void *arg) {
    (void)name; SDL_Thread *t = malloc(sizeof(*t)); assert(t); t->fn = fn; t->arg = arg;
    assert(!pthread_create(&t->id, NULL, entry, t)); return t;
}
static void SDL_WaitThread(SDL_Thread *t, int *status) { (void)status; assert(!pthread_join(t->id, NULL)); free(t); }
static atomic_int live_packets, reads, next_pts;
static int packet_bytes;
static AVPacket *av_packet_alloc(void) { AVPacket *p = calloc(1, sizeof(*p)); assert(p); atomic_fetch_add(&live_packets, 1); return p; }
static void av_packet_free(AVPacket **p) { if (*p) { free(*p); *p = NULL; atomic_fetch_sub(&live_packets, 1); } }
static void av_packet_move_ref(AVPacket *out, AVPacket *p) { *out = *p; memset(p, 0, sizeof(*p)); }
static int av_read_frame(AVFormatContext *fmt, AVPacket *p) {
    (void)fmt; atomic_fetch_add(&reads, 1); p->size = packet_bytes; p->pts = atomic_fetch_add(&next_pts, 1); return 0;
}
static void diag_player_event(const char *a, const char *b, const char *fmt, ...) { (void)a; (void)b; (void)fmt; }
#include "demux_worker_test.inc"

static int occupancy(DemuxWorker *w) { SDL_LockMutex(w->mutex); int n = w->count; assert(w->queued_bytes <= DEMUX_QUEUE_BYTES); SDL_UnlockMutex(w->mutex); return n; }
static void wait_count(DemuxWorker *w, int desired) {
    Uint32 started = SDL_GetTicks(); while (occupancy(w) != desired && SDL_GetTicks() - started < 3000) SDL_Delay(1);
    assert(occupancy(w) == desired);
}
static void barrier(DemuxWorker *w) {
    demux_worker_request_pause(w); Uint32 started = SDL_GetTicks();
    while (!demux_worker_pause_state(w) && SDL_GetTicks() - started < 3000) SDL_Delay(1);
    assert(demux_worker_pause_state(w) == 1);
}
int main(void) {
    AVCodecParameters codec = {AVMEDIA_TYPE_VIDEO}; AVStream stream = { &codec, 1.0 / 30 }; AVStream *streams[] = { &stream };
    AVFormatContext fmt = { streams }; PlayerOpenDeadline watch = {0}; DemuxWorker w; AVPacket out; Uint32 read_ms;
    for (int repeat = 0; repeat < 25; repeat++) {
        packet_bytes = 1024 * 1024 + 327; atomic_store(&reads, 0); atomic_store(&next_pts, 0);
        assert(!demux_worker_start(&w, &fmt, &watch)); wait_count(&w, 3);
        Uint32 started = SDL_GetTicks();
        while (atomic_load(&reads) < 4 && SDL_GetTicks() - started < 3000) SDL_Delay(1);
        assert(atomic_load(&reads) == 4); // fourth packet parked, not enqueued
        barrier(&w);
        DemuxSnapshot snapshot = demux_worker_snapshot(&w);
        assert(snapshot.count == 3 && snapshot.bytes == 3u * (size_t)packet_bytes);
        assert(snapshot.video_seconds > 0.06 && snapshot.video_seconds < 0.07);
        demux_worker_clear(&w);
        snapshot = demux_worker_snapshot(&w);
        assert(snapshot.count == 0 && snapshot.bytes == 0 && snapshot.video_seconds == 0);
        atomic_store(&next_pts, 100000); demux_worker_resume(&w);
        wait_count(&w, 3); assert(demux_worker_take(&w, &out, &read_ms) == 1 && out.pts >= 100000);
        wait_count(&w, 3); demux_worker_stop(&w); assert(atomic_load(&live_packets) == 0);
    }
    packet_bytes = 12000; atomic_store(&reads, 0); assert(!demux_worker_start(&w, &fmt, &watch));
    wait_count(&w, 128); barrier(&w); demux_worker_clear(&w); demux_worker_resume(&w); wait_count(&w, 128);
    demux_worker_stop(&w); assert(atomic_load(&live_packets) == 0);
    packet_bytes = DEMUX_QUEUE_BYTES + 1; assert(!demux_worker_start(&w, &fmt, &watch));
    Uint32 started = SDL_GetTicks(); int rc = 0;
    while (!(rc = demux_worker_take(&w, &out, NULL)) && SDL_GetTicks() - started < 3000) SDL_Delay(1);
    assert(rc == AVERROR(ENOBUFS)); demux_worker_stop(&w); assert(atomic_load(&live_packets) == 0);
    puts("OK actual demux pthread worker: full-byte/full-slot barriers, 25 seeks, stale pending discarded, stop, oversize, no packet leaks");
    return 0;
}
