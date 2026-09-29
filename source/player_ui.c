// player_ui.c - desenho do HUD do player no estilo da versao PC.
// Tudo aqui e so apresentacao: o estado vem pronto em PlayerHud. As formas usam
// SDL_RenderGeometry (degrades, circulos e icones vetoriais) para nao depender de
// imagens extras no NRO.
#include "player_ui.h"
#include "text.h"
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI_F 3.14159265f
#define ST_NORMAL 0
#define ST_TITLE 1
#define ST_SMALL 2
#define ST_DISPLAY 3
#define ST_TIME 4
#define ST_SUB 5

static const SDL_Color K_WHITE = { 245, 247, 252, 255 };
static const SDL_Color K_MUTED = { 196, 202, 216, 255 };
static const SDL_Color K_DIM   = { 150, 158, 176, 255 };
static const SDL_Color K_ACC   = { 139, 92, 246, 255 };
static const SDL_Color K_ACC2  = { 59, 130, 246, 255 };
static const SDL_Color K_ROSE  = { 251, 113, 133, 255 };
static const SDL_Color K_BLACK = { 0, 0, 0, 255 };
static const SDL_Color K_PANEL = { 14, 17, 26, 255 };

// Layout base (1280x720). O mesmo desenho vale para portatil e dock.
#define BAR_Y 600
#define BAR_X0 48
#define CTRL_Y 654
#define CTRL_R 24
#define X_PLAY 72
#define X_REW 138
#define X_FWD 204
#define X_VOL 270
#define X_TITLE 318
#define X_RIGHT 1208

static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static Uint8 alpha8(float a) { return (Uint8)(clamp01(a) * 255.0f + 0.5f); }

static SDL_Vertex vtx(float x, float y, SDL_Color c, float a) {
    SDL_Vertex v;
    v.position.x = x; v.position.y = y;
    v.color = c; v.color.a = alpha8(a * (c.a / 255.0f));
    v.tex_coord.x = 0; v.tex_coord.y = 0;
    return v;
}

static void fill(SDL_Renderer *r, float x, float y, float w, float h, SDL_Color c, float a) {
    if (w <= 0 || h <= 0 || a <= 0.003f) return;
    SDL_Vertex v[4] = { vtx(x, y, c, a), vtx(x + w, y, c, a), vtx(x + w, y + h, c, a), vtx(x, y + h, c, a) };
    int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(r, NULL, v, 4, idx, 6);
}

// Degrade vertical: alpha do topo ate a base.
static void vgrad(SDL_Renderer *r, float x, float y, float w, float h, SDL_Color c, float top, float bottom) {
    if (top <= 0.003f && bottom <= 0.003f) return;
    SDL_Vertex v[4] = { vtx(x, y, c, top), vtx(x + w, y, c, top), vtx(x + w, y + h, c, bottom), vtx(x, y + h, c, bottom) };
    int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(r, NULL, v, 4, idx, 6);
}

static void hgrad(SDL_Renderer *r, float x, float y, float w, float h, SDL_Color c, float left, float right) {
    if (left <= 0.003f && right <= 0.003f) return;
    SDL_Vertex v[4] = { vtx(x, y, c, left), vtx(x + w, y, c, right), vtx(x + w, y + h, c, right), vtx(x, y + h, c, left) };
    int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(r, NULL, v, 4, idx, 6);
}

static void tri(SDL_Renderer *r, float x1, float y1, float x2, float y2, float x3, float y3, SDL_Color c, float a) {
    if (a <= 0.003f) return;
    SDL_Vertex v[3] = { vtx(x1, y1, c, a), vtx(x2, y2, c, a), vtx(x3, y3, c, a) };
    SDL_RenderGeometry(r, NULL, v, 3, NULL, 0);
}

// Setor circular (fan). sweep em radianos; 2*PI = disco.
static void sector(SDL_Renderer *r, float cx, float cy, float rad, float a0, float sweep, SDL_Color c, float a) {
    if (rad <= 0 || a <= 0.003f) return;
    enum { SEG = 40 };
    SDL_Vertex v[SEG + 2];
    int idx[SEG * 3];
    v[0] = vtx(cx, cy, c, a);
    for (int i = 0; i <= SEG; i++) {
        float t = a0 + sweep * i / SEG;
        v[i + 1] = vtx(cx + cosf(t) * rad, cy + sinf(t) * rad, c, a);
    }
    for (int i = 0; i < SEG; i++) { idx[i * 3] = 0; idx[i * 3 + 1] = i + 1; idx[i * 3 + 2] = i + 2; }
    SDL_RenderGeometry(r, NULL, v, SEG + 2, idx, SEG * 3);
}

static void circle(SDL_Renderer *r, float cx, float cy, float rad, SDL_Color c, float a) {
    sector(r, cx, cy, rad, 0, 2 * PI_F, c, a);
}

// Arco com espessura (anel parcial).
static void arc(SDL_Renderer *r, float cx, float cy, float rad, float thick, float a0, float sweep,
                SDL_Color c, float a) {
    if (rad <= 0 || thick <= 0 || a <= 0.003f) return;
    enum { SEG = 48 };
    SDL_Vertex v[(SEG + 1) * 2];
    int idx[SEG * 6];
    float inner = rad - thick * 0.5f, outer = rad + thick * 0.5f;
    for (int i = 0; i <= SEG; i++) {
        float t = a0 + sweep * i / SEG, ct = cosf(t), st = sinf(t);
        v[i * 2] = vtx(cx + ct * inner, cy + st * inner, c, a);
        v[i * 2 + 1] = vtx(cx + ct * outer, cy + st * outer, c, a);
    }
    for (int i = 0; i < SEG; i++) {
        int b = i * 2;
        idx[i * 6] = b; idx[i * 6 + 1] = b + 1; idx[i * 6 + 2] = b + 3;
        idx[i * 6 + 3] = b; idx[i * 6 + 4] = b + 3; idx[i * 6 + 5] = b + 2;
    }
    SDL_RenderGeometry(r, NULL, v, (SEG + 1) * 2, idx, SEG * 6);
}

