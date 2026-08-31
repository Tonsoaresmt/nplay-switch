#include "api.h"
#include "net.h"
#include <stdio.h>
#include <string.h>

static char g_api_last_error[192] = "";

const char *api_last_error(void) { return g_api_last_error; }

static void api_set_error(long code, const char *transport, cJSON *json) {
    const char *message = jstr(json, "error");
    if (!message || !message[0]) message = jstr(json, "message");
    if (message && message[0])
        snprintf(g_api_last_error, sizeof(g_api_last_error), "HTTP %ld: %.150s", code, message);
    else if (transport && transport[0])
        snprintf(g_api_last_error, sizeof(g_api_last_error), "Rede: %.170s", transport);
    else
        snprintf(g_api_last_error, sizeof(g_api_last_error), "Resposta HTTP %ld", code);
}

static void absolute_play_url(const char *play, char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!play || !play[0]) return;
    if (!strncmp(play, "http://", 7) || !strncmp(play, "https://", 8))
        snprintf(out, cap, "%s", play);
    else if (play[0] == '/')
        snprintf(out, cap, "%s%s", BASE, play);
    else
        snprintf(out, cap, "%s/%s", BASE, play);
}

static void parse_playback_source(cJSON *j, PlaybackSource *out) {
    int value;
    const char *text;
    if ((value = jint(j, "session_id")) > 0) out->session_id = value;
    if ((value = jint(j, "source_id")) > 0) out->source_id = value;
    if ((text = jstr(j, "kind"))) snprintf(out->kind, sizeof(out->kind), "%s", text);
    if ((text = jstr(j, "section"))) snprintf(out->section, sizeof(out->section), "%s", text);
    if ((text = jstr(j, "container"))) snprintf(out->container, sizeof(out->container), "%s", text);
    if ((text = jstr(j, "delivery"))) {
        snprintf(out->delivery_str, sizeof(out->delivery_str), "%s", text);
        out->delivery = !strcmp(text, "r2") ? DELIVERY_R2 :
                        !strcmp(text, "upstream") ? DELIVERY_UPSTREAM : DELIVERY_UNKNOWN;
    }
    absolute_play_url(jstr(j, "play_url"), out->play_url, sizeof(out->play_url));
    if ((text = jstr(j, "quality"))) snprintf(out->quality, sizeof(out->quality), "%s", text);
    cJSON *qs = cJSON_GetObjectItem(j, "qualities");
    if (cJSON_IsArray(qs)) {
        out->quality_count = 0;
        int n = cJSON_GetArraySize(qs);
        if (n > 8) n = 8;
        for (int i = 0; i < n; i++) {
            cJSON *qi = cJSON_GetArrayItem(qs, i);
            if (cJSON_IsString(qi) && qi->valuestring)
                snprintf(out->qualities[out->quality_count++], sizeof(out->qualities[0]), "%s", qi->valuestring);
        }
    }
    out->season = jint(j, "season");
    out->episode = jint(j, "episode");
    cJSON *bytes = cJSON_GetObjectItem(j, "source_bytes");
    if (cJSON_IsNumber(bytes)) out->source_bytes = (long long)bytes->valuedouble;
    out->is_cam = jint(j, "is_cam");
}

cJSON *api_get_timeout(const char *path, long connect_timeout, long total_timeout) {
    char url[1024];
    snprintf(url, sizeof(url), "%s%s", BASE, path);
    struct membuf out = { 0 };
    const char *err = NULL;
    g_api_last_error[0] = '\0';
    long code = net_request_timeout(url, "GET", NULL, g_token[0] ? g_token : NULL,
                                    &out, &err, connect_timeout, total_timeout);
    cJSON *j = NULL;
    if (code == 200 && out.data) j = cJSON_Parse(out.data);
    if (!j) {
        if (code == 200) snprintf(g_api_last_error, sizeof(g_api_last_error), "Resposta de catalogo invalida");
        else api_set_error(code, err, NULL);
    }
    membuf_free(&out);
    return j;
}

cJSON *api_get(const char *path) {
    return api_get_timeout(path, 5L, 15L);
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

static int resolve_playback_with_timeout(int item_id, const char *quality,
                                         long connect_timeout, long total_timeout,
                                         PlaybackSource *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(PlaybackSource));
    out->item_id = item_id;

    char url[1024];
    snprintf(url, sizeof(url), "%s/api/stream/%d", BASE, item_id);
    
    g_api_last_error[0] = '\0';
    char *body = NULL;
    cJSON *request = cJSON_CreateObject();
    if (!request) {
        snprintf(g_api_last_error, sizeof(g_api_last_error), "Memoria insuficiente ao preparar a reproducao");
        return -1;
    }
    if (quality && quality[0]) {
        cJSON_AddStringToObject(request, "quality", quality);
    }
    body = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);
    if (!body) {
        snprintf(g_api_last_error, sizeof(g_api_last_error), "Memoria insuficiente ao preparar a reproducao");
        return -1;
    }

    struct membuf resp = { 0 };
    const char *err = NULL;
    long code = net_request_timeout(url, "POST", body, g_token[0] ? g_token : NULL,
                                    &resp, &err, connect_timeout, total_timeout);
    cJSON_free(body);
    cJSON *j = resp.data ? cJSON_Parse(resp.data) : NULL;
    if (code != 200 || !j) {
        api_set_error(code, err, j);
        if (j) cJSON_Delete(j);
        membuf_free(&resp);
        return -1;
    }

    parse_playback_source(j, out);

    cJSON_Delete(j);
    membuf_free(&resp);
    return 0;
}

