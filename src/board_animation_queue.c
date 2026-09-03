#include "memory.h"

/*
 * Reserve the queue in the link map.  The Makefile compiles this translation
 * unit into the page-aligned _BOARD_ANIMATION WRAM area.
 */
volatile board_animation_entry_t
    board_animation_queue_storage[BOARD_ANIMATION_QUEUE_MAX];
