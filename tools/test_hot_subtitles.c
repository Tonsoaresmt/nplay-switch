#include "hot_subtitles.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *sid = "1234567890abcdef_safe-session";
    char url[512];
    assert(hot_subtitle_session_valid(sid));
    assert(!hot_subtitle_session_valid(NULL));
    assert(!hot_subtitle_session_valid("short"));
    assert(!hot_subtitle_session_valid("1234567890abcdef?x=1"));
    assert(!hot_subtitle_session_valid("1234567890abcdef%2f"));
    assert(!hot_subtitle_session_valid("1234567890abcdef/../../"));
    assert(hot_subtitle_url("https://nplay.test/", sid, -1, url, sizeof(url)));
    assert(strstr(url, "/api/stream/hot/1234567890abcdef_safe-session/probe"));
    assert(!hot_subtitle_url("http://nplay.test", sid, 0, url, sizeof(url)));
    assert(!hot_subtitle_url("https://user@host", sid, 0, url, sizeof(url)));
    assert(!hot_subtitle_url("https://host?x=1", sid, 0, url, sizeof(url)));
    assert(!hot_subtitle_url("https://host", sid, 101, url, sizeof(url)));
    assert(!hot_subtitle_url("https://host", sid, 0, url, 8) && !url[0]);
    cJSON *j = cJSON_Parse("{\"ok\":true,\"subtitles\":["
        "{\"index\":2,\"codec\":\"subrip\",\"language\":\"por\",\"title\":\"Portugues\"},"
        "{\"index\":2},{\"index\":3,\"codec\":\"hdmv_pgs_subtitle\"},"
        "{\"index\":-1},{\"index\":1.5},{\"index\":\"4\"},{\"index\":101},"
        "{\"index\":4,\"codec\":\"ass\",\"forced\":true}]}");
    HlsManifestTrack tracks[16];
    assert(hot_subtitle_tracks(j, "https://host", sid, tracks, 16) == 2);
    assert(!strcmp(tracks[0].language, "por"));
    assert(strstr(tracks[0].uri, "/subtitles/2.vtt"));
    assert(tracks[1].forced && strstr(tracks[1].uri, "/subtitles/4.vtt"));
    assert(hot_subtitle_tracks(j, "https://host", sid, tracks, 1) == 1);
    cJSON_Delete(j);
    j = cJSON_CreateObject(); cJSON_AddBoolToObject(j, "ok", 1);
    cJSON *array = cJSON_AddArrayToObject(j, "subtitles");
    for (int i=0;i<40;i++) { cJSON *entry=cJSON_CreateObject(); cJSON_AddNumberToObject(entry,"index",i); cJSON_AddItemToArray(array,entry); }
    assert(hot_subtitle_tracks(j, "https://host", sid, tracks, 999) == 16);
    cJSON_Delete(j);
    j = cJSON_Parse("{\"ok\":false,\"subtitles\":[{\"index\":2}]}");
    assert(!hot_subtitle_tracks(j, "https://host", sid, tracks, 16));
    cJSON_Delete(j);
    puts("HOT SUBTITLES: secret id, HTTPS URL, codec/index validation, dedup and 16-track cap passed");
}
