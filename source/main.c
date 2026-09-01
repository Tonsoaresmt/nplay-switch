// Nplay Switch - app homebrew do Nplay para Nintendo Switch.
// Abas com RAILS por secao (como o app de PC): Inicio, Filmes, Series, Animes,
// Doramas (todas com hero + Lancamentos + Minha lista + prateleiras por genero),
// Historico, biblioteca de itens preparados e Config. Busca global (Y),
// favoritar (X -> Minha lista). Capas em THREADS de fundo (navegacao fluida).
// Player de video via ffmpeg + libcurl (TLS), tocando https direto.
#include <switch.h>
#include <SDL.h>
#include <SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include "net.h"
#include "store.h"
#include "text.h"
#include "cJSON.h"
#include "update.h"
#include "player.h"
#include "api.h"
#include "diag.h"

#define WIN_W 1280
#define WIN_H 720



const char *BASE = "https://nplay.tonserverlocal.uk";

SDL_Renderer *gRen = NULL;
static SDL_Joystick *g_joy = NULL;
char g_token[640] = {0};
static char g_status[160] = {0};
Uint32 g_toast_until = 0;
char g_toast[160] = {0};
static char g_self_path[600] = {0};
static char g_user[128] = {0};
static int g_do_update = 0;
static Uint32 g_restart_at = 0;
static int g_running; // definido/inicializado na secao de roteamento de input


#include "ui.h"
#include "screen_movie.h"


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
static int g_cov_hash[COVER_HASH_SIZE];   // indice+1; zero=vazio; -1=tumulo
static int g_cov_texN = 0;
static SDL_mutex *g_cov_mtx;
static int g_q[MAX_COV]; static int g_qh = 0, g_qt = 0, g_qn = 0;
static SDL_mutex *g_q_mtx; static SDL_sem *g_q_sem;
static int g_ready[MAX_COV]; static int g_rh = 0, g_rt = 0, g_rn = 0;
static SDL_mutex *g_ready_mtx;
static volatile int g_run = 1;

static unsigned cover_hash(const char *s) {
    unsigned h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

// Chamado com g_cov_mtx travado. O hash evita comparar ate 3000 URLs por card/frame.
static int cover_find_locked(const char *url, int create) {
    unsigned slot = cover_hash(url) & (COVER_HASH_SIZE - 1);
    int insert_slot = -1;
    for (int probe = 0; probe < COVER_HASH_SIZE; probe++) {
        int entry = g_cov_hash[slot];
        if (entry == 0) { if (insert_slot < 0) insert_slot = (int)slot; break; }
        if (entry < 0) { if (insert_slot < 0) insert_slot = (int)slot; }
        else {
            int idx = entry - 1;
            if (!strcmp(g_cov[idx].url, url)) return idx;
        }
        slot = (slot + 1) & (COVER_HASH_SIZE - 1);
    }
    if (!create || insert_slot < 0) return -1;

    int idx;
    if (g_covN < MAX_COV) idx = g_covN++;
    else {
        // Recicla apenas uma entrada ociosa. Workers nunca perdem o indice que
        // estao usando e o teto de memoria continua fixo mesmo apos navegar por
        // catalogos com mais de MAX_COV capas diferentes.
        idx = -1;
        for (int i = 0; i < g_covN; i++) {
            if (g_cov[i].state == 3 && !g_cov[i].surf &&
                (idx < 0 || g_cov[i].last_used < g_cov[idx].last_used)) idx = i;
        }
        if (idx < 0) return -1;
        unsigned old = cover_hash(g_cov[idx].url) & (COVER_HASH_SIZE - 1);
        for (int probe = 0; probe < COVER_HASH_SIZE; probe++) {
            if (g_cov_hash[old] == idx + 1) { g_cov_hash[old] = -1; break; }
            if (g_cov_hash[old] == 0) break;
            old = (old + 1) & (COVER_HASH_SIZE - 1);
        }
        if (g_cov[idx].tex) { SDL_DestroyTexture(g_cov[idx].tex); g_cov_texN--; }
        memset(&g_cov[idx], 0, sizeof(g_cov[idx]));
    }
    snprintf(g_cov[idx].url, sizeof(g_cov[idx].url), "%s", url);
    g_cov_hash[insert_slot] = idx + 1;
    return idx;
}

SDL_Texture *cover_get(const char *url) {   // chamado no main (render)
    if (!url || !url[0]) return NULL;
    SDL_LockMutex(g_cov_mtx);
    int f = cover_find_locked(url, 1);
    SDL_Texture *tex = (f >= 0) ? g_cov[f].tex : NULL;
    if (tex) g_cov[f].last_used = SDL_GetTicks();
    if (f >= 0 && g_cov[f].state == 0) {
        int queued = 0;
        SDL_LockMutex(g_q_mtx);
        if (g_qn < MAX_COV) { g_q[g_qt] = f; g_qt = (g_qt + 1) % MAX_COV; g_qn++; queued = 1; }
        SDL_UnlockMutex(g_q_mtx);
        if (queued) { g_cov[f].state = 1; SDL_SemPost(g_q_sem); }
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
        if (g_qn > 0) { idx = g_q[g_qh]; g_qh = (g_qh + 1) % MAX_COV; g_qn--; }
        SDL_UnlockMutex(g_q_mtx);
        if (idx < 0) continue;
        char url[900];
        SDL_LockMutex(g_cov_mtx);
        if (strncmp(g_cov[idx].url, "http", 4) == 0) snprintf(url, sizeof(url), "%s", g_cov[idx].url);
        else snprintf(url, sizeof(url), "%s%s", BASE, g_cov[idx].url);
        SDL_UnlockMutex(g_cov_mtx);
        struct membuf out = { 0 };
        const char *err = NULL;
        // Capa morta nao pode prender um dos tres workers por 45 segundos.
        long code = net_request_timeout(url, "GET", NULL, NULL, &out, &err, 6L, 15L);
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
            int queued = 0;
            SDL_LockMutex(g_ready_mtx);
            if (g_rn < MAX_COV) { g_ready[g_rt] = idx; g_rt = (g_rt + 1) % MAX_COV; g_rn++; queued = 1; }
            SDL_UnlockMutex(g_ready_mtx);
            if (!queued) {
                SDL_LockMutex(g_cov_mtx);
                if (g_cov[idx].surf == s) { g_cov[idx].surf = NULL; g_cov[idx].state = 0; }
                SDL_UnlockMutex(g_cov_mtx);
                SDL_FreeSurface(s);
            }
        }
    }
    return 0;
}
static void cover_pump(void) {   // main: converte surfaces prontas em texturas
    // Criar textura e eventualmente expulsar uma LRU custa CPU/GPU. Limitar a
    // duas por frame evita os picos visiveis quando varias capas chegam juntas.
    for (int done = 0; done < 2; done++) {
        int idx = -1; SDL_Surface *s = NULL;
        SDL_LockMutex(g_ready_mtx);
        if (g_rn > 0) { idx = g_ready[g_rh]; g_rh = (g_rh + 1) % MAX_COV; g_rn--; }
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
    if (selected) {
        fill_rect(x - 8, y - 8, cw + 16, coverH + 16, (SDL_Color){ 4, 6, 11, 255 });
        border_rect(x - 5, y - 5, cw + 10, coverH + 10, 2, C_ACC);
    }
    SDL_Rect cr = { x, y, cw, coverH };
    if (tex) ui_cover(tex, &cr);
    else {
        fill_rect(x, y, cw, coverH, C_CARD);
        char ini[2] = { title[0] ? title[0] : '?', 0 };
        text_center_at(ini, x, cw, y + coverH / 2 - 18, C_MUT, 1);
    }
    const char *kind = jstr(item, "kind");
    int ready = cJSON_IsTrue(cJSON_GetObjectItem(item, "r2_ready")) || jint(item, "r2_ready") != 0;
    int cam = cJSON_IsTrue(cJSON_GetObjectItem(item, "is_cam")) || jint(item, "is_cam") != 0;
    const char *year = jstr(item, "year");
    if (kind && !strcmp(kind, "live")) ui_card_badge("AO VIVO", x + 7, y + 7, C_ROSE);
    else if (cam) ui_card_badge("CAM", x + 7, y + 7, C_ROSE);
    else if (year && year[0]) ui_card_badge(year, x + 7, y + 7, C_MUT);
    if (ready && !(kind && !strcmp(kind, "live"))) {
        int tw = 0, th = 0; text_cached(gRen, "Pronto", C_TEXT, 2, &tw, &th);
        int bw = tw + 14; if (bw < 28) bw = 28;
        ui_card_badge("Pronto", x + cw - bw - 7, y + coverH - 29, C_GREEN);
    }
    if (fav) {
        if (selected) {
            int tw = 0, th = 0; text_cached(gRen, "Na lista", C_TEXT, 2, &tw, &th);
            int bw = tw + 14; if (bw < 28) bw = 28;
            ui_card_badge("Na lista", x + cw - bw - 7, y + 7, C_ROSE);
        } else fill_rect(x + cw - 5, y + 8, 3, 18, C_ROSE);
    }
    // O recorte e feito pelo renderer; nao corte por bytes, pois isso quebrava
    // acentos/UTF-8 e abreviava titulos antes de ocupar a largura disponivel.
    text_clip(title, x, y + coverH + 8, selected ? C_TEXT : C_MUT, 0, cw);
    if (selected) fill_rect(x, y + coverH + 37, cw, 2, C_ACC2);
}

// ============================================================= estado / telas
Screen g_screen = SC_LOGIN;
static Screen g_detail_return = SC_MAIN;

// Config saiu da barra de abas -> abre pelo botao (-). Assim L a partir do
// Inicio ja cai em Baixados (ultima aba).
#define TAB_HOME 0
#define TAB_DOWNLOADS 5
#define NTABS 6
static const char *TAB_NAME[] = { "Inicio", "Filmes", "Series", "Animes", "Doramas", "Historico" };
static int g_tab = 0;

// --- landing (rails) das abas 0..4 ---
static cJSON *g_land = NULL;          // root JSON da aba atual (home / tab-home / anime-home)
static cJSON *g_land_cache[5] = {0};  // troca de aba instantanea depois do 1o carregamento
static cJSON *g_land_pending = NULL;
static SDL_Thread *g_land_thread = NULL;
static SDL_atomic_t g_land_done;
static int g_land_fetch_tab = -1, g_land_queued_tab = -1;
static unsigned g_land_attempted_mask = 0;
static char g_land_error[192] = "";
static cJSON *g_heroesArr = NULL;     // array (dentro de g_land) usado no destaque
static int g_heroSeriesDefault = 1;   // hero abre como serie? (Filmes = 0)
typedef struct { char label[48]; cJSON *arr; int is_series; } Rail;
static Rail g_rails[48]; static int g_railsN = 0;
static int g_railSel = 0, g_railItem = 0, g_homeScroll = 0;
static int g_heroIdx = 0; static Uint32 g_hero_next = 0;
static int hero_count(void) { int n = arr_len(g_heroesArr); return n > 8 ? 8 : n; }

// --- busca ---
static cJSON *g_search = NULL;
static char g_srchQuery[128] = {0};
static int g_srchSel = 0, g_srchScroll = 0, g_srchFilter = 0;

// --- downloads (acelerador) ---
static cJSON *g_dl = NULL;
static int g_dlSel = 0, g_dlScroll = 0; static Uint32 g_dl_next = 0;
static Uint32 g_dl_last_ok = 0;
static int g_download_awake = 0;
static SDL_Thread *g_dl_thread = NULL;
static SDL_atomic_t g_dl_done;
static cJSON *g_dl_pending = NULL;
// Historico de reproducao e carregado separadamente dos itens preparados. Ele
// muda ao sair do player, nao a cada polling de status da biblioteca.
static cJSON *g_history = NULL, *g_history_pending = NULL, *g_watchlater_pending = NULL;
static SDL_Thread *g_history_thread = NULL, *g_watchlater_thread = NULL;
static SDL_atomic_t g_history_done, g_watchlater_done;
static int g_history_refresh_requested = 0;
static int g_history_sel = 0, g_history_zone = 0; // 0=continuar, 1=biblioteca
static int g_history_menu = 0, g_history_menu_sel = 0;
// vista do Historico: 0=inicio, 1=episodios preparados, 2=biblioteca, 3=lista pessoal
static int g_dlView = 0, g_dlGroup = 0, g_dlDetSel = 0, g_dlDetScroll = 0;
static int g_list_sel = 0, g_open_list = 0, g_list_item_sel = 0;
// agrupamento dos jobs por obra (series_id) ou filme (item_id negativo)
#define MAX_DLG 300
typedef struct { int key; int job[128]; int nJobs; int isMovie; } DlGroup;
static DlGroup g_dlg[MAX_DLG]; static int g_dlgN = 0;
// episodios ja assistidos (completed) da obra aberta no detalhe de Baixados
static int g_dlDone[256]; static int g_dlDoneN = 0;
// status de armazenamento (aba config)
static cJSON *g_accel_status = NULL;
static cJSON *g_account_status = NULL;
static cJSON *g_settings_accel_pending = NULL, *g_account_pending = NULL;
static SDL_Thread *g_settings_thread = NULL;
static SDL_atomic_t g_account_ready, g_settings_done;
static int g_pref_hide_adult = 1, g_pref_autoplay = 1, g_pref_reduce_motion = 0;
static int g_pref_audio = 0; // 0=dublado, 1=legendado, 2=tanto faz
static int g_prefs_open = 0, g_prefs_sel = 0;
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
static void load_history(void);
static cJSON *history_items(void);
static int accel_start(int itemId);
static int accel_wait_and_play(int itemId, const char *title);
static void do_search(void);
static void open_series(int id);
int resolve_and_play(int itemId, const char *title);
static int play_with_progress(int itemId, const char *title, const char *url, int is_hls);

// Detalhes sao modais sobre a tela que os abriu. Pesquisa, landing e listas
// permanecem em memoria; voltar apenas restaura a tela anterior e sua selecao.
void detail_capture_origin(void) {
    if (g_screen == SC_SEARCH || g_screen == SC_MAIN) g_detail_return = g_screen;
    else g_detail_return = SC_MAIN;
}
void detail_return_to_origin(void) {
    g_screen = g_detail_return;
    g_detail_return = SC_MAIN;
}

// ------------------------------------------------------------- favoritos
static int idx_of(int *arr, int n, int v) { for (int i = 0; i < n; i++) if (arr[i] == v) return i; return -1; }
static int catalog_item_is_series(cJSON *item, int fallback) {
    const char *kind = item ? jstr(item, "kind") : NULL;
    if (kind && (!strcmp(kind, "movie") || !strcmp(kind, "live"))) return 0;
    if (kind && (!strcmp(kind, "series") || !strcmp(kind, "episode"))) return 1;
    return fallback;
}
static int catalog_favorite_id(cJSON *item, int is_series) {
    int sid = is_series ? jint(item, "series_id") : 0;
    return sid > 0 ? sid : jint(item, "id");
}

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

static void hero_pool_add(cJSON *pool, cJSON *items) {
    if (!pool || !cJSON_IsArray(items)) return;
    cJSON *item;
    cJSON_ArrayForEach(item, items) {
        if (arr_len(pool) >= 8) break;
        int id = jint(item, "id"), duplicate = 0;
        cJSON *existing;
        cJSON_ArrayForEach(existing, pool) {
            if (id > 0 && jint(existing, "id") == id) { duplicate = 1; break; }
        }
        if (!duplicate) cJSON_AddItemReferenceToArray(pool, item);
    }
}
// Carrega a landing da aba (0..4). Cada aba vira hero + rails, como no app de PC.
static void landing_apply(int tab, cJSON *land) {
    g_land = land;
    g_railsN = 0; g_railItem = 0; g_homeScroll = 0;
    g_heroIdx = 0; g_hero_next = SDL_GetTicks() + 6000; g_heroesArr = NULL;
    g_heroSeriesDefault = (tab == 1) ? 0 : 1;

    if (!g_land) { snprintf(g_status, sizeof(g_status), "Falha ao carregar %s", TAB_NAME[tab]); g_railSel = 0; return; }
    g_status[0] = '\0';

    if (tab == 0) {
        g_heroesArr = cJSON_GetObjectItem(g_land, "heroes");
        add_rail("Jogos do dia",         cJSON_GetObjectItem(g_land, "jogos"), 0);
        add_rail("Continuar assistindo", cJSON_GetObjectItem(g_land, "continue"), 1);
        add_rail("Filmes em alta",       cJSON_GetObjectItem(g_land, "trendingMovies"), 0);
        add_rail("Series em alta",       cJSON_GetObjectItem(g_land, "trendingSeries"), 1);
        add_rail("Filmes recentes",     cJSON_GetObjectItem(g_land, "recentMovies"), 0);
        add_rail("Series atualizadas",  cJSON_GetObjectItem(g_land, "recentSeries"), 1);
        add_rail("Animes recentes",     cJSON_GetObjectItem(g_land, "recentAnimes"), 1);
        cJSON *sh = cJSON_GetObjectItem(g_land, "movieShelves"), *e;
        cJSON_ArrayForEach(e, sh) add_rail(jstr(e, "title"), cJSON_GetObjectItem(e, "items"), 0);
        sh = cJSON_GetObjectItem(g_land, "liveShelves");
        cJSON_ArrayForEach(e, sh) add_rail(jstr(e, "title"), cJSON_GetObjectItem(e, "items"), 0);
    } else if (tab == 3) {   // anime-home
        cJSON_DeleteItemFromObject(g_land, "_switchHeroes");
        g_heroesArr = cJSON_CreateArray();
        hero_pool_add(g_heroesArr, cJSON_GetObjectItem(g_land, "updatedToday"));
        hero_pool_add(g_heroesArr, cJSON_GetObjectItem(g_land, "popular"));
        hero_pool_add(g_heroesArr, cJSON_GetObjectItem(g_land, "updatedWeek"));
        hero_pool_add(g_heroesArr, cJSON_GetObjectItem(g_land, "updated"));
        cJSON_AddItemToObject(g_land, "_switchHeroes", g_heroesArr);
        add_rail("Continuar assistindo", cJSON_GetObjectItem(g_land, "continueWatching"), 1);
        add_rail("Minha lista",          cJSON_GetObjectItem(g_land, "favoritos"), 1);
        cJSON *today = cJSON_GetObjectItem(g_land, "updatedToday");
        cJSON *week = cJSON_GetObjectItem(g_land, "updatedWeek");
        add_rail("Atualizados hoje", today, 1);
        add_rail("Atualizados esta semana", week, 1);
        if (arr_len(today) == 0 && arr_len(week) == 0)
            add_rail("Atualizacoes recentes", cJSON_GetObjectItem(g_land, "updated"), 1);
        add_rail("Populares",          cJSON_GetObjectItem(g_land, "popular"), 1);
        add_rail("Dublados",          cJSON_GetObjectItem(g_land, "dublados"), 1);
        add_rail("Filmes de anime",   cJSON_GetObjectItem(g_land, "filmes"), 1);
        cJSON *gs = cJSON_GetObjectItem(g_land, "genreShelves"), *e;
        cJSON_ArrayForEach(e, gs) add_rail(jstr(e, "genre"), cJSON_GetObjectItem(e, "items"), 1);
    } else {                 // tab-home (movie/series/dorama)
        int is_series = (tab != 1);
        cJSON *hero = cJSON_GetObjectItem(g_land, "hero");
        cJSON_DeleteItemFromObject(g_land, "_switchHeroes");
        g_heroesArr = cJSON_CreateArray();
        if (hero) cJSON_AddItemReferenceToArray(g_heroesArr, hero);
        if (arr_len(g_heroesArr) == 0) {
            cJSON_Delete(g_heroesArr);
            g_heroesArr = cJSON_GetObjectItem(g_land, "recent");
        } else cJSON_AddItemToObject(g_land, "_switchHeroes", g_heroesArr);
        add_rail("Pronto pra tocar", cJSON_GetObjectItem(g_land, "prontos"), is_series);
        add_rail("Minha lista", cJSON_GetObjectItem(g_land, "favoritos"), is_series);
        add_rail("Lancamentos", cJSON_GetObjectItem(g_land, "recent"), is_series);
        add_rail("Em alta", cJSON_GetObjectItem(g_land, "emAlta"), is_series);
        cJSON *sh = cJSON_GetObjectItem(g_land, "shelves"), *e;
        cJSON_ArrayForEach(e, sh) add_rail(jstr(e, "title"), cJSON_GetObjectItem(e, "items"), is_series);
    }
    g_railSel = (arr_len(g_heroesArr) > 0) ? -1 : 0;
}

static const char *landing_path(int tab) {
    switch (tab) {
        case 1: return "/api/catalog/tab-home?tab=movie";
        case 2: return "/api/catalog/tab-home?tab=series";
        case 3: return "/api/catalog/anime-home";
        case 4: return "/api/catalog/tab-home?tab=dorama";
        default: return "/api/catalog/home";
    }
}

static int landing_fetch_thread(void *unused) {
    (void)unused;
    int tab = g_land_fetch_tab;
    // Series pode gerar um payload grande no Pi. A espera maior nao bloqueia a
    // interface porque esta funcao roda exclusivamente na thread de catalogo.
    g_land_error[0] = '\0';
    g_land_pending = api_get_timeout(landing_path(tab), 6L, 30L);
    if (!g_land_pending) snprintf(g_land_error, sizeof(g_land_error), "%s", api_last_error());
    SDL_AtomicSet(&g_land_done, 1);
    return 0;
}

static void landing_start(int tab) {
    g_land_attempted_mask |= 1u << tab;
    g_land_fetch_tab = tab;
    g_land_pending = NULL;
    SDL_AtomicSet(&g_land_done, 0);
    g_land_thread = SDL_CreateThread(landing_fetch_thread, "catalog-fetch", NULL);
    if (!g_land_thread) {
        g_land_fetch_tab = -1;
        snprintf(g_status, sizeof(g_status), "Nao consegui iniciar a sincronizacao de %s", TAB_NAME[tab]);
    }
}

static void load_landing(int tab) {
    if (tab < 0 || tab > 4) return;
    if (g_land_cache[tab]) {
        landing_apply(tab, g_land_cache[tab]);
        return;
    }
    g_land = NULL;
    g_heroesArr = NULL;
    g_railsN = 0;
    g_railSel = 0;
    snprintf(g_status, sizeof(g_status), "Carregando %s...", TAB_NAME[tab]);
    if (g_land_thread) g_land_queued_tab = tab;
    else landing_start(tab);
}

static void landing_invalidate(int tab) {
    if (tab < 0 || tab > 4) return;
    if (g_land == g_land_cache[tab]) g_land = NULL;
    if (g_land_cache[tab]) { cJSON_Delete(g_land_cache[tab]); g_land_cache[tab] = NULL; }
    load_landing(tab);
}

static void pump_landing(void) {
    if (!g_land_thread || !SDL_AtomicGet(&g_land_done)) return;
    SDL_WaitThread(g_land_thread, NULL);
    g_land_thread = NULL;
    int tab = g_land_fetch_tab;
    g_land_fetch_tab = -1;
    cJSON *received = g_land_pending;
    g_land_pending = NULL;
    if (received && tab >= 0 && tab <= 4) {
        if (g_land_cache[tab]) cJSON_Delete(g_land_cache[tab]);
        g_land_cache[tab] = received;
        if (g_screen == SC_MAIN && g_tab == tab) landing_apply(tab, received);
    } else if (g_screen == SC_MAIN && g_tab == tab) {
        const char *detail = g_land_error;
        if (detail && detail[0]) snprintf(g_status, sizeof(g_status), "Falha em %s: %.100s", TAB_NAME[tab], detail);
        else snprintf(g_status, sizeof(g_status), "Falha ao sincronizar %s", TAB_NAME[tab]);
    }
    int queued = g_land_queued_tab;
    g_land_queued_tab = -1;
    if (queued >= 0 && queued <= 4 && !g_land_cache[queued]) {
        landing_start(queued);
        return;
    }
    // Depois da Home, aquece catalogos em serie, nunca em paralelo. Assim a
    // primeira entrada em Filmes/Series tende a ser instantanea sem repetir a
    // disputa HTTPS que tornou a 0.7.0 instavel. Uma falha automatica nao entra
    // em loop; abrir a aba continua permitindo nova tentativa manual.
    static const int prefetch_order[] = { 1, 2, 3, 4 };
    for (size_t i = 0; i < sizeof(prefetch_order) / sizeof(prefetch_order[0]); i++) {
        int candidate = prefetch_order[i];
        if (!g_land_cache[candidate] && !(g_land_attempted_mask & (1u << candidate))) {
            landing_start(candidate);
            break;
        }
    }
}

static void on_player_progress(int item_id, int pos, int dur, void *u) {
    (void)u;
    api_playback_progress(item_id, pos, dur);
}

static int on_player_heartbeat(int session_id, void *u) {
    (void)u;
    return api_playback_heartbeat(session_id);
}

static int on_player_renew(const PlaybackSource *current, PlaybackSource *out, void *u) {
    (void)u;
    if (!current) return -1;
    if (current->delivery == DELIVERY_R2 && current->session_id > 0)
        return api_refresh_playback(current, out);
    return api_reresolve_playback(current->item_id,
                                  current->quality[0] ? current->quality : NULL, out);
}

static int on_player_fallback(const PlaybackSource *current, PlaybackSource *out, void *u) {
    (void)u;
    return api_fail_playback(current, out);
}

static void format_short_time(int seconds, char *out, size_t cap) {
    if (seconds < 0) seconds = 0;
    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    int secs = seconds % 60;
    if (hours > 0) snprintf(out, cap, "%d:%02d:%02d", hours, minutes, secs);
    else snprintf(out, cap, "%d:%02d", minutes, secs);
}

// 1 continua, 0 recomeca, -1 cancela. Continuar e o padrao depois de 6 s,
// como no site, mas nenhuma escolha e aplicada antes de o modal aparecer.
static int prompt_resume_playback(const char *title, int position_seconds) {
    Uint32 deadline = SDL_GetTicks() + 6000;
    while (g_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) { g_running = 0; return -1; }
            if (event.type != SDL_JOYBUTTONDOWN) continue;
            if (event.jbutton.button == JOY_A) return 1;
            if (event.jbutton.button == JOY_X) return 0;
            if (event.jbutton.button == JOY_B || event.jbutton.button == JOY_MINUS) return -1;
        }
        Uint32 now = SDL_GetTicks();
        if ((Sint32)(deadline - now) <= 0) return 1;
        int remaining = (int)((deadline - now + 999) / 1000);
        char at[32], countdown[80]; format_short_time(position_seconds, at, sizeof(at));
        snprintf(countdown, sizeof(countdown), "Continuando automaticamente em %d...", remaining);
        SDL_SetRenderDrawColor(gRen, C_BG.r, C_BG.g, C_BG.b, 255); SDL_RenderClear(gRen);
        ui_header("NPLAY PLAYER", "Retomar reproducao", "B Cancelar");
        ui_panel(238, 178, WIN_W - 476, 350, C_ACC);
        text_draw(gRen, "CONTINUAR ASSISTINDO", 282, 218, C_ACC, 0);
        text_clip(title && title[0] ? title : "Sua obra", 282, 258, C_TEXT, 1, WIN_W - 564);
        char point[96]; snprintf(point, sizeof(point), "Voce parou em %s", at);
        text_draw(gRen, point, 282, 310, C_MUT, 0);
        fill_rect(282, 370, 300, 58, C_ACC);
        text_center_at("A  Continuar", 282, 300, 385, C_TEXT, 0);
        fill_rect(606, 370, 300, 58, C_CARD);
        text_center_at("X  Comecar do inicio", 606, 300, 385, C_TEXT, 0);
        text_draw(gRen, countdown, 282, 462, C_MUT, 0);
        fill_rect(282, 494, (WIN_W - 564) * (6 - remaining) / 6, 4, C_ACC2);
        SDL_RenderPresent(gRen);
        SDL_Delay(16);
    }
    return -1;
}

