#include <gb/gb.h>
#include <gb/hardware.h>

#include "cursor.h"
#include "display.h"
#include "memory.h"

#define FIXED_FRACTION(value) (((uint8_t *)&(value))[0])
#define FIXED_PIXEL(value) (((uint8_t *)&(value))[1])
#define HARDWARE_OAM ((volatile OAM_item_t *)0xFE00u)

_Static_assert(CURSOR_SPR_UL == 0 && CURSOR_SPR_UR == 1 && CURSOR_SPR_LL == 2 &&
                   CURSOR_SPR_LR == 3 && GHOST_SPR == 4,
               "direct OAM layout requires cursor sprites 0 through 4");

/* Compute target OAM X.
 * Board is drawn at BG tile (0,0) and centered via scroll registers.
 * OAM X = screen_offset + col*CELL_W + 8 (OAM hardware offset). */
inline uint8_t target_x(uint8_t col, uint8_t board_w) {
    const uint8_t offset = (SCREEN_W * 8 - board_w * CELL_W) / 2;
    return offset + col * CELL_W + 8;
}

/* Compute target OAM Y.
 * Vertical compression: each cell is CELL_H pixels on screen.
 * OAM Y = screen_offset + row*CELL_H + 16 (OAM hardware offset). */
inline uint8_t target_y(uint8_t row, uint8_t board_h) {
    const uint8_t offset =
        (SCREEN_H * 8 - board_h * CELL_H) / 2 - SCROLL_ADJUST_Y;
    return offset + row * CELL_H + 15;
}

/* Move `cur` toward `tgt` with exponential tracking.
 * Clamps to CURSOR_MIN_STEP to avoid slow crawl, but never overshoots. */
// clang-format off
static uint8_t track(uint16_t *cur, uint8_t tgt) __naked {
    cur;
    tgt;
    __asm
        ; ABI: DE = &cur, A = target pixel.
        ; C = direction, HL = delta then step.
        push de
        ld   h, a
        ld   a, (de)
        ld   b, a
        inc  de
        ld   a, (de)
        ld   c, a

        cp   h
        jr   C, 01001$
        jr   NZ, 01002$
        ld   a, b
        or   a
        jr   Z, 01009$

    01002$:
        ; Decreasing: delta = current - target.
        ld   l, b
        ld   a, c
        sub  h
        ld   h, a
        ld   c, #0
        jr   01003$

    01001$:
        ; Increasing: delta = target - current.
        xor  a
        sub  b
        ld   l, a
        ld   a, h
        sbc  a, c
        ld   h, a
        ld   c, #1

    01003$:
        ; A delta of at most 8 subpixels reaches the target directly.
        ld   a, h
        or   a
        jr   NZ, 01014$
        ld   a, l
        cp   #(CURSOR_MIN_STEP + 1)
        jr   NC, 01014$
        ld   b, #0
        jr   01011$

    01014$:
        ; Preserve the negative remainder, then compute delta / 8 in HL.
        ld   a, c
        or   a
        jr   NZ, 01015$
        ld   b, l
    01015$:
        srl  h
        rr   l
        srl  h
        rr   l
        srl  h
        rr   l

        ; DE = delta / 8; HL = delta / 16, then add both.
        ld   d, h
        ld   e, l
        srl  h
        rr   l
        add  hl, de
        ld   a, c
        or   a
        jr   NZ, 01006$

        ; Negative arithmetic shifts round each non-exact division up.
        ld   a, b
        and  #0x07
        jr   Z, 01004$
        inc  l
        jr   NZ, 01004$
        inc  h
    01004$:
        ld   a, b
        and  #0x0f
        jr   Z, 01005$
        inc  l
        jr   NZ, 01005$
        inc  h
    01005$:
    01006$:
        ld   b, #1

        ; Clamp to the minimum subpixel step.
        ld   a, h
        or   a
        jr   NZ, 01008$
        ld   a, l
        cp   #CURSOR_MIN_STEP
        jr   NC, 01008$
        ld   l, #CURSOR_MIN_STEP

    01008$:
    01011$:
        pop  de
        ld   a, c
        or   a
        jr   Z, 01012$

        ; Increasing: current += step.
        ld   a, (de)
        add  a, l
        ld   (de), a
        inc  de
        ld   a, (de)
        adc  a, h
        ld   (de), a
        ld   a, b
        ret

    01012$:
        ; Decreasing: current -= step.
        ld   a, (de)
        sub  l
        ld   (de), a
        inc  de
        ld   a, (de)
        sbc  a, h
        ld   (de), a
        ld   a, b
        ret

    01009$:
        pop  de
        xor  a
        ret
    __endasm;
}

static void draw_cursor(uint8_t px, uint16_t py_spread) __naked {
    px;
    py_spread;
    __asm
        ; ABI: A = px, D = spread, E = py.
        ; Result coordinates: B = left, C = right, E = top, D = bottom.
        sub  #2
        sub  d
        ld   b, a
        ld   a, d
        add  a, a
        add  a, #3
        add  a, b
        ld   c, a
        ld   a, e
        dec  a
        sub  d
        ld   e, a
        ld   a, d
        add  a, a
        add  a, #3
        add  a, e
        ld   d, a

        ld   hl, #0xfe00
        ld   (hl), e
        inc  hl
        ld   (hl), b
        ld   hl, #0xfe04
        ld   (hl), e
        inc  hl
        ld   (hl), c
        ld   hl, #0xfe08
        ld   (hl), d
        inc  hl
        ld   (hl), b
        ld   hl, #0xfe0c
        ld   (hl), d
        inc  hl
        ld   (hl), c
        ret
    __endasm;
}
// clang-format on

