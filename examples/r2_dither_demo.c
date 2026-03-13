/*
 * R2 quasirandom dither demo.
 * https://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
 *
 * Animates a 16x16 sprite dissolving in and out using dithered transparency
 * driven by the R2 low-discrepancy sequence. Comparing each pixel's R2 value
 * against a rotating threshold produces a smooth dither animation.
 *
 * Rather than re-evaluating every pixel each frame, an inverted R2 lookup
 * table and head/tail pointers index directly into the pixels that cross the
 * threshold. A spatial offset applied every 256 steps lengthens the cycle.
 *
 * Build (GBDK-2020):
 *   lcc -DNDEBUG -Wf--opt-code-speed -Wf--max-allocs-per-node50000 -o
 * r2_dither_demo.gb r2_dither_demo.c
 */

#include <gb/gb.h>
#include <gb/hardware.h>
#include <gbdk/console.h>
#include <gbdk/font.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WRAP_STEP 213 /* Spatial offset per 256-step cycle: 13*16 + 5 */
#define MAX_SPEED 64  /* Maximum pixel updates per frame */

#define SPR_TILE 130 /* Sprite tile start index */
#define BG_TILE 128  /* Background tile start index */
#define WRAP_X 168   /* 160 + OAM x-offset 8 */
#define WRAP_Y 160   /* 144 + OAM y-offset 16 */
#define WRAP(v, lim) ((v) >= (lim) ? (v) - (lim) : (v))
#define DPAD (J_LEFT | J_RIGHT | J_UP | J_DOWN)

const __at(0x2100) uint8_t _mushroom_rom[64] = {
    0x07, 0x07, 0x1E, 0x19, 0x3E, 0x21, 0x7C, 0x43, 0x79, 0x46, 0x83,
    0xFC, 0xB3, 0xCC, 0xFB, 0x84, 0xE0, 0xE0, 0x18, 0xF8, 0x1C, 0xE4,
    0x0E, 0xF2, 0xE6, 0x1A, 0xF1, 0x0F, 0xF1, 0x0F, 0xF3, 0x0D, 0xF9,
    0x86, 0xB0, 0xCF, 0x8F, 0xFF, 0x7F, 0x72, 0x3F, 0x22, 0x3F, 0x20,
    0x1F, 0x10, 0x0F, 0x0F, 0xE7, 0x19, 0x07, 0xF9, 0xF3, 0xFD, 0xFE,
    0x4E, 0xFC, 0x44, 0xFC, 0x04, 0xF8, 0x08, 0xF0, 0xF0,
};
#define mushroom ((const uint8_t *)0x2100u)

/* pos_lut[fill_order] = linear pixel index (0..255).
 * 256-byte aligned so the high byte is constant for all lookups. */
