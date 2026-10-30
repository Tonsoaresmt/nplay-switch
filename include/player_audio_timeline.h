#pragma once
#include <math.h>
#include <stdint.h>

#define PLAYER_PCM_LIMIT_SECONDS 4
#define PLAYER_PCM_GAP_SECONDS 2
static inline int player_audio_ready(int paused, int presented, int preroll) {
    return !paused && presented && !preroll;
}
typedef struct {
    double floor, end;
    int valid;
    uint64_t trimmed, padded;
    unsigned blocks, rejected;
} PlayerAudioTimeline;
typedef struct { int skip, count, silence; double end; } PlayerAudioPlan;
static inline void player_audio_timeline_reset(PlayerAudioTimeline *s, double target) {
    *s = (PlayerAudioTimeline){0};
    s->floor = isfinite(target) && target > 0 ? target : 0;
    s->end = s->floor;
}
// SDL consumes bytes, not PTS. Make every accepted sample contiguous with the
// queue's end; floor survives the first video frame. Pure, transactional plan.
static inline int player_audio_plan(const PlayerAudioTimeline *s, double pts,
    int samples, int rate, int channels, double position, unsigned queued,
    PlayerAudioPlan *out) {
    if (!s || !out || samples <= 0 || rate <= 0 || rate > 192000 ||
        channels < 1 || channels > 8) return -2;
    double expected = s->valid ? s->end : s->floor;
    if (!queued && isfinite(position) && position > expected) expected = position;
    if (!isfinite(expected)) return -2;
    if (!isfinite(pts)) pts = expected;
    double cut = expected - pts;
    int skip = 0;
    if (cut > 1.5 / rate) {
        double n = ceil(cut * rate - 1e-6);
        if (n >= samples) return 0;
        skip = (int)n;
        pts += skip / (double)rate;
    }
    double gap = pts - expected;
    if (gap > PLAYER_PCM_GAP_SECONDS) return -2;
    int silence = gap > 1.5 / rate ? (int)llround(gap * rate) : 0;
    int count = samples - skip;
    uint64_t bytes = ((uint64_t)silence + (unsigned)count) * (unsigned)channels * 2u;
    uint64_t cap = (uint64_t)rate * channels * 2u * PLAYER_PCM_LIMIT_SECONDS;
    if (bytes + queued > cap) return -2;
    *out = (PlayerAudioPlan){skip,count,silence,expected + (silence + count)/(double)rate};
    return 1;
}
// Retain compressed audio in the existing bounded demux queue instead of
// decoding/dropping it while SDL is paused for preroll. EOF must still drain.
static inline int player_audio_video_first(unsigned queued_ms, int preroll, int terminal) {
    return !terminal && queued_ms >= (preroll ? 350u : 1000u);
}
