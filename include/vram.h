#ifndef VRAM_H
#define VRAM_H

#include <stdint.h>

/* Write one BG-map tile at packed coordinate `pc` without disabling
 * interrupts.  `pc` is a tile-map offset: (row << 5) | col.
 * Waits for VRAM-accessible mode then stores a single byte.
 * Used only during init (display off) by board_redraw. */
void vram_set_tile(uint16_t pc, uint8_t tile);

/* Fill the entire 32x32 BG tilemap with a single tile index.
 * Must cover the full map since BG scrolling wraps at 256x256. */
void fill_bkg(uint8_t tile);

#endif /* VRAM_H */