static void line(SDL_Renderer *r, float x1, float y1, float x2, float y2, float thick, SDL_Color c, float a) {
    float dx = x2 - x1, dy = y2 - y1, len = sqrtf(dx * dx + dy * dy);
    if (len <= 0.01f || a <= 0.003f) return;
    float nx = -dy / len * thick * 0.5f, ny = dx / len * thick * 0.5f;
    SDL_Vertex v[4] = { vtx(x1 + nx, y1 + ny, c, a), vtx(x2 + nx, y2 + ny, c, a),
                        vtx(x2 - nx, y2 - ny, c, a), vtx(x1 - nx, y1 - ny, c, a) };
    int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(r, NULL, v, 4, idx, 6);
    circle(r, x1, y1, thick * 0.5f, c, a);
    circle(r, x2, y2, thick * 0.5f, c, a);
}

static void rrect(SDL_Renderer *r, float x, float y, float w, float h, float rad, SDL_Color c, float a) {
    if (w <= 0 || h <= 0 || a <= 0.003f) return;
    if (rad * 2 > h) rad = h / 2;
    if (rad * 2 > w) rad = w / 2;
    fill(r, x + rad, y, w - 2 * rad, h, c, a);
    fill(r, x, y + rad, rad, h - 2 * rad, c, a);
    fill(r, x + w - rad, y + rad, rad, h - 2 * rad, c, a);
    sector(r, x + rad, y + rad, rad, PI_F, PI_F / 2, c, a);
    sector(r, x + w - rad, y + rad, rad, -PI_F / 2, PI_F / 2, c, a);
    sector(r, x + w - rad, y + h - rad, rad, 0, PI_F / 2, c, a);
    sector(r, x + rad, y + h - rad, rad, PI_F / 2, PI_F / 2, c, a);
}

static void rrect_outline(SDL_Renderer *r, float x, float y, float w, float h, float rad, float t,
                          SDL_Color c, float a) {
    if (a <= 0.003f) return;
    fill(r, x + rad, y, w - 2 * rad, t, c, a);
    fill(r, x + rad, y + h - t, w - 2 * rad, t, c, a);
    fill(r, x, y + rad, t, h - 2 * rad, c, a);
    fill(r, x + w - t, y + rad, t, h - 2 * rad, c, a);
    arc(r, x + rad, y + rad, rad - t / 2, t, PI_F, PI_F / 2, c, a);
    arc(r, x + w - rad, y + rad, rad - t / 2, t, -PI_F / 2, PI_F / 2, c, a);
    arc(r, x + w - rad, y + h - rad, rad - t / 2, t, 0, PI_F / 2, c, a);
    arc(r, x + rad, y + h - rad, rad - t / 2, t, PI_F / 2, PI_F / 2, c, a);
}

// ------------------------------------------------------------------ texto
static int text_w(SDL_Renderer *r, const char *s, int style) {
    int w = 0, h = 0;
    if (!s || !s[0]) return 0;
    if (text_measure(s, style, &w, &h) == 0) return w;
    text_cached(r, s, K_WHITE, style, &w, &h);
    return w;
}

// Desenha com transparencia. maxw > 0 recorta a largura sem distorcer.
static int text_a(SDL_Renderer *r, const char *s, int x, int y, SDL_Color c, int style, float a, int maxw) {
    int w = 0, h = 0;
    if (!s || !s[0] || a <= 0.01f) return 0;
    SDL_Texture *t = text_cached(r, s, c, style, &w, &h);
    if (!t) return 0;
    SDL_Rect src = { 0, 0, w, h }, dst = { x, y, w, h };
    if (maxw > 0 && w > maxw) { src.w = maxw; dst.w = maxw; }
    SDL_SetTextureAlphaMod(t, alpha8(a));
    SDL_RenderCopy(r, t, &src, &dst);
    SDL_SetTextureAlphaMod(t, 255);
    return w;
}

static void text_center_a(SDL_Renderer *r, const char *s, int cx, int y, SDL_Color c, int style, float a) {
    int w = text_w(r, s, style);
    text_a(r, s, cx - w / 2, y, c, style, a, 0);
}

static void text_right_a(SDL_Renderer *r, const char *s, int right, int y, SDL_Color c, int style, float a) {
    int w = text_w(r, s, style);
    text_a(r, s, right - w, y, c, style, a, 0);
}

// Quebra por palavra usando a medida real da fonte. Resultado pequeno e em cache:
// o HUD redesenha a cada quadro e nao pode medir a sinopse inteira sempre.
typedef struct {
    unsigned long signature;
    int style, width, max_lines, count;
    char lines[4][200];
} WrapCache;

static unsigned long wrap_signature(const char *text) {
    unsigned long hash = 2166136261u;
    if (!text) return 0;
    while (*text) {
        hash ^= (unsigned char)*text++;
        hash *= 16777619u;
    }
    return hash;
}

