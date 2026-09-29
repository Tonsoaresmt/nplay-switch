#include "hls_manifest.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *master =
        "#EXTM3U\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud\",LANGUAGE=\"pt-BR\"\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud\",LANGUAGE=\"en\"\n"
        "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"subs\",LANGUAGE=\"pt-BR\",URI=\"pt.m3u8\"\n"
        "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"subs\",LANGUAGE=\"en\",URI=\"en.m3u8\"\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=3000000,AUDIO=\"aud\",SUBTITLES=\"subs\"\n"
        "video.m3u8\n";
    int audio = -1, subtitles = -1;
    assert(hls_manifest_media_counts(master, strlen(master), &audio, &subtitles) == 1);
    assert(audio == 2);
    assert(subtitles == 2);

    HlsManifestTrack tracks[4];
    assert(hls_manifest_subtitle_tracks(master, strlen(master), tracks, 4) == 2);
    assert(!strcmp(tracks[0].language, "pt-BR"));
    assert(!strcmp(tracks[1].language, "en"));

    const char *described =
        "#EXTM3U\n"
        "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"subs\",NAME=\"Português (Brasil)\","
        "LANGUAGE=\"por\",DEFAULT=YES,AUTOSELECT=YES,FORCED=NO,URI=\"subtitle-0.m3u8\"\n";
    assert(hls_manifest_subtitle_tracks(described, strlen(described), tracks, 4) == 1);
    assert(!strcmp(tracks[0].name, "Português (Brasil)"));
    assert(!strcmp(tracks[0].uri, "subtitle-0.m3u8"));
    assert(tracks[0].is_default == 1 && tracks[0].forced == 0);
    char resolved[256];
    assert(hls_manifest_resolve_url("https://cdn.example/a/index.m3u8?token=x",
                                    tracks[0].uri, resolved, sizeof(resolved)) == 1);
    assert(!strcmp(resolved, "https://cdn.example/a/subtitle-0.m3u8?token=x"));
    assert(hls_manifest_resolve_url("https://cdn.example/a/index.m3u8",
                                    "/signed/sub.m3u8", resolved, sizeof(resolved)) == 1);
    assert(!strcmp(resolved, "https://cdn.example/signed/sub.m3u8"));

    const char *media = "#EXTM3U\n#EXTINF:4.0,\nseg-1.m4s\n";
    assert(hls_manifest_media_counts(media, strlen(media), &audio, &subtitles) == 0);
    assert(audio == 0 && subtitles == 0);
    puts("hls manifest: ok");
    return 0;
}
