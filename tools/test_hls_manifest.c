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
    // Audio filtrado: lista na ordem do FFmpeg, master com uma unica rendition.
    const char *r2 =
        "#EXTM3U\n#EXT-X-VERSION:7\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"group_audio\",NAME=\"audio_0\",DEFAULT=YES,LANGUAGE=\"jpn\",URI=\"stream-audio_0_jpn.m3u8\"\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"group_audio\",NAME=\"audio_1\",DEFAULT=NO,LANGUAGE=\"por\",URI=\"stream-audio_1_por.m3u8\"\r\n"
        "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"subs\",NAME=\"PT\",LANGUAGE=\"por\",URI=\"subtitle-0.m3u8\"\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=5200000,AUDIO=\"group_audio\",SUBTITLES=\"subs\"\n"
        "stream-video.m3u8\n";
    int filterable = 0;
    assert(hls_manifest_audio_tracks(r2, strlen(r2), tracks, 4, &filterable) == 2 && filterable);
    assert(!strcmp(tracks[0].language, "jpn") && tracks[0].is_default && !tracks[1].is_default);
    assert(!strcmp(tracks[1].name, "audio_1") && !strcmp(tracks[1].uri, "stream-audio_1_por.m3u8"));
    char *kept = NULL; size_t kept_len = 0;
    assert(hls_manifest_keep_audio(r2, strlen(r2), 1, &kept, &kept_len) == 1);
    assert(!strstr(kept, "audio_0") && strstr(kept, "audio_1_por") && strstr(kept, "subtitle-0") &&
           strstr(kept, "stream-video.m3u8") && !strchr(kept, '\r') && kept_len == strlen(kept));
    int a2 = 0, s2 = 0;
    assert(hls_manifest_media_counts(kept, kept_len, &a2, &s2) == 1 && a2 == 1 && s2 == 1);
    free(kept);
    assert(hls_manifest_keep_audio(r2, strlen(r2), 2, &kept, &kept_len) == 0 && !kept);
    // Dois grupos (ex.: audio por bitrate) ou rendition sem URI: nao filtrar.
    const char *groups =
        "#EXTM3U\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"lo\",LANGUAGE=\"en\",URI=\"a.m3u8\"\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"hi\",LANGUAGE=\"en\",URI=\"b.m3u8\"\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=1,AUDIO=\"lo\"\nv1.m3u8\n#EXT-X-STREAM-INF:BANDWIDTH=2,AUDIO=\"hi\"\nv2.m3u8\n";
    assert(hls_manifest_audio_tracks(groups, strlen(groups), tracks, 4, &filterable) == 2 && !filterable);
    const char *muxed =
        "#EXTM3U\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",LANGUAGE=\"en\",DEFAULT=YES\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",LANGUAGE=\"pt\",URI=\"pt.m3u8\"\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=1,AUDIO=\"a\"\nv.m3u8\n";
    assert(hls_manifest_audio_tracks(muxed, strlen(muxed), tracks, 4, &filterable) == 2 && !filterable);
    assert(hls_manifest_audio_tracks(r2, strlen(r2), tracks, 1, &filterable) == 1 && !filterable);
    // URIs que o demuxer abre (sem legendas) e init de uma playlist de midia.
    char uris[8][HLS_MANIFEST_URI_MAX];
    assert(hls_manifest_playlist_uris(r2, strlen(r2), uris, 8) == 3);
    assert(!strcmp(uris[0], "stream-video.m3u8") && !strcmp(uris[1], "stream-audio_0_jpn.m3u8"));
    assert(hls_manifest_playlist_uris(r2, strlen(r2), uris, 1) == 1 && !strcmp(uris[0], "stream-video.m3u8"));
    assert(hls_manifest_playlist_uris(groups, strlen(groups), uris, 8) == 4);
    const char *mapped = "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-MAP:URI=\"init-video.mp4\"\n#EXTINF:6,\nseg0.m4s\n";
    char map[64];
    assert(hls_media_playlist_map_uri(mapped, strlen(mapped), map, sizeof(map)) == 1 && !strcmp(map, "init-video.mp4"));
    assert(hls_media_playlist_map_uri(r2, strlen(r2), map, sizeof(map)) == 0);
    // Resolucao igual a do FFmpeg: sem heranca da query (confirmado no 7.1).
    assert(hls_manifest_resolve_like_ffmpeg("https://cdn.example/a/index.m3u8?token=x",
                                            "stream-video.m3u8", resolved, sizeof(resolved)) == 1);
    assert(!strcmp(resolved, "https://cdn.example/a/stream-video.m3u8"));
    assert(hls_manifest_resolve_like_ffmpeg("https://cdn.example/a/b/v.m3u8",
                                            "https://r2.example/x.mp4?sig=1", resolved, sizeof(resolved)) == 1);
    assert(!strcmp(resolved, "https://r2.example/x.mp4?sig=1"));
    puts("hls manifest: ok");
    return 0;
}