// Toca uma URL retomando de onde parou e salvando o progresso ("continuar
// assistindo"). Usado tanto no link direto quanto no arquivo do acelerador.
// Retorna 1 se o video terminou naturalmente (p/ auto-play do proximo).
static int play_with_progress(int itemId, const char *title, const char *url, int is_hls) {
    double start = 0;
    int completed = 0;
    char p[96]; snprintf(p, sizeof(p), "/api/sync/progress/%d", itemId);
    cJSON *pr = api_get(p);
    if (pr) {
        cJSON *prog = cJSON_GetObjectItem(pr, "progress");
        cJSON *ps = prog ? cJSON_GetObjectItem(prog, "position_seconds") : NULL;
        if (ps && cJSON_IsNumber(ps)) start = ps->valuedouble;
        completed = prog ? jint(prog, "completed") : 0;
        cJSON_Delete(pr);
    }
    if (!completed && start > 10) {
        int choice = prompt_resume_playback(title, (int)start);
        if (choice < 0) return 0;
        if (choice == 0) start = 0;
    }
    
    PlayerRequest req = {0};
    req.item_id = itemId;
    req.session_id = 0; // Local ou arquivo direto
    req.title = title;
    req.url = url;
    req.start_sec = start;
    req.progress_cb = on_player_progress;
    req.heartbeat_cb = NULL;
    // Sem renew_cb pois nao e uma stream resolvida via API.
    req.userdata = NULL;

    appletSetMediaPlaybackState(true);
    PlayerResult res = {0};
    int run_rc = player_run(gRen, g_joy, &req, &res);
    appletSetMediaPlaybackState(false);
    g_download_awake = 0;

    if (g_tab == TAB_DOWNLOADS) load_history();
    
    if (run_rc < 0) {
        char m[160]; const char *detail = player_last_error();
        if (detail && detail[0]) snprintf(m, sizeof(m), "%s", detail);
        else snprintf(m, sizeof(m), "Reproducao interrompida (erro %d)", run_rc);
        toast(m); 
        return 0;
    }
    return (res.reason == EXIT_REASON_NATURAL) ? 1 : 0;
}

// Resolve a fonte e reproduz usando a maquina de estados e PlayerRequest.
int resolve_and_play(int itemId, const char *title) {
    SDL_SetRenderDrawColor(gRen, C_BG.r, C_BG.g, C_BG.b, 255); SDL_RenderClear(gRen);
    ui_header("NPLAY", "Abrindo video", "");
    ui_panel(260, 210, WIN_W - 520, 250, C_ACC2);
    text_draw(gRen, "PREPARANDO", 300, 244, C_ACC2, 0);
    text_center_at(title && title[0] ? title : "Video", 300, WIN_W - 600, 286, C_TEXT, 1);
    text_center_at("Organizando tudo para comecar...", 300, WIN_W - 600, 354, C_MUT, 0);
    for (int i = 0; i < 5; i++) fill_rect(WIN_W / 2 - 58 + i * 28, 408, 14, 6, i == 0 ? C_ACC : C_CARD);
    SDL_RenderPresent(gRen);
    
    PlaybackSource src = {0};
    if (api_resolve_playback(itemId, NULL, &src) < 0) {
        const char *detail = api_last_error();
        toast(detail && detail[0] ? detail : "Falha de rede ou de acesso ao abrir o video");
        return 0;
    }
    
    int rc = 0;
    if (src.container[0] && !strcmp(src.container, "torrent")) {
        // Para torrent, o fluxo e separado (usa accel_wait_and_play que tem I/O diferente)
        rc = accel_wait_and_play(itemId, title);
    } else if (src.container[0] && !strcmp(src.container, "embed")) {
        toast("Este conteudo ainda nao esta disponivel neste dispositivo");
    } else if (src.play_url[0]) {
        double start = 0;
        int completed = 0;
        char p[96]; snprintf(p, sizeof(p), "/api/sync/progress/%d", itemId);
        cJSON *pr = api_get(p);
        if (pr) {
            cJSON *prog = cJSON_GetObjectItem(pr, "progress");
            cJSON *ps = prog ? cJSON_GetObjectItem(prog, "position_seconds") : NULL;
            if (ps && cJSON_IsNumber(ps)) start = ps->valuedouble;
            completed = prog ? jint(prog, "completed") : 0;
            cJSON_Delete(pr);
        }
        if (!completed && start > 10) {
            int choice = prompt_resume_playback(title, (int)start);
            if (choice < 0) { if (src.session_id > 0) api_stop_playback(itemId); return 0; }
            if (choice == 0) start = 0;
        }

        PlayerRequest req = {0};
        req.playback = src;
        req.item_id = itemId;
        req.session_id = src.session_id;
        req.source_id = src.source_id;
        req.delivery = src.delivery;
        req.title = title;
        req.section = src.section;
        req.container = src.container;
        req.url = src.play_url;
        req.season = src.season;
        req.episode = src.episode;
        req.start_sec = start;
        req.progress_cb = on_player_progress;
        req.renew_cb = on_player_renew;
        req.fallback_cb = on_player_fallback;
        req.heartbeat_cb = on_player_heartbeat;
        req.userdata = NULL;

        appletSetMediaPlaybackState(true);
        PlayerResult res = {0};
        int run_rc = player_run(gRen, g_joy, &req, &res);
        appletSetMediaPlaybackState(false);
        g_download_awake = 0;

        if (g_tab == TAB_DOWNLOADS) load_history();
        
        if (run_rc < 0) {
            char m[160]; const char *detail = player_last_error();
            if (detail && detail[0]) snprintf(m, sizeof(m), "%s", detail);
            else snprintf(m, sizeof(m), "Reproducao interrompida (erro %d)", run_rc);
            toast(m); 
            rc = 0;
        } else {
            rc = (res.reason == EXIT_REASON_NATURAL) ? 1 : 0;
        }
    } else {
        toast("Este titulo esta indisponivel no momento");
    }
    
    // Stop the session if we had one
    if (src.session_id > 0) api_stop_playback(itemId);
    
    return rc;
}
static int episode_completed(cJSON *episode) {
    return jint(episode, "completed") != 0 || cJSON_IsTrue(cJSON_GetObjectItem(episode, "completed"));
}

