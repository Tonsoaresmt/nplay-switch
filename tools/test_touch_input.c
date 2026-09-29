#include "touch_input.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    TouchInput touch = {0};
    int dx = 0, dy = 0;

    assert(touch_input_begin(&touch, 7, 300, 200, 100));
    assert(!touch_input_begin(&touch, 8, 500, 400, 101));
    assert(touch_input_move(&touch, 7, 306, 205, 110, &dx, &dy));
    assert(!touch.dragging && dx == 0 && dy == 0);
    TouchFinish tap = touch_input_end(&touch, 7, 306, 205, 120);
    assert(tap.accepted && tap.tap && !tap.dragged);

    assert(touch_input_begin(&touch, 11, 800, 500, 200));
    assert(touch_input_move(&touch, 11, 758, 496, 216, &dx, &dy));
    assert(touch.dragging && touch.axis == TOUCH_AXIS_HORIZONTAL);
    assert(dx == -42 && dy == 0);
    assert(touch_input_move(&touch, 11, 720, 470, 232, &dx, &dy));
    assert(dx == -38 && dy == 0); // eixo horizontal permanece travado.
    TouchFinish horizontal = touch_input_end(&touch, 11, 700, 455, 248);
    assert(horizontal.dragged && horizontal.axis == TOUCH_AXIS_HORIZONTAL);
    assert(horizontal.total_x == -100 && horizontal.velocity_x < 0.0f);

    assert(touch_input_begin(&touch, 15, 400, 180, 300));
    assert(!touch_input_move(&touch, 99, 400, 120, 316, &dx, &dy));
    assert(touch_input_move(&touch, 15, 405, 130, 316, &dx, &dy));
    assert(touch.axis == TOUCH_AXIS_VERTICAL && dx == 0 && dy == -50);
    TouchFinish wrong = touch_input_end(&touch, 99, 405, 100, 332);
    assert(!wrong.accepted && touch.active);
    TouchFinish vertical = touch_input_end(&touch, 15, 405, 100, 332);
    assert(vertical.dragged && vertical.axis == TOUCH_AXIS_VERTICAL);

    puts("touch input: tap, axis lock, drag and finger ownership ok");
    return 0;
}
