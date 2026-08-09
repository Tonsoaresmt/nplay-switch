#pragma once
#include "cJSON.h"
#include <SDL.h>

extern const char *BASE;
extern char g_token[640];


// Para podermos usar as funcoes compartilhadas de main.c sem mover tudo:
cJSON *api_get(const char *path);
long api_send(const char *path, const char *method, const char *body);

const char *jstr(cJSON *o, const char *k);
int jint(cJSON *o, const char *k);
int arr_len(cJSON *a);

SDL_Texture *cover_get(const char *url);
int resolve_and_play(int item_id, const char *title);
int is_fav_item(int id);
void toggle_fav_item(int id);