static int wrap(WrapCache *cache, const char *text, int style, int width, int max_lines) {
    if (!text) { cache->count = 0; cache->signature = 0; return 0; }
    unsigned long signature = wrap_signature(text);
    if (cache->signature == signature && cache->style == style &&
        cache->width == width && cache->max_lines == max_lines)
        return cache->count;
    cache->signature = signature;
    cache->style = style;
    cache->width = width;
    cache->max_lines = max_lines;
    cache->count = 0;
    if (max_lines > 4) max_lines = 4;
    const char *p = text;
    while (*p && cache->count < max_lines) {
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
        if (!*p) break;
        char line[200] = "";
        size_t used = 0;
        const char *line_end = p;
        while (*line_end) {
            const char *word = line_end;
            while (*word == ' ' || *word == '\t') word++;
            const char *end = word;
            while (*end && *end != ' ' && *end != '\n' && *end != '\r' && *end != '\t') end++;
            if (end == word) break;
            char candidate[200];
            int n = snprintf(candidate, sizeof(candidate), used ? "%s %.*s" : "%s%.*s", line, (int)(end - word), word);
            if (n >= (int)sizeof(candidate)) break;
            int w = 0, h = 0;
            if (text_measure(candidate, style, &w, &h) != 0) w = n * 11;
            if (used && w > width) break;
            snprintf(line, sizeof(line), "%s", candidate);
            used = strlen(line);
            line_end = end;
            if (*end == '\n' || *end == '\r') break;
        }
        if (!used) {  // palavra maior que a linha: corta por bytes para garantir progresso
            size_t k = 0;
            while (p[k] && p[k] != ' ' && k < 60) k++;
            snprintf(line, sizeof(line), "%.*s", (int)k, p);
            line_end = p + k;
        }
        snprintf(cache->lines[cache->count++], sizeof(cache->lines[0]), "%s", line);
        p = line_end;
    }
    if (*p && cache->count == max_lines) {  // havia mais texto: reticencias na ultima linha
        char *last = cache->lines[cache->count - 1];
        size_t len = strlen(last);
        while (len > 0 && last[len - 1] == ' ') len--;
        if (len > 3) {
            // recua ate o fim de um caractere UTF-8 completo
            size_t cut = len > 190 ? 190 : len;
            while (cut > 0 && (last[cut] & 0xC0) == 0x80) cut--;
            snprintf(last + cut, sizeof(cache->lines[0]) - cut, "...");
        }
    }
    return cache->count;
}

void pui_format_time(double s, char *out, int cap) {
    if (s < 0 || s != s) s = 0;
    int t = (int)s, h = t / 3600, m = (t % 3600) / 60, sec = t % 60;
    if (h > 0) snprintf(out, cap, "%d:%02d:%02d", h, m, sec);
    else snprintf(out, cap, "%d:%02d", m, sec);
}

// ------------------------------------------------------------------ icones
static void icon_play(SDL_Renderer *r, float cx, float cy, float s, SDL_Color c, float a) {
    tri(r, cx - s * 0.26f, cy - s * 0.36f, cx - s * 0.26f, cy + s * 0.36f, cx + s * 0.38f, cy, c, a);
}

static void icon_pause(SDL_Renderer *r, float cx, float cy, float s, SDL_Color c, float a) {
    fill(r, cx - s * 0.30f, cy - s * 0.34f, s * 0.20f, s * 0.68f, c, a);
    fill(r, cx + s * 0.10f, cy - s * 0.34f, s * 0.20f, s * 0.68f, c, a);
}

// Seta circular com o numero de segundos no centro (igual aos botoes do PC).
static void icon_skip(SDL_Renderer *r, float cx, float cy, float s, int forward, int seconds, SDL_Color c, float a) {
    float rad = s * 0.40f;
    float gap = 0.9f;  // abertura no topo
    float start = forward ? (-PI_F / 2 + gap * 0.5f) : (-PI_F / 2 + gap * 0.5f);
    arc(r, cx, cy, rad, s * 0.075f, start, 2 * PI_F - gap, c, a);
    // ponta da seta no fim da abertura
    float tip_angle = forward ? (-PI_F / 2 + gap * 0.5f) : (-PI_F / 2 - gap * 0.5f);
    float tx = cx + cosf(tip_angle) * rad, ty = cy + sinf(tip_angle) * rad;
    float dir = forward ? 1.0f : -1.0f;
    tri(r, tx - dir * s * 0.02f, ty - s * 0.15f, tx - dir * s * 0.02f, ty + s * 0.15f, tx + dir * s * 0.20f, ty, c, a);
    char label[8]; snprintf(label, sizeof(label), "%d", seconds);
    int w = 0, h = 0;
    SDL_Texture *t = text_cached(r, label, c, ST_SMALL, &w, &h);
    if (t) {
        SDL_Rect d = { (int)(cx - w / 2.0f), (int)(cy - h / 2.0f + 1), w, h };
        SDL_SetTextureAlphaMod(t, alpha8(a)); SDL_RenderCopy(r, t, NULL, &d); SDL_SetTextureAlphaMod(t, 255);
    }
}

