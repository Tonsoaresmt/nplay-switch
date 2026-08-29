// player.h - player de video via ffmpeg (decode) + SDL (render/audio).
#pragma once
#include <SDL.h>
#include "api.h"

typedef enum {
    PLAYER_RESOLVING,
    PLAYER_PREPARING,
    PLAYER_BUFFERING,
    PLAYER_PLAYING,
    PLAYER_PAUSED,
    PLAYER_SEEKING,
    PLAYER_FINISHED,
    PLAYER_ERROR
} PlayerState;

typedef enum {
    EXIT_REASON_NATURAL,
    EXIT_REASON_USER,
    EXIT_REASON_ERROR
} PlayerExitReason;

typedef void (*PlayerProgressCallback)(int item_id, int position_sec, int duration_sec, void *userdata);
typedef int (*PlayerResolveCallback)(int item_id, const char *quality, PlaybackSource *out, void *userdata);

typedef struct {
    int item_id;
    int session_id;
    int source_id;

    DeliveryType delivery;

    const char *title;
    const char *section;
    const char *container;
    const char *url;

    int season;
    int episode;

    double start_sec;

    PlayerProgressCallback progress_cb;
    PlayerResolveCallback resolve_cb;
    void *userdata;
} PlayerRequest;

typedef struct {
    PlayerExitReason reason;
    double position;
    double duration;
} PlayerResult;

int player_run(SDL_Renderer *ren, SDL_Joystick *joy, PlayerRequest *request, PlayerResult *result);
int player_play(SDL_Renderer *ren, SDL_Joystick *joy, const char *url, int is_hls,
                const char *title, double start_sec, double *out_pos, double *out_dur);

const char *player_last_error(void);

