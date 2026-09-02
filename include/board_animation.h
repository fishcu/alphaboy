#ifndef BOARD_ANIMATION_H
#define BOARD_ANIMATION_H

#include <assert.h>
#include <stdint.h>

#include "display.h"
#include "memory.h"

/*
 * The upper tile byte bit ends the current visual animation step.  VBlank
 * drains all preceding commands in the same frame, then yields after this
 * command.  Tile indices currently occupy only the lower seven bits.
 */
#define BOARD_ANIMATION_FRAME_END 0x80u
#define BOARD_ANIMATION_TILE_MASK 0x7Fu

/* Keep streamed, uncommitted chunks comfortably below ring capacity. */
#define BOARD_ANIMATION_STREAM_CHUNK 8u

_Static_assert(TILE_COUNT <= BOARD_ANIMATION_FRAME_END,
               "tile index collides with animation flag");
_Static_assert((BOARD_ANIMATION_QUEUE_MAX & (BOARD_ANIMATION_QUEUE_MAX - 1u)) ==
                   0,
               "animation queue capacity must be a power of two");
_Static_assert(BOARD_ANIMATION_STREAM_CHUNK < BOARD_ANIMATION_QUEUE_MAX - 1u,
               "stream chunk must leave committed queue space");
_Static_assert(BOARD_ANIMATION_MAX_WRITES_PER_FRAME >= 4u,
               "VBlank budget must fit the largest immediate group");

inline uint8_t board_animation_next(uint8_t index) {
    return (uint8_t)((index + 1u) & (BOARD_ANIMATION_QUEUE_MAX - 1u));
}

inline uint8_t board_animation_free(void) {
    return (uint8_t)((board_animation_head - board_animation_tail - 1u) &
                     (BOARD_ANIMATION_QUEUE_MAX - 1u));
}

/*
 * Reserve bounded speculative space before mutating game state.  The
 * consumer may only increase available space while this producer waits.
 */
inline void board_animation_wait_for(uint8_t count) {
    assert(count < BOARD_ANIMATION_QUEUE_MAX &&
           "reservation must fit animation queue");
    while (board_animation_free() < count) {
    }
}

/* Append one uncommitted command. */
inline void board_animation_push(uint16_t pc, uint8_t tile) {
    const uint8_t tail = board_animation_tail;
    const uint8_t next = board_animation_next(tail);
    while (next == board_animation_head) {
    }
    board_animation_queue[tail].pc = pc;
    board_animation_queue[tail].tile = tile;
    board_animation_tail = next;
}

/* Append one command which ends the current animation step. */
inline void board_animation_push_paced(uint16_t pc, uint8_t tile) {
    board_animation_push(pc, tile | BOARD_ANIMATION_FRAME_END);
}

/* End the frame after the most recently staged command. */
inline void board_animation_end_frame(void) {
    const uint8_t last =
        (board_animation_tail - 1u) & (BOARD_ANIMATION_QUEUE_MAX - 1u);
    board_animation_queue[last].tile |= BOARD_ANIMATION_FRAME_END;
}

/* Make all staged commands visible to VBlank. */
inline void board_animation_commit(void) {
    board_animation_committed = board_animation_tail;
}

/* Discard all staged commands. */
inline void board_animation_rewind(void) {
    board_animation_tail = board_animation_committed;
}

/*
 * Stream finalized board changes without allowing an uncommitted chunk to
 * fill the ring.  Call board_animation_stream_flush() at the end.
 */
inline void board_animation_stream_push(uint16_t pc, uint8_t tile,
                                        uint8_t *pending) {
    board_animation_push_paced(pc, tile);
    (*pending)++;
    if (*pending == BOARD_ANIMATION_STREAM_CHUNK) {
        board_animation_commit();
        *pending = 0;
    }
}

inline void board_animation_stream_flush(uint8_t *pending) {
    if (*pending != 0) {
        board_animation_commit();
        *pending = 0;
    }
}

#endif /* BOARD_ANIMATION_H */
