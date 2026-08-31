#include <gb/gb.h>
#include <string.h>

#include "cursor.h"
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
    memset(game_input, 0, sizeof(input_t));

    game_action_pending = 0;
    game_action_busy = 1;
    game_action_coord = COORD_PASS;

    board_animation_head = 0;
    board_animation_tail = 0;
    board_animation_committed = 0;

    cursor_init(game_cursor, g->width / 2, g->height / 2, g);
    display_init();
    board_redraw(g);
    gameplay_interrupts_init(g->width, g->height);
    display_start();

    while (1) {
        vsync();
        go_replay_step(g);
    }
}
