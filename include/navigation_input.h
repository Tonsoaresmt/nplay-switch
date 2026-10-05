#pragma once
#include <stdint.h>

// Press edges are never replaced by held-state polling: a down/up pair may
// both arrive between two frames. After a route change, require neutral.
typedef struct { int direction, blocked; uint32_t next; } NavigationRepeat;
static inline void navigation_repeat_block(NavigationRepeat *n) {
    n->direction = -1; n->blocked = 1;
}
static inline void navigation_repeat_press(NavigationRepeat *n, int dir, uint32_t now) {
    n->blocked = 0; n->direction = dir; n->next = now + 380;
}
static inline int navigation_repeat_poll(NavigationRepeat *n, int dir, uint32_t now) {
    if (dir < 0) { n->direction = -1; n->blocked = 0; return -1; }
    if (n->blocked) return -1;
    if (dir != n->direction) { navigation_repeat_press(n, dir, now); return dir; }
    if ((int32_t)(now - n->next) < 0) return -1;
    n->next = now + 55; // one repeat per frame; never catch up in a burst
    return dir;
}
