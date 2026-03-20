#include "go_debug.h"

#ifndef NDEBUG

#include <gbdk/emu_debug.h>

static const char *const legality_reasons[] = {"legal", "non-empty", "suicidal",
                                               "ko"};

void game_debug_log_move(const game_t *g, color_t color, uint8_t col,
                         uint8_t row) {
    EMU_printf("Move %u: %s at (%hu,%hu)\n", (unsigned)g->move_count,
               (color == COLOR_BLACK) ? "B" : "W", col, row);
    game_debug_print(g);
}

void game_debug_log_illegal(move_legality_t result, color_t color, uint8_t col,
                            uint8_t row) {
    EMU_printf("Illegal (%s): %s at (%hu,%hu)\n", legality_reasons[result],
               (color == COLOR_BLACK) ? "B" : "W", col, row);
}

void game_debug_log_undo(const game_t *g) {
    EMU_printf("Undo -> move_count=%u\n", (unsigned)g->move_count);
    game_debug_print(g);
}

void game_debug_print(const game_t *g) {
    const uint8_t w = g->width;
    const uint8_t h = g->height;
    uint16_t pos = BOARD_COORD(0, 0);
    char row_str[BOARD_MAX_SIZE * 2];

    EMU_printf("Board %hux%hu\n", (uint8_t)w, (uint8_t)h);

    for (uint8_t row = 0; row < h; row++) {
        uint16_t p = pos;
        uint8_t idx = 0;
        for (uint8_t col = 0; col < w; col++) {
            if (col > 0)
                row_str[idx++] = ' ';
            switch (g->board[p]) {
            case COLOR_BLACK:
                row_str[idx++] = 'X';
                break;
            case COLOR_WHITE:
                row_str[idx++] = 'O';
                break;
            default:
                row_str[idx++] = '.';
                break;
            }
            p++;
        }
        row_str[idx] = '\0';
        EMU_printf("%s\n", row_str);
        pos += DIR_DOWN;
    }
}

#endif /* NDEBUG */
