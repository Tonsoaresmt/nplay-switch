// player.c - player de video: ffmpeg decodifica, SDL desenha (textura YUV) e toca
// o audio (SDL Audio + swresample). O relogio de video e monotono e recebe
// apenas pequenas correcoes do audio, inclusive em streams TorBox irregulares.
// MP4/MKV remoto usa HTTPS nativo. HLS usa callbacks AVIO com libcurl para abrir
// cada playlist e segmento sem depender do TLS interno do FFmpeg/libnx.
// Retoma de onde parou (start_sec), reporta a posicao (out_pos/out_dur) e mostra
// um HUD (titulo + barra de progresso + tempo) ao pausar/buscar.
#include <switch.h>
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/error.h>
#include <libavutil/time.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/hwcontext.h>
#include "player.h"
#include "curl_avio.h"
#include "text.h"
#include "store.h"
#include "diag.h"
#include "ui.h"
#include "player_clock.h"

#define JOY_A 0
#define JOY_B 1
#define JOY_X 2
#define JOY_Y 3
#define JOY_L 6
#define JOY_R 7
#define JOY_ZL 8
#define JOY_ZR 9
#define JOY_PLUS 10
#define JOY_MINUS 11
#define JOY_DLEFT 12
#define JOY_UP 13
#define JOY_DRIGHT 14
#define JOY_DOWN 15
#define PWIN_W 1280
#define PWIN_H 720
#define TRACK_MENU_AUDIO 1
#define TRACK_MENU_SUB   2
#define SEEK_ENTER_AXIS  24500
#define SEEK_MOVE_AXIS   14000
#define SEEK_RELEASE_AXIS 9000
#define SEEK_HOLD_MS       550

static char g_player_last_error[160] = "";
// Os callbacks HLS de abertura/fechamento rodam dentro de av_read_frame na
// thread que desenha os quadros. Depois do primeiro quadro, nao grave eventos
// normais na microSD em toda troca de segmento.
static int g_player_presented_frame = 0;
static const SDL_Color PC_DARK = { 8, 10, 15, 255 };

static void player_boot_stage(const char *stage) {
    // O caminho plano existe mesmo quando o NRO foi instalado fora de uma pasta.
    // Os caminhos antigos ficam como fallback para instalacoes ja existentes.
    FILE *file = fopen("sdmc:/switch/.nplay-player-boot.txt", "wb");
    if (!file) file = fopen("sdmc:/switch/Nplay/player_boot.txt", "wb");
    if (!file) file = fopen("sdmc:/switch/Meruem/player_boot.txt", "wb");
    if (!file) return;
    fprintf(file, "%s\n", stage ? stage : "desconhecido");
    fclose(file);
}

const char *player_last_error(void) { return g_player_last_error; }

static void player_error_text(const char *stage, int code) {
    char detail[AV_ERROR_MAX_STRING_SIZE] = "erro desconhecido";
    av_strerror(code, detail, sizeof(detail));
    snprintf(g_player_last_error, sizeof(g_player_last_error), "%s: %s", stage, detail);
}

static void player_error_message(const char *message) {
    snprintf(g_player_last_error, sizeof(g_player_last_error), "%s", message);
}

static void draw_center_state(SDL_Renderer *ren, const char *state,
                              const char *detail, int accent);

typedef struct {
    int64_t deadline_us;
    SDL_Joystick *joy;
    int cancelled;
    int timed_out;
    Uint32 started_tick;
    Uint32 last_progress_tick;
    const char *phase;
    int native_hls;
    SDL_Renderer *renderer;
    SDL_threadID render_thread;
    Uint32 last_render_tick;
    const char *detail;
} PlayerOpenDeadline;

static int player_open_interrupted(void *userdata) {
    PlayerOpenDeadline *watch = (PlayerOpenDeadline *)userdata;
    if (!watch) return 0;
    // Uma faixa HLS pode falhar e o FFmpeg continuar com as demais. Depois
    // que B foi observado, a tentativa inteira deve permanecer cancelada.
    if (watch->cancelled || watch->timed_out) return 1;
    // avformat_open_input/find_stream_info sao sincronas. Bombeie o controle
    // dentro do callback de interrupcao para B/- realmente funcionarem mesmo
    // enquanto FFmpeg espera rede ou uma rendition HLS.
    // O AVIO de segmentos possui uma thread produtora. SDL events e renderer
    // pertencem exclusivamente a thread que iniciou o player.
    int on_render_thread = SDL_ThreadID() == watch->render_thread;
    if (on_render_thread) {
        SDL_PumpEvents();
        SDL_Event queued[16];
        int queued_count = SDL_PeepEvents(queued, 16, SDL_PEEKEVENT,
                                          SDL_JOYBUTTONDOWN, SDL_JOYBUTTONDOWN);
        for (int i = 0; i < queued_count; i++) {
            if (queued[i].jbutton.button == JOY_B ||
                queued[i].jbutton.button == JOY_MINUS) {
                watch->cancelled = 1;
                return 1;
            }
        }
    }
    if (on_render_thread && watch->joy &&
        (SDL_JoystickGetButton(watch->joy, JOY_B) ||
         SDL_JoystickGetButton(watch->joy, JOY_MINUS))) {
        watch->cancelled = 1;
        return 1;
    }
    if (watch->deadline_us > 0 && av_gettime_relative() >= watch->deadline_us) {
        watch->timed_out = 1;
        return 1;
    }
    Uint32 now = SDL_GetTicks();
    // avformat_open_input, find_stream_info e o seek de retomada bloqueiam
    // o loop normal. O interrupt callback roda nessas esperas; redesenhe
    // a pipoca aqui para que ela continue animada ate o primeiro quadro.
    if (on_render_thread && watch->renderer && !g_player_presented_frame &&
        now - watch->last_render_tick >= 80u) {
        SDL_SetRenderDrawColor(watch->renderer, PC_DARK.r, PC_DARK.g, PC_DARK.b, 255);
        SDL_RenderClear(watch->renderer);
        draw_center_state(watch->renderer, "PREPARANDO VIDEO",
                          watch->detail ? watch->detail : "Conectando...  |  B para cancelar", 0);
        SDL_RenderPresent(watch->renderer);
        watch->last_render_tick = now;
    }
    if (watch->native_hls && !g_player_presented_frame &&
        now - watch->last_progress_tick >= 5000u) {
        int active = 0, reserved_kb = 0;
        nplay_curl_avio_stats(&active, &reserved_kb);
        unsigned elapsed = now - watch->started_tick;
        diag_player_event("startup", "waiting", "phase=%s ms=%u active=%d reserved=%dKB",
                          watch->phase ? watch->phase : "?", elapsed, active, reserved_kb);
        char stage[96];
        snprintf(stage, sizeof(stage), "HLS %s %u s: %d recursos, %d KB",
                 watch->phase ? watch->phase : "?", elapsed / 1000u, active, reserved_kb);
        player_boot_stage(stage);
        watch->last_progress_tick = now;
    }
    return 0;
}

// O HTTPS nativo do FFmpeg/libnx abre o master R2, mas no hardware pode ficar
// preso ao abrir as playlists/segmentos seguintes. O demuxer HLS chama io_open
// para cada recurso aninhado; entregue todos ao libcurl, a mesma pilha TLS usada
// com sucesso pela API e pelas capas do aplicativo.
static int player_hls_io_open(AVFormatContext *fmt, AVIOContext **pb,
                              const char *url, int flags, AVDictionary **options) {
    (void)options;
    if (!pb || !url || (flags & AVIO_FLAG_WRITE)) return AVERROR(EINVAL);
    *pb = NULL;
    if (fmt && fmt->interrupt_callback.callback &&
        fmt->interrupt_callback.callback(fmt->interrupt_callback.opaque))
        return AVERROR_EXIT;
    if (strncmp(url, "http://", 7) && strncmp(url, "https://", 8))
        return AVERROR_PROTOCOL_NOT_FOUND;
    *pb = nplay_curl_avio_open_hls(url);
    if (!*pb) {
        int active = 0, reserved_kb = 0;
        char stage[96];
        nplay_curl_avio_stats(&active, &reserved_kb);
        snprintf(stage, sizeof(stage), "03 HLS falhou: %d recursos, %d KB",
                 active, reserved_kb);
        player_boot_stage(stage);
        diag_player_event("hls-io", "open-fail",
                          "active=%d reserved=%dKB", active, reserved_kb);
    }
    if (!*pb && fmt && fmt->interrupt_callback.callback &&
        fmt->interrupt_callback.callback(fmt->interrupt_callback.opaque))
        return AVERROR_EXIT;
    return *pb ? 0 : AVERROR(ENOMEM);
}

static int player_hls_io_close(AVFormatContext *fmt, AVIOContext *pb) {
    (void)fmt;
    nplay_curl_avio_close(pb);
    return 0;
}

static void player_select_hls_streams(AVFormatContext *fmt, int video, int audio,
                                      int subtitle) {
    if (!fmt) return;
    for (unsigned i = 0; i < fmt->nb_streams; i++) {
        enum AVMediaType type = fmt->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO || type == AVMEDIA_TYPE_AUDIO ||
            type == AVMEDIA_TYPE_SUBTITLE)
            fmt->streams[i]->discard = ((int)i == video || (int)i == audio ||
                                        (int)i == subtitle) ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    }
}

static enum AVPixelFormat player_select_video_format(AVCodecContext *ctx,
                                                       const enum AVPixelFormat *formats) {
    for (const enum AVPixelFormat *it = formats; it && *it != AV_PIX_FMT_NONE; it++) {
        if (*it == AV_PIX_FMT_NVTEGRA) return *it;
    }
    return avcodec_default_get_format(ctx, formats);
}

static const SDL_Color PC_TEXT = { 234, 240, 250, 255 };
static const SDL_Color PC_MUT  = { 170, 178, 196, 255 };
static const SDL_Color PC_ACC  = { 139, 92, 246, 255 };
static const SDL_Color PC_ACC2 = { 59, 130, 246, 255 };
static const SDL_Color PC_CARD = { 24, 29, 43, 255 };

static void pfill(SDL_Renderer *r, int x, int y, int w, int h, SDL_Color c, int a) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, a);
    SDL_Rect rr = { x, y, w, h };
    SDL_RenderFillRect(r, &rr);
}
static void fmt_time(double s, char *out, int cap) {
    if (s < 0 || s != s) s = 0;
    int t = (int)s, h = t / 3600, m = (t % 3600) / 60, sec = t % 60;
    if (h > 0) snprintf(out, cap, "%d:%02d:%02d", h, m, sec);
    else snprintf(out, cap, "%d:%02d", m, sec);
}

// idioma de um stream (tag "language"), ex.: "por", "eng", "jpn".
static const char *stream_lang(AVFormatContext *fmt, int idx) {
    if (idx < 0) return "?";
    AVDictionaryEntry *e = av_dict_get(fmt->streams[idx]->metadata, "language", NULL, 0);
    return (e && e->value) ? e->value : "und";
}

static const char *lang_label(const char *lang) {
    if (!lang) return "?";
    if (!strncasecmp(lang, "por", 3) || !strncasecmp(lang, "pt", 2)) return "PT";
    if (!strncasecmp(lang, "eng", 3) || !strncasecmp(lang, "en", 2)) return "EN";
    if (!strncasecmp(lang, "jpn", 3) || !strncasecmp(lang, "ja", 2)) return "JP";
    if (!strncasecmp(lang, "spa", 3) || !strncasecmp(lang, "es", 2)) return "ES";
    return lang[0] ? lang : "?";
}

static void draw_clipped_text(SDL_Renderer *ren, const char *text, int x, int y,
                              int maxw, SDL_Color color, int big) {
    SDL_Rect clip = { x, y - 3, maxw, big ? 42 : 32 };
    SDL_RenderSetClipRect(ren, &clip);
    text_draw(ren, text, x, y, color, big);
    SDL_RenderSetClipRect(ren, NULL);
}

static const char *lang_name(const char *lang) {
    const char *code = lang_label(lang);
    if (!strcmp(code, "PT")) return "Portugues";
    if (!strcmp(code, "EN")) return "Ingles";
    if (!strcmp(code, "JP")) return "Japones";
    if (!strcmp(code, "ES")) return "Espanhol";
    return "Desconhecido";
}

static void format_language(const char *lang, char *out, size_t cap) {
    snprintf(out, cap, "%s - %s", lang_label(lang), lang_name(lang));
}

static void draw_notice(SDL_Renderer *ren, const char *message) {
    int texture_w = 0, h = 0;
    SDL_Texture *t;
    SDL_Color bg = { 12, 15, 23, 255 };
    if (!message || !message[0]) return;
    t = text_cached(ren, message, PC_TEXT, 0, &texture_w, &h);
    if (!t) return;
    int w = texture_w;
    if (w > 520) w = 520;
    int x = (PWIN_W - w) / 2;
    pfill(ren, x - 24, 272, w + 48, h + 24, bg, 190);
    SDL_Rect src = { 0, 0, w, h };
    SDL_Rect dst = { x, 284, w, h };
    SDL_RenderCopy(ren, t, texture_w > w ? &src : NULL, &dst);
}

