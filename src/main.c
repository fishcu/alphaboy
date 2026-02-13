#include <gb/gb.h>
#include <gb/hardware.h>
#include <stdint.h>
#include <string.h>

#include "../res/tiles.h"

/*
 * VRAM layout (LCDC bit 4 = 1, unsigned addressing):
 *
 *   0x8000-0x8FFF  Shared BG + Sprite tiles  (4 KB, 256 tiles)
 *   0x9000-0x97FF  Free                      (2 KB)
 *   0x9800-0x9BFF  BG Map                    (1 KB, hardware-fixed)
 *   0x9C00-0x9FFF  Window Map                (1 KB, hardware-fixed)
 *
 * Tile allocation at 0x8000:
 *   0        = blank (solid color-index-0, i.e. black)
 *   1 .. 12  = sprite sheet tiles (loaded from png2asset data)
 *   13..255  = free
 */

#define TILE_DATA_BASE  0x80

#define TILE_OFFSET  1

/* Tile indices (sprite-sheet position + offset). */
#define TILE_BLANK      0
#define TILE_CURSOR     (0  + TILE_OFFSET)
#define TILE_STONE_W    (1  + TILE_OFFSET)
#define TILE_STONE_B    (2  + TILE_OFFSET)
#define TILE_CORNER_TL  (3  + TILE_OFFSET)
#define TILE_EDGE_T     (4  + TILE_OFFSET)
#define TILE_CORNER_TR  (5  + TILE_OFFSET)
#define TILE_EDGE_L     (6  + TILE_OFFSET)
#define TILE_CENTER     (7  + TILE_OFFSET)
#define TILE_EDGE_R     (8  + TILE_OFFSET)
#define TILE_CORNER_BL  (9  + TILE_OFFSET)
#define TILE_EDGE_B     (10 + TILE_OFFSET)
#define TILE_CORNER_BR  (11 + TILE_OFFSET)

#define BOARD_SIZE  9

/* Board position on the background map (in tiles, from top-left). */
#define BOARD_X  1
#define BOARD_Y  1

/* Visible screen size in tiles. */
#define SCREEN_W  20
#define SCREEN_H  18

/* Blank tile: 16 zero bytes = all pixels at color index 0 (black). */
static const uint8_t blank_tile[16] = {0};

/* 9x9 Go board built from the board surface tiles. */
static const uint8_t board_map[BOARD_SIZE * BOARD_SIZE] = {
    TILE_CORNER_TL, TILE_EDGE_T, TILE_EDGE_T, TILE_EDGE_T, TILE_EDGE_T, TILE_EDGE_T, TILE_EDGE_T, TILE_EDGE_T, TILE_CORNER_TR,
    TILE_EDGE_L,  TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_EDGE_R,
    TILE_EDGE_L,  TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_EDGE_R,
    TILE_EDGE_L,  TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_EDGE_R,
    TILE_EDGE_L,  TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_EDGE_R,
    TILE_EDGE_L,  TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_EDGE_R,
    TILE_EDGE_L,  TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_EDGE_R,
    TILE_EDGE_L,  TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_CENTER, TILE_EDGE_R,
    TILE_CORNER_BL, TILE_EDGE_B, TILE_EDGE_B, TILE_EDGE_B, TILE_EDGE_B, TILE_EDGE_B, TILE_EDGE_B, TILE_EDGE_B, TILE_CORNER_BR,
};

/* Fill the visible background with a single tile index. */
static void fill_bkg(uint8_t tile) {
    uint8_t row[SCREEN_W];
    memset(row, tile, sizeof(row));
    for (uint8_t y = 0; y < SCREEN_H; y++) {
        set_bkg_tiles(0, y, SCREEN_W, 1, row);
    }
}

void main(void) {
    DISPLAY_OFF;

    /*
     * Set LCDC bit 4: BG + Window read tile data from 0x8000 (unsigned),
     * sharing the same region as sprites.  All other LCDC bits start clear
     * (display off, layers off) — we turn them on at the end.
     */
    LCDC_REG = LCDCF_BG8000;

    /*
     * DMG palette: map color indices to hardware shades.
     *   Index 0 -> black  (shade 3)
     *   Index 1 -> dark   (shade 2)
     *   Index 2 -> light  (shade 1)
     *   Index 3 -> white  (shade 0)
     */
    BGP_REG = 0x1Bu;

    /* Load tiles to 0x8000 (shared BG + Sprite region). */
    set_tile_data(0, 1, blank_tile, TILE_DATA_BASE);
    set_tile_data(TILE_OFFSET, tiles_TILE_COUNT, tiles_tiles, TILE_DATA_BASE);

    /* Fill entire visible background with the blank tile. */
    fill_bkg(TILE_BLANK);

    /* Draw the 9x9 board. */
    set_bkg_tiles(BOARD_X, BOARD_Y, BOARD_SIZE, BOARD_SIZE, board_map);

    SHOW_BKG;
    DISPLAY_ON;

    while (1) {
        vsync();
    }
}
