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

/* ---- Board animation queue ----
 * Game logic appends ordered BG-map changes here.  The VBlank ISR applies
 * commands until an animation-step boundary is reached, keeping all VRAM
 * writes inside VBlank.  The committed frontier hides speculative move
 * prefixes until legality is known.
 *
 * See board_animation.h for the producer-side inline helpers. */
#define BOARD_ANIMATION_QUEUE_MAX 32u /* must be power of 2 */
#define BOARD_ANIMATION_MAX_WRITES_PER_FRAME 4u

typedef struct board_animation_entry {
    uint16_t pc;
    uint8_t tile;
} board_animation_entry_t;

typedef struct sram_layout {
    game_t game;
    input_t input;
    cursor_t cursor;
    uint8_t game_action_pending;
    uint8_t game_action_busy;
    uint16_t game_action_coord;
    uint8_t board_animation_head;
    uint8_t board_animation_tail;
    uint8_t board_animation_committed;
    board_animation_entry_t board_animation_queue[BOARD_ANIMATION_QUEUE_MAX];
    uint16_t flood_deque[BOARD_POSITIONS];
    uint8_t flood_visited[BOARD_CELLS];
} sram_layout_t;

_Static_assert(sizeof(sram_layout_t) <= 0x2000u, "SRAM overflow");

#define game_state ((game_t *)(SRAM_BASE + offsetof(sram_layout_t, game)))
#define game_input ((input_t *)(SRAM_BASE + offsetof(sram_layout_t, input)))
#define game_cursor ((cursor_t *)(SRAM_BASE + offsetof(sram_layout_t, cursor)))
#define game_action_pending                                                    \
    (*(volatile uint8_t *)(SRAM_BASE +                                         \
                           offsetof(sram_layout_t, game_action_pending)))
#define game_action_busy                                                       \
    (*(volatile uint8_t *)(SRAM_BASE +                                         \
                           offsetof(sram_layout_t, game_action_busy)))
#define game_action_coord                                                      \
    (*(volatile uint16_t *)(SRAM_BASE +                                        \
                            offsetof(sram_layout_t, game_action_coord)))
#define board_animation_head                                                   \
    (*(volatile uint8_t *)(SRAM_BASE +                                         \
                           offsetof(sram_layout_t, board_animation_head)))
#define board_animation_tail                                                   \
    (*(uint8_t *)(SRAM_BASE + offsetof(sram_layout_t, board_animation_tail)))
#define board_animation_committed                                              \
    (*(volatile uint8_t *)(SRAM_BASE + offsetof(sram_layout_t,                 \
                                                board_animation_committed)))
#define board_animation_queue                                                  \
    ((volatile board_animation_entry_t *)(SRAM_BASE +                          \
                                          offsetof(sram_layout_t,              \
                                                   board_animation_queue)))
#define flood_deque                                                            \
    ((uint16_t *)(SRAM_BASE + offsetof(sram_layout_t, flood_deque)))
#define flood_visited                                                          \
    ((uint8_t *)(SRAM_BASE + offsetof(sram_layout_t, flood_visited)))

#endif /* MEMORY_H */