int api_resolve_playback(int item_id, const char *quality, PlaybackSource *out) {
    return resolve_playback_with_timeout(item_id, quality, 8L, 20L, out);
}

int api_reresolve_playback(int item_id, const char *quality, PlaybackSource *out) {
    return resolve_playback_with_timeout(item_id, quality, 4L, 8L, out);
}

int api_refresh_playback(const PlaybackSource *current, PlaybackSource *out) {
    if (!current || !out || current->session_id <= 0) return -1;
    *out = *current;
    out->play_url[0] = '\0';
    char path[128], body[80];
    snprintf(path, sizeof(path), "/api/stream/session/%d/refresh", current->session_id);
    snprintf(body, sizeof(body), "{\"source_id\":%d}", current->source_id);
    char url[1024]; snprintf(url, sizeof(url), "%s%s", BASE, path);
    struct membuf resp = {0};
    const char *err = NULL;
    long code = net_request_timeout(url, "POST", body, g_token[0] ? g_token : NULL,
                                    &resp, &err, 4L, 8L);
    cJSON *json = resp.data ? cJSON_Parse(resp.data) : NULL;
    if (code != 200 || !json) {
        api_set_error(code, err, json);
        if (json) cJSON_Delete(json);
        membuf_free(&resp);
        return -1;
    }
    parse_playback_source(json, out);
    cJSON_Delete(json);
    membuf_free(&resp);
    return out->play_url[0] ? 0 : -1;
}

int api_fail_playback(const PlaybackSource *current, PlaybackSource *out) {
    if (!current || !out || current->session_id <= 0) return -1;
    *out = *current;
    out->play_url[0] = '\0';
    char path[128], body[80], url[1024];
    snprintf(path, sizeof(path), "/api/stream/session/%d/fail", current->session_id);
    snprintf(body, sizeof(body), "{\"source_id\":%d}", current->source_id);
    snprintf(url, sizeof(url), "%s%s", BASE, path);
    struct membuf resp = {0};
    const char *err = NULL;
    long code = net_request_timeout(url, "POST", body, g_token[0] ? g_token : NULL,
                                    &resp, &err, 4L, 8L);
    cJSON *json = resp.data ? cJSON_Parse(resp.data) : NULL;
    if (code != 200 || !json) {
        api_set_error(code, err, json);
        if (json) cJSON_Delete(json);
        membuf_free(&resp);
        return -1;
    }
    parse_playback_source(json, out);
    // O endpoint de fail troca a fonte, mas hoje nao devolve delivery/container.
    // Tenta obter o descritor completo da nova fonte; se a rede cair justamente
    // aqui, preserva a URL de fallback e o container anterior como ultimo recurso.
    out->delivery = DELIVERY_UNKNOWN;
    out->delivery_str[0] = '\0';
    cJSON_Delete(json);
    membuf_free(&resp);
    if (!out->play_url[0]) return -1;
    PlaybackSource complete = {0};
    if (api_reresolve_playback(current->item_id,
                               current->quality[0] ? current->quality : NULL,
                               &complete) == 0 && complete.play_url[0]) {
        *out = complete;
    }
    return 0;
}

int api_playback_heartbeat(int session_id) {
    if (session_id <= 0) return 0;
    char path[112], url[1024];
    snprintf(path, sizeof(path), "/api/stream/session/%d/heartbeat", session_id);
    snprintf(url, sizeof(url), "%s%s", BASE, path);
    struct membuf out = {0}; const char *err = NULL;
    long code = net_request_timeout(url, "POST", "{}", g_token[0] ? g_token : NULL,
                                    &out, &err, 3L, 6L);
    membuf_free(&out);
    return code == 200 ? 0 : -1;
}

int api_playback_progress(int item_id, int position_sec, int duration_sec) {
    if (item_id <= 0 || position_sec <= 5 || duration_sec <= 0) return 0;
    char body[160], url[1024];
    snprintf(body, sizeof(body), "{\"item_id\":%d,\"position_seconds\":%d,\"duration_seconds\":%d}",
             item_id, position_sec, duration_sec);
    snprintf(url, sizeof(url), "%s/api/sync/progress", BASE);
    struct membuf out = {0}; const char *err = NULL;
    long code = net_request_timeout(url, "POST", body, g_token[0] ? g_token : NULL,
                                    &out, &err, 3L, 6L);
    membuf_free(&out);
    return code == 200 ? 0 : -1;
}

int api_stop_playback(int item_id) {
    if (item_id <= 0) return 0;
    char url[1024]; snprintf(url, sizeof(url), "%s/api/stream/%d/stop", BASE, item_id);
    struct membuf out = {0}; const char *err = NULL;
    long code = net_request_timeout(url, "POST", "{}", g_token[0] ? g_token : NULL,
                                    &out, &err, 3L, 6L);
    membuf_free(&out);
    return code == 200 ? 0 : -1;
}
