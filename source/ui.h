#pragma once

#include <SDL.h>
#include <switch.h>

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

typedef enum { SC_LOGIN, SC_MAIN, SC_SERIES, SC_SEARCH, SC_CONFIG, SC_MOVIE } Screen;
extern Screen g_screen;

// Cores padronizadas da UI
extern const SDL_Color C_BG;
extern const SDL_Color C_BAR;
extern const SDL_Color C_CARD;
extern const SDL_Color C_TEXT;
extern const SDL_Color C_MUT;
extern const SDL_Color C_ACC;
extern const SDL_Color C_ACC2;
extern const SDL_Color C_ROSE;
extern const SDL_Color C_GREEN;

// Variaveis globais de estado da UI
extern SDL_Renderer *gRen;
extern Uint32 g_toast_until;
extern char g_toast[160];

// Helpers de desenho
void fill_rect(int x, int y, int w, int h, SDL_Color c);
void border_rect(int x, int y, int w, int h, int th, SDL_Color c);
void ui_cover(SDL_Texture *texture, const SDL_Rect *dst);
void text_clip(const char *s, int x, int y, SDL_Color c, int big, int maxw);
int  text_center(const char *s, int y, SDL_Color c, int big);
int  text_center_at(const char *s, int x, int w, int y, SDL_Color c, int big);
int  text_right(const char *s, int right, int y, SDL_Color c, int big);
void ui_header(const char *section, const char *title, const char *action);
void ui_footer(const char *hint);
void ui_panel(int x, int y, int w, int h, SDL_Color accent);
void ui_focus(int x, int y, int w, int h);
void ui_badge(const char *label, int x, int y, SDL_Color color);
int ui_card_badge(const char *label, int x, int y, SDL_Color color);
void ui_empty_state(const char *title, const char *detail);
void ui_progress(int x, int y, int w, int value, SDL_Color color);

// Utilitarios visuais
void toast(const char *msg);
void short_title(const char *title, char *out, int cap);
int prompt_text(const char *guide, char *out, size_t cap, int password);
