#include "touch_input.h"
#include <stdlib.h>
#include <string.h>

// Um dedo pode oscilar alguns pixels mesmo parado. So depois dessa distancia o
// gesto deixa de ser um toque e passa a controlar uma rolagem.
#define TOUCH_DRAG_THRESHOLD 12
#define TOUCH_VELOCITY_BLEND 0.32f

void touch_input_reset(TouchInput *touch) {
    if (touch) memset(touch, 0, sizeof(*touch));
}

int touch_input_begin(TouchInput *touch, int64_t finger_id,
                      int x, int y, uint32_t tick) {
    if (!touch || touch->active) return 0;
    touch_input_reset(touch);
    touch->active = 1;
    touch->finger_id = finger_id;
    touch->start_x = touch->last_x = x;
    touch->start_y = touch->last_y = y;
    touch->last_tick = tick;
    return 1;
}

int touch_input_move(TouchInput *touch, int64_t finger_id,
                     int x, int y, uint32_t tick, int *dx, int *dy) {
    if (dx) *dx = 0;
    if (dy) *dy = 0;
    if (!touch || !touch->active || touch->finger_id != finger_id) return 0;

    int step_x = x - touch->last_x;
    int step_y = y - touch->last_y;
    touch->total_x = x - touch->start_x;
    touch->total_y = y - touch->start_y;
    if (!touch->dragging &&
        (abs(touch->total_x) >= TOUCH_DRAG_THRESHOLD ||
         abs(touch->total_y) >= TOUCH_DRAG_THRESHOLD)) {
        touch->dragging = 1;
        touch->axis = abs(touch->total_x) > abs(touch->total_y) ?
                      TOUCH_AXIS_HORIZONTAL : TOUCH_AXIS_VERTICAL;
    }

    uint32_t elapsed = tick - touch->last_tick;
    if (elapsed > 0 && elapsed < 250) {
        float sample_x = (float)step_x / (float)elapsed;
        float sample_y = (float)step_y / (float)elapsed;
        touch->velocity_x += (sample_x - touch->velocity_x) * TOUCH_VELOCITY_BLEND;
        touch->velocity_y += (sample_y - touch->velocity_y) * TOUCH_VELOCITY_BLEND;
    }
    touch->last_x = x;
    touch->last_y = y;
    touch->last_tick = tick;

    if (!touch->dragging) return 1;
    if (dx) *dx = touch->axis == TOUCH_AXIS_HORIZONTAL ? step_x : 0;
    if (dy) *dy = touch->axis == TOUCH_AXIS_VERTICAL ? step_y : 0;
    return 1;
}

TouchFinish touch_input_end(TouchInput *touch, int64_t finger_id,
                            int x, int y, uint32_t tick) {
    TouchFinish out = {0};
    if (!touch || !touch->active || touch->finger_id != finger_id) return out;
    int ignored_dx = 0, ignored_dy = 0;
    touch_input_move(touch, finger_id, x, y, tick, &ignored_dx, &ignored_dy);
    out.accepted = 1;
    out.tap = !touch->dragging;
    out.dragged = touch->dragging;
    out.x = x; out.y = y;
    out.total_x = touch->total_x; out.total_y = touch->total_y;
    out.velocity_x = touch->velocity_x; out.velocity_y = touch->velocity_y;
    out.axis = touch->axis;
    touch_input_reset(touch);
    return out;
}

