// player.h - player de video via ffmpeg (decode) + SDL (render/audio).
// Toca uma URL HTTP(S) direta (I/O via libcurl, ver curl_avio). Bloqueia ate o
// usuario sair (B/-). Suporta retomar (start_sec) e reporta onde parou.
#pragma once
#include <SDL.h>

// Toca a URL.
//   title     : nome mostrado no HUD (pode ser NULL)
//   start_sec : retoma a partir deste segundo (0 = do comeco)
//   out_pos   : (opcional) recebe a posicao em que parou, em segundos
//   out_dur   : (opcional) recebe a duracao total, em segundos
// Retorna 1 se terminou naturalmente (p/ auto-play do proximo), 0 se o usuario
// saiu, negativo em erro de abertura/decode.
int player_play(SDL_Renderer *ren, SDL_Joystick *joy, const char *url, int is_hls,
                const char *title, double start_sec, double *out_pos, double *out_dur);

// Explica o ultimo retorno negativo sem expor URL ou token de reproducao.
const char *player_last_error(void);
