#include <gb/gb.h>
#include <gb/hardware.h>

#include "display.h"
#include "tiles.h"
#include "vram.h"

void display_init(void) {
    /* BG + Window read tile data from 0x8000 (unsigned),
     * sharing the same region as sprites. */
    LCDC_REG = LCDCF_BG8000;

    BGP_REG = DMG_PALETTE(0, 1, 2, 3);
    OBP0_REG = DMG_PALETTE(0, 0, 2, 3); /* cursor */

    set_tile_data(0, tiles_TILE_COUNT, tiles_tiles, TILE_DATA_BASE);
    fill_bkg(TILE_EMPTY);
}

void display_start(void) {
    SHOW_BKG;
    SHOW_SPRITES;
    DISPLAY_ON;
}
