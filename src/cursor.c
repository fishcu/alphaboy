#include <gb/gb.h>
#include <gb/hardware.h>

#include "cursor.h"
#include "layout.h"

/* Compute target OAM X as fixed-point 8.8.
 * Board is drawn at BG tile (0,0) and centered via scroll registers.
 * OAM X = screen_offset + col*8 + 8 (OAM hardware offset). */
static uint16_t target_x(uint8_t col, uint8_t board_w) {
    uint8_t offset = (SCREEN_W - board_w) * 4;
    return (uint16_t)(offset + col * 8 + 8) << 8;
}

/* Compute target OAM Y as fixed-point 8.8.
 * OAM Y = screen_offset + row*8 + 16 (OAM hardware offset). */
static uint16_t target_y(uint8_t row, uint8_t board_h) {
    uint8_t offset = (SCREEN_H - board_h) * 4;
    return (uint16_t)(offset + row * 8 + 16) << 8;
}

/* Move `cur` toward `tgt` with exponential tracking.
 * Clamps to CURSOR_MIN_STEP to avoid slow crawl, but never overshoots. */
static uint16_t track(uint16_t cur, uint16_t tgt) {
    int16_t delta = (int16_t)(tgt - cur);
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
}

void cursor_update(cursor_t *c, const input_t *inp, const game_t *g) {
    uint8_t trigger = inp->pressed | inp->repeated;

    if ((trigger & J_LEFT) && c->col > 0)
        c->col--;
    if ((trigger & J_RIGHT) && c->col < g->width - 1)
        c->col++;
    if ((trigger & J_UP) && c->row > 0)
        c->row--;
    if ((trigger & J_DOWN) && c->row < g->height - 1)
        c->row++;

    /* Smooth tracking toward target pixel position. */
    uint16_t tx = target_x(c->col, g->width);
    uint16_t ty = target_y(c->row, g->height);
    c->x = track(c->x, tx);
    c->y = track(c->y, ty);

    /* Sprite spread: floor at 2 while d-pad held with room to move,
     * otherwise decay based on remaining distance to target. */
    uint8_t held = inp->current & (J_LEFT | J_RIGHT | J_UP | J_DOWN);
    if ((held & J_LEFT && c->col > 0) ||
        (held & J_RIGHT && c->col < g->width - 1) ||
        (held & J_UP && c->row > 0) ||
        (held & J_DOWN && c->row < g->height - 1)) {
        c->spread = 2;
    } else {
        int16_t dx = (int16_t)(tx - c->x);
        int16_t dy = (int16_t)(ty - c->y);
        uint16_t adx = dx < 0 ? (uint16_t)(-dx) : (uint16_t)dx;
        uint16_t ady = dy < 0 ? (uint16_t)(-dy) : (uint16_t)dy;
        uint16_t dist = adx > ady ? adx : ady;
        uint8_t s = dist >> CURSOR_SPREAD_SHIFT;
        c->spread = s > 2 ? 2 : s;
    }
}

void cursor_draw(const cursor_t *c) {
    uint8_t px = (c->x + 128) >> 8;
    uint8_t py = (c->y + 128) >> 8;
    uint8_t s = c->spread;

    move_sprite(CURSOR_SPR_UL, px - s, py - s);
    move_sprite(CURSOR_SPR_UR, px + 1 + s, py - s);
    move_sprite(CURSOR_SPR_LL, px - s, py + 1 + s);
    move_sprite(CURSOR_SPR_LR, px + 1 + s, py + 1 + s);
}
