#include "player_clock.h"
#include <math.h>

double player_clock_master(double now, double video_pts, double audio_end_pts,
                           double audio_queued_seconds, int audio_recent,
                           double *wall_start, int first_frame) {
    if (!wall_start) return video_pts;
    if (first_frame) {
        *wall_start = now - video_pts;
        return video_pts;
    }
    double master = now - *wall_start;
    if (audio_recent && audio_queued_seconds > 0 &&
        isfinite(audio_end_pts) && isfinite(audio_queued_seconds)) {
        double audio_master = audio_end_pts - audio_queued_seconds;
        double drift = audio_master - master;
        if (isfinite(drift) && fabs(drift) <= 0.75) {
            double correction = drift * 0.04;
            if (correction > 0.004) correction = 0.004;
            if (correction < -0.004) correction = -0.004;
            *wall_start -= correction;
            master += correction;
        }
    }
    return master;
}

double player_clock_account_read(double *wall_start, double read_seconds,
                                 double queued_before_seconds, int got_packet,
                                 int playback_started) {
    if (!wall_start || !got_packet || !playback_started ||
        !isfinite(read_seconds) || !isfinite(queued_before_seconds) ||
        read_seconds < 0.25) return 0;
    double frozen = read_seconds - fmax(0, queued_before_seconds);
    if (frozen <= 0) return 0;
    *wall_start += frozen;
    return frozen;
}
