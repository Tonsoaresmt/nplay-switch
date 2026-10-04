#include "player_buffer.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(DEMUX_QUEUE_PACKETS == 512);
    assert(demux_buffer_can_enqueue(127, 1000, 100));
    assert(!demux_buffer_can_enqueue(512, 0, 1));
    assert(demux_buffer_can_enqueue(511, 1000, 100));
    assert(demux_buffer_can_enqueue(0, 0, DEMUX_QUEUE_BYTES));
    assert(!demux_buffer_can_enqueue(1, DEMUX_QUEUE_BYTES, 1));
    assert(!demux_buffer_can_enqueue(0, 0, DEMUX_QUEUE_BYTES + 1));
    assert(!demux_buffer_can_enqueue(0, (size_t)-1, 1));
    assert(!demux_buffer_should_refill(1, 1000, 0, 0, 249));
    assert(demux_buffer_should_refill(8, 8000, 0.2, 0, 250));
    assert(!demux_buffer_should_refill(30, 30000, 0.75, 0, 700));
    assert(!demux_buffer_should_refill(512, 30000, 0, 0, 700));
    assert(!demux_buffer_should_refill(3, DEMUX_QUEUE_BYTES * 3u / 4u, 0, 0, 700));
    assert(!demux_buffer_should_refill(8, 8000, 0.2, -1, 700));
    assert(!demux_buffer_should_refill(8, 8000, 0, 0, 3000));
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
    assert(count == 349 && bytes <= DEMUX_QUEUE_BYTES);
    // Remux sequencial: 10 s de video 1080p (~6 MB) cabem antes do audio.
    assert(demux_buffer_can_enqueue_cap(240, 6u * 1024u * 1024u, 30000, DEMUX_QUEUE_BYTES_SEQUENTIAL));
    assert(!demux_buffer_can_enqueue_cap(240, 6u * 1024u * 1024u, 30000, DEMUX_QUEUE_BYTES));
    assert(!demux_buffer_can_enqueue_cap(1, DEMUX_QUEUE_BYTES_SEQUENTIAL, 1, DEMUX_QUEUE_BYTES_SEQUENTIAL));
    assert(demux_buffer_prefer_audio(400, 0) && !demux_buffer_prefer_audio(400, 1));
    assert(!demux_buffer_prefer_audio(1500, 0));
    printf("OK buffer policy: 100000 operations, strict 4MiB/12MiB queue, 512 slots (peak=%u)\n", peak);
    return 0;
}