static void icon_volume(SDL_Renderer *r, float cx, float cy, float s, int volume, SDL_Color c, float a) {
    float bx = cx - s * 0.36f;
    fill(r, bx, cy - s * 0.13f, s * 0.16f, s * 0.26f, c, a);
    SDL_Vertex v[4] = { vtx(bx + s * 0.15f, cy - s * 0.13f, c, a), vtx(bx + s * 0.40f, cy - s * 0.34f, c, a),
                        vtx(bx + s * 0.40f, cy + s * 0.34f, c, a), vtx(bx + s * 0.15f, cy + s * 0.13f, c, a) };
    int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(r, NULL, v, 4, idx, 6);
    float ox = bx + s * 0.40f;
    if (volume <= 0) {
        line(r, ox + s * 0.10f, cy - s * 0.12f, ox + s * 0.34f, cy + s * 0.12f, s * 0.07f, c, a);
        line(r, ox + s * 0.10f, cy + s * 0.12f, ox + s * 0.34f, cy - s * 0.12f, s * 0.07f, c, a);
        return;
    }
    arc(r, ox, cy, s * 0.16f, s * 0.06f, -0.85f, 1.7f, c, a);
    if (volume > 35) arc(r, ox, cy, s * 0.30f, s * 0.06f, -0.85f, 1.7f, c, a);
    if (volume > 70) arc(r, ox, cy, s * 0.44f, s * 0.06f, -0.85f, 1.7f, c, a * 0.9f);
}

// Balao de fala: audio e legendas (mesmo simbolo usado pelo PC/Netflix).
static void icon_tracks(SDL_Renderer *r, float cx, float cy, float s, SDL_Color c, float a) {
    float w = s * 0.82f, h = s * 0.56f, x = cx - w / 2, y = cy - h / 2 - s * 0.06f;
    rrect_outline(r, x, y, w, h, s * 0.10f, s * 0.065f, c, a);
    tri(r, x + w * 0.22f, y + h - 1, x + w * 0.22f, y + h + s * 0.17f, x + w * 0.42f, y + h - 1, c, a);
    fill(r, x + w * 0.20f, y + h * 0.36f, w * 0.34f, s * 0.06f, c, a);
    fill(r, x + w * 0.60f, y + h * 0.36f, w * 0.20f, s * 0.06f, c, a);
    fill(r, x + w * 0.20f, y + h * 0.60f, w * 0.20f, s * 0.06f, c, a);
    fill(r, x + w * 0.46f, y + h * 0.60f, w * 0.34f, s * 0.06f, c, a);
}

static void icon_next(SDL_Renderer *r, float cx, float cy, float s, SDL_Color c, float a) {
    tri(r, cx - s * 0.30f, cy - s * 0.32f, cx - s * 0.30f, cy + s * 0.32f, cx + s * 0.16f, cy, c, a);
    fill(r, cx + s * 0.18f, cy - s * 0.32f, s * 0.11f, s * 0.64f, c, a);
}

static void icon_back(SDL_Renderer *r, float cx, float cy, float s, SDL_Color c, float a) {
    line(r, cx + s * 0.14f, cy - s * 0.30f, cx - s * 0.16f, cy, s * 0.09f, c, a);
    line(r, cx - s * 0.16f, cy, cx + s * 0.14f, cy + s * 0.30f, s * 0.09f, c, a);
}

static void icon_check(SDL_Renderer *r, float cx, float cy, float s, SDL_Color c, float a) {
    line(r, cx - s * 0.30f, cy, cx - s * 0.08f, cy + s * 0.22f, s * 0.12f, c, a);
    line(r, cx - s * 0.08f, cy + s * 0.22f, cx + s * 0.32f, cy - s * 0.24f, s * 0.12f, c, a);
}

// ------------------------------------------------------------------ partes do HUD
static int right_controls_x(const PlayerHud *h, int focus) {
    // Da direita para a esquerda: proximo episodio (quando existe) e faixas.
    if (focus == PUI_FOCUS_NEXT) return X_RIGHT;
    if (focus == PUI_FOCUS_TRACKS) return h->has_next ? X_RIGHT - 66 : X_RIGHT;
    return 0;
}

static int control_x(const PlayerHud *h, int focus) {
    switch (focus) {
        case PUI_FOCUS_PLAY: return X_PLAY;
        case PUI_FOCUS_REW: return X_REW;
        case PUI_FOCUS_FWD: return X_FWD;
        case PUI_FOCUS_VOLUME: return X_VOL;
        default: return right_controls_x(h, focus);
    }
}

const char *pui_focus_label(const PlayerHud *h, int focus, char *out, int cap) {
    switch (focus) {
        case PUI_FOCUS_PLAY: snprintf(out, cap, "%s", h->paused ? "Continuar" : "Pausar"); break;
        case PUI_FOCUS_REW: snprintf(out, cap, "Voltar 10 s"); break;
        case PUI_FOCUS_FWD: snprintf(out, cap, "Avancar 10 s"); break;
        case PUI_FOCUS_VOLUME: snprintf(out, cap, "Volume %d%%", h->volume); break;
        case PUI_FOCUS_TRACKS: snprintf(out, cap, "Audio e legendas"); break;
        case PUI_FOCUS_NEXT: snprintf(out, cap, "Proximo episodio"); break;
        case PUI_FOCUS_TIMELINE: snprintf(out, cap, "Esquerda/direita para buscar"); break;
        default: out[0] = '\0'; break;
    }
    return out;
}

