#include "go.h"
#include "layout.h"

#include <assert.h>
#include <string.h>

#ifndef NDEBUG
#include <gbdk/emu_debug.h>
#endif

/* Bit-field mask lookup table. */
const uint8_t bf_masks[8] = {1, 2, 4, 8, 16, 32, 64, 128};

/* Four cardinal neighbor offsets in the packed coordinate space. */
static const int16_t dirs[4] = {DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT};

/* ---- Flood fill for liberty detection ---- */

/* Flood-fill the group containing `seed`, recording every stone in
 * `queue[0..*group_size-1]`.  Uses BFS to fully traverse the group
 * (no early-out) so that `visited` stays complete across calls.
 * `stones` is the bitfield of the color to follow (black or white).
 * Precondition: seed must not already be in `visited`.
 * Returns the number of liberties (capped at UINT8_MAX); 0 = captured. */
static uint8_t group_liberties(const game_t *g, uint16_t seed,
                               const uint8_t *stones, uint8_t *visited,
                               uint16_t *queue, uint16_t *group_size) {
    assert(!BF_GET(visited, seed) && "seed already visited");

    uint16_t head = 0;
    uint16_t tail = 0;
    uint8_t liberties = 0;

    BF_SET(visited, seed);
    queue[tail++] = seed;

    while (head < tail) {
        uint16_t pos = queue[head++];
        for (uint8_t d = 0; d < 4; d++) {
            uint16_t nb = pos + dirs[d];
            uint8_t bi = BF_BYTE(nb);
            uint8_t bm = BF_MASK(nb);
            if (visited[bi] & bm)
                continue;
            if (stones[bi] & bm) {
                visited[bi] |= bm;
                queue[tail++] = nb;
            } else if ((g->on_board[bi] & bm) && !(g->black_stones[bi] & bm) &&
                       !(g->white_stones[bi] & bm)) {
                if (liberties < UINT8_MAX)
                    liberties++;
            }
        }
    }

    *group_size = tail;
    return liberties;
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
    g->history_base = 0;

    memset(g->on_board, 0, BOARD_FIELD_BYTES);
    memset(g->black_stones, 0, BOARD_FIELD_BYTES);
    memset(g->white_stones, 0, BOARD_FIELD_BYTES);

    /* Mark every coordinate inside the board area.
     * BOARD_COORD(0, 0) is a compile-time constant (no runtime multiply).
     * We stride by DIR_DOWN per row (one add) and increment per column,
     * avoiding any multiplication in the loop. */
    uint16_t pos = BOARD_COORD(0, 0);
    for (uint8_t row = 0; row < height; row++) {
        for (uint8_t col = 0; col < width; col++) {
            BF_SET(g->on_board, pos);
            pos++;
        }
        pos += DIR_DOWN - width;
    }
}

/* ---- Play a move ---- */

void game_play_pass(game_t *g, uint8_t color) {
    if (g->ko != COORD_PASS) {
        vram_set_tile(g->ko, surface_tile(BOARD_COL(g->ko), BOARD_ROW(g->ko),
                                          g->width, g->height));
    }
    g->ko = COORD_PASS;
    if (g->move_count >= g->history_base + HISTORY_MAX)
        g->history_base++;
    g->history[g->move_count++ % HISTORY_MAX] = MOVE_MAKE(COORD_PASS, color);
}

