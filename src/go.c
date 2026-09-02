#include "go.h"

#include <assert.h>
#include <string.h>

#include "board_animation.h"
#include "display.h"
#include "go_draw.h"
#include "memory.h"

/* Last used flood-visited generation; 0 is reserved for "clear". */
static uint8_t flood_generation = 0;

/* Unrolled four-neighbor iteration.  Each direction offset and bit
 * mask is a compile-time immediate  --  no loop counter, no table lookup.
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

/* Clear the first 21 bytes of each 32-byte row in the padded flood-visited
 * array (21 rows total).  The remaining 11 padding bytes per row are never
 * addressed by board logic, so we skip them. */
// clang-format off
static void flood_clear(uint8_t *p) __naked {
    (void)p;
    __asm
        ld l, e
        ld h, d
        xor a
        ld c, #21
    00160$:
        ld b, #21
    00161$:
        ld (hl+), a
        dec b
        jr NZ, 00161$
        ld de, #11
        add hl, de
        dec c
        jr NZ, 00160$
        ret
    __endasm;
}
// clang-format on

/* Advance to a fresh flood generation.  On wrap to 0, clear the visited
 * array and restart from 1. */
inline uint8_t flood_next_generation(void) {
    flood_generation++;
    if (flood_generation == 0) {
        flood_clear(flood_visited);
        flood_generation = 1;
    }
    return flood_generation;
}

#define GROUP_HAS_LIBERTY_CORE()                                               \
    while (head < tail) {                                                      \
        const uint16_t pos = flood_deque[head++];                              \
        uint16_t nb;                                                           \
        FOR_EACH_NEIGHBOR(pos, nb, {                                           \
            if (flood_visited[nb] != generation) {                             \
                const uint8_t cell = g->board[nb];                             \
                if (cell == stone_color) {                                     \
                    flood_visited[nb] = generation;                            \
                    flood_deque[tail++] = nb;                                  \
                } else if (cell == COLOR_EMPTY) {                              \
                    return 1;                                                  \
                }                                                              \
            }                                                                  \
        });                                                                    \
    }

/* Returns 1 immediately on the first liberty found.  If it returns 0,
 * flood_deque[0..group_size-1] contains the fully traversed dead group. */
static uint8_t group_has_liberty(const game_t *g, uint16_t seed,
                                 uint8_t stone_color, uint16_t *group_size) {
    uint16_t head = 0;
    uint16_t tail = 0;
    const uint8_t generation = flood_next_generation();

    flood_visited[seed] = generation;
    flood_deque[tail++] = seed;

    GROUP_HAS_LIBERTY_CORE();
    *group_size = tail;
    return 0;
}

#undef GROUP_HAS_LIBERTY_CORE

void game_reset(game_t *g, uint8_t width, uint8_t height, int8_t komi2) {
    assert(width == height && "board must be square");
    assert((width == BOARD_SIZE_9 || width == BOARD_SIZE_13 ||
            width == BOARD_SIZE_19) &&
           "unsupported board size");

    g->width = width;
    g->height = height;
    g->komi2 = komi2;
    g->ko = COORD_PASS;
    g->move_count = 0;
    g->history_base = 0;

    flood_clear(flood_visited);
    flood_generation = 0;

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
    uint8_t animation_start;

    board_animation_wait_for(2);
    assert(board_animation_tail == board_animation_committed &&
           "previous board animation was not fully published");
    animation_start = board_animation_tail;

    if (g->move_count > g->history_base) {
        const move_t prev = g->history[(g->move_count - 1) % HISTORY_MAX];
        const uint16_t pc = MOVE_COORD(prev);
        if (pc != COORD_PASS) {
            const uint8_t prev_color = g->board[pc];
            if (prev_color == COLOR_BLACK)
                board_animation_push(pc, TILE_STONE_B);
            else if (prev_color == COLOR_WHITE)
                board_animation_push(pc, TILE_STONE_W);
        }
    }

    if (g->ko != COORD_PASS)
        board_animation_push(g->ko, surface_tile(g->ko));
    if (board_animation_tail != animation_start) {
        board_animation_end_frame();
        board_animation_commit();
    }

    g->ko = COORD_PASS;
    if (g->move_count >= g->history_base + HISTORY_MAX)
        g->history_base++;
    g->history[g->move_count++ % HISTORY_MAX] = MOVE_MAKE(COORD_PASS, color);
}

