#ifndef GO_DRAW_H
#define GO_DRAW_H

#include <stdint.h>

#include "go.h"

/* Return the board-surface tile index for an empty packed coordinate. */
uint8_t surface_tile(uint16_t coord);

/* Return the ko-marked variant of a surface tile for an empty
 * intersection.  Maps hoshi to the center ko tile. */
uint8_t ko_tile(uint16_t coord);

/* Full board redraw  --  used only at init (display off, fast).
 * Draws the decorative frame and all intersections.
 * During gameplay, game_play_move updates tiles incrementally. */
void board_redraw(const game_t *g);

#endif /* GO_DRAW_H */
