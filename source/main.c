// Nplay Switch - app homebrew do Nplay para Nintendo Switch.
// Abas com RAILS por secao (como o app de PC): Inicio, Filmes, Series, Animes,
// Catalogos ativos com hero + Lancamentos + Minha lista + prateleiras por genero,
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
#include "catalog_fetch.h"
#include "episode_flow.h"
#include "brand_bin.h"
#include "curl_avio.h"

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
static SDL_Texture *g_brand = NULL;
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
// O player e o catalogo disputam a mesma heap do processo. Durante a abertura
// de HLS, suspendemos capas para que workers nao decodifiquem JPEG/WebP enquanto
// o FFmpeg cria demuxers, playlists e decoders.
static SDL_atomic_t g_cover_suspended;

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
    if (f >= 0 && g_cov[f].state == 0 && !SDL_AtomicGet(&g_cover_suspended)) {
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
        if (SDL_AtomicGet(&g_cover_suspended)) {
            SDL_LockMutex(g_cov_mtx);
            if (g_cov[idx].state == 1) g_cov[idx].state = 0;
            SDL_UnlockMutex(g_cov_mtx);
            continue;
        }
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
        int discard = 0;
        SDL_LockMutex(g_cov_mtx);
        if (SDL_AtomicGet(&g_cover_suspended)) {
            g_cov[idx].state = 0; discard = 1;
        } else if (s) {
            g_cov[idx].surf = s; g_cov[idx].state = 2;
        } else g_cov[idx].state = 3;
        SDL_UnlockMutex(g_cov_mtx);
        if (discard) {
            if (s) SDL_FreeSurface(s);
            continue;
        }
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
    fill_rect(x, y, cw, coverH + 52, selected ? (SDL_Color){38, 34, 61, 255} : C_CARD);
    if (selected) ui_focus(x - 4, y - 4, cw + 8, coverH + 60);
    SDL_Rect cr = { x, y, cw, coverH };
    // Poster vertical precisa permanecer inteiro, inclusive o texto da arte.
    if (tex) ui_contain(tex, &cr);
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
    text_clip(title, x + 8, y + coverH + 7, C_TEXT, 0, cw - 16);
    const char *kind_label = kind && !strcmp(kind, "movie") ? "Filme" : "Serie";
    text_clip(kind_label, x + 8, y + coverH + 32, C_MUT, 2, cw - 16);
}

// ============================================================= estado / telas
Screen g_screen = SC_LOGIN;
static Screen g_detail_return = SC_MAIN;

// Config saiu da barra de abas -> abre pelo botao (-). Assim L a partir do
// Inicio ja cai em Baixados (ultima aba).
#define TAB_HOME 0
#define TAB_SAGAS 4
#define TAB_DOWNLOADS 5
#define NTABS 6
#define SEARCH_FILTERS 4
static const char *TAB_NAME[] = { "Inicio", "Filmes", "Series", "Animes", "Sagas", "Historico" };
static int g_tab = 0;

// --- landing (rails) das abas 0..4 ---
static cJSON *g_land = NULL;          // root JSON da aba atual (home / tab-home / anime-home)
static cJSON *g_land_cache[5] = {0};  // troca de aba instantanea depois do 1o carregamento
static cJSON *g_land_pending = NULL;
static SDL_Thread *g_land_thread = NULL;
static SDL_atomic_t g_land_done;
static int g_land_fetch_tab = -1, g_land_queued_tab = -1;
static char g_land_error[192] = "";
static cJSON *g_heroesArr = NULL;     // array (dentro de g_land) usado no destaque
static int g_heroSeriesDefault = 1;   // hero abre como serie? (Filmes = 0)
typedef struct { char label[48]; cJSON *arr; int count; int is_series; } Rail;
static Rail g_rails[48]; static int g_railsN = 0;
static int g_railSel = 0, g_railItem = 0, g_homeScroll = 0;
static int g_heroIdx = 0; static Uint32 g_hero_next = 0;
static int hero_count(void) { int n = arr_len(g_heroesArr); return n > 8 ? 8 : n; }
static int g_saga_sel = 0, g_saga_variant_sel = 0, g_saga_scroll = 0;
static cJSON *g_saga_detail = NULL;
static int g_saga_item_sel = 0;

// --- busca ---
static cJSON *g_search = NULL;
static int g_search_counts[SEARCH_FILTERS] = {0};
static int g_search_counts_valid = 0;
static char g_srchQuery[128] = {0};
static int g_srchSel = 0, g_srchScroll = 0, g_srchFilter = 0;
typedef enum { FETCH_NONE, FETCH_MOVIE, FETCH_RELATED, FETCH_SERIES, FETCH_SEARCH, FETCH_PROFILES, FETCH_SAGA, FETCH_AVATARS } FetchKind;
typedef struct {
    FetchKind kind;
    Screen origin;
    char path[512];
    char query[128];
} FetchIntent;
static CatalogFetch g_fetch = {0};
static FetchIntent g_fetch_current = {0}, g_fetch_queued = {0};
static int g_fetch_discard = 0;
static cJSON *g_profiles = NULL;
static int g_profile_sel = 0, g_profile_id = 0, g_profile_required = 0;
static Screen g_profiles_return = SC_CONFIG;
static cJSON *g_avatar_catalog = NULL;
static cJSON *g_avatar_items[256];
static int g_avatar_item_n = 0;
static cJSON *g_avatar_pending = NULL;
static SDL_Thread *g_avatar_thread = NULL;
static SDL_atomic_t g_avatar_done;
static Uint32 g_avatar_due = 0;
static int g_avatar_attempted = 0, g_avatar_picker_await = 0;
static int g_profile_menu = 0, g_profile_menu_sel = 0;
static int g_profile_editor = 0, g_profile_edit_sel = 0, g_profile_edit_id = 0;
static Screen g_profile_editor_return = SC_CONFIG;
static int g_avatar_picker = 0, g_avatar_sel = 0, g_avatar_page = 0;
static int g_profile_delete_confirm = 0;

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
static CatalogFetch g_dl_done_fetch = {0};
static int g_dl_done_requested = 0, g_dl_done_inflight = 0, g_dl_done_profile = 0;
// status de armazenamento (aba config)
static cJSON *g_accel_status = NULL;
static cJSON *g_account_status = NULL;
static cJSON *g_settings_accel_pending = NULL, *g_account_pending = NULL;
static SDL_Thread *g_settings_thread = NULL;
static SDL_atomic_t g_account_ready, g_settings_done;
static int g_pref_hide_adult = 1, g_pref_autoplay = 1, g_pref_reduce_motion = 0;
static int g_pref_audio = 0; // 0=dublado, 1=legendado, 2=tanto faz
static int g_prefs_sel = 0;
static int g_settings_section = 0, g_settings_focus = 1;
static Screen g_settings_return = SC_MAIN;
// menu "baixar episodios" (Y no detalhe da serie)
static int g_dlmenu = 0;
static char g_epChk[512]; static int g_epChkN = 0;

// --- favoritos (set local p/ togglar rapido) ---
static int g_favItem[512]; static int g_favItemN = 0;
static int g_favSeries[512]; static int g_favSeriesN = 0;
static CatalogFetch g_favs_fetch = {0};
static int g_favs_profile_id = 0;

// --- serie (detalhe) ---
static cJSON *g_ser = NULL;
static int g_seasonIdx = 0, g_epSel = 0, g_epScroll = 0;
static struct {
    int active;
    int series_id;
    int finished_item_id;
    int first_in_group;
} g_episode_pending = {0};
static char g_ser_plot_lines[3][220];
static int g_ser_plot_count = 0;
static char g_ep_plot_lines[24][220];
static int g_ep_plot_count = 0, g_ep_plot_scroll = 0, g_ep_plot_id = -1;
static void rebuild_series_plot(void);

// --- prototipos (funcoes que se chamam entre si) ---
static void enter_tab(int tab);
static void load_landing(int tab);
static void load_downloads(void);
static void load_history(void);
static cJSON *history_items(void);
static int accel_start(int itemId);
static int accel_wait_and_play(int itemId, const char *title);
static int hot_wait_for_stream(int itemId, int sourceId, const char *title,
                               PlaybackSource *out);
static void do_search(void);
static void open_series(int id);
int resolve_and_play(int itemId, const char *title);
static int play_with_progress(int itemId, const char *title, const char *url, int is_hls);
static void mark_episode_completed_in_detail(int item_id);
static int choose_next_episode(int series_id, int finished_item_id, int first_in_group,
                               int allow_refresh, char *title, size_t title_cap);
static void play_episode_sequence(int item_id, int series_id, const char *title);

// Detalhes sao modais sobre a tela que os abriu. Pesquisa, landing e listas
// permanecem em memoria; voltar apenas restaura a tela anterior e sua selecao.
void detail_capture_origin(void) {
    if (g_screen == SC_SEARCH || g_screen == SC_MAIN || g_screen == SC_SAGA) g_detail_return = g_screen;
    else g_detail_return = SC_MAIN;
}
void detail_return_to_origin(void) {
    g_screen = g_detail_return;
    g_detail_return = SC_MAIN;
    // A landing e liberada antes do player para reservar memoria ao FFmpeg.
    // Ao voltar, recarrega apenas a aba realmente visivel e em segundo plano.
    if (g_screen == SC_MAIN && g_tab <= TAB_SAGAS && !g_land) load_landing(g_tab);
}

// ------------------------------------------------------------- favoritos
static int idx_of(int *arr, int n, int v) { for (int i = 0; i < n; i++) if (arr[i] == v) return i; return -1; }
static int catalog_item_is_series(cJSON *item, int fallback) {
    const char *kind = item ? jstr(item, "kind") : NULL;
    if (!kind) kind = item ? jstr(item, "hero_type") : NULL;
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
    if (g_favs_fetch.thread) return;
    g_favItemN = g_favSeriesN = 0;
    g_favs_profile_id = net_get_profile_id();
    if (catalog_fetch_start(&g_favs_fetch, "/api/sync/favorites", g_token) != 0)
        toast("Nao foi possivel sincronizar Minha lista");
}
static void pump_favs(void) {
    cJSON *j = NULL;
    if (!catalog_fetch_take(&g_favs_fetch, &j, NULL, 0)) return;
    if (!j) return;
    if (g_favs_profile_id != net_get_profile_id()) { cJSON_Delete(j); return; }
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
    if (g_favs_fetch.thread) { toast("Sincronizando Minha lista..."); return; }
    char body[48]; snprintf(body, sizeof(body), "{\"item_id\":%d}", id);
    int i = idx_of(g_favItem, g_favItemN, id);
    if (i >= 0) { api_send("/api/sync/favorites", "DELETE", body); g_favItem[i] = g_favItem[--g_favItemN]; toast("Removido da Minha lista"); }
    else { long c = api_send("/api/sync/favorites", "POST", body); if (c == 200) { if (g_favItemN < 512) g_favItem[g_favItemN++] = id; toast("Adicionado a Minha lista"); } else toast("Nao foi possivel favoritar"); }
}
static void toggle_fav_series(int id) {
    if (g_favs_fetch.thread) { toast("Sincronizando Minha lista..."); return; }
    char body[48]; snprintf(body, sizeof(body), "{\"series_id\":%d}", id);
    int i = idx_of(g_favSeries, g_favSeriesN, id);
    if (i >= 0) { api_send("/api/sync/favorites", "DELETE", body); g_favSeries[i] = g_favSeries[--g_favSeriesN]; toast("Removido da Minha lista"); }
    else { long c = api_send("/api/sync/favorites", "POST", body); if (c == 200) { if (g_favSeriesN < 512) g_favSeries[g_favSeriesN++] = id; toast("Adicionado a Minha lista"); } else toast("Nao foi possivel favoritar"); }
}

// ------------------------------------------------------------- landing (rails)
static void add_rail(const char *label, cJSON *arr, int is_series) {
    int count = arr_len(arr);
    if (count == 0 || g_railsN >= 48) return;
    strncpy(g_rails[g_railsN].label, label ? label : "Categoria", 47); g_rails[g_railsN].label[47] = '\0';
    g_rails[g_railsN].arr = arr; g_rails[g_railsN].count = count;
    g_rails[g_railsN].is_series = is_series;
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
static void hero_pool_add_ready(cJSON *pool, cJSON *items) {
    if (!pool || !cJSON_IsArray(items)) return;
    cJSON *item;
    cJSON_ArrayForEach(item, items) {
        if (arr_len(pool) >= 8) break;
        if (!(cJSON_IsTrue(cJSON_GetObjectItem(item, "r2_ready")) || jint(item, "r2_ready")) || !jstr(item, "logo")) continue;
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
    if (tab == TAB_SAGAS) {
        int groups = arr_len(cJSON_GetObjectItem(g_land, "sagas"));
        if (g_saga_sel >= groups) g_saga_sel = groups > 0 ? groups - 1 : 0;
        return;
    }

    if (tab == 0) {
        g_heroesArr = cJSON_GetObjectItem(g_land, "heroes");
        add_rail("Jogos do dia",         cJSON_GetObjectItem(g_land, "jogos"), 0);
        add_rail("Continuar assistindo", cJSON_GetObjectItem(g_land, "continue"), 1);
        add_rail("Filmes em alta",       cJSON_GetObjectItem(g_land, "trendingMovies"), 0);
        add_rail("Series em alta",       cJSON_GetObjectItem(g_land, "trendingSeries"), 1);
        add_rail("Filmes recentes",     cJSON_GetObjectItem(g_land, "recentMovies"), 0);
        add_rail("Series atualizadas",  cJSON_GetObjectItem(g_land, "recentSeries"), 1);
        add_rail("Animes recentes",     cJSON_GetObjectItem(g_land, "recentAnimes"), 1);
        cJSON *sh = cJSON_GetObjectItem(g_land, "readyMovieShelves"), *e;
        if (!sh) sh = cJSON_GetObjectItem(g_land, "movieShelves");
        cJSON_ArrayForEach(e, sh) {
            char label[48]; snprintf(label, sizeof(label), "Filmes: %s", jstr(e, "title") ? jstr(e, "title") : "Outros");
            add_rail(label, cJSON_GetObjectItem(e, "items"), 0);
        }
        sh = cJSON_GetObjectItem(g_land, "readySeriesShelves");
        cJSON_ArrayForEach(e, sh) {
            char label[48]; snprintf(label, sizeof(label), "Series: %s", jstr(e, "title") ? jstr(e, "title") : "Outras");
            add_rail(label, cJSON_GetObjectItem(e, "items"), 1);
        }
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
    } else {                 // tab-home (movie/series)
        int is_series = (tab != 1);
        cJSON_DeleteItemFromObject(g_land, "_switchHeroes");
        g_heroesArr = cJSON_CreateArray();
        // O site prioriza os destaques editoriais que ja estao prontos no R2.
        hero_pool_add_ready(g_heroesArr, cJSON_GetObjectItem(g_land, "featured"));
        if (arr_len(g_heroesArr) == 0) {
            hero_pool_add_ready(g_heroesArr, cJSON_GetObjectItem(g_land, "prontos"));
            hero_pool_add_ready(g_heroesArr, cJSON_GetObjectItem(g_land, "recent"));
        }
        cJSON_AddItemToObject(g_land, "_switchHeroes", g_heroesArr);
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
        case TAB_SAGAS: return "/api/catalog/sagas";
        default: return "/api/catalog/home";
    }
}

// Chamado exclusivamente pela thread principal antes do player. Esvaziar as
// filas e destruir texturas aqui e seguro: nenhum desenho do catalogo ocorre
// enquanto player_run controla o renderer. Workers ja em HTTP descartam o
// resultado ao observar g_cover_suspended.
static void cover_suspend_and_release(void) {
    SDL_AtomicSet(&g_cover_suspended, 1);

    SDL_LockMutex(g_cov_mtx);
    SDL_LockMutex(g_q_mtx);
    while (g_qn > 0) {
        int idx = g_q[g_qh];
        g_qh = (g_qh + 1) % MAX_COV; g_qn--;
        if (idx >= 0 && idx < g_covN && g_cov[idx].state == 1)
            g_cov[idx].state = 0;
    }
    g_qh = g_qt = 0;
    SDL_UnlockMutex(g_q_mtx);

    for (int i = 0; i < g_covN; i++) {
        if (g_cov[i].tex) {
            SDL_DestroyTexture(g_cov[i].tex);
            g_cov[i].tex = NULL;
        }
        if (g_cov[i].surf) {
            SDL_FreeSurface(g_cov[i].surf);
            g_cov[i].surf = NULL;
        }
        if (g_cov[i].state == 2 || g_cov[i].state == 3) g_cov[i].state = 0;
    }
    g_cov_texN = 0;
    SDL_UnlockMutex(g_cov_mtx);

    SDL_LockMutex(g_ready_mtx);
    g_rh = g_rt = g_rn = 0;
    SDL_UnlockMutex(g_ready_mtx);
}

static void cover_resume_after_playback(void) {
    SDL_AtomicSet(&g_cover_suspended, 0);
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
    if (tab < 0 || tab > TAB_SAGAS) return;
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
    if (tab < 0 || tab > TAB_SAGAS) return;
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
    if (received && tab >= 0 && tab <= TAB_SAGAS) {
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
    if (queued >= 0 && queued <= TAB_SAGAS && !g_land_cache[queued]) {
        landing_start(queued);
        return;
    }
}

// Reserva memoria para o pico previsivel da abertura HLS. Preservamos busca,
// detalhe e historico para manter o retorno contextual; somente landings grandes
// e recursos visuais reconstruiveis sao descartados.
static void playback_memory_enter(void) {
    cover_suspend_and_release();
    g_land = NULL; g_heroesArr = NULL; g_railsN = 0;
    for (int i = 0; i <= TAB_SAGAS; i++) {
        if (g_land_cache[i]) {
            cJSON_Delete(g_land_cache[i]);
            g_land_cache[i] = NULL;
        }
    }
}

static void playback_memory_leave(void) {
    cover_resume_after_playback();
    // O player descarta as landings para liberar memoria ao HLS. Recarregue a
    // aba atual assim que ele termina, enquanto o detalhe ainda esta aberto:
    // esperar o usuario apertar B produzia uma tela vazia de "Carregando".
    if (g_tab <= TAB_SAGAS && !g_land) load_landing(g_tab);
}

typedef struct { int item_id, saved, completed; } PlaybackSyncStatus;
static void on_player_progress(int item_id, int pos, int dur, void *u) {
    int saved = api_playback_progress(item_id, pos, dur) == 0;
    PlaybackSyncStatus *status = (PlaybackSyncStatus *)u;
    if (status) {
        status->item_id = item_id;
        status->saved = saved;
        status->completed = saved && dur > 0 && (double)pos / dur >= 0.92;
    }
}

// HLS pode chegar ao EOF sem duracao conhecida. Nesse caso, /progress deixa
// o item em Continuar assistindo mesmo apos a reproducao completa.
static int finalize_natural_playback(int item_id, const PlayerResult *result,
                                     PlaybackSyncStatus *sync) {
    if (!result || result->reason != EXIT_REASON_NATURAL || !result->presented_frame) return 0;
    if (sync && sync->completed && sync->item_id == item_id) return 1;
    if (item_id <= 0 || result->position <= 5) return 0;
    int saved = api_mark_watched(item_id) == 0;
    if (sync) {
        sync->item_id = item_id;
        sync->saved = saved;
        sync->completed = saved;
    }
    return saved;
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
            if (event.type == SDL_FINGERDOWN) {
                int x = (int)(event.tfinger.x * WIN_W);
                int y = (int)(event.tfinger.y * WIN_H);
                if (y >= 370 && y < 428) {
                    if (x >= 282 && x < 582) return 1;
                    if (x >= 606 && x < 906) return 0;
                }
                continue;
            }
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
    char stable_title[256];
    snprintf(stable_title, sizeof(stable_title), "%s", title && title[0] ? title : "Video");
    double start = 0;
    int completed = 0;
    char p[96]; snprintf(p, sizeof(p), "/api/sync/progress/%d", itemId);
    cJSON *pr = api_get_timeout(p, 2L, 5L);
    if (pr) {
        cJSON *prog = cJSON_GetObjectItem(pr, "progress");
        cJSON *ps = prog ? cJSON_GetObjectItem(prog, "position_seconds") : NULL;
        if (ps && cJSON_IsNumber(ps)) start = ps->valuedouble;
        completed = prog ? jint(prog, "completed") : 0;
        cJSON_Delete(pr);
    }
    if (!completed && start > 10) {
        int choice = prompt_resume_playback(stable_title, (int)start);
        if (choice < 0) return 0;
        if (choice == 0) start = 0;
    }
    
    PlayerRequest req = {0};
    req.item_id = itemId;
    req.session_id = 0; // Local ou arquivo direto
    req.title = stable_title;
    req.url = url;
    req.start_sec = start;
    req.progress_cb = on_player_progress;
    req.heartbeat_cb = NULL;
    // Sem renew_cb pois nao e uma stream resolvida via API.
    PlaybackSyncStatus sync = {0};
    req.userdata = &sync;

    playback_memory_enter();
    appletSetMediaPlaybackState(true);
    PlayerResult res = {0};
    int run_rc = player_run(gRen, g_joy, &req, &res);
    appletSetMediaPlaybackState(false);
    playback_memory_leave();
    g_download_awake = 0;
    if (finalize_natural_playback(itemId, &res, &sync)) {
        mark_episode_completed_in_detail(itemId);
        if (g_tab == TAB_DOWNLOADS && g_dlView == 1 && g_dlDoneN < 256) {
            int known = 0;
            for (int i = 0; i < g_dlDoneN; i++) if (g_dlDone[i] == itemId) known = 1;
            if (!known) g_dlDone[g_dlDoneN++] = itemId;
        }
    }

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

typedef struct {
    int item_id, rc;
    PlaybackSource source;
    char error[192];
    SDL_atomic_t done, cancel;
} ResolvePoll;

static int resolve_open_thread(void *userdata) {
    ResolvePoll *poll = (ResolvePoll *)userdata;
    poll->rc = api_resolve_playback_cancel(poll->item_id, NULL,
                                            &poll->cancel, &poll->source);
    if (poll->rc != 0) snprintf(poll->error, sizeof(poll->error), "%s", api_last_error());
    SDL_AtomicSet(&poll->done, 1);
    return 0;
}

// O remux em tempo real nao oferece Range. Se a pessoa voltar antes de o R2
// ficar pronto, deixe claro que esta fonte so consegue recomecar.
static int prompt_sequential_restart(const char *title) {
    Uint32 deadline = SDL_GetTicks() + 4000u;
    while (g_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) { g_running = 0; return 0; }
            if (event.type == SDL_JOYBUTTONDOWN) {
                if (event.jbutton.button == JOY_A) return 1;
                if (event.jbutton.button == JOY_B || event.jbutton.button == JOY_MINUS) return 0;
            }
            if (event.type == SDL_FINGERDOWN) {
                if (event.tfinger.y > 0.52f && event.tfinger.y < 0.67f) return 1;
                if (event.tfinger.x > 0.80f && event.tfinger.y < 0.15f) return 0;
            }
        }
        Uint32 now = SDL_GetTicks();
        if ((Sint32)(deadline - now) <= 0) return 1;
        SDL_SetRenderDrawColor(gRen, C_BG.r, C_BG.g, C_BG.b, 255);
        SDL_RenderClear(gRen);
        ui_header("NPLAY PLAYER", "Retomada indisponivel nesta fonte", "B Voltar");
        ui_panel(238, 188, WIN_W - 476, 300, C_ACC2);
        text_clip(title && title[0] ? title : "Sua obra", 282, 240, C_TEXT, 1, WIN_W - 564);
        text_clip("Esta fonte ainda esta sendo preparada. A retomada volta quando chegar ao R2.",
                  282, 310, C_MUT, 0, WIN_W - 564);
        fill_rect(282, 375, 320, 58, C_ACC);
        text_center_at("A  Comecar do inicio", 282, 320, 390, C_TEXT, 0);
        text_draw(gRen, "B  Voltar", 650, 391, C_MUT, 0);
        SDL_RenderPresent(gRen);
        SDL_Delay(16);
    }
    return 0;
}

// A resolucao da API pode incluir uma fonte externa. Renderiza a espera e
// permite cancelar sem deixar a tela de abertura congelada por ate 20 s.
static int resolve_open_with_animation(int itemId, const char *title, PlaybackSource *out) {
    ResolvePoll poll = { .item_id = itemId };
    SDL_Thread *thread = SDL_CreateThread(resolve_open_thread, "resolve-play", &poll);
    if (!thread) { toast("Nao foi possivel abrir a fonte"); return -1; }
    int cancelled = 0;
    appletSetMediaPlaybackState(true);
    while (g_running && !SDL_AtomicGet(&poll.done)) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) { g_running = 0; cancelled = 1; break; }
            if (event.type == SDL_JOYBUTTONDOWN &&
                (event.jbutton.button == JOY_B || event.jbutton.button == JOY_MINUS)) {
                cancelled = 1; break;
            }
            if (event.type == SDL_FINGERDOWN && event.tfinger.x > 0.80f &&
                event.tfinger.y < 0.15f) { cancelled = 1; break; }
        }
        if (cancelled) { SDL_AtomicSet(&poll.cancel, 1); break; }
        SDL_SetRenderDrawColor(gRen, C_BG.r, C_BG.g, C_BG.b, 255);
        SDL_RenderClear(gRen);
        ui_header("NPLAY", "Abrindo video", "B Cancelar");
        ui_popcorn_draw(gRen, WIN_W / 2, 110, 170);
        text_center_at(title && title[0] ? title : "Video", 160, WIN_W - 320, 338, C_TEXT, 1);
        text_center("Buscando a melhor fonte...", 394, C_MUT, 0);
        SDL_RenderPresent(gRen);
        SDL_Delay(16);
    }
    SDL_WaitThread(thread, NULL);
    appletSetMediaPlaybackState(false);
    if (!g_running || cancelled || SDL_AtomicGet(&poll.cancel)) return -2;
    if (poll.rc != 0) {
        toast(poll.error[0] ? poll.error : "Falha ao abrir a fonte");
        return -1;
    }
    *out = poll.source;
    return 0;
}

