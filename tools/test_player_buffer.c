#include "player_buffer.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(DEMUX_QUEUE_PACKETS == 128);
    assert(demux_buffer_can_enqueue(127, 1000, 100));
    assert(!demux_buffer_can_enqueue(128, 0, 1));
    assert(demux_buffer_can_enqueue(0, 0, DEMUX_QUEUE_BYTES));
    assert(!demux_buffer_can_enqueue(1, DEMUX_QUEUE_BYTES, 1));
    assert(!demux_buffer_can_enqueue(0, 0, DEMUX_QUEUE_BYTES + 1));
    assert(!demux_buffer_can_enqueue(0, (size_t)-1, 1));
    unsigned seed = 17;
    int count = 0; size_t bytes = 0, queue[DEMUX_QUEUE_PACKETS];
    unsigned head = 0, peak = 0;
    for (int i = 0; i < 100000; i++) {
        seed = seed * 1664525u + 1013904223u;
        size_t incoming = (seed % (1024 * 1024)) + 1;
        if (demux_buffer_can_enqueue(count, bytes, incoming)) {
            queue[(head + count) % DEMUX_QUEUE_PACKETS] = incoming;
            bytes += incoming; count++;
            if ((unsigned)count > peak) peak = (unsigned)count;
        } else if (count) {
            bytes -= queue[head]; head = (head + 1) % DEMUX_QUEUE_PACKETS; count--;
        }
        assert(count <= DEMUX_QUEUE_PACKETS && bytes <= DEMUX_QUEUE_BYTES);
        if (i % 97 == 0) { head = 0; count = 0; bytes = 0; } // seek/reset
    }
    // A realistic small-packet stream no longer saturates at 32 packets.
    count = 0; bytes = 0;
    while (demux_buffer_can_enqueue(count, bytes, 12000)) { count++; bytes += 12000; }
    assert(count == 128 && bytes < DEMUX_QUEUE_BYTES);
    printf("OK buffer policy: 100000 operations, strict 4MiB queue, 128 slots (peak=%u)\n", peak);
    return 0;
}
