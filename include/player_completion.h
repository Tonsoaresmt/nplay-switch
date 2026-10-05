#pragma once
#include <math.h>

// EOF is a transport/demux event, not proof that the whole VOD was watched.
// Unknown duration keeps legacy EOF semantics only without an I/O failure.
static inline int player_eof_complete(double position, double duration, int io_failed) {
    if (io_failed || !isfinite(position) || position < 0) return 0;
    if (!isfinite(duration) || duration <= 0) return 1;
    double tolerance = duration * 0.02;
    if (tolerance < 2.0) tolerance = 2.0;
    if (tolerance > 10.0) tolerance = 10.0;
    return position >= duration - tolerance;
}

// A paused reopen still needs to decode the new point once, without playing
// its audio. After presenting the new generation it must stop consuming.
static inline int player_pause_holds_decode(int paused, int presented, int preroll) {
    return paused && presented && !preroll;
}
