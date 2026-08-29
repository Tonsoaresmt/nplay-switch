#include "api.h"
#include "net.h"
#include <stdio.h>
#include <string.h>

cJSON *api_get(const char *path) {
    char url[1024];
    snprintf(url, sizeof(url), "%s%s", BASE, path);
    struct membuf out = { 0 };
    const char *err = NULL;
    long code = net_request_timeout(url, "GET", NULL, g_token[0] ? g_token : NULL,
                                    &out, &err, 5L, 15L);
    cJSON *j = NULL;
    if (code == 200 && out.data) j = cJSON_Parse(out.data);
    membuf_free(&out);
    return j;
}

long api_send(const char *path, const char *method, const char *body) {
    char url[1024];
    snprintf(url, sizeof(url), "%s%s", BASE, path);
    struct membuf out = { 0 };
    const char *err = NULL;
    long code = net_request_timeout(url, method, body ? body : "{}",
                                    g_token[0] ? g_token : NULL, &out, &err, 5L, 20L);
    membuf_free(&out);
    return code;
}

const char *jstr(cJSON *o, const char *k) {
    cJSON *v = o ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL;
    return (v && v->valuestring) ? v->valuestring : NULL;
}

int jint(cJSON *o, const char *k) {
    cJSON *v = o ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL;
    return v ? v->valueint : 0;
}

int arr_len(cJSON *a) {
    return cJSON_IsArray(a) ? cJSON_GetArraySize(a) : 0;
}

int api_resolve_playback(int item_id, const char *quality, PlaybackSource *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(PlaybackSource));
    out->item_id = item_id;

    char url[1024];
    snprintf(url, sizeof(url), "%s/api/stream/%d", BASE, item_id);
    
    // Add quality to request body if specified
    char body[128] = "{}";
    if (quality && quality[0]) {
        snprintf(body, sizeof(body), "{\"quality\":\"%s\"}", quality);
    }

    struct membuf resp = { 0 };
    const char *err = NULL;
    long code = net_request_timeout(url, "POST", body, g_token[0] ? g_token : NULL,
                                    &resp, &err, 8L, 20L);
    cJSON *j = resp.data ? cJSON_Parse(resp.data) : NULL;
    if (code != 200 || !j) {
        if (j) cJSON_Delete(j);
        membuf_free(&resp);
        return -1;
    }

    out->session_id = jint(j, "session_id");
    out->source_id = jint(j, "source_id");

    const char *k = jstr(j, "kind");
    if (k) snprintf(out->kind, sizeof(out->kind), "%s", k);

    const char *sec = jstr(j, "section");
    if (sec) snprintf(out->section, sizeof(out->section), "%s", sec);

    const char *cnt = jstr(j, "container");
    if (cnt) snprintf(out->container, sizeof(out->container), "%s", cnt);

    const char *del = jstr(j, "delivery");
    if (del) {
        snprintf(out->delivery_str, sizeof(out->delivery_str), "%s", del);
        if (strcmp(del, "r2") == 0) out->delivery = DELIVERY_R2;
        else if (strcmp(del, "upstream") == 0) out->delivery = DELIVERY_UPSTREAM;
        else out->delivery = DELIVERY_UNKNOWN;
    }

    const char *play = jstr(j, "play_url");
    if (play) {
        if (strncmp(play, "http", 4) == 0) {
            snprintf(out->play_url, sizeof(out->play_url), "%s", play);
        } else {
            snprintf(out->play_url, sizeof(out->play_url), "%s%s", BASE, play);
        }
    }

    const char *q = jstr(j, "quality");
    if (q) snprintf(out->quality, sizeof(out->quality), "%s", q);

    cJSON *qs = cJSON_GetObjectItem(j, "qualities");
    if (qs && cJSON_IsArray(qs)) {
        int n = cJSON_GetArraySize(qs);
        if (n > 8) n = 8;
        out->quality_count = n;
        for (int i = 0; i < n; i++) {
            cJSON *qi = cJSON_GetArrayItem(qs, i);
            if (qi && qi->valuestring) {
                snprintf(out->qualities[i], sizeof(out->qualities[i]), "%s", qi->valuestring);
            }
        }
    }

    out->season = jint(j, "season");
    out->episode = jint(j, "episode");

    cJSON *sb = cJSON_GetObjectItem(j, "source_bytes");
    if (sb && cJSON_IsNumber(sb)) {
        out->source_bytes = (long long)sb->valuedouble;
    }

    out->is_cam = jint(j, "is_cam");

    cJSON_Delete(j);
    membuf_free(&resp);
    return 0;
}
