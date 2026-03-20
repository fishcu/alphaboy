#ifndef GO_DEBUG_H
#define GO_DEBUG_H

#include "go.h"

#ifndef NDEBUG
void game_debug_print(const game_t *g);
void game_debug_log_move(const game_t *g, color_t color, uint8_t col,
                         uint8_t row);
void game_debug_log_illegal(move_legality_t result, color_t color, uint8_t col,
                            uint8_t row);
void game_debug_log_undo(const game_t *g);

#define DEBUG_LOG_MOVE(g, color, col, row)                                     \
    game_debug_log_move(g, color, col, row)
#define DEBUG_LOG_ILLEGAL(r, color, col, row)                                  \
    game_debug_log_illegal(r, color, col, row)
#define DEBUG_LOG_UNDO(g) game_debug_log_undo(g)
#else
#define DEBUG_LOG_MOVE(g, color, col, row) ((void)0)
#define DEBUG_LOG_ILLEGAL(r, color, col, row) ((void)0)
#define DEBUG_LOG_UNDO(g) ((void)0)
#endif

#endif /* GO_DEBUG_H */
