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
#include <sys/stat.h>
#include <dirent.h>
#include "net.h"
#include "store.h"
#include "text.h"
#include "cJSON.h"
#include "update.h"
#include "player.h"

#define WIN_W 1280
#define WIN_H 720



static const char *BASE = "https://nplay.tonserverlocal.uk";

SDL_Renderer *gRen = NULL;
static SDL_Joystick *g_joy = NULL;
static char g_token[640] = {0};
static char g_status[160] = {0};
Uint32 g_toast_until = 0;
char g_toast[160] = {0};
static char g_self_path[600] = {0};
static char g_user[128] = {0};
static int g_do_update = 0;
static int g_running; // definido/inicializado na secao de roteamento de input

#include "ui.h"
#include "screen_movie.h"

cJSON *api_get(const char *path) {
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
long api_send(const char *path, const char *method, const char *body) {
    char url[1024];
    snprintf(url, sizeof(url), "%s%s", BASE, path);
    struct membuf out = { 0 };
    const char *err = NULL;
    long code = net_request(url, method, body ? body : "{}", g_token[0] ? g_token : NULL, &out, &err);
    membuf_free(&out);
    return code;
}
const char *jstr(cJSON *o, const char *k) {
    cJSON *v = o ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL;
    return (v && v->valuestring) ? v->valuestring : NULL;
}
int jint(cJSON *o, const char *k) {
    cJSON *v = o ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL;
    return v ? v->valueint : 0;
}
int arr_len(cJSON *a) { return cJSON_IsArray(a) ? cJSON_GetArraySize(a) : 0; }

// ============================================================= capas (threads)
// state: 0 novo, 1 na fila/baixando, 2 surface pronta (main cria textura), 3 feito
#define MAX_COV 3000
#define COVER_HASH_SIZE 8192             // potencia de 2; carga < 37% com MAX_COV
#define MAX_COVER_TEXTURES 160           // limita memoria de GPU/heap usada pelas capas
typedef struct {
    char url[720];
    SDL_Texture *tex;
    SDL_Surface *surf;
    int state;
    Uint32 last_used;
} Cover;
static Cover g_cov[MAX_COV];
static int g_covN = 0;
static int g_cov_hash[COVER_HASH_SIZE];   // indice + 1; zero = slot vazio
static int g_cov_texN = 0;
static SDL_mutex *g_cov_mtx;
static int g_q[MAX_COV]; static int g_qh = 0, g_qt = 0;
static SDL_mutex *g_q_mtx; static SDL_sem *g_q_sem;
static int g_ready[MAX_COV]; static int g_rh = 0, g_rt = 0;
static SDL_mutex *g_ready_mtx;
static int g_run = 1;

static unsigned cover_hash(const char *s) {
    unsigned h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

// Chamado com g_cov_mtx travado. O hash evita comparar ate 3000 URLs por card/frame.
static int cover_find_locked(const char *url, int create) {
    unsigned slot = cover_hash(url) & (COVER_HASH_SIZE - 1);
    for (int probe = 0; probe < COVER_HASH_SIZE; probe++) {
        int entry = g_cov_hash[slot];
        if (!entry) {
            if (!create || g_covN >= MAX_COV) return -1;
            int idx = g_covN++;
            snprintf(g_cov[idx].url, sizeof(g_cov[idx].url), "%s", url);
            g_cov_hash[slot] = idx + 1;
            return idx;
        }
        int idx = entry - 1;
        if (!strcmp(g_cov[idx].url, url)) return idx;
        slot = (slot + 1) & (COVER_HASH_SIZE - 1);
    }
    return -1;
}

SDL_Texture *cover_get(const char *url) {   // chamado no main (render)
    if (!url || !url[0]) return NULL;
    SDL_LockMutex(g_cov_mtx);
    int f = cover_find_locked(url, 1);
    SDL_Texture *tex = (f >= 0) ? g_cov[f].tex : NULL;
    if (tex) g_cov[f].last_used = SDL_GetTicks();
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
        if (s) {
            SDL_LockMutex(g_ready_mtx);
            g_ready[g_rt] = idx; g_rt = (g_rt + 1) % MAX_COV;
            SDL_UnlockMutex(g_ready_mtx);
        }
    }
    return 0;
}
static void cover_pump(void) {   // main: converte surfaces prontas em texturas
    for (int done = 0; done < 6; done++) {
        int idx = -1; SDL_Surface *s = NULL;
        SDL_LockMutex(g_ready_mtx);
        if (g_rh != g_rt) { idx = g_ready[g_rh]; g_rh = (g_rh + 1) % MAX_COV; }
        SDL_UnlockMutex(g_ready_mtx);
        if (idx < 0) break;
        SDL_LockMutex(g_cov_mtx);
        if (g_cov[idx].state == 2) { s = g_cov[idx].surf; g_cov[idx].surf = NULL; g_cov[idx].state = 3; }
        SDL_UnlockMutex(g_cov_mtx);
        if (!s) continue;
        SDL_Texture *tex = SDL_CreateTextureFromSurface(gRen, s);
        SDL_FreeSurface(s);
        SDL_LockMutex(g_cov_mtx);
        if (tex && g_cov_texN >= MAX_COVER_TEXTURES) {
            int victim = -1;
            for (int i = 0; i < g_covN; i++) {
                if (i != idx && g_cov[i].tex && (victim < 0 || g_cov[i].last_used < g_cov[victim].last_used)) victim = i;
            }
            if (victim >= 0) {
                SDL_DestroyTexture(g_cov[victim].tex);
                g_cov[victim].tex = NULL;
                g_cov[victim].state = 0; // recarrega sob demanda se voltar a tela
                g_cov_texN--;
            }
        }
        g_cov[idx].tex = tex;
        g_cov[idx].last_used = SDL_GetTicks();
        if (tex) g_cov_texN++;
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
Screen g_screen = SC_LOGIN;

// Config saiu da barra de abas -> abre pelo botao (-). Assim L a partir do
// Inicio ja cai em Baixados (ultima aba).
#define TAB_HOME 0
#define TAB_DOWNLOADS 5
#define NTABS 6
static const char *TAB_NAME[] = { "Inicio", "Filmes", "Series", "Animes", "Doramas", "Salvos" };
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
static Uint32 g_dl_last_ok = 0;
static int g_download_awake = 0;
// vista da aba: 0 = grade de capas (por obra), 1 = episodios de uma obra
static int g_dlView = 0, g_dlGroup = 0, g_dlDetSel = 0, g_dlDetScroll = 0;
// agrupamento dos jobs por obra (series_id) ou filme (item_id negativo)
#define MAX_DLG 300
typedef struct { int key; int job[128]; int nJobs; int isMovie; } DlGroup;
static DlGroup g_dlg[MAX_DLG]; static int g_dlgN = 0;
// episodios ja assistidos (completed) da obra aberta no detalhe de Baixados
static int g_dlDone[256]; static int g_dlDoneN = 0;
// status de armazenamento (aba config)
static cJSON *g_accel_status = NULL;
// menu "baixar episodios" (Y no detalhe da serie)
static int g_dlmenu = 0;
static char g_epChk[512]; static int g_epChkN = 0;

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
static int accel_start(int itemId);
static int accel_wait_and_play(int itemId, const char *title);
static void do_search(void);
static void open_series(int id);
int resolve_and_play(int itemId, const char *title);
static int play_with_progress(int itemId, const char *title, const char *url);

// ------------------------------------------------------------- favoritos
static int idx_of(int *arr, int n, int v) { for (int i = 0; i < n; i++) if (arr[i] == v) return i; return -1; }

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
int is_fav_item(int id) { return idx_of(g_favItem, g_favItemN, id) >= 0; }
void toggle_fav_item(int id) {
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
// Retorna 1 se o video terminou naturalmente (p/ auto-play do proximo).
static int play_with_progress(int itemId, const char *title, const char *url) {
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
    // Cobre tambem a abertura da rede/FFmpeg e todos os retornos de erro.
    appletSetMediaPlaybackState(true);
    int r = player_play(gRen, g_joy, url, title, start, &pos, &dur);
    appletSetMediaPlaybackState(false);
    // O player controla a mesma flag de energia e sempre a desliga ao sair.
    // Forca a aba Salvos a recalcular/reaplicar seu proprio estado no proximo frame.
    g_download_awake = 0;
    if (dur > 0 && pos > 5) {
        char body[160];
        snprintf(body, sizeof(body), "{\"item_id\":%d,\"position_seconds\":%d,\"duration_seconds\":%d}", itemId, (int)pos, (int)dur);
        api_send("/api/sync/progress", "POST", body);
    }
    if (r < 0) { char m[80]; snprintf(m, sizeof(m), "Reproducao interrompida (erro %d)", r); toast(m); return 0; }
    return r;   // 1 = terminou
}

// Resolve a fonte e reproduz. Link direto (anime/dorama) toca na hora; torrent
// fica numa preparacao visual e inicia automaticamente quando estiver pronto.
int resolve_and_play(int itemId, const char *title) {
    SDL_SetRenderDrawColor(gRen, C_BG.r, C_BG.g, C_BG.b, 255); SDL_RenderClear(gRen);
    text_draw(gRen, "Carregando video...", WIN_W / 2 - 120, WIN_H / 2 - 16, C_TEXT, 1);
    SDL_RenderPresent(gRen);
    char url[1024]; snprintf(url, sizeof(url), "%s/api/stream/%d", BASE, itemId);
    struct membuf out = { 0 }; const char *err = NULL;
    long code = net_request(url, "POST", "{}", g_token[0] ? g_token : NULL, &out, &err);
    if (code != 200 || !out.data) { membuf_free(&out); toast("Falha ao resolver o stream"); return 0; }
    cJSON *j = cJSON_Parse(out.data);
    const char *container = jstr(j, "container");
    const char *play = jstr(j, "play_url");
    char purl[1200] = { 0 };
    if (play) {
        if (strncmp(play, "http", 4) == 0) snprintf(purl, sizeof(purl), "%s", play);
        else snprintf(purl, sizeof(purl), "%s%s", BASE, play);
    }
    int rc = 0;
    if (container && !strcmp(container, "torrent")) {
        rc = accel_wait_and_play(itemId, title);
    } else if (container && !strcmp(container, "embed")) {
        toast("Este conteudo (embed) ainda nao toca no Switch");
    } else if (purl[0]) {
        rc = play_with_progress(itemId, title, purl);
    } else toast("Sem fonte para tocar");
    if (j) cJSON_Delete(j);
    membuf_free(&out);
    return rc;
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
    else {
        if (open_movie_details(id) == 0) g_screen = SC_MOVIE;
        else toast("Falha ao carregar info do filme");
    }
}

// ------------------------------------------------------------- downloads (acelerador)
static cJSON *dl_jobs(void) { return g_dl ? cJSON_GetObjectItem(g_dl, "jobs") : NULL; }

static int dl_has_active(void) {
    cJSON *jobs = dl_jobs(), *job;
    if (!jobs || !g_dl_last_ok || SDL_GetTicks() - g_dl_last_ok > 30000) return 0;
    cJSON_ArrayForEach(job, jobs) {
        const char *state = jstr(job, "state");
        if (!cJSON_IsTrue(cJSON_GetObjectItem(job, "ready")) &&
            !(state && (!strcmp(state, "erro") || !strcmp(state, "error") ||
                        !strcmp(state, "failed") || !strcmp(state, "cancelled") ||
                        !strcmp(state, "canceled")))) return 1;
    }
    return 0;
}

// Os jobs rodam no servidor e continuam ate com o app fechado. Mantemos o
// console acordado somente enquanto o usuario acompanha a aba Salvos.
static void update_download_awake(void) {
    int want = (g_screen == SC_MAIN && g_tab == TAB_DOWNLOADS && dl_has_active());
    if (want == g_download_awake) return;
    appletSetMediaPlaybackState(want);
    g_download_awake = want;
}

static int accel_start(int itemId) {
    char path[64]; snprintf(path, sizeof(path), "/api/accel/download/%d", itemId);
    return api_send(path, "POST", "{}") == 200 ? 0 : -1;
}

static int accel_state_error(const char *state) {
    return state && (!strcmp(state, "erro") || !strcmp(state, "error") ||
                     !strcmp(state, "failed") || !strcmp(state, "cancelled") ||
                     !strcmp(state, "canceled"));
}

static void format_wait_time(int seconds, char *out, size_t cap) {
    if (seconds <= 0) { snprintf(out, cap, "calculando..."); return; }
    if (seconds < 60) snprintf(out, cap, "%d s", seconds);
    else if (seconds < 3600) snprintf(out, cap, "%d min %02d s", seconds / 60, seconds % 60);
    else snprintf(out, cap, "%d h %02d min", seconds / 3600, (seconds % 3600) / 60);
}

typedef struct {
    char path[80];
    cJSON *result;
    SDL_atomic_t done;
} AccelPoll;

// Poll curto em thread: a animacao nao para se a API estiver lenta ou durante
// uma reconexao. O player continua usando os timeouts normais mais tolerantes.
static int accel_poll_thread(void *userdata) {
    AccelPoll *poll = (AccelPoll *)userdata;
    char url[1024]; snprintf(url, sizeof(url), "%s%s", BASE, poll->path);
    struct membuf out = {0}; const char *err = NULL;
    long code = net_request_timeout(url, "GET", NULL, g_token[0] ? g_token : NULL,
                                    &out, &err, 2L, 4L);
    if (code == 200 && out.data) poll->result = cJSON_Parse(out.data);
    membuf_free(&out);
    SDL_AtomicSet(&poll->done, 1);
    return 0;
}

// Tela de espera deliberadamente animada: informa que o preparo ocorre no
// servidor, mostra dados reais do job e evita a falsa impressao de download no SD.
static void draw_accel_wait(const char *title, cJSON *job, int offline, Uint32 now) {
    int pct = job ? jint(job, "percent") : 0;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    const char *state = job ? jstr(job, "state") : NULL;
    cJSON *sz = job ? cJSON_GetObjectItem(job, "size") : NULL;
    cJSON *dl = job ? cJSON_GetObjectItem(job, "downloaded") : NULL;
    cJSON *sp = job ? cJSON_GetObjectItem(job, "speed") : NULL;
    double size = cJSON_IsNumber(sz) ? sz->valuedouble : 0;
    double downloaded = cJSON_IsNumber(dl) ? dl->valuedouble : 0;
    double speed = cJSON_IsNumber(sp) ? sp->valuedouble : 0;
    int eta = job ? jint(job, "eta_seconds") : 0;
    int peers = job ? jint(job, "peers") : 0;

    SDL_SetRenderDrawColor(gRen, C_BG.r, C_BG.g, C_BG.b, 255); SDL_RenderClear(gRen);
    fill_rect(0, 0, WIN_W, 68, C_BAR);
    text_draw(gRen, "NPLAY  /  PREPARANDO REPRODUCAO", 42, 21, C_ACC, 0);
    text_draw(gRen, "B  voltar e continuar em segundo plano", WIN_W - 430, 21, C_MUT, 0);
    text_clip(title ? title : "Video", 70, 96, C_TEXT, 1, WIN_W - 140);

    // Oito barras em onda. O movimento continua mesmo enquanto o percentual
    // ainda e desconhecido (fila, busca de peers ou leitura de metadados).
    int phase = (int)((now / 90) % 8);
    for (int i = 0; i < 8; i++) {
        int d = (i - phase + 8) % 8;
        int h = 18 + (7 - d) * 5;
        SDL_Color c = d < 2 ? C_ACC : (d < 5 ? C_ACC2 : C_CARD);
        fill_rect(WIN_W / 2 - 94 + i * 24, 178 + (54 - h) / 2, 14, h, c);
    }

    const char *headline = offline ? "Reconectando ao servidor..." :
        (state && !strcmp(state, "fila")) ? "Seu pedido esta na fila" :
        (state && !strcmp(state, "baixando") && pct == 0) ? "Localizando fontes e preparando o arquivo" :
        "Preparando o video para reproduzir";
    int hw = 0, hh = 0; text_cached(gRen, headline, C_TEXT, 1, &hw, &hh);
    text_draw(gRen, headline, (WIN_W - hw) / 2, 255, C_TEXT, 1);
    text_draw(gRen, "O processamento acontece no servidor e nao ocupa espaco no seu Switch.", 246, 305, C_MUT, 0);

    int bx = 120, by = 365, bw = WIN_W - 240;
    fill_rect(bx, by, bw, 20, C_CARD);
    if (pct > 0) fill_rect(bx, by, bw * pct / 100, 20, C_ACC2);
    else { int iw = 150, ix = bx + (int)((now / 5) % (bw + iw)) - iw; if (ix < bx) iw -= bx - ix, ix = bx; if (ix + iw > bx + bw) iw = bx + bw - ix; if (iw > 0) fill_rect(ix, by, iw, 20, C_ACC2); }
    char percent[32]; snprintf(percent, sizeof(percent), "%d%%", pct);
    text_draw(gRen, percent, WIN_W / 2 - 24, 399, C_TEXT, 1);

    char amount[128], rate[96], remaining[96];
    if (size > 0) snprintf(amount, sizeof(amount), "Preparado: %.1f de %.1f MB", downloaded / 1048576.0, size / 1048576.0);
    else snprintf(amount, sizeof(amount), "Preparado: %.1f MB", downloaded / 1048576.0);
    if (speed > 0) snprintf(rate, sizeof(rate), "Velocidade: %.1f MB/s", speed / 1048576.0);
    else snprintf(rate, sizeof(rate), "Velocidade: calculando...");
    char eta_text[48]; format_wait_time(eta, eta_text, sizeof(eta_text));
    snprintf(remaining, sizeof(remaining), "Tempo restante: %s", eta_text);
    fill_rect(120, 466, 330, 82, C_CARD); fill_rect(475, 466, 300, 82, C_CARD); fill_rect(800, 466, 360, 82, C_CARD);
    text_draw(gRen, "PROGRESSO", 140, 478, C_ACC2, 0); text_clip(amount, 140, 512, C_TEXT, 0, 290);
    text_draw(gRen, "VELOCIDADE", 495, 478, C_ACC2, 0); text_clip(rate, 495, 512, C_TEXT, 0, 260);
    text_draw(gRen, "PREVISAO", 820, 478, C_ACC2, 0); text_clip(remaining, 820, 512, C_TEXT, 0, 320);
    if (peers > 0) { char ps[64]; snprintf(ps, sizeof(ps), "%d fonte%s conectada%s", peers, peers == 1 ? "" : "s", peers == 1 ? "" : "s"); text_draw(gRen, ps, 120, 579, C_GREEN, 0); }
    text_draw(gRen, "Quando ficar pronto, a reproducao comeca automaticamente.", 120, 618, C_TEXT, 0);
    text_draw(gRen, "B volta sem cancelar: o servidor continua preparando e o item aparece em Salvos.", 120, 654, C_MUT, 0);
    SDL_RenderPresent(gRen);
}

static int accel_wait_and_play(int itemId, const char *title) {
    if (accel_start(itemId) != 0) { toast("Nao foi possivel preparar este video"); return 0; }
    cJSON *status = NULL;
    Uint32 next_poll = 0;
    int failures = 0, waiting = 1, rc = 0, user_back = 0;
    AccelPoll poll = {0};
    SDL_Thread *poll_thread = NULL;
    appletSetMediaPlaybackState(true);
    while (waiting) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { g_running = 0; waiting = 0; }
            else if (e.type == SDL_JOYBUTTONDOWN &&
                     (e.jbutton.button == JOY_B || e.jbutton.button == JOY_MINUS)) {
                user_back = 1; waiting = 0;
            }
        }
        Uint32 now = SDL_GetTicks();
        if (poll_thread && SDL_AtomicGet(&poll.done)) {
            SDL_WaitThread(poll_thread, NULL); poll_thread = NULL;
            if (poll.result) {
                if (status) cJSON_Delete(status);
                status = poll.result; poll.result = NULL; failures = 0;
            } else failures++;
            next_poll = SDL_GetTicks() + 900;
        }
        if (waiting && !poll_thread && (next_poll == 0 || now >= next_poll)) {
            snprintf(poll.path, sizeof(poll.path), "/api/accel/jobs/%d", itemId);
            poll.result = NULL; SDL_AtomicSet(&poll.done, 0);
            poll_thread = SDL_CreateThread(accel_poll_thread, "accel-poll", &poll);
            if (!poll_thread) { failures++; next_poll = now + 900; }
        }
        cJSON *job = status ? cJSON_GetObjectItem(status, "job") : NULL;
        if (job && cJSON_IsTrue(cJSON_GetObjectItem(job, "ready"))) {
            const char *fu = jstr(status, "file_url");
            if (fu) {
                char url[1400];
                if (!strncmp(fu, "http", 4)) snprintf(url, sizeof(url), "%s", fu);
                else snprintf(url, sizeof(url), "%s%s", BASE, fu);
                appletSetMediaPlaybackState(false); g_download_awake = 0;
                rc = play_with_progress(itemId, title, url);
                waiting = 0;
                break;
            }
        }
        if (job && accel_state_error(jstr(job, "state"))) {
            const char *msg = jstr(job, "error");
            toast(msg && msg[0] ? msg : "Falha ao preparar o video");
            waiting = 0; break;
        }
        draw_accel_wait(title, job, failures > 0, now);
        SDL_Delay(16);
    }
    if (poll_thread) SDL_WaitThread(poll_thread, NULL);
    if (poll.result) cJSON_Delete(poll.result);
    if (status) cJSON_Delete(status);
    appletSetMediaPlaybackState(false); g_download_awake = 0;
    if (user_back) toast("Preparacao continua no servidor");
    return rc;
}
static void accel_remove(int itemId) {
    char path[64]; snprintf(path, sizeof(path), "/api/accel/jobs/%d", itemId);
    api_send(path, "DELETE", "{}");
}
// Agrupa os jobs por OBRA: serie (series_id) num card so; filme = card avulso.
static void build_dl_groups(void) {
    g_dlgN = 0;
    cJSON *jobs = dl_jobs(); int n = arr_len(jobs);
    for (int i = 0; i < n; i++) {
        cJSON *j = cJSON_GetArrayItem(jobs, i);
        const char *kind = jstr(j, "kind");
        int isEp = kind && !strcmp(kind, "episode");
        int key = (isEp && jint(j, "series_id") > 0) ? jint(j, "series_id") : -jint(j, "item_id");
        int g = -1;
        for (int k = 0; k < g_dlgN; k++) if (g_dlg[k].key == key) { g = k; break; }
        if (g < 0 && g_dlgN < MAX_DLG) { g = g_dlgN++; g_dlg[g].key = key; g_dlg[g].nJobs = 0; g_dlg[g].isMovie = !isEp; }
        if (g >= 0 && g_dlg[g].nJobs < 128) g_dlg[g].job[g_dlg[g].nJobs++] = i;
    }
}
static cJSON *dlg_job(int g, int idx) { return cJSON_GetArrayItem(dl_jobs(), g_dlg[g].job[idx]); }
// carrega os episodios ja assistidos da serie (p/ marcar "visto" no detalhe)
static void load_dl_done(int series_id) {
    g_dlDoneN = 0;
    if (series_id <= 0) return;
    char p[64]; snprintf(p, sizeof(p), "/api/catalog/series/%d", series_id);
    cJSON *sd = api_get(p);
    if (!sd) return;
    cJSON *seasons = cJSON_GetObjectItem(sd, "seasons"), *arr;
    cJSON_ArrayForEach(arr, seasons) {
        cJSON *ep;
        cJSON_ArrayForEach(ep, arr) {
            if (cJSON_IsTrue(cJSON_GetObjectItem(ep, "completed")) && g_dlDoneN < 256) g_dlDone[g_dlDoneN++] = jint(ep, "id");
        }
    }
    cJSON_Delete(sd);
}
static int dl_is_done(int item_id) { for (int i = 0; i < g_dlDoneN; i++) if (g_dlDone[i] == item_id) return 1; return 0; }

#define LOCAL_DL_DIR "sdmc:/switch/Nplay/downloads"
static void local_dl_path(int item_id, char *out, size_t cap) {
    snprintf(out, cap, LOCAL_DL_DIR "/%d.media", item_id);
}
static int local_dl_exists(int item_id) {
    char path[160]; local_dl_path(item_id, path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f); return 1;
}
static int local_dl_count(void) {
    int n = 0; DIR *d = opendir(LOCAL_DL_DIR); if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d))) if (e->d_name[0] != '.' && strstr(e->d_name, ".media")) n++;
    closedir(d); return n;
}

