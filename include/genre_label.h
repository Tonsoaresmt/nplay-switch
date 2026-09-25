#pragma once
#include <stddef.h>

// Remove marcadores de ingestao/fonte do rotulo exibido ao espectador.
void movie_genre_label(const char *raw, char *out, size_t cap);
