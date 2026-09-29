#include "hls_manifest.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *master =
        "#EXTM3U\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud\",LANGUAGE=\"pt-BR\"\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud\",LANGUAGE=\"en\"\n"
        "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"subs\",LANGUAGE=\"pt-BR\"\n"
        "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"subs\",LANGUAGE=\"en\"\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=3000000,AUDIO=\"aud\",SUBTITLES=\"subs\"\n"
        "video.m3u8\n";
    int audio = -1, subtitles = -1;
    assert(hls_manifest_media_counts(master, strlen(master), &audio, &subtitles) == 1);
    assert(audio == 2);
    assert(subtitles == 2);

    const char *media = "#EXTM3U\n#EXTINF:4.0,\nseg-1.m4s\n";
    assert(hls_manifest_media_counts(media, strlen(media), &audio, &subtitles) == 0);
    assert(audio == 0 && subtitles == 0);
    puts("hls manifest: ok");
    return 0;
}
