#include "hls_manifest.h"
#include <string.h>

static int line_contains(const char *line, size_t len, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0 || len < nlen) return 0;
    for (size_t i = 0; i + nlen <= len; i++)
        if (!memcmp(line + i, needle, nlen)) return 1;
    return 0;
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
