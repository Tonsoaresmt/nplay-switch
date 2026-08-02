// Nplay Switch - app homebrew do Nplay para Nintendo Switch.
// Fase 1: login + catalogo navegavel (Filmes/Series/Animes/Doramas/Canais)
// com capas. Reaproveita net/store/text/cJSON do Meruem. O player de video
// (ffmpeg) vem na Fase 2.
#include <switch.h>
#include <SDL.h>
#include <SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "net.h"
#include "store.h"
#include "text.h"
#include "cJSON.h"

#define WIN_W 1280
#define WIN_H 720

// Mapeamento dos botoes do Joy-Con/Pro via SDL (igual ao Meruem).
#define JOY_A 0
#define JOY_B 1
#define JOY_X 2
#define JOY_Y 3
#define JOY_L 6
#define JOY_R 7
#define JOY_PLUS 10
#define JOY_MINUS 11
#define JOY_DLEFT 12
#define JOY_UP 13
#define JOY_DRIGHT 14
#define JOY_DOWN 15
#define JOY_ZL 8
#define JOY_ZR 9

static const char *BASE = "https://nplay.tonserverlocal.uk";

static SDL_Renderer *gRen = NULL;
static SDL_Joystick *g_joy = NULL;
static char g_token[640] = {0};
static char g_status[160] = {0};
static Uint32 g_toast_until = 0;
static char g_toast[128] = {0};

static const SDL_Color C_BG   = {  8, 10, 15, 255 };
static const SDL_Color C_BAR  = { 12, 15, 23, 255 };
static const SDL_Color C_CARD = { 20, 24, 36, 255 };
static const SDL_Color C_TEXT = { 234, 240, 250, 255 };
static const SDL_Color C_MUT  = { 139, 150, 173, 255 };
static const SDL_Color C_ACC  = { 139, 92, 246, 255 };
static const SDL_Color C_ACC2 = { 59, 130, 246, 255 };

// ------------------------------------------------------------- helpers
static void fill_rect(int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(gRen, c.r, c.g, c.b, c.a);
    SDL_Rect r = { x, y, w, h };
    SDL_RenderFillRect(gRen, &r);
}
static void border_rect(int x, int y, int w, int h, int th, SDL_Color c) {
    fill_rect(x, y, w, th, c);
    fill_rect(x, y + h - th, w, th, c);
    fill_rect(x, y, th, h, c);
    fill_rect(x + w - th, y, th, h, c);
}
static void toast(const char *msg) {
    strncpy(g_toast, msg, sizeof(g_toast) - 1);
    g_toast[sizeof(g_toast) - 1] = '\0';
    g_toast_until = SDL_GetTicks() + 2200;
}

