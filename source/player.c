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
#define JOY_UP 13
#define JOY_DOWN 15
#define PWIN_W 1280
#define PWIN_H 720

static const SDL_Color PC_TEXT = { 234, 240, 250, 255 };
static const SDL_Color PC_MUT  = { 170, 178, 196, 255 };
static const SDL_Color PC_ACC  = { 139, 92, 246, 255 };

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
// HUD inferior: faixa escura + titulo + barra de progresso + tempo + volume.
static void draw_hud(SDL_Renderer *ren, const char *title, double pos, double dur, int paused, int vol, const char *hint) {
    SDL_Color black = { 0, 0, 0, 255 };
    pfill(ren, 0, PWIN_H - 96, PWIN_W, 96, black, 150);           // faixa translucida
    if (title && title[0]) text_draw(ren, title, 60, PWIN_H - 84, PC_TEXT, 1);
    char vv[24]; snprintf(vv, sizeof(vv), "%s Vol %d%%", paused ? "PAUSADO  " : "", vol);
    text_draw(ren, vv, PWIN_W - 240, PWIN_H - 84, paused ? PC_ACC : PC_MUT, 0);

    int bx = 60, by = PWIN_H - 34, bw = PWIN_W - 260, bh = 6;
    pfill(ren, bx, by, bw, bh, PC_MUT, 90);                        // trilho
    if (dur > 0) {
        int fw = (int)(bw * (pos / dur));
        if (fw < 0) fw = 0; if (fw > bw) fw = bw;
        pfill(ren, bx, by, fw, bh, PC_ACC, 255);                  // preenchido
        pfill(ren, bx + fw - 2, by - 4, 4, bh + 8, PC_TEXT, 255);  // "cabeca"
    }
    char t1[16], t2[16], line[40];
    fmt_time(pos, t1, sizeof(t1));
    if (dur > 0) { fmt_time(dur, t2, sizeof(t2)); snprintf(line, sizeof(line), "%s / %s", t1, t2); }
    else snprintf(line, sizeof(line), "%s", t1);
    text_draw(ren, line, PWIN_W - 190, PWIN_H - 40, PC_MUT, 0);
    text_draw(ren, hint ? hint : "A pausa   L/R 10s   ZL/ZR 60s   cima/baixo volume   B volta", 60, PWIN_H - 62, PC_MUT, 0);
}
// monta a linha de status/controles do HUD (mostra audio/legenda quando ha varias faixas).
static void build_hint(AVFormatContext *fmt, int naud, int aidx, int nsub, int scur, char *out, int cap);

// idioma de um stream (tag "language"), ex.: "por", "eng", "jpn".
static const char *stream_lang(AVFormatContext *fmt, int idx) {
    if (idx < 0) return "?";
    AVDictionaryEntry *e = av_dict_get(fmt->streams[idx]->metadata, "language", NULL, 0);
    return (e && e->value) ? e->value : "und";
}
// (re)abre o decoder de audio + resample p/ o stream aidx. Fecha o anterior.
static int open_audio_dec(AVFormatContext *fmt, int aidx, AVCodecContext **pactx, struct SwrContext **pswr, int OCH, int ORATE) {
    if (*pswr) swr_free(pswr);
    if (*pactx) avcodec_free_context(pactx);
    if (aidx < 0) return -1;
    AVCodecParameters *apar = fmt->streams[aidx]->codecpar;
    const AVCodec *adec = avcodec_find_decoder(apar->codec_id);
    if (!adec) return -1;
    AVCodecContext *actx = avcodec_alloc_context3(adec);
    avcodec_parameters_to_context(actx, apar);
    if (avcodec_open2(actx, adec, NULL) != 0) { avcodec_free_context(&actx); return -1; }
    AVChannelLayout outl; av_channel_layout_default(&outl, OCH);
    struct SwrContext *swr = NULL;
    if (swr_alloc_set_opts2(&swr, &outl, AV_SAMPLE_FMT_S16, ORATE, &actx->ch_layout, actx->sample_fmt, actx->sample_rate, 0, NULL) == 0)
        swr_init(swr);
    *pactx = actx; *pswr = swr;
    return 0;
}
// (re)abre o decoder de legenda p/ o stream sidx (-1 = desliga).
static int open_sub_dec(AVFormatContext *fmt, int sidx, AVCodecContext **psctx) {
    if (*psctx) avcodec_free_context(psctx);
    if (sidx < 0) return -1;
    AVCodecParameters *sp = fmt->streams[sidx]->codecpar;
    const AVCodec *sd = avcodec_find_decoder(sp->codec_id);
    if (!sd) return -1;
    AVCodecContext *sc = avcodec_alloc_context3(sd);
    avcodec_parameters_to_context(sc, sp);
    if (avcodec_open2(sc, sd, NULL) != 0) { avcodec_free_context(&sc); return -1; }
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
    int w = 0, h = 0;
    SDL_Color white = { 245, 245, 245, 255 };
    SDL_Texture *t = text_cached(ren, txt, white, 0, &w, &h);
    if (w > PWIN_W - 80) w = PWIN_W - 80;
    int x = (PWIN_W - w) / 2, y = PWIN_H - 150;
    SDL_Color black = { 0, 0, 0, 255 };
    pfill(ren, x - 14, y - 6, w + 28, h + 12, black, 165);
    if (t) { SDL_Rect d = { x, y, w, h }; SDL_RenderCopy(ren, t, NULL, &d); }
}
static void build_hint(AVFormatContext *fmt, int naud, int aidx, int nsub, int scur, char *out, int cap) {
    char a[48] = "", s[32] = "";
    if (naud > 1) snprintf(a, sizeof(a), "Audio: %s (Y)   ", stream_lang(fmt, aidx));
    if (nsub > 0) snprintf(s, sizeof(s), "Leg: %s (X)   ", scur < 0 ? "off" : "on");
    snprintf(out, cap, "%s%sA pausa  L/R 10s  ZL/ZR 60s  cima/baixo vol  B volta", a, s);
}

