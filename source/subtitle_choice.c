#include "subtitle_choice.h"
#include <string.h>
#include <strings.h>
static int auth_parameter(const unsigned char *name, size_t len) {
    const char *keys[] = { "token", "signature", "expires", "X-Amz-Algorithm",
        "X-Amz-Credential", "X-Amz-Date", "X-Amz-Expires", "X-Amz-SignedHeaders",
        "X-Amz-Signature", "X-Amz-Security-Token" };
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        if (strlen(keys[i]) == len && !strncasecmp((const char *)name, keys[i], len)) return 1;
    return 0;
}
uint64_t subtitle_rendition_key(const char *uri) {
    if (!uri || !*uri) return 0;
    uint64_t h = UINT64_C(14695981039346656037);
    const unsigned char *p = (const unsigned char *)uri;
    for (; *p && *p != '?' && *p != '#'; p++) h = (h ^ *p) * UINT64_C(1099511628211);
    // Selection identity only, NEVER a cache/download key. Preserve semantic
    // query parameters (language/format/etc); ignore only known auth fields.
    if (*p == '?') for (p++; *p && *p != '#'; ) {
        const unsigned char *start = p, *equal = NULL;
        while (*p && *p != '&' && *p != '#') { if (*p == '=' && !equal) equal = p; p++; }
        if (!equal || !auth_parameter(start, (size_t)(equal - start))) {
            h = (h ^ '&') * UINT64_C(1099511628211);
            for (const unsigned char *q = start; q < p; q++) h = (h ^ *q) * UINT64_C(1099511628211);
        }
        if (*p == '&') p++;
    }
    return h;
}
static int equal(const SubtitleTrackKey *a, const SubtitleTrackKey *b) {
    return a->forced == b->forced && !strcmp(a->language, b->language) && !strcmp(a->name, b->name);
}
static uint64_t roster(const SubtitleTrackKey *tracks, int count) {
    uint64_t h = UINT64_C(14695981039346656037);
    for (int i = 0; i < count; i++) {
        const char *parts[] = { tracks[i].language, tracks[i].name };
        for (int p = 0; p < 2; p++) {
            for (const unsigned char *s = (const unsigned char *)parts[p]; *s; s++)
                h = (h ^ *s) * UINT64_C(1099511628211);
            h = (h ^ 255u) * UINT64_C(1099511628211);
        }
        h = (h ^ (unsigned)tracks[i].forced) * UINT64_C(1099511628211);
        h = (h ^ tracks[i].rendition) * UINT64_C(1099511628211);
    }
    return (h ^ (unsigned)count) * UINT64_C(1099511628211);
}
void subtitle_choice_capture(SubtitleChoice *out, const SubtitleTrackKey *tracks,
                             int count, int index, int source_id) {
    if (!out || index < -1 || index >= count) return;
    memset(out, 0, sizeof(*out));
    out->valid = 1; out->index = index; out->source_id = source_id;
    out->roster = roster(tracks, count);
    if (index >= 0) out->key = tracks[index];
}
int subtitle_choice_resolve(const SubtitleChoice *choice, const SubtitleTrackKey *tracks,
                            int count, int source_id) {
    if (!choice || !choice->valid) return -2;
    if (choice->index < 0) return -1;
    if (source_id > 0 && source_id == choice->source_id &&
        choice->index < count && choice->roster == roster(tracks, count) &&
        equal(&choice->key, &tracks[choice->index])) return choice->index;
    int match = -2;
    int exact = -2, exact_count = 0;
    for (int i = 0; i < count; i++) {
        if (!equal(&choice->key, &tracks[i])) continue;
        if (choice->key.rendition && tracks[i].rendition == choice->key.rendition) {
            exact = i; exact_count++;
        }
        match = match == -2 ? i : -3;
    }
    if (exact_count == 1) return exact;
    return match >= 0 ? match : -2;
}
