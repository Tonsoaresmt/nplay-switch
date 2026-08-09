#include "screen_movie.h"
#include "ui.h"
#include "text.h"
#include "api.h"
#include <stdio.h>
#include <string.h>

static cJSON *g_movie = NULL;
static int g_movie_sel = 0; // 0=Play, 1=Fav

int open_movie_details(int movie_id) {
    char path[128];
    snprintf(path, sizeof(path), "/api/catalog/movie/%d/info", movie_id);
    cJSON *resp = api_get(path);
    if (!resp) return -1;
    
    g_movie = cJSON_GetObjectItemCaseSensitive(resp, "item");
    if (!g_movie) {
        cJSON_Delete(resp);
        return -1;
    }
    // Detach from parent so we can keep it and delete resp
    cJSON_DetachItemViaPointer(resp, g_movie);
    cJSON_Delete(resp);
    
    g_movie_sel = 0;
    return 0;
}

void close_movie_details(void) {
    if (g_movie) {
        cJSON_Delete(g_movie);
        g_movie = NULL;
    }
}

void draw_movie(void) {
    if (!g_movie) return;
    
    const char *title = jstr(g_movie, "title");
    const char *plot = jstr(g_movie, "plot");
    const char *logo = jstr(g_movie, "logo");
    int year = jint(g_movie, "year");
    int duration = jint(g_movie, "duration");
    const char *genre = jstr(g_movie, "genre");
    
    // Draw Backdrop / Background
    // ... we don't have backdrop loading in Switch yet, just use dark BG
    
    // Draw Cover
    SDL_Texture *tex = cover_get(logo);
    if (tex) {
        SDL_Rect cr = { 60, 60, 200, 300 };
        SDL_RenderCopy(gRen, tex, NULL, &cr);
    }
    
    // Info
    int dx = 290;
    int dy = 60;
    text_draw(gRen, title ? title : "Filme", dx, dy, C_ACC, 1);
    dy += 40;
    
    char meta[128];
    snprintf(meta, sizeof(meta), "%d  |  %d min  |  %s", year, duration, genre ? genre : "");
    text_draw(gRen, meta, dx, dy, C_MUT, 0);
    dy += 30;
    
    // Plot (multiline not well supported by basic text_draw, we just clip or draw basic)
    if (plot) {
        text_clip(plot, dx, dy, C_TEXT, 0, WIN_W - dx - 40);
        dy += 80;
    } else {
        dy += 30;
    }
    
    // Buttons
    int btn_y = dy;
    
    // Play button
    SDL_Color c_play = (g_movie_sel == 0) ? C_ACC : C_TEXT;
    border_rect(dx, btn_y, 140, 40, 2, c_play);
    text_draw(gRen, "Assistir", dx + 30, btn_y + 10, c_play, 0);
    
    // Fav button
    int is_fav = is_fav_item(jint(g_movie, "id"));
    SDL_Color c_fav = (g_movie_sel == 1) ? C_ACC : C_TEXT;
    border_rect(dx + 160, btn_y, 200, 40, 2, c_fav);
    char fav_txt[64];
    snprintf(fav_txt, sizeof(fav_txt), "%s", is_fav ? "- Minha Lista" : "+ Minha Lista");
    text_draw(gRen, fav_txt, dx + 190, btn_y + 10, c_fav, 0);
    
    // Legend
    text_draw(gRen, "(B) Voltar", 60, WIN_H - 50, C_MUT, 0);
}

void input_movie(int b) {
    if (!g_movie) return;
    
    if (b == JOY_B || b == JOY_MINUS) {
        close_movie_details();
        g_screen = SC_MAIN; // Ou volta para a aba atual (filmes)
        return;
    }
    
    if (b == JOY_DLEFT) {
        if (g_movie_sel > 0) g_movie_sel--;
    } else if (b == JOY_DRIGHT) {
        if (g_movie_sel < 1) g_movie_sel++;
    } else if (b == JOY_A) {
        int id = jint(g_movie, "id");
        if (g_movie_sel == 0) { // Assistir
            resolve_and_play(id, jstr(g_movie, "title"));
        } else if (g_movie_sel == 1) { // Fav
            toggle_fav_item(id);
        }
    }
}