typedef struct { const char *title; Uint32 last_draw; int cancel; } LocalDlProgress;
static int local_dl_progress(long long received, long long total, void *userdata) {
    LocalDlProgress *p = (LocalDlProgress *)userdata;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT || (ev.type == SDL_JOYBUTTONDOWN && ev.jbutton.button == JOY_B)) p->cancel = 1;
    }
    Uint32 now = SDL_GetTicks();
    if (now - p->last_draw >= 100 || received == total) {
        p->last_draw = now;
        SDL_SetRenderDrawColor(gRen, C_BG.r, C_BG.g, C_BG.b, 255); SDL_RenderClear(gRen);
        text_draw(gRen, "Baixando para a microSD", 60, 82, C_TEXT, 1);
        text_clip(p->title ? p->title : "Video", 60, 132, C_MUT, 0, WIN_W - 120);
        int pct = total > 0 ? (int)(received * 100 / total) : 0;
        if (pct > 100) pct = 100;
        fill_rect(60, 210, WIN_W - 120, 18, C_CARD);
        fill_rect(60, 210, (WIN_W - 120) * pct / 100, 18, C_ACC2);
        char status[128];
        if (total > 0) snprintf(status, sizeof(status), "%d%%  -  %.1f de %.1f MB", pct, received / 1048576.0, total / 1048576.0);
        else snprintf(status, sizeof(status), "%.1f MB recebidos", received / 1048576.0);
        text_draw(gRen, status, 60, 250, C_TEXT, 0);
        text_draw(gRen, "Mantenha o app aberto  |  B cancela", 60, WIN_H - 54, C_MUT, 0);
        SDL_RenderPresent(gRen);
    }
    return p->cancel;
}

