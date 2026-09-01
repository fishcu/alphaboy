#ifndef GO_REPLAY_H
#define GO_REPLAY_H

#include "go.h"

/* Advance the play/undo stress test by one action.
 * Returns 1 when progress was made, 0 while draining or after completion. */
uint8_t go_replay_step(game_t *g);

#endif /* GO_REPLAY_H */
