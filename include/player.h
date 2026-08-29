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
typedef int (*PlayerRenewCallback)(const PlaybackSource *current, PlaybackSource *out, void *userdata);
typedef int (*PlayerHeartbeatCallback)(int session_id, void *userdata);

typedef struct {
    // Snapshot completo do contrato da API. Para arquivos locais, fica zerado e
    // os campos legados abaixo continuam sendo usados.
    PlaybackSource playback;
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
    PlayerRenewCallback renew_cb;
    PlayerRenewCallback fallback_cb;
    PlayerHeartbeatCallback heartbeat_cb;
    void *userdata;
} PlayerRequest;

typedef struct {
    PlayerExitReason reason;
    double position;
    double duration;
    PlayerState final_state;
    int recovery_count;
} PlayerResult;

int player_run(SDL_Renderer *ren, SDL_Joystick *joy, PlayerRequest *request, PlayerResult *result);

const char *player_last_error(void);
