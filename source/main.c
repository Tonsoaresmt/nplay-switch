// Nplay Switch - app homebrew do Nplay para Nintendo Switch.
// Abas com RAILS por secao (como o app de PC): Inicio, Filmes, Series, Animes,
// Doramas (todas com hero + Lancamentos + Minha lista + prateleiras por genero),
// Baixados (torrent baixa no servidor e toca aqui) e Config. Busca global (Y),
// favoritar (X -> Minha lista). Capas em THREADS de fundo (navegacao fluida).
// Player de video via ffmpeg + libcurl (TLS), tocando https direto.
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
#include "update.h"
#include "player.h"

#define WIN_W 1280
#define WIN_H 720

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
static char g_toast[160] = {0};
static char g_self_path[600] = {0};
static char g_user[128] = {0};
static int g_do_update = 0;

static const SDL_Color C_BG   = {  8, 10, 15, 255 };
static const SDL_Color C_BAR  = { 12, 15, 23, 255 };
static const SDL_Color C_CARD = { 20, 24, 36, 255 };
static const SDL_Color C_TEXT = { 234, 240, 250, 255 };
static const SDL_Color C_MUT  = { 139, 150, 173, 255 };
static const SDL_Color C_ACC  = { 139, 92, 246, 255 };
static const SDL_Color C_ACC2 = { 59, 130, 246, 255 };
static const SDL_Color C_ROSE = { 251, 113, 133, 255 };
static const SDL_Color C_GREEN= { 52, 211, 153, 255 };

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
    g_toast_until = SDL_GetTicks() + 2600;
}
static void short_title(const char *title, char *out, int cap) {
    int k = 0;
    for (const char *p = title; *p && k < cap - 1; p++, k++) out[k] = *p;
    out[k] = '\0';
    if (k >= cap - 1 && k >= 2) { out[k - 2] = '.'; out[k - 1] = '.'; }
}
// Desenha texto recortado a uma largura (evita vazar pro card/coluna vizinha).
static void text_clip(const char *s, int x, int y, SDL_Color c, int big, int maxw) {
    SDL_Rect clip = { x, y - 3, maxw, big ? 40 : 30 };
    SDL_RenderSetClipRect(gRen, &clip);
    text_draw(gRen, s, x, y, c, big);
    SDL_RenderSetClipRect(gRen, NULL);
}
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
// POST/DELETE simples (retorna o codigo HTTP). Body padrao "{}" evita 415.
static long api_send(const char *path, const char *method, const char *body) {
    char url[1024];
    snprintf(url, sizeof(url), "%s%s", BASE, path);
    struct membuf out = { 0 };
    const char *err = NULL;
    long code = net_request(url, method, body ? body : "{}", g_token[0] ? g_token : NULL, &out, &err);
    membuf_free(&out);
    return code;
}
static const char *jstr(cJSON *o, const char *k) {
    cJSON *v = o ? cJSON_GetObjectItem(o, k) : NULL;
    return (v && v->valuestring) ? v->valuestring : NULL;
}
static int jint(cJSON *o, const char *k) {
    cJSON *v = o ? cJSON_GetObjectItem(o, k) : NULL;
    return v ? v->valueint : 0;
}
static int arr_len(cJSON *a) { return cJSON_IsArray(a) ? cJSON_GetArraySize(a) : 0; }

// ============================================================= capas (threads)
// state: 0 novo, 1 na fila/baixando, 2 surface pronta (main cria textura), 3 feito
#define MAX_COV 3000
typedef struct { char url[720]; SDL_Texture *tex; SDL_Surface *surf; int state; } Cover;
static Cover g_cov[MAX_COV];
static int g_covN = 0;
static SDL_mutex *g_cov_mtx;
static int g_q[MAX_COV]; static int g_qh = 0, g_qt = 0;
static SDL_mutex *g_q_mtx; static SDL_sem *g_q_sem;
static int g_run = 1;

static SDL_Texture *cover_get(const char *url) {   // chamado no main (render)
    if (!url || !url[0]) return NULL;
    SDL_LockMutex(g_cov_mtx);
    int f = -1;
    for (int i = 0; i < g_covN; i++) if (!strcmp(g_cov[i].url, url)) { f = i; break; }
    if (f < 0 && g_covN < MAX_COV) {
        f = g_covN++;
        strncpy(g_cov[f].url, url, sizeof(g_cov[0].url) - 1);
        g_cov[f].url[sizeof(g_cov[0].url) - 1] = '\0';
        g_cov[f].tex = NULL; g_cov[f].surf = NULL; g_cov[f].state = 0;
    }
    SDL_Texture *tex = (f >= 0) ? g_cov[f].tex : NULL;
    if (f >= 0 && g_cov[f].state == 0) {
        g_cov[f].state = 1;
        SDL_LockMutex(g_q_mtx);
        g_q[g_qt] = f; g_qt = (g_qt + 1) % MAX_COV;
        SDL_UnlockMutex(g_q_mtx);
        SDL_SemPost(g_q_sem);
    }
    SDL_UnlockMutex(g_cov_mtx);
    return tex;
}
static int cover_worker(void *arg) {
    (void)arg;
    while (g_run) {
        if (SDL_SemWaitTimeout(g_q_sem, 250) != 0) continue;
        if (!g_run) break;
        int idx = -1;
        SDL_LockMutex(g_q_mtx);
        if (g_qh != g_qt) { idx = g_q[g_qh]; g_qh = (g_qh + 1) % MAX_COV; }
        SDL_UnlockMutex(g_q_mtx);
        if (idx < 0) continue;
        char url[900];
        SDL_LockMutex(g_cov_mtx);
        if (strncmp(g_cov[idx].url, "http", 4) == 0) snprintf(url, sizeof(url), "%s", g_cov[idx].url);
        else snprintf(url, sizeof(url), "%s%s", BASE, g_cov[idx].url);
        SDL_UnlockMutex(g_cov_mtx);
        struct membuf out = { 0 };
        const char *err = NULL;
        long code = net_request(url, "GET", NULL, NULL, &out, &err);
        SDL_Surface *s = NULL;
        if (code == 200 && out.data && out.len > 32) {
            SDL_RWops *rw = SDL_RWFromMem(out.data, (int)out.len);
            s = IMG_Load_RW(rw, 1);
        }
        membuf_free(&out);
        SDL_LockMutex(g_cov_mtx);
        if (s) { g_cov[idx].surf = s; g_cov[idx].state = 2; }
        else g_cov[idx].state = 3;
        SDL_UnlockMutex(g_cov_mtx);
    }
    return 0;
}
static void cover_pump(void) {   // main: converte surfaces prontas em texturas
    for (int done = 0; done < 6; done++) {
        int idx = -1; SDL_Surface *s = NULL;
        SDL_LockMutex(g_cov_mtx);
        for (int i = 0; i < g_covN; i++) if (g_cov[i].state == 2) { idx = i; s = g_cov[i].surf; g_cov[i].surf = NULL; g_cov[i].state = 3; break; }
        SDL_UnlockMutex(g_cov_mtx);
        if (idx < 0) break;
        SDL_Texture *tex = SDL_CreateTextureFromSurface(gRen, s);
        SDL_FreeSurface(s);
        SDL_LockMutex(g_cov_mtx);
        g_cov[idx].tex = tex;
        SDL_UnlockMutex(g_cov_mtx);
    }
}

