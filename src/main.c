#include <gb/gb.h>
#include <gb/hardware.h>
#include <stdint.h>
#include <string.h>

#include "../res/tiles.h"
#include "layout.h"

/* Blank tile: 16 zero bytes = all pixels at color index 0 (black). */
static const uint8_t blank_tile[16] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                       0xFF, 0xFF, 0xFF, 0xFF};

/* Return the board-surface tile for an empty intersection. */
static uint8_t surface_tile(uint8_t col, uint8_t row, uint8_t w, uint8_t h) {
    uint8_t top = (row == 0);
    uint8_t bottom = (row == h - 1);
    uint8_t left = (col == 0);
    uint8_t right = (col == w - 1);

    if (top) {
        if (left)
            return TILE_CORNER_TL;
        if (right)
            return TILE_CORNER_TR;
        return TILE_EDGE_T;
    }
    if (bottom) {
        if (left)
            return TILE_CORNER_BL;
        if (right)
            return TILE_CORNER_BR;
        return TILE_EDGE_B;
    }
    if (left)
        return TILE_EDGE_L;
    if (right)
        return TILE_EDGE_R;
    return TILE_CENTER;
}

/* Write BG tilemap entries from the current board state.
 * Iterates the board row by row, choosing stone tiles or the
 * appropriate board-surface tile for each intersection. */
static void board_draw(const board_t *b, uint8_t bkg_x, uint8_t bkg_y) {
    uint8_t w = b->width;
    uint8_t h = b->height;
    uint16_t pos = BOARD_COORD(0, 0);
    uint8_t row_buf[BOARD_MAX_SIZE];

    for (uint8_t row = 0; row < h; row++) {
        uint16_t p = pos;
        for (uint8_t col = 0; col < w; col++) {
            if (BF_GET(b->black_stones, p))
                row_buf[col] = TILE_STONE_B;
            else if (BF_GET(b->white_stones, p))
                row_buf[col] = TILE_STONE_W;
            else
                row_buf[col] = surface_tile(col, row, w, h);
            p++;
        }
        set_bkg_tiles(bkg_x, bkg_y + row, w, 1, row_buf);
        pos += BOARD_MAX_EXTENT;
    }
}

/* Fill the visible background with a single tile index. */
static void fill_bkg(uint8_t tile) {
    uint8_t row[SCREEN_W];
    memset(row, tile, sizeof(row));
    for (uint8_t y = 0; y < SCREEN_H; y++)
        set_bkg_tiles(0, y, SCREEN_W, 1, row);
}

void main(void) {
    DISPLAY_OFF;

    /*
     * Set LCDC bit 4: BG + Window read tile data from 0x8000 (unsigned),
     * sharing the same region as sprites.  All other LCDC bits start clear
     * (display off, layers off) — we turn them on at the end.
     */
    LCDC_REG = LCDCF_BG8000;

    /* DMG palettes: DMG_PAL(idx0, idx1, idx2, idx3)
     * Shades: 0=white, 1=light, 2=dark, 3=black.
     * Sprite index 0 is always transparent regardless of OBP value. */
    BGP_REG = DMG_PAL(0, 1, 2, 3);
    OBP0_REG = DMG_PAL(0, 0, 3, 2);

    /* Load tiles to 0x8000 (shared BG + Sprite region). */
    set_tile_data(0, 1, blank_tile, TILE_DATA_BASE);
    set_tile_data(TILE_OFFSET, tiles_TILE_COUNT, tiles_tiles, TILE_DATA_BASE);

    /* Fill entire visible background with the blank tile. */
    fill_bkg(TILE_BLANK);

    /* Enable SRAM and zero-init input state. */
    ENABLE_RAM;
    memset(game_input, 0, sizeof(input_t));

    /* Initialize and draw the board. */
    board_t *b = game_board;
    board_reset(b, 13, 9);

    /* A small opening for display purposes. */
    BF_SET(b->black_stones, BOARD_COORD(3, 3));
    BF_SET(b->black_stones, BOARD_COORD(3, 5));
    BF_SET(b->black_stones, BOARD_COORD(4, 4));
    BF_SET(b->black_stones, BOARD_COORD(9, 3));
    BF_SET(b->black_stones, BOARD_COORD(9, 5));

    BF_SET(b->white_stones, BOARD_COORD(6, 3));
    BF_SET(b->white_stones, BOARD_COORD(6, 4));
    BF_SET(b->white_stones, BOARD_COORD(6, 5));
    BF_SET(b->white_stones, BOARD_COORD(3, 4));
    BF_SET(b->white_stones, BOARD_COORD(10, 4));

#ifndef NDEBUG
    board_debug_print(b);
#endif
    board_draw(b, BOARD_BKG_X, BOARD_BKG_Y);

    /* Initialize the cursor at the center of the board. */
    cursor_init(game_cursor, b->width / 2, b->height / 2, BOARD_BKG_X,
                BOARD_BKG_Y);
    cursor_draw(game_cursor);

    SHOW_BKG;
    SHOW_SPRITES;
    DISPLAY_ON;

    while (1) {
        vsync();
        input_poll(game_input);
        cursor_update(game_cursor, game_input, b, BOARD_BKG_X, BOARD_BKG_Y);
        cursor_draw(game_cursor);
    }
}
