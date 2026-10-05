#pragma once
#include <stdint.h>
typedef struct { char language[8], name[96]; int forced; uint64_t rendition; } SubtitleTrackKey;
uint64_t subtitle_rendition_key(const char *uri); // excludes known auth only; never logged or used as cache key
typedef struct {
    int valid, index, source_id; // index=-1 is deliberate OFF; valid=0 is UNSET
    uint64_t roster;
    SubtitleTrackKey key;
} SubtitleChoice;
void subtitle_choice_capture(SubtitleChoice *out, const SubtitleTrackKey *tracks,
                             int count, int index, int source_id);
// -2: missing/ambiguous identity; -1: deliberate OFF.
int subtitle_choice_resolve(const SubtitleChoice *choice, const SubtitleTrackKey *tracks,
                            int count, int source_id);
