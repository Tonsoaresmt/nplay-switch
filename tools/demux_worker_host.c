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
static int packet_bytes, audio_every; // audio_every>0: um pacote de audio (stream 1) a cada N
static int synthetic_audio_burst, fail_worker_alloc;
static void *worker_allocation;

static AVPacket *av_packet_alloc(void) { AVPacket *p = calloc(1, sizeof(*p)); assert(p); atomic_fetch_add(&live_packets, 1); return p; }
static void av_packet_free(AVPacket **p) { if (*p) { free(*p); *p = NULL; atomic_fetch_sub(&live_packets, 1); } }
static void av_packet_move_ref(AVPacket *out, AVPacket *p) { *out = *p; memset(p, 0, sizeof(*p)); }
static int av_read_frame(AVFormatContext *fmt, AVPacket *p) {
    (void)fmt; int n = atomic_fetch_add(&reads, 1); p->size = packet_bytes; p->pts = atomic_fetch_add(&next_pts, 1);
    if (synthetic_audio_burst) p->stream_index = n < synthetic_audio_burst ? 1 : 0;
    else p->stream_index = audio_every > 0 && n % audio_every == audio_every - 1 ? 1 : 0;
    return 0;
}
static void diag_player_event(const char *a, const char *b, const char *fmt, ...) { (void)a; (void)b; (void)fmt; }
static void *host_worker_calloc(size_t n,size_t s) {
    if(fail_worker_alloc)return NULL;
    assert(!worker_allocation);worker_allocation=calloc(n,s);return worker_allocation;
}
static void host_worker_free(void *p) {
    if(p==worker_allocation)worker_allocation=NULL;
    free(p);
}
#define calloc host_worker_calloc
#define free host_worker_free
#include "demux_worker_test.inc"
#undef calloc
#undef free

