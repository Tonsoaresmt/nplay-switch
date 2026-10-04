#pragma once
#include <math.h>

// A completed title is a replay, not a resume at its saved ending. Keep the
// stored history intact; only normalize this new playback request.
static inline double playback_resume_position(double position, int completed) {
    return completed || !isfinite(position) || position < 0 ? 0 : position;
}
