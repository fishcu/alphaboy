#ifndef CURSOR_H
#define CURSOR_H

#include "go.h"
#include "input.h"
#include <stdint.h>

#define CURSOR_MIN_STEP 8 /* minimum subpixel movement per frame */

typedef struct cursor {
    uint8_t col;           /* target board column [0, board.width)  */
    uint8_t row;           /* target board row    [0, board.height) */
    uint8_t spread;        /* current sprite separation (0, 1, 2)   */
    uint16_t x;            /* current screen X, fixed-point 8.8     */
    uint16_t y;            /* current screen Y, fixed-point 8.8     */
    uint8_t ghost_tile;    /* stone tile to flicker, 0 = inactive   */
    uint8_t surface_cache; /* cached surface tile at (col, row)     */
} cursor_t;

/* Initialize cursor at (col, row), snap position, set up OAM sprites. */
void cursor_init(cursor_t *c, uint8_t col, uint8_t row, const game_t *g);

/* Move cursor based on input, then animate toward target.
 * Call once per frame after input_poll. */
void cursor_update(cursor_t *c, const input_t *inp, const game_t *g);

/* Update OAM positions from current smoothed coordinates. */
void cursor_draw(const cursor_t *c);

#endif /* CURSOR_H */