// ------------------------------------------------------------- card
static void draw_card(int x, int y, int cw, int coverH, cJSON *item, int selected, int fav) {
    const char *title = jstr(item, "title"); if (!title) title = "";
    const char *logo = jstr(item, "logo");
    SDL_Texture *tex = cover_get(logo);
    SDL_Rect cr = { x, y, cw, coverH };
    if (tex) SDL_RenderCopy(gRen, tex, NULL, &cr);
    else {
        fill_rect(x, y, cw, coverH, C_CARD);
        char ini[2] = { title[0] ? title[0] : '?', 0 };
        text_draw(gRen, ini, x + cw / 2 - 8, y + coverH / 2 - 16, C_MUT, 1);
    }
    if (fav) { fill_rect(x + cw - 26, y + 6, 20, 20, C_ROSE); text_draw(gRen, "*", x + cw - 21, y + 4, C_TEXT, 0); }
    char sh[48]; short_title(title, sh, (int)sizeof(sh));
    text_clip(sh, x, y + coverH + 6, selected ? C_TEXT : C_MUT, 0, cw);
    if (selected) border_rect(x - 3, y - 3, cw + 6, coverH + 6, 3, C_ACC2);
}

// ============================================================= estado / telas
typedef enum { SC_LOGIN, SC_MAIN, SC_SERIES, SC_SEARCH } Screen;
static Screen g_screen = SC_LOGIN;

#define TAB_HOME 0
#define TAB_DOWNLOADS 5
#define TAB_CONFIG 6
#define NTABS 7
static const char *TAB_NAME[] = { "Inicio", "Filmes", "Series", "Animes", "Doramas", "Baixados", "Config" };
static int g_tab = 0;

// --- landing (rails) das abas 0..4 ---
static cJSON *g_land = NULL;          // root JSON da aba atual (home / tab-home / anime-home)
static cJSON *g_heroesArr = NULL;     // array (dentro de g_land) usado no destaque
static int g_heroSeriesDefault = 1;   // hero abre como serie? (Filmes = 0)
typedef struct { char label[48]; cJSON *arr; int is_series; } Rail;
static Rail g_rails[48]; static int g_railsN = 0;
static int g_railSel = 0, g_railItem = 0, g_homeScroll = 0;
static int g_heroIdx = 0; static Uint32 g_hero_next = 0;

// --- busca ---
static cJSON *g_search = NULL;
static char g_srchQuery[128] = {0};
static int g_srchSel = 0, g_srchScroll = 0;

// --- downloads (acelerador) ---
static cJSON *g_dl = NULL;
static int g_dlSel = 0, g_dlScroll = 0; static Uint32 g_dl_next = 0;

// --- favoritos (set local p/ togglar rapido) ---
static int g_favItem[512]; static int g_favItemN = 0;
static int g_favSeries[512]; static int g_favSeriesN = 0;

// --- serie (detalhe) ---
static cJSON *g_ser = NULL;
static int g_seasonIdx = 0, g_epSel = 0, g_epScroll = 0;

// --- prototipos (funcoes que se chamam entre si) ---
static void enter_tab(int tab);
static void load_landing(int tab);
static void load_downloads(void);
static void accel_start(int itemId);
static void do_search(void);
static void open_series(int id);
static void resolve_and_play(int itemId, const char *title);
static void play_with_progress(int itemId, const char *title, const char *url);

// ------------------------------------------------------------- favoritos
static int idx_of(int *arr, int n, int v) { for (int i = 0; i < n; i++) if (arr[i] == v) return i; return -1; }
static int is_fav_item(int id) { return idx_of(g_favItem, g_favItemN, id) >= 0; }
static int is_fav_series(int id) { return idx_of(g_favSeries, g_favSeriesN, id) >= 0; }
static void load_favs(void) {
    g_favItemN = g_favSeriesN = 0;
    cJSON *j = api_get("/api/sync/favorites");
    if (!j) return;
    cJSON *items = cJSON_GetObjectItem(j, "items"), *e;
    cJSON_ArrayForEach(e, items) {
        cJSON *it = cJSON_GetObjectItem(e, "item_id");
        cJSON *se = cJSON_GetObjectItem(e, "series_id");
        if (it && cJSON_IsNumber(it) && g_favItemN < 512) g_favItem[g_favItemN++] = it->valueint;
        if (se && cJSON_IsNumber(se) && g_favSeriesN < 512) g_favSeries[g_favSeriesN++] = se->valueint;
    }
    cJSON_Delete(j);
}
static void toggle_fav_item(int id) {
    char body[48]; snprintf(body, sizeof(body), "{\"item_id\":%d}", id);
    int i = idx_of(g_favItem, g_favItemN, id);
    if (i >= 0) { api_send("/api/sync/favorites", "DELETE", body); g_favItem[i] = g_favItem[--g_favItemN]; toast("Removido da Minha lista"); }
    else { long c = api_send("/api/sync/favorites", "POST", body); if (c == 200) { if (g_favItemN < 512) g_favItem[g_favItemN++] = id; toast("Adicionado a Minha lista"); } else toast("Nao foi possivel favoritar"); }
}
static void toggle_fav_series(int id) {
    char body[48]; snprintf(body, sizeof(body), "{\"series_id\":%d}", id);
    int i = idx_of(g_favSeries, g_favSeriesN, id);
    if (i >= 0) { api_send("/api/sync/favorites", "DELETE", body); g_favSeries[i] = g_favSeries[--g_favSeriesN]; toast("Removido da Minha lista"); }
    else { long c = api_send("/api/sync/favorites", "POST", body); if (c == 200) { if (g_favSeriesN < 512) g_favSeries[g_favSeriesN++] = id; toast("Adicionado a Minha lista"); } else toast("Nao foi possivel favoritar"); }
}

