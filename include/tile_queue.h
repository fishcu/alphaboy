#ifndef TILE_QUEUE_H
#define TILE_QUEUE_H

#include "memory.h"

/* Push a tile update onto the deferred queue.  The VBlank ISR drains
 * committed entries to VRAM each frame.  Busy-waits if the queue is full.
 * No critical section needed: single-producer single-consumer ring
 * buffer  --  the ISR only touches head, the producer only touches tail. */
static inline void tile_push(uint16_t pc, uint8_t tile) {
    const uint8_t t = tile_queue_tail;
    const uint8_t next = (t + 1) % TILE_QUEUE_MAX;
    while (next == tile_queue_head) {
    }
    tile_queue[t].pc = pc;
    tile_queue[t].tile = tile;
    tile_queue_tail = next;
}

/* Make all queued tile pushes visible to the VBlank ISR for draining. */
static inline void tile_commit(void) { tile_queue_committed = tile_queue_tail; }

/* Discard all uncommitted tile pushes (rewind tail to committed). */
static inline void tile_rewind(void) { tile_queue_tail = tile_queue_committed; }

#endif /* TILE_QUEUE_H */
