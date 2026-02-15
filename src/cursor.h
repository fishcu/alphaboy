#ifndef CURSOR_H
#define CURSOR_H

#include "board.h"
#include "input.h"
#include <stdint.h>

#define CURSOR_MIN_STEP 8 /* minimum subpixel movement per frame */

/* Spread: Cursor sprites separate when moving.
 * Shift 6 -> thresholds at 2px (spread 1) and 4px (spread 2) of remaining
 * distance to target.*/
#define CURSOR_SPREAD_SHIFT 6

typedef struct cursor {
    uint8_t col;    /* target board column [0, board.width)  */
    uint8_t row;    /* target board row    [0, board.height) */
    uint8_t spread; /* current sprite separation (0, 1, 2)   */
    uint16_t x;     /* current screen X, fixed-point 8.8     */
    uint16_t y;     /* current screen Y, fixed-point 8.8     */
} cursor_t;

/* Initialize cursor at (col, row), snap position, set up OAM sprites.
 * bkg_x/bkg_y: board origin on the BG tilemap (in tiles). */
void cursor_init(cursor_t *c, uint8_t col, uint8_t row, uint8_t bkg_x,
                 uint8_t bkg_y);

/* Move cursor based on input, then animate toward target.
 * Call once per frame after input_poll. */
void cursor_update(cursor_t *c, const input_t *inp, const board_t *b,
                   uint8_t bkg_x, uint8_t bkg_y);

/* Update OAM positions from current smoothed coordinates. */
void cursor_draw(const cursor_t *c);

#endif /* CURSOR_H */
