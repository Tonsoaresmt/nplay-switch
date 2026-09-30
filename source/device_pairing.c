#include "device_pairing.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

static const char *json_string(cJSON *obj, const char *key) {
    cJSON *value = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(value) ? value->valuestring : NULL;
}

static void set_error(char *out, size_t cap, const char *value) {
    if (!out || cap == 0) return;
    snprintf(out, cap, "%s", value && value[0] ? value : "Resposta invalida do Nplay");
}

static int is_public_code(const char *code) {
    if (!code) return 0;
    size_t n = strlen(code);
    if (n != 6) return 0;
    for (size_t i = 0; i < n; i++) if (!isdigit((unsigned char)code[i])) return 0;
    return 1;
}

static int is_device_secret(const char *code) {
    if (!code) return 0;
    size_t n = strlen(code);
    if (n < 32 || n >= 112) return 0;
    for (size_t i = 0; i < n; i++) if (!isxdigit((unsigned char)code[i])) return 0;
    return 1;
}

static void parse_qr(cJSON *root, DevicePairingCode *out) {
    cJSON *qr = cJSON_GetObjectItemCaseSensitive(root, "qr");
    if (!cJSON_IsObject(qr)) return;
    const char *encoding = json_string(qr, "encoding");
    cJSON *size_value = cJSON_GetObjectItemCaseSensitive(qr, "size");
    cJSON *quiet_value = cJSON_GetObjectItemCaseSensitive(qr, "quiet_zone");
    cJSON *rows = cJSON_GetObjectItemCaseSensitive(qr, "rows");
    int size = cJSON_IsNumber(size_value) ? size_value->valueint : 0;
    int quiet = cJSON_IsNumber(quiet_value) ? quiet_value->valueint : 4;
    if (!encoding || strcmp(encoding, "bit-rows-v1") || size < 21 ||
        size > DEVICE_PAIRING_QR_MAX || (size - 21) % 4 != 0 ||
        !cJSON_IsArray(rows) || cJSON_GetArraySize(rows) != size) return;
    if (quiet < 2) quiet = 2;
    if (quiet > 8) quiet = 8;
    for (int y = 0; y < size; y++) {
        cJSON *row = cJSON_GetArrayItem(rows, y);
        if (!cJSON_IsString(row) || !row->valuestring ||
            strlen(row->valuestring) != (size_t)size) return;
        for (int x = 0; x < size; x++)
            if (row->valuestring[x] != '0' && row->valuestring[x] != '1') return;
    }
    out->qr_size = size;
    out->qr_quiet_zone = quiet;
    for (int y = 0; y < size; y++)
        snprintf(out->qr_rows[y], sizeof(out->qr_rows[y]), "%s",
                 cJSON_GetArrayItem(rows, y)->valuestring);
}

int device_pairing_parse_code(const char *json, DevicePairingCode *out,
                              char *error, size_t error_cap) {
    if (!json || !out) { set_error(error, error_cap, NULL); return -1; }
    memset(out, 0, sizeof(*out));
    cJSON *root = cJSON_Parse(json);
    if (!root) { set_error(error, error_cap, "Resposta ilegivel do Nplay"); return -1; }
    const char *user_code = json_string(root, "user_code");
    const char *device_code = json_string(root, "device_code");
    const char *uri = json_string(root, "verification_uri_complete");
    cJSON *interval = cJSON_GetObjectItemCaseSensitive(root, "interval");
    cJSON *expires = cJSON_GetObjectItemCaseSensitive(root, "expires_in");
    int ok = is_public_code(user_code) && is_device_secret(device_code) && uri &&
             !strncmp(uri, "https://", 8) && strlen(uri) < sizeof(out->verification_uri);
    if (ok) {
        snprintf(out->user_code, sizeof(out->user_code), "%s", user_code);
        snprintf(out->device_code, sizeof(out->device_code), "%s", device_code);
        snprintf(out->verification_uri, sizeof(out->verification_uri), "%s", uri);
        out->interval_seconds = cJSON_IsNumber(interval) ? interval->valueint : 5;
        out->expires_seconds = cJSON_IsNumber(expires) ? expires->valueint : 600;
        if (out->interval_seconds < 3) out->interval_seconds = 3;
        if (out->interval_seconds > 60) out->interval_seconds = 60;
        if (out->expires_seconds < 30) out->expires_seconds = 30;
        if (out->expires_seconds > 1800) out->expires_seconds = 1800;
        parse_qr(root, out); /* QR opcional: o codigo digitavel continua funcionando. */
    }
    cJSON_Delete(root);
    if (!ok) { memset(out, 0, sizeof(*out)); set_error(error, error_cap, NULL); return -1; }
    if (error && error_cap) error[0] = '\0';
    return 0;
}

