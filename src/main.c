#include <gb/gb.h>
#include <gb/hardware.h>
#include <gb/isr.h>
#include <stdint.h>
#include <string.h>

#ifndef NDEBUG
#include <gbdk/emu_debug.h>
#endif

#include "../res/tiles.h"
#include "demo.h"
#include "layout.h"

/* Timer-based vertical compression (ISR_VECTOR for Timer).
 * Requires NDEBUG (relwithdebinfo/release) to avoid lcd.o linker conflict. */

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

/* ---- HBlank vertical compression (timer-based) ----
 *
 * Each tile is 8x8 in VRAM but we display only 7 pixel rows per tile
 * by bumping SCY once per tile row.  A hardware timer fires every
 * ~800 M-cycles (~7 scanlines).  With the right initial sync all fires
 * land in or near HBlank; a short STAT safety-wait covers imprecision.
 *
 * 262 KHz timer with alternating TMA: TMA toggles between 0x38
 * (period 800, drift +2) and 0x39 (period 796, drift -2) each fire.
 * Average period = 798 = ideal.  Max drift = 2 M-cycles.
 *
 * The initial delay from VBlank to the first tile row exceeds the max
 * single 262 KHz period (1024 M-cycles), so the first overflow is a
 * "dummy" fire that lands during VBlank.  base_scy is offset by -1
 * so the dummy's SCY++ is absorbed before rendering begins.
 *
 * TIMER_CALIB: signed tick offset applied to the computed initial TIMA.
 *   Each unit = 1 tick = 4 M-cycles at 262 KHz. */

#define TIMER_TMA    0x38
#define TIMER_TAC    (TACF_START | TACF_262KHZ)
#define TIMER_CALIB  -1

static uint8_t base_scy;
volatile uint8_t frame_count;

static uint8_t timer_initial;

static void timer_isr(void) CRITICAL INTERRUPT {
    TMA_REG ^= 1;
    while (STAT_REG & STATF_BUSY)
        ;
    SCY_REG++;
}
ISR_VECTOR(VECTOR_TIMER, timer_isr)

static void vbl_isr(void) NONBANKED {
    SCY_REG = base_scy;
    TAC_REG = TACF_STOP;
    TIMA_REG = timer_initial;
    TMA_REG = TIMER_TMA;
    IF_REG &= ~TIM_IFLAG;
    DIV_REG = 0;
    TAC_REG = TIMER_TAC;
    frame_count++;
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

    /* Enable SRAM. */
    ENABLE_RAM;

    /* Initialize and draw the board. */
    game_t *g = game_state;
    game_reset(g, 19, 19, 13);

#ifndef NDEBUG
    game_debug_print(g);
#endif
    board_redraw(g);

    /* Center the board on screen via BG scroll registers.
     * Board is drawn at BG tile (0,0); the 256x256 BG wraps around,
     * so the negative offset shows blank (black) tiles as margin. */
    uint8_t offset_x = (SCREEN_W * 8 - g->width * CELL_W) / 2;
    uint8_t offset_y = (SCREEN_H * 8 - g->height * CELL_H) / 2;

    SCX_REG = (uint8_t)(-(int16_t)offset_x);
    base_scy = (uint8_t)(-(int16_t)offset_y - 1);
    SCY_REG = base_scy;

    /* Precompute timer parameters for 262 KHz with dummy fire.
     * Total delay from VBL to first real fire = dummy_period + first_period.
     * first_period = (256 - 0x39) * 4 = 796 (TMA flips to 0x39 after dummy).
     * dummy_period = total_delay - 796.
     * Each TIMER_CALIB unit = 1 tick = 4 M-cycles. */
    {
        uint8_t first_line = offset_y - 1;
        uint16_t delay = (uint16_t)7 * 114
                       + (uint16_t)first_line * 114 + 63;
        uint16_t dummy_delay = delay - ((uint16_t)(256 - (TIMER_TMA ^ 1)) << 2);
        uint8_t ticks = (uint8_t)((dummy_delay + 3) >> 2) + TIMER_CALIB;
        timer_initial = (uint8_t)(0 - ticks);
    }

    memset(game_input, 0, sizeof(input_t));

    CRITICAL {
        add_VBL(vbl_isr);
        add_VBL(nowait_int_handler);
    }
    set_interrupts(VBL_IFLAG | TIM_IFLAG);

    cursor_init(game_cursor, g->width / 2, g->height / 2, g);
    cursor_draw(game_cursor);

    SHOW_BKG;
    SHOW_SPRITES;
    DISPLAY_ON;

    while (1) {
        vsync();

#ifdef DEMO_MODE
        demo_step(g, flood_stack, flood_visited);
#else
        input_poll(game_input);

        if (game_input->pressed & J_A) {
            uint8_t color = game_color_to_play(g);
            move_legality_t result =
                game_play_move(g, game_cursor->col, game_cursor->row, color,
                               flood_stack, flood_visited);

            if (result == MOVE_LEGAL) {
                game_cursor->ghost_tile = 0;
#ifndef NDEBUG
                EMU_printf("Move %u: %s at (%hu,%hu)\n",
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
                EMU_printf("Illegal (%s): %s at (%hu,%hu)\n", reasons[result],
                           (color == BLACK) ? "B" : "W", game_cursor->col,
                           game_cursor->row);
            }
#endif
        }

        cursor_update(game_cursor, game_input, g);
        cursor_draw(game_cursor);
#endif /* DEMO_MODE */
    }
}
