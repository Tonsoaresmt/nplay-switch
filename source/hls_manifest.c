#include "hls_manifest.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static int line_contains(const char *line, size_t len, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0 || len < nlen) return 0;
    for (size_t i = 0; i + nlen <= len; i++)
        if (!memcmp(line + i, needle, nlen)) return 1;
    return 0;
}

static int attr_copy(const char *line, size_t len, const char *key,
                     char *out, size_t out_size) {
    if (!line || !key || !out || out_size == 0) return 0;
    out[0] = 0;
    size_t key_len = strlen(key);
    for (size_t i = 0; i + key_len + 1 <= len; i++) {
        if ((i == 0 || line[i - 1] == ',' || line[i - 1] == ':') &&
            !strncasecmp(line + i, key, key_len) && line[i + key_len] == '=') {
            size_t at = i + key_len + 1, end = at;
            if (at < len && line[at] == '"') {
                at++; end = at;
                while (end < len && line[end] != '"') end++;
            } else {
                while (end < len && line[end] != ',' && line[end] != '\r') end++;
            }
            size_t copy = end - at;
            if (copy >= out_size) copy = out_size - 1;
            memcpy(out, line + at, copy);
            out[copy] = 0;
            return 1;
        }
    }
    return 0;
}

int hls_manifest_subtitle_tracks(const char *body, size_t len,
                                 HlsManifestTrack *tracks, int capacity) {
    if (!body || !tracks || capacity <= 0) return 0;
    int count = 0;
    const char *end = body + len, *line = body;
    while (line < end && count < capacity) {
        const char *next = memchr(line, '\n', (size_t)(end - line));
        if (!next) next = end;
        size_t line_len = (size_t)(next - line);
        while (line_len && (*line == ' ' || *line == '\t' ||
                            (unsigned char)*line == 0xef ||
                            (unsigned char)*line == 0xbb ||
                            (unsigned char)*line == 0xbf)) {
            line++; line_len--;
        }
        if (line_len > 13 && !strncasecmp(line, "#EXT-X-MEDIA:", 13) &&
            line_contains(line + 13, line_len - 13, "TYPE=SUBTITLES")) {
            HlsManifestTrack *track = &tracks[count];
            memset(track, 0, sizeof(*track));
            if (attr_copy(line, line_len, "URI", track->uri, sizeof(track->uri)) &&
                track->uri[0]) {
                attr_copy(line, line_len, "NAME", track->name, sizeof(track->name));
                attr_copy(line, line_len, "LANGUAGE", track->language,
                          sizeof(track->language));
                char flag[8];
                track->is_default = attr_copy(line, line_len, "DEFAULT", flag,
                                              sizeof(flag)) && !strcasecmp(flag, "YES");
                track->forced = attr_copy(line, line_len, "FORCED", flag,
                                          sizeof(flag)) && !strcasecmp(flag, "YES");
                count++;
            }
        }
        line = next < end ? next + 1 : end;
    }
    return count;
}

int hls_manifest_resolve_url(const char *base, const char *reference,
                             char *out, size_t out_size) {
    if (!base || !reference || !out || out_size == 0) return 0;
    out[0] = 0;
    if (!strncasecmp(reference, "http://", 7) ||
        !strncasecmp(reference, "https://", 8)) {
        int n = snprintf(out, out_size, "%s", reference);
        return n >= 0 && (size_t)n < out_size;
    }
    const char *scheme = strstr(base, "://");
    if (!scheme) return 0;
    const char *authority = scheme + 3;
    const char *path = strchr(authority, '/');
    const char *base_end = strpbrk(base, "?#");
    if (!base_end) base_end = base + strlen(base);
    if (!path || path > base_end) path = base_end;
    const char *base_query = strchr(base, '?');
    const char *base_fragment = strchr(base, '#');
    size_t query_len = base_query
        ? (size_t)((base_fragment && base_fragment > base_query
                    ? base_fragment : base + strlen(base)) - base_query) : 0;
    int inherit_query = base_query && !strchr(reference, '?');
    if (reference[0] == '/') {
        int prefix = (int)(path - base);
        int n = snprintf(out, out_size, "%.*s%s%.*s", prefix, base, reference,
                         inherit_query ? (int)query_len : 0,
                         inherit_query ? base_query : "");
        return n >= 0 && (size_t)n < out_size;
    }
    const char *slash = base_end;
    while (slash > path && slash[-1] != '/') slash--;
    int prefix = (int)(slash - base);
    int n = snprintf(out, out_size, "%.*s%s%.*s", prefix, base, reference,
                     inherit_query ? (int)query_len : 0,
                     inherit_query ? base_query : "");
    return n >= 0 && (size_t)n < out_size;
}

int hls_manifest_media_counts(const char *body, size_t len,
                              int *audio, int *subtitles) {
    if (audio) *audio = 0;
    if (subtitles) *subtitles = 0;
    if (!body || len == 0) return 0;
    int found = 0;
    const char *end = body + len;
    const char *line = body;
    while (line < end) {
        const char *next = memchr(line, '\n', (size_t)(end - line));
        if (!next) next = end;
        size_t line_len = (size_t)(next - line);
        if (line_len > 13 && !strncmp(line, "#EXT-X-MEDIA:", 13)) {
            found = 1;
            if (line_contains(line + 13, line_len - 13, "TYPE=AUDIO")) {
                if (audio) (*audio)++;
            } else if (line_contains(line + 13, line_len - 13, "TYPE=SUBTITLES")) {
                if (subtitles) (*subtitles)++;
            }
        }
        line = next < end ? next + 1 : end;
    }
    return found;
}