move_legality_t game_play_move(game_t *g, uint16_t coord, color_t color) {
    if (coord == g->ko)
        return MOVE_KO;

    assert(g->board[coord] != COLOR_OFF_BOARD && "coord must be on board");
    if (g->board[coord] != COLOR_EMPTY)
        return MOVE_NON_EMPTY;

    /*
     * The speculative visual prefix is bounded: previous last marker,
     * previous ko marker, new stone, and a possible new ko marker.
     */
    board_animation_wait_for(4);
    assert(board_animation_tail == board_animation_committed &&
           "previous board animation was not fully published");

    const color_t own_color = color;
    const color_t opp_color = COLOR_OPPOSITE(color);
    g->board[coord] = own_color;

    uint8_t move_hi = (uint8_t)((coord >> 8) | (color << (MOVE_COLOR_BIT - 8)));
    uint8_t captured_total = 0;
    uint8_t stream_pending = 0;
    uint8_t prefix_committed = 0;
    uint16_t pending_single_capture = COORD_PASS;

    /* ---- Speculative animation prefix (uncommitted) ----
     * Pushed before captures so the FIFO drain shows cosmetic updates
     * first.  On suicide the queue is rewound and none reach VRAM. */

    /* Un-mark previous last-played stone. */
    if (g->move_count > g->history_base) {
        const move_t prev = g->history[(g->move_count - 1) % HISTORY_MAX];
        const uint16_t pc = MOVE_COORD(prev);
        if (pc != COORD_PASS) {
            const uint8_t prev_color = g->board[pc];
            if (prev_color == COLOR_BLACK)
                board_animation_push(pc, TILE_STONE_B);
            else if (prev_color == COLOR_WHITE)
                board_animation_push(pc, TILE_STONE_W);
        }
    }

    /* Clear previous ko marker tile. */
    if (g->ko != COORD_PASS)
        board_animation_push(g->ko, surface_tile(g->ko));

    /* Mark new last-played stone. */
    board_animation_push(coord,
                         (color == COLOR_BLACK) ? TILE_LAST_B : TILE_LAST_W);

    /* ---- Capture loop ---- */

    uint16_t nb;
    uint8_t dir_bit;
    FOR_EACH_NEIGHBOR_DIR(coord, nb, dir_bit, {
        const uint8_t cell = g->board[nb];
        if (cell == opp_color) {
            if (g->board[nb + DIR_UP] != COLOR_EMPTY &&
                g->board[nb + DIR_DOWN] != COLOR_EMPTY &&
                g->board[nb + DIR_LEFT] != COLOR_EMPTY &&
                g->board[nb + DIR_RIGHT] != COLOR_EMPTY) {
                uint16_t group_size;
                if (!group_has_liberty(g, nb, opp_color, &group_size)) {
                    move_hi |= dir_bit << (MOVE_CAP_SHIFT - 8);

                    /*
                     * Hold a lone first capture locally until ko detection.
                     * A ko tile belongs in the immediate prefix and replaces
                     * that capture's normal removal command.
                     */
                    if (captured_total == 0 && group_size == 1) {
                        pending_single_capture = flood_deque[0];
                        g->board[pending_single_capture] = COLOR_EMPTY;
                        captured_total = 1;
                    } else {
                        if (!prefix_committed) {
                            board_animation_end_frame();
                            board_animation_commit();
                            prefix_committed = 1;
                        }

                        if (captured_total == 1) {
                            board_animation_stream_push(
                                pending_single_capture,
                                surface_tile(pending_single_capture),
                                &stream_pending);
                            pending_single_capture = COORD_PASS;
                        }

                        captured_total = 2;
                        for (uint16_t i = 0; i < group_size; i++) {
                            const uint16_t cap = flood_deque[i];
                            g->board[cap] = COLOR_EMPTY;
                            board_animation_stream_push(cap, surface_tile(cap),
                                                        &stream_pending);
                        }
                    }
                }
            }
        }
    });

    /* ---- Suicide check ---- */

    if (captured_total == 0 && g->board[coord + DIR_UP] != COLOR_EMPTY &&
        g->board[coord + DIR_DOWN] != COLOR_EMPTY &&
        g->board[coord + DIR_LEFT] != COLOR_EMPTY &&
        g->board[coord + DIR_RIGHT] != COLOR_EMPTY) {
        if (!group_has_liberty(g, coord, own_color, &nb)) {
            board_animation_rewind();
            g->board[coord] = COLOR_EMPTY;
            return MOVE_SUICIDAL;
        }
    }

    /* Ko detection: exactly one single-stone group captured, the
     * played stone is a lone stone, and it has exactly one liberty
     * (the position where the captured stone was). */
    g->ko = COORD_PASS;
    if (captured_total == 1) {
        uint16_t ko = COORD_PASS;
        uint8_t liberties = 0;
        FOR_EACH_NEIGHBOR(coord, nb, {
            if (g->board[nb] == own_color)
                goto ko_done;
            if (g->board[nb] == COLOR_EMPTY) {
                ko = nb;
                liberties++;
                if (liberties > 1)
                    goto ko_done;
            }
        });
        g->ko = ko;
        move_hi |= (1 << (MOVE_KO_BIT - 8));
    }
ko_done:;

    if (!prefix_committed) {
        if (g->ko != COORD_PASS) {
            assert(g->ko == pending_single_capture &&
                   "ko must be the sole captured coordinate");
            board_animation_push(g->ko, ko_tile(g->ko));
            pending_single_capture = COORD_PASS;
        }
        board_animation_end_frame();
        board_animation_commit();
    }

    if (pending_single_capture != COORD_PASS) {
        board_animation_stream_push(pending_single_capture,
                                    surface_tile(pending_single_capture),
                                    &stream_pending);
    }
    board_animation_stream_flush(&stream_pending);

    if (g->move_count >= g->history_base + HISTORY_MAX)
        g->history_base++;
    /* Reassemble the full move_t from the 8-bit high byte (flags + coord
     * upper bits) and the low byte of the original coordinate. */
    g->history[g->move_count++ % HISTORY_MAX] =
        ((move_t)move_hi << 8) | (uint8_t)coord;
    return MOVE_LEGAL;
}

