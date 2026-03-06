#ifndef GO_H
#define GO_H

#include <stdint.h>

/* Maximum supported board dimensions. */
#define BOARD_MIN_SIZE 5
#define BOARD_MAX_SIZE 19

/* Maximum number of playable intersections. */
#define BOARD_POSITIONS (BOARD_MAX_SIZE * BOARD_MAX_SIZE)

/* Margin on each side for sentinel/boundary detection. */
#define BOARD_MARGIN 1

/* --- Packed coordinate system ---
 *
 * A board position is encoded as (padded_row << COORD_SHIFT) | padded_col,
 * where padded_row = board_row + BOARD_MARGIN and likewise for col.
 * The stride of 1 << COORD_SHIFT (32) gives each bitfield row 4 bytes,
 * directly matching the BG tile map layout (32 tiles per row).
 * This unifies bitfield addressing, tile-map addressing, and neighbor
 * arithmetic under a single coordinate representation.
 *
 * BF_BYTE(pc) = pc >> 3, BF_MASK(pc) = 1 << (pc & 7). */
#define COORD_SHIFT 5
#define COORD_COL_MASK ((1u << COORD_SHIFT) - 1) /* 0x1F */

/* Bitfield storage dimensions (4 bytes per row, matching stride 32).
 * Still used for flood_visited. */
#define BF_ROW_BYTES (1 << (COORD_SHIFT - 3)) /* 4 */
#define BF_NUM_ROWS (BOARD_MAX_SIZE + 2 * BOARD_MARGIN)
#define BOARD_FIELD_BYTES (BF_NUM_ROWS * BF_ROW_BYTES) /* 84 */

/* Board array: one uint8_t per coordinate in the padded grid. */
#define BOARD_CELLS (BF_NUM_ROWS << COORD_SHIFT) /* 672 */

/* --- Cell state (one uint8_t per board position) ---
 * COLOR_BLACK and COLOR_WHITE double as both color constants and cell values.
 */

typedef enum {
    COLOR_BLACK = 0,
    COLOR_WHITE = 1,
    COLOR_EMPTY = 2,
    COLOR_OFF_BOARD = 3
} color_t;

#define COLOR_OPPOSITE(c) ((c) ^ 1)

/* --- Move legality --- */

typedef uint8_t move_legality_t;

#define MOVE_LEGAL 0
#define MOVE_NON_EMPTY 1
#define MOVE_SUICIDAL 2
#define MOVE_KO 3

/* --- Undo result --- */

typedef uint8_t undo_result_t;

#define UNDO_OK 0
#define UNDO_NO_HISTORY 1

/* --- Coordinates and moves --- */

/* COORD_PASS is a sentinel that can never be a valid packed coordinate.
 * Decodes to padded row=31, col=31 — well outside the valid range.
 * The 10-bit move coordinate field supports boards up to 30x30. */
#define COORD_PASS 0x03FFu

/* Packed move (16 bits):
 *   bit  15       color (0=COLOR_BLACK, 1=COLOR_WHITE)
 *   bit  14       ko flag (this move caused ko)
 *   bits 13-10    capture direction flags (UP, DOWN, LEFT, RIGHT)
 *   bits 9-0      board coordinate or COORD_PASS */
typedef uint16_t move_t;

#define MOVE_COLOR_BIT 15
#define MOVE_KO_BIT 14
#define MOVE_CAP_SHIFT 10
#define MOVE_COORD_MASK 0x03FFu

#define MOVE_MAKE(coord, color)                                                \
    ((move_t)((coord) | ((move_t)(color) << MOVE_COLOR_BIT)))
#define MOVE_COORD(m) ((m)&MOVE_COORD_MASK)
#define MOVE_COLOR(m) ((m) >> MOVE_COLOR_BIT)

/* Maximum number of moves stored in history. */
#define HISTORY_MAX 512

/* --- Coordinate extraction --- */

/* Extract board-relative row / column (0-based, no margin) from a
 * packed coordinate.  Used in cold paths for surface tile lookups. */
#define BOARD_ROW(pc) ((uint8_t)((pc) >> COORD_SHIFT) - BOARD_MARGIN)
#define BOARD_COL(pc) ((uint8_t)((pc)&COORD_COL_MASK) - BOARD_MARGIN)

