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

    tile_queue_head = 0;
    tile_queue_tail = 0;
    tile_queue_committed = 0;

    cursor_init(game_cursor, g->width / 2, g->height / 2, g);

    /* ---- Display init ---- */

    display_init();
    board_redraw(g);
    interrupts_init(g->width, g->height);
    display_start();

    /* ---- Main loop ---- */

    while (1) {
        vsync();

        input_poll(game_input);

        if (game_input->pressed & J_A) {
            const color_t color = game_color_to_play(g);
            const uint16_t coord =
                BOARD_COORD(game_cursor->col, game_cursor->row);
            const move_legality_t result = game_play_move(g, coord, color);

            if (result == MOVE_LEGAL)
                DEBUG_LOG_MOVE(g, color, game_cursor->col, game_cursor->row);
            else
                DEBUG_LOG_ILLEGAL(result, color, game_cursor->col,
                                  game_cursor->row);
        }

        if (game_input->pressed & J_B) {
            if (game_undo(g) == UNDO_OK)
                DEBUG_LOG_UNDO(g);
        }

        cursor_update(game_cursor, game_input, g);
    }
}
