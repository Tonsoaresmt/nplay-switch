#include <assert.h>
#include "player_recovery.h"
int main(void) {
    assert(!player_recovery_should_renew_after_stall(11999, 1, 1, 0, 0));
    assert(player_recovery_should_renew_after_stall(12000, 1, 1, 0, 0));
    assert(!player_recovery_should_renew_after_stall(60000, 1, 0, 0, 0));
    assert(!player_recovery_should_renew_after_stall(60000, 1, 1, 1, 0));
    assert(!player_recovery_should_renew_after_stall(60000, 1, 1, 0, 8192));
    assert(!player_recovery_should_renew_after_stall(60000, 0, 1, 0, 0));
    return 0;
}