// ------------------------------------------------------------- landing (rails)
static void add_rail(const char *label, cJSON *arr, int is_series) {
    if (arr_len(arr) == 0 || g_railsN >= 48) return;
    strncpy(g_rails[g_railsN].label, label ? label : "Categoria", 47); g_rails[g_railsN].label[47] = '\0';
    g_rails[g_railsN].arr = arr; g_rails[g_railsN].is_series = is_series;
    g_railsN++;
}
// Carrega a landing da aba (0..4). Cada aba vira hero + rails, como no app de PC.
static void load_landing(int tab) {
    if (g_land) { cJSON_Delete(g_land); g_land = NULL; }
    g_railsN = 0; g_railItem = 0; g_homeScroll = 0;
    g_heroIdx = 0; g_hero_next = SDL_GetTicks() + 6000; g_heroesArr = NULL;
    g_heroSeriesDefault = (tab == 1) ? 0 : 1;

    const char *path;
    switch (tab) {
        case 1: path = "/api/catalog/tab-home?tab=movie"; break;
        case 2: path = "/api/catalog/tab-home?tab=series"; break;
        case 3: path = "/api/catalog/anime-home"; break;
        case 4: path = "/api/catalog/tab-home?tab=dorama"; break;
        default: path = "/api/catalog/home"; break;
    }
    g_land = api_get(path);
    if (!g_land) { snprintf(g_status, sizeof(g_status), "Falha ao carregar %s", TAB_NAME[tab]); g_railSel = 0; return; }
    g_status[0] = '\0';

    if (tab == 0) {
        g_heroesArr = cJSON_GetObjectItem(g_land, "heroes");
        add_rail("Continuar assistindo", cJSON_GetObjectItem(g_land, "continue"), 1);
        add_rail("Filmes recentes",     cJSON_GetObjectItem(g_land, "recentMovies"), 0);
        add_rail("Series atualizadas",  cJSON_GetObjectItem(g_land, "recentSeries"), 1);
        add_rail("Animes recentes",     cJSON_GetObjectItem(g_land, "recentAnimes"), 1);
        cJSON *sh = cJSON_GetObjectItem(g_land, "movieShelves"), *e;
        cJSON_ArrayForEach(e, sh) add_rail(jstr(e, "title"), cJSON_GetObjectItem(e, "items"), 0);
    } else if (tab == 3) {   // anime-home
        g_heroesArr = cJSON_GetObjectItem(g_land, "updated");
        add_rail("Minha lista",       cJSON_GetObjectItem(g_land, "favoritos"), 1);
        add_rail("Recem-atualizados", cJSON_GetObjectItem(g_land, "updated"), 1);
        add_rail("Em alta",           cJSON_GetObjectItem(g_land, "popular"), 1);
        add_rail("Dublados",          cJSON_GetObjectItem(g_land, "dublados"), 1);
        add_rail("Filmes de anime",   cJSON_GetObjectItem(g_land, "filmes"), 1);
        cJSON *gs = cJSON_GetObjectItem(g_land, "genreShelves"), *e;
        cJSON_ArrayForEach(e, gs) add_rail(jstr(e, "genre"), cJSON_GetObjectItem(e, "items"), 1);
    } else {                 // tab-home (movie/series/dorama)
        int is_series = (tab != 1);
        g_heroesArr = cJSON_GetObjectItem(g_land, "recent");
        add_rail("Minha lista", cJSON_GetObjectItem(g_land, "favoritos"), is_series);
        add_rail("Lancamentos", cJSON_GetObjectItem(g_land, "recent"), is_series);
        cJSON *sh = cJSON_GetObjectItem(g_land, "shelves"), *e;
        cJSON_ArrayForEach(e, sh) add_rail(jstr(e, "title"), cJSON_GetObjectItem(e, "items"), is_series);
    }
    g_railSel = (arr_len(g_heroesArr) > 0) ? -1 : 0;
}

// Toca uma URL retomando de onde parou e salvando o progresso ("continuar
// assistindo"). Usado tanto no link direto quanto no arquivo do acelerador.
static void play_with_progress(int itemId, const char *title, const char *url) {
    double start = 0;
    char p[96]; snprintf(p, sizeof(p), "/api/sync/progress/%d", itemId);
    cJSON *pr = api_get(p);
    if (pr) {
        cJSON *prog = cJSON_GetObjectItem(pr, "progress");
        cJSON *ps = prog ? cJSON_GetObjectItem(prog, "position_seconds") : NULL;
        if (ps && cJSON_IsNumber(ps)) start = ps->valuedouble;
        cJSON_Delete(pr);
    }
    double pos = 0, dur = 0;
    int r = player_play(gRen, g_joy, url, title, start, &pos, &dur);
    if (r < 0) { char m[80]; snprintf(m, sizeof(m), "Nao consegui tocar (erro %d)", r); toast(m); return; }
    if (dur > 0 && pos > 5) {
        char body[160];
        snprintf(body, sizeof(body), "{\"item_id\":%d,\"position_seconds\":%d,\"duration_seconds\":%d}", itemId, (int)pos, (int)dur);
        api_send("/api/sync/progress", "POST", body);
    }
}

// Resolve a fonte e reproduz. Link direto (anime/dorama) toca na hora; torrent
// (filme/serie) manda pro acelerador do servidor e abre a aba Baixados.
static void resolve_and_play(int itemId, const char *title) {
    SDL_SetRenderDrawColor(gRen, C_BG.r, C_BG.g, C_BG.b, 255); SDL_RenderClear(gRen);
    text_draw(gRen, "Carregando video...", WIN_W / 2 - 120, WIN_H / 2 - 16, C_TEXT, 1);
    SDL_RenderPresent(gRen);
    char url[1024]; snprintf(url, sizeof(url), "%s/api/stream/%d", BASE, itemId);
    struct membuf out = { 0 }; const char *err = NULL;
    long code = net_request(url, "POST", "{}", g_token[0] ? g_token : NULL, &out, &err);
    if (code != 200 || !out.data) { membuf_free(&out); toast("Falha ao resolver o stream"); return; }
    cJSON *j = cJSON_Parse(out.data);
    const char *container = jstr(j, "container");
    const char *play = jstr(j, "play_url");
    char purl[1200] = { 0 };
    if (play) {
        if (strncmp(play, "http", 4) == 0) snprintf(purl, sizeof(purl), "%s", play);
        else snprintf(purl, sizeof(purl), "%s%s", BASE, play);
    }
    if (container && !strcmp(container, "torrent")) {
        accel_start(itemId);
        toast("Baixando no servidor - veja em Baixados");
        g_screen = SC_MAIN; enter_tab(TAB_DOWNLOADS);
    } else if (container && !strcmp(container, "embed")) {
        toast("Este conteudo (embed) ainda nao toca no Switch");
    } else if (purl[0]) {
        play_with_progress(itemId, title, purl);
    } else toast("Sem fonte para tocar");
    if (j) cJSON_Delete(j);
    membuf_free(&out);
}
static void open_series(int id) {
    if (g_ser) { cJSON_Delete(g_ser); g_ser = NULL; }
    char p[96]; snprintf(p, sizeof(p), "/api/catalog/series/%d", id);
    g_ser = api_get(p);
    g_seasonIdx = 0; g_epSel = 0; g_epScroll = 0;
    if (g_ser) g_screen = SC_SERIES;
    else toast("Nao consegui abrir a serie");
}
static void open_item(cJSON *item, int is_series) {
    if (!item) return;
    int id = jint(item, "id");
    cJSON *sid = cJSON_GetObjectItem(item, "series_id");
    if (sid && cJSON_IsNumber(sid)) { open_series(sid->valueint); return; }
    if (is_series) open_series(id);
    else resolve_and_play(id, jstr(item, "title"));
}