void cursor_init(cursor_t *c, uint8_t col, uint8_t row, const game_t *g) {
    c->col = col;
    c->row = row;
    c->board_w = g->width;
    c->board_h = g->height;
    c->ghost_tile = 0;
    c->target_x = target_x(col, c->board_w);
    c->target_y = target_y(row, c->board_h);
    FIXED_FRACTION(c->x) = 0;
    FIXED_PIXEL(c->x) = c->target_x;
    FIXED_FRACTION(c->y) = 0;
    FIXED_PIXEL(c->y) = c->target_y;

    const uint8_t px = FIXED_PIXEL(c->x);
    const uint8_t py = FIXED_PIXEL(c->y);

    cursor_refresh_ghost(c, g);

    /* The display is off during initialization, so hardware OAM is writable. */
    draw_cursor(px, py);
    HARDWARE_OAM[CURSOR_SPR_UL].tile = TILE_CURSOR;
    HARDWARE_OAM[CURSOR_SPR_UL].prop = 0;
    HARDWARE_OAM[CURSOR_SPR_UR].tile = TILE_CURSOR;
    HARDWARE_OAM[CURSOR_SPR_UR].prop = S_FLIPX;
    HARDWARE_OAM[CURSOR_SPR_LL].tile = TILE_CURSOR;
    HARDWARE_OAM[CURSOR_SPR_LL].prop = S_FLIPY;
    HARDWARE_OAM[CURSOR_SPR_LR].tile = TILE_CURSOR;
    HARDWARE_OAM[CURSOR_SPR_LR].prop = S_FLIPX | S_FLIPY;
    HARDWARE_OAM[GHOST_SPR].x = px;
    HARDWARE_OAM[GHOST_SPR].tile = c->ghost_tile;
    HARDWARE_OAM[GHOST_SPR].prop = S_PALETTE;
    HARDWARE_OAM[GHOST_SPR].y = py + 1;
}

void cursor_vbl_handle_input(void) {
    const uint8_t trigger = game_input->pressed | game_input->repeated;

    if ((trigger & J_LEFT) && game_cursor->col > 0) {
        game_cursor->col--;
        game_cursor->target_x -= CELL_W;
        game_cursor->ghost_tile = 0;
    }
    if ((trigger & J_RIGHT) &&
        game_cursor->col < (uint8_t)(game_cursor->board_w - 1u)) {
        game_cursor->col++;
        game_cursor->target_x += CELL_W;
        game_cursor->ghost_tile = 0;
    }
    if ((trigger & J_UP) && game_cursor->row > 0) {
        game_cursor->row--;
        game_cursor->target_y -= CELL_H;
        game_cursor->ghost_tile = 0;
    }
    if ((trigger & J_DOWN) &&
        game_cursor->row < (uint8_t)(game_cursor->board_h - 1u)) {
        game_cursor->row++;
        game_cursor->target_y += CELL_H;
        game_cursor->ghost_tile = 0;
    }
}

void cursor_vbl_update_oam(void) {
    uint8_t spread;
    uint8_t px;
    uint8_t py;

    /* Test each axis once and track only axes that are still moving. */
    if (FIXED_PIXEL(game_cursor->x) != game_cursor->target_x ||
        FIXED_FRACTION(game_cursor->x) != 0) {
        spread = track(&game_cursor->x, game_cursor->target_x);
        if (FIXED_PIXEL(game_cursor->y) != game_cursor->target_y ||
            FIXED_FRACTION(game_cursor->y) != 0)
            spread |= track(&game_cursor->y, game_cursor->target_y);
    } else if (FIXED_PIXEL(game_cursor->y) != game_cursor->target_y ||
               FIXED_FRACTION(game_cursor->y) != 0) {
        spread = track(&game_cursor->y, game_cursor->target_y);
    } else {
        goto update_ghost;
    }

    px = FIXED_PIXEL(game_cursor->x);
    if (FIXED_FRACTION(game_cursor->x) & 0x80u)
        px++;
    py = FIXED_PIXEL(game_cursor->y);
    if (FIXED_FRACTION(game_cursor->y) & 0x80u)
        py++;
    draw_cursor(px, ((uint16_t)spread << 8) | py);

update_ghost:
    /*
     * The logical board may be ahead of a capture animation.  Keep the
     * ghost hidden until queued board visuals catch up.
     */
    if (game_cursor->ghost_tile != 0 && !game_action_busy &&
        board_animation_head == board_animation_committed) {
        HARDWARE_OAM[GHOST_SPR].x = game_cursor->target_x;
        HARDWARE_OAM[GHOST_SPR].tile = game_cursor->ghost_tile;
        HARDWARE_OAM[GHOST_SPR].y = game_cursor->target_y + 1;
    } else {
        HARDWARE_OAM[GHOST_SPR].y = 0;
    }
}

void cursor_refresh_ghost(cursor_t *c, const game_t *g) {
    const uint8_t col = c->col;
    const uint8_t row = c->row;

    /*
     * VBlank may move the cursor after these coordinates are sampled.
     * At worst, one frame uses stale ghost visibility; the next main-loop
     * pass recalculates it for the current position.
     */
    if (game_can_play_approx(g, col, row)) {
        const uint8_t black = (game_color_to_play(g) == COLOR_BLACK);
        OBP1_REG = black ? DMG_PALETTE(0, 1, 2, 3) : DMG_PALETTE(0, 0, 1, 2);
        c->ghost_tile = black ? TILE_SPR_STONE_B : TILE_SPR_STONE_W;
    } else {
        c->ghost_tile = 0;
    }
}
