#include "diag.h"
#include <switch.h>
#include <SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>

#define PLAYER_LOG "sdmc:/switch/.nplay-player-trace.log"
#define PLAYER_PREV_LOG "sdmc:/switch/.nplay-player-trace.prev.log"
#define NETWORK_LOG "sdmc:/switch/.nplay-network-trace.log"
#define NETWORK_LOG_LIMIT (64 * 1024)

static SDL_mutex *g_diag_mutex = NULL;
static SDL_atomic_t g_player_sequence = {0};

void diag_init(void) {
    if (!g_diag_mutex) g_diag_mutex = SDL_CreateMutex();
}

void diag_exit(void) {
    if (g_diag_mutex) SDL_DestroyMutex(g_diag_mutex);
    g_diag_mutex = NULL;
}

static void diag_lock(void) {
    if (g_diag_mutex) SDL_LockMutex(g_diag_mutex);
}

static void diag_unlock(void) {
    if (g_diag_mutex) SDL_UnlockMutex(g_diag_mutex);
}

static void append_line(const char *path, const char *line, int rotate_network) {
    diag_lock();
    if (rotate_network) {
        FILE *check = fopen(path, "rb");
        long size = 0;
        if (check) { fseek(check, 0, SEEK_END); size = ftell(check); fclose(check); }
        if (size >= NETWORK_LOG_LIMIT) {
            remove(path);
        }
    }
    FILE *file = fopen(path, "ab");
    if (file) {
        fprintf(file, "%s\n", line ? line : "");
        fflush(file);
        fclose(file);
    }
    diag_unlock();
}

void diag_player_begin(int item_id, int session_id, int source_id,
                       const char *container, const char *delivery) {
    diag_lock();
    remove(PLAYER_PREV_LOG);
    rename(PLAYER_LOG, PLAYER_PREV_LOG);
    FILE *file = fopen(PLAYER_LOG, "wb");
    if (file) {
        fprintf(file, "NPLAY_DIAG version=%s mode=%s item=%d session=%d source=%d container=%s delivery=%s\n",
                APP_VERSION_STR,
                appletGetAppletType() == AppletType_Application ? "application" : "applet",
                item_id, session_id, source_id,
                container && container[0] ? container : "unknown",
                delivery && delivery[0] ? delivery : "unknown");
        fflush(file);
        fclose(file);
    }
    SDL_AtomicSet(&g_player_sequence, 0);
    diag_unlock();
}

void diag_player_event(const char *component, const char *event,
                       const char *format, ...) {
    char detail[104] = "";
    if (format && format[0]) {
        va_list args;
        va_start(args, format);
        vsnprintf(detail, sizeof(detail), format, args);
        va_end(args);
    }
    u64 process_memory = 0;
    svcGetInfo(&process_memory, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    struct mallinfo heap = mallinfo();
    unsigned sequence = (unsigned)SDL_AtomicAdd(&g_player_sequence, 1) + 1;
    char line[DIAG_LINE_CAP];
    snprintf(line, sizeof(line), "%04u %10u heap=%u/%uKB proc=%lluMB %-7.7s %-24.24s %s",
             sequence, SDL_GetTicks(),
             (unsigned)(heap.uordblks / 1024), (unsigned)(heap.fordblks / 1024),
             (unsigned long long)(process_memory / (1024 * 1024)),
             component ? component : "player", event ? event : "event", detail);
    append_line(PLAYER_LOG, line, 0);
}

void diag_player_finish(int result_code) {
    diag_player_event("player", "finish", "rc=%d", result_code);
}

static void safe_path(const char *path, char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!path) return;
    const char *query = strchr(path, '?');
    size_t count = query ? (size_t)(query - path) : strlen(path);
    if (count >= cap) count = cap - 1;
    memcpy(out, path, count);
    out[count] = '\0';
}

void diag_network_event(const char *method, const char *path, long http_code,
                        unsigned elapsed_ms, size_t response_bytes) {
    char clean[104], line[DIAG_LINE_CAP];
    safe_path(path, clean, sizeof(clean));
    snprintf(line, sizeof(line), "%10u %-4.4s code=%ld ms=%u bytes=%u %.100s",
             SDL_GetTicks(), method ? method : "?", http_code, elapsed_ms,
             (unsigned)(response_bytes > 0xffffffffu ? 0xffffffffu : response_bytes), clean);
    append_line(NETWORK_LOG, line, 1);
}

static int read_tail(const char *path, char lines[][DIAG_LINE_CAP], int max_lines) {
    if (!lines || max_lines <= 0) return 0;
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    char ring[12][DIAG_LINE_CAP];
    int total = 0;
    char line[DIAG_LINE_CAP];
    while (fgets(line, sizeof(line), file)) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        snprintf(ring[total % 12], DIAG_LINE_CAP, "%s", line);
        total++;
    }
    fclose(file);
    int available = total < 12 ? total : 12;
    int count = available < max_lines ? available : max_lines;
    int first = total - count;
    for (int i = 0; i < count; i++)
        snprintf(lines[i], DIAG_LINE_CAP, "%s", ring[(first + i) % 12]);
    return count;
}

int diag_read_player_tail(char lines[][DIAG_LINE_CAP], int max_lines) {
    return read_tail(PLAYER_LOG, lines, max_lines);
}

int diag_read_network_tail(char lines[][DIAG_LINE_CAP], int max_lines) {
    return read_tail(NETWORK_LOG, lines, max_lines);
}
