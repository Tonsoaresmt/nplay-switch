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
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
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
#include "audio_policy.h"
#include "player_ui.h"
#include "subtitle_queue.h"
#include "hls_manifest.h"
#include "player_next.h"
#include "player_loading.h"
#include "player_sync.h"

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
#define PLAYER_RESTART_SEEK  2
#define PLAYER_RESTART_TRACK 3
#define PLAYER_REQUEST_NEXT  4
#include "player_buffer.h"

static char g_player_last_error[160] = "";
static int g_player_audio_index = 0;
static char g_player_audio_language[8] = "";
static int g_player_subtitle_index = 0;
// Depois da abertura, callbacks HLS rodam pelo worker de demux. Eles nunca
// desenham nem processam SDL; depois do primeiro quadro tambem evitam I/O de
// diagnostico por segmento na microSD.
static int g_player_presented_frame = 0;
static const SDL_Color PC_DARK = { 8, 10, 15, 255 };

static int player_url_is_hls(const char *url) {
    if (!url) return 0;
    const char *query = strchr(url, '?');
    size_t len = query ? (size_t)(query - url) : strlen(url);
    return len >= 5 && !strncasecmp(url + len - 5, ".m3u8", 5);
}

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
    snprintf(g_player_last_error, sizeof(g_player_last_error), "%.*s",
             (int)sizeof(g_player_last_error) - 1, message ? message : "");
}

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
    const char *title;
    int force_loading;
    PlayerLoadingOwner loading_owner;
    const char *headline;
    int operation_active;
    int operation_cancelled;
    int operation_timed_out;
    int suppress_cancel_until_release;
    int64_t operation_deadline_us;
    SDL_atomic_t demux_abort;
} PlayerOpenDeadline;

