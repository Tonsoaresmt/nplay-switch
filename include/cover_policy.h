#pragma once
#include <stdint.h>

#define COVER_URL_MAX 2048
#define COVER_URL_BYTES_MAX (3u * 1024u * 1024u)
#define COVER_READY_MAX 16
#define COVER_READY_BYTES_MAX (16u * 1024u * 1024u)
#define COVER_TEXTURE_BYTES_MAX (64u * 1024u * 1024u)
#define COVER_DOWNLOAD_MAX (4u * 1024u * 1024u)

static inline uint32_t cover_retry_delay(unsigned failures, long http) {
    if (http == 404 || http == 410) return 300000;
    if (failures <= 1) return 2000;
    if (failures == 2) return 5000;
    if (failures == 3) return 15000;
    return 60000;
}
// Preserve aspect ratio. Never upscale avatars/posters. A single retained
// landscape uses at most 1280x720; portrait at most 768 high; square 384.
static inline int cover_surface_size(int w, int h, int *dw, int *dh) {
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return 0;
    int maxw = w > h ? 1280 : w < h ? 768 : 384;
    int maxh = w > h ? 720 : w < h ? 768 : 384;
    double scale = 1.0;
    if (w > maxw) scale = (double)maxw / w;
    if (h * scale > maxh) scale = (double)maxh / h;
    *dw = (int)(w * scale); *dh = (int)(h * scale);
    if (*dw < 1) *dw = 1;
    if (*dh < 1) *dh = 1;
    return 1;
}
