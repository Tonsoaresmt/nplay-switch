/* Executes extracted draw_signs, with deterministic font/draw spies.
 * Geometry only: no SDL pixels, GPU, actual glyphs or Switch validation. */
#include "subtitle_queue.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { int unused; } SDL_Renderer;
typedef struct { unsigned char r, g, b, a; } SDL_Color;
typedef struct {
    const SubtitleSigns *signs;
    int video_x, video_y, video_w, video_h;
} PlayerHud;
#define PUI_W 1280
#define PUI_H 720
#define ST_NORMAL 0
static const SDL_Color K_BLACK = {0,0,0,255}, K_WHITE = {255,255,255,255};
static struct { int x, y, maxw; } draws[64];
static int count;
static int text_w(SDL_Renderer *r, const char *s, int style) {
    (void)r; (void)style; return (int)strlen(s) * 10;
}
static int text_a(SDL_Renderer *r, const char *s, int x, int y, SDL_Color c,
                  int style, float alpha, int maxw) {
    (void)r; (void)style; (void)alpha;
    if (c.r == 255) {
        assert(count < 64);
        draws[count].x = x; draws[count].y = y; draws[count].maxw = maxw; count++;
    }
    return (int)strlen(s) * 10;
}
#include "test_draw_signs.inc"
int main(void) {
    SDL_Renderer renderer = {0};
    SubtitleSigns signs = {.count = 1};
    signs.items[0].at = (SubtitlePlacement){25,24.6f,1,1,0};
    strcpy(signs.items[0].text, "ABCDEFGHIJ");
    PlayerHud hud = {&signs,0,90,1280,540};
    draw_signs(&renderer, &hud);
    assert(count == 1 && draws[0].x == 270 && draws[0].y == 222);
    puts("PASS actual draw_signs: letterbox position relative to video");
    count = 0; hud = (PlayerHud){&signs,160,0,960,720};
    draw_signs(&renderer, &hud);
    assert(draws[0].x == 350 && draws[0].y == 177);
    puts("PASS actual draw_signs: pillarbox position relative to video");
    count = 0; signs.items[0].at.halign = 0;
    draw_signs(&renderer, &hud); assert(draws[0].x == 400);
    count = 0; signs.items[0].at.halign = 2;
    draw_signs(&renderer, &hud); assert(draws[0].x == 300);
    puts("PASS actual draw_signs: left/center/right anchors");
    count = 0; signs.items[0].at.halign = 1;
    strcpy(signs.items[0].text, "ABCDEFGHIJ\nABCDE");
    draw_signs(&renderer, &hud);
    assert(count == 2 && draws[0].x == 350 && draws[1].x == 375 &&
           draws[0].y == 177 && draws[1].y == 205);
    puts("PASS actual draw_signs: multiline center, top anchor, 28px spacing");
    count = 0; signs.items[0].at.x = 0; signs.items[0].at.y = 0;
    draw_signs(&renderer, &hud);
    assert(draws[0].x == 168 && draws[0].y == 4);
    count = 0; signs.items[0].at.x = 100; signs.items[0].at.y = 100;
    draw_signs(&renderer, &hud);
    assert(draws[0].x == 1012 && draws[0].y == 660);
    puts("PASS actual draw_signs: clipping-safe edge clamps");
    count = 0; signs.count = SUBTITLE_SIGNS_MAX;
    for (int i = 1; i < SUBTITLE_SIGNS_MAX; i++) signs.items[i] = signs.items[0];
    draw_signs(&renderer, &hud); assert(count == SUBTITLE_SIGNS_MAX * 2);
    puts("PASS actual draw_signs: bounded eight-sign rendering");
    puts("GEOMETRY PASS: extracted implementation; not a hardware/pixel test");
    return 0;
}