// ------------------------------------------------------------- downloads (acelerador)
static cJSON *dl_jobs(void) { return g_dl ? cJSON_GetObjectItem(g_dl, "jobs") : NULL; }
static void accel_start(int itemId) {
    char path[64]; snprintf(path, sizeof(path), "/api/accel/download/%d", itemId);
    api_send(path, "POST", "{}");
}
static void accel_remove(int itemId) {
    char path[64]; snprintf(path, sizeof(path), "/api/accel/jobs/%d", itemId);
    api_send(path, "DELETE", "{}");
}
static void load_downloads(void) {
    if (g_dl) { cJSON_Delete(g_dl); g_dl = NULL; }
    g_dl = api_get("/api/accel/jobs");
    int n = arr_len(dl_jobs());
    if (g_dlSel >= n) g_dlSel = n > 0 ? n - 1 : 0;
}
static void dl_play(cJSON *job) {
    const char *fu = jstr(job, "file_url");
    if (!fu) { toast("Sem arquivo"); return; }
    char url[1400];
    if (strncmp(fu, "http", 4) == 0) snprintf(url, sizeof(url), "%s", fu);
    else snprintf(url, sizeof(url), "%s%s", BASE, fu);
    play_with_progress(jint(job, "item_id"), jstr(job, "title"), url);
}

// ------------------------------------------------------------- busca
static int srch_movies(void) { return g_search ? arr_len(cJSON_GetObjectItem(g_search, "items")) : 0; }
static int srch_series(void) { return g_search ? arr_len(cJSON_GetObjectItem(g_search, "series")) : 0; }
static cJSON *srch_at(int i, int *is_series) {
    int nm = srch_movies();
    if (i < nm) { *is_series = 0; return cJSON_GetArrayItem(cJSON_GetObjectItem(g_search, "items"), i); }
    *is_series = 1; return cJSON_GetArrayItem(cJSON_GetObjectItem(g_search, "series"), i - nm);
}
static void do_search(void) {
    char q[128];
    if (prompt_text("Buscar filme, serie, anime, dorama...", q, sizeof(q), 0) != 0) return;
    if (g_search) { cJSON_Delete(g_search); g_search = NULL; }
    char enc[300]; int k = 0;
    for (int i = 0; q[i] && k < 294; i++) { if (q[i] == ' ') { enc[k++] = '%'; enc[k++] = '2'; enc[k++] = '0'; } else enc[k++] = q[i]; }
    enc[k] = 0;
    char path[360]; snprintf(path, sizeof(path), "/api/catalog/search?q=%s", enc);
    g_search = api_get(path);
    g_srchSel = 0; g_srchScroll = 0;
    snprintf(g_srchQuery, sizeof(g_srchQuery), "%s", q);
    g_screen = SC_SEARCH;
}

// ------------------------------------------------------------- render: barra
static void draw_topbar(void) {
    fill_rect(0, 0, WIN_W, 66, C_BAR);
    text_draw(gRen, "Nplay", 40, 18, C_ACC, 1);
    int tx = 172;
    for (int t = 0; t < NTABS; t++) {
        int w = text_draw(gRen, TAB_NAME[t], tx, 20, (t == g_tab) ? C_TEXT : C_MUT, 0);
        if (t == g_tab) fill_rect(tx, 46, w, 3, C_ACC);
        tx += w + 24;
    }
    text_draw(gRen, "Y busca  (+) sair", WIN_W - 220, 24, C_MUT, 0);
}

// ------------------------------------------------------------- render: landing
#define RCW 150
#define RCH 214
#define RGAP 16
#define HERO_H 224
#define RAILS_TOP (108 + HERO_H + 24)
static void draw_landing(void) {
    draw_topbar();
    if (!g_land) { text_draw(gRen, g_status[0] ? g_status : "Carregando...", 40, 110, C_MUT, 0); return; }
    char hi[180]; snprintf(hi, sizeof(hi), "Bem-vindo de volta%s%s", g_user[0] ? ", " : "", g_user[0] ? g_user : "");
    if (g_tab == 0) text_draw(gRen, hi, 40, 76 - g_homeScroll, C_MUT, 0);
    else text_draw(gRen, TAB_NAME[g_tab], 40, 76 - g_homeScroll, C_MUT, 0);

    int nh = arr_len(g_heroesArr);
    int hy = 108 - g_homeScroll;
    if (nh > 0) {
        cJSON *h = cJSON_GetArrayItem(g_heroesArr, g_heroIdx % nh);
        fill_rect(40, hy, WIN_W - 80, HERO_H, C_BAR);
        if (g_railSel == -1) border_rect(37, hy - 3, WIN_W - 74, HERO_H + 6, 3, C_ACC2);
        SDL_Texture *tex = cover_get(jstr(h, "logo"));
        SDL_Rect pr = { 60, hy + 16, 138, HERO_H - 32 };
        if (tex) SDL_RenderCopy(gRen, tex, NULL, &pr); else fill_rect(60, hy + 16, 138, HERO_H - 32, C_CARD);
        text_draw(gRen, "DESTAQUE", 228, hy + 28, C_ACC, 0);
        const char *ht = jstr(h, "title"); if (!ht) ht = "";
        char ht2[42]; short_title(ht, ht2, (int)sizeof(ht2));
        text_draw(gRen, ht2, 228, hy + 54, C_TEXT, 1);
        const char *hk = jstr(h, "kind");
        const char *kl = hk ? (!strcmp(hk, "movie") ? "Filme" : !strcmp(hk, "live") ? "Ao vivo" : "Serie")
                            : (g_heroSeriesDefault ? "Serie" : "Filme");
        text_draw(gRen, kl, 228, hy + 100, C_MUT, 0);
        text_draw(gRen, "A abre   -   D-pad esq/dir troca o destaque", 228, hy + HERO_H - 40, C_MUT, 0);
        char cnt[24]; snprintf(cnt, sizeof(cnt), "%d / %d", (g_heroIdx % nh) + 1, nh);
        text_draw(gRen, cnt, WIN_W - 128, hy + HERO_H - 40, C_MUT, 0);
    }

    int y = (nh > 0 ? RAILS_TOP : 120) - g_homeScroll;
    for (int r = 0; r < g_railsN; r++) {
        int items = arr_len(g_rails[r].arr);
        text_draw(gRen, g_rails[r].label, 40, y, (r == g_railSel) ? C_TEXT : C_MUT, 0);
        int ry = y + 30, rowScroll = 0;
        if (r == g_railSel) {
            int selX = 40 + g_railItem * (RCW + RGAP);
            if (selX + RCW - rowScroll > WIN_W - 40) rowScroll = selX + RCW - (WIN_W - 40);
            if (selX - rowScroll < 40) rowScroll = selX - 40;
            if (rowScroll < 0) rowScroll = 0;
        }
        for (int i = 0; i < items; i++) {
            int x = 40 + i * (RCW + RGAP) - rowScroll;
            if (x + RCW < 0 || x > WIN_W) continue;
            cJSON *it = cJSON_GetArrayItem(g_rails[r].arr, i);
            int fav = g_rails[r].is_series ? is_fav_series(jint(it, "id")) : is_fav_item(jint(it, "id"));
            draw_card(x, ry, RCW, RCH, it, (r == g_railSel && i == g_railItem), fav);
        }
        y += 30 + RCH + 40;
        if (y > WIN_H + 240) break;
    }
    if (g_railsN == 0 && nh == 0) text_draw(gRen, "Nada por aqui ainda.", 40, 140, C_MUT, 0);
}

