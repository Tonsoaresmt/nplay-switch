#pragma once

#include <stddef.h>

// Uma rendition WebVTT pode entregar todos os cues de um segmento de uma vez.
// O teto antigo de 8 removia as primeiras falas antes que o relogio chegasse
// nelas em segmentos longos/com dialogo rapido. 32 cues usam cerca de 17 KB.
#define SUBTITLE_QUEUE_CAP 32
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

// Resolve timestamps do decoder. decoded_pts/packet_pts estao em segundos;
// use NAN quando ausentes. A duracao do pacote e o fallback preferencial para
// WebVTT, cujo decoder normalmente deixa end_display_time zerado.
void subtitle_cue_times(double decoded_pts, double packet_pts,
                        double packet_duration, unsigned start_display_ms,
                        unsigned end_display_ms, double timeline_origin,
                        double *start, double *end);
