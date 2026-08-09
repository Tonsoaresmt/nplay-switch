#pragma once

#include <SDL.h>
#include "cJSON.h"

// Inicializa e baixa as infos do filme (bater na rota /api/catalog/movie/:id/info)
// Retorna 0 em caso de sucesso.
int open_movie_details(int movie_id);

// Desenha a tela SC_MOVIE
void draw_movie(void);

// Trata os inputs do gamepad
void input_movie(int b);

// Limpa memoria do json do filme
void close_movie_details(void);
