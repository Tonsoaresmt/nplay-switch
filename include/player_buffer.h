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
