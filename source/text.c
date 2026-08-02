// text.c - fonte do sistema do Switch (via servico pl) + SDL2_ttf.
#include "text.h"
#include <switch.h>
#include <SDL_ttf.h>
#include <stdlib.h>
#include <string.h>

static TTF_Font *g_font     = NULL;   // texto normal (listas)
static TTF_Font *g_font_big = NULL;   // titulos
static int       g_pl_ok    = 0;

// ---- cache de texturas de texto: evita rasterizar a mesma string todo frame ----
#define TEXT_CACHE_MAX 192
typedef struct {
    char *text;
    Uint8 r, g, b;
    int big;
    SDL_Texture *tex;
    int w, h;
    Uint32 lastUsed;
} TextCacheEntry;
static TextCacheEntry g_tcache[TEXT_CACHE_MAX];
static Uint32 g_tclock = 0;

static void tcache_free_all(void) {
    for (int i = 0; i < TEXT_CACHE_MAX; i++) {
        if (g_tcache[i].tex) SDL_DestroyTexture(g_tcache[i].tex);
        free(g_tcache[i].text);
        g_tcache[i].tex = NULL;
        g_tcache[i].text = NULL;
    }
}

int text_init(void) {
    if (TTF_Init() != 0) return -1;
    if (R_FAILED(plInitialize(PlServiceType_User))) return -2;
    g_pl_ok = 1;

    PlFontData fd;
    if (R_FAILED(plGetSharedFontByType(&fd, PlSharedFontType_Standard))) return -3;

    // Dois ponteiros RWops sobre a MESMA memoria da fonte (so leitura). A memoria
    // pertence ao servico pl e fica valida ate plExit (chamado em text_exit).
    SDL_RWops *rw1 = SDL_RWFromConstMem(fd.address, (int)fd.size);
    g_font = TTF_OpenFontRW(rw1, 1, 23);
    SDL_RWops *rw2 = SDL_RWFromConstMem(fd.address, (int)fd.size);
    g_font_big = TTF_OpenFontRW(rw2, 1, 31);

    if (!g_font || !g_font_big) return -4;
    return 0;
}

void text_exit(void) {
    tcache_free_all();
    if (g_font)     { TTF_CloseFont(g_font);     g_font = NULL; }
    if (g_font_big) { TTF_CloseFont(g_font_big); g_font_big = NULL; }
    if (g_pl_ok)    { plExit(); g_pl_ok = 0; }
    TTF_Quit();
}

// Retorna uma textura do CACHE (NAO destruir). Reaproveita entre frames.
SDL_Texture *text_cached(SDL_Renderer *ren, const char *utf8, SDL_Color color, int big, int *outW, int *outH) {
    if (outW) *outW = 0;
    if (outH) *outH = 0;
    TTF_Font *f = big ? g_font_big : g_font;
    if (!f || !utf8 || !utf8[0]) return NULL;

    TextCacheEntry *hit = NULL;
    TextCacheEntry *victim = &g_tcache[0];
    for (int i = 0; i < TEXT_CACHE_MAX; i++) {
        TextCacheEntry *e = &g_tcache[i];
        if (e->tex && e->big == big && e->r == color.r && e->g == color.g && e->b == color.b &&
            e->text && strcmp(e->text, utf8) == 0) { hit = e; break; }
        if (!e->tex) { victim = e; }               // prefere slot vazio
        else if (victim->tex && e->lastUsed < victim->lastUsed) victim = e;
    }
    if (!hit) {
        SDL_Surface *s = TTF_RenderUTF8_Blended(f, utf8, color);
        if (!s) return NULL;
        SDL_Texture *t = SDL_CreateTextureFromSurface(ren, s);
        int w = s->w, h = s->h;
        SDL_FreeSurface(s);
        if (!t) return NULL;
        if (victim->tex) SDL_DestroyTexture(victim->tex);
        free(victim->text);
        victim->text = strdup(utf8);
        victim->r = color.r; victim->g = color.g; victim->b = color.b; victim->big = big;
        victim->tex = t; victim->w = w; victim->h = h;
        hit = victim;
    }
    hit->lastUsed = ++g_tclock;
    if (outW) *outW = hit->w;
    if (outH) *outH = hit->h;
    return hit->tex;
}

SDL_Texture *text_make(SDL_Renderer *ren, const char *utf8, SDL_Color color, int big, int *outW, int *outH) {
    if (outW) *outW = 0;
    if (outH) *outH = 0;
    TTF_Font *f = big ? g_font_big : g_font;
    if (!f || !utf8 || !utf8[0]) return NULL;

    SDL_Surface *s = TTF_RenderUTF8_Blended(f, utf8, color);
    if (!s) return NULL;

    SDL_Texture *t = SDL_CreateTextureFromSurface(ren, s);
    if (outW) *outW = s->w;
    if (outH) *outH = s->h;
    SDL_FreeSurface(s);
    return t;
}

int text_draw(SDL_Renderer *ren, const char *utf8, int x, int y, SDL_Color color, int big) {
    int w = 0, h = 0;
    SDL_Texture *t = text_cached(ren, utf8, color, big, &w, &h);  // usa cache (nao destroi)
    if (t) {
        SDL_Rect dst = { x, y, w, h };
        SDL_RenderCopy(ren, t, NULL, &dst);
    }
    return w;
}
