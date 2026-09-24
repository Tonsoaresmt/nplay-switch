#include "ui.h"
#include "text.h"
#include "pipoca_atlas_bin.h"
#include <SDL_image.h>
#include <string.h>
#include <stdio.h>

const SDL_Color C_BG   = {  8, 10, 15, 255 };
const SDL_Color C_BAR  = { 12, 15, 23, 255 };
const SDL_Color C_CARD = { 21, 25, 39, 255 };
const SDL_Color C_TEXT = { 244, 245, 252, 255 };
const SDL_Color C_MUT  = { 170, 178, 197, 255 };
const SDL_Color C_ACC  = { 155, 124, 255, 255 };
const SDL_Color C_ACC2 = { 101, 168, 255, 255 };
const SDL_Color C_ROSE = { 239, 120, 199, 255 };
const SDL_Color C_GREEN= { 52, 211, 153, 255 };

static SDL_Texture *g_popcorn_atlas = NULL;
#define POPCORN_FRAME_SIZE 160
#define POPCORN_COLUMNS 8
#define POPCORN_FRAMES 29

void ui_popcorn_draw(SDL_Renderer *ren, int center_x, int y, int size) {
    if (!ren) return;
    if (!g_popcorn_atlas) {
        SDL_RWops *bytes = SDL_RWFromConstMem(pipoca_atlas_bin,
                                              (int)pipoca_atlas_bin_size);
        if (bytes) {
            SDL_Surface *surface = IMG_Load_RW(bytes, 1);
            if (surface) {
                g_popcorn_atlas = SDL_CreateTextureFromSurface(ren, surface);
                SDL_FreeSurface(surface);
            }
        }
    }
    if (!g_popcorn_atlas) return;
    int frame = (int)((SDL_GetTicks() / 65) % POPCORN_FRAMES);
    SDL_Rect src = {(frame % POPCORN_COLUMNS) * POPCORN_FRAME_SIZE,
                    (frame / POPCORN_COLUMNS) * POPCORN_FRAME_SIZE,
                    POPCORN_FRAME_SIZE, POPCORN_FRAME_SIZE};
    SDL_Rect dst = {center_x - size / 2, y, size, size};
    SDL_RenderCopy(ren, g_popcorn_atlas, &src, &dst);
}

void ui_popcorn_release(void) {
    if (g_popcorn_atlas) SDL_DestroyTexture(g_popcorn_atlas);
    g_popcorn_atlas = NULL;
}

void ui_loading_state(const char *title, const char *detail) {
    ui_popcorn_draw(gRen, WIN_W / 2, 197, 176);
    text_center_at(title ? title : "Carregando", 200, WIN_W - 400, 405, C_TEXT, 1);
    text_center_at(detail ? detail : "", 220, WIN_W - 440, 455, C_MUT, 0);
}

void fill_rect(int x, int y, int w, int h, SDL_Color c) {
    if (!gRen) return;
    SDL_SetRenderDrawColor(gRen, c.r, c.g, c.b, c.a);
    SDL_Rect r = { x, y, w, h };
    SDL_RenderFillRect(gRen, &r);
}

void border_rect(int x, int y, int w, int h, int th, SDL_Color c) {
    fill_rect(x, y, w, th, c);
    fill_rect(x, y + h - th, w, th, c);
    fill_rect(x, y, th, h, c);
    fill_rect(x + w - th, y, th, h, c);
}

// Equivalente a object-fit: cover do site: preserva proporcao e recorta o
// excesso, em vez de esticar rostos/capas para dimensoes diferentes.
void ui_cover(SDL_Texture *texture, const SDL_Rect *dst) {
    int tw = 0, th = 0;
    if (!texture || !dst || SDL_QueryTexture(texture, NULL, NULL, &tw, &th) != 0 || tw <= 0 || th <= 0) return;
    SDL_Rect src = { 0, 0, tw, th };
    long long lhs = (long long)tw * dst->h, rhs = (long long)th * dst->w;
    if (lhs > rhs) { src.w = th * dst->w / dst->h; src.x = (tw - src.w) / 2; }
    else if (lhs < rhs) { src.h = tw * dst->h / dst->w; src.y = (th - src.h) / 2; }
    SDL_RenderCopy(gRen, texture, &src, dst);
}

