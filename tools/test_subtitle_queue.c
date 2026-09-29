#include "subtitle_queue.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    SubtitleQueue queue;
    subtitle_queue_reset(&queue);

    subtitle_queue_push(&queue, 10.5, 12.5, "Primeira fala");
    assert(!strcmp(subtitle_queue_text(&queue, 10.0), ""));
    assert(!strcmp(subtitle_queue_text(&queue, 10.5), "Primeira fala"));

    subtitle_queue_push(&queue, 11.0, 13.0, "Fala sobreposta");
    assert(!strcmp(subtitle_queue_text(&queue, 11.5),
                   "Primeira fala\nFala sobreposta"));
    assert(!strcmp(subtitle_queue_text(&queue, 12.75), "Fala sobreposta"));
    assert(!strcmp(subtitle_queue_text(&queue, 13.1), ""));

    subtitle_queue_push(&queue, 20.0, 20.0, "Duracao ausente");
    assert(!strcmp(subtitle_queue_text(&queue, 22.0), "Duracao ausente"));
    subtitle_queue_reset(&queue);
    assert(!strcmp(subtitle_queue_text(&queue, 22.0), ""));

    // Um segmento HLS pode publicar mais de oito cues antes do primeiro deles
    // chegar a tela. A fila nao pode expulsar essas falas prematuramente.
    for (int i = 0; i < 20; i++) {
        char cue[32];
        snprintf(cue, sizeof(cue), "Fala %d", i);
        subtitle_queue_push(&queue, 30.0 + i * 0.4, 31.0 + i * 0.4, cue);
    }
    assert(!strcmp(subtitle_queue_text(&queue, 30.1), "Fala 0"));
    subtitle_queue_reset(&queue);

    double start = 0, end = 0;
    subtitle_cue_times(12.0, 99.0, 2.5, 0, 0, 1.0, &start, &end);
    assert(fabs(start - 11.0) < 0.00001);
    assert(fabs(end - 13.5) < 0.00001);
    subtitle_cue_times(NAN, 20.0, 0.0, 250, 2250, 2.0, &start, &end);
    assert(fabs(start - 18.25) < 0.00001);
    assert(fabs(end - 20.25) < 0.00001);

    puts("subtitle queue: ok");
    return 0;
}
