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
 *   0x9800-0x9BFF  BG Map 0 (_SCRN0)         (1 KB, hardware-fixed)
 *   0x9C00-0x9FFF  BG Map 1 (_SCRN1)         (1 KB, hardware-fixed)
 *
 * SRAM (0xA000-0xBFFF, 8 KB, MBC5 + RAM + Battery):
 *   Manually managed; allocations listed below.
 *   Persistent logical game state lives here.
 *
 * WRAM (0xC000-0xDFFF, 8 KB):
 *   0xC000-0xC09F  GBDK shadow OAM (160 bytes)
 *   0xC0A0-0xC0FF  Free alignment gap (96 bytes)
 *   0xC100-0xC17F  Board animation queue (128 bytes)
 *   0xC180-0xDEFF  C runtime data and free WRAM
 *   0xDF00-0xDFFF  Reserved stack budget (256 bytes)
 *
 * The queue occupies the named linker area _BOARD_ANIMATION.  Automatic
 * _DATA placement starts after it, while romusage verifies at build time that
 * linker areas do not overlap shadow OAM or the reserved stack budget.
 *
 * Tile allocation at 0x8000:
 *   0 .. TILE_COUNT-1  = png2asset tiles (see display.h)
 *   TILE_COUNT..255    = free
 */

/* ---- Board animation queue (page-aligned WRAM linker area) ----
 * Game logic appends ordered BG-map changes here.  The VBlank ISR applies
 * commands until an animation-step boundary is reached, keeping all VRAM
 * writes inside VBlank.  The committed frontier hides speculative move
 * prefixes until legality is known.
 *
 * See board_animation.h for the producer-side inline helpers. */
#define BOARD_ANIMATION_QUEUE_MAX 32u /* must be power of 2 */
#define BOARD_ANIMATION_MAX_WRITES_PER_FRAME 4u
#ifndef BOARD_ANIMATION_QUEUE_BASE
#define BOARD_ANIMATION_QUEUE_BASE 0xC100u
#endif

typedef struct board_animation_entry {
    uint8_t tile;
    uint16_t destination;
    uint8_t padding; /* keeps index-to-address conversion in eight bits */
} board_animation_entry_t;

_Static_assert((BOARD_ANIMATION_QUEUE_BASE & 0x00FFu) == 0,
               "animation queue must start on a page boundary");

#if defined(__SDCC)
_Static_assert(sizeof(board_animation_entry_t) == 4u,
               "animation entries must use a four-byte stride");
_Static_assert(offsetof(board_animation_entry_t, tile) == 0u &&
                   offsetof(board_animation_entry_t, destination) == 1u,
               "animation entry layout must support sequential reads");
_Static_assert(sizeof(board_animation_entry_t) * BOARD_ANIMATION_QUEUE_MAX <=
                   0x80u,
               "animation queue exceeds its reserved WRAM page");
#endif

/* ------------------------------------------------------------------ */
/*  SRAM object layout                                                */
/* ------------------------------------------------------------------ */

#define SRAM_BASE 0xA000u

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
    ((volatile board_animation_entry_t *)BOARD_ANIMATION_QUEUE_BASE)
#define flood_deque                                                            \
    ((uint16_t *)(SRAM_BASE + offsetof(sram_layout_t, flood_deque)))
#define flood_visited                                                          \
    ((uint8_t *)(SRAM_BASE + offsetof(sram_layout_t, flood_visited)))

#endif /* MEMORY_H */