static int episode_started(cJSON *episode) {
    return !episode_completed(episode) && jint(episode, "position_seconds") > 10;
}

static void select_series_resume_target(cJSON *detail) {
    cJSON *series = detail ? cJSON_GetObjectItem(detail, "series") : NULL;
    cJSON *seasons = detail ? cJSON_GetObjectItem(detail, "seasons") : NULL;
    int grouped = arr_len(cJSON_GetObjectItem(series, "season_group")) > 1;
    int best_season = 0, best_local = 0, best_flat = 0;
    int fallback_season = 0, fallback_local = 0, fallback_flat = 0;
    int have_started = 0, have_fallback = 0, season_index = 0, flat_index = 0;
    cJSON *season;
    cJSON_ArrayForEach(season, seasons) {
        int local = 0;
        cJSON *episode;
        cJSON_ArrayForEach(episode, season) {
            if (!have_fallback && !episode_completed(episode)) {
                fallback_season = season_index; fallback_local = local;
                fallback_flat = flat_index; have_fallback = 1;
            }
            if (!have_started && episode_started(episode)) {
                best_season = season_index; best_local = local;
                best_flat = flat_index; have_started = 1;
            }
            local++; flat_index++;
        }
        season_index++;
    }
    if (!have_started && have_fallback) {
        best_season = fallback_season; best_local = fallback_local; best_flat = fallback_flat;
    }
    g_seasonIdx = best_season;
    g_epSel = grouped ? best_flat : best_local;
    g_epScroll = 0;
}

static void open_series(int id) {
    if (g_ser) { cJSON_Delete(g_ser); g_ser = NULL; }
    char p[96]; snprintf(p, sizeof(p), "/api/catalog/series/%d", id);
    g_ser = api_get(p);
    g_seasonIdx = 0; g_epSel = 0; g_epScroll = 0;
    if (g_ser) { select_series_resume_target(g_ser); g_screen = SC_SERIES; }
    else toast("Nao consegui abrir a serie");
}
static void open_item(cJSON *item, int is_series) {
    if (!item) return;
    detail_capture_origin();
    int id = jint(item, "id");
    const char *kind = jstr(item, "kind");
    if (kind && (!strcmp(kind, "live") || !strcmp(kind, "episode"))) {
        resolve_and_play(id, jstr(item, "title"));
        return;
    }
    cJSON *sid = cJSON_GetObjectItem(item, "series_id");
    if (sid && cJSON_IsNumber(sid)) { open_series(sid->valueint); return; }
    if ((kind && !strcmp(kind, "series")) || is_series) open_series(id);
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

// Os jobs remotos continuam ate com o app fechado. Mantemos o console acordado
// somente enquanto o usuario acompanha a aba Historico.
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

// Tela de espera deliberadamente animada: mostra dados reais do preparo e evita
// a falsa impressao de download no SD sem expor detalhes de infraestrutura.
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

    SDL_SetRenderDrawColor(gRen, C_BG.r, C_BG.g, C_BG.b, 255); SDL_RenderClear(gRen);
    ui_header("NPLAY", "Preparando reproducao", "B Segundo plano");
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

    const char *headline = offline ? "Reconectando..." :
        (state && !strcmp(state, "fila")) ? "Seu pedido esta na fila" :
        (state && !strcmp(state, "baixando") && pct == 0) ? "Encontrando a melhor opcao para voce" :
        "Preparando sua obra para assistir";
    int hw = 0, hh = 0; text_cached(gRen, headline, C_TEXT, 1, &hw, &hh);
    text_draw(gRen, headline, (WIN_W - hw) / 2, 255, C_TEXT, 1);
    static const char *cozy[] = {
        "Pode pegar a pipoca. Avisaremos assim que estiver tudo pronto.",
        "Relaxe no sofa enquanto preparamos a sua obra.",
        "Estamos cuidando dos detalhes para a reproducao comecar bem."
    };
    text_center(cozy[(now / 7000) % 3], 305, C_MUT, 0);

    int bx = 120, by = 365, bw = WIN_W - 240;
    fill_rect(bx, by, bw, 20, C_CARD);
    if (pct > 0) fill_rect(bx, by, bw * pct / 100, 20, C_ACC2);
    else { int iw = 150, ix = bx + (int)((now / 5) % (bw + iw)) - iw; if (ix < bx) iw -= bx - ix, ix = bx; if (ix + iw > bx + bw) iw = bx + bw - ix; if (iw > 0) fill_rect(ix, by, iw, 20, C_ACC2); }
    char percent[32]; snprintf(percent, sizeof(percent), "%d%%", pct);
    text_center(percent, 399, C_TEXT, 1);

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
    text_draw(gRen, "Quando ficar pronto, a reproducao comeca automaticamente.", 120, 596, C_TEXT, 0);
    text_draw(gRen, "B volta sem cancelar. Continuaremos o preparo e a obra aparecera no Historico.", 120, 642, C_MUT, 0);
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
                rc = play_with_progress(itemId, title, url, 0);
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
    if (user_back) toast("Continuaremos preparando em segundo plano");
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
            if (episode_completed(ep) && g_dlDoneN < 256) g_dlDone[g_dlDoneN++] = jint(ep, "id");
        }
    }
    cJSON_Delete(sd);
}
static int dl_is_done(int item_id) { for (int i = 0; i < g_dlDoneN; i++) if (g_dlDone[i] == item_id) return 1; return 0; }

#define LOCAL_DL_DIR "sdmc:/switch/Nplay/downloads"
#define LOCAL_DL_MAX 2048
static int g_local_dl[LOCAL_DL_MAX];
static int g_local_dl_n = 0, g_local_dl_loaded = 0;

static void local_dl_path(int item_id, char *out, size_t cap) {
    snprintf(out, cap, LOCAL_DL_DIR "/%d.media", item_id);
}
static void local_dl_refresh(void) {
    g_local_dl_n = 0; g_local_dl_loaded = 1;
    DIR *d = opendir(LOCAL_DL_DIR); if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && g_local_dl_n < LOCAL_DL_MAX) {
        char *end = NULL; long id = strtol(e->d_name, &end, 10);
        if (id > 0 && end && !strcmp(end, ".media")) g_local_dl[g_local_dl_n++] = (int)id;
    }
    closedir(d);
}
static int local_dl_exists(int item_id) {
    if (!g_local_dl_loaded) local_dl_refresh();
    for (int i = 0; i < g_local_dl_n; i++) if (g_local_dl[i] == item_id) return 1;
    return 0;
}
static int __attribute__((unused)) local_dl_count(void) {
    if (!g_local_dl_loaded) local_dl_refresh();
    return g_local_dl_n;
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
        ui_header("DOWNLOAD OFFLINE", "Baixando para a microSD", "B Cancelar");
        ui_panel(120, 150, WIN_W - 240, 300, C_ACC2);
        text_draw(gRen, "ARQUIVO", 160, 180, C_ACC2, 0);
        text_clip(p->title ? p->title : "Video", 160, 220, C_TEXT, 1, WIN_W - 320);
        int pct = total > 0 ? (int)(received * 100 / total) : 0;
        if (pct > 100) pct = 100;
        ui_progress(160, 298, WIN_W - 320, pct, C_ACC2);
        char status[128];
        if (total > 0) snprintf(status, sizeof(status), "%d%%  -  %.1f de %.1f MB", pct, received / 1048576.0, total / 1048576.0);
        else snprintf(status, sizeof(status), "%.1f MB recebidos", received / 1048576.0);
        text_center_at(status, 160, WIN_W - 320, 330, C_TEXT, 1);
        text_center_at("Este arquivo ocupa espaco na microSD e fica disponivel sem internet.",
                       160, WIN_W - 320, 392, C_MUT, 0);
        ui_footer("Mantenha o aplicativo aberto    B Cancelar download");
        SDL_RenderPresent(gRen);
    }
    return p->cancel;
}

static void __attribute__((unused)) download_to_switch(cJSON *job) {
    if (!job || !cJSON_IsTrue(cJSON_GetObjectItem(job, "ready"))) { toast("Esta obra ainda esta sendo preparada"); return; }
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
    if (code == 200 && rename(part, final) == 0) { local_dl_refresh(); toast("Download concluido na microSD"); }
    else { remove(part); toast(progress.cancel ? "Download cancelado" : "Falha ao baixar para a microSD"); }
}
static void __attribute__((unused)) remove_from_switch(cJSON *job) {
    if (!job) return;
    char path[180]; local_dl_path(jint(job, "item_id"), path, sizeof(path));
    if (remove(path) == 0) { local_dl_refresh(); toast("Arquivo removido da microSD"); }
    else toast("Este item nao esta na microSD");
}
static int downloads_fetch_thread(void *unused) {
    (void)unused;
    char url[1024]; snprintf(url, sizeof(url), "%s/api/accel/jobs", BASE);
    struct membuf out = {0}; const char *err = NULL;
    long code = net_request_timeout(url, "GET", NULL, g_token[0] ? g_token : NULL,
                                    &out, &err, 3L, 6L);
    if (code == 200 && out.data) g_dl_pending = cJSON_Parse(out.data);
    membuf_free(&out);
    SDL_AtomicSet(&g_dl_done, 1);
    return 0;
}

static int history_fetch_thread(void *unused) {
    (void)unused;
    char url[1024]; snprintf(url, sizeof(url), "%s/api/sync/progress", BASE);
    struct membuf out = {0}; const char *err = NULL;
    long code = net_request_timeout(url, "GET", NULL, g_token[0] ? g_token : NULL,
                                    &out, &err, 3L, 7L);
    if (code == 200 && out.data) g_history_pending = cJSON_Parse(out.data);
    membuf_free(&out);
    SDL_AtomicSet(&g_history_done, 1);
    return 0;
}

static int watchlater_fetch_thread(void *unused) {
    (void)unused;
    char url[1024];
    struct membuf out = {0}; const char *err = NULL;
    snprintf(url, sizeof(url), "%s/api/sync/watchlater", BASE);
    long code = net_request_timeout(url, "GET", NULL, g_token[0] ? g_token : NULL,
                                    &out, &err, 3L, 7L);
    if (code == 200 && out.data) g_watchlater_pending = cJSON_Parse(out.data);
    membuf_free(&out);
    SDL_AtomicSet(&g_watchlater_done, 1);
    return 0;
}

static void load_history(void) {
    if (g_history_thread || g_watchlater_thread) { g_history_refresh_requested = 1; return; }
    g_history_pending = NULL;
    if (g_watchlater_pending) { cJSON_Delete(g_watchlater_pending); g_watchlater_pending = NULL; }
    SDL_AtomicSet(&g_history_done, 0);
    SDL_AtomicSet(&g_watchlater_done, 0);
    g_history_thread = SDL_CreateThread(history_fetch_thread, "history-fetch", NULL);
    g_watchlater_thread = SDL_CreateThread(watchlater_fetch_thread, "watchlater-fetch", NULL);
}

static void pump_history(void) {
    if (g_history_thread && SDL_AtomicGet(&g_history_done)) {
        SDL_WaitThread(g_history_thread, NULL); g_history_thread = NULL;
    }
    if (!g_history_thread && g_history_pending) {
        int old_n = arr_len(history_items());
        if (g_history) cJSON_Delete(g_history);
        g_history = g_history_pending; g_history_pending = NULL;
        int n = arr_len(cJSON_GetObjectItemCaseSensitive(g_history, "items"));
        if (g_history_sel >= n) g_history_sel = n > 0 ? n - 1 : 0;
        if (n == 0) g_history_zone = 1;
        else if (old_n == 0) g_history_zone = 0;
    }
    if (g_watchlater_thread && SDL_AtomicGet(&g_watchlater_done)) {
        SDL_WaitThread(g_watchlater_thread, NULL); g_watchlater_thread = NULL;
    }
    if (!g_watchlater_thread && g_watchlater_pending) {
        int list = store_media_list_create("Assistir mais tarde");
        cJSON *items = cJSON_GetObjectItemCaseSensitive(g_watchlater_pending, "items");
        cJSON *item;
        cJSON_ArrayForEach(item, items) {
            int series_id = jint(item, "series_id"), item_id = jint(item, "item_id");
            int is_series = series_id > 0;
            store_media_list_add(list, is_series ? series_id : item_id, is_series,
                                 jstr(item, "title"), jstr(item, "logo"));
        }
        cJSON_Delete(g_watchlater_pending); g_watchlater_pending = NULL;
    }
    if (!g_history_thread && !g_watchlater_thread && g_history_refresh_requested) {
        g_history_refresh_requested = 0;
        load_history();
    }
}

static cJSON *history_items(void) {
    return g_history ? cJSON_GetObjectItemCaseSensitive(g_history, "items") : NULL;
}

