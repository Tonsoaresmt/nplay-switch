#include <assert.h>
#include "player_recovery.h"
int main(void) {
    // Failed seek/renewal and an empty pipeline cannot erase a 50-minute film.
    double pos = 3000;
    for (int i = 0; i < 5; ++i) {
        pos = player_recovery_position(pos, 0, 0, 0);
        assert(pos == 3000);
    }
    assert(player_recovery_position(pos, 0, 1, 0) == 3000);
    assert(player_recovery_position(pos, 3060, 0, 1) == 3060);
    assert(player_recovery_position(pos, 2940, 0, 1) == 2940);
    assert(player_recovery_position(pos, 0, 0, 1) == 0);
    assert(player_recovery_position(pos, 3001, 1, 0) == 3001);
    assert(player_recovery_position(pos, NAN, 1, 0) == pos);
    assert(player_recovery_position(pos, -1, 1, 1) == pos);
    assert(!player_recovery_should_renew_after_stall(11999, 1, 1, 0, 0));
    assert(player_recovery_should_renew_after_stall(12000, 1, 1, 0, 0));
    assert(!player_recovery_should_renew_after_stall(60000, 1, 0, 0, 0));
    assert(!player_recovery_should_renew_after_stall(60000, 1, 1, 1, 0));
    assert(!player_recovery_should_renew_after_stall(60000, 1, 1, 0, 8192));
    assert(!player_recovery_should_renew_after_stall(60000, 0, 1, 0, 0));
    return 0;
}
