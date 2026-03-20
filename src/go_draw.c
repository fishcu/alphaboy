#include "go_draw.h"

#include "display.h"
#include "vram.h"

uint8_t surface_tile(uint8_t col, uint8_t row, uint8_t w, uint8_t h) {
    const uint8_t top = (row == 0);
    const uint8_t bottom = (row == h - 1);
    const uint8_t left = (col == 0);
    const uint8_t right = (col == w - 1);

    if (top) {
        if (left)
            return TILE_CORNER_TL;
        if (right)
            return TILE_CORNER_TR;
        return TILE_EDGE_T;
    }
    if (bottom) {
        if (left)
            return TILE_CORNER_BL;
        if (right)
            return TILE_CORNER_BR;
        return TILE_EDGE_B;
    }
    if (left)
        return TILE_EDGE_L;
    if (right)
        return TILE_EDGE_R;

    /* Star-point (hoshi) check for interior intersections.
     * >= 13: 4th line from edge, corners + sides + center.
     * < 13:  3rd line from edge, corners + center only. */
    const uint8_t d = (w >= 13 && h >= 13) ? 3 : 2;
    const uint8_t on_col = (col == d || col == w - 1 - d || col == w / 2);
    const uint8_t on_row = (row == d || row == h - 1 - d || row == h / 2);
    if (on_col && on_row) {
        if (w >= 13 && h >= 13)
            return TILE_HOSHI;
        if ((col == w / 2) == (row == h / 2))
            return TILE_HOSHI;
    }

    return TILE_CENTER;
}

uint8_t ko_tile(uint8_t col, uint8_t row, uint8_t w, uint8_t h) {
    uint8_t t = surface_tile(col, row, w, h);
    if (t == TILE_HOSHI)
        t = TILE_CENTER;
    return t + (TILE_KO_TL - TILE_CORNER_TL);
}

void board_redraw(const game_t *g) {
    const uint8_t w = g->width;
    const uint8_t h = g->height;

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
                tile = surface_tile(col, row, w, h);
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
