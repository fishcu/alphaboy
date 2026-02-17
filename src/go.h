#ifndef GO_H
#define GO_H

#include <stdint.h>

/* Maximum supported board dimensions. */
#define BOARD_MIN_SIZE 5
#define BOARD_MAX_SIZE 19

/* Maximum number of playable intersections (BOARD_MAX_SIZE squared). */
#define BOARD_POSITIONS (BOARD_MAX_SIZE * BOARD_MAX_SIZE)

/* Margin on each side for sentinel/boundary detection. */
#define BOARD_MARGIN 1
#define BOARD_MAX_EXTENT (BOARD_MAX_SIZE + (BOARD_MARGIN * 2))

/* Total number of coordinates in the padded grid. */
#define BOARD_DATA_LEN (BOARD_MAX_EXTENT * BOARD_MAX_EXTENT)

/* Number of bytes needed to store BOARD_DATA_LEN bits. */
#define BOARD_FIELD_BYTES ((BOARD_DATA_LEN + 7) / 8)

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

/* --- Coordinates and moves --- */

/* Board positions are uint16_t indices into the padded grid.
 * COORD_PASS is a sentinel that can never be a valid position. */
#define COORD_PASS 0x7FFFu

/* Packed move: bits 0-14 = board coordinate, bit 15 = color.
 * Two bytes per move; used for the move history. */
typedef uint16_t move_t;

#define MOVE_COLOR_BIT 15
#define MOVE_COORD_MASK 0x7FFFu

#define MOVE_MAKE(coord, color)                                                \
    ((move_t)((coord) | ((move_t)(color) << MOVE_COLOR_BIT)))
#define MOVE_COORD(m) ((m)&MOVE_COORD_MASK)
#define MOVE_COLOR(m) ((m) >> MOVE_COLOR_BIT)

/* Maximum number of moves stored in history. */
#define HISTORY_MAX 512

/* --- Bit-field access helpers --- */

/* Return the byte index for bit position `pos`. */
#define BF_BYTE(pos) ((pos) >> 3)

/* Bit-field mask lookup table (avoids variable shifts on SM83). */
extern const uint8_t bf_masks[8];

/* Return the bit mask for bit position `pos` within its byte. */
#define BF_MASK(pos) (bf_masks[(pos)&7])

/* Test whether bit `pos` is set in field `f`. */
#define BF_GET(f, pos) ((f)[BF_BYTE(pos)] & BF_MASK(pos))

/* Set bit `pos` in field `f`. */
#define BF_SET(f, pos) ((f)[BF_BYTE(pos)] |= BF_MASK(pos))

/* Clear bit `pos` in field `f`. */
#define BF_CLR(f, pos) ((f)[BF_BYTE(pos)] &= (uint8_t)~BF_MASK(pos))

/* --- Coordinate helpers --- */

/* Convert (x, y) in the padded grid to a linear index.
 * x and y are in [0, BOARD_MAX_EXTENT). */
#define BOARD_POS(x, y) ((y)*BOARD_MAX_EXTENT + (x))

/* Convert board coordinates (col, row) in [0, size) to a padded-grid
 * position.  The margin offset of 1 is applied automatically. */
#define BOARD_COORD(col, row)                                                  \
    BOARD_POS((col) + BOARD_MARGIN, (row) + BOARD_MARGIN)

typedef uint8_t bitfield_t[BOARD_FIELD_BYTES];

typedef struct game {
    uint8_t width;
    uint8_t height;
    int8_t komi2;                /* 2 * komi (bonus for white at game end) */
    uint16_t ko;                 /* active ko position, COORD_PASS if none */
    uint16_t move_count;         /* number of moves played so far          */
    bitfield_t on_board;         /* 1 = coordinate lies inside the board   */
    bitfield_t black_stones;     /* 1 = black stone present                */
    bitfield_t white_stones;     /* 1 = white stone present                */
    move_t history[HISTORY_MAX]; /* packed move log for undo/replay    */
} game_t;

/* --- Neighbor offsets in the padded grid --- */

#define DIR_UP (-(int16_t)BOARD_MAX_EXTENT)
#define DIR_DOWN ((int16_t)BOARD_MAX_EXTENT)
#define DIR_LEFT (-1)
#define DIR_RIGHT (1)

/* Reset game to an empty board of the given dimensions.
 * komi2 is 2*komi (e.g. 13 for 6.5 komi). Clears ko and history.
 * Asserts that width and height are in [BOARD_MIN_SIZE, BOARD_MAX_SIZE]. */
void game_reset(game_t *g, uint8_t width, uint8_t height, int8_t komi2);

/* Play a move at (col, row) for `color`.  Updates the board state and
 * writes changed tiles to VRAM incrementally (via vram_set_tile).
 * `queue` and `visited` are scratch buffers for the flood-fill capture
 * check; they must be large enough (BOARD_POSITIONS entries / one
 * bitfield_t respectively).
 * Returns a move_legality_t indicating whether the move was played. */
move_legality_t game_play_move(game_t *g, uint8_t col, uint8_t row,
                               uint8_t color, uint16_t *queue,
                               uint8_t *visited);

#ifndef NDEBUG
/* Print the board state to the emulator debug message window.
 * Output resembles GnuGo's ASCII board: X=black, O=white, .=empty.
 * Compiles away in release builds (NDEBUG defined). */
void game_debug_print(const game_t *g);
#endif

#endif /* GO_H */