// Resolve a fonte e reproduz usando a maquina de estados e PlayerRequest.
int resolve_and_play(int itemId, const char *title) {
    char stable_title[256];
    snprintf(stable_title, sizeof(stable_title), "%s", title && title[0] ? title : "Video");
    PlaybackSource src = {0};
    int resolved = resolve_open_with_animation(itemId, stable_title, &src);
    if (resolved == -2) return 0;
    if (resolved < 0) return 0;
    
    int rc = 0;
    if (src.container[0] && !strcmp(src.container, "torrent")) {
        // /stream devolve um magnet para clientes nativos. No Switch a rota
        // reproduzivel e a mesma usada pelo navegador: TorBox/hot-stream.
        // /stream pode escolher uma fonte que o hot-stream considera nao
        // confiavel. Sem escolha manual de variante, deixe o endpoint selecionar
        // a melhor fonte validada para TorBox.
        if (src.session_id > 0) api_stop_playback(itemId);
        src.session_id = 0;
        int hot = hot_wait_for_stream(itemId, 0, stable_title, &src);
        if (hot == 2) {
            if (api_reresolve_playback(itemId, NULL, &src) < 0 || !src.play_url[0]) {
                toast("Video preparado, mas a fonte R2 nao abriu");
                return 0;
            }
        } else if (hot == -2) {
            // Instalacoes antigas sem hot-stream mantem a preparacao existente.
            return accel_wait_and_play(itemId, stable_title);
        } else if (hot != 1) {
            return 0;
        }
    }
    if (src.container[0] && !strcmp(src.container, "embed")) {
        toast("Este conteudo ainda nao esta disponivel neste dispositivo");
    } else if (src.play_url[0]) {
        double start = 0;
        int completed = 0;
        char p[96]; snprintf(p, sizeof(p), "/api/sync/progress/%d", itemId);
        cJSON *pr = api_get_timeout(p, 2L, 5L);
        if (pr) {
            cJSON *prog = cJSON_GetObjectItem(pr, "progress");
            cJSON *ps = prog ? cJSON_GetObjectItem(prog, "position_seconds") : NULL;
            if (ps && cJSON_IsNumber(ps)) start = ps->valuedouble;
            completed = prog ? jint(prog, "completed") : 0;
            cJSON_Delete(pr);
        }
        if (!completed && start > 10 && src.sequential_stream) {
            if (!prompt_sequential_restart(stable_title)) return 0;
            start = 0;
        } else if (!completed && start > 10) {
            int choice = prompt_resume_playback(stable_title, (int)start);
            if (choice < 0) { if (src.session_id > 0) api_stop_playback(itemId); return 0; }
            if (choice == 0) start = 0;
        }

        PlayerRequest req = {0};
        req.playback = src;
        req.item_id = itemId;
        req.session_id = src.session_id;
        req.source_id = src.source_id;
        req.delivery = src.delivery;
        req.title = stable_title;
        req.section = src.section;
        req.container = src.container;
        req.url = src.play_url;
        req.season = src.season;
        req.episode = src.episode;
        req.start_sec = start;
        if (src.sequential_stream) req.start_sec = 0;
        req.progress_cb = on_player_progress;
        // Hot/debrid nao tem sessao /stream renovavel. Uma recuperacao via
        // /stream poderia entregar magnet ao demuxer e repetir a falha.
        req.renew_cb = src.delivery_str[0] &&
            (!strcmp(src.delivery_str, "hot") || !strcmp(src.delivery_str, "debrid"))
            ? NULL : on_player_renew;
        req.fallback_cb = req.renew_cb ? on_player_fallback : NULL;
        req.heartbeat_cb = src.session_id > 0 ? on_player_heartbeat : NULL;
        PlaybackSyncStatus sync = {0};
        req.userdata = &sync;

        playback_memory_enter();
        appletSetMediaPlaybackState(true);
        PlayerResult res = {0};
        int run_rc = player_run(gRen, g_joy, &req, &res);
        appletSetMediaPlaybackState(false);
        playback_memory_leave();
        g_download_awake = 0;
        if (finalize_natural_playback(itemId, &res, &sync))
            mark_episode_completed_in_detail(itemId);

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
    const char *latest_progress_at = NULL;
    cJSON *season;
    cJSON_ArrayForEach(season, seasons) {
        int local = 0;
        cJSON *episode;
        cJSON_ArrayForEach(episode, season) {
            if (!have_fallback && !episode_completed(episode)) {
                fallback_season = season_index; fallback_local = local;
                fallback_flat = flat_index; have_fallback = 1;
            }
            if (episode_started(episode)) {
                const char *updated_at = jstr(episode, "progress_updated_at");
                if (!have_started || (updated_at &&
                    (!latest_progress_at || strcmp(updated_at, latest_progress_at) >= 0))) {
                    best_season = season_index; best_local = local;
                    best_flat = flat_index; have_started = 1;
                    latest_progress_at = updated_at;
                }
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

static void begin_catalog_fetch(FetchKind kind, const char *path, const char *query) {
    FetchIntent intent = {0};
    intent.kind = kind;
    intent.origin = g_screen == SC_LOADING ? g_fetch_current.origin : g_screen;
    snprintf(intent.path, sizeof(intent.path), "%s", path);
    if (query) snprintf(intent.query, sizeof(intent.query), "%s", query);
    // Mantem o ultimo perfil visivel se a atualizacao da lista falhar/cancelar.
    if (g_fetch.thread) {
        // A newer selection wins. Keep at most one request in flight and one
        // pending intent so repeated button presses cannot flood the server.
        g_fetch_queued = intent;
        g_fetch_discard = 1;
        catalog_fetch_cancel(&g_fetch);
    } else {
        g_fetch_current = intent;
        g_fetch_discard = 0;
        if (catalog_fetch_start(&g_fetch, intent.path, g_token) != 0) {
            g_fetch_current.kind = FETCH_NONE;
            toast("Nao foi possivel iniciar a consulta");
            if (kind == FETCH_PROFILES) g_screen = g_profile_required ? SC_PROFILES : g_profiles_return;
            return;
        }
    }
    g_screen = SC_LOADING;
}

static void install_avatar_catalog(cJSON *catalog) {
    if (g_avatar_catalog) cJSON_Delete(g_avatar_catalog);
    g_avatar_catalog = catalog;
    g_avatar_item_n = 0;
    cJSON *groups = cJSON_GetObjectItemCaseSensitive(catalog, "catalog");
    for (int g = 0; g < arr_len(groups) && g_avatar_item_n < 256; g++) {
        cJSON *items = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(groups, g), "items");
        for (int i = 0; i < arr_len(items) && g_avatar_item_n < 256; i++) {
            cJSON *item = cJSON_GetArrayItem(items, i);
            const char *key = jstr(item, "k"), *url = jstr(item, "url");
            if ((!key || (strncmp(key, "char:", 5) && strncmp(key, "img:", 4))) || !url) continue;
            if (strstr(url, ".svg")) continue; // SDL_image do NRO nao decodifica SVG.
            g_avatar_items[g_avatar_item_n++] = item;
        }
    }
}
static int avatar_fetch_thread(void *unused) {
    (void)unused;
    g_avatar_pending = api_get_timeout("/api/account/avatars", 3L, 12L);
    SDL_AtomicSet(&g_avatar_done, 1);
    return 0;
}
static void start_avatar_fetch(void) {
    if (g_avatar_catalog || g_avatar_thread) return;
    SDL_AtomicSet(&g_avatar_done, 0);
    g_avatar_thread = SDL_CreateThread(avatar_fetch_thread, "avatar-catalog", NULL);
}
static void pump_avatar_fetch(void) {
    if (!g_avatar_thread || !SDL_AtomicGet(&g_avatar_done)) return;
    SDL_WaitThread(g_avatar_thread, NULL); g_avatar_thread = NULL;
    if (g_avatar_pending && cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(g_avatar_pending, "catalog"))) {
        install_avatar_catalog(g_avatar_pending);
        g_avatar_pending = NULL;
        if (g_avatar_picker_await && g_screen == SC_CONFIG) {
            g_avatar_picker = 1; g_avatar_sel = 0; g_avatar_page = 0;
        }
    } else {
        if (g_avatar_pending) { cJSON_Delete(g_avatar_pending); g_avatar_pending = NULL; }
        if (g_avatar_picker_await) toast("Avatares indisponiveis. Tente novamente.");
    }
    g_avatar_picker_await = 0;
}

static void pump_catalog_fetch(void) {
    cJSON *result = NULL;
    char error[192] = {0};
    if (!catalog_fetch_take(&g_fetch, &result, error, sizeof(error))) return;
    if (g_fetch_queued.kind != FETCH_NONE) {
        if (result) cJSON_Delete(result);
        g_fetch_current = g_fetch_queued;
        g_fetch_queued.kind = FETCH_NONE;
        g_fetch_discard = 0;
        if (catalog_fetch_start(&g_fetch, g_fetch_current.path, g_token) == 0) return;
        error[0] = '\0';
        snprintf(error, sizeof(error), "Nao foi possivel iniciar a consulta");
        result = NULL;
    }
    if (g_fetch_discard || g_screen != SC_LOADING) {
        if (g_fetch_current.kind == FETCH_SERIES) g_episode_pending.active = 0;
        if (result) cJSON_Delete(result);
        g_fetch_current.kind = FETCH_NONE;
        g_fetch_discard = 0;
        return;
    }
    int applied = 0;
    if (result && (g_fetch_current.kind == FETCH_MOVIE || g_fetch_current.kind == FETCH_RELATED)) {
        applied = (g_fetch_current.kind == FETCH_RELATED ? open_related_details_response(result) :
                   open_movie_details_response(result)) == 0;
        if (applied) g_screen = SC_MOVIE;
    } else if (result && g_fetch_current.kind == FETCH_SERIES &&
               cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(result, "series"))) {
        if (g_ser) cJSON_Delete(g_ser);
        g_ser = result;
        result = NULL;
        select_series_resume_target(g_ser);
        rebuild_series_plot();
        g_screen = SC_SERIES;
        applied = 1;
    } else if (result && g_fetch_current.kind == FETCH_SEARCH && cJSON_IsObject(result)) {
        if (g_search) cJSON_Delete(g_search);
        g_search = result;
        g_search_counts_valid = 0;
        result = NULL;
        snprintf(g_srchQuery, sizeof(g_srchQuery), "%s", g_fetch_current.query);
        g_srchSel = 0; g_srchScroll = 0; g_srchFilter = 0;
        g_screen = SC_SEARCH;
        applied = 1;
    } else if (result && g_fetch_current.kind == FETCH_SAGA &&
               cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(result, "items"))) {
        if (g_saga_detail) cJSON_Delete(g_saga_detail);
        g_saga_detail = result;
        result = NULL;
        g_saga_item_sel = 0;
        g_screen = SC_SAGA;
        applied = 1;
    } else if (result && g_fetch_current.kind == FETCH_PROFILES &&
               cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(result, "profiles"))) {
        if (g_profiles) cJSON_Delete(g_profiles);
        g_profiles = result;
        result = NULL;
        g_profile_sel = 0;
        cJSON *profiles = cJSON_GetObjectItemCaseSensitive(g_profiles, "profiles");
        for (int i = 0; i < arr_len(profiles); i++) {
            if (jint(cJSON_GetArrayItem(profiles, i), "id") == g_profile_id) g_profile_sel = i;
        }
        g_avatar_due = SDL_GetTicks() + 3000;
        g_avatar_attempted = 0;
        if (g_profile_required && g_profile_id > 0 && arr_len(profiles) > 0 &&
            jint(cJSON_GetArrayItem(profiles, g_profile_sel), "id") == g_profile_id) {
            // O backend usa o primeiro perfil como fallback para um ID apagado.
            // So carrega dados pessoais depois de confirmar o ID salvo.
            g_profile_required = 0;
            store_select_profile(g_profile_id, g_user);
            load_favs(); g_screen = SC_MAIN; enter_tab(0);
        } else g_screen = SC_PROFILES;
        applied = 1;
    } else if (result && g_fetch_current.kind == FETCH_AVATARS &&
               cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(result, "catalog"))) {
        install_avatar_catalog(result);
        result = NULL;
        g_avatar_picker = 1;
        g_avatar_sel = 0;
        g_avatar_page = 0;
        g_screen = SC_CONFIG;
        applied = 1;
    }
    if (result) cJSON_Delete(result);
    if (!applied) {
        g_screen = g_fetch_current.kind == FETCH_PROFILES ?
                   (g_profile_required ? SC_PROFILES : g_profiles_return) : g_fetch_current.origin;
        toast(error[0] ? error : "Resposta invalida do servidor");
    }
    FetchKind completed_kind = g_fetch_current.kind;
    g_fetch_current.kind = FETCH_NONE;
    if (completed_kind == FETCH_SERIES && g_episode_pending.active) {
        int series_id = g_episode_pending.series_id;
        int finished_item_id = g_episode_pending.finished_item_id;
        int first_in_group = g_episode_pending.first_in_group;
        g_episode_pending.active = 0;
        if (applied && jint(cJSON_GetObjectItem(g_ser, "series"), "id") == series_id) {
            char next_title[256];
            int next_id = choose_next_episode(series_id, finished_item_id,
                                              first_in_group, 0,
                                              next_title, sizeof(next_title));
            if (next_id > 0) play_episode_sequence(next_id, series_id, next_title);
        }
    }
}

