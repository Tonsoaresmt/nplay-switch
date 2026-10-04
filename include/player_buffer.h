#pragma once
#include <stddef.h>
// Pacotes de audio AAC tem poucas centenas de bytes: a contagem nao deve ser o
// limite real. 512 slots deixam o teto em bytes decidir o tamanho da reserva.
#define DEMUX_QUEUE_PACKETS 512
#define DEMUX_QUEUE_BYTES (4u * 1024u * 1024u)
// Remux sequencial (MKV de torrent -> fMP4 frag_keyframe) grava um fragmento
// por quadro-chave: 5-10 s so de video e depois o audio do mesmo trecho. A
// reserva maior permite alcancar esse audio sem esperar o video ser exibido.
#define DEMUX_QUEUE_BYTES_SEQUENTIAL (12u * 1024u * 1024u)
// The reader can hold ONE pending packet (also <=cap), separate from queue.
// Test this policy on host; pause/stop must take priority over space waits.
static inline int demux_buffer_can_enqueue_cap(int count, size_t bytes, size_t incoming,
                                               size_t cap) {
    return count >= 0 && count < DEMUX_QUEUE_PACKETS &&
        bytes <= cap && incoming <= cap - bytes;
}
static inline int demux_buffer_can_enqueue(int count, size_t bytes, size_t incoming) {
    return demux_buffer_can_enqueue_cap(count, bytes, incoming, DEMUX_QUEUE_BYTES);
}

// Audio na frente do proximo video: quando a fila do SDL esta baixa, o audio
// que ja esta na reserva e decodificado antes, sem esperar a apresentacao do
// video que veio antes dele no arquivo.
static inline int demux_buffer_prefer_audio(unsigned queued_audio_ms, int paused) {
    return !paused && queued_audio_ms < 1000u;
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
