// curl_avio.h - AVIOContext do ffmpeg alimentado pelo libcurl.
// O switch-ffmpeg foi compilado SEM backend TLS, entao ele nao abre https://
// sozinho. O libcurl (com mbedtls) abre. Aqui a gente pluga o curl como camada
// de I/O do ffmpeg: leitura sequencial + seek por HTTP Range. Assim o player
// toca qualquer https (link direto do R2 dos animes e o arquivo do acelerador).
#ifndef NPLAY_CURL_AVIO_H
#define NPLAY_CURL_AVIO_H

#include <libavformat/avio.h>

// Cria um AVIOContext que le a URL via libcurl. Retorna NULL em falha.
// Passe o resultado em fmt->pb + AVFMT_FLAG_CUSTOM_IO antes de avformat_open_input.
AVIOContext *nplay_curl_avio_open(const char *url);

// Fecha e libera o AVIOContext criado acima (curl + buffers). Chame DEPOIS de
// avformat_close_input (com AVFMT_FLAG_CUSTOM_IO o ffmpeg nao libera o pb).
void nplay_curl_avio_close(AVIOContext *ctx);

#endif
