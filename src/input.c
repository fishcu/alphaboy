#include <gb/gb.h>

#include "input.h"

void input_poll(input_t *inp) {
    uint8_t prev = inp->current;
    inp->current = joypad();
    inp->pressed = inp->current & ~prev;
}