static void draw_track_menu(SDL_Renderer *ren, AVFormatContext *fmt, int menu,
                            const int *indexes, int count, int selected, int current) {
    const int x = 230, y = 70, w = 820, h = 580;
    const int visible = 7, row_h = 54, list_y = y + 112;
    const int total = count + (menu == TRACK_MENU_SUB ? 1 : 0);
    int scroll = selected - visible / 2;
    if (scroll < 0) scroll = 0;
    if (scroll > total - visible) scroll = total - visible;
    if (scroll < 0) scroll = 0;

    SDL_Color black = { 0, 0, 0, 255 };
    pfill(ren, 0, 0, PWIN_W, PWIN_H, black, 150);
    pfill(ren, x, y, w, h, PC_DARK, 248);
    pfill(ren, x, y, 6, h, PC_ACC, 255);
    text_draw(ren, menu == TRACK_MENU_AUDIO ? "Escolher audio" : "Escolher legenda",
              x + 34, y + 24, PC_TEXT, 1);
    char summary[64];
    snprintf(summary, sizeof(summary), "%d opcao%s disponive%s", total,
             total == 1 ? "" : "s", total == 1 ? "l" : "is");
    text_draw(ren, summary, x + 36, y + 67, PC_MUT, 0);

    for (int row = 0; row < visible; row++) {
        int option = scroll + row;
        if (option >= total) break;
        int yy = list_y + row * row_h;
        int focused = option == selected, active = option == current;
        if (focused) pfill(ren, x + 26, yy - 3, w - 52, row_h - 3, PC_CARD, 255);
        if (focused) pfill(ren, x + 26, yy - 3, 4, row_h - 3, PC_ACC2, 255);

        char primary[128], secondary[128];
        if (menu == TRACK_MENU_SUB && option == 0) {
            snprintf(primary, sizeof(primary), "Desligadas");
            snprintf(secondary, sizeof(secondary), "Reproduzir sem legendas");
        } else {
            int list_index = option - (menu == TRACK_MENU_SUB ? 1 : 0);
            int stream_index = indexes[list_index];
            AVStream *stream = fmt->streams[stream_index];
            AVDictionaryEntry *track_title = av_dict_get(stream->metadata, "title", NULL, 0);
            char language[48];
            format_language(stream_lang(fmt, stream_index), language, sizeof(language));
            snprintf(primary, sizeof(primary), "%s", track_title && track_title->value && track_title->value[0]
                     ? track_title->value : language);
            if (menu == TRACK_MENU_AUDIO) {
                snprintf(secondary, sizeof(secondary), "%s  |  %s  |  %d canal%s", language,
                         avcodec_get_name(stream->codecpar->codec_id), stream->codecpar->ch_layout.nb_channels,
                         stream->codecpar->ch_layout.nb_channels == 1 ? "" : "is");
            } else {
                snprintf(secondary, sizeof(secondary), "%s  |  %s", language,
                         avcodec_get_name(stream->codecpar->codec_id));
            }
        }
        draw_clipped_text(ren, primary, x + 48, yy + 1, w - 210,
                          focused ? PC_TEXT : PC_MUT, 0);
        draw_clipped_text(ren, secondary, x + 48, yy + 26, w - 210, PC_MUT, 0);
        if (active) text_draw(ren, "ATUAL", x + w - 125, yy + 12, PC_ACC, 0);
    }

    text_draw(ren, "D-pad escolhe", x + 36, y + h - 44, PC_MUT, 0);
    text_draw(ren, "A confirma", x + 320, y + h - 44, PC_TEXT, 0);
    text_draw(ren, "B cancela", x + 590, y + h - 44, PC_MUT, 0);
}

static void hud_line(SDL_Renderer *ren, int x1, int y1, int x2, int y2,
                     SDL_Color color) {
    SDL_SetRenderDrawColor(ren, color.r, color.g, color.b, 255);
    SDL_RenderDrawLine(ren, x1, y1, x2, y2);
    SDL_RenderDrawLine(ren, x1, y1 + 1, x2, y2 + 1);
}

static void hud_transport(SDL_Renderer *ren, int x, int y, int paused) {
    if (paused) {
        for (int row = 0; row < 34; row++)
            hud_line(ren, x + 9, y + row, x + 9 + row * 4 / 5, y + row, PC_TEXT);
    } else {
        pfill(ren, x + 7, y, 9, 34, PC_TEXT, 255);
        pfill(ren, x + 23, y, 9, 34, PC_TEXT, 255);
    }
}

static void hud_skip(SDL_Renderer *ren, int x, int y, int forward) {
    // A circular arrow and large 10 mirror the site's transport controls.
    SDL_Color c = PC_TEXT;
    hud_line(ren, x + 7, y + 18, x + 12, y + 7, c);
    hud_line(ren, x + 12, y + 7, x + 27, y + 3, c);
    hud_line(ren, x + 27, y + 3, x + 39, y + 10, c);
    hud_line(ren, x + 39, y + 10, x + 43, y + 23, c);
    hud_line(ren, x + 43, y + 23, x + 38, y + 35, c);
    hud_line(ren, x + 38, y + 35, x + 25, y + 40, c);
    hud_line(ren, x + 25, y + 40, x + 12, y + 35, c);
    if (forward) {
        hud_line(ren, x + 38, y + 17, x + 43, y + 23, c);
        hud_line(ren, x + 43, y + 23, x + 49, y + 17, c);
    } else {
        hud_line(ren, x + 1, y + 12, x + 7, y + 18, c);
        hud_line(ren, x + 7, y + 18, x + 14, y + 12, c);
    }
    text_draw(ren, "10", x + 13, y + 8, PC_TEXT, 0);
}

// Full-bleed video, back action above and one transport row below, following
// the site player instead of a second screen of rectangular shortcut cards.
static void draw_hud(SDL_Renderer *ren, const char *title, double pos, double dur,
                     int paused, int vol, AVFormatContext *fmt, int aidx,
                     int acur, int naud, int nsub, int scur, int sidx,
                     int hud_pinned, int seekable) {
    (void)hud_pinned;
    (void)fmt;
    (void)aidx;
    (void)acur;
    (void)scur;
    (void)sidx;
    for (int band = 0; band < 12; band++)
        pfill(ren, 0, 360 + band * 30, PWIN_W, 30, PC_DARK,
              10 + band * 18);
    pfill(ren, 0, 0, PWIN_W, 86, PC_DARK, 56);
    hud_line(ren, 48, 40, 74, 40, PC_TEXT);
    hud_line(ren, 48, 40, 59, 28, PC_TEXT);
    hud_line(ren, 48, 40, 59, 52, PC_TEXT);
    text_draw(ren, "B  Voltar", 88, 27, PC_TEXT, 0);

    if (paused) {
        text_draw(ren, "PAUSADO", 48, 477, PC_ACC, 0);
        draw_clipped_text(ren, (title && title[0]) ? title : "Reproducao",
                          48, 508, 1080, PC_TEXT, 1);
    }
    const int bx = 48, by = 600, bw = 1080, bh = 6;
    pfill(ren, bx, by, bw, bh, PC_CARD, 190);
    int fw = 0;
    if (seekable && dur > 0) fw = (int)(bw * (pos / dur));
    else if (!seekable) fw = bw;
    if (fw < 0) fw = 0;
    if (fw > bw) fw = bw;
    if (fw > 0) pfill(ren, bx, by, fw, bh, PC_ACC, 255);
    if (seekable && dur > 0) pfill(ren, bx + fw - 5, by - 6, 11, bh + 12, PC_ACC, 255);

    char now[16], total[16];
    fmt_time(pos, now, sizeof(now));
    fmt_time(dur, total, sizeof(total));
    text_draw(ren, now, bx, by + 13, PC_TEXT, 0);
    const char *time_label = seekable && dur > 0 ? total : "--:--";
    int time_width = 0, time_height = 0;
    SDL_Texture *time_texture = text_cached(ren, time_label, PC_TEXT, 0,
                                            &time_width, &time_height);
    if (time_texture) {
        SDL_Rect time_rect = { PWIN_W - 48 - time_width, by - 9,
                               time_width, time_height };
        SDL_RenderCopy(ren, time_texture, NULL, &time_rect);
    }

    hud_transport(ren, 56, 652, paused);
    text_draw(ren, "A", 64, 631, PC_MUT, 0);
    if (seekable) {
        hud_skip(ren, 136, 647, 0);
        hud_skip(ren, 213, 647, 1);
        text_draw(ren, "L", 154, 628, PC_MUT, 0);
        text_draw(ren, "R", 232, 628, PC_MUT, 0);
    }
    char volume_text[32];
    snprintf(volume_text, sizeof(volume_text), "VOL %d%%", vol);
    text_draw(ren, volume_text, seekable ? 300 : 145, 658, PC_MUT, 0);
    if (!paused)
        draw_clipped_text(ren, (title && title[0]) ? title : "Reproducao",
                          seekable ? 425 : 320, 651, seekable ? 420 : 540, PC_TEXT, 0);
    text_draw(ren, "Y Audio", 874, 654, naud ? PC_TEXT : PC_MUT, 0);
    text_draw(ren, "X Legendas", 979, 654, nsub ? PC_TEXT : PC_MUT, 0);
    text_draw(ren, "+ Painel", 1116, 654, PC_TEXT, 0);
}

static void draw_center_state(SDL_Renderer *ren, const char *state, const char *detail, int accent) {
    // Same clean loading surface as the site: animated popcorn above centered
    // copy, with no separate rectangular screen inside the video.
    pfill(ren, 0, 0, PWIN_W, PWIN_H, PC_DARK, 184);
    ui_popcorn_draw(ren, PWIN_W / 2, 157, 190);
    int sw = 0, sh = 0;
    SDL_Texture *st = text_cached(ren, state, accent ? PC_ACC : PC_TEXT, 1, &sw, &sh);
    if (st) { SDL_Rect d = { (PWIN_W - sw) / 2, 369, sw, sh }; SDL_RenderCopy(ren, st, NULL, &d); }
    int dw = 0, dh = 0;
    SDL_Texture *dt = text_cached(ren, detail, PC_MUT, 0, &dw, &dh);
    if (dt) { SDL_Rect d = { (PWIN_W - dw) / 2, 420, dw, dh }; SDL_RenderCopy(ren, dt, NULL, &d); }
}

static int chapter_at(AVFormatContext *fmt, double target, double timeline_origin,
                      char *label, size_t label_cap) {
    if (label && label_cap) label[0] = 0;
    if (!fmt || fmt->nb_chapters == 0) return -1;
    int current = -1;
    for (unsigned i = 0; i < fmt->nb_chapters; i++) {
        AVChapter *chapter = fmt->chapters[i];
        double start = chapter->start * av_q2d(chapter->time_base) - timeline_origin;
        if (start <= target + 0.25) current = (int)i;
        else break;
    }
    if (current >= 0 && label && label_cap) {
        AVDictionaryEntry *title = av_dict_get(fmt->chapters[current]->metadata, "title", NULL, 0);
        if (title && title->value && title->value[0]) snprintf(label, label_cap, "%s", title->value);
        else snprintf(label, label_cap, "Capitulo %d", current + 1);
    }
    return current;
}

static double adjacent_chapter(AVFormatContext *fmt, double target, int forward,
                               double timeline_origin) {
    if (!fmt || fmt->nb_chapters == 0) return target;
    if (forward) {
        for (unsigned i = 0; i < fmt->nb_chapters; i++) {
            double start = fmt->chapters[i]->start * av_q2d(fmt->chapters[i]->time_base) - timeline_origin;
            if (start > target + 1.0) return start;
        }
    } else {
        for (int i = (int)fmt->nb_chapters - 1; i >= 0; i--) {
            double start = fmt->chapters[i]->start * av_q2d(fmt->chapters[i]->time_base) - timeline_origin;
            if (start < target - 1.0) return start;
        }
        return 0;
    }
    return target;
}

static void draw_timeline_seek(SDL_Renderer *ren, AVFormatContext *fmt,
                               double from, double target, double dur,
                               double timeline_origin) {
    const int x = 330, y = 126, w = 620, h = 214;
    char target_time[20], total_time[20], delta[72];
    fmt_time(target, target_time, sizeof(target_time));
    fmt_time(dur, total_time, sizeof(total_time));
    double difference = target - from;
    snprintf(delta, sizeof(delta), "%s%.0f segundos  |  %s / %s",
             difference >= 0 ? "+" : "", difference, target_time, total_time);
    pfill(ren, x, y, w, h, PC_DARK, 245);
    pfill(ren, x, y, 6, h, PC_ACC, 255);
    text_draw(ren, "ESCOLHER PONTO DO VIDEO", x + 34, y + 22, PC_ACC, 0);
    text_draw(ren, target_time, x + 34, y + 56, PC_TEXT, 1);
    draw_clipped_text(ren, delta, x + 190, y + 64, w - 224, PC_MUT, 0);
    char chapter[160];
    int chapter_index = chapter_at(fmt, target, timeline_origin, chapter, sizeof(chapter));
    if (chapter_index >= 0) {
        char chapter_line[196];
        snprintf(chapter_line, sizeof(chapter_line), "CAPITULO %d/%u  %s", chapter_index + 1, fmt->nb_chapters, chapter);
        draw_clipped_text(ren, chapter_line, x + 34, y + 104, w - 68, PC_ACC2, 0);
    } else text_draw(ren, "Mova o analogico para ajustar", x + 34, y + 104, PC_MUT, 0);
    draw_clipped_text(ren, chapter_index >= 0 ?
                      "Cima/baixo Capitulos  |  Analogico Ajustar" :
                      "Analogico Ajustar  |  Nada muda sem confirmar",
                      x + 34, y + 140, w - 68, PC_MUT, 0);
    text_draw(ren, "A Ir para este ponto", x + 34, y + 176, PC_TEXT, 0);
    text_draw(ren, "B Cancelar", x + 382, y + 176, PC_MUT, 0);
}

static int seek_video_time(AVFormatContext *fmt, int video_index, int is_hls,
                           double target, double timeline_origin, int flags) {
    int64_t global_ts = (int64_t)((target + timeline_origin) * AV_TIME_BASE);
    if (is_hls && video_index >= 0 &&
        fmt->streams[video_index]->time_base.num > 0 &&
        fmt->streams[video_index]->time_base.den > 0) {
        int64_t video_ts = av_rescale_q(global_ts, AV_TIME_BASE_Q,
                                        fmt->streams[video_index]->time_base);
        return av_seek_frame(fmt, video_index, video_ts, flags);
    }
    return av_seek_frame(fmt, -1, global_ts, flags);
}

