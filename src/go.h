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
 * The stride of 1 << COORD_SHIFT (32) enables shift/mask extraction of
 * row and column from the packed value without division.
 *
 * Bitfield storage is decoupled: each row is BF_ROW_BYTES (3) wide,
 * so the bitfield only occupies BF_NUM_ROWS * 3 = 63 bytes instead of
 * the 84 bytes a stride-32 flat layout would require. */
#define COORD_SHIFT 5
#define COORD_COL_MASK ((1u << COORD_SHIFT) - 1) /* 0x1F */

/* Bitfield storage dimensions. */
#define BF_ROW_BYTES 3
#define BF_NUM_ROWS (BOARD_MAX_SIZE + 2 * BOARD_MARGIN)
#define BOARD_FIELD_BYTES (BF_ROW_BYTES * BF_NUM_ROWS) /* 63 */

/* --- Colors --- */

#define BLACK 0
#define WHITE 1
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
 *   bit  15       color (BLACK=0, WHITE=1)
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

/* Extract padded row / column from a packed coordinate.
 * Results include the BOARD_MARGIN offset (padded, not board-relative).
 * COORD_PR uses byte decomposition to avoid a 16-bit shift by 5:
 * the high byte holds the top row bits and the low byte's upper bits
 * hold the bottom row bits, combined with 8-bit shifts only. */
#define COORD_PR(pc)                                                           \
    ((uint8_t)(((uint8_t)((pc) >> 8) << 3) | ((uint8_t)(pc) >> 5)))
#define COORD_PC(pc) ((uint8_t)((pc)&COORD_COL_MASK))

/* --- Bit-field access helpers --- */

/* Byte index for packed coordinate `pc` in a 3-byte-per-row layout.
 * Uses explicit shift+add for row*3 (avoids SDCC emitting a 16-bit
 * multiply) and uint8_t casts to keep arithmetic 8-bit. */
#define BF_BYTE(pc) (COORD_PR(pc) + (COORD_PR(pc) << 1) + (COORD_PC(pc) >> 3))

/* Bit-field mask lookup table (avoids variable shifts on SM83). */
extern const uint8_t bf_masks[8];

/* Bit mask for packed coordinate `pc` within its byte. */
#define BF_MASK(pc) (bf_masks[(pc)&7])

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
    bitfield_t on_board;         /* 1 = coordinate lies inside the board   */
    bitfield_t black_stones;     /* 1 = black stone present                */
    bitfield_t white_stones;     /* 1 = white stone present                */
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
void game_play_pass(game_t *g, uint8_t color);

/* Play a move at (col, row) for `color`.  Updates the board state and
 * writes changed tiles to VRAM incrementally (via vram_set_tile).
 * `queue` and `visited` are scratch buffers for the flood-fill capture
 * check; they must be large enough (BOARD_POSITIONS entries / one
 * bitfield_t respectively).
 * Returns a move_legality_t indicating whether the move was played. */
move_legality_t game_play_move(game_t *g, uint8_t col, uint8_t row,
                               uint8_t color, uint16_t *queue,
                               uint8_t *visited);

/* Undo the last move, restoring captured stones and ko state.
 * `queue` is a scratch buffer (BOARD_POSITIONS entries).
 * Returns UNDO_OK on success, UNDO_NO_HISTORY if nothing to undo. */
undo_result_t game_undo(game_t *g, uint16_t *queue);

/* Return the color to play next (BLACK or WHITE).
 * Derives from the last history entry; handles handicap correctly. */
uint8_t game_color_to_play(const game_t *g);

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