static int player_open_interrupted(void *userdata) {
    PlayerOpenDeadline *watch = (PlayerOpenDeadline *)userdata;
    if (!watch) return 0;
    if (SDL_AtomicGet(&watch->demux_abort)) return 1;
    // av_read_frame roda na thread de demux durante a reproducao. Os demais
    // campos de watch pertencem a thread de render e nao sao atomicos; a thread
    // de demux observa somente demux_abort, evitando uma corrida de dados ao
    // limpar deadlines ou iniciar uma operacao de seek.
    int on_render_thread = SDL_ThreadID() == watch->render_thread;
    if (!on_render_thread) return 0;
    // Uma faixa HLS pode falhar e o FFmpeg continuar com as demais. Depois
    // que B foi observado, a tentativa inteira deve permanecer cancelada.
    if (watch->cancelled || watch->timed_out) return 1;
    if (watch->operation_active &&
        (watch->operation_cancelled || watch->operation_timed_out)) return 1;
    // avformat_open_input/find_stream_info sao sincronas. Bombeie o controle
    // dentro do callback de interrupcao para B/- realmente funcionarem mesmo
    // enquanto FFmpeg espera rede ou uma rendition HLS.
    // O AVIO de segmentos possui uma thread produtora. SDL events e renderer
    // pertencem exclusivamente a thread que iniciou o player.
    {
        SDL_PumpEvents();
        if (watch->suppress_cancel_until_release) {
            int held = watch->joy &&
                (SDL_JoystickGetButton(watch->joy, JOY_B) ||
                 SDL_JoystickGetButton(watch->joy, JOY_MINUS));
            if (!held) watch->suppress_cancel_until_release = 0;
            return 0;
        }
        SDL_Event queued[16];
        int queued_count = SDL_PeepEvents(queued, 16, SDL_PEEKEVENT,
                                          SDL_JOYBUTTONDOWN, SDL_JOYBUTTONDOWN);
        for (int i = 0; i < queued_count; i++) {
            if (queued[i].jbutton.button == JOY_B ||
                queued[i].jbutton.button == JOY_MINUS) {
                if (watch->operation_active) {
                    watch->operation_cancelled = 1;
                    watch->suppress_cancel_until_release = 1;
                } else watch->cancelled = 1;
                return 1;
            }
        }
    }
    if (watch->joy &&
        (SDL_JoystickGetButton(watch->joy, JOY_B) ||
         SDL_JoystickGetButton(watch->joy, JOY_MINUS))) {
        if (watch->operation_active) {
            watch->operation_cancelled = 1;
            watch->suppress_cancel_until_release = 1;
        } else watch->cancelled = 1;
        return 1;
    }
    if (watch->operation_active && watch->operation_deadline_us > 0 &&
        av_gettime_relative() >= watch->operation_deadline_us) {
        watch->operation_timed_out = 1;
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
    if (watch->renderer && player_loading_interrupt_can_draw(
            watch->loading_owner, on_render_thread, g_player_presented_frame,
            watch->force_loading, now, watch->last_render_tick, 80u)) {
        SDL_SetRenderDrawColor(watch->renderer, PC_DARK.r, PC_DARK.g, PC_DARK.b, 255);
        SDL_RenderClear(watch->renderer);
        pui_draw_loading(watch->renderer, watch->title,
                         watch->headline ? watch->headline : "Preparando video",
                         watch->detail ? watch->detail : "Conectando...", now, 0);
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

static void track_operation_begin(PlayerOpenDeadline *watch,
                                  const char *headline, const char *detail,
                                  unsigned timeout_ms) {
    if (!watch) return;
    watch->operation_active = 1;
    watch->operation_cancelled = 0;
    watch->operation_timed_out = 0;
    watch->operation_deadline_us = av_gettime_relative() +
                                   (int64_t)timeout_ms * 1000;
    watch->force_loading = 1;
    watch->loading_owner = PLAYER_LOADING_OPERATION;
    watch->headline = headline;
    watch->detail = detail;
    watch->last_render_tick = 0;
}

// 0=concluida, 1=cancelada, 2=tempo esgotado.
static int track_operation_end(PlayerOpenDeadline *watch) {
    if (!watch) return 0;
    int result = watch->operation_cancelled ? 1 :
                 watch->operation_timed_out ? 2 : 0;
    watch->operation_active = 0;
    watch->operation_deadline_us = 0;
    watch->operation_cancelled = 0;
    watch->operation_timed_out = 0;
    watch->force_loading = 0;
    watch->loading_owner = PLAYER_LOADING_PLAYBACK;
    watch->headline = NULL;
    watch->detail = "Lendo os primeiros quadros...  |  B para cancelar";
    if (result == 1) SDL_FlushEvent(SDL_JOYBUTTONDOWN);
    return result;
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

static void pfill(SDL_Renderer *r, int x, int y, int w, int h, SDL_Color c, int a) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, a);
    SDL_Rect rr = { x, y, w, h };
    SDL_RenderFillRect(r, &rr);
}
// idioma de um stream (tag "language"), ex.: "por", "eng", "jpn".
static const char *lang_norm(const char *value) {
    return audio_language_normalize(value, NULL);
}

static const char *stream_track_title(const AVStream *stream) {
    if (!stream) return NULL;
    static const char *keys[] = { "title", "comment", "name" };
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        AVDictionaryEntry *entry = av_dict_get(stream->metadata, keys[i], NULL, 0);
        if (entry && entry->value && entry->value[0]) return entry->value;
    }
    return NULL;
}

static const char *stream_norm(AVFormatContext *fmt, int idx) {
    if (!fmt || idx < 0 || idx >= (int)fmt->nb_streams) return "";
    AVDictionaryEntry *language = av_dict_get(fmt->streams[idx]->metadata, "language", NULL, 0);
    return audio_language_normalize(language ? language->value : NULL,
                                    stream_track_title(fmt->streams[idx]));
}

static const char *lang_label(const char *norm) {
    if (!norm || !norm[0]) return "?";
    if (!strcmp(norm, "pt")) return "PT";
    if (!strcmp(norm, "en")) return "EN";
    if (!strcmp(norm, "ja")) return "JP";
    if (!strcmp(norm, "es")) return "ES";
    if (!strcmp(norm, "fr")) return "FR";
    if (!strcmp(norm, "it")) return "IT";
    if (!strcmp(norm, "de")) return "DE";
    if (!strcmp(norm, "ko")) return "KO";
    if (!strcmp(norm, "zh")) return "ZH";
    return "?";
}

static const char *lang_name(const char *norm) {
    if (!strcmp(norm, "pt")) return "Portugues";
    if (!strcmp(norm, "en")) return "Ingles";
    if (!strcmp(norm, "ja")) return "Japones";
    if (!strcmp(norm, "es")) return "Espanhol";
    if (!strcmp(norm, "fr")) return "Frances";
    if (!strcmp(norm, "it")) return "Italiano";
    if (!strcmp(norm, "de")) return "Alemao";
    if (!strcmp(norm, "ko")) return "Coreano";
    if (!strcmp(norm, "zh")) return "Chines";
    return "Desconhecido";
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

static void modern_track_labels(AVFormatContext *fmt, const int *aidxs, int naud,
                                const int *sidxs, int nsub,
                                char audio_names[][96], char audio_details[][96],
                                char sub_names[][96], int inferred_dub,
                                PlayerHud *hud) {
    hud->audio_count = naud;
    hud->sub_count = nsub + 1;
    for (int i = 0; i < naud; i++) {
        AVStream *stream = fmt->streams[aidxs[i]];
        const char *track_title = stream_track_title(stream);
        const char *norm = stream_norm(fmt, aidxs[i]);
        const char *display_name = track_title && track_title[0]
            ? track_title : lang_name(norm);
        const char *display_lang = lang_label(norm);
        if (i == inferred_dub) {
            display_name = "Portugues (dublado)";
            display_lang = "PT*";
        }
        snprintf(audio_names[i], 96, "%s", display_name);
        snprintf(audio_details[i], 96, "%s  |  %s  |  %d canal%s",
                 display_lang, avcodec_get_name(stream->codecpar->codec_id),
                 stream->codecpar->ch_layout.nb_channels,
                 stream->codecpar->ch_layout.nb_channels == 1 ? "" : "is");
        hud->audio_names[i] = audio_names[i];
        hud->audio_details[i] = audio_details[i];
    }
    snprintf(sub_names[0], 96, "Desligadas");
    hud->sub_names[0] = sub_names[0];
    for (int i = 0; i < nsub; i++) {
        AVStream *stream = fmt->streams[sidxs[i]];
        const char *track_title = stream_track_title(stream);
        const char *norm = stream_norm(fmt, sidxs[i]);
        const char *label = track_title && track_title[0]
            ? track_title : lang_name(norm);
        // O master pode oferecer varias renditions com o mesmo idioma (normal,
        // SDH, forced, provedores distintos). Numere-as para que nenhuma pareca
        // uma duplicata sem funcao no painel do controle.
        if (nsub > 1) snprintf(sub_names[i + 1], 96, "%.64s  |  %d/%d", label, i + 1, nsub);
        else snprintf(sub_names[i + 1], 96, "%s", label);
        hud->sub_names[i + 1] = sub_names[i + 1];
    }
}

static void draw_modern_track_menu(SDL_Renderer *ren, const PlayerHud *base,
                                   int menu, int selected, int acur, int scur,
                                   const char *subtitle_text) {
    PlayerHud hud = *base;
    hud.panel_open = 1;
    hud.panel_column = menu == TRACK_MENU_SUB ? 1 : 0;
    hud.audio_sel = menu == TRACK_MENU_AUDIO ? selected : acur;
    hud.audio_current = acur;
    hud.sub_sel = menu == TRACK_MENU_SUB ? selected : scur + 1;
    hud.sub_current = scur + 1;
    hud.subtitle_text = subtitle_text;
    pui_draw(ren, &hud, SDL_GetTicks());
}

static void draw_hud(SDL_Renderer *ren, const PlayerHud *base,
                     double pos, double dur, int paused, int vol,
                     int acur, int scur, int seekable,
                     const char *buffering_text, double scrub_target,
                     const char *scrub_label, const char *subtitle_text) {
    PlayerHud hud = *base;
    hud.pos = pos;
    hud.dur = seekable ? dur : 0;
    hud.paused = paused;
    hud.hud_alpha = 1.0f;
    hud.pause_info_alpha = paused && scrub_target < 0 ? 1.0f : 0.0f;
    hud.focus = base->focus;
    hud.volume = vol;
    hud.audio_current = acur;
    hud.audio_sel = acur;
    hud.sub_current = scur + 1;
    hud.sub_sel = scur + 1;
    hud.buffering = buffering_text && buffering_text[0];
    hud.buffering_text = buffering_text;
    hud.scrubbing = scrub_target >= 0;
    hud.scrub_target = scrub_target >= 0 ? scrub_target : pos;
    hud.scrub_label = scrub_label;
    hud.subtitle_text = subtitle_text;
    if (hud.scrubbing) hud.focus = PUI_FOCUS_TIMELINE;
    pui_draw(ren, &hud, SDL_GetTicks());
}

static void draw_subtitle_overlay(SDL_Renderer *ren, const PlayerHud *base,
                                  const char *subtitle_text) {
    if ((!subtitle_text || !subtitle_text[0]) && base->next_card_alpha <= 0.01f) return;
    PlayerHud hud = *base;
    hud.hud_alpha = 0;
    hud.pause_info_alpha = 0;
    hud.subtitle_text = subtitle_text;
    pui_draw(ren, &hud, SDL_GetTicks());
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

static int seek_video_time(AVFormatContext *fmt, int video_index, int is_hls,
                           double target, double timeline_origin, int flags) {
    int64_t global_ts = (int64_t)((target + timeline_origin) * AV_TIME_BASE);
    if (is_hls && video_index >= 0 &&
        fmt->streams[video_index]->time_base.num > 0 &&
        fmt->streams[video_index]->time_base.den > 0) {
        int64_t video_ts = av_rescale_q(global_ts, AV_TIME_BASE_Q,
                                        fmt->streams[video_index]->time_base);
        int64_t window = av_rescale_q(15LL * AV_TIME_BASE, AV_TIME_BASE_Q,
                                      fmt->streams[video_index]->time_base);
        int64_t min_ts = video_ts > INT64_MIN + window ? video_ts - window : INT64_MIN;
        int64_t max_ts = video_ts < INT64_MAX - window / 3
            ? video_ts + window / 3 : INT64_MAX;
        int rc = avformat_seek_file(fmt, video_index, min_ts, video_ts, max_ts,
                                    flags & ~AVSEEK_FLAG_BACKWARD);
        if (rc >= 0) return rc;
        diag_player_event("seek", "window-fallback", "rc=%d target=%.2f", rc, target);
        return av_seek_frame(fmt, video_index, video_ts, flags);
    }
    int64_t min_ts = global_ts > INT64_MIN + 15LL * AV_TIME_BASE
        ? global_ts - 15LL * AV_TIME_BASE : INT64_MIN;
    int64_t max_ts = global_ts < INT64_MAX - 5LL * AV_TIME_BASE
        ? global_ts + 5LL * AV_TIME_BASE : INT64_MAX;
    int rc = avformat_seek_file(fmt, -1, min_ts, global_ts, max_ts,
                                flags & ~AVSEEK_FLAG_BACKWARD);
    if (rc >= 0) return rc;
    diag_player_event("seek", "window-fallback", "rc=%d target=%.2f", rc, target);
    return av_seek_frame(fmt, -1, global_ts, flags);
}

static int apply_player_seek(AVFormatContext *fmt, int video_index, int is_hls,
                             AVCodecContext *vctx, AVCodecContext *actx,
                             AVCodecContext *sctx, SDL_AudioDeviceID adev,
                             double target, int force_backward,
                             double timeline_origin, double *wall_start,
                             double *audio_clock, double *cur_pos,
                             double *last_ac, double *last_ac_wall,
                             SubtitleQueue *subtitles) {
    int flags = force_backward || target < *cur_pos ? AVSEEK_FLAG_BACKWARD : 0;
    int rc = seek_video_time(fmt, video_index, is_hls, target,
                             timeline_origin, flags);
    if (rc < 0) return rc;
    avcodec_flush_buffers(vctx);
    if (actx) avcodec_flush_buffers(actx);
    if (sctx) avcodec_flush_buffers(sctx);
    if (adev) SDL_ClearQueuedAudio(adev);
    subtitle_queue_reset(subtitles);
    double now = av_gettime_relative() / 1000000.0;
    *wall_start = now - target;
    *audio_clock = target;
    *cur_pos = target;
    *last_ac = -1;
    *last_ac_wall = now;
    return 0;
}

static int create_audio_dec(AVFormatContext *fmt, int aidx,
                            AVCodecContext **created) {
    if (!fmt || !created || aidx < 0) return -1;
    *created = NULL;
    AVCodecParameters *apar = fmt->streams[aidx]->codecpar;
    const AVCodec *adec = avcodec_find_decoder(apar->codec_id);
    if (!adec) return -1;
    AVCodecContext *actx = avcodec_alloc_context3(adec);
    if (!actx || avcodec_parameters_to_context(actx, apar) < 0) {
        avcodec_free_context(&actx);
        return -1;
    }
    actx->pkt_timebase = fmt->streams[aidx]->time_base;
    if (avcodec_open2(actx, adec, NULL) != 0) { avcodec_free_context(&actx); return -1; }
    *created = actx;
    return 0;
}

// Reabre somente o decoder de audio para a faixa escolhida e fecha o anterior.
// O resample e montado com os parametros reais de cada frame.
// HE-AAC e fontes que nao sao 48kHz (senao o audio sai errado e o video trava).
static int open_audio_dec(AVFormatContext *fmt, int aidx, AVCodecContext **pactx,
                          struct SwrContext **pswr, int OCH, int ORATE) {
    (void)OCH; (void)ORATE;
    AVCodecContext *actx = NULL;
    if (create_audio_dec(fmt, aidx, &actx) != 0) return -1;
    if (*pswr) swr_free(pswr);       // o loop reconstroi com os parametros do novo frame
    if (*pactx) avcodec_free_context(pactx);
    *pactx = actx;
    return 0;
}
static int create_sub_dec(AVFormatContext *fmt, int sidx,
                          AVCodecContext **created) {
    if (!fmt || !created || sidx < 0) return -1;
    *created = NULL;
    AVCodecParameters *sp = fmt->streams[sidx]->codecpar;
    const AVCodec *sd = avcodec_find_decoder(sp->codec_id);
    if (!sd) return -1;
    AVCodecContext *sc = avcodec_alloc_context3(sd);
    if (!sc || avcodec_parameters_to_context(sc, sp) < 0) {
        avcodec_free_context(&sc);
        return -1;
    }
    sc->pkt_timebase = fmt->streams[sidx]->time_base;
    if (avcodec_open2(sc, sd, NULL) != 0) { avcodec_free_context(&sc); return -1; }
    *created = sc;
    return 0;
}

// (re)abre o decoder de legenda p/ o stream sidx (-1 = desliga).
static int open_sub_dec(AVFormatContext *fmt, int sidx, AVCodecContext **psctx) {
    if (sidx < 0) {
        if (*psctx) avcodec_free_context(psctx);
        return 0;
    }
    AVCodecContext *sc = NULL;
    if (create_sub_dec(fmt, sidx, &sc) != 0) return -1;
    if (*psctx) avcodec_free_context(psctx);
    *psctx = sc;
    return 0;
}
// extrai o texto legivel de uma linha ASS (tira campos e tags {\...}, \N -> linha).
static void ass_to_text(const char *ass, char *out, int cap) {
    const char *p = ass; int commas = 0;
    for (const char *q = ass; *q && commas < 8; q++) if (*q == ',') { commas++; p = q + 1; }
    if (commas < 8) p = ass;
    int k = 0;
    while (*p && k < cap - 1) {
        if (p[0] == '{') { const char *e = strchr(p, '}'); if (e) { p = e + 1; continue; } }
        if (p[0] == '\\' && (p[1] == 'N' || p[1] == 'n')) { out[k++] = '\n'; p += 2; continue; }
        if (p[0] == '\r') { p++; continue; }
        if (p[0] == '\n') { out[k++] = '\n'; p++; continue; }
        out[k++] = *p++;
    }
    out[k] = 0;
}

typedef struct {
    SubtitleCue *cues;
    int count;
    int capacity;
    char composed[SUBTITLE_COMPOSED_CAP];
} ExternalSubtitleStore;

static void external_subtitle_clear(ExternalSubtitleStore *store) {
    if (!store) return;
    free(store->cues);
    memset(store, 0, sizeof(*store));
}

static int external_subtitle_add(ExternalSubtitleStore *store, double start,
                                 double end, const char *text) {
    if (!store || !text || !text[0] || store->count >= 8192) return 0;
    if (store->count == store->capacity) {
        int next = store->capacity ? store->capacity * 2 : 128;
        if (next > 8192) next = 8192;
        SubtitleCue *grown = realloc(store->cues, (size_t)next * sizeof(*grown));
        if (!grown) return 0;
        store->cues = grown;
        store->capacity = next;
    }
    SubtitleCue *cue = &store->cues[store->count++];
    cue->start = start < 0 ? 0 : start;
    cue->end = end > cue->start ? end : cue->start + 4.0;
    snprintf(cue->text, sizeof(cue->text), "%s", text);
    return 1;
}

static const char *external_subtitle_text(ExternalSubtitleStore *store,
                                          double position) {
    if (!store) return "";
    store->composed[0] = 0;
    for (int i = 0; i < store->count; i++) {
        SubtitleCue *cue = &store->cues[i];
        if (cue->start > position + 0.05) break;
        if (position >= cue->end) continue;
        size_t used = strlen(store->composed);
        size_t remaining = sizeof(store->composed) - used;
        if (remaining <= 1) break;
        if (used) {
            strncat(store->composed, "\n", remaining - 1);
            used++;
            remaining = sizeof(store->composed) - used;
        }
        strncat(store->composed, cue->text, remaining - 1);
    }
    return store->composed;
}

static const char *active_subtitle_text(int selected, int external,
                                        SubtitleQueue *queue,
                                        ExternalSubtitleStore *store,
                                        double position) {
    if (selected < 0) return "";
    return external ? external_subtitle_text(store, position)
                    : subtitle_queue_text(queue, position);
}

// Alguns builds do demuxer HLS do Switch nao criam AVStreams para EXT-X-MEDIA
// de legenda, embora o mesmo master seja entendido pelo hls.js. Abra somente a
// rendition WebVTT escolhida e materialize seus cues; isto nao toca/reabre a
// pipeline principal de video e audio.
static int load_external_hls_subtitle(const char *url,
                                      ExternalSubtitleStore *store) {
    if (!url || !url[0] || !store) return -1;
    ExternalSubtitleStore loaded = {0};
    AVIOContext *root = nplay_curl_avio_open_hls(url);
    AVFormatContext *subfmt = avformat_alloc_context();
    if (!root || !subfmt) {
        nplay_curl_avio_close(root);
        avformat_free_context(subfmt);
        return -1;
    }
    subfmt->pb = root;
    subfmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    subfmt->io_open = player_hls_io_open;
    subfmt->io_close2 = player_hls_io_close;
    const AVInputFormat *hls = av_find_input_format("hls");
    AVDictionary *opts = NULL;
    av_dict_set(&opts, "allowed_extensions", "ALL", 0);
    av_dict_set(&opts, "http_persistent", "0", 0);
    int rc = avformat_open_input(&subfmt, url, hls, &opts);
    av_dict_free(&opts);
    if (rc < 0 || avformat_find_stream_info(subfmt, NULL) < 0) goto fail;
    int stream = av_find_best_stream(subfmt, AVMEDIA_TYPE_SUBTITLE, -1, -1, NULL, 0);
    if (stream < 0) goto fail;
    AVCodecParameters *par = subfmt->streams[stream]->codecpar;
    if (par->codec_id == AV_CODEC_ID_NONE) par->codec_id = AV_CODEC_ID_WEBVTT;
    const AVCodec *decoder = avcodec_find_decoder(par->codec_id);
    AVCodecContext *ctx = decoder ? avcodec_alloc_context3(decoder) : NULL;
    AVPacket *packet = av_packet_alloc();
    if (!ctx || !packet || avcodec_parameters_to_context(ctx, par) < 0 ||
        avcodec_open2(ctx, decoder, NULL) < 0) {
        av_packet_free(&packet);
        avcodec_free_context(&ctx);
        goto fail;
    }
    AVStream *st = subfmt->streams[stream];
    while ((rc = av_read_frame(subfmt, packet)) >= 0) {
        if (packet->stream_index == stream) {
            AVSubtitle sub = {0}; int got = 0;
            if (avcodec_decode_subtitle2(ctx, &sub, &got, packet) >= 0 && got) {
                char text[SUBTITLE_TEXT_CAP] = "";
                for (unsigned i = 0; i < sub.num_rects; i++) {
                    AVSubtitleRect *rect = sub.rects[i]; char part[400] = "";
                    if (rect->type == SUBTITLE_ASS && rect->ass)
                        ass_to_text(rect->ass, part, sizeof(part));
                    else if (rect->type == SUBTITLE_TEXT && rect->text)
                        snprintf(part, sizeof(part), "%s", rect->text);
                    if (part[0]) {
                        size_t left = sizeof(text) - strlen(text) - 1;
                        if (text[0] && left > 1) { strncat(text, "\n", left); left--; }
                        strncat(text, part, left);
                    }
                }
                double decoded = sub.pts != AV_NOPTS_VALUE
                    ? sub.pts / (double)AV_TIME_BASE : NAN;
                double packet_pts = packet->pts != AV_NOPTS_VALUE
                    ? packet->pts * av_q2d(st->time_base) : NAN;
                double packet_duration = packet->duration > 0
                    ? packet->duration * av_q2d(st->time_base) : 0;
                double start = 0, end = 0;
                subtitle_cue_times(decoded, packet_pts, packet_duration,
                                   sub.start_display_time, sub.end_display_time,
                                   0, &start, &end);
                if (text[0] && !external_subtitle_add(&loaded, start, end, text)) {
                    avsubtitle_free(&sub);
                    av_packet_unref(packet);
                    break;
                }
                avsubtitle_free(&sub);
            }
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    avcodec_free_context(&ctx);
    avformat_close_input(&subfmt);
    nplay_curl_avio_close(root);
    if (loaded.count <= 0) { external_subtitle_clear(&loaded); return -1; }
    external_subtitle_clear(store);
    *store = loaded;
    return 0;
fail:
    avformat_close_input(&subfmt);
    nplay_curl_avio_close(root);
    external_subtitle_clear(&loaded);
    return -1;
}
typedef struct {
    int item_id;
    SDL_atomic_t session_id;
    SDL_atomic_t running;
    SDL_atomic_t current_pos;
    SDL_atomic_t duration;
    SDL_atomic_t force_progress;
    SDL_atomic_t pipeline_ready;
    SDL_atomic_t final_progress;
    SDL_atomic_t cancel_io;
    SDL_atomic_t finished;
    PlayerProgressCallback progress_cb;
    PlayerHeartbeatCallback heartbeat_cb;
    PlayerStopCallback stop_cb;
    void *callback_userdata;
} PlaybackHeartbeat;

typedef struct {
    PlayerRenewCallback callback;
    PlaybackSource current;
    PlaybackSource renewed;
    void *callback_userdata;
    SDL_atomic_t cancel;
    SDL_atomic_t done;
    int rc;
    char error[192];
} PlayerRecoveryJob;

static int player_recovery_thread(void *userdata) {
    PlayerRecoveryJob *job = (PlayerRecoveryJob *)userdata;
    job->rc = job->callback(&job->current, &job->renewed, &job->cancel,
                            job->callback_userdata);
    if (job->rc != 0) {
        const char *error = api_last_error();
        if (error && error[0])
            snprintf(job->error, sizeof(job->error), "%s", error);
    }
    SDL_AtomicSet(&job->done, 1);
    return 0;
}

// Executa rede fora da thread que desenha. Retorna 0=concluiu, 1=cancelou,
// -1=falhou. A thread e sempre reunida antes de a struct local sair de escopo.
static int player_recovery_call(SDL_Renderer *ren, SDL_Joystick *joy,
                                const char *title, const char *headline,
                                const char *detail, PlayerRenewCallback callback,
                                const PlaybackSource *current, PlaybackSource *renewed,
                                void *callback_userdata) {
    if (!callback || !current || !renewed) return -1;
    PlayerRecoveryJob job = {0};
    job.callback = callback;
    job.current = *current;
    job.callback_userdata = callback_userdata;
    SDL_AtomicSet(&job.cancel, 0);
    SDL_AtomicSet(&job.done, 0);
    SDL_Thread *thread = SDL_CreateThread(player_recovery_thread,
                                          "player-recovery", &job);
    if (!thread) return -1;

    int cancelled = 0;
    while (!SDL_AtomicGet(&job.done)) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_JOYBUTTONDOWN &&
                 (event.jbutton.button == JOY_B || event.jbutton.button == JOY_MINUS))) {
                cancelled = 1;
                SDL_AtomicSet(&job.cancel, 1);
                break;
            }
        }
        if (!cancelled && joy && (SDL_JoystickGetButton(joy, JOY_B) ||
                                  SDL_JoystickGetButton(joy, JOY_MINUS))) {
            cancelled = 1;
            SDL_AtomicSet(&job.cancel, 1);
        }
        pui_draw_loading(ren, title, headline, detail, SDL_GetTicks(), 1);
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
    SDL_WaitThread(thread, NULL);
    if (cancelled) {
        SDL_FlushEvent(SDL_JOYBUTTONDOWN);
        return 1;
    }
    if (job.rc != 0 || !job.renewed.play_url[0]) {
        if (job.error[0]) player_error_message(job.error);
        return -1;
    }
    *renewed = job.renewed;
    return 0;
}

typedef struct {
    AVPacket *packet;
    Uint32 read_ms;
} DemuxItem;

typedef struct {
    AVFormatContext *fmt;
    PlayerOpenDeadline *watch;
    SDL_mutex *mutex;
    SDL_cond *data_ready;
    SDL_cond *space_ready;
    SDL_Thread *thread;
    DemuxItem queue[DEMUX_QUEUE_PACKETS];
    int head, count, stop, terminal;
    int pause_request, paused, exited;
    size_t queued_bytes;
    int high_packets;
    size_t high_bytes;
    unsigned generation, empty_reads;
    Uint32 space_wait_ms;
    double high_video_seconds;
} DemuxWorker;

static int demux_worker_thread(void *userdata) {
    DemuxWorker *worker = (DemuxWorker *)userdata;
    while (1) {
        SDL_LockMutex(worker->mutex);
        while (!worker->stop) {
            if (worker->pause_request) {
                worker->paused = 1;
                SDL_CondBroadcast(worker->data_ready);
                while (!worker->stop && worker->pause_request)
                    SDL_CondWait(worker->space_ready, worker->mutex);
                worker->paused = 0;
                SDL_CondBroadcast(worker->data_ready);
                continue;
            }
            if (worker->count < DEMUX_QUEUE_PACKETS &&
                (worker->count == 0 || worker->queued_bytes < DEMUX_QUEUE_BYTES))
                break;
            SDL_CondWait(worker->space_ready, worker->mutex);
        }
        int stop = worker->stop;
        unsigned generation = worker->generation;
        SDL_UnlockMutex(worker->mutex);
        if (stop) break;

        AVPacket *packet = av_packet_alloc();
        if (!packet) {
            SDL_LockMutex(worker->mutex);
            worker->terminal = AVERROR(ENOMEM);
            SDL_CondBroadcast(worker->data_ready);
            SDL_UnlockMutex(worker->mutex);
            break;
        }
        Uint32 started = SDL_GetTicks();
        int rc = av_read_frame(worker->fmt, packet);
        Uint32 elapsed = SDL_GetTicks() - started;
        if (rc == AVERROR(EAGAIN)) {
            av_packet_free(&packet);
            SDL_Delay(8);
            continue;
        }

        SDL_LockMutex(worker->mutex);
        if (worker->stop) {
            SDL_UnlockMutex(worker->mutex);
            av_packet_free(&packet);
            break;
        }
        if (rc < 0) {
            worker->terminal = rc;
            SDL_CondBroadcast(worker->data_ready);
            SDL_UnlockMutex(worker->mutex);
            av_packet_free(&packet);
            break;
        }
        size_t incoming = packet->size > 0 ? (size_t)packet->size : 0;
        if (incoming > DEMUX_QUEUE_BYTES) {
            worker->terminal = AVERROR(ENOBUFS);
            SDL_CondBroadcast(worker->data_ready);
            SDL_UnlockMutex(worker->mutex);
            av_packet_free(&packet);
            break;
        }
        Uint32 wait_started = SDL_GetTicks();
        // A pending packet never defeats the byte ceiling. A full queue still
        // acknowledges the barrier; clear() changes generation before resume.
        while (!worker->stop && generation == worker->generation) {
            if (worker->pause_request) {
                worker->paused = 1;
                SDL_CondBroadcast(worker->data_ready);
                while (!worker->stop && worker->pause_request)
                    SDL_CondWait(worker->space_ready, worker->mutex);
                worker->paused = 0;
                SDL_CondBroadcast(worker->data_ready);
            } else if (demux_buffer_can_enqueue(worker->count, worker->queued_bytes, incoming)) {
                break;
            } else SDL_CondWait(worker->space_ready, worker->mutex);
        }
        worker->space_wait_ms += SDL_GetTicks() - wait_started;
        if (worker->stop || generation != worker->generation) {
            SDL_UnlockMutex(worker->mutex);
            av_packet_free(&packet);
            continue;
        }
        int tail = (worker->head + worker->count) % DEMUX_QUEUE_PACKETS;
        worker->queue[tail].packet = packet;
        worker->queue[tail].read_ms = elapsed;
        worker->count++;
        if (packet->size > 0) worker->queued_bytes += (size_t)packet->size;
        if (worker->count > worker->high_packets) worker->high_packets = worker->count;
        if (worker->queued_bytes > worker->high_bytes) worker->high_bytes = worker->queued_bytes;
        double first = HUGE_VAL, last = -HUGE_VAL;
        for (int i = 0; i < worker->count; i++) {
            AVPacket *queued = worker->queue[(worker->head + i) % DEMUX_QUEUE_PACKETS].packet;
            AVStream *stream = worker->fmt->streams[queued->stream_index];
            if (stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO || queued->pts == AV_NOPTS_VALUE) continue;
            double pts = queued->pts * av_q2d(stream->time_base);
            if (pts < first) first = pts;
            if (pts > last) last = pts;
        }
        if (last >= first && last - first > worker->high_video_seconds)
            worker->high_video_seconds = last - first;
        SDL_CondSignal(worker->data_ready);
        SDL_UnlockMutex(worker->mutex);
    }
    SDL_LockMutex(worker->mutex);
    worker->exited = 1;
    worker->paused = 0;
    SDL_CondBroadcast(worker->data_ready);
    SDL_UnlockMutex(worker->mutex);
    return 0;
}

static int demux_worker_start(DemuxWorker *worker, AVFormatContext *fmt,
                              PlayerOpenDeadline *watch) {
    memset(worker, 0, sizeof(*worker));
    worker->fmt = fmt;
    worker->watch = watch;
    worker->mutex = SDL_CreateMutex();
    worker->data_ready = SDL_CreateCond();
    worker->space_ready = SDL_CreateCond();
    if (!worker->mutex || !worker->data_ready || !worker->space_ready) {
        if (worker->data_ready) SDL_DestroyCond(worker->data_ready);
        if (worker->space_ready) SDL_DestroyCond(worker->space_ready);
        if (worker->mutex) SDL_DestroyMutex(worker->mutex);
        memset(worker, 0, sizeof(*worker));
        return -1;
    }
    SDL_AtomicSet(&watch->demux_abort, 0);
    worker->thread = SDL_CreateThread(demux_worker_thread, "player-demux", worker);
    if (!worker->thread) {
        SDL_DestroyCond(worker->data_ready);
        SDL_DestroyCond(worker->space_ready);
        SDL_DestroyMutex(worker->mutex);
        memset(worker, 0, sizeof(*worker));
        return -1;
    }
    return 0;
}

// 1=packet, 0=ainda lendo, negativo=fim/erro.
static int demux_worker_take(DemuxWorker *worker, AVPacket *out, Uint32 *read_ms) {
    int result = 0;
    SDL_LockMutex(worker->mutex);
    if (worker->count > 0) {
        DemuxItem *item = &worker->queue[worker->head];
        if (item->packet->size > 0) worker->queued_bytes -= (size_t)item->packet->size;
        av_packet_move_ref(out, item->packet);
        if (read_ms) *read_ms = item->read_ms;
        av_packet_free(&item->packet);
        item->read_ms = 0;
        worker->head = (worker->head + 1) % DEMUX_QUEUE_PACKETS;
        worker->count--;
        SDL_CondSignal(worker->space_ready);
        result = 1;
    } else if (worker->terminal) result = worker->terminal;
    else worker->empty_reads++;
    SDL_UnlockMutex(worker->mutex);
    return result;
}

static void demux_worker_request_pause(DemuxWorker *worker) {
    if (!worker || !worker->mutex) return;
    SDL_LockMutex(worker->mutex);
    worker->pause_request = 1;
    SDL_CondBroadcast(worker->space_ready);
    SDL_UnlockMutex(worker->mutex);
}

// 1=barreira atingida, 0=ainda lendo, -1=worker terminou.
static int demux_worker_pause_state(DemuxWorker *worker) {
    if (!worker || !worker->mutex) return -1;
    SDL_LockMutex(worker->mutex);
    int state = worker->paused ? 1 : worker->exited ? -1 : 0;
    SDL_UnlockMutex(worker->mutex);
    return state;
}

static void demux_worker_resume(DemuxWorker *worker) {
    if (!worker || !worker->mutex) return;
    SDL_LockMutex(worker->mutex);
    worker->pause_request = 0;
    SDL_CondBroadcast(worker->space_ready);
    SDL_UnlockMutex(worker->mutex);
}

// Deve ser chamado apenas depois de pause_state==1. Libera pacotes do ponto
// antigo antes de seek/troca de rendition para nenhum decoder receber gerações
// misturadas.
static void demux_worker_clear(DemuxWorker *worker) {
    if (!worker || !worker->mutex) return;
    SDL_LockMutex(worker->mutex);
    for (int i = 0; i < DEMUX_QUEUE_PACKETS; i++) {
        if (worker->queue[i].packet) av_packet_free(&worker->queue[i].packet);
        worker->queue[i].read_ms = 0;
    }
    worker->head = 0;
    worker->count = 0;
    worker->queued_bytes = 0;
    worker->generation++;
    SDL_UnlockMutex(worker->mutex);
}

// A thread de UI permanece viva enquanto av_read_frame termina a chamada em
// andamento. Nunca toca no AVFormatContext antes da confirmação da barreira.
static int demux_worker_wait_paused(DemuxWorker *worker, SDL_Renderer *ren,
                                    SDL_Joystick *joy, const char *title,
                                    const char *headline, Uint32 timeout_ms) {
    demux_worker_request_pause(worker);
    Uint32 started = SDL_GetTicks();
    while (SDL_GetTicks() - started < timeout_ms) {
        int state = demux_worker_pause_state(worker);
        if (state != 0) return state > 0 ? 0 : -1;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                demux_worker_resume(worker);
                return 3;
            }
            if (event.type == SDL_JOYBUTTONDOWN &&
                (event.jbutton.button == JOY_B || event.jbutton.button == JOY_MINUS)) {
                demux_worker_resume(worker);
                SDL_FlushEvent(SDL_JOYBUTTONDOWN);
                return 1;
            }
        }
        if (joy && (SDL_JoystickGetButton(joy, JOY_B) ||
                    SDL_JoystickGetButton(joy, JOY_MINUS))) {
            demux_worker_resume(worker);
            SDL_FlushEvent(SDL_JOYBUTTONDOWN);
            return 1;
        }
        pui_draw_loading(ren, title, headline,
                         "Aguardando um ponto seguro...  |  B para cancelar",
                         SDL_GetTicks(), 1);
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
    demux_worker_resume(worker);
    return 2;
}

static int player_seek_with_barrier(DemuxWorker *worker,
                                    PlayerOpenDeadline *watch,
                                    SDL_Renderer *ren, SDL_Joystick *joy,
                                    const char *title, const char *headline,
                                    AVFormatContext *fmt, int video_index,
                                    int is_hls, AVCodecContext *vctx,
                                    AVCodecContext *actx, AVCodecContext *sctx,
                                    SDL_AudioDeviceID adev, double target,
                                    int force_backward, double timeline_origin,
                                    double *wall_start, double *audio_clock,
                                    double *cur_pos, double *last_ac,
                                    double *last_ac_wall,
                                    SubtitleQueue *subtitles) {
    double original_pos = *cur_pos;
    int barrier = demux_worker_wait_paused(worker, ren, joy, title, headline, 5000u);
    if (barrier != 0) return barrier;
    demux_worker_clear(worker);
    track_operation_begin(watch, headline,
                          "Buscando o ponto sem reabrir o video...  |  B para cancelar",
                          12000u);
    int rc = apply_player_seek(fmt, video_index, is_hls, vctx, actx, sctx,
                               adev, target, force_backward, timeline_origin,
                               wall_start, audio_clock, cur_pos, last_ac,
                               last_ac_wall, subtitles);
    int operation = track_operation_end(watch);
    demux_worker_resume(worker);
    // Depois que avformat_seek_file comecou, cancelar/expirar nao significa que
    // o contexto permaneceu no ponto antigo. Nao continue uma pipeline de estado
    // incerto: o chamador deve reabri-la na posicao original.
    if (operation != 0) {
        *cur_pos = original_pos;
        return 4;
    }
    return rc >= 0 ? 0 : 2;
}

static void demux_worker_stop(DemuxWorker *worker) {
    if (!worker) return;
    if (worker->mutex) {
        SDL_LockMutex(worker->mutex);
        worker->stop = 1;
        SDL_AtomicSet(&worker->watch->demux_abort, 1);
        if (worker->space_ready) SDL_CondBroadcast(worker->space_ready);
        if (worker->data_ready) SDL_CondBroadcast(worker->data_ready);
        SDL_UnlockMutex(worker->mutex);
    }
    if (worker->thread) SDL_WaitThread(worker->thread, NULL);
    diag_player_event("demux", "worker-summary",
                      "maxPackets=%d maxKB=%u terminal=%d remaining=%d videoSec=%.2f empty=%u waitMs=%u",
                      worker->high_packets, (unsigned)(worker->high_bytes / 1024),
                      worker->terminal, worker->count, worker->high_video_seconds,
                      worker->empty_reads, worker->space_wait_ms);
    for (int i = 0; i < DEMUX_QUEUE_PACKETS; i++)
        if (worker->queue[i].packet) av_packet_free(&worker->queue[i].packet);
    if (worker->data_ready) SDL_DestroyCond(worker->data_ready);
    if (worker->space_ready) SDL_DestroyCond(worker->space_ready);
    if (worker->mutex) SDL_DestroyMutex(worker->mutex);
    if (worker->watch) SDL_AtomicSet(&worker->watch->demux_abort, 0);
    memset(worker, 0, sizeof(*worker));
}
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
    int is_hls = (req->container && !strcasecmp(req->container, "m3u8")) ||
                 player_url_is_hls(url);
    int sequential_stream = req->playback.sequential_stream;
    const char *title = req->title;
    // Tela de preparacao enquanto abre a conexao e le os metadados.
    pui_draw_loading(ren, title, "Preparando video", "Conectando...", SDL_GetTicks(), 0);
    SDL_RenderPresent(ren);

    // Arquivos sdmc:/ usam o protocolo local. HLS remoto delega master, filhos e
    // segmentos ao callback libcurl; MP4 remoto continua no protocolo do FFmpeg.
    int remote = !strncmp(url, "http://", 7) || !strncmp(url, "https://", 8);
    // HLS precisa abrir a playlist e depois seus sub-manifestos/segmentos.
    int native_hls = remote && is_hls;
    PlayerOpenDeadline open_watch = {
        .deadline_us = av_gettime_relative() + 30000000LL,
        .joy = joy,
        .started_tick = SDL_GetTicks(),
        .last_progress_tick = SDL_GetTicks(),
        .phase = "abrindo",
        .native_hls = native_hls,
        .renderer = ren,
        .render_thread = SDL_ThreadID(),
        .last_render_tick = SDL_GetTicks(),
        .detail = "Conectando...",
        .title = title,
        .loading_owner = PLAYER_LOADING_OPENING
    };
    nplay_curl_avio_set_abort_check(native_hls ? player_open_interrupted : NULL,
                                    native_hls ? &open_watch : NULL);
    nplay_curl_avio_set_startup_window(native_hls ? 30000u : 0u);
    // A build local ja possui HTTPS+TLS validado. Use o protocolo nativo tambem
    // para MP4: ele conhece Range/seek do MOV e elimina o AVIO por blocos que no
    // hardware ainda encerrava anime com `abrir fonte: End of file`.
    AVIOContext *avio = NULL;
    int master_audio_count = 0, master_subtitle_count = 0;
    HlsManifestTrack manifest_subtitles[HLS_MANIFEST_TRACK_CAP] = {0};
    int manifest_subtitle_count = 0;
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
        nplay_curl_avio_hls_media_counts(avio, &master_audio_count,
                                         &master_subtitle_count);
        const unsigned char *master_body = NULL;
        size_t master_length = 0;
        char master_url[2048] = "";
        if (nplay_curl_avio_metadata(avio, &master_body, &master_length,
                                    master_url, sizeof(master_url))) {
            HlsManifestTrack parsed[HLS_MANIFEST_TRACK_CAP] = {0};
            int parsed_count = hls_manifest_subtitle_tracks(
                (const char *)master_body, master_length, parsed,
                HLS_MANIFEST_TRACK_CAP);
            for (int i = 0; i < parsed_count; i++) {
                if (!hls_manifest_resolve_url(master_url, parsed[i].uri,
                                              manifest_subtitles[manifest_subtitle_count].uri,
                                              sizeof(manifest_subtitles[0].uri)))
                    continue;
                snprintf(manifest_subtitles[manifest_subtitle_count].name,
                         sizeof(manifest_subtitles[0].name), "%s", parsed[i].name);
                snprintf(manifest_subtitles[manifest_subtitle_count].language,
                         sizeof(manifest_subtitles[0].language), "%s", parsed[i].language);
                manifest_subtitles[manifest_subtitle_count].is_default = parsed[i].is_default;
                manifest_subtitles[manifest_subtitle_count].forced = parsed[i].forced;
                manifest_subtitle_count++;
            }
        }
        diag_player_event("hls-master", "renditions", "audio=%d subtitle=%d",
                          master_audio_count, master_subtitle_count);
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
    pui_draw_loading(ren, title, "Preparando video", open_watch.detail, SDL_GetTicks(), 0);
    SDL_RenderPresent(ren);
    open_watch.last_render_tick = SDL_GetTicks();
    if (!native_hls) open_watch.deadline_us = av_gettime_relative() + 30000000LL;
    // Na abertura HLS, priorize video e um audio. Legendas e audios alternativos
    // continuam enumerados; suas playlists so sao lidas quando selecionadas.
    int probe_audio = -1, probe_video = -1, discovered_subtitles = 0;
    if (native_hls) {
        for (unsigned i = 0; i < fmt->nb_streams; i++) {
            if (probe_audio < 0 && fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                probe_audio = (int)i;
            if (probe_video < 0 && fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                probe_video = (int)i;
            if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE)
                discovered_subtitles++;
        }
        // Sem legendas, mantenha a abertura rapida. Quando o master anuncia
        // SUBTITLES, o FFmpeg precisa sondar as renditions uma vez para criar
        // AVStreams/codecpar utilizaveis; descarta-las aqui fazia o painel ficar
        // vazio mesmo com varias legendas disponiveis no site.
        if (master_subtitle_count == 0 ||
            discovered_subtitles >= master_subtitle_count)
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
        int subtitles_ready = master_subtitle_count == 0 ||
                              discovered_subtitles >= master_subtitle_count;
        headers_ready = subtitles_ready && header_video >= 0 &&
                        (probe_audio < 0 || header_audio >= 0);
    }
    player_boot_stage(headers_ready ? "05 headers completos" : "05 lendo faixas");
    diag_player_event("format", headers_ready ? "probe-skip" : "probe-begin",
                      "streams=%u masterSub=%d foundSub=%d", fmt->nb_streams,
                      master_subtitle_count, discovered_subtitles);
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

    // Enumera audio e legendas de texto. Na abertura rapida do HLS R2, o master
    // ja criou as AVStreams de legenda, mas o codec pode continuar NONE porque
    // nao abrimos cada rendition durante o probe. O contrato do empacotador e
    // WebVTT; complete apenas esse caso conhecido em vez de esconder as faixas.
    int aidxs[16], naud = 0, sidxs[16], nsub = 0;
    int repaired_subtitles = 0, unsupported_subtitles = 0;
    for (unsigned i = 0; i < fmt->nb_streams; i++) {
        int t = fmt->streams[i]->codecpar->codec_type, cid = fmt->streams[i]->codecpar->codec_id;
        if (t == AVMEDIA_TYPE_SUBTITLE && cid == AV_CODEC_ID_NONE &&
            native_hls && req->delivery == DELIVERY_R2) {
            fmt->streams[i]->codecpar->codec_id = AV_CODEC_ID_WEBVTT;
            cid = AV_CODEC_ID_WEBVTT;
            repaired_subtitles++;
        }
        if (t == AVMEDIA_TYPE_AUDIO && naud < 16) aidxs[naud++] = (int)i;
        else if (t == AVMEDIA_TYPE_SUBTITLE && nsub < 16 &&
                 (cid == AV_CODEC_ID_SUBRIP || cid == AV_CODEC_ID_ASS || cid == AV_CODEC_ID_SSA ||
                  cid == AV_CODEC_ID_MOV_TEXT || cid == AV_CODEC_ID_TEXT || cid == AV_CODEC_ID_WEBVTT))
            sidxs[nsub++] = (int)i;
        else if (t == AVMEDIA_TYPE_SUBTITLE) unsupported_subtitles++;
    }
    diag_player_event("streams", "enumerated",
                      "total=%u audio=%d subtitle=%d repaired=%d unsupported=%d",
                      fmt->nb_streams, naud, nsub, repaired_subtitles, unsupported_subtitles);
    int native_nsub = nsub;
    int manifest_subtitle_fallback = manifest_subtitle_count > native_nsub;
    if (manifest_subtitle_fallback) {
        nsub = manifest_subtitle_count;
        diag_player_event("subtitle", "manifest-fallback",
                          "master=%d native=%d", manifest_subtitle_count, native_nsub);
    }
    int acur = 0, best_stream = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    int best_audio = 0;
    for (int i = 0; i < naud; i++) if (aidxs[i] == best_stream) best_audio = i;
    
    char pref_aud[32] = ""; store_load_pref_audio(pref_aud, sizeof(pref_aud));
    char pref_sub[32] = ""; store_load_pref_sub(pref_sub, sizeof(pref_sub));
    int scur = -1, sub_chosen = 0, pt_audio = -1, known_audio = 0;
    AudioTrackInfo audio_tracks[AUDIO_POLICY_MAX_TRACKS] = {0};
    char audio_map[192] = "";
    for (int i = 0; i < naud; i++) {
        AVStream *stream = fmt->streams[aidxs[i]];
        AVDictionaryEntry *language = av_dict_get(stream->metadata, "language", NULL, 0);
        const char *track_title = stream_track_title(stream);
        audio_tracks[i].language = language ? language->value : NULL;
        audio_tracks[i].title = track_title;
        audio_tracks[i].is_default = !!(stream->disposition & AV_DISPOSITION_DEFAULT);
        const char *norm = audio_language_normalize(audio_tracks[i].language, audio_tracks[i].title);
        if (norm[0]) known_audio++;
        if (pt_audio < 0 && !strcmp(norm, "pt")) pt_audio = i;
        char part[24];
        snprintf(part, sizeof(part), "%s%d:%s%s",
                 audio_map[0] ? "," : "", i + 1, norm[0] ? norm : "und",
                 audio_tracks[i].is_default ? "*" : "");
        strncat(audio_map, part, sizeof(audio_map) - strlen(audio_map) - 1);
    }
    const char *saved_audio = req->audio_pref_explicit ? NULL : pref_aud;
    acur = audio_policy_choose(audio_tracks, naud, req->audio_pref, saved_audio,
                               req->audio_hint_language, req->audio_hint,
                               req->audio_hint_priority, best_audio);
    if (acur < 0) acur = 0;
    int inferred_dub = -1;
    if (naud == 2 && pt_audio < 0) {
        int english = -1, unknown = -1;
        for (int i = 0; i < naud; i++) {
            AudioLanguageKind kind = audio_language_kind(audio_tracks[i].language,
                                                          audio_tracks[i].title);
            if (kind == AUDIO_KIND_ENGLISH) english = i;
            else if (kind == AUDIO_KIND_UNKNOWN) unknown = i;
        }
        if (english >= 0 && unknown >= 0 &&
            audio_policy_choose(audio_tracks, naud, 0, NULL, NULL, 0, 0,
                                best_audio) == unknown)
            inferred_dub = unknown;
    }
    if (req->subtitle_hint_priority) {
        sub_chosen = 1;
        scur = req->subtitle_hint > 0 && req->subtitle_hint <= nsub
            ? req->subtitle_hint - 1 : -1;
    } else if (pref_sub[0]) {
        if (!strcasecmp(pref_sub, "off")) sub_chosen = 1;
        else {
            const char *want_sub = lang_norm(pref_sub);
            for (int i = 0; want_sub[0] && i < nsub; i++) {
                const char *candidate = manifest_subtitle_fallback
                    ? audio_language_normalize(manifest_subtitles[i].language,
                                               manifest_subtitles[i].name)
                    : stream_norm(fmt, sidxs[i]);
                if (!strcmp(candidate, want_sub)) { scur = i; sub_chosen = 1; break; }
            }
        }
    }
    if (!sub_chosen && naud > 0) {
        const char *audio_norm = stream_norm(fmt, aidxs[acur]);
        if ((audio_norm[0] && strcmp(audio_norm, "pt")) ||
            (req->audio_pref == 1 && pt_audio < 0 && known_audio > 0)) {
            for (int i = 0; i < nsub; i++) {
                const char *candidate = manifest_subtitle_fallback
                    ? audio_language_normalize(manifest_subtitles[i].language,
                                               manifest_subtitles[i].name)
                    : stream_norm(fmt, sidxs[i]);
                if (!strcmp(candidate, "pt")) { scur = i; break; }
            }
        }
    }

    int aidx = naud ? aidxs[acur] : -1;
    g_player_audio_index = naud ? acur + 1 : 0;
    snprintf(g_player_audio_language, sizeof(g_player_audio_language), "%s",
             naud ? (stream_norm(fmt, aidx)[0] ? stream_norm(fmt, aidx) : "und") : "");
    diag_player_event("streams", "selected",
                      "video=%d audio=%d lang=%s pref=%d explicit=%d saved=%s hint=%s/%d priority=%d tracks=%s sub=%d",
                      vidx, aidx, g_player_audio_language, req->audio_pref,
                      req->audio_pref_explicit, saved_audio && saved_audio[0] ? saved_audio : "-",
                      req->audio_hint_language ? req->audio_hint_language : "-",
                      req->audio_hint, req->audio_hint_priority,
                      audio_map[0] ? audio_map : "-", nsub);
    AVCodecContext *sctx = NULL;
    SubtitleQueue subtitles;
    subtitle_queue_reset(&subtitles);
    ExternalSubtitleStore external_subtitles = {0};
    if (scur >= 0 && !manifest_subtitle_fallback &&
        open_sub_dec(fmt, sidxs[scur], &sctx) != 0) scur = -1;
    if (scur >= 0 && manifest_subtitle_fallback &&
        load_external_hls_subtitle(manifest_subtitles[scur].uri,
                                   &external_subtitles) != 0) {
        diag_player_event("subtitle", "manifest-load-fail", "choice=%d", scur + 1);
        scur = -1;
    }
    g_player_subtitle_index = scur >= 0 ? scur + 1 : 0;
    player_select_hls_streams(fmt, vidx, aidx,
                              scur >= 0 && !manifest_subtitle_fallback
                                  ? sidxs[scur] : -1);
    if (native_hls) {
        diag_player_event("hls-io", "tracks", "video=%d audio=%d subtitle=%d",
                          vidx, aidx,
                          scur >= 0 && !manifest_subtitle_fallback ? sidxs[scur] : -1);
    }

    double dur = (fmt->duration > 0) ? fmt->duration / (double)AV_TIME_BASE : 0;
    double timeline_origin = (fmt->start_time != AV_NOPTS_VALUE)
        ? fmt->start_time / (double)AV_TIME_BASE : 0;
    if (out_dur) *out_dur = dur;

    // Metadados do HUD nao mudam durante a tentativa de reproducao. Prepare-os
    // uma unica vez: enumerar streams e montar rotulos a cada quadro causava
    // trabalho desnecessario justamente no caminho mais sensivel a engasgos.
    PlayerHud hud_base = {0};
    char hud_audio_names[16][96] = {{0}};
    char hud_audio_details[16][96] = {{0}};
    char hud_sub_names[17][96] = {{0}};
    double hud_chapters[64] = {0};
    modern_track_labels(fmt, aidxs, naud, sidxs, native_nsub,
                        hud_audio_names, hud_audio_details, hud_sub_names,
                        inferred_dub, &hud_base);
    if (manifest_subtitle_fallback) {
        hud_base.sub_count = nsub + 1;
        for (int i = 0; i < nsub; i++) {
            const char *norm = audio_language_normalize(
                manifest_subtitles[i].language, manifest_subtitles[i].name);
            const char *label = manifest_subtitles[i].name[0]
                ? manifest_subtitles[i].name
                : !strcmp(norm, "pt") ? "Portugues (Brasil)"
                : !strcmp(norm, "en") ? "Ingles"
                : !strcmp(norm, "ja") ? "Japones" : "Legenda";
            snprintf(hud_sub_names[i + 1], sizeof(hud_sub_names[i + 1]),
                     "%.95s", label);
            hud_base.sub_names[i + 1] = hud_sub_names[i + 1];
        }
    }
    hud_base.title = title;
    hud_base.subtitle = req->subtitle;
    hud_base.overview = req->overview;
    hud_base.has_next = req->has_next && req->next_title && req->next_title[0];
    hud_base.next_title = req->next_title;
    hud_base.focus = PUI_FOCUS_PLAY;
    for (unsigned i = 0; i < fmt->nb_chapters && hud_base.chapter_count < 64; i++) {
        double start = fmt->chapters[i]->start * av_q2d(fmt->chapters[i]->time_base)
                     - timeline_origin;
        if (start >= 0 && (dur <= 0 || start <= dur))
            hud_chapters[hud_base.chapter_count++] = start;
    }
    hud_base.chapters = hud_chapters;

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
    external_subtitle_clear(&external_subtitles); \
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
    vctx->pkt_timebase = fmt->streams[vidx]->time_base;
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
        vctx->pkt_timebase = fmt->streams[vidx]->time_base;
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
    int controlled_restart = 0;
    int decoded_video = 0, dropped_video = 0, buffering_events = 0, hardware_decode = 0;
    int slow_reads = 0, present_gaps = 0;
    int read_gaps = 0, sync_gaps = 0, other_gaps = 0;
    int audio_underruns = 0, audio_queue_high_events = 0;
    int audio_queue_failures = 0;
    int audio_queue_ready = 0, audio_queue_high = 0;
    int track_switch_failures = 0;
    unsigned max_track_switch_ms = 0;
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
    int next_selected = 0, next_requested = 0;
    int subtitle_packets = 0, subtitle_cues = 0;
    int audio_switch_pending = 0, subtitle_switch_pending = 0;
    Uint32 audio_switch_started = 0, subtitle_switch_started = 0;
    int timeline_seek = 0, timeline_seek_was_paused = 0, seek_axis_lock = 0;
    int seek_arm_dir = 0;
    double timeline_seek_from = 0, timeline_seek_target = 0;
    Uint32 timeline_seek_tick = SDL_GetTicks(), seek_arm_since = 0;
    Uint32 first_frame_started = SDL_GetTicks();
    Uint32 first_frame_budget_ms = native_hls ? 30000u : 45000u;
    int resume_preroll = 0, resume_preroll_frames = 0;
    double resume_target = 0;
    int post_resume_kind = 0;
    Uint32 post_resume_started = 0;
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
    DemuxWorker demux = {0};
    int demux_started = 0;
    if (running) {
        // A partir daqui o loop principal desenha buffering/video/HUD. O
        // interrupt callback continua cancelando I/O, mas nao pode apresentar
        // o loader antigo por cima de "Aguardando dados" antes do primeiro frame.
        open_watch.loading_owner = PLAYER_LOADING_PLAYBACK;
        if (demux_worker_start(&demux, fmt, &open_watch) == 0) {
            demux_started = 1;
            diag_player_event("demux", "worker-start", "queue=%d cap=%dKB",
                              DEMUX_QUEUE_PACKETS, DEMUX_QUEUE_BYTES / 1024);
        } else {
            player_error_message("Nao foi possivel iniciar a leitura do video");
            playback_error = -4;
            running = 0;
        }
    }
    char playing_stage[96];
    snprintf(playing_stage, sizeof(playing_stage), "09 reproduzindo %s %s%s",
             req->playback.delivery_str[0] ? req->playback.delivery_str : "direto",
             req->container ? req->container : "arquivo",
             sequential_stream ? " continuo" : "");
    player_boot_stage(playing_stage);

    while (running) {
        Uint32 now_ticks = SDL_GetTicks();
        // Uma operacao dentro da sessao nao pode deixar o usuario eternamente
        // olhando o ultimo quadro. Se a nova geracao nao produz video em 20 s,
        // use a reabertura completa ja existente como fallback, preservando a
        // posicao e a faixa escolhida.
        if (logged_first_present && resume_preroll) {
            if (paused) post_resume_started = 0;
            else if (!post_resume_started) post_resume_started = now_ticks;
            else if (now_ticks - post_resume_started >= 20000u) {
                diag_player_event("controls", "inplace-resume-timeout",
                                  "kind=%d target=%.2f", post_resume_kind,
                                  resume_target);
                cur_pos = resume_target;
                controlled_restart = post_resume_kind == PLAYER_RESTART_TRACK
                    ? PLAYER_RESTART_TRACK : PLAYER_RESTART_SEEK;
                running = 0;
                break;
            }
        }
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
        if (adev && aidx >= 0 && logged_first_present && !paused) {
            unsigned queued = SDL_GetQueuedAudioSize(adev);
            unsigned queued_ms = (unsigned)(queued * 1000.0 / bps);
            if (queued_ms >= 150) audio_queue_ready = 1;
            if (audio_queue_ready && queued_ms <= 5 && !buffering_since &&
                !audio_switch_pending) {
                audio_underruns++;
                audio_queue_ready = 0;
                diag_player_event("audio", "underrun", "count=%d pos=%.2f",
                                  audio_underruns, cur_pos);
            }
            if (!audio_queue_high && queued_ms >= 1500) {
                audio_queue_high = 1;
                audio_queue_high_events++;
                diag_player_event("audio", "queue-high", "ms=%u count=%d pos=%.2f",
                                  queued_ms, audio_queue_high_events, cur_pos);
            } else if (audio_queue_high && queued_ms < 1000) {
                audio_queue_high = 0;
            }
        }

        PlayerNextUi next_ui = {0};
        player_next_ui(hud_base.has_next, cur_pos, dur, next_selected, &next_ui);
        hud_base.next_card_alpha = next_ui.alpha;
        hud_base.next_card_progress = next_ui.progress;
        hud_base.focus = next_selected ? PUI_FOCUS_NEXT_CARD : PUI_FOCUS_PLAY;

        while (SDL_PollEvent(&e)) {
            // O painel e uma superficie direta no modo portatil: tocar numa
            // faixa deve executar a mesma operacao transacional do botao A,
            // sem fabricar eventos de Joy-Con nem manter um cursor invisivel.
            int touch_track_button = -1;
            if (e.type == SDL_FINGERDOWN && track_menu) {
                int tx = (int)(e.tfinger.x * PWIN_W);
                int ty = (int)(e.tfinger.y * PWIN_H);
                if (tx < 170 || tx >= 1110 || ty < 96 || ty >= 624) {
                    touch_track_button = JOY_B;
                } else {
                    int touched_menu = 0;
                    if (tx >= 210 && tx < 620) touched_menu = TRACK_MENU_AUDIO;
                    else if (tx >= 660 && tx < 1070) touched_menu = TRACK_MENU_SUB;
                    if (touched_menu && ty >= 234 && ty < 570) {
                        int total = touched_menu == TRACK_MENU_AUDIO ? naud : nsub + 1;
                        int selected = touched_menu == track_menu
                            ? track_sel
                            : (touched_menu == TRACK_MENU_AUDIO ? acur : scur + 1);
                        int first = selected - 3;
                        if (first > total - 6) first = total - 6;
                        if (first < 0) first = 0;
                        int row = first + (ty - 234) / 56;
                        if (row >= 0 && row < total) {
                            track_menu = touched_menu;
                            track_sel = row;
                            touch_track_button = JOY_A;
                        }
                    }
                }
            }
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_JOYBUTTONDOWN || touch_track_button >= 0) {
                int b = touch_track_button >= 0
                    ? touch_track_button : e.jbutton.button;
                // Antes do primeiro quadro, so cancelar faz sentido. Pausa,
                // menus e novo seek poderiam deixar a retomada parada.
                if (native_hls && !have_video_frame &&
                    b != JOY_B && b != JOY_MINUS) continue;
                hud_until = SDL_GetTicks() + 4000;
                if (next_selected) {
                    if (b == JOY_A) {
                        next_requested = 1;
                        if (hb) SDL_AtomicSet(&hb->force_progress, 1);
                        diag_player_event("controls", "next-episode",
                                          "pos=%.2f dur=%.2f", cur_pos, dur);
                        running = 0;
                    } else if (b == JOY_B || b == JOY_MINUS || b == JOY_DLEFT) {
                        next_selected = 0;
                        hud_base.focus = PUI_FOCUS_PLAY;
                        snprintf(notice, sizeof(notice), "Continuando este episodio");
                        notice_until = SDL_GetTicks() + 1500;
                    }
                    continue;
                }
                if (timeline_seek) {
                    if (b == JOY_A) {
                        double target = timeline_seek_target;
                        if (adev) SDL_PauseAudioDevice(adev, 1);
                        int seek_rc = player_seek_with_barrier(
                            &demux, &open_watch, ren, joy, title,
                            "Indo para o ponto escolhido", fmt, vidx, native_hls,
                            vctx, actx, sctx, adev, target, 0, timeline_origin,
                            &wall_start, &audio_clock, &cur_pos, &last_ac,
                            &last_ac_wall, &subtitles);
                        diag_player_event("seek", seek_rc == 0 ? "inplace-ok" :
                                          seek_rc == 1 ? "inplace-cancel" : "inplace-fallback",
                                          "source=timeline rc=%d target=%.2f", seek_rc, target);
                        if (seek_rc == 0) {
                            resume_preroll = 1;
                            resume_preroll_frames = 0;
                            resume_target = target;
                            post_resume_kind = PLAYER_RESTART_SEEK;
                            post_resume_started = 0;
                            audio_queue_ready = audio_queue_high = 0;
                            buffering_since = 0;
                            last_present_tick = 0;
                            snprintf(notice, sizeof(notice), "Reproducao em %.0f%%",
                                     dur > 0 ? target * 100.0 / dur : 0.0);
                            notice_until = SDL_GetTicks() + 1800;
                            if (hb) SDL_AtomicSet(&hb->force_progress, 1);
                        } else if (seek_rc == 1) {
                            snprintf(notice, sizeof(notice), "Busca cancelada");
                            notice_until = SDL_GetTicks() + 1500;
                            double resume_now = av_gettime_relative() / 1000000.0;
                            wall_start = resume_now - cur_pos;
                            if (adev && !timeline_seek_was_paused)
                                SDL_PauseAudioDevice(adev, 0);
                        } else if (seek_rc == 3) {
                            running = 0;
                        } else if (seek_rc == 4) {
                            controlled_restart = PLAYER_RESTART_SEEK;
                            running = 0;
                        } else {
                            cur_pos = target;
                            controlled_restart = PLAYER_RESTART_SEEK;
                            running = 0;
                        }
                        timeline_seek = 0;
                        seek_axis_lock = 1;
                        paused = timeline_seek_was_paused;
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
                    else if (b == JOY_DLEFT && track_menu == TRACK_MENU_SUB && naud > 0) {
                        track_menu = TRACK_MENU_AUDIO; track_sel = acur;
                    }
                    else if (b == JOY_DRIGHT && track_menu == TRACK_MENU_AUDIO && nsub > 0) {
                        track_menu = TRACK_MENU_SUB; track_sel = scur + 1;
                    }
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
                                double switch_pos = cur_pos;
                                Uint32 switch_started = SDL_GetTicks();
                                int barrier = demux_worker_wait_paused(
                                    &demux, ren, joy, title, "Trocando o audio", 5000u);
                                int changed = 0;
                                if (barrier == 0) {
                                    AVCodecContext *next_actx = NULL;
                                    if (create_audio_dec(fmt, next_idx, &next_actx) == 0) {
                                        demux_worker_clear(&demux);
                                        player_select_hls_streams(fmt, vidx, next_idx,
                                            scur >= 0 && !manifest_subtitle_fallback
                                                ? sidxs[scur] : -1);
                                        track_operation_begin(&open_watch, "Trocando o audio",
                                            "Sincronizando a nova faixa...  |  B para cancelar",
                                            12000u);
                                        int seek_rc = apply_player_seek(
                                            fmt, vidx, native_hls, vctx, next_actx, sctx,
                                            adev, cur_pos, 1, timeline_origin, &wall_start,
                                            &audio_clock, &cur_pos, &last_ac, &last_ac_wall,
                                            &subtitles);
                                        int operation = track_operation_end(&open_watch);
                                        if (seek_rc >= 0 && operation == 0) {
                                            if (swr) swr_free(&swr);
                                            avcodec_free_context(&actx);
                                            actx = next_actx;
                                            next_actx = NULL;
                                            acur = track_sel;
                                            aidx = next_idx;
                                            atb = fmt->streams[aidx]->time_base;
                                            swr_rate = 0; swr_fmt = -1; swr_ch = 0;
                                            const char *selected_norm = stream_norm(fmt, aidx);
                                            g_player_audio_index = acur + 1;
                                            snprintf(g_player_audio_language,
                                                     sizeof(g_player_audio_language), "%s",
                                                     selected_norm[0] ? selected_norm : "und");
                                            if (selected_norm[0])
                                                store_save_pref_audio(selected_norm);
                                            resume_preroll = 1;
                                            resume_preroll_frames = 0;
                                            resume_target = cur_pos;
                                            post_resume_kind = PLAYER_RESTART_TRACK;
                                            post_resume_started = 0;
                                            audio_switch_pending = 1;
                                            audio_switch_started = switch_started;
                                            audio_queue_ready = audio_queue_high = 0;
                                            buffering_since = 0;
                                            last_present_tick = 0;
                                            snprintf(notice, sizeof(notice), "Audio %d/%d  %.56s",
                                                     acur + 1, naud, hud_audio_names[acur]);
                                            changed = 1;
                                        } else {
                                            player_select_hls_streams(fmt, vidx, aidx,
                                                scur >= 0 && !manifest_subtitle_fallback
                                                    ? sidxs[scur] : -1);
                                            diag_player_event("tracks", "audio-inplace-fail",
                                                "seek=%d operation=%d from=%d to=%d",
                                                seek_rc, operation, aidx, next_idx);
                                            cur_pos = switch_pos;
                                            controlled_restart = PLAYER_RESTART_SEEK;
                                            running = 0;
                                        }
                                        avcodec_free_context(&next_actx);
                                    }
                                    demux_worker_resume(&demux);
                                }
                                if (barrier == 3) running = 0;
                                if (!changed && running) {
                                    track_switch_failures++;
                                    snprintf(notice, sizeof(notice), barrier == 1
                                             ? "Troca de audio cancelada"
                                             : "Nao consegui trocar o audio agora");
                                    if (adev && !paused) SDL_PauseAudioDevice(adev, 0);
                                } else if (changed) {
                                    diag_player_event("tracks", "audio-inplace-applied",
                                        "stream=%d choice=%d pos=%.2f barrier=%ums",
                                        aidx, acur + 1, cur_pos,
                                        SDL_GetTicks() - switch_started);
                                }
                            }
                        } else {
                            int next = track_sel - 1;
                            if (next == scur) {
                                snprintf(notice, sizeof(notice), "Legenda atual mantida");
                            } else if (manifest_subtitle_fallback) {
                                Uint32 switch_started = SDL_GetTicks();
                                int changed = 0;
                                if (next < 0) {
                                    external_subtitle_clear(&external_subtitles);
                                    scur = -1;
                                    store_save_pref_sub("off");
                                    changed = 1;
                                } else {
                                    pui_draw_loading(ren, title, "Carregando legenda",
                                        "Baixando apenas o texto da faixa escolhida...",
                                        SDL_GetTicks(), 1);
                                    SDL_RenderPresent(ren);
                                    if (load_external_hls_subtitle(
                                            manifest_subtitles[next].uri,
                                            &external_subtitles) == 0) {
                                        scur = next;
                                        const char *norm = audio_language_normalize(
                                            manifest_subtitles[scur].language,
                                            manifest_subtitles[scur].name);
                                        if (norm[0]) store_save_pref_sub(norm);
                                        changed = 1;
                                    }
                                }
                                if (changed) {
                                    g_player_subtitle_index = scur + 1;
                                    if (scur < 0)
                                        snprintf(notice, sizeof(notice),
                                                 "Legendas desativadas");
                                    else
                                        snprintf(notice, sizeof(notice),
                                                 "Legenda %d/%d  %.54s",
                                                 scur + 1, nsub,
                                                 hud_sub_names[scur + 1]);
                                    diag_player_event("tracks", "subtitle-manifest-applied",
                                        "choice=%d cues=%d ms=%u", scur + 1,
                                        external_subtitles.count,
                                        SDL_GetTicks() - switch_started);
                                } else {
                                    track_switch_failures++;
                                    snprintf(notice, sizeof(notice),
                                             "Nao consegui carregar esta legenda");
                                    diag_player_event("tracks", "subtitle-manifest-fail",
                                        "choice=%d ms=%u", next + 1,
                                        SDL_GetTicks() - switch_started);
                                }
                            } else {
                                double switch_pos = cur_pos;
                                Uint32 switch_started = SDL_GetTicks();
                                int barrier = demux_worker_wait_paused(
                                    &demux, ren, joy, title, "Trocando a legenda", 5000u);
                                int changed = 0;
                                if (barrier == 0) {
                                    AVCodecContext *next_sctx = NULL;
                                    int decoder_ok = next < 0 ||
                                        create_sub_dec(fmt, sidxs[next], &next_sctx) == 0;
                                    if (decoder_ok) {
                                        int seek_rc = 0, operation = 0;
                                        player_select_hls_streams(fmt, vidx, aidx,
                                            next >= 0 ? sidxs[next] : -1);
                                        if (next >= 0) {
                                            demux_worker_clear(&demux);
                                            track_operation_begin(&open_watch,
                                                "Trocando a legenda",
                                                "Sincronizando a nova faixa...  |  B para cancelar",
                                                12000u);
                                            seek_rc = apply_player_seek(
                                                fmt, vidx, native_hls, vctx, actx, next_sctx,
                                                adev, cur_pos, 1, timeline_origin, &wall_start,
                                                &audio_clock, &cur_pos, &last_ac, &last_ac_wall,
                                                &subtitles);
                                            operation = track_operation_end(&open_watch);
                                        } else {
                                            subtitle_queue_reset(&subtitles);
                                        }
                                        if (seek_rc >= 0 && operation == 0) {
                                            avcodec_free_context(&sctx);
                                            sctx = next_sctx;
                                            next_sctx = NULL;
                                            scur = next;
                                            g_player_subtitle_index = scur + 1;
                                            if (scur < 0) store_save_pref_sub("off");
                                            else {
                                                const char *norm = stream_norm(fmt, sidxs[scur]);
                                                if (norm[0]) store_save_pref_sub(norm);
                                                resume_preroll = 1;
                                                resume_preroll_frames = 0;
                                                resume_target = cur_pos;
                                                post_resume_kind = PLAYER_RESTART_TRACK;
                                                post_resume_started = 0;
                                                subtitle_switch_pending = 1;
                                                subtitle_switch_started = switch_started;
                                                if (adev) SDL_PauseAudioDevice(adev, 1);
                                                audio_queue_ready = audio_queue_high = 0;
                                                buffering_since = 0;
                                                last_present_tick = 0;
                                            }
                                            if (scur < 0)
                                                snprintf(notice, sizeof(notice),
                                                         "Legendas desativadas");
                                            else
                                                snprintf(notice, sizeof(notice),
                                                         "Legenda %d/%d  %.54s",
                                                         scur + 1, nsub,
                                                         hud_sub_names[scur + 1]);
                                            changed = 1;
                                        } else {
                                            player_select_hls_streams(fmt, vidx, aidx,
                                                scur >= 0 && !manifest_subtitle_fallback
                                                    ? sidxs[scur] : -1);
                                            diag_player_event("tracks", "subtitle-inplace-fail",
                                                "seek=%d operation=%d from=%d to=%d",
                                                seek_rc, operation, scur, next);
                                            cur_pos = switch_pos;
                                            controlled_restart = PLAYER_RESTART_SEEK;
                                            running = 0;
                                        }
                                        avcodec_free_context(&next_sctx);
                                    }
                                    demux_worker_resume(&demux);
                                }
                                if (barrier == 3) running = 0;
                                if (!changed && running) {
                                    track_switch_failures++;
                                    snprintf(notice, sizeof(notice), barrier == 1
                                             ? "Troca de legenda cancelada"
                                             : "Nao consegui trocar a legenda agora");
                                    if (adev && !paused) SDL_PauseAudioDevice(adev, 0);
                                } else if (changed) {
                                    diag_player_event("tracks", "subtitle-inplace-applied",
                                        "choice=%d pos=%.2f barrier=%ums",
                                        scur + 1, cur_pos,
                                        SDL_GetTicks() - switch_started);
                                }
                            }
                        }
                        notice_until = SDL_GetTicks() + 2200;
                        track_menu = 0;
                        if (!audio_switch_pending && !subtitle_switch_pending) {
                            double resume_now = av_gettime_relative() / 1000000.0;
                            wall_start = resume_now - cur_pos;
                            audio_clock = cur_pos; last_ac = -1;
                            last_ac_wall = resume_now;
                            if (adev && !paused) SDL_PauseAudioDevice(adev, 0);
                        }
                    }
                    continue;
                }
                if (b == JOY_B || b == JOY_MINUS) running = 0;
                else if (b == JOY_DRIGHT && hud_base.has_next) {
                    next_selected = 1;
                    hud_base.focus = PUI_FOCUS_NEXT_CARD;
                    hud_base.next_card_alpha = 1.0f;
                    hud_until = SDL_GetTicks() + 8000;
                }
                else if (b == JOY_PLUS) hud_pinned = !hud_pinned;
                else if (b == JOY_A) {
                    paused = !paused;
                    diag_player_event("controls", paused ? "pause" : "resume",
                                      "pos=%.2f audioq=%u", cur_pos,
                                      adev ? SDL_GetQueuedAudioSize(adev) : 0);
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
                    // Nao dispare uma requisicao HLS por toque. Abra o mesmo
                    // scrub confirmado do analogico: o usuario pode acumular
                    // quantos saltos quiser e A executa um unico seek.
                    timeline_seek = 1;
                    timeline_seek_was_paused = paused;
                    timeline_seek_from = cur_pos;
                    timeline_seek_target = t;
                    timeline_seek_tick = SDL_GetTicks();
                    seek_axis_lock = 1;
                    if (adev) SDL_PauseAudioDevice(adev, 1);
                    snprintf(notice, sizeof(notice),
                             "A confirma  |  B cancela  |  L/R ajusta");
                    notice_until = SDL_GetTicks() + 4000;
                    diag_player_event("seek", "preview-open",
                                      "source=button target=%.2f", t);
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
                if (hud_base.has_next && hud_base.next_card_alpha > 0.01f &&
                    tx >= 872 && tx < 1232 && ty >= 432 && ty < 550) {
                    next_requested = 1;
                    if (hb) SDL_AtomicSet(&hb->force_progress, 1);
                    diag_player_event("controls", "next-episode-touch",
                                      "pos=%.2f dur=%.2f", cur_pos, dur);
                    running = 0;
                } else if (hud_base.has_next && ty >= 620 && tx >= 1195) {
                    next_selected = 1;
                    hud_base.focus = PUI_FOCUS_NEXT_CARD;
                    hud_base.next_card_alpha = 1.0f;
                    hud_until = SDL_GetTicks() + 8000;
                } else if (!sequential_stream && dur > 1 && ty >= 580 && ty < 635 &&
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
                } else if (ty >= 620 && tx >= 1090 && tx < 1195 &&
                           (naud > 1 || nsub > 0)) {
                    // O HUD moderno possui uma unica acao de faixas no canto
                    // direito. O mapeamento antigo tinha dois botoes invisiveis
                    // nessa regiao e fazia o toque alternar o painel fixo.
                    track_menu = naud > 1 ? TRACK_MENU_AUDIO : TRACK_MENU_SUB;
                    track_sel = track_menu == TRACK_MENU_AUDIO ? acur : scur + 1;
                    if (adev && !paused) SDL_PauseAudioDevice(adev, 1);
                }
                if (touch_seek) {
                    if (target < 0) target = 0;
                    if (target > dur - 1) target = dur - 1;
                    if (adev) SDL_PauseAudioDevice(adev, 1);
                    int seek_rc = player_seek_with_barrier(
                        &demux, &open_watch, ren, joy, title,
                        "Indo para o ponto escolhido", fmt, vidx, native_hls,
                        vctx, actx, sctx, adev, target, 0, timeline_origin,
                        &wall_start, &audio_clock, &cur_pos, &last_ac,
                        &last_ac_wall, &subtitles);
                    diag_player_event("seek", seek_rc == 0 ? "inplace-ok" :
                                      seek_rc == 1 ? "inplace-cancel" : "inplace-fallback",
                                      "source=touch rc=%d target=%.2f", seek_rc, target);
                    if (seek_rc == 0) {
                        resume_preroll = 1;
                        resume_preroll_frames = 0;
                        resume_target = target;
                        post_resume_kind = PLAYER_RESTART_SEEK;
                        post_resume_started = 0;
                        audio_queue_ready = audio_queue_high = 0;
                        buffering_since = 0;
                        last_present_tick = 0;
                        if (hb) SDL_AtomicSet(&hb->force_progress, 1);
                    } else if (seek_rc == 1) {
                        double resume_now = av_gettime_relative() / 1000000.0;
                        wall_start = resume_now - cur_pos;
                        if (adev && !paused) SDL_PauseAudioDevice(adev, 0);
                    } else if (seek_rc == 3) {
                        running = 0;
                    } else if (seek_rc == 4) {
                        controlled_restart = PLAYER_RESTART_SEEK;
                        running = 0;
                    } else {
                        cur_pos = target;
                        controlled_restart = PLAYER_RESTART_SEEK;
                        running = 0;
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
            char scrub_label[160] = "";
            chapter_at(fmt, timeline_seek_target, timeline_origin,
                       scrub_label, sizeof(scrub_label));
            const char *active_subtitle = active_subtitle_text(
                scur, manifest_subtitle_fallback, &subtitles,
                &external_subtitles, timeline_seek_from);
            draw_hud(ren, &hud_base, timeline_seek_from, dur, 1, vol,
                     acur, scur, !sequential_stream, NULL,
                     timeline_seek_target, scrub_label[0] ? scrub_label : NULL,
                     active_subtitle);
            SDL_RenderPresent(ren);
            SDL_Delay(16);
            continue;
        }
        if (track_menu) {
            last_present_tick = 0;
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
            if (have_video_frame) SDL_RenderCopy(ren, tex, NULL, &dst);
            const char *active_subtitle = active_subtitle_text(
                scur, manifest_subtitle_fallback, &subtitles,
                &external_subtitles, cur_pos);
            draw_modern_track_menu(ren, &hud_base, track_menu, track_sel,
                                   acur, scur, active_subtitle);
            SDL_RenderPresent(ren);
            SDL_Delay(30);
            continue;
        }
        if (paused) {   // continua desenhando (quadro congelado + HUD)
            last_present_tick = 0;
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
            if (have_video_frame) SDL_RenderCopy(ren, tex, NULL, &dst);
            const char *active_subtitle = active_subtitle_text(
                scur, manifest_subtitle_fallback, &subtitles,
                &external_subtitles, cur_pos);
            draw_hud(ren, &hud_base, cur_pos, dur, 1, vol,
                     acur, scur, !sequential_stream, NULL, -1, NULL,
                     active_subtitle);
            if (SDL_GetTicks() < notice_until) draw_notice(ren, notice);
            SDL_RenderPresent(ren);
            SDL_Delay(30);
            continue;
        }

        Uint32 audio_before_ms = adev ? (Uint32)(SDL_GetQueuedAudioSize(adev) * 1000.0 / bps) : 0;
        Uint32 read_ms = 0;
        int take = demux_worker_take(&demux, pkt, &read_ms);
        int ret = take == 1 ? 0 : take == 0 ? AVERROR(EAGAIN) : take;
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
        // A leitura ocorre na thread de demux. O tempo gasto nela nao parou o
        // relogio/renderizador e portanto nao pode mais ser somado a wall_start.
        // Fazer isso deslocaria o video para tras a cada segmento lento.
        read_since_present_ms += read_ms;
        if (read_ms > worst_read_ms) worst_read_ms = read_ms;
        if (take == 1 && read_ms >= 250) {
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
                if (audio_switch_pending)
                    snprintf(dots, sizeof(dots), "Sincronizando audio%.*s", ndots, "...");
                else if (subtitle_switch_pending)
                    snprintf(dots, sizeof(dots), "Ativando legendas%.*s", ndots, "...");
                else if (stalled < 8000) snprintf(dots, sizeof(dots), "Aguardando dados%.*s", ndots, "...");
                else if (stalled < 30000) snprintf(dots, sizeof(dots), "Tentando reconectar%.*s", ndots, "...");
                else snprintf(dots, sizeof(dots), "Conexao lenta  |  B para voltar");
                const char *active_subtitle = active_subtitle_text(
                    scur, manifest_subtitle_fallback, &subtitles,
                    &external_subtitles, cur_pos);
                draw_hud(ren, &hud_base, cur_pos, dur, 0, vol,
                         acur, scur, !sequential_stream, dots, -1, NULL,
                         active_subtitle);
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
                            if (SDL_QueueAudio(adev, audio_buf, n * OCH * 2) < 0) {
                                audio_queue_failures++;
                                diag_player_event("audio", "queue-fail", "count=%d error=%.80s",
                                                  audio_queue_failures, SDL_GetError());
                                player_error_message("Falha ao enviar audio para o console");
                                playback_error = -4;
                                running = 0;
                                break;
                            }
                            if (audio_switch_pending) {
                                unsigned switch_ms = SDL_GetTicks() - audio_switch_started;
                                if (switch_ms > max_track_switch_ms)
                                    max_track_switch_ms = switch_ms;
                                audio_switch_pending = 0;
                                if (!subtitle_switch_pending) {
                                    open_watch.force_loading = 0;
                                    open_watch.headline = NULL;
                                }
                                diag_player_event("tracks", "audio-switch-ready",
                                                  "ms=%u pts=%.2f queue=%u",
                                                  switch_ms,
                                                  audio_pts, SDL_GetQueuedAudioSize(adev));
                            }
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
                        // A fila permaneceu pausada durante a operacao. Libere-a
                        // somente quando o primeiro quadro da nova geracao ja
                        // estiver pronto, evitando audio antigo ou tela congelada.
                        if (adev && !paused) SDL_PauseAudioDevice(adev, 0);
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
                        Uint32 wait_until = sync_started + (Uint32)(delay * 1000);
                        while (!SDL_TICKS_PASSED(SDL_GetTicks(), wait_until)) {
                            Uint32 left = wait_until - SDL_GetTicks();
                            SDL_Delay(left > 8 ? 8 : left);
                            SDL_PumpEvents();
                            if (SDL_PeepEvents(NULL, 0, SDL_PEEKEVENT,
                                               SDL_JOYBUTTONDOWN, SDL_JOYBUTTONDOWN) > 0) break;
                        }
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
                    if (subtitle_switch_pending) {
                        unsigned switch_ms = SDL_GetTicks() - subtitle_switch_started;
                        if (switch_ms > max_track_switch_ms)
                            max_track_switch_ms = switch_ms;
                        subtitle_switch_pending = 0;
                        if (!audio_switch_pending) {
                            open_watch.force_loading = 0;
                            open_watch.headline = NULL;
                        }
                        diag_player_event("tracks", "subtitle-switch-ready",
                                          "ms=%u pos=%.2f",
                                          switch_ms, cur_pos);
                    }
                    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
                    SDL_RenderCopy(ren, tex, NULL, &dst);
                    const char *active_subtitle = active_subtitle_text(
                        scur, manifest_subtitle_fallback, &subtitles,
                        &external_subtitles, cur_pos);
                    if (hud_pinned || SDL_GetTicks() < hud_until) {
                        draw_hud(ren, &hud_base, cur_pos, dur, 0, vol,
                                 acur, scur, !sequential_stream, NULL, -1, NULL,
                                 active_subtitle);
                    } else {
                        draw_subtitle_overlay(ren, &hud_base, active_subtitle);
                    }
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
            AVSubtitle sub = {0}; int got = 0;
            subtitle_packets++;
            if (avcodec_decode_subtitle2(sctx, &sub, &got, pkt) >= 0 && got) {
                char cue_text[SUBTITLE_TEXT_CAP] = "";
                for (unsigned r = 0; r < sub.num_rects; r++) {
                    AVSubtitleRect *rc = sub.rects[r]; char tmp[400] = "";
                    if (rc->type == SUBTITLE_ASS && rc->ass) ass_to_text(rc->ass, tmp, sizeof(tmp));
                    else if (rc->type == SUBTITLE_TEXT && rc->text) snprintf(tmp, sizeof(tmp), "%s", rc->text);
                    if (tmp[0]) {
                        size_t rem = sizeof(cue_text) - strlen(cue_text) - 1;
                        if (cue_text[0] && rem > 1) {
                            strncat(cue_text, "\n", rem);
                            rem--;
                        }
                        strncat(cue_text, tmp, rem);
                    }
                }
                AVRational stb = fmt->streams[sidxs[scur]]->time_base;
                double decoded_pts = sub.pts != AV_NOPTS_VALUE
                    ? sub.pts / (double)AV_TIME_BASE : NAN;
                double packet_pts = pkt->pts != AV_NOPTS_VALUE
                    ? pkt->pts * av_q2d(stb) : NAN;
                double packet_duration = pkt->duration > 0
                    ? pkt->duration * av_q2d(stb) : 0.0;
                double cue_start = 0, cue_end = 0;
                subtitle_cue_times(decoded_pts, packet_pts, packet_duration,
                                   sub.start_display_time, sub.end_display_time,
                                   timeline_origin, &cue_start, &cue_end);
                if (cue_text[0]) {
                    subtitle_queue_push(&subtitles, cue_start, cue_end, cue_text);
                    subtitle_cues++;
                    if (subtitle_cues == 1)
                        diag_player_event("subtitle", "first-cue",
                                          "stream=%d start=%.2f end=%.2f now=%.2f chars=%u",
                                          sidxs[scur], cue_start, cue_end, cur_pos,
                                          (unsigned)strlen(cue_text));
                }
                avsubtitle_free(&sub);
            }
        }
        av_packet_unref(pkt);
    }

    if (demux_started) {
        demux_worker_stop(&demux);
        diag_player_event("demux", "worker-stop", "restart=%d", controlled_restart);
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
    if (audio_switch_pending || subtitle_switch_pending) {
        Uint32 started = audio_switch_pending ? audio_switch_started : subtitle_switch_started;
        unsigned elapsed = SDL_GetTicks() - started;
        if (elapsed > max_track_switch_ms) max_track_switch_ms = elapsed;
        track_switch_failures++;
    }
    store_save_player_volume(vol);
    diag_player_event("player", "timing", "open=%u probe=%u first=%u gap=io%d/sync%d/other%d",
                      open_elapsed_ms, probe_elapsed_ms, first_present_ms,
                      read_gaps, sync_gaps, other_gaps);
    store_save_player_stats(vw, vh, decoded_video, dropped_video,
                            buffering_events, max_audio_queue, playback_error,
                            hardware_decode, slow_reads, worst_read_ms,
                            present_gaps, worst_present_ms,
                            read_gaps, sync_gaps, other_gaps,
                            open_elapsed_ms, probe_elapsed_ms, first_present_ms,
                            audio_underruns, audio_queue_high_events,
                            track_switch_failures, max_track_switch_ms);
    diag_player_event("player", "cleanup-begin", "pos=%.1f frames=%d drop=%d waits=%d max=%ums gaps=%d hw=%d err=%d",
                      cur_pos, decoded_video, dropped_video, slow_reads,
                      worst_read_ms, present_gaps, hardware_decode, playback_error);
    diag_player_event("audio", "queue-summary",
                      "failures=%d maxBytes=%u underruns=%d",
                      audio_queue_failures, max_audio_queue, audio_underruns);
    diag_player_event("subtitle", "summary", "selected=%d packets=%d cues=%d",
                      scur + 1, subtitle_packets, subtitle_cues);

    if (adev) SDL_CloseAudioDevice(adev);
    if (sws) sws_freeContext(sws);
    if (yuv) av_frame_free(&yuv);
    if (transfer) av_frame_free(&transfer);
    if (swr) swr_free(&swr);
    av_freep(&audio_buf);
    if (actx) avcodec_free_context(&actx);
    if (sctx) avcodec_free_context(&sctx);
    external_subtitle_clear(&external_subtitles);
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
    return next_requested ? PLAYER_REQUEST_NEXT :
           controlled_restart ? controlled_restart :
           playback_error ? playback_error : reached_end;
}



static int playback_heartbeat_thread(void *userdata) {
    PlaybackHeartbeat *hb = (PlaybackHeartbeat *)userdata;
    int elapsed_ms = 0, progress_ms = 0;
    int last_saved_pos = -1;
    while (SDL_AtomicGet(&hb->running)) {
        SDL_Delay(100);
        if (!SDL_AtomicGet(&hb->running)) break;
        if (!SDL_AtomicGet(&hb->pipeline_ready)) {
            elapsed_ms = 0;
            progress_ms = 0;
            continue;
        }
        elapsed_ms += 100;
        progress_ms += 100;

        if (elapsed_ms >= 20000) {
            int session_id = SDL_AtomicGet(&hb->session_id);
            if (session_id > 0 && hb->heartbeat_cb)
                hb->heartbeat_cb(session_id, &hb->cancel_io, hb->callback_userdata);
            elapsed_ms = 0;
        }

        if (progress_ms >= 15000 || SDL_AtomicCAS(&hb->force_progress, 1, 0)) {
            int pos = SDL_AtomicGet(&hb->current_pos);
            int dur = SDL_AtomicGet(&hb->duration);
            if (hb->progress_cb && pos > 5 &&
                hb->progress_cb(hb->item_id, pos, dur, &hb->cancel_io,
                                hb->callback_userdata) == 0)
                last_saved_pos = pos;
            progress_ms = 0;
        }
    }
    int final_pos = SDL_AtomicGet(&hb->current_pos);
    int final_dur = SDL_AtomicGet(&hb->duration);
    if (SDL_AtomicGet(&hb->final_progress) && hb->progress_cb &&
        player_sync_final_progress_needed(1, final_pos, last_saved_pos)) {
        if (hb->progress_cb(hb->item_id, final_pos, final_dur, &hb->cancel_io,
                            hb->callback_userdata) == 0)
            last_saved_pos = final_pos;
    }
    int session_id = SDL_AtomicGet(&hb->session_id);
    if (session_id > 0 && hb->stop_cb)
        hb->stop_cb(hb->item_id, session_id, &hb->cancel_io,
                    hb->callback_userdata);
    (void)last_saved_pos;
    SDL_AtomicSet(&hb->finished, 1);
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
    SDL_AtomicSet(&hb.final_progress, 0);
    SDL_AtomicSet(&hb.cancel_io, 0);
    SDL_AtomicSet(&hb.finished, 0);
    hb.progress_cb = request->progress_cb;
    hb.heartbeat_cb = request->heartbeat_cb;
    hb.stop_cb = request->stop_cb;
    hb.callback_userdata = request->userdata;
    
    SDL_Thread *heartbeat = NULL;
    SDL_AtomicSet(&hb.running, 1);
    heartbeat = SDL_CreateThread(playback_heartbeat_thread, "play-heartbeat", &hb);

    int retry_count = 0, total_recoveries = 0;
    double current_pos = request->start_sec;
    double attempt_start = current_pos;
    int resume_restart_attempted = 0;
    double dur = 0.0;
    int ever_presented_frame = 0;
    int final_rc = 0;
    int last_audio = 0;
    int last_audio_priority = 0;
    int last_subtitle = 0;
    int last_subtitle_priority = 0;
    int controlled_restarts = 0;
    char last_audio_language[8] = "";
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
        if (last_audio > 0) attempt.audio_hint = last_audio;
        if (last_audio_language[0]) attempt.audio_hint_language = last_audio_language;
        if (last_audio > 0 || last_audio_language[0])
            attempt.audio_hint_priority = last_audio_priority ? last_audio_priority : 1;
        if (last_subtitle_priority) {
            attempt.subtitle_hint = last_subtitle;
            attempt.subtitle_hint_priority = 1;
        }
        g_player_audio_index = 0;
        g_player_audio_language[0] = '\0';
        g_player_subtitle_index = 0;
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
        if (g_player_audio_index > 0) last_audio = g_player_audio_index;
        if (g_player_audio_language[0])
            snprintf(last_audio_language, sizeof(last_audio_language), "%s", g_player_audio_language);
        last_subtitle = g_player_subtitle_index;

        if (rc == PLAYER_RESTART_SEEK || rc == PLAYER_RESTART_TRACK) {
            current_pos = out_pos;
            attempt_start = current_pos;
            controlled_restarts++;
            // O usuario so consegue pedir seek/faixa depois de uma pipeline
            // funcional. Falhas antigas nao podem consumir para sempre o
            // orcamento de recuperacao de uma sessao longa.
            retry_count = 0;
            resume_restart_attempted = 0;
            if (rc == PLAYER_RESTART_TRACK) {
                last_audio_priority = 2;
                last_subtitle_priority = 1;
            }
            diag_player_event("player", "controlled-restart",
                              "kind=%s generation=%d pos=%.2f audio=%d sub=%d",
                              rc == PLAYER_RESTART_SEEK ? "seek" : "track",
                              controlled_restarts, current_pos, last_audio, last_subtitle);
            pui_draw_loading(ren, request->title,
                             rc == PLAYER_RESTART_SEEK ? "Indo para o ponto escolhido" :
                                                        "Aplicando audio e legendas",
                             "Reabrindo o video com seguranca...  |  B para cancelar",
                             SDL_GetTicks(), 1);
            SDL_RenderPresent(ren);
            continue;
        }

        if (rc < 0 && rc != -11 && resume_seeked && !presented_frame &&
            !resume_restart_attempted && attempt_start > 3 &&
            active.container[0] && !strcmp(active.container, "m3u8")) {
            resume_restart_attempted = 1;
            attempt_start = 0;
            diag_player_event("recover", "resume-from-start",
                              "rc=%d saved=%.1f", rc, current_pos);
            pui_draw_loading(ren, request->title, "Retomada indisponivel",
                             "Abrindo o video desde o inicio...", SDL_GetTicks(), 1);
            SDL_RenderPresent(ren);
            continue;
        }

        if (rc == PLAYER_REQUEST_NEXT) {
            result->reason = EXIT_REASON_NEXT_EPISODE;
            result->final_state = PLAYER_FINISHED;
            final_rc = rc;
            break;
        } else if (rc == 1) { // Terminou naturalmente somente se houve video
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
            if (presented_frame && out_pos >= attempt_start + 30.0) {
                diag_player_event("recover", "stable-window-reset",
                                  "played=%.1f oldRetry=%d", out_pos - attempt_start,
                                  retry_count);
                retry_count = 0;
            }
            int startup_failure = !presented_frame;
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
                char detail[96];
                if (use_fallback) snprintf(detail, sizeof(detail), "A fonte atual nao respondeu. Buscando alternativa...");
                else snprintf(detail, sizeof(detail), "Reconectando com seguranca...  %d/%d", renew_try + 1, max_renew_tries);
                pui_draw_loading(ren, request->title,
                                 use_fallback ? "Tentando outra fonte" : "Recuperando sessao",
                                 detail, SDL_GetTicks(), 1);
                SDL_RenderPresent(ren);

                int recovery_rc = player_recovery_call(
                    ren, joy, request->title,
                    use_fallback ? "Tentando outra fonte" : "Recuperando sessao",
                    detail, recovery_cb, &active, &renewed, request->userdata);
                if (recovery_rc == 0) {
                    diag_player_event("recover", "resolve-ok", "try=%d session=%d source=%d",
                                      renew_try + 1, renewed.session_id, renewed.source_id);
                    renewed_ok = 1;
                    break;
                }
                if (recovery_rc == 1) {
                    recovery_cancelled = 1;
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
                    pui_draw_loading(ren, request->title, "Recuperando sessao",
                                     detail, SDL_GetTicks(), 1);
                    SDL_RenderPresent(ren);
                    SDL_Delay(33);
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
            total_recoveries++;
            result->recovery_count = total_recoveries;
            continue;
        }
    }

    result->position = current_pos;
    result->duration = dur;
    result->audio_index = last_audio;
    snprintf(result->audio_language, sizeof(result->audio_language), "%s", last_audio_language);
    result->subtitle_index = last_subtitle;
    result->presented_frame = ever_presented_frame;

    SDL_AtomicSet(&hb.current_pos, (int)current_pos);
    SDL_AtomicSet(&hb.duration, (int)dur);
    SDL_AtomicSet(&hb.final_progress, ever_presented_frame ? 1 : 0);

    // Uma falha rara ao criar a thread no inicio nao deve vazar a sessao nem
    // perder todo o progresso. Tente uma thread curta apenas para o fechamento.
    if (!heartbeat && (hb.progress_cb || hb.stop_cb)) {
        SDL_AtomicSet(&hb.running, 0);
        heartbeat = SDL_CreateThread(playback_heartbeat_thread,
                                     "play-sync-exit", &hb);
    }
    if (heartbeat) {
        SDL_AtomicSet(&hb.running, 0);
        Uint32 sync_started = SDL_GetTicks();
        while (!SDL_AtomicGet(&hb.finished) &&
               !player_sync_exit_should_cancel(SDL_GetTicks() - sync_started, 0))
            SDL_Delay(10);
        if (!SDL_AtomicGet(&hb.finished)) {
            diag_player_event("sync", "exit-cancel",
                              "waited=%u pos=%d", SDL_GetTicks() - sync_started,
                              SDL_AtomicGet(&hb.current_pos));
            SDL_AtomicSet(&hb.cancel_io, 1);
        }
        SDL_WaitThread(heartbeat, NULL);
        diag_player_event("sync", "exit-finished", "ms=%u",
                          SDL_GetTicks() - sync_started);
    } else {
        diag_player_event("sync", "thread-unavailable", NULL);
    }

    if (final_rc < 0)
        diag_player_event("player", "final-error", "rc=%d %s", final_rc,
                          g_player_last_error[0] ? g_player_last_error : "falha sem detalhe");
    diag_player_finish(final_rc);
    ui_popcorn_release();
    nplay_curl_avio_pool_clear();

    return final_rc;
}