int player_play(SDL_Renderer *ren, SDL_Joystick *joy, const char *url,
                const char *title, double start_sec, double *out_pos, double *out_dur) {
    (void)joy;
    if (out_pos) *out_pos = 0;
    if (out_dur) *out_dur = 0;
    // tela preta + "carregando" enquanto abre (pode levar alguns segundos)
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
    text_draw(ren, "Carregando video...", PWIN_W / 2 - 120, PWIN_H / 2 - 16, PC_TEXT, 1);
    SDL_RenderPresent(ren);

    // O switch-ffmpeg nao tem TLS: pluga o libcurl como I/O do ffmpeg (ver curl_avio.c).
    AVIOContext *avio = nplay_curl_avio_open(url);
    if (!avio) return -1;
    AVFormatContext *fmt = avformat_alloc_context();
    if (!fmt) { nplay_curl_avio_close(avio); return -1; }
    fmt->pb = avio;
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO;

    int rc = avformat_open_input(&fmt, NULL, NULL, NULL);
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
    int aidx = naud ? aidxs[acur] : -1;
    int scur = -1;                       // -1 = legenda desligada
    AVCodecContext *sctx = NULL;
    char sub_text[512] = ""; double sub_end = 0;

    double dur = (fmt->duration > 0) ? fmt->duration / (double)AV_TIME_BASE : 0;
    if (out_dur) *out_dur = dur;

    // ---- decoder de video ----
    AVCodecParameters *vpar = fmt->streams[vidx]->codecpar;
    const AVCodec *vdec = avcodec_find_decoder(vpar->codec_id);
    AVCodecContext *vctx = avcodec_alloc_context3(vdec);
    avcodec_parameters_to_context(vctx, vpar);
    vctx->thread_count = 4;
    if (!vdec || avcodec_open2(vctx, vdec, NULL) < 0) { avcodec_free_context(&vctx); avformat_close_input(&fmt); nplay_curl_avio_close(avio); return -4; }

    // ---- decoder de audio + resample + saida SDL ----
    AVCodecContext *actx = NULL;
    struct SwrContext *swr = NULL;
    SDL_AudioDeviceID adev = 0;
    const int OCH = 2, ORATE = 48000;
    if (aidx >= 0 && open_audio_dec(fmt, aidx, &actx, &swr, OCH, ORATE) == 0) {
        SDL_AudioSpec want; SDL_zero(want);
        want.freq = ORATE; want.format = AUDIO_S16SYS; want.channels = OCH; want.samples = 2048;
        adev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
        if (adev) SDL_PauseAudioDevice(adev, 0);
    }

    int vw = vctx->width, vh = vctx->height;
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, vw, vh);
    struct SwsContext *sws = NULL; AVFrame *yuv = NULL;
    if (vctx->pix_fmt != AV_PIX_FMT_YUV420P) {
        sws = sws_getContext(vw, vh, vctx->pix_fmt, vw, vh, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);
        yuv = av_frame_alloc(); yuv->format = AV_PIX_FMT_YUV420P; yuv->width = vw; yuv->height = vh;
        av_frame_get_buffer(yuv, 32);
    }

    // retangulo com letterbox (1280x720)
    int dw = PWIN_W, dh = PWIN_H;
    double ar = (double)vw / vh, dar = (double)dw / dh;
    SDL_Rect dst;
    if (ar > dar) { dst.w = dw; dst.h = (int)(dw / ar); } else { dst.h = dh; dst.w = (int)(dh * ar); }
    dst.x = (dw - dst.w) / 2; dst.y = (dh - dst.h) / 2;

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVRational vtb = fmt->streams[vidx]->time_base;
    AVRational atb = (aidx >= 0) ? fmt->streams[aidx]->time_base : (AVRational){1, ORATE};
    double bps = (double)ORATE * OCH * 2.0;
    double audio_clock = 0, wall_start = av_gettime() / 1000000.0;
    double cur_pos = 0;
    int running = 1, paused = 0, vol = 100, reached_end = 0;
    Uint32 hud_until = SDL_GetTicks() + 4000;   // HUD visivel ao iniciar
    SDL_Event e;

    // Retoma de onde parou (só se fizer sentido: > 3s e não no finzinho).
    if (start_sec > 3 && (dur <= 0 || start_sec < dur - 5)) {
        av_seek_frame(fmt, -1, (int64_t)(start_sec * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);
        audio_clock = start_sec; cur_pos = start_sec;
        wall_start = av_gettime() / 1000000.0 - start_sec;
    }

    // Impede o Switch de escurecer/dormir enquanto o video toca (sem input o
    // console apagaria a tela). So durante a reproducao; nos menus deixa dormir.
    appletSetMediaPlaybackState(true);

    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_JOYBUTTONDOWN) {
                int b = e.jbutton.button;
                hud_until = SDL_GetTicks() + 4000;
                if (b == JOY_B || b == JOY_PLUS || b == JOY_MINUS) running = 0;
                else if (b == JOY_A) { paused = !paused; if (adev) SDL_PauseAudioDevice(adev, paused); }
                else if (b == JOY_UP) { vol += 10; if (vol > 150) vol = 150; }
                else if (b == JOY_DOWN) { vol -= 10; if (vol < 0) vol = 0; }
                else if (b == JOY_R || b == JOY_L || b == JOY_ZR || b == JOY_ZL) {
                    double step = (b == JOY_ZR || b == JOY_ZL) ? 60 : 10;
                    double t = audio_clock + ((b == JOY_R || b == JOY_ZR) ? step : -step);
                    if (t < 0) t = 0;
                    if (dur > 0 && t > dur - 1) t = dur - 1;
                    int back = (b == JOY_L || b == JOY_ZL);
                    av_seek_frame(fmt, -1, (int64_t)(t * AV_TIME_BASE), back ? AVSEEK_FLAG_BACKWARD : 0);
                    avcodec_flush_buffers(vctx); if (actx) avcodec_flush_buffers(actx);
                    if (sctx) avcodec_flush_buffers(sctx);
                    if (adev) SDL_ClearQueuedAudio(adev);
                    sub_text[0] = 0; sub_end = 0;
                    wall_start = av_gettime() / 1000000.0 - t; audio_clock = t; cur_pos = t;
                }
                else if (b == JOY_Y) {   // proximo audio (idioma) quando ha varias faixas
                    if (naud > 1) {
                        acur = (acur + 1) % naud; aidx = aidxs[acur];
                        open_audio_dec(fmt, aidx, &actx, &swr, OCH, ORATE);
                        atb = fmt->streams[aidx]->time_base;
                        if (adev) SDL_ClearQueuedAudio(adev);
                    }
                }
                else if (b == JOY_X) {   // legenda: desligada -> faixa 0 -> ... -> desligada
                    if (nsub > 0) {
                        scur++; if (scur >= nsub) scur = -1;
                        open_sub_dec(fmt, scur >= 0 ? sidxs[scur] : -1, &sctx);
                        sub_text[0] = 0; sub_end = 0;
                    }
                }
            }
        }
        if (!running) break;
        if (paused) {   // continua desenhando (quadro congelado + HUD)
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, NULL, &dst);
            if (scur >= 0 && sub_text[0] && cur_pos < sub_end) draw_sub(ren, sub_text);
            char hint[128]; build_hint(fmt, naud, aidx, nsub, scur, hint, sizeof(hint));
            draw_hud(ren, title, cur_pos, dur, 1, vol, hint);
            SDL_RenderPresent(ren);
            SDL_Delay(30);
            continue;
        }

        int ret = av_read_frame(fmt, pkt);
        if (ret < 0) {  // fim do arquivo
            if (!adev || SDL_GetQueuedAudioSize(adev) < 8192) { reached_end = 1; break; }
            SDL_Delay(40); continue;
        }
        if (aidx >= 0 && pkt->stream_index == aidx && actx) {
            if (avcodec_send_packet(actx, pkt) == 0) {
                while (avcodec_receive_frame(actx, frame) == 0) {
                    if (frame->pts != AV_NOPTS_VALUE) audio_clock = frame->pts * av_q2d(atb);
                    uint8_t *ob = NULL;
                    int os = swr_get_out_samples(swr, frame->nb_samples);
                    if (av_samples_alloc(&ob, NULL, OCH, os, AV_SAMPLE_FMT_S16, 0) >= 0) {
                        int n = swr_convert(swr, &ob, os, (const uint8_t **)frame->data, frame->nb_samples);
                        if (n > 0 && vol != 100) {   // aplica o volume nas amostras S16
                            int16_t *sm = (int16_t *)ob; int cnt = n * OCH;
                            for (int i = 0; i < cnt; i++) { int v = sm[i] * vol / 100; sm[i] = v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v); }
                        }
                        if (n > 0 && adev) SDL_QueueAudio(adev, ob, n * OCH * 2);
                    }
                    av_freep(&ob);
                }
            }
        } else if (pkt->stream_index == vidx) {
            if (avcodec_send_packet(vctx, pkt) == 0) {
                while (avcodec_receive_frame(vctx, frame) == 0) {
                    double vpts = (frame->pts != AV_NOPTS_VALUE) ? frame->pts * av_q2d(vtb) : 0;
                    double master = adev ? (audio_clock - SDL_GetQueuedAudioSize(adev) / bps)
                                         : (av_gettime() / 1000000.0 - wall_start);
                    cur_pos = master;
                    double delay = vpts - master;
                    if (delay > 0.001) { if (delay > 0.4) delay = 0.4; SDL_Delay((Uint32)(delay * 1000)); }
                    AVFrame *u = frame;
                    if (sws) { sws_scale(sws, (const uint8_t * const *)frame->data, frame->linesize, 0, vh, yuv->data, yuv->linesize); u = yuv; }
                    SDL_UpdateYUVTexture(tex, NULL, u->data[0], u->linesize[0], u->data[1], u->linesize[1], u->data[2], u->linesize[2]);
                    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
                    SDL_RenderCopy(ren, tex, NULL, &dst);
                    if (scur >= 0 && sub_text[0] && cur_pos < sub_end) draw_sub(ren, sub_text);
                    if (SDL_GetTicks() < hud_until) { char hint[128]; build_hint(fmt, naud, aidx, nsub, scur, hint, sizeof(hint)); draw_hud(ren, title, cur_pos, dur, 0, vol, hint); }
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
                double base = (pkt->pts != AV_NOPTS_VALUE) ? pkt->pts * av_q2d(fmt->streams[sidxs[scur]]->time_base) : cur_pos;
                double sd = (sub.end_display_time > sub.start_display_time) ? (sub.end_display_time - sub.start_display_time) / 1000.0 : 4.0;
                sub_end = base + sd;
                avsubtitle_free(&sub);
            }
        }
        av_packet_unref(pkt);
    }

    if (out_pos) *out_pos = cur_pos;
    if (out_dur) *out_dur = dur;

    appletSetMediaPlaybackState(false);   // volta ao normal (pode dormir de novo)

    if (adev) SDL_CloseAudioDevice(adev);
    if (sws) sws_freeContext(sws);
    if (yuv) av_frame_free(&yuv);
    if (swr) swr_free(&swr);
    if (actx) avcodec_free_context(&actx);
    if (sctx) avcodec_free_context(&sctx);
    avcodec_free_context(&vctx);
    av_frame_free(&frame); av_packet_free(&pkt);
    SDL_DestroyTexture(tex);
    avformat_close_input(&fmt);
    nplay_curl_avio_close(avio);
    return reached_end;   // 1 = terminou naturalmente (p/ auto-play do proximo)
}
