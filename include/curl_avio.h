// curl_avio.h - AVIOContext do ffmpeg alimentado pelo libcurl.
// Para arquivos remotos unicos, o libcurl fornece prefetch, leitura sequencial e
// seek por HTTP Range. HLS nao usa este adaptador: a playlist precisa abrir
// submanifestos e segmentos e segue pelo HTTP+TLS nativo do FFmpeg.
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
