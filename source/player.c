// player.c - player de video: ffmpeg decodifica, SDL desenha (textura YUV) e toca
// o audio (SDL Audio + swresample). Sincroniza o video pelo relogio do audio.
// MP4/MKV remoto usa HTTPS nativo. HLS usa callbacks AVIO com libcurl para abrir
// cada playlist e segmento sem depender do TLS interno do FFmpeg/libnx.
// Retoma de onde parou (start_sec), reporta a posicao (out_pos/out_dur). O HUD
// segue o player da versao PC: controles focaveis, timeline, painel de audio e
// legendas e proximo episodio (desenho em player_ui.c).
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
#include "player_ui.h"

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

typedef struct {
    int64_t deadline_us;
    SDL_Joystick *joy;
    int cancelled;
    // Enquanto FFmpeg abre a fonte, o callback tambem anima o loader. Fica NULL
    // depois da abertura: o callback continua instalado durante a reproducao.
    SDL_Renderer *ren;
    const char *title, *headline, *detail;
    Uint32 last_draw;
} PlayerOpenDeadline;

static int player_open_interrupted(void *userdata) {
    PlayerOpenDeadline *watch = (PlayerOpenDeadline *)userdata;
    if (!watch) return 0;
    // avformat_open_input/find_stream_info sao sincronas. Bombeie o controle
    // dentro do callback de interrupcao para B/- realmente funcionarem mesmo
    // enquanto FFmpeg espera rede ou uma rendition HLS.
    SDL_PumpEvents();
    if (watch->ren) {
        Uint32 now = SDL_GetTicks();
        if ((Uint32)(now - watch->last_draw) >= 60) {
            watch->last_draw = now;
            pui_draw_loading(watch->ren, watch->title, watch->headline, watch->detail, now, 0);
            SDL_RenderPresent(watch->ren);
        }
    }
    if (watch->joy && (SDL_JoystickGetButton(watch->joy, JOY_B) ||
                       SDL_JoystickGetButton(watch->joy, JOY_MINUS))) {
        watch->cancelled = 1;
        return 1;
    }
    return watch->deadline_us > 0 && av_gettime_relative() >= watch->deadline_us;
}

// O HTTPS nativo do FFmpeg/libnx abre o master R2, mas no hardware pode ficar
// preso ao abrir as playlists/segmentos seguintes. O demuxer HLS chama io_open
// para cada recurso aninhado; entregue todos ao libcurl, a mesma pilha TLS usada
// com sucesso pela API e pelas capas do aplicativo.
// Cada segmento HLS passa por io_open. Registrar todos gravava varias linhas na
// microSD por segmento, na mesma thread que decodifica e desenha. Os primeiros
// recursos (abertura) e qualquer falha continuam no trace.
#define HLS_TRACE_DETAILED 24
static int g_hls_io_opened = 0;

static int player_hls_io_open(AVFormatContext *fmt, AVIOContext **pb,
                              const char *url, int flags, AVDictionary **options) {
    (void)fmt;
    (void)options;
    if (!pb || !url || (flags & AVIO_FLAG_WRITE)) return AVERROR(EINVAL);
    *pb = NULL;
    if (strncmp(url, "http://", 7) && strncmp(url, "https://", 8))
        return AVERROR_PROTOCOL_NOT_FOUND;
    int active = 0, reserved_kb = 0;
    char stage[96];
    int detailed = g_hls_io_opened++ < HLS_TRACE_DETAILED;
    nplay_curl_avio_stats(&active, &reserved_kb);
    if (detailed) {
        snprintf(stage, sizeof(stage), "03 HLS abrindo recurso %d (%d KB)", active + 1, reserved_kb);
        player_boot_stage(stage);
        diag_player_event("hls-io", "open-begin", "active=%d reserved=%dKB", active, reserved_kb);
    }
    *pb = nplay_curl_avio_open_hls(url);
    nplay_curl_avio_stats(&active, &reserved_kb);
    if (detailed || !*pb) {
        snprintf(stage, sizeof(stage), *pb ? "03 HLS ativo: %d recursos, %d KB"
                                          : "03 HLS sem memoria: %d recursos, %d KB",
                 active, reserved_kb);
        player_boot_stage(stage);
        diag_player_event("hls-io", *pb ? "open-ok" : "open-fail",
                          "active=%d reserved=%dKB", active, reserved_kb);
    }
    return *pb ? 0 : AVERROR(ENOMEM);
}

static int player_hls_io_close(AVFormatContext *fmt, AVIOContext *pb) {
    (void)fmt;
    if (g_hls_io_opened <= HLS_TRACE_DETAILED)
        diag_player_event("hls-io", "close", "pb=%s", pb ? "yes" : "no");
    nplay_curl_avio_close(pb);
    return 0;
}

static enum AVPixelFormat player_select_video_format(AVCodecContext *ctx,
                                                       const enum AVPixelFormat *formats) {
    for (const enum AVPixelFormat *it = formats; it && *it != AV_PIX_FMT_NONE; it++) {
        if (*it == AV_PIX_FMT_NVTEGRA) return *it;
    }
    return avcodec_default_get_format(ctx, formats);
}

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

