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
static void board_draw(const game_t *g) {
    uint8_t w = g->width;
    uint8_t h = g->height;
    uint16_t pos = BOARD_COORD(0, 0);
    uint8_t row_buf[BOARD_MAX_SIZE];

    for (uint8_t row = 0; row < h; row++) {
        uint16_t p = pos;
        for (uint8_t col = 0; col < w; col++) {
            if (BF_GET(g->black_stones, p))
                row_buf[col] = TILE_STONE_B;
            else if (BF_GET(g->white_stones, p))
                row_buf[col] = TILE_STONE_W;
            else
                row_buf[col] = surface_tile(col, row, w, h);
            p++;
        }
        set_bkg_tiles(0, row, w, 1, row_buf);
        pos += BOARD_MAX_EXTENT;
    }
}

/* Fill the entire 32x32 BG tilemap with a single tile index.
 * Must cover the full map since BG scrolling wraps at 256x256. */
static void fill_bkg(uint8_t tile) {
    uint8_t row[32];
    memset(row, tile, sizeof(row));
    for (uint8_t y = 0; y < 32; y++)
        set_bkg_tiles(0, y, 32, 1, row);
}

/* ---- HBlank vertical compression ----
 * Each tile is 8x8 in VRAM but we display only 7 rows per tile by
 * bumping SCY once per tile row via LYC-chained STAT interrupts.
 * The ISR waits for HBlank before writing so the change never
 * tears the scanline.  nowait_int_handler is added to the chain
 * to skip the dispatcher's default WAIT_STAT on exit. */

static uint8_t base_scy;
static uint8_t first_lyc;

static void lcd_isr(void) {
    while (STAT_REG & STATF_BUSY) {
    }
    SCY_REG++;
    LYC_REG += CELL_H;
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
    game_t *g = game_state;
    game_reset(g, 19, 19, 13);

#ifndef NDEBUG
    game_debug_print(g);
#endif
    board_draw(g);

    /* Center the board on screen via BG scroll registers.
     * Board is drawn at BG tile (0,0); the 256x256 BG wraps around,
     * so the negative offset shows blank (black) tiles as margin.
     * The first LYC fires 1 scanline before the board to skip each
     * tile's duplicate row 0 before it ever appears on screen. */
    uint8_t offset_x = (SCREEN_W * 8 - g->width * CELL_W) / 2;
    uint8_t offset_y = (SCREEN_H * 8 - g->height * CELL_H) / 2;

    SCX_REG = (uint8_t)(-(int16_t)offset_x);
    base_scy = (uint8_t)(-(int16_t)offset_y);
    first_lyc = offset_y - 1;
    SCY_REG = base_scy;

    /* Install the LYC-chained ISR via the GBDK dispatcher.
     * nowait_int_handler skips the dispatcher's WAIT_STAT on exit,
     * saving ~15 cycles per invocation. */
    LYC_REG = first_lyc;
    CRITICAL {
        STAT_REG |= STATF_LYC;
        add_LCD(lcd_isr);
        add_LCD(nowait_int_handler);
    }
    set_interrupts(VBL_IFLAG | LCD_IFLAG);

    /* Initialize the cursor at the center of the board. */
    cursor_init(game_cursor, g->width / 2, g->height / 2, g);
    cursor_draw(game_cursor);

    SHOW_BKG;
    SHOW_SPRITES;
    DISPLAY_ON;

    while (1) {
        vsync();

        /* Reset scroll and LYC for the new frame. */
        SCY_REG = base_scy;
        LYC_REG = first_lyc;

        input_poll(game_input);
        cursor_update(game_cursor, game_input, g);
        cursor_draw(game_cursor);
    }
}
