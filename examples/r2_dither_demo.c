/*
 * R2 quasirandom dither dissolve demo.
 *
 * A 16x16 mushroom sprite dissolves in and out using the R2 low-discrepancy
 * sequence.  Head/tail pointers traverse a position LUT with some distance
 * between them, clearing and restoring pixels. An offset shifts the pattern
 * each wrap to break the 256-step period into a much longer visual cycle.
 *
 * Build (GBDK-2020):
 *   lcc -DNDEBUG -Wf--opt-code-speed -o r2_dither_demo.gb r2_dither_demo.c
 * ../src/input.c
 */

#include "../src/input.h"
#include <gb/gb.h>
#include <gb/hardware.h>
#include <gbdk/console.h>
#include <gbdk/font.h>
#include <stdint.h>
#include <stdio.h>

#define BG_TILE_BASE 128
#define SPR_TILE_BASE 130
#define SPR_VRAM ((uint8_t *)(0x8000 + SPR_TILE_BASE * 16))

#define WRAP_X 168 /* screen 160 + OAM x-offset 8 */
#define WRAP_Y 160 /* screen 144 + OAM y-offset 16 */

#define WRAP_STEP 213 /* DY * 16 + DX with DX=5, DY=13 */
#define MAX_SPEED 3

static const uint8_t mushroom_tiles[64] = {
    0x07, 0x07, 0x1E, 0x19, 0x3E, 0x21, 0x7C, 0x43, 0x79, 0x46, 0x83,
    0xFC, 0xB3, 0xCC, 0xFB, 0x84, 0xE0, 0xE0, 0x18, 0xF8, 0x1C, 0xE4,
    0x0E, 0xF2, 0xE6, 0x1A, 0xF1, 0x0F, 0xF1, 0x0F, 0xF3, 0x0D, 0xF9,
    0x86, 0xB0, 0xCF, 0x8F, 0xFF, 0x7F, 0x72, 0x3F, 0x22, 0x3F, 0x20,
    0x1F, 0x10, 0x0F, 0x0F, 0xE7, 0x19, 0x07, 0xF9, 0xF3, 0xFD, 0xFE,
    0x4E, 0xFC, 0x44, 0xFC, 0x04, 0xF8, 0x08, 0xF0, 0xF0,
};

/* Two BG tiles for checkerboard background. */
static const uint8_t bg_tiles[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
    0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
};

