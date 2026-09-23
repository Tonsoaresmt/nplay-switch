#pragma once

#include <SDL.h>
#include "cJSON.h"

// Takes ownership of the /api/catalog/movie/:id/info response on success.
int open_movie_details_response(cJSON *response);
int open_related_details_response(cJSON *response);
void request_related_movie_details(int movie_id);

// Desenha a tela SC_MOVIE
void draw_movie(void);

// Trata os inputs do gamepad
void input_movie(int b);

// Limpa memoria do json do filme
void close_movie_details(void);
