#include "go.h"
#include "layout.h"

#include <assert.h>
#include <string.h>

#ifndef NDEBUG
#include <gbdk/emu_debug.h>
#endif

/* Bit-field mask lookup table. */
const uint8_t bf_masks[8] = {1, 2, 4, 8, 16, 32, 64, 128};

/* Four cardinal neighbor offsets in the padded grid. */
static const int16_t dirs[4] = {DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT};

/* ---- Flood fill for liberty detection ---- */

/* Flood-fill the group containing `seed`, recording every stone in
 * `queue[0..*group_size-1]`.  Uses BFS to fully traverse the group
 * (no early-out) so that `visited` stays complete across calls.
 * `stones` is the bitfield of the color to follow (black or white).
 * Precondition: seed must not already be in `visited`.
 * Returns 1 if captured (no liberties), 0 if alive. */
static uint8_t flood_fill_captured(const game_t *g, uint16_t seed,
                                   const uint8_t *stones, uint8_t *visited,
                                   uint16_t *queue, uint16_t *group_size) {
    assert(!BF_GET(visited, seed) && "seed already visited");

    uint16_t head = 0;
    uint16_t tail = 0;
    uint8_t has_liberty = 0;

    queue[tail++] = seed;
    BF_SET(visited, seed);

    while (head < tail) {
        uint16_t pos = queue[head++];

        for (uint8_t d = 0; d < 4; d++) {
            uint16_t nb = pos + dirs[d];

            if (BF_GET(visited, nb))
                continue;

            if (BF_GET(stones, nb)) {
                BF_SET(visited, nb);
                queue[tail++] = nb;
                continue;
            }

            /* Empty on-board neighbor = liberty. */
            if (!has_liberty && BF_GET(g->on_board, nb) &&
                !BF_GET(g->black_stones, nb) && !BF_GET(g->white_stones, nb))
                has_liberty = 1;
        }
    }

    *group_size = tail;
    return !has_liberty;
}

void game_reset(game_t *g, uint8_t width, uint8_t height, int8_t komi2) {
    assert(width >= BOARD_MIN_SIZE && width <= BOARD_MAX_SIZE &&
           "width out of range");
    assert(height >= BOARD_MIN_SIZE && height <= BOARD_MAX_SIZE &&
           "height out of range");

    g->width = width;
    g->height = height;
    g->komi2 = komi2;
    g->ko = COORD_PASS;
    g->move_count = 0;

    memset(g->on_board, 0, BOARD_FIELD_BYTES);
    memset(g->black_stones, 0, BOARD_FIELD_BYTES);
    memset(g->white_stones, 0, BOARD_FIELD_BYTES);

    /* Mark every coordinate inside the board area.
     * BOARD_COORD(0, 0) is a compile-time constant (no runtime multiply).
     * We stride by BOARD_MAX_EXTENT per row (one add) and increment per
     * column, avoiding any multiplication in the loop. */
    uint16_t pos = BOARD_COORD(0, 0);
    for (uint8_t row = 0; row < height; row++) {
        uint16_t p = pos;
        for (uint8_t col = 0; col < width; col++) {
            BF_SET(g->on_board, p);
            p++;
        }
        pos += BOARD_MAX_EXTENT;
    }
}

/* ---- Play a move ---- */

/* Remove captured stones listed in `queue[0..group_size-1]`.
 * Clears each stone from `opp`, writes the appropriate surface tile
 * to VRAM, and accumulates the capture count/position for ko. */
static void remove_captured(game_t *g, uint8_t *opp, const uint16_t *queue,
                            uint16_t group_size, uint16_t *captured_total,
                            uint16_t *captured_at) {
    for (uint16_t i = 0; i < group_size; i++) {
        uint16_t pos = queue[i];
        BF_CLR(opp, pos);
        uint8_t row = pos / BOARD_MAX_EXTENT - BOARD_MARGIN;
        uint8_t col = pos % BOARD_MAX_EXTENT - BOARD_MARGIN;
        vram_set_tile(col, row, surface_tile(col, row, g->width, g->height));
    }
    *captured_total += group_size;
    *captured_at = queue[0];
}

