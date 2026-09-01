#ifndef CURSOR_H
#define CURSOR_H

#include "go.h"
#include "input.h"
#include <stdint.h>

#define CURSOR_MIN_STEP 8 /* minimum 8.8 subpixel movement per frame */

typedef struct cursor {
    uint8_t col;                 /* target board column [0, board.width)  */
    uint8_t row;                 /* target board row    [0, board.height) */
    uint8_t board_w;             /* cached immutable board width          */
    uint8_t board_h;             /* cached immutable board height         */
    volatile uint8_t ghost_tile; /* desired tile, 0 when hidden           */
    uint8_t target_x;            /* target OAM X coordinate    */
    uint8_t target_y;            /* target OAM Y coordinate    */
    uint16_t x;                  /* current OAM X, 8.8 fixed   */
    uint16_t y;                  /* current OAM Y, 8.8 fixed   */
} cursor_t;

/* Initialize cursor at (col, row), snap position, set up OAM sprites. */
void cursor_init(cursor_t *c, uint8_t col, uint8_t row, const game_t *g);

/*
 * VBlank-owned directional input. Updates the logical target even while
 * the main loop is blocked publishing a large board animation.
 */
void cursor_vbl_handle_input(void);

/* VBlank-owned smooth tracking and shadow OAM positioning. */
void cursor_vbl_update_oam(void);

/* Refresh ghost legality, color, and visibility while game state is stable. */
void cursor_refresh_ghost(cursor_t *c, const game_t *g);

#endif /* CURSOR_H */