// Teclado do sistema (login).
static int prompt_text(const char *guide, char *out, size_t cap, int password) {
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

// GET na API -> cJSON (caller libera) ou NULL.
static cJSON *api_get(const char *path) {
    char url[1024];
    snprintf(url, sizeof(url), "%s%s", BASE, path);
    struct membuf out = { 0 };
    const char *err = NULL;
    long code = net_request(url, "GET", NULL, g_token[0] ? g_token : NULL, &out, &err);
    cJSON *j = NULL;
    if (code == 200 && out.data) j = cJSON_Parse(out.data);
    membuf_free(&out);
    return j;
}

// ------------------------------------------------------------- cache de capas
#define MAX_COV 600
typedef struct { char url[720]; SDL_Texture *tex; int tried; } Cover;
static Cover g_cov[MAX_COV];
static int g_covN = 0;

static SDL_Texture *cover_get(const char *url) {
    if (!url || !url[0]) return NULL;
    for (int i = 0; i < g_covN; i++)
        if (!strcmp(g_cov[i].url, url)) return g_cov[i].tex;
    if (g_covN < MAX_COV) {
        strncpy(g_cov[g_covN].url, url, sizeof(g_cov[0].url) - 1);
        g_cov[g_covN].url[sizeof(g_cov[0].url) - 1] = '\0';
        g_cov[g_covN].tex = NULL;
        g_cov[g_covN].tried = 0;
        g_covN++;
    }
    return NULL;
}
// Baixa UMA capa ainda nao tentada por chamada (preenche a grade progressivamente).
static void cover_load_one(void) {
    for (int i = 0; i < g_covN; i++) {
        if (g_cov[i].tried) continue;
        g_cov[i].tried = 1;
        char url[900];
        if (strncmp(g_cov[i].url, "http", 4) == 0) snprintf(url, sizeof(url), "%s", g_cov[i].url);
        else snprintf(url, sizeof(url), "%s%s", BASE, g_cov[i].url);
        struct membuf out = { 0 };
        const char *err = NULL;
        long code = net_request(url, "GET", NULL, NULL, &out, &err);
        if (code == 200 && out.data && out.len > 32) {
            SDL_RWops *rw = SDL_RWFromMem(out.data, (int)out.len);
            SDL_Surface *s = IMG_Load_RW(rw, 1);
            if (s) { g_cov[i].tex = SDL_CreateTextureFromSurface(gRen, s); SDL_FreeSurface(s); }
        }
        membuf_free(&out);
        return;
    }
}

// ------------------------------------------------------------- catalogo
static const char *TAB_NAME[] = { "Filmes", "Series", "Animes", "Doramas", "Canais" };
#define NTABS 5
static int g_tab = 0, g_sel = 0, g_page = 1;
static cJSON *g_list = NULL;   // {items:[...], total, ...}

static const char *tab_path(int tab, int page) {
    static char p[160];
    switch (tab) {
        case 0: snprintf(p, sizeof(p), "/api/catalog/movies?page=%d", page); break;
        case 1: snprintf(p, sizeof(p), "/api/catalog/series?page=%d", page); break;
        case 2: snprintf(p, sizeof(p), "/api/catalog/series?section=anime&page=%d", page); break;
        case 3: snprintf(p, sizeof(p), "/api/catalog/series?q=dorama&page=%d", page); break;
        default: snprintf(p, sizeof(p), "/api/catalog/live?page=%d", page); break;
    }
    return p;
}
static int list_count(void) {
    if (!g_list) return 0;
    cJSON *items = cJSON_GetObjectItem(g_list, "items");
    return cJSON_IsArray(items) ? cJSON_GetArraySize(items) : 0;
}
static cJSON *list_item(int i) {
    if (!g_list) return NULL;
    cJSON *items = cJSON_GetObjectItem(g_list, "items");
    return cJSON_IsArray(items) ? cJSON_GetArrayItem(items, i) : NULL;
}
static void load_tab(void) {
    if (g_list) { cJSON_Delete(g_list); g_list = NULL; }
    g_sel = 0;
    g_list = api_get(tab_path(g_tab, g_page));
    if (!g_list) snprintf(g_status, sizeof(g_status), "Falha ao carregar %s (rede/login)", TAB_NAME[g_tab]);
    else g_status[0] = '\0';
}

// layout da grade
#define TB 66
#define COLS 6
#define MX 40
#define GAP 16
#define CW 180
#define COVER_W 158
#define COVER_H 226
#define CH 268

static void draw_catalog(int scrollY) {
    // barra superior com abas
    fill_rect(0, 0, WIN_W, TB, C_BAR);
    text_draw(gRen, "Nplay", MX, 18, C_ACC, 1);
    int tx = 180;
    for (int t = 0; t < NTABS; t++) {
        SDL_Color col = (t == g_tab) ? C_TEXT : C_MUT;
        int w = text_draw(gRen, TAB_NAME[t], tx, 20, col, 0);
        if (t == g_tab) fill_rect(tx, 46, w, 3, C_ACC);
        tx += w + 34;
    }
    text_draw(gRen, "L/R aba  A abre  (-) sair conta  (+) sair", WIN_W - 470, 24, C_MUT, 0);

    int n = list_count();
    if (n == 0) {
        text_draw(gRen, g_status[0] ? g_status : "Carregando...", MX, TB + 40, C_MUT, 0);
        return;
    }
    int gridTop = TB + 16;
    for (int i = 0; i < n; i++) {
        int col = i % COLS, row = i / COLS;
        int x = MX + col * (CW + GAP);
        int y = gridTop + row * (CH + GAP) - scrollY;
        if (y + CH < TB || y > WIN_H) continue;  // fora da tela
        cJSON *it = list_item(i);
        const char *title = "";
        const char *logo = NULL;
        if (it) {
            cJSON *jt = cJSON_GetObjectItem(it, "title"); if (jt && jt->valuestring) title = jt->valuestring;
            cJSON *jl = cJSON_GetObjectItem(it, "logo");  if (jl && jl->valuestring) logo = jl->valuestring;
        }
        int cx = x + (CW - COVER_W) / 2;
        // capa
        SDL_Texture *tex = cover_get(logo);
        SDL_Rect cr = { cx, y, COVER_W, COVER_H };
        if (tex) {
            SDL_RenderCopy(gRen, tex, NULL, &cr);
        } else {
            fill_rect(cx, y, COVER_W, COVER_H, C_CARD);
            char ini[2] = { title[0] ? title[0] : '?', 0 };
            text_draw(gRen, ini, cx + COVER_W / 2 - 8, y + COVER_H / 2 - 18, C_MUT, 1);
        }
        // titulo (curto)
        char sh[40];
        int k = 0;
        for (const char *p = title; *p && k < 22; p++, k++) sh[k] = *p;
        sh[k] = '\0';
        if (k >= 22) { sh[20] = '.'; sh[21] = '.'; }
        text_draw(gRen, sh, cx, y + COVER_H + 6, C_TEXT, 0);
        // selecao
        if (i == g_sel) border_rect(cx - 4, y - 4, COVER_W + 8, COVER_H + 8, 3, C_ACC2);
    }
}

// ------------------------------------------------------------- login
static int do_login(void) {
    char user[128] = { 0 }, pass[128] = { 0 };
    if (prompt_text("Usuario Nplay", user, sizeof(user), 0) != 0) return -1;
    if (prompt_text("Senha", pass, sizeof(pass), 1) != 0) return -1;
    char body[640];
    snprintf(body, sizeof(body),
        "{\"username\":\"%s\",\"password\":\"%s\",\"device\":{\"fingerprint\":\"nplay-switch\",\"type\":\"tv\",\"name\":\"Nintendo Switch\"}}",
        user, pass);
    char url[512];
    snprintf(url, sizeof(url), "%s/api/auth/login", BASE);
    struct membuf out = { 0 };
    const char *err = NULL;
    long code = net_request(url, "POST", body, NULL, &out, &err);
    int ok = -1;
    if (out.data) {
        cJSON *j = cJSON_Parse(out.data);
        if (j) {
            cJSON *t = cJSON_GetObjectItem(j, "token");
            if (code == 200 && t && t->valuestring) {
                strncpy(g_token, t->valuestring, sizeof(g_token) - 1);
                store_save_token(g_token);
                store_save_user(user);
                ok = 0;
            } else {
                cJSON *e = cJSON_GetObjectItem(j, "error");
                snprintf(g_status, sizeof(g_status), "%s", (e && e->valuestring) ? e->valuestring : "Falha no login");
            }
            cJSON_Delete(j);
        }
    } else {
        snprintf(g_status, sizeof(g_status), "Sem conexao (%s)", err ? err : "rede");
    }
    membuf_free(&out);
    return ok;
}

static void draw_login(void) {
    SDL_Color rose = { 251, 113, 133, 255 };
    text_draw(gRen, "Nplay", WIN_W / 2 - 70, 220, C_ACC, 1);
    text_draw(gRen, "Aperte  A  para entrar com sua conta", WIN_W / 2 - 220, 320, C_TEXT, 0);
    text_draw(gRen, "(+) para sair do app", WIN_W / 2 - 110, 360, C_MUT, 0);
    if (g_status[0]) text_draw(gRen, g_status, WIN_W / 2 - 220, 420, rose, 0);
}

// ------------------------------------------------------------- main
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    socketInitializeDefault();
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_WEBP);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_Window *win = SDL_CreateWindow("Nplay", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    gRen = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawBlendMode(gRen, SDL_BLENDMODE_BLEND);
    SDL_InitSubSystem(SDL_INIT_JOYSTICK);
    g_joy = SDL_JoystickOpen(0);

    text_init();
    net_init();
    store_init();

    store_load_token(g_token, sizeof(g_token));
    int logged = g_token[0] ? 1 : 0;
    if (logged) load_tab();

    int running = 1;
    int scrollY = 0;
    while (appletMainLoop() && running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = 0; break; }
            if (e.type != SDL_JOYBUTTONDOWN) continue;
            int b = e.jbutton.button;
            if (!logged) {
                if (b == JOY_A) { if (do_login() == 0) { logged = 1; g_page = 1; g_tab = 0; load_tab(); } }
                else if (b == JOY_PLUS) running = 0;
                continue;
            }
            int n = list_count();
            if (b == JOY_UP)         { if (g_sel - COLS >= 0) g_sel -= COLS; }
            else if (b == JOY_DOWN)  { if (g_sel + COLS < n)  g_sel += COLS; }
            else if (b == JOY_DLEFT) { if (g_sel > 0) g_sel--; }
            else if (b == JOY_DRIGHT){ if (g_sel + 1 < n) g_sel++; }
            else if (b == JOY_L || b == JOY_ZL) { g_tab = (g_tab - 1 + NTABS) % NTABS; g_page = 1; scrollY = 0; load_tab(); }
            else if (b == JOY_R || b == JOY_ZR) { g_tab = (g_tab + 1) % NTABS; g_page = 1; scrollY = 0; load_tab(); }
            else if (b == JOY_A) {
                cJSON *it = list_item(g_sel);
                cJSON *jt = it ? cJSON_GetObjectItem(it, "title") : NULL;
                char m[160];
                snprintf(m, sizeof(m), "\"%s\" - player chega na Fase 2", (jt && jt->valuestring) ? jt->valuestring : "item");
                toast(m);
            }
            else if (b == JOY_MINUS) { store_clear_token(); g_token[0] = '\0'; logged = 0; g_status[0] = '\0'; }
            else if (b == JOY_PLUS)  { running = 0; }
        }

        // rolagem p/ manter a selecao visivel
        if (logged) {
            int selRow = g_sel / COLS;
            int rowTop = (TB + 16) + selRow * (CH + GAP);
            int rowBot = rowTop + CH;
            if (rowBot - scrollY > WIN_H) scrollY = rowBot - WIN_H + 16;
            if (rowTop - scrollY < TB + 16) scrollY = rowTop - (TB + 16);
            if (scrollY < 0) scrollY = 0;
        }

        // render
        SDL_SetRenderDrawColor(gRen, C_BG.r, C_BG.g, C_BG.b, 255);
        SDL_RenderClear(gRen);
        if (!logged) draw_login();
        else {
            draw_catalog(scrollY);
            cover_load_one();  // preenche uma capa por frame
        }
        if (g_toast[0] && SDL_GetTicks() < g_toast_until) {
            int w = 0, h = 0;
            SDL_Texture *tx = text_cached(gRen, g_toast, C_TEXT, 0, &w, &h);
            fill_rect(WIN_W / 2 - w / 2 - 18, WIN_H - 90, w + 36, h + 20, C_BAR);
            if (tx) { SDL_Rect d = { WIN_W / 2 - w / 2, WIN_H - 80, w, h }; SDL_RenderCopy(gRen, tx, NULL, &d); }
        }
        SDL_RenderPresent(gRen);
    }

    if (g_list) cJSON_Delete(g_list);
    for (int i = 0; i < g_covN; i++) if (g_cov[i].tex) SDL_DestroyTexture(g_cov[i].tex);
    text_exit();
    net_exit();
    if (g_joy) SDL_JoystickClose(g_joy);
    SDL_DestroyRenderer(gRen);
    SDL_DestroyWindow(win);
    IMG_Quit();
    SDL_Quit();
    socketExit();
    return 0;
}
