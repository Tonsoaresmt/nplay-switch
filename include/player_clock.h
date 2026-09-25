#ifndef NPLAY_PLAYER_CLOCK_H
#define NPLAY_PLAYER_CLOCK_H

// The wall clock is the stable video timeline. SDL's queued byte count is only
// an estimate of audible time, so audio may gently correct it but never reset
// it by hundreds of milliseconds on every decoded frame.
double player_clock_master(double now, double video_pts, double audio_end_pts,
                           double audio_queued_seconds, int audio_recent,
                           double *wall_start, int first_frame);

// A blocking read can outlast all queued audio. Pause the media timeline for
// the uncovered part so playback resumes at the next frame instead of skipping
// several seconds of a film after a network interruption.
double player_clock_account_read(double *wall_start, double read_seconds,
                                 double queued_before_seconds, int got_packet,
                                 int playback_started);

#endif
