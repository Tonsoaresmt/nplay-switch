#include "subtitle_store.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
int main(void) {
    SubtitleStore store = {0};
    int failures = 0;
    assert(subtitle_store_add(&store, 0, 30, "LOJA ABERTA"));
    assert(subtitle_store_add(&store, 1, 3, "Oi"));
    const char *shown = subtitle_store_text(&store, 2);
    if (!strstr(shown, "Oi")) { puts("FAIL: legitimate short dialogue hidden by sign"); failures++; }
    subtitle_store_free(&store);
    assert(subtitle_store_add(&store, 0, 2, "Fala antiga"));
    for (int i = 0; i < 20; i++) {
        char label[32]; snprintf(label, sizeof(label), "Outra fala %d", i);
        assert(subtitle_store_add(&store, 20 + i * 3, 22 + i * 3, label));
    }
    assert(subtitle_store_add(&store, 0, 10, "Fala antiga"));
    if (strcmp(subtitle_store_text(&store, 8), "Fala antiga")) { puts("FAIL: replay extension missing before cue end"); failures++; }
    assert(!strcmp(subtitle_store_text(&store, 10.1), ""));
    subtitle_store_free(&store);
    // Replayed cues can arrive far outside the 16-cue recent merge window.
    for (int i = 0; i < 200; i++) {
        char label[32]; snprintf(label, sizeof(label), "Dialogo %d", i);
        assert(subtitle_store_add(&store, i * 20, i * 20 + 2, label));
    }
    for (int k = 0; k < 200; k++) {
        int i = k * 73 % 200; // Deterministic permutation, not chronological.
        char label[32]; snprintf(label, sizeof(label), "Dialogo %d", i);
        assert(subtitle_store_add(&store, i * 20, i * 20 + 10, label));
    }
    assert(subtitle_store_count(&store) == 200);
    for (int i = 0; i < 200; i++) {
        char label[32]; snprintf(label, sizeof(label), "Dialogo %d", i);
        assert(!strcmp(subtitle_store_text(&store, i * 20 + 8), label));
    }
    subtitle_store_free(&store);
    if (failures) return 1;
    puts("SUBTITLE EDGES OK: short dialogue overlap, out-of-order extension, cue expiry, 200 shuffled replays");
    return 0;
}
