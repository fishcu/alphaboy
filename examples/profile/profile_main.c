#include <gb/gb.h>
#include <string.h>

#include "display.h"
#include "go.h"
#include "go_draw.h"
#include "go_replay.h"
#include "interrupts.h"
#include "memory.h"

void main(void) {
    DISPLAY_OFF;
    ENABLE_RAM;

    game_t *const g = game_state;
    game_reset(g, 19, 19, 13);

    tile_queue_head = 0;
    tile_queue_tail = 0;
    tile_queue_committed = 0;

    display_init();
    board_redraw(g);
    interrupts_init(g->width, g->height);
    display_start();

    while (1) {
        vsync();
        go_replay_step(g);
    }
}