static const char *lang_name(const char *lang) {
    const char *code = lang_label(lang);
    if (!strcmp(code, "PT")) return "Portugues";
    if (!strcmp(code, "EN")) return "Ingles";
    if (!strcmp(code, "JP")) return "Japones";
    if (!strcmp(code, "ES")) return "Espanhol";
    return "Desconhecido";
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
// ---------------------------------------------------------------- estado do HUD
#define HUD_TIMEOUT_MS 3500
#define SCRUB_COMMIT_MS 1000
#define FLASH_MS 600
#define NEXT_CARD_SECONDS 30.0

typedef struct {
    Uint32 last_input, flash_at, notice_until, vol_until, paused_since, scrub_last;
    Uint32 held_since, held_next, last_anim;
    float hud_alpha, pause_alpha, vol_alpha, notice_alpha, next_alpha;
    int focus, pinned, panel_open, panel_col, panel_audio_sel, panel_sub_sel;
    int flash, flash_seconds, scrubbing, scrub_was_paused, held_button;
    int next_dismissed, next_was_visible;
    double scrub_target;
    char notice[112];
} PlayerUi;

static void ui_touch(PlayerUi *ui) { ui->last_input = SDL_GetTicks(); }

static void ui_notice(PlayerUi *ui, const char *text, Uint32 ms) {
    snprintf(ui->notice, sizeof(ui->notice), "%s", text ? text : "");
    ui->notice_until = SDL_GetTicks() + ms;
}

static void ui_flash(PlayerUi *ui, int kind, int seconds) {
    ui->flash = kind; ui->flash_seconds = seconds; ui->flash_at = SDL_GetTicks();
}

static int ui_hud_wanted(const PlayerUi *ui, int paused, Uint32 now) {
    return ui->pinned || paused || ui->panel_open || ui->scrubbing ||
           (Uint32)(now - ui->last_input) < HUD_TIMEOUT_MS;
}

static float ui_approach(float value, float target, float step) {
    if (value < target) { value += step; if (value > target) value = target; }
    else if (value > target) { value -= step; if (value < target) value = target; }
    return value;
}

// Transicoes suaves: o HUD aparece/some em ~0,2 s, a sinopse de pausa entra
// depois de 1,2 s parado e o cartao de proximo episodio desliza.
static void ui_animate(PlayerUi *ui, int paused, int card_visible, Uint32 now) {
    float dt = (Uint32)(now - ui->last_anim) / 1000.0f;
    if (dt > 0.1f) dt = 0.1f;
    ui->last_anim = now;
    ui->hud_alpha = ui_approach(ui->hud_alpha, ui_hud_wanted(ui, paused, now) ? 1.0f : 0.0f, dt / 0.22f);
    int pause_info = paused && !ui->panel_open && !ui->scrubbing && (Uint32)(now - ui->paused_since) > 1200;
    ui->pause_alpha = ui_approach(ui->pause_alpha, pause_info ? 1.0f : 0.0f, dt / 0.45f);
    ui->vol_alpha = ui_approach(ui->vol_alpha, SDL_TICKS_PASSED(now, ui->vol_until) ? 0.0f : 1.0f, dt / 0.15f);
    ui->notice_alpha = ui_approach(ui->notice_alpha, SDL_TICKS_PASSED(now, ui->notice_until) ? 0.0f : 1.0f, dt / 0.2f);
    ui->next_alpha = ui_approach(ui->next_alpha, card_visible ? 1.0f : 0.0f, dt / 0.3f);
}

// Ordem dos controles na fileira inferior (mesma ordem visual do HUD).
static int ui_row(int tracks, int has_next, int card, int *row) {
    int n = 0;
    row[n++] = PUI_FOCUS_PLAY; row[n++] = PUI_FOCUS_REW; row[n++] = PUI_FOCUS_FWD; row[n++] = PUI_FOCUS_VOLUME;
    if (tracks) row[n++] = PUI_FOCUS_TRACKS;
    if (has_next) row[n++] = PUI_FOCUS_NEXT;
    if (card) row[n++] = PUI_FOCUS_NEXT_CARD;
    return n;
}

static int ui_move_focus(int focus, int dir, const int *row, int n) {
    int index = -1;
    for (int i = 0; i < n; i++) if (row[i] == focus) index = i;
    if (index < 0) return row[0];
    index += dir;
    if (index < 0) index = 0;
    if (index >= n) index = n - 1;
    return row[index];
}

// Passo da busca pela timeline: comeca fino e acelera enquanto a direcao e segurada.
static double ui_scrub_step(Uint32 held_ms, double dur) {
    double step = held_ms < 1200 ? 10.0 : held_ms < 3000 ? 30.0 : 60.0;
    if (dur > 0 && dur < 600 && step > 10.0) step = 10.0;
    return step;
}

static void sync_hud(PlayerHud *h, PlayerUi *ui, double pos, double dur, int paused, int vol,
                     int card_visible, int acur, int scur, const char *subtitle_text,
                     const char *scrub_label, Uint32 now) {
    h->pos = pos; h->dur = dur; h->paused = paused; h->volume = vol;
    h->scrubbing = ui->scrubbing; h->scrub_target = ui->scrub_target;
    h->scrub_label = ui->scrubbing ? scrub_label : NULL;
    h->hud_alpha = ui->hud_alpha;
    h->pause_info_alpha = ui->pause_alpha;
    h->focus = ui->focus;
    h->volume_popup_alpha = ui->vol_alpha;
    h->next_card_alpha = ui->next_alpha;
    double remaining = dur - pos;
    h->next_card_progress = card_visible ? (float)(1.0 - remaining / NEXT_CARD_SECONDS) : 0.0f;
    Uint32 flash_age = (Uint32)(now - ui->flash_at);
    h->flash = flash_age < FLASH_MS ? ui->flash : PUI_FLASH_NONE;
    h->flash_t = flash_age / (float)FLASH_MS;
    h->flash_seconds = ui->flash_seconds;
    h->notice = ui->notice_alpha > 0.01f ? ui->notice : NULL;
    h->notice_alpha = ui->notice_alpha;
    h->panel_open = ui->panel_open;
    h->panel_column = ui->panel_col;
    h->audio_sel = ui->panel_audio_sel; h->audio_current = acur;
    h->sub_sel = ui->panel_sub_sel; h->sub_current = scur + 1;
    h->subtitle_text = subtitle_text;
}

static void present_player(SDL_Renderer *ren, SDL_Texture *tex, const SDL_Rect *dst,
                           int have_frame, const PlayerHud *hud) {
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    if (have_frame && tex) SDL_RenderCopy(ren, tex, NULL, dst);
    pui_draw(ren, hud, SDL_GetTicks());
    SDL_RenderPresent(ren);
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
                                double *out_pos, double *out_dur) {
    g_player_last_error[0] = '\0';
    player_boot_stage("01 inicio do player");
    player_boot_stage(appletGetAppletType() == AppletType_Application
                      ? "01 memoria: modo application"
                      : "01 memoria: modo applet");
    if (out_pos) *out_pos = 0;
    if (out_dur) *out_dur = 0;
    
    const char *url = req->url;
    int is_hls = (req->container && !strcmp(req->container, "m3u8"));
    const char *title = req->title;
    g_hls_io_opened = 0;
    nplay_curl_avio_trace_reset();
    // Tela de preparacao enquanto abre a conexao e le os metadados.
    pui_draw_loading(ren, title, "Preparando video", "Conectando ao servidor...", SDL_GetTicks(), 0);
    SDL_RenderPresent(ren);

    // Arquivos sdmc:/ usam o protocolo local. HLS remoto delega master, filhos e
    // segmentos ao callback libcurl; MP4 remoto continua no protocolo do FFmpeg.
    int remote = !strncmp(url, "http://", 7) || !strncmp(url, "https://", 8);
    // HLS precisa abrir a playlist e depois seus sub-manifestos/segmentos.
    int native_hls = remote && is_hls;
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
            player_error_message("nao foi possivel baixar o manifesto HLS");
            return -10;
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
    PlayerOpenDeadline open_watch = {
        av_gettime_relative() + (native_hls ? 20000000LL : 30000000LL), joy, 0,
        ren, title, "Preparando video", "Conectando ao servidor...", SDL_GetTicks()
    };
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
            av_dict_set(&open_opts, "http_multiple", "0", 0);
            av_dict_set(&open_opts, "seg_max_retry", "3", 0);
        } else {
            av_dict_set(&open_opts, "seekable", "1", 0);
            av_dict_set(&open_opts, "multiple_requests", "1", 0);
        }
    }
    player_boot_stage("03 abrindo fonte");
    diag_player_event("format", "open-begin", "timeout=%ds", native_hls ? 20 : 30);
    int rc = avformat_open_input(&fmt, url, forced_format, &open_opts);
    av_dict_free(&open_opts);
    if (rc != 0) {
        diag_player_event("format", "open-fail", "rc=%d cancelled=%d", rc, open_watch.cancelled);
        if (open_watch.cancelled) player_error_message("Abertura cancelada");
        else if (rc == AVERROR_EXIT) player_error_message(native_hls ? "abrir playlist HLS: tempo esgotado" : "abrir fonte: tempo esgotado");
        else player_error_text(native_hls ? "abrir playlist HLS" : "abrir fonte", rc);
        nplay_curl_avio_close(avio); return open_watch.cancelled ? -11 : -10;
    }
    diag_player_event("format", "open-ok", "streams=%u", fmt->nb_streams);
    player_boot_stage("04 fonte aberta");
    open_watch.detail = native_hls ? "Playlist aberta. Lendo video e audio..." : "Fonte aberta. Lendo video e audio...";
    pui_draw_loading(ren, title, open_watch.headline, open_watch.detail, SDL_GetTicks(), 0);
    SDL_RenderPresent(ren);
    open_watch.deadline_us = av_gettime_relative() + (native_hls ? 20000000LL : 30000000LL);
    // Na abertura HLS, priorize video e um audio. Legendas e audios alternativos
    // continuam enumerados/restaurados depois, mas nao podem segurar o primeiro
    // quadro enquanto FFmpeg tenta sondar todas as 5-6 playlists do R2.
    enum AVDiscard saved_discard[64];
    unsigned saved_count = fmt->nb_streams < 64 ? fmt->nb_streams : 64;
    int probe_audio = -1;
    if (native_hls) {
        for (unsigned i = 0; i < saved_count; i++) {
            saved_discard[i] = fmt->streams[i]->discard;
            if (probe_audio < 0 && fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                probe_audio = (int)i;
        }
        for (unsigned i = 0; i < saved_count; i++) {
            enum AVMediaType type = fmt->streams[i]->codecpar->codec_type;
            if (type == AVMEDIA_TYPE_SUBTITLE || (type == AVMEDIA_TYPE_AUDIO && (int)i != probe_audio))
                fmt->streams[i]->discard = AVDISCARD_ALL;
        }
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
    rc = headers_ready ? 0 : avformat_find_stream_info(fmt, NULL);
    if (native_hls) {
        for (unsigned i = 0; i < saved_count; i++) fmt->streams[i]->discard = saved_discard[i];
    }
    open_watch.deadline_us = 0;
    open_watch.ren = NULL;  // daqui em diante o callback nao desenha mais
    if (rc < 0) {
        diag_player_event("format", "probe-fail", "rc=%d streams=%u", rc, fmt->nb_streams);
        if (open_watch.cancelled) player_error_message("Abertura cancelada");
        else if (rc == AVERROR_EXIT) player_error_message("ler faixas do video: tempo esgotado");
        else player_error_text("ler faixas do video", rc);
        avformat_close_input(&fmt); nplay_curl_avio_close(avio);
        return open_watch.cancelled ? -11 : -2;
    }

    player_boot_stage("06 faixas prontas");
    diag_player_event("format", "probe-ok", "streams=%u duration=%lld",
                      fmt->nb_streams, (long long)fmt->duration);
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

    double dur = (fmt->duration > 0) ? fmt->duration / (double)AV_TIME_BASE : 0;
    double timeline_origin = (fmt->start_time != AV_NOPTS_VALUE)
        ? fmt->start_time / (double)AV_TIME_BASE : 0;
    if (out_dur) *out_dur = dur;

    // Rotulos do painel de audio e legendas (titulo da faixa ou idioma).
    char audio_names[16][96], audio_details[16][96], sub_names[17][96];
    for (int i = 0; i < naud; i++) {
        AVStream *st = fmt->streams[aidxs[i]];
        AVDictionaryEntry *tt = av_dict_get(st->metadata, "title", NULL, 0);
        const char *lang = stream_lang(fmt, aidxs[i]);
        const char *name = lang_name(lang);
        int channels = st->codecpar->ch_layout.nb_channels;
        snprintf(audio_names[i], sizeof(audio_names[i]), "%s",
                 tt && tt->value && tt->value[0] ? tt->value : strcmp(name, "Desconhecido") ? name : lang);
        snprintf(audio_details[i], sizeof(audio_details[i]), "%s  |  %s  |  %d canal%s", lang_label(lang),
                 avcodec_get_name(st->codecpar->codec_id), channels, channels == 1 ? "" : "is");
    }
    snprintf(sub_names[0], sizeof(sub_names[0]), "Desligadas");
    for (int i = 0; i < nsub; i++) {
        AVDictionaryEntry *tt = av_dict_get(fmt->streams[sidxs[i]]->metadata, "title", NULL, 0);
        const char *lang = stream_lang(fmt, sidxs[i]);
        const char *name = lang_name(lang);
        snprintf(sub_names[i + 1], sizeof(sub_names[i + 1]), "%s",
                 tt && tt->value && tt->value[0] ? tt->value : strcmp(name, "Desconhecido") ? name : lang);
    }
    double chapter_starts[64];
    int chapter_count = 0;
    for (unsigned i = 0; i < fmt->nb_chapters && chapter_count < 64; i++) {
        AVChapter *chapter = fmt->chapters[i];
        chapter_starts[chapter_count++] = chapter->start * av_q2d(chapter->time_base) - timeline_origin;
    }
    PlayerHud hud;
    memset(&hud, 0, sizeof(hud));
    hud.title = title;
    hud.subtitle = req->subtitle;
    hud.overview = req->overview;
    hud.has_next = req->has_next;
    hud.next_title = req->next_title;
    hud.audio_count = naud;
    hud.sub_count = nsub + 1;
    for (int i = 0; i < naud; i++) { hud.audio_names[i] = audio_names[i]; hud.audio_details[i] = audio_details[i]; }
    for (int i = 0; i <= nsub; i++) hud.sub_names[i] = sub_names[i];
    hud.chapters = chapter_starts;
    hud.chapter_count = chapter_count;

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
        if (adev) SDL_PauseAudioDevice(adev, 0);
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
    double audio_clock = 0, wall_start = av_gettime() / 1000000.0;
    double last_ac = -1, last_ac_wall = av_gettime() / 1000000.0;  // detecta audio travado
    double cur_pos = 0;
    int running = 1, paused = 0, vol = 100, reached_end = 0, playback_error = 0, want_next = 0;
    int decoded_video = 0, dropped_video = 0, buffering_events = 0, hardware_decode = 0;
    unsigned max_audio_queue = 0;
    store_load_player_volume(&vol);
    int swr_rate = 0, swr_fmt = -1, swr_ch = 0;   // config atual do resample (do frame real)
    uint8_t *audio_buf = NULL;
    unsigned int audio_buf_cap = 0;                // reutilizado entre frames (evita churn no heap)
    Uint32 buffering_since = 0;
    int have_video_frame = 0;
    int logged_first_read = 0, logged_first_video_packet = 0;
    int logged_first_video_frame = 0, logged_first_present = 0;
    int seek_axis_lock = 0, seek_arm_dir = 0;
    Uint32 seek_arm_since = 0, stick_tick = SDL_GetTicks();
    char scrub_label[160] = "";
    PlayerUi ui;
    memset(&ui, 0, sizeof(ui));
    ui.last_input = ui.last_anim = SDL_GetTicks();
    ui.hud_alpha = 1.0f;                 // HUD visivel ao iniciar
    ui.focus = PUI_FOCUS_PLAY;
    ui.held_button = -1;
    SDL_Event e;

    // Retoma de onde parou somente quando ha margem suficiente ate o fim e o
    // FFmpeg confirma a busca; sem isso o HUD e o progresso salvo mentiriam.
    if (start_sec > 3 && (dur <= 0 || start_sec < dur - 5)) {
        if (av_seek_frame(fmt, -1, (int64_t)((start_sec + timeline_origin) * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD) >= 0) {
            audio_clock = start_sec; cur_pos = start_sec;
            wall_start = av_gettime() / 1000000.0 - start_sec;
        } else diag_player_event("player", "resume-seek-fail", "target=%.1f", start_sec);
    }

    // Heartbeat & Progress tracking are now managed by a separate thread
    Uint32 last_heartbeat = SDL_GetTicks();
    PlaybackHeartbeat *hb = heartbeat;
    if (hb) SDL_AtomicSet(&hb->pipeline_ready, 1);
    player_boot_stage("09 reproduzindo");

    // Operacoes usadas pelos controles. Macros porque dependem do estado local
    // do pipeline (clocks, decoders e dispositivo de audio).
#define SEEK_TO(target) apply_player_seek(fmt, vctx, actx, sctx, adev, (target), timeline_origin, \
        &wall_start, &audio_clock, &cur_pos, &last_ac, &last_ac_wall, sub_text, &sub_end)
#define REANCHOR_CLOCKS() do { double rn_ = av_gettime() / 1000000.0; wall_start = rn_ - cur_pos; \
        last_ac = -1; last_ac_wall = rn_; } while (0)
#define TOGGLE_PAUSE() do { paused = !paused; if (adev) SDL_PauseAudioDevice(adev, paused); \
        if (paused) { ui.paused_since = SDL_GetTicks(); if (hb) SDL_AtomicSet(&hb->force_progress, 1); } \
        else REANCHOR_CLOCKS(); \
        ui_flash(&ui, paused ? PUI_FLASH_PAUSE : PUI_FLASH_PLAY, 0); } while (0)