static void apply_downloads(cJSON *fresh) {
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
static void load_downloads(void) {
    if (g_dl_thread) return;
    g_dl_pending = NULL; SDL_AtomicSet(&g_dl_done, 0);
    g_dl_thread = SDL_CreateThread(downloads_fetch_thread, "jobs-fetch", NULL);
}
static void pump_downloads(void) {
    if (!g_dl_thread || !SDL_AtomicGet(&g_dl_done)) return;
    SDL_WaitThread(g_dl_thread, NULL); g_dl_thread = NULL;
    if (g_dl_pending) { cJSON *fresh = g_dl_pending; g_dl_pending = NULL; apply_downloads(fresh); }
}
static int dl_play(cJSON *job) {
    char local[180]; local_dl_path(jint(job, "item_id"), local, sizeof(local));
    if (local_dl_exists(jint(job, "item_id")))
        return play_with_progress(jint(job, "item_id"), jstr(job, "title"), local, 0);
    const char *fu = jstr(job, "file_url");
    if (!fu) { toast("Sem arquivo"); return 0; }
    char url[1400];
    if (strncmp(fu, "http", 4) == 0) snprintf(url, sizeof(url), "%s", fu);
    else snprintf(url, sizeof(url), "%s%s", BASE, fu);
    return play_with_progress(jint(job, "item_id"), jstr(job, "title"), url, 0);
}

// ------------------------------------------------------------- busca
// A API mistura filmes e canais em `items` e envia series separadamente. O
// cliente antigo tratava qualquer `item` como filme, abrindo detalhe incorreto
// para canais ao vivo. A camada abaixo tipa e filtra sem copiar o JSON recebido.
static int srch_matches(cJSON *item, int is_series, int filter) {
    if (filter == 0) return 1;
    const char *scope = jstr(item, "search_scope");
    if (filter == 3) return scope && !strcmp(scope, "anime");
    if (filter == 4) return scope && !strcmp(scope, "dorama");
    if (filter == 2) return is_series && (!scope || !strcmp(scope, "series"));
    return !is_series && (!scope || !strcmp(scope, "movie"));
}
static int srch_count_for(int filter) {
    if (!g_search) return 0;
    int n = 0; cJSON *it;
    cJSON_ArrayForEach(it, cJSON_GetObjectItem(g_search, "series")) if (srch_matches(it, 1, filter)) n++;
    cJSON_ArrayForEach(it, cJSON_GetObjectItem(g_search, "items")) if (srch_matches(it, 0, filter)) n++;
    return n;
}
static cJSON *srch_at(int wanted, int *is_series) {
    cJSON *it;
    cJSON_ArrayForEach(it, cJSON_GetObjectItem(g_search, "series")) {
        if (!srch_matches(it, 1, g_srchFilter)) continue;
        if (wanted-- == 0) { *is_series = 1; return it; }
    }
    cJSON_ArrayForEach(it, cJSON_GetObjectItem(g_search, "items")) {
        if (!srch_matches(it, 0, g_srchFilter)) continue;
        if (wanted-- == 0) { *is_series = 0; return it; }
    }
    *is_series = 0; return NULL;
}
static void url_encode_utf8(const char *input, char *output, size_t capacity) {
    static const char hex[] = "0123456789ABCDEF";
    size_t used = 0;
    if (!output || capacity == 0) return;
    for (const unsigned char *p = (const unsigned char *)(input ? input : ""); *p; p++) {
        int safe = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                   (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.' || *p == '~';
        size_t needed = safe ? 1 : 3;
        if (used + needed >= capacity) break;
        if (safe) output[used++] = (char)*p;
        else {
            output[used++] = '%';
            output[used++] = hex[*p >> 4];
            output[used++] = hex[*p & 15];
        }
    }
    output[used] = '\0';
}
static void do_search(void) {
    char q[128];
    if (prompt_text("Buscar filme, serie, anime, dorama...", q, sizeof(q), 0) != 0) return;
    if (g_search) { cJSON_Delete(g_search); g_search = NULL; }
    char enc[400]; url_encode_utf8(q, enc, sizeof(enc));
    char path[460]; snprintf(path, sizeof(path), "/api/catalog/search-v2?q=%s", enc);
    g_search = api_get(path);
    g_srchSel = 0; g_srchScroll = 0; g_srchFilter = 0;
    snprintf(g_srchQuery, sizeof(g_srchQuery), "%s", q);
    g_screen = SC_SEARCH;
}

// ------------------------------------------------------------- render: barra
static void draw_topbar(void) {
    fill_rect(0, 0, WIN_W, 72, C_BAR);
    fill_rect(0, 0, 6, 72, C_ACC);
    text_draw(gRen, "Nplay", 40, 18, C_ACC, 1);
    int tx = 172;
    for (int t = 0; t < NTABS; t++) {
        int w = text_draw(gRen, TAB_NAME[t], tx, 22, (t == g_tab) ? C_TEXT : C_MUT, 0);
        if (t == g_tab) fill_rect(tx, 53, w, 3, C_ACC);
        tx += w + 24;
    }
    text_right("Y Busca   - Config.   + Sair", WIN_W - 40, 24, C_MUT, 0);
    fill_rect(40, 71, WIN_W - 80, 1, C_CARD);
}

// ------------------------------------------------------------- render: landing
#define RCW 164
#define RCH 232
#define RGAP 14
#define HERO_H 224
#define RAILS_TOP (108 + HERO_H + 24)
static void draw_landing(void) {
    draw_topbar();
    if (!g_land) {
        ui_empty_state(g_status[0] ? g_status : "Carregando catalogo", "Verifique a conexao caso esta tela demore para responder.");
        ui_footer("Y Buscar    L/R Trocar categoria    - Configuracoes");
        return;
    }
    char hi[180]; snprintf(hi, sizeof(hi), "Bem-vindo de volta%s%s", g_user[0] ? ", " : "", g_user[0] ? g_user : "");
    if (g_tab == 0) text_draw(gRen, hi, 40, 76 - g_homeScroll, C_MUT, 0);
    else text_draw(gRen, TAB_NAME[g_tab], 40, 76 - g_homeScroll, C_MUT, 0);

    int nh = hero_count();
    int hy = 108 - g_homeScroll;
    if (nh > 0) {
        cJSON *h = cJSON_GetArrayItem(g_heroesArr, g_heroIdx % nh);
        const char *backdrop = jstr(h, "backdrop");
        SDL_Texture *bg = cover_get(backdrop);
        if (bg) {
            SDL_Rect br = { 40, hy, WIN_W - 80, HERO_H };
            ui_cover(bg, &br);
            fill_rect(40, hy, 720, HERO_H, (SDL_Color){ 8, 10, 15, 218 });
            fill_rect(760, hy, 480, HERO_H, (SDL_Color){ 8, 10, 15, 92 });
            fill_rect(40, hy, 5, HERO_H, C_ACC);
        } else ui_panel(40, hy, WIN_W - 80, HERO_H, C_ACC);
        if (g_railSel == -1) ui_focus(36, hy - 4, WIN_W - 72, HERO_H + 8);
        int content_x = bg ? 72 : 228;
        if (!bg) {
            SDL_Texture *tex = cover_get(jstr(h, "logo"));
            SDL_Rect pr = { 60, hy + 16, 138, HERO_H - 32 };
            if (tex) ui_cover(tex, &pr); else fill_rect(60, hy + 16, 138, HERO_H - 32, C_CARD);
        }
        text_draw(gRen, "EM DESTAQUE AGORA", content_x, hy + 24, C_ACC, 0);
        const char *ht = jstr(h, "title"); if (!ht) ht = "";
        text_clip(ht, content_x, hy + 52, C_TEXT, 1, bg ? 620 : WIN_W - 228 - 180);
        const char *hk = jstr(h, "kind");
        const char *kl = hk ? (!strcmp(hk, "movie") ? "Filme" : !strcmp(hk, "live") ? "Ao vivo" : "Serie")
                            : (g_heroSeriesDefault ? "Serie" : "Filme");
        char meta[96]; const char *year = jstr(h, "year");
        snprintf(meta, sizeof(meta), "%s%s%s%s", kl, year && year[0] ? "  |  " : "", year && year[0] ? year : "",
                 (cJSON_IsTrue(cJSON_GetObjectItem(h, "r2_ready")) || jint(h, "r2_ready")) ? "  |  Pronto pra tocar" : "");
        text_draw(gRen, meta, content_x, hy + 96, C_MUT, 0);
        const char *plot = jstr(h, "plot");
        if (plot && plot[0]) text_clip(plot, content_x, hy + 128, C_TEXT, 0, bg ? 650 : 700);
        text_draw(gRen, "A Abrir     Esquerda/direita Trocar destaque", content_x, hy + HERO_H - 36, C_MUT, 0);
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
            int series_item = catalog_item_is_series(it, g_rails[r].is_series);
            int fav_id = catalog_favorite_id(it, series_item);
            int fav = series_item ? is_fav_series(fav_id) : is_fav_item(fav_id);
            draw_card(x, ry, RCW, RCH, it, (r == g_railSel && i == g_railItem), fav);
        }
        y += 30 + RCH + 44;
        if (y > WIN_H + 240) break;
    }
    int search_y = (nh > 0 ? RAILS_TOP : 120) + g_railsN * (30 + RCH + 44) - g_homeScroll;
    if (search_y + 116 >= 72 && search_y < WIN_H) {
        int selected = g_railSel == g_railsN;
        ui_panel(40, search_y, WIN_W - 80, 112, C_ACC2);
        if (selected) ui_focus(36, search_y - 4, WIN_W - 72, 120);
        text_draw(gRen, "MAIS NO NPLAY", 68, search_y + 18, C_ACC2, 0);
        const char *prompt = g_tab == 1 ? "Procurando outro filme?" :
                             g_tab == 2 ? "Procurando outra serie?" :
                             g_tab == 3 ? "Procurando outro anime?" :
                             g_tab == 4 ? "Procurando outro dorama?" :
                                          "Quer encontrar uma obra especifica?";
        text_draw(gRen, prompt, 68, search_y + 48, C_TEXT, 1);
        text_draw(gRen, "Busque pelo nome ou por parte do titulo.", 68, search_y + 80, C_MUT, 0);
        ui_badge(selected ? "A  BUSCAR" : "Y  BUSCAR", WIN_W - 190, search_y + 42, selected ? C_ACC : C_ACC2);
    }
    ui_footer(g_railSel == g_railsN ?
        "A ou Y Abrir busca    Cima Voltar ao catalogo    L/R Trocar categoria" :
        "A Abrir    X Minha lista    Y Buscar    L/R Trocar categoria");
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
    int n = srch_count_for(g_srchFilter);
    char hd[200]; snprintf(hd, sizeof(hd), "Resultados para \"%s\"", g_srchQuery);
    text_clip(hd, 40, 88, C_TEXT, 1, 850);
    char count[64]; snprintf(count, sizeof(count), "%d resultado%s", n, n == 1 ? "" : "s");
    text_right(count, WIN_W - 40, 94, C_MUT, 0);
    static const char *filters[] = { "Tudo", "Filmes", "Series", "Animes", "Doramas" };
    int chip_x = 40;
    for (int i = 0; i < 5; i++) {
        int count = srch_count_for(i); char label[48];
        snprintf(label, sizeof(label), "%s  %d", filters[i], count);
        int tw = 0, th = 0; text_cached(gRen, label, C_TEXT, 0, &tw, &th);
        int cw = tw + 28;
        fill_rect(chip_x, 126, cw, 36, i == g_srchFilter ? C_ACC : C_CARD);
        text_center_at(label, chip_x, cw, 132, i == g_srchFilter ? C_TEXT : C_MUT, 0);
        chip_x += cw + 12;
    }
    if (n == 0) {
        ui_empty_state("Nada neste filtro", "Use ZL/ZR para trocar o tipo ou Y para fazer outra busca.");
        ui_footer("ZL/ZR Filtrar    Y Nova busca    B Voltar");
        return;
    }
    int top = 184;
    for (int i = 0; i < n; i++) {
        int col = i % GCOLS, row = i / GCOLS;
        int x = GMX + col * (GCW + GGAP) + (GCW - GCOVERW) / 2;
        int yy = top + row * (GCH + GGAP) - g_srchScroll;
        if (yy + GCH < 66 || yy > WIN_H) continue;
        int is; cJSON *it = srch_at(i, &is);
        int fav = is ? is_fav_series(jint(it, "id")) : is_fav_item(jint(it, "id"));
        draw_card(x, yy, GCOVERW, GCOVERH, it, i == g_srchSel, fav);
    }
    ui_footer("A Abrir    X Minha lista    ZL/ZR Filtrar    Y Nova busca    B Voltar");
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
static const char *ep_display_title(cJSON *episode) {
    const char *title = jstr(episode, "ep_title");
    if (!title || !title[0]) title = jstr(episode, "title");
    return ep_clean(title);
}
static void draw_series(void) {
    cJSON *s = ser_obj();
    int sid = jint(s, "id");
    int fav = is_fav_series(sid);
    const char *title = jstr(s, "title"); if (!title) title = "Serie";
    const char *section = jstr(s, "section");
    const char *area = section && !strcmp(section, "anime") ? "NPLAY / ANIME" :
                       section && !strcmp(section, "dorama") ? "NPLAY / DORAMA" : "NPLAY / SERIE";
    ui_header(area, title, "B Voltar");

    int LX = 48, LW = 212;
    ui_panel(32, 88, 244, 548, C_ACC);
    SDL_Texture *tex = cover_get(jstr(s, "logo"));
    SDL_Rect cr = { LX, 104, LW, 302 };
    if (tex) ui_cover(tex, &cr); else fill_rect(LX, 104, LW, 302, C_BAR);
    const char *year = jstr(s, "year");
    double rating = 0; cJSON *jr = cJSON_GetObjectItem(s, "rating"); if (jr && cJSON_IsNumber(jr)) rating = jr->valuedouble;
    char l1[80];
    if (rating > 0) snprintf(l1, sizeof(l1), "%s%sNota %.1f", year ? year : "", year ? "   " : "", rating);
    else snprintf(l1, sizeof(l1), "%s", year ? year : "");
    if (l1[0]) text_clip(l1, LX, 420, C_TEXT, 0, LW);
    const char *genre = jstr(s, "genre");
    if (genre) text_clip(genre, LX, 452, C_MUT, 0, LW);
    char epc[48]; snprintf(epc, sizeof(epc), "%d episodios", jint(s, "episode_count"));
    text_clip(epc, LX, 484, C_MUT, 0, LW);

    // versoes de audio (Legendado/Dublado) - troca com Y
    cJSON *au = ser_audio();
    if (arr_len(au) > 1) {
        text_draw(gRen, "VERSAO DE AUDIO", LX, 526, C_ACC2, 0);
        int ai = 0, current = 0; const char *current_label = "?"; cJSON *av;
        cJSON_ArrayForEach(av, au) {
            if (cJSON_IsTrue(cJSON_GetObjectItem(av, "current"))) {
                current = ai; current_label = jstr(av, "label"); if (!current_label) current_label = "?";
            }
            ai++;
        }
        char audio_state[96]; snprintf(audio_state, sizeof(audio_state), "%s  %d/%d", current_label, current + 1, arr_len(au));
        text_clip(audio_state, LX, 558, C_TEXT, 0, LW);
        text_draw(gRen, "ZL/ZR para trocar", LX, 594, C_MUT, 0);
    }

    int RX = 300, REND = WIN_W - 40;
    int grouped = ser_grouped();
    int nsea = ser_nseasons(), nep = ser_nep();
    char sh[64];
    if (grouped) snprintf(sh, sizeof(sh), "Temporada %d de %d", ser_group_idx() + 1, nsea);
    else { cJSON *sa = season_arr(); snprintf(sh, sizeof(sh), "Temporada %s", (sa && sa->string) ? sa->string : "1"); }
    text_draw(gRen, sh, RX, 96, C_TEXT, 1);
    if (nsea > 1) { char hint[64]; snprintf(hint, sizeof(hint), "L/R  %d temporadas", nsea); text_draw(gRen, hint, RX + 300, 104, C_MUT, 0); }
    fill_rect(WIN_W - 276, 92, 236, 42, fav ? C_ROSE : C_CARD);
    text_center_at(fav ? "X  Na Minha lista" : "X  Adicionar a lista",
                   WIN_W - 276, 236, 99, C_TEXT, 0);
    fill_rect(RX, 148, REND - RX, 2, C_CARD);

    int listTop = 168, rowH = 44, visible = (WIN_H - listTop - 44) / rowH;
    if (g_epSel < g_epScroll) g_epScroll = g_epSel;
    if (g_epSel >= g_epScroll + visible) g_epScroll = g_epSel - visible + 1;
    for (int i = g_epScroll; i < nep && i < g_epScroll + visible; i++) {
        cJSON *ep = ser_ep_at(i);
        int yy = listTop + (i - g_epScroll) * rowH;
        if (i == g_epSel) { fill_rect(RX - 8, yy - 5, REND - RX + 16, rowH - 2, C_CARD); fill_rect(RX - 8, yy - 5, 4, rowH - 2, C_ACC2); }
        int en = jint(ep, "episode"); char nb[16]; snprintf(nb, sizeof(nb), "%d", en > 0 ? en : i + 1);
        int done = episode_completed(ep);
        int pos = jint(ep, "position_seconds"), duration = jint(ep, "duration_seconds");
        int progress = (!done && pos > 10 && duration > 0) ? pos * 100 / duration : 0;
        if (progress > 99) progress = 99;
        text_draw(gRen, nb, RX, yy, (i == g_epSel) ? C_ACC : (done ? C_GREEN : C_MUT), 0);
        text_clip(ep_display_title(ep), RX + 52, yy, (i == g_epSel) ? C_TEXT : C_MUT, 0, REND - RX - 130);
        if (done) text_draw(gRen, "Visto", REND - 70, yy, C_GREEN, 2);
        else if (progress > 0) {
            char pct[16]; snprintf(pct, sizeof(pct), "%d%%", progress);
            text_draw(gRen, pct, REND - 62, yy, C_ACC2, 2);
            fill_rect(RX + 52, yy + 29, REND - RX - 138, 3, C_CARD);
            fill_rect(RX + 52, yy + 29, (REND - RX - 138) * progress / 100, 3, C_ACC);
        }
    }
    // barra de rolagem (ha muitos episodios)
    if (nep > visible) {
        int trkH = visible * rowH, thumbH = trkH * visible / nep;
        int thumbY = listTop + (trkH - thumbH) * g_epScroll / (nep - visible);
        fill_rect(REND + 8, listTop, 4, trkH, C_CARD);
        fill_rect(REND + 8, thumbY, 4, thumbH < 12 ? 12 : thumbH, C_ACC);
    }
    if (nep == 0) text_draw(gRen, "Sem episodios", RX, listTop, C_MUT, 0);
    ui_footer("A Assistir    L/R Temporada    Y Preparar episodios    X Minha lista    + Listas    ZL/ZR Audio");
}

// Menu "baixar episodios" (Y no detalhe): marca quais episodios baixar.
static void open_dlmenu(void) {
    int n = ser_nep();
    g_epChkN = n > 512 ? 512 : n;
    for (int i = 0; i < g_epChkN; i++) g_epChk[i] = 0;
    g_dlmenu = 1;
}
static void draw_dlmenu(void) {
    cJSON *s = ser_obj(); const char *title = jstr(s, "title");
    ui_header("SALVAR NA CONTA", title ? title : "Escolher episodios", "B Cancelar");
    text_center("Escolha os episodios que deseja deixar prontos para assistir", 78, C_MUT, 0);
    int n = ser_nep(), cnt = 0;
    for (int i = 0; i < g_epChkN; i++) if (g_epChk[i]) cnt++;
    int listTop = 118, rowH = 40, visible = (WIN_H - listTop - 56) / rowH;
    if (g_epSel < g_epScroll) g_epScroll = g_epSel;
    if (g_epSel >= g_epScroll + visible) g_epScroll = g_epSel - visible + 1;
    for (int i = g_epScroll; i < n && i < g_epScroll + visible; i++) {
        cJSON *ep = ser_ep_at(i);
        int yy = listTop + (i - g_epScroll) * rowH, sel = (i == g_epSel);
        if (sel) { fill_rect(32, yy - 5, WIN_W - 64, rowH - 2, C_CARD); fill_rect(32, yy - 5, 4, rowH - 2, C_ACC2); }
        int chk = (i < g_epChkN) && g_epChk[i];
        border_rect(48, yy + 1, 22, 22, 2, chk ? C_ACC : C_MUT);
        if (chk) fill_rect(53, yy + 6, 12, 12, C_ACC);
        int en = jint(ep, "episode"); char nb[16]; snprintf(nb, sizeof(nb), "Ep %d", en > 0 ? en : i + 1);
        text_draw(gRen, nb, 86, yy, sel ? C_ACC : C_MUT, 0);
        text_clip(ep_display_title(ep), 170, yy, sel ? C_TEXT : C_MUT, 0, WIN_W - 240);
    }
    char foot[140]; snprintf(foot, sizeof(foot), "%d selecionado%s    A Marcar    X Todos    Y Salvar selecionados    B Cancelar", cnt, cnt == 1 ? "" : "s");
    ui_footer(foot);
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
        char m[64]; snprintf(m, sizeof(m), "%d episodio(s) adicionado(s) a biblioteca", cnt);
        toast(m); g_dlmenu = 0; g_screen = SC_MAIN; enter_tab(TAB_DOWNLOADS);
    }
}

// ------------------------------------------------------------- render: downloads
static int is_account_watchlater(const char *name) {
    return name && !strcasecmp(name, "Assistir mais tarde");
}

static long sync_watchlater_item(const char *method, int id, int is_series) {
    char body[96];
    snprintf(body, sizeof(body), is_series ? "{\"series_id\":%d}" : "{\"item_id\":%d}", id);
    return api_send("/api/sync/watchlater", method, body);
}

int media_list_add_named(const char *name, int id, int is_series, const char *title, const char *logo) {
    int list = store_media_list_create(name);
    if (list < 0) { toast("Nao foi possivel criar essa lista"); return -1; }
    int r = store_media_list_add(list, id, is_series, title, logo);
    if (r == 1) toast("Este titulo ja esta nessa lista");
    else if (r == 0) { char msg[96]; snprintf(msg, sizeof(msg), "Adicionado a %s", store_media_list_name(list)); toast(msg); }
    else toast(r == -2 ? "A lista chegou ao limite de itens" : "Nao foi possivel adicionar");
    if (r >= 0 && is_account_watchlater(store_media_list_name(list)) &&
        sync_watchlater_item("POST", id, is_series) != 200)
        toast("Salvo apenas neste Switch; tente novamente quando houver internet");
    return r;
}

// O usuario escolhe o nome: se ja existir, adiciona; senao cria uma nova lista.
int media_list_prompt_add(int id, int is_series, const char *title, const char *logo) {
    char name[48];
    if (prompt_text("Nome da lista (nova ou existente)", name, sizeof(name), 0) != 0) return -1;
    return media_list_add_named(name, id, is_series, title, logo);
}

#define HIST_CW 168
#define HIST_CH 224
#define HIST_GAP 20

static int horizontal_scroll(int selected, int item_w, int gap) {
    int x = 40 + selected * (item_w + gap), scroll = 0;
    if (x + item_w > WIN_W - 40) scroll = x + item_w - (WIN_W - 40);
    return scroll > 0 ? scroll : 0;
}

static void draw_history_card(int x, int y, cJSON *item, int selected) {
    const char *title = jstr(item, "title"); if (!title) title = "Titulo";
    if (selected) {
        fill_rect(x - 8, y - 8, HIST_CW + 16, HIST_CH + 16, (SDL_Color){ 4, 6, 11, 255 });
        border_rect(x - 5, y - 5, HIST_CW + 10, HIST_CH + 10, 2, C_ACC);
    }
    SDL_Texture *cover = cover_get(jstr(item, "logo"));
    if (cover) { SDL_Rect r = {x, y, HIST_CW, HIST_CH}; ui_cover(cover, &r); }
    else fill_rect(x, y, HIST_CW, HIST_CH, C_CARD);
    int pos = jint(item, "position_seconds"), dur = jint(item, "duration_seconds");
    int pct = dur > 0 ? pos * 100 / dur : 0;
    ui_progress(x, y + HIST_CH - 8, HIST_CW, pct, C_ACC);
    text_clip(title, x, y + HIST_CH + 8, selected ? C_TEXT : C_MUT, 0, HIST_CW);
    if (selected) fill_rect(x, y + HIST_CH + 37, HIST_CW, 2, C_ACC2);
    if (!strcmp(jstr(item, "kind") ? jstr(item, "kind") : "", "episode")) {
        char ep[32]; snprintf(ep, sizeof(ep), "T%d  E%d", jint(item, "season") > 0 ? jint(item, "season") : 1, jint(item, "episode"));
        ui_card_badge(ep, x + 7, y + 7, C_ACC2);
    }
}

static void draw_history_actions(void) {
    cJSON *items = history_items();
    int n = arr_len(items);
    if (!g_history_menu || g_history_sel < 0 || g_history_sel >= n) return;
    cJSON *item = cJSON_GetArrayItem(items, g_history_sel);
    const char *options[] = { "Continuar assistindo", "Recomecar do inicio", "Marcar como concluido", "Remover do Historico" };
    ui_panel(286, 112, 708, 490, C_ACC);
    text_draw(gRen, "OPCOES DO HISTORICO", 324, 142, C_ACC, 0);
    text_clip(jstr(item, "title") ? jstr(item, "title") : "Titulo", 324, 180, C_TEXT, 1, 632);
    int pos = jint(item, "position_seconds"), dur = jint(item, "duration_seconds");
    char progress[96]; snprintf(progress, sizeof(progress), "%d min assistidos  |  %d%% concluido",
                                pos / 60, dur > 0 ? pos * 100 / dur : 0);
    text_draw(gRen, progress, 324, 226, C_MUT, 0);
    for (int i = 0; i < 4; i++) {
        int y = 278 + i * 64;
        fill_rect(324, y, 632, 50, C_CARD);
        if (i == g_history_menu_sel) { ui_focus(320, y - 4, 640, 58); fill_rect(324, y, 5, 50, C_ACC2); }
        text_draw(gRen, options[i], 350, y + 10, i == g_history_menu_sel ? C_TEXT : C_MUT, 0);
    }
    text_center_at("A Confirmar    B Cancelar", 324, 632, 552, C_MUT, 0);
}

static int history_set_position(cJSON *item, int position) {
    int id = jint(item, "item_id"), duration = jint(item, "duration_seconds");
    if (id <= 0) return -1;
    if (position < 0) position = 0;
    char body[192];
    snprintf(body, sizeof(body), "{\"item_id\":%d,\"position_seconds\":%d,\"duration_seconds\":%d}",
             id, position, duration);
    return api_send("/api/sync/progress", "POST", body) == 200 ? 0 : -1;
}

static void draw_history_home(void) {
    cJSON *items = history_items(); int nh = arr_len(items);
    text_draw(gRen, "Continuar assistindo", 40, 88, g_history_zone == 0 ? C_TEXT : C_MUT, 1);
    text_right(nh ? "A retoma do ponto salvo" : "Seu progresso aparecera aqui", WIN_W - 40, 96, C_MUT, 0);
    if (!g_history && g_history_thread) {
        text_draw(gRen, "Carregando seu historico...", 40, 146, C_MUT, 0);
    } else if (nh <= 0) {
        ui_panel(40, 134, WIN_W - 80, 250, C_ACC2);
        text_center_at("Nenhuma obra em andamento", 70, WIN_W - 140, 202, C_TEXT, 1);
        text_center_at("Quando voce parar um video, ele ficara pronto para continuar aqui.", 70, WIN_W - 140, 264, C_MUT, 0);
    } else {
        int scroll = horizontal_scroll(g_history_sel, HIST_CW, HIST_GAP);
        for (int i = 0; i < nh; i++) {
            int x = 40 + i * (HIST_CW + HIST_GAP) - scroll;
            if (x + HIST_CW < 0 || x > WIN_W) continue;
            draw_history_card(x, 134, cJSON_GetArrayItem(items, i), g_history_zone == 0 && i == g_history_sel);
        }
    }

    int list_count = store_media_list_count();
    text_draw(gRen, "Suas listas", 40, 418, g_history_zone == 1 ? C_TEXT : C_MUT, 1);
    text_right("Biblioteca, favoritos e colecoes pessoais", WIN_W - 40, 426, C_MUT, 0);
    int total = list_count + 2; // Biblioteca + listas locais + Nova lista
    int tile_w = 270, tile_gap = 18, scroll = horizontal_scroll(g_list_sel, tile_w, tile_gap);
    for (int i = 0; i < total; i++) {
        int x = 40 + i * (tile_w + tile_gap) - scroll, y = 466;
        if (x + tile_w < 0 || x > WIN_W) continue;
        int selected = g_history_zone == 1 && i == g_list_sel;
        ui_panel(x, y, tile_w, 158, C_ACC);
        if (selected) ui_focus(x - 3, y - 3, tile_w + 6, 164);
        if (i == 0) {
            text_draw(gRen, "BIBLIOTECA", x + 22, y + 18, C_ACC2, 0);
            text_draw(gRen, "Conteudos preparados", x + 22, y + 58, C_TEXT, 0);
            char count[64]; snprintf(count, sizeof(count), "%d obra%s", g_dlgN, g_dlgN == 1 ? "" : "s");
            text_draw(gRen, count, x + 22, y + 112, C_MUT, 0);
        } else if (i == total - 1) {
            text_draw(gRen, "CRIAR LISTA", x + 22, y + 18, C_ACC2, 0);
            text_clip("+ Nova colecao", x + 22, y + 58, C_TEXT, 1, tile_w - 44);
            text_draw(gRen, "Marvel, DC, comedia...", x + 22, y + 112, C_MUT, 0);
        } else {
            const char *name = store_media_list_name(i - 1);
            text_draw(gRen, "LISTA", x + 22, y + 18, C_ACC2, 0);
            text_clip(name, x + 22, y + 58, C_TEXT, 1, tile_w - 44);
            char count[64]; int n = store_media_list_item_count(i - 1);
            snprintf(count, sizeof(count), "%d titulo%s", n, n == 1 ? "" : "s");
            text_draw(gRen, count, x + 22, y + 112, C_MUT, 0);
        }
    }
    if (g_history_zone == 0) ui_footer(nh > 0 ?
        "Esquerda/direita Escolher    A Continuar    X Opcoes    Baixo Suas listas" :
        "Baixo Suas listas");
    else if (g_list_sel == 0) ui_footer("A Abrir Biblioteca    Esquerda/direita Escolher    Cima Continuar assistindo");
    else if (g_list_sel == total - 1) ui_footer("A Criar nova lista    Esquerda/direita Escolher    Cima Continuar assistindo");
    else ui_footer("A Abrir    Y Renomear    ZR Excluir    Cima Continuar assistindo");
    draw_history_actions();
}

// Biblioteca: uma capa por obra preparada para reproducao.
static void draw_dl_grid(void) {
    text_draw(gRen, "Biblioteca", 40, 88, C_TEXT, 1);
    text_draw(gRen, "Obras preparadas para assistir", 40, 126, C_MUT, 0);
    if (dl_has_active()) ui_badge("TELA ATIVA", WIN_W - 174, 90, C_GREEN);
    if (!g_dl && g_dl_thread) {
        ui_empty_state("Atualizando seus itens", "Buscando o estado mais recente da sua conta...");
        ui_footer("B Voltar");
        return;
    }
    if (g_dlgN == 0) {
        ui_empty_state("Sua biblioteca esta vazia", "Abra uma obra e escolha assistir; cuidaremos do preparo para voce.");
        ui_footer("B Voltar ao Historico");
        return;
    }
    int top = 164;
    for (int i = 0; i < g_dlgN; i++) {
        int col = i % GCOLS, row = i / GCOLS;
        int x = GMX + col * (GCW + GGAP) + (GCW - GCOVERW) / 2;
        int yy = top + row * (GCH + GGAP) - g_dlScroll;
        if (yy + GCH < 66 || yy > WIN_H) continue;
        cJSON *j0 = dlg_job(i, 0);
        if (i == g_dlSel) {
            fill_rect(x - 8, yy - 8, GCOVERW + 16, GCOVERH + 16, (SDL_Color){ 4, 6, 11, 255 });
            border_rect(x - 5, yy - 5, GCOVERW + 10, GCOVERH + 10, 2, C_ACC);
        }
        SDL_Texture *cov = cover_get(jstr(j0, "cover"));
        SDL_Rect cr = { x, yy, GCOVERW, GCOVERH };
        if (cov) ui_cover(cov, &cr); else fill_rect(x, yy, GCOVERW, GCOVERH, C_CARD);
        int nJobs = g_dlg[i].nJobs, baixando = 0;
        for (int k = 0; k < nJobs; k++) if (!cJSON_IsTrue(cJSON_GetObjectItem(dlg_job(i, k), "ready"))) baixando++;
        char badge[32];
        if (g_dlg[i].isMovie) { if (baixando) snprintf(badge, sizeof(badge), "%d%%", jint(j0, "percent")); else snprintf(badge, sizeof(badge), "Pronto"); }
        else snprintf(badge, sizeof(badge), "%d ep%s", nJobs, nJobs == 1 ? "" : "s");
        int tw = 0, th = 0; text_cached(gRen, badge, C_TEXT, 2, &tw, &th);
        int bw = tw + 14; if (bw < 28) bw = 28;
        ui_card_badge(badge, x + GCOVERW - bw - 7, yy + GCOVERH - 29, baixando ? C_ACC : C_GREEN);
        const char *title = jstr(j0, "title"); if (!title) title = "";
        text_clip(title, x, yy + GCOVERH + 8, i == g_dlSel ? C_TEXT : C_MUT, 0, GCOVERW);
        if (i == g_dlSel) fill_rect(x, yy + GCOVERH + 37, GCOVERW, 2, C_ACC2);
    }
    ui_footer("A Abrir    X Remover    B Historico");
}
// Vista 2: episodios baixados de UMA obra (com status), estilo menu de serie.
static void draw_dl_detail(void) {
    int g = g_dlGroup;
    if (g >= g_dlgN) { g_dlView = 0; return; }
    cJSON *j0 = dlg_job(g, 0);
    const char *title = jstr(j0, "title"); if (!title) title = "";
    text_draw(gRen, "EPISODIOS PREPARADOS", 40, 92, C_ACC2, 0);
    text_clip(title, 40, 124, C_TEXT, 1, WIN_W - 300);
    if (dl_has_active()) ui_badge("TELA ATIVA", WIN_W - 174, 96, C_GREEN);
    int nj = g_dlg[g].nJobs;
    int listTop = 176, rowH = 46, visible = (WIN_H - listTop - 56) / rowH;
    if (g_dlDetSel < g_dlDetScroll) g_dlDetScroll = g_dlDetSel;
    if (g_dlDetSel >= g_dlDetScroll + visible) g_dlDetScroll = g_dlDetSel - visible + 1;
    for (int i = g_dlDetScroll; i < nj && i < g_dlDetScroll + visible; i++) {
        cJSON *j = dlg_job(g, i);
        int yy = listTop + (i - g_dlDetScroll) * rowH, sel = (i == g_dlDetSel);
        if (sel) { fill_rect(32, yy - 6, WIN_W - 64, rowH - 4, C_CARD); fill_rect(32, yy - 6, 4, rowH - 4, C_ACC2); }
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
        if (ready) snprintf(st, sizeof(st), "PRONTO");
        else if (erro) snprintf(st, sizeof(st), "ERRO");
        else snprintf(st, sizeof(st), "preparando %d%%", pct);
        text_draw(gRen, st, WIN_W - 260, yy, ready ? C_GREEN : (erro ? C_ROSE : C_ACC), 0);
        ui_progress(WIN_W - 260, yy + 27, 200, pct, ready ? C_GREEN : (erro ? C_ROSE : C_ACC));
    }
    if (nj > visible) {
        int trkH = visible * rowH, thumbH = trkH * visible / nj;
        int thumbY = listTop + (trkH - thumbH) * g_dlDetScroll / (nj - visible);
        fill_rect(WIN_W - 22, listTop, 4, trkH, C_CARD);
        fill_rect(WIN_W - 22, thumbY, 4, thumbH < 12 ? 12 : thumbH, C_ACC);
    }
    ui_footer("A Assistir    X Remover    B Biblioteca");
}

static void draw_custom_list(void) {
    const char *name = store_media_list_name(g_open_list);
    int n = store_media_list_item_count(g_open_list);
    text_draw(gRen, "LISTA PESSOAL", 40, 90, C_ACC2, 0);
    text_clip(name, 40, 122, C_TEXT, 1, 760);
    char total[64]; snprintf(total, sizeof(total), "%d titulo%s", n, n == 1 ? "" : "s");
    text_right(total, WIN_W - 40, 130, C_MUT, 0);
    if (n <= 0) {
        ui_empty_state("Esta lista esta vazia", "Use X nos relacionados para adicionar uma obra.");
        ui_footer("Y Renomear lista    ZR Excluir lista    B Voltar");
        return;
    }
    int top = 174, selected_row = g_list_item_sel / GCOLS;
    int selected_bottom = top + selected_row * (GCH + GGAP) + GCH;
    int scroll_px = selected_bottom > WIN_H - 52 ? selected_bottom - (WIN_H - 52) + 16 : 0;
    for (int i = 0; i < n; i++) {
        int col = i % GCOLS, row = i / GCOLS;
        int x = GMX + col * (GCW + GGAP) + (GCW - GCOVERW) / 2;
        int y = top + row * (GCH + GGAP) - scroll_px;
        if (y + GCH < 72 || y > WIN_H - 52) continue;
        int id = 0, is_series = 0; char title[128], logo[720];
        if (!store_media_list_get(g_open_list, i, &id, &is_series, title, sizeof(title), logo, sizeof(logo))) continue;
        if (i == g_list_item_sel) {
            fill_rect(x - 8, y - 8, GCOVERW + 16, GCOVERH + 16, (SDL_Color){ 4, 6, 11, 255 });
            border_rect(x - 5, y - 5, GCOVERW + 10, GCOVERH + 10, 2, C_ACC);
        }
        SDL_Texture *cover = cover_get(logo);
        if (cover) { SDL_Rect r = {x, y, GCOVERW, GCOVERH}; ui_cover(cover, &r); }
        else fill_rect(x, y, GCOVERW, GCOVERH, C_CARD);
        text_clip(title, x, y + GCOVERH + 8, i == g_list_item_sel ? C_TEXT : C_MUT, 0, GCOVERW);
        if (i == g_list_item_sel) fill_rect(x, y + GCOVERH + 37, GCOVERW, 2, C_ACC2);
    }
    ui_footer("A Abrir    X Remover da lista    Y Renomear lista    ZR Excluir lista    B Voltar");
}

static void draw_downloads(void) {
    draw_topbar();
    if (g_dlView == 1) draw_dl_detail();
    else if (g_dlView == 2) draw_dl_grid();
    else if (g_dlView == 3) draw_custom_list();
    else draw_history_home();
}

// ------------------------------------------------------------- login
static int do_login(void) {
    char user[128] = { 0 }, pass[128] = { 0 };
    if (prompt_text("Usuario Nplay", user, sizeof(user), 0) != 0) return -1;
    if (prompt_text("Senha", pass, sizeof(pass), 1) != 0) return -1;
    char fingerprint[80] = {0};
    if (!store_load_device_id(fingerprint, sizeof(fingerprint))) {
        unsigned char random_id[16]; randomGet(random_id, sizeof(random_id));
        strcpy(fingerprint, "nplay-switch-");
        int off = (int)strlen(fingerprint);
        for (int i = 0; i < 16; i++) snprintf(fingerprint + off + i * 2, sizeof(fingerprint) - (size_t)(off + i * 2), "%02x", random_id[i]);
        store_save_device_id(fingerprint);
    }
    cJSON *request = cJSON_CreateObject();
    if (!request) { memset(pass, 0, sizeof(pass)); snprintf(g_status, sizeof(g_status), "Memoria insuficiente para entrar"); return -1; }
    cJSON_AddStringToObject(request, "username", user);
    cJSON_AddStringToObject(request, "password", pass);
    cJSON *device = cJSON_AddObjectToObject(request, "device");
    if (!device) { cJSON_Delete(request); memset(pass, 0, sizeof(pass)); snprintf(g_status, sizeof(g_status), "Memoria insuficiente para entrar"); return -1; }
    cJSON_AddStringToObject(device, "fingerprint", fingerprint);
    cJSON_AddStringToObject(device, "type", "tv");
    cJSON_AddStringToObject(device, "name", "Nintendo Switch");
    char *body = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);
    memset(pass, 0, sizeof(pass));
    if (!body) { snprintf(g_status, sizeof(g_status), "Nao foi possivel preparar o login"); return -1; }
    char url[512]; snprintf(url, sizeof(url), "%s/api/auth/login", BASE);
    struct membuf out = { 0 }; const char *err = NULL;
    long code = net_request_timeout(url, "POST", body, NULL, &out, &err, 8L, 20L);
    free(body);
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
    const int x = 330, y = 138, w = 620, h = 430;
    ui_panel(x, y, w, h, C_ACC);
    text_center_at("NPLAY", x, w, y + 52, C_ACC, 1);
    text_center_at("Entre para continuar no Nintendo Switch", x, w, y + 105, C_MUT, 0);
    fill_rect(x + 90, y + 174, w - 180, 58, C_ACC);
    text_center_at("A   Entrar com minha conta", x + 90, w - 180, y + 187, C_TEXT, 0);
    text_center_at("Sua senha e digitada pelo teclado seguro do console", x, w, y + 258, C_MUT, 0);
    if (g_status[0]) {
        fill_rect(x + 36, y + 308, w - 72, 48, C_BAR);
        text_center_at(g_status, x + 52, w - 104, y + 318, C_ROSE, 0);
    }
    text_center("+  Sair do aplicativo", WIN_H - 70, C_MUT, 0);
}

