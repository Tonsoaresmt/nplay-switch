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
