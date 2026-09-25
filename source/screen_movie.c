#include "screen_movie.h"
#include "ui.h"
#include "text.h"
#include "api.h"
#include "genre_label.h"
#include <stdio.h>
#include <string.h>

static cJSON *g_movie = NULL;
static int g_movie_sel = 0; // 0=Assistir, 1=Minha lista
static int g_plot_scroll = 0;
static int g_movie_zone = 0; // 0=acoes, 1=relacionados
static int g_related_sel = 0;
static int g_related_panel_y = 458;

#define MOVIE_BACK_MAX 6
typedef struct {
    cJSON *movie;
    int movie_sel, plot_scroll, movie_zone, related_sel, panel_y;
} MovieBackState;
static MovieBackState g_movie_back[MOVIE_BACK_MAX];
static int g_movie_back_n = 0;

#define PLOT_LINES 3
#define PLOT_MAX_LINES 32
#define PLOT_LINE_CAP 180
static char g_plot_lines[PLOT_MAX_LINES][PLOT_LINE_CAP];
static int g_plot_line_count = 0;

// Quebra por palavras usando a largura real da fonte. Isso evita cortar a
// sinopse no meio, como acontecia com o clip de uma unica linha.
static int wrap_text(const char *text, char lines[][PLOT_LINE_CAP], int max_lines, int max_width) {
    if (!text || !text[0]) return 0;
    int count = 0;
    const char *p = text;
    while (*p && count < max_lines) {
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
        if (!*p) break;
        char line[PLOT_LINE_CAP] = {0};
        int used = 0;
        while (*p && *p != '\n' && *p != '\r') {
            while (*p == ' ' || *p == '\t') p++;
            const char *start = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
            int word_len = (int)(p - start);
            if (word_len <= 0) break;
            if (word_len >= PLOT_LINE_CAP) word_len = PLOT_LINE_CAP - 1;
            char candidate[PLOT_LINE_CAP];
            if (used) snprintf(candidate, sizeof(candidate), "%s %.*s", line, word_len, start);
            else snprintf(candidate, sizeof(candidate), "%.*s", word_len, start);
            int width = 0, height = 0;
            text_cached(gRen, candidate, C_TEXT, 0, &width, &height);
            if (used && width > max_width) break;
            snprintf(line, sizeof(line), "%s", candidate);
            used = (int)strlen(line);
        }
        if (used) snprintf(lines[count++], PLOT_LINE_CAP, "%s", line);
        else if (*p) p++; // garante progresso em entrada inesperada
    }
    return count;
}

static void rebuild_movie_plot(void) {
    memset(g_plot_lines, 0, sizeof(g_plot_lines));
    const char *plot = jstr(g_movie, "plot");
    g_plot_line_count = wrap_text(plot ? plot : "Sinopse nao disponivel.",
                                  g_plot_lines, PLOT_MAX_LINES, WIN_W - 280 - 80);
}

static void activate_movie(cJSON *movie) {
    g_movie = movie;
    g_movie_sel = 0;
    g_plot_scroll = 0;
    g_movie_zone = 0;
    g_related_sel = 0;
    g_related_panel_y = 458;
    rebuild_movie_plot();
}

int open_movie_details_response(cJSON *response) {
    cJSON *next_movie = response ? cJSON_GetObjectItemCaseSensitive(response, "item") : NULL;
    if (!cJSON_IsObject(next_movie)) return -1;
    cJSON_DetachItemViaPointer(response, next_movie);
    close_movie_details();
    activate_movie(next_movie);
    return 0;
}

int open_related_details_response(cJSON *response) {
    cJSON *next_movie = response ? cJSON_GetObjectItemCaseSensitive(response, "item") : NULL;
    if (!cJSON_IsObject(next_movie)) return -1;
    cJSON_DetachItemViaPointer(response, next_movie);
    if (g_movie_back_n == MOVIE_BACK_MAX) {
        cJSON_Delete(g_movie_back[0].movie);
        memmove(&g_movie_back[0], &g_movie_back[1], sizeof(g_movie_back[0]) * (MOVIE_BACK_MAX - 1));
        g_movie_back_n--;
    }
    MovieBackState *back = &g_movie_back[g_movie_back_n++];
    back->movie = g_movie;
    back->movie_sel = g_movie_sel;
    back->plot_scroll = g_plot_scroll;
    back->movie_zone = g_movie_zone;
    back->related_sel = g_related_sel;
    back->panel_y = g_related_panel_y;
    g_movie = NULL;
    activate_movie(next_movie);
    return 0;
}

static int restore_previous_movie(void) {
    if (g_movie_back_n <= 0) return 0;
    if (g_movie) cJSON_Delete(g_movie);
    MovieBackState *back = &g_movie_back[--g_movie_back_n];
    g_movie = back->movie;
    g_movie_sel = back->movie_sel;
    g_plot_scroll = back->plot_scroll;
    g_movie_zone = back->movie_zone;
    g_related_sel = back->related_sel;
    g_related_panel_y = back->panel_y;
    memset(back, 0, sizeof(*back));
    rebuild_movie_plot();
    return 1;
}

