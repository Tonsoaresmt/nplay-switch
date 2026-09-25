#include "genre_label.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int equal_ascii(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return 0;
    }
    return !*a && !*b;
}

static int technical(const char *token) {
    static const char *const exact[] = {
        "Filme", "Filmes", "Torrent", "HDR Torrent", "HDR", "SDR", "4K",
        "UHD", "Full HD", "1080p", "720p", "BluRay", "WEB-DL", "WEBRip",
        "Remux", "Dual Audio", "Dublado", "Legendado", "TorBox", "R2"
    };
    for (size_t i = 0; i < sizeof(exact) / sizeof(exact[0]); i++)
        if (equal_ascii(token, exact[i])) return 1;
    return 0;
}

void movie_genre_label(const char *raw, char *out, size_t cap) {
    if (!out || !cap) return;
    snprintf(out, cap, "Filme");
    if (!raw) return;
    const char *cursor = raw;
    while (*cursor) {
        const char *sep = cursor;
        while (*sep && *sep != ';' && *sep != ',') sep++;
        const char *end = sep;
        while (cursor < end && isspace((unsigned char)*cursor)) cursor++;
        while (end > cursor && isspace((unsigned char)end[-1])) end--;
        char token[96];
        size_t len = (size_t)(end - cursor);
        if (len && len < sizeof(token)) {
            memcpy(token, cursor, len);
            token[len] = 0;
            if (!technical(token)) {
                size_t used = strlen(out);
                if (cap > used + len + 4) snprintf(out + used, cap - used, " · %s", token);
            }
        }
        cursor = *sep ? sep + 1 : sep;
    }
}
