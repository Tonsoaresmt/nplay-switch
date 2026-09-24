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

// Limite absoluto entre abrir o manifesto HLS e apresentar o primeiro quadro.
// Passe 0 quando o primeiro quadro aparecer ou a tentativa terminar.
void nplay_curl_avio_set_startup_window(unsigned timeout_ms);

// O callback roda apenas na thread de abertura/leitura do FFmpeg, nunca na
// thread produtora do segmento. Permite cancelar uma leitura bloqueante com B.
void nplay_curl_avio_set_abort_check(int (*check)(void *), void *userdata);

// Diagnostico sem URLs: quantidade de recursos HLS abertos e memoria reservada.
void nplay_curl_avio_stats(int *active_contexts, int *reserved_kb);

// Resumo sem URLs das requisicoes de segmentos da tentativa atual. O resumo
// evita gravar cada resposta bem-sucedida na microSD durante a reproducao.
typedef struct {
    int requests, slow_first_bytes, worst_first_ms, new_connections, failures;
    int first_reads, ready_first_reads, young_first_reads, old_empty_first_reads;
} NplayCurlAvioQuality;
void nplay_curl_avio_quality_reset(void);
void nplay_curl_avio_quality_get(NplayCurlAvioQuality *out);

// Fecha e libera o AVIOContext criado acima (curl + buffers). Chame DEPOIS de
// avformat_close_input (com AVFMT_FLAG_CUSTOM_IO o ffmpeg nao libera o pb).
void nplay_curl_avio_close(AVIOContext *ctx);

// Liberar as conexoes HLS ociosas antes de curl_global_cleanup.
void nplay_curl_avio_pool_clear(void);

#endif
