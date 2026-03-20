#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Tile data                                                         */
/* ------------------------------------------------------------------ */

/* Base address value for set_tile_data() to target 0x8000. */
#define TILE_DATA_BASE 0x80

/* Tile indices  --  sequential order matches png2asset output.
 * The PNG is scanned left-to-right, top-to-bottom; duplicate (empty)
 * tiles are deduplicated so only TILE_EMPTY occupies index 0. */
enum {
    TILE_EMPTY = 0,

    /* Board frame (11 tiles). */
    TILE_FRAME_TL,
    TILE_FRAME_T,
    TILE_FRAME_TR,
    TILE_FRAME_L,
    TILE_FRAME_R,
    TILE_FRAME_BL_U,
    TILE_FRAME_B_U,
    TILE_FRAME_BR_U,
    TILE_FRAME_BL_D,
    TILE_FRAME_B_D,
    TILE_FRAME_BR_D,

    /* Board surface intersections (9 tiles). */
    TILE_CORNER_TL,
    TILE_EDGE_T,
    TILE_CORNER_TR,
    TILE_EDGE_L,
    TILE_CENTER,
    TILE_EDGE_R,
    TILE_CORNER_BL,
    TILE_EDGE_B,
    TILE_CORNER_BR,

    /* Star point. */
    TILE_HOSHI,

    /* Stones. */
    TILE_STONE_B,
    TILE_STONE_W,

    /* Last-played stone markers. */
    TILE_LAST_B,
    TILE_LAST_W,

    /* Ko-point surface intersections (9 tiles). */
    TILE_KO_TL,
    TILE_KO_T,
    TILE_KO_TR,
    TILE_KO_L,
    TILE_KO_C,
    TILE_KO_R,
    TILE_KO_BL,
    TILE_KO_B,
    TILE_KO_BR,

    /* Black territory markers (8 unique tiles + 1 deduplicated). */
    TILE_TERR_B_TL,
    TILE_TERR_B_T,
    TILE_TERR_B_TR,
    TILE_TERR_B_L,
    /* TILE_TERR_B_C is identical to TILE_HOSHI; defined as alias below. */
    TILE_TERR_B_R,
    TILE_TERR_B_BL,
    TILE_TERR_B_B,
    TILE_TERR_B_BR,

    /* White territory markers (9 tiles). */
    TILE_TERR_W_TL,
    TILE_TERR_W_T,
    TILE_TERR_W_TR,
    TILE_TERR_W_L,
    TILE_TERR_W_C,
    TILE_TERR_W_R,
    TILE_TERR_W_BL,
    TILE_TERR_W_B,
    TILE_TERR_W_BR,

    /* Cursor sprites. */
    TILE_CURSOR,
    TILE_CURSOR2,

    /* Ghost stone sprites (3-color + transparency). */
    TILE_SPR_STONE_B,
    TILE_SPR_STONE_W,

    TILE_COUNT
};

/* Deduplicated: TILE_TERR_B_C has identical tile data to TILE_HOSHI. */
#define TILE_TERR_B_C TILE_HOSHI

/* ------------------------------------------------------------------ */
/*  VRAM addressing                                                   */
/* ------------------------------------------------------------------ */

/* Construct a BG tile-map offset from raw tile-map (x, y) positions.
 * The BG map is 32 tiles wide (shift 5), matching COORD_SHIFT in go.h
 * by design so board coordinates double as tile-map offsets. */
#define VRAM_XY(x, y) ((uint16_t)((y) << 5) | (x))

/* ------------------------------------------------------------------ */
/*  Screen / board positioning                                        */
/* ------------------------------------------------------------------ */

#define SCREEN_W 20 /* screen width in tiles  (160 px) */
#define SCREEN_H 18 /* screen height in tiles (144 px) */

/* Cell dimensions as displayed on screen.
 * Vertical HBlank compression skips the top pixel row of each tile,
 * giving 7 visible rows per cell.  Horizontal is uncompressed. */
#define CELL_W 8
#define CELL_H 7

/* Extra upward pixel shift so the board + frame fits on screen. */
#define SCROLL_ADJUST_Y 2

/* ------------------------------------------------------------------ */
/*  OAM sprite allocation                                             */
/* ------------------------------------------------------------------ */

#define CURSOR_SPR_UL 0
#define CURSOR_SPR_UR 1
#define CURSOR_SPR_LL 2
#define CURSOR_SPR_LR 3
#define GHOST_SPR 4

/* ------------------------------------------------------------------ */
/*  Display lifecycle                                                 */
/* ------------------------------------------------------------------ */

/* Configure LCDC addressing mode, load palettes and tile data,
 * and fill the BG map.  Assumes the display is already off and
 * SRAM is enabled.  Call before board_redraw and interrupts_init. */
void display_init(void);

/* Turn on BG, sprites, and the display.
 * Call once after all game and interrupt initialization is complete. */
void display_start(void);

#endif /* DISPLAY_H */
