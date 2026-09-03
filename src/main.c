#include <gb/gb.h>
#include <string.h>

#include "cursor.h"
#include "display.h"
#include "go.h"
#include "go_debug.h"
#include "go_draw.h"
#include "input.h"
#include "interrupts.h"
#include "memory.h"

void main(void) {
    DISPLAY_OFF;
    ENABLE_RAM;

    /* ---- State init ---- */

    game_t *const g = game_state;
    game_reset(g, 19, 19, 13);
    memset(game_input, 0, sizeof(input_t));

    game_action_pending = 0;
    game_action_busy = 0;
    game_action_coord = COORD_PASS;

    board_animation_head = 0;
    board_animation_tail = 0;
    board_animation_committed = 0;

    cursor_init(g->width / 2, g->height / 2);

    /* ---- Display init ---- */

    display_init();
    board_redraw(g);
    gameplay_interrupts_init(g->width, g->height);
    display_start();

    /* ---- Main loop ---- */

    while (1) {
        uint8_t actions;
        uint16_t coord = COORD_PASS;

        vsync();

        actions = game_action_pending;
        if (actions != 0) {
            /*
             * Keep pending set while reading its payload, then close the
             * producer with busy before releasing the mailbox.
             */
            coord = game_action_coord;
            game_action_busy = 1;
            game_action_pending = 0;
        }

        if (actions & J_A) {
            const color_t color = game_color_to_play(g);
            const move_legality_t result = game_play_move(g, coord, color);
            const uint8_t col = BOARD_COL(coord);
            const uint8_t row = BOARD_ROW(coord);

            if (result == MOVE_LEGAL)
                DEBUG_LOG_MOVE(g, color, col, row);
            else
                DEBUG_LOG_ILLEGAL(result, color, col, row);
        }

        if (actions & J_B) {
            if (game_undo(g) == UNDO_OK)
                DEBUG_LOG_UNDO(g);
        }

        if (actions != 0) {
            /*
             * An action ends once all its commands are published, not once
             * their animation drains.  The next action may queue behind it;
             * producer reservations apply backpressure when space is low.
             */
            game_action_busy = 0;
        }
    }
}
