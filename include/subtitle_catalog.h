#pragma once
#include "hls_manifest.h"

enum { SUBTITLE_NATIVE = 0, SUBTITLE_HLS = 1, SUBTITLE_HOT = 2 };
/* Append only: indices already visible to the UI never move. Distinct URLs,
 * native streams and forced/sign tracks must not collapse by language. */
int subtitle_catalog_add(HlsManifestTrack *tracks, int *streams,
                         unsigned char *origins, int *count,
                         const HlsManifestTrack *track, int stream, int origin);
