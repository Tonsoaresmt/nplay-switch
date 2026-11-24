#pragma once
#include <stddef.h>
#include <string.h>

// Body limits are route-specific and count received bytes, not Content-Length.
// Explicit media/descriptor limits always win over this default policy.
static inline size_t response_body_budget(const char *url) {
    const char *path = url ? strstr(url, "://") : NULL;
    path = path ? strchr(path + 3, '/') : url;
    if (!path) path = "";
    if (!strncmp(path, "/api/catalog/", 13)) return 16u * 1024u * 1024u;
    if (!strncmp(path, "/api/sync/progress", 18) ||
        !strncmp(path, "/api/accel/jobs", 15)) return 8u * 1024u * 1024u;
    return 4u * 1024u * 1024u;
}