typedef struct captured_restore {
    uint8_t *board;
    uint16_t last_coord;
    uint8_t last_tile;
    uint8_t opp_color;
    uint8_t opp_tile;
    uint8_t stream_pending;
} captured_restore_t;

/* Restore one group recorded by a capture-direction flag. */
static void restore_captured_group(captured_restore_t *restore, uint16_t seed) {
    uint16_t head = 0;
    uint16_t tail = 0;
    uint8_t *const board = restore->board;

    flood_deque[tail++] = seed;
    board[seed] = restore->opp_color;

    while (head < tail) {
        const uint16_t pos = flood_deque[head++];
        board_animation_stream_push(pos,
                                    (pos == restore->last_coord)
                                        ? restore->last_tile
                                        : restore->opp_tile,
                                    &restore->stream_pending);
        uint16_t adj;
        FOR_EACH_NEIGHBOR(pos, adj, {
            if (board[adj] == COLOR_EMPTY) {
                board[adj] = restore->opp_color;
                flood_deque[tail++] = adj;
            }
        });
    }
}

undo_result_t game_undo(game_t *g) {
    if (g->move_count <= g->history_base)
        return UNDO_NO_HISTORY;
    if (g->history_base > 0 && g->move_count <= g->history_base + 1)
        return UNDO_NO_HISTORY;

    board_animation_wait_for(4);
    assert(board_animation_tail == board_animation_committed &&
           "previous board animation was not fully published");

    const uint16_t old_ko = g->ko;
    uint16_t last_coord = COORD_PASS;
    uint8_t last_tile = 0;

    g->move_count--;
    const move_t move = g->history[g->move_count % HISTORY_MAX];
    const uint16_t coord = MOVE_COORD(move);

    /* Restore ko state.  The early-out above guarantees that when
     * move_count > 0 the previous history entry is still valid. */
    if (g->move_count == 0) {
        g->ko = COORD_PASS;
    } else {
        const move_t prev = g->history[(g->move_count - 1) % HISTORY_MAX];
        g->ko = COORD_PASS;
        if (prev & (1u << MOVE_KO_BIT)) {
            const uint16_t prev_coord = MOVE_COORD(prev);
            uint16_t nb;
            uint8_t dir_bit;
            FOR_EACH_NEIGHBOR_DIR(prev_coord, nb, dir_bit, {
                if (prev & ((uint16_t)dir_bit << MOVE_CAP_SHIFT))
                    g->ko = nb;
            });
        }
    }

    if (g->move_count > g->history_base) {
        const move_t last = g->history[(g->move_count - 1) % HISTORY_MAX];
        last_coord = MOVE_COORD(last);
        if (last_coord != COORD_PASS) {
            last_tile =
                (MOVE_COLOR(last) == COLOR_BLACK) ? TILE_LAST_B : TILE_LAST_W;
        }
    }

    /*
     * Publish the fixed-size state transition first: old ko removal,
     * played-stone removal, restored ko, and restored last-move marker.
     */
    {
        const uint8_t animation_start = board_animation_tail;

        if (old_ko != COORD_PASS)
            board_animation_push(old_ko, surface_tile(old_ko));
        if (coord != COORD_PASS)
            board_animation_push(coord, surface_tile(coord));
        if (g->ko != COORD_PASS)
            board_animation_push(g->ko, ko_tile(g->ko));
        if (last_coord != COORD_PASS)
            board_animation_push(last_coord, last_tile);

        if (board_animation_tail != animation_start) {
            board_animation_end_frame();
            board_animation_commit();
        }
    }

    if (coord != COORD_PASS) {
        if (move & (0x0Fu << MOVE_CAP_SHIFT)) {
            const color_t color = MOVE_COLOR(move);
            const color_t opp_color = COLOR_OPPOSITE(color);
            const uint8_t opp_tile =
                (color == COLOR_BLACK) ? TILE_STONE_W : TILE_STONE_B;
            captured_restore_t restore = {g->board,  last_coord, last_tile,
                                          opp_color, opp_tile,   0};

            /* Restore captured groups by flood-filling through empties.
             * Each captured group's empty region is fully enclosed by the
             * capturing player's stones and the board edge, so a BFS from
             * the capture-direction neighbor recovers exactly the group. */
            uint16_t nb;
            uint8_t dir_bit;
            FOR_EACH_NEIGHBOR_DIR(coord, nb, dir_bit, {
                if (move & ((uint16_t)dir_bit << MOVE_CAP_SHIFT))
                    restore_captured_group(&restore, nb);
            });

            /* Keep the played point occupied until reconstruction ends. */
            g->board[coord] = COLOR_EMPTY;
            board_animation_stream_flush(&restore.stream_pending);
        } else {
            g->board[coord] = COLOR_EMPTY;
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
    const uint16_t coord = BOARD_COORD(col, row);
    if (coord == g->ko)
        return 0;
    if (g->board[coord] != COLOR_EMPTY)
        return 0;
    return 1;
}