// ------------------------------------------------------------- input
static void enter_tab(int tab) {
    g_tab = tab;
    g_status[0] = '\0';
    if (tab == TAB_DOWNLOADS) {
        g_dlSel = 0; g_dlScroll = 0; g_dlView = 0; g_history_sel = 0;
        g_history_zone = arr_len(history_items()) > 0 ? 0 : 1; g_list_sel = 0;
        g_history_menu = 0; g_history_menu_sel = 0;
        local_dl_refresh(); load_downloads(); load_history();
        g_dl_next = SDL_GetTicks() + 2000; return;
    }
    load_landing(tab);
}
static void input_landing(int b) {
    int nh = hero_count();
    if (g_railSel < 0) {   // destaque focado
        if (b == JOY_DOWN) g_railSel = 0; // primeira rail ou chamada de busca
        else if (b == JOY_DLEFT) { if (nh) g_heroIdx = (g_heroIdx - 1 + nh) % nh; g_hero_next = SDL_GetTicks() + 6000; }
        else if (b == JOY_DRIGHT) { if (nh) g_heroIdx = (g_heroIdx + 1) % nh; g_hero_next = SDL_GetTicks() + 6000; }
        else if (b == JOY_A) { if (nh) { cJSON *h = cJSON_GetArrayItem(g_heroesArr, g_heroIdx % nh); const char *k = jstr(h, "kind"); open_item(h, k ? strcmp(k, "movie") != 0 : g_heroSeriesDefault); } }
        else if (b == JOY_X) { if (nh) { cJSON *h = cJSON_GetArrayItem(g_heroesArr, g_heroIdx % nh); int is = catalog_item_is_series(h, g_heroSeriesDefault); int id = catalog_favorite_id(h, is); if (is) toggle_fav_series(id); else toggle_fav_item(id); } }
        g_homeScroll = 0;
        return;
    }
    if (g_railSel == g_railsN) { // chamada de busca ao final do catalogo
        if (b == JOY_UP) {
            if (g_railsN > 0) {
                g_railSel = g_railsN - 1;
                int n = arr_len(g_rails[g_railSel].arr);
                if (g_railItem >= n) g_railItem = n ? n - 1 : 0;
            } else if (nh > 0) g_railSel = -1;
        } else if (b == JOY_A) do_search();
        if (g_railSel == g_railsN) {
            int sy = (nh > 0 ? RAILS_TOP : 120) + g_railsN * (30 + RCH + 44);
            if (sy + 132 - g_homeScroll > WIN_H - 52) g_homeScroll = sy + 132 - (WIN_H - 52) + 12;
        } else if (g_railSel < 0) g_homeScroll = 0;
        return;
    }
    int items = arr_len(g_rails[g_railSel].arr);
    if (b == JOY_UP) { if (g_railSel == 0) { g_railSel = (nh > 0) ? -1 : 0; g_homeScroll = 0; if (nh > 0) return; } else { g_railSel--; int n = arr_len(g_rails[g_railSel].arr); if (g_railItem >= n) g_railItem = n ? n - 1 : 0; } }
    else if (b == JOY_DOWN) { if (g_railSel < g_railsN - 1) { g_railSel++; int n = arr_len(g_rails[g_railSel].arr); if (g_railItem >= n) g_railItem = n ? n - 1 : 0; } else g_railSel = g_railsN; }
    else if (b == JOY_DLEFT) { if (g_railItem > 0) g_railItem--; }
    else if (b == JOY_DRIGHT) { if (g_railItem < items - 1) g_railItem++; }
    else if (b == JOY_A) { open_item(cJSON_GetArrayItem(g_rails[g_railSel].arr, g_railItem), g_rails[g_railSel].is_series); }
    else if (b == JOY_X) { cJSON *it = cJSON_GetArrayItem(g_rails[g_railSel].arr, g_railItem); if (it) { int is = catalog_item_is_series(it, g_rails[g_railSel].is_series); int id = catalog_favorite_id(it, is); if (is) toggle_fav_series(id); else toggle_fav_item(id); } }
    int ry = (nh > 0 ? RAILS_TOP : 120) + g_railSel * (30 + RCH + 44);
    int item_h = g_railSel == g_railsN ? 112 : RCH + 60;
    if (ry + item_h - g_homeScroll > WIN_H - 52) g_homeScroll = ry + item_h - (WIN_H - 52) + 20;
    if (ry - g_homeScroll < 80) g_homeScroll = ry - 80;
    if (g_homeScroll < 0) g_homeScroll = 0;
}
static void input_search(int b) {
    int n = srch_count_for(g_srchFilter);
    if (b == JOY_B || b == JOY_MINUS) { g_screen = SC_MAIN; return; }
    if (b == JOY_Y) { do_search(); return; }
    if (b == JOY_ZL || b == JOY_ZR) {
        int step = b == JOY_ZR ? 1 : -1;
        g_srchFilter = (g_srchFilter + step + 5) % 5;
        g_srchSel = 0; g_srchScroll = 0;
        return;
    }
    if (b == JOY_UP) { if (g_srchSel - GCOLS >= 0) g_srchSel -= GCOLS; }
    else if (b == JOY_DOWN) { if (g_srchSel + GCOLS < n) g_srchSel += GCOLS; }
    else if (b == JOY_DLEFT) { if (g_srchSel > 0) g_srchSel--; }
    else if (b == JOY_DRIGHT) { if (g_srchSel + 1 < n) g_srchSel++; }
    else if (b == JOY_A) { int is; cJSON *it = srch_at(g_srchSel, &is); if (it) open_item(it, is); }
    else if (b == JOY_X) { int is; cJSON *it = srch_at(g_srchSel, &is); if (it) { if (is) toggle_fav_series(jint(it, "id")); else toggle_fav_item(jint(it, "id")); } }
    int row = g_srchSel / GCOLS, rowTop = 184 + row * (GCH + GGAP), rowBot = rowTop + GCH;
    if (rowBot - g_srchScroll > WIN_H - 52) g_srchScroll = rowBot - (WIN_H - 52) + 16;
    if (rowTop - g_srchScroll < 184) g_srchScroll = rowTop - 184;
    if (g_srchScroll < 0) g_srchScroll = 0;
}
static int prompt_next_episode(cJSON *episode) {
    Uint32 deadline = SDL_GetTicks() + 5000;
    cJSON *series = ser_obj();
    while (g_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) { g_running = 0; return 0; }
            if (event.type != SDL_JOYBUTTONDOWN) continue;
            if (event.jbutton.button == JOY_A) return 1;
            if (event.jbutton.button == JOY_B || event.jbutton.button == JOY_MINUS) return 0;
        }
        Uint32 now = SDL_GetTicks();
        if ((Sint32)(deadline - now) <= 0) return 1;
        int remaining = (int)((deadline - now + 999) / 1000);
        SDL_SetRenderDrawColor(gRen, C_BG.r, C_BG.g, C_BG.b, 255); SDL_RenderClear(gRen);
        SDL_Texture *backdrop = cover_get(jstr(series, "backdrop"));
        if (!backdrop) backdrop = cover_get(jstr(series, "logo"));
        if (backdrop) {
            SDL_Rect bg = {0, 0, WIN_W, WIN_H}; ui_cover(backdrop, &bg);
            fill_rect(0, 0, WIN_W, WIN_H, (SDL_Color){7, 9, 15, 224});
        }
        ui_header("NPLAY PLAYER", "Proximo episodio", "B Cancelar");
        ui_panel(210, 170, WIN_W - 420, 360, C_ACC2);
        text_draw(gRen, "A SEGUIR", 258, 212, C_ACC2, 0);
        text_clip(jstr(series, "title") ? jstr(series, "title") : "Serie",
                  258, 252, C_TEXT, 1, WIN_W - 516);
        char number[64];
        snprintf(number, sizeof(number), "Temporada %d  |  Episodio %d",
                 jint(episode, "season") > 0 ? jint(episode, "season") : 1,
                 jint(episode, "episode") > 0 ? jint(episode, "episode") : g_epSel + 1);
        text_draw(gRen, number, 258, 306, C_MUT, 0);
        text_clip(ep_display_title(episode), 258, 346, C_TEXT, 0, WIN_W - 516);
        char countdown[80]; snprintf(countdown, sizeof(countdown), "Comecando em %d segundos", remaining);
        text_draw(gRen, countdown, 258, 408, C_MUT, 0);
        fill_rect(258, 456, 300, 54, C_ACC);
        text_center_at("A  Assistir agora", 258, 300, 469, C_TEXT, 0);
        fill_rect(582, 456, 300, 54, C_CARD);
        text_center_at("B  Ficar na lista", 582, 300, 469, C_TEXT, 0);
        SDL_RenderPresent(gRen);
        SDL_Delay(16);
    }
    return 0;
}
static void input_series(int b) {
    if (g_dlmenu) { input_dlmenu(b); return; }   // menu "baixar episodios" aberto
    int nep = ser_nep();
    if (b == JOY_B || b == JOY_MINUS) { detail_return_to_origin(); }
    else if (b == JOY_X) { cJSON *s = ser_obj(); if (s) toggle_fav_series(jint(s, "id")); }
    else if (b == JOY_PLUS) { cJSON *s = ser_obj(); if (s) media_list_prompt_add(jint(s, "id"), 1, jstr(s, "title"), jstr(s, "logo")); }
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
            int ended = resolve_and_play(jint(ep, "id"), ep_display_title(ep));
            if (ended != 1) break;
            int next = idx + 1;
            if (next >= ser_nep()) break;
            g_epSel = next;
            cJSON *next_ep = ser_ep_at(next);
            if (!g_pref_autoplay || !next_ep || !prompt_next_episode(next_ep)) break;
            idx = next;
        }
    }
}
static void input_downloads(int b) {
    if (g_dlView == 0) { // Historico + atalhos para listas
        int nh = arr_len(history_items()), nl = store_media_list_count(), total = nl + 2;
        if (g_history_menu) {
            if (b == JOY_B || b == JOY_MINUS || b == JOY_X) g_history_menu = 0;
            else if (b == JOY_UP && g_history_menu_sel > 0) g_history_menu_sel--;
            else if (b == JOY_DOWN && g_history_menu_sel < 3) g_history_menu_sel++;
            else if (b == JOY_A && g_history_sel < nh) {
                cJSON *item = cJSON_GetArrayItem(history_items(), g_history_sel);
                int id = jint(item, "item_id"), duration = jint(item, "duration_seconds");
                int action = g_history_menu_sel;
                g_history_menu = 0;
                if (action == 0) {
                    resolve_and_play(id, jstr(item, "title"));
                } else if (action == 1) {
                    if (history_set_position(item, 0) == 0) resolve_and_play(id, jstr(item, "title"));
                    else toast("Nao foi possivel reiniciar o progresso");
                } else if (action == 2) {
                    if (duration <= 0) toast("A duracao desta obra ainda e desconhecida");
                    else if (history_set_position(item, duration) == 0) {
                        cJSON_DeleteItemFromArray(history_items(), g_history_sel);
                        int left = arr_len(history_items()); if (g_history_sel >= left) g_history_sel = left > 0 ? left - 1 : 0;
                        toast("Marcado como concluido");
                    }
                    else toast("Nao foi possivel atualizar o Historico");
                } else {
                    if (history_set_position(item, 0) == 0) {
                        cJSON_DeleteItemFromArray(history_items(), g_history_sel);
                        int left = arr_len(history_items()); if (g_history_sel >= left) g_history_sel = left > 0 ? left - 1 : 0;
                        toast("Removido do Historico");
                    }
                    else toast("Nao foi possivel atualizar o Historico");
                }
                load_history();
            }
            return;
        }
        if (b == JOY_UP && g_history_zone == 1 && nh > 0) g_history_zone = 0;
        else if (b == JOY_DOWN && g_history_zone == 0) g_history_zone = 1;
        else if (b == JOY_DLEFT) {
            if (g_history_zone == 0 && g_history_sel > 0) g_history_sel--;
            else if (g_history_zone == 1 && g_list_sel > 0) g_list_sel--;
        } else if (b == JOY_DRIGHT) {
            if (g_history_zone == 0 && g_history_sel + 1 < nh) g_history_sel++;
            else if (g_history_zone == 1 && g_list_sel + 1 < total) g_list_sel++;
        } else if (b == JOY_A) {
            if (g_history_zone == 0 && g_history_sel < nh) {
                cJSON *item = cJSON_GetArrayItem(history_items(), g_history_sel);
                resolve_and_play(jint(item, "item_id"), jstr(item, "title"));
                load_history();
            } else if (g_history_zone == 1) {
                if (g_list_sel == 0) { g_dlView = 2; g_dlSel = 0; g_dlScroll = 0; }
                else if (g_list_sel == total - 1) {
                    char name[48];
                    if (prompt_text("Nome da nova lista", name, sizeof(name), 0) == 0) {
                        int created = store_media_list_create(name);
                        if (created >= 0) { g_open_list = created; g_list_item_sel = 0; g_dlView = 3; }
                        else toast("Nao foi possivel criar a lista");
                    }
                } else { g_open_list = g_list_sel - 1; g_list_item_sel = 0; g_dlView = 3; }
            }
        } else if (g_history_zone == 0 && nh > 0 && b == JOY_X) {
            g_history_menu = 1; g_history_menu_sel = 0;
        } else if (g_history_zone == 1 && g_list_sel > 0 && g_list_sel < total - 1 && b == JOY_Y) {
            if (is_account_watchlater(store_media_list_name(g_list_sel - 1))) {
                toast("Assistir mais tarde acompanha sua conta e mantem este nome"); return;
            }
            char name[48];
            if (prompt_text("Novo nome da lista", name, sizeof(name), 0) == 0)
                store_media_list_rename(g_list_sel - 1, name);
        } else if (g_history_zone == 1 && g_list_sel > 0 && g_list_sel < total - 1 && b == JOY_ZR) {
            if (is_account_watchlater(store_media_list_name(g_list_sel - 1))) {
                toast("Remova os titulos individualmente desta lista"); return;
            }
            char confirm[24];
            if (prompt_text("Digite EXCLUIR para apagar a lista", confirm, sizeof(confirm), 0) == 0 && !strcasecmp(confirm, "EXCLUIR")) {
                store_media_list_delete(g_list_sel - 1);
                if (g_list_sel >= store_media_list_count() + 2) g_list_sel--;
                toast("Lista excluida");
            }
        }
        return;
    }
    if (g_dlView == 3) { // conteudo de uma lista pessoal
        int n = store_media_list_item_count(g_open_list);
        if (b == JOY_B || b == JOY_MINUS) { g_dlView = 0; g_history_zone = 1; g_list_sel = g_open_list + 1; }
        else if (b == JOY_UP && g_list_item_sel - GCOLS >= 0) g_list_item_sel -= GCOLS;
        else if (b == JOY_DOWN && g_list_item_sel + GCOLS < n) g_list_item_sel += GCOLS;
        else if (b == JOY_DLEFT && g_list_item_sel > 0) g_list_item_sel--;
        else if (b == JOY_DRIGHT && g_list_item_sel + 1 < n) g_list_item_sel++;
        else if (b == JOY_A && g_list_item_sel < n) {
            int id = 0, is_series = 0; char title[128], logo[720];
            if (store_media_list_get(g_open_list, g_list_item_sel, &id, &is_series, title, sizeof(title), logo, sizeof(logo))) {
                detail_capture_origin();
                if (is_series) open_series(id);
                else if (open_movie_details(id) == 0) g_screen = SC_MOVIE;
                else toast("Nao foi possivel abrir este titulo");
            }
        } else if (b == JOY_X && g_list_item_sel < n) {
            int id = 0, is_series = 0; char title[128], logo[720];
            if (is_account_watchlater(store_media_list_name(g_open_list)) &&
                store_media_list_get(g_open_list, g_list_item_sel, &id, &is_series,
                                      title, sizeof(title), logo, sizeof(logo)) &&
                sync_watchlater_item("DELETE", id, is_series) != 200) {
                toast("Sem conexao: mantive o titulo para nao perder a sincronizacao"); return;
            }
            store_media_list_remove(g_open_list, g_list_item_sel);
            n = store_media_list_item_count(g_open_list);
            if (g_list_item_sel >= n) g_list_item_sel = n > 0 ? n - 1 : 0;
            toast("Removido da lista");
        } else if (b == JOY_Y) {
            if (is_account_watchlater(store_media_list_name(g_open_list))) {
                toast("Assistir mais tarde acompanha sua conta e mantem este nome"); return;
            }
            char name[48];
            if (prompt_text("Novo nome da lista", name, sizeof(name), 0) == 0) store_media_list_rename(g_open_list, name);
        } else if (b == JOY_ZR) {
            if (is_account_watchlater(store_media_list_name(g_open_list))) {
                toast("Remova os titulos individualmente desta lista"); return;
            }
            char confirm[24];
            if (prompt_text("Digite EXCLUIR para apagar a lista", confirm, sizeof(confirm), 0) == 0 && !strcasecmp(confirm, "EXCLUIR")) {
                store_media_list_delete(g_open_list); g_dlView = 0; g_history_zone = 1; g_list_sel = 0; toast("Lista excluida");
            }
        }
        return;
    }
    if (g_dlView == 1) {   // detalhe: episodios preparados de uma obra
        int g = g_dlGroup, nj = (g < g_dlgN) ? g_dlg[g].nJobs : 0;
        if (b == JOY_B || b == JOY_MINUS) { g_dlView = 2; }
        else if (b == JOY_UP) { if (g_dlDetSel > 0) g_dlDetSel--; }
        else if (b == JOY_DOWN) { if (g_dlDetSel < nj - 1) g_dlDetSel++; }
        else if (b == JOY_A) {   // assistir + auto-play do proximo baixado
            int idx = g_dlDetSel;
            while (idx < g_dlg[g].nJobs) {
                cJSON *j = dlg_job(g, idx);
                if (!cJSON_IsTrue(cJSON_GetObjectItem(j, "ready"))) { toast("Ainda estamos preparando..."); break; }
                g_dlDetSel = idx;
                if (dl_play(j) != 1 || !g_pref_autoplay) break;
                idx++;
            }
        }
        else if (b == JOY_X) { cJSON *j = dlg_job(g, g_dlDetSel); if (j) { accel_remove(jint(j, "item_id")); load_downloads(); toast("Removido"); } }
        return;
    }
    // Biblioteca (g_dlView == 2)
    int n = g_dlgN;
    if (b == JOY_B || b == JOY_MINUS) { g_dlView = 0; g_history_zone = 1; g_list_sel = 0; return; }
    if (b == JOY_UP) { if (g_dlSel - GCOLS >= 0) g_dlSel -= GCOLS; }
    else if (b == JOY_DOWN) { if (g_dlSel + GCOLS < n) g_dlSel += GCOLS; }
    else if (b == JOY_DLEFT) { if (g_dlSel > 0) g_dlSel--; }
    else if (b == JOY_DRIGHT) { if (g_dlSel + 1 < n) g_dlSel++; }
    else if (b == JOY_A) {
        if (g_dlSel < n) {
            if (g_dlg[g_dlSel].isMovie) { cJSON *j = dlg_job(g_dlSel, 0); if (cJSON_IsTrue(cJSON_GetObjectItem(j, "ready"))) dl_play(j); else toast("Ainda estamos preparando..."); }
            else { g_dlGroup = g_dlSel; g_dlDetSel = 0; g_dlDetScroll = 0; g_dlView = 1; load_dl_done(jint(dlg_job(g_dlSel, 0), "series_id")); }
        }
    }
    else if (b == JOY_X) {   // remove a obra inteira
        if (g_dlSel < n) { int g = g_dlSel; for (int k = g_dlg[g].nJobs - 1; k >= 0; k--) { cJSON *j = dlg_job(g, k); if (j) accel_remove(jint(j, "item_id")); } load_downloads(); toast("Removido"); }
    }
    int row = g_dlSel / GCOLS, rowTop = 164 + row * (GCH + GGAP), rowBot = rowTop + GCH;
    if (rowBot - g_dlScroll > WIN_H - 52) g_dlScroll = rowBot - (WIN_H - 52) + 16;
    if (rowTop - g_dlScroll < 164) g_dlScroll = rowTop - 164;
    if (g_dlScroll < 0) g_dlScroll = 0;
}

