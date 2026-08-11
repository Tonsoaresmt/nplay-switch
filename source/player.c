// player.c - player de video: ffmpeg decodifica, SDL desenha (textura YUV) e toca
// o audio (SDL Audio + swresample). Sincroniza o video pelo relogio do audio.
// I/O via libcurl (curl_avio) porque o switch-ffmpeg nao tem TLS.
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
#include <libavutil/time.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include "player.h"
#include "curl_avio.h"
#include "text.h"
#include "store.h"

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

static const SDL_Color PC_TEXT = { 234, 240, 250, 255 };
static const SDL_Color PC_MUT  = { 170, 178, 196, 255 };
static const SDL_Color PC_ACC  = { 139, 92, 246, 255 };
static const SDL_Color PC_ACC2 = { 59, 130, 246, 255 };
static const SDL_Color PC_DARK = { 8, 10, 15, 255 };
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

static void draw_control(SDL_Renderer *ren, int x, int y, int w,
                         const char *key, const char *label, int active) {
    SDL_Color key_color = active ? PC_ACC : PC_ACC2;
    int kw = 0, kh = 0;
    SDL_Texture *kt = text_cached(ren, key, PC_TEXT, 0, &kw, &kh);
    int key_w = kw + 12;
    if (key_w < 38) key_w = 38;
    pfill(ren, x, y + 3, key_w, 30, key_color, 245);
    if (kt) {
        SDL_Rect d = { x + (key_w - kw) / 2, y + 5, kw, kh };
        SDL_RenderCopy(ren, kt, NULL, &d);
    }
    int label_x = x + key_w + 10;
    draw_clipped_text(ren, label, label_x, y + 6, x + w - label_x, active ? PC_TEXT : PC_MUT, 0);
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

static void draw_setting_card(SDL_Renderer *ren, int x, int y, int w,
                              const char *key, const char *label,
                              const char *value, int active) {
    SDL_Color accent = active ? PC_ACC : PC_ACC2;
    pfill(ren, x, y, w, 76, PC_CARD, 240);
    pfill(ren, x, y, 4, 76, accent, 255);

    int kw = 0, kh = 0;
    SDL_Texture *kt = text_cached(ren, key, PC_TEXT, 0, &kw, &kh);
    int key_w = kw + 18;
    if (key_w < 48) key_w = 48;
    pfill(ren, x + 14, y + 22, key_w, 34, accent, 245);
    if (kt) {
        SDL_Rect d = { x + 14 + (key_w - kw) / 2, y + 27, kw, kh };
        SDL_RenderCopy(ren, kt, NULL, &d);
    }

    int tx = x + 28 + key_w;
    draw_clipped_text(ren, label, tx, y + 8, x + w - tx - 12, PC_MUT, 0);
    draw_clipped_text(ren, value, tx, y + 38, x + w - tx - 12, PC_TEXT, 0);
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
    pfill(ren, x - 22, 108, w + 44, h + 18, bg, 235);
    pfill(ren, x - 22, 108, 4, h + 18, PC_ACC, 255);
    SDL_Rect src = { 0, 0, w, h };
    SDL_Rect dst = { x, 116, w, h };
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

// HUD organizado em tres zonas: identidade, progresso/status e ferramentas.
static void draw_hud(SDL_Renderer *ren, const char *title, double pos, double dur,
                     int paused, int vol, AVFormatContext *fmt, int aidx,
                     int acur, int naud, int nsub, int scur, int sidx,
                     int hud_pinned) {
    const int expanded = paused || hud_pinned;
    pfill(ren, 0, 0, PWIN_W, 86, PC_DARK, 205);
    pfill(ren, 0, 0, 6, 86, PC_ACC, 255);
    text_draw(ren, "NPLAY PLAYER", 46, 10, PC_ACC, 0);
    draw_clipped_text(ren, (title && title[0]) ? title : "Reproducao", 46, 39, 970, PC_TEXT, 1);
    text_draw(ren, paused ? "PAUSADO" : "REPRODUZINDO", 1060, 30,
              paused ? PC_ACC : PC_ACC2, 0);

    const int panel_y = expanded ? 438 : 584;
    pfill(ren, 0, panel_y, PWIN_W, PWIN_H - panel_y, PC_DARK, 222);
    int bx = 48, by = panel_y + 27, bw = PWIN_W - 96, bh = 6;
    pfill(ren, bx, by, bw, bh, PC_CARD, 255);
    int fw = 0;
    if (dur > 0) fw = (int)(bw * (pos / dur));
    if (fw < 0) fw = 0;
    if (fw > bw) fw = bw;
    if (fw > 0) pfill(ren, bx, by, fw, bh, PC_ACC, 255);
    pfill(ren, bx + fw - 3, by - 4, 6, bh + 8, PC_TEXT, 255);

    char now[16], total[16];
    fmt_time(pos, now, sizeof(now));
    fmt_time(dur, total, sizeof(total));
    text_draw(ren, now, bx, by + 12, PC_TEXT, 0);
    int tw = 0, th = 0;
    SDL_Texture *tt = text_cached(ren, dur > 0 ? total : "--:--", PC_MUT, 0, &tw, &th);
    if (tt) { SDL_Rect d = { PWIN_W - 48 - tw, by + 12, tw, th }; SDL_RenderCopy(ren, tt, NULL, &d); }

    if (expanded) {
        int y = panel_y + 86;
        draw_control(ren, 48,   y, 190, "A", paused ? "Continuar" : "Pausar", paused);
        draw_control(ren, 254,  y, 210, "L/R", "- / + 10s", 0);
        draw_control(ren, 480,  y, 232, "ZL/ZR", "- / + 60s", 0);
        draw_control(ren, 728,  y, 270, "LS", "Buscar na timeline", 0);
        draw_control(ren, 1014, y, 218, "B", "Voltar", 0);

        char volume_value[24], audio_value[48], subtitle_value[48];
        char audio_label[48], subtitle_label[48];
        snprintf(volume_value, sizeof(volume_value), "%d%%", vol);
        if (naud <= 0) snprintf(audio_value, sizeof(audio_value), "Indisponivel");
        else format_language(stream_lang(fmt, aidx), audio_value, sizeof(audio_value));
        if (nsub <= 0) snprintf(subtitle_value, sizeof(subtitle_value), "Indisponivel");
        else if (sidx < 0) snprintf(subtitle_value, sizeof(subtitle_value), "Desligada");
        else format_language(stream_lang(fmt, sidx), subtitle_value, sizeof(subtitle_value));
        snprintf(audio_label, sizeof(audio_label), "AUDIO  %d/%d", naud ? acur + 1 : 0, naud);
        snprintf(subtitle_label, sizeof(subtitle_label), "LEGENDAS  %d/%d", scur >= 0 ? scur + 1 : 0, nsub);

        int cy = panel_y + 164;
        draw_setting_card(ren, 48,  cy, 284, "UP/DN", "VOLUME", volume_value, 0);
        draw_setting_card(ren, 348, cy, 284, "Y", audio_label, audio_value, 0);
        draw_setting_card(ren, 648, cy, 284, "X", subtitle_label, subtitle_value, sidx >= 0);
        draw_setting_card(ren, 948, cy, 284, "+", "MODO DO PAINEL",
                          hud_pinned ? "Fixo" : "Automatico", hud_pinned);
    } else {
        int y = panel_y + 91;
        draw_control(ren, 48,   y, 190, "A", "Pausar", 0);
        draw_control(ren, 258,  y, 190, "L/R", "10s", 0);
        draw_control(ren, 468,  y, 220, "ZL/ZR", "60s", 0);
        draw_control(ren, 708,  y, 280, "LS/+", "Buscar / Opcoes", 0);
        draw_control(ren, 1008, y, 224, "B", "Voltar", 0);
    }
}

static void draw_center_state(SDL_Renderer *ren, const char *state, const char *detail, int accent) {
    const int w = 410, h = 108, x = (PWIN_W - w) / 2, y = (PWIN_H - h) / 2 - 15;
    pfill(ren, x, y, w, h, PC_DARK, 225);
    pfill(ren, x, y, 6, h, accent ? PC_ACC : PC_ACC2, 255);
    int sw = 0, sh = 0;
    SDL_Texture *st = text_cached(ren, state, PC_TEXT, 1, &sw, &sh);
    if (st) { SDL_Rect d = { x + (w - sw) / 2, y + 18, sw, sh }; SDL_RenderCopy(ren, st, NULL, &d); }
    int dw = 0, dh = 0;
    SDL_Texture *dt = text_cached(ren, detail, PC_MUT, 0, &dw, &dh);
    if (dt) { SDL_Rect d = { x + (w - dw) / 2, y + 65, dw, dh }; SDL_RenderCopy(ren, dt, NULL, &d); }
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
    text_draw(ren, chapter_index >= 0 ? "Cima/baixo Capitulos" : "Analogico Ajustar",
              x + 34, y + 140, PC_MUT, 0);
    text_draw(ren, "A Ir para este ponto", x + 34, y + 176, PC_TEXT, 0);
    text_draw(ren, "B Cancelar", x + 382, y + 176, PC_MUT, 0);
}

static int apply_player_seek(AVFormatContext *fmt, AVCodecContext *vctx,
                             AVCodecContext *actx, AVCodecContext *sctx,
                             SDL_AudioDeviceID adev, double target,
                             double timeline_origin, double *wall_start,
                             double *audio_clock, double *cur_pos,
                             double *last_ac, double *last_ac_wall,
                             char *sub_text, double *sub_end) {
    int flags = target < *cur_pos ? AVSEEK_FLAG_BACKWARD : 0;
    if (av_seek_frame(fmt, -1, (int64_t)((target + timeline_origin) * AV_TIME_BASE), flags) < 0)
        return -1;
    avcodec_flush_buffers(vctx);
    if (actx) avcodec_flush_buffers(actx);
    if (sctx) avcodec_flush_buffers(sctx);
    if (adev) SDL_ClearQueuedAudio(adev);
    sub_text[0] = 0;
    *sub_end = 0;
    double now = av_gettime() / 1000000.0;
    *wall_start = now - target;
    *audio_clock = target;
    *cur_pos = target;
    *last_ac = -1;
    *last_ac_wall = now;
    return 0;
}
// (re)abre SÓ o decoder de audio p/ o stream aidx (fecha o anterior). O resample
// (swr) é montado no loop a partir dos parametros REAIS do frame — importante pra
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
int player_play(SDL_Renderer *ren, SDL_Joystick *joy, const char *url,
                const char *title, double start_sec, double *out_pos, double *out_dur) {
    (void)joy;
    if (out_pos) *out_pos = 0;
    if (out_dur) *out_dur = 0;
    // Tela de preparacao enquanto abre a conexao e le os metadados.
    SDL_SetRenderDrawColor(ren, PC_DARK.r, PC_DARK.g, PC_DARK.b, 255); SDL_RenderClear(ren);
    draw_center_state(ren, "PREPARANDO VIDEO", "Conectando e lendo o arquivo...", 0);
    SDL_RenderPresent(ren);

    // HTTPS usa o AVIO do libcurl; arquivos sdmc:/ usam o protocolo local do
    // FFmpeg e podem ser assistidos offline sem reservar o ring de rede.
    int remote = !strncmp(url, "http://", 7) || !strncmp(url, "https://", 8);
    AVIOContext *avio = remote ? nplay_curl_avio_open(url) : NULL;
    if (remote && !avio) return -1;
    AVFormatContext *fmt = avformat_alloc_context();
    if (!fmt) { nplay_curl_avio_close(avio); return -1; }
    if (avio) { fmt->pb = avio; fmt->flags |= AVFMT_FLAG_CUSTOM_IO; }

    int rc = avformat_open_input(&fmt, remote ? NULL : url, NULL, NULL);
    if (rc != 0) { nplay_curl_avio_close(avio); return -1; }
    if (avformat_find_stream_info(fmt, NULL) < 0) { avformat_close_input(&fmt); nplay_curl_avio_close(avio); return -2; }

    int vidx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (vidx < 0) { avformat_close_input(&fmt); nplay_curl_avio_close(avio); return -3; }

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
    AVCodecContext *sctx = NULL;
    char sub_text[512] = ""; double sub_end = 0;
    if (scur >= 0 && open_sub_dec(fmt, sidxs[scur], &sctx) != 0) scur = -1;

    double dur = (fmt->duration > 0) ? fmt->duration / (double)AV_TIME_BASE : 0;
    double timeline_origin = (fmt->start_time != AV_NOPTS_VALUE)
        ? fmt->start_time / (double)AV_TIME_BASE : 0;
    if (out_dur) *out_dur = dur;

    AVCodecContext *vctx = NULL, *actx = NULL;
    struct SwrContext *swr = NULL;
    SDL_AudioDeviceID adev = 0;
    SDL_Texture *tex = NULL;
    struct SwsContext *sws = NULL;
    AVFrame *yuv = NULL, *frame = NULL;
    AVPacket *pkt = NULL;
#define PLAYER_SETUP_FAIL(code) do { \
    if (adev) SDL_CloseAudioDevice(adev); \
    if (sws) sws_freeContext(sws); \
    if (yuv) av_frame_free(&yuv); \
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
    const AVCodec *vdec = avcodec_find_decoder(vpar->codec_id);
    if (!vdec) PLAYER_SETUP_FAIL(-4);
    vctx = avcodec_alloc_context3(vdec);
    if (!vctx || avcodec_parameters_to_context(vctx, vpar) < 0) PLAYER_SETUP_FAIL(-4);
    vctx->thread_count = 4;
    if (avcodec_open2(vctx, vdec, NULL) < 0) PLAYER_SETUP_FAIL(-4);

    // ---- decoder de audio + resample + saida SDL ----
    const int OCH = 2, ORATE = 48000;
    if (aidx >= 0 && open_audio_dec(fmt, aidx, &actx, &swr, OCH, ORATE) == 0) {
        SDL_AudioSpec want; SDL_zero(want);
        want.freq = ORATE; want.format = AUDIO_S16SYS; want.channels = OCH; want.samples = 2048;
        adev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
        if (adev) SDL_PauseAudioDevice(adev, 0);
        else { if (swr) swr_free(&swr); avcodec_free_context(&actx); }
    }

    int vw = vctx->width, vh = vctx->height;
    if (vw <= 0 || vh <= 0) PLAYER_SETUP_FAIL(-4);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, vw, vh);
    if (!tex) PLAYER_SETUP_FAIL(-4);
    if (vctx->pix_fmt != AV_PIX_FMT_YUV420P) {
        sws = sws_getContext(vw, vh, vctx->pix_fmt, vw, vh, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);
        yuv = av_frame_alloc();
        if (!sws || !yuv) PLAYER_SETUP_FAIL(-4);
        yuv->format = AV_PIX_FMT_YUV420P; yuv->width = vw; yuv->height = vh;
        if (av_frame_get_buffer(yuv, 32) < 0) PLAYER_SETUP_FAIL(-4);
    }

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
    double audio_clock = 0, wall_start = av_gettime() / 1000000.0;
    double last_ac = -1, last_ac_wall = av_gettime() / 1000000.0;  // detecta audio travado
    double cur_pos = 0;
    int running = 1, paused = 0, vol = 100, reached_end = 0, playback_error = 0;
    int decoded_video = 0, dropped_video = 0, buffering_events = 0;
    unsigned max_audio_queue = 0;
    store_load_player_volume(&vol);
    int swr_rate = 0, swr_fmt = -1, swr_ch = 0;   // config atual do resample (do frame real)
    uint8_t *audio_buf = NULL;
    unsigned int audio_buf_cap = 0;                // reutilizado entre frames (evita churn no heap)
    Uint32 hud_until = SDL_GetTicks() + 4000;   // HUD visivel ao iniciar
    Uint32 buffering_since = 0;
    Uint32 notice_until = 0;
    char notice[96] = "";
    int hud_pinned = 0, have_video_frame = 0;
    int track_menu = 0, track_sel = 0;
    int timeline_seek = 0, timeline_seek_was_paused = 0, seek_axis_lock = 0;
    double timeline_seek_from = 0, timeline_seek_target = 0;
    Uint32 timeline_seek_tick = SDL_GetTicks();
    SDL_Event e;

    // Retoma de onde parou (só se fizer sentido: > 3s e não no finzinho).
    if (start_sec > 3 && (dur <= 0 || start_sec < dur - 5)) {
        av_seek_frame(fmt, -1, (int64_t)((start_sec + timeline_origin) * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);
        audio_clock = start_sec; cur_pos = start_sec;
        wall_start = av_gettime() / 1000000.0 - start_sec;
    }

    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_JOYBUTTONDOWN) {
                int b = e.jbutton.button;
                hud_until = SDL_GetTicks() + 4000;
                if (timeline_seek) {
                    if (b == JOY_A) {
                        if (apply_player_seek(fmt, vctx, actx, sctx, adev,
                                              timeline_seek_target, timeline_origin,
                                              &wall_start, &audio_clock, &cur_pos,
                                              &last_ac, &last_ac_wall, sub_text, &sub_end) == 0) {
                            snprintf(notice, sizeof(notice), "Reproducao em %.0f%%",
                                     dur > 0 ? timeline_seek_target * 100.0 / dur : 0.0);
                        } else snprintf(notice, sizeof(notice), "Nao foi possivel buscar neste video");
                        notice_until = SDL_GetTicks() + 2000;
                        timeline_seek = 0; seek_axis_lock = 1;
                        paused = timeline_seek_was_paused;
                        if (adev && !paused) SDL_PauseAudioDevice(adev, 0);
                    } else if (b == JOY_B || b == JOY_MINUS) {
                        timeline_seek = 0; seek_axis_lock = 1;
                        paused = timeline_seek_was_paused;
                        double resume_now = av_gettime() / 1000000.0;
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
                        double resume_now = av_gettime() / 1000000.0;
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
                                    atb = fmt->streams[aidx]->time_base;
                                    if (adev) SDL_ClearQueuedAudio(adev);
                                    audio_clock = cur_pos; last_ac = -1;
                                    last_ac_wall = av_gettime() / 1000000.0;
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
                        double resume_now = av_gettime() / 1000000.0;
                        wall_start = resume_now - cur_pos;
                        audio_clock = cur_pos; last_ac = -1; last_ac_wall = resume_now;
                        if (adev && !paused) SDL_PauseAudioDevice(adev, 0);
                    }
                    continue;
                }
                if (b == JOY_B || b == JOY_MINUS) running = 0;
                else if (b == JOY_PLUS) hud_pinned = !hud_pinned;
                else if (b == JOY_A) { paused = !paused; if (adev) SDL_PauseAudioDevice(adev, paused); }
                else if (b == JOY_UP || b == JOY_DOWN) {
                    vol += (b == JOY_UP) ? 10 : -10;
                    if (vol > 100) vol = 100;
                    if (vol < 0) vol = 0;
                    snprintf(notice, sizeof(notice), "Volume  %d%%", vol);
                    notice_until = SDL_GetTicks() + 1800;
                }
                else if (b == JOY_R || b == JOY_L || b == JOY_ZR || b == JOY_ZL) {
                    double step = (b == JOY_ZR || b == JOY_ZL) ? 60 : 10;
                    int forward = (b == JOY_R || b == JOY_ZR);
                    double t = cur_pos + (forward ? step : -step);
                    if (t < 0) t = 0;
                    if (dur > 0 && t > dur - 1) t = dur - 1;
                    if (apply_player_seek(fmt, vctx, actx, sctx, adev, t,
                                          timeline_origin, &wall_start, &audio_clock,
                                          &cur_pos, &last_ac, &last_ac_wall,
                                          sub_text, &sub_end) == 0) {
                        snprintf(notice, sizeof(notice), "%s %.0f segundos", forward ? "Avancou" : "Voltou", step);
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
            }
        }
        if (!running) break;
        int stick_x = joy ? SDL_JoystickGetAxis(joy, 0) : 0;
        int stick_abs = stick_x < 0 ? -stick_x : stick_x;
        Uint32 seek_now = SDL_GetTicks();
        if (stick_abs < 8000) seek_axis_lock = 0;
        if (!track_menu && dur > 1 && !timeline_seek && !seek_axis_lock && stick_abs >= 18000) {
            timeline_seek = 1;
            timeline_seek_was_paused = paused;
            timeline_seek_from = cur_pos;
            timeline_seek_target = cur_pos;
            timeline_seek_tick = seek_now;
            if (adev && !paused) SDL_PauseAudioDevice(adev, 1);
        }
        if (timeline_seek) {
            double elapsed = (seek_now - timeline_seek_tick) / 1000.0;
            if (elapsed > 0.08) elapsed = 0.08;
            timeline_seek_tick = seek_now;
            if (stick_abs >= 8000) {
                double amount = (stick_abs - 8000) / 24767.0;
                if (amount > 1.0) amount = 1.0;
                double speed = dur * (0.012 + amount * amount * 0.068);
                timeline_seek_target += (stick_x > 0 ? 1.0 : -1.0) * speed * elapsed;
                if (timeline_seek_target < 0) timeline_seek_target = 0;
                if (timeline_seek_target > dur - 1) timeline_seek_target = dur - 1;
            }
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
            if (have_video_frame) SDL_RenderCopy(ren, tex, NULL, &dst);
            draw_hud(ren, title, timeline_seek_target, dur, 1, vol, fmt, aidx,
                     acur, naud, nsub, scur, scur >= 0 ? sidxs[scur] : -1, 1);
            draw_timeline_seek(ren, fmt, timeline_seek_from, timeline_seek_target, dur, timeline_origin);
            SDL_RenderPresent(ren);
            SDL_Delay(16);
            continue;
        }
        if (track_menu) {
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
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
            if (have_video_frame) SDL_RenderCopy(ren, tex, NULL, &dst);
            if (scur >= 0 && sub_text[0] && cur_pos < sub_end) draw_sub(ren, sub_text);
            draw_hud(ren, title, cur_pos, dur, 1, vol, fmt, aidx,
                     acur, naud, nsub, scur, scur >= 0 ? sidxs[scur] : -1, hud_pinned);
            if (SDL_GetTicks() < notice_until) draw_notice(ren, notice);
            SDL_RenderPresent(ren);
            SDL_Delay(30);
            continue;
        }

        int ret = av_read_frame(fmt, pkt);
        if (ret == AVERROR(EAGAIN)) {
            // Buffer vazio e/ou timeout de rede, thread de download ainda esta trabalhando.
            Uint32 now_ticks = SDL_GetTicks();
            if (!buffering_since) { buffering_since = now_ticks; buffering_events++; }
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
                         acur, naud, nsub, scur, scur >= 0 ? sidxs[scur] : -1, hud_pinned);
                if (now_ticks < notice_until) draw_notice(ren, notice);
                SDL_RenderPresent(ren);
            }
            SDL_Delay(30);
            continue;
        }
        buffering_since = 0;
        if (ret < 0) {  // fim real ou falha definitiva da fonte/rede
            if (!adev || SDL_GetQueuedAudioSize(adev) < 8192) {
                if (ret == AVERROR_EOF) reached_end = 1;
                else playback_error = -5;
                break;
            }
            SDL_Delay(40); continue;
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
                    if (ats != AV_NOPTS_VALUE) audio_clock = ats * av_q2d(atb) - timeline_origin;
                    int os = swr_get_out_samples(swr, frame->nb_samples);
                    int bytes = av_samples_get_buffer_size(NULL, OCH, os, AV_SAMPLE_FMT_S16, 0);
                    if (bytes > 0) av_fast_malloc(&audio_buf, &audio_buf_cap, (size_t)bytes);
                    if (audio_buf) {
                        int n = swr_convert(swr, &audio_buf, os, (const uint8_t **)frame->data, frame->nb_samples);
                        if (n > 0 && vol != 100) {   // aplica o volume nas amostras S16
                            int16_t *sm = (int16_t *)audio_buf; int cnt = n * OCH;
                            for (int i = 0; i < cnt; i++) { int v = sm[i] * vol / 100; sm[i] = v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v); }
                        }
                        if (n > 0 && adev) {
                            SDL_QueueAudio(adev, audio_buf, n * OCH * 2);
                            unsigned queued = SDL_GetQueuedAudioSize(adev);
                            if (queued > max_audio_queue) max_audio_queue = queued;
                        }
                    }
                }
            }
        } else if (pkt->stream_index == vidx) {
            if (avcodec_send_packet(vctx, pkt) == 0) {
                while (avcodec_receive_frame(vctx, frame) == 0) {
                    decoded_video++;
                    int64_t vts = frame->best_effort_timestamp != AV_NOPTS_VALUE
                        ? frame->best_effort_timestamp : frame->pts;
                    double vpts = (vts != AV_NOPTS_VALUE) ? vts * av_q2d(vtb) - timeline_origin : cur_pos;
                    double now = av_gettime() / 1000000.0;
                    // Se o relogio de AUDIO parou de avancar (decode travando), o video
                    // NAO fica esperando: segue pelo relogio de parede (nao congela).
                    if (audio_clock != last_ac) { last_ac = audio_clock; last_ac_wall = now; }
                    int audio_ok = adev && (now - last_ac_wall < 0.7);
                    double master;
                    if (audio_ok) { master = audio_clock - SDL_GetQueuedAudioSize(adev) / bps; wall_start = now - master; }
                    else master = now - wall_start;   // audio travado / sem audio: video toca sozinho
                    cur_pos = master;
                    double delay = vpts - master;
                    // Se ja perdeu o prazo por mais de 120 ms, converter e enviar
                    // este quadro para a GPU so aumenta o atraso. Descartar aqui
                    // permite recuperar sincronismo em fontes pesadas/instaveis.
                    if (delay < -0.12) { dropped_video++; continue; }
                    if (delay > 0.001) { if (delay > 0.35) delay = 0.35; SDL_Delay((Uint32)(delay * 1000)); }
                    AVFrame *u = frame;
                    if (sws) { sws_scale(sws, (const uint8_t * const *)frame->data, frame->linesize, 0, vh, yuv->data, yuv->linesize); u = yuv; }
                    SDL_UpdateYUVTexture(tex, NULL, u->data[0], u->linesize[0], u->data[1], u->linesize[1], u->data[2], u->linesize[2]);
                    have_video_frame = 1;
                    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
                    SDL_RenderCopy(ren, tex, NULL, &dst);
                    if (scur >= 0 && sub_text[0] && cur_pos < sub_end) draw_sub(ren, sub_text);
                    if (hud_pinned || SDL_GetTicks() < hud_until)
                        draw_hud(ren, title, cur_pos, dur, 0, vol, fmt, aidx,
                                 acur, naud, nsub, scur, scur >= 0 ? sidxs[scur] : -1, hud_pinned);
                    if (SDL_GetTicks() < notice_until) draw_notice(ren, notice);
                    SDL_RenderPresent(ren);
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

    if (out_pos) *out_pos = cur_pos;
    if (out_dur) *out_dur = dur;
    store_save_player_volume(vol);
    store_save_player_stats(vw, vh, decoded_video, dropped_video,
                            buffering_events, max_audio_queue, playback_error);

    if (adev) SDL_CloseAudioDevice(adev);
    if (sws) sws_freeContext(sws);
    if (yuv) av_frame_free(&yuv);
    if (swr) swr_free(&swr);
    av_freep(&audio_buf);
    if (actx) avcodec_free_context(&actx);
    if (sctx) avcodec_free_context(&sctx);
    avcodec_free_context(&vctx);
    av_frame_free(&frame); av_packet_free(&pkt);
    SDL_DestroyTexture(tex);
    avformat_close_input(&fmt);
    nplay_curl_avio_close(avio);
    return playback_error ? playback_error : reached_end;
}
