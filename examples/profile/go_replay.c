#include "go_replay.h"
#include "memory.h"

/* Ishii Akane 2p vs Kato Keiko 6p, 26th Women's Meijin League
 * (2013-08-08), W+R. Move 304, W[gh], captures 48 stones.
 * 318 moves stored as (col, row) pairs, parsed from SGF. */
static const uint8_t replay_moves[] = {
    15, 3,  3,  15, 16, 15, 3,  2,  /* pd dp qp dc */
    14, 16, 2,  4,  8,  2,  16, 5,  /* oq ce ic qf */
    13, 3,  17, 3,  16, 7,  16, 2,  /* nd rd qh qc */
    16, 4,  17, 4,  15, 5,  15, 6,  /* qe re pf pg */
    16, 6,  17, 5,  14, 6,  9,  15, /* qg rf og jp */
    15, 9,  5,  16, 15, 1,  16, 1,  /* pj fq pb qb */
    11, 15, 9,  13, 2,  10, 2,  8,  /* lp jn ck ci */
    2,  13, 1,  15, 5,  2,  4,  10, /* cn bp fc ek */
    3,  9,  4,  4,  4,  9,  5,  10, /* dj ee ej fk */
    3,  7,  2,  7,  6,  8,  6,  3,  /* dh ch gi gd */
    6,  2,  7,  3,  8,  3,  8,  4,  /* gc hd id ie */
    9,  4,  8,  5,  6,  5,  6,  4,  /* je if gf ge */
    3,  5,  2,  5,  7,  2,  9,  5,  /* df cf hc jf */
    10, 4,  17, 15, 17, 16, 15, 15, /* ke rp rq pp */
    16, 16, 15, 13, 14, 15, 15, 11, /* qq pn op pl */
    13, 11, 16, 14, 1,  14, 2,  14, /* nl qo bo co */
    3,  13, 1,  13, 1,  12, 0,  14, /* dn bn bm ao */
    3,  1,  2,  2,  16, 12, 13, 12, /* db cc qm nm */
    12, 12, 14, 12, 17, 10, 12, 11, /* mm om rk ml */
    11, 12, 13, 10, 11, 11, 12, 10, /* lm nk ll mk */
    11, 10, 16, 10, 16, 9,  17, 11, /* lk qk qj rl */
    16, 11, 15, 10, 17, 12, 14, 9,  /* ql pk rm oj */
    7,  16, 7,  15, 8,  16, 9,  16, /* hq hp iq jq */
    6,  15, 7,  14, 6,  16, 4,  16, /* gp ho gq eq */
    6,  14, 6,  13, 4,  14, 5,  13, /* go gn eo fn */
    4,  15, 2,  11, 1,  11, 3,  11, /* ep cl bl dl */
    2,  12, 1,  9,  1,  10, 7,  9,  /* cm bj bk hj */
    6,  9,  6,  10, 8,  12, 8,  10, /* gj gk im ik */
    9,  12, 7,  13, 8,  7,  9,  8,  /* jm hn ih ji */
    9,  7,  10, 8,  11, 9,  10, 7,  /* jh ki lj kh */
    10, 6,  9,  6,  8,  8,  9,  9,  /* kg jg ii jj */
    4,  5,  4,  3,  7,  8,  10, 13, /* ef ed hi kn */
    12, 14, 10, 12, 13, 8,  14, 14, /* mo km ni oo */
    13, 13, 13, 14, 13, 15, 5,  9,  /* nn no np fj */
    5,  8,  3,  8,  4,  8,  9,  17, /* fi di ei jr */
    5,  17, 4,  17, 6,  18, 4,  18, /* fr er gs es */
    8,  17, 6,  17, 7,  17, 5,  18, /* ir gr hr fs */
    8,  18, 6,  17, 11, 17, 9,  18, /* is gr lr js */
    5,  17, 13, 9,  14, 11, 6,  17, /* fr nj ol gr */
    8,  15, 8,  14, 5,  17, 17, 9,  /* ip io fr rj */
    18, 11, 6,  17, 10, 14, 9,  14, /* sl gr ko jo */
    5,  17, 17, 8,  17, 7,  6,  17, /* fr ri rh gr */
    8,  9,  9,  10, 5,  17, 12, 8,  /* ij jk fr mi */
    13, 7,  6,  17, 2,  16, 3,  16, /* nh gr cq dq */
    5,  17, 18, 8,  16, 8,  6,  17, /* fr si qi gr */
    3,  14, 2,  15, 5,  17, 11, 6,  /* do cp fr lg */
    10, 5,  6,  17, 7,  10, 7,  11, /* kf gr hk hl */
    5,  17, 3,  6,  7,  5,  7,  4,  /* fr dg hf he */
    4,  6,  4,  7,  5,  7,  5,  5,  /* eg eh fh ff */
    5,  6,  7,  6,  5,  4,  5,  3,  /* fg hg fe fd */
    8,  6,  6,  6,  5,  5,  6,  17, /* ig gg ff gr */
    3,  7,  2,  6,  5,  17, 15, 16, /* dh cg fr pq */
    15, 17, 6,  17, 0,  12, 0,  10, /* pr gr am ak */
    5,  17, 12, 13, 11, 13, 6,  17, /* fr mn ln gr */
    4,  12, 5,  17, 6,  11, 5,  11, /* em fr gl fl */
    5,  12, 6,  12, 3,  10, 4,  11, /* fm gm dk el */
    3,  12, 7,  9,  10, 11, 12, 7,  /* dm hj kl mh */
    7,  10, 6,  11, 11, 8,  11, 7,  /* hk gl li lh */
    12, 9,  13, 6,  15, 7,  9,  11, /* mj ng ph jl */
    11, 5,  7,  9,  2,  17, 7,  10, /* lf hj cr hk */
    1,  16, 0,  17, 0,  16, 0,  15, /* bq ar aq ap */
    13, 5,  0,  13, 12, 6,  2,  9,  /* nf an mg cj */
    10, 17, 2,  18, 10, 15, 0,  9,  /* kr cs kp aj */
    10, 16, 4,  7,  10, 18, 3,  7,  /* kq eh ks dh */
    5,  14, 3,  4,  10, 10, 0,  11, /* fo de kk al */
    10, 9,  5,  15, 8,  11, 6,  7,  /* kj fp il gh */
    7,  12, 7,  18, 1,  18, 3,  18, /* hm hs bs ds */
    6,  18, 12, 5,  12, 4,  7,  18, /* gs mf me hs */
    3,  17, 1,  17, 6,  18, 14, 13, /* dr br gs on */
    4,  13, 8,  13, 8,  12, 7,  18, /* en in im hs */
    7,  7,  6,  6,  6,  18, 7,  6,  /* hh gg gs hg */
    9,  12, 7,  18, 16, 0,  17, 0,  /* jm hs qa ra */
    6,  18, 12, 13, 11, 14, 7,  18, /* gs mn lo hs */
    17, 1,  15, 0,  6,  18, 14, 8,  /* rb pa gs oi */
    18, 1,  18, 5,  14, 0,  17, 2,  /* sb sf oa rc */
    16, 0,  7,  18, 15, 0,  6,  7,  /* qa hs pa gh */
    18, 0,  18, 7,  18, 6,  17, 6,  /* sa sh sg rg */
    18, 13, 18, 10, 18, 9,  14, 7,  /* sn sk sj oh */
    13, 6,  18, 10, 17, 11, 18, 14, /* ng sk rl so */
    17, 13, 17, 14,                 /* rn ro */
};

