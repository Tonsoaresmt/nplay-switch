#pragma once
#include <stddef.h>
#define VTT_BLOCK_LIMIT (64u * 1024u)
typedef int (*VttBlockCallback)(void *, const char *, size_t);
typedef struct {
    char *block;
    size_t length, capacity;
    int header, failed, skip_lf;
    VttBlockCallback callback;
    void *userdata;
} VttStream;
void vtt_stream_init(VttStream *, VttBlockCallback, void *);
void vtt_stream_free(VttStream *);
int vtt_stream_feed(VttStream *, const char *, size_t);
int vtt_stream_finish(VttStream *);
