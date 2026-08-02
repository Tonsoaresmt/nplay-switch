// text.h - renderizacao de texto usando a fonte do sistema do Switch (SDL2_ttf).
#pragma once
#include <SDL.h>

// Inicializa TTF + servico pl + abre a fonte Standard do sistema. Retorna 0 em sucesso.
int  text_init(void);
void text_exit(void);

// Cria uma textura com o texto (UTF-8). big=1 usa a fonte maior (titulos).
// Preenche *outW/*outH com o tamanho. Caller destroi a textura. NULL se vazio/erro.
SDL_Texture *text_make(SDL_Renderer *ren, const char *utf8, SDL_Color color, int big, int *outW, int *outH);

// Desenha texto direto em (x,y) usando cache interno. Retorna a largura.
int  text_draw(SDL_Renderer *ren, const char *utf8, int x, int y, SDL_Color color, int big);

// Retorna textura do CACHE (NAO destruir). Para desenhar com posicionamento proprio.
SDL_Texture *text_cached(SDL_Renderer *ren, const char *utf8, SDL_Color color, int big, int *outW, int *outH);
