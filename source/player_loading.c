#include "player_loading.h"

int player_loading_interrupt_can_draw(PlayerLoadingOwner owner,
                                      int on_render_thread,
                                      int presented_frame,
                                      int force_loading,
                                      unsigned now_ms,
                                      unsigned last_render_ms,
                                      unsigned interval_ms) {
    if (!on_render_thread || owner == PLAYER_LOADING_PLAYBACK) return 0;
    if (presented_frame && !force_loading) return 0;
    return now_ms - last_render_ms >= interval_ms;
}
