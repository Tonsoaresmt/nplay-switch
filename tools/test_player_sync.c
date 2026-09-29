#include "player_sync.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(!player_sync_final_progress_needed(0, 120, -1));
    assert(!player_sync_final_progress_needed(1, 5, -1));
    assert(player_sync_final_progress_needed(1, 6, -1));
    assert(!player_sync_final_progress_needed(1, 100, 99));
    assert(player_sync_final_progress_needed(1, 101, 99));
    assert(player_sync_final_progress_needed(1, 90, 100));

    assert(!player_sync_exit_should_cancel(1199, 0));
    assert(player_sync_exit_should_cancel(1200, 0));
    assert(!player_sync_exit_should_cancel(5000, 1));
    puts("player sync policy: ok");
    return 0;
}