// ------------------------------------------------------------- config
static const char *SET_ITEMS[] = { "Preferencias do Switch", "Buscar atualizacao", "Reiniciar Nplay", "Fechar Nplay", "Sair da conta" };
#define NSET 5
static int g_setSel = 0;
static int g_diag_open = 0;
static char g_diag_player_lines[6][DIAG_LINE_CAP];
static char g_diag_network_lines[2][DIAG_LINE_CAP];
static int g_diag_player_count = 0;
static int g_diag_network_count = 0;

static void reload_player_diagnostics(void) {
    g_diag_player_count = diag_read_player_tail(g_diag_player_lines, 6);
    g_diag_network_count = diag_read_network_tail(g_diag_network_lines, 2);
}

static int schedule_restart(const char *path) {
    const char *target = (path && path[0]) ? path : g_self_path;
    FILE *installed = fopen(target, "rb");
    if (!installed) {
        snprintf(g_status, sizeof(g_status), "Nao localizei a instalacao do Nplay para reiniciar.");
        return -1;
    }
    fclose(installed);
    if (!envHasNextLoad()) {
        snprintf(g_status, sizeof(g_status), "Reinicio automatico indisponivel neste carregador.");
        return -1;
    }
    Result rc = envSetNextLoad(target, target);
    if (R_FAILED(rc)) {
        snprintf(g_status, sizeof(g_status), "Nao foi possivel reiniciar automaticamente (0x%08x).", (unsigned)rc);
        return -1;
    }
    snprintf(g_status, sizeof(g_status), "Tudo pronto. Reiniciando o Nplay...");
    g_restart_at = SDL_GetTicks() + 1400;
    return 0;
}
static int settings_fetch_thread(void *unused) {
    (void)unused;
    g_account_pending = api_get("/api/account/me");
    if (!g_account_pending) g_account_pending = api_get("/api/auth/me"); // servidor anterior
    SDL_AtomicSet(&g_account_ready, 1);
    g_settings_accel_pending = api_get("/api/accel/status");
    SDL_AtomicSet(&g_settings_done, 1);
    return 0;
}
static void load_settings_status(void) {
    if (g_settings_thread) return;
    if (g_account_pending) { cJSON_Delete(g_account_pending); g_account_pending = NULL; }
    if (g_settings_accel_pending) { cJSON_Delete(g_settings_accel_pending); g_settings_accel_pending = NULL; }
    SDL_AtomicSet(&g_account_ready, 0);
    SDL_AtomicSet(&g_settings_done, 0);
    g_settings_thread = SDL_CreateThread(settings_fetch_thread, "settings-fetch", NULL);
}
static void pump_settings_status(void) {
    if (SDL_AtomicGet(&g_account_ready) && g_account_pending) {
        if (g_account_status) cJSON_Delete(g_account_status);
        g_account_status = g_account_pending; g_account_pending = NULL;
        cJSON *prefs = cJSON_GetObjectItem(g_account_status, "prefs");
        if (prefs) {
            cJSON *v = cJSON_GetObjectItem(prefs, "hideAdult");
            if (cJSON_IsBool(v)) g_pref_hide_adult = cJSON_IsTrue(v);
            v = cJSON_GetObjectItem(prefs, "autoplayNext");
            if (cJSON_IsBool(v)) g_pref_autoplay = cJSON_IsTrue(v);
            v = cJSON_GetObjectItem(prefs, "reduceMotion");
            if (cJSON_IsBool(v)) g_pref_reduce_motion = cJSON_IsTrue(v);
            const char *audio = jstr(prefs, "audioPref");
            g_pref_audio = audio && !strcmp(audio, "leg") ? 1 : audio && !strcmp(audio, "any") ? 2 : 0;
        }
    }
    if (!g_settings_thread || !SDL_AtomicGet(&g_settings_done)) return;
    SDL_WaitThread(g_settings_thread, NULL); g_settings_thread = NULL;
    if (g_settings_accel_pending) {
        if (g_accel_status) cJSON_Delete(g_accel_status);
        g_accel_status = g_settings_accel_pending; g_settings_accel_pending = NULL;
    }
}
static int load_player_boot_stage(char *out, size_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    const char *paths[] = {
        "sdmc:/switch/.nplay-player-boot.txt",
        "sdmc:/switch/Nplay/player_boot.txt",
        "sdmc:/switch/Meruem/player_boot.txt"
    };
    FILE *file = NULL;
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]) && !file; i++)
        file = fopen(paths[i], "rb");
    if (!file) return 0;
    size_t read = fread(out, 1, cap - 1, file);
    fclose(file);
    out[read] = '\0';
    while (read > 0 && (out[read - 1] == '\n' || out[read - 1] == '\r' || out[read - 1] == ' '))
        out[--read] = '\0';
    return read > 0;
}
static void draw_player_diagnostics(void) {
    if (!g_diag_open) return;
    struct player_stats stats;
    char boot_stage[96];
    int has_boot_stage = load_player_boot_stage(boot_stage, sizeof(boot_stage));
    int has_stats = store_load_player_stats(&stats);
    ui_panel(220, 66, 840, 586, C_ACC2);
    text_draw(gRen, "DIAGNOSTICO DA ULTIMA REPRODUCAO", 252, 92, C_ACC2, 0);
    char summary[180];
    if (has_stats) {
        snprintf(summary, sizeof(summary), "Video %dx%d  |  frames %d  |  buffer %d  |  resultado %d",
                 stats.width, stats.height, stats.decoded_frames,
                 stats.buffering_events, stats.playback_error);
        text_clip(summary, 252, 132, stats.playback_error < 0 ? C_ROSE : C_GREEN, 0, 776);
    } else text_draw(gRen, "O trace abaixo sobrevive mesmo quando o aplicativo fecha.", 252, 132, C_TEXT, 0);
    if (has_boot_stage) {
        snprintf(summary, sizeof(summary), "Ultima etapa simples: %s", boot_stage);
        text_clip(summary, 252, 166, C_ACC, 0, 776);
    }

    text_draw(gRen, "RASTRO DO PLAYER", 252, 204, C_MUT, 0);
    if (g_diag_player_count == 0) text_draw(gRen, "Nenhuma tentativa registrada nesta instalacao.", 252, 238, C_TEXT, 0);
    for (int i = 0; i < g_diag_player_count; i++)
        text_clip(g_diag_player_lines[i], 252, 238 + i * 32,
                  i == g_diag_player_count - 1 ? C_ACC : C_TEXT, 0, 776);

    text_draw(gRen, "ULTIMAS REQUISICOES (codigo / tempo / tamanho)", 252, 446, C_MUT, 0);
    if (g_diag_network_count == 0) text_draw(gRen, "Nenhuma requisicao registrada.", 252, 480, C_TEXT, 0);
    for (int i = 0; i < g_diag_network_count; i++)
        text_clip(g_diag_network_lines[i], 252, 480 + i * 32, C_TEXT, 0, 776);

    text_clip("Fotografe esta tela apos reabrir o Nplay. Nenhuma URL assinada ou senha e gravada.",
              252, 558, C_MUT, 0, 776);
    text_center_at("A, B ou X Fechar", 252, 776, 606, C_TEXT, 0);
}
static void draw_preferences(void) {
    if (!g_prefs_open) return;
    ui_panel(220, 84, 840, 552, C_ACC2);
    text_draw(gRen, "PREFERENCIAS DO SWITCH", 260, 116, C_ACC2, 0);
    text_draw(gRen, "Sincronizadas com sua conta Nplay", 260, 150, C_MUT, 0);
    static const char *names[] = { "Ocultar conteudo +18", "Proximo episodio automatico", "Reduzir animacoes", "Audio preferido" };
    static const char *details[] = {
        "Aplica o controle de catalogo da conta.",
        "Continua a serie quando um episodio termina.",
        "Desativa a rotacao automatica dos destaques.",
        "Prioridade usada quando a obra oferece versoes diferentes."
    };
    const char *audio[] = { "Dublado", "Legendado", "Tanto faz" };
    for (int i = 0; i < 4; i++) {
        int y = 190 + i * 88;
        fill_rect(252, y, 776, 72, C_BAR);
        if (i == g_prefs_sel) { ui_focus(248, y - 4, 784, 80); fill_rect(252, y, 5, 72, C_ACC); }
        text_draw(gRen, names[i], 278, y + 9, i == g_prefs_sel ? C_TEXT : C_MUT, 0);
        text_draw(gRen, details[i], 278, y + 39, C_MUT, 0);
        const char *value = i == 0 ? (g_pref_hide_adult ? "Ativado" : "Desativado") :
                            i == 1 ? (g_pref_autoplay ? "Ativado" : "Desativado") :
                            i == 2 ? (g_pref_reduce_motion ? "Ativado" : "Desativado") : audio[g_pref_audio];
        text_right(value, 998, y + 21, i == g_prefs_sel ? C_GREEN : C_MUT, 0);
    }
    text_center_at("A Alterar    Esquerda/direita Escolher audio    B Concluir", 252, 776, 588, C_TEXT, 0);
}
static void save_selected_preference(int direction) {
    int old_hide = g_pref_hide_adult, old_auto = g_pref_autoplay, old_motion = g_pref_reduce_motion, old_audio = g_pref_audio;
    char body[96];
    if (g_prefs_sel == 0) { g_pref_hide_adult = !g_pref_hide_adult; snprintf(body, sizeof(body), "{\"hideAdult\":%s}", g_pref_hide_adult ? "true" : "false"); }
    else if (g_prefs_sel == 1) { g_pref_autoplay = !g_pref_autoplay; snprintf(body, sizeof(body), "{\"autoplayNext\":%s}", g_pref_autoplay ? "true" : "false"); }
    else if (g_prefs_sel == 2) { g_pref_reduce_motion = !g_pref_reduce_motion; snprintf(body, sizeof(body), "{\"reduceMotion\":%s}", g_pref_reduce_motion ? "true" : "false"); }
    else {
        g_pref_audio = (g_pref_audio + (direction < 0 ? 2 : 1)) % 3;
        const char *audio[] = { "dub", "leg", "any" };
        snprintf(body, sizeof(body), "{\"audioPref\":\"%s\"}", audio[g_pref_audio]);
    }
    if (api_send("/api/account/prefs", "PUT", body) != 200) {
        g_pref_hide_adult = old_hide; g_pref_autoplay = old_auto; g_pref_reduce_motion = old_motion; g_pref_audio = old_audio;
        toast("Nao foi possivel salvar a preferencia");
    } else {
        if (g_prefs_sel == 0 && g_tab <= 4) landing_invalidate(g_tab);
        g_hero_next = SDL_GetTicks() + 8000;
        toast("Preferencia sincronizada");
    }
}
static const char *subscription_status_label(const char *status) {
    if (!status || !status[0]) return "Status nao informado";
    if (!strcmp(status, "active")) return "Plano ativo";
    if (!strcmp(status, "trialing") || !strcmp(status, "trial")) return "Periodo de teste";
    if (!strcmp(status, "past_due")) return "Pagamento pendente";
    if (!strcmp(status, "canceled") || !strcmp(status, "cancelled")) return "Plano cancelado";
    if (!strcmp(status, "expired")) return "Plano expirado";
    return status;
}
static void format_subscription_period(cJSON *account, cJSON *subscription, char *out, size_t cap) {
    const char *end = account ? jstr(account, "access_expires_at") : NULL;
    if ((!end || !end[0]) && subscription) end = jstr(subscription, "current_period_end");
    if (!end || !end[0]) { snprintf(out, cap, "Validade sem data definida"); return; }
    int year = 0, month = 0, day = 0;
    if (sscanf(end, "%d-%d-%d", &year, &month, &day) != 3) {
        snprintf(out, cap, "Validade informada pela conta"); return;
    }
    struct tm expiry = {0};
    expiry.tm_year = year - 1900; expiry.tm_mon = month - 1; expiry.tm_mday = day;
    expiry.tm_hour = 23; expiry.tm_min = 59; expiry.tm_sec = 59; expiry.tm_isdst = -1;
    time_t until = mktime(&expiry), now = time(NULL);
    int days = (until != (time_t)-1 && now != (time_t)-1) ? (int)(difftime(until, now) / 86400.0) + 1 : -1;
    if (days > 1) snprintf(out, cap, "Valido ate %02d/%02d/%04d  |  %d dias restantes", day, month, year, days);
    else if (days == 1) snprintf(out, cap, "Valido ate %02d/%02d/%04d  |  ultimo dia", day, month, year);
    else if (days == 0) snprintf(out, cap, "Validade encerra hoje");
    else snprintf(out, cap, "Periodo encerrado em %02d/%02d/%04d", day, month, year);
}
static void draw_settings(void) {
    ui_header("NPLAY", "Configuracoes", "B Voltar");
    char v[96]; snprintf(v, sizeof(v), "Versao %s", APP_VERSION_STR);
    char u[180]; snprintf(u, sizeof(u), "Conta  %s", g_user[0] ? g_user : "-");
    text_draw(gRen, u, 40, 92, C_TEXT, 0);
    text_right(v, WIN_W - 40, 92, C_MUT, 0);
    text_draw(gRen, "CONTA E BIBLIOTECA", 40, 138, C_ACC2, 0);

    ui_panel(40, 174, 580, 170, C_ACC);
    text_draw(gRen, "SUA BIBLIOTECA", 68, 194, C_ACC, 0);
    text_draw(gRen, "Obras preparadas para assistir.", 68, 232, C_TEXT, 0);
    text_draw(gRen, "Disponiveis nos seus aparelhos.", 68, 264, C_MUT, 0);
    if (g_accel_status) {
        int count = jint(g_accel_status, "count");
        char sl[96]; snprintf(sl, sizeof(sl), count == 1 ? "%d obra preparada" : "%d obras preparadas", count);
        text_draw(gRen, sl, 68, 302, C_GREEN, 0);
    } else text_draw(gRen, "Consultando sua conta...", 68, 302, C_MUT, 0);

    ui_panel(644, 174, 596, 170, C_ACC2);
    text_draw(gRen, "SEU PLANO", 672, 194, C_ACC2, 0);
    cJSON *account = g_account_status ? cJSON_GetObjectItem(g_account_status, "user") : NULL;
    if (!account && g_account_status && cJSON_IsObject(g_account_status)) account = g_account_status;
    if (account) {
        cJSON *plan = cJSON_GetObjectItem(account, "plan");
        cJSON *subscription = cJSON_GetObjectItem(account, "subscription");
        const char *plan_name = plan ? jstr(plan, "name") : NULL;
        const char *status = subscription ? jstr(subscription, "status") : NULL;
        if (!status) status = jstr(account, "subscription_status");
        text_clip(plan_name && plan_name[0] ? plan_name : "Plano da conta", 672, 226, C_TEXT, 1, 520);
        text_draw(gRen, subscription_status_label(status), 672, 260,
                  status && (!strcmp(status, "active") || !strcmp(status, "trialing")) ? C_GREEN : C_ACC, 0);
        int screens = plan ? jint(plan, "session_limit") : 0;
        int devices = plan ? jint(plan, "device_limit") : 0;
        if (screens <= 0) screens = jint(account, "max_sessions");
        if (devices <= 0) devices = jint(account, "max_devices");
        int profiles = arr_len(cJSON_GetObjectItem(g_account_status, "profiles"));
        int profile_limit = jint(account, "profile_limit");
        char limits[128];
        if (screens > 0 && devices > 0 && profile_limit > 0) snprintf(limits, sizeof(limits), "%d tela%s  |  %d dispositivo%s  |  %d/%d perfis", screens, screens == 1 ? "" : "s", devices, devices == 1 ? "" : "s", profiles, profile_limit);
        else if (screens > 0 && devices > 0) snprintf(limits, sizeof(limits), "%d tela%s simultanea%s  |  %d dispositivo%s", screens, screens == 1 ? "" : "s", screens == 1 ? "" : "s", devices, devices == 1 ? "" : "s");
        else if (screens > 0) snprintf(limits, sizeof(limits), "%d tela%s simultanea%s", screens, screens == 1 ? "" : "s", screens == 1 ? "" : "s");
        else snprintf(limits, sizeof(limits), "Limites gerenciados pela sua conta");
        text_draw(gRen, limits, 672, 288, C_MUT, 0);
        char period[160]; format_subscription_period(account, subscription, period, sizeof(period));
        text_draw(gRen, period, 672, 316, C_MUT, 0);
    } else {
        text_draw(gRen, SDL_AtomicGet(&g_settings_done) ? "Plano indisponivel agora" : "Consultando seu plano...", 672, 238, C_TEXT, 0);
        text_draw(gRen, "Tente abrir as configuracoes novamente.", 672, 278, C_MUT, 0);
    }

    text_draw(gRen, "ACOES", 40, 366, C_ACC2, 0);
    for (int i = 0; i < NSET; i++) {
        int y = 390 + i * 48;
        fill_rect(260, y, 760, 44, C_CARD);
        if (i == g_setSel) { ui_focus(256, y - 4, 768, 52); fill_rect(260, y, 4, 44, C_ACC2); }
        text_draw(gRen, SET_ITEMS[i], 286, y + 7, (i == g_setSel) ? C_TEXT : C_MUT, 0);
        text_right(i == 0 ? "A Abrir" : i == 1 ? "A Verificar" : "A Confirmar", 994, y + 7, C_MUT, 0);
    }
    if (g_status[0]) text_center_at(g_status, 120, WIN_W - 240, 632, C_ACC, 0);
    ui_footer("Cima/baixo Navegar    A Confirmar    X Diagnostico do player    B Voltar");
    draw_player_diagnostics();
    draw_preferences();
}
static void input_settings(int b) {
    if (g_prefs_open) {
        if (b == JOY_B || b == JOY_MINUS) { g_prefs_open = 0; return; }
        if (b == JOY_UP && g_prefs_sel > 0) g_prefs_sel--;
        else if (b == JOY_DOWN && g_prefs_sel < 3) g_prefs_sel++;
        else if (b == JOY_A) save_selected_preference(1);
        else if (g_prefs_sel == 3 && b == JOY_DLEFT) save_selected_preference(-1);
        else if (g_prefs_sel == 3 && b == JOY_DRIGHT) save_selected_preference(1);
        return;
    }
    if (g_diag_open) {
        if (b == JOY_A || b == JOY_B || b == JOY_MINUS || b == JOY_X) g_diag_open = 0;
        return;
    }
    if (b == JOY_B || b == JOY_MINUS) { g_screen = SC_MAIN; return; }
    if (b == JOY_X) { reload_player_diagnostics(); g_diag_open = 1; return; }
    if (b == JOY_UP) { if (g_setSel > 0) g_setSel--; }
    else if (b == JOY_DOWN) { if (g_setSel < NSET - 1) g_setSel++; }
    else if (b == JOY_A) {
        if (g_setSel == 0) { g_prefs_sel = 0; g_prefs_open = 1; }
        else if (g_setSel == 1) { snprintf(g_status, sizeof(g_status), "Verificando atualizacao..."); g_do_update = 1; }
        else if (g_setSel == 2) schedule_restart(NULL);
        else if (g_setSel == 3) g_running = 0;
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
        else if (updated > 0) {
            snprintf(g_status, sizeof(g_status), "v%s instalada. Preparando o reinicio...", info.latest_version);
            schedule_restart(installed[0] ? installed : target);
        }
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
        else if (b == JOY_MINUS) { g_setSel = 0; load_settings_status(); g_screen = SC_CONFIG; }
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
    diag_init();
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
    // Catalogo primeiro: a consulta de conta/configuracoes so e iniciada quando
    // o usuario abre Config. Isso evita duas requisicoes HTTPS concorrentes no
    // boot, um ponto especialmente caro no limite de memoria/rede do Switch.
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
        if (!g_pref_reduce_motion && g_screen == SC_MAIN && g_tab <= 4 && g_land && SDL_GetTicks() > g_hero_next) {
            int nh = hero_count();
            if (nh > 0) g_heroIdx = (g_heroIdx + 1) % nh;
            g_hero_next = SDL_GetTicks() + 6000;
        }
        // atualiza a lista de downloads sozinho
        if (g_screen == SC_MAIN && g_tab == TAB_DOWNLOADS && SDL_GetTicks() > g_dl_next) {
            load_downloads(); g_dl_next = SDL_GetTicks() + 2000;
        }
        pump_downloads();
        pump_history();
        pump_settings_status();
        pump_landing();
        update_download_awake();
        // Aplique criacoes/expulsoes do cache antes de enfileirar o desenho.
        // Assim nenhuma textura usada neste frame e destruida antes do Present.
        cover_pump();

        SDL_SetRenderDrawColor(gRen, C_BG.r, C_BG.g, C_BG.b, 255);
        SDL_RenderClear(gRen);
        if (g_screen == SC_LOGIN) draw_login();
        else if (g_screen == SC_CONFIG) draw_settings();
        else if (g_screen == SC_MOVIE) draw_movie();
        else if (g_screen == SC_SERIES) { if (g_dlmenu) draw_dlmenu(); else draw_series(); }
        else if (g_screen == SC_SEARCH) draw_search();
        else { if (g_tab == TAB_DOWNLOADS) draw_downloads(); else draw_landing(); }

        if (g_toast[0] && SDL_GetTicks() < g_toast_until) {
            int w = 0, h = 0;
            SDL_Texture *tx = text_cached(gRen, g_toast, C_TEXT, 0, &w, &h);
            fill_rect(WIN_W / 2 - w / 2 - 18, WIN_H - 120, w + 36, h + 20, C_BAR);
            fill_rect(WIN_W / 2 - w / 2 - 18, WIN_H - 120, 4, h + 20, C_ACC);
            if (tx) { SDL_Rect d = { WIN_W / 2 - w / 2, WIN_H - 110, w, h }; SDL_RenderCopy(gRen, tx, NULL, &d); }
        }
        SDL_RenderPresent(gRen);
        if (g_do_update) { g_do_update = 0; run_update(); }
        if (g_restart_at && SDL_GetTicks() >= g_restart_at) g_running = 0;
    }

    g_run = 0;
    for (int i = 0; i < 3; i++) SDL_SemPost(g_q_sem);
    for (int i = 0; i < 3; i++) SDL_WaitThread(wk[i], NULL);
    if (g_dl_thread) { SDL_WaitThread(g_dl_thread, NULL); g_dl_thread = NULL; }
    if (g_dl_pending) { cJSON_Delete(g_dl_pending); g_dl_pending = NULL; }
    if (g_history_thread) { SDL_WaitThread(g_history_thread, NULL); g_history_thread = NULL; }
    if (g_watchlater_thread) { SDL_WaitThread(g_watchlater_thread, NULL); g_watchlater_thread = NULL; }
    if (g_history_pending) { cJSON_Delete(g_history_pending); g_history_pending = NULL; }
    if (g_watchlater_pending) { cJSON_Delete(g_watchlater_pending); g_watchlater_pending = NULL; }
    if (g_settings_thread) { SDL_WaitThread(g_settings_thread, NULL); g_settings_thread = NULL; }
    if (g_account_pending) { cJSON_Delete(g_account_pending); g_account_pending = NULL; }
    if (g_settings_accel_pending) { cJSON_Delete(g_settings_accel_pending); g_settings_accel_pending = NULL; }

    if (g_download_awake) { appletSetMediaPlaybackState(false); g_download_awake = 0; }
    if (g_land_thread) { SDL_WaitThread(g_land_thread, NULL); g_land_thread = NULL; }
    if (g_land_pending) { cJSON_Delete(g_land_pending); g_land_pending = NULL; }
    for (int i = 0; i < 5; i++) {
        if (g_land_cache[i]) { cJSON_Delete(g_land_cache[i]); g_land_cache[i] = NULL; }
    }
    g_land = NULL;
    if (g_search) cJSON_Delete(g_search);
    if (g_dl) cJSON_Delete(g_dl);
    if (g_history) cJSON_Delete(g_history);
    if (g_ser) cJSON_Delete(g_ser);
    if (g_accel_status) cJSON_Delete(g_accel_status);
    if (g_account_status) cJSON_Delete(g_account_status);
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
    diag_exit();
    IMG_Quit(); SDL_Quit(); socketExit();
    return 0;
}