void close_movie_details(void) {
    if (g_movie) { cJSON_Delete(g_movie); g_movie = NULL; }
    for (int i = 0; i < g_movie_back_n; i++) if (g_movie_back[i].movie) cJSON_Delete(g_movie_back[i].movie);
    memset(g_movie_back, 0, sizeof(g_movie_back));
    g_movie_back_n = 0;
    g_plot_scroll = 0;
    g_movie_zone = 0;
    g_related_sel = 0;
    g_related_panel_y = 458;
    g_plot_line_count = 0;
    memset(g_plot_lines, 0, sizeof(g_plot_lines));
}

static void draw_button(int x, int y, int w, const char *label, int selected) {
    fill_rect(x, y, w, 48, selected ? C_ACC : C_CARD);
    if (selected) ui_focus(x - 3, y - 3, w + 6, 54);
    else border_rect(x, y, w, 48, 1, C_MUT);
    text_center_at(label, x, w, y + 10, selected ? C_TEXT : C_MUT, 0);
}

void draw_movie(void) {
    if (!g_movie) return;
    const char *title = jstr(g_movie, "title");
    const char *logo = jstr(g_movie, "logo");
    const char *genre = jstr(g_movie, "genre");
    int year = jint(g_movie, "year");
    int duration = jint(g_movie, "duration");

    ui_header("NPLAY / FILME", NULL, "B Voltar");
    SDL_Rect hero = {52, 110, WIN_W - 104, 316};
    fill_rect(hero.x, hero.y, hero.w, hero.h, C_CARD);
    SDL_Texture *backdrop = cover_get(jstr(g_movie, "backdrop"));
    if (backdrop) ui_backdrop(backdrop, &hero);
    fill_rect(hero.x, hero.y, 5, hero.h, C_ACC);
    SDL_Texture *poster = cover_get(logo);
    SDL_Rect cr = {65, 126, 184, 276};
    fill_rect(cr.x, cr.y, cr.w, cr.h, C_BAR);
    if (poster) ui_contain(poster, &cr);
    else text_center_at("Sem capa", cr.x, cr.w, 242, C_MUT, 0);

    const int dx = 280;
    text_clip(title ? title : "Filme", dx, 136, C_TEXT, 1, WIN_W - dx - 60);
    char meta[320] = {0};
    if (year > 0) snprintf(meta + strlen(meta), sizeof(meta) - strlen(meta), "%d", year);
    if (duration > 0) snprintf(meta + strlen(meta), sizeof(meta) - strlen(meta), "%s%d min", meta[0] ? "  |  " : "", duration);
    char genres[240];
    movie_genre_label(genre, genres, sizeof(genres));
    snprintf(meta + strlen(meta), sizeof(meta) - strlen(meta), "%s%s", meta[0] ? "  |  " : "", genres);
    text_clip(meta[0] ? meta : "Informacoes ainda nao disponiveis", dx, 180, C_ACC2, 0, WIN_W - dx - 60);
    int max_scroll = g_plot_line_count > PLOT_LINES ? g_plot_line_count - PLOT_LINES : 0;
    if (g_plot_scroll > max_scroll) g_plot_scroll = max_scroll;
    for (int i = 0; i < PLOT_LINES && i + g_plot_scroll < g_plot_line_count; i++)
        text_draw(gRen, g_plot_lines[i + g_plot_scroll], dx, 224 + i * 28, C_MUT, 0);
    if (g_plot_line_count > PLOT_LINES) {
        char page[80];
        snprintf(page, sizeof(page), "Sinopse %d/%d  ·  cima/baixo", g_plot_scroll + 1, max_scroll + 1);
        text_right(page, WIN_W - 65, 314, C_MUT, 2);
    }

    int is_fav = is_fav_item(jint(g_movie, "id"));
    draw_button(dx, 350, 172, "A  Assistir", g_movie_zone == 0 && g_movie_sel == 0);
    draw_button(dx + 188, 350, 230, is_fav ? "Favoritado" : "Favoritar", g_movie_zone == 0 && g_movie_sel == 1);

    cJSON *related = cJSON_GetObjectItemCaseSensitive(g_movie, "related");
    int related_n = arr_len(related);
    if (related_n > 0) {
        text_draw(gRen, "Titulos relacionados", 54, 433, C_TEXT, 0);
        char count[48]; snprintf(count, sizeof(count), "%d titulos", related_n);
        text_right(count, WIN_W - 54, 439, C_MUT, 2);
        int start = g_related_sel - 1;
        if (start < 0) start = 0;
        if (start > related_n - 3) start = related_n > 3 ? related_n - 3 : 0;
        for (int i = start; i < related_n && i < start + 3; i++) {
            cJSON *item = cJSON_GetArrayItem(related, i);
            int x = 54 + (i - start) * 390;
            fill_rect(x, 461, 366, 198, g_movie_zone == 1 && i == g_related_sel ?
                      (SDL_Color){38, 34, 61, 255} : C_CARD);
            if (g_movie_zone == 1 && i == g_related_sel) ui_focus(x - 4, 457, 374, 206);
            SDL_Texture *cover = cover_get(jstr(item, "logo"));
            if (cover) { SDL_Rect rr = {x + 9, 468, 124, 184}; ui_contain(cover, &rr); }
            char lines[2][PLOT_LINE_CAP] = {{0}};
            int nlines = wrap_text(jstr(item, "title"), lines, 2, 207);
            for (int line = 0; line < nlines; line++)
                text_clip(lines[line], x + 145, 480 + line * 27, C_TEXT, 0, 207);
            int item_year = jint(item, "year");
            if (item_year > 0) {
                char year_label[16]; snprintf(year_label, sizeof(year_label), "%d", item_year);
                text_draw(gRen, year_label, x + 145, 558, C_MUT, 2);
            }
            text_draw(gRen, "A  Abrir", x + 145, 618, C_ACC2, 2);
        }
    }
    ui_footer(g_movie_zone == 1 ?
              "Esquerda/direita Escolher    A Abrir    Y Ver depois    Cima Voltar" :
              "A Confirmar    Y Ver depois    X Outra lista    Baixo Relacionados    B Voltar");
}

