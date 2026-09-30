#pragma once

#include <stddef.h>

#define DEVICE_PAIRING_QR_MAX 69

typedef struct {
    char user_code[16];
    char device_code[112];
    char verification_uri[320];
    int interval_seconds;
    int expires_seconds;
    int qr_size;
    int qr_quiet_zone;
    char qr_rows[DEVICE_PAIRING_QR_MAX][DEVICE_PAIRING_QR_MAX + 1];
} DevicePairingCode;

typedef struct {
    char token[640];
    char username[128];
} DevicePairingToken;

typedef enum {
    DEVICE_PAIRING_POLL_RETRY = -1,
    DEVICE_PAIRING_POLL_ERROR = 0,
    DEVICE_PAIRING_POLL_PENDING = 1,
    DEVICE_PAIRING_POLL_SUCCESS = 2,
    DEVICE_PAIRING_POLL_SLOW_DOWN = 3,
    DEVICE_PAIRING_POLL_EXPIRED = 4,
    DEVICE_PAIRING_POLL_DENIED = 5
} DevicePairingPoll;

int device_pairing_parse_code(const char *json, DevicePairingCode *out,
                              char *error, size_t error_cap);
int device_pairing_parse_token(const char *json, DevicePairingToken *out,
                               char *error, size_t error_cap);
DevicePairingPoll device_pairing_classify_poll(long http_code, const char *json,
                                                int *interval_seconds,
                                                DevicePairingToken *token,
                                                char *error, size_t error_cap);
void device_pairing_format_code(const char *code, char *out, size_t out_cap);