move_legality_t game_play_move(game_t *g, uint8_t col, uint8_t row,
                               uint8_t color, uint16_t *queue,
                               uint8_t *visited) {
    uint16_t coord = BOARD_COORD(col, row);

    /* Ko check. */
    if (coord == g->ko)
        return MOVE_KO;

    /* Must be an empty on-board intersection. */
    uint8_t ci = BF_BYTE(coord);
    uint8_t cm = BF_MASK(coord);
    if (!(g->on_board[ci] & cm) || (g->black_stones[ci] & cm) ||
        (g->white_stones[ci] & cm))
        return MOVE_NON_EMPTY;

    /* Place the stone in the bitfield (required for correct liberty
     * counting).  The VRAM tile write is deferred until the move is
     * confirmed legal, so suicidal moves never flash on screen. */
    uint8_t *own = (color == BLACK) ? g->black_stones : g->white_stones;
    uint8_t *opp = (color == BLACK) ? g->white_stones : g->black_stones;
    own[ci] |= cm;

    /* Clear visited once for all flood fills this move. */
    memset(visited, 0, BOARD_FIELD_BYTES);

    /* Single pass over the four neighbors: capture opponent groups,
     * and classify each neighbor for ko / suicide detection. */
    move_t move = MOVE_MAKE(coord, color);
    uint16_t captured_total = 0;
    uint16_t captured_at = COORD_PASS;
    uint16_t group_size;
    uint8_t is_single = 1;
    uint8_t own_liberties = 0;

    for (uint8_t d = 0; d < 4; d++) {
        uint16_t nb = coord + dirs[d];
        uint8_t bi = BF_BYTE(nb);
        uint8_t bm = BF_MASK(nb);

        if ((opp[bi] & bm) && !(visited[bi] & bm)) {
            if (group_liberties(g, nb, opp, visited, queue, &group_size) == 0) {
                for (uint16_t i = 0; i < group_size; i++) {
                    uint16_t cap = queue[i];
                    BF_CLR(opp, cap);
                    vram_set_tile(cap,
                                  surface_tile(BOARD_COL(cap), BOARD_ROW(cap),
                                               g->width, g->height));
                }
                captured_total += group_size;
                captured_at = queue[0];
                move |= (1u << (MOVE_CAP_SHIFT + d));
            }
        }

        if (own[bi] & bm)
            is_single = 0;
        else if ((g->on_board[bi] & bm) && !(opp[bi] & bm))
            own_liberties++;
    }

    /* Suicide: only possible when nothing was captured and the played
     * stone has no immediate liberty.  Checked before touching g->ko
     * so an illegal attempt leaves the ko state undisturbed. */
    if (captured_total == 0 && own_liberties == 0 &&
        group_liberties(g, coord, own, visited, queue, &group_size) == 0) {
        own[ci] &= (uint8_t)~cm;
        return MOVE_SUICIDAL;
    }

    /* Clear previous ko marker tile. */
    if (g->ko != COORD_PASS) {
        vram_set_tile(g->ko, surface_tile(BOARD_COL(g->ko), BOARD_ROW(g->ko),
                                          g->width, g->height));
    }

    /* Set new ko state and tile. */
    if (captured_total == 1 && is_single && own_liberties == 1) {
        g->ko = captured_at;
        move |= (1u << MOVE_KO_BIT);
        vram_set_tile(g->ko, ko_tile(BOARD_COL(g->ko), BOARD_ROW(g->ko),
                                     g->width, g->height));
    } else {
        g->ko = COORD_PASS;
    }

    /* Move is legal — un-mark previous last-played stone. */
    if (g->move_count > g->history_base) {
        move_t prev = g->history[(g->move_count - 1) % HISTORY_MAX];
        uint16_t pc = MOVE_COORD(prev);
        if (pc != COORD_PASS) {
            uint8_t prev_color = MOVE_COLOR(prev);
            uint8_t *prev_field =
                (prev_color == BLACK) ? g->black_stones : g->white_stones;
            if (BF_GET(prev_field, pc)) {
                vram_set_tile(pc, (prev_color == BLACK) ? TILE_STONE_B
                                                        : TILE_STONE_W);
            }
        }
    }

    /* Commit the new stone with last-played marker. */
    vram_set_tile(coord, (color == BLACK) ? TILE_LAST_B : TILE_LAST_W);
    if (g->move_count >= g->history_base + HISTORY_MAX)
        g->history_base++;
    g->history[g->move_count++ % HISTORY_MAX] = move;
    return MOVE_LEGAL;
}

