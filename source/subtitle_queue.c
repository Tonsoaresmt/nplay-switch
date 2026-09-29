#include "subtitle_queue.h"
#include <math.h>

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

void subtitle_cue_times(double decoded_pts, double packet_pts,
                        double packet_duration, unsigned start_display_ms,
                        unsigned end_display_ms, double timeline_origin,
                        double *start, double *end) {
    double base = isfinite(decoded_pts) ? decoded_pts :
                  isfinite(packet_pts) ? packet_pts : 0.0;
    base -= isfinite(timeline_origin) ? timeline_origin : 0.0;
    double cue_start = base + start_display_ms / 1000.0;
    double cue_end;
    if (end_display_ms > start_display_ms)
        cue_end = base + end_display_ms / 1000.0;
    else if (isfinite(packet_duration) && packet_duration > 0.01)
        cue_end = base + packet_duration;
    else
        cue_end = cue_start + 4.0;
    if (cue_end <= cue_start) cue_end = cue_start + 4.0;
    if (start) *start = cue_start;
    if (end) *end = cue_end;
}
