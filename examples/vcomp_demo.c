/*
 * Displays 8x8 VRAM tiles at 7 visible pixel rows each by bumping SCY
 * once per tile row during HBlank. A 262 KHz hardware timer with
 * alternating TMA (0x38/0x39) gives periods of 800 and 796 M-cycles,
 * averaging to the ideal 798 (7 scanlines). Max drift: 2 M-cycles.
 *
 * The first overflow is a dummy that lands in VBlank. base_scy is
 * offset by -1 so its SCY++ is absorbed before rendering begins.
 *
 * Build (GBDK-2020):
 *   lcc -DNDEBUG -Wf--opt-code-speed -o vcomp_demo.gb vcomp_demo.c
 */

#include <gb/gb.h>
#include <gb/hardware.h>
#include <gb/isr.h>
#include <stdint.h>
#include <string.h>

/* 8x8 tile with a 1px line at row 3 (visual center of 7 visible rows). */
static const uint8_t tile_data[16] = {
    0xFF, 0x00, /* row 1: light gray, hidden by compression */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, /* row 4 (displayed as row 3): black line */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

#define TIMER_TMA 0x38
#define TIMER_TAC (TACF_START | TACF_262KHZ)

static uint8_t base_scy = 250;
static uint8_t timer_initial = 126;

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
}

void main(void) {
    DISPLAY_OFF;
    LCDC_REG = LCDCF_BG8000;
    BGP_REG = DMG_PALETTE(0, 1, 2, 3);

    set_bkg_data(0, 1, tile_data);

    uint8_t row[32];
    memset(row, 0, sizeof(row));
    for (uint8_t y = 0; y < 32; y++)
        set_bkg_tiles(0, y, 32, 1, row);

    SCY_REG = base_scy;

    CRITICAL {
        add_VBL(vbl_isr);
        add_VBL(nowait_int_handler);
    }
    set_interrupts(VBL_IFLAG | TIM_IFLAG);

    SHOW_BKG;
    DISPLAY_ON;

    while (1)
        vsync();
}
