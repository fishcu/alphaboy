#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

/* Typematic repeat: initial delay before auto-repeat starts,
 * then repeat fires every REPEAT_RATE frames. (in frames, ~60 fps) */
#define INPUT_REPEAT_DELAY  20
#define INPUT_REPEAT_RATE    6

typedef struct input {
    uint8_t current;      /* buttons held this frame              */
    uint8_t pressed;      /* buttons newly pressed this frame     */
    uint8_t repeated;     /* buttons firing due to auto-repeat    */
    uint8_t repeat_timer; /* frames since d-pad state last changed */
} input_t;

/* Sample the joypad and update all derived fields.
 * Call exactly once per frame, before any consumers read the state. */
void input_poll(input_t *inp);

#endif /* INPUT_H */
