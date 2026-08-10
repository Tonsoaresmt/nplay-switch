#include "ui.h"
#include "text.h"
#include <string.h>
#include <stdio.h>

const SDL_Color C_BG   = {  8, 10, 15, 255 };
const SDL_Color C_BAR  = { 12, 15, 23, 255 };
const SDL_Color C_CARD = { 20, 24, 36, 255 };
const SDL_Color C_TEXT = { 234, 240, 250, 255 };
const SDL_Color C_MUT  = { 139, 150, 173, 255 };
const SDL_Color C_ACC  = { 139, 92, 246, 255 };
const SDL_Color C_ACC2 = { 59, 130, 246, 255 };
const SDL_Color C_ROSE = { 251, 113, 133, 255 };
const SDL_Color C_GREEN= { 52, 211, 153, 255 };

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
    SDL_RenderSetClipRect(gRen, &clip);
    text_draw(gRen, s, x, y, c, big);
    SDL_RenderSetClipRect(gRen, NULL);
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
        SDL_RenderSetClipRect(gRen, &clip);
        SDL_Rect d = { x + (area_w - w) / 2, y, w, h };
        SDL_RenderCopy(gRen, t, NULL, &d);
        SDL_RenderSetClipRect(gRen, NULL);
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
    fill_rect(0, 0, WIN_W, 72, C_BAR);
    fill_rect(0, 0, 6, 72, C_ACC);
    if (section && section[0]) text_draw(gRen, section, 40, 24, C_ACC, 0);
    if (title && title[0]) {
        int w = 0, h = 0;
        SDL_Texture *t = text_cached(gRen, title, C_TEXT, 1, &w, &h);
        int maxw = 680;
        if (t) {
            SDL_Rect clip = { 300, 12, maxw, 48 };
            SDL_RenderSetClipRect(gRen, &clip);
            SDL_Rect d = { 300 + (maxw - (w > maxw ? maxw : w)) / 2, 17, w, h };
            SDL_RenderCopy(gRen, t, NULL, &d);
            SDL_RenderSetClipRect(gRen, NULL);
        }
    }
    if (action && action[0]) text_right(action, WIN_W - 40, 24, C_MUT, 0);
    fill_rect(40, 71, WIN_W - 80, 1, C_CARD);
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
    border_rect(x, y, w, h, 3, C_ACC2);
}

void ui_badge(const char *label, int x, int y, SDL_Color color) {
    int w = 0, h = 0;
    SDL_Texture *t = text_cached(gRen, label, C_TEXT, 0, &w, &h);
    int bw = w + 20;
    fill_rect(x, y, bw, 30, color);
    if (t) { SDL_Rect d = { x + 10, y + 3, w, h }; SDL_RenderCopy(gRen, t, NULL, &d); }
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