// ------------------------------------------------------------- render: busca (grade)
#define GCOLS 6
#define GMX 40
#define GGAP 16
#define GCW 180
#define GCOVERW 158
#define GCOVERH 226
#define GCH 268
static void draw_search(void) {
    draw_topbar();
    int nm = srch_movies(), ns = srch_series(), n = nm + ns;
    char hd[200]; snprintf(hd, sizeof(hd), "Busca: \"%s\"  -  %d resultado%s   (B volta, Y nova busca)", g_srchQuery, n, n == 1 ? "" : "s");
    text_draw(gRen, hd, 40, 78, C_TEXT, 0);
    if (n == 0) { text_draw(gRen, "Nada encontrado. Aperte Y pra tentar outro termo.", 40, 130, C_MUT, 0); return; }
    int top = 108;
    for (int i = 0; i < n; i++) {
        int col = i % GCOLS, row = i / GCOLS;
        int x = GMX + col * (GCW + GGAP) + (GCW - GCOVERW) / 2;
        int yy = top + row * (GCH + GGAP) - g_srchScroll;
        if (yy + GCH < 66 || yy > WIN_H) continue;
        int is; cJSON *it = srch_at(i, &is);
        int fav = is ? is_fav_series(jint(it, "id")) : is_fav_item(jint(it, "id"));
        draw_card(x, yy, GCOVERW, GCOVERH, it, i == g_srchSel, fav);
    }
}

// ------------------------------------------------------------- render: serie
static cJSON *season_arr(void) {
    cJSON *seasons = g_ser ? cJSON_GetObjectItem(g_ser, "seasons") : NULL;
    if (!seasons) return NULL;
    return cJSON_GetArrayItem(seasons, g_seasonIdx);
}
static int season_count(void) {
    cJSON *seasons = g_ser ? cJSON_GetObjectItem(g_ser, "seasons") : NULL;
    return seasons ? cJSON_GetArraySize(seasons) : 0;
}
static const char *ep_clean(const char *t) {
    if (t && t[0] == 'T') {
        const char *colon = strchr(t, ':');
        const char *space = strchr(t, ' ');
        if (colon && space && colon < space) return space + 1;
    }
    return t ? t : "Episodio";
}
static void draw_series(void) {
    cJSON *s = g_ser ? cJSON_GetObjectItem(g_ser, "series") : NULL;
    int sid = jint(s, "id");
    int fav = is_fav_series(sid);
    fill_rect(0, 0, WIN_W, 66, C_BAR);
    text_draw(gRen, "< (B) voltar", 40, 22, C_MUT, 0);
    const char *title = jstr(s, "title"); if (!title) title = "Serie";
    text_clip(title, 200, 16, C_TEXT, 1, WIN_W - 420);
    // botao favoritar (X)
    fill_rect(WIN_W - 300, 14, 260, 40, fav ? C_ROSE : C_CARD);
    text_draw(gRen, fav ? "* Na Minha lista (X)" : "+ Minha lista (X)", WIN_W - 288, 22, C_TEXT, 0);

    int LX = 40, LW = 220;
    SDL_Texture *tex = cover_get(jstr(s, "logo"));
    SDL_Rect cr = { LX, 100, LW, 314 };
    if (tex) SDL_RenderCopy(gRen, tex, NULL, &cr); else fill_rect(LX, 100, LW, 314, C_CARD);
    const char *year = jstr(s, "year");
    double rating = 0; cJSON *jr = cJSON_GetObjectItem(s, "rating"); if (jr && cJSON_IsNumber(jr)) rating = jr->valuedouble;
    char l1[80];
    if (rating > 0) snprintf(l1, sizeof(l1), "%s%sNota %.1f", year ? year : "", year ? "   " : "", rating);
    else snprintf(l1, sizeof(l1), "%s", year ? year : "");
    if (l1[0]) text_clip(l1, LX, 426, C_TEXT, 0, LW);
    const char *genre = jstr(s, "genre");
    if (genre) text_clip(genre, LX, 456, C_MUT, 0, LW);
    char epc[48]; snprintf(epc, sizeof(epc), "%d episodios", jint(s, "episode_count"));
    text_clip(epc, LX, 486, C_MUT, 0, LW);

    int RX = 300, REND = WIN_W - 40;
    cJSON *sa = season_arr();
    int nsea = season_count(), nep = arr_len(sa);
    char sh[48]; snprintf(sh, sizeof(sh), "Temporada %s", (sa && sa->string) ? sa->string : "1");
    text_draw(gRen, sh, RX, 100, C_ACC, 1);
    if (nsea > 1) { char hint[64]; snprintf(hint, sizeof(hint), "L/R troca temporada  (%d)", nsea); text_draw(gRen, hint, RX + 260, 108, C_MUT, 0); }
    fill_rect(RX, 148, REND - RX, 2, C_CARD);

    int listTop = 168, rowH = 40, visible = (WIN_H - listTop - 44) / rowH;
    if (g_epSel < g_epScroll) g_epScroll = g_epSel;
    if (g_epSel >= g_epScroll + visible) g_epScroll = g_epSel - visible + 1;
    for (int i = g_epScroll; i < nep && i < g_epScroll + visible; i++) {
        cJSON *ep = cJSON_GetArrayItem(sa, i);
        int yy = listTop + (i - g_epScroll) * rowH;
        if (i == g_epSel) fill_rect(RX - 8, yy - 5, REND - RX + 16, rowH - 2, C_CARD);
        int en = jint(ep, "episode"); char nb[10]; snprintf(nb, sizeof(nb), "%d", en > 0 ? en : i + 1);
        text_draw(gRen, nb, RX, yy, (i == g_epSel) ? C_ACC : C_MUT, 0);
        text_clip(ep_clean(jstr(ep, "title")), RX + 52, yy, (i == g_epSel) ? C_TEXT : C_MUT, 0, REND - RX - 60);
    }
    if (nep == 0) text_draw(gRen, "Sem episodios nesta temporada", RX, listTop, C_MUT, 0);
    text_draw(gRen, "cima/baixo navega   -   A assistir   -   X favoritar", RX, WIN_H - 36, C_MUT, 0);
}

