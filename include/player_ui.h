// player_ui.h - HUD do player no estilo da versao PC (Netflix-like).
// Este modulo so desenha: recebe um retrato do estado (PlayerHud) e nao conhece
// FFmpeg, rede ou controle. Assim o layout pode ser renderizado e conferido fora
// do console com o mesmo codigo.
#pragma once
#include <SDL.h>
#include "subtitle_queue.h"
#include "player_touch.h"

#define PUI_W 1280
#define PUI_H 720
#define PUI_MAX_TRACKS 17

typedef enum {
    PUI_FOCUS_TIMELINE = 0,
    PUI_FOCUS_PLAY,
    PUI_FOCUS_REW,
    PUI_FOCUS_FWD,
    PUI_FOCUS_VOLUME,
    PUI_FOCUS_TRACKS,
    PUI_FOCUS_NEXT,
    PUI_FOCUS_NEXT_CARD,
    PUI_FOCUS_COUNT
} PuiFocus;

typedef enum {
    PUI_FLASH_NONE = 0,
    PUI_FLASH_PLAY,
    PUI_FLASH_PAUSE,
    PUI_FLASH_REW,
    PUI_FLASH_FWD
} PuiFlash;

typedef struct {
    // Identidade
    const char *title;        // obra (ou serie)
    const char *subtitle;     // "T1 E3 - Episodio" ou metadados do filme
    const char *overview;     // sinopse mostrada ao pausar

    // Tempo
    double pos, dur;          // segundos
    int scrubbing;            // 1 enquanto o usuario escolhe um ponto
    double scrub_target;
    const char *scrub_label;  // capitulo no ponto escolhido (opcional)
    const double *chapters;   // inicios de capitulos em segundos
    int chapter_count;

    // Estado
    int paused;
    float hud_alpha;          // 0..1 (animado pelo player)
    float pause_info_alpha;   // 0..1, sinopse ao pausar
    int focus;                // PuiFocus
    int volume;               // 0..100
    float volume_popup_alpha; // 0..1
    const char *tracks_label; // "Portugues - Legendas desligadas"

    // Proximo episodio
    int has_next;
    const char *next_title;
    float next_card_alpha;    // 0..1
    float next_card_progress; // 0..1 (contagem regressiva)

    // Feedback
    int flash;                // PuiFlash
    float flash_t;            // 0..1 progresso da animacao
    int flash_seconds;        // 10/60 para os saltos
    const char *notice;
    float notice_alpha;

    // Buffering sobre o ultimo quadro
    int buffering;
    const char *buffering_text;

    // Legenda do video
    const char *subtitle_text;
    // Letreiros posicionados (placas/onomatopeias do fansub) e o retangulo onde
    // o video e desenhado (sem as tarjas). NULL/0 = sem letreiros.
    const SubtitleSigns *signs;
    int video_x, video_y, video_w, video_h;

    // Painel de audio e legendas
    int panel_open;
    int panel_column;         // 0 audio, 1 legendas
    int audio_count, audio_sel, audio_current;
    const char *audio_names[PUI_MAX_TRACKS];
    const char *audio_details[PUI_MAX_TRACKS];
    int sub_count, sub_sel, sub_current;  // indice 0 = desligadas
    const char *sub_names[PUI_MAX_TRACKS];

    // Lista de episodios dentro do player (como o botao Episodios do site).
    int has_episodes;          // mostra a dica no topo do HUD
    int episodes_open;
    int episode_count, episode_sel, episode_current;
    const char *const *episode_labels;
    const unsigned char *episode_watched;
} PlayerHud;

// Desenha legenda + HUD sobre o quadro de video ja copiado no renderer.
void pui_draw(SDL_Renderer *ren, const PlayerHud *hud, Uint32 now);

// Tela de preparacao/recuperacao com anel animado (sem quadro de video).
void pui_draw_loading(SDL_Renderer *ren, const char *title, const char *headline,
                      const char *detail, Uint32 now, int warning);

// Ultimo quadro do video usado como fundo da tela de espera durante seek ou
// troca de faixa (reabertura). NULL volta a tela cheia de preparacao.
void pui_set_loading_backdrop(SDL_Texture *frame);

// Formata segundos como 1:02:03 ou 2:03.
void pui_format_time(double seconds, char *out, int cap);

// Rotulo curto do controle focado (usado no HUD e em testes).
const char *pui_focus_label(const PlayerHud *hud, int focus, char *out, int cap);
