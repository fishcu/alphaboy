#include <gb/gb.h>
#include <stdint.h>
#include <string.h>

#include "../res/tiles.h"

/*
 * VRAM tile layout:
 *   0        = blank (solid color-index-0, i.e. black)
 *   1 .. 12  = sprite sheet tiles (loaded from png2asset data)
 *
 * We load the sprite sheet at offset 1 so tile 0 stays blank.
 * The background tilemap defaults to 0 after we fill it, giving
 * a clean black surround.
 */

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
    /*
     * DMG palette: map color indices to hardware shades.
     *   Index 0 -> black  (shade 3)
     *   Index 1 -> dark   (shade 2)
     *   Index 2 -> light  (shade 1)
     *   Index 3 -> white  (shade 0)
     */
    BGP_REG = 0x1Bu;

    /* Load blank tile at VRAM position 0. */
    set_bkg_data(0, 1, blank_tile);

    /* Load sprite sheet tiles at VRAM positions 1..12. */
    set_bkg_data(TILE_OFFSET, tiles_TILE_COUNT, tiles_tiles);

    /* Fill entire visible background with the blank tile. */
    fill_bkg(TILE_BLANK);

    /* Draw the 9x9 board. */
    set_bkg_tiles(BOARD_X, BOARD_Y, BOARD_SIZE, BOARD_SIZE, board_map);

    SHOW_BKG;

    while (1) {
        vsync();
    }
}
