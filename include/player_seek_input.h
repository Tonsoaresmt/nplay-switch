#pragma once
#include <stdint.h>
#include <math.h>

// SDL Switch: L/R/ZL/ZR are buttons 6..9; right X is axis 2.
typedef struct { unsigned mask; uint32_t since; int repeating, blocked; } PlayerSeekHold;
static inline int player_seek_button(int b) { return b >= 6 && b <= 9; }
static inline double player_seek_target(double base, double duration, double delta) {
    if (!isfinite(base) || base < 0) base = 0;
    double target = base + delta;
    if (!isfinite(target) || target < 0) target = 0;
    if (isfinite(duration) && duration > 1 && target > duration - 1) target = duration - 1;
    return target;
}
// One repeat per poll, never catch up after a long stall. Opposing buttons do
// nothing. A modal/cancel requires release before repeats can be armed again.
static inline int player_seek_hold_tick(PlayerSeekHold *s, unsigned mask,
                                        uint32_t now, int allowed) {
    mask &= 15u;
    if (!mask) { *s = (PlayerSeekHold){0}; return -1; }
    if (!allowed) { s->blocked = 1; return -1; }
    if (s->blocked) return -1;
    if (mask != s->mask) { s->mask = mask; s->since = now; s->repeating = 0; return -1; }
    if ((mask & (mask - 1u)) || now - s->since < (s->repeating ? 150u : 450u)) return -1;
    s->since = now; s->repeating = 1;
    for (int i = 0; i < 4; i++) if (mask == (1u << i)) return i + 6;
    return -1;
}
static inline int player_seek_stick_arm(int enabled, int allowed, int x,
                                        uint32_t now, int *lock, int *dir,
                                        uint32_t *since) {
    int magnitude = x < 0 ? -x : x; // int, not Sint16: -32768 is valid.
    if (!enabled || !allowed) { *lock = 1; *dir = 0; *since = 0; return 0; }
    if (magnitude < 9000) { *lock = 0; *dir = 0; *since = 0; return 0; }
    if (*lock || magnitude < 24500) { *dir = 0; *since = 0; return 0; }
    int direction = x > 0 ? 1 : -1;
    if (*dir != direction) { *dir = direction; *since = now; return 0; }
    return now - *since >= 550u;
}
