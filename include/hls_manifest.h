// Leitura minima e sem alocacoes do manifesto master HLS.
#pragma once
#include <stddef.h>

// Retorna 1 se encontrou EXT-X-MEDIA e conta renditions de audio/legenda.
int hls_manifest_media_counts(const char *body, size_t len,
                              int *audio, int *subtitles);

#define HLS_MANIFEST_TRACK_CAP 16
#define HLS_MANIFEST_URI_MAX 2048
typedef struct {
    char name[96];
    char language[24];
    char uri[HLS_MANIFEST_URI_MAX];
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

// Mesma regra de ff_make_absolute_url do FFmpeg 7.1: a query da base NAO e
// herdada. Use para prever exatamente a URL que o demuxer HLS vai pedir.
int hls_manifest_resolve_like_ffmpeg(const char *base, const char *reference,
                                     char *out, size_t out_size);

// Renditions de audio do master, na ordem em que o FFmpeg cria as AVStreams.
// *filterable = 1 quando todas tem URI, ha 2+ e pertencem ao mesmo GROUP-ID
// usado pelas variantes: so entao e seguro abrir apenas uma delas.
int hls_manifest_audio_tracks(const char *body, size_t len, HlsManifestTrack *tracks,
                              int capacity, int *filterable);

// Copia o master (LF) mantendo somente a rendition de audio `keep` (0-based).
// Cada rendition aberta custa playlist + init + segmentos na abertura e em
// cada salto; trocar de audio ja reabre a fonte com a faixa nova.
int hls_manifest_keep_audio(const char *body, size_t len, int keep,
                            char **out, size_t *out_len);

// URIs cruas das playlists que o demuxer abre (variantes primeiro, depois
// EXT-X-MEDIA de audio/video; sem legendas), sem repeticao.
int hls_manifest_playlist_uris(const char *body, size_t len,
                               char (*uris)[HLS_MANIFEST_URI_MAX], int capacity);

// URI crua do EXT-X-MAP (secao init fMP4) de uma playlist de midia.
int hls_media_playlist_map_uri(const char *body, size_t len, char *out, size_t out_size);

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
