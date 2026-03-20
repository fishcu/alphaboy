#include <gb/gb.h>
#include <gb/hardware.h>
#include <string.h>

#include "vram.h"

void vram_set_tile(uint16_t pc, uint8_t tile) {
    volatile uint8_t *const addr = (volatile uint8_t *)(0x9800u + pc);
    while (STAT_REG & STATF_BUSY) {
    }
    *addr = tile;
}

void fill_bkg(uint8_t tile) {
    uint8_t row[32];
    memset(row, tile, sizeof(row));
    for (uint8_t y = 0; y < 32; y++)
        set_bkg_tiles(0, y, 32, 1, row);
}