static void download_to_switch(cJSON *job) {
    if (!job || !cJSON_IsTrue(cJSON_GetObjectItem(job, "ready"))) { toast("O arquivo do servidor ainda nao esta pronto"); return; }
    int item_id = jint(job, "item_id");
    if (local_dl_exists(item_id)) { toast("Este item ja esta na microSD"); return; }
    const char *fu = jstr(job, "file_url");
    if (!fu) { toast("Link do arquivo indisponivel"); return; }
    mkdir("sdmc:/switch/Nplay", 0777); mkdir(LOCAL_DL_DIR, 0777);
    char url[1400], final[180], part[190];
    if (!strncmp(fu, "http", 4)) snprintf(url, sizeof(url), "%s", fu);
    else snprintf(url, sizeof(url), "%s%s", BASE, fu);
    local_dl_path(item_id, final, sizeof(final));
    snprintf(part, sizeof(part), "%s.part", final);
    LocalDlProgress progress = { jstr(job, "title"), 0, 0 };
    const char *err = NULL;
    appletSetMediaPlaybackState(true);
    long code = net_download_file_progress(url, NULL, part, &err, local_dl_progress, &progress);
    appletSetMediaPlaybackState(false); g_download_awake = 0;
    if (code == 200 && rename(part, final) == 0) toast("Download concluido na microSD");
    else { remove(part); toast(progress.cancel ? "Download cancelado" : "Falha ao baixar para a microSD"); }
}
static void remove_from_switch(cJSON *job) {
    if (!job) return;
    char path[180]; local_dl_path(jint(job, "item_id"), path, sizeof(path));
    if (remove(path) == 0) toast("Arquivo removido da microSD");
    else toast("Este item nao esta na microSD");
}
static void load_downloads(void) {
    cJSON *fresh = api_get("/api/accel/jobs");
    if (!fresh) return; // preserva a ultima lista numa falha transitoria de rede
    if (g_dl) cJSON_Delete(g_dl);
    g_dl = fresh;
    g_dl_last_ok = SDL_GetTicks();
    build_dl_groups();
    if (g_dlSel >= g_dlgN) g_dlSel = g_dlgN > 0 ? g_dlgN - 1 : 0;
    if (g_dlView == 1) {
        if (g_dlGroup >= g_dlgN) { g_dlView = 0; g_dlGroup = 0; g_dlDetSel = 0; }
        else { int nj = g_dlg[g_dlGroup].nJobs; if (g_dlDetSel >= nj) g_dlDetSel = nj > 0 ? nj - 1 : 0; }
    }
}
static int dl_play(cJSON *job) {
    char local[180]; local_dl_path(jint(job, "item_id"), local, sizeof(local));
    if (local_dl_exists(jint(job, "item_id")))
        return play_with_progress(jint(job, "item_id"), jstr(job, "title"), local);
    const char *fu = jstr(job, "file_url");
    if (!fu) { toast("Sem arquivo"); return 0; }
    char url[1400];
    if (strncmp(fu, "http", 4) == 0) snprintf(url, sizeof(url), "%s", fu);
    else snprintf(url, sizeof(url), "%s%s", BASE, fu);
    return play_with_progress(jint(job, "item_id"), jstr(job, "title"), url);
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
    text_draw(gRen, "Y busca   (-) config   (+) sair", WIN_W - 330, 24, C_MUT, 0);
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
        char cnt[32]; snprintf(cnt, sizeof(cnt), "%d / %d", (g_heroIdx % nh) + 1, nh);
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
// A obra pode ter temporadas AGRUPADAS (season_group = series-irmas por group_key,
// ex.: Grand Blue T1/T2/T3) e versoes de audio (Legendado/Dublado). Unificamos:
// se agrupado, L/R troca de temporada CARREGANDO a serie-irma; senao, troca a
// temporada interna (seasons).
static cJSON *ser_obj(void) { return g_ser ? cJSON_GetObjectItem(g_ser, "series") : NULL; }
static cJSON *ser_group(void) { return cJSON_GetObjectItem(ser_obj(), "season_group"); }
static cJSON *ser_audio(void) { return cJSON_GetObjectItem(ser_obj(), "audio_versions"); }
static cJSON *seasons_obj(void) { return g_ser ? cJSON_GetObjectItem(g_ser, "seasons") : NULL; }
static cJSON *season_arr(void) { return cJSON_GetArrayItem(seasons_obj(), g_seasonIdx); }
static int season_count(void) { cJSON *s = seasons_obj(); return s ? cJSON_GetArraySize(s) : 0; }
static int ser_grouped(void) { return arr_len(ser_group()) > 1; }
static int ser_group_idx(void) {
    cJSON *g = ser_group(); int sid = jint(ser_obj(), "id"), k = 0, i = 0; cJSON *e;
    cJSON_ArrayForEach(e, g) { if (jint(e, "id") == sid) { k = i; break; } i++; }
    return k;
}
static int ser_nseasons(void) { return ser_grouped() ? arr_len(ser_group()) : season_count(); }
// episodios visiveis: agrupado -> junta as temporadas internas (em geral 1);
// senao -> a temporada interna selecionada.
static int ser_nep(void) {
    if (!ser_grouped()) return arr_len(season_arr());
    cJSON *arr; int n = 0; cJSON_ArrayForEach(arr, seasons_obj()) n += arr_len(arr);
    return n;
}
static cJSON *ser_ep_at(int idx) {
    if (!ser_grouped()) return cJSON_GetArrayItem(season_arr(), idx);
    cJSON *arr; cJSON_ArrayForEach(arr, seasons_obj()) { int k = arr_len(arr); if (idx < k) return cJSON_GetArrayItem(arr, idx); idx -= k; }
    return NULL;
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
    cJSON *s = ser_obj();
    int sid = jint(s, "id");
    int fav = is_fav_series(sid);
    fill_rect(0, 0, WIN_W, 66, C_BAR);
    text_draw(gRen, "< (B) voltar", 40, 22, C_MUT, 0);
    const char *title = jstr(s, "title"); if (!title) title = "Serie";
    text_clip(title, 200, 16, C_TEXT, 1, WIN_W - 420);
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

    // versoes de audio (Legendado/Dublado) - troca com Y
    cJSON *au = ser_audio();
    if (arr_len(au) > 1) {
        text_draw(gRen, "Audio (ZL/ZR):", LX, 520, C_MUT, 0);
        int axx = LX; cJSON *av;
        cJSON_ArrayForEach(av, au) {
            int cur = cJSON_IsTrue(cJSON_GetObjectItem(av, "current"));
            const char *lb = jstr(av, "label"); if (!lb) lb = "?";
            int w = text_draw(gRen, lb, axx, 548, cur ? C_TEXT : C_MUT, 0);
            if (cur) fill_rect(axx, 570, w, 3, C_ACC);
            axx += w + 20;
        }
    }

    int RX = 300, REND = WIN_W - 40;
    int grouped = ser_grouped();
    int nsea = ser_nseasons(), nep = ser_nep();
    char sh[64];
    if (grouped) snprintf(sh, sizeof(sh), "Temporada %d de %d", ser_group_idx() + 1, nsea);
    else { cJSON *sa = season_arr(); snprintf(sh, sizeof(sh), "Temporada %s", (sa && sa->string) ? sa->string : "1"); }
    text_draw(gRen, sh, RX, 100, C_ACC, 1);
    if (nsea > 1) { char hint[64]; snprintf(hint, sizeof(hint), "L/R troca temporada  (%d)", nsea); text_draw(gRen, hint, RX + 300, 108, C_MUT, 0); }
    fill_rect(RX, 148, REND - RX, 2, C_CARD);

    int listTop = 168, rowH = 40, visible = (WIN_H - listTop - 44) / rowH;
    if (g_epSel < g_epScroll) g_epScroll = g_epSel;
    if (g_epSel >= g_epScroll + visible) g_epScroll = g_epSel - visible + 1;
    for (int i = g_epScroll; i < nep && i < g_epScroll + visible; i++) {
        cJSON *ep = ser_ep_at(i);
        int yy = listTop + (i - g_epScroll) * rowH;
        if (i == g_epSel) fill_rect(RX - 8, yy - 5, REND - RX + 16, rowH - 2, C_CARD);
        int en = jint(ep, "episode"); char nb[16]; snprintf(nb, sizeof(nb), "%d", en > 0 ? en : i + 1);
        int done = cJSON_IsTrue(cJSON_GetObjectItem(ep, "completed"));
        text_draw(gRen, nb, RX, yy, (i == g_epSel) ? C_ACC : (done ? C_GREEN : C_MUT), 0);
        text_clip(ep_clean(jstr(ep, "title")), RX + 52, yy, (i == g_epSel) ? C_TEXT : C_MUT, 0, REND - RX - 110);
        if (done) text_draw(gRen, "visto", REND - 60, yy, C_GREEN, 0);
    }
    // barra de rolagem (ha muitos episodios)
    if (nep > visible) {
        int trkH = visible * rowH, thumbH = trkH * visible / nep;
        int thumbY = listTop + (trkH - thumbH) * g_epScroll / (nep - visible);
        fill_rect(REND + 8, listTop, 4, trkH, C_CARD);
        fill_rect(REND + 8, thumbY, 4, thumbH < 12 ? 12 : thumbH, C_ACC);
    }
    if (nep == 0) text_draw(gRen, "Sem episodios", RX, listTop, C_MUT, 0);
    text_draw(gRen, "A assistir  L/R temporada  Y salvar na conta  X lista  ZL/ZR audio", RX, WIN_H - 36, C_MUT, 0);
}

// Menu "baixar episodios" (Y no detalhe): marca quais episodios baixar.
static void open_dlmenu(void) {
    int n = ser_nep();
    g_epChkN = n > 512 ? 512 : n;
    for (int i = 0; i < g_epChkN; i++) g_epChk[i] = 0;
    g_dlmenu = 1;
}
static void draw_dlmenu(void) {
    fill_rect(0, 0, WIN_W, 66, C_BAR);
    text_draw(gRen, "Salvar na conta (fica no servidor, pronto pra assistir)", 40, 20, C_TEXT, 1);
    cJSON *s = ser_obj(); const char *title = jstr(s, "title");
    if (title) text_clip(title, 320, 22, C_MUT, 0, WIN_W - 360);
    int n = ser_nep(), cnt = 0;
    for (int i = 0; i < g_epChkN; i++) if (g_epChk[i]) cnt++;
    int listTop = 92, rowH = 40, visible = (WIN_H - listTop - 56) / rowH;
    if (g_epSel < g_epScroll) g_epScroll = g_epSel;
    if (g_epSel >= g_epScroll + visible) g_epScroll = g_epSel - visible + 1;
    for (int i = g_epScroll; i < n && i < g_epScroll + visible; i++) {
        cJSON *ep = ser_ep_at(i);
        int yy = listTop + (i - g_epScroll) * rowH, sel = (i == g_epSel);
        if (sel) fill_rect(32, yy - 5, WIN_W - 64, rowH - 2, C_CARD);
        int chk = (i < g_epChkN) && g_epChk[i];
        border_rect(48, yy + 1, 22, 22, 2, chk ? C_ACC : C_MUT);
        if (chk) fill_rect(53, yy + 6, 12, 12, C_ACC);
        int en = jint(ep, "episode"); char nb[16]; snprintf(nb, sizeof(nb), "Ep %d", en > 0 ? en : i + 1);
        text_draw(gRen, nb, 86, yy, sel ? C_ACC : C_MUT, 0);
        text_clip(ep_clean(jstr(ep, "title")), 170, yy, sel ? C_TEXT : C_MUT, 0, WIN_W - 240);
    }
    char foot[110]; snprintf(foot, sizeof(foot), "%d selecionado(s)   -   A marca   X todos   Y salva selecionados   B cancela", cnt);
    text_draw(gRen, foot, 40, WIN_H - 40, C_MUT, 0);
}
static void input_dlmenu(int b) {
    int n = ser_nep();
    if (b == JOY_B || b == JOY_MINUS) { g_dlmenu = 0; }
    else if (b == JOY_UP) { if (g_epSel > 0) g_epSel--; }
    else if (b == JOY_DOWN) { if (g_epSel < n - 1) g_epSel++; }
    else if (b == JOY_A) { if (g_epSel < g_epChkN) g_epChk[g_epSel] = !g_epChk[g_epSel]; }
    else if (b == JOY_X) {   // todos / nenhum
        int any = 0; for (int i = 0; i < g_epChkN; i++) if (!g_epChk[i]) { any = 1; break; }
        for (int i = 0; i < g_epChkN; i++) g_epChk[i] = any ? 1 : 0;
    }
    else if (b == JOY_Y) {   // confirma
        char body[6000]; int k = 0, cnt = 0;
        k += snprintf(body + k, sizeof(body) - k, "{\"item_ids\":[");
        for (int i = 0; i < n && i < g_epChkN; i++) {
            if (!g_epChk[i]) continue;
            cJSON *ep = ser_ep_at(i); if (!ep) continue;
            if (k > (int)sizeof(body) - 20) break;
            k += snprintf(body + k, sizeof(body) - k, "%s%d", cnt ? "," : "", jint(ep, "id"));
            cnt++;
        }
        k += snprintf(body + k, sizeof(body) - k, "]}");
        if (cnt == 0) { toast("Selecione ao menos um episodio (A)"); return; }
        api_send("/api/accel/download-batch", "POST", body);
        char m[64]; snprintf(m, sizeof(m), "%d episodio(s) salvos na sua conta", cnt);
        toast(m); g_dlmenu = 0; g_screen = SC_MAIN; enter_tab(TAB_DOWNLOADS);
    }
}

// ------------------------------------------------------------- render: downloads
// Vista 1: GRADE de capas, uma por obra (serie agrupada / filme avulso).
static void draw_dl_grid(void) {
    text_draw(gRen, "Salvos na sua conta", 40, 78, C_TEXT, 1);
    text_draw(gRen, "(ficam no servidor - assiste de qualquer aparelho, sem ocupar o Switch)", 40, 108, C_MUT, 0);
    if (dl_has_active()) text_draw(gRen, "TELA ATIVA", WIN_W - 170, 84, C_GREEN, 0);
    if (g_dlgN == 0) {
        text_draw(gRen, "Nada salvo ainda.", 40, 152, C_MUT, 0);
        text_draw(gRen, "Abra um filme e A, ou Y numa serie pra escolher os episodios.", 40, 184, C_MUT, 0);
        return;
    }
    int top = 138;
    for (int i = 0; i < g_dlgN; i++) {
        int col = i % GCOLS, row = i / GCOLS;
        int x = GMX + col * (GCW + GGAP) + (GCW - GCOVERW) / 2;
        int yy = top + row * (GCH + GGAP) - g_dlScroll;
        if (yy + GCH < 66 || yy > WIN_H) continue;
        cJSON *j0 = dlg_job(i, 0);
        SDL_Texture *cov = cover_get(jstr(j0, "cover"));
        SDL_Rect cr = { x, yy, GCOVERW, GCOVERH };
        if (cov) SDL_RenderCopy(gRen, cov, NULL, &cr); else fill_rect(x, yy, GCOVERW, GCOVERH, C_CARD);
        int nJobs = g_dlg[i].nJobs, baixando = 0;
        for (int k = 0; k < nJobs; k++) if (!cJSON_IsTrue(cJSON_GetObjectItem(dlg_job(i, k), "ready"))) baixando++;
        char badge[32];
        if (g_dlg[i].isMovie) { if (local_dl_exists(jint(j0, "item_id"))) snprintf(badge, sizeof(badge), "MICROSD"); else if (baixando) snprintf(badge, sizeof(badge), "%d%%", jint(j0, "percent")); else snprintf(badge, sizeof(badge), "PRONTO"); }
        else snprintf(badge, sizeof(badge), baixando ? "%d ep - salvando" : "%d ep", nJobs);
        fill_rect(x, yy + GCOVERH - 26, GCOVERW, 26, C_BAR);
        text_draw(gRen, badge, x + 6, yy + GCOVERH - 24, baixando ? C_ACC : C_GREEN, 0);
        const char *title = jstr(j0, "title"); if (!title) title = "";
        char sh[48]; short_title(title, sh, sizeof(sh));
        text_clip(sh, x, yy + GCOVERH + 6, i == g_dlSel ? C_TEXT : C_MUT, 0, GCOVERW);
        if (i == g_dlSel) border_rect(x - 3, yy - 3, GCOVERW + 6, GCOVERH + 6, 3, C_ACC2);
    }
    text_draw(gRen, "A abre  |  Y microSD  |  ZR apaga microSD  |  X remove do servidor", 40, WIN_H - 34, C_MUT, 0);
}
// Vista 2: episodios baixados de UMA obra (com status), estilo menu de serie.
static void draw_dl_detail(void) {
    int g = g_dlGroup;
    if (g >= g_dlgN) { g_dlView = 0; return; }
    cJSON *j0 = dlg_job(g, 0);
    const char *title = jstr(j0, "title"); if (!title) title = "";
    text_draw(gRen, "< (B) voltar", 40, 84, C_MUT, 0);
    if (dl_has_active()) text_draw(gRen, "TELA ATIVA", WIN_W - 170, 84, C_GREEN, 0);
    text_clip(title, 200, 82, C_TEXT, 1, WIN_W - 240);
    int nj = g_dlg[g].nJobs;
    int listTop = 128, rowH = 46, visible = (WIN_H - listTop - 44) / rowH;
    if (g_dlDetSel < g_dlDetScroll) g_dlDetScroll = g_dlDetSel;
    if (g_dlDetSel >= g_dlDetScroll + visible) g_dlDetScroll = g_dlDetSel - visible + 1;
    for (int i = g_dlDetScroll; i < nj && i < g_dlDetScroll + visible; i++) {
        cJSON *j = dlg_job(g, i);
        int yy = listTop + (i - g_dlDetScroll) * rowH, sel = (i == g_dlDetSel);
        if (sel) fill_rect(32, yy - 6, WIN_W - 64, rowH - 4, C_CARD);
        int ready = cJSON_IsTrue(cJSON_GetObjectItem(j, "ready"));
        const char *state = jstr(j, "state");
        int erro = state && (!strcmp(state, "erro") || !strcmp(state, "error") ||
                             !strcmp(state, "failed") || !strcmp(state, "cancelled") ||
                             !strcmp(state, "canceled"));
        int pct = jint(j, "percent");
        int done = dl_is_done(jint(j, "item_id"));
        char lab[48];
        if (g_dlg[g].isMovie) snprintf(lab, sizeof(lab), "Filme");
        else snprintf(lab, sizeof(lab), "T%d  Ep %d", jint(j, "season") > 0 ? jint(j, "season") : 1, jint(j, "episode"));
        text_draw(gRen, lab, 52, yy, sel ? C_ACC : (done ? C_GREEN : C_MUT), 0);
        const char *et = jstr(j, "ep_title"); if (!et || !et[0]) et = title;
        text_clip(ep_clean(et), 200, yy, sel ? C_TEXT : C_MUT, 0, WIN_W - 500);
        if (done) text_draw(gRen, "visto", WIN_W - 340, yy, C_GREEN, 0);
        char st[40];
        if (local_dl_exists(jint(j, "item_id"))) snprintf(st, sizeof(st), "MICROSD");
        else if (ready) snprintf(st, sizeof(st), "PRONTO");
        else if (erro) snprintf(st, sizeof(st), "ERRO");
        else snprintf(st, sizeof(st), "salvando %d%%", pct);
        text_draw(gRen, st, WIN_W - 260, yy, ready ? C_GREEN : (erro ? C_ROSE : C_ACC), 0);
        int bx = WIN_W - 260, by = yy + 24, bw = 200, bh = 5;
        fill_rect(bx, by, bw, bh, C_BAR);
        int fw = bw * pct / 100; if (fw > bw) fw = bw; if (fw < 0) fw = 0;
        fill_rect(bx, by, fw, bh, ready ? C_GREEN : (erro ? C_ROSE : C_ACC));
    }
    if (nj > visible) {
        int trkH = visible * rowH, thumbH = trkH * visible / nj;
        int thumbY = listTop + (trkH - thumbH) * g_dlDetScroll / (nj - visible);
        fill_rect(WIN_W - 22, listTop, 4, trkH, C_CARD);
        fill_rect(WIN_W - 22, thumbY, 4, thumbH < 12 ? 12 : thumbH, C_ACC);
    }
    text_draw(gRen, "A assistir  |  Y microSD  |  ZR apaga microSD  |  X remove servidor  |  B volta", 40, WIN_H - 34, C_MUT, 0);
}
static void draw_downloads(void) {
    draw_topbar();
    if (g_dlView == 1) draw_dl_detail(); else draw_dl_grid();
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
    if (tab == TAB_DOWNLOADS) { g_dlSel = 0; g_dlScroll = 0; g_dlView = 0; load_downloads(); g_dl_next = SDL_GetTicks() + 2000; return; }
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
    if (g_dlmenu) { input_dlmenu(b); return; }   // menu "baixar episodios" aberto
    int nep = ser_nep();
    if (b == JOY_B || b == JOY_MINUS) { g_screen = SC_MAIN; }
    else if (b == JOY_X) { cJSON *s = ser_obj(); if (s) toggle_fav_series(jint(s, "id")); }
    else if (b == JOY_Y) { open_dlmenu(); }       // escolher episodios pra baixar
    else if (b == JOY_ZL || b == JOY_ZR) {        // troca audio (Legendado <-> Dublado)
        cJSON *au = ser_audio();
        if (arr_len(au) > 1) { cJSON *av; cJSON_ArrayForEach(av, au) { if (!cJSON_IsTrue(cJSON_GetObjectItem(av, "current"))) { open_series(jint(av, "id")); break; } } }
    }
    else if (b == JOY_UP) { if (g_epSel > 0) g_epSel--; }
    else if (b == JOY_DOWN) { if (g_epSel < nep - 1) g_epSel++; }
    else if (b == JOY_L) {
        if (ser_grouped()) { int i = ser_group_idx(); if (i > 0) open_series(jint(cJSON_GetArrayItem(ser_group(), i - 1), "id")); }
        else if (g_seasonIdx > 0) { g_seasonIdx--; g_epSel = 0; g_epScroll = 0; }
    }
    else if (b == JOY_R) {
        if (ser_grouped()) { int i = ser_group_idx(); if (i < arr_len(ser_group()) - 1) open_series(jint(cJSON_GetArrayItem(ser_group(), i + 1), "id")); }
        else if (g_seasonIdx < season_count() - 1) { g_seasonIdx++; g_epSel = 0; g_epScroll = 0; }
    }
    else if (b == JOY_A) {   // assistir + auto-play do proximo episodio
        int idx = g_epSel;
        while (idx < ser_nep()) {
            cJSON *ep = ser_ep_at(idx); if (!ep) break;
            g_epSel = idx;
            int ended = resolve_and_play(jint(ep, "id"), ep_clean(jstr(ep, "title")));
            if (ended != 1) break;   // usuario saiu / erro / virou download -> para
            idx++;
        }
    }
}
static void input_downloads(int b) {
    if (g_dlView == 1) {   // detalhe: episodios baixados de uma obra
        int g = g_dlGroup, nj = (g < g_dlgN) ? g_dlg[g].nJobs : 0;
        if (b == JOY_B || b == JOY_MINUS) { g_dlView = 0; }
        else if (b == JOY_UP) { if (g_dlDetSel > 0) g_dlDetSel--; }
        else if (b == JOY_DOWN) { if (g_dlDetSel < nj - 1) g_dlDetSel++; }
        else if (b == JOY_A) {   // assistir + auto-play do proximo baixado
            int idx = g_dlDetSel;
            while (idx < g_dlg[g].nJobs) {
                cJSON *j = dlg_job(g, idx);
                if (!cJSON_IsTrue(cJSON_GetObjectItem(j, "ready"))) { toast("Ainda salvando..."); break; }
                g_dlDetSel = idx;
                if (dl_play(j) != 1) break;
                idx++;
            }
        }
        else if (b == JOY_Y) { cJSON *j = dlg_job(g, g_dlDetSel); if (j) download_to_switch(j); }
        else if (b == JOY_ZR) { cJSON *j = dlg_job(g, g_dlDetSel); if (j) remove_from_switch(j); }
        else if (b == JOY_X) { cJSON *j = dlg_job(g, g_dlDetSel); if (j) { accel_remove(jint(j, "item_id")); load_downloads(); toast("Removido"); } }
        return;
    }
    // grade de obras
    int n = g_dlgN;
    if (b == JOY_UP) { if (g_dlSel - GCOLS >= 0) g_dlSel -= GCOLS; }
    else if (b == JOY_DOWN) { if (g_dlSel + GCOLS < n) g_dlSel += GCOLS; }
    else if (b == JOY_DLEFT) { if (g_dlSel > 0) g_dlSel--; }
    else if (b == JOY_DRIGHT) { if (g_dlSel + 1 < n) g_dlSel++; }
    else if (b == JOY_A) {
        if (g_dlSel < n) {
            if (g_dlg[g_dlSel].isMovie) { cJSON *j = dlg_job(g_dlSel, 0); if (cJSON_IsTrue(cJSON_GetObjectItem(j, "ready"))) dl_play(j); else toast("Ainda salvando..."); }
            else { g_dlGroup = g_dlSel; g_dlDetSel = 0; g_dlDetScroll = 0; g_dlView = 1; load_dl_done(jint(dlg_job(g_dlSel, 0), "series_id")); }
        }
    }
    else if (b == JOY_Y) {
        if (g_dlSel < n) {
            if (g_dlg[g_dlSel].isMovie) download_to_switch(dlg_job(g_dlSel, 0));
            else { g_dlGroup = g_dlSel; g_dlDetSel = 0; g_dlDetScroll = 0; g_dlView = 1; toast("Escolha um episodio e pressione Y"); }
        }
    }
    else if (b == JOY_ZR) { if (g_dlSel < n && g_dlg[g_dlSel].isMovie) remove_from_switch(dlg_job(g_dlSel, 0)); }
    else if (b == JOY_X) {   // remove a obra inteira
        if (g_dlSel < n) { int g = g_dlSel; for (int k = g_dlg[g].nJobs - 1; k >= 0; k--) { cJSON *j = dlg_job(g, k); if (j) accel_remove(jint(j, "item_id")); } load_downloads(); toast("Removido"); }
    }
    int row = g_dlSel / GCOLS, rowTop = 138 + row * (GCH + GGAP), rowBot = rowTop + GCH;
    if (rowBot - g_dlScroll > WIN_H) g_dlScroll = rowBot - WIN_H + 16;
    if (rowTop - g_dlScroll < 138) g_dlScroll = rowTop - 138;
    if (g_dlScroll < 0) g_dlScroll = 0;
}

// ------------------------------------------------------------- config
static const char *SET_ITEMS[] = { "Buscar atualizacao", "Sair da conta" };
#define NSET 2
static int g_setSel = 0;
static void load_accel_status(void) {
    if (g_accel_status) { cJSON_Delete(g_accel_status); g_accel_status = NULL; }
    g_accel_status = api_get("/api/accel/status");
}
static void draw_settings(void) {
    fill_rect(0, 0, WIN_W, 66, C_BAR);
    text_draw(gRen, "Nplay - Configuracoes", 40, 20, C_ACC, 1);
    text_draw(gRen, "(B) volta", WIN_W - 150, 24, C_MUT, 0);
    char v[96]; snprintf(v, sizeof(v), "Versao do app: %s", APP_VERSION_STR);
    text_draw(gRen, v, 40, 92, C_MUT, 0);
    char u[180]; snprintf(u, sizeof(u), "Conta: %s", g_user[0] ? g_user : "-");
    text_draw(gRen, u, 40, 122, C_MUT, 0);
    text_draw(gRen, "ARMAZENAMENTO", 40, 180, C_ACC2, 0);
    text_draw(gRen, "Salvos na conta", 40, 218, C_TEXT, 0);
    text_draw(gRen, "Continuam no servidor mesmo com o app fechado e nao ocupam espaco no console.", 40, 248, C_MUT, 0);
    if (g_accel_status) {
        char sl[96]; snprintf(sl, sizeof(sl), "%d item(ns) disponiveis na sua conta", jint(g_accel_status, "count"));
        text_draw(gRen, sl, 40, 278, C_GREEN, 0);
    } else text_draw(gRen, "Consultando sua conta...", 40, 278, C_MUT, 0);
    text_draw(gRen, "Downloads no Switch", 40, 322, C_TEXT, 0);
    text_draw(gRen, "Copias offline ficam somente na microSD. Use Y em um item pronto na aba Salvos.", 40, 352, C_MUT, 0);
    char local[96]; snprintf(local, sizeof(local), "%d arquivo(s) offline na microSD", local_dl_count());
    text_draw(gRen, local, 40, 382, C_GREEN, 0);
    for (int i = 0; i < NSET; i++) {
        int y = 448 + i * 56;
        if (i == g_setSel) fill_rect(36, y - 6, 470, 48, C_CARD);
        text_draw(gRen, SET_ITEMS[i], 48, y, (i == g_setSel) ? C_TEXT : C_MUT, 0);
    }
    if (g_status[0]) text_draw(gRen, g_status, 40, 448 + NSET * 56 + 22, C_ACC, 0);
    text_draw(gRen, "A confirma   D-pad move   B volta", 40, WIN_H - 44, C_MUT, 0);
}
static void input_settings(int b) {
    if (b == JOY_B || b == JOY_MINUS) { g_screen = SC_MAIN; return; }
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
        char installed[600] = "";
        char err[256] = "";
        int updated = update_apply(&info, target, installed, sizeof(installed), err, sizeof(err));
        if (updated > 0 && err[0])
            snprintf(g_status, sizeof(g_status), "Atualizacao parcial: %.*s", 138, err);
        else if (updated > 0)
            snprintf(g_status, sizeof(g_status), "v%s instalada em %d copia(s). Feche com + e abra novamente.",
                     info.latest_version, updated);
        else
            snprintf(g_status, sizeof(g_status), "Falha na atualizacao: %s", err[0] ? err : "download");
    } else if (r == UPDATE_CHECK_UP_TO_DATE) {
        snprintf(g_status, sizeof(g_status), "Voce ja esta na versao mais recente (%s).", APP_VERSION_STR);
    } else {
        snprintf(g_status, sizeof(g_status), "%.*s", (int)sizeof(g_status) - 1,
                 info.message[0] ? info.message : "Erro ao verificar atualizacao");
    }
}

