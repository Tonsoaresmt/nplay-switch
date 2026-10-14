#include "subtitle_catalog.h"
#include <string.h>

int subtitle_catalog_add(HlsManifestTrack *tracks, int *streams,
                         unsigned char *origins, int *count,
                         const HlsManifestTrack *track, int stream, int origin) {
    if (!tracks || !streams || !origins || !count || !track ||
        *count < 0 || *count > HLS_MANIFEST_TRACK_CAP ||
        origin < SUBTITLE_NATIVE || origin > SUBTITLE_HOT ||
        (origin == SUBTITLE_NATIVE ? stream < 0 : !track->uri[0])) return -1;
    for (int i = 0; i < *count; i++) {
        if (origins[i] != origin) continue;
        if (origin == SUBTITLE_NATIVE ? streams[i] == stream :
            (!strcmp(tracks[i].uri, track->uri) &&
             !strcmp(tracks[i].name, track->name) &&
             !strcmp(tracks[i].language, track->language) &&
             tracks[i].forced == track->forced)) return i;
    }
    if (*count == HLS_MANIFEST_TRACK_CAP) return -1;
    int slot = (*count)++;
    tracks[slot] = *track;
    streams[slot] = origin == SUBTITLE_NATIVE ? stream : -1;
    origins[slot] = (unsigned char)origin;
    return slot;
}