const __at(0x2000) uint8_t _pos_lut_rom[256] = {
    0x14, 0xA0, 0x3C, 0xC8, 0x54, 0xE0, 0x7C, 0x08, 0xA4, 0x30, 0xCC, 0x58,
    0xE5, 0x71, 0x0D, 0x99, 0x25, 0xC1, 0x5D, 0xE9, 0x75, 0x01, 0x9D, 0x29,
    0xB5, 0x52, 0xEE, 0x7A, 0x06, 0x92, 0x2E, 0xBA, 0x46, 0xD2, 0x7E, 0x0A,
    0x96, 0x22, 0xBF, 0x4B, 0xD7, 0x63, 0x0F, 0x9B, 0x27, 0xB3, 0x4F, 0xDB,
    0x67, 0xF3, 0x8F, 0x2C, 0xB8, 0x44, 0xD0, 0x6C, 0xF8, 0x84, 0x10, 0xBC,
    0x48, 0xD4, 0x60, 0xFC, 0x89, 0x15, 0xA1, 0x3D, 0xD9, 0x65, 0xF1, 0x8D,
    0x19, 0xA5, 0x31, 0xCD, 0x6A, 0xF6, 0x82, 0x1E, 0xAA, 0x36, 0xC2, 0x5E,
    0xEA, 0x86, 0x12, 0xAE, 0x3A, 0xC7, 0x53, 0xEF, 0x7B, 0x17, 0xA3, 0x3F,
    0xCB, 0x57, 0xE3, 0x7F, 0x0B, 0x97, 0x34, 0xC0, 0x5C, 0xE8, 0x74, 0x00,
    0x9C, 0x28, 0xC4, 0x50, 0xEC, 0x78, 0x04, 0x91, 0x2D, 0xB9, 0x45, 0xE1,
    0x7D, 0x09, 0x95, 0x21, 0xBD, 0x49, 0xD5, 0x0E, 0x9A, 0x26, 0xB2, 0x4E,
    0xDA, 0x66, 0xF2, 0x9E, 0x2A, 0xB6, 0x42, 0xDF, 0x6B, 0xF7, 0x83, 0x2F,
    0xBB, 0x47, 0xD3, 0x6F, 0xFB, 0x87, 0x13, 0xAF, 0x4C, 0xD8, 0x64, 0xF0,
    0x8C, 0x18, 0xDC, 0x68, 0xF4, 0x80, 0x1C, 0xA9, 0x35, 0xF9, 0x85, 0x11,
    0xAD, 0x39, 0xC5, 0x51, 0xED, 0x16, 0xA2, 0x3E, 0xCA, 0x56, 0xE2, 0xA6,
    0x32, 0xCE, 0x5A, 0xE6, 0x73, 0x37, 0xC3, 0x5F, 0xEB, 0x77, 0x03, 0x9F,
    0x2B, 0xB7, 0x94, 0x20, 0xE4, 0x70, 0x0C, 0x98, 0x24, 0xB1, 0x4D, 0x41,
    0xDD, 0x69, 0xF5, 0x6E, 0xFA, 0xBE, 0x4A, 0xD6, 0x62, 0xFE, 0x8B, 0x1B,
    0xA7, 0x33, 0xCF, 0xAC, 0x38, 0x88, 0x55, 0x59, 0x76, 0x02, 0xC6, 0x93,
    0x23, 0xB4, 0x40, 0x90, 0x6D, 0x61, 0xFD, 0x8E, 0x1A, 0xDE, 0xAB, 0x3B,
    0xA8, 0x79, 0x05, 0x72, 0x43, 0xB0, 0x81, 0x1D, 0x8A, 0x5B, 0xE7, 0xFF,
    0xC9, 0x07, 0xD1, 0x1F,
};
#define pos_lut ((const uint8_t *)0x2000u)

const __at(0x2200) uint8_t _bit_mask_rom[8] = {0x80, 0x40, 0x20, 0x10,
                                               0x08, 0x04, 0x02, 0x01};
#define bit_mask ((const uint8_t *)0x2200u)

static const uint8_t bg_tiles[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
    0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
};

/* Dithering state */
static uint8_t head, tail, head_off, tail_off;
__at(0xC100) uint8_t _spr_buf_mem[64];
#define spr_buf ((uint8_t *)0xC100u)

static uint8_t speed = 4, transparency = 64;
static uint8_t spr_x = 72, spr_y = 64;

/* Head/tail pointers chase through R2 LUT; gap between them == transparency.
 * When transparency >= 128, swap head/tail so the same loop body handles
 * both halves. */
static void update_sprite_buf(void) {
    uint8_t do_swap = transparency & 0x80;

    if (do_swap) {
        if (head == tail)
            /* Advance head by one step so the swapped gap is 255, not 0.
             * Without this, the clear/restore roles invert at gap 0. */
            if (++head == 0)
                head_off += WRAP_STEP;
        uint8_t tmp = head;
        head = tail;
        tail = tmp;
        tmp = head_off;
        head_off = tail_off;
        tail_off = tmp;
    }

    for (uint8_t n = speed; n--;) {
        uint8_t gap = head - tail;
        if (gap <= transparency) {
            uint8_t idx = pos_lut[head] + head_off;
            uint8_t off = (idx >> 2) & 0x3E;
            uint8_t m = ~bit_mask[idx & 7];
            spr_buf[off] &= m;
            spr_buf[off + 1] &= m;
            if (++head == 0)
                head_off += WRAP_STEP;
        }
        if (gap >= transparency) {
            uint8_t idx = pos_lut[tail] + tail_off;
            uint8_t off = (idx >> 2) & 0x3E;
            uint8_t m = bit_mask[idx & 7];
            spr_buf[off] ^= mushroom[off] & m;
            spr_buf[off + 1] ^= mushroom[off + 1] & m;
            if (++tail == 0)
                tail_off += WRAP_STEP;
        }
    }

    if (do_swap) {
        uint8_t tmp = head;
        head = tail;
        tail = tmp;
        tmp = head_off;
        head_off = tail_off;
        tail_off = tmp;
    }
}

