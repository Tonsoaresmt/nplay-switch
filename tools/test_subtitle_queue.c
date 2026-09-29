#include "subtitle_queue.h"

#include <assert.h>
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

    puts("subtitle queue: ok");
    return 0;
}
