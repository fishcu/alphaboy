#ifndef LAYOUT_H
#define LAYOUT_H

#include <stddef.h>
#include <stdint.h>

#include "cursor.h"
#include "go.h"
#include "input.h"

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
 *   0 .. TILE_COUNT-1  = png2asset tiles (see enum below)
 *   TILE_COUNT..255    = free
 */

/* ------------------------------------------------------------------ */
/*  SRAM object layout                                                */
/* ------------------------------------------------------------------ */

#define SRAM_BASE 0xA000u

typedef struct sram_layout {
    game_t game;
    input_t input;
    cursor_t cursor;
    uint16_t flood_queue[BOARD_POSITIONS];
    uint8_t flood_visited[BOARD_FIELD_BYTES];
} sram_layout_t;

_Static_assert(sizeof(sram_layout_t) <= 0x2000u, "SRAM overflow");

#define game_state ((game_t *)(SRAM_BASE + offsetof(sram_layout_t, game)))
#define game_input ((input_t *)(SRAM_BASE + offsetof(sram_layout_t, input)))
#define game_cursor ((cursor_t *)(SRAM_BASE + offsetof(sram_layout_t, cursor)))
#define flood_stack                                                            \
    ((uint16_t *)(SRAM_BASE + offsetof(sram_layout_t, flood_queue)))
#define flood_visited                                                          \
    ((uint8_t *)(SRAM_BASE + offsetof(sram_layout_t, flood_visited)))

/* ------------------------------------------------------------------ */
/*  Palette helper                                                    */
/* ------------------------------------------------------------------ */

/* Pack four 2-bit shade values into a DMG palette register byte.
 * Each argument is the shade (0=white, 1=light, 2=dark, 3=black)
 * for the corresponding color index. */
#define DMG_PAL(s0, s1, s2, s3)                                                \
    ((uint8_t)((s0) | ((s1) << 2) | ((s2) << 4) | ((s3) << 6)))

/* ------------------------------------------------------------------ */
/*  Tile data                                                         */
/* ------------------------------------------------------------------ */

/* Base address value for set_tile_data() to target 0x8000. */
#define TILE_DATA_BASE 0x80

/* Tile indices — sequential order matches png2asset output.
 * The PNG is scanned left-to-right, top-to-bottom; duplicate (empty)
 * tiles are deduplicated so only TILE_EMPTY occupies index 0. */
enum {
    TILE_EMPTY = 0,

    /* Board frame (11 tiles). */
    TILE_FRAME_TL,
    TILE_FRAME_T,
    TILE_FRAME_TR,
    TILE_FRAME_L,
    TILE_FRAME_R,
    TILE_FRAME_BL_U,
    TILE_FRAME_B_U,
    TILE_FRAME_BR_U,
    TILE_FRAME_BL_D,
    TILE_FRAME_B_D,
    TILE_FRAME_BR_D,

    /* Board surface intersections (9 tiles). */
    TILE_CORNER_TL,
    TILE_EDGE_T,
    TILE_CORNER_TR,
    TILE_EDGE_L,
    TILE_CENTER,
    TILE_EDGE_R,
    TILE_CORNER_BL,
    TILE_EDGE_B,
    TILE_CORNER_BR,

    /* Star point. */
    TILE_HOSHI,

    /* Stones. */
    TILE_STONE_B,
    TILE_STONE_W,

    /* Last-played stone markers. */
    TILE_LAST_B,
    TILE_LAST_W,

    /* Ko-point surface intersections (9 tiles). */
    TILE_KO_TL,
    TILE_KO_T,
    TILE_KO_TR,
    TILE_KO_L,
    TILE_KO_C,
    TILE_KO_R,
    TILE_KO_BL,
    TILE_KO_B,
    TILE_KO_BR,

    /* Black territory markers (8 unique tiles + 1 deduplicated). */
    TILE_TERR_B_TL,
    TILE_TERR_B_T,
    TILE_TERR_B_TR,
    TILE_TERR_B_L,
    /* TILE_TERR_B_C is identical to TILE_HOSHI; defined as alias below. */
    TILE_TERR_B_R,
    TILE_TERR_B_BL,
    TILE_TERR_B_B,
    TILE_TERR_B_BR,

    /* White territory markers (9 tiles). */
    TILE_TERR_W_TL,
    TILE_TERR_W_T,
    TILE_TERR_W_TR,
    TILE_TERR_W_L,
    TILE_TERR_W_C,
    TILE_TERR_W_R,
    TILE_TERR_W_BL,
    TILE_TERR_W_B,
    TILE_TERR_W_BR,

    /* Cursor sprites. */
    TILE_CURSOR,
    TILE_CURSOR2,

    TILE_COUNT
};

/* Deduplicated: TILE_TERR_B_C has identical tile data to TILE_HOSHI. */
#define TILE_TERR_B_C TILE_HOSHI

/* Board origin in the BG tilemap.  The board surface is drawn at
 * (BOARD_BG_X, BOARD_BG_Y) to leave room for the frame around it. */
#define BOARD_BG_X 1
#define BOARD_BG_Y 1

/* ------------------------------------------------------------------ */
/*  Screen / board positioning                                        */
/* ------------------------------------------------------------------ */

#define SCREEN_W 20 /* screen width in tiles  (160 px) */
#define SCREEN_H 18 /* screen height in tiles (144 px) */

/* Cell dimensions as displayed on screen.
 * Vertical HBlank compression skips the top pixel row of each tile,
 * giving 7 visible rows per cell.  Horizontal is uncompressed. */
#define CELL_W 8
#define CELL_H 7

/* Extra upward pixel shift so the board + frame fits on screen. */
#define SCROLL_ADJUST_Y 2

/* ------------------------------------------------------------------ */
/*  OAM sprite allocation                                             */
/* ------------------------------------------------------------------ */

#define CURSOR_SPR_UL 0
#define CURSOR_SPR_UR 1
#define CURSOR_SPR_LL 2
#define CURSOR_SPR_LR 3

/* ------------------------------------------------------------------ */
/*  Display helpers (defined in main.c)                               */
/* ------------------------------------------------------------------ */

/* Write one BG-map tile without disabling interrupts.
 * Waits for VRAM-accessible mode then stores a single byte. */
void vram_set_tile(uint8_t x, uint8_t y, uint8_t tile);

/* Return the board-surface tile index for an empty intersection. */
uint8_t surface_tile(uint8_t col, uint8_t row, uint8_t w, uint8_t h);

/* Frame counter incremented by the VBlank ISR. */
extern volatile uint8_t frame_count;

#endif /* LAYOUT_H */