static void put_u8_pad3(uint8_t v) {
    putchar(v < 100 ? ' ' : '0' + v / 100);
    putchar(v < 10 ? ' ' : '0' + v / 10 % 10);
    putchar('0' + v % 10);
}

static void print_status(void) {
    gotoxy(0, 0);
    printf("Transparency ");
    put_u8_pad3(transparency);
    gotoxy(0, 2);
    printf("Speed ");
    put_u8_pad3(speed);
}

void main(void) {
    /* Set up graphics */
    DISPLAY_OFF;
    BGP_REG = OBP0_REG = DMG_PALETTE(0, 1, 2, 3);

    font_init();
    font_load(font_spect);

    set_bkg_data(BG_TILE, 2, bg_tiles);
    uint8_t row[32];
    for (uint8_t y = 0; y < 32; y++) {
        for (uint8_t x = 0; x < 32; x++)
            row[x] = BG_TILE + ((x + y) & 1);
        set_bkg_tiles(0, y, 32, 1, row);
    }

    print_status();
    gotoxy(0, 1);
    printf("Hold A+L/R to change\nHold A+U/D to changeDPAD to move sprites");

    memset(spr_buf, 0, 64);
    set_sprite_data(SPR_TILE, 4, spr_buf);
    for (uint8_t i = 0; i < 4; i++)
        set_sprite_tile(i, SPR_TILE + i);
    move_sprite(0, spr_x + 8, spr_y + 16);
    move_sprite(1, spr_x + 16, spr_y + 16);
    move_sprite(2, spr_x + 8, spr_y + 24);
    move_sprite(3, spr_x + 16, spr_y + 24);

    SHOW_BKG;
    SHOW_SPRITES;
    DISPLAY_ON;

    /* Initialize input state */
    uint8_t cur = 0, rep_timer = 0;

    while (1) {
        vsync();

        /* Copy the sprite buffer during vblank */
        set_sprite_data(SPR_TILE, 4, spr_buf);

        /* Apply the dissolve animation */
        update_sprite_buf();

        /* Handle input */
        uint8_t prev = cur;
        cur = joypad();
        uint8_t repeated = 0;
        uint8_t held = cur & DPAD;
        if (held && held == (prev & DPAD)) {
            if (++rep_timer == 25) {
                repeated = held;
                rep_timer = 21;
            }
        } else
            rep_timer = 0;
        if (cur & J_A) {
            uint8_t action = (cur & ~prev) | repeated;
            if (action & J_RIGHT)
                transparency++;
            if (action & J_LEFT)
                transparency--;
            if (action & J_UP)
                speed = (speed >= MAX_SPEED) ? 0 : speed + 1;
            if (action & J_DOWN)
                speed = (speed == 0) ? MAX_SPEED : speed - 1;
            if (action & DPAD)
                print_status();
        } else {
            if (cur & J_UP)
                spr_y = (spr_y == 0) ? WRAP_Y - 1 : spr_y - 1;
            if (cur & J_DOWN)
                spr_y = WRAP(spr_y + 1, WRAP_Y);
            if (cur & J_LEFT)
                spr_x = (spr_x == 0) ? WRAP_X - 1 : spr_x - 1;
            if (cur & J_RIGHT)
                spr_x = WRAP(spr_x + 1, WRAP_X);
            if (cur & DPAD) {
                uint8_t x0 = WRAP(spr_x + 8, WRAP_X);
                uint8_t x1 = WRAP(spr_x + 16, WRAP_X);
                uint8_t y0 = WRAP(spr_y + 16, WRAP_Y);
                uint8_t y1 = WRAP(spr_y + 24, WRAP_Y);
                move_sprite(0, x0, y0);
                move_sprite(1, x1, y0);
                move_sprite(2, x0, y1);
                move_sprite(3, x1, y1);
            }
        }
    }
}
