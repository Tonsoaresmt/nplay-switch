#pragma once
#include <stdint.h>

// Desired and applied tracks differ during a transactional background switch.
// Retrying the desired track must not require destroying the applied track.
typedef struct {
    int desired, pending;
    unsigned attempts;
    uint32_t deadline;
} SubtitleRetry;

static inline void subtitle_retry_select(SubtitleRetry *retry, int choice) {
    *retry = (SubtitleRetry){ .desired = choice };
}
static inline int subtitle_retry_failed(SubtitleRetry *retry, int choice, uint32_t now) {
    static const uint32_t delays[] = {2000, 5000, 10000, 20000};
    if (choice < 0 || choice != retry->desired || retry->attempts >= 4) return 0;
    retry->deadline = now + delays[retry->attempts++];
    retry->pending = 1;
    return 1;
}
static inline int subtitle_retry_due(SubtitleRetry *retry, uint32_t now) {
    if (!retry->pending || retry->desired < 0 || (int32_t)(now - retry->deadline) < 0) return -1;
    retry->pending = 0;
    return retry->desired;
}
static inline void subtitle_retry_succeeded(SubtitleRetry *retry, int choice) {
    if (choice == retry->desired) { retry->attempts = 0; retry->pending = 0; }
}
