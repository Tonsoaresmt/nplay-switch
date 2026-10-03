#pragma once
#include <stddef.h>
#define DEMUX_QUEUE_PACKETS 128
#define DEMUX_QUEUE_BYTES (4u * 1024u * 1024u)
// The reader can hold ONE pending packet (also <=4 MiB), separate from queue.
// Test this policy on host; pause/stop must take priority over space waits.
static inline int demux_buffer_can_enqueue(int count, size_t bytes, size_t incoming) {
    return count >= 0 && count < DEMUX_QUEUE_PACKETS &&
        bytes <= DEMUX_QUEUE_BYTES && incoming <= DEMUX_QUEUE_BYTES - bytes;
}

// After a visible starvation, rebuild a small video cushion before consuming
// one packet at a time. Never deadlock on a full queue, EOF, or unknown PTS.
static inline int demux_buffer_should_refill(int count, size_t bytes,
                                             double video_seconds, int terminal,
                                             unsigned waiting_ms) {
    return waiting_ms >= 250u && waiting_ms < 3000u && !terminal &&
           count < DEMUX_QUEUE_PACKETS && bytes < DEMUX_QUEUE_BYTES * 3u / 4u &&
           !(video_seconds >= 0.75);
}
