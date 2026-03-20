#ifndef CURSOR_H
#define CURSOR_H

#include "go.h"
#include "input.h"
#include <stdint.h>

#define CURSOR_MIN_STEP 8 /* minimum subpixel movement per frame */

typedef struct cursor {
    uint8_t col;    /* target board column [0, board.width)  */
    uint8_t row;    /* target board row    [0, board.height) */
    uint8_t spread; /* current sprite separation (0, 1, 2)   */
    uint16_t x;     /* current screen X, fixed-point 8.8     */
    uint16_t y;     /* current screen Y, fixed-point 8.8     */
} cursor_t;

/* Initialize cursor at (col, row), snap position, set up OAM sprites. */
void cursor_init(cursor_t *c, uint8_t col, uint8_t row, const game_t *g);

/* Process input, update ghost sprite, animate cursor, write OAM.
 * Call once per frame after input_poll. */
void cursor_update(cursor_t *c, const input_t *inp, const game_t *g);

#endif /* CURSOR_H */