static void draw_control(SDL_Renderer *r, const PlayerHud *h, int focus, float a) {
    int x = control_x(h, focus);
    if (!x) return;
    int focused = h->focus == focus;
    float s = focused ? 44.0f : 40.0f;
    if (focused) {
        circle(r, x, CTRL_Y, CTRL_R + 4, K_WHITE, 0.20f * a);
        arc(r, x, CTRL_Y, CTRL_R + 4, 2.5f, 0, 2 * PI_F, K_WHITE, 0.95f * a);
    }
    SDL_Color c = focused ? K_WHITE : K_MUTED;
    switch (focus) {
        case PUI_FOCUS_PLAY: if (h->paused) icon_play(r, x + 2, CTRL_Y, s, c, a); else icon_pause(r, x, CTRL_Y, s, c, a); break;
        case PUI_FOCUS_REW: icon_skip(r, x, CTRL_Y, s, 0, 10, c, a); break;
        case PUI_FOCUS_FWD: icon_skip(r, x, CTRL_Y, s, 1, 10, c, a); break;
        case PUI_FOCUS_VOLUME: icon_volume(r, x + 2, CTRL_Y, s, h->volume, c, a); break;
        case PUI_FOCUS_TRACKS: icon_tracks(r, x, CTRL_Y, s, c, a); break;
        case PUI_FOCUS_NEXT: icon_next(r, x, CTRL_Y, s, c, a); break;
        default: break;
    }
}

static void draw_timeline(SDL_Renderer *r, const PlayerHud *h, float a) {
    char cur[24], total[24], timebuf[64];
    pui_format_time(h->scrubbing ? h->scrub_target : h->pos, cur, sizeof(cur));
    pui_format_time(h->dur, total, sizeof(total));
    if (h->dur > 0) snprintf(timebuf, sizeof(timebuf), "%s / %s", cur, total);
    else snprintf(timebuf, sizeof(timebuf), "%s", cur);
    int tw = text_w(r, timebuf, ST_TIME);
    int x1 = PUI_W - 48 - tw - 24;
    int w = x1 - BAR_X0;
    int focused = h->focus == PUI_FOCUS_TIMELINE || h->scrubbing;
    float bh = focused ? 10.0f : 6.0f;
    float y = BAR_Y - bh / 2;
    double shown = h->scrubbing ? h->scrub_target : h->pos;
    float fp = h->dur > 0 ? (float)(h->pos / h->dur) : 0;
    float ft = h->dur > 0 ? (float)(shown / h->dur) : 0;
    fp = clamp01(fp); ft = clamp01(ft);

    rrect(r, BAR_X0, y, w, bh, bh / 2, K_WHITE, 0.28f * a);
    if (h->scrubbing) {
        float lo = fp < ft ? fp : ft, hi = fp < ft ? ft : fp;
        rrect(r, BAR_X0, y, w * lo, bh, bh / 2, K_ACC, a);
        fill(r, BAR_X0 + w * lo, y, w * (hi - lo), bh, K_WHITE, 0.55f * a);
    } else {
        rrect(r, BAR_X0, y, w * fp, bh, bh / 2, K_ACC, a);
    }
    for (int i = 0; i < h->chapter_count && h->dur > 0; i++) {
        float cx = BAR_X0 + w * clamp01((float)(h->chapters[i] / h->dur));
        if (cx > BAR_X0 + 2 && cx < x1 - 2) fill(r, cx - 1.5f, y, 3, bh, K_BLACK, 0.75f * a);
    }
    float thumb_x = BAR_X0 + w * ft;
    if (focused) {
        circle(r, thumb_x, BAR_Y, 11, K_WHITE, a);
        circle(r, thumb_x, BAR_Y, 5, K_ACC, a);
    } else {
        circle(r, thumb_x, BAR_Y, 7, K_WHITE, a);
    }
    text_a(r, timebuf, PUI_W - 48 - tw, BAR_Y - 12, K_WHITE, ST_TIME, a, 0);

    if (h->scrubbing) {
        // Balao sobre o ponto escolhido: tempo alvo, diferenca e capitulo.
        char target[24], delta[40];
        pui_format_time(h->scrub_target, target, sizeof(target));
        double diff = h->scrub_target - h->pos;
        char dt[24]; pui_format_time(diff < 0 ? -diff : diff, dt, sizeof(dt));
        snprintf(delta, sizeof(delta), "%s%s", diff < 0 ? "-" : "+", dt);
        int w1 = text_w(r, target, ST_TIME), w2 = text_w(r, delta, ST_SMALL);
        int w3 = h->scrub_label ? text_w(r, h->scrub_label, ST_SMALL) : 0;
        int w4 = text_w(r, "A  Confirmar     B  Cancelar", ST_SMALL);
        if (w3 > 300) w3 = 300;
        int bw = w1 + 16 + w2 + 32;
        if (w3 + 32 > bw) bw = w3 + 32;
        if (w4 + 32 > bw) bw = w4 + 32;
        int bhh = h->scrub_label ? 94 : 68;
        float bx = thumb_x - bw / 2.0f;
        if (bx < 24) bx = 24;
        if (bx + bw > PUI_W - 24) bx = PUI_W - 24 - bw;
        float by = BAR_Y - 22 - bhh;
        rrect(r, bx, by, bw, bhh, 10, K_PANEL, 0.94f * a);
        tri(r, thumb_x - 8, by + bhh, thumb_x + 8, by + bhh, thumb_x, by + bhh + 8, K_PANEL, 0.94f * a);
        int row = (int)by + 10;
        if (h->scrub_label) { text_a(r, h->scrub_label, (int)bx + 16, row, K_ACC, ST_SMALL, a, bw - 32); row += 26; }
        text_a(r, target, (int)bx + 16, row, K_WHITE, ST_TIME, a, 0);
        text_a(r, delta, (int)bx + 16 + w1 + 16, row + 2, K_DIM, ST_SMALL, a, 0);
        text_a(r, "A  Confirmar     B  Cancelar", (int)bx + 16, row + 27,
               K_MUTED, ST_SMALL, a, bw - 32);
    }
}

