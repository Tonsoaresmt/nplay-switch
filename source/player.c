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
#include "player.h"
#include "curl_avio.h"
#include "text.h"

#define JOY_A 0
#define JOY_B 1
#define JOY_L 6
#define JOY_R 7
#define JOY_PLUS 10
#define JOY_MINUS 11
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
// HUD inferior: faixa escura + titulo + barra de progresso + tempo (+ PAUSADO).
static void draw_hud(SDL_Renderer *ren, const char *title, double pos, double dur, int paused) {
    SDL_Color black = { 0, 0, 0, 255 };
    pfill(ren, 0, PWIN_H - 96, PWIN_W, 96, black, 150);           // faixa translucida
    if (title && title[0]) text_draw(ren, title, 60, PWIN_H - 84, PC_TEXT, 1);
    if (paused) text_draw(ren, "PAUSADO", PWIN_W - 180, PWIN_H - 84, PC_ACC, 0);

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
    text_draw(ren, "A pausa   L/R -+15s   B/+ volta", 60, PWIN_H - 62, PC_MUT, 0);
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
    int aidx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (vidx < 0) { avformat_close_input(&fmt); nplay_curl_avio_close(avio); return -3; }

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
    if (aidx >= 0) {
        AVCodecParameters *apar = fmt->streams[aidx]->codecpar;
        const AVCodec *adec = avcodec_find_decoder(apar->codec_id);
        if (adec) {
            actx = avcodec_alloc_context3(adec);
            avcodec_parameters_to_context(actx, apar);
            if (avcodec_open2(actx, adec, NULL) == 0) {
                AVChannelLayout outl; av_channel_layout_default(&outl, OCH);
                if (swr_alloc_set_opts2(&swr, &outl, AV_SAMPLE_FMT_S16, ORATE,
                                        &actx->ch_layout, actx->sample_fmt, actx->sample_rate, 0, NULL) == 0)
                    swr_init(swr);
                SDL_AudioSpec want; SDL_zero(want);
                want.freq = ORATE; want.format = AUDIO_S16SYS; want.channels = OCH; want.samples = 2048;
                adev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
                if (adev) SDL_PauseAudioDevice(adev, 0);
            }
        }
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
    int running = 1, paused = 0;
    Uint32 hud_until = SDL_GetTicks() + 4000;   // HUD visivel ao iniciar
    SDL_Event e;

    // Retoma de onde parou (só se fizer sentido: > 3s e não no finzinho).
    if (start_sec > 3 && (dur <= 0 || start_sec < dur - 5)) {
        av_seek_frame(fmt, -1, (int64_t)(start_sec * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);
        audio_clock = start_sec; cur_pos = start_sec;
        wall_start = av_gettime() / 1000000.0 - start_sec;
    }

    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_JOYBUTTONDOWN) {
                int b = e.jbutton.button;
                hud_until = SDL_GetTicks() + 4000;
                if (b == JOY_B || b == JOY_PLUS || b == JOY_MINUS) running = 0;
                else if (b == JOY_A) { paused = !paused; if (adev) SDL_PauseAudioDevice(adev, paused); }
                else if (b == JOY_R || b == JOY_L) {
                    double t = audio_clock + (b == JOY_R ? 15 : -15);
                    if (t < 0) t = 0;
                    if (dur > 0 && t > dur - 1) t = dur - 1;
                    av_seek_frame(fmt, -1, (int64_t)(t * AV_TIME_BASE), (b == JOY_L) ? AVSEEK_FLAG_BACKWARD : 0);
                    avcodec_flush_buffers(vctx); if (actx) avcodec_flush_buffers(actx);
                    if (adev) SDL_ClearQueuedAudio(adev);
                    wall_start = av_gettime() / 1000000.0 - t; audio_clock = t; cur_pos = t;
                }
            }
        }
        if (!running) break;
        if (paused) {   // continua desenhando (quadro congelado + HUD)
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, NULL, &dst);
            draw_hud(ren, title, cur_pos, dur, 1);
            SDL_RenderPresent(ren);
            SDL_Delay(30);
            continue;
        }

        int ret = av_read_frame(fmt, pkt);
        if (ret < 0) {  // fim do arquivo
            if (!adev || SDL_GetQueuedAudioSize(adev) < 8192) break;
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
                    if (paused || SDL_GetTicks() < hud_until) draw_hud(ren, title, cur_pos, dur, 0);
                    SDL_RenderPresent(ren);
                }
            }
        }
        av_packet_unref(pkt);
    }

    if (out_pos) *out_pos = cur_pos;
    if (out_dur) *out_dur = dur;

    if (adev) SDL_CloseAudioDevice(adev);
    if (sws) sws_freeContext(sws);
    if (yuv) av_frame_free(&yuv);
    if (swr) swr_free(&swr);
    if (actx) avcodec_free_context(&actx);
    avcodec_free_context(&vctx);
    av_frame_free(&frame); av_packet_free(&pkt);
    SDL_DestroyTexture(tex);
    avformat_close_input(&fmt);
    nplay_curl_avio_close(avio);
    return 0;
}
