#include "subtitle_queue.h"
#include <math.h>

#include <stdio.h>
#include <stdlib.h>
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

static int token_is(const char *value, size_t len, const char *word) {
    return strlen(word) == len && !strncmp(value, word, len);
}

int subtitle_settings_parse(const char *settings, size_t len, SubtitlePlacement *placement) {
    if (!placement) return 0;
    memset(placement, 0, sizeof(*placement));
    if (!settings) return 0;
    int has_line = 0, has_position = 0, pos_align = -1, align = 1;
    float x = 50, y = 0;
    size_t i = 0;
    while (i < len) {
        while (i < len && (settings[i] == ' ' || settings[i] == '\t' || settings[i] == '\n' ||
                           settings[i] == '\r')) i++;
        size_t start = i;
        while (i < len && settings[i] != ' ' && settings[i] != '\t' && settings[i] != '\n' &&
               settings[i] != '\r') i++;
        size_t tlen = i - start;
        const char *token = settings + start;
        const char *colon = memchr(token, ':', tlen);
        if (!colon) continue;
        size_t klen = (size_t)(colon - token);
        const char *value = colon + 1;
        size_t vlen = tlen - klen - 1;
        const char *comma = memchr(value, ',', vlen);
        size_t main_len = comma ? (size_t)(comma - value) : vlen;
        const char *extra = comma ? comma + 1 : NULL;
        size_t extra_len = comma ? vlen - main_len - 1 : 0;
        char number[24];
        int percent = main_len > 1 && main_len < sizeof(number) && value[main_len - 1] == '%';
        if (percent) { memcpy(number, value, main_len - 1); number[main_len - 1] = 0; }
        if (token_is(token, klen, "line")) {
            char *end = NULL;
            double v = percent ? strtod(number, &end) : 0;
            if (percent && end && !*end && v >= 0 && v <= 100) { has_line = 1; y = (float)v; }
            if (extra && token_is(extra, extra_len, "center")) placement->valign = 1;
            else if (extra && token_is(extra, extra_len, "end")) placement->valign = 2;
        } else if (token_is(token, klen, "position")) {
            char *end = NULL;
            double v = percent ? strtod(number, &end) : 0;
            if (percent && end && !*end && v >= 0 && v <= 100) { has_position = 1; x = (float)v; }
            if (extra && (token_is(extra, extra_len, "line-left") || token_is(extra, extra_len, "start")))
                pos_align = 0;
            else if (extra && token_is(extra, extra_len, "center")) pos_align = 1;
            else if (extra && (token_is(extra, extra_len, "line-right") || token_is(extra, extra_len, "end")))
                pos_align = 2;
        } else if (token_is(token, klen, "align")) {
            if (token_is(value, vlen, "left") || token_is(value, vlen, "start")) align = 0;
            else if (token_is(value, vlen, "right") || token_is(value, vlen, "end")) align = 2;
            else align = 1;
        }
    }
    if (!has_line) { memset(placement, 0, sizeof(*placement)); return 0; }
    placement->positioned = 1;
    placement->halign = (unsigned char)(pos_align >= 0 ? pos_align : align);
    placement->x = has_position ? x : placement->halign * 50.0f;
    placement->y = y;
    return 1;
}

// Coordinates in the packed store have 0.01% precision. Half a unit is
// below a tenth of a pixel at 1280px; never merge distinct on-screen signs.
static int same_placement(const SubtitlePlacement *a, const SubtitlePlacement *b) {
    int ap = a && a->positioned, bp = b && b->positioned;
    if (!ap || !bp) return ap == bp;
    return a->halign == b->halign && a->valign == b->valign &&
           fabsf(a->x - b->x) < 0.005f && fabsf(a->y - b->y) < 0.005f;
}

void subtitle_signs_add(SubtitleSigns *signs, const SubtitlePlacement *at,
                        const char *text, size_t len) {
    if (!signs || !at || !text || signs->count >= SUBTITLE_SIGNS_MAX) return;
    if (len >= SUBTITLE_SIGN_TEXT) {
        len = SUBTITLE_SIGN_TEXT - 1;
        while (len && ((unsigned char)text[len] & 0xC0) == 0x80) len--; // UTF-8 inteiro
    }
    // Only effect layers at the same position are duplicates.
    for (int i = 0; i < signs->count; i++)
        if (same_placement(&signs->items[i].at, at) &&
            strlen(signs->items[i].text) == len && !memcmp(signs->items[i].text, text, len)) return;
    SubtitleSign *sign = &signs->items[signs->count++];
    sign->at = *at;
    memcpy(sign->text, text, len);
    sign->text[len] = 0;
}

void subtitle_queue_push(SubtitleQueue *queue, double start, double end,
                         const char *text) {
    subtitle_queue_push_at(queue, start, end, text, NULL);
}

void subtitle_queue_push_at(SubtitleQueue *queue, double start, double end,
                            const char *text, const SubtitlePlacement *at) {
    if (!queue || !text || !text[0]) return;
    if (start < 0) start = 0;
    if (end <= start) end = start + 4.0;

    for (int i = 0; i < queue->count; i++) {
        SubtitleCue *cue = &queue->cues[i];
        if (cue->start == start && cue->end == end &&
            same_placement(&cue->at, at) && !strcmp(cue->text, text))
            return;
    }

    // Share the same fixed 32 slots. Signs may use at most eight, and can
    // never evict dialogue. Dialogue can use all slots when there are no
    // signs; on pressure it reclaims a sign before replacing old dialogue.
    int first_sign = -1, sign_count = 0, remove = -1;
    for (int i = 0; i < queue->count; i++) {
        if (!queue->cues[i].at.positioned) continue;
        if (first_sign < 0) first_sign = i;
        sign_count++;
    }
    int incoming_sign = at && at->positioned;
    if (incoming_sign && sign_count >= SUBTITLE_SIGNS_MAX) remove = first_sign;
    else if (queue->count == SUBTITLE_QUEUE_CAP) {
        if (incoming_sign && first_sign < 0) return;
        remove = first_sign >= 0 ? first_sign : 0;
    }
    if (remove >= 0) {
        memmove(&queue->cues[remove], &queue->cues[remove + 1],
                sizeof(queue->cues[0]) * (size_t)(queue->count - remove - 1));
        queue->count--;
    }

    SubtitleCue *cue = &queue->cues[queue->count++];
    cue->start = start;
    cue->end = end;
    if (at && at->positioned) cue->at = *at; else memset(&cue->at, 0, sizeof(cue->at));
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
        if (cue->at.positioned) continue;   // letreiro: desenhado no lugar dele
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

void subtitle_queue_signs(const SubtitleQueue *queue, double position, SubtitleSigns *signs) {
    if (!queue || !signs) return;
    for (int i = 0; i < queue->count; i++) {
        const SubtitleCue *cue = &queue->cues[i];
        if (!cue->at.positioned || position + 0.05 < cue->start || position >= cue->end) continue;
        subtitle_signs_add(signs, &cue->at, cue->text, strlen(cue->text));
    }
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
