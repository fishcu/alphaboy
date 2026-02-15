#include <gb/gb.h>
#include <gb/hardware.h>

#include "cursor.h"
#include "layout.h"

void cursor_init(cursor_t *c, uint8_t col, uint8_t row) {
    c->col = col;
    c->row = row;

    /* Assign the cursor tile to all 4 corner sprites. */
    set_sprite_tile(CURSOR_SPR_UL, TILE_CURSOR);
    set_sprite_tile(CURSOR_SPR_UR, TILE_CURSOR);
    set_sprite_tile(CURSOR_SPR_LL, TILE_CURSOR);
    set_sprite_tile(CURSOR_SPR_LR, TILE_CURSOR);

    /* Set flip attributes for each corner. */
    set_sprite_prop(CURSOR_SPR_UL, 0);
    set_sprite_prop(CURSOR_SPR_UR, S_FLIPX);
    set_sprite_prop(CURSOR_SPR_LL, S_FLIPY);
    set_sprite_prop(CURSOR_SPR_LR, S_FLIPX | S_FLIPY);
}

void cursor_update(cursor_t *c, const input_t *inp, const board_t *b) {
    uint8_t pressed = inp->pressed;

    if ((pressed & J_LEFT)  && c->col > 0)
        c->col--;
    if ((pressed & J_RIGHT) && c->col < b->width - 1)
        c->col++;
    if ((pressed & J_UP)    && c->row > 0)
        c->row--;
    if ((pressed & J_DOWN)  && c->row < b->height - 1)
        c->row++;
}

void cursor_draw(const cursor_t *c, uint8_t bkg_x, uint8_t bkg_y) {
    /* Screen pixel position of the board cell.
     * OAM X offset = 8, OAM Y offset = 16. */
    uint8_t px = (bkg_x + c->col) * 8 + 8;
    uint8_t py = (bkg_y + c->row) * 8 + 16;

    move_sprite(CURSOR_SPR_UL, px,     py);
    move_sprite(CURSOR_SPR_UR, px + 1, py);
    move_sprite(CURSOR_SPR_LL, px,     py + 1);
    move_sprite(CURSOR_SPR_LR, px + 1, py + 1);
}