static int occupancy(DemuxWorker *w) { SDL_LockMutex(w->mutex); int n = w->count; assert(w->queued_bytes <= w->byte_cap); SDL_UnlockMutex(w->mutex); return n; }
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
    AVCodecParameters codec = {AVMEDIA_TYPE_VIDEO}, audio_codec = {2};
    AVStream stream = { &codec, 1.0 / 30 }, audio_stream = { &audio_codec, 1.0 / 48000 };
    AVStream *streams[] = { &stream, &audio_stream };
    AVFormatContext fmt = { streams }; PlayerOpenDeadline watch = {0}; DemuxWorker w; AVPacket out; Uint32 read_ms;
    for (int repeat = 0; repeat < 25; repeat++) {
        packet_bytes = 1024 * 1024 + 327; atomic_store(&reads, 0); atomic_store(&next_pts, 0);
        assert(!demux_worker_start(&w, &fmt, &watch, 0)); wait_count(&w, 3);
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
    size_t reserve_headers=DEMUX_LOOKAHEAD_PACKETS*(sizeof(DemuxItem)+sizeof(AVPacket));
    int small_fill = (int)((DEMUX_QUEUE_BYTES-reserve_headers) / 12000u);
    if (small_fill > DEMUX_QUEUE_PACKETS) small_fill = DEMUX_QUEUE_PACKETS;
    packet_bytes = 12000; atomic_store(&reads, 0); assert(!demux_worker_start(&w, &fmt, &watch, 0));
    wait_count(&w, small_fill); barrier(&w); demux_worker_clear(&w); demux_worker_resume(&w); wait_count(&w, small_fill);
    demux_worker_stop(&w); assert(atomic_load(&live_packets) == 0);
    // Remux sequencial: o teto maior acomoda o bloco de video que antecede o audio.
    packet_bytes = 30000; atomic_store(&reads, 0); assert(!demux_worker_start(&w, &fmt, &watch, 1));
    assert(w.byte_cap == DEMUX_QUEUE_BYTES_SEQUENTIAL);
    wait_count(&w, (int)(w.payload_cap / 30000u)); demux_worker_stop(&w);
    assert(atomic_load(&live_packets) == 0);
    // Audio atras de video: take_stream tira o audio sem perder a ordem do video.
    packet_bytes = 1000; audio_every = 10; atomic_store(&reads, 0); atomic_store(&next_pts, 0);
    assert(!demux_worker_start(&w, &fmt, &watch, 0)); wait_count(&w, DEMUX_QUEUE_PACKETS); barrier(&w);
    assert(demux_worker_take_stream(&w, 1, &out, NULL) == 1 && out.stream_index == 1 && out.pts == 9);
    assert(demux_worker_take_stream(&w, 1, &out, NULL) == 1 && out.pts == 19 && w.audio_ahead == 2);
    int64_t last = -1;
    for (int i = 0; i < 10; i++) {
        assert(demux_worker_take(&w, &out, NULL) == 1 && out.stream_index == 0 && out.pts > last);
        last = out.pts;
    }
    assert(last == 10); // 0..8 e depois 10: o audio 9 saiu antes e a ordem do video ficou
    assert(demux_worker_take_stream(&w, 5, &out, NULL) == 0);
    demux_worker_resume(&w); audio_every = 0; demux_worker_stop(&w); assert(atomic_load(&live_packets) == 0);
    packet_bytes = DEMUX_QUEUE_BYTES + 1; assert(!demux_worker_start(&w, &fmt, &watch, 0));
    Uint32 started = SDL_GetTicks(); int rc = 0;
    while (!(rc = demux_worker_take(&w, &out, NULL)) && SDL_GetTicks() - started < 3000) SDL_Delay(1);
    assert(rc == AVERROR(ENOBUFS)); demux_worker_stop(&w); assert(atomic_load(&live_packets) == 0);
    // Newly covered .58 circular wait: compressed audio is retained, while
    // the producer is given more INDEX slots inside its unchanged byte budget.
    for(int burst=600;burst<=3000;burst+=1200) {
        synthetic_audio_burst=burst;packet_bytes=1000;atomic_store(&reads,0);
        assert(!demux_worker_start(&w,&fmt,&watch,1));wait_count(&w,DEMUX_QUEUE_PACKETS);
        int got=0;started=SDL_GetTicks();
        while(!got&&SDL_GetTicks()-started<3000) {
            got=demux_worker_take_stream(&w,0,&out,NULL);
            if(!got)assert(demux_worker_video_lookahead(&w,0)>=0);
            SDL_Delay(1);
        }
        assert(got&&out.stream_index==0);barrier(&w);
        assert(w.queue_slots>DEMUX_QUEUE_PACKETS&&w.queue_slots<=DEMUX_LOOKAHEAD_PACKETS);
        assert(w.queued_bytes+reserve_headers<=w.byte_cap);
        for(int i=0;i<burst;i++)assert(demux_worker_take(&w,&out,NULL)==1&&out.stream_index==1);
        demux_worker_clear(&w);assert(w.queue_slots==DEMUX_QUEUE_PACKETS);
        demux_worker_stop(&w);assert(!atomic_load(&live_packets)&&!worker_allocation);
    }
    // Ring wrap during index extension retains order and no pointer is freed twice.
    synthetic_audio_burst=3000;packet_bytes=1000;atomic_store(&reads,0);atomic_store(&next_pts,0);
    assert(!demux_worker_start(&w,&fmt,&watch,1));wait_count(&w,DEMUX_QUEUE_PACKETS);barrier(&w);
    for(int i=0;i<123;i++)assert(demux_worker_take(&w,&out,NULL)==1&&out.pts==i);
    demux_worker_resume(&w);wait_count(&w,DEMUX_QUEUE_PACKETS);
    started=SDL_GetTicks();int extension=0;
    while(!extension&&SDL_GetTicks()-started<3000){extension=demux_worker_video_lookahead(&w,0);SDL_Delay(1);}
    assert(extension==1);barrier(&w);
    assert(demux_worker_take(&w,&out,NULL)==1&&out.pts==123);
    // Deterministic races: a stale "full" flag after a slot was freed, and a
    // video arriving between take_stream's scan and lookahead's decision.
    SDL_LockMutex(w.mutex);size_t old_pending=w.pending_bytes;int old_blocked=w.space_blocked;
    w.space_blocked=1;w.pending_bytes=0;SDL_UnlockMutex(w.mutex);
    assert(demux_worker_video_lookahead(&w,0)==0);
    SDL_LockMutex(w.mutex);w.space_blocked=2;w.pending_bytes=w.payload_cap;
    w.queue[w.head].packet->stream_index=0;SDL_UnlockMutex(w.mutex);
    assert(demux_worker_video_lookahead(&w,0)==0);
    SDL_LockMutex(w.mutex);w.queue[w.head].packet->stream_index=1;
    w.space_blocked=old_blocked;w.pending_bytes=old_pending;SDL_UnlockMutex(w.mutex);
    for(int i=124;i<512;i++)assert(demux_worker_take(&w,&out,NULL)==1&&out.pts==i);
    demux_worker_stop(&w);assert(!atomic_load(&live_packets)&&!worker_allocation);
    // A stream with no video within the bounded slot budget returns an explicit
    // limit, not an infinite circular wait or discarded PCM.
    synthetic_audio_burst=10000;packet_bytes=100;atomic_store(&reads,0);
    assert(!demux_worker_start(&w,&fmt,&watch,1));started=SDL_GetTicks();int limit=0;
    while(!limit&&SDL_GetTicks()-started<3000){limit=demux_worker_video_lookahead(&w,0);if(limit>0)limit=0;SDL_Delay(1);}
    assert(limit==-1&&occupancy(&w)==DEMUX_LOOKAHEAD_PACKETS);
    demux_worker_stop(&w);assert(!atomic_load(&live_packets)&&!worker_allocation);
    // Byte-bound pressure cannot be solved by allocating unbounded packet slots.
    synthetic_audio_burst=10000;packet_bytes=1024*1024;atomic_store(&reads,0);
    assert(!demux_worker_start(&w,&fmt,&watch,0));started=SDL_GetTicks();limit=0;
    while(!limit&&SDL_GetTicks()-started<3000){limit=demux_worker_video_lookahead(&w,0);SDL_Delay(1);}
    assert(limit==-1&&w.queue_slots==DEMUX_QUEUE_PACKETS);
    demux_worker_stop(&w);assert(!atomic_load(&live_packets)&&!worker_allocation);
    synthetic_audio_burst=0;fail_worker_alloc=1;
    assert(demux_worker_start(&w,&fmt,&watch,0)==-1&&!w.queue&&!w.thread);
    demux_worker_stop(&w);fail_worker_alloc=0;
    puts("OK actual demux pthread worker: full-byte/full-slot barriers, 25 seeks, stale pending discarded, stop, oversize, sequential cap, audio ahead, no packet leaks");
    puts("PASS bounded video lookahead: 600/1800/3000 audio bursts, wrapped ring, retained order, clear/reset, slot/byte limits and allocation failure; SDK/media input simulated.");
    return 0;
}
