#pragma once

#include <stdint.h>

typedef enum {
    TOUCH_AXIS_NONE = 0,
    TOUCH_AXIS_HORIZONTAL,
    TOUCH_AXIS_VERTICAL
} TouchAxis;

typedef struct {
    int active;
    int dragging;
    int64_t finger_id;
    int start_x, start_y;
    int last_x, last_y;
    int total_x, total_y;
    uint32_t last_tick;
    float velocity_x, velocity_y;
    TouchAxis axis;
} TouchInput;

typedef struct {
    int accepted;
    int tap;
    int dragged;
    int x, y;
    int total_x, total_y;
    float velocity_x, velocity_y;
    TouchAxis axis;
} TouchFinish;

void touch_input_reset(TouchInput *touch);
int touch_input_begin(TouchInput *touch, int64_t finger_id,
                      int x, int y, uint32_t tick);
int touch_input_move(TouchInput *touch, int64_t finger_id,
                     int x, int y, uint32_t tick, int *dx, int *dy);
TouchFinish touch_input_end(TouchInput *touch, int64_t finger_id,
                            int x, int y, uint32_t tick);

