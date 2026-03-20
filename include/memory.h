#ifndef MEMORY_H
#define MEMORY_H

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
 *   0 .. TILE_COUNT-1  = png2asset tiles (see display.h)
 *   TILE_COUNT..255    = free
 */

/* ------------------------------------------------------------------ */
/*  SRAM object layout                                                */
/* ------------------------------------------------------------------ */

#define SRAM_BASE 0xA000u

/* ---- Tile update queue ----
 * Game logic and cursor restoration push tile changes here instead of
 * writing VRAM directly.  The VBlank ISR drains up to TILE_DRAIN_LIMIT
 * committed entries per frame (FIFO), keeping all BG-map writes inside
 * the VBlank window.  Uncommitted entries are invisible to the ISR;
 * tile_commit() makes them drainable, tile_rewind() discards them.
 *
 * See tile_queue.h for the producer-side inline helpers. */
#define TILE_QUEUE_MAX 32u /* must be power of 2 */
#define TILE_DRAIN_LIMIT 1

typedef struct tile_entry {
    uint16_t pc;
    uint8_t tile;
} tile_entry_t;

typedef struct sram_layout {
    game_t game;
    input_t input;
    cursor_t cursor;
    uint8_t tile_queue_head;
    uint8_t tile_queue_tail;
    uint8_t tile_queue_committed;
    tile_entry_t tile_queue[TILE_QUEUE_MAX];
    uint16_t flood_deque[BOARD_POSITIONS];
    uint8_t flood_visited[BOARD_CELLS];
} sram_layout_t;

_Static_assert(sizeof(sram_layout_t) <= 0x2000u, "SRAM overflow");

#define game_state ((game_t *)(SRAM_BASE + offsetof(sram_layout_t, game)))
#define game_input ((input_t *)(SRAM_BASE + offsetof(sram_layout_t, input)))
#define game_cursor ((cursor_t *)(SRAM_BASE + offsetof(sram_layout_t, cursor)))
#define tile_queue_head                                                        \
    (*(volatile uint8_t *)(SRAM_BASE +                                         \
                           offsetof(sram_layout_t, tile_queue_head)))
#define tile_queue_tail                                                        \
    (*(uint8_t *)(SRAM_BASE + offsetof(sram_layout_t, tile_queue_tail)))
#define tile_queue_committed                                                   \
    (*(uint8_t *)(SRAM_BASE + offsetof(sram_layout_t, tile_queue_committed)))
#define tile_queue                                                             \
    ((tile_entry_t *)(SRAM_BASE + offsetof(sram_layout_t, tile_queue)))
#define flood_deque                                                            \
    ((uint16_t *)(SRAM_BASE + offsetof(sram_layout_t, flood_deque)))
#define flood_visited                                                          \
    ((uint8_t *)(SRAM_BASE + offsetof(sram_layout_t, flood_visited)))

#endif /* MEMORY_H */
