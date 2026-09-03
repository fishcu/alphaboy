#ifndef CURSOR_H
#define CURSOR_H

#include <stdint.h>

#define CURSOR_MIN_STEP 8 /* minimum 8.8 subpixel movement per frame */

typedef struct cursor {
    uint8_t col;      /* target board column [0, board.width)  */
    uint8_t row;      /* target board row    [0, board.height) */
    uint16_t coord;   /* packed target board coordinate        */
    uint8_t target_x; /* target OAM X coordinate               */
    uint8_t target_y; /* target OAM Y coordinate               */
    uint16_t x;       /* current OAM X, 8.8 fixed              */
    uint16_t y;       /* current OAM Y, 8.8 fixed              */
} cursor_t;

/* Initialize cursor at (col, row), snap position, set up OAM sprites. */
void cursor_init(uint8_t col, uint8_t row);

/*
 * VBlank-owned directional input. Updates the logical target even while
 * the main loop is blocked publishing a large board animation.
 */
void cursor_vbl_handle_input(void);

/* Draw the edge coordinates prepared by the previous frame's tracking. */
void cursor_vbl_draw_edges(void);

/* Update the same-frame logical ghost in OAM and its tile in VRAM. */
void cursor_vbl_update_ghost(void);

/* WRAM-only easing tail; prepares edge coordinates for the next frame. */
void cursor_vbl_track(void);

#endif /* CURSOR_H */
