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