// ------------------------------------------------------------- render: downloads
static void draw_downloads(void) {
    draw_topbar();
    text_draw(gRen, "Baixados", 40, 88, C_TEXT, 1);
    cJSON *jobs = dl_jobs();
    int n = arr_len(jobs);
    if (n == 0) {
        text_draw(gRen, "Nenhum download ainda.", 40, 150, C_MUT, 0);
        text_draw(gRen, "Abra um filme ou serie e aperte A: ele baixa aqui no servidor.", 40, 182, C_MUT, 0);
        text_draw(gRen, "Quando ficar pronto, aperte A pra assistir.", 40, 214, C_MUT, 0);
        return;
    }
    int top = 132, rowH = 88, visible = (WIN_H - top - 46) / rowH;
    if (g_dlSel < g_dlScroll) g_dlScroll = g_dlSel;
    if (g_dlSel >= g_dlScroll + visible) g_dlScroll = g_dlSel - visible + 1;
    for (int i = g_dlScroll; i < n && i < g_dlScroll + visible; i++) {
        cJSON *j = cJSON_GetArrayItem(jobs, i);
        int yy = top + (i - g_dlScroll) * rowH, sel = (i == g_dlSel);
        if (sel) fill_rect(34, yy - 8, WIN_W - 68, rowH - 8, C_CARD);
        const char *title = jstr(j, "title"); if (!title) title = "";
        char st[64]; short_title(title, st, (int)sizeof(st));
        text_clip(st, 48, yy, sel ? C_TEXT : C_MUT, 0, WIN_W - 340);
        const char *state = jstr(j, "state");
        int pct = jint(j, "percent");
        int ready = cJSON_IsTrue(cJSON_GetObjectItem(j, "ready"));
        int erro = state && !strcmp(state, "erro");
        int bx = 48, by = yy + 36, bw = WIN_W - 360, bh = 12;
        fill_rect(bx, by, bw, bh, C_BAR);
        int fw = bw * pct / 100; if (fw < 0) fw = 0; if (fw > bw) fw = bw;
        fill_rect(bx, by, fw, bh, ready ? C_GREEN : (erro ? C_ROSE : C_ACC));
        char info[96];
        if (ready) snprintf(info, sizeof(info), "Pronto  -  aperte A pra assistir");
        else if (erro) snprintf(info, sizeof(info), "Erro: %s", jstr(j, "error") ? jstr(j, "error") : "falhou");
        else {
            int eta = jint(j, "eta_seconds");
            char et[24] = "";
            if (eta > 0) { if (eta >= 3600) snprintf(et, sizeof(et), "  ~%dh%02dm", eta / 3600, (eta % 3600) / 60); else snprintf(et, sizeof(et), "  ~%dmin", (eta + 59) / 60); }
            snprintf(info, sizeof(info), "Baixando  %d%%   %.1f MB/s   %d peers%s", pct, jint(j, "speed") / 1048576.0, jint(j, "peers"), et);
        }
        text_clip(info, 48, yy + 56, ready ? C_GREEN : (erro ? C_ROSE : C_MUT), 0, WIN_W - 360);
        char pc[8]; snprintf(pc, sizeof(pc), "%d%%", pct);
        text_draw(gRen, pc, WIN_W - 150, yy + 6, ready ? C_GREEN : C_TEXT, 1);
    }
    text_draw(gRen, "A assistir (quando pronto)   -   X remover   -   atualiza sozinho", 40, WIN_H - 34, C_MUT, 0);
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
    char url[512]; snprintf(url, sizeof(url), "%s/api/auth/login", BASE);
    struct membuf out = { 0 }; const char *err = NULL;
    long code = net_request(url, "POST", body, NULL, &out, &err);
    int ok = -1;
    if (out.data) {
        cJSON *j = cJSON_Parse(out.data);
        if (j) {
            const char *tk = jstr(j, "token");
            if (code == 200 && tk) {
                strncpy(g_token, tk, sizeof(g_token) - 1);
                store_save_token(g_token); store_save_user(user); ok = 0;
            } else {
                const char *e = jstr(j, "error");
                snprintf(g_status, sizeof(g_status), "%s", e ? e : "Falha no login");
            }
            cJSON_Delete(j);
        }
    } else snprintf(g_status, sizeof(g_status), "Sem conexao (%s)", err ? err : "rede");
    membuf_free(&out);
    return ok;
}
static void draw_login(void) {
    text_draw(gRen, "Nplay", WIN_W / 2 - 70, 220, C_ACC, 1);
    text_draw(gRen, "Aperte  A  para entrar com sua conta", WIN_W / 2 - 220, 320, C_TEXT, 0);
    text_draw(gRen, "(+) para sair do app", WIN_W / 2 - 110, 360, C_MUT, 0);
    if (g_status[0]) text_draw(gRen, g_status, WIN_W / 2 - 220, 420, C_ROSE, 0);
}

