#include <gb/gb.h>
#include <gb/hardware.h>
#include <stdint.h>
#include <string.h>

#ifndef NDEBUG
#include <gbdk/emu_debug.h>
#endif

#include "../res/tiles.h"
#include "layout.h"

/* Blank tile: 16 zero bytes = all pixels at color index 0 (black). */
static const uint8_t blank_tile[16] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                       0xFF, 0xFF, 0xFF, 0xFF};

/* Return the board-surface tile for an empty intersection. */
uint8_t surface_tile(uint8_t col, uint8_t row, uint8_t w, uint8_t h) {
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

/* ---- Lightweight VRAM tile write ----
 * Writes one byte to the BG tilemap without disabling interrupts.
 * Waits for VRAM-accessible mode (HBlank or VBlank), then stores.
 * A single byte store completes in 1 M-cycle (4 clocks), well
 * within any VRAM-accessible window. */
void vram_set_tile(uint8_t x, uint8_t y, uint8_t tile) {
    volatile uint8_t *addr =
        (volatile uint8_t *)(0x9800u + ((uint16_t)y << 5) + x);
    while (STAT_REG & STATF_BUSY) {
    }
    *addr = tile;
}

/* Full board redraw — used only at init (display off, fast).
 * During gameplay, game_play_move updates tiles incrementally. */
static void board_redraw(const game_t *g) {
    uint8_t w = g->width;
    uint8_t h = g->height;
    uint16_t pos = BOARD_COORD(0, 0);

    for (uint8_t row = 0; row < h; row++) {
        uint16_t p = pos;
        for (uint8_t col = 0; col < w; col++) {
            uint8_t tile;
            if (BF_GET(g->black_stones, p))
                tile = TILE_STONE_B;
            else if (BF_GET(g->white_stones, p))
                tile = TILE_STONE_W;
            else
                tile = surface_tile(col, row, w, h);
            vram_set_tile(col, row, tile);
            p++;
        }
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
 *
 * lcd_isr: Advances LYC for the next fire (safe outside HBlank),
 *   then waits for HBlank and bumps SCY.  No counter, no reset —
 *   the chain naturally terminates when LYC exceeds scanline 153.
 *   nowait_int_handler skips the dispatcher's WAIT_STAT on exit.
 *
 * vbl_isr: Resets SCY and LYC at the start of each VBlank so the
 *   chain restarts on the next visible frame.  VBlank has higher
 *   interrupt priority than STAT, so it fires at line 144 before
 *   any stale LYC match could trigger. */

static uint8_t base_scy;
static uint8_t first_lyc;

static void lcd_isr(void) NONBANKED {
    LYC_REG += CELL_H;
    while (STAT_REG & STATF_BUSY)
        ;
    SCY_REG++;
}

static void vbl_isr(void) NONBANKED {
    SCY_REG = base_scy;
    LYC_REG = first_lyc;
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
    board_redraw(g);

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

    /* Install the LYC-chained LCD ISR and the VBlank reset handler.
     * nowait_int_handler skips the dispatcher's WAIT_STAT on exit. */
    LYC_REG = first_lyc;
    CRITICAL {
        STAT_REG |= STATF_LYC;
        add_LCD(lcd_isr);
        add_LCD(nowait_int_handler);
        add_VBL(vbl_isr);
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
        input_poll(game_input);

        /* A button: play a stone at the cursor position. */
        if (game_input->pressed & J_A) {
            uint8_t color = g->move_count & 1;
            move_legality_t result =
                game_play_move(g, game_cursor->col, game_cursor->row, color,
                               flood_stack, flood_visited);

            if (result == MOVE_LEGAL) {
#ifndef NDEBUG
                EMU_printf("Move %u: %s at (%hhu,%hhu)",
                           (unsigned)g->move_count,
                           (color == BLACK) ? "B" : "W", game_cursor->col,
                           game_cursor->row);
                game_debug_print(g);
#endif
            }
#ifndef NDEBUG
            else {
                static const char *const reasons[] = {"legal", "non-empty",
                                                      "suicidal", "ko"};
                EMU_printf("Illegal (%s): %s at (%hhu,%hhu)", reasons[result],
                           (color == BLACK) ? "B" : "W", game_cursor->col,
                           game_cursor->row);
            }
#endif
        }

        cursor_update(game_cursor, game_input, g);
        cursor_draw(game_cursor);
    }
}
