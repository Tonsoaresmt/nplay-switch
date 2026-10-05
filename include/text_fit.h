#pragma once
#include <stddef.h>
#include <string.h>

typedef int (*TextFitMeasure)(const char *, int, int *, int *);
// Bounded, UTF-8 boundary-safe ellipsis. Returns 0 on measurement/cap failure
// so the caller can retain its existing clipped rendering fallback.
static inline int text_fit_build(const char *s, char *out, size_t cap,
                                 int style, int maxw, TextFitMeasure measure) {
    if (!s || !out || cap < 4 || maxw <= 0) return 0;
    size_t len = strlen(s);
    if (len >= cap) return 0;
    int w = 0, h = 0;
    if (!len) { out[0] = 0; return 1; }
    if (measure(s, style, &w, &h) != 0) return 0;
    if (w <= maxw) { memcpy(out, s, len + 1); return 1; }
    if (measure("...", style, &w, &h) != 0) return 0;
    if (w > maxw) { out[0] = 0; return 1; }
    size_t lo = 0, hi = len, best = 0;
    while (lo <= hi) {
        size_t mid = lo + (hi - lo) / 2, cut = mid;
        while (cut && ((unsigned char)s[cut] & 0xc0) == 0x80) cut--;
        if (cut + 4 > cap) { hi = mid ? mid - 1 : 0; continue; }
        memcpy(out, s, cut); memcpy(out + cut, "...", 4);
        if (measure(out, style, &w, &h) != 0) return 0;
        if (w <= maxw) { best = cut; lo = mid + 1; }
        else { if (!mid) break; hi = mid - 1; }
    }
    memcpy(out, s, best); memcpy(out + best, "...", 4);
    return 1;
}