undo_result_t game_undo(game_t *g, uint16_t *queue) {
    if (g->move_count <= g->history_base)
        return UNDO_NO_HISTORY;
    if (g->history_base > 0 && g->move_count <= g->history_base + 1)
        return UNDO_NO_HISTORY;

    g->move_count--;
    move_t move = g->history[g->move_count % HISTORY_MAX];
    uint16_t coord = MOVE_COORD(move);

    if (coord != COORD_PASS) {
        uint8_t color = MOVE_COLOR(move);
        uint8_t *own = (color == BLACK) ? g->black_stones : g->white_stones;
        uint8_t *opp = (color == BLACK) ? g->white_stones : g->black_stones;
        uint8_t opp_tile = (color == BLACK) ? TILE_STONE_W : TILE_STONE_B;

        /* Restore captured groups by flood-filling through empties.
         * Each captured group's empty region is fully enclosed by the
         * capturing player's stones and the board edge, so a BFS from
         * the capture-direction neighbor recovers exactly the group. */
        for (uint8_t d = 0; d < 4; d++) {
            if (!(move & (1u << (MOVE_CAP_SHIFT + d))))
                continue;

            uint16_t nb = coord + dirs[d];
            uint16_t head = 0;
            uint16_t tail = 0;

            queue[tail++] = nb;
            BF_SET(opp, nb);

            while (head < tail) {
                uint16_t pos = queue[head++];
                vram_set_tile(pos, opp_tile);
                for (uint8_t dd = 0; dd < 4; dd++) {
                    uint16_t adj = pos + dirs[dd];
                    uint8_t bi = BF_BYTE(adj);
                    uint8_t bm = BF_MASK(adj);
                    if (!(g->on_board[bi] & bm))
                        continue;
                    if ((g->black_stones[bi] & bm) ||
                        (g->white_stones[bi] & bm))
                        continue;
                    opp[bi] |= bm;
                    queue[tail++] = adj;
                }
            }
        }

        /* Remove the played stone. */
        BF_CLR(own, coord);
        vram_set_tile(coord, surface_tile(BOARD_COL(coord), BOARD_ROW(coord),
                                          g->width, g->height));
    }

    /* Restore ko state.  The early-out above guarantees that when
     * move_count > 0 the previous history entry is still valid. */
    if (g->move_count == 0) {
        g->ko = COORD_PASS;
    } else {
        move_t prev = g->history[(g->move_count - 1) % HISTORY_MAX];
        if (prev & (1u << MOVE_KO_BIT)) {
            uint16_t prev_coord = MOVE_COORD(prev);
            for (uint8_t d = 0; d < 4; d++) {
                if (prev & (1u << (MOVE_CAP_SHIFT + d))) {
                    g->ko = prev_coord + dirs[d];
                    break;
                }
            }
        } else {
            g->ko = COORD_PASS;
        }
    }

    /* Write ko tile if the restored state has an active ko. */
    if (g->ko != COORD_PASS) {
        vram_set_tile(g->ko, ko_tile(BOARD_COL(g->ko), BOARD_ROW(g->ko),
                                     g->width, g->height));
    }

    /* Mark the now-current last move as last-played. */
    if (g->move_count > g->history_base) {
        move_t last = g->history[(g->move_count - 1) % HISTORY_MAX];
        uint16_t lc = MOVE_COORD(last);
        if (lc != COORD_PASS) {
            vram_set_tile(lc, (MOVE_COLOR(last) == BLACK) ? TILE_LAST_B
                                                          : TILE_LAST_W);
        }
    }

    return UNDO_OK;
}

uint8_t game_color_to_play(const game_t *g) {
    if (g->move_count == 0)
        return BLACK;
    return COLOR_OPPOSITE(
        MOVE_COLOR(g->history[(g->move_count - 1) % HISTORY_MAX]));
}

uint8_t game_can_play_approx(const game_t *g, uint8_t col, uint8_t row) {
    uint16_t coord = BOARD_COORD(col, row);
    if (coord == g->ko)
        return 0;
    uint8_t bi = BF_BYTE(coord);
    uint8_t bm = BF_MASK(coord);
    if ((g->black_stones[bi] & bm) || (g->white_stones[bi] & bm))
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
        pos += DIR_DOWN;
    }
}
#endif
