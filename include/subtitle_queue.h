#pragma once

#include <stddef.h>

// Uma rendition WebVTT pode entregar todos os cues de um segmento de uma vez.
// O teto antigo de 8 removia as primeiras falas antes que o relogio chegasse
// nelas em segmentos longos/com dialogo rapido. Mantemos os mesmos 32 slots;
// letreiros usam no maximo oito e nunca expulsam falas para entrar.
#define SUBTITLE_QUEUE_CAP 32
#define SUBTITLE_TEXT_CAP 512
#define SUBTITLE_COMPOSED_CAP 1024

// Letreiro posicionado (placa, onomatopeia, creditos do fansub). O backend
// converte o ASS para WebVTT com `position:X% line:Y% align:A`; o FFmpeg
// entrega essas configuracoes como AV_PKT_DATA_WEBVTT_SETTINGS. Sem posicao,
// o cue e fala comum e vai para o bloco de baixo.
typedef struct {
    float x, y;                 // % do quadro de video (ponto de ancora)
    unsigned char positioned;   // 0 = fala comum
    unsigned char halign;       // ancora x: 0 esquerda, 1 centro, 2 direita
    unsigned char valign;       // ancora y: 0 topo, 1 meio, 2 base
} SubtitlePlacement;

#define SUBTITLE_SIGNS_MAX 8
#define SUBTITLE_SIGN_TEXT 192

typedef struct {
    SubtitlePlacement at;
    char text[SUBTITLE_SIGN_TEXT];
} SubtitleSign;

typedef struct {
    int count;
    SubtitleSign items[SUBTITLE_SIGNS_MAX];
} SubtitleSigns;

// Le as configuracoes de um cue WebVTT. So `line:` em % marca letreiro (como
// no player web): `line` inteiro e contagem de linhas, sem quadro de
// referencia. Retorna placement->positioned.
int subtitle_settings_parse(const char *settings, size_t len, SubtitlePlacement *placement);
// Acrescenta um letreiro; so texto e posicionamento iguais sao duplicatas.
void subtitle_signs_add(SubtitleSigns *signs, const SubtitlePlacement *at,
                        const char *text, size_t len);

typedef struct {
    double start;
    double end;
    SubtitlePlacement at;
    char text[SUBTITLE_TEXT_CAP];
} SubtitleCue;

typedef struct {
    SubtitleCue cues[SUBTITLE_QUEUE_CAP];
    int count;
    char composed[SUBTITLE_COMPOSED_CAP];
} SubtitleQueue;

void subtitle_queue_reset(SubtitleQueue *queue);
void subtitle_queue_advance(SubtitleQueue *queue, double position);
void subtitle_queue_push(SubtitleQueue *queue, double start, double end,
                         const char *text);
void subtitle_queue_push_at(SubtitleQueue *queue, double start, double end,
                            const char *text, const SubtitlePlacement *at);
// Falas (sem posicao) a exibir embaixo.
const char *subtitle_queue_text(SubtitleQueue *queue, double position);
// Letreiros posicionados ativos em `position` (acrescenta a `signs`).
void subtitle_queue_signs(const SubtitleQueue *queue, double position, SubtitleSigns *signs);

// Resolve timestamps do decoder. decoded_pts/packet_pts estao em segundos;
// use NAN quando ausentes. A duracao do pacote e o fallback preferencial para
// WebVTT, cujo decoder normalmente deixa end_display_time zerado.
void subtitle_cue_times(double decoded_pts, double packet_pts,
                        double packet_duration, unsigned start_display_ms,
                        unsigned end_display_ms, double timeline_origin,
                        double *start, double *end);
