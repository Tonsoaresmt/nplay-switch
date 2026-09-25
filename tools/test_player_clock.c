#include "player_clock.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void) {
    double anchor = 0;
    double master = player_clock_master(100.0, 35.7, 0, 0, 0, &anchor, 1);
    assert(fabs(master - 35.7) < 0.00001);
    assert(fabs(anchor - 64.3) < 0.00001);

    // A timestamp jump in remuxed audio used to reset the master every frame,
    // producing 250-350 ms video sleeps. A bad audio estimate cannot do that.
    for (int frame = 1; frame <= 900; frame++) {
        double now = 100.0 + frame / 30.0;
        double pts = 35.7 + frame / 30.0;
        double bad_audio_end = pts + 1.6 - 1.2;
        master = player_clock_master(now, pts, bad_audio_end, 1.6, 1,
                                     &anchor, 0);
        assert(fabs(pts - master) < 0.08);
    }

    // Valid audio can nudge the video clock but by at most 4 ms per frame.
    double before = anchor;
    master = player_clock_master(131.0, 66.7, 68.4, 1.6, 1, &anchor, 0);
    assert(fabs(anchor - before) <= 0.004001);
    assert(isfinite(master));

    // 1.6 seconds of audio survive a 5.6-second direct TorBox read. Freeze the
    // media timeline for the remaining four seconds; don't skip that content.
    double before_stall = anchor;
    double frozen = player_clock_account_read(&anchor, 5.6, 1.6, 1, 1);
    assert(fabs(frozen - 4.0) < 0.00001);
    assert(fabs(anchor - before_stall - 4.0) < 0.00001);
    assert(player_clock_account_read(&anchor, 5.6, 0, 0, 1) == 0);
    assert(player_clock_account_read(&anchor, 5.6, 0, 1, 0) == 0);
    // The packet now resumes near the expected media time, without the old
    // 350 ms repeated sleeps caused by an audio timestamp jump.
    master = player_clock_master(136.0, 66.75, 0, 0, 0, &anchor, 0);
    assert(master > 66.0 && master < 68.0);

    // Pausing and seeking explicitly rebase; the first frame is never dropped.
    master = player_clock_master(250.0, 121.0, 0, 0, 0, &anchor, 1);
    assert(fabs(master - 121.0) < 0.00001);
    puts("player clock simulation: ok");
    return 0;
}
