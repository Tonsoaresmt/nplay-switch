#pragma once

// Geometria compartilhada com ui_header; sem SDL para testar no host.
#define UI_HEADER_HEIGHT 95
#define UI_HEADER_ACTION_RIGHT_MARGIN 50
#define UI_HEADER_ACTION_TOUCH_PAD 16

static inline int header_action_touch_contains(int x, int y, int screen_width,
                                               int action_width) {
    if (action_width <= 0 || y < 12 || y >= UI_HEADER_HEIGHT - 12) return 0;
    int right = screen_width - UI_HEADER_ACTION_RIGHT_MARGIN;
    int left = right - action_width - UI_HEADER_ACTION_TOUCH_PAD;
    if (left < 0) left = 0;
    return x >= left && x < right + UI_HEADER_ACTION_TOUCH_PAD;
}
