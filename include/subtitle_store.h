// subtitle_store.h - cues de legenda externa (WebVTT do R2, torrent) em memoria.
//
// Fansubs de anime convertidos de ASS trazem karaoke e typesetting como
// dezenas de milhares de eventos. O armazenamento anterior guardava 512 bytes
// por cue e parava em 8192: a partir dai NENHUMA fala aparecia (no host, um
// episodio com karaoke ficava sem legenda depois da abertura). Aqui cada cue
// ocupa 16 bytes e o texto vai para uma area compartilhada.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "subtitle_queue.h"

#define SUBTITLE_STORE_LONG_SECONDS 60.0
#define SUBTITLE_STORE_MAX_CUES 200000
#define SUBTITLE_STORE_MAX_TEXT (4u * 1024u * 1024u)

typedef struct {
    float start, end;
    uint32_t text;      // deslocamento na area de texto (terminado em NUL)
    uint16_t len;
    uint16_t flags;
} SubtitleStoreCue;

typedef struct {
    SubtitleStoreCue *cues;   // duracao <= 60 s, ordenados pelo inicio
    int count, capacity;
    SubtitleStoreCue *longs;  // placas longas (raras), varridas inteiras
    int long_count, long_capacity;
    char *text;
    size_t text_len, text_cap;
    float max_short;          // maior duracao entre os curtos: limita a busca
    int merged, skipped, truncated;
    char composed[SUBTITLE_COMPOSED_CAP];
} SubtitleStore;

// 1 = guardado, fundido com um cue igual adjacente ou ignorado de proposito
// (vazio, desenho vetorial do ASS). 0 = limite ou memoria: o cue foi perdido.
int subtitle_store_add(SubtitleStore *store, double start, double end, const char *text);
int subtitle_store_count(const SubtitleStore *store);
// Itera curtos e depois longos. Retorna 0 fora do intervalo.
int subtitle_store_get(const SubtitleStore *store, int index, double *start,
                       double *end, const char **text);
// Texto a exibir em `position`. Repetidos saem uma vez; com muitos cues ao
// mesmo tempo, fragmentos de karaoke e placas longas cedem lugar a fala
// (no maximo 4 linhas, o limite do HUD).
const char *subtitle_store_text(SubtitleStore *store, double position);
void subtitle_store_move(SubtitleStore *dst, SubtitleStore *src);
void subtitle_store_free(SubtitleStore *store);
// Comandos de desenho do ASS ("m 0 0 l 100 0 ...") viram texto na conversao
// para WebVTT e apareciam como lixo na tela.
int subtitle_text_is_drawing(const char *text);