static void open_series(int id) {
    if (id <= 0) return;
    // Navegacao explicita invalida um autoavanco anterior ainda pendente.
    g_episode_pending.active = 0;
    char path[96]; snprintf(path, sizeof(path), "/api/catalog/series/%d", id);
    begin_catalog_fetch(FETCH_SERIES, path, NULL);
}
void request_related_movie_details(int id) {
    if (id <= 0) return;
    char path[96]; snprintf(path, sizeof(path), "/api/catalog/movie/%d/info", id);
    begin_catalog_fetch(FETCH_RELATED, path, NULL);
}
static void open_item(cJSON *item, int is_series) {
    if (!item) return;
    detail_capture_origin();
    int id = jint(item, "id");
    const char *kind = jstr(item, "kind");
    if (kind && (!strcmp(kind, "live") || !strcmp(kind, "episode"))) {
        if (!strcmp(kind, "episode"))
            play_episode_sequence(id, jint(item, "series_id"), jstr(item, "title"));
        else resolve_and_play(id, jstr(item, "title"));
        return;
    }
    cJSON *sid = cJSON_GetObjectItem(item, "series_id");
    if (sid && cJSON_IsNumber(sid)) { open_series(sid->valueint); return; }
    if (catalog_item_is_series(item, is_series)) open_series(id);
    else {
        if (id <= 0) return;
        char path[96]; snprintf(path, sizeof(path), "/api/catalog/movie/%d/info", id);
        begin_catalog_fetch(FETCH_MOVIE, path, NULL);
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

typedef struct {
    int item_id, source_id, rc;
    HotStreamResult result;
    char error[192];
    SDL_atomic_t done, cancel;
} HotPoll;

static int hot_poll_thread(void *userdata) {
    HotPoll *poll = (HotPoll *)userdata;
    poll->rc = api_hot_stream_attempt(poll->item_id, poll->source_id,
                                      &poll->cancel, &poll->result);
    if (poll->rc != 0) snprintf(poll->error, sizeof(poll->error), "%s", api_last_error());
    SDL_AtomicSet(&poll->done, 1);
    return 0;
}

// Retorna 1 para stream, 2 para R2 publicado, -2 quando a rota nao existe.
// A requisicao roda em thread para a tela e o botao B continuarem responsivos.
static int hot_wait_for_stream(int itemId, int sourceId, const char *title,
                               PlaybackSource *out) {
    Uint32 deadline = SDL_GetTicks() + 60000u;
    Uint32 next_poll = 0;
    int progress = 0, result = 0;
    HotPoll poll = { .item_id = itemId, .source_id = sourceId };
    SDL_Thread *thread = NULL;
    appletSetMediaPlaybackState(true);
    while (g_running && (Sint32)(deadline - SDL_GetTicks()) > 0) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) { g_running = 0; result = 0; goto done; }
            if (event.type == SDL_JOYBUTTONDOWN &&
                (event.jbutton.button == JOY_B || event.jbutton.button == JOY_MINUS)) {
                result = 0; goto done;
            }
            if (event.type == SDL_FINGERDOWN && event.tfinger.x > 0.80f &&
                event.tfinger.y < 0.15f) { result = 0; goto done; }
        }
        Uint32 now = SDL_GetTicks();
        if (thread && SDL_AtomicGet(&poll.done)) {
            SDL_WaitThread(thread, NULL); thread = NULL;
            if (poll.rc != 0) {
                const char *error = poll.error;
                result = error && (strstr(error, "HTTP 404") || strstr(error, "HTTP 501")) ? -2 : -1;
                if (result == -1) toast(error && error[0] ? error : "Falha ao preparar o video");
                goto done;
            }
            if (poll.result.status == HOT_STREAMING) {
                *out = poll.result.source;
                result = 1; goto done;
            }
            if (poll.result.status == HOT_R2_READY) {
                result = 2; goto done;
            }
            progress = poll.result.progress;
            next_poll = now + (Uint32)poll.result.poll_after_ms;
        }
        if (!thread && now >= next_poll) {
            SDL_AtomicSet(&poll.done, 0);
            SDL_AtomicSet(&poll.cancel, 0);
            thread = SDL_CreateThread(hot_poll_thread, "hot-stream", &poll);
            if (!thread) { toast("Nao foi possivel consultar o preparo do video"); result = -1; goto done; }
        }
        SDL_SetRenderDrawColor(gRen, C_BG.r, C_BG.g, C_BG.b, 255);
        SDL_RenderClear(gRen);
        ui_header("NPLAY", "Preparando reproducao", "B Cancelar");
        ui_popcorn_draw(gRen, WIN_W / 2, 110, 170);
        text_center_at(title && title[0] ? title : "Video", 150, WIN_W - 300, 330, C_TEXT, 1);
        text_center(progress > 0 ? "Preparando os primeiros segundos..." :
                    "Conectando a melhor fonte...", 385, C_MUT, 0);
        int bx = 260, by = 455, bw = WIN_W - 520;
        fill_rect(bx, by, bw, 8, C_CARD);
        int shown = progress > 0 ? (bw * progress / 100) : (int)((now / 8) % bw);
        fill_rect(bx, by, shown, 8, C_ACC2);
        text_center("O video comeca assim que o buffer estiver pronto.", 510, C_MUT, 0);
        SDL_RenderPresent(gRen);
        SDL_Delay(16);
    }
    toast("O video ainda esta sendo preparado. Tente novamente em instantes.");
    result = -1;
done:
    if (thread) {
        SDL_AtomicSet(&poll.cancel, 1);
        SDL_WaitThread(thread, NULL);
    }
    appletSetMediaPlaybackState(false);
    return result;
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
    text_clip(title ? title : "Video", 70, 108, C_TEXT, 1, WIN_W - 140);

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
    // listJobs() e ordenado por estado/data, nao pela ordem dos episodios.
    // A lista da Biblioteca e o auto-avanco precisam de T/E crescente.
    for (int g = 0; g < g_dlgN; g++) {
        if (g_dlg[g].isMovie) continue;
        for (int i = 1; i < g_dlg[g].nJobs; i++) {
            int job_index = g_dlg[g].job[i];
            cJSON *job = cJSON_GetArrayItem(jobs, job_index);
            int season = jint(job, "season"), episode = jint(job, "episode");
            int j = i;
            while (j > 0) {
                cJSON *previous = cJSON_GetArrayItem(jobs, g_dlg[g].job[j - 1]);
                int ps = jint(previous, "season"), pe = jint(previous, "episode");
                if (ps < season || (ps == season && pe <= episode)) break;
                g_dlg[g].job[j] = g_dlg[g].job[j - 1];
                j--;
            }
            g_dlg[g].job[j] = job_index;
        }
    }
}
static cJSON *dlg_job(int g, int idx) { return cJSON_GetArrayItem(dl_jobs(), g_dlg[g].job[idx]); }
static int dl_jobs_adjacent(cJSON *current, cJSON *next) {
    int season = jint(current, "season"), episode = jint(current, "episode");
    int next_season = jint(next, "season"), next_episode = jint(next, "episode");
    return episode_coordinates_adjacent(season, episode, next_season, next_episode);
}
// Carrega marcas "visto" sem bloquear o renderer. Ao trocar de obra, descarta
// a resposta antiga e aplica somente a serie que continua aberta.
static void start_dl_done_fetch(int series_id) {
    if (series_id <= 0) return;
    char path[64]; snprintf(path, sizeof(path), "/api/catalog/series/%d", series_id);
    if (catalog_fetch_start(&g_dl_done_fetch, path, g_token) == 0) {
        g_dl_done_inflight = series_id;
        g_dl_done_profile = net_get_profile_id();
    }
}
static void load_dl_done(int series_id) {
    g_dlDoneN = 0;
    g_dl_done_requested = series_id > 0 ? series_id : 0;
    if (g_dl_done_fetch.thread) {
        if (g_dl_done_inflight != g_dl_done_requested)
            catalog_fetch_cancel(&g_dl_done_fetch);
        return;
    }
    start_dl_done_fetch(g_dl_done_requested);
}
static void pump_dl_done(void) {
    cJSON *sd = NULL;
    if (!catalog_fetch_take(&g_dl_done_fetch, &sd, NULL, 0)) return;
    int fetched_id = g_dl_done_inflight;
    g_dl_done_inflight = 0;
    if (g_dl_done_profile != net_get_profile_id()) {
        g_dl_done_requested = 0;
        if (sd) cJSON_Delete(sd);
        return;
    }
    if (fetched_id != g_dl_done_requested) {
        if (sd) cJSON_Delete(sd);
        start_dl_done_fetch(g_dl_done_requested);
        return;
    }
    if (!sd || g_dlView != 1 || g_dlGroup < 0 || g_dlGroup >= g_dlgN ||
        jint(dlg_job(g_dlGroup, 0), "series_id") != fetched_id) {
        if (sd) cJSON_Delete(sd);
        return;
    }
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
    int old_key = 0, old_item_id = 0;
    if (g_dlView == 1 && g_dlGroup >= 0 && g_dlGroup < g_dlgN) {
        old_key = g_dlg[g_dlGroup].key;
        if (g_dlDetSel >= 0 && g_dlDetSel < g_dlg[g_dlGroup].nJobs)
            old_item_id = jint(dlg_job(g_dlGroup, g_dlDetSel), "item_id");
    }
    if (g_dl) cJSON_Delete(g_dl);
    g_dl = fresh;
    g_dl_last_ok = SDL_GetTicks();
    build_dl_groups();
    if (g_dlSel >= g_dlgN) g_dlSel = g_dlgN > 0 ? g_dlgN - 1 : 0;
    if (g_dlView == 1) {
        int keys[MAX_DLG];
        for (int i = 0; i < g_dlgN; i++) keys[i] = g_dlg[i].key;
        int selected = episode_group_index(keys, g_dlgN, old_key);
        if (selected < 0) {
            g_dlView = 2; g_dlGroup = 0; g_dlDetSel = 0;
            g_dl_done_requested = 0; g_dlDoneN = 0;
        } else {
            g_dlGroup = selected;
            int nj = g_dlg[selected].nJobs;
            int matching = -1;
            for (int i = 0; i < nj; i++) {
                if (jint(dlg_job(selected, i), "item_id") == old_item_id) { matching = i; break; }
            }
            if (matching >= 0) g_dlDetSel = matching;
            else if (g_dlDetSel >= nj) g_dlDetSel = nj > 0 ? nj - 1 : 0;
        }
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
    if (filter == 2) return is_series && (!scope || !strcmp(scope, "series"));
    return !is_series && (!scope || !strcmp(scope, "movie"));
}
static int srch_count_for(int filter) {
    if (!g_search || filter < 0 || filter >= SEARCH_FILTERS) return 0;
    if (!g_search_counts_valid) {
        memset(g_search_counts, 0, sizeof(g_search_counts));
        for (int group = 0; group < 2; group++) {
            int is_series = group == 0;
            cJSON *array = cJSON_GetObjectItem(g_search, is_series ? "series" : "items");
            cJSON *it;
            cJSON_ArrayForEach(it, array) {
                for (int f = 0; f < SEARCH_FILTERS; f++)
                    if (srch_matches(it, is_series, f)) g_search_counts[f]++;
            }
        }
        g_search_counts_valid = 1;
    }
    return g_search_counts[filter];
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
    if (prompt_text("Buscar filme, serie ou anime...", q, sizeof(q), 0) != 0) return;
    char enc[400]; url_encode_utf8(q, enc, sizeof(enc));
    char path[460]; snprintf(path, sizeof(path), "/api/catalog/search-v2?q=%s", enc);
    begin_catalog_fetch(FETCH_SEARCH, path, q);
}

// ------------------------------------------------------------- render: barra
static cJSON *profile_by_id(int id) {
    cJSON *arr = g_profiles ? cJSON_GetObjectItemCaseSensitive(g_profiles, "profiles") : NULL;
    for (int i = 0; i < arr_len(arr); i++) {
        cJSON *profile = cJSON_GetArrayItem(arr, i);
        if (jint(profile, "id") == id) return profile;
    }
    return NULL;
}
static const char *profile_avatar_url(cJSON *profile) {
    const char *key = jstr(profile, "avatar");
    if (!key || !key[0]) return NULL;
    if (!strncmp(key, "img:", 4) && !strchr(key + 4, '/') && !strchr(key + 4, '\\')) {
        static char local_url[320];
        snprintf(local_url, sizeof(local_url), "/img/avatars/%s", key + 4);
        return local_url;
    }
    cJSON *groups = g_avatar_catalog ? cJSON_GetObjectItemCaseSensitive(g_avatar_catalog, "catalog") : NULL;
    for (int g = 0; g < arr_len(groups); g++) {
        cJSON *items = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(groups, g), "items");
        for (int i = 0; i < arr_len(items); i++) {
            cJSON *item = cJSON_GetArrayItem(items, i);
            const char *candidate = jstr(item, "k");
            if (candidate && !strcmp(candidate, key)) return jstr(item, "url");
        }
    }
    return NULL;
}
static void draw_profile_avatar(cJSON *profile, int x, int y, int size) {
    const char *name = jstr(profile, "name");
    SDL_Color bg = C_ACC2;
    const char *hex = jstr(profile, "color");
    unsigned red, green, blue;
    if (hex && strlen(hex) == 7 && hex[0] == '#' &&
        sscanf(hex + 1, "%02x%02x%02x", &red, &green, &blue) == 3) {
        bg = (SDL_Color){(Uint8)red, (Uint8)green, (Uint8)blue, 255};
    }
    fill_rect(x, y, size, size, bg);
    const char *url = profile_avatar_url(profile);
    SDL_Texture *tex = url ? cover_get(url) : NULL;
    if (tex) { SDL_Rect dst = { x, y, size, size }; ui_cover(tex, &dst); }
    else {
        char initial[2] = { name && name[0] ? name[0] : '?', 0 };
        text_center_at(initial, x, size, y + size / 2 - 16, C_TEXT, 1);
    }
}
static void draw_topbar(void) {
    fill_rect(0, 0, WIN_W, 95, C_BAR);
    fill_rect(0, 0, WIN_W, 3, C_ACC);
    if (g_brand) { SDL_Rect mark = {48, 23, 46, 46}; SDL_RenderCopy(gRen, g_brand, NULL, &mark); }
    else { fill_rect(50, 27, 39, 39, C_ACC); text_center_at("N", 50, 39, 30, C_BG, 1); }
    text_draw(gRen, "NPLAY", 96, 30, C_TEXT, 1);
    int tx = 255;
    for (int t = 0; t < NTABS; t++) {
        int w = text_draw(gRen, TAB_NAME[t], tx + 11, 33, (t == g_tab) ? C_ACC : C_TEXT, 0);
        if (t == g_tab) {
            fill_rect(tx, 76, w + 22, 3, C_ACC);
        }
        tx += w + 33;
    }
    text_draw(gRen, "Y Buscar", 1040, 33, C_TEXT, 0);
    cJSON *active = profile_by_id(g_profile_id);
    draw_profile_avatar(active, 1188, 24, 46);
    border_rect(1186, 22, 50, 50, 2, C_ACC2);
    fill_rect(0, 94, WIN_W, 1, (SDL_Color){41, 46, 64, 255});
}
static void draw_profile_menu(void) {
    if (!g_profile_menu || (g_screen != SC_MAIN && g_screen != SC_SEARCH)) return;
    fill_rect(0, 95, WIN_W, WIN_H - 95, (SDL_Color){4, 5, 12, 160});
    ui_panel(844, 87, 390, 310, C_ACC2);
    cJSON *active = profile_by_id(g_profile_id);
    draw_profile_avatar(active, 868, 110, 58);
    text_clip(jstr(active, "name") ? jstr(active, "name") : g_user,
              942, 113, C_TEXT, 1, 260);
    text_draw(gRen, "Perfil ativo", 942, 147, C_MUT, 0);
    static const char *items[] = { "Alterar perfil", "Configuracoes", "Sair da conta" };
    for (int i = 0; i < 3; i++) {
        int y = 190 + i * 63;
        fill_rect(868, y, 342, 52, i == g_profile_menu_sel ? (SDL_Color){55, 47, 92, 255} : C_BAR);
        if (i == g_profile_menu_sel) ui_focus(865, y - 3, 348, 58);
        text_draw(gRen, items[i], 888, y + 11, C_TEXT, 0);
    }
}

// ------------------------------------------------------------- render: landing
#define RCW 210
#define RCH 270
#define RGAP 18
#define HERO_H 190
#define RAILS_TOP 308
#define RAIL_STEP (30 + RCH + 70)
static void draw_landing(void) {
    if (!g_land) {
        draw_topbar();
        if (g_land_thread) ui_loading_state(g_status[0] ? g_status : "Carregando catalogo",
                                            "Buscando os destaques e as capas da sua conta");
        else ui_empty_state(g_status[0] ? g_status : "Falha ao carregar catalogo",
                            "Pressione A para tentar novamente.");
        ui_footer("Y Buscar    L/R Trocar categoria    - Perfil");
        return;
    }
    SDL_Rect content_clip = { 0, 95, WIN_W, WIN_H - 95 - 52 };
    SDL_RenderSetClipRect(gRen, &content_clip);
    int nh = hero_count();
    int hy = 110 - g_homeScroll;
    if (nh > 0 && hy + HERO_H >= 95 && hy < WIN_H) {
        cJSON *h = cJSON_GetArrayItem(g_heroesArr, g_heroIdx % nh);
        const char *backdrop = jstr(h, "backdrop");
        SDL_Texture *bg = cover_get(backdrop);
        fill_rect(52, hy, WIN_W - 104, HERO_H, C_CARD);
        SDL_Texture *visual = bg ? bg : cover_get(jstr(h, "logo"));
        if (visual) {
            SDL_Rect br = { 750, hy + 7, 466, HERO_H - 14 };
            ui_contain(visual, &br);
        }
        fill_rect(52, hy, 5, HERO_H, C_ACC);
        if (g_railSel == -1) ui_focus(52, hy - 4, WIN_W - 104, HERO_H + 8);
        int content_x = 78;
        text_draw(gRen, "DESTAQUE NPLAY", content_x, hy + 14, C_ACC, 2);
        const char *ht = jstr(h, "title"); if (!ht) ht = "";
        text_clip(ht, content_x, hy + 39, C_TEXT, 1, 650);
        const char *hk = jstr(h, "kind");
        if (!hk) hk = jstr(h, "hero_type");
        const char *kl = hk ? (!strcmp(hk, "movie") ? "Filme" : !strcmp(hk, "live") ? "Ao vivo" : "Serie")
                            : (g_heroSeriesDefault ? "Serie" : "Filme");
        char meta[96]; const char *year = jstr(h, "year");
        snprintf(meta, sizeof(meta), "%s%s%s%s", kl, year && year[0] ? "  |  " : "", year && year[0] ? year : "",
                 (cJSON_IsTrue(cJSON_GetObjectItem(h, "r2_ready")) || jint(h, "r2_ready")) ? "  |  Pronto pra tocar" : "");
        text_clip(meta, content_x, hy + 83, C_MUT, 2, 650);
        const char *plot = jstr(h, "plot");
        if (plot && plot[0]) text_clip(plot, content_x, hy + 111, C_MUT, 0, 650);
        char cnt[32]; snprintf(cnt, sizeof(cnt), "%d / %d", (g_heroIdx % nh) + 1, nh);
        text_draw(gRen, cnt, WIN_W - 126, hy + HERO_H - 29, C_MUT, 2);
    }

    int y = (nh > 0 ? RAILS_TOP : 125) - g_homeScroll;
    for (int r = 0; r < g_railsN; r++) {
        int items = g_rails[r].count;
        int ry = y + 30;
        // O titulo e o indicador de foco ficam abaixo da capa.
        if (ry + RCH + 52 < 95) { y += RAIL_STEP; continue; }
        if (y >= WIN_H) break;
        text_draw(gRen, g_rails[r].label, 54, y, C_TEXT, 0);
        char total[40]; snprintf(total, sizeof(total), "%d titulos", items);
        text_right(total, WIN_W - 55, y + 2, C_MUT, 2);
        int rowScroll = 0;
        if (r == g_railSel) {
            int selX = 54 + g_railItem * (RCW + RGAP);
            if (selX + RCW - rowScroll > WIN_W - 54) rowScroll = selX + RCW - (WIN_W - 54);
            if (selX - rowScroll < 54) rowScroll = selX - 54;
            if (rowScroll < 0) rowScroll = 0;
        }
        const int step = RCW + RGAP;
        int first = rowScroll > 54 + RCW ? (rowScroll - 54 - RCW) / step : 0;
        int last = (WIN_W + rowScroll - 54) / step + 1;
        if (last > items) last = items;
        cJSON *it = cJSON_GetArrayItem(g_rails[r].arr, first);
        for (int i = first; i < last && it; i++, it = it->next) {
            int x = 54 + i * (RCW + RGAP) - rowScroll;
            if (x + RCW < 0 || x > WIN_W) continue;
            int series_item = catalog_item_is_series(it, g_rails[r].is_series);
            int fav_id = catalog_favorite_id(it, series_item);
            int fav = series_item ? is_fav_series(fav_id) : is_fav_item(fav_id);
            draw_card(x, ry, RCW, RCH, it, (r == g_railSel && i == g_railItem), fav);
        }
        y += RAIL_STEP;
    }
    int search_y = (nh > 0 ? RAILS_TOP : 125) + g_railsN * RAIL_STEP - g_homeScroll;
    if (search_y + 116 >= 95 && search_y < WIN_H) {
        int selected = g_railSel == g_railsN;
        ui_panel(40, search_y, WIN_W - 80, 112, C_ACC2);
        if (selected) ui_focus(36, search_y - 4, WIN_W - 72, 120);
        text_draw(gRen, "MAIS NO NPLAY", 68, search_y + 18, C_ACC2, 0);
        const char *prompt = g_tab == 1 ? "Procurando outro filme?" :
                             g_tab == 2 ? "Procurando outra serie?" :
                             g_tab == 3 ? "Procurando outro anime?" :
                                          "Quer encontrar uma obra especifica?";
        text_draw(gRen, prompt, 68, search_y + 48, C_TEXT, 1);
        text_draw(gRen, "Busque pelo nome ou por parte do titulo.", 68, search_y + 80, C_MUT, 0);
        ui_badge(selected ? "A  BUSCAR" : "Y  BUSCAR", WIN_W - 190, search_y + 42, selected ? C_ACC : C_ACC2);
    }
    SDL_RenderSetClipRect(gRen, NULL);
    // text_clip substitui temporariamente o clip da landing; o cabeçalho
    // precisa ser desenhado por ultimo para cobrir qualquer glifo acima de y=95.
    draw_topbar();
    ui_footer(g_railSel == g_railsN ?
        "A ou Y Abrir busca    Cima Voltar ao catalogo    L/R Trocar categoria" :
        "A Abrir    X Minha lista    Y Buscar    L/R Trocar categoria");
}

