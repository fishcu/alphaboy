#include <gb/gb.h>
#include <gb/hardware.h>
#include <gb/isr.h>

#include "display.h"
#include "go.h"
#include "interrupts.h"
#include "memory.h"

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
static uint8_t timer_initial;

/* ---- Timer ISR: HBlank SCY bump ----
 * Fires every ~7 scanlines (alternating 800/796 M-cycle periods).
 * Waits for VRAM-accessible mode, then increments SCY to skip
 * one pixel row per tile row (8->7 vertical compression).
 * Also toggles TMA between 0x38 and 0x39 for drift correction. */
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

/* ---- VBlank ISR ----
 * 1. Resets SCY to base_scy for the next frame's vertical compression.
 * 2. Re-syncs the hardware timer (TIMA, TMA, DIV) so the first
 *    timer overflow after VBlank lands correctly.
 * 3. Drains committed tile-queue entries to VRAM (freely accessible
 *    during VBlank).  Only committed entries are visible; speculative
 *    pushes from an in-progress move are not touched. */
static void vbl_isr(void) NONBANKED {
    SCY_REG = base_scy;
    TIMA_REG = timer_initial;
    TMA_REG = TIMER_TMA;
    DIV_REG = 0;
    IF_REG &= ~TIM_IFLAG;

    uint8_t h = tile_queue_head;
    const uint8_t com = tile_queue_committed;
    uint8_t n = TILE_DRAIN_LIMIT;
    while (h != com && n > 0) {
        *(volatile uint8_t *)(0x9800u + tile_queue[h].pc) = tile_queue[h].tile;
        h = (h + 1) % TILE_QUEUE_MAX;
        n--;
    }
    tile_queue_head = h;
}

void interrupts_init(uint8_t board_w, uint8_t board_h) {
    const uint8_t offset_x = (SCREEN_W * 8 - board_w * CELL_W) / 2;
    const uint8_t offset_y =
        (SCREEN_H * 8 - board_h * CELL_H) / 2 - SCROLL_ADJUST_Y;

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

    TMA_REG = TIMER_TMA;
    TAC_REG = TIMER_TAC;

    CRITICAL {
        add_VBL(vbl_isr);
        add_VBL(nowait_int_handler);
    }
    set_interrupts(VBL_IFLAG | TIM_IFLAG);
}
