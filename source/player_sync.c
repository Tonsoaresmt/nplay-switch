#include "player_sync.h"

int player_sync_final_progress_needed(int presented_frame, int position_sec,
                                      int last_saved_position_sec) {
    if (!presented_frame || position_sec <= 5) return 0;
    if (last_saved_position_sec < 0) return 1;
    int delta = position_sec - last_saved_position_sec;
    if (delta < 0) delta = -delta;
    return delta >= 2;
}

int player_sync_exit_should_cancel(unsigned elapsed_ms, int worker_finished) {
    return !worker_finished && elapsed_ms >= PLAYER_SYNC_EXIT_GRACE_MS;
}
