#pragma once
#include <SDL.h>

typedef struct { int type; double start, end; } IntroSegment; // 1 opening, 2 ending
typedef struct { int count; IntroSegment ranges[2]; } IntroSegments;
typedef struct {
    SDL_Thread *thread;
    SDL_atomic_t cancel, done;
    int item_id, source_id, attempted;
    double duration;
    char bearer[640];
    IntroSegments pending, ready;
} IntroSkip;

// Strict, optional metadata. Invalid/ambiguous ranges stay unavailable.
int intro_skip_parse(const char *json, double duration, IntroSegments *out);
double intro_skip_target(const IntroSegment *range, double duration);
// Owner thread only. One request per attempt; no wait until worker has finished.
void intro_skip_poll(IntroSkip *job, int item_id, int source_id, double duration,
                     const char *bearer);
const IntroSegment *intro_skip_current(const IntroSkip *job, double position,
                                       double duration, int enabled);
void intro_skip_stop(IntroSkip *job);

// Shared draw/hit geometry, centered at 640. Above timeline, below dialogue.
#define INTRO_BUTTON_X 450
#define INTRO_BUTTON_Y 508
#define INTRO_BUTTON_W 380
#define INTRO_BUTTON_H 68
static inline int intro_skip_hit(int x, int y) {
    return x >= INTRO_BUTTON_X && x < INTRO_BUTTON_X + INTRO_BUTTON_W &&
           y >= INTRO_BUTTON_Y && y < INTRO_BUTTON_Y + INTRO_BUTTON_H;
}