void input_movie(int b) {
    if (!g_movie) return;
    if (b == JOY_B || b == JOY_MINUS) {
        if (!restore_previous_movie()) { close_movie_details(); detail_return_to_origin(); }
        return;
    }
    cJSON *related = cJSON_GetObjectItemCaseSensitive(g_movie, "related");
    int related_n = arr_len(related);
    int max_scroll = g_plot_line_count > PLOT_LINES ? g_plot_line_count - PLOT_LINES : 0;
    if (b == JOY_DLEFT) {
        if (g_movie_zone == 1 && g_related_sel > 0) g_related_sel--;
        else if (g_movie_zone == 0 && g_movie_sel > 0) g_movie_sel--;
    }
    else if (b == JOY_DRIGHT) {
        if (g_movie_zone == 1 && g_related_sel + 1 < related_n) g_related_sel++;
        else if (g_movie_zone == 0 && g_movie_sel < 1) g_movie_sel++;
    }
    else if (b == JOY_UP) {
        if (g_movie_zone == 1) g_movie_zone = 0;
        else if (g_plot_scroll > 0) g_plot_scroll--;
    }
    else if (b == JOY_DOWN) {
        if (g_movie_zone == 0 && g_plot_scroll < max_scroll) g_plot_scroll++;
        else if (related_n > 0) g_movie_zone = 1;
    }
    else if (b == JOY_X && g_movie_zone == 1 && g_related_sel < related_n) {
        cJSON *item = cJSON_GetArrayItem(related, g_related_sel);
        media_list_prompt_add(jint(item, "id"), 0, jstr(item, "title"), jstr(item, "logo"));
    }
    else if (b == JOY_X && g_movie_zone == 0) {
        media_list_prompt_add(jint(g_movie, "id"), 0, jstr(g_movie, "title"), jstr(g_movie, "logo"));
    }
    else if (b == JOY_Y) {
        cJSON *item = g_movie_zone == 1 && g_related_sel < related_n ? cJSON_GetArrayItem(related, g_related_sel) : g_movie;
        media_list_add_named("Assistir mais tarde", jint(item, "id"), 0, jstr(item, "title"), jstr(item, "logo"));
    }
    else if (b == JOY_A) {
        if (g_movie_zone == 1 && g_related_sel < related_n) {
            cJSON *item = cJSON_GetArrayItem(related, g_related_sel);
            int id = jint(item, "id");
            if (id > 0) request_related_movie_details(id);
        } else {
            int id = jint(g_movie, "id");
            if (g_movie_sel == 0) resolve_and_play(id, jstr(g_movie, "title"));
            else toggle_fav_item(id);
        }
    }
}

void movie_touch_action(int favorite) {
    if (!g_movie) return;
    g_movie_zone = 0;
    g_movie_sel = favorite ? 1 : 0;
    input_movie(JOY_A);
}

void movie_touch_related(int x, int y) {
    if (!g_movie || y < 461 || y >= 659 || x < 54) return;
    cJSON *related = cJSON_GetObjectItemCaseSensitive(g_movie, "related");
    int count = arr_len(related);
    int start = g_related_sel - 1;
    if (start < 0) start = 0;
    if (start > count - 3) start = count > 3 ? count - 3 : 0;
    int col = (x - 54) / 390;
    if (col < 0 || col >= 3 || (x - 54) % 390 >= 366) return;
    int index = start + col;
    if (index < count) {
        g_related_sel = index;
        g_movie_zone = 1;
        input_movie(JOY_A);
    }
}