/* R2 position LUT: pos_lut[fill_order] = linear pixel index (0-255). */
static const uint8_t pos_lut[256] = {
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

static const uint8_t bit_mask[8] = {0x80, 0x40, 0x20, 0x10,
                                    0x08, 0x04, 0x02, 0x01};

static uint8_t head, tail; /* LUT indices where pixels are hidden/restored */
static uint8_t head_offset;
static uint8_t tail_offset;
static uint8_t speed = 3;
static uint8_t transparency = 127; /* 0 = opaque .. 255 = invisible */

static uint8_t spr_x = 72, spr_y = 64;

// #define FLIP_RESTORE /* XOR toggle vs selective copy for tail (restore) */
// #define FLIP_CLEAR   /* XOR toggle vs AND ~mask for head (clear) */

static void vbl_callback(void) NONBANKED {
    if (transparency < 128) {
        for (uint8_t n = speed; n--;) {
            uint8_t idx, off, mask;

            /* tail: restore pixel */
            idx = pos_lut[tail] + tail_offset;
            off = (idx >> 2) & 0x3E;
            mask = bit_mask[idx & 7];
#ifdef FLIP_RESTORE
            SPR_VRAM[off] ^= mushroom_tiles[off] & mask;
            SPR_VRAM[off + 1] ^= mushroom_tiles[off + 1] & mask;
#else
            SPR_VRAM[off] ^= (SPR_VRAM[off] ^ mushroom_tiles[off]) & mask;
            SPR_VRAM[off + 1] ^=
                (SPR_VRAM[off + 1] ^ mushroom_tiles[off + 1]) & mask;
#endif
            tail++;
            if (tail == 0)
                tail_offset += WRAP_STEP;

            /* head: clear pixel */
            idx = pos_lut[head] + head_offset;
            off = (idx >> 2) & 0x3E;
            mask = bit_mask[idx & 7];
#ifdef FLIP_CLEAR
            SPR_VRAM[off] ^= mushroom_tiles[off] & mask;
            SPR_VRAM[off + 1] ^= mushroom_tiles[off + 1] & mask;
#else
            SPR_VRAM[off] &= ~mask;
            SPR_VRAM[off + 1] &= ~mask;
#endif
            head++;
            if (head == 0)
                head_offset += WRAP_STEP;
        }
    } else {
        for (uint8_t n = speed; n--;) {
            uint8_t idx, off, mask;

            /* tail: clear pixel (inverted) */
            idx = pos_lut[tail] + tail_offset;
            off = (idx >> 2) & 0x3E;
            mask = bit_mask[idx & 7];
#ifdef FLIP_CLEAR
            SPR_VRAM[off] ^= mushroom_tiles[off] & mask;
            SPR_VRAM[off + 1] ^= mushroom_tiles[off + 1] & mask;
#else
            SPR_VRAM[off] &= ~mask;
            SPR_VRAM[off + 1] &= ~mask;
#endif
            tail++;
            if (tail == 0)
                tail_offset += WRAP_STEP;

            /* head: restore pixel (inverted) */
            idx = pos_lut[head] + head_offset;
            off = (idx >> 2) & 0x3E;
            mask = bit_mask[idx & 7];
#ifdef FLIP_RESTORE
            SPR_VRAM[off] ^= mushroom_tiles[off] & mask;
            SPR_VRAM[off + 1] ^= mushroom_tiles[off + 1] & mask;
#else
            SPR_VRAM[off] ^= (SPR_VRAM[off] ^ mushroom_tiles[off]) & mask;
            SPR_VRAM[off + 1] ^=
                (SPR_VRAM[off + 1] ^ mushroom_tiles[off + 1]) & mask;
#endif
            head++;
            if (head == 0)
                head_offset += WRAP_STEP;
        }
    }
}

static void position_sprites(void) {
    uint8_t sx0 = spr_x + 8;
    uint8_t sx1 = spr_x + 16;
    uint8_t sy0 = spr_y + 16;
    uint8_t sy1 = spr_y + 24;
    if (sx0 >= WRAP_X)
        sx0 -= WRAP_X;
    if (sx1 >= WRAP_X)
        sx1 -= WRAP_X;
    if (sy0 >= WRAP_Y)
        sy0 -= WRAP_Y;
    if (sy1 >= WRAP_Y)
        sy1 -= WRAP_Y;
    move_sprite(0, sx0, sy0);
    move_sprite(1, sx1, sy0);
    move_sprite(2, sx0, sy1);
    move_sprite(3, sx1, sy1);
}

static void print_status(void) {
    gotoxy(0, 0);
    printf("TRANS:%-3u SPD:%u", (unsigned)transparency, (unsigned)speed);
}

static void sync_transparency(void) {
    CRITICAL {
        head_offset = 0;
        tail_offset = 0;
        tail = 0;
        if (transparency >= 128) {
            uint8_t gap = (uint8_t)(256u - transparency);
            for (uint8_t i = 0; i < 64; i++)
                SPR_VRAM[i] = 0;
            head = gap;
            for (uint8_t i = 0; i < gap; i++) {
                uint8_t idx = pos_lut[i];
                uint8_t off = (idx >> 2) & 0x3E;
                uint8_t mask = bit_mask[idx & 7];
                SPR_VRAM[off] |= mushroom_tiles[off] & mask;
                SPR_VRAM[off + 1] |= mushroom_tiles[off + 1] & mask;
            }
        } else {
            for (uint8_t i = 0; i < 64; i++)
                SPR_VRAM[i] = mushroom_tiles[i];
            head = transparency;
            for (uint8_t i = 0; i < transparency; i++) {
                uint8_t idx = pos_lut[i];
                uint8_t off = (idx >> 2) & 0x3E;
                uint8_t mask = bit_mask[idx & 7];
                SPR_VRAM[off] &= ~mask;
                SPR_VRAM[off + 1] &= ~mask;
            }
        }
    }
}

void main(void) {
    DISPLAY_OFF;

    BGP_REG = DMG_PALETTE(0, 1, 2, 3);
    OBP0_REG = DMG_PALETTE(0, 1, 2, 3);

    set_sprite_data(SPR_TILE_BASE, 4, mushroom_tiles);
    for (uint8_t i = 0; i < 4; i++)
        set_sprite_tile(i, SPR_TILE_BASE + i);
    position_sprites();

    /* Pre-set the initial state so the sliding window starts correct. */
    if (transparency >= 128) {
        /* Inverted: start cleared, restore the small visible window. */
        uint8_t gap = (uint8_t)(256u - transparency);
        for (uint8_t i = 0; i < 64; i++)
            SPR_VRAM[i] = 0;
        head = gap;
        for (uint8_t i = 0; i < gap; i++) {
            uint8_t idx = pos_lut[i];
            uint8_t off = (idx >> 2) & 0x3E;
            uint8_t mask = bit_mask[idx & 7];
            SPR_VRAM[off] |= mushroom_tiles[off] & mask;
            SPR_VRAM[off + 1] |= mushroom_tiles[off + 1] & mask;
        }
    } else {
        /* Normal: start opaque, clear the transparent window. */
        head = transparency;
        for (uint8_t i = 0; i < transparency; i++) {
            uint8_t idx = pos_lut[i];
            uint8_t off = (idx >> 2) & 0x3E;
            uint8_t mask = bit_mask[idx & 7];
            SPR_VRAM[off] &= ~mask;
            SPR_VRAM[off + 1] &= ~mask;
        }
    }

    font_init();
    font_load(font_min);
    cls();

    set_bkg_data(BG_TILE_BASE, 2, bg_tiles);
    uint8_t row[32];
    for (uint8_t y = 3; y < 32; y++) {
        for (uint8_t x = 0; x < 32; x++)
            row[x] = BG_TILE_BASE + ((x + y) & 1);
        set_bkg_tiles(0, y, 32, 1, row);
    }

    print_status();

    CRITICAL {
        add_VBL(vbl_callback);
        add_VBL(nowait_int_handler);
    }

    SHOW_BKG;
    SHOW_SPRITES;
    DISPLAY_ON;

    while (1) {
        vsync();

        uint8_t keys = joypad();
        if (keys & J_UP)
            spr_y = (spr_y == 0) ? WRAP_Y - 1 : spr_y - 1;
        if (keys & J_DOWN) {
            spr_y++;
            if (spr_y >= WRAP_Y)
                spr_y = 0;
        }
        if (keys & J_LEFT)
            spr_x = (spr_x == 0) ? WRAP_X - 1 : spr_x - 1;
        if (keys & J_RIGHT) {
            spr_x++;
            if (spr_x >= WRAP_X)
                spr_x = 0;
        }
        if (keys & (J_UP | J_DOWN | J_LEFT | J_RIGHT))
            position_sprites();
    }
}
