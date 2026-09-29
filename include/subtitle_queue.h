#pragma once

#include <stddef.h>

#define SUBTITLE_QUEUE_CAP 8
#define SUBTITLE_TEXT_CAP 512
#define SUBTITLE_COMPOSED_CAP 1024

typedef struct {
    double start;
    double end;
    char text[SUBTITLE_TEXT_CAP];
} SubtitleCue;

typedef struct {
    SubtitleCue cues[SUBTITLE_QUEUE_CAP];
    int count;
    char composed[SUBTITLE_COMPOSED_CAP];
} SubtitleQueue;

void subtitle_queue_reset(SubtitleQueue *queue);
void subtitle_queue_push(SubtitleQueue *queue, double start, double end,
                         const char *text);
const char *subtitle_queue_text(SubtitleQueue *queue, double position);
