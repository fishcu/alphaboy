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

/* Return the board-surface tile for an empty intersection. */
uint8_t surface_tile(uint8_t col, uint8_t row, uint8_t w, uint8_t h) {
    const uint8_t top = (row == 0);
    const uint8_t bottom = (row == h - 1);
    const uint8_t left = (col == 0);
    const uint8_t right = (col == w - 1);

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

    /* Star-point (hoshi) check for interior intersections.
     * >= 13: 4th line from edge, corners + sides + center.
     * < 13:  3rd line from edge, corners + center only. */
    const uint8_t d = (w >= 13 && h >= 13) ? 3 : 2;
    const uint8_t on_col = (col == d || col == w - 1 - d || col == w / 2);
    const uint8_t on_row = (row == d || row == h - 1 - d || row == h / 2);
    if (on_col && on_row) {
        if (w >= 13 && h >= 13)
            return TILE_HOSHI;
        if ((col == w / 2) == (row == h / 2))
            return TILE_HOSHI;
    }

    return TILE_CENTER;
}

/* Return the ko-marked variant of a surface tile for an empty
 * intersection.  Ko tiles mirror the 9 surface intersection tiles
 * at a fixed offset; hoshi maps to the center ko tile. */
uint8_t ko_tile(uint8_t col, uint8_t row, uint8_t w, uint8_t h) {
    uint8_t t = surface_tile(col, row, w, h);
    if (t == TILE_HOSHI)
        t = TILE_CENTER;
    return t + (TILE_KO_TL - TILE_CORNER_TL);
}

/* ---- Lightweight VRAM tile write ----
 * Writes one byte to the BG tilemap without disabling interrupts.
 * Waits for VRAM-accessible mode (HBlank or VBlank), then stores.
 * A single byte store completes in 1 M-cycle (4 clocks), well
 * within any VRAM-accessible window.
 * Used only during init (display off) by board_redraw. */
void vram_set_tile(uint16_t pc, uint8_t tile) {
    volatile uint8_t *const addr = (volatile uint8_t *)(0x9800u + pc);
    while (STAT_REG & STATF_BUSY) {
    }
    *addr = tile;
}

/* Full board redraw  --  used only at init (display off, fast).
 * Draws the decorative frame and all intersections.
 * During gameplay, game_play_move updates tiles incrementally. */
