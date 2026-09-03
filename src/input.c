#include <gb/gb.h>

#include "input.h"
#include "memory.h"

#define DPAD_MASK (J_LEFT | J_RIGHT | J_UP | J_DOWN)

void input_poll(void) {
    const uint8_t prev = game_input->current;
    const uint8_t current = joypad();
    game_input->current = current;
    game_input->pressed = current & ~prev;
    game_input->repeated = 0;

    const uint8_t held = current & DPAD_MASK;
    const uint8_t prev_held = prev & DPAD_MASK;

    if (held != prev_held) {
        /* D-pad state changed (new press, release, or direction switch). */
        game_input->repeat_timer = 0;
    } else if (held) {
        game_input->repeat_timer++;
        if (game_input->repeat_timer == INPUT_REPEAT_DELAY) {
            game_input->repeated = held;
            game_input->repeat_timer = INPUT_REPEAT_DELAY - INPUT_REPEAT_RATE;
        }
    } else {
        game_input->repeat_timer = 0;
    }
}
