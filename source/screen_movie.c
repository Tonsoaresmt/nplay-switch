#include "screen_movie.h"
#include "ui.h"
#include "text.h"
#include "api.h"
#include <stdio.h>
#include <string.h>

static cJSON *g_movie = NULL;
static int g_movie_sel = 0; // 0=Assistir, 1=Minha lista
static int g_plot_scroll = 0;
static int g_info_tab = 0;  // 0=Elenco, 1=Relacionados

#define PLOT_LINES 5
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

static const char *cast_photo(cJSON *person) {
    const char *photo = jstr(person, "photo");
    return photo ? photo : jstr(person, "profile_path");
}

int open_movie_details(int movie_id) {
    close_movie_details();
    char path[128];
    snprintf(path, sizeof(path), "/api/catalog/movie/%d/info", movie_id);
    cJSON *resp = api_get(path);
    if (!resp) return -1;
    g_movie = cJSON_GetObjectItemCaseSensitive(resp, "item");
    if (!g_movie) { cJSON_Delete(resp); return -1; }
    cJSON_DetachItemViaPointer(resp, g_movie);
    cJSON_Delete(resp);
    g_movie_sel = 0;
    g_plot_scroll = 0;
    g_info_tab = 0;
    memset(g_plot_lines, 0, sizeof(g_plot_lines));
    const char *plot = jstr(g_movie, "plot");
    g_plot_line_count = wrap_text(plot ? plot : "Sinopse nao disponivel.",
                                  g_plot_lines, PLOT_MAX_LINES, WIN_W - 280 - 48);
    return 0;
}

void close_movie_details(void) {
    if (g_movie) { cJSON_Delete(g_movie); g_movie = NULL; }
    g_plot_scroll = 0;
    g_info_tab = 0;
    g_plot_line_count = 0;
    memset(g_plot_lines, 0, sizeof(g_plot_lines));
}

static void draw_button(int x, int y, int w, const char *label, int selected) {
    if (selected) fill_rect(x, y, w, 44, C_ACC);
    else border_rect(x, y, w, 44, 2, C_MUT);
    text_draw(gRen, label, x + 18, y + 9, selected ? C_TEXT : C_MUT, 0);
}