// Roteia um botao para a tela atual. Usado pelos eventos E pela navegacao
// continua (segurar D-pad OU empurrar o analogico). g_running/g_dir/g_dir_next
// controlam o loop e a repeticao.
static int g_running = 1;
static int g_dir = -1;            // direcao ativa (D-pad ou analogico), -1 = nenhuma
static Uint32 g_dir_next = 0;

// D-pad fisicamente segurado (-1 = nenhum).
static int dpad_held(SDL_Joystick *j) {
    if (!j) return -1;
    if (SDL_JoystickGetButton(j, JOY_UP)) return JOY_UP;
    if (SDL_JoystickGetButton(j, JOY_DOWN)) return JOY_DOWN;
    if (SDL_JoystickGetButton(j, JOY_DLEFT)) return JOY_DLEFT;
    if (SDL_JoystickGetButton(j, JOY_DRIGHT)) return JOY_DRIGHT;
    return -1;
}
// Direcao do analogico esquerdo (eixos 0/1) com zona morta (-1 = centro).
static int stick_dir(SDL_Joystick *j) {
    if (!j) return -1;
    int ax = SDL_JoystickGetAxis(j, 0), ay = SDL_JoystickGetAxis(j, 1);
    const int DZ = 16000;
    int aax = ax < 0 ? -ax : ax, aay = ay < 0 ? -ay : ay;
    if (aax < DZ && aay < DZ) return -1;
    if (aay >= aax) return ay < 0 ? JOY_UP : JOY_DOWN;
    return ax < 0 ? JOY_DLEFT : JOY_DRIGHT;
}
static void handle_button(int b) {
    if (g_screen == SC_LOGIN) {
        if (b == JOY_A) { if (do_login() == 0) { load_favs(); g_screen = SC_MAIN; enter_tab(0); } }
        else if (b == JOY_PLUS) g_running = 0;
    } else if (g_screen == SC_MAIN) {
        if (b == JOY_L || b == JOY_ZL) enter_tab((g_tab - 1 + NTABS) % NTABS);
        else if (b == JOY_R || b == JOY_ZR) enter_tab((g_tab + 1) % NTABS);
        else if (b == JOY_PLUS) g_running = 0;
        else if (b == JOY_MINUS) { g_setSel = 0; load_accel_status(); g_screen = SC_CONFIG; }
        else if (b == JOY_Y) do_search();
        else if (g_tab == TAB_DOWNLOADS) input_downloads(b);
        else input_landing(b);
    } else if (g_screen == SC_SERIES) {
        input_series(b);
    } else if (g_screen == SC_SEARCH) {
        input_search(b);
    } else if (g_screen == SC_CONFIG) {
        input_settings(b);
    } else if (g_screen == SC_MOVIE) {
        input_movie(b);
    }
}

