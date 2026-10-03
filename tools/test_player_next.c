#include "player_next.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static int close_to(float a, float b) {
    return fabsf(a - b) < 0.01f;
}

int main(void) {
    PlayerNextUi ui = {0};
    player_next_ui(0, 90.0, 100.0, 1, &ui);
    assert(!ui.available && close_to(ui.alpha, 0.0f));

    player_next_ui(1, 10.0, 100.0, 0, &ui);
    assert(ui.available && close_to(ui.alpha, 0.0f));

    player_next_ui(1, 55.0, 100.0, 0, &ui);
    assert(close_to(ui.alpha, 0.0f) && close_to(ui.progress, 0.0f));

    player_next_ui(1, 56.0, 100.0, 0, &ui);
    assert(ui.alpha > 0.45f && ui.alpha < 0.55f);
    assert(ui.progress > 0.02f && ui.progress < 0.03f);

    player_next_ui(1, 77.5, 100.0, 0, &ui);
    assert(close_to(ui.alpha, 1.0f) && close_to(ui.progress, 0.5f));

    player_next_ui(1, 20.0, 0.0, 1, &ui);
    assert(close_to(ui.alpha, 1.0f) && close_to(ui.progress, 0.0f));

    // Explicit visibility (pause/pinned HUD/selection) works before credits.
    player_next_ui(1, 120.0, 1440.0, 1, &ui);
    assert(ui.available && close_to(ui.alpha, 1.0f) && close_to(ui.progress, 0.0f));
    player_next_ui(0, 120.0, 1440.0, 1, &ui);
    assert(!ui.available && close_to(ui.alpha, 0.0f));

    puts("next episode UI: ok");
    return 0;
}