// ------------------------------------------------------------- input
static void enter_tab(int tab) {
    g_tab = tab;
    g_status[0] = '\0';
    if (tab == TAB_CONFIG) return;
    if (tab == TAB_DOWNLOADS) { g_dlSel = 0; g_dlScroll = 0; load_downloads(); g_dl_next = SDL_GetTicks() + 2000; return; }
    load_landing(tab);
}
static void input_landing(int b) {
    int nh = arr_len(g_heroesArr);
    if (g_railSel < 0) {   // destaque focado
        if (b == JOY_DOWN) { if (g_railsN > 0) g_railSel = 0; }
        else if (b == JOY_DLEFT) { if (nh) g_heroIdx = (g_heroIdx - 1 + nh) % nh; g_hero_next = SDL_GetTicks() + 6000; }
        else if (b == JOY_DRIGHT) { if (nh) g_heroIdx = (g_heroIdx + 1) % nh; g_hero_next = SDL_GetTicks() + 6000; }
        else if (b == JOY_A) { if (nh) { cJSON *h = cJSON_GetArrayItem(g_heroesArr, g_heroIdx % nh); const char *k = jstr(h, "kind"); open_item(h, k ? strcmp(k, "movie") != 0 : g_heroSeriesDefault); } }
        else if (b == JOY_X) { if (nh) { cJSON *h = cJSON_GetArrayItem(g_heroesArr, g_heroIdx % nh); const char *k = jstr(h, "kind"); int is = k ? strcmp(k, "movie") != 0 : g_heroSeriesDefault; if (is) toggle_fav_series(jint(h, "id")); else toggle_fav_item(jint(h, "id")); } }
        g_homeScroll = 0;
        return;
    }
    int items = arr_len(g_rails[g_railSel].arr);
    if (b == JOY_UP) { if (g_railSel == 0) { g_railSel = (nh > 0) ? -1 : 0; g_homeScroll = 0; if (nh > 0) return; } else { g_railSel--; int n = arr_len(g_rails[g_railSel].arr); if (g_railItem >= n) g_railItem = n ? n - 1 : 0; } }
    else if (b == JOY_DOWN) { if (g_railSel < g_railsN - 1) { g_railSel++; int n = arr_len(g_rails[g_railSel].arr); if (g_railItem >= n) g_railItem = n ? n - 1 : 0; } }
    else if (b == JOY_DLEFT) { if (g_railItem > 0) g_railItem--; }
    else if (b == JOY_DRIGHT) { if (g_railItem < items - 1) g_railItem++; }
    else if (b == JOY_A) { open_item(cJSON_GetArrayItem(g_rails[g_railSel].arr, g_railItem), g_rails[g_railSel].is_series); }
    else if (b == JOY_X) { cJSON *it = cJSON_GetArrayItem(g_rails[g_railSel].arr, g_railItem); if (it) { if (g_rails[g_railSel].is_series) toggle_fav_series(jint(it, "id")); else toggle_fav_item(jint(it, "id")); } }
    int ry = (nh > 0 ? RAILS_TOP : 120) + g_railSel * (30 + RCH + 40);
    if (ry + RCH + 60 - g_homeScroll > WIN_H) g_homeScroll = ry + RCH + 60 - WIN_H + 20;
    if (ry - g_homeScroll < 76) g_homeScroll = ry - 76;
    if (g_homeScroll < 0) g_homeScroll = 0;
}
static void input_search(int b) {
    int n = srch_movies() + srch_series();
    if (b == JOY_B || b == JOY_MINUS) { g_screen = SC_MAIN; return; }
    if (b == JOY_UP) { if (g_srchSel - GCOLS >= 0) g_srchSel -= GCOLS; }
    else if (b == JOY_DOWN) { if (g_srchSel + GCOLS < n) g_srchSel += GCOLS; }
    else if (b == JOY_DLEFT) { if (g_srchSel > 0) g_srchSel--; }
    else if (b == JOY_DRIGHT) { if (g_srchSel + 1 < n) g_srchSel++; }
    else if (b == JOY_A) { int is; cJSON *it = srch_at(g_srchSel, &is); if (it) open_item(it, is); }
    else if (b == JOY_X) { int is; cJSON *it = srch_at(g_srchSel, &is); if (it) { if (is) toggle_fav_series(jint(it, "id")); else toggle_fav_item(jint(it, "id")); } }
    int row = g_srchSel / GCOLS, rowTop = 108 + row * (GCH + GGAP), rowBot = rowTop + GCH;
    if (rowBot - g_srchScroll > WIN_H) g_srchScroll = rowBot - WIN_H + 16;
    if (rowTop - g_srchScroll < 108) g_srchScroll = rowTop - 108;
    if (g_srchScroll < 0) g_srchScroll = 0;
}
static void input_series(int b) {
    cJSON *sa = season_arr();
    int nep = arr_len(sa);
    if (b == JOY_B || b == JOY_MINUS) { g_screen = SC_MAIN; }
    else if (b == JOY_Y) { do_search(); }
    else if (b == JOY_X) { cJSON *s = g_ser ? cJSON_GetObjectItem(g_ser, "series") : NULL; if (s) toggle_fav_series(jint(s, "id")); }
    else if (b == JOY_UP) { if (g_epSel > 0) g_epSel--; }
    else if (b == JOY_DOWN) { if (g_epSel < nep - 1) g_epSel++; }
    else if (b == JOY_L || b == JOY_ZL) { if (g_seasonIdx > 0) { g_seasonIdx--; g_epSel = 0; g_epScroll = 0; } }
    else if (b == JOY_R || b == JOY_ZR) { if (g_seasonIdx < season_count() - 1) { g_seasonIdx++; g_epSel = 0; g_epScroll = 0; } }
    else if (b == JOY_A) { cJSON *ep = cJSON_GetArrayItem(sa, g_epSel); if (ep) resolve_and_play(jint(ep, "id"), ep_clean(jstr(ep, "title"))); }
}
static void input_downloads(int b) {
    cJSON *jobs = dl_jobs();
    int n = arr_len(jobs);
    if (b == JOY_UP) { if (g_dlSel > 0) g_dlSel--; }
    else if (b == JOY_DOWN) { if (g_dlSel < n - 1) g_dlSel++; }
    else if (b == JOY_A) { cJSON *j = cJSON_GetArrayItem(jobs, g_dlSel); if (j) { if (cJSON_IsTrue(cJSON_GetObjectItem(j, "ready"))) dl_play(j); else toast("Ainda baixando..."); } }
    else if (b == JOY_X) { cJSON *j = cJSON_GetArrayItem(jobs, g_dlSel); if (j) { accel_remove(jint(j, "item_id")); load_downloads(); toast("Download removido"); } }
}

