#ifndef GO_REPLAY_H
#define GO_REPLAY_H

#include "go.h"

#ifndef REPLAY_FRAME_INTERVAL
#define REPLAY_FRAME_INTERVAL 30
#endif

/* Advance the replay by one move if enough frames have elapsed.
 * Returns 1 if a move was played, 0 otherwise. */
uint8_t go_replay_step(game_t *g);

#endif /* GO_REPLAY_H */
