#include "subtitle_queue.h"

#include <stdio.h>
#include <string.h>

void subtitle_queue_reset(SubtitleQueue *queue) {
    if (!queue) return;
    memset(queue, 0, sizeof(*queue));
}

static void subtitle_queue_sort(SubtitleQueue *queue) {
    for (int i = 1; i < queue->count; i++) {
        SubtitleCue cue = queue->cues[i];
        int j = i - 1;
        while (j >= 0 && queue->cues[j].start > cue.start) {
            queue->cues[j + 1] = queue->cues[j];
            j--;
        }
        queue->cues[j + 1] = cue;
    }
}

void subtitle_queue_push(SubtitleQueue *queue, double start, double end,
                         const char *text) {
    if (!queue || !text || !text[0]) return;
    if (start < 0) start = 0;
    if (end <= start) end = start + 4.0;

    for (int i = 0; i < queue->count; i++) {
        SubtitleCue *cue = &queue->cues[i];
        if (cue->start == start && cue->end == end && !strcmp(cue->text, text))
            return;
    }

    if (queue->count == SUBTITLE_QUEUE_CAP) {
        memmove(&queue->cues[0], &queue->cues[1],
                sizeof(queue->cues[0]) * (SUBTITLE_QUEUE_CAP - 1));
        queue->count--;
    }

    SubtitleCue *cue = &queue->cues[queue->count++];
    cue->start = start;
    cue->end = end;
    snprintf(cue->text, sizeof(cue->text), "%s", text);
    subtitle_queue_sort(queue);
}

const char *subtitle_queue_text(SubtitleQueue *queue, double position) {
    if (!queue) return "";

    int keep = 0;
    for (int i = 0; i < queue->count; i++) {
        if (queue->cues[i].end > position - 0.25) {
            if (keep != i) queue->cues[keep] = queue->cues[i];
            keep++;
        }
    }
    queue->count = keep;
    queue->composed[0] = 0;

    for (int i = 0; i < queue->count; i++) {
        const SubtitleCue *cue = &queue->cues[i];
        if (position + 0.05 < cue->start || position >= cue->end) continue;
        size_t used = strlen(queue->composed);
        size_t remaining = sizeof(queue->composed) - used;
        if (remaining <= 1) break;
        if (used) {
            strncat(queue->composed, "\n", remaining - 1);
            used++;
            remaining = sizeof(queue->composed) - used;
        }
        strncat(queue->composed, cue->text, remaining - 1);
    }
    return queue->composed;
}
