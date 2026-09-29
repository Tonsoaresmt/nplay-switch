#pragma once

typedef enum {
    PLAYER_LOADING_OPENING = 0,
    PLAYER_LOADING_PLAYBACK,
    PLAYER_LOADING_OPERATION
} PlayerLoadingOwner;

int player_loading_interrupt_can_draw(PlayerLoadingOwner owner,
                                      int on_render_thread,
                                      int presented_frame,
                                      int force_loading,
                                      unsigned now_ms,
                                      unsigned last_render_ms,
                                      unsigned interval_ms);
