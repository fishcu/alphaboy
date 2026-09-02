#include "go_draw.h"

#include <string.h>

#include "board_surfaces.h"
#include "display.h"

_Static_assert(TILE_CORNER_BR == TILE_CORNER_TL + 8,
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

uint8_t surface_tile(uint16_t coord) { return board_surface[coord]; }

uint8_t ko_tile(uint16_t coord) {
    uint8_t tile = board_surface[coord];
    if (tile == TILE_HOSHI)
        tile = TILE_CENTER;
    return tile + (TILE_KO_TL - TILE_CORNER_TL);
}

void board_redraw(const game_t *g) {
    const uint8_t w = g->width;
    const uint8_t h = g->height;
    uint8_t *const tilemap = (uint8_t *)0x9800u;

    board_surface_select(w);
    memcpy(tilemap, board_surface, BOARD_SURFACE_TILEMAP_SIZE);

    if (g->move_count == 0)
        return;

    /* ---- Board intersections ---- */

    uint16_t pos = BOARD_COORD(0, 0);
    for (uint8_t row = 0; row < h; row++) {
        uint16_t p = pos;
        for (uint8_t col = 0; col < w; col++) {
            switch (g->board[p]) {
            case COLOR_BLACK:
                tilemap[p] = TILE_STONE_B;
                break;
            case COLOR_WHITE:
                tilemap[p] = TILE_STONE_W;
                break;
            default:
                break;
            }
            p++;
        }
        pos += DIR_DOWN;
    }

    /* ---- Last-played marker ---- */

    if (g->move_count > g->history_base) {
        const move_t last = g->history[(g->move_count - 1) % HISTORY_MAX];
        const uint16_t lc = MOVE_COORD(last);
        if (lc != COORD_PASS) {
            tilemap[lc] =
                (MOVE_COLOR(last) == COLOR_BLACK) ? TILE_LAST_B : TILE_LAST_W;
        }
    }
}