void ui_contain(SDL_Texture *texture, const SDL_Rect *dst) {
    int tw = 0, th = 0;
    if (!texture || !dst || SDL_QueryTexture(texture, NULL, NULL, &tw, &th) != 0 || tw <= 0 || th <= 0) return;
    SDL_Rect out = *dst;
    if ((long long)tw * dst->h > (long long)th * dst->w) {
        out.h = th * dst->w / tw;
        out.y += (dst->h - out.h) / 2;
    } else {
        out.w = tw * dst->h / th;
        out.x += (dst->w - out.w) / 2;
    }
    SDL_RenderCopy(gRen, texture, NULL, &out);
}

void ui_backdrop(SDL_Texture *texture, const SDL_Rect *dst) {
    if (!texture || !dst) return;
    ui_cover(texture, dst);
    // TV shell: imagem legivel a direita, texto sobre uma rampa escura a esquerda.
    for (int i = 0; i < 16; i++) {
        int x = dst->x + dst->w * i / 16;
        int next = dst->x + dst->w * (i + 1) / 16;
        fill_rect(x, dst->y, next - x + 1, dst->h,
                  (SDL_Color){8, 10, 15, (Uint8)(248 - i * 14)});
    }
}

void toast(const char *msg) {
    strncpy(g_toast, msg, sizeof(g_toast) - 1);
    g_toast[sizeof(g_toast) - 1] = '\0';
    g_toast_until = SDL_GetTicks() + 2600;
}

void short_title(const char *title, char *out, int cap) {
    int k = 0;
    for (const char *p = title; *p && k < cap - 1; p++, k++) out[k] = *p;
    out[k] = '\0';
    if (k >= cap - 1 && k >= 2) { out[k - 2] = '.'; out[k - 1] = '.'; }
}

void text_clip(const char *s, int x, int y, SDL_Color c, int big, int maxw) {
    if (!gRen) return;
    SDL_Rect clip = { x, y - 3, maxw, big ? 40 : 30 };
    SDL_bool had_clip = SDL_RenderIsClipEnabled(gRen);
    SDL_Rect previous;
    if (had_clip) {
        SDL_RenderGetClipRect(gRen, &previous);
        if (!SDL_IntersectRect(&clip, &previous, &clip)) return;
    }
    SDL_RenderSetClipRect(gRen, &clip);
    text_draw(gRen, s, x, y, c, big);
    SDL_RenderSetClipRect(gRen, had_clip ? &previous : NULL);
}

int text_center(const char *s, int y, SDL_Color c, int big) {
    int w = 0, h = 0;
    SDL_Texture *t = text_cached(gRen, s, c, big, &w, &h);
    if (t) { SDL_Rect d = { (WIN_W - w) / 2, y, w, h }; SDL_RenderCopy(gRen, t, NULL, &d); }
    return w;
}

int text_center_at(const char *s, int x, int area_w, int y, SDL_Color c, int big) {
    int w = 0, h = 0;
    SDL_Texture *t = text_cached(gRen, s, c, big, &w, &h);
    if (t) {
        SDL_Rect clip = { x, y - 3, area_w, big ? 42 : 32 };
        SDL_bool had_clip = SDL_RenderIsClipEnabled(gRen);
        SDL_Rect previous;
        if (had_clip) {
            SDL_RenderGetClipRect(gRen, &previous);
            if (!SDL_IntersectRect(&clip, &previous, &clip)) return w;
        }
        SDL_RenderSetClipRect(gRen, &clip);
        SDL_Rect d = { x + (area_w - w) / 2, y, w, h };
        SDL_RenderCopy(gRen, t, NULL, &d);
        SDL_RenderSetClipRect(gRen, had_clip ? &previous : NULL);
    }
    return w;
}

int text_right(const char *s, int right, int y, SDL_Color c, int big) {
    int w = 0, h = 0;
    SDL_Texture *t = text_cached(gRen, s, c, big, &w, &h);
    if (t) { SDL_Rect d = { right - w, y, w, h }; SDL_RenderCopy(gRen, t, NULL, &d); }
    return w;
}

