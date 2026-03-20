#include <gb/gb.h>
#include <gb/hardware.h>

#include "cursor.h"
#include "display.h"

/* Compute target OAM X as fixed-point 8.8.
 * Board is drawn at BG tile (0,0) and centered via scroll registers.
 * OAM X = screen_offset + col*CELL_W + 8 (OAM hardware offset). */
static uint16_t target_x(uint8_t col, uint8_t board_w) {
    const uint8_t offset = (SCREEN_W * 8 - board_w * CELL_W) / 2;
    return (uint16_t)(offset + col * CELL_W + 8) << 8;
}

/* Compute target OAM Y as fixed-point 8.8.
 * Vertical compression: each cell is CELL_H pixels on screen.
 * OAM Y = screen_offset + row*CELL_H + 16 (OAM hardware offset). */
static uint16_t target_y(uint8_t row, uint8_t board_h) {
    const uint8_t offset =
        (SCREEN_H * 8 - board_h * CELL_H) / 2 - SCROLL_ADJUST_Y;
    return (uint16_t)(offset + row * CELL_H + 15) << 8;
}

/* Move `cur` toward `tgt` with exponential tracking.
 * Clamps to CURSOR_MIN_STEP to avoid slow crawl, but never overshoots. */
static uint16_t track(uint16_t cur, uint16_t tgt) {
    const int16_t delta = (int16_t)(tgt - cur);
    if (delta == 0)
        return cur;

    int16_t step = (delta >> 3) + (delta >> 4); /* 3/16 per frame */

    /* Clamp: at least MIN_STEP toward target, but never overshoot. */
    if (delta > 0) {
        if (step < CURSOR_MIN_STEP)
            step = CURSOR_MIN_STEP;
        if (step > delta)
            step = delta;
    } else {
        if (step > -CURSOR_MIN_STEP)
            step = -CURSOR_MIN_STEP;
        if (step < delta)
            step = delta;
    }
    return cur + step;
}

void cursor_init(cursor_t *c, uint8_t col, uint8_t row, const game_t *g) {
    c->col = col;
    c->row = row;
    c->spread = 0;
    c->x = target_x(col, g->width);
    c->y = target_y(row, g->height);

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

    const uint8_t px = (c->x + 128) >> 8;
    const uint8_t py = (c->y + 128) >> 8;
    move_sprite(CURSOR_SPR_UL, px - 2, py - 1);
    move_sprite(CURSOR_SPR_UR, px + 1, py - 1);
    move_sprite(CURSOR_SPR_LL, px - 2, py + 2);
    move_sprite(CURSOR_SPR_LR, px + 1, py + 2);
    move_sprite(GHOST_SPR, px, py + 1);
}

void cursor_update(cursor_t *c, const input_t *inp, const game_t *g) {
    const uint8_t trigger = inp->pressed | inp->repeated;

    if ((trigger & J_LEFT) && c->col > 0)
        c->col--;
    if ((trigger & J_RIGHT) && c->col < g->width - 1)
        c->col++;
    if ((trigger & J_UP) && c->row > 0)
        c->row--;
    if ((trigger & J_DOWN) && c->row < g->height - 1)
        c->row++;

    const uint16_t tx = target_x(c->col, g->width);
    const uint16_t ty = target_y(c->row, g->height);

    /* Ghost stone sprite: show the next stone to play, or hide.
     * The ghost always uses OBP1 (S_PALETTE); we swap the palette
     * register to match the stone color since only one plays at a time. */
    if (game_can_play_approx(g, c->col, c->row)) {
        const uint8_t black = (game_color_to_play(g) == COLOR_BLACK);
        OBP1_REG = black ? DMG_PALETTE(0, 1, 2, 3) : DMG_PALETTE(0, 0, 1, 2);
        set_sprite_tile(GHOST_SPR, black ? TILE_SPR_STONE_B : TILE_SPR_STONE_W);
        set_sprite_prop(GHOST_SPR, S_PALETTE);
        move_sprite(GHOST_SPR, (tx + 128) >> 8, ((ty + 128) >> 8) + 1);
    } else {
        move_sprite(GHOST_SPR, 0, 0);
    }

    /* Smooth tracking toward target pixel position. */
    c->x = track(c->x, tx);
    c->y = track(c->y, ty);

    const uint8_t px = (c->x + 128) >> 8;
    const uint8_t py = (c->y + 128) >> 8;
    const uint8_t s = (c->x != tx || c->y != ty);
    c->spread = s;

    move_sprite(CURSOR_SPR_UL, px - 2 - s, py - 1 - s);
    move_sprite(CURSOR_SPR_UR, px + 1 + s, py - 1 - s);
    move_sprite(CURSOR_SPR_LL, px - 2 - s, py + 2 + s);
    move_sprite(CURSOR_SPR_LR, px + 1 + s, py + 2 + s);
}