static void draw_bottom(SDL_Renderer *r, const PlayerHud *h, float a) {
    vgrad(r, 0, 420, PUI_W, 300, K_BLACK, 0.0f, 0.93f * a);
    draw_timeline(r, h, a);
    draw_control(r, h, PUI_FOCUS_PLAY, a);
    draw_control(r, h, PUI_FOCUS_REW, a);
    draw_control(r, h, PUI_FOCUS_FWD, a);
    draw_control(r, h, PUI_FOCUS_VOLUME, a);
    if (h->audio_count > 1 || h->sub_count > 1) draw_control(r, h, PUI_FOCUS_TRACKS, a);
    if (h->has_next) draw_control(r, h, PUI_FOCUS_NEXT, a);

    int right_limit = (h->audio_count > 1 || h->sub_count > 1)
        ? right_controls_x(h, PUI_FOCUS_TRACKS) - 44 : (h->has_next ? X_RIGHT - 44 : PUI_W - 48);
    int maxw = right_limit - X_TITLE;
    const char *title = h->title && h->title[0] ? h->title : "Reproducao";
    if (h->subtitle && h->subtitle[0]) {
        text_a(r, title, X_TITLE, CTRL_Y - 26, K_WHITE, ST_NORMAL, a, maxw);
        text_a(r, h->subtitle, X_TITLE, CTRL_Y + 4, K_DIM, ST_SMALL, a, maxw);
    } else {
        text_a(r, title, X_TITLE, CTRL_Y - 14, K_WHITE, ST_NORMAL, a, maxw);
    }

    char label[64];
    pui_focus_label(h, h->focus, label, sizeof(label));
    if (label[0] && h->focus != PUI_FOCUS_TIMELINE && h->focus != PUI_FOCUS_NEXT_CARD) {
        int cx = control_x(h, h->focus), lw = text_w(r, label, ST_SMALL);
        if (cx - lw / 2 < 24) cx = 24 + lw / 2;
        if (cx + lw / 2 > PUI_W - 24) cx = PUI_W - 24 - lw / 2;
        text_center_a(r, label, cx, CTRL_Y + 32, K_WHITE, ST_SMALL, a);
    }
}

static void draw_top(SDL_Renderer *r, const PlayerHud *h, float a) {
    (void)h;
    vgrad(r, 0, 0, PUI_W, 150, K_BLACK, 0.72f * a, 0.0f);
    icon_back(r, 60, 58, 40, K_WHITE, 0.95f * a);
    text_a(r, "B  Voltar", 88, 45, K_MUTED, ST_SMALL, a, 0);
}

static WrapCache g_overview_wrap;

static void draw_pause_info(SDL_Renderer *r, const PlayerHud *h, float a) {
    if (a <= 0.01f) return;
    hgrad(r, 0, 0, 820, PUI_H, (SDL_Color){ 6, 8, 13, 255 }, 0.72f * a, 0.0f);
    float slide = (1.0f - a) * -14.0f;
    int x = 64 + (int)slide;
    int y = 196;
    text_a(r, "VOCE ESTA ASSISTINDO", x, y, K_MUTED, ST_SMALL, a, 0);
    y += 28;
    const char *title = h->title && h->title[0] ? h->title : "Reproducao";
    text_a(r, title, x, y, K_WHITE, ST_DISPLAY, a, 720);
    y += 56;
    if (h->subtitle && h->subtitle[0]) { text_a(r, h->subtitle, x, y, K_ACC2, ST_NORMAL, a, 720); y += 38; }
    int lines = wrap(&g_overview_wrap, h->overview, ST_NORMAL, 640, 3);
    for (int i = 0; i < lines; i++) text_a(r, g_overview_wrap.lines[i], x, y + i * 31, K_MUTED, ST_NORMAL, a, 680);
}

static void draw_volume_popup(SDL_Renderer *r, const PlayerHud *h) {
    float a = h->volume_popup_alpha;
    if (a <= 0.01f) return;
    float x = X_VOL - 30, y = 408, w = 60, hh = 162;
    rrect(r, x, y, w, hh, 14, K_PANEL, 0.94f * a);
    char pct[16]; snprintf(pct, sizeof(pct), "%d%%", h->volume);
    text_center_a(r, pct, X_VOL, (int)y + 10, K_WHITE, ST_SMALL, a);
    float tx = X_VOL - 4, ty = y + 42, th = hh - 58;
    rrect(r, tx, ty, 8, th, 4, K_WHITE, 0.25f * a);
    float fh = th * h->volume / 100.0f;
    rrect(r, tx, ty + th - fh, 8, fh, 4, K_ACC, a);
    circle(r, tx + 4, ty + th - fh, 9, K_WHITE, a);
}

