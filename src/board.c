#include "board.h"

#include <assert.h>
#include <string.h>

void board_reset(board_t *b, uint8_t width, uint8_t height) {
    assert(width  >= BOARD_MIN_SIZE && width  <= BOARD_MAX_SIZE && "width out of range");
    assert(height >= BOARD_MIN_SIZE && height <= BOARD_MAX_SIZE && "height out of range");

    b->width  = width;
    b->height = height;

    memset(b->on_board,     0, BOARD_FIELD_BYTES);
    memset(b->black_stones, 0, BOARD_FIELD_BYTES);
    memset(b->white_stones, 0, BOARD_FIELD_BYTES);

    /* Mark every coordinate inside the board area.
     * BOARD_COORD(0, 0) is a compile-time constant (no runtime multiply).
     * We stride by BOARD_MAX_EXTENT per row (one add) and increment per
     * column, avoiding any multiplication in the loop. */
    uint16_t pos = BOARD_COORD(0, 0);
    for (uint8_t row = 0; row < height; row++) {
        uint16_t p = pos;
        for (uint8_t col = 0; col < width; col++) {
            BF_SET(b->on_board, p);
            p++;
        }
        pos += BOARD_MAX_EXTENT;
    }
}
