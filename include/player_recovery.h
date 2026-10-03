// Politica pura para escalar uma interrupcao HLS visivel. Sem SDL/FFmpeg para
// manter o limite coberto por teste de host.
#ifndef NPLAY_PLAYER_RECOVERY_H
#define NPLAY_PLAYER_RECOVERY_H

#define PLAYER_HLS_STALL_RENEW_MS 12000u
#define PLAYER_RECOVERY_AUDIO_GRACE_BYTES 8192u

static inline int player_recovery_should_renew_after_stall(
    unsigned stalled_ms, int native_hls, int frame_presented,
    int track_switch_pending, unsigned audio_queued_bytes) {
    return native_hls && frame_presented && !track_switch_pending &&
           audio_queued_bytes < PLAYER_RECOVERY_AUDIO_GRACE_BYTES &&
           stalled_ms >= PLAYER_HLS_STALL_RENEW_MS;
}
#endif
