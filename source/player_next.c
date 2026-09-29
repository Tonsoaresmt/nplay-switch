#include "player_next.h"

#define NEXT_CARD_WINDOW_SECONDS 45.0
#define NEXT_CARD_FADE_SECONDS    2.0

static float clamp01(double value) {
    if (value <= 0.0) return 0.0f;
    if (value >= 1.0) return 1.0f;
    return (float)value;
}

void player_next_ui(int has_next, double position, double duration,
                    int selected, PlayerNextUi *out) {
    if (!out) return;
    out->available = has_next ? 1 : 0;
    out->alpha = 0.0f;
    out->progress = 0.0f;
    if (!has_next) return;

    double remaining = duration > 0.0 ? duration - position : -1.0;
    if (remaining >= 0.0 && remaining <= NEXT_CARD_WINDOW_SECONDS) {
        out->progress = clamp01((NEXT_CARD_WINDOW_SECONDS - remaining) /
                                NEXT_CARD_WINDOW_SECONDS);
        out->alpha = clamp01((NEXT_CARD_WINDOW_SECONDS - remaining) /
                             NEXT_CARD_FADE_SECONDS);
    }
    if (selected) out->alpha = 1.0f;
}
