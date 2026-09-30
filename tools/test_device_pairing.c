#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "device_pairing.h"

static char *make_code_response(int bad_row) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "user_code", "123456");
    cJSON_AddStringToObject(root, "device_code", "0123456789abcdef0123456789abcdef0123456789abcdef");
    cJSON_AddStringToObject(root, "verification_uri_complete", "https://nplay.uk/?tv=0#/pair?code=123456");
    cJSON_AddNumberToObject(root, "interval", 5);
    cJSON_AddNumberToObject(root, "expires_in", 600);
    cJSON *qr = cJSON_AddObjectToObject(root, "qr");
    cJSON_AddStringToObject(qr, "encoding", "bit-rows-v1");
    cJSON_AddNumberToObject(qr, "size", 21);
    cJSON_AddNumberToObject(qr, "quiet_zone", 4);
    cJSON *rows = cJSON_AddArrayToObject(qr, "rows");
    for (int i = 0; i < 21; i++)
        cJSON_AddItemToArray(rows, cJSON_CreateString(i == 20 && bad_row ? "101" : "101010101010101010101"));
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

int main(void) {
    DevicePairingCode code;
    char error[160];
    char *json = make_code_response(0);
    assert(device_pairing_parse_code(json, &code, error, sizeof(error)) == 0);
    assert(!strcmp(code.user_code, "123456"));
    assert(code.qr_size == 21 && code.qr_quiet_zone == 4);
    assert(strstr(json, code.device_code));
    cJSON_free(json);

    json = make_code_response(1);
    assert(device_pairing_parse_code(json, &code, error, sizeof(error)) == 0);
    assert(code.qr_size == 0); /* QR ruim nao inutiliza o codigo digitavel. */
    cJSON_free(json);

    DevicePairingToken token;
    int interval = 5;
    assert(device_pairing_classify_poll(428, "{\"error\":\"authorization_pending\"}",
           &interval, &token, error, sizeof(error)) == DEVICE_PAIRING_POLL_PENDING);
    assert(device_pairing_classify_poll(429, "{\"error\":\"slow_down\"}",
           &interval, &token, error, sizeof(error)) == DEVICE_PAIRING_POLL_SLOW_DOWN);
    assert(interval == 10);
    assert(device_pairing_classify_poll(200,
           "{\"token\":\"jwt.example\",\"user\":{\"username\":\"visitante\"}}",
           &interval, &token, error, sizeof(error)) == DEVICE_PAIRING_POLL_SUCCESS);
    assert(!strcmp(token.token, "jwt.example"));
    assert(!strcmp(token.username, "visitante"));
    assert(device_pairing_classify_poll(503, NULL, &interval, &token, error, sizeof(error)) ==
           DEVICE_PAIRING_POLL_RETRY);

    char formatted[16];
    device_pairing_format_code("123456", formatted, sizeof(formatted));
    assert(!strcmp(formatted, "123 456"));
    puts("Device pairing parser and polling policy passed.");
    return 0;
}
