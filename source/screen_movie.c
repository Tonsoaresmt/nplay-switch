#include "screen_movie.h"
#include "ui.h"
#include "text.h"
#include "api.h"
#include <stdio.h>
#include <string.h>

static cJSON *g_movie = NULL;
static int g_movie_sel = 0; // 0=Assistir, 1=Minha lista
static int g_plot_scroll = 0;
static int g_movie_zone = 0; // 0=acoes, 1=relacionados
static int g_related_sel = 0;

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

int open_movie_details(int movie_id) {
    char path[128];
    snprintf(path, sizeof(path), "/api/catalog/movie/%d/info", movie_id);
    cJSON *resp = api_get(path);
    if (!resp) return -1;
    cJSON *next_movie = cJSON_GetObjectItemCaseSensitive(resp, "item");
    if (!next_movie) { cJSON_Delete(resp); return -1; }
    cJSON_DetachItemViaPointer(resp, next_movie);
    cJSON_Delete(resp);
    close_movie_details();
    g_movie = next_movie;
    g_movie_sel = 0;
    g_plot_scroll = 0;
    g_movie_zone = 0;
    g_related_sel = 0;
    memset(g_plot_lines, 0, sizeof(g_plot_lines));
    const char *plot = jstr(g_movie, "plot");
    g_plot_line_count = wrap_text(plot ? plot : "Sinopse nao disponivel.",
                                  g_plot_lines, PLOT_MAX_LINES, WIN_W - 280 - 48);
    return 0;
}

void close_movie_details(void) {
    if (g_movie) { cJSON_Delete(g_movie); g_movie = NULL; }
    g_plot_scroll = 0;
    g_movie_zone = 0;
    g_related_sel = 0;
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
    const char *director = jstr(g_movie, "director");
    int year = jint(g_movie, "year");
    int duration = jint(g_movie, "duration");

    ui_header("NPLAY / FILME", title ? title : "Filme", "B Voltar");

    ui_panel(28, 86, 234, 343, C_ACC);
    SDL_Texture *poster = cover_get(logo);
    if (poster) { SDL_Rect cr = {40, 98, 210, 315}; SDL_RenderCopy(gRen, poster, NULL, &cr); }
    else { fill_rect(40, 98, 210, 315, C_BAR); text_center_at("Sem capa", 40, 210, 242, C_MUT, 0); }

    const int dx = 280;
    text_draw(gRen, "INFORMACOES", dx, 94, C_ACC2, 0);
    char meta[320] = {0};
    if (year > 0) snprintf(meta + strlen(meta), sizeof(meta) - strlen(meta), "%d", year);
    if (duration > 0) snprintf(meta + strlen(meta), sizeof(meta) - strlen(meta), "%s%d min", meta[0] ? "  |  " : "", duration);
    if (genre && genre[0]) snprintf(meta + strlen(meta), sizeof(meta) - strlen(meta), "%s%s", meta[0] ? "  |  " : "", genre);
    text_clip(meta[0] ? meta : "Informacoes ainda nao disponiveis", dx, 126, C_MUT, 0, WIN_W - dx - 40);

    text_draw(gRen, "SINOPSE", dx, 168, C_ACC2, 0);
    int max_scroll = g_plot_line_count > PLOT_LINES ? g_plot_line_count - PLOT_LINES : 0;
    if (g_plot_scroll > max_scroll) g_plot_scroll = max_scroll;
    for (int i = 0; i < PLOT_LINES && i + g_plot_scroll < g_plot_line_count; i++)
        text_draw(gRen, g_plot_lines[i + g_plot_scroll], dx, 201 + i * 28, C_TEXT, 0);
    if (g_plot_line_count > PLOT_LINES) {
        char page[80];
        snprintf(page, sizeof(page), "Sinopse %d/%d  -  cima/baixo para ler", g_plot_scroll + 1, max_scroll + 1);
        text_right(page, WIN_W - 40, 344, C_MUT, 0);
    }
    if (director && director[0]) {
        char credit[260]; snprintf(credit, sizeof(credit), "Direcao: %s", director);
        text_clip(credit, dx, 372, C_MUT, 0, WIN_W - dx - 40);
    }

    int is_fav = is_fav_item(jint(g_movie, "id"));
    draw_button(dx, 408, 172, "A  Assistir", g_movie_zone == 0 && g_movie_sel == 0);
    draw_button(dx + 188, 408, 230, is_fav ? "Na Minha Lista" : "+ Minha Lista", g_movie_zone == 0 && g_movie_sel == 1);

    cJSON *related = cJSON_GetObjectItemCaseSensitive(g_movie, "related");
    int related_n = arr_len(related);
    fill_rect(40, 474, WIN_W - 80, 2, C_CARD);
    text_draw(gRen, "TITULOS RELACIONADOS", 40, 488, g_movie_zone == 1 ? C_TEXT : C_ACC2, 0);
    text_right(related_n > 0 ? "Baixo para explorar" : "Novas sugestoes aparecerao aqui", WIN_W - 40, 488, C_MUT, 0);
    if (related_n <= 0) {
        text_draw(gRen, "Ainda nao encontramos obras relacionadas a este titulo.", 40, 540, C_MUT, 0);
    } else {
        int stride = 122, scroll = 0;
        int selected_x = 40 + g_related_sel * stride;
        if (selected_x + 112 > WIN_W - 40) scroll = selected_x + 112 - (WIN_W - 40);
        for (int i = 0; i < related_n; i++) {
            cJSON *item = cJSON_GetArrayItem(related, i);
            int x = 40 + i * stride - scroll;
            if (x + 112 < 0 || x > WIN_W) continue;
            SDL_Texture *cover = cover_get(jstr(item, "logo"));
            if (cover) { SDL_Rect rr = {x, 524, 88, 106}; SDL_RenderCopy(gRen, cover, NULL, &rr); }
            else fill_rect(x, 524, 88, 106, C_CARD);
            text_clip(jstr(item, "title") ? jstr(item, "title") : "-", x, 638, C_TEXT, 0, 112);
            if (g_movie_zone == 1 && i == g_related_sel) ui_focus(x - 3, 521, 118, 144);
        }
    }
    ui_footer(g_movie_zone == 1 ?
              "Esquerda/direita Escolher    A Abrir    Y Assistir mais tarde    X Outra lista    Cima Voltar" :
              "A Confirmar    Y Assistir mais tarde    X Outra lista    Baixo Relacionados    B Voltar");
}

void input_movie(int b) {
    if (!g_movie) return;
    if (b == JOY_B || b == JOY_MINUS) { close_movie_details(); g_screen = SC_MAIN; return; }
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
            if (id > 0 && open_movie_details(id) != 0) toast("Nao foi possivel abrir este titulo");
        } else {
            int id = jint(g_movie, "id");
            if (g_movie_sel == 0) resolve_and_play(id, jstr(g_movie, "title"));
            else toggle_fav_item(id);
        }
    }
}
