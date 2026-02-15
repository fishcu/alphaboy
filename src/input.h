#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

typedef struct input {
    uint8_t current; /* buttons held this frame          */
    uint8_t pressed; /* buttons newly pressed this frame */
} input_t;

/* Sample the joypad and update all derived fields.
 * Call exactly once per frame, before any consumers read the state. */
void input_poll(input_t *inp);

#endif /* INPUT_H */
