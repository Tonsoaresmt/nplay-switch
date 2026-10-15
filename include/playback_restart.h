#pragma once
#include "api.h"

/* Server-backed restart is separate from byte Range seek. Only the exact
 * debrid media endpoint with a validated opaque SID can use this contract. */
int playback_restart_supported(const PlaybackSource *source, const char *base);
int playback_restart_urls(const PlaybackSource *source, const char *base, double target,
                           char *probe, size_t probe_cap, char *video, size_t video_cap);
int playback_restart_plan(const cJSON *json, double target, double *start, double *duration);
double playback_restart_clock(double timestamp, double first_timestamp, double base);
