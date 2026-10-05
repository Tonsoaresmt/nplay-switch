#pragma once
// Geometry shared by drawing and modal input. No SDL/FFmpeg dependency.
enum { PLAYER_TOUCH_NONE, PLAYER_TOUCH_CANCEL, PLAYER_TOUCH_CONFIRM,
       PLAYER_TOUCH_PREVIEW, PLAYER_TOUCH_EPISODE };
#define PLAYER_TOUCH_BAR_LEFT 48
#define PLAYER_TOUCH_BAR_RIGHT 1128
#define PLAYER_TOUCH_CONFIRM_X 820
#define PLAYER_TOUCH_CANCEL_X 1032
#define PLAYER_TOUCH_BUTTON_Y 636
#define PLAYER_TOUCH_BUTTON_W 192
#define PLAYER_TOUCH_BUTTON_H 52
static inline int player_episode_first(int selected, int count) {
    int first = selected - 4;
    if (first > count - 8) first = count - 8;
    return first < 0 ? 0 : first;
}
static inline int player_touch_back(int x, int y) {
    return x >= 24 && x < 180 && y >= 24 && y < 96;
}
static inline int player_touch_timeline(int x, int y) {
    if (player_touch_back(x, y)) return PLAYER_TOUCH_CANCEL;
    if (y >= PLAYER_TOUCH_BUTTON_Y && y < PLAYER_TOUCH_BUTTON_Y + PLAYER_TOUCH_BUTTON_H) {
        if (x >= PLAYER_TOUCH_CONFIRM_X && x < PLAYER_TOUCH_CONFIRM_X + PLAYER_TOUCH_BUTTON_W) return PLAYER_TOUCH_CONFIRM;
        if (x >= PLAYER_TOUCH_CANCEL_X && x < PLAYER_TOUCH_CANCEL_X + PLAYER_TOUCH_BUTTON_W) return PLAYER_TOUCH_CANCEL;
    }
    if (x >= PLAYER_TOUCH_BAR_LEFT && x <= PLAYER_TOUCH_BAR_RIGHT && y >= 574 && y < 630) return PLAYER_TOUCH_PREVIEW;
    return PLAYER_TOUCH_NONE;
}
static inline int player_touch_episode(int x, int y, int selected, int count) {
    if (x < 720 || x >= 1240 || y < 110 || y >= 110 + 8 * 62) return -1;
    int offset = y - 110;
    if (offset % 62 >= 54) return -1; // Row gaps are not actionable.
    int row = player_episode_first(selected, count) + offset / 62;
    return row < count ? row : -1;
}
