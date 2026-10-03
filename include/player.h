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
    EXIT_REASON_ERROR,
    EXIT_REASON_NEXT_EPISODE
} PlayerExitReason;

typedef int (*PlayerProgressCallback)(int item_id, int position_sec, int duration_sec,
                                      SDL_atomic_t *cancel, void *userdata);
typedef int (*PlayerRenewCallback)(const PlaybackSource *current, PlaybackSource *out,
                                   SDL_atomic_t *cancel, void *userdata);
typedef int (*PlayerHeartbeatCallback)(int session_id, SDL_atomic_t *cancel, void *userdata);
typedef int (*PlayerStopCallback)(int item_id, int session_id,
                                  SDL_atomic_t *cancel, void *userdata);

typedef struct {
    // Snapshot completo do contrato da API. Para arquivos locais, fica zerado e
    // os campos legados abaixo continuam sendo usados.
    PlaybackSource playback;
    int item_id;
    int session_id;
    int source_id;

    DeliveryType delivery;

    const char *title;
    const char *subtitle;
    const char *overview;
    const char *next_title;
    int has_next;
    const char *section;
    const char *container;
    const char *url;

    int season;
    int episode;

    double start_sec;
    // Preferencia da conta e continuidade entre episodios. audio_hint e 1-based.
    int audio_pref; // 0=dublado, 1=legendado, 2=tanto faz
    // Uma versao Dublado/Legendado escolhida explicitamente no detalhe da serie
    // deve vencer a ultima faixa manual salva. Sem esta flag, a escolha manual
    // feita no player continua valendo entre obras e episodios.
    int audio_pref_explicit;
    int audio_hint;
    const char *audio_hint_language; // pt/en/ja/... ou und; opcional
    int audio_hint_priority; // 1=mesma reproducao/idioma; 2=faixa manual exata
    int subtitle_hint; // 0=desligada; >0=faixa exata (1-based)
    int subtitle_hint_priority; // 1 quando subtitle_hint deve vencer a preferencia salva

    PlayerProgressCallback progress_cb;
    PlayerRenewCallback renew_cb;
    PlayerRenewCallback fallback_cb;
    PlayerHeartbeatCallback heartbeat_cb;
    PlayerStopCallback stop_cb;
    void *userdata;
} PlayerRequest;

typedef struct {
    PlayerExitReason reason;
    double position;
    double duration;
    int presented_frame;
    PlayerState final_state;
    int recovery_count;
    int audio_index; // faixa ativa ao sair (1-based; 0 = sem audio)
    char audio_language[8]; // idioma normalizado ou "und"
    int subtitle_index; // 0=desligada; >0=faixa ativa (1-based)
} PlayerResult;

int player_run(SDL_Renderer *ren, SDL_Joystick *joy, PlayerRequest *request, PlayerResult *result);

const char *player_last_error(void);
int player_last_error_access_expired(void);
