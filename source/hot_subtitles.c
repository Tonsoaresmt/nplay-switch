#include "hot_subtitles.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

int hot_subtitle_session_valid(const char *sid) {
    if (!sid) return 0;
    size_t n = strlen(sid);
    return n >= 16 && n < 96 && strspn(sid,
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == n;
}

int hot_subtitle_url(const char *base, const char *sid, int index, char *out, size_t cap) {
    if (!out || !cap) return 0;
    out[0] = 0;
    if (!base || strncmp(base, "https://", 8) || !base[8] ||
        strpbrk(base + 8, "?#@\r\n") || !hot_subtitle_session_valid(sid) || index < -1 || index > 100) return 0;
    size_t n = strlen(base);
    while (n && base[n-1] == '/') n--;
    int written = index < 0
        ? snprintf(out, cap, "%.*s/api/stream/hot/%s/probe", (int)n, base, sid)
        : snprintf(out, cap, "%.*s/api/stream/hot/%s/subtitles/%d.vtt", (int)n, base, sid, index);
    if (written < 0 || (size_t)written >= cap) { out[0] = 0; return 0; }
    return 1;
}

static const char *string_field(const cJSON *o, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}

int hot_subtitle_tracks(const cJSON *probe, const char *base, const char *sid,
                        HlsManifestTrack *tracks, int capacity) {
    if (!tracks || capacity <= 0 || !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(probe, "ok"))) return 0;
    if (capacity > HLS_MANIFEST_TRACK_CAP) capacity = HLS_MANIFEST_TRACK_CAP;
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(probe, "subtitles"), *entry;
    if (!cJSON_IsArray(list)) return 0;
    int count = 0, seen[101] = {0};
    cJSON_ArrayForEach(entry, list) {
        if (count == capacity) break;
        const cJSON *idx = cJSON_GetObjectItemCaseSensitive(entry, "index");
        if (!cJSON_IsNumber(idx) || !isfinite(idx->valuedouble) || idx->valuedouble < 0 ||
            idx->valuedouble > 100 || floor(idx->valuedouble) != idx->valuedouble || seen[idx->valueint]) continue;
        const char *codec = string_field(entry, "codec");
        if (codec[0] && strcmp(codec,"subrip") && strcmp(codec,"ass") && strcmp(codec,"ssa") &&
            strcmp(codec,"webvtt") && strcmp(codec,"mov_text") && strcmp(codec,"text") && strcmp(codec,"srt")) continue;
        HlsManifestTrack track = {0};
        if (!hot_subtitle_url(base, sid, idx->valueint, track.uri, sizeof(track.uri))) continue;
        const char *label = string_field(entry, "label");
        if (!label[0]) label = string_field(entry, "title");
        snprintf(track.name, sizeof(track.name), "%s", label);
        snprintf(track.language, sizeof(track.language), "%s", string_field(entry, "language"));
        track.is_default = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(entry, "default"));
        track.forced = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(entry, "forced"));
        seen[idx->valueint] = 1;
        tracks[count++] = track;
    }
    return count;
}