#define JUMP(delta) do { double d_ = (delta), t_ = cur_pos + d_; if (t_ < 0) t_ = 0; \
        if (dur > 0 && t_ > dur - 1) t_ = dur - 1; \
        if (SEEK_TO(t_) == 0) { ui_flash(&ui, d_ > 0 ? PUI_FLASH_FWD : PUI_FLASH_REW, (int)(d_ > 0 ? d_ : -d_)); \
            if (hb) SDL_AtomicSet(&hb->force_progress, 1); } \
        else ui_notice(&ui, "Nao foi possivel buscar neste video", 1800); } while (0)
#define SCRUB_BEGIN() do { if (!ui.scrubbing && dur > 1) { ui.scrubbing = 1; ui.scrub_was_paused = paused; \
        ui.scrub_target = cur_pos; ui.focus = PUI_FOCUS_TIMELINE; \
        if (adev && !paused) { SDL_PauseAudioDevice(adev, 1); } paused = 1; } } while (0)
#define SCRUB_END(commit) do { if (ui.scrubbing) { ui.scrubbing = 0; paused = ui.scrub_was_paused; \
        if (commit) { if (SEEK_TO(ui.scrub_target) == 0) { if (hb) SDL_AtomicSet(&hb->force_progress, 1); } \
            else ui_notice(&ui, "Nao foi possivel buscar neste video", 1800); } \
        else REANCHOR_CLOCKS(); \
        if (adev && !paused) { SDL_PauseAudioDevice(adev, 0); } ui.scrub_last = 0; seek_axis_lock = 1; } } while (0)
