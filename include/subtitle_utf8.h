#pragma once
#include <stddef.h>
#include <string.h>
// Valid UTF-8 input: do not split a code point at a bounded copy boundary.
static inline size_t subtitle_utf8_prefix(const char *text, size_t limit) {
    size_t n = strlen(text);
    if (n <= limit) return n;
    n = limit;
    while (n && ((unsigned char)text[n] & 0xc0) == 0x80) n--;
    return n;
}
static inline size_t subtitle_utf8_copy(char *out, size_t cap, const char *text) {
    if (!cap) return 0;
    size_t n = subtitle_utf8_prefix(text, cap - 1);
    memcpy(out, text, n); out[n] = 0; return n;
}
