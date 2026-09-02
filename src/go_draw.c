#include "go_draw.h"

#include "board_surfaces.h"
#include "display.h"
#include "vram.h"

_Static_assert(TILE_HOSHI == TILE_CORNER_TL + 9,
               "surface tile kinds must be contiguous");
_Static_assert(TILE_KO_BR == TILE_KO_TL + 8,
               "ko surface tiles must be contiguous");

static const uint8_t *board_surface;

static void board_surface_select(uint8_t size) {
    if (size == BOARD_SIZE_9)
        board_surface = board_surface_9;
    else if (size == BOARD_SIZE_13)
        board_surface = board_surface_13;
    else
        board_surface = board_surface_19;
}

static uint8_t surface_kind(uint16_t coord) {
    const uint8_t packed = board_surface[coord >> 1];
    return (coord & 1u) ? (packed >> 4) : (packed & 0x0Fu);
}

uint8_t surface_tile(uint16_t coord) {
    return TILE_CORNER_TL + surface_kind(coord);
}

uint8_t ko_tile(uint16_t coord) {
    uint8_t kind = surface_kind(coord);
    if (kind == TILE_HOSHI - TILE_CORNER_TL)
        kind = TILE_CENTER - TILE_CORNER_TL;
    return TILE_KO_TL + kind;
}

void board_redraw(const game_t *g) {
    const uint8_t w = g->width;
    const uint8_t h = g->height;

    board_surface_select(w);

    /* ---- Frame ---- */

    /* Top row. */
    vram_set_tile(0, TILE_FRAME_TL);
    for (uint8_t col = 0; col < w; col++)
        vram_set_tile(col + BOARD_MARGIN, TILE_FRAME_T);
    vram_set_tile(w + BOARD_MARGIN, TILE_FRAME_TR);

    /* Left and right columns. */
    for (uint8_t row = 0; row < h; row++) {
        const uint16_t ry = VRAM_XY(0, row + BOARD_MARGIN);
        vram_set_tile(ry, TILE_FRAME_L);
        vram_set_tile(ry | (w + BOARD_MARGIN), TILE_FRAME_R);
    }

    /* Bottom rows (two tiles tall). */
    const uint16_t by1 = VRAM_XY(0, h + BOARD_MARGIN);
    const uint16_t by2 = VRAM_XY(0, h + BOARD_MARGIN + 1);
    vram_set_tile(by1, TILE_FRAME_BL_U);
    vram_set_tile(by2, TILE_FRAME_BL_D);
    for (uint8_t col = 0; col < w; col++) {
        vram_set_tile(by1 | (col + BOARD_MARGIN), TILE_FRAME_B_U);
        vram_set_tile(by2 | (col + BOARD_MARGIN), TILE_FRAME_B_D);
    }
    vram_set_tile(by1 | (w + BOARD_MARGIN), TILE_FRAME_BR_U);
    vram_set_tile(by2 | (w + BOARD_MARGIN), TILE_FRAME_BR_D);

    /* ---- Board intersections ---- */

    uint16_t pos = BOARD_COORD(0, 0);
    for (uint8_t row = 0; row < h; row++) {
        uint16_t p = pos;
        for (uint8_t col = 0; col < w; col++) {
            uint8_t tile;
            switch (g->board[p]) {
            case COLOR_BLACK:
                tile = TILE_STONE_B;
                break;
            case COLOR_WHITE:
                tile = TILE_STONE_W;
                break;
            default:
                tile = surface_tile(p);
                break;
            }
            vram_set_tile(p, tile);
            p++;
        }
        pos += DIR_DOWN;
    }

    /* ---- Last-played marker ---- */

    if (g->move_count > g->history_base) {
        const move_t last = g->history[(g->move_count - 1) % HISTORY_MAX];
        const uint16_t lc = MOVE_COORD(last);
        if (lc != COORD_PASS) {
            vram_set_tile(lc, (MOVE_COLOR(last) == COLOR_BLACK) ? TILE_LAST_B
                                                                : TILE_LAST_W);
        }
    }
}