#define REPLAY_MOVE_COUNT (sizeof(replay_moves) / 2)

#define REPLAY_PHASE_PLAY 0
#define REPLAY_PHASE_UNDO 1
#define REPLAY_PHASE_DRAIN 2
#define REPLAY_PHASE_DONE 3

static uint16_t replay_index;
static uint8_t replay_phase;

static void replay_fail(void) {
    while (1) {
    }
}

static void replay_validate_empty(const game_t *g) {
    if (g->move_count != 0 || g->history_base != 0 || g->ko != COORD_PASS)
        replay_fail();

    uint16_t row_coord = board_coord(0, 0);
    for (uint8_t row = 0; row < g->height; row++) {
        for (uint8_t col = 0; col < g->width; col++)
            if (g->board[row_coord + col] != COLOR_EMPTY)
                replay_fail();
        row_coord += DIR_DOWN;
    }
}

uint8_t go_replay_step(game_t *g) {
    if (replay_phase == REPLAY_PHASE_DONE)
        return 0;

    if (replay_phase == REPLAY_PHASE_DRAIN) {
        if (board_animation_head != board_animation_committed)
            return 0;

        replay_validate_empty(g);
        replay_phase = REPLAY_PHASE_DONE;
        return 1;
    }

    if (replay_phase == REPLAY_PHASE_UNDO) {
        if (game_undo(g) != UNDO_OK)
            replay_fail();

        if (--replay_index == 0)
            replay_phase = REPLAY_PHASE_DRAIN;
        return 1;
    }

    if (replay_index >= REPLAY_MOVE_COUNT)
        replay_fail();

    const uint8_t col = replay_moves[replay_index * 2];
    const uint8_t row = replay_moves[replay_index * 2 + 1];
    const uint8_t color = game_color_to_play(g);

    if (game_play_move(g, board_coord(col, row), color) != MOVE_LEGAL)
        replay_fail();

    if (++replay_index == REPLAY_MOVE_COUNT)
        replay_phase = REPLAY_PHASE_UNDO;
    return 1;
}
