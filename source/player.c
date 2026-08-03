// player.c - player de video: ffmpeg decodifica, SDL desenha (textura YUV) e toca
// o audio (SDL Audio + swresample). Sincroniza o video pelo relogio do audio.
// Software decode por enquanto (NVDEC/hardware fica pra otimizacao futura).
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

#define JOY_A 0
#define JOY_B 1
#define JOY_L 6
#define JOY_R 7
#define JOY_PLUS 10
#define JOY_MINUS 11

int player_play(SDL_Renderer *ren, SDL_Joystick *joy, const char *url) {
    (void)joy;
    // tela preta enquanto abre (pode levar alguns segundos)
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren); SDL_RenderPresent(ren);

    // O switch-ffmpeg nao tem TLS, entao NAO abrimos a URL direto: pluga o libcurl
    // (com mbedtls) como camada de I/O do ffmpeg. Funciona pra https do R2 (animes)
    // e pro arquivo do acelerador (Range). Ver curl_avio.c.
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
    int dw = 1280, dh = 720;
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
    int running = 1, paused = 0;
    SDL_Event e;

    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_JOYBUTTONDOWN) {
                int b = e.jbutton.button;
                if (b == JOY_B || b == JOY_PLUS || b == JOY_MINUS) running = 0;
                else if (b == JOY_A) { paused = !paused; if (adev) SDL_PauseAudioDevice(adev, paused); }
                else if (b == JOY_R || b == JOY_L) {
                    double t = audio_clock + (b == JOY_R ? 15 : -15);
                    if (t < 0) t = 0;
                    av_seek_frame(fmt, -1, (int64_t)(t * AV_TIME_BASE), (b == JOY_L) ? AVSEEK_FLAG_BACKWARD : 0);
                    avcodec_flush_buffers(vctx); if (actx) avcodec_flush_buffers(actx);
                    if (adev) SDL_ClearQueuedAudio(adev);
                    wall_start = av_gettime() / 1000000.0 - t; audio_clock = t;
                }
            }
        }
        if (!running) break;
        if (paused) { SDL_Delay(30); continue; }

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
                    double delay = vpts - master;
                    if (delay > 0.001) { if (delay > 0.4) delay = 0.4; SDL_Delay((Uint32)(delay * 1000)); }
                    AVFrame *u = frame;
                    if (sws) { sws_scale(sws, (const uint8_t * const *)frame->data, frame->linesize, 0, vh, yuv->data, yuv->linesize); u = yuv; }
                    SDL_UpdateYUVTexture(tex, NULL, u->data[0], u->linesize[0], u->data[1], u->linesize[1], u->data[2], u->linesize[2]);
                    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
                    SDL_RenderCopy(ren, tex, NULL, &dst);
                    SDL_RenderPresent(ren);
                }
            }
        }
        av_packet_unref(pkt);
    }

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