static int apply_player_seek(AVFormatContext *fmt, int video_index, int is_hls,
                             AVCodecContext *vctx,
                             AVCodecContext *actx, AVCodecContext *sctx,
                             SDL_AudioDeviceID adev, double target,
                             double timeline_origin, double *wall_start,
                             double *audio_clock, double *cur_pos,
                             double *last_ac, double *last_ac_wall,
                             char *sub_text, double *sub_end) {
    int flags = target < *cur_pos ? AVSEEK_FLAG_BACKWARD : 0;
    if (seek_video_time(fmt, video_index, is_hls, target,
                        timeline_origin, flags) < 0)
        return -1;
    avcodec_flush_buffers(vctx);
    if (actx) avcodec_flush_buffers(actx);
    if (sctx) avcodec_flush_buffers(sctx);
    if (adev) SDL_ClearQueuedAudio(adev);
    sub_text[0] = 0;
    *sub_end = 0;
    double now = av_gettime_relative() / 1000000.0;
    *wall_start = now - target;
    *audio_clock = target;
    *cur_pos = target;
    *last_ac = -1;
    *last_ac_wall = now;
    return 0;
}
// Reabre somente o decoder de audio para a faixa escolhida e fecha o anterior.
// O resample e montado com os parametros reais de cada frame.
// HE-AAC e fontes que nao sao 48kHz (senao o audio sai errado e o video trava).
static int open_audio_dec(AVFormatContext *fmt, int aidx, AVCodecContext **pactx, struct SwrContext **pswr, int OCH, int ORATE) {
    (void)OCH; (void)ORATE;
    if (aidx < 0) {
        if (*pswr) swr_free(pswr);
        if (*pactx) avcodec_free_context(pactx);
        return -1;
    }
    AVCodecParameters *apar = fmt->streams[aidx]->codecpar;
    const AVCodec *adec = avcodec_find_decoder(apar->codec_id);
    if (!adec) return -1;
    AVCodecContext *actx = avcodec_alloc_context3(adec);
    if (!actx || avcodec_parameters_to_context(actx, apar) < 0) {
        avcodec_free_context(&actx);
        return -1;
    }
    if (avcodec_open2(actx, adec, NULL) != 0) { avcodec_free_context(&actx); return -1; }
    if (*pswr) swr_free(pswr);       // o loop reconstroi com os parametros do novo frame
    if (*pactx) avcodec_free_context(pactx);
    *pactx = actx;
    return 0;
}
// (re)abre o decoder de legenda p/ o stream sidx (-1 = desliga).
static int open_sub_dec(AVFormatContext *fmt, int sidx, AVCodecContext **psctx) {
    if (sidx < 0) {
        if (*psctx) avcodec_free_context(psctx);
        return 0;
    }
    AVCodecParameters *sp = fmt->streams[sidx]->codecpar;
    const AVCodec *sd = avcodec_find_decoder(sp->codec_id);
    if (!sd) return -1;
    AVCodecContext *sc = avcodec_alloc_context3(sd);
    if (!sc || avcodec_parameters_to_context(sc, sp) < 0) {
        avcodec_free_context(&sc);
        return -1;
    }
    if (avcodec_open2(sc, sd, NULL) != 0) { avcodec_free_context(&sc); return -1; }
    if (*psctx) avcodec_free_context(psctx);
    *psctx = sc;
    return 0;
}
// extrai o texto legivel de uma linha ASS (tira campos e tags {\...}, \N -> espaco).
static void ass_to_text(const char *ass, char *out, int cap) {
    const char *p = ass; int commas = 0;
    for (const char *q = ass; *q && commas < 8; q++) if (*q == ',') { commas++; p = q + 1; }
    if (commas < 8) p = ass;
    int k = 0;
    while (*p && k < cap - 1) {
        if (p[0] == '{') { const char *e = strchr(p, '}'); if (e) { p = e + 1; continue; } }
        if (p[0] == '\\' && (p[1] == 'N' || p[1] == 'n')) { out[k++] = ' '; p += 2; continue; }
        if (p[0] == '\r') { p++; continue; }
        if (p[0] == '\n') { out[k++] = ' '; p++; continue; }
        out[k++] = *p++;
    }
    out[k] = 0;
}
// desenha a legenda centralizada perto do rodape (com faixa de fundo).
static void draw_sub(SDL_Renderer *ren, const char *txt) {
    if (!txt || !txt[0]) return;
    SDL_Color white = { 245, 245, 245, 255 };
    const int maxw = PWIN_W - 100;
    char line1[512] = {0}, line2[512] = {0};
    snprintf(line1, sizeof(line1), "%s", txt);
    int full_w = 0, full_h = 0;
    text_cached(ren, line1, white, 0, &full_w, &full_h);
    if (full_w > maxw) {
        size_t len = strlen(line1), middle = len / 2, split = middle;
        while (split > 0 && line1[split] != ' ') split--;
        if (split == 0) { split = middle; while (split < len && line1[split] != ' ') split++; }
        if (split > 0 && split < len) {
            snprintf(line2, sizeof(line2), "%s", line1 + split + 1);
            line1[split] = '\0';
        }
    }
    int w1 = 0, h1 = 0, w2 = 0, h2 = 0;
    SDL_Texture *t1 = text_cached(ren, line1, white, 0, &w1, &h1);
    SDL_Texture *t2 = line2[0] ? text_cached(ren, line2, white, 0, &w2, &h2) : NULL;
    int boxw = w1 > w2 ? w1 : w2; if (boxw > maxw) boxw = maxw;
    int boxh = h1 + (t2 ? h2 + 4 : 0);
    int x = (PWIN_W - boxw) / 2, y = PWIN_H - 205 - (t2 ? h2 + 4 : 0);
    SDL_Color black = { 0, 0, 0, 255 };
    pfill(ren, x - 16, y - 7, boxw + 32, boxh + 14, black, 175);
    if (t1) {
        int rw = w1 > maxw ? maxw : w1;
        SDL_Rect src = { 0, 0, rw, h1 }, d = { (PWIN_W - rw) / 2, y, rw, h1 };
        SDL_RenderCopy(ren, t1, w1 > maxw ? &src : NULL, &d);
    }
    if (t2) {
        int rw = w2 > maxw ? maxw : w2;
        SDL_Rect src = { 0, 0, rw, h2 }, d = { (PWIN_W - rw) / 2, y + h1 + 4, rw, h2 };
        SDL_RenderCopy(ren, t2, w2 > maxw ? &src : NULL, &d);
    }
}

