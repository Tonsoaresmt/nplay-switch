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

    // Configuracoes WebVTT do backend (ass-webvtt.js): so `line:` em % marca
    // letreiro; a ancora x vem de align, a vertical e o topo.
    SubtitlePlacement at;
    const char *sign = "position:25% line:24.6% align:center";
    assert(subtitle_settings_parse(sign, strlen(sign), &at));
    assert(fabsf(at.x - 25.0f) < 0.001f && fabsf(at.y - 24.6f) < 0.001f);
    assert(at.halign == 1 && at.valign == 0);
    const char *left = "position:15.6% line:37% align:left";
    assert(subtitle_settings_parse(left, strlen(left), &at) && at.halign == 0);
    const char *right = "align:right line:90.4%";   // sem position: borda pela ancora
    assert(subtitle_settings_parse(right, strlen(right), &at) && at.halign == 2 && at.x == 100.0f);
    const char *full = "position:80%,line-right line:50%,center align:left";
    assert(subtitle_settings_parse(full, strlen(full), &at) && at.halign == 2 && at.valign == 1);
    // Sem `line:` em %: fala comum (inclusive `line` inteiro e so position).
    const char *plain[] = { "", "align:center", "position:50%", "line:-1", "line:abc%", "line:150%" };
    for (unsigned i = 0; i < sizeof(plain) / sizeof(plain[0]); i++)
        assert(!subtitle_settings_parse(plain[i], strlen(plain[i]), &at) && !at.positioned);
    assert(!subtitle_settings_parse(NULL, 0, &at));
    // Side data nao termina em NUL: o tamanho e respeitado.
    char raw[64]; memcpy(raw, "line:30%XXXX", 12);
    assert(subtitle_settings_parse(raw, 8, &at) && fabsf(at.y - 30.0f) < 0.001f);

    // Letreiro posicionado nao entra na fala; vai para a lista de letreiros.
    SubtitlePlacement nhac;
    subtitle_settings_parse(sign, strlen(sign), &nhac);
    subtitle_queue_push_at(&queue, 40.0, 42.0, "nhac nhac", &nhac);
    subtitle_queue_push_at(&queue, 40.0, 42.0, "nhac nhac", &nhac);
    subtitle_queue_push(&queue, 40.0, 43.0, "A Yamada pulando uma refeicao?");
    assert(!strcmp(subtitle_queue_text(&queue, 41.0), "A Yamada pulando uma refeicao?"));
    SubtitleSigns signs = {0};
    subtitle_queue_signs(&queue, 41.0, &signs);
    assert(signs.count == 1 && !strcmp(signs.items[0].text, "nhac nhac"));
    assert(fabsf(signs.items[0].at.x - 25.0f) < 0.001f);
    signs.count = 0;
    subtitle_queue_signs(&queue, 42.5, &signs);
    assert(signs.count == 0);
    // Teto de letreiros por quadro e texto repetido uma vez.
    for (int i = 0; i < 20; i++) subtitle_signs_add(&signs, &nhac, i % 2 ? "a" : "b", 1);
    assert(signs.count == 2);
    for (int i = 0; i < 20; i++) { char t[8]; snprintf(t, sizeof(t), "s%d", i); subtitle_signs_add(&signs, &nhac, t, strlen(t)); }
    assert(signs.count == SUBTITLE_SIGNS_MAX);
    subtitle_queue_reset(&queue);

    puts("subtitle queue: ok");
    return 0;
}
