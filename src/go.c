#include "go.h"
#include "layout.h"

#include <assert.h>
#include <string.h>

#ifndef NDEBUG
#include <gbdk/emu_debug.h>
#endif

/* Powers-of-two lookup table (avoids variable shifts on SM83). */
const uint8_t pow2[8] = {1, 2, 4, 8, 16, 32, 64, 128};

/* Unrolled four-neighbor iteration.  Each direction offset and bit
 * mask is a compile-time immediate — no loop counter, no table lookup.
 * Bodies must not use break/continue (restructure to if/else). */
#define FOR_EACH_NEIGHBOR(center, nb, body)                                    \
    do {                                                                       \
        {                                                                      \
            nb = (center) + DIR_UP;                                            \
            body                                                               \
        }                                                                      \
        {                                                                      \
            nb = (center) + DIR_DOWN;                                          \
            body                                                               \
        }                                                                      \
        {                                                                      \
            nb = (center) + DIR_LEFT;                                          \
            body                                                               \
        }                                                                      \
        {                                                                      \
            nb = (center) + DIR_RIGHT;                                         \
            body                                                               \
        }                                                                      \
    } while (0)

#define FOR_EACH_NEIGHBOR_DIR(center, nb, dir_bit, body)                       \
    do {                                                                       \
        {                                                                      \
            nb = (center) + DIR_UP;                                            \
            dir_bit = 1;                                                       \
            body                                                               \
        }                                                                      \
        {                                                                      \
            nb = (center) + DIR_DOWN;                                          \
            dir_bit = 2;                                                       \
            body                                                               \
        }                                                                      \
        {                                                                      \
            nb = (center) + DIR_LEFT;                                          \
            dir_bit = 4;                                                       \
            body                                                               \
        }                                                                      \
        {                                                                      \
            nb = (center) + DIR_RIGHT;                                         \
            dir_bit = 8;                                                       \
            body                                                               \
        }                                                                      \
    } while (0)

/* ---- Flood fill for liberty detection ---- */

/* Flood-fill the group containing `seed`, recording every stone in
 * flood_deque[0..tail-1].  Uses BFS to fully traverse the group
 * (no early-out) so that flood_visited stays complete across calls.
 * `stones` is the bitfield of the color to follow (black or white).
 * Precondition: seed must not already be in flood_visited.
 * Returns the number of liberties (capped at UINT8_MAX); 0 = captured.
 * After return, flood_deque[0..group_size-1] holds the group coords;
 * read the returned tail via the `group_size` output parameter. */
static uint8_t group_liberties(const game_t *g, uint16_t seed,
                               const uint8_t *stones, uint16_t *group_size) {
    assert(!BF_GET(flood_visited, seed) && "seed already visited");

    uint16_t head = 0;
    uint16_t tail = 0;
    uint8_t liberties = 0;

    BF_SET(flood_visited, seed);
    flood_deque[tail++] = seed;

    while (head < tail) {
        uint16_t pos = flood_deque[head++];
        uint16_t nb;
        FOR_EACH_NEIGHBOR(pos, nb, {
            if (!BF_GET(flood_visited, nb)) {
                if (BF_GET(stones, nb)) {
                    BF_SET(flood_visited, nb);
                    flood_deque[tail++] = nb;
                } else if (BF_GET(g->on_board, nb) &&
                           !BF_GET(g->black_stones, nb) &&
                           !BF_GET(g->white_stones, nb)) {
                    if (liberties < UINT8_MAX)
                        liberties++;
                }
            }
        });
    }

    *group_size = tail;
    return liberties;
}

/* Quick suicide test: does the group at `seed` have at least one liberty?
 * Returns 1 immediately on the first liberty found, 0 if captured.
 * Does not record group size.  May leave flood_visited incomplete,
 * so this must be the last flood operation for the current move. */