// ------------------------------------------------------------- config
static const char *SET_ITEMS[] = { "Buscar atualizacao", "Sair da conta" };
#define NSET 2
static int g_setSel = 0;
static void draw_settings(void) {
    draw_topbar();
    text_draw(gRen, "Configuracoes", 40, 100, C_TEXT, 1);
    char v[96]; snprintf(v, sizeof(v), "Versao do app: %s", APP_VERSION_STR);
    text_draw(gRen, v, 40, 152, C_MUT, 0);
    char u[180]; snprintf(u, sizeof(u), "Conta: %s", g_user[0] ? g_user : "-");
    text_draw(gRen, u, 40, 182, C_MUT, 0);
    text_draw(gRen, BASE, 40, 212, C_MUT, 0);
    for (int i = 0; i < NSET; i++) {
        int y = 270 + i * 56;
        if (i == g_setSel) fill_rect(36, y - 6, 470, 48, C_CARD);
        text_draw(gRen, SET_ITEMS[i], 48, y, (i == g_setSel) ? C_TEXT : C_MUT, 0);
    }
    if (g_status[0]) text_draw(gRen, g_status, 40, 270 + NSET * 56 + 22, C_ACC, 0);
    text_draw(gRen, "A confirma  -  D-pad move", 40, WIN_H - 50, C_MUT, 0);
}
static void input_settings(int b) {
    if (b == JOY_UP) { if (g_setSel > 0) g_setSel--; }
    else if (b == JOY_DOWN) { if (g_setSel < NSET - 1) g_setSel++; }
    else if (b == JOY_A) {
        if (g_setSel == 0) { snprintf(g_status, sizeof(g_status), "Verificando atualizacao..."); g_do_update = 1; }
        else { store_clear_token(); g_token[0] = '\0'; g_screen = SC_LOGIN; g_status[0] = '\0'; }
    }
}
static void run_update(void) {
    struct update_info info;
    int r = update_check(&info);
    if (r == UPDATE_CHECK_AVAILABLE) {
        char target[600]; update_resolve_target_path(g_self_path, target, sizeof(target));
        char err[256] = "";
        if (update_apply(&info, target, err, sizeof(err)) == 0)
            snprintf(g_status, sizeof(g_status), "Atualizado p/ %s! Feche e reabra o Nplay.", info.latest_version);
        else
            snprintf(g_status, sizeof(g_status), "Falha na atualizacao: %s", err[0] ? err : "download");
    } else if (r == UPDATE_CHECK_UP_TO_DATE) {
        snprintf(g_status, sizeof(g_status), "Voce ja esta na versao mais recente (%s).", APP_VERSION_STR);
    } else {
        snprintf(g_status, sizeof(g_status), "%s", info.message[0] ? info.message : "Erro ao verificar atualizacao");
    }
}

// ------------------------------------------------------------- main
int main(int argc, char **argv) {
    update_resolve_target_path((argc > 0 && argv) ? argv[0] : NULL, g_self_path, sizeof(g_self_path));
    socketInitializeDefault();
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_WEBP);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_Window *win = SDL_CreateWindow("Nplay", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    gRen = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawBlendMode(gRen, SDL_BLENDMODE_BLEND);
    SDL_InitSubSystem(SDL_INIT_JOYSTICK);
    g_joy = SDL_JoystickOpen(0);

    text_init(); net_init(); store_init();

    g_cov_mtx = SDL_CreateMutex(); g_q_mtx = SDL_CreateMutex(); g_q_sem = SDL_CreateSemaphore(0);
    SDL_Thread *wk[3];
    for (int i = 0; i < 3; i++) wk[i] = SDL_CreateThread(cover_worker, "cov", NULL);

    store_load_token(g_token, sizeof(g_token));
    store_load_user(g_user, sizeof(g_user));
    if (g_token[0]) { load_favs(); g_screen = SC_MAIN; enter_tab(0); }

    int running = 1;
    while (appletMainLoop() && running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = 0; break; }
            if (e.type != SDL_JOYBUTTONDOWN) continue;
            int b = e.jbutton.button;
            if (g_screen == SC_LOGIN) {
                if (b == JOY_A) { if (do_login() == 0) { load_favs(); g_screen = SC_MAIN; enter_tab(0); } }
                else if (b == JOY_PLUS) running = 0;
            } else if (g_screen == SC_MAIN) {
                if (b == JOY_L || b == JOY_ZL) enter_tab((g_tab - 1 + NTABS) % NTABS);
                else if (b == JOY_R || b == JOY_ZR) enter_tab((g_tab + 1) % NTABS);
                else if (b == JOY_PLUS) running = 0;
                else if (b == JOY_Y) do_search();
                else if (g_tab == TAB_CONFIG) input_settings(b);
                else if (g_tab == TAB_DOWNLOADS) input_downloads(b);
                else input_landing(b);
            } else if (g_screen == SC_SERIES) {
                input_series(b);
            } else if (g_screen == SC_SEARCH) {
                input_search(b);
            }
        }

        // destaque rotativo nas abas 0..4 (a cada ~6s)
        if (g_screen == SC_MAIN && g_tab <= 4 && g_land && SDL_GetTicks() > g_hero_next) {
            int nh = arr_len(g_heroesArr);
            if (nh > 0) g_heroIdx = (g_heroIdx + 1) % nh;
            g_hero_next = SDL_GetTicks() + 6000;
        }
        // atualiza a lista de downloads sozinho
        if (g_screen == SC_MAIN && g_tab == TAB_DOWNLOADS && SDL_GetTicks() > g_dl_next) {
            load_downloads(); g_dl_next = SDL_GetTicks() + 2000;
        }

        SDL_SetRenderDrawColor(gRen, C_BG.r, C_BG.g, C_BG.b, 255);
        SDL_RenderClear(gRen);
        if (g_screen == SC_LOGIN) draw_login();
        else if (g_screen == SC_SERIES) draw_series();
        else if (g_screen == SC_SEARCH) draw_search();
        else { if (g_tab == TAB_CONFIG) draw_settings(); else if (g_tab == TAB_DOWNLOADS) draw_downloads(); else draw_landing(); }

        cover_pump();

        if (g_toast[0] && SDL_GetTicks() < g_toast_until) {
            int w = 0, h = 0;
            SDL_Texture *tx = text_cached(gRen, g_toast, C_TEXT, 0, &w, &h);
            fill_rect(WIN_W / 2 - w / 2 - 18, WIN_H - 90, w + 36, h + 20, C_BAR);
            if (tx) { SDL_Rect d = { WIN_W / 2 - w / 2, WIN_H - 80, w, h }; SDL_RenderCopy(gRen, tx, NULL, &d); }
        }
        SDL_RenderPresent(gRen);
        if (g_do_update) { g_do_update = 0; run_update(); }
    }

    g_run = 0;
    for (int i = 0; i < 3; i++) SDL_SemPost(g_q_sem);
    for (int i = 0; i < 3; i++) SDL_WaitThread(wk[i], NULL);

    if (g_land) cJSON_Delete(g_land);
    if (g_search) cJSON_Delete(g_search);
    if (g_dl) cJSON_Delete(g_dl);
    if (g_ser) cJSON_Delete(g_ser);
    for (int i = 0; i < g_covN; i++) if (g_cov[i].tex) SDL_DestroyTexture(g_cov[i].tex);
    text_exit(); net_exit();
    if (g_joy) SDL_JoystickClose(g_joy);
    SDL_DestroyRenderer(gRen); SDL_DestroyWindow(win);
    IMG_Quit(); SDL_Quit(); socketExit();
    return 0;
}
