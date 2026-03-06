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
 * `stone_color` is the color to follow (COLOR_BLACK or COLOR_WHITE).
 * Precondition: seed must not already be in flood_visited.
 * Returns the number of liberties (capped at UINT8_MAX); 0 = captured.
 * After return, flood_deque[0..group_size-1] holds the group coords;
 * read the returned tail via the `group_size` output parameter. */
static uint8_t group_liberties(const game_t *g, uint16_t seed,
                               uint8_t stone_color, uint16_t *group_size) {
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
                uint8_t cell = g->board[nb];
                if (cell == stone_color) {
                    BF_SET(flood_visited, nb);
                    flood_deque[tail++] = nb;
                } else if (cell == COLOR_EMPTY) {
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
                                 uint8_t stone_color) {
    uint16_t head = 0;
    uint16_t tail = 0;

    BF_SET(flood_visited, seed);
    flood_deque[tail++] = seed;

    while (head < tail) {
        uint16_t pos = flood_deque[head++];
        uint16_t nb;
        FOR_EACH_NEIGHBOR(pos, nb, {
            if (!BF_GET(flood_visited, nb)) {
                uint8_t cell = g->board[nb];
                if (cell == stone_color) {
                    BF_SET(flood_visited, nb);
                    flood_deque[tail++] = nb;
                } else if (cell == COLOR_EMPTY) {
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

    memset(g->board, COLOR_OFF_BOARD, BOARD_CELLS);

    uint16_t pos = BOARD_COORD(0, 0);
    for (uint8_t row = 0; row < height; row++) {
        for (uint8_t col = 0; col < width; col++) {
            g->board[pos] = COLOR_EMPTY;
            pos++;
        }
        pos += DIR_DOWN - width;
    }
}

/* ---- Play a move ---- */

void game_play_pass(game_t *g, color_t color) {
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

move_legality_t game_play_move(game_t *g, uint16_t coord, color_t color) {
    if (coord == g->ko)
        return MOVE_KO;

    assert(g->board[coord] != COLOR_OFF_BOARD && "coord must be on board");
    if (g->board[coord] != COLOR_EMPTY)
        return MOVE_NON_EMPTY;

    bf_clear(flood_visited);

    color_t own_color = color;
    color_t opp_color = COLOR_OPPOSITE(color);
    g->board[coord] = own_color;

    uint8_t move_hi = (uint8_t)((coord >> 8) | (color << (MOVE_COLOR_BIT - 8)));
    uint8_t captured_total = 0;
    uint8_t own_liberties = 0;

    uint16_t nb;
    uint8_t dir_bit;
    FOR_EACH_NEIGHBOR_DIR(coord, nb, dir_bit, {
        uint8_t cell = g->board[nb];
        if (cell == opp_color) {
            if (!BF_GET(flood_visited, nb) &&
                g->board[nb + DIR_UP] != COLOR_EMPTY &&
                g->board[nb + DIR_DOWN] != COLOR_EMPTY &&
                g->board[nb + DIR_LEFT] != COLOR_EMPTY &&
                g->board[nb + DIR_RIGHT] != COLOR_EMPTY) {
                uint16_t group_size;
                if (group_liberties(g, nb, opp_color, &group_size) == 0) {
                    for (uint16_t i = 0; i < group_size; i++) {
                        uint16_t cap = flood_deque[i];
                        g->board[cap] = COLOR_EMPTY;
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
        } else if (cell == COLOR_EMPTY) {
            own_liberties++;
        }
    });

    if (captured_total == 0 && own_liberties == 0 &&
        !group_has_liberty(g, coord, own_color)) {
        g->board[coord] = COLOR_EMPTY;
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
            if (g->board[nb] == own_color)
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
            uint8_t prev_color = g->board[pc];
            if (prev_color == COLOR_BLACK)
                vram_set_tile(pc, TILE_STONE_B);
            else if (prev_color == COLOR_WHITE)
                vram_set_tile(pc, TILE_STONE_W);
        }
    }

    vram_set_tile(coord, (color == COLOR_BLACK) ? TILE_LAST_B : TILE_LAST_W);
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
        color_t color = MOVE_COLOR(move);
        color_t opp_color = COLOR_OPPOSITE(color);
        uint8_t opp_tile = (color == COLOR_BLACK) ? TILE_STONE_W : TILE_STONE_B;

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
                g->board[nb] = opp_color;

                while (head < tail) {
                    uint16_t pos = flood_deque[head++];
                    vram_set_tile(pos, opp_tile);
                    uint16_t adj;
                    FOR_EACH_NEIGHBOR(pos, adj, {
                        if (g->board[adj] == COLOR_EMPTY) {
                            g->board[adj] = opp_color;
                            flood_deque[tail++] = adj;
                        }
                    });
                }
            }
        });

        /* Remove the played stone. */
        g->board[coord] = COLOR_EMPTY;
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
            vram_set_tile(lc, (MOVE_COLOR(last) == COLOR_BLACK) ? TILE_LAST_B
                                                                : TILE_LAST_W);
        }
    }

    return UNDO_OK;
}

color_t game_color_to_play(const game_t *g) {
    if (g->move_count == 0)
        return COLOR_BLACK;
    return COLOR_OPPOSITE(
        MOVE_COLOR(g->history[(g->move_count - 1) % HISTORY_MAX]));
}

uint8_t game_can_play_approx(const game_t *g, uint8_t col, uint8_t row) {
    uint16_t coord = BOARD_COORD(col, row);
    if (coord == g->ko)
        return 0;
    if (g->board[coord] != COLOR_EMPTY)
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
#endif
