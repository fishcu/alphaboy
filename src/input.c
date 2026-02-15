#include <gb/gb.h>

#include "input.h"

#define DPAD_MASK (J_LEFT | J_RIGHT | J_UP | J_DOWN)

void input_poll(input_t *inp) {
    uint8_t prev = inp->current;
    inp->current = joypad();
    inp->pressed = inp->current & ~prev;
    inp->repeated = 0;

    uint8_t held      = inp->current & DPAD_MASK;
    uint8_t prev_held = prev & DPAD_MASK;

    if (held != prev_held) {
        /* D-pad state changed (new press, release, or direction switch). */
        inp->repeat_timer = 0;
    } else if (held) {
        inp->repeat_timer++;
        if (inp->repeat_timer == INPUT_REPEAT_DELAY) {
            inp->repeated = held;
            inp->repeat_timer = INPUT_REPEAT_DELAY - INPUT_REPEAT_RATE;
        }
    } else {
        inp->repeat_timer = 0;
    }
}
