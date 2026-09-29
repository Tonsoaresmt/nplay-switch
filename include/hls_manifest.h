// Leitura minima e sem alocacoes do manifesto master HLS.
#pragma once
#include <stddef.h>

// Retorna 1 se encontrou EXT-X-MEDIA e conta renditions de audio/legenda.
int hls_manifest_media_counts(const char *body, size_t len,
                              int *audio, int *subtitles);