#define PANEL_OPEN(column) do { ui.panel_open = 1; ui.panel_col = (column); ui.panel_audio_sel = acur; \
        ui.panel_sub_sel = scur + 1; if (adev && !paused) SDL_PauseAudioDevice(adev, 1); } while (0)
#define PANEL_CLOSE() do { ui.panel_open = 0; audio_clock = cur_pos; REANCHOR_CLOCKS(); \
        if (adev && !paused) SDL_PauseAudioDevice(adev, 0); } while (0)

    while (running) {
        Uint32 now_ticks = SDL_GetTicks();
        if (now_ticks - last_heartbeat > 1000) {
            last_heartbeat = now_ticks;
            if (hb) {
                SDL_AtomicSet(&hb->current_pos, (int)cur_pos);
                SDL_AtomicSet(&hb->duration, (int)dur);
            }
        }

        int tracks_available = naud > 1 || nsub > 0;
        int card_visible = req->has_next && dur > 60 && dur - cur_pos <= NEXT_CARD_SECONDS &&
                           !ui.next_dismissed && !ui.panel_open;
        if (card_visible && !ui.next_was_visible) ui.focus = PUI_FOCUS_NEXT_CARD;
        if (!card_visible && ui.focus == PUI_FOCUS_NEXT_CARD) ui.focus = PUI_FOCUS_PLAY;
        ui.next_was_visible = card_visible;

        // Coleta botoes: eventos novos e repeticao do direcional segurado.
        int pending[16], pending_repeat[16], npending = 0;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = 0; break; }
            if (e.type != SDL_JOYBUTTONDOWN || npending >= 16) continue;
            int b = e.jbutton.button;
            pending[npending] = b; pending_repeat[npending++] = 0;
            if (b == JOY_UP || b == JOY_DOWN || b == JOY_DLEFT || b == JOY_DRIGHT) {
                ui.held_button = b; ui.held_since = now_ticks; ui.held_next = now_ticks + 380;
            }
        }
        if (ui.held_button >= 0) {
            if (!joy || !SDL_JoystickGetButton(joy, ui.held_button)) ui.held_button = -1;
            else if (SDL_TICKS_PASSED(now_ticks, ui.held_next) && npending < 16) {
                pending[npending] = ui.held_button; pending_repeat[npending++] = 1;
                ui.held_next = now_ticks + 110;
            }
        }

        for (int k = 0; k < npending && running; k++) {
            int b = pending[k], repeat = pending_repeat[k];
            Uint32 tnow = SDL_GetTicks();
            int was_visible = ui_hud_wanted(&ui, paused, tnow);
            int vol_popup = !SDL_TICKS_PASSED(tnow, ui.vol_until);
            if (b == JOY_MINUS) { running = 0; break; }   // saida rapida por compatibilidade

            // ---- painel de audio e legendas
            if (ui.panel_open) {
                ui_touch(&ui);
                if (b == JOY_B || b == JOY_PLUS || (b == JOY_Y && ui.panel_col == 0) ||
                    (b == JOY_X && ui.panel_col == 1)) { PANEL_CLOSE(); continue; }
                if (b == JOY_Y && naud > 0) ui.panel_col = 0;
                else if (b == JOY_X && nsub > 0) ui.panel_col = 1;
                else if (b == JOY_DLEFT && ui.panel_col == 1 && naud > 0) ui.panel_col = 0;
                else if (b == JOY_DRIGHT && ui.panel_col == 0 && nsub > 0) ui.panel_col = 1;
                else if (b == JOY_UP) {
                    if (ui.panel_col == 0 && ui.panel_audio_sel > 0) ui.panel_audio_sel--;
                    if (ui.panel_col == 1 && ui.panel_sub_sel > 0) ui.panel_sub_sel--;
                } else if (b == JOY_DOWN) {
                    if (ui.panel_col == 0 && ui.panel_audio_sel + 1 < naud) ui.panel_audio_sel++;
                    if (ui.panel_col == 1 && ui.panel_sub_sel < nsub) ui.panel_sub_sel++;
                } else if (b == JOY_A && ui.panel_col == 0) {
                    int sel = ui.panel_audio_sel;
                    if (sel == acur) ui_notice(&ui, "Este audio ja esta tocando", 1600);
                    else if (!adev) ui_notice(&ui, "Saida de audio indisponivel", 2000);
                    else if (open_audio_dec(fmt, aidxs[sel], &actx, &swr, OCH, ORATE) == 0) {
                        acur = sel; aidx = aidxs[sel];
                        atb = fmt->streams[aidx]->time_base;
                        SDL_ClearQueuedAudio(adev);
                        audio_clock = cur_pos; last_ac = -1;
                        last_ac_wall = av_gettime() / 1000000.0;
                        char msg[112]; snprintf(msg, sizeof(msg), "Audio: %s", audio_names[acur]);
                        ui_notice(&ui, msg, 2000);
                        AVDictionaryEntry *tag = av_dict_get(fmt->streams[aidx]->metadata, "language", NULL, 0);
                        if (tag) store_save_pref_audio(tag->value);
                    } else ui_notice(&ui, "Nao consegui abrir esta faixa de audio", 2200);
                } else if (b == JOY_A && ui.panel_col == 1) {
                    int next = ui.panel_sub_sel - 1;
                    if (next == scur) ui_notice(&ui, "Esta legenda ja esta ativa", 1600);
                    else if (open_sub_dec(fmt, next >= 0 ? sidxs[next] : -1, &sctx) == 0) {
                        scur = next; sub_text[0] = 0; sub_end = 0;
                        char msg[112];
                        if (scur >= 0) {
                            snprintf(msg, sizeof(msg), "Legendas: %s", sub_names[scur + 1]);
                            AVDictionaryEntry *tag = av_dict_get(fmt->streams[sidxs[scur]]->metadata, "language", NULL, 0);
                            if (tag) store_save_pref_sub(tag->value);
                        } else {
                            snprintf(msg, sizeof(msg), "Legendas desligadas");
                            store_save_pref_sub("off");
                        }
                        ui_notice(&ui, msg, 2000);
                    } else ui_notice(&ui, "Nao consegui abrir esta legenda", 2200);
                }
                continue;
            }

            // ---- escolhendo um ponto na timeline
            if (ui.scrubbing) {
                ui_touch(&ui);
                if (b == JOY_A) SCRUB_END(1);
                else if (b == JOY_B) { SCRUB_END(0); ui_notice(&ui, "Busca cancelada", 1400); }
                else if (b == JOY_DLEFT || b == JOY_DRIGHT || b == JOY_L || b == JOY_R ||
                         b == JOY_ZL || b == JOY_ZR) {
                    int forward = b == JOY_DRIGHT || b == JOY_R || b == JOY_ZR;
                    double step = (b == JOY_ZL || b == JOY_ZR) ? 60.0 :
                                  (b == JOY_L || b == JOY_R) ? 10.0 :
                                  ui_scrub_step(repeat ? (Uint32)(tnow - ui.held_since) : 0, dur);
                    ui.scrub_target += forward ? step : -step;
                    if (ui.scrub_target < 0) ui.scrub_target = 0;
                    if (ui.scrub_target > dur - 1) ui.scrub_target = dur - 1;
                    ui.scrub_last = tnow;
                } else if ((b == JOY_UP || b == JOY_DOWN) && fmt->nb_chapters > 0) {
                    ui.scrub_target = adjacent_chapter(fmt, ui.scrub_target, b == JOY_DOWN, timeline_origin);
                    if (ui.scrub_target < 0) ui.scrub_target = 0;
                    if (ui.scrub_target > dur - 1) ui.scrub_target = dur - 1;
                    ui.scrub_last = tnow;
                } else if (b == JOY_DOWN) SCRUB_END(1);
                continue;
            }

            if (b == JOY_B) {
                if (card_visible && ui.focus == PUI_FOCUS_NEXT_CARD) {
                    ui.next_dismissed = 1; ui.focus = PUI_FOCUS_PLAY; ui_touch(&ui); continue;
                }
                if (vol_popup) { ui.vol_until = tnow; ui_touch(&ui); continue; }
                running = 0; break;
            }
            ui_touch(&ui);
            int row[8], nrow = ui_row(tracks_available, req->has_next, card_visible, row);
            switch (b) {
                case JOY_A:
                    // O cartao de proximo episodio aparece mesmo com o HUD escondido
                    // e anuncia "A Assistir agora"; nesse caso A nao pausa.
                    if (!was_visible && !(card_visible && ui.focus == PUI_FOCUS_NEXT_CARD)) {
                        TOGGLE_PAUSE(); break;
                    }
                    switch (ui.focus) {
                        case PUI_FOCUS_REW: JUMP(-10.0); break;
                        case PUI_FOCUS_FWD: JUMP(10.0); break;
                        case PUI_FOCUS_VOLUME: ui.vol_until = tnow + 2500; break;
                        case PUI_FOCUS_TRACKS: PANEL_OPEN(naud > 1 ? 0 : 1); break;
                        case PUI_FOCUS_NEXT:
                        case PUI_FOCUS_NEXT_CARD:
                            if (req->has_next) { want_next = 1; running = 0; }
                            break;
                        default: TOGGLE_PAUSE(); break;
                    }
                    break;
                case JOY_L: JUMP(-10.0); break;
                case JOY_R: JUMP(10.0); break;
                case JOY_ZL: JUMP(-60.0); break;
                case JOY_ZR: JUMP(60.0); break;
                case JOY_Y:
                    if (tracks_available) PANEL_OPEN(naud > 1 ? 0 : 1);
                    else ui_notice(&ui, "Este video possui apenas um audio", 2000);
                    break;
                case JOY_X:
                    if (nsub > 0) PANEL_OPEN(1);
                    else ui_notice(&ui, "Este video nao possui legendas", 2000);
                    break;
                case JOY_PLUS:
                    ui.pinned = !ui.pinned;
                    ui_notice(&ui, ui.pinned ? "Controles fixos na tela" : "Controles automaticos", 1600);
                    break;
                case JOY_UP:
                    if (vol_popup) {
                        vol = vol + 10 > 100 ? 100 : vol + 10; ui.vol_until = tnow + 2200;
                    } else if (was_visible && ui.focus != PUI_FOCUS_TIMELINE && dur > 1 && !repeat) {
                        ui.focus = PUI_FOCUS_TIMELINE;
                    }
                    break;
                case JOY_DOWN:
                    if (vol_popup) {
                        vol = vol - 10 < 0 ? 0 : vol - 10; ui.vol_until = tnow + 2200;
                    } else if (was_visible && (ui.focus == PUI_FOCUS_TIMELINE || ui.focus == PUI_FOCUS_NEXT_CARD)) {
                        ui.focus = PUI_FOCUS_PLAY;
                    }
                    break;
                case JOY_DLEFT:
                case JOY_DRIGHT: {
                    int dir = b == JOY_DRIGHT ? 1 : -1;
                    if ((!was_visible || ui.focus == PUI_FOCUS_TIMELINE) && dur > 1) {
                        // Como na TV do PC: com o HUD escondido, esquerda/direita ja buscam.
                        SCRUB_BEGIN();
                        ui.scrub_target += dir * ui_scrub_step(repeat ? (Uint32)(tnow - ui.held_since) : 0, dur);
                        if (ui.scrub_target < 0) ui.scrub_target = 0;
                        if (ui.scrub_target > dur - 1) ui.scrub_target = dur - 1;
                        ui.scrub_last = tnow;
                    } else if (was_visible) {
                        if (vol_popup) ui.vol_until = tnow;
                        ui.focus = ui_move_focus(ui.focus, dir, row, nrow);
                    }
                    break;
                }
                default: break;
            }
        }
        if (!running) break;

        // Analogico esquerdo: segurar inclinado abre a busca pela timeline. Mantem
        // a protecao contra drift (75% do curso por 550 ms) das versoes anteriores.
        int stick_x = joy ? SDL_JoystickGetAxis(joy, 0) : 0;
        int stick_abs = stick_x < 0 ? -stick_x : stick_x;
        Uint32 seek_now = SDL_GetTicks();
        if (stick_abs < SEEK_RELEASE_AXIS) {
            seek_axis_lock = 0;
            seek_arm_dir = 0;
            seek_arm_since = 0;
        }
        if (!ui.panel_open && dur > 1 && !ui.scrubbing && !seek_axis_lock) {
            if (stick_abs >= SEEK_ENTER_AXIS) {
                int direction = stick_x > 0 ? 1 : -1;
                if (seek_arm_dir != direction) {
                    seek_arm_dir = direction;
                    seek_arm_since = seek_now;
                } else if (seek_now - seek_arm_since >= SEEK_HOLD_MS) {
                    SCRUB_BEGIN();
                    ui.scrub_last = seek_now;
                    stick_tick = seek_now;
                    seek_arm_dir = 0; seek_arm_since = 0;
                    ui_touch(&ui);
                } else if (seek_now - seek_arm_since >= 280) {
                    ui_notice(&ui, "Continue segurando para buscar na timeline", 300);
                }
            } else {
                seek_arm_dir = 0;
                seek_arm_since = 0;
            }
        }
        if (ui.scrubbing) {
            double elapsed = (seek_now - stick_tick) / 1000.0;
            if (elapsed > 0.08) elapsed = 0.08;
            stick_tick = seek_now;
            if (stick_abs >= SEEK_MOVE_AXIS) {
                double amount = (stick_abs - SEEK_MOVE_AXIS) / (32767.0 - SEEK_MOVE_AXIS);
                if (amount > 1.0) amount = 1.0;
                double speed = dur * (0.006 + amount * amount * 0.039);
                ui.scrub_target += (stick_x > 0 ? 1.0 : -1.0) * speed * elapsed;
                if (ui.scrub_target < 0) ui.scrub_target = 0;
                if (ui.scrub_target > dur - 1) ui.scrub_target = dur - 1;
                ui.scrub_last = seek_now;
                ui_touch(&ui);
            } else if (ui.scrub_last && ui.held_button < 0 &&
                       (Uint32)(seek_now - ui.scrub_last) > SCRUB_COMMIT_MS) {
                SCRUB_END(1);   // confirma sozinho depois de uma pausa, como na TV do PC
            }
        }

        if (ui.scrubbing) {
            int ch = chapter_at(fmt, ui.scrub_target, timeline_origin, scrub_label, sizeof(scrub_label));
            if (ch < 0) scrub_label[0] = 0;
        }
        ui_animate(&ui, paused, card_visible, SDL_GetTicks());
        const char *visible_sub = scur >= 0 && sub_text[0] && cur_pos < sub_end ? sub_text : NULL;
        sync_hud(&hud, &ui, cur_pos, dur, paused, vol, card_visible, acur, scur, visible_sub,
                 scrub_label[0] ? scrub_label : NULL, SDL_GetTicks());
        hud.buffering = 0;
        if (paused || ui.panel_open) {   // quadro congelado + HUD animado
            present_player(ren, tex, &dst, have_video_frame, &hud);
            SDL_Delay(16);
            continue;
        }

        int ret = av_read_frame(fmt, pkt);
        if (!logged_first_read) {
            diag_player_event("demux", ret >= 0 ? "first-read-ok" : "first-read-fail",
                              "rc=%d stream=%d", ret, ret >= 0 ? pkt->stream_index : -1);
            logged_first_read = 1;
        }
        if (ret == AVERROR(EAGAIN)) {
            // Buffer vazio e/ou timeout de rede, thread de download ainda esta trabalhando.
            Uint32 now_ticks = SDL_GetTicks();
            if (!buffering_since) { buffering_since = now_ticks; buffering_events++; }
            if (now_ticks - buffering_since >= 250) {
                Uint32 stalled = now_ticks - buffering_since;
                hud.buffering = 1;
                hud.buffering_text = stalled < 8000 ? "Carregando..." :
                                     stalled < 30000 ? "Reconectando..." : "Conexao lenta  |  B para voltar";
                present_player(ren, tex, &dst, have_video_frame, &hud);
                hud.buffering = 0;
            }
            SDL_Delay(30);
            continue;
        }
        buffering_since = 0;
        if (ret < 0) {  // fim real ou falha definitiva da fonte/rede
            if (!adev || SDL_GetQueuedAudioSize(adev) < 8192) {
                if (ret == AVERROR_EOF) reached_end = 1;
                else playback_error = -5;
                diag_player_event("demux", "read-terminal", "rc=%d eof=%d", ret, reached_end);
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
                    {
                        Uint32 frame_now = SDL_GetTicks();
                        ui_animate(&ui, paused, card_visible, frame_now);
                        const char *frame_sub = scur >= 0 && sub_text[0] && cur_pos < sub_end ? sub_text : NULL;
                        sync_hud(&hud, &ui, cur_pos, dur, paused, vol, card_visible, acur, scur, frame_sub,
                                 NULL, frame_now);
                        present_player(ren, tex, &dst, 1, &hud);
                    }
                    if (!logged_first_present) {
                        diag_player_event("render", "first-present", "position=%.2f", cur_pos);
                        logged_first_present = 1;
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

    if (out_pos) *out_pos = cur_pos;
    if (out_dur) *out_dur = dur;
    store_save_player_volume(vol);
    store_save_player_stats(vw, vh, decoded_video, dropped_video,
                            buffering_events, max_audio_queue, playback_error,
                            hardware_decode);
    diag_player_event("player", "cleanup-begin", "pos=%.1f frames=%d drop=%d err=%d",
                      cur_pos, decoded_video, dropped_video, playback_error);

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
    diag_player_event("player", "cleanup-end", NULL);
#undef SEEK_TO
#undef REANCHOR_CLOCKS
#undef TOGGLE_PAUSE
#undef JUMP
#undef SCRUB_BEGIN
#undef SCRUB_END
#undef PANEL_OPEN
#undef PANEL_CLOSE
    if (playback_error) return playback_error;
    if (want_next) return 2;
    return reached_end;
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
            if (hb->progress_cb && dur > 0 && pos > 5)
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
    double last_failure_pos = -1.0;
    double dur = 0.0;
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
        PlayerRequest attempt = *request;
        attempt.playback = active;
        attempt.session_id = active.session_id;
        attempt.source_id = active.source_id;
        attempt.delivery = active.delivery;
        attempt.section = active.section;
        attempt.container = active.container;
        attempt.url = active.play_url;
        diag_player_event("player", "attempt-begin", "attempt=%d pos=%.1f session=%d source=%d",
                          retry_count + 1, current_pos, active.session_id, active.source_id);
        int rc = player_play_internal(ren, joy, &attempt, &hb, current_pos, &out_pos, &out_dur);
        diag_player_event("player", "attempt-end", "attempt=%d rc=%d pos=%.1f dur=%.1f",
                          retry_count + 1, rc, out_pos, out_dur);
        if (out_pos > 0) current_pos = out_pos;
        if (out_dur > 0) dur = out_dur;

        if (rc == 1) { // Terminou naturalmente
            result->reason = EXIT_REASON_NATURAL;
            result->final_state = PLAYER_FINISHED;
            final_rc = rc;
            break;
        } else if (rc == 2) { // Usuario pediu o proximo episodio pelo HUD
            result->reason = EXIT_REASON_NEXT;
            result->final_state = PLAYER_FINISHED;
            final_rc = 0;
            break;
        } else if (rc == 0) { // Usuario saiu
            result->reason = EXIT_REASON_USER;
            result->final_state = PLAYER_FINISHED;
            final_rc = rc;
            break;
        } else { // Erro
            // rc < 0
            int recoverable = rc == -2 || rc == -5 || rc == -10;
            // O orcamento de recuperacao vale por incidente. Se a reproducao ja
            // avancou bem alem da falha anterior, uma nova queda de rede recomeca
            // pela renovacao e nao penaliza no servidor uma fonte que funcionava.
            if (retry_count > 0 && last_failure_pos >= 0 && current_pos > last_failure_pos + 60.0) {
                diag_player_event("recover", "budget-reset", "pos=%.1f last=%.1f", current_pos, last_failure_pos);
                retry_count = 0;
            }
            last_failure_pos = current_pos;
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
                char detail[96];
                const char *headline = use_fallback ? "Tentando outra fonte" : "Reconectando";
                if (use_fallback) snprintf(detail, sizeof(detail), "A fonte atual nao respondeu. Buscando alternativa...");
                else snprintf(detail, sizeof(detail), "Recuperando a transmissao...  %d/%d", renew_try + 1, max_renew_tries);
                pui_draw_loading(ren, request->title, headline, detail, SDL_GetTicks(), 1);
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
                    pui_draw_loading(ren, request->title, headline, detail, SDL_GetTicks(), 1);
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
            retry_count++;
            result->recovery_count = retry_count;
            continue;
        }
    }

    result->position = current_pos;
    result->duration = dur;

    if (heartbeat) {
        SDL_AtomicSet(&hb.running, 0);
        SDL_WaitThread(heartbeat, NULL);
    }
    
    // Save progress once at the end
    if (request->progress_cb) {
        request->progress_cb(request->item_id, (int)current_pos, (int)dur, request->userdata);
    }

    diag_player_finish(final_rc);

    return final_rc;
}
