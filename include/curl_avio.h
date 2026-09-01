// curl_avio.h - AVIOContext do ffmpeg alimentado pelo libcurl.
// O libcurl fornece prefetch, leitura sequencial e seek por HTTP Range. Alem de
// arquivos remotos unicos, o perfil HLS e usado pelo callback AVFormatContext.io_open
// para que playlists e segmentos nao dependam do TLS interno do FFmpeg/libnx.
#ifndef NPLAY_CURL_AVIO_H
#define NPLAY_CURL_AVIO_H

#include <libavformat/avio.h>

// Cria um AVIOContext que le a URL via libcurl. Retorna NULL em falha.
// Passe o resultado em fmt->pb + AVFMT_FLAG_CUSTOM_IO antes de avformat_open_input.
// expected_size pode ser -1 quando a API nao conhece o tamanho. Quando existe,
// evita que o demuxer MP4 dependa de uma segunda requisicao apenas para descobrir
// onde fica o indice/moov no fim do arquivo.
AVIOContext *nplay_curl_avio_open(const char *url, int64_t expected_size);

// Perfil leve para cada recurso HLS. Um master pode manter varias playlists
// abertas ao mesmo tempo, portanto este usa blocos/ring menores que um MP4.
AVIOContext *nplay_curl_avio_open_hls(const char *url);

// Diagnostico sem URLs: quantidade de recursos HLS abertos e memoria reservada.
void nplay_curl_avio_stats(int *active_contexts, int *reserved_kb);

// Fecha e libera o AVIOContext criado acima (curl + buffers). Chame DEPOIS de
// avformat_close_input (com AVFMT_FLAG_CUSTOM_IO o ffmpeg nao libera o pb).
void nplay_curl_avio_close(AVIOContext *ctx);

#endif
