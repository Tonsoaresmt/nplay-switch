#include <assert.h>
#include <stdio.h>
typedef int DemuxWorker;
typedef int PlayerOpenDeadline;
typedef int SDL_Renderer;
typedef int SDL_Joystick;
typedef int AVFormatContext;
typedef int AVCodecContext;
typedef int SDL_AudioDeviceID;
typedef int SubtitleQueue;
static int barrier, seek_result, operation, clear_calls, resume_calls, seek_calls;
static int demux_worker_wait_paused(DemuxWorker *w, ...) { (void)w; return barrier; }
static void demux_worker_clear(DemuxWorker *w) { (void)w; ++clear_calls; }
static void demux_worker_resume(DemuxWorker *w) { (void)w; ++resume_calls; }
static void track_operation_begin(PlayerOpenDeadline *w, ...) { (void)w; }
static int track_operation_end(PlayerOpenDeadline *w) { (void)w; return operation; }
static int apply_player_seek(AVFormatContext *fmt, ...) { (void)fmt; ++seek_calls; return seek_result; }
#include "seek_barrier_function.inc"
static int run(double *pos) {
    double wall = 0, audio = 0, ac = 0, acwall = 0;
    clear_calls = resume_calls = seek_calls = 0;
    return player_seek_with_barrier(NULL, NULL, NULL, NULL, "fixture", "seek",
        NULL, 0, 1, NULL, NULL, NULL, 0, 3060, 0, 0,
        &wall, &audio, pos, &ac, &acwall, NULL);
}
int main(void) {
    double pos = 3000;
    barrier = 2; assert(run(&pos) == 5 && pos == 3000 && !clear_calls && !seek_calls);
    barrier = 1; assert(run(&pos) == 1 && !clear_calls && !seek_calls);
    barrier = -1; assert(run(&pos) == -1 && !clear_calls && !seek_calls);
    barrier = 0; seek_result = 0;
    assert(run(&pos) == 0 && clear_calls == 1 && seek_calls == 1 && resume_calls == 1);
    operation = 1; assert(run(&pos) == 4 && pos == 3000 && resume_calls == 1);
    operation = 0; seek_result = -1; assert(run(&pos) == 2 && resume_calls == 1);
    puts("SEEK BARRIER OK: actual helper, busy read is not restart, no unsafe mutation, cancel/failure paths");
}