void draw_movie(void) {
    if (!g_movie) return;
    const char *title = jstr(g_movie, "title");
    const char *logo = jstr(g_movie, "logo");
    const char *genre = jstr(g_movie, "genre");
    const char *director = jstr(g_movie, "director");
    int year = jint(g_movie, "year");
    int duration = jint(g_movie, "duration");

    fill_rect(0, 0, WIN_W, 66, C_BAR);
    text_draw(gRen, "NPLAY  /  FILME", 40, 20, C_ACC, 0);
    text_draw(gRen, "B  Voltar", WIN_W - 145, 20, C_MUT, 0);

    SDL_Texture *poster = cover_get(logo);
    if (poster) { SDL_Rect cr = {40, 90, 210, 315}; SDL_RenderCopy(gRen, poster, NULL, &cr); }
    else { fill_rect(40, 90, 210, 315, C_CARD); text_draw(gRen, "Sem capa", 98, 230, C_MUT, 0); }

    const int dx = 280;
    text_clip(title ? title : "Filme", dx, 88, C_TEXT, 1, WIN_W - dx - 40);
    char meta[320] = {0};
    if (year > 0) snprintf(meta + strlen(meta), sizeof(meta) - strlen(meta), "%d", year);
    if (duration > 0) snprintf(meta + strlen(meta), sizeof(meta) - strlen(meta), "%s%d min", meta[0] ? "  |  " : "", duration);
    if (genre && genre[0]) snprintf(meta + strlen(meta), sizeof(meta) - strlen(meta), "%s%s", meta[0] ? "  |  " : "", genre);
    text_clip(meta[0] ? meta : "Informacoes ainda nao disponiveis", dx, 130, C_MUT, 0, WIN_W - dx - 40);

    text_draw(gRen, "SINOPSE", dx, 171, C_ACC2, 0);
    int max_scroll = g_plot_line_count > PLOT_LINES ? g_plot_line_count - PLOT_LINES : 0;
    if (g_plot_scroll > max_scroll) g_plot_scroll = max_scroll;
    for (int i = 0; i < PLOT_LINES && i + g_plot_scroll < g_plot_line_count; i++)
        text_draw(gRen, g_plot_lines[i + g_plot_scroll], dx, 201 + i * 28, C_TEXT, 0);
    if (g_plot_line_count > PLOT_LINES) {
        char page[80];
        snprintf(page, sizeof(page), "Sinopse %d/%d  -  cima/baixo para ler", g_plot_scroll + 1, max_scroll + 1);
        text_draw(gRen, page, WIN_W - 390, 344, C_MUT, 0);
    }
    if (director && director[0]) {
        char credit[260]; snprintf(credit, sizeof(credit), "Direcao: %s", director);
        text_clip(credit, dx, 372, C_MUT, 0, WIN_W - dx - 40);
    }

    int is_fav = is_fav_item(jint(g_movie, "id"));
    draw_button(dx, 408, 150, "A  Assistir", g_movie_sel == 0);
    draw_button(dx + 168, 408, 215, is_fav ? "Na Minha Lista" : "+ Minha Lista", g_movie_sel == 1);

    cJSON *cast = cJSON_GetObjectItemCaseSensitive(g_movie, "cast_list");
    cJSON *related = cJSON_GetObjectItemCaseSensitive(g_movie, "related");
    int cast_n = arr_len(cast);
    int related_n = arr_len(related);
    text_draw(gRen, g_info_tab ? "RELACIONADOS" : "ELENCO", 40, 474, C_ACC2, 0);
    if (related_n > 0) text_draw(gRen, g_info_tab ? "L  Elenco" : "R  Relacionados", WIN_W - 210, 474, C_MUT, 0);
    if (!g_info_tab && cast_n <= 0) {
        text_draw(gRen, "Elenco ainda nao disponivel para este titulo.", 40, 516, C_MUT, 0);
    } else if (!g_info_tab) {
        int shown = cast_n > 8 ? 8 : cast_n;
        for (int i = 0; i < shown; i++) {
            cJSON *person = cJSON_GetArrayItem(cast, i);
            int x = 40 + i * 152;
            SDL_Texture *photo = cover_get(cast_photo(person));
            if (photo) { SDL_Rect pr = {x, 510, 64, 82}; SDL_RenderCopy(gRen, photo, NULL, &pr); }
            else { fill_rect(x, 510, 64, 82, C_CARD); text_draw(gRen, "?", x + 24, 535, C_MUT, 1); }
            text_clip(jstr(person, "name") ? jstr(person, "name") : "-", x, 600, C_TEXT, 0, 140);
            const char *character = jstr(person, "character");
            if (character && character[0]) text_clip(character, x, 628, C_MUT, 0, 140);
        }
    } else {
        int shown = related_n > 10 ? 10 : related_n;
        for (int i = 0; i < shown; i++) {
            cJSON *item = cJSON_GetArrayItem(related, i);
            int x = 40 + i * 122;
            SDL_Texture *cover = cover_get(jstr(item, "logo"));
            if (cover) { SDL_Rect rr = {x, 510, 88, 126}; SDL_RenderCopy(gRen, cover, NULL, &rr); }
            else fill_rect(x, 510, 88, 126, C_CARD);
            text_clip(jstr(item, "title") ? jstr(item, "title") : "-", x, 644, C_TEXT, 0, 112);
        }
    }
    text_draw(gRen, "Esquerda/direita seleciona  |  A confirma  |  B volta", 40, WIN_H - 42, C_MUT, 0);
}

void input_movie(int b) {
    if (!g_movie) return;
    if (b == JOY_B || b == JOY_MINUS) { close_movie_details(); g_screen = SC_MAIN; return; }
    if (b == JOY_DLEFT && g_movie_sel > 0) g_movie_sel--;
    else if (b == JOY_DRIGHT && g_movie_sel < 1) g_movie_sel++;
    else if (b == JOY_UP && g_plot_scroll > 0) g_plot_scroll--;
    else if (b == JOY_DOWN) g_plot_scroll++;
    else if (b == JOY_R && arr_len(cJSON_GetObjectItemCaseSensitive(g_movie, "related")) > 0) g_info_tab = 1;
    else if (b == JOY_L) g_info_tab = 0;
    else if (b == JOY_A) {
        int id = jint(g_movie, "id");
        if (g_movie_sel == 0) resolve_and_play(id, jstr(g_movie, "title"));
        else toggle_fav_item(id);
    }
}
