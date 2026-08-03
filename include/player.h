// player.h - player de video via ffmpeg (decode) + SDL (render/audio).
// Toca uma URL HTTP(S) direta (mp4/m3u8). Bloqueia ate o usuario sair (B/+).
#pragma once
#include <SDL.h>

// Retorna 0 normal, negativo em erro de abertura/decode.
int player_play(SDL_Renderer *ren, SDL_Joystick *joy, const char *url);
