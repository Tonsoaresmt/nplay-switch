/* Mandatory regressions against the real queue/store implementation. */
#include "subtitle_queue.h"
#include "subtitle_store.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures, checks;
static void check(int ok, const char *name) {
    checks++;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) failures++;
}
int main(void) {
    SubtitlePlacement a, b;
    const char settings[] = "position:25% line:24.6% align:centerTRAILER";
    check(subtitle_settings_parse(settings, sizeof(settings) - 1 - 7, &a) &&
          fabsf(a.x - 25) < .001f && fabsf(a.y - 24.6f) < .001f &&
          a.halign == 1 && a.valign == 0,
          "non-NUL FFmpeg settings, percent top and center anchor");
    b = a; b.x = 70;
    SubtitleStore store = {0};
    SubtitleSigns signs = {0};
    subtitle_store_add(&store, 10, 12, "A Yamada pulando uma refeicao?");
    subtitle_store_add_at(&store, 10, 12, "nhac nhac", &a);
    subtitle_store_add_at(&store, 10, 12, "chup chup", &b);
    subtitle_store_signs(&store, 11, &signs);
    check(signs.count == 2 && !strcmp(subtitle_store_text(&store, 11),
          "A Yamada pulando uma refeicao?"), "signs separated from dialogue");
    signs.count = 0; subtitle_store_signs(&store, 13, &signs);
    check(signs.count == 0, "expired signs hidden");
    signs.count = 0; subtitle_store_signs(&store, 11, &signs);
    check(signs.count == 2, "backward lookup preserves signs");
    subtitle_store_free(&store);

    subtitle_store_add_at(&store, 10, 12, "nhac nhac", &a);
    subtitle_store_add_at(&store, 10, 12, "nhac nhac", &b);
    signs.count = 0; subtitle_store_signs(&store, 11, &signs);
    printf("  distinct placements: stored=%d displayed=%d expected=2\n",
           subtitle_store_count(&store), signs.count);
    check(signs.count == 2, "identical text at different positions must both display");
    subtitle_store_free(&store);

    SubtitleQueue queue; subtitle_queue_reset(&queue);
    subtitle_queue_push_at(&queue, 10, 12, "OK", &a);
    subtitle_queue_push(&queue, 10, 12, "OK");
    signs.count = 0; subtitle_queue_signs(&queue, 11, &signs);
    const char *speech = subtitle_queue_text(&queue, 11);
    printf("  sign+speech: queued=%d signs=%d dialogue='%s'\n", queue.count, signs.count, speech);
    check(signs.count == 1 && !strcmp(speech, "OK"),
          "embedded queue preserves sign and dialogue with same timing/text");

    subtitle_store_add_at(&store, 0, 2, "Placa persistente", &a);
    for (int i = 0; i < 20; i++) {
        char cue[32]; snprintf(cue, sizeof(cue), "Posterior %d", i);
        subtitle_store_add(&store, 20 + i, 21 + i, cue);
    }
    subtitle_store_add_at(&store, 0, 10, "Placa persistente", &a);
    double start, end; const char *text;
    subtitle_store_get(&store, 0, &start, &end, &text, NULL);
    signs.count = 0; subtitle_store_signs(&store, 8, &signs);
    printf("  replay extension: end=%.1f lookup_horizon=%.1f signs_at_8=%d\n",
           end, store.max_short, signs.count);
    check(end == 10 && signs.count == 1,
          "out-of-order extension remains visible until new end");
    subtitle_store_free(&store);

    subtitle_queue_reset(&queue);
    subtitle_queue_push(&queue, 0, 20, "Fala nao pode desaparecer");
    for (int i = 0; i < SUBTITLE_QUEUE_CAP; i++) {
        char cue[32]; snprintf(cue, sizeof(cue), "Letreiro %d", i);
        subtitle_queue_push_at(&queue, .1 + i * .01, 10, cue, &a);
    }
    check(!strcmp(subtitle_queue_text(&queue, 1), "Fala nao pode desaparecer"),
          "embedded positioned-cue burst must not evict active dialogue");
    // Exercise both insertion orders and the unchanged full dialogue capacity.
    subtitle_queue_reset(&queue);
    subtitle_queue_push(&queue, 10, 12, "OK");
    subtitle_queue_push_at(&queue, 10, 12, "OK", &a);
    subtitle_queue_push_at(&queue, 10, 12, "OK", &b);
    subtitle_queue_push_at(&queue, 10, 12, "OK", &b);
    signs.count = 0; subtitle_queue_signs(&queue, 11, &signs);
    check(queue.count == 3 && signs.count == 2 &&
          !strcmp(subtitle_queue_text(&queue, 11), "OK"),
          "reverse insertion order, distinct placements and exact layer dedup");
    subtitle_queue_reset(&queue);
    for (int i = 0; i < SUBTITLE_QUEUE_CAP; i++) {
        char cue[32]; snprintf(cue, sizeof(cue), "Fala %d", i);
        subtitle_queue_push(&queue, 0, 20, cue);
    }
    subtitle_queue_push_at(&queue, 0, 20, "Placa extra", &a);
    check(queue.count == SUBTITLE_QUEUE_CAP &&
          strstr(subtitle_queue_text(&queue, 1), "Fala 0\n") != NULL,
          "full dialogue-only capacity cannot be evicted by a sign");
    subtitle_queue_reset(&queue);
    subtitle_queue_push(&queue, 0, 20, "Fala protegida");
    int bounded = 1;
    for (int i = 0; i < 10000; i++) {
        char cue[32]; snprintf(cue, sizeof(cue), "Placa %d", i);
        subtitle_queue_push_at(&queue, 0, 20, cue, &a);
        signs.count = 0; subtitle_queue_signs(&queue, 1, &signs);
        if (queue.count > SUBTITLE_QUEUE_CAP || signs.count > SUBTITLE_SIGNS_MAX ||
            strcmp(subtitle_queue_text(&queue, 1), "Fala protegida")) bounded = 0;
    }
    check(bounded, "10000-sign burst: fixed memory, bounded signs, protected dialogue");
    printf("POSITIONED SUBTITLES: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