void ui_header(const char *section, const char *title, const char *action) {
    fill_rect(0, 0, WIN_W, 95, C_BAR);
    fill_rect(0, 0, WIN_W, 3, C_ACC);
    if (section && section[0]) text_draw(gRen, section, 50, 31, C_ACC, 0);
    if (title && title[0]) {
        int w = 0, h = 0;
        SDL_Texture *t = text_cached(gRen, title, C_TEXT, 1, &w, &h);
        int maxw = 680;
        if (t) {
            SDL_Rect clip = { 300, 22, maxw, 48 };
            SDL_RenderSetClipRect(gRen, &clip);
            SDL_Rect d = { 300 + (maxw - (w > maxw ? maxw : w)) / 2, 27, w, h };
            SDL_RenderCopy(gRen, t, NULL, &d);
            SDL_RenderSetClipRect(gRen, NULL);
        }
    }
    if (action && action[0]) text_right(action, WIN_W - 50, 31, C_MUT, 0);
    fill_rect(0, 94, WIN_W, 1, (SDL_Color){41, 46, 64, 255});
}

void ui_footer(const char *hint) {
    fill_rect(0, WIN_H - 52, WIN_W, 52, C_BAR);
    fill_rect(40, WIN_H - 52, WIN_W - 80, 1, C_CARD);
    if (hint && hint[0]) text_center_at(hint, 40, WIN_W - 80, WIN_H - 37, C_MUT, 0);
}

void ui_panel(int x, int y, int w, int h, SDL_Color accent) {
    fill_rect(x, y, w, h, C_CARD);
    fill_rect(x, y, 4, h, accent);
}

void ui_focus(int x, int y, int w, int h) {
    border_rect(x, y, w, h, 4, (SDL_Color){255, 255, 255, 255});
}

void ui_badge(const char *label, int x, int y, SDL_Color color) {
    int w = 0, h = 0;
    SDL_Texture *t = text_cached(gRen, label, C_TEXT, 0, &w, &h);
    int bw = w + 20;
    fill_rect(x, y, bw, 30, color);
    if (t) { SDL_Rect d = { x + 10, y + 3, w, h }; SDL_RenderCopy(gRen, t, NULL, &d); }
}

// Badge de catalogo: proximo da escala usada no site e sem cobrir a arte.
// Retorna a largura para permitir alinhamento pelo canto direito do poster.
int ui_card_badge(const char *label, int x, int y, SDL_Color color) {
    int w = 0, h = 0;
    SDL_Texture *t = text_cached(gRen, label, C_TEXT, 2, &w, &h);
    int bw = w + 14;
    if (bw < 28) bw = 28;
    SDL_Color dark = { 8, 10, 15, 224 };
    fill_rect(x, y, bw, 22, dark);
    fill_rect(x, y, 3, 22, color);
    if (t) {
        SDL_Rect d = { x + 8, y + (22 - h) / 2, w, h };
        SDL_RenderCopy(gRen, t, NULL, &d);
    }
    return bw;
}

void ui_empty_state(const char *title, const char *detail) {
    const int w = 720, h = 150, x = (WIN_W - w) / 2, y = 250;
    ui_panel(x, y, w, h, C_ACC2);
    text_center_at(title ? title : "Nada por aqui", x + 28, w - 56, y + 28, C_TEXT, 1);
    text_center_at(detail ? detail : "", x + 28, w - 56, y + 82, C_MUT, 0);
}

void ui_progress(int x, int y, int w, int value, SDL_Color color) {
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    fill_rect(x, y, w, 8, C_BAR);
    if (value > 0) fill_rect(x, y, w * value / 100, 8, color);
}

int prompt_text(const char *guide, char *out, size_t cap, int password) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) return -1;
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetGuideText(&kbd, guide);
    swkbdConfigSetStringLenMax(&kbd, (u32)(cap - 1));
    if (password) swkbdConfigSetPasswordFlag(&kbd, 1);
    out[0] = '\0';
    Result rc = swkbdShow(&kbd, out, cap);
    swkbdClose(&kbd);
    if (R_FAILED(rc)) return -1;
    return out[0] ? 0 : -2;
}
