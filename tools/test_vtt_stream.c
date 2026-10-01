#include "vtt_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int count;
    char blocks[3][256];
} Seen;

static int seen_block(void *opaque, const char *block, size_t length) {
    Seen *seen = opaque;
    assert(seen->count < 3 && length < sizeof(seen->blocks[0]));
    memcpy(seen->blocks[seen->count], block, length);
    seen->blocks[seen->count][length] = 0;
    seen->count++;
    return 1;
}

static int reject_block(void *opaque, const char *block, size_t length) {
    (void)opaque; (void)block; (void)length;
    return 0;
}

int main(void) {
    VttStream stream;
    Seen seen = {0};
    vtt_stream_init(&stream, seen_block, &seen);
    static const char header_a[] = "\xef\xbb\xbfWEB";
    static const char header_b[] = "VTT - live\r\n\r\n00:00:01.000 --> ";
    static const char cue_a[] = "00:00:02.000\r\nprimeira\r\n\r\n\r\n";
    static const char cue_b[] = "id-2\n00:00:03.000 --> 00:00:04.500\nsegunda";
    assert(vtt_stream_feed(&stream, header_a, sizeof(header_a) - 1));
    assert(vtt_stream_feed(&stream, header_b, sizeof(header_b) - 1));
    assert(stream.header && seen.count == 0);
    assert(vtt_stream_feed(&stream, cue_a, sizeof(cue_a) - 1));
    assert(seen.count == 1);
    assert(!strcmp(seen.blocks[0], "00:00:01.000 --> 00:00:02.000\nprimeira"));
    assert(vtt_stream_feed(&stream, cue_b, sizeof(cue_b) - 1));
    assert(vtt_stream_finish(&stream));
    assert(seen.count == 2);
    assert(!strcmp(seen.blocks[1], "id-2\n00:00:03.000 --> 00:00:04.500\nsegunda"));
    vtt_stream_free(&stream);

    vtt_stream_init(&stream, seen_block, &seen);
    static const char invalid[] = "nao-e-webvtt\n\n";
    assert(!vtt_stream_feed(&stream, invalid, sizeof(invalid) - 1));
    assert(stream.failed);
    vtt_stream_free(&stream);

    vtt_stream_init(&stream, reject_block, NULL);
    static const char bare_header[] = "WEBVTT\n\n";
    static const char reject_cue[] = "00:00:01.000 --> 00:00:02.000\nnao\n\n";
    assert(vtt_stream_feed(&stream, bare_header, sizeof(bare_header) - 1));
    assert(!vtt_stream_feed(&stream, reject_cue, sizeof(reject_cue) - 1));
    assert(stream.failed);
    vtt_stream_free(&stream);

    vtt_stream_init(&stream, seen_block, &seen);
    assert(vtt_stream_feed(&stream, bare_header, sizeof(bare_header) - 1));
    char oversized[VTT_BLOCK_LIMIT + 2];
    memset(oversized, 'x', sizeof(oversized));
    assert(!vtt_stream_feed(&stream, oversized, sizeof(oversized)));
    assert(stream.failed);
    vtt_stream_free(&stream);
    puts("VTT stream: fragmented header, CRLF, keepalive, EOF cue, malformed, callback failure and cap passed");
    return 0;
}