static void draw_next_card(SDL_Renderer *r, const PlayerHud *h) {
    float a = h->next_card_alpha;
    if (!h->has_next || a <= 0.01f) return;
    float w = 360, hh = 118, x = PUI_W - 48 - w + (1 - a) * 30, y = 432;
    int focused = h->focus == PUI_FOCUS_NEXT_CARD;
    rrect(r, x, y, w, hh, 12, K_PANEL, 0.92f * a);
    if (focused) rrect_outline(r, x - 3, y - 3, w + 6, hh + 6, 14, 3, K_WHITE, a);
    icon_next(r, x + 38, y + 44, 34, K_WHITE, a);
    text_a(r, "PROXIMO EPISODIO", (int)x + 72, (int)y + 16, K_MUTED, ST_SMALL, a, 0);
    text_a(r, h->next_title && h->next_title[0] ? h->next_title : "Continuar a serie",
           (int)x + 72, (int)y + 40, K_WHITE, ST_NORMAL, a, (int)w - 90);
    text_a(r, focused ? "A  Assistir agora" : "Direita para escolher", (int)x + 72, (int)y + 74,
           focused ? K_WHITE : K_DIM, ST_SMALL, a, 0);
    rrect(r, x + 16, y + hh - 10, w - 32, 4, 2, K_WHITE, 0.2f * a);
    rrect(r, x + 16, y + hh - 10, (w - 32) * clamp01(h->next_card_progress), 4, 2, K_ACC, a);
}

static void draw_flash(SDL_Renderer *r, const PlayerHud *h) {
    if (h->flash == PUI_FLASH_NONE) return;
    float t = clamp01(h->flash_t);
    float a = 1.0f - t;
    if (a <= 0.01f) return;
    float scale = 0.85f + 0.3f * t;
    if (h->flash == PUI_FLASH_PLAY || h->flash == PUI_FLASH_PAUSE) {
        float rad = 54 * scale;
        circle(r, PUI_W / 2.0f, PUI_H / 2.0f, rad, K_BLACK, 0.55f * a);
        if (h->flash == PUI_FLASH_PLAY) icon_play(r, PUI_W / 2.0f + 4, PUI_H / 2.0f, 56 * scale, K_WHITE, a);
        else icon_pause(r, PUI_W / 2.0f, PUI_H / 2.0f, 56 * scale, K_WHITE, a);
        return;
    }
    int forward = h->flash == PUI_FLASH_FWD;
    float cx = forward ? PUI_W * 0.76f : PUI_W * 0.24f, cy = PUI_H / 2.0f;
    circle(r, cx, cy, 58 * scale, K_BLACK, 0.5f * a);
    icon_skip(r, cx, cy - 8, 52 * scale, forward, h->flash_seconds, K_WHITE, a);
    char label[16]; snprintf(label, sizeof(label), "%s%d s", forward ? "+" : "-", h->flash_seconds);
    text_center_a(r, label, (int)cx, (int)(cy + 26 * scale), K_WHITE, ST_SMALL, a);
}

static void draw_notice(SDL_Renderer *r, const PlayerHud *h) {
    if (!h->notice || !h->notice[0] || h->notice_alpha <= 0.01f) return;
    float a = h->notice_alpha;
    int w = text_w(r, h->notice, ST_NORMAL);
    if (w > 760) w = 760;
    float x = (PUI_W - w) / 2.0f - 26, y = 32 + (1 - a) * -10;
    rrect(r, x, y, w + 52, 50, 25, K_PANEL, 0.9f * a);
    circle(r, x + 22, y + 25, 5, K_ACC, a);
    text_a(r, h->notice, (int)x + 34, (int)y + 11, K_WHITE, ST_NORMAL, a, 760);
}

static WrapCache g_sub_wrap;

static void draw_subtitle(SDL_Renderer *r, const PlayerHud *h) {
    if (!h->subtitle_text || !h->subtitle_text[0]) return;
    // Sobe quando a barra aparece, para nunca ficar embaixo dos controles.
    float raise = clamp01(h->hud_alpha);
    int bottom = (int)(668 - raise * 130);
    // Com o cartao de proximo episodio na tela, a legenda sobe acima dele.
    if (h->has_next && h->next_card_alpha > 0.01f) {
        int above_card = 420;
        bottom = (int)(bottom + (above_card - bottom) * clamp01(h->next_card_alpha));
        if (bottom > above_card && h->next_card_alpha > 0.5f) bottom = above_card;
    }
    int lines = wrap(&g_sub_wrap, h->subtitle_text, ST_SUB, PUI_W - 200, 2);
    int lh = 38;
    int y = bottom - lines * lh;
    for (int i = 0; i < lines; i++) {
        const char *s = g_sub_wrap.lines[i];
        int w = text_w(r, s, ST_SUB);
        int x = (PUI_W - w) / 2;
        rrect(r, x - 12, y + i * lh - 2, w + 24, lh, 6, K_BLACK, 0.45f);
        text_a(r, s, x + 2, y + i * lh + 2, K_BLACK, ST_SUB, 0.9f, 0);
        text_a(r, s, x, y + i * lh, K_WHITE, ST_SUB, 1.0f, 0);
    }
}

static void draw_buffering(SDL_Renderer *r, const PlayerHud *h, Uint32 now) {
    if (!h->buffering) return;
    fill(r, 0, 0, PUI_W, PUI_H, K_BLACK, 0.35f);
    float cx = PUI_W / 2.0f, cy = PUI_H / 2.0f - 20;
    float spin = (now % 1200) / 1200.0f * 2 * PI_F;
    arc(r, cx, cy, 34, 6, 0, 2 * PI_F, K_WHITE, 0.18f);
    arc(r, cx, cy, 34, 6, spin, 1.9f, K_ACC, 1.0f);
    if (h->buffering_text) text_center_a(r, h->buffering_text, (int)cx, (int)cy + 54, K_WHITE, ST_NORMAL, 1.0f);
}

