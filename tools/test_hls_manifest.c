#include "hls_manifest.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

    // Abertura posicionada: corta a playlist de midia no segmento do ponto.
    const char *vod =
        "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:8\n#EXT-X-MEDIA-SEQUENCE:0\n"
        "#EXT-X-PLAYLIST-TYPE:VOD\n#EXT-X-INDEPENDENT-SEGMENTS\n#EXT-X-MAP:URI=\"init-video.mp4\"\n"
        "#EXTINF:7.833,\nsegment-video-000000.m4s\n#EXTINF:6.000,\nsegment-video-000001.m4s\n"
        "#EXTINF:6.000,\nsegment-video-000002.m4s\n#EXTINF:5.500,\nsegment-video-000003.m4s\n"
        "#EXT-X-ENDLIST\n";
    char *trimmed = NULL; size_t trimmed_len = 0; double seg = -1, full = -1;
    assert(hls_media_playlist_trim(vod, strlen(vod), 15.0, &trimmed, &trimmed_len, &seg, &full) == 1);
    assert(seg > 13.8 && seg < 13.9 && full > 25.3 && full < 25.4);
    assert(strstr(trimmed, "#EXT-X-MEDIA-SEQUENCE:2\n"));
    assert(strstr(trimmed, "#EXT-X-MAP:URI=\"init-video.mp4\"\n"));
    assert(!strstr(trimmed, "segment-video-000001.m4s"));
    assert(strstr(trimmed, "segment-video-000002.m4s") && strstr(trimmed, "#EXT-X-ENDLIST"));
    assert(strstr(trimmed, "#EXT-X-MAP") < strstr(trimmed, "#EXTINF"));
    assert(strlen(trimmed) == trimmed_len);
    free(trimmed);
    // Ponto exatamente no inicio de um segmento usa esse segmento.
    assert(hls_media_playlist_trim(vod, strlen(vod), 7.833, &trimmed, &trimmed_len, &seg, &full) == 1);
    assert(strstr(trimmed, "#EXT-X-MEDIA-SEQUENCE:1\n") && seg > 7.8 && seg < 7.9);
    free(trimmed);
    // Nada a cortar: primeiro segmento, alem do fim, master, ao vivo, BYTERANGE implicito.
    assert(hls_media_playlist_trim(vod, strlen(vod), 3.0, &trimmed, &trimmed_len, &seg, &full) == 0 && !trimmed);
    assert(full > 25.3);
    assert(hls_media_playlist_trim(vod, strlen(vod), 99.0, &trimmed, &trimmed_len, &seg, &full) == 0);
    assert(hls_media_playlist_trim(master, strlen(master), 15.0, &trimmed, &trimmed_len, &seg, &full) == 0);
    const char *live = "#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:40\n#EXTINF:6,\na.ts\n#EXTINF:6,\nb.ts\n#EXTINF:6,\nc.ts\n";
    assert(hls_media_playlist_trim(live, strlen(live), 7.0, &trimmed, &trimmed_len, &seg, &full) == 0);
    const char *ranges = "#EXTM3U\n#EXT-X-PLAYLIST-TYPE:VOD\n#EXTINF:6,\n#EXT-X-BYTERANGE:100\nall.mp4\n"
                         "#EXTINF:6,\n#EXT-X-BYTERANGE:100\nall.mp4\n#EXT-X-ENDLIST\n";
    assert(hls_media_playlist_trim(ranges, strlen(ranges), 7.0, &trimmed, &trimmed_len, &seg, &full) == 0);
    // Sequencia inicial, CRLF, chave e descontinuidade anteriores ao corte.
    const char *crlf =
        "#EXTM3U\r\n#EXT-X-MEDIA-SEQUENCE:10\r\n#EXT-X-KEY:METHOD=AES-128,URI=\"k1\"\r\n"
        "#EXTINF:4,\r\na.ts\r\n#EXT-X-DISCONTINUITY\r\n#EXTINF:4,\r\nb.ts\r\n"
        "#EXT-X-KEY:METHOD=AES-128,URI=\"k2\"\r\n#EXTINF:4,\r\nc.ts\r\n#EXTINF:4,\r\nd.ts\r\n#EXT-X-ENDLIST\r\n";
    assert(hls_media_playlist_trim(crlf, strlen(crlf), 9.0, &trimmed, &trimmed_len, &seg, &full) == 1);
    assert(strstr(trimmed, "#EXT-X-MEDIA-SEQUENCE:12\n") && strstr(trimmed, "URI=\"k2\""));
    // k1 vem antes e k2 (redefinida no proprio segmento de corte) prevalece.
    assert(!strstr(trimmed, "\r") && strstr(trimmed, "URI=\"k1\"") < strstr(trimmed, "URI=\"k2\""));
    assert(strstr(trimmed, "#EXT-X-DISCONTINUITY-SEQUENCE:1\n"));
    assert(!strstr(trimmed, "b.ts") && strstr(trimmed, "c.ts") && seg == 8.0);
    free(trimmed);
    const char binary[] = "\0\0\0\x18" "ftypiso6\n#EXTINF:1,\nx\n";
    assert(hls_media_playlist_trim(binary, sizeof(binary) - 1, 5.0, &trimmed, &trimmed_len, &seg, &full) == 0 && !trimmed);
    puts("hls manifest: ok");
    return 0;
}
