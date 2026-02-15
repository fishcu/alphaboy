#ifndef LAYOUT_H
#define LAYOUT_H

#include <stdint.h>

#include "board.h"
#include "input.h"
#include "cursor.h"

/*
 * Memory Layout
 * =============
 *
 * VRAM (0x8000-0x9FFF):
 *   0x8000-0x8FFF  Shared BG + Sprite tiles  (4 KB, 256 tiles)
 *   0x9000-0x97FF  Free                      (2 KB)
 *   0x9800-0x9BFF  BG Map                    (1 KB, hardware-fixed)
 *   0x9C00-0x9FFF  Window Map                (1 KB, hardware-fixed)
 *
 * SRAM (0xA000-0xBFFF, 8 KB, MBC5 + RAM + Battery):
 *   Manually managed; allocations listed below.
 *   We keep all game state here so WRAM remains fully available
 *   to the C runtime (stack, BSS, locals).
 *
 * WRAM (0xC000-0xDFFF, 8 KB):
 *   Managed by the C runtime (BSS, DATA, stack).
 *   Do NOT place manually-addressed objects here.
 *
 * Tile allocation at 0x8000:
 *   0        = blank (solid color-index-0, i.e. black)
 *   1 .. 12  = sprite sheet tiles (loaded from png2asset data)
 *   13..255  = free
 */

/* ------------------------------------------------------------------ */
/*  SRAM object layout (chained via sizeof, auto-adjusts)             */
/* ------------------------------------------------------------------ */

#define SRAM_BASE  0xA000u

#define game_board  ((board_t *)SRAM_BASE)

#define game_input  ((input_t *)(SRAM_BASE \
                     + sizeof(board_t)))

#define game_cursor ((cursor_t *)(SRAM_BASE \
                     + sizeof(board_t) \
                     + sizeof(input_t)))

/* ------------------------------------------------------------------ */
/*  Palette helper                                                    */
/* ------------------------------------------------------------------ */

/* Pack four 2-bit shade values into a DMG palette register byte.
 * Each argument is the shade (0=white, 1=light, 2=dark, 3=black)
 * for the corresponding color index. */
#define DMG_PAL(s0, s1, s2, s3) \
    ((s0) | ((s1) << 2) | ((s2) << 4) | ((s3) << 6))

/* ------------------------------------------------------------------ */
/*  Tile data                                                         */
/* ------------------------------------------------------------------ */

/* Base address value for set_tile_data() to target 0x8000. */
#define TILE_DATA_BASE  0x80

/* Tile offset: tiles 1..N follow the blank tile at index 0. */
#define TILE_OFFSET  1

/* Tile indices (sprite-sheet position + offset). */
#define TILE_BLANK      0
#define TILE_CURSOR     (0  + TILE_OFFSET)
#define TILE_STONE_W    (1  + TILE_OFFSET)
#define TILE_STONE_B    (2  + TILE_OFFSET)
#define TILE_CORNER_TL  (3  + TILE_OFFSET)
#define TILE_EDGE_T     (4  + TILE_OFFSET)
#define TILE_CORNER_TR  (5  + TILE_OFFSET)
#define TILE_EDGE_L     (6  + TILE_OFFSET)
#define TILE_CENTER     (7  + TILE_OFFSET)
#define TILE_EDGE_R     (8  + TILE_OFFSET)
#define TILE_CORNER_BL  (9  + TILE_OFFSET)
#define TILE_EDGE_B     (10 + TILE_OFFSET)
#define TILE_CORNER_BR  (11 + TILE_OFFSET)

/* ------------------------------------------------------------------ */
/*  Screen / board positioning                                        */
/* ------------------------------------------------------------------ */

#define SCREEN_W  20
#define SCREEN_H  18

/* Board draw position on the BG tilemap (in tiles). */
#define BOARD_BKG_X  1
#define BOARD_BKG_Y  1

/* ------------------------------------------------------------------ */
/*  OAM sprite allocation                                             */
/* ------------------------------------------------------------------ */

#define CURSOR_SPR_UL  0
#define CURSOR_SPR_UR  1
#define CURSOR_SPR_LL  2
#define CURSOR_SPR_LR  3

#endif /* LAYOUT_H */