// ------------------------------------------------------------- render: busca (grade)
#define GCOLS 5
#define GMX 54
#define GGAP 18
#define GCW 210
#define GCOVERW 210
#define GCOVERH 270
#define GCH 322
static void draw_search(void) {
    draw_topbar();
    SDL_Rect content_clip = { 0, 95, WIN_W, WIN_H - 95 - 52 };
    SDL_RenderSetClipRect(gRen, &content_clip);
    int n = srch_count_for(g_srchFilter);
    char hd[200]; snprintf(hd, sizeof(hd), "Resultados para \"%s\"", g_srchQuery);
    text_clip(hd, 54, 111, C_TEXT, 1, 850);
    char count[64]; snprintf(count, sizeof(count), "%d resultado%s", n, n == 1 ? "" : "s");
    text_right(count, WIN_W - 54, 119, C_MUT, 0);
    static const char *filters[] = { "Tudo", "Filmes", "Series", "Animes" };
    int chip_x = 54;
    for (int i = 0; i < SEARCH_FILTERS; i++) {
        int count = srch_count_for(i); char label[48];
        snprintf(label, sizeof(label), "%s  %d", filters[i], count);
        int tw = 0, th = 0; text_cached(gRen, label, C_TEXT, 0, &tw, &th);
        int cw = tw + 28;
        fill_rect(chip_x, 161, cw, 36, i == g_srchFilter ? C_ACC : C_CARD);
        text_center_at(label, chip_x, cw, 167, i == g_srchFilter ? C_BG : C_MUT, 0);
        chip_x += cw + 12;
    }
    if (n == 0) {
        ui_empty_state("Nada neste filtro", "Use ZL/ZR para trocar o tipo ou Y para fazer outra busca.");
        SDL_RenderSetClipRect(gRen, NULL);
        ui_footer("ZL/ZR Filtrar    Y Nova busca    B Voltar");
        return;
    }
    int top = 221;
    int index = 0;
    for (int group = 0; group < 2; group++) {
        int is_series = group == 0;
        cJSON *array = cJSON_GetObjectItem(g_search, is_series ? "series" : "items");
        cJSON *it;
        cJSON_ArrayForEach(it, array) {
            if (!srch_matches(it, is_series, g_srchFilter)) continue;
            int col = index % GCOLS, row = index / GCOLS;
            int yy = top + row * (GCH + GGAP) - g_srchScroll;
            if (yy + GCH >= 95 && yy <= WIN_H) {
                int x = GMX + col * (GCW + GGAP) + (GCW - GCOVERW) / 2;
                int fav = is_series ? is_fav_series(jint(it, "id")) : is_fav_item(jint(it, "id"));
                draw_card(x, yy, GCOVERW, GCOVERH, it, index == g_srchSel, fav);
            }
            index++;
        }
    }
    SDL_RenderSetClipRect(gRen, NULL);
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
static int season_number_at(int index) {
    cJSON *season = cJSON_GetArrayItem(seasons_obj(), index);
    if (season && season->string) {
        char *end = NULL;
        long number = strtol(season->string, &end, 10);
        if (end && !*end && number >= 0 && number < 1000) return (int)number;
    }
    cJSON *first = cJSON_GetArrayItem(season, 0);
    cJSON *raw = cJSON_GetObjectItemCaseSensitive(first, "season");
    return cJSON_IsNumber(raw) ? raw->valueint : index + 1;
}
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
static void mark_episode_completed_in_detail(int item_id) {
    cJSON *season;
    cJSON_ArrayForEach(season, seasons_obj()) {
        cJSON *episode;
        cJSON_ArrayForEach(episode, season) {
            if (jint(episode, "id") != item_id) continue;
            cJSON *completed = cJSON_CreateTrue();
            if (!completed) return;
            if (cJSON_GetObjectItemCaseSensitive(episode, "completed"))
                cJSON_ReplaceItemInObjectCaseSensitive(episode, "completed", completed);
            else cJSON_AddItemToObject(episode, "completed", completed);
            return;
        }
    }
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

// A aba Sagas usa a curadoria ja publicada em /api/catalog/sagas. Variantes
// (por exemplo, ordem/edicao diferentes) permanecem dentro do mesmo grupo.
static cJSON *saga_groups(void) { return cJSON_GetObjectItem(g_land, "sagas"); }
static cJSON *saga_group_at(int index) { return cJSON_GetArrayItem(saga_groups(), index); }
static cJSON *saga_variant_at(cJSON *group, int index) {
    cJSON *variants = cJSON_GetObjectItem(group, "variants");
    int count = arr_len(variants);
    return cJSON_GetArrayItem(variants, index >= 0 && index < count ? index : 0);
}
static void draw_sagas(void) {
    draw_topbar();
    if (!g_land) {
        if (g_land_thread) ui_loading_state(g_status[0] ? g_status : "Carregando sagas",
                                            "Buscando as colecoes da sua conta");
        else ui_empty_state(g_status[0] ? g_status : "Falha ao carregar sagas",
                            "Pressione A para tentar novamente.");
        ui_footer("L/R Trocar categoria    B Voltar");
        return;
    }
    int count = arr_len(saga_groups());
    text_draw(gRen, "Sagas", 54, 112, C_TEXT, 1);
    text_draw(gRen, "Filmes reunidos na ordem da historia ou do lancamento", 55, 152, C_MUT, 0);
    if (!count) {
        ui_empty_state("Nenhuma saga disponivel", "As sagas publicadas no site aparecerao aqui.");
        ui_footer("L/R Trocar categoria");
        return;
    }
    SDL_Rect clip = {0, 178, WIN_W, WIN_H - 178 - 52};
    SDL_RenderSetClipRect(gRen, &clip);
    for (int i = 0; i < count; i++) {
        int col = i % 3, row = i / 3;
        int x = 54 + col * 390, y = 185 + row * 231 - g_saga_scroll;
        if (y + 212 < 178 || y > WIN_H - 52) continue;
        cJSON *group = saga_group_at(i);
        cJSON *variant = saga_variant_at(group, i == g_saga_sel ? g_saga_variant_sel : 0);
        fill_rect(x, y, 366, 211, i == g_saga_sel ? (SDL_Color){38, 34, 61, 255} : C_CARD);
        if (i == g_saga_sel) ui_focus(x - 3, y - 3, 372, 217);
        const char *poster = jstr(variant, "poster");
        SDL_Texture *art = cover_get(poster);
        if (art) { SDL_Rect r = {x + 10, y + 10, 125, 184}; ui_contain(art, &r); }
        text_clip(jstr(group, "title") ? jstr(group, "title") : "Saga", x + 145, y + 19, C_TEXT, 0, 208);
        const char *label = jstr(variant, "variant_label");
        if (label && label[0]) text_clip(label, x + 145, y + 54, C_ACC2, 2, 208);
        char info[72];
        snprintf(info, sizeof(info), "%d filmes  |  %d vistos", jint(variant, "count"), jint(variant, "watched"));
        text_clip(info, x + 145, y + 87, C_MUT, 2, 208);
        if (i == g_saga_sel && arr_len(cJSON_GetObjectItem(group, "variants")) > 1)
            text_clip("ZL/ZR Trocar versao", x + 145, y + 159, C_ACC, 2, 208);
    }
    SDL_RenderSetClipRect(gRen, NULL);
    ui_footer("A Abrir saga    Direcoes Navegar    ZL/ZR Versao    L/R Categoria");
}
static void draw_saga_detail(void) {
    cJSON *saga = cJSON_GetObjectItem(g_saga_detail, "saga");
    cJSON *items = cJSON_GetObjectItem(g_saga_detail, "items");
    int count = arr_len(items);
    cJSON *selected = cJSON_GetArrayItem(items, g_saga_item_sel);
    ui_header("NPLAY / SAGA", NULL, "B Voltar");
    fill_rect(52, 110, WIN_W - 104, 250, C_CARD);
    fill_rect(52, 110, 5, 250, C_ACC);
    text_clip(jstr(saga, "title") ? jstr(saga, "title") : "Saga", 72, 133, C_TEXT, 1, 715);
    const char *subtitle = jstr(saga, "subtitle");
    if (subtitle) text_clip(subtitle, 72, 182, C_MUT, 0, 715);
    char order[72]; snprintf(order, sizeof(order), "OBRA %d DE %d", count ? g_saga_item_sel + 1 : 0, count);
    text_draw(gRen, order, 72, 234, C_ACC2, 2);
    if (selected) text_clip(jstr(selected, "title") ? jstr(selected, "title") : "Titulo", 72, 263, C_TEXT, 1, 715);
    const char *art_url = jstr(selected, "backdrop");
    if (!art_url) art_url = jstr(selected, "logo");
    SDL_Texture *art = cover_get(art_url);
    if (art) { SDL_Rect r = {813, 123, 391, 225}; ui_contain(art, &r); }
    text_draw(gRen, "Assista na sequencia", 54, 373, C_TEXT, 0);
    int start = g_saga_item_sel - 2;
    if (start < 0) start = 0;
    if (start > count - 5) start = count > 5 ? count - 5 : 0;
    for (int i = start; i < count && i < start + 5; i++) {
        int x = 54 + (i - start) * (RCW + RGAP);
        draw_card(x, 411, RCW, 205, cJSON_GetArrayItem(items, i), i == g_saga_item_sel, 0);
    }
    if (!count) text_draw(gRen, "Esta saga ainda nao possui titulos disponiveis.", 54, 442, C_MUT, 0);
    ui_footer("A Abrir titulo    Esquerda/direita Escolher    B Voltar");
}
static void rebuild_episode_plot(cJSON *ep) {
    g_ep_plot_count = g_ep_plot_scroll = 0;
    g_ep_plot_id = jint(ep, "id");
    const char *p = jstr(ep, "ep_overview");
    if (!p || !p[0]) p = jstr(ep, "plot");
    if (!p || !p[0]) return;
    while (*p && g_ep_plot_count < 24) {
        while (*p == ' ' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;
        char *line = g_ep_plot_lines[g_ep_plot_count];
        line[0] = '\0';
        while (*p && *p != '\n' && *p != '\r') {
            while (*p == ' ') p++;
            const char *end = p;
            while (*end && *end != ' ' && *end != '\n' && *end != '\r') end++;
            if (end == p) break;
            int word_len = (int)(end - p);
            if (word_len > 190) word_len = 190;
            char candidate[220];
            snprintf(candidate, sizeof(candidate), "%s%s%.*s", line, line[0] ? " " : "", word_len, p);
            int width = 0, height = 0;
            text_cached(gRen, candidate, C_TEXT, 0, &width, &height);
            if (line[0] && width > 730) break;
            snprintf(line, 220, "%s", candidate);
            p = end;
            if (width > 730) break;
        }
        if (line[0]) g_ep_plot_count++;
        else if (*p) p++;
        while (*p == '\n' || *p == '\r') p++;
    }
}
static void rebuild_series_plot(void) {
    g_ser_plot_count = 0;
    const char *plot = jstr(ser_obj(), "plot");
    if (!plot) return;
    const char *p = plot;
    while (*p && g_ser_plot_count < 3) {
        while (*p == ' ' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;
        char *line = g_ser_plot_lines[g_ser_plot_count];
        line[0] = '\0';
        while (*p && *p != '\n' && *p != '\r') {
            while (*p == ' ') p++;
            const char *end = p;
            while (*end && *end != ' ' && *end != '\n' && *end != '\r') end++;
            if (end == p) break;
            char candidate[220];
            int word_len = (int)(end - p);
            if (word_len > 190) word_len = 190;
            snprintf(candidate, sizeof(candidate), "%s%s%.*s", line, line[0] ? " " : "", word_len, p);
            int width = 0, height = 0;
            text_cached(gRen, candidate, C_MUT, 0, &width, &height);
            if (line[0] && width > 890) break;
            snprintf(line, 220, "%s", candidate);
            p = end;
            if (width > 890) break;
        }
        if (line[0]) g_ser_plot_count++;
        else if (*p) p++;
        while (*p == '\n' || *p == '\r') p++;
    }
}
static void draw_series(void) {
    cJSON *s = ser_obj();
    int sid = jint(s, "id");
    int fav = is_fav_series(sid);
    const char *title = jstr(s, "title"); if (!title) title = "Serie";
    const char *section = jstr(s, "section");
    const char *area = section && !strcmp(section, "anime") ? "NPLAY / ANIME" : "NPLAY / SERIE";
    ui_header(area, NULL, "B Voltar");

    SDL_Rect hero = { 52, 110, WIN_W - 104, 306 };
    fill_rect(hero.x, hero.y, hero.w, hero.h, C_CARD);
    fill_rect(52, 110, 5, hero.h, C_ACC);

    text_clip(title, 72, 125, C_TEXT, 1, 750);
    const char *year = jstr(s, "year");
    double rating = 0; cJSON *jr = cJSON_GetObjectItem(s, "rating"); if (jr && cJSON_IsNumber(jr)) rating = jr->valuedouble;
    char meta[128];
    if (rating > 0) snprintf(meta, sizeof(meta), "★ %.1f    %s    %d episodios", rating, year ? year : "", jint(s, "episode_count"));
    else snprintf(meta, sizeof(meta), "%s    %d episodios", year ? year : "", jint(s, "episode_count"));
    text_clip(meta, 72, 169, C_ACC2, 0, 730);
    const char *genre = jstr(s, "genre");
    if (genre) text_clip(genre, 72, 200, C_MUT, 2, 730);

    cJSON *focused = ser_ep_at(g_epSel);
    if (jint(focused, "id") != g_ep_plot_id) rebuild_episode_plot(focused);
    fill_rect(72, 231, 730, 1, C_MUT);
    int current_season = ser_grouped() ? ser_group_idx() + 1 : season_number_at(g_seasonIdx);
    int ep_no = jint(focused, "episode");
    char episode_label[70];
    if (current_season == 0)
        snprintf(episode_label, sizeof(episode_label), "ESPECIAIS  /  EPISODIO %d",
                 ep_no > 0 ? ep_no : g_epSel + 1);
    else snprintf(episode_label, sizeof(episode_label), "TEMPORADA %d  /  EPISODIO %d",
                  current_season, ep_no > 0 ? ep_no : g_epSel + 1);
    text_draw(gRen, episode_label, 72, 244, C_ACC2, 2);
    text_clip(focused ? ep_display_title(focused) : "Escolha um episodio", 72, 270, C_TEXT, 1, 730);
    if (g_ep_plot_count == 0) text_draw(gRen, "Sinopse deste episodio ainda nao disponivel.", 72, 310, C_MUT, 0);
    for (int i = 0; i < 4 && i + g_ep_plot_scroll < g_ep_plot_count; i++)
        text_clip(g_ep_plot_lines[i + g_ep_plot_scroll], 72, 308 + i * 23, C_TEXT, 0, 730);
    if (g_ep_plot_count > 4) {
        char page[68]; snprintf(page, sizeof(page), "Sinopse %d/%d  |  cima/baixo",
                                g_ep_plot_scroll + 1, g_ep_plot_count - 3);
        text_clip(page, 72, 399, C_MUT, 2, 730);
    }
    const char *still = jstr(focused, "ep_still");
    if (!still) still = jstr(focused, "logo");
    SDL_Texture *visual = cover_get(still ? still : jstr(s, "backdrop"));
    if (visual) {
        SDL_Rect er = { 835, 164, 365, 208 };
        ui_contain(visual, &er);
    }
    fill_rect(52, 424, 177, 44, C_ACC);
    text_center_at("A  Assistir", 52, 177, 433, C_BG, 0);
    fill_rect(243, 424, 206, 44, C_CARD);
    text_center_at(fav ? "X  Favoritado" : "X  Favoritar", 243, 206, 433, C_TEXT, 0);

    cJSON *au = ser_audio();
    if (arr_len(au) > 1) {
        const char *label = "Audio"; cJSON *av;
        cJSON_ArrayForEach(av, au) if (cJSON_IsTrue(cJSON_GetObjectItem(av, "current"))) {
            const char *current = jstr(av, "label"); if (current) label = current;
        }
        fill_rect(463, 424, 228, 44, C_CARD);
        char audio_label[100]; snprintf(audio_label, sizeof(audio_label), "ZL/ZR  %s", label);
        text_center_at(audio_label, 463, 228, 433, C_TEXT, 0);
    }
    int nsea = ser_nseasons(), nep = ser_nep();
    text_draw(gRen, "Temporadas", 54, 480, C_TEXT, 0);
    if (nsea > 1) text_right("L/R trocar temporada", WIN_W - 54, 483, C_MUT, 2);
    int selected_season_index = ser_grouped() ? ser_group_idx() : g_seasonIdx;
    int first_season = selected_season_index > 5 ? selected_season_index - 5 : 0;
    for (int i = first_season; i < nsea && i < first_season + 7; i++) {
        int x = 54 + (i - first_season) * 160;
        fill_rect(x, 511, 150, 34, i == selected_season_index ? C_ACC : C_CARD);
        char chip[48];
        int season_number = ser_grouped() ? i + 1 : season_number_at(i);
        if (season_number == 0) snprintf(chip, sizeof(chip), "Especiais");
        else snprintf(chip, sizeof(chip), "Temporada %d", season_number);
        text_center_at(chip, x, 150, 516, i == selected_season_index ? C_BG : C_TEXT, 2);
    }
    char count[50]; snprintf(count, sizeof(count), "%d episodios", nep);
    text_right(count, WIN_W - 54, 518, C_MUT, 2);
    int start = g_epSel - 2;
    if (start < 0) start = 0;
    if (start > nep - 5) start = nep > 5 ? nep - 5 : 0;
    for (int i = start; i < nep && i < start + 5; i++) {
        cJSON *ep = ser_ep_at(i);
        int x = 54 + (i - start) * (RCW + RGAP);
        fill_rect(x, 551, RCW, 105, i == g_epSel ? (SDL_Color){38, 34, 61, 255} : C_CARD);
        if (i == g_epSel) ui_focus(x - 4, 547, RCW + 8, 113);
        const char *card_still = jstr(ep, "ep_still");
        if (!card_still) card_still = jstr(ep, "logo");
        SDL_Texture *thumb = cover_get(card_still ? card_still : jstr(s, "logo"));
        if (thumb) { SDL_Rect er = {x, 551, RCW, 74}; ui_contain(thumb, &er); }
        int en = jint(ep, "episode"); char nb[32]; snprintf(nb, sizeof(nb), "T%d · E%d", current_season, en > 0 ? en : i + 1);
        int done = episode_completed(ep);
        int pos = jint(ep, "position_seconds"), duration = jint(ep, "duration_seconds");
        int progress = (!done && pos > 10 && duration > 0) ? pos * 100 / duration : 0;
        if (progress > 99) progress = 99;
        text_clip(nb, x + 8, 631, C_ACC2, 2, 57);
        text_clip(ep_display_title(ep), x + 68, 631, C_TEXT, 2, RCW - 76);
        if (done || progress > 0) {
            fill_rect(x, 622, RCW, 3, C_MUT);
            fill_rect(x, 622, RCW * (done ? 100 : progress) / 100, 3, C_ROSE);
        }
    }
    if (nep == 0) text_draw(gRen, "Sem episodios nesta temporada", 54, 568, C_MUT, 0);
    ui_footer("A Assistir    Esquerda/direita Episodio    Cima/baixo Sinopse    L/R Temporada");
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
    text_center("Escolha os episodios que deseja deixar prontos para assistir", 104, C_MUT, 0);
    int n = ser_nep(), cnt = 0;
    for (int i = 0; i < g_epChkN; i++) if (g_epChk[i]) cnt++;
    int listTop = 143, rowH = 40, visible = (WIN_H - listTop - 56) / rowH;
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
        long code = api_send("/api/accel/download-batch", "POST", body);
        if (code != 200) {
            toast(code == 503 ? "Preparacao indisponivel no servidor agora" :
                 "Nao foi possivel preparar os episodios selecionados");
            return;
        }
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

#define HIST_CW 216
#define HIST_CH 216
#define HIST_GAP 18

static int horizontal_scroll(int selected, int item_w, int gap) {
    int x = 54 + selected * (item_w + gap), scroll = 0;
    if (x + item_w > WIN_W - 54) scroll = x + item_w - (WIN_W - 54);
    return scroll > 0 ? scroll : 0;
}

static void draw_history_card(int x, int y, cJSON *item, int selected) {
    const char *title = jstr(item, "title"); if (!title) title = "Titulo";
    fill_rect(x, y, HIST_CW, HIST_CH + 52, selected ? (SDL_Color){38, 34, 61, 255} : C_CARD);
    if (selected) ui_focus(x - 4, y - 4, HIST_CW + 8, HIST_CH + 60);
    SDL_Texture *cover = cover_get(jstr(item, "logo"));
    if (cover) { SDL_Rect r = {x, y, HIST_CW, HIST_CH}; ui_contain(cover, &r); }
    else fill_rect(x, y, HIST_CW, HIST_CH, C_CARD);
    int pos = jint(item, "position_seconds"), dur = jint(item, "duration_seconds");
    int pct = dur > 0 ? pos * 100 / dur : 0;
    ui_progress(x, y + HIST_CH - 4, HIST_CW, pct, C_ROSE);
    text_clip(title, x + 8, y + HIST_CH + 8, C_TEXT, 0, HIST_CW - 16);
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
    text_draw(gRen, "Continuar assistindo", 54, 110, C_TEXT, 1);
    text_right(nh ? "A retoma do ponto salvo" : "Seu progresso aparecera aqui", WIN_W - 54, 119, C_MUT, 0);
    if (!g_history && g_history_thread) {
        text_draw(gRen, "Carregando seu historico...", 54, 157, C_MUT, 0);
    } else if (nh <= 0) {
        ui_panel(54, 152, WIN_W - 108, 240, C_ACC2);
        text_center_at("Nenhuma obra em andamento", 70, WIN_W - 140, 214, C_TEXT, 1);
        text_center_at("Quando voce parar um video, ele ficara pronto para continuar aqui.", 70, WIN_W - 140, 276, C_MUT, 0);
    } else {
        int scroll = horizontal_scroll(g_history_sel, HIST_CW, HIST_GAP);
        for (int i = 0; i < nh; i++) {
            int x = 54 + i * (HIST_CW + HIST_GAP) - scroll;
            if (x + HIST_CW < 0 || x > WIN_W) continue;
            draw_history_card(x, 152, cJSON_GetArrayItem(items, i), g_history_zone == 0 && i == g_history_sel);
        }
    }

    int list_count = store_media_list_count();
    text_draw(gRen, "Suas listas", 54, 433, C_TEXT, 1);
    text_right("Biblioteca, favoritos e colecoes pessoais", WIN_W - 54, 442, C_MUT, 0);
    int total = list_count + 2; // Biblioteca + listas locais + Nova lista
    int tile_w = 270, tile_gap = 18, scroll = horizontal_scroll(g_list_sel, tile_w, tile_gap);
    for (int i = 0; i < total; i++) {
        int x = 54 + i * (tile_w + tile_gap) - scroll, y = 481;
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
    text_draw(gRen, "Biblioteca", 54, 111, C_TEXT, 1);
    text_draw(gRen, "Obras preparadas para assistir", 54, 149, C_MUT, 0);
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
    int top = 184;
    for (int i = 0; i < g_dlgN; i++) {
        int col = i % GCOLS, row = i / GCOLS;
        int x = GMX + col * (GCW + GGAP) + (GCW - GCOVERW) / 2;
        int yy = top + row * (GCH + GGAP) - g_dlScroll;
        if (yy + GCH < 66 || yy > WIN_H) continue;
        cJSON *j0 = dlg_job(i, 0);
        if (i == g_dlSel) {
            fill_rect(x, yy, GCOVERW, GCH, (SDL_Color){38, 34, 61, 255});
            ui_focus(x - 4, yy - 4, GCOVERW + 8, GCH + 8);
        }
        SDL_Texture *cov = cover_get(jstr(j0, "cover"));
        SDL_Rect cr = { x, yy, GCOVERW, GCOVERH };
        if (cov) ui_contain(cov, &cr); else fill_rect(x, yy, GCOVERW, GCOVERH, C_CARD);
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
    text_draw(gRen, "EPISODIOS PREPARADOS", 54, 114, C_ACC2, 0);
    text_clip(title, 54, 146, C_TEXT, 1, WIN_W - 300);
    if (dl_has_active()) ui_badge("TELA ATIVA", WIN_W - 174, 96, C_GREEN);
    int nj = g_dlg[g].nJobs;
    int listTop = 195, rowH = 46, visible = (WIN_H - listTop - 56) / rowH;
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
    text_draw(gRen, "LISTA PESSOAL", 54, 112, C_ACC2, 0);
    text_clip(name, 54, 144, C_TEXT, 1, 760);
    char total[64]; snprintf(total, sizeof(total), "%d titulo%s", n, n == 1 ? "" : "s");
    text_right(total, WIN_W - 54, 152, C_MUT, 0);
    if (n <= 0) {
        ui_empty_state("Esta lista esta vazia", "Use X nos relacionados para adicionar uma obra.");
        ui_footer("Y Renomear lista    ZR Excluir lista    B Voltar");
        return;
    }
    int top = 194, selected_row = g_list_item_sel / GCOLS;
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
            fill_rect(x, y, GCOVERW, GCH, (SDL_Color){38, 34, 61, 255});
            ui_focus(x - 4, y - 4, GCOVERW + 8, GCH + 8);
        }
        SDL_Texture *cover = cover_get(logo);
        if (cover) { SDL_Rect r = {x, y, GCOVERW, GCOVERH}; ui_contain(cover, &r); }
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
                store_save_token(g_token); store_save_user(user);
                snprintf(g_user, sizeof(g_user), "%s", user);
                ok = 0;
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
        else if (b == JOY_A) { if (nh) { cJSON *h = cJSON_GetArrayItem(g_heroesArr, g_heroIdx % nh); open_item(h, catalog_item_is_series(h, g_heroSeriesDefault)); } }
        else if (b == JOY_X) { if (nh) { cJSON *h = cJSON_GetArrayItem(g_heroesArr, g_heroIdx % nh); int is = catalog_item_is_series(h, g_heroSeriesDefault); int id = catalog_favorite_id(h, is); if (is) toggle_fav_series(id); else toggle_fav_item(id); } }
        g_homeScroll = 0;
        return;
    }
    if (g_railSel == g_railsN) { // chamada de busca ao final do catalogo
        if (b == JOY_UP) {
            if (g_railsN > 0) {
                g_railSel = g_railsN - 1;
                int n = g_rails[g_railSel].count;
                if (g_railItem >= n) g_railItem = n ? n - 1 : 0;
            } else if (nh > 0) g_railSel = -1;
        } else if (b == JOY_A) do_search();
        if (g_railSel == g_railsN) {
            int sy = (nh > 0 ? RAILS_TOP : 125) + g_railsN * RAIL_STEP;
            if (sy + 132 - g_homeScroll > WIN_H - 52) g_homeScroll = sy + 132 - (WIN_H - 52) + 12;
        } else if (g_railSel < 0) g_homeScroll = 0;
        return;
    }
    int items = g_rails[g_railSel].count;
    if (b == JOY_UP) { if (g_railSel == 0) { g_railSel = (nh > 0) ? -1 : 0; g_homeScroll = 0; if (nh > 0) return; } else { g_railSel--; int n = g_rails[g_railSel].count; if (g_railItem >= n) g_railItem = n ? n - 1 : 0; } }
    else if (b == JOY_DOWN) { if (g_railSel < g_railsN - 1) { g_railSel++; int n = g_rails[g_railSel].count; if (g_railItem >= n) g_railItem = n ? n - 1 : 0; } else g_railSel = g_railsN; }
    else if (b == JOY_DLEFT) { if (g_railItem > 0) g_railItem--; }
    else if (b == JOY_DRIGHT) { if (g_railItem < items - 1) g_railItem++; }
    else if (b == JOY_A) { open_item(cJSON_GetArrayItem(g_rails[g_railSel].arr, g_railItem), g_rails[g_railSel].is_series); }
    else if (b == JOY_X) { cJSON *it = cJSON_GetArrayItem(g_rails[g_railSel].arr, g_railItem); if (it) { int is = catalog_item_is_series(it, g_rails[g_railSel].is_series); int id = catalog_favorite_id(it, is); if (is) toggle_fav_series(id); else toggle_fav_item(id); } }
    int ry = (nh > 0 ? RAILS_TOP : 125) + g_railSel * RAIL_STEP;
    int item_h = g_railSel == g_railsN ? 112 : RCH + 90;
    if (ry + item_h - g_homeScroll > WIN_H - 52) g_homeScroll = ry + item_h - (WIN_H - 52) + 20;
    if (ry - g_homeScroll < 105) g_homeScroll = ry - 105;
    if (g_homeScroll < 0) g_homeScroll = 0;
}
static void input_sagas(int b) {
    int count = arr_len(saga_groups());
    int next = g_saga_sel;
    if (b == JOY_DLEFT && next > 0) next--;
    else if (b == JOY_DRIGHT && next + 1 < count) next++;
    else if (b == JOY_UP && next >= 3) next -= 3;
    else if (b == JOY_DOWN && next + 3 < count) next += 3;
    else if (b == JOY_ZL || b == JOY_ZR) {
        cJSON *group = saga_group_at(g_saga_sel);
        int variants = arr_len(cJSON_GetObjectItem(group, "variants"));
        if (variants > 1) g_saga_variant_sel = (g_saga_variant_sel + (b == JOY_ZR ? 1 : variants - 1)) % variants;
    } else if (b == JOY_A && count > 0) {
        cJSON *variant = saga_variant_at(saga_group_at(g_saga_sel), g_saga_variant_sel);
        const char *slug = jstr(variant, "slug");
        if (slug && slug[0]) {
            char path[256]; snprintf(path, sizeof(path), "/api/catalog/sagas/%.200s", slug);
            begin_catalog_fetch(FETCH_SAGA, path, NULL);
        }
    }
    if (next != g_saga_sel) { g_saga_sel = next; g_saga_variant_sel = 0; }
    int row_top = 185 + (g_saga_sel / 3) * 231;
    if (row_top - g_saga_scroll < 183) g_saga_scroll = row_top - 183;
    if (row_top + 211 - g_saga_scroll > WIN_H - 54)
        g_saga_scroll = row_top + 211 - (WIN_H - 54);
    if (g_saga_scroll < 0) g_saga_scroll = 0;
}
static void input_saga_detail(int b) {
    cJSON *items = cJSON_GetObjectItem(g_saga_detail, "items");
    int count = arr_len(items);
    if (b == JOY_B || b == JOY_MINUS) { g_screen = SC_MAIN; enter_tab(TAB_SAGAS); }
    else if ((b == JOY_DLEFT || b == JOY_UP) && g_saga_item_sel > 0) g_saga_item_sel--;
    else if ((b == JOY_DRIGHT || b == JOY_DOWN) && g_saga_item_sel + 1 < count) g_saga_item_sel++;
    else if (b == JOY_A && count > 0) {
        cJSON *item = cJSON_GetArrayItem(items, g_saga_item_sel);
        if (cJSON_IsFalse(cJSON_GetObjectItem(item, "available")) ||
            (cJSON_IsNumber(cJSON_GetObjectItem(item, "available")) && !jint(item, "available"))) {
            toast("Este titulo ainda nao esta disponivel");
            return;
        }
        open_item(item, catalog_item_is_series(item, 0));
    }
}
static void input_search(int b) {
    int n = srch_count_for(g_srchFilter);
    if (b == JOY_B || b == JOY_MINUS) { g_screen = SC_MAIN; return; }
    if (b == JOY_Y) { do_search(); return; }
    if (b == JOY_ZL || b == JOY_ZR) {
        int step = b == JOY_ZR ? 1 : -1;
        g_srchFilter = (g_srchFilter + step + SEARCH_FILTERS) % SEARCH_FILTERS;
        g_srchSel = 0; g_srchScroll = 0;
        return;
    }
    if (b == JOY_UP) { if (g_srchSel - GCOLS >= 0) g_srchSel -= GCOLS; }
    else if (b == JOY_DOWN) { if (g_srchSel + GCOLS < n) g_srchSel += GCOLS; }
    else if (b == JOY_DLEFT) { if (g_srchSel > 0) g_srchSel--; }
    else if (b == JOY_DRIGHT) { if (g_srchSel + 1 < n) g_srchSel++; }
    else if (b == JOY_A) { int is; cJSON *it = srch_at(g_srchSel, &is); if (it) open_item(it, is); }
    else if (b == JOY_X) { int is; cJSON *it = srch_at(g_srchSel, &is); if (it) { if (is) toggle_fav_series(jint(it, "id")); else toggle_fav_item(jint(it, "id")); } }
    int row = g_srchSel / GCOLS, rowTop = 221 + row * (GCH + GGAP), rowBot = rowTop + GCH;
    if (rowBot - g_srchScroll > WIN_H - 52) g_srchScroll = rowBot - (WIN_H - 52) + 16;
    if (rowTop - g_srchScroll < 221) g_srchScroll = rowTop - 221;
    if (g_srchScroll < 0) g_srchScroll = 0;
}
static int prompt_next_episode(cJSON *episode, cJSON *series) {
    Uint32 deadline = SDL_GetTicks() + 5000;
    while (g_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) { g_running = 0; return 0; }
            if (event.type == SDL_FINGERDOWN) {
                int x = (int)(event.tfinger.x * WIN_W);
                int y = (int)(event.tfinger.y * WIN_H);
                if (y >= 456 && y < 510) {
                    if (x >= 258 && x < 558) return 1;
                    if (x >= 582 && x < 882) return 0;
                }
                continue;
            }
            if (event.type != SDL_JOYBUTTONDOWN) continue;
            if (event.jbutton.button == JOY_A) return 1;
            if (event.jbutton.button == JOY_B || event.jbutton.button == JOY_MINUS) return 0;
        }
        Uint32 now = SDL_GetTicks();
        if ((Sint32)(deadline - now) <= 0) return 1;
        int remaining = (int)((deadline - now + 999) / 1000);
        SDL_SetRenderDrawColor(gRen, C_BG.r, C_BG.g, C_BG.b, 255); SDL_RenderClear(gRen);
        SDL_Texture *backdrop = cover_get(series ? jstr(series, "backdrop") : jstr(episode, "cover"));
        if (!backdrop && series) backdrop = cover_get(jstr(series, "logo"));
        if (backdrop) {
            SDL_Rect bg = {0, 0, WIN_W, WIN_H}; ui_cover(backdrop, &bg);
            fill_rect(0, 0, WIN_W, WIN_H, (SDL_Color){7, 9, 15, 224});
        }
        ui_header("NPLAY PLAYER", "Proximo episodio", "B Cancelar");
        ui_panel(210, 170, WIN_W - 420, 360, C_ACC2);
        text_draw(gRen, "A SEGUIR", 258, 212, C_ACC2, 0);
        const char *series_title = series ? jstr(series, "title") : jstr(episode, "series_title");
        text_clip(series_title ? series_title : "Serie",
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
static void fetch_episode_context(int series_id, int finished_item_id,
                                  int first_in_group) {
    if (series_id <= 0) return;
    open_series(series_id);
    // A criacao da thread pode falhar antes de existir uma consulta. Evite
    // reaplicar esse autoavanco quando o usuario abrir a serie mais tarde.
    if (g_fetch_current.kind != FETCH_SERIES && g_fetch_queued.kind != FETCH_SERIES) return;
    g_episode_pending.active = 1;
    g_episode_pending.series_id = series_id;
    g_episode_pending.finished_item_id = finished_item_id;
    g_episode_pending.first_in_group = first_in_group;
}

// Return the episode to play, or zero when the user declined, there is no next
// episode, or its series detail is being loaded asynchronously. The series
// screen remains visible after completion even when autoplay is disabled.
static int choose_next_episode(int series_id, int finished_item_id, int first_in_group,
                               int allow_refresh, char *title, size_t title_cap) {
    if (series_id <= 0) return 0;
    if (!g_ser || jint(ser_obj(), "id") != series_id) {
        if (allow_refresh) fetch_episode_context(series_id, finished_item_id, first_in_group);
        return 0;
    }
    g_screen = SC_SERIES;
    EpisodeNext next = first_in_group ? episode_first(g_ser) :
                                       episode_after(g_ser, finished_item_id);
    if (!first_in_group && !next.found_current) {
        if (allow_refresh) fetch_episode_context(series_id, finished_item_id, 0);
        else toast("Episodio nao encontrado nesta serie");
        return 0;
    }
    if (next.item_id > 0) {
        g_seasonIdx = next.season_index;
        g_epSel = ser_grouped() ? next.flat_index : next.episode_index;
        g_epScroll = 0;
        g_ep_plot_id = -1;
        if (!g_pref_autoplay) return 0;
        cJSON *episode = ser_ep_at(g_epSel);
        if (!episode || jint(episode, "id") != next.item_id) return 0;
        if (!prompt_next_episode(episode, ser_obj())) return 0;
        snprintf(title, title_cap, "%s", ep_display_title(episode));
        return next.item_id;
    }
    if (!first_in_group && next.series_id > 0 && next.series_id != series_id) {
        fetch_episode_context(next.series_id, 0, 1);
    }
    return 0;
}

static void play_episode_sequence(int item_id, int series_id, const char *title) {
    if (item_id <= 0) return;
    char current_title[256];
    snprintf(current_title, sizeof(current_title), "%s", title && title[0] ? title : "Episodio");
    while (g_running && item_id > 0) {
        if (resolve_and_play(item_id, current_title) != 1) return;
        char next_title[256] = {0};
        int next_id = choose_next_episode(series_id, item_id, 0, 1,
                                          next_title, sizeof(next_title));
        if (next_id <= 0) return;
        item_id = next_id;
        snprintf(current_title, sizeof(current_title), "%s", next_title);
    }
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
    else if (b == JOY_DLEFT) { if (g_epSel > 0) g_epSel--; }
    else if (b == JOY_DRIGHT) { if (g_epSel < nep - 1) g_epSel++; }
    else if (b == JOY_UP) { if (g_ep_plot_scroll > 0) g_ep_plot_scroll--; }
    else if (b == JOY_DOWN) { if (g_ep_plot_scroll + 4 < g_ep_plot_count) g_ep_plot_scroll++; }
    else if (b == JOY_L) {
        if (ser_grouped()) { int i = ser_group_idx(); if (i > 0) open_series(jint(cJSON_GetArrayItem(ser_group(), i - 1), "id")); }
        else if (g_seasonIdx > 0) { g_seasonIdx--; g_epSel = 0; g_epScroll = 0; }
    }
    else if (b == JOY_R) {
        if (ser_grouped()) { int i = ser_group_idx(); if (i < arr_len(ser_group()) - 1) open_series(jint(cJSON_GetArrayItem(ser_group(), i + 1), "id")); }
        else if (g_seasonIdx < season_count() - 1) { g_seasonIdx++; g_epSel = 0; g_epScroll = 0; }
    }
    else if (b == JOY_A) {
        cJSON *ep = ser_ep_at(g_epSel);
        if (ep) play_episode_sequence(jint(ep, "id"), jint(ser_obj(), "id"),
                                      ep_display_title(ep));
    }
}
static void play_history_item(cJSON *item) {
    int item_id = jint(item, "item_id");
    if (item_id <= 0) return;
    const char *kind = jstr(item, "kind");
    if (kind && !strcmp(kind, "episode")) {
        detail_capture_origin();
        play_episode_sequence(item_id, jint(item, "series_id"), jstr(item, "title"));
    } else resolve_and_play(item_id, jstr(item, "title"));
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
                int duration = jint(item, "duration_seconds");
                int action = g_history_menu_sel;
                g_history_menu = 0;
                if (action == 0) {
                    play_history_item(item);
                } else if (action == 1) {
                    if (history_set_position(item, 0) == 0) play_history_item(item);
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
                play_history_item(item);
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
                else if (id > 0) {
                    char path[96]; snprintf(path, sizeof(path), "/api/catalog/movie/%d/info", id);
                    begin_catalog_fetch(FETCH_MOVIE, path, NULL);
                }
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
                if (dl_play(j) != 1) break;
                int next = idx + 1;
                if (next >= g_dlg[g].nJobs) break;
                cJSON *next_job = dlg_job(g, next);
                if (!dl_jobs_adjacent(j, next_job)) break;
                g_dlDetSel = next;
                if (!g_pref_autoplay ||
                    !cJSON_IsTrue(cJSON_GetObjectItem(next_job, "ready")) ||
                    !prompt_next_episode(next_job, NULL)) break;
                idx = next;
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
    int row = g_dlSel / GCOLS, rowTop = 184 + row * (GCH + GGAP), rowBot = rowTop + GCH;
    if (rowBot - g_dlScroll > WIN_H - 52) g_dlScroll = rowBot - (WIN_H - 52) + 16;
    if (rowTop - g_dlScroll < 184) g_dlScroll = rowTop - 184;
    if (g_dlScroll < 0) g_dlScroll = 0;
}

// ------------------------------------------------------------- config
static int g_setSel = 0;
static int g_diag_open = 0;
static char g_diag_player_lines[6][DIAG_LINE_CAP];
static char g_diag_network_lines[2][DIAG_LINE_CAP];
static int g_diag_player_count = 0;
static int g_diag_player_total = 0;
static int g_diag_page = 0;
static int g_diag_network_count = 0;
static unsigned g_ui_frames = 0, g_ui_over_20ms = 0, g_ui_over_33ms = 0, g_ui_max_ms = 0;

static void reload_player_diagnostics(void) {
    g_diag_player_count = diag_read_player_page(g_diag_player_lines, 6,
                                                g_diag_page, &g_diag_player_total);
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
static void logout_and_restart(void) {
    store_clear_token();
    store_clear_profile_id();
    g_screen = SC_LOGIN;
    snprintf(g_status, sizeof(g_status), "Saindo da conta...");
    // Encerrar o processo tambem descarta consultas e caches de outro usuario.
    if (schedule_restart(NULL) != 0) g_restart_at = SDL_GetTicks() + 1400;
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
    text_draw(gRen, "DIAGNOSTICO DO PLAYER", 252, 92, C_ACC2, 0);
    char summary[180];
    if (has_stats) {
        snprintf(summary, sizeof(summary), "Ultimo video iniciado %dx%d  |  frames %d  |  descartados %d  |  HW %s",
                 stats.width, stats.height, stats.decoded_frames, stats.dropped_frames,
                 stats.hardware_decode ? "sim" : "nao");
        text_clip(summary, 252, 132, stats.playback_error < 0 ? C_ROSE : C_GREEN, 0, 776);
        if (stats.read_gaps + stats.sync_gaps + stats.other_gaps == stats.present_gaps)
            snprintf(summary, sizeof(summary), "Pausas %d (rede/leitura %d, sincronia %d, outros %d)  |  pior %u ms",
                     stats.present_gaps, stats.read_gaps, stats.sync_gaps,
                     stats.other_gaps, stats.worst_present_ms);
        else
            snprintf(summary, sizeof(summary), "Pausas %d  |  pior intervalo %u ms",
                     stats.present_gaps, stats.worst_present_ms);
        text_clip(summary, 252, 157, C_ACC2, 0, 776);
        unsigned audio_queue_ms = (unsigned)((unsigned long long)stats.max_audio_bytes * 1000u / 192000u);
        snprintf(summary, sizeof(summary), "Leituras >=250 ms: %d (pior %u ms)  |  audio em fila: %u ms  |  erro %d",
                 stats.slow_reads, stats.worst_read_ms, audio_queue_ms, stats.playback_error);
        text_clip(summary, 252, 184, C_ACC2, 0, 776);
        snprintf(summary, sizeof(summary), "Inicio: %u ms (abrir %u, faixas %u)",
                 stats.first_present_ms, stats.open_ms, stats.probe_ms);
        text_clip(summary, 252, 209, C_ACC2, 0, 776);
    } else text_draw(gRen, "O trace abaixo sobrevive mesmo quando o aplicativo fecha.", 252, 132, C_TEXT, 0);
    if (has_boot_stage) {
        snprintf(summary, sizeof(summary), "Ultima etapa simples: %s", boot_stage);
        text_clip(summary, 252, 236, C_ACC, 0, 776);
    }

    int last_event = g_diag_player_total - g_diag_page * 6;
    int first_event = last_event - g_diag_player_count + 1;
    snprintf(summary, sizeof(summary), "ULTIMA TENTATIVA  |  eventos %d-%d de %d",
             g_diag_player_count ? first_event : 0,
             g_diag_player_count ? last_event : 0, g_diag_player_total);
    text_draw(gRen, summary, 252, 262, C_MUT, 0);
    if (g_diag_player_count == 0) text_draw(gRen, "Nenhuma tentativa registrada nesta instalacao.", 252, 288, C_TEXT, 0);
    for (int i = 0; i < g_diag_player_count; i++)
        text_clip(g_diag_player_lines[i], 252, 288 + i * 24,
                  i == g_diag_player_count - 1 ? C_ACC : C_TEXT, 0, 776);

    text_draw(gRen, "ULTIMAS REQUISICOES (codigo / tempo / tamanho)", 252, 446, C_MUT, 0);
    if (g_diag_network_count == 0) text_draw(gRen, "Nenhuma requisicao registrada.", 252, 480, C_TEXT, 0);
    for (int i = 0; i < g_diag_network_count; i++)
        text_clip(g_diag_network_lines[i], 252, 480 + i * 32, C_TEXT, 0, 776);

    snprintf(summary, sizeof(summary), "UI nesta sessao: %u quadros  |  >20 ms: %u  |  >33 ms: %u  |  pior: %u ms",
             g_ui_frames, g_ui_over_20ms, g_ui_over_33ms, g_ui_max_ms);
    text_clip(summary, 252, 558, C_ACC2, 0, 776);
    text_clip("Fotografe esta tela apos reabrir o Nplay. Nenhuma URL assinada ou senha e gravada.",
              252, 584, C_MUT, 0, 776);
    text_center_at("Cima anteriores  |  Baixo recentes  |  X atualizar  |  B fechar",
                   252, 776, 614, C_TEXT, 0);
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
        if (g_prefs_sel == 0 && g_tab <= TAB_SAGAS) landing_invalidate(g_tab);
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
static const char *const SETTINGS_NAV[] = { "Meu perfil", "Reproducao", "Conta", "Aplicativo" };
static const char *const SETTINGS_ROWS[4][5] = {
    { "Editar meu cartao", "Trocar perfil", "Adicionar perfil", NULL },
    { "Ocultar conteudo +18", "Proximo episodio automatico", "Reduzir animacoes", "Audio preferido", NULL },
    { "Alterar senha", "E-mail de recuperacao", "Sair da conta", NULL },
    { "Buscar atualizacao", "Diagnostico do player", "Reiniciar Nplay", "Fechar Nplay", NULL }
};
static const char *const SETTINGS_DETAILS[4][5] = {
    { "Nome, avatar, cor e acesso infantil", "Escolha quem esta assistindo", "Crie outro espaco para favoritos e progresso", NULL },
    { "Remove titulos adultos da descoberta", "Continua a serie apos o fim do episodio", "Para a rotacao automatica dos destaques", "Prioridade quando ha varias versoes", NULL },
    { "Proteja o acesso com uma nova senha", "Para recuperar a conta se esquecer a senha", "Encerra o acesso neste Switch", NULL },
    { "Instale a versao mais recente pelo aplicativo", "Dados locais para investigar a reproducao", "Reabre o aplicativo", "Volta ao menu HOME", NULL }
};
static int settings_row_count(void) { return g_settings_section == 0 || g_settings_section == 2 ? 3 : 4; }
static void open_profile_editor(int id) {
    if (!profile_by_id(id)) { toast("Perfil indisponivel. Atualize a lista."); return; }
    g_profile_editor_return = g_screen;
    g_profile_edit_id = id;
    g_profile_edit_sel = 0;
    g_profile_delete_confirm = 0;
    g_profile_editor = 1;
    g_screen = SC_CONFIG;
}
static int patch_profile(cJSON *body) {
    char path[96];
    snprintf(path, sizeof(path), "/api/account/profiles/%d", g_profile_edit_id);
    char *json = cJSON_PrintUnformatted(body);
    if (!json) return 0;
    long code = api_send(path, "PATCH", json);
    free(json);
    if (code != 200) { toast("Nao foi possivel salvar o perfil"); return 0; }
    cJSON *profile = profile_by_id(g_profile_edit_id);
    if (profile) {
        for (cJSON *field = body->child; field; field = field->next)
            cJSON_ReplaceItemInObjectCaseSensitive(profile, field->string, cJSON_Duplicate(field, 1));
    }
    toast("Perfil atualizado");
    return 1;
}
static void edit_profile_action(void) {
    cJSON *profile = profile_by_id(g_profile_edit_id);
    if (!profile) { g_profile_editor = 0; return; }
    cJSON *body = NULL;
    if (g_profile_edit_sel == 0) {
        char name[31];
        if (prompt_text("Novo nome do perfil", name, sizeof(name), 0) != 0) return;
        body = cJSON_CreateObject(); cJSON_AddStringToObject(body, "name", name);
    } else if (g_profile_edit_sel == 1) {
        if (g_avatar_catalog) { g_avatar_picker = 1; g_avatar_sel = 0; g_avatar_page = 0; }
        else if (g_avatar_thread) { g_avatar_picker_await = 1; toast("Carregando avatares..."); }
        else begin_catalog_fetch(FETCH_AVATARS, "/api/account/avatars", NULL);
        return;
    } else if (g_profile_edit_sel == 2) {
        static const char *colors[] = { "#8b5cf6", "#3d9bff", "#e368aa", "#20b99a", "#ef9f51" };
        const char *current = jstr(profile, "color");
        int next = 0;
        for (int i = 0; i < 5; i++) if (current && !strcasecmp(current, colors[i])) { next = (i + 1) % 5; break; }
        body = cJSON_CreateObject(); cJSON_AddStringToObject(body, "color", colors[next]);
    } else if (g_profile_edit_sel == 3) {
        body = cJSON_CreateObject(); cJSON_AddBoolToObject(body, "is_kids", !jint(profile, "is_kids"));
    } else if (g_profile_edit_sel == 4) {
        if (g_profile_edit_id == g_profile_id) { toast("Troque de perfil antes de excluir o atual"); return; }
        if (!g_profile_delete_confirm) { g_profile_delete_confirm = 1; return; }
        char path[96]; snprintf(path, sizeof(path), "/api/account/profiles/%d", g_profile_edit_id);
        if (api_send(path, "DELETE", "{}") == 200) {
            g_profile_editor = 0; g_profile_delete_confirm = 0;
            begin_catalog_fetch(FETCH_PROFILES, "/api/account/profiles", NULL);
            toast("Perfil excluido");
        } else toast("Nao foi possivel excluir o perfil");
        return;
    }
    if (body) { patch_profile(body); cJSON_Delete(body); }
}
static void add_profile_from_settings(void) {
    if (g_screen == SC_CONFIG) g_profiles_return = SC_CONFIG;
    int limit = g_profiles ? jint(g_profiles, "limit") : 4;
    cJSON *profiles = g_profiles ? cJSON_GetObjectItemCaseSensitive(g_profiles, "profiles") : NULL;
    if (limit > 0 && arr_len(profiles) >= limit) { toast("Limite de perfis da conta atingido"); return; }
    char name[31];
    if (prompt_text("Nome do novo perfil", name, sizeof(name), 0) != 0) return;
    cJSON *body = cJSON_CreateObject(); cJSON_AddStringToObject(body, "name", name);
    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) return;
    long code = api_send("/api/account/profiles", "POST", json);
    free(json);
    if (code == 200) { toast("Perfil criado"); begin_catalog_fetch(FETCH_PROFILES, "/api/account/profiles", NULL); }
    else toast("Nao foi possivel criar o perfil");
}
static void draw_profile_editor(void) {
    if (!g_profile_editor) return;
    cJSON *profile = profile_by_id(g_profile_edit_id);
    if (!profile) return;
    fill_rect(0, 95, WIN_W, WIN_H - 95, (SDL_Color){4, 5, 12, 190});
    ui_panel(194, 82, 892, 560, C_ACC2);
    draw_profile_avatar(profile, 224, 112, 84);
    text_clip(jstr(profile, "name"), 328, 114, C_TEXT, 1, 690);
    text_draw(gRen, "Personalize seu cartao", 328, 155, C_MUT, 0);
    static const char *labels[] = { "Nome", "Avatar", "Cor do perfil", "Perfil infantil", "Excluir perfil" };
    for (int i = 0; i < 5; i++) {
        int y = 218 + i * 70;
        fill_rect(224, y, 832, 58, C_BAR);
        if (i == g_profile_edit_sel) ui_focus(220, y - 4, 840, 66);
        text_draw(gRen, labels[i], 246, y + 15, i == 4 ? C_ROSE : C_TEXT, 0);
        const char *value = i == 0 ? jstr(profile, "name") : i == 1 ? (jstr(profile, "avatar") ? "Selecionado" : "Letra do nome") :
                            i == 2 ? (jstr(profile, "color") ? jstr(profile, "color") : "Roxo") :
                            i == 3 ? (jint(profile, "is_kids") ? "Sim" : "Nao") :
                            (g_profile_delete_confirm ? "A novamente para confirmar" : "Remove favoritos e progresso");
        text_right(value ? value : "", 1028, y + 15, i == 4 ? C_ROSE : C_ACC2, 0);
    }
    text_center_at("A Alterar    Cima/baixo Navegar    B Voltar", 224, 832, 596, C_MUT, 0);
}
static void draw_avatar_picker(void) {
    if (!g_avatar_picker) return;
    fill_rect(0, 95, WIN_W, WIN_H - 95, (SDL_Color){4, 5, 12, 190});
    ui_panel(146, 76, 988, 574, C_ACC2);
    text_draw(gRen, "Escolher avatar", 176, 101, C_TEXT, 1);
    char page[72]; snprintf(page, sizeof(page), "Pagina %d de %d", g_avatar_page + 1, (g_avatar_item_n + 18) / 18);
    text_right(page, 1096, 108, C_MUT, 0);
    for (int slot = 0; slot < 18; slot++) {
        int idx = g_avatar_page * 18 + slot;
        if (idx > g_avatar_item_n) break;
        int col = slot % 6, row = slot / 6;
        int x = 177 + col * 155, y = 168 + row * 137;
        fill_rect(x, y, 133, 124, C_BAR);
        if (idx == g_avatar_sel) ui_focus(x - 3, y - 3, 139, 130);
        if (idx == 0) text_center_at("Sem avatar", x + 4, 125, y + 49, C_TEXT, 0);
        else {
            cJSON *item = g_avatar_items[idx - 1];
            SDL_Texture *tex = cover_get(jstr(item, "url"));
            if (tex) { SDL_Rect dst = { x + 24, y + 7, 84, 84 }; ui_cover(tex, &dst); }
            else text_center_at("...", x + 4, 125, y + 39, C_MUT, 1);
            text_clip(jstr(item, "label"), x + 6, y + 98, C_MUT, 2, 121);
        }
    }
    text_center_at("A Escolher    L/R Pagina    B Voltar", 176, 928, 613, C_MUT, 0);
}
static void settings_execute(void) {
    if (g_settings_section == 0) {
        if (g_setSel == 0) open_profile_editor(g_profile_id);
        else if (g_setSel == 1) { g_profiles_return = SC_CONFIG; begin_catalog_fetch(FETCH_PROFILES, "/api/account/profiles", NULL); }
        else add_profile_from_settings();
    } else if (g_settings_section == 1) {
        g_prefs_sel = g_setSel;
        save_selected_preference(1);
    } else if (g_settings_section == 2) {
        if (g_setSel == 0) {
            char current[128], next[128], confirm[128];
            if (prompt_text("Senha atual", current, sizeof(current), 1) != 0) return;
            if (prompt_text("Nova senha (4 ou mais caracteres)", next, sizeof(next), 1) != 0) { memset(current, 0, sizeof(current)); return; }
            if (prompt_text("Repita a nova senha", confirm, sizeof(confirm), 1) != 0) { memset(current, 0, sizeof(current)); memset(next, 0, sizeof(next)); return; }
            if (strlen(next) < 4 || strcmp(next, confirm)) { toast("As novas senhas nao conferem"); }
            else {
                cJSON *body = cJSON_CreateObject();
                cJSON_AddStringToObject(body, "current", current);
                cJSON_AddStringToObject(body, "next", next);
                char *json = cJSON_PrintUnformatted(body);
                cJSON_Delete(body);
                if (json) {
                    long code = api_send("/api/account/password", "PATCH", json);
                    memset(json, 0, strlen(json)); free(json);
                    toast(code == 200 ? "Senha alterada" : "Confira a senha atual e tente novamente");
                }
            }
            memset(current, 0, sizeof(current)); memset(next, 0, sizeof(next)); memset(confirm, 0, sizeof(confirm));
        } else if (g_setSel == 1) {
            char email[201];
            if (prompt_text("E-mail de recuperacao", email, sizeof(email), 0) != 0) return;
            cJSON *body = cJSON_CreateObject(); cJSON_AddStringToObject(body, "email", email);
            char *json = cJSON_PrintUnformatted(body); cJSON_Delete(body);
            if (json) {
                long code = api_send("/api/account/email", "PATCH", json); free(json);
                if (code == 200) {
                    cJSON *user = g_account_status ? cJSON_GetObjectItem(g_account_status, "user") : NULL;
                    if (user) cJSON_ReplaceItemInObjectCaseSensitive(user, "email", cJSON_CreateString(email));
                    toast("E-mail atualizado");
                } else toast("Nao foi possivel salvar o e-mail");
            }
        } else logout_and_restart();
    } else {
        if (g_setSel == 0) { snprintf(g_status, sizeof(g_status), "Verificando atualizacao..."); g_do_update = 1; }
        else if (g_setSel == 1) { g_diag_page = 0; reload_player_diagnostics(); g_diag_open = 1; }
        else if (g_setSel == 2) schedule_restart(NULL);
        else g_running = 0;
    }
}
static void draw_settings(void) {
    ui_header("NPLAY", "Configuracoes", "B Voltar");
    cJSON *active = profile_by_id(g_profile_id);
    cJSON *account = g_account_status ? cJSON_GetObjectItem(g_account_status, "user") : NULL;
    const char *name = jstr(active, "name");
    draw_profile_avatar(active, 40, 111, 64);
    text_clip(name && name[0] ? name : g_user, 120, 113, C_TEXT, 1, 560);
    char secondary[180]; snprintf(secondary, sizeof(secondary), "Conta %s  |  Perfil atual", g_user[0] ? g_user : "-");
    text_clip(secondary, 120, 152, C_MUT, 0, 600);
    char version[60]; snprintf(version, sizeof(version), "Switch v%s", APP_VERSION_STR);
    text_right(version, 1238, 135, C_MUT, 0);

    ui_panel(40, 202, 224, 406, C_ACC2);
    text_draw(gRen, "AJUSTES", 62, 224, C_ACC2, 0);
    for (int i = 0; i < 4; i++) {
        int y = 264 + i * 77;
        fill_rect(56, y, 192, 60, i == g_settings_section ? (SDL_Color){49, 42, 82, 255} : C_BAR);
        if (i == g_settings_section && !g_settings_focus) ui_focus(53, y - 3, 198, 66);
        text_draw(gRen, SETTINGS_NAV[i], 72, y + 16, i == g_settings_section ? C_TEXT : C_MUT, 0);
    }
    ui_panel(290, 202, 950, 406, C_ACC);
    const char *heading = g_settings_section == 0 ? "Seu perfil" : g_settings_section == 1 ? "Conteudo e reproducao" :
                          g_settings_section == 2 ? "Seguranca e acesso" : "Sobre o aplicativo";
    text_draw(gRen, heading, 320, 219, C_TEXT, 1);
    const char *intro = g_settings_section == 0 ? "Um perfil para cada pessoa, com historico e lista proprios." :
                        g_settings_section == 1 ? "Escolhas sincronizadas com a sua conta Nplay." :
                        g_settings_section == 2 ? "Cuide de sua conta e do acesso neste aparelho." :
                        "Atualizacao, suporte e controles do Nplay no Switch.";
    text_draw(gRen, intro, 320, 256, C_MUT, 0);
    if (g_settings_section == 2 && account) {
        cJSON *plan = cJSON_GetObjectItem(account, "plan");
        cJSON *subscription = cJSON_GetObjectItem(account, "subscription");
        const char *plan_name = plan ? jstr(plan, "name") : NULL;
        const char *status = jstr(account, "subscription_status");
        char info[180]; snprintf(info, sizeof(info), "%s  |  %s", plan_name ? plan_name : "Plano da conta", subscription_status_label(status));
        text_clip(info, 320, 285, C_ACC2, 0, 850);
        char period[160]; format_subscription_period(account, subscription, period, sizeof(period));
        text_clip(period, 320, 584, C_MUT, 2, 850);
    } else if (g_settings_section == 0) {
        int limit = g_profiles ? jint(g_profiles, "limit") : 0;
        cJSON *arr = g_profiles ? cJSON_GetObjectItem(g_profiles, "profiles") : NULL;
        char info[100]; snprintf(info, sizeof(info), "%d de %d perfis usados", arr_len(arr), limit > 0 ? limit : 4);
        text_draw(gRen, info, 320, 285, C_ACC2, 0);
    } else if (g_settings_section == 3 && g_accel_status) {
        char info[100]; snprintf(info, sizeof(info), "%d obras preparadas na sua biblioteca", jint(g_accel_status, "count"));
        text_draw(gRen, info, 320, 285, C_ACC2, 0);
    }
    int count = settings_row_count();
    for (int i = 0; i < count; i++) {
        int y = 322 + i * 65;
        fill_rect(316, y, 898, 57, C_BAR);
        if (g_settings_focus && g_setSel == i) { ui_focus(313, y - 3, 904, 63); fill_rect(316, y, 4, 57, C_ACC); }
        text_draw(gRen, SETTINGS_ROWS[g_settings_section][i], 338, y + 5,
                  g_settings_section == 2 && i == 2 ? C_ROSE : C_TEXT, 0);
        text_clip(SETTINGS_DETAILS[g_settings_section][i], 338, y + 32, C_MUT, 2, 600);
        if (g_settings_section == 1) {
            static const char *audio[] = { "Dublado", "Legendado", "Tanto faz" };
            const char *value = i == 0 ? (g_pref_hide_adult ? "Ligado" : "Desligado") :
                                i == 1 ? (g_pref_autoplay ? "Ligado" : "Desligado") :
                                i == 2 ? (g_pref_reduce_motion ? "Ligado" : "Desligado") : audio[g_pref_audio];
            text_right(value, 1188, y + 14, C_ACC2, 0);
        }
    }
    if (g_status[0]) text_clip(g_status, 48, 622, C_ACC2, 0, 1160);
    ui_footer("Esquerda/direita Secao    Cima/baixo Navegar    A Confirmar    X Diagnostico    B Voltar");
    draw_player_diagnostics();
    draw_profile_editor();
    draw_avatar_picker();
}
static void input_settings(int b) {
    if (g_avatar_picker) {
        int total = g_avatar_item_n + 1;
        if (b == JOY_B || b == JOY_MINUS) { g_avatar_picker = 0; return; }
        if (b == JOY_L && g_avatar_page > 0) g_avatar_sel = (g_avatar_page - 1) * 18;
        else if (b == JOY_R && (g_avatar_page + 1) * 18 < total) g_avatar_sel = (g_avatar_page + 1) * 18;
        else if (b == JOY_DLEFT && g_avatar_sel > 0) g_avatar_sel--;
        else if (b == JOY_DRIGHT && g_avatar_sel + 1 < total) g_avatar_sel++;
        else if (b == JOY_UP && g_avatar_sel >= 6) g_avatar_sel -= 6;
        else if (b == JOY_DOWN && g_avatar_sel + 6 < total) g_avatar_sel += 6;
        else if (b == JOY_A) {
            const char *key = g_avatar_sel == 0 ? "" : jstr(g_avatar_items[g_avatar_sel - 1], "k");
            cJSON *body = cJSON_CreateObject(); cJSON_AddStringToObject(body, "avatar", key ? key : "");
            if (patch_profile(body)) g_avatar_picker = 0;
            cJSON_Delete(body);
        }
        g_avatar_page = g_avatar_sel / 18;
        return;
    }
    if (g_profile_editor) {
        if (b == JOY_B || b == JOY_MINUS) {
            g_profile_editor = 0; g_profile_delete_confirm = 0; g_avatar_picker_await = 0;
            g_screen = g_profile_editor_return;
        } else if (b == JOY_UP && g_profile_edit_sel > 0) { g_profile_edit_sel--; g_profile_delete_confirm = 0; }
        else if (b == JOY_DOWN && g_profile_edit_sel < 4) { g_profile_edit_sel++; g_profile_delete_confirm = 0; }
        else if (b == JOY_A) edit_profile_action();
        return;
    }
    if (g_diag_open) {
        if (b == JOY_UP && (g_diag_page + 1) * 6 < g_diag_player_total) { g_diag_page++; reload_player_diagnostics(); }
        else if (b == JOY_DOWN && g_diag_page > 0) { g_diag_page--; reload_player_diagnostics(); }
        else if (b == JOY_X) reload_player_diagnostics();
        else if (b == JOY_A || b == JOY_B || b == JOY_MINUS) g_diag_open = 0;
        return;
    }
    if (b == JOY_B || b == JOY_MINUS) { g_screen = g_settings_return; return; }
    if (b == JOY_X) { g_diag_page = 0; reload_player_diagnostics(); g_diag_open = 1; return; }
    if (b == JOY_DLEFT) { g_settings_focus = 0; return; }
    if (b == JOY_DRIGHT) { g_settings_focus = 1; return; }
    if (!g_settings_focus) {
        if (b == JOY_UP && g_settings_section > 0) { g_settings_section--; g_setSel = 0; }
        else if (b == JOY_DOWN && g_settings_section < 3) { g_settings_section++; g_setSel = 0; }
        else if (b == JOY_A) g_settings_focus = 1;
    } else {
        if (b == JOY_UP && g_setSel > 0) g_setSel--;
        else if (b == JOY_DOWN && g_setSel + 1 < settings_row_count()) g_setSel++;
        else if (b == JOY_A) settings_execute();
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

static void draw_catalog_loading(void) {
    const char *label = g_fetch_current.kind == FETCH_PROFILES ? "Carregando perfis" :
                        g_fetch_current.kind == FETCH_AVATARS ? "Carregando avatares" :
                        g_fetch_current.kind == FETCH_SEARCH ? "Buscando titulos" :
                        g_fetch_current.kind == FETCH_SAGA ? "Abrindo saga" :
                        g_fetch_current.kind == FETCH_SERIES ? "Abrindo serie" : "Abrindo filme";
    ui_header("NPLAY", label, "B Voltar");
    ui_panel(220, 210, 840, 250, C_ACC2);
    text_center_at(label, 260, 760, 260, C_TEXT, 1);
    text_center_at(g_fetch_current.kind == FETCH_PROFILES ?
                   "Confirmando sua identidade no aparelho..." :
                   "Consultando o catalogo sem interromper os controles...",
                   260, 760, 338, C_MUT, 0);
    ui_footer(g_fetch_current.kind == FETCH_PROFILES ? "B Cancelar" :
                                                    "B Cancelar    L/R Trocar categoria depois da consulta");
}

static void draw_profiles(void) {
    ui_header("NPLAY", "Escolha seu perfil", g_profile_required ? "B Sair da conta" : "B Voltar");
    cJSON *profiles = g_profiles ? cJSON_GetObjectItemCaseSensitive(g_profiles, "profiles") : NULL;
    int count = arr_len(profiles);
    if (count <= 0) {
        ui_empty_state("Perfis indisponiveis", "A Tentar novamente   B Sair da conta");
    } else {
        int width = count * 240 - 20;
        int left = (WIN_W - width) / 2;
        for (int i = 0; i < count && i < 4; i++) {
            cJSON *profile = cJSON_GetArrayItem(profiles, i);
            int x = left + i * 240;
            const char *name = jstr(profile, "name");
            ui_panel(x, 227, 220, 240, i == g_profile_sel ? C_ACC2 : C_ACC);
            if (i == g_profile_sel) ui_focus(x - 5, 222, 230, 250);
            draw_profile_avatar(profile, x + 76, 263, 68);
            text_clip(name && name[0] ? name : "Perfil", x + 20, 365, C_TEXT, 0, 180);
            if (jint(profile, "id") == g_profile_id) text_center_at("Atual", x + 20, 180, 411, C_GREEN, 0);
        }
    }
    if (!g_profile_required && count > 0) {
        fill_rect(418, 513, 444, 48, C_CARD);
        text_center_at("X Editar o perfil selecionado", 418, 444, 522, C_TEXT, 0);
    }
    if (g_status[0]) text_center_at(g_status, 120, WIN_W - 240, 608, C_ACC, 0);
    ui_footer(g_profile_required ? "Esquerda/direita Escolher    A Entrar    B Sair da conta" :
                                 "Esquerda/direita Escolher    A Entrar    X Editar    Y Adicionar    B Voltar");
}

static void input_profiles(int b) {
    cJSON *profiles = g_profiles ? cJSON_GetObjectItemCaseSensitive(g_profiles, "profiles") : NULL;
    int count = arr_len(profiles);
    if (b == JOY_DLEFT && g_profile_sel > 0) g_profile_sel--;
    else if (b == JOY_DRIGHT && g_profile_sel + 1 < count && g_profile_sel < 3) g_profile_sel++;
    else if (b == JOY_B || b == JOY_MINUS) {
        if (g_profile_required) logout_and_restart();
        else g_screen = g_profiles_return;
    } else if (b == JOY_PLUS) {
        g_running = 0;
    } else if (b == JOY_A) {
        if (count <= 0) { begin_catalog_fetch(FETCH_PROFILES, "/api/account/profiles", NULL); return; }
        int selected = jint(cJSON_GetArrayItem(profiles, g_profile_sel), "id");
        if (selected <= 0) return;
        if (selected == g_profile_id && !g_profile_required) { g_screen = g_profiles_return; return; }
        if (!store_save_profile_id(selected)) { toast("Nao foi possivel salvar o perfil na microSD"); return; }
        if (g_profile_required) {
            g_profile_required = 0; g_profile_id = selected;
            net_set_profile_id(selected);
            store_select_profile(selected, g_user);
            load_favs(); g_screen = SC_MAIN; enter_tab(0);
        } else {
            // Finaliza as requisicoes do perfil antigo no encerramento. O novo
            // header e os caches locais entram apenas no processo reiniciado.
            if (schedule_restart(NULL) != 0) {
                snprintf(g_status, sizeof(g_status), "Perfil salvo. Abra o Nplay novamente.");
                g_restart_at = SDL_GetTicks() + 2600;
            }
        }
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
    if (g_profile_menu && (g_screen == SC_MAIN || g_screen == SC_SEARCH)) {
        if (b == JOY_B || b == JOY_MINUS) g_profile_menu = 0;
        else if (b == JOY_UP && g_profile_menu_sel > 0) g_profile_menu_sel--;
        else if (b == JOY_DOWN && g_profile_menu_sel < 2) g_profile_menu_sel++;
        else if (b == JOY_A) {
            int action = g_profile_menu_sel;
            g_profile_menu = 0;
            if (action == 0) { g_profiles_return = g_screen; begin_catalog_fetch(FETCH_PROFILES, "/api/account/profiles", NULL); }
            else if (action == 1) {
                g_settings_section = 0; g_settings_focus = 1; g_setSel = 0;
                g_settings_return = g_screen;
                load_settings_status(); g_screen = SC_CONFIG;
            } else logout_and_restart();
        }
        return;
    }
    if (g_screen == SC_LOGIN) {
        if (b == JOY_A) { if (do_login() == 0) {
            store_clear_profile_id(); g_profile_id = 0; net_set_profile_id(0);
            g_profile_required = 1;
            begin_catalog_fetch(FETCH_PROFILES, "/api/account/profiles", NULL);
        } }
        else if (b == JOY_PLUS) g_running = 0;
    } else if (g_screen == SC_MAIN) {
        if (g_tab == TAB_SAGAS && (b == JOY_ZL || b == JOY_ZR)) input_sagas(b);
        else if (b == JOY_L || b == JOY_ZL) enter_tab((g_tab - 1 + NTABS) % NTABS);
        else if (b == JOY_R || b == JOY_ZR) enter_tab((g_tab + 1) % NTABS);
        else if (b == JOY_PLUS) g_running = 0;
        else if (b == JOY_MINUS) { g_profile_menu_sel = 0; g_profile_menu = 1; }
        else if (b == JOY_Y) do_search();
        else if (b == JOY_A && g_tab <= TAB_SAGAS && !g_land && !g_land_thread) load_landing(g_tab);
        else if (g_tab == TAB_DOWNLOADS) input_downloads(b);
        else if (g_tab == TAB_SAGAS) input_sagas(b);
        else input_landing(b);
    } else if (g_screen == SC_SERIES) {
        input_series(b);
    } else if (g_screen == SC_SEARCH) {
        if (b == JOY_MINUS) { g_profile_menu_sel = 0; g_profile_menu = 1; }
        else input_search(b);
    } else if (g_screen == SC_CONFIG) {
        input_settings(b);
    } else if (g_screen == SC_MOVIE) {
        input_movie(b);
    } else if (g_screen == SC_PROFILES) {
        if (b == JOY_X) {
            cJSON *profiles = g_profiles ? cJSON_GetObjectItemCaseSensitive(g_profiles, "profiles") : NULL;
            if (g_profile_sel < arr_len(profiles)) open_profile_editor(jint(cJSON_GetArrayItem(profiles, g_profile_sel), "id"));
        } else if (b == JOY_Y && !g_profile_required) add_profile_from_settings();
        else input_profiles(b);
    } else if (g_screen == SC_LOADING) {
        if (b == JOY_B || b == JOY_MINUS) {
            g_episode_pending.active = 0;
            Screen origin = g_fetch_queued.kind != FETCH_NONE ? g_fetch_queued.origin : g_fetch_current.origin;
            if (g_fetch_current.kind == FETCH_PROFILES || g_fetch_queued.kind == FETCH_PROFILES)
                origin = g_profile_required ? SC_PROFILES : g_profiles_return;
            g_fetch_queued.kind = FETCH_NONE;
            g_fetch_discard = 1;
            catalog_fetch_cancel(&g_fetch);
            g_screen = origin;
        }
    } else if (g_screen == SC_SAGA) {
        input_saga_detail(b);
    }
}

// Hit-tests da aba Historico seguem as mesmas coordenadas usadas no desenho.
// Um toque fora de um card nao deve confirmar a selecao antiga (e abrir outro
// video sem que o usuario o tenha escolhido).
static void handle_history_touch(int x, int y) {
    if (g_dlView == 0) {
        if (g_history_menu) {
            if (x >= 324 && x < 956 && y >= 278 && y < 520) {
                int row = (y - 278) / 64;
                if ((y - 278) % 64 < 50 && row < 4) {
                    g_history_menu_sel = row;
                    input_downloads(JOY_A);
                }
            }
            return;
        }
        int nh = arr_len(history_items());
        if (y >= 152 && y < 152 + HIST_CH + 52) {
            int scroll = horizontal_scroll(g_history_sel, HIST_CW, HIST_GAP);
            int relative = x - 54 + scroll;
            if (relative >= 0) {
                int index = relative / (HIST_CW + HIST_GAP);
                if (index < nh && relative % (HIST_CW + HIST_GAP) < HIST_CW) {
                    g_history_zone = 0;
                    g_history_sel = index;
                    input_downloads(JOY_A);
                }
            }
            return;
        }
        if (y >= 481 && y < 639) {
            int scroll = horizontal_scroll(g_list_sel, 270, 18);
            int relative = x - 54 + scroll;
            if (relative >= 0) {
                int index = relative / 288;
                if (index < store_media_list_count() + 2 && relative % 288 < 270) {
                    g_history_zone = 1;
                    g_list_sel = index;
                    input_downloads(JOY_A);
                }
            }
        }
        return;
    }
    if (g_dlView == 1) {
        if (g_dlGroup < 0 || g_dlGroup >= g_dlgN) return;
        if (x < 32 || x >= WIN_W - 32 || y < 195 || y >= WIN_H - 52) return;
        int visible = (WIN_H - 195 - 56) / 46;
        int index = g_dlDetScroll + (y - 195) / 46;
        if (index < g_dlg[g_dlGroup].nJobs && index < g_dlDetScroll + visible) {
            g_dlDetSel = index;
            input_downloads(JOY_A);
        }
        return;
    }
    if (x < GMX || y >= WIN_H - 52) return;
    int top = g_dlView == 2 ? 184 : 194;
    int scroll = g_dlScroll;
    if (g_dlView == 3) {
        int selected_bottom = top + (g_list_item_sel / GCOLS) * (GCH + GGAP) + GCH;
        scroll = selected_bottom > WIN_H - 52 ? selected_bottom - (WIN_H - 52) + 16 : 0;
    }
    int relative_x = x - GMX, relative_y = y + scroll - top;
    if (relative_y < 0) return;
    int col = relative_x / (GCW + GGAP), row = relative_y / (GCH + GGAP);
    if (col >= GCOLS || relative_x % (GCW + GGAP) >= GCW ||
        relative_y % (GCH + GGAP) >= GCH) return;
    int index = row * GCOLS + col;
    int count = g_dlView == 2 ? g_dlgN : store_media_list_item_count(g_open_list);
    if (index >= count) return;
    if (g_dlView == 2) g_dlSel = index;
    else g_list_item_sel = index;
    input_downloads(JOY_A);
}

static void handle_touch_tap(int x, int y) {
    if (g_screen == SC_LOGIN) { if (y >= 300 && y < 395) handle_button(JOY_A); return; }
    if (g_screen == SC_LOADING) { if (y < 92) handle_button(JOY_B); return; }
    if ((g_screen == SC_MAIN || g_screen == SC_SEARCH) && g_profile_menu) {
        if (x >= 868 && x < 1210 && y >= 190 && y < 368) {
            int index = (y - 190) / 63;
            if (index < 3 && (y - 190) % 63 < 52) { g_profile_menu_sel = index; handle_button(JOY_A); }
        } else handle_button(JOY_B);
        return;
    }
    if (g_screen == SC_MAIN || g_screen == SC_SEARCH) {
        if (y < 95) {
            int tx = 255;
            for (int t = 0; t < NTABS; t++) {
                int w = 0, h = 0;
                text_cached(gRen, TAB_NAME[t], t == g_tab ? C_ACC : C_TEXT, 0, &w, &h);
                if (x >= tx && x <= tx + w + 22) { g_screen = SC_MAIN; enter_tab(t); return; }
                tx += w + 33;
            }
            if (x >= 1160) { g_profile_menu_sel = 0; g_profile_menu = 1; }
            else if (x >= 1050) do_search();
            return;
        }
        if (g_screen == SC_SEARCH) {
            if (y >= 161 && y < 197) {
                static const char *filters[] = { "Tudo", "Filmes", "Series", "Animes" };
                int chip_x = 54;
                for (int i = 0; i < SEARCH_FILTERS; i++) {
                    char label[48];
                    snprintf(label, sizeof(label), "%s  %d", filters[i], srch_count_for(i));
                    int tw = 0, th = 0;
                    text_cached(gRen, label, C_TEXT, 0, &tw, &th);
                    int chip_w = tw + 28;
                    if (x >= chip_x && x < chip_x + chip_w) {
                        g_srchFilter = i; g_srchSel = 0; g_srchScroll = 0;
                        return;
                    }
                    chip_x += chip_w + 12;
                }
            }
            if (x >= GMX && y >= 221 && y < WIN_H - 52) {
                int col = (x - GMX) / (GCW + GGAP);
                int row = (y + g_srchScroll - 221) / (GCH + GGAP);
                if (col >= 0 && col < GCOLS && row >= 0 &&
                    (x - GMX) % (GCW + GGAP) < GCW &&
                    (y + g_srchScroll - 221) % (GCH + GGAP) < GCH) {
                    int index = row * GCOLS + col;
                    if (index < srch_count_for(g_srchFilter)) {
                        g_srchSel = index;
                        input_search(JOY_A);
                    }
                }
            }
            return;
        }
        if (g_tab == TAB_SAGAS) {
            if (x < 54 || y < 178 || y >= WIN_H - 52 || y < 185 - g_saga_scroll) return;
            int col = (x - 54) / 390, row = (y + g_saga_scroll - 185) / 231;
            if (col >= 0 && col < 3 && row >= 0 &&
                x < 54 + col * 390 + 366 && y + g_saga_scroll < 185 + row * 231 + 211) {
                int index = row * 3 + col;
                if (index < arr_len(saga_groups())) {
                    if (index != g_saga_sel) { g_saga_sel = index; g_saga_variant_sel = 0; }
                    input_sagas(JOY_A);
                }
            }
            return;
        }
        if (g_tab == TAB_DOWNLOADS) { handle_history_touch(x, y); return; }
        int nh = hero_count(), hy = 110 - g_homeScroll;
        if (nh > 0 && y >= hy && y < hy + HERO_H && x >= 52 && x < WIN_W - 52) {
            g_railSel = -1; input_landing(JOY_A); return;
        }
        int first_y = (nh > 0 ? RAILS_TOP : 125);
        for (int r = 0; r < g_railsN; r++) {
            int ry = first_y + r * RAIL_STEP + 30 - g_homeScroll;
            if (y < ry || y >= ry + RCH + 52) continue;
            int row_scroll = 0;
            if (r == g_railSel) {
                int sel_x = 54 + g_railItem * (RCW + RGAP);
                if (sel_x + RCW > WIN_W - 54) row_scroll = sel_x + RCW - (WIN_W - 54);
                if (sel_x - row_scroll < 54) row_scroll = sel_x - 54;
                if (row_scroll < 0) row_scroll = 0;
            }
            int relative = x - 54 + row_scroll;
            if (relative < 0) return;
            int index = relative / (RCW + RGAP);
            if (relative % (RCW + RGAP) >= RCW || index >= g_rails[r].count) return;
            g_railSel = r; g_railItem = index;
            cJSON *item = cJSON_GetArrayItem(g_rails[r].arr, index);
            open_item(item, catalog_item_is_series(item, g_rails[r].is_series));
            return;
        }
        int search_y = first_y + g_railsN * RAIL_STEP - g_homeScroll;
        if (y >= search_y && y < search_y + 112) do_search();
        return;
    }
    if (y < 95 && x < 260) { handle_button(JOY_B); return; }
    if (g_screen == SC_PROFILES) {
        cJSON *profiles = g_profiles ? cJSON_GetObjectItemCaseSensitive(g_profiles, "profiles") : NULL;
        int count = arr_len(profiles);
        if (count > 4) count = 4;
        int left = (WIN_W - (count * 240 - 20)) / 2;
        if (y >= 227 && y < 467 && x >= left) {
            int relative = x - left, index = relative / 240;
            if (index < count && relative % 240 < 220) {
                g_profile_sel = index;
                input_profiles(JOY_A);
            }
        } else if (count <= 0 && y >= 220 && y < 540) input_profiles(JOY_A);
        else if (!g_profile_required && y >= 513 && y < 561 && x >= 418 && x < 862) handle_button(JOY_X);
        return;
    }
    if (g_screen == SC_CONFIG) {
        if (g_avatar_picker) {
            if (x >= 177 && x < 1107 && y >= 168 && y < 566) {
                int col = (x - 177) / 155, row = (y - 168) / 137;
                int index = g_avatar_page * 18 + row * 6 + col;
                if (col < 6 && row < 3 && (x - 177) % 155 < 133 && (y - 168) % 137 < 124 && index <= g_avatar_item_n) {
                    g_avatar_sel = index; input_settings(JOY_A);
                }
            } else input_settings(JOY_B);
            return;
        }
        if (g_profile_editor) {
            if (x >= 224 && x < 1056 && y >= 218 && y < 556) {
                int index = (y - 218) / 70;
                if (index < 5 && (y - 218) % 70 < 58) { g_profile_edit_sel = index; input_settings(JOY_A); }
            } else input_settings(JOY_B);
            return;
        }
        if (g_diag_open) { input_settings(JOY_B); return; }
        if (x >= 56 && x < 248 && y >= 264 && y < 555) {
            int section = (y - 264) / 77;
            if (section < 4 && (y - 264) % 77 < 60) {
                g_settings_section = section; g_settings_focus = 1; g_setSel = 0;
            }
        } else if (x >= 316 && x < 1214 && y >= 322 && y < 582) {
            int index = (y - 322) / 65;
            if (index < settings_row_count() && (y - 322) % 65 < 57) {
                g_settings_focus = 1; g_setSel = index; input_settings(JOY_A);
            }
        }
        return;
    }
    if (g_screen == SC_SERIES && !g_dlmenu) {
        if (y >= 424 && y < 468) {
            if (x >= 52 && x < 229) input_series(JOY_A);
            else if (x >= 243 && x < 449) input_series(JOY_X);
            else if (x >= 463 && x < 691) input_series(JOY_ZR);
            return;
        }
        if (y >= 511 && y < 545) {
            int index = (x - 54) / 160;
            int current = ser_grouped() ? ser_group_idx() : g_seasonIdx;
            int first = current > 5 ? current - 5 : 0;
            int target = first + index;
            if (x >= 54 && index >= 0 && target < ser_nseasons()) {
                if (ser_grouped()) open_series(jint(cJSON_GetArrayItem(ser_group(), target), "id"));
                else { g_seasonIdx = target; g_epSel = 0; g_ep_plot_id = -1; }
            }
            return;
        }
        if (y >= 551 && y < 656) {
            int count = ser_nep();
            int start = g_epSel - 2;
            if (start < 0) start = 0;
            if (start > count - 5) start = count > 5 ? count - 5 : 0;
            int col = (x - 54) / (RCW + RGAP);
            int index = start + col;
            if (x >= 54 && col >= 0 && col < 5 && index < count &&
                (x - 54) % (RCW + RGAP) < RCW) g_epSel = index;
            return;
        }
        return;
    }
    if (g_screen == SC_SAGA && y >= 411 && y < 668) {
        int count = arr_len(cJSON_GetObjectItem(g_saga_detail, "items"));
        int start = g_saga_item_sel - 2;
        if (start < 0) start = 0;
        if (start > count - 5) start = count > 5 ? count - 5 : 0;
        int col = (x - 54) / (RCW + RGAP), index = start + col;
        if (x >= 54 && col >= 0 && col < 5 && index < count &&
            (x - 54) % (RCW + RGAP) < RCW) {
            g_saga_item_sel = index;
            input_saga_detail(JOY_A);
        }
        return;
    }
    if (g_screen == SC_MOVIE && y >= 350 && y < 398) {
        if (x >= 280 && x < 452) movie_touch_action(0);
        else if (x >= 468 && x < 698) movie_touch_action(1);
    }
    if (g_screen == SC_MOVIE && y >= 461 && y < 659) movie_touch_related(x, y);
}
static void handle_touch_swipe(int x, int y, int dx, int dy) {
    if (g_screen == SC_SEARCH && y >= 205) {
        int rows = (srch_count_for(g_srchFilter) + GCOLS - 1) / GCOLS;
        int max_scroll = 221 + rows * (GCH + GGAP) - GGAP - (WIN_H - 52);
        if (max_scroll < 0) max_scroll = 0;
        g_srchScroll -= dy;
        if (g_srchScroll < 0) g_srchScroll = 0;
        if (g_srchScroll > max_scroll) g_srchScroll = max_scroll;
        return;
    }
    if (g_screen == SC_MAIN && y < 105 && dx > 90) { enter_tab((g_tab - 1 + NTABS) % NTABS); return; }
    if (g_screen == SC_MAIN && y < 105 && dx < -90) { enter_tab((g_tab + 1) % NTABS); return; }
    if (g_screen == SC_SERIES && !g_dlmenu) {
        if (y >= 235 && y < 416 && (dy > 45 || dy < -45)) {
            input_series(dy < 0 ? JOY_DOWN : JOY_UP); return;
        }
        if (dx > 55 || dx < -55) input_series(dx < 0 ? JOY_DRIGHT : JOY_DLEFT);
        return;
    }
    if (g_screen != SC_MAIN) return;
    if (g_tab == TAB_SAGAS) {
        g_saga_scroll -= dy;
        int rows = (arr_len(saga_groups()) + 2) / 3;
        int max_scroll = 185 + rows * 231 - (WIN_H - 52);
        if (max_scroll < 0) max_scroll = 0;
        if (g_saga_scroll < 0) g_saga_scroll = 0;
        if (g_saga_scroll > max_scroll) g_saga_scroll = max_scroll;
        return;
    }
    if (g_tab == TAB_DOWNLOADS) return;
    if ((dx > 65 || dx < -65) && y < 110 + HERO_H) {
        input_landing(dx < 0 ? JOY_DRIGHT : JOY_DLEFT); return;
    }
    if (dx > 65 || dx < -65) {
        int first_y = hero_count() > 0 ? RAILS_TOP : 125;
        for (int r = 0; r < g_railsN; r++) {
            int row_y = first_y + r * RAIL_STEP + 30 - g_homeScroll;
            if (y < row_y || y >= row_y + RCH + 52) continue;
            if (g_railSel != r) { g_railSel = r; g_railItem = 0; }
            input_landing(dx < 0 ? JOY_DRIGHT : JOY_DLEFT);
            return;
        }
    }
    if (dy > 35 || dy < -35) {
        int nh = hero_count();
        int max_scroll = (nh > 0 ? RAILS_TOP : 125) + g_railsN * RAIL_STEP + 112 - (WIN_H - 52);
        g_homeScroll -= dy;
        if (g_homeScroll < 0) g_homeScroll = 0;
        if (g_homeScroll > max_scroll && max_scroll > 0) g_homeScroll = max_scroll;
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
    SDL_RWops *brand_data = SDL_RWFromConstMem(brand_bin, (int)brand_bin_size);
    if (brand_data) {
        SDL_Surface *brand_surface = IMG_Load_RW(brand_data, 1);
        if (brand_surface) { g_brand = SDL_CreateTextureFromSurface(gRen, brand_surface); SDL_FreeSurface(brand_surface); }
    }
    SDL_InitSubSystem(SDL_INIT_JOYSTICK);
    g_joy = SDL_JoystickOpen(0);

    text_init(); net_init(); store_init();

    g_cov_mtx = SDL_CreateMutex(); g_q_mtx = SDL_CreateMutex(); g_ready_mtx = SDL_CreateMutex();
    g_q_sem = SDL_CreateSemaphore(0);
    SDL_Thread *wk[3];
    for (int i = 0; i < 3; i++) wk[i] = SDL_CreateThread(cover_worker, "cov", NULL);

    store_load_token(g_token, sizeof(g_token));
    store_load_user(g_user, sizeof(g_user));
    store_load_profile_id(&g_profile_id);
    net_set_profile_id(g_profile_id);
    // Catalogo primeiro: a consulta de conta/configuracoes so e iniciada quando
    // o usuario abre Config. Isso evita duas requisicoes HTTPS concorrentes no
    // boot, um ponto especialmente caro no limite de memoria/rede do Switch.
    if (g_token[0]) { g_profile_required = 1; begin_catalog_fetch(FETCH_PROFILES, "/api/account/profiles", NULL); }

    while (appletMainLoop() && g_running) {
        SDL_Event e;
        static SDL_FingerID touch_id = 0;
        static int touch_active = 0, touch_x = 0, touch_y = 0;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { g_running = 0; break; }
            if (e.type == SDL_FINGERDOWN) {
                touch_id = e.tfinger.fingerId;
                touch_x = (int)(e.tfinger.x * WIN_W);
                touch_y = (int)(e.tfinger.y * WIN_H);
                touch_active = 1;
                continue;
            }
            if (e.type == SDL_FINGERUP && touch_active && e.tfinger.fingerId == touch_id) {
                int x = (int)(e.tfinger.x * WIN_W), y = (int)(e.tfinger.y * WIN_H);
                int dx = x - touch_x, dy = y - touch_y;
                touch_active = 0;
                if (dx > 30 || dx < -30 || dy > 30 || dy < -30)
                    handle_touch_swipe(touch_x, touch_y, dx, dy);
                else handle_touch_tap(x, y);
                continue;
            }
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
        // Mede apenas trabalho da UI: a reproducao e seus waits ocorrem no
        // tratamento de entrada acima e nao contaminam a contagem de quadros.
        Uint32 ui_frame_start = SDL_GetTicks();

        // destaque rotativo nas abas 0..4 (a cada ~6s)
        if (!g_pref_reduce_motion && g_screen == SC_MAIN && g_tab < TAB_SAGAS && g_land && SDL_GetTicks() > g_hero_next) {
            int nh = hero_count();
            if (nh > 0) g_heroIdx = (g_heroIdx + 1) % nh;
            g_hero_next = SDL_GetTicks() + 6000;
        }
        // atualiza a lista de downloads sozinho
        if (g_screen == SC_MAIN && g_tab == TAB_DOWNLOADS && SDL_GetTicks() > g_dl_next) {
            load_downloads(); g_dl_next = SDL_GetTicks() + 2000;
        }
        pump_downloads();
        pump_dl_done();
        pump_favs();
        pump_history();
        pump_settings_status();
        pump_avatar_fetch();
        pump_landing();
        pump_catalog_fetch();
        if (!g_avatar_attempted && g_avatar_due && SDL_GetTicks() >= g_avatar_due &&
            g_screen == SC_MAIN && g_land) {
            g_avatar_attempted = 1;
            const char *avatar = jstr(profile_by_id(g_profile_id), "avatar");
            if (avatar && !strncmp(avatar, "char:", 5)) start_avatar_fetch();
        }
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
        else if (g_screen == SC_SAGA) draw_saga_detail();
        else if (g_screen == SC_SEARCH) draw_search();
        else if (g_screen == SC_PROFILES) draw_profiles();
        else if (g_screen == SC_LOADING) draw_catalog_loading();
        else { if (g_tab == TAB_DOWNLOADS) draw_downloads(); else if (g_tab == TAB_SAGAS) draw_sagas(); else draw_landing(); }

        draw_profile_menu();
        if (g_toast[0] && SDL_GetTicks() < g_toast_until) {
            int w = 0, h = 0;
            SDL_Texture *tx = text_cached(gRen, g_toast, C_TEXT, 0, &w, &h);
            fill_rect(WIN_W / 2 - w / 2 - 18, WIN_H - 120, w + 36, h + 20, C_BAR);
            fill_rect(WIN_W / 2 - w / 2 - 18, WIN_H - 120, 4, h + 20, C_ACC);
            if (tx) { SDL_Rect d = { WIN_W / 2 - w / 2, WIN_H - 110, w, h }; SDL_RenderCopy(gRen, tx, NULL, &d); }
        }
        SDL_RenderPresent(gRen);
        unsigned ui_frame_ms = SDL_GetTicks() - ui_frame_start;
        g_ui_frames++;
        if (ui_frame_ms > 20) g_ui_over_20ms++;
        if (ui_frame_ms > 33) g_ui_over_33ms++;
        if (ui_frame_ms > g_ui_max_ms) g_ui_max_ms = ui_frame_ms;
        if (g_do_update) { g_do_update = 0; run_update(); }
        if (g_restart_at && SDL_GetTicks() >= g_restart_at) g_running = 0;
    }

    g_run = 0;
    for (int i = 0; i < 3; i++) SDL_SemPost(g_q_sem);
    for (int i = 0; i < 3; i++) SDL_WaitThread(wk[i], NULL);
    if (g_dl_thread) { SDL_WaitThread(g_dl_thread, NULL); g_dl_thread = NULL; }
    catalog_fetch_dispose(&g_favs_fetch);
    catalog_fetch_dispose(&g_dl_done_fetch);
    if (g_dl_pending) { cJSON_Delete(g_dl_pending); g_dl_pending = NULL; }
    if (g_history_thread) { SDL_WaitThread(g_history_thread, NULL); g_history_thread = NULL; }
    if (g_watchlater_thread) { SDL_WaitThread(g_watchlater_thread, NULL); g_watchlater_thread = NULL; }
    if (g_history_pending) { cJSON_Delete(g_history_pending); g_history_pending = NULL; }
    if (g_watchlater_pending) { cJSON_Delete(g_watchlater_pending); g_watchlater_pending = NULL; }
    if (g_settings_thread) { SDL_WaitThread(g_settings_thread, NULL); g_settings_thread = NULL; }
    if (g_avatar_thread) { SDL_WaitThread(g_avatar_thread, NULL); g_avatar_thread = NULL; }
    if (g_avatar_pending) { cJSON_Delete(g_avatar_pending); g_avatar_pending = NULL; }
    if (g_avatar_catalog) { cJSON_Delete(g_avatar_catalog); g_avatar_catalog = NULL; }
    if (g_account_pending) { cJSON_Delete(g_account_pending); g_account_pending = NULL; }
    if (g_settings_accel_pending) { cJSON_Delete(g_settings_accel_pending); g_settings_accel_pending = NULL; }

    if (g_download_awake) { appletSetMediaPlaybackState(false); g_download_awake = 0; }
    if (g_land_thread) { SDL_WaitThread(g_land_thread, NULL); g_land_thread = NULL; }
    if (g_land_pending) { cJSON_Delete(g_land_pending); g_land_pending = NULL; }
    catalog_fetch_dispose(&g_fetch);
    for (int i = 0; i <= TAB_SAGAS; i++) {
        if (g_land_cache[i]) { cJSON_Delete(g_land_cache[i]); g_land_cache[i] = NULL; }
    }
    if (g_saga_detail) { cJSON_Delete(g_saga_detail); g_saga_detail = NULL; }
    g_land = NULL;
    if (g_search) cJSON_Delete(g_search);
    ui_popcorn_release();
    if (g_profiles) cJSON_Delete(g_profiles);
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
    text_exit(); nplay_curl_avio_pool_clear(); net_exit();
    if (g_joy) SDL_JoystickClose(g_joy);
    if (g_brand) SDL_DestroyTexture(g_brand);
    SDL_DestroyRenderer(gRen); SDL_DestroyWindow(win);
    diag_exit();
    IMG_Quit(); SDL_Quit(); socketExit();
    return 0;
}
