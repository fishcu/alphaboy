#ifndef DEMO_H
#define DEMO_H

/* DEMO_MODE is activated via -DDEMO_MODE from the Makefile
 * (e.g. the "profile" build variant used by `make flamegraph`). */

#ifdef DEMO_MODE

#include "go.h"
#include <stdint.h>

#ifndef DEMO_FRAME_INTERVAL
#define DEMO_FRAME_INTERVAL 30
#endif

/* Advance the demo by one move if enough frames have elapsed.
 * Returns 1 if a move was played, 0 otherwise. */
uint8_t demo_step(game_t *g);

#endif /* DEMO_MODE */
#endif /* DEMO_H */