/* --- Bit-field access helpers --- */

/* Byte index for packed coordinate `pc`.  Trivial with stride-32 rows:
 * each row is 4 bytes, and bits within a row map naturally. */
#define BF_BYTE(pc) ((pc) >> 3)

/* Powers-of-two lookup table (avoids variable shifts on SM83). */
extern const uint8_t pow2[8];

/* Bit mask for packed coordinate `pc` within its byte. */
#define BF_MASK(pc) (pow2[(pc)&7])

/* Test whether the bit for packed coord `pc` is set in field `f`. */
#define BF_GET(f, pc) ((f)[BF_BYTE(pc)] & BF_MASK(pc))

/* Set the bit for packed coord `pc` in field `f`. */
#define BF_SET(f, pc) ((f)[BF_BYTE(pc)] |= BF_MASK(pc))

/* Clear the bit for packed coord `pc` in field `f`. */
#define BF_CLR(f, pc) ((f)[BF_BYTE(pc)] &= (uint8_t)~BF_MASK(pc))

/* --- Coordinate helpers --- */

/* Convert board coordinates (col, row) in [0, size) to a packed
 * padded coordinate.  The margin offset is applied automatically. */
#define BOARD_COORD(col, row)                                                  \
    ((uint16_t)((((row) + BOARD_MARGIN) << COORD_SHIFT) |                      \
                ((col) + BOARD_MARGIN)))

typedef uint8_t bitfield_t[BOARD_FIELD_BYTES];

typedef struct game {
    uint8_t width;
    uint8_t height;
    int8_t komi2;                /* 2 * komi (bonus for white at game end) */
    uint16_t ko;                 /* active ko position, COORD_PASS if none */
    uint16_t move_count;         /* number of moves played so far          */
    uint16_t history_base;       /* oldest undoable move_count value       */
    uint8_t board[BOARD_CELLS];  /* cell state per padded coordinate       */
    move_t history[HISTORY_MAX]; /* ring buffer of packed moves            */
} game_t;

/* --- Neighbor offsets in the packed coordinate space ---
 *
 * UP/DOWN shift by one row (1 << COORD_SHIFT = 32).
 * LEFT/RIGHT shift by one column (1). */

#define DIR_UP (-(1 << COORD_SHIFT))
#define DIR_DOWN (1 << COORD_SHIFT)
#define DIR_LEFT (-1)
#define DIR_RIGHT (1)

/* Reset game to an empty board of the given dimensions.
 * komi2 is 2*komi (e.g. 13 for 6.5 komi). Clears ko and history.
 * Asserts that width and height are in [BOARD_MIN_SIZE, BOARD_MAX_SIZE]. */
void game_reset(game_t *g, uint8_t width, uint8_t height, int8_t komi2);

/* Play a pass for `color`.  Clears ko and records the pass in history. */
void game_play_pass(game_t *g, color_t color);

/* Play a stone at the packed coordinate `coord` for `color`.  Updates the
 * board state and writes changed tiles to VRAM incrementally.
 * Uses flood_stack / flood_visited from layout.h as scratch buffers.
 * Returns a move_legality_t indicating whether the move was played. */
move_legality_t game_play_move(game_t *g, uint16_t coord, color_t color);

/* Undo the last move, restoring captured stones and ko state.
 * Uses flood_stack from layout.h as a scratch buffer.
 * Returns UNDO_OK on success, UNDO_NO_HISTORY if nothing to undo. */
undo_result_t game_undo(game_t *g);

/* Return the color to play next (COLOR_BLACK or COLOR_WHITE).
 * Derives from the last history entry; handles handicap correctly. */
color_t game_color_to_play(const game_t *g);

/* Cheap legality approximation: 1 if (col, row) is empty and not ko.
 * Does not check suicide — intended for ghost stone display gating. */
uint8_t game_can_play_approx(const game_t *g, uint8_t col, uint8_t row);

#ifndef NDEBUG
/* Print the board state to the emulator debug message window.
 * Output resembles GnuGo's ASCII board: X=black, O=white, .=empty.
 * Compiles away in release builds (NDEBUG defined). */
void game_debug_print(const game_t *g);
#endif

#endif /* GO_H */