// ------------------------------------------------------------- main
int main(int argc, char **argv) {
    update_resolve_target_path((argc > 0 && argv) ? argv[0] : NULL, g_self_path, sizeof(g_self_path));
    socketInitializeDefault();
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO);
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_WEBP);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_Window *win = SDL_CreateWindow("Nplay", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    gRen = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawBlendMode(gRen, SDL_BLENDMODE_BLEND);
    SDL_InitSubSystem(SDL_INIT_JOYSTICK);
    g_joy = SDL_JoystickOpen(0);

    text_init(); net_init(); store_init();

    g_cov_mtx = SDL_CreateMutex(); g_q_mtx = SDL_CreateMutex(); g_ready_mtx = SDL_CreateMutex();
    g_q_sem = SDL_CreateSemaphore(0);
    SDL_Thread *wk[3];
    for (int i = 0; i < 3; i++) wk[i] = SDL_CreateThread(cover_worker, "cov", NULL);

    store_load_token(g_token, sizeof(g_token));
    store_load_user(g_user, sizeof(g_user));
    if (g_token[0]) { load_favs(); g_screen = SC_MAIN; enter_tab(0); }

    while (appletMainLoop() && g_running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { g_running = 0; break; }
            if (e.type != SDL_JOYBUTTONDOWN) continue;
            int b = e.jbutton.button;
            // direcoes (D-pad) sao tratadas no bloco de navegacao abaixo (junto
            // com o analogico); aqui so os demais botoes.
            if (b == JOY_UP || b == JOY_DOWN || b == JOY_DLEFT || b == JOY_DRIGHT) continue;
            handle_button(b);
        }
        // Navegacao continua: D-pad segurado OU analogico empurrado. 1a ativacao
        // na hora, depois repete (segurar rola rapido em listas longas).
        {
            int dir = dpad_held(g_joy);
            if (dir < 0) dir = stick_dir(g_joy);
            Uint32 now = SDL_GetTicks();
            if (dir < 0) g_dir = -1;
            else if (dir != g_dir) { handle_button(dir); g_dir = dir; g_dir_next = now + 380; }
            else if (now >= g_dir_next) { handle_button(dir); g_dir_next = now + 55; }
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
        update_download_awake();

        SDL_SetRenderDrawColor(gRen, C_BG.r, C_BG.g, C_BG.b, 255);
        SDL_RenderClear(gRen);
        if (g_screen == SC_LOGIN) draw_login();
        else if (g_screen == SC_CONFIG) draw_settings();
        else if (g_screen == SC_MOVIE) draw_movie();
        else if (g_screen == SC_SERIES) { if (g_dlmenu) draw_dlmenu(); else draw_series(); }
        else if (g_screen == SC_SEARCH) draw_search();
        else { if (g_tab == TAB_DOWNLOADS) draw_downloads(); else draw_landing(); }

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

    if (g_download_awake) { appletSetMediaPlaybackState(false); g_download_awake = 0; }
    if (g_land) cJSON_Delete(g_land);
    if (g_search) cJSON_Delete(g_search);
    if (g_dl) cJSON_Delete(g_dl);
    if (g_ser) cJSON_Delete(g_ser);
    for (int i = 0; i < g_covN; i++) {
        if (g_cov[i].tex) SDL_DestroyTexture(g_cov[i].tex);
        if (g_cov[i].surf) SDL_FreeSurface(g_cov[i].surf);
    }
    SDL_DestroySemaphore(g_q_sem);
    SDL_DestroyMutex(g_ready_mtx);
    SDL_DestroyMutex(g_q_mtx);
    SDL_DestroyMutex(g_cov_mtx);
    text_exit(); net_exit();
    if (g_joy) SDL_JoystickClose(g_joy);
    SDL_DestroyRenderer(gRen); SDL_DestroyWindow(win);
    IMG_Quit(); SDL_Quit(); socketExit();
    return 0;
}