typedef struct { 
    int item_id;
    SDL_atomic_t session_id;
    SDL_atomic_t running;
    SDL_atomic_t current_pos;
    SDL_atomic_t duration;
    SDL_atomic_t force_progress;
    SDL_atomic_t pipeline_ready;
    PlayerProgressCallback progress_cb;
    PlayerHeartbeatCallback heartbeat_cb;
    void *callback_userdata;
} PlaybackHeartbeat;
static int player_play_internal(SDL_Renderer *ren, SDL_Joystick *joy, PlayerRequest *req,
                                PlaybackHeartbeat *heartbeat, double start_sec,
                                double *out_pos, double *out_dur,
                                int *out_resume_seeked, int *out_presented_frame) {
    Uint32 play_started_tick = SDL_GetTicks();
    Uint32 open_elapsed_ms = 0, probe_elapsed_ms = 0, first_present_ms = 0;
    g_player_presented_frame = 0;
    nplay_curl_avio_quality_reset();
    g_player_last_error[0] = '\0';
    player_boot_stage("01 inicio do player");
    player_boot_stage(appletGetAppletType() == AppletType_Application
                      ? "01 memoria: modo application"
                      : "01 memoria: modo applet");
    if (out_pos) *out_pos = 0;
    if (out_dur) *out_dur = 0;
    if (out_resume_seeked) *out_resume_seeked = 0;
    if (out_presented_frame) *out_presented_frame = 0;
    
    const char *url = req->url;
    int is_hls = (req->container && !strcmp(req->container, "m3u8"));
    int sequential_stream = req->playback.sequential_stream;
    const char *title = req->title;
    // Tela de preparacao enquanto abre a conexao e le os metadados.
    SDL_SetRenderDrawColor(ren, PC_DARK.r, PC_DARK.g, PC_DARK.b, 255); SDL_RenderClear(ren);
    draw_center_state(ren, "PREPARANDO VIDEO", "Conectando...  |  B para cancelar", 0);
    SDL_RenderPresent(ren);

    // Arquivos sdmc:/ usam o protocolo local. HLS remoto delega master, filhos e
    // segmentos ao callback libcurl; MP4 remoto continua no protocolo do FFmpeg.
    int remote = !strncmp(url, "http://", 7) || !strncmp(url, "https://", 8);
    // HLS precisa abrir a playlist e depois seus sub-manifestos/segmentos.
    int native_hls = remote && is_hls;
    PlayerOpenDeadline open_watch = {
        av_gettime_relative() + 30000000LL, joy, 0, 0,
        SDL_GetTicks(), SDL_GetTicks(), "abrindo", native_hls,
        ren, SDL_ThreadID(), SDL_GetTicks(), "Conectando...  |  B para cancelar"
    };
    nplay_curl_avio_set_abort_check(native_hls ? player_open_interrupted : NULL,
                                    native_hls ? &open_watch : NULL);
    nplay_curl_avio_set_startup_window(native_hls ? 30000u : 0u);
    // A build local ja possui HTTPS+TLS validado. Use o protocolo nativo tambem
    // para MP4: ele conhece Range/seek do MOV e elimina o AVIO por blocos que no
    // hardware ainda encerrava anime com `abrir fonte: End of file`.
    AVIOContext *avio = NULL;
    diag_player_event("format", "alloc-begin", "hls=%d remote=%d", native_hls, remote);
    AVFormatContext *fmt = avformat_alloc_context();
    if (!fmt) {
        diag_player_event("format", "alloc-fail", NULL);
        nplay_curl_avio_close(avio); player_error_message("memoria insuficiente para o formato"); return -1;
    }
    diag_player_event("format", "alloc-ok", NULL);
    const AVInputFormat *forced_format = NULL;
    if (native_hls) {
        // Nao entregue o manifesto raiz pelo io_open do proprio probe. As duas
        // capturas reais (0.9.8/0.9.9) mostram o processo morrendo exatamente
        // depois desse callback e antes de avformat_open_input retornar. Abra o
        // root explicitamente, marque-o como custom IO e informe o demuxer HLS;
        // a URL continua presente para resolver playlists/segmentos relativos.
        avio = nplay_curl_avio_open_hls(url);
        if (!avio) {
            diag_player_event("format", "root-open-fail", NULL);
            avformat_free_context(fmt);
            player_error_message(open_watch.cancelled ? "Abertura cancelada" :
                                 open_watch.timed_out ? "Manifesto HLS: tempo esgotado" :
                                 "nao foi possivel baixar o manifesto HLS");
            return open_watch.cancelled ? -11 : -10;
        }
        if (open_watch.cancelled || open_watch.timed_out) {
            diag_player_event("format", "root-open-interrupted", "cancel=%d timeout=%d",
                              open_watch.cancelled, open_watch.timed_out);
            nplay_curl_avio_close(avio);
            avformat_free_context(fmt);
            player_error_message(open_watch.cancelled ? "Abertura cancelada" :
                                 "Manifesto HLS: tempo esgotado");
            return open_watch.cancelled ? -11 : -10;
        }
        fmt->pb = avio;
        fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
        fmt->io_open = player_hls_io_open;
        fmt->io_close2 = player_hls_io_close;
        forced_format = av_find_input_format("hls");
        if (!forced_format) {
            diag_player_event("format", "hls-demuxer-missing", NULL);
            nplay_curl_avio_close(avio);
            avformat_free_context(fmt);
            player_error_message("demuxer HLS indisponivel nesta instalacao");
            return -10;
        }
        diag_player_event("format", "root-ready", "forced=hls");
        player_boot_stage("02 manifesto HLS raiz pronto");
    }
    player_boot_stage(native_hls ? "02 contexto HLS libcurl pronto" : "02 contexto de arquivo pronto");
    // A sondagem padrao pode ler cinco segundos/5 MB de CADA rendition HLS.
    // O R2 publica video, audios e legendas em playlists separadas; limite o
    // trabalho inicial sem impedir a leitura dos headers fMP4.
    fmt->probesize = native_hls ? 2 * 1024 * 1024 : 5 * 1024 * 1024;
    fmt->max_analyze_duration = native_hls ? 1500000 : 5000000;
    fmt->fps_probe_size = native_hls ? 6 : -1;
    if (native_hls) av_opt_set_int(fmt, "max_probe_packets", 64, 0);
    if (native_hls && req->delivery == DELIVERY_R2) {
        // O empacotador R2 e fixo em H.264/AAC/WebVTT, mas manifests antigos
        // reparados nao trazem CODECS. Sem esta informacao o Switch tenta inferir
        // codec abrindo todas as playlists antes do primeiro quadro.
        fmt->video_codec_id = AV_CODEC_ID_H264;
        fmt->audio_codec_id = AV_CODEC_ID_AAC;
        fmt->subtitle_codec_id = AV_CODEC_ID_WEBVTT;
    }
    if (!native_hls) open_watch.deadline_us = av_gettime_relative() + 30000000LL;
    fmt->interrupt_callback.callback = player_open_interrupted;
    fmt->interrupt_callback.opaque = &open_watch;

    AVDictionary *open_opts = NULL;
    if (remote) {
        av_dict_set(&open_opts, "user_agent", "Nplay-Switch/1.0", 0);
        av_dict_set(&open_opts, "tls_verify", "1", 0);
        av_dict_set(&open_opts, "rw_timeout", "30000000", 0);
        av_dict_set(&open_opts, "reconnect", "1", 0);
        av_dict_set(&open_opts, "reconnect_streamed", "1", 0);
        av_dict_set(&open_opts, "reconnect_on_network_error", "1", 0);
        av_dict_set(&open_opts, "reconnect_on_http_error", "4xx,5xx", 0);
        av_dict_set(&open_opts, "reconnect_delay_max", "5", 0);
        av_dict_set(&open_opts, "reconnect_max_retries", "3", 0);
        av_dict_set(&open_opts, "reconnect_delay_total_max", "15", 0);
        av_dict_set(&open_opts, "respect_retry_after", "1", 0);
        if (native_hls) {
            av_dict_set(&open_opts, "seekable", "0", 0);
            av_dict_set(&open_opts, "http_seekable", "0", 0);
            av_dict_set(&open_opts, "allowed_extensions", "ALL", 0);
            // Cada recurso e um AVIO libcurl proprio. A reutilizacao interna do
            // protocolo HTTP do FFmpeg nao e compativel com um io_open customizado.
            av_dict_set(&open_opts, "http_persistent", "0", 0);
            // O demuxer HLS do FFmpeg abre o proximo segmento antes de consumir
            // o atual quando http_multiple=1. Nosso io_open cria um AVIO/libcurl
            // independente para ele; nao reutiliza o AVIO atual nem o protocolo
            // HTTP nativo. Limite o prefetch ao R2, cujos segmentos sao imutaveis.
            av_dict_set(&open_opts, "http_multiple",
                        req->delivery == DELIVERY_R2 ? "1" : "0", 0);
            av_dict_set(&open_opts, "seg_max_retry", "3", 0);
        } else {
            av_dict_set(&open_opts, "seekable", sequential_stream ? "0" : "1", 0);
            av_dict_set(&open_opts, "multiple_requests", sequential_stream ? "0" : "1", 0);
        }
    }
    player_boot_stage("03 abrindo fonte");
    diag_player_event("format", "open-begin", "timeout=30s");
    Uint32 open_started_tick = SDL_GetTicks();
    int rc = avformat_open_input(&fmt, url, forced_format, &open_opts);
    open_elapsed_ms = SDL_GetTicks() - open_started_tick;
    av_dict_free(&open_opts);
    if (rc != 0) {
        diag_player_event("format", "open-fail", "rc=%d cancelled=%d ms=%u", rc,
                          open_watch.cancelled, open_elapsed_ms);
        if (open_watch.cancelled) player_error_message("Abertura cancelada");
        else if (rc == AVERROR_EXIT) player_error_message(native_hls ? "abrir playlist HLS: tempo esgotado" : "abrir fonte: tempo esgotado");
        else player_error_text(native_hls ? "abrir playlist HLS" : "abrir fonte", rc);
        nplay_curl_avio_close(avio); return open_watch.cancelled ? -11 : -10;
    }
    if (open_watch.cancelled || open_watch.timed_out) {
        diag_player_event("format", "open-interrupted", "ms=%u cancel=%d timeout=%d",
                          open_elapsed_ms, open_watch.cancelled, open_watch.timed_out);
        avformat_close_input(&fmt);
        nplay_curl_avio_close(avio);
        player_error_message(open_watch.cancelled ? "Abertura cancelada" :
                             "Abrir playlist HLS: tempo esgotado");
        return open_watch.cancelled ? -11 : -10;
    }
    diag_player_event("format", "open-ok", "streams=%u ms=%u", fmt->nb_streams,
                      open_elapsed_ms);
    open_watch.phase = "faixas";
    open_watch.detail = native_hls ? "Playlist aberta. Lendo video e audio..." :
                                     "Fonte aberta. Lendo video e audio...";
    player_boot_stage("04 fonte aberta");
    SDL_SetRenderDrawColor(ren, PC_DARK.r, PC_DARK.g, PC_DARK.b, 255); SDL_RenderClear(ren);
    draw_center_state(ren, "PREPARANDO VIDEO", open_watch.detail, 0);
    SDL_RenderPresent(ren);
    open_watch.last_render_tick = SDL_GetTicks();
    if (!native_hls) open_watch.deadline_us = av_gettime_relative() + 30000000LL;
    // Na abertura HLS, priorize video e um audio. Legendas e audios alternativos
    // continuam enumerados; suas playlists so sao lidas quando selecionadas.
    int probe_audio = -1, probe_video = -1;
    if (native_hls) {
        for (unsigned i = 0; i < fmt->nb_streams; i++) {
            if (probe_audio < 0 && fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                probe_audio = (int)i;
            if (probe_video < 0 && fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                probe_video = (int)i;
        }
        // O primeiro pacote do demuxer HLS aplica discard as playlists. Nao
        // sondar variantes que nao participam do primeiro quadro.
        player_select_hls_streams(fmt, probe_video, probe_audio, -1);
    }
    int headers_ready = 0;
    if (native_hls) {
        int header_video = -1, header_audio = -1;
        for (unsigned i = 0; i < fmt->nb_streams; i++) {
            AVCodecParameters *par = fmt->streams[i]->codecpar;
            if (header_video < 0 && par->codec_type == AVMEDIA_TYPE_VIDEO &&
                par->codec_id != AV_CODEC_ID_NONE && par->width > 0 && par->height > 0)
                header_video = (int)i;
            if (header_audio < 0 && par->codec_type == AVMEDIA_TYPE_AUDIO &&
                par->codec_id != AV_CODEC_ID_NONE)
                header_audio = (int)i;
        }
        headers_ready = header_video >= 0 && (probe_audio < 0 || header_audio >= 0);
    }
    player_boot_stage(headers_ready ? "05 headers completos" : "05 lendo faixas");
    diag_player_event("format", headers_ready ? "probe-skip" : "probe-begin",
                      "streams=%u", fmt->nb_streams);
    Uint32 probe_started_tick = SDL_GetTicks();
    rc = headers_ready ? 0 : avformat_find_stream_info(fmt, NULL);
    probe_elapsed_ms = SDL_GetTicks() - probe_started_tick;
    if (rc < 0) {
        diag_player_event("format", "probe-fail", "rc=%d streams=%u ms=%u", rc,
                          fmt->nb_streams, probe_elapsed_ms);
        if (open_watch.cancelled) player_error_message("Abertura cancelada");
        else if (rc == AVERROR_EXIT) player_error_message("ler faixas do video: tempo esgotado");
        else player_error_text("ler faixas do video", rc);
        avformat_close_input(&fmt); nplay_curl_avio_close(avio);
        return open_watch.cancelled ? -11 : -2;
    }
    if (open_watch.cancelled || open_watch.timed_out) {
        diag_player_event("format", "probe-interrupted", "ms=%u cancel=%d timeout=%d",
                          probe_elapsed_ms, open_watch.cancelled, open_watch.timed_out);
        avformat_close_input(&fmt);
        nplay_curl_avio_close(avio);
        player_error_message(open_watch.cancelled ? "Abertura cancelada" :
                             "Ler faixas HLS: tempo esgotado");
        return open_watch.cancelled ? -11 : -2;
    }
    // Continue vigiando ate o primeiro quadro: playlists podem abrir sem que
    // o primeiro segmento tenha entregue bytes suficientes para reproduzir.
    // MP4 remoto tambem precisa de limite ate o primeiro quadro. Um servidor
    // que devolve metadados mas nunca entrega media nao pode prender a tela.
    if (!native_hls) open_watch.deadline_us = remote ? av_gettime_relative() + 45000000LL : 0;
    open_watch.phase = "quadro";
    open_watch.detail = "Lendo os primeiros quadros...  |  B para cancelar";

    player_boot_stage("06 faixas prontas");
    diag_player_event("format", "probe-ok", "streams=%u ms=%u duration=%lld",
                      fmt->nb_streams, probe_elapsed_ms, (long long)fmt->duration);
    int vidx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (vidx < 0) {
        diag_player_event("streams", "video-missing", "rc=%d", vidx);
        player_error_text("localizar faixa de video", vidx); avformat_close_input(&fmt); nplay_curl_avio_close(avio); return -3;
    }

    // enumera faixas de AUDIO e de LEGENDA (so legendas de texto: SRT/ASS/mov_text)
    int aidxs[16], naud = 0, sidxs[16], nsub = 0;
    for (unsigned i = 0; i < fmt->nb_streams; i++) {
        int t = fmt->streams[i]->codecpar->codec_type, cid = fmt->streams[i]->codecpar->codec_id;
        if (t == AVMEDIA_TYPE_AUDIO && naud < 16) aidxs[naud++] = (int)i;
        else if (t == AVMEDIA_TYPE_SUBTITLE && nsub < 16 &&
                 (cid == AV_CODEC_ID_SUBRIP || cid == AV_CODEC_ID_ASS || cid == AV_CODEC_ID_SSA ||
                  cid == AV_CODEC_ID_MOV_TEXT || cid == AV_CODEC_ID_TEXT || cid == AV_CODEC_ID_WEBVTT))
            sidxs[nsub++] = (int)i;
    }
    int acur = 0, best = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    for (int i = 0; i < naud; i++) if (aidxs[i] == best) acur = i;
    
    char pref_aud[32] = ""; store_load_pref_audio(pref_aud, sizeof(pref_aud));
    char pref_sub[32] = ""; store_load_pref_sub(pref_sub, sizeof(pref_sub));
    int scur = -1;                       // -1 = legenda desligada

    if (!pref_aud[0] && !pref_sub[0]) {
        // --- SMART DEFAULT (PORTUGUES) ---
        int has_pt_audio = 0;
        for (int i = 0; i < naud; i++) {
            AVDictionaryEntry *tag = av_dict_get(fmt->streams[aidxs[i]]->metadata, "language", NULL, 0);
            if (tag && (strncasecmp(tag->value, "por", 3) == 0 || strncasecmp(tag->value, "pt", 2) == 0)) { 
                acur = i; 
                has_pt_audio = 1;
                break; 
            }
        }
        if (!has_pt_audio) {
            // Se nao tem audio PT, liga a legenda PT (se existir)
            for (int i = 0; i < nsub; i++) {
                AVDictionaryEntry *tag = av_dict_get(fmt->streams[sidxs[i]]->metadata, "language", NULL, 0);
                if (tag && (strncasecmp(tag->value, "por", 3) == 0 || strncasecmp(tag->value, "pt", 2) == 0)) { 
                    scur = i; 
                    break; 
                }
            }
        }
    } else {
        // --- PREFERENCIAS SALVAS ---
        if (pref_aud[0]) {
            for (int i = 0; i < naud; i++) {
                AVDictionaryEntry *tag = av_dict_get(fmt->streams[aidxs[i]]->metadata, "language", NULL, 0);
                if (tag && strncasecmp(tag->value, pref_aud, 3) == 0) { acur = i; break; }
            }
        }
        if (pref_sub[0]) {
            if (strcasecmp(pref_sub, "off") == 0) scur = -1;
            else {
                for (int i = 0; i < nsub; i++) {
                    AVDictionaryEntry *tag = av_dict_get(fmt->streams[sidxs[i]]->metadata, "language", NULL, 0);
                    if (tag && strncasecmp(tag->value, pref_sub, 3) == 0) { scur = i; break; }
                }
            }
        }
    }
    
    int aidx = naud ? aidxs[acur] : -1;
    diag_player_event("streams", "selected", "video=%d audio=%d naud=%d nsub=%d", vidx, aidx, naud, nsub);
    AVCodecContext *sctx = NULL;
    char sub_text[512] = ""; double sub_end = 0;
    if (scur >= 0 && open_sub_dec(fmt, sidxs[scur], &sctx) != 0) scur = -1;
    if (native_hls) {
        player_select_hls_streams(fmt, vidx, aidx, scur >= 0 ? sidxs[scur] : -1);
        diag_player_event("hls-io", "tracks", "video=%d audio=%d subtitle=%d",
                          vidx, aidx, scur >= 0 ? sidxs[scur] : -1);
    }

    double dur = (fmt->duration > 0) ? fmt->duration / (double)AV_TIME_BASE : 0;
    double timeline_origin = (fmt->start_time != AV_NOPTS_VALUE)
        ? fmt->start_time / (double)AV_TIME_BASE : 0;
    if (out_dur) *out_dur = dur;

    AVCodecContext *vctx = NULL, *actx = NULL;
    struct SwrContext *swr = NULL;
    SDL_AudioDeviceID adev = 0;
    SDL_Texture *tex = NULL;
    struct SwsContext *sws = NULL;
    AVFrame *yuv = NULL, *frame = NULL, *transfer = NULL;
    AVPacket *pkt = NULL;
#define PLAYER_SETUP_FAIL(code) do { \
    diag_player_event("setup", "fail", "rc=%d", (code)); \
    if (adev) SDL_CloseAudioDevice(adev); \
    if (sws) sws_freeContext(sws); \
    if (yuv) av_frame_free(&yuv); \
    if (transfer) av_frame_free(&transfer); \
    if (swr) swr_free(&swr); \
    if (actx) avcodec_free_context(&actx); \
    if (sctx) avcodec_free_context(&sctx); \
    if (vctx) avcodec_free_context(&vctx); \
    if (frame) av_frame_free(&frame); \
    if (pkt) av_packet_free(&pkt); \
    if (tex) SDL_DestroyTexture(tex); \
    avformat_close_input(&fmt); nplay_curl_avio_close(avio); \
    return (code); \
} while (0)

    // ---- decoder de video ----
    AVCodecParameters *vpar = fmt->streams[vidx]->codecpar;
    diag_player_event("video", "decoder-begin", "codec=%d size=%dx%d", vpar->codec_id, vpar->width, vpar->height);
    const AVCodec *vdec = avcodec_find_decoder(vpar->codec_id);
    if (!vdec) PLAYER_SETUP_FAIL(-4);
    vctx = avcodec_alloc_context3(vdec);
    if (!vctx || avcodec_parameters_to_context(vctx, vpar) < 0) PLAYER_SETUP_FAIL(-4);
    vctx->thread_count = 4;
    int tried_hw = 0;
    if (vpar->codec_id == AV_CODEC_ID_H264 || vpar->codec_id == AV_CODEC_ID_HEVC) {
        AVBufferRef *device = NULL;
        diag_player_event("video", "hw-device-begin", NULL);
        int hw_rc = av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_NVTEGRA, NULL, NULL, 0);
        diag_player_event("video", hw_rc >= 0 ? "hw-device-ok" : "hw-device-fail", "rc=%d", hw_rc);
        if (hw_rc >= 0) {
            vctx->hw_device_ctx = av_buffer_ref(device);
            av_buffer_unref(&device);
            tried_hw = vctx->hw_device_ctx != NULL;
            if (tried_hw) vctx->get_format = player_select_video_format;
        }
    }
    int video_open_rc = avcodec_open2(vctx, vdec, NULL);
    diag_player_event("video", video_open_rc >= 0 ? "decoder-open-ok" : "decoder-open-fail",
                      "rc=%d hw=%d", video_open_rc, tried_hw);
    if (video_open_rc < 0) {
        // Um perfil nao suportado pelo NVDEC nao pode impedir a reproducao.
        // Reabre o mesmo decoder sem dispositivo e preserva o caminho por CPU.
        if (!tried_hw) PLAYER_SETUP_FAIL(-4);
        avcodec_free_context(&vctx);
        vctx = avcodec_alloc_context3(vdec);
        if (!vctx || avcodec_parameters_to_context(vctx, vpar) < 0) PLAYER_SETUP_FAIL(-4);
        vctx->thread_count = 4;
        video_open_rc = avcodec_open2(vctx, vdec, NULL);
        diag_player_event("video", video_open_rc >= 0 ? "cpu-fallback-ok" : "cpu-fallback-fail",
                          "rc=%d", video_open_rc);
        if (video_open_rc < 0) PLAYER_SETUP_FAIL(-4);
    }
    player_boot_stage("07 decoder de video pronto");

    // ---- decoder de audio + resample + saida SDL ----
    const int OCH = 2, ORATE = 48000;
    if (aidx >= 0 && open_audio_dec(fmt, aidx, &actx, &swr, OCH, ORATE) == 0) {
        SDL_AudioSpec want; SDL_zero(want);
        want.freq = ORATE; want.format = AUDIO_S16SYS; want.channels = OCH; want.samples = 2048;
        adev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
        diag_player_event("audio", adev ? "device-ok" : "device-fail",
                          "stream=%d status=%s", aidx, adev ? "ok" : SDL_GetError());
        // Start sound with the first visible frame. On slow direct/remux reads,
        // playing the queue while video is still opening creates an A/V offset.
        if (adev) SDL_PauseAudioDevice(adev, 1);
        else { if (swr) swr_free(&swr); avcodec_free_context(&actx); }
    } else diag_player_event("audio", "decoder-unavailable", "stream=%d", aidx);
    player_boot_stage("08 audio pronto");

    int vw = vctx->width, vh = vctx->height;
    if (vw <= 0 || vh <= 0) PLAYER_SETUP_FAIL(-4);
    transfer = av_frame_alloc();
    if (!transfer) PLAYER_SETUP_FAIL(-4);
    Uint32 texture_format = 0;

    // retangulo com letterbox (1280x720)
    int dw = PWIN_W, dh = PWIN_H;
    double ar = (double)vw / vh, dar = (double)dw / dh;
    SDL_Rect dst;
    if (ar > dar) { dst.w = dw; dst.h = (int)(dw / ar); } else { dst.h = dh; dst.w = (int)(dh * ar); }
    dst.x = (dw - dst.w) / 2; dst.y = (dh - dst.h) / 2;

    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    if (!pkt || !frame) PLAYER_SETUP_FAIL(-4);
#undef PLAYER_SETUP_FAIL
    AVRational vtb = fmt->streams[vidx]->time_base;
    AVRational atb = (aidx >= 0) ? fmt->streams[aidx]->time_base : (AVRational){1, ORATE};
    double bps = (double)ORATE * OCH * 2.0;
    double audio_clock = 0, wall_start = av_gettime_relative() / 1000000.0;
    double last_ac = -1, last_ac_wall = av_gettime_relative() / 1000000.0;  // detecta audio travado
    double cur_pos = 0;
    int running = 1, paused = 0, vol = 100, reached_end = 0, playback_error = 0;
    int decoded_video = 0, dropped_video = 0, buffering_events = 0, hardware_decode = 0;
    int slow_reads = 0, present_gaps = 0;
    int read_gaps = 0, sync_gaps = 0, other_gaps = 0;
    Uint32 worst_read_ms = 0, worst_present_ms = 0, last_present_tick = 0;
    Uint32 read_since_present_ms = 0, sync_since_present_ms = 0;
    unsigned max_audio_queue = 0;
    store_load_player_volume(&vol);
    int swr_rate = 0, swr_fmt = -1, swr_ch = 0;   // config atual do resample (do frame real)
    uint8_t *audio_buf = NULL;
    unsigned int audio_buf_cap = 0;                // reutilizado entre frames (evita churn no heap)
    Uint32 hud_until = SDL_GetTicks() + 4000;   // HUD visivel ao iniciar
    Uint32 buffering_since = 0;
    Uint32 buffering_audio_ms = 0, longest_buffer_ms = 0;
    Uint32 notice_until = 0;
    char notice[96] = "";
    int hud_pinned = 0, have_video_frame = 0;
    int logged_first_read = 0, logged_first_video_packet = 0;
    int logged_first_video_frame = 0, logged_first_present = 0;
    int track_menu = 0, track_sel = 0;
    int timeline_seek = 0, timeline_seek_was_paused = 0, seek_axis_lock = 0;
    int seek_arm_dir = 0;
    double timeline_seek_from = 0, timeline_seek_target = 0;
    Uint32 timeline_seek_tick = SDL_GetTicks(), seek_arm_since = 0;
    Uint32 first_frame_started = SDL_GetTicks();
    Uint32 first_frame_budget_ms = native_hls ? 30000u : 45000u;
    int resume_preroll = 0, resume_preroll_frames = 0;
    double resume_target = 0;
    SDL_Event e;

    // Retoma de onde parou somente quando ha margem suficiente ate o fim.
    if (!sequential_stream && start_sec > 3 && (dur <= 0 || start_sec < dur - 5)) {
        if (native_hls) {
            // hls_read_seek calcula o segmento usando first_timestamp. Com o
            // probe de cabecalhos pulado, ele ainda nao existe ate o primeiro
            // pacote. Leia um pacote (sem decodificar) para fixar a linha do
            // tempo antes de buscar a posicao salva.
            player_boot_stage("08 fixando linha do tempo");
            open_watch.detail = "Preparando a retomada...  |  B para cancelar";
            Uint32 warm_started = SDL_GetTicks();
            int warm_rc = AVERROR(EAGAIN);
            while (SDL_GetTicks() - warm_started < 8000u && !open_watch.cancelled &&
                   !open_watch.timed_out) {
                warm_rc = av_read_frame(fmt, pkt);
                if (warm_rc != AVERROR(EAGAIN)) break;
                player_open_interrupted(&open_watch);
                SDL_Delay(25);
            }
            diag_player_event("seek", "timeline-warm",
                              "rc=%d ms=%u stream=%d", warm_rc,
                              SDL_GetTicks() - warm_started,
                              warm_rc >= 0 ? pkt->stream_index : -1);
            av_packet_unref(pkt);
            if (warm_rc < 0 || open_watch.cancelled || open_watch.timed_out) {
                if (out_resume_seeked) *out_resume_seeked = 1;
                if (open_watch.cancelled) running = 0;
                else {
                    player_error_message("Nao foi possivel preparar a retomada HLS");
                    playback_error = -5;
                    running = 0;
                }
            }
        }
        if (running) {
            player_boot_stage("08 retomando posicao");
            open_watch.detail = "Buscando o ponto salvo...  |  B para cancelar";
            diag_player_event("seek", "resume-begin", "pos=%.1f video=%d", start_sec, vidx);
            Uint32 seek_started = SDL_GetTicks();
            int seek_rc = seek_video_time(fmt, vidx, native_hls, start_sec,
                                          timeline_origin, AVSEEK_FLAG_BACKWARD);
            diag_player_event("seek", "resume-end", "rc=%d ms=%u", seek_rc,
                              SDL_GetTicks() - seek_started);
            if (seek_rc >= 0) {
                if (out_resume_seeked) *out_resume_seeked = 1;
                audio_clock = start_sec; cur_pos = start_sec;
                wall_start = av_gettime_relative() / 1000000.0 - start_sec;
                if (native_hls) {
                    resume_preroll = 1;
                    resume_target = start_sec;
                    if (adev) {
                        SDL_ClearQueuedAudio(adev);
                        SDL_PauseAudioDevice(adev, 1);
                    }
                }
                // Um seek HLS pode retornar sucesso mas nao entregar o primeiro
                // quadro. O supervisor preserva o progresso e recupera a fonte.
                if (native_hls) {
                    int64_t resume_deadline = av_gettime_relative() + 20000000LL;
                    if (open_watch.deadline_us > resume_deadline)
                        open_watch.deadline_us = resume_deadline;
                    nplay_curl_avio_set_startup_window(20000u);
                    first_frame_started = SDL_GetTicks();
                    first_frame_budget_ms = 20000u;
                }
            }
            if (open_watch.timed_out && out_resume_seeked) *out_resume_seeked = 1;
            if (open_watch.cancelled) running = 0;
            if (open_watch.timed_out) {
                player_error_message("Video nao iniciou no tempo esperado");
                playback_error = -5;
                running = 0;
            }
        }
    }

    // Heartbeat & Progress tracking are now managed by a separate thread
    Uint32 last_heartbeat = SDL_GetTicks();
    PlaybackHeartbeat *hb = heartbeat;
    char playing_stage[96];
    snprintf(playing_stage, sizeof(playing_stage), "09 reproduzindo %s %s%s",
             req->playback.delivery_str[0] ? req->playback.delivery_str : "direto",
             req->container ? req->container : "arquivo",
             sequential_stream ? " continuo" : "");
    player_boot_stage(playing_stage);

    while (running) {
        Uint32 now_ticks = SDL_GetTicks();
        // A retomada pode consumir muitos pacotes de preroll sem bloquear na
        // rede. Continue redesenhando a animacao tambem nesse caminho de CPU.
        if (!logged_first_present && player_open_interrupted(&open_watch)) {
            if (open_watch.timed_out) {
                player_error_message("Video nao iniciou no tempo esperado");
                playback_error = -5;
            }
            break;
        }
        if (remote && !logged_first_present &&
            now_ticks - first_frame_started >= first_frame_budget_ms) {
            diag_player_event("player", "first-frame-timeout", "elapsed=%u", now_ticks - first_frame_started);
            player_error_message("Video nao iniciou no tempo esperado");
            playback_error = -5;
            break;
        }
        if (now_ticks - last_heartbeat > 1000) {
            last_heartbeat = now_ticks;
            if (hb) {
                SDL_AtomicSet(&hb->current_pos, (int)cur_pos);
                SDL_AtomicSet(&hb->duration, (int)dur);
            }
        }

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_JOYBUTTONDOWN) {
                int b = e.jbutton.button;
                // Antes do primeiro quadro, so cancelar faz sentido. Pausa,
                // menus e novo seek poderiam deixar a retomada parada.
                if (native_hls && !have_video_frame &&
                    b != JOY_B && b != JOY_MINUS) continue;
                hud_until = SDL_GetTicks() + 4000;
                if (timeline_seek) {
                    if (b == JOY_A) {
                        if (apply_player_seek(fmt, vidx, native_hls, vctx, actx, sctx, adev,
                                              timeline_seek_target, timeline_origin,
                                              &wall_start, &audio_clock, &cur_pos,
                                              &last_ac, &last_ac_wall, sub_text, &sub_end) == 0) {
                            last_present_tick = 0;
                            snprintf(notice, sizeof(notice), "Reproducao em %.0f%%",
                                     dur > 0 ? timeline_seek_target * 100.0 / dur : 0.0);
                            if (hb) SDL_AtomicSet(&hb->force_progress, 1);
                        } else snprintf(notice, sizeof(notice), "Nao foi possivel buscar neste video");
                        notice_until = SDL_GetTicks() + 2000;
                        timeline_seek = 0; seek_axis_lock = 1;
                        paused = timeline_seek_was_paused;
                        if (adev && !paused) SDL_PauseAudioDevice(adev, 0);
                    } else if (b == JOY_B || b == JOY_MINUS) {
                        timeline_seek = 0; seek_axis_lock = 1;
                        paused = timeline_seek_was_paused;
                        double resume_now = av_gettime_relative() / 1000000.0;
                        wall_start = resume_now - cur_pos;
                        last_ac = -1; last_ac_wall = resume_now;
                        if (adev && !paused) SDL_PauseAudioDevice(adev, 0);
                        snprintf(notice, sizeof(notice), "Busca cancelada");
                        notice_until = SDL_GetTicks() + 1500;
                    } else if ((b == JOY_UP || b == JOY_DOWN) && fmt->nb_chapters > 0) {
                        timeline_seek_target = adjacent_chapter(fmt, timeline_seek_target,
                                                               b == JOY_DOWN, timeline_origin);
                        if (timeline_seek_target < 0) timeline_seek_target = 0;
                        if (timeline_seek_target > dur - 1) timeline_seek_target = dur - 1;
                    } else if (b == JOY_DLEFT || b == JOY_DRIGHT ||
                               b == JOY_L || b == JOY_R || b == JOY_ZL || b == JOY_ZR) {
                        double step = (b == JOY_ZL || b == JOY_ZR) ? 60.0 : 10.0;
                        int forward = b == JOY_DRIGHT || b == JOY_R || b == JOY_ZR;
                        timeline_seek_target += forward ? step : -step;
                        if (timeline_seek_target < 0) timeline_seek_target = 0;
                        if (timeline_seek_target > dur - 1) timeline_seek_target = dur - 1;
                    }
                    continue;
                }
                if (track_menu) {
                    int total = (track_menu == TRACK_MENU_AUDIO) ? naud : nsub + 1;
                    if (b == JOY_UP && track_sel > 0) track_sel--;
                    else if (b == JOY_DOWN && track_sel + 1 < total) track_sel++;
                    else if (b == JOY_B || b == JOY_MINUS ||
                             (track_menu == TRACK_MENU_AUDIO && b == JOY_Y) ||
                             (track_menu == TRACK_MENU_SUB && b == JOY_X)) {
                        track_menu = 0;
                        double resume_now = av_gettime_relative() / 1000000.0;
                        wall_start = resume_now - cur_pos;
                        audio_clock = cur_pos; last_ac = -1; last_ac_wall = resume_now;
                        if (adev && !paused) SDL_PauseAudioDevice(adev, 0);
                    } else if (b == JOY_A) {
                        if (track_menu == TRACK_MENU_AUDIO) {
                            if (track_sel == acur) {
                                snprintf(notice, sizeof(notice), "Audio atual mantido");
                            } else {
                                int next_idx = aidxs[track_sel];
                                if (!adev) {
                                    snprintf(notice, sizeof(notice), "Saida de audio indisponivel");
                                } else if (open_audio_dec(fmt, next_idx, &actx, &swr, OCH, ORATE) == 0) {
                                    acur = track_sel; aidx = next_idx;
                                    if (native_hls)
                                        player_select_hls_streams(fmt, vidx, aidx,
                                                                  scur >= 0 ? sidxs[scur] : -1);
                                    atb = fmt->streams[aidx]->time_base;
                                    if (adev) SDL_ClearQueuedAudio(adev);
                                    audio_clock = cur_pos; last_ac = -1;
                                    last_ac_wall = av_gettime_relative() / 1000000.0;
                                    char lang[48]; format_language(stream_lang(fmt, aidx), lang, sizeof(lang));
                                    snprintf(notice, sizeof(notice), "Audio %d/%d  %s", acur + 1, naud, lang);
                                    AVDictionaryEntry *tag = av_dict_get(fmt->streams[aidx]->metadata, "language", NULL, 0);
                                    if (tag) store_save_pref_audio(tag->value);
                                } else snprintf(notice, sizeof(notice), "Nao consegui abrir esta faixa de audio");
                            }
                        } else {
                            int next = track_sel - 1;
                            if (next == scur) {
                                snprintf(notice, sizeof(notice), "Legenda atual mantida");
                            } else if (open_sub_dec(fmt, next >= 0 ? sidxs[next] : -1, &sctx) == 0) {
                                scur = next; sub_text[0] = 0; sub_end = 0;
                                if (native_hls)
                                    player_select_hls_streams(fmt, vidx, aidx,
                                                              scur >= 0 ? sidxs[scur] : -1);
                                if (scur >= 0) {
                                    char lang[48]; format_language(stream_lang(fmt, sidxs[scur]), lang, sizeof(lang));
                                    snprintf(notice, sizeof(notice), "Legenda %d/%d  %s", scur + 1, nsub, lang);
                                    AVDictionaryEntry *tag = av_dict_get(fmt->streams[sidxs[scur]]->metadata, "language", NULL, 0);
                                    if (tag) store_save_pref_sub(tag->value);
                                } else {
                                    snprintf(notice, sizeof(notice), "Legendas desligadas");
                                    store_save_pref_sub("off");
                                }
                            } else snprintf(notice, sizeof(notice), "Nao consegui abrir esta legenda");
                        }
                        notice_until = SDL_GetTicks() + 2200;
                        track_menu = 0;
                        double resume_now = av_gettime_relative() / 1000000.0;
                        wall_start = resume_now - cur_pos;
                        audio_clock = cur_pos; last_ac = -1; last_ac_wall = resume_now;
                        if (adev && !paused) SDL_PauseAudioDevice(adev, 0);
                    }
                    continue;
                }
                if (b == JOY_B || b == JOY_MINUS) running = 0;
                else if (b == JOY_PLUS) hud_pinned = !hud_pinned;
                else if (b == JOY_A) {
                    paused = !paused;
                    last_present_tick = 0;
                    if (!paused) {
                        double resume_now = av_gettime_relative() / 1000000.0;
                        wall_start = resume_now - cur_pos;
                        last_ac = -1;
                        last_ac_wall = resume_now;
                    }
                    if (adev) SDL_PauseAudioDevice(adev, paused || !logged_first_present);
                    if (hb && paused) SDL_AtomicSet(&hb->force_progress, 1);
                }
                else if (b == JOY_UP || b == JOY_DOWN) {
                    vol += (b == JOY_UP) ? 10 : -10;
                    if (vol > 100) vol = 100;
                    if (vol < 0) vol = 0;
                    snprintf(notice, sizeof(notice), "Volume  %d%%", vol);
                    notice_until = SDL_GetTicks() + 1800;
                }
                else if (sequential_stream &&
                         (b == JOY_R || b == JOY_L || b == JOY_ZR || b == JOY_ZL)) {
                    snprintf(notice, sizeof(notice), "Busca disponivel apos o preparo completo");
                    notice_until = SDL_GetTicks() + 2200;
                }
                else if (b == JOY_R || b == JOY_L || b == JOY_ZR || b == JOY_ZL) {
                    double step = (b == JOY_ZR || b == JOY_ZL) ? 60 : 10;
                    int forward = (b == JOY_R || b == JOY_ZR);
                    double t = cur_pos + (forward ? step : -step);
                    if (t < 0) t = 0;
                    if (dur > 0 && t > dur - 1) t = dur - 1;
                    if (apply_player_seek(fmt, vidx, native_hls, vctx, actx, sctx, adev, t,
                                          timeline_origin, &wall_start, &audio_clock,
                                          &cur_pos, &last_ac, &last_ac_wall,
                                          sub_text, &sub_end) == 0) {
                        last_present_tick = 0;
                        snprintf(notice, sizeof(notice), "%s %.0f segundos", forward ? "Avancou" : "Voltou", step);
                        if (hb) SDL_AtomicSet(&hb->force_progress, 1);
                    } else {
                        snprintf(notice, sizeof(notice), "Nao foi possivel buscar neste video");
                    }
                    notice_until = SDL_GetTicks() + 1800;
                }
                else if (b == JOY_Y) {
                    if (naud > 1) {
                        track_menu = TRACK_MENU_AUDIO; track_sel = acur;
                        if (adev && !paused) SDL_PauseAudioDevice(adev, 1);
                    } else snprintf(notice, sizeof(notice), "Este video possui apenas um audio");
                    if (!track_menu) notice_until = SDL_GetTicks() + 2200;
                }
                else if (b == JOY_X) {
                    if (nsub > 0) {
                        track_menu = TRACK_MENU_SUB; track_sel = scur + 1;
                        if (adev && !paused) SDL_PauseAudioDevice(adev, 1);
                    } else snprintf(notice, sizeof(notice), "Este video nao possui legendas");
                    if (!track_menu) notice_until = SDL_GetTicks() + 2200;
                }
            } else if (e.type == SDL_FINGERDOWN) {
                int tx = (int)(e.tfinger.x * PWIN_W);
                int ty = (int)(e.tfinger.y * PWIN_H);
                hud_until = SDL_GetTicks() + 4000;
                if (ty < 96 && tx < 180) { running = 0; continue; }
                if (!have_video_frame || track_menu || timeline_seek) continue;
                int touch_seek = 0;
                double target = cur_pos;
                if (!sequential_stream && dur > 1 && ty >= 580 && ty < 635 &&
                    tx >= 48 && tx <= 1128) {
                    target = dur * (tx - 48) / 1080.0;
                    touch_seek = 1;
                } else if (!sequential_stream && ty >= 635 && tx >= 120 && tx < 290) {
                    target += tx < 205 ? -10 : 10;
                    touch_seek = 1;
                } else if (ty >= 635 && tx < 115) {
                    paused = !paused;
                    last_present_tick = 0;
                    if (!paused) {
                        double resume_now = av_gettime_relative() / 1000000.0;
                        wall_start = resume_now - cur_pos;
                        last_ac = -1;
                        last_ac_wall = resume_now;
                    }
                    if (adev) SDL_PauseAudioDevice(adev, paused);
                    if (hb && paused) SDL_AtomicSet(&hb->force_progress, 1);
                } else if (ty >= 635 && tx >= 870 && tx < 975 && naud > 1) {
                    track_menu = TRACK_MENU_AUDIO; track_sel = acur;
                    if (adev && !paused) SDL_PauseAudioDevice(adev, 1);
                } else if (ty >= 635 && tx >= 975 && tx < 1110 && nsub > 0) {
                    track_menu = TRACK_MENU_SUB; track_sel = scur + 1;
                    if (adev && !paused) SDL_PauseAudioDevice(adev, 1);
                } else if (ty >= 635 && tx >= 1110) hud_pinned = !hud_pinned;
                if (touch_seek) {
                    if (target < 0) target = 0;
                    if (target > dur - 1) target = dur - 1;
                    if (apply_player_seek(fmt, vidx, native_hls, vctx, actx, sctx,
                                          adev, target, timeline_origin, &wall_start,
                                          &audio_clock, &cur_pos, &last_ac,
                                          &last_ac_wall, sub_text, &sub_end) == 0) {
                        last_present_tick = 0;
                        if (hb) SDL_AtomicSet(&hb->force_progress, 1);
                    } else {
                        snprintf(notice, sizeof(notice), "Nao foi possivel buscar neste video");
                        notice_until = SDL_GetTicks() + 1800;
                    }
                }
            }
        }
        if (!running) break;
        int stick_x = joy ? SDL_JoystickGetAxis(joy, 0) : 0;
        int stick_abs = stick_x < 0 ? -stick_x : stick_x;
        Uint32 seek_now = SDL_GetTicks();
        if (stick_abs < SEEK_RELEASE_AXIS) {
            seek_axis_lock = 0;
            seek_arm_dir = 0;
            seek_arm_since = 0;
        }
        if (!sequential_stream && !track_menu && dur > 1 && !timeline_seek && !seek_axis_lock) {
            if (stick_abs >= SEEK_ENTER_AXIS) {
                int direction = stick_x > 0 ? 1 : -1;
                if (seek_arm_dir != direction) {
                    seek_arm_dir = direction;
                    seek_arm_since = seek_now;
                } else if (seek_now - seek_arm_since >= SEEK_HOLD_MS) {
                    timeline_seek = 1;
                    timeline_seek_was_paused = paused;
                    timeline_seek_from = cur_pos;
                    timeline_seek_target = cur_pos;
                    timeline_seek_tick = seek_now;
                    seek_arm_dir = 0; seek_arm_since = 0;
                    if (adev && !paused) SDL_PauseAudioDevice(adev, 1);
                } else if (seek_now - seek_arm_since >= 280) {
                    snprintf(notice, sizeof(notice), "Continue segurando para abrir a timeline");
                    notice_until = seek_now + 300;
                    hud_until = seek_now + 1200;
                }
            } else {
                seek_arm_dir = 0;
                seek_arm_since = 0;
            }
        }
        if (timeline_seek) {
            last_present_tick = 0;
            double elapsed = (seek_now - timeline_seek_tick) / 1000.0;
            if (elapsed > 0.08) elapsed = 0.08;
            timeline_seek_tick = seek_now;
            if (stick_abs >= SEEK_MOVE_AXIS) {
                double amount = (stick_abs - SEEK_MOVE_AXIS) / (32767.0 - SEEK_MOVE_AXIS);
                if (amount > 1.0) amount = 1.0;
                double speed = dur * (0.006 + amount * amount * 0.039);
                timeline_seek_target += (stick_x > 0 ? 1.0 : -1.0) * speed * elapsed;
                if (timeline_seek_target < 0) timeline_seek_target = 0;
                if (timeline_seek_target > dur - 1) timeline_seek_target = dur - 1;
            }
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
            if (have_video_frame) SDL_RenderCopy(ren, tex, NULL, &dst);
            draw_hud(ren, title, timeline_seek_target, dur, 1, vol, fmt, aidx,
                     acur, naud, nsub, scur, scur >= 0 ? sidxs[scur] : -1, 1, !sequential_stream);
            draw_timeline_seek(ren, fmt, timeline_seek_from, timeline_seek_target, dur, timeline_origin);
            SDL_RenderPresent(ren);
            SDL_Delay(16);
            continue;
        }
        if (track_menu) {
            last_present_tick = 0;
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
            if (have_video_frame) SDL_RenderCopy(ren, tex, NULL, &dst);
            if (scur >= 0 && sub_text[0] && cur_pos < sub_end) draw_sub(ren, sub_text);
            draw_track_menu(ren, fmt, track_menu,
                            track_menu == TRACK_MENU_AUDIO ? aidxs : sidxs,
                            track_menu == TRACK_MENU_AUDIO ? naud : nsub,
                            track_sel, track_menu == TRACK_MENU_AUDIO ? acur : scur + 1);
            SDL_RenderPresent(ren);
            SDL_Delay(30);
            continue;
        }
        if (paused) {   // continua desenhando (quadro congelado + HUD)
            last_present_tick = 0;
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
            if (have_video_frame) SDL_RenderCopy(ren, tex, NULL, &dst);
            if (scur >= 0 && sub_text[0] && cur_pos < sub_end) draw_sub(ren, sub_text);
            draw_hud(ren, title, cur_pos, dur, 1, vol, fmt, aidx,
                     acur, naud, nsub, scur, scur >= 0 ? sidxs[scur] : -1, hud_pinned, !sequential_stream);
            if (SDL_GetTicks() < notice_until) draw_notice(ren, notice);
            SDL_RenderPresent(ren);
            SDL_Delay(30);
            continue;
        }

        Uint32 read_started = SDL_GetTicks();
        Uint32 audio_before_ms = adev ? (Uint32)(SDL_GetQueuedAudioSize(adev) * 1000.0 / bps) : 0;
        int ret = av_read_frame(fmt, pkt);
        Uint32 read_ms = SDL_GetTicks() - read_started;
        if (open_watch.cancelled) {
            running = 0;
            av_packet_unref(pkt);
            break;
        }
        if (open_watch.timed_out) {
            player_error_message("Video nao iniciou em 30 segundos; tentando outra fonte");
            playback_error = -5;
            av_packet_unref(pkt);
            break;
        }
        if (ret >= 0 && logged_first_present)
            player_clock_account_read(&wall_start, read_ms / 1000.0,
                                      audio_before_ms / 1000.0, 1, 1);
        read_since_present_ms += read_ms;
        if (read_ms > worst_read_ms) worst_read_ms = read_ms;
        if (read_ms >= 250) {
            slow_reads++;
            // O tempo ja fica no resumo persistente. Evite abrir/fechar o trace
            // na microSD para cada espera curta na thread que desenha quadros.
            if (read_ms >= 1000)
                diag_player_event("demux", "read-wait", "ms=%u audio=%u rc=%d pos=%.1f",
                                  read_ms, audio_before_ms, ret, cur_pos);
        }
        if (!logged_first_read) {
            diag_player_event("demux", ret >= 0 ? "first-read-ok" : "first-read-fail",
                              "rc=%d stream=%d", ret, ret >= 0 ? pkt->stream_index : -1);
            logged_first_read = 1;
        }
        if (ret == AVERROR(EAGAIN)) {
            // Buffer vazio e/ou timeout de rede, thread de download ainda esta trabalhando.
            Uint32 now_ticks = SDL_GetTicks();
            if (!buffering_since) {
                buffering_since = now_ticks;
                buffering_audio_ms = adev ? (Uint32)(SDL_GetQueuedAudioSize(adev) * 1000.0 / bps) : 0;
                buffering_events++;
            }
            if (now_ticks - buffering_since >= 250) {
                SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
                if (have_video_frame) SDL_RenderCopy(ren, tex, NULL, &dst);
                char dots[48];
                int ndots = (int)((now_ticks / 350) % 3) + 1;
                Uint32 stalled = now_ticks - buffering_since;
                if (stalled < 8000) snprintf(dots, sizeof(dots), "Aguardando dados%.*s", ndots, "...");
                else if (stalled < 30000) snprintf(dots, sizeof(dots), "Tentando reconectar%.*s", ndots, "...");
                else snprintf(dots, sizeof(dots), "Conexao lenta  |  B para voltar");
                draw_center_state(ren, stalled < 8000 ? "CARREGANDO" : "RECUPERANDO", dots, stalled >= 30000);
                draw_hud(ren, title, cur_pos, dur, 0, vol, fmt, aidx,
                         acur, naud, nsub, scur, scur >= 0 ? sidxs[scur] : -1, hud_pinned, !sequential_stream);
                if (now_ticks < notice_until) draw_notice(ren, notice);
                SDL_RenderPresent(ren);
            }
            SDL_Delay(30);
            continue;
        }
        if (ret < 0) {  // fim real ou falha definitiva da fonte/rede
            if (open_watch.cancelled) { running = 0; break; }
            if (!adev || SDL_GetQueuedAudioSize(adev) < 8192) {
                if (ret == AVERROR_EOF) reached_end = 1;
                else {
                    playback_error = -5;
                    if (native_hls && !logged_first_present)
                        player_error_text("primeiro segmento HLS", ret);
                }
                diag_player_event("demux", "read-terminal", "rc=%d eof=%d", ret, reached_end);
                break;
            }
            SDL_Delay(40); continue;
        }
        if (buffering_since) {
            Uint32 waited = SDL_GetTicks() - buffering_since;
            if (waited > longest_buffer_ms) longest_buffer_ms = waited;
            // Depois de uma queda, o relogio de parede avancou sem quadros.
            // Desconte somente o tempo alem do audio que ainda estava na fila,
            // senao a retomada considera os quadros atrasados e os descarta.
            if (waited >= 750 && (!adev || SDL_GetQueuedAudioSize(adev) < 8192)) {
                Uint32 frozen = waited > buffering_audio_ms ? waited - buffering_audio_ms : 0;
                wall_start += frozen / 1000.0;
            }
            if (waited >= 500)
                diag_player_event("demux", "buffering-end", "ms=%u audioq=%u pos=%.1f",
                                  waited, adev ? SDL_GetQueuedAudioSize(adev) : 0, cur_pos);
            buffering_since = 0;
        }
        if (aidx >= 0 && pkt->stream_index == aidx && actx) {
            if (avcodec_send_packet(actx, pkt) == 0) {
                while (avcodec_receive_frame(actx, frame) == 0) {
                    // (re)configura o resample conforme os parametros REAIS do frame
                    // (HE-AAC/SBR pode mudar a taxa; fontes 44.1kHz precisam disto).
                    int fr = frame->sample_rate, ff = frame->format, fc = frame->ch_layout.nb_channels;
                    if (!swr || fr != swr_rate || ff != swr_fmt || fc != swr_ch) {
                        if (swr) swr_free(&swr);
                        AVChannelLayout outl; av_channel_layout_default(&outl, OCH);
                        AVChannelLayout inl; av_channel_layout_default(&inl, 2);
                        const AVChannelLayout *pin = (fc > 0) ? &frame->ch_layout : &inl;
                        int swr_ok = swr_alloc_set_opts2(&swr, &outl, AV_SAMPLE_FMT_S16, ORATE,
                                                        pin, ff, fr > 0 ? fr : ORATE, 0, NULL);
                        av_channel_layout_uninit(&outl);
                        av_channel_layout_uninit(&inl);
                        if (swr_ok < 0 || !swr || swr_init(swr) < 0) swr_free(&swr);
                        swr_rate = fr; swr_fmt = ff; swr_ch = fc;
                    }
                    if (!swr) continue;
                    int64_t ats = frame->best_effort_timestamp != AV_NOPTS_VALUE
                        ? frame->best_effort_timestamp : frame->pts;
                    int os = swr_get_out_samples(swr, frame->nb_samples);
                    int bytes = av_samples_get_buffer_size(NULL, OCH, os, AV_SAMPLE_FMT_S16, 0);
                    if (bytes > 0) av_fast_malloc(&audio_buf, &audio_buf_cap, (size_t)bytes);
                    if (audio_buf) {
                        int n = swr_convert(swr, &audio_buf, os, (const uint8_t **)frame->data, frame->nb_samples);
                        if (n > 0 && vol != 100) {   // aplica o volume nas amostras S16
                            int16_t *sm = (int16_t *)audio_buf; int cnt = n * OCH;
                            for (int i = 0; i < cnt; i++) { int v = sm[i] * vol / 100; sm[i] = v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v); }
                        }
                        double audio_pts = ats != AV_NOPTS_VALUE
                            ? ats * av_q2d(atb) - timeline_origin : -1;
                        int queue_during_resume = !resume_preroll ||
                            (adev && audio_pts >= resume_target - 0.15 &&
                             SDL_GetQueuedAudioSize(adev) < (unsigned)(bps * 0.35));
                        if (n > 0 && adev && queue_during_resume) {
                            SDL_QueueAudio(adev, audio_buf, n * OCH * 2);
                            if (audio_pts >= 0) audio_clock = audio_pts + n / (double)ORATE;
                            else audio_clock += n / (double)ORATE;
                            unsigned queued = SDL_GetQueuedAudioSize(adev);
                            if (queued > max_audio_queue) max_audio_queue = queued;
                        }
                    }
                }
            }
        } else if (pkt->stream_index == vidx) {
            if (!logged_first_video_packet) {
                diag_player_event("video", "first-packet", "size=%d pts=%lld",
                                  pkt->size, (long long)pkt->pts);
                logged_first_video_packet = 1;
            }
            if (avcodec_send_packet(vctx, pkt) == 0) {
                while (avcodec_receive_frame(vctx, frame) == 0) {
                    if (!logged_first_video_frame) {
                        diag_player_event("video", "first-frame", "fmt=%d size=%dx%d",
                                          frame->format, frame->width, frame->height);
                        logged_first_video_frame = 1;
                    }
                    decoded_video++;
                    int64_t vts = frame->best_effort_timestamp != AV_NOPTS_VALUE
                        ? frame->best_effort_timestamp : frame->pts;
                    double vpts = (vts != AV_NOPTS_VALUE) ? vts * av_q2d(vtb) - timeline_origin : cur_pos;
                    double now = av_gettime_relative() / 1000000.0;
                    int resume_first_frame = 0;
                    if (resume_preroll) {
                        // O seek HLS volta ao segmento/chave anterior. Decodifique
                        // esse trecho sem tocar audio nem usar o relogio de parede
                        // que envelheceu durante a transferencia de rede.
                        if (resume_preroll_frames == 0)
                            diag_player_event("seek", "preroll-first",
                                              "pts=%.2f target=%.2f", vpts, resume_target);
                        if (vpts < resume_target - 0.15) {
                            resume_preroll_frames++;
                            continue;
                        }
                        resume_preroll = 0;
                        resume_first_frame = 1;
                        wall_start = now - vpts;
                        cur_pos = vpts;
                        last_ac = audio_clock;
                        last_ac_wall = now;
                        diag_player_event("seek", "resume-frame",
                                          "wanted=%.2f actual=%.2f preroll=%d audioq=%u",
                                          resume_target, vpts, resume_preroll_frames,
                                          adev ? SDL_GetQueuedAudioSize(adev) : 0);
                        // The output remains paused until this frame is shown.
                    }
                    // Se o relogio de AUDIO parou de avancar (decode travando), o video
                    // NAO fica esperando: segue pelo relogio de parede (nao congela).
                    if (audio_clock != last_ac) { last_ac = audio_clock; last_ac_wall = now; }
                    unsigned audio_queued = adev ? SDL_GetQueuedAudioSize(adev) : 0;
                    int audio_ok = adev && audio_queued > 0 && (now - last_ac_wall < 0.7);
                    double master = player_clock_master(now, vpts, audio_clock,
                                        audio_queued / bps, audio_ok,
                                        &wall_start, !logged_first_present || resume_first_frame);
                    cur_pos = master;
                    double delay = vpts - master;
                    // Se ja perdeu o prazo por mais de 120 ms, converter e enviar
                    // este quadro para a GPU so aumenta o atraso. Descartar aqui
                    // permite recuperar sincronismo em fontes pesadas/instaveis.
                    if (delay < -0.12) { dropped_video++; continue; }
                    if (delay > 0.001) {
                        if (delay > 0.35) delay = 0.35;
                        Uint32 sync_started = SDL_GetTicks();
                        SDL_Delay((Uint32)(delay * 1000));
                        sync_since_present_ms += SDL_GetTicks() - sync_started;
                    }
                    AVFrame *u = frame;
                    if (frame->format == AV_PIX_FMT_NVTEGRA) {
                        av_frame_unref(transfer);
                        if (av_hwframe_transfer_data(transfer, frame, 0) < 0) {
                            dropped_video++;
                            continue;
                        }
                        hardware_decode = 1;
                        u = transfer;
                    }
                    int direct_nv12 = u->format == AV_PIX_FMT_NV12;
                    if (!direct_nv12 && u->format != AV_PIX_FMT_YUV420P) {
                        sws = sws_getCachedContext(sws, u->width, u->height, u->format,
                                                   vw, vh, AV_PIX_FMT_YUV420P,
                                                   SWS_BILINEAR, NULL, NULL, NULL);
                        if (!sws) { playback_error = -4; running = 0; break; }
                        if (!yuv) {
                            yuv = av_frame_alloc();
                            if (!yuv) { playback_error = -4; running = 0; break; }
                            yuv->format = AV_PIX_FMT_YUV420P; yuv->width = vw; yuv->height = vh;
                            if (av_frame_get_buffer(yuv, 32) < 0) { playback_error = -4; running = 0; break; }
                        }
                        if (av_frame_make_writable(yuv) < 0) { playback_error = -4; running = 0; break; }
                        sws_scale(sws, (const uint8_t * const *)u->data, u->linesize,
                                  0, u->height, yuv->data, yuv->linesize);
                        u = yuv;
                    }
                    Uint32 wanted_format = direct_nv12 ? SDL_PIXELFORMAT_NV12 : SDL_PIXELFORMAT_IYUV;
                    if (!tex || texture_format != wanted_format) {
                        SDL_Texture *next = SDL_CreateTexture(ren, wanted_format,
                                                              SDL_TEXTUREACCESS_STREAMING, vw, vh);
                        // Alguns renderers SDL anunciam NV12 no header mas nao o
                        // implementam. Nesse caso preserve a reproducao pelo
                        // conversor YUV420P em vez de transformar otimizacao em erro.
                        if (!next && direct_nv12) {
                            direct_nv12 = 0;
                            sws = sws_getCachedContext(sws, u->width, u->height, u->format,
                                                       vw, vh, AV_PIX_FMT_YUV420P,
                                                       SWS_BILINEAR, NULL, NULL, NULL);
                            if (!sws) { playback_error = -4; running = 0; break; }
                            if (!yuv) {
                                yuv = av_frame_alloc();
                                if (!yuv) { playback_error = -4; running = 0; break; }
                                yuv->format = AV_PIX_FMT_YUV420P; yuv->width = vw; yuv->height = vh;
                                if (av_frame_get_buffer(yuv, 32) < 0) { playback_error = -4; running = 0; break; }
                            }
                            if (av_frame_make_writable(yuv) < 0) { playback_error = -4; running = 0; break; }
                            sws_scale(sws, (const uint8_t * const *)u->data, u->linesize,
                                      0, u->height, yuv->data, yuv->linesize);
                            u = yuv;
                            wanted_format = SDL_PIXELFORMAT_IYUV;
                            next = SDL_CreateTexture(ren, wanted_format,
                                                     SDL_TEXTUREACCESS_STREAMING, vw, vh);
                        }
                        if (!next) { playback_error = -4; running = 0; break; }
                        if (tex) SDL_DestroyTexture(tex);
                        tex = next;
                        texture_format = wanted_format;
                        diag_player_event("render", "texture-ok", "format=%u size=%dx%d",
                                          wanted_format, vw, vh);
                    }
                    int upload_rc = direct_nv12
                        ? SDL_UpdateNVTexture(tex, NULL, u->data[0], u->linesize[0], u->data[1], u->linesize[1])
                        : SDL_UpdateYUVTexture(tex, NULL, u->data[0], u->linesize[0],
                                               u->data[1], u->linesize[1], u->data[2], u->linesize[2]);
                    if (upload_rc < 0) { playback_error = -4; running = 0; break; }
                    have_video_frame = 1;
                    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
                    SDL_RenderCopy(ren, tex, NULL, &dst);
                    if (scur >= 0 && sub_text[0] && cur_pos < sub_end) draw_sub(ren, sub_text);
                    if (hud_pinned || SDL_GetTicks() < hud_until)
                        draw_hud(ren, title, cur_pos, dur, 0, vol, fmt, aidx,
                                 acur, naud, nsub, scur, scur >= 0 ? sidxs[scur] : -1, hud_pinned, !sequential_stream);
                    if (SDL_GetTicks() < notice_until) draw_notice(ren, notice);
                    SDL_RenderPresent(ren);
                    Uint32 present_tick = SDL_GetTicks();
                    if (last_present_tick) {
                        Uint32 gap_ms = present_tick - last_present_tick;
                        if (gap_ms > worst_present_ms) worst_present_ms = gap_ms;
                        if (gap_ms >= 250) {
                            present_gaps++;
                            if (read_since_present_ms >= gap_ms * 2 / 5)
                                read_gaps++;
                            else if (sync_since_present_ms >= gap_ms * 2 / 5)
                                sync_gaps++;
                            else
                                other_gaps++;
                        }
                    }
                    last_present_tick = present_tick;
                    read_since_present_ms = sync_since_present_ms = 0;
                    if (!logged_first_present) {
                        open_watch.deadline_us = 0;
                        nplay_curl_avio_set_startup_window(0);
                        g_player_presented_frame = 1;
                        ui_popcorn_release();
                        first_present_ms = SDL_GetTicks() - play_started_tick;
                        if (hb) {
                            SDL_AtomicSet(&hb->current_pos, (int)cur_pos);
                            SDL_AtomicSet(&hb->duration, (int)dur);
                            SDL_AtomicSet(&hb->pipeline_ready, 1);
                        }
                        diag_player_event("render", "first-present", "position=%.2f ms=%u",
                                          cur_pos, first_present_ms);
                        logged_first_present = 1;
                        if (adev && !paused) SDL_PauseAudioDevice(adev, 0);
                    }
                }
            }
        } else if (scur >= 0 && sctx && pkt->stream_index == sidxs[scur]) {   // legenda
            AVSubtitle sub; int got = 0;
            if (avcodec_decode_subtitle2(sctx, &sub, &got, pkt) >= 0 && got) {
                sub_text[0] = 0;
                for (unsigned r = 0; r < sub.num_rects; r++) {
                    AVSubtitleRect *rc = sub.rects[r]; char tmp[400] = "";
                    if (rc->type == SUBTITLE_ASS && rc->ass) ass_to_text(rc->ass, tmp, sizeof(tmp));
                    else if (rc->type == SUBTITLE_TEXT && rc->text) snprintf(tmp, sizeof(tmp), "%s", rc->text);
                    if (tmp[0]) { size_t rem = sizeof(sub_text) - strlen(sub_text) - 1; if (sub_text[0] && rem > 1) { strncat(sub_text, " ", rem); rem--; } strncat(sub_text, tmp, rem); }
                }
                double base = (pkt->pts != AV_NOPTS_VALUE)
                    ? pkt->pts * av_q2d(fmt->streams[sidxs[scur]]->time_base) - timeline_origin : cur_pos;
                double sd = (sub.end_display_time > sub.start_display_time) ? (sub.end_display_time - sub.start_display_time) / 1000.0 : 4.0;
                sub_end = base + sd;
                avsubtitle_free(&sub);
            }
        }
        av_packet_unref(pkt);
    }

    // Um HLS que termina logo apos o seek sem mostrar quadro nao concluiu a
    // reproducao. Trate como falha para acionar a segunda abertura desde zero.
    if (native_hls && out_resume_seeked && *out_resume_seeked &&
        !logged_first_present && reached_end) {
        diag_player_event("seek", "resume-empty", "pos=%.1f", start_sec);
        player_error_message("Retomada nao entregou video");
        playback_error = -5;
        reached_end = 0;
    }
    if (out_pos) *out_pos = cur_pos;
    if (out_dur) *out_dur = dur;
    if (out_presented_frame) *out_presented_frame = logged_first_present;
    if (open_watch.cancelled) SDL_FlushEvent(SDL_JOYBUTTONDOWN);
    store_save_player_volume(vol);
    diag_player_event("player", "timing", "open=%u probe=%u first=%u gap=io%d/sync%d/other%d",
                      open_elapsed_ms, probe_elapsed_ms, first_present_ms,
                      read_gaps, sync_gaps, other_gaps);
    store_save_player_stats(vw, vh, decoded_video, dropped_video,
                            buffering_events, max_audio_queue, playback_error,
                            hardware_decode, slow_reads, worst_read_ms,
                            present_gaps, worst_present_ms,
                            read_gaps, sync_gaps, other_gaps,
                            open_elapsed_ms, probe_elapsed_ms, first_present_ms);
    diag_player_event("player", "cleanup-begin", "pos=%.1f frames=%d drop=%d waits=%d max=%ums gaps=%d hw=%d err=%d",
                      cur_pos, decoded_video, dropped_video, slow_reads,
                      worst_read_ms, present_gaps, hardware_decode, playback_error);

    if (adev) SDL_CloseAudioDevice(adev);
    if (sws) sws_freeContext(sws);
    if (yuv) av_frame_free(&yuv);
    if (transfer) av_frame_free(&transfer);
    if (swr) swr_free(&swr);
    av_freep(&audio_buf);
    if (actx) avcodec_free_context(&actx);
    if (sctx) avcodec_free_context(&sctx);
    avcodec_free_context(&vctx);
    av_frame_free(&frame); av_packet_free(&pkt);
    SDL_DestroyTexture(tex);
    avformat_close_input(&fmt);
    nplay_curl_avio_close(avio);
    g_player_presented_frame = 0;
    NplayCurlAvioQuality quality = {0};
    nplay_curl_avio_quality_get(&quality);
    diag_player_event("avio", "summary", "req=%d first>=250=%d max=%dms conn=%d fail=%d",
                      quality.requests, quality.slow_first_bytes,
                      quality.worst_first_ms, quality.new_connections,
                      quality.failures);
    diag_player_event("avio", "prefetch", "first=%d ready=%d young=%d oldempty=%d",
                      quality.first_reads, quality.ready_first_reads,
                      quality.young_first_reads, quality.old_empty_first_reads);
    diag_player_event("player", "cleanup-end", NULL);
    return playback_error ? playback_error : reached_end;
}



