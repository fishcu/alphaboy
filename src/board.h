#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

/* Maximum supported board dimensions. */
#define BOARD_MIN_SIZE    5
#define BOARD_MAX_SIZE    19

/* Margin on each side for sentinel/boundary detection. */
#define BOARD_MARGIN       1
#define BOARD_MAX_EXTENT  (BOARD_MAX_SIZE + (BOARD_MARGIN * 2))

/* Total number of coordinates in the padded grid. */
#define BOARD_DATA_LEN    (BOARD_MAX_EXTENT * BOARD_MAX_EXTENT)

/* Number of bytes needed to store BOARD_DATA_LEN bits. */
#define BOARD_FIELD_BYTES ((BOARD_DATA_LEN + 7) / 8)

/* --- Bit-field access helpers --- */

/* Return the byte index for bit position `pos`. */
#define BF_BYTE(pos) ((pos) >> 3)

/* Return the bit mask for bit position `pos` within its byte. */
#define BF_MASK(pos) (1u << ((pos) & 7))

/* Test whether bit `pos` is set in field `f`. */
#define BF_GET(f, pos)       ((f)[BF_BYTE(pos)] & BF_MASK(pos))

/* Set bit `pos` in field `f`. */
#define BF_SET(f, pos)       ((f)[BF_BYTE(pos)] |= BF_MASK(pos))

/* Clear bit `pos` in field `f`. */
#define BF_CLR(f, pos)       ((f)[BF_BYTE(pos)] &= ~BF_MASK(pos))

/* --- Coordinate helpers --- */

/* Convert (x, y) in the padded grid to a linear index.
 * x and y are in [0, BOARD_MAX_EXTENT). */
#define BOARD_POS(x, y) ((y) * BOARD_MAX_EXTENT + (x))

/* Convert board coordinates (col, row) in [0, size) to a padded-grid
 * position.  The margin offset of 1 is applied automatically. */
#define BOARD_COORD(col, row) BOARD_POS((col) + BOARD_MARGIN, (row) + BOARD_MARGIN)

typedef uint8_t bitfield_t[BOARD_FIELD_BYTES];

typedef struct board {
    uint8_t    width;
    uint8_t    height;
    bitfield_t on_board;     /* 1 = coordinate lies inside the board */
    bitfield_t black_stones; /* 1 = black stone present */
    bitfield_t white_stones; /* 1 = white stone present */
} board_t;

/* Reset `b` to an empty board of the given dimensions.
 * Asserts that width and height are in [1, BOARD_MAX_SIZE]. */
void board_reset(board_t *b, uint8_t width, uint8_t height);

#endif /* BOARD_H */