static uint8_t group_has_liberty(const game_t *g, uint16_t seed,
                                 const uint8_t *stones) {
    uint16_t head = 0;
    uint16_t tail = 0;

    BF_SET(flood_visited, seed);
    flood_deque[tail++] = seed;

    while (head < tail) {
        uint16_t pos = flood_deque[head++];
        uint16_t nb;
        FOR_EACH_NEIGHBOR(pos, nb, {
            if (!BF_GET(flood_visited, nb)) {
                if (BF_GET(stones, nb)) {
                    BF_SET(flood_visited, nb);
                    flood_deque[tail++] = nb;
                } else if (BF_GET(g->on_board, nb) &&
                           !BF_GET(g->black_stones, nb) &&
                           !BF_GET(g->white_stones, nb)) {
                    return 1;
                }
            }
        });
    }

    return 0;
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

/* Clear the first 3 bytes of each 4-byte bitfield row (21 rows).
 * Byte 3 of each row (columns 24-31) is never set by any board
 * operation and stays zero, so we skip it.  3x unrolled loop:
 * 7 iterations × 3 rows = 21 rows, ~196 cycles.
 *
 * __sdcccall(1): first 16-bit param arrives in DE. */
// clang-format off
static void bf_clear(uint8_t *p) __naked {
    (void)p;
    __asm
        ld l, e
        ld h, d
        xor a
        ld c, #7
    00180$:
        ld (hl+), a
        ld (hl+), a
        ld (hl+), a
        inc hl
        ld (hl+), a
        ld (hl+), a
        ld (hl+), a
        inc hl
        ld (hl+), a
        ld (hl+), a
        ld (hl+), a
        inc hl
        dec c
        jr NZ, 00180$
        ret
    __endasm;
}
// clang-format on

move_legality_t game_play_move(game_t *g, uint16_t coord, uint8_t color) {
    if (coord == g->ko)
        return MOVE_KO;

    uint8_t ci = BF_BYTE(coord);
    uint8_t cm = BF_MASK(coord);
    assert((g->on_board[ci] & cm) && "coord must be on board");
    if ((g->black_stones[ci] & cm) || (g->white_stones[ci] & cm))
        return MOVE_NON_EMPTY;

    bf_clear(flood_visited);

    uint8_t *own = (color == BLACK) ? g->black_stones : g->white_stones;
    uint8_t *opp = (color == BLACK) ? g->white_stones : g->black_stones;
    own[ci] |= cm;

    uint8_t move_hi = (uint8_t)((coord >> 8) | (color << (MOVE_COLOR_BIT - 8)));
    uint8_t captured_total = 0;
    uint8_t own_liberties = 0;

    uint16_t nb;
    uint8_t dir_bit;
    FOR_EACH_NEIGHBOR_DIR(coord, nb, dir_bit, {
        uint8_t bi = BF_BYTE(nb);
        uint8_t bm = BF_MASK(nb);
        if (opp[bi] & bm) {
            if (!(flood_visited[bi] & bm)) {
                uint16_t group_size;
                if (group_liberties(g, nb, opp, &group_size) == 0) {
                    for (uint16_t i = 0; i < group_size; i++) {
                        uint16_t cap = flood_deque[i];
                        BF_CLR(opp, cap);
                        vram_set_tile(cap, surface_tile(BOARD_COL(cap),
                                                        BOARD_ROW(cap),
                                                        g->width, g->height));
                    }
                    move_hi |= dir_bit << (MOVE_CAP_SHIFT - 8);
                    if (captured_total == 0 && group_size == 1)
                        captured_total = 1;
                    else
                        captured_total = 2;
                    own_liberties++;
                }
            }
        } else if (!(own[bi] & bm) && (g->on_board[bi] & bm)) {
            own_liberties++;
        }
    });

    if (captured_total == 0 && own_liberties == 0 &&
        !group_has_liberty(g, coord, own)) {
        own[ci] &= (uint8_t)~cm;
        return MOVE_SUICIDAL;
    }

    /* Clear previous ko marker tile. */
    if (g->ko != COORD_PASS) {
        vram_set_tile(g->ko, surface_tile(BOARD_COL(g->ko), BOARD_ROW(g->ko),
                                          g->width, g->height));
    }

    /* Ko detection: exactly one single-stone group captured and the
     * played stone has exactly one liberty and no own-color neighbors.
     * Reconstruct the ko position here rather than tracking it in the
     * hot loop.  goto early-outs when an own-color neighbor is found. */
    g->ko = COORD_PASS;
    if (captured_total == 1 && own_liberties == 1) {
        uint16_t ko = COORD_PASS;
        FOR_EACH_NEIGHBOR_DIR(coord, nb, dir_bit, {
            if (BF_GET(own, nb))
                goto ko_done;
            if (move_hi & (dir_bit << (MOVE_CAP_SHIFT - 8)))
                ko = nb;
        });
        g->ko = ko;
        move_hi |= (1 << (MOVE_KO_BIT - 8));
        vram_set_tile(g->ko, ko_tile(BOARD_COL(g->ko), BOARD_ROW(g->ko),
                                     g->width, g->height));
    }
ko_done:;

    /* Un-mark previous last-played stone. */
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

    vram_set_tile(coord, (color == BLACK) ? TILE_LAST_B : TILE_LAST_W);
    if (g->move_count >= g->history_base + HISTORY_MAX)
        g->history_base++;
    /* Reassemble the full move_t from the 8-bit high byte (flags + coord
     * upper bits) and the low byte of the original coordinate. */
    g->history[g->move_count++ % HISTORY_MAX] =
        ((move_t)move_hi << 8) | (uint8_t)coord;
    return MOVE_LEGAL;
}

undo_result_t game_undo(game_t *g) {
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
        uint16_t nb;
        uint8_t dir_bit;
        FOR_EACH_NEIGHBOR_DIR(coord, nb, dir_bit, {
            if (move & ((uint16_t)dir_bit << MOVE_CAP_SHIFT)) {
                uint16_t head = 0;
                uint16_t tail = 0;

                flood_deque[tail++] = nb;
                BF_SET(opp, nb);

                while (head < tail) {
                    uint16_t pos = flood_deque[head++];
                    vram_set_tile(pos, opp_tile);
                    uint16_t adj;
                    FOR_EACH_NEIGHBOR(pos, adj, {
                        if (BF_GET(g->on_board, adj) &&
                            !BF_GET(g->black_stones, adj) &&
                            !BF_GET(g->white_stones, adj)) {
                            BF_SET(opp, adj);
                            flood_deque[tail++] = adj;
                        }
                    });
                }
            }
        });

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
            uint16_t nb;
            uint8_t dir_bit;
            FOR_EACH_NEIGHBOR_DIR(prev_coord, nb, dir_bit, {
                if (prev & ((uint16_t)dir_bit << MOVE_CAP_SHIFT))
                    g->ko = nb;
            });
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
        pos += DIR_DOWN;
    }
}
#endif
