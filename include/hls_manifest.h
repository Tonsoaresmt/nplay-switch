// Leitura minima e sem alocacoes do manifesto master HLS.
#pragma once
#include <stddef.h>

// Retorna 1 se encontrou EXT-X-MEDIA e conta renditions de audio/legenda.
int hls_manifest_media_counts(const char *body, size_t len,
                              int *audio, int *subtitles);

#define HLS_MANIFEST_TRACK_CAP 16
typedef struct {
    char name[96];
    char language[24];
    char uri[2048];
    int is_default;
    int forced;
} HlsManifestTrack;

// Fallback equivalente ao usado pelo player web: le as renditions de legenda
// diretamente do master mesmo quando o demuxer nao cria AVStreams para elas.
int hls_manifest_subtitle_tracks(const char *body, size_t len,
                                 HlsManifestTrack *tracks, int capacity);

// Resolve URI absoluta, raiz (/...) e relativa sem carregar query/fragments da
// base. Retorna 1 quando o resultado coube por inteiro.
int hls_manifest_resolve_url(const char *base, const char *reference,
                             char *out, size_t out_size);

// Abertura posicionada de HLS VOD. O seek interno do FFmpeg 7.1 em fMP4 mantem
// o indice antigo do demuxer mov de cada rendition: depois do seek ele le os
// novos segmentos com offsets velhos, descarta o audio ate o fim e nao entrega
// quadros. Em vez de buscar, o player reabre com cada playlist de midia ja
// comecando no segmento que contem o ponto desejado.
//
// Retorna 1 e aloca *out (malloc) quando cortou. Retorna 0 sem alocar quando
// nao ha o que cortar ou quando cortar seria inseguro (master, ao vivo,
// BYTERANGE sem offset explicito, ponto no primeiro segmento).
// *segment_start recebe o inicio do primeiro segmento mantido e *total a
// duracao somada de toda a playlist original (ambos em segundos).
int hls_media_playlist_trim(const char *body, size_t len, double start,
                            char **out, size_t *out_len,
                            double *segment_start, double *total);