move_legality_t game_play_move(game_t *g, uint8_t col, uint8_t row,
                               uint8_t color, uint16_t *queue,
                               uint8_t *visited) {
    uint16_t coord = BOARD_COORD(col, row);

    /* Pass is always legal. */
    /* Currently unreachable, will refactor later */
    if (coord == COORD_PASS) {
        g->ko = COORD_PASS;
        g->history[g->move_count++] = MOVE_MAKE(COORD_PASS, color);
        return MOVE_LEGAL;
    }

    /* Ko check. */
    if (coord == g->ko)
        return MOVE_KO;

    /* Must be an empty on-board intersection. */
    if (!BF_GET(g->on_board, coord) || BF_GET(g->black_stones, coord) ||
        BF_GET(g->white_stones, coord))
        return MOVE_NON_EMPTY;

    /* Place the stone in the bitfield (required for correct liberty
     * counting).  The VRAM tile write is deferred until the move is
     * confirmed legal, so suicidal moves never flash on screen. */
    uint8_t *own = (color == BLACK) ? g->black_stones : g->white_stones;
    uint8_t *opp = (color == BLACK) ? g->white_stones : g->black_stones;
    BF_SET(own, coord);

    /* Clear visited once for all flood fills this move. */
    memset(visited, 0, BOARD_FIELD_BYTES);

    /* Check each adjacent opponent group for captures. */
    uint16_t captured_total = 0;
    uint16_t captured_at = COORD_PASS;
    uint16_t group_size;

    for (uint8_t d = 0; d < 4; d++) {
        uint16_t nb = coord + dirs[d];

        if (!BF_GET(opp, nb))
            continue;

        /* Skip groups already explored by a prior direction. */
        if (BF_GET(visited, nb))
            continue;

        if (!flood_fill_captured(g, nb, opp, visited, queue, &group_size))
            continue;

        remove_captured(g, opp, queue, group_size, &captured_total,
                        &captured_at);
    }

    /* Ko: exactly one stone captured → record its position. */
    g->ko = (captured_total == 1) ? captured_at : COORD_PASS;

    /* Suicide check: if nothing was captured, the placed stone's own
     * group must have at least one liberty.  Visited is not cleared —
     * own and opponent stones are exclusive sets, so opponent marks
     * in visited cannot interfere with the own-color flood. */
    if (captured_total == 0 &&
        flood_fill_captured(g, coord, own, visited, queue, &group_size)) {
        BF_CLR(own, coord);
        return MOVE_SUICIDAL;
    }

    /* Move is legal — commit the stone tile to VRAM. */
    vram_set_tile(col, row, (color == BLACK) ? TILE_STONE_B : TILE_STONE_W);
    g->history[g->move_count++] = MOVE_MAKE(coord, color);
    return MOVE_LEGAL;
}

uint8_t game_color_to_play(const game_t *g) {
    if (g->move_count == 0)
        return BLACK;
    return COLOR_OPPOSITE(MOVE_COLOR(g->history[g->move_count - 1]));
}

uint8_t game_can_play_approx(const game_t *g, uint8_t col, uint8_t row) {
    uint16_t coord = BOARD_COORD(col, row);
    if (coord == g->ko)
        return 0;
    if (BF_GET(g->black_stones, coord) || BF_GET(g->white_stones, coord))
        return 0;
    return 1;
}

#ifndef NDEBUG
void game_debug_print(const game_t *g) {
    uint8_t w = g->width;
    uint8_t h = g->height;
    uint16_t pos = BOARD_COORD(0, 0);
    /* Worst case: 19 chars + 18 spaces + null = 38 bytes. */
    char row_str[BOARD_MAX_SIZE * 2];

    EMU_printf("Board %hux%hu\n", (uint8_t)w, (uint8_t)h);

    for (uint8_t row = 0; row < h; row++) {
        uint16_t p = pos;
        uint8_t idx = 0;
        for (uint8_t col = 0; col < w; col++) {
            if (col > 0)
                row_str[idx++] = ' ';
            if (BF_GET(g->black_stones, p))
                row_str[idx++] = 'X';
            else if (BF_GET(g->white_stones, p))
                row_str[idx++] = 'O';
            else
                row_str[idx++] = '.';
            p++;
        }
        row_str[idx] = '\0';
        EMU_printf("%s\n", row_str);
        pos += BOARD_MAX_EXTENT;
    }
}
#endif
