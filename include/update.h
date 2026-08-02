#pragma once

#include <stddef.h>

#ifndef APP_VERSION_STR
#define APP_VERSION_STR "0.6.1"
#endif

#ifndef UPDATE_REPO_OWNER
#define UPDATE_REPO_OWNER ""
#endif

#ifndef UPDATE_REPO_NAME
#define UPDATE_REPO_NAME "meruem-switch"
#endif

enum {
    UPDATE_CHECK_DISABLED = -2,
    UPDATE_CHECK_ERROR = -1,
    UPDATE_CHECK_UP_TO_DATE = 0,
    UPDATE_CHECK_AVAILABLE = 1
};

struct update_info {
    char latest_version[32];
    char asset_name[128];
    char download_url[512];
    char release_notes[512];
    char message[256];
    long http_code;
};

int update_check(struct update_info *info);
int update_apply(const struct update_info *info, const char *target_path, char *err, size_t errcap);
void update_resolve_target_path(const char *argv0, char *out, size_t cap);
