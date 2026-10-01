#pragma once
#include "hls_manifest.h"
#include "cJSON.h"
#include <stddef.h>

int hot_subtitle_session_valid(const char *sid);
// index -1 requests probe, 0..100 requests WebVTT. Only configured HTTPS origin.
int hot_subtitle_url(const char *base, const char *sid, int index, char *out, size_t cap);
int hot_subtitle_tracks(const cJSON *probe, const char *base, const char *sid,
                        HlsManifestTrack *tracks, int capacity);