int device_pairing_parse_token(const char *json, DevicePairingToken *out,
                               char *error, size_t error_cap) {
    if (!json || !out) { set_error(error, error_cap, NULL); return -1; }
    memset(out, 0, sizeof(*out));
    cJSON *root = cJSON_Parse(json);
    if (!root) { set_error(error, error_cap, "Resposta ilegivel do Nplay"); return -1; }
    const char *token = json_string(root, "token");
    cJSON *user = cJSON_GetObjectItemCaseSensitive(root, "user");
    const char *username = cJSON_IsObject(user) ? json_string(user, "username") : NULL;
    int ok = token && token[0] && strlen(token) < sizeof(out->token);
    if (ok) {
        snprintf(out->token, sizeof(out->token), "%s", token);
        snprintf(out->username, sizeof(out->username), "%s",
                 username && username[0] ? username : "Conta Nplay");
    }
    cJSON_Delete(root);
    if (!ok) { memset(out, 0, sizeof(*out)); set_error(error, error_cap, NULL); return -1; }
    if (error && error_cap) error[0] = '\0';
    return 0;
}

static const char *response_error(const char *json) {
    static char safe[128];
    safe[0] = '\0';
    if (!json) return NULL;
    cJSON *root = cJSON_Parse(json);
    if (!root) return NULL;
    const char *message = json_string(root, "message");
    if (!message) message = json_string(root, "error");
    if (message) snprintf(safe, sizeof(safe), "%s", message);
    cJSON_Delete(root);
    return safe[0] ? safe : NULL;
}

DevicePairingPoll device_pairing_classify_poll(long http_code, const char *json,
                                                int *interval_seconds,
                                                DevicePairingToken *token,
                                                char *error, size_t error_cap) {
    if (http_code == 200)
        return device_pairing_parse_token(json, token, error, error_cap) == 0 ?
               DEVICE_PAIRING_POLL_SUCCESS : DEVICE_PAIRING_POLL_ERROR;
    if (http_code == 428) return DEVICE_PAIRING_POLL_PENDING;
    if (http_code == 429) {
        if (interval_seconds) {
            *interval_seconds += 5;
            if (*interval_seconds > 60) *interval_seconds = 60;
        }
        return DEVICE_PAIRING_POLL_SLOW_DOWN;
    }
    if (http_code < 0 || http_code >= 500) return DEVICE_PAIRING_POLL_RETRY;
    const char *remote = response_error(json);
    if (http_code == 400 && remote && strstr(remote, "expir")) {
        set_error(error, error_cap, "O codigo expirou. Gere outro codigo.");
        return DEVICE_PAIRING_POLL_EXPIRED;
    }
    if (http_code == 403) {
        set_error(error, error_cap, "A conexao foi recusada no celular.");
        return DEVICE_PAIRING_POLL_DENIED;
    }
    set_error(error, error_cap, remote);
    return DEVICE_PAIRING_POLL_ERROR;
}

void device_pairing_format_code(const char *code, char *out, size_t out_cap) {
    if (!out || out_cap == 0) return;
    if (code && strlen(code) == 6)
        snprintf(out, out_cap, "%.3s %.3s", code, code + 3);
    else snprintf(out, out_cap, "%s", code ? code : "");
}
