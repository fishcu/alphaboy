#ifndef CURSOR_H
#define CURSOR_H

#include "board.h"
#include "input.h"
#include <stdint.h>


typedef struct cursor {
    uint8_t col; /* board column [0, board.width)  */
    uint8_t row; /* board row    [0, board.height) */
} cursor_t;

/* Initialize cursor at (col, row) and set up OAM sprite entries. */
void cursor_init(cursor_t *c, uint8_t col, uint8_t row);

/* Move cursor based on input state. Call once per frame after input_poll.
 * Clamps position to [0, b->width) x [0, b->height). */
void cursor_update(cursor_t *c, const input_t *inp, const board_t *b);

/* Update OAM positions for the 4 cursor sprites.
 * bkg_x/bkg_y: board origin on the BG tilemap (in tiles). */
void cursor_draw(const cursor_t *c, uint8_t bkg_x, uint8_t bkg_y);

#endif /* CURSOR_H */
