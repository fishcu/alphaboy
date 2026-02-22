#ifndef DEMO_H
#define DEMO_H

/* Comment out the next line to disable demo mode. */
#define DEMO_MODE

#ifdef DEMO_MODE

#include "go.h"
#include <stdint.h>

#define DEMO_FRAME_INTERVAL 30

/* Advance the demo by one move if enough frames have elapsed.
 * Returns 1 if a move was played, 0 otherwise. */
uint8_t demo_step(game_t *g, uint16_t *queue, uint8_t *visited);

#endif /* DEMO_MODE */
#endif /* DEMO_H */