static void draw_track_column(SDL_Renderer *r, int x, int y, int width, const char *heading, int active,
                              const char *const *names, const char *const *details,
                              int count, int sel, int current) {
    text_a(r, heading, x, y, active ? K_ACC : K_DIM, ST_SMALL, 1.0f, 0);
    fill(r, x, y + 28, width, 2, active ? K_ACC : K_WHITE, active ? 0.9f : 0.15f);
    const int row_h = 56, visible = 6;
    int first = sel - visible / 2;
    if (first > count - visible) first = count - visible;
    if (first < 0) first = 0;
    int ry = y + 42;
    for (int i = first; i < count && i < first + visible; i++, ry += row_h) {
        int focused = active && i == sel;
        if (focused) {
            rrect(r, x, ry, width, row_h - 6, 8, K_WHITE, 0.12f);
            fill(r, x, ry + 8, 4, row_h - 22, K_ACC, 1.0f);
        }
        if (i == current) icon_check(r, x + 26, ry + (row_h - 6) / 2.0f, 22, focused ? K_WHITE : K_ACC, 1.0f);
        const char *name = names[i] ? names[i] : "Faixa";
        const char *detail = details ? details[i] : NULL;
        SDL_Color nc = focused || i == current ? K_WHITE : K_MUTED;
        if (detail && detail[0]) {
            text_a(r, name, x + 50, ry + 4, nc, ST_NORMAL, 1.0f, width - 60);
            text_a(r, detail, x + 50, ry + 29, K_DIM, ST_SMALL, 1.0f, width - 60);
        } else {
            text_a(r, name, x + 50, ry + 12, nc, ST_NORMAL, 1.0f, width - 60);
        }
    }
    if (count > visible) {
        char more[32]; snprintf(more, sizeof(more), "%d de %d", sel + 1, count);
        text_right_a(r, more, x + width, y, K_DIM, ST_SMALL, 1.0f);
    }
}

static void draw_panel(SDL_Renderer *r, const PlayerHud *h) {
    if (!h->panel_open) return;
    fill(r, 0, 0, PUI_W, PUI_H, K_BLACK, 0.72f);
    float x = 170, y = 96, w = 940, hh = 528;
    rrect(r, x, y, w, hh, 18, K_PANEL, 0.97f);
    text_a(r, "Audio e legendas", (int)x + 40, (int)y + 30, K_WHITE, ST_TITLE, 1.0f, 0);
    int col_w = 410;
    draw_track_column(r, (int)x + 40, (int)y + 96, col_w, "AUDIO", h->panel_column == 0,
                      h->audio_names, h->audio_details, h->audio_count, h->audio_sel, h->audio_current);
    draw_track_column(r, (int)x + 40 + col_w + 40, (int)y + 96, col_w, "LEGENDAS", h->panel_column == 1,
                      h->sub_names, NULL, h->sub_count, h->sub_sel, h->sub_current);
    text_a(r, "Esquerda/direita  Coluna      Cima/baixo  Escolher      A  Aplicar      B  Fechar",
           (int)x + 40, (int)(y + hh - 44), K_DIM, ST_SMALL, 1.0f, (int)w - 80);
}

void pui_draw(SDL_Renderer *r, const PlayerHud *h, Uint32 now) {
    if (!r || !h) return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    float a = clamp01(h->hud_alpha);
    draw_subtitle(r, h);
    if (a > 0.01f) {
        draw_pause_info(r, h, clamp01(h->pause_info_alpha) * a);
        draw_top(r, h, a);
        draw_bottom(r, h, a);
        draw_volume_popup(r, h);
    }
    draw_next_card(r, h);
    draw_flash(r, h);
    draw_buffering(r, h, now);
    draw_notice(r, h);
    draw_panel(r, h);
}

void pui_draw_loading(SDL_Renderer *r, const char *title, const char *headline,
                      const char *detail, Uint32 now, int warning) {
    if (!r) return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 8, 10, 15, 255);
    SDL_RenderClear(r);
    vgrad(r, 0, 0, PUI_W, PUI_H, (SDL_Color){ 30, 24, 60, 255 }, 0.55f, 0.0f);
    float cx = PUI_W / 2.0f, cy = 350;
    float spin = (now % 1400) / 1400.0f * 2 * PI_F;
    SDL_Color accent = warning ? K_ROSE : K_ACC;
    // A pipoca e a identidade da espera do Nplay. O atlas e animado e fica
    // embutido no NRO, portanto nao depende de rede nem disputa memoria com capas.
    ui_popcorn_draw(r, (int)cx, 132, 188);
    arc(r, cx, cy, 24, 4, 0, 2 * PI_F, K_WHITE, 0.10f);
    arc(r, cx, cy, 24, 4, spin, 2.0f, accent, 1.0f);
    text_center_a(r, headline && headline[0] ? headline : "Preparando", (int)cx, 396, K_WHITE, ST_TITLE, 1.0f);
    if (title && title[0]) {
        int w = text_w(r, title, ST_NORMAL);
        if (w > 900) w = 900;
        text_a(r, title, (int)cx - w / 2, 442, K_MUTED, ST_NORMAL, 1.0f, 900);
    }
    if (detail && detail[0]) text_center_a(r, detail, (int)cx, 482, K_DIM, ST_SMALL, 1.0f);
    text_center_a(r, "B  Cancelar", (int)cx, 652, K_DIM, ST_SMALL, 1.0f);
}