static int playback_heartbeat_thread(void *userdata) {
    PlaybackHeartbeat *hb = (PlaybackHeartbeat *)userdata;
    int elapsed_ms = 0, progress_ms = 0;
    while (SDL_AtomicGet(&hb->running)) {
        SDL_Delay(500);
        if (!SDL_AtomicGet(&hb->running)) break;
        if (!SDL_AtomicGet(&hb->pipeline_ready)) {
            elapsed_ms = 0;
            progress_ms = 0;
            continue;
        }
        elapsed_ms += 500;
        progress_ms += 500;

        if (elapsed_ms >= 20000) {
            int session_id = SDL_AtomicGet(&hb->session_id);
            if (session_id > 0 && hb->heartbeat_cb)
                hb->heartbeat_cb(session_id, hb->callback_userdata);
            elapsed_ms = 0;
        }

        if (progress_ms >= 15000 || SDL_AtomicCAS(&hb->force_progress, 1, 0)) {
            int pos = SDL_AtomicGet(&hb->current_pos);
            int dur = SDL_AtomicGet(&hb->duration);
            if (hb->progress_cb && pos > 5)
                hb->progress_cb(hb->item_id, pos, dur, hb->callback_userdata);
            progress_ms = 0;
        }
    }
    return 0;
}

int player_run(SDL_Renderer *ren, SDL_Joystick *joy, PlayerRequest *request, PlayerResult *result) {
    if (!request || !result) return -1;
    memset(result, 0, sizeof(PlayerResult));

    PlaybackHeartbeat hb = {0};
    hb.item_id = request->item_id;
    SDL_AtomicSet(&hb.session_id, request->session_id);
    SDL_AtomicSet(&hb.current_pos, (int)request->start_sec);
    SDL_AtomicSet(&hb.duration, 0);
    SDL_AtomicSet(&hb.force_progress, 0);
    SDL_AtomicSet(&hb.pipeline_ready, 0);
    hb.progress_cb = request->progress_cb;
    hb.heartbeat_cb = request->heartbeat_cb;
    hb.callback_userdata = request->userdata;
    
    SDL_Thread *heartbeat = NULL;
    SDL_AtomicSet(&hb.running, 1);
    heartbeat = SDL_CreateThread(playback_heartbeat_thread, "play-heartbeat", &hb);

    int retry_count = 0;
    double current_pos = request->start_sec;
    double attempt_start = current_pos;
    int resume_restart_attempted = 0;
    double dur = 0.0;
    int ever_presented_frame = 0;
    int final_rc = 0;
    PlaybackSource active = request->playback;
    if (active.item_id <= 0) active.item_id = request->item_id;
    if (active.session_id <= 0) active.session_id = request->session_id;
    if (active.source_id <= 0) active.source_id = request->source_id;
    if (active.delivery == DELIVERY_UNKNOWN) active.delivery = request->delivery;
    if (!active.section[0] && request->section) snprintf(active.section, sizeof(active.section), "%s", request->section);
    if (!active.container[0] && request->container) snprintf(active.container, sizeof(active.container), "%s", request->container);
    if (!active.play_url[0] && request->url) snprintf(active.play_url, sizeof(active.play_url), "%s", request->url);

    const char *delivery = active.delivery_str[0] ? active.delivery_str :
                           active.delivery == DELIVERY_R2 ? "r2" :
                           active.delivery == DELIVERY_UPSTREAM ? "upstream" : "unknown";
    diag_player_begin(active.item_id, active.session_id, active.source_id,
                      active.container, delivery);
    diag_player_event("player", "run-begin", "start=%.1f", current_pos);

    while (1) {
        SDL_AtomicSet(&hb.pipeline_ready, 0);
        double out_pos = 0, out_dur = 0;
        int resume_seeked = 0, presented_frame = 0;
        PlayerRequest attempt = *request;
        attempt.playback = active;
        attempt.session_id = active.session_id;
        attempt.source_id = active.source_id;
        attempt.delivery = active.delivery;
        attempt.section = active.section;
        attempt.container = active.container;
        attempt.url = active.play_url;
        diag_player_event("player", "attempt-begin", "attempt=%d pos=%.1f session=%d source=%d",
                          retry_count + 1, attempt_start, active.session_id, active.source_id);
        int rc = player_play_internal(ren, joy, &attempt, &hb, attempt_start,
                                      &out_pos, &out_dur, &resume_seeked, &presented_frame);
        SDL_AtomicSet(&hb.pipeline_ready, 0);
        nplay_curl_avio_set_abort_check(NULL, NULL);
        nplay_curl_avio_set_startup_window(0);
        diag_player_event("player", "attempt-end", "attempt=%d rc=%d pos=%.1f dur=%.1f",
                          retry_count + 1, rc, out_pos, out_dur);
        if (presented_frame) ever_presented_frame = 1;
        // So grave uma nova posicao depois de realmente mostrar video. Uma
        // tentativa de retomada pode atualizar cur_pos sem decodificar nada.
        if (out_pos > 0 && (presented_frame || rc == 1)) current_pos = out_pos;
        if (out_dur > 0) dur = out_dur;

        if (rc < 0 && rc != -11 && resume_seeked && !presented_frame &&
            !resume_restart_attempted && attempt_start > 3 &&
            active.container[0] && !strcmp(active.container, "m3u8")) {
            resume_restart_attempted = 1;
            attempt_start = 0;
            diag_player_event("recover", "resume-from-start",
                              "rc=%d saved=%.1f", rc, current_pos);
            SDL_SetRenderDrawColor(ren, 15, 15, 15, 255);
            SDL_RenderClear(ren);
            draw_center_state(ren, "RETOMADA INDISPONIVEL",
                              "Abrindo o video desde o inicio...", 0);
            SDL_RenderPresent(ren);
            continue;
        }

        if (rc == 1) { // Terminou naturalmente somente se houve video
            if (!ever_presented_frame) {
                player_error_message("Fonte terminou antes do primeiro quadro");
                result->reason = EXIT_REASON_ERROR;
                result->final_state = PLAYER_ERROR;
                final_rc = -5;
            } else {
                result->reason = EXIT_REASON_NATURAL;
                result->final_state = PLAYER_FINISHED;
                final_rc = rc;
            }
            break;
        } else if (rc == 0 || rc == -11) { // Usuario saiu ou cancelou a abertura
            if (rc == -11) {
                // O callback inspeciona B/- sem remover eventos enquanto FFmpeg
                // bloqueia. Nao entregue esse toque a tela anterior ao voltar.
                SDL_FlushEvent(SDL_JOYBUTTONDOWN);
            }
            result->reason = EXIT_REASON_USER;
            result->final_state = PLAYER_FINISHED;
            final_rc = rc;
            break;
        } else { // Erro
            // rc < 0
            int recoverable = rc == -2 || rc == -5 || rc == -10;
            int startup_failure = current_pos <= request->start_sec + 1.0;
            // Na abertura, tentativa 0 renova a mesma sessao e tentativa 1 usa
            // fallback_cb para trocar a fonte. Limite 1 encerrava antes do
            // fallback e deixava qualquer ponteiro R2 404 sem alternativa.
            int retry_limit = startup_failure ? 2 : 3;
            if (!recoverable || retry_count >= retry_limit || !request->renew_cb) {
                result->reason = EXIT_REASON_ERROR;
                result->final_state = PLAYER_ERROR;
                final_rc = rc;
                break;
            }

            PlaybackSource renewed = {0};
            int renewed_ok = 0;
            int recovery_cancelled = 0;
            int use_fallback = retry_count == 1 && request->fallback_cb;
            int max_renew_tries = use_fallback ? 1 : 4;
            PlayerRenewCallback recovery_cb = use_fallback ? request->fallback_cb : request->renew_cb;
            diag_player_event("recover", use_fallback ? "fallback-begin" : "renew-begin",
                              "retry=%d max=%d", retry_count, max_renew_tries);
            for (int renew_try = 0; renew_try < max_renew_tries && !renewed_ok; renew_try++) {
                SDL_SetRenderDrawColor(ren, 15, 15, 15, 255);
                SDL_RenderClear(ren);
                char detail[96];
                if (use_fallback) snprintf(detail, sizeof(detail), "A fonte atual nao respondeu. Buscando alternativa...");
                else snprintf(detail, sizeof(detail), "Reconectando com seguranca...  %d/%d", renew_try + 1, max_renew_tries);
                draw_center_state(ren, use_fallback ? "TENTANDO OUTRA FONTE" : "RECUPERANDO SESSAO", detail, 1);
                SDL_RenderPresent(ren);

                if (recovery_cb(&active, &renewed, request->userdata) == 0 && renewed.play_url[0]) {
                    diag_player_event("recover", "resolve-ok", "try=%d session=%d source=%d",
                                      renew_try + 1, renewed.session_id, renewed.source_id);
                    renewed_ok = 1;
                    break;
                }
                if (use_fallback) break;
                Uint32 wait_start = SDL_GetTicks();
                int cancelled = 0;
                while (SDL_GetTicks() - wait_start < (Uint32)(1000 + renew_try * 1000)) {
                    SDL_Event event;
                    while (SDL_PollEvent(&event)) {
                        if (event.type == SDL_QUIT ||
                            (event.type == SDL_JOYBUTTONDOWN &&
                             (event.jbutton.button == JOY_B || event.jbutton.button == JOY_MINUS))) {
                            cancelled = 1;
                            break;
                        }
                    }
                    if (cancelled) break;
                    SDL_Delay(50);
                }
                if (cancelled) { recovery_cancelled = 1; break; }
            }
            if (recovery_cancelled) {
                result->reason = EXIT_REASON_USER;
                result->final_state = PLAYER_FINISHED;
                final_rc = 0;
                break;
            }
            if (!renewed_ok) {
                diag_player_event("recover", "resolve-fail", "rc=%d", rc);
                result->reason = EXIT_REASON_ERROR;
                result->final_state = PLAYER_ERROR;
                final_rc = rc;
                break;
            }
            active = renewed;
            SDL_AtomicSet(&hb.session_id, active.session_id);
            attempt_start = presented_frame ? current_pos :
                            (resume_restart_attempted ? 0 : current_pos);
            retry_count++;
            result->recovery_count = retry_count;
            continue;
        }
    }

    result->position = current_pos;
    result->duration = dur;
    result->presented_frame = ever_presented_frame;

    if (heartbeat) {
        SDL_AtomicSet(&hb.running, 0);
        SDL_WaitThread(heartbeat, NULL);
    }
    
    // Save progress once at the end
    if (request->progress_cb && ever_presented_frame) {
        request->progress_cb(request->item_id, (int)current_pos, (int)dur, request->userdata);
    }

    if (final_rc < 0)
        diag_player_event("player", "final-error", "rc=%d %s", final_rc,
                          g_player_last_error[0] ? g_player_last_error : "falha sem detalhe");
    diag_player_finish(final_rc);
    ui_popcorn_release();

    return final_rc;
}
