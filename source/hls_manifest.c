#include "hls_manifest.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

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

static int resolve_url(const char *base, const char *reference,
                       char *out, size_t out_size, int allow_inherit) {
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
    int inherit_query = allow_inherit && base_query && !strchr(reference, '?');
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

int hls_manifest_resolve_url(const char *base, const char *reference,
                             char *out, size_t out_size) {
    return resolve_url(base, reference, out, out_size, 1);
}

int hls_manifest_resolve_like_ffmpeg(const char *base, const char *reference,
                                     char *out, size_t out_size) {
    return resolve_url(base, reference, out, out_size, 0);
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

static int line_starts(const char *line, size_t len, const char *prefix) {
    size_t plen = strlen(prefix);
    return len >= plen && !strncasecmp(line, prefix, plen);
}

typedef struct {
    char *data;
    size_t len, cap;
    int failed;
} TrimBuffer;

static void trim_put(TrimBuffer *b, const char *text, size_t len) {
    if (b->failed) return;
    if (b->len + len + 2 > b->cap) {
        size_t cap = b->cap ? b->cap : 1024;
        while (b->len + len + 2 > cap) cap *= 2;
        char *grown = realloc(b->data, cap);
        if (!grown) { b->failed = 1; return; }
        b->data = grown;
        b->cap = cap;
    }
    memcpy(b->data + b->len, text, len);
    b->len += len;
    b->data[b->len++] = '\n';
    b->data[b->len] = 0;
}

int hls_media_playlist_trim(const char *body, size_t len, double start,
                            char **out, size_t *out_len,
                            double *segment_start, double *total) {
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (segment_start) *segment_start = 0;
    if (total) *total = 0;
    if (!body || len == 0 || !out || !out_len) return 0;
    // So texto HLS: secoes de init e legendas tambem passam pelo AVIO de metadados.
    size_t bom = len >= 3 && !memcmp(body, "\xef\xbb\xbf", 3) ? 3 : 0;
    if (len < bom + 7 || strncmp(body + bom, "#EXTM3U", 7)) return 0;

    // Primeira passada: valida o formato e escolhe o segmento de corte.
    const char *end = body + len;
    long long media_sequence = 0, discontinuity_sequence = 0;
    int vod = 0, segments = 0, cut = -1;
    double elapsed = 0, cut_start = 0, pending_duration = -1;
    for (const char *line = body; line < end;) {
        const char *next = memchr(line, '\n', (size_t)(end - line));
        if (!next) next = end;
        size_t n = (size_t)(next - line);
        while (n && (line[n - 1] == '\r' || line[n - 1] == ' ')) n--;
        if (line_starts(line, n, "#EXT-X-STREAM-INF") ||
            line_starts(line, n, "#EXT-X-MEDIA:")) return 0;   // master
        if (line_starts(line, n, "#EXT-X-ENDLIST") ||
            (line_starts(line, n, "#EXT-X-PLAYLIST-TYPE:") &&
             n >= 24 && !strncasecmp(line + 21, "VOD", 3))) vod = 1;
        if (line_starts(line, n, "#EXT-X-BYTERANGE:") &&
            !memchr(line, '@', n)) return 0;                  // offset implicito
        if (line_starts(line, n, "#EXT-X-MEDIA-SEQUENCE:"))
            media_sequence = atoll(line + 22);
        if (line_starts(line, n, "#EXT-X-DISCONTINUITY-SEQUENCE:"))
            discontinuity_sequence = atoll(line + 30);
        if (line_starts(line, n, "#EXTINF:")) pending_duration = atof(line + 8);
        if (n && line[0] != '#') {                             // URI do segmento
            if (pending_duration < 0) return 0;
            if (elapsed <= start + 0.001) { cut = segments; cut_start = elapsed; }
            elapsed += pending_duration;
            segments++;
            pending_duration = -1;
        }
        line = next < end ? next + 1 : end;
    }
    if (total) *total = elapsed;
    if (!vod || segments < 2 || cut <= 0 || start >= elapsed) return 0;
    // A descontinuidade do proprio segmento de corte continua no corpo copiado.
    // Antes dele, cada uma ja vista avanca DISCONTINUITY-SEQUENCE.
    int seen = 0;

    // Segunda passada: cabecalho, MAP/KEY ativos no corte e segmentos dali em diante.
    TrimBuffer b = {0};
    char header[96];
    const char *map = NULL, *key = NULL;
    size_t map_len = 0, key_len = 0;
    int index = 0, in_body = 0, wrote_state = 0;
    for (const char *line = body; line < end;) {
        const char *next = memchr(line, '\n', (size_t)(end - line));
        if (!next) next = end;
        size_t n = (size_t)(next - line);
        while (n && (line[n - 1] == '\r' || line[n - 1] == ' ')) n--;
        int is_uri = n && line[0] != '#';
        if (!in_body) {
            if (line_starts(line, n, "#EXTINF:") || line_starts(line, n, "#EXT-X-MAP:") ||
                line_starts(line, n, "#EXT-X-KEY:") || line_starts(line, n, "#EXT-X-BYTERANGE:") ||
                (line_starts(line, n, "#EXT-X-DISCONTINUITY") &&
                 !line_starts(line, n, "#EXT-X-DISCONTINUITY-SEQUENCE")) ||
                line_starts(line, n, "#EXT-X-PROGRAM-DATE-TIME:") || is_uri) {
                in_body = 1;
            } else {
                if (!line_starts(line, n, "#EXT-X-MEDIA-SEQUENCE:") &&
                    !line_starts(line, n, "#EXT-X-DISCONTINUITY-SEQUENCE:") && n)
                    trim_put(&b, line, n);
                line = next < end ? next + 1 : end;
                continue;
            }
        }
        if (index < cut) {
            if (line_starts(line, n, "#EXT-X-MAP:")) { map = line; map_len = n; }
            if (line_starts(line, n, "#EXT-X-KEY:")) { key = line; key_len = n; }
            if (line_starts(line, n, "#EXT-X-DISCONTINUITY") &&
                !line_starts(line, n, "#EXT-X-DISCONTINUITY-SEQUENCE")) seen++;
            if (is_uri) index++;
        } else {
            if (!wrote_state) {
                snprintf(header, sizeof(header), "#EXT-X-MEDIA-SEQUENCE:%lld",
                         media_sequence + cut);
                trim_put(&b, header, strlen(header));
                if (discontinuity_sequence || seen) {
                    snprintf(header, sizeof(header), "#EXT-X-DISCONTINUITY-SEQUENCE:%lld",
                             discontinuity_sequence + seen);
                    trim_put(&b, header, strlen(header));
                }
                if (key) trim_put(&b, key, key_len);
                if (map) trim_put(&b, map, map_len);
                wrote_state = 1;
            }
            if (n) trim_put(&b, line, n);
        }
        line = next < end ? next + 1 : end;
    }
    if (b.failed || !wrote_state) { free(b.data); return 0; }
    *out = b.data;
    *out_len = b.len;
    if (segment_start) *segment_start = cut_start;
    return 1;
}

// Percorre as linhas do manifesto sem BOM/espacos iniciais nem CR final.
typedef struct { const char *at, *end; } LineCursor;
static int next_line(LineCursor *cursor, const char **line, size_t *len) {
    if (cursor->at >= cursor->end) return 0;
    const char *next = memchr(cursor->at, '\n', (size_t)(cursor->end - cursor->at));
    if (!next) next = cursor->end;
    const char *l = cursor->at;
    size_t n = (size_t)(next - l);
    while (n && (*l == ' ' || *l == '\t' || (unsigned char)*l == 0xef ||
                 (unsigned char)*l == 0xbb || (unsigned char)*l == 0xbf)) { l++; n--; }
    while (n && (l[n - 1] == '\r' || l[n - 1] == ' ')) n--;
    *line = l; *len = n;
    cursor->at = next < cursor->end ? next + 1 : cursor->end;
    return 1;
}

static int is_audio_media(const char *line, size_t n) {
    return n > 13 && !strncasecmp(line, "#EXT-X-MEDIA:", 13) &&
           line_contains(line + 13, n - 13, "TYPE=AUDIO");
}

int hls_manifest_audio_tracks(const char *body, size_t len, HlsManifestTrack *tracks,
                              int capacity, int *filterable) {
    if (filterable) *filterable = 0;
    if (!body || !tracks || capacity <= 0) return 0;
    LineCursor cursor = { body, body + len };
    const char *line; size_t n;
    int count = 0, all_uri = 1, overflow = 0, same_group = 1, variants = 0;
    char group[64] = "", other[64];
    while (next_line(&cursor, &line, &n)) {
        if (line_starts(line, n, "#EXT-X-STREAM-INF:")) {
            variants++;
            // Uma variante sem AUDIO= toca o audio muxado nela; filtrar as
            // renditions nao a afetaria, mas mistura de grupos sim.
            if (attr_copy(line, n, "AUDIO", other, sizeof(other)) &&
                group[0] && strcmp(other, group)) same_group = 0;
            continue;
        }
        if (!is_audio_media(line, n)) continue;
        if (count >= capacity) { overflow = 1; continue; }
        HlsManifestTrack *track = &tracks[count];
        memset(track, 0, sizeof(*track));
        if (!attr_copy(line, n, "URI", track->uri, sizeof(track->uri)) || !track->uri[0])
            all_uri = 0;
        attr_copy(line, n, "NAME", track->name, sizeof(track->name));
        attr_copy(line, n, "LANGUAGE", track->language, sizeof(track->language));
        char flag[8];
        track->is_default = attr_copy(line, n, "DEFAULT", flag, sizeof(flag)) &&
                            !strcasecmp(flag, "YES");
        if (!attr_copy(line, n, "GROUP-ID", other, sizeof(other))) other[0] = 0;
        if (!count) snprintf(group, sizeof(group), "%s", other);
        else if (strcmp(group, other)) same_group = 0;
        count++;
    }
    if (filterable)
        *filterable = count >= 2 && all_uri && !overflow && same_group &&
                      group[0] && variants > 0;
    return count;
}

int hls_manifest_keep_audio(const char *body, size_t len, int keep,
                            char **out, size_t *out_len) {
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (!body || len == 0 || !out || !out_len || keep < 0) return 0;
    LineCursor cursor = { body, body + len };
    const char *line; size_t n;
    TrimBuffer b = {0};
    int index = 0, kept = 0;
    while (next_line(&cursor, &line, &n)) {
        if (is_audio_media(line, n)) {
            if (index++ != keep) continue;
            kept = 1;
        }
        if (n) trim_put(&b, line, n);
    }
    if (b.failed || !kept) { free(b.data); return 0; }
    *out = b.data;
    *out_len = b.len;
    return 1;
}

int hls_manifest_playlist_uris(const char *body, size_t len,
                               char (*uris)[HLS_MANIFEST_URI_MAX], int capacity) {
    if (!body || !uris || capacity <= 0) return 0;
    int count = 0;
    // Variantes (video) primeiro: com teto de capacidade, elas nunca ficam de
    // fora por causa de muitas renditions de audio listadas antes no master.
    for (int pass = 0; pass < 2; pass++) {
        LineCursor cursor = { body, body + len };
        const char *line; size_t n;
        int after_stream_inf = 0;
        while (next_line(&cursor, &line, &n) && count < capacity) {
            char uri[HLS_MANIFEST_URI_MAX] = "";
            if (after_stream_inf && n && line[0] != '#') {
                if (pass == 0 && n < sizeof(uri)) { memcpy(uri, line, n); uri[n] = 0; }
                after_stream_inf = 0;
            } else if (line_starts(line, n, "#EXT-X-STREAM-INF:")) {
                after_stream_inf = 1;
                continue;
            } else if (pass == 1 && n > 13 && !strncasecmp(line, "#EXT-X-MEDIA:", 13) &&
                       (line_contains(line + 13, n - 13, "TYPE=AUDIO") ||
                        line_contains(line + 13, n - 13, "TYPE=VIDEO"))) {
                attr_copy(line, n, "URI", uri, sizeof(uri));
            }
            if (!uri[0]) continue;
            int duplicate = 0;
            for (int i = 0; i < count && !duplicate; i++) duplicate = !strcmp(uris[i], uri);
            if (!duplicate) snprintf(uris[count++], HLS_MANIFEST_URI_MAX, "%s", uri);
        }
    }
    return count;
}

int hls_media_playlist_map_uri(const char *body, size_t len, char *out, size_t out_size) {
    if (!out || out_size == 0) return 0;
    out[0] = 0;
    if (!body) return 0;
    LineCursor cursor = { body, body + len };
    const char *line; size_t n;
    while (next_line(&cursor, &line, &n)) {
        if (line_starts(line, n, "#EXT-X-STREAM-INF") ||
            line_starts(line, n, "#EXT-X-MEDIA:")) return 0;
        if (line_starts(line, n, "#EXT-X-MAP:"))
            return attr_copy(line, n, "URI", out, out_size) && out[0];
        if (n && line[0] != '#') return 0; // MAP vem antes do primeiro segmento
    }
    return 0;
}
