#pragma once

// Internal entry outcome. Public callers still receive 0=user/terminal exit,
// 1=natural finish, 2=chosen next. Never confuse an opening failure with EOF.
#define PLAYBACK_ENTRY_FAILED (-20)

// GET can safely be repeated within its bounded budget; admissions cannot.
// Exclude auth/plan/rate-limit/TLS denials and unsupported/missing endpoints.
static inline int playback_checkpoint_retryable(long code) {
    return code == 0 || code == -6 || code == -7 || code == -18 ||
           code == -28 || code == -52 || code == -55 || code == -56 ||
           code == 500 || code == 502 || code == 503 || code == 504;
}