static void board_redraw(const game_t *g) {
    const uint8_t w = g->width;
    const uint8_t h = g->height;

    /* ---- Frame ---- */

    /* Top row. */
    vram_set_tile(0, TILE_FRAME_TL);
    for (uint8_t col = 0; col < w; col++)
        vram_set_tile(col + BOARD_MARGIN, TILE_FRAME_T);
    vram_set_tile(w + BOARD_MARGIN, TILE_FRAME_TR);

    /* Left and right columns. */
    for (uint8_t row = 0; row < h; row++) {
        const uint16_t ry = VRAM_XY(0, row + BOARD_MARGIN);
        vram_set_tile(ry, TILE_FRAME_L);
        vram_set_tile(ry | (w + BOARD_MARGIN), TILE_FRAME_R);
    }

    /* Bottom rows (two tiles tall). */
    const uint16_t by1 = VRAM_XY(0, h + BOARD_MARGIN);
    const uint16_t by2 = VRAM_XY(0, h + BOARD_MARGIN + 1);
    vram_set_tile(by1, TILE_FRAME_BL_U);
    vram_set_tile(by2, TILE_FRAME_BL_D);
    for (uint8_t col = 0; col < w; col++) {
        vram_set_tile(by1 | (col + BOARD_MARGIN), TILE_FRAME_B_U);
        vram_set_tile(by2 | (col + BOARD_MARGIN), TILE_FRAME_B_D);
    }
    vram_set_tile(by1 | (w + BOARD_MARGIN), TILE_FRAME_BR_U);
    vram_set_tile(by2 | (w + BOARD_MARGIN), TILE_FRAME_BR_D);

    /* ---- Board intersections ---- */

    uint16_t pos = BOARD_COORD(0, 0);
    for (uint8_t row = 0; row < h; row++) {
        uint16_t p = pos;
        for (uint8_t col = 0; col < w; col++) {
            uint8_t tile;
            switch (g->board[p]) {
            case COLOR_BLACK:
                tile = TILE_STONE_B;
                break;
            case COLOR_WHITE:
                tile = TILE_STONE_W;
                break;
            default:
                tile = surface_tile(col, row, w, h);
                break;
            }
            vram_set_tile(p, tile);
            p++;
        }
        pos += DIR_DOWN;
    }

    /* ---- Last-played marker ---- */

    if (g->move_count > g->history_base) {
        const move_t last = g->history[(g->move_count - 1) % HISTORY_MAX];
        const uint16_t lc = MOVE_COORD(last);
        if (lc != COORD_PASS) {
            vram_set_tile(lc, (MOVE_COLOR(last) == COLOR_BLACK) ? TILE_LAST_B
                                                                : TILE_LAST_W);
        }
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
 * TAC and TMA are configured once at init and never rewritten, avoiding
 * the spurious TIMA increments that the falling-edge detector can cause.
 * VBlank re-syncs the timer by writing TIMA and resetting DIV only.
 *
 * The ISR is hand-written __naked assembly, saving only af (the sole
 * register pair it uses) instead of SDCC's default four-pair save.
 *
 * The initial delay from VBlank to the first tile row exceeds the max
 * single 262 KHz period (1024 M-cycles), so the first overflow is a
 * "dummy" fire that lands during VBlank.  base_scy is offset by -1
 * so the dummy's SCY++ is absorbed before rendering begins.
 *
 * TIMER_CALIB: signed tick offset applied to the computed initial TIMA.
 *   Each unit = 1 tick = 4 M-cycles at 262 KHz. */

#define TIMER_TMA 0x38
#define TIMER_TAC (TACF_START | TACF_262KHZ)
#define TIMER_CALIB 10

static uint8_t base_scy;
volatile uint8_t frame_count;

static uint8_t timer_initial;

// clang-format off
void timer_isr(void) __naked {
    __asm
    push    af
    ldh     a, (0x06)       ; TMA_REG
    xor     a, #0x01
    ldh     (0x06), a
00200$:
    ldh     a, (0x41)       ; STAT_REG
    bit     1, a
    jr      NZ, 00200$
    ldh     a, (0x42)       ; SCY_REG
    inc     a
    ldh     (0x42), a
    pop     af
    reti
    __endasm;
}
ISR_VECTOR(VECTOR_TIMER, timer_isr)
// clang-format on

static void vbl_isr(void) NONBANKED {
    SCY_REG = base_scy;
    TIMA_REG = timer_initial;
    TMA_REG = TIMER_TMA;
    DIV_REG = 0;
    IF_REG &= ~TIM_IFLAG;

    /* Drain tile queue  --  VRAM is freely accessible during VBlank.
     * Only committed entries are visible; speculative (uncommitted)
     * pushes from an in-progress move are not touched. */
    uint8_t h = tile_queue_head;
    const uint8_t com = tile_queue_committed;
    uint8_t n = TILE_DRAIN_LIMIT;
    while (h != com && n > 0) {
        *(volatile uint8_t *)(0x9800u + tile_queue[h].pc) = tile_queue[h].tile;
        h = (h + 1) % TILE_QUEUE_MAX;
        n--;
    }
    tile_queue_head = h;

    frame_count++;
}

void main(void) {
    DISPLAY_OFF;

    /*
     * Set LCDC bit 4: BG + Window read tile data from 0x8000 (unsigned),
     * sharing the same region as sprites.  All other LCDC bits start clear
     * (display off, layers off)  --  we turn them on at the end.
     */
    LCDC_REG = LCDCF_BG8000;

    /* DMG palettes: DMG_PAL(idx0, idx1, idx2, idx3)
     * Shades: 0=white, 1=light, 2=dark, 3=black.
     * Sprite index 0 is always transparent regardless of OBP value. */
    BGP_REG = DMG_PAL(0, 1, 2, 3);
    OBP0_REG = DMG_PAL(0, 0, 2, 3); /* cursor */

    /* Load tiles to 0x8000 (shared BG + Sprite region). */
    set_tile_data(0, tiles_TILE_COUNT, tiles_tiles, TILE_DATA_BASE);

    /* Fill entire visible background with the empty tile. */
    fill_bkg(TILE_EMPTY);

    /* Enable SRAM. */
    ENABLE_RAM;

    tile_queue_head = 0;
    tile_queue_tail = 0;
    tile_queue_committed = 0;

    /* Initialize and draw the board. */
    game_t *const g = game_state;
    game_reset(g, 19, 19, 13);

#ifndef NDEBUG
    game_debug_print(g);
#endif
    board_redraw(g);

    /* Position the board on screen via BG scroll registers.
     * Board intersections start at BG tile column/row BOARD_MARGIN.
     * The 256x256 BG wraps; scroll offsets place the board roughly
     * centered, shifted up by SCROLL_ADJUST_Y to show the bottom frame. */
    const uint8_t offset_x = (SCREEN_W * 8 - g->width * CELL_W) / 2;
    const uint8_t offset_y =
        (SCREEN_H * 8 - g->height * CELL_H) / 2 - SCROLL_ADJUST_Y;

    SCX_REG = (uint8_t)(BOARD_MARGIN * 8 - (int16_t)offset_x);
    base_scy = (uint8_t)(BOARD_MARGIN * 8 - (int16_t)offset_y - 1);
    SCY_REG = base_scy;

    /* Precompute timer parameters for 262 KHz with dummy fire.
     * Total delay from VBL to first real fire = dummy_period + first_period.
     * first_period = (256 - 0x39) * 4 = 796 (TMA flips to 0x39 after dummy).
     * dummy_period = total_delay - 796.
     * Each TIMER_CALIB unit = 1 tick = 4 M-cycles. */
    {
        const uint8_t first_line = offset_y - 1;
        const uint16_t delay =
            (uint16_t)7 * 114 + (uint16_t)first_line * 114 + 63;
        const uint16_t dummy_delay =
            delay - ((uint16_t)(256 - (TIMER_TMA ^ 1)) << 2);
        const uint8_t ticks = (uint8_t)((dummy_delay + 3) >> 2) + TIMER_CALIB;
        timer_initial = (uint8_t)(0 - ticks);
    }

    memset(game_input, 0, sizeof(input_t));

    TMA_REG = TIMER_TMA;
    TAC_REG = TIMER_TAC;

    CRITICAL {
        add_VBL(vbl_isr);
        add_VBL(nowait_int_handler);
    }
    set_interrupts(VBL_IFLAG | TIM_IFLAG);

    cursor_init(game_cursor, g->width / 2, g->height / 2, g);

    SHOW_BKG;
    SHOW_SPRITES;
    DISPLAY_ON;

    while (1) {
        vsync();

#ifdef DEMO_MODE
        demo_step(g);
#else
        input_poll(game_input);

        if (game_input->pressed & J_A) {
            const color_t color = game_color_to_play(g);
            const uint16_t coord =
                BOARD_COORD(game_cursor->col, game_cursor->row);
            const move_legality_t result = game_play_move(g, coord, color);

            if (result == MOVE_LEGAL) {
#ifndef NDEBUG
                EMU_printf("Move %u: %s at (%hu,%hu)\n",
                           (unsigned)g->move_count,
                           (color == COLOR_BLACK) ? "B" : "W", game_cursor->col,
                           game_cursor->row);
                game_debug_print(g);
#endif
            }
#ifndef NDEBUG
            else {
                static const char *const reasons[] = {"legal", "non-empty",
                                                      "suicidal", "ko"};
                EMU_printf("Illegal (%s): %s at (%hu,%hu)\n", reasons[result],
                           (color == COLOR_BLACK) ? "B" : "W", game_cursor->col,
                           game_cursor->row);
            }
#endif
        }

        if (game_input->pressed & J_B) {
            if (game_undo(g) == UNDO_OK) {
#ifndef NDEBUG
                EMU_printf("Undo -> move_count=%u\n", (unsigned)g->move_count);
                game_debug_print(g);
#endif
            }
        }

        cursor_update(game_cursor, game_input, g);
#endif /* DEMO_MODE */
    }
}
