#include <gb/gb.h>
#include <gb/hardware.h>

#include "cursor.h"
#include "cursor_easing.h"
#include "display.h"
#include "memory.h"

#define FIXED_FRACTION(value) (((uint8_t *)&(value))[0])
#define FIXED_PIXEL(value) (((uint8_t *)&(value))[1])
#define HARDWARE_OAM ((volatile OAM_item_t *)0xFE00u)
#define DIRECTION_BUTTON_MASK (J_LEFT | J_RIGHT | J_UP | J_DOWN)

_Static_assert(CURSOR_SPR_UL == 0 && CURSOR_SPR_UR == 1 && CURSOR_SPR_LL == 2 &&
                   CURSOR_SPR_LR == 3 && GHOST_SPR == 4,
               "direct OAM layout requires cursor sprites 0 through 4");
_Static_assert(CURSOR_EASING_MAX_INDEX >= SCREEN_W * 16u,
               "cursor easing LUT must span the full screen width");
_Static_assert(CURSOR_EASING_MIN_STEP == CURSOR_MIN_STEP,
               "cursor easing LUT minimum step mismatch");
_Static_assert(COLOR_BLACK == 0u && COLOR_WHITE == 1u,
               "ghost tile arithmetic requires consecutive colors");
_Static_assert(TILE_SPR_STONE_W == TILE_SPR_STONE_B + 1u,
               "ghost stone sprite tiles must be consecutive");

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

/* Move `cur` toward `tgt` using the generated 3/16 easing curve.
 * Distance is rounded to the nearest half pixel for the ROM lookup. */
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
        ; Quantize the fractional byte through its dedicated ROM page.
        ld   d, h
        ld   h, #>_cursor_easing_fraction
        ld   b, (hl)

        ; Double the pixel byte.  Carry selects indices 256 through 320.
        ld   a, d
        add  a
        jr   C, 01018$
        add  b
        jr   C, 01019$

        ; Normal page: low and high bytes occupy consecutive ROM pages.
        ld   l, a
        ld   h, #>_cursor_easing_low
        ld   e, (hl)
        inc  h
        ld   d, (hl)
        jr   01020$

    01018$:
        add  b
    01019$:
        ; Extension page: high bytes begin at offset 128.
        ld   l, a
        ld   h, #>_cursor_easing_extension
        ld   e, (hl)
        set  7, l
        ld   d, (hl)

    01020$:
        ld   h, d
        ld   l, e
        ld   b, #1

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

inline uint8_t ghost_can_play(void) {
    const uint16_t coord = game_cursor->coord;

    if (game_state->board[coord] != COLOR_EMPTY)
        return 0;
    return coord != game_state->ko;
}

static void update_ghost_oam(void) {
    if (ghost_can_play()) {
        const uint8_t color = game_color_to_play(game_state);
        OBP1_REG = (color == COLOR_BLACK) ? DMG_PALETTE(0, 1, 2, 3)
                                          : DMG_PALETTE(0, 0, 1, 2);
        HARDWARE_OAM[GHOST_SPR].x = game_cursor->target_x;
        HARDWARE_OAM[GHOST_SPR].tile = TILE_SPR_STONE_B + color;
        HARDWARE_OAM[GHOST_SPR].y = game_cursor->target_y + 1u;
    } else {
        HARDWARE_OAM[GHOST_SPR].y = 0;
    }
}

void cursor_init(uint8_t col, uint8_t row) {
    cursor_t *const c = game_cursor;
    const game_t *const g = game_state;

    c->col = col;
    c->row = row;
    c->coord = board_coord(col, row);
    c->target_x = target_x(col, g->width);
    c->target_y = target_y(row, g->height);
    FIXED_FRACTION(c->x) = 0;
    FIXED_PIXEL(c->x) = c->target_x;
    FIXED_FRACTION(c->y) = 0;
    FIXED_PIXEL(c->y) = c->target_y;

    const uint8_t px = FIXED_PIXEL(c->x);
    const uint8_t py = FIXED_PIXEL(c->y);

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
    HARDWARE_OAM[GHOST_SPR].prop = S_PALETTE;
    update_ghost_oam();
}

void cursor_vbl_handle_input(void) {
    const uint8_t trigger = game_input->pressed | game_input->repeated;

    if ((trigger & DIRECTION_BUTTON_MASK) == 0)
        return;

    if ((trigger & J_LEFT) && game_cursor->col > 0) {
        game_cursor->col--;
        game_cursor->target_x -= CELL_W;
    }
    if ((trigger & J_RIGHT) &&
        game_cursor->col < (uint8_t)(game_state->width - 1u)) {
        game_cursor->col++;
        game_cursor->target_x += CELL_W;
    }
    if ((trigger & J_UP) && game_cursor->row > 0) {
        game_cursor->row--;
        game_cursor->target_y -= CELL_H;
    }
    if ((trigger & J_DOWN) &&
        game_cursor->row < (uint8_t)(game_state->height - 1u)) {
        game_cursor->row++;
        game_cursor->target_y += CELL_H;
    }

    game_cursor->coord = board_coord(game_cursor->col, game_cursor->row);
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
    if (!game_action_busy)
        update_ghost_oam();
    else
        HARDWARE_OAM[GHOST_SPR].y = 0;
}
