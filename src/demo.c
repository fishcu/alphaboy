#include "demo.h"

#ifdef DEMO_MODE

#include "layout.h"

/* AlphaGo vs Lee Sedol, Game 4 (2016-03-13), W+R.
 * 180 moves stored as (col, row) pairs, parsed from SGF. */
static const uint8_t demo_moves[] = {
    15,  3,  3, 15,  2,  3, 16, 15, /* pd dp cd qp */
    14, 15, 14, 16, 13, 16, 15, 16, /* op oq nq pq */
     2, 13,  5, 16, 12, 15, 15, 14, /* cn fq mp po */
     8, 16,  4,  2,  7,  3,  2,  6, /* iq ec hd cg */
     4,  3,  2,  9,  3,  2,  1, 15, /* ed cj dc bp */
    13,  2, 16,  8,  4, 15,  4, 14, /* nc qi ep eo */
     3, 10,  5, 15,  2, 10,  3,  9, /* dk fp ck dj */
     4,  9,  4,  8,  5,  8,  4,  7, /* ej ei fi eh */
     5,  7,  1,  9,  5, 10,  5,  6, /* fh bj fk fg */
     6,  6,  5,  5,  6,  5, 12,  2, /* gg ff gf mc */
    12,  3, 11,  2, 13,  1,  8,  3, /* md lc nb id */
     7,  2,  9,  6, 15,  9, 15,  8, /* hc jg pj pi */
    14,  9, 14,  8, 13,  8, 13,  7, /* oj oi ni nh */
    12,  7, 13,  6, 12,  6, 12,  8, /* mh ng mg mi */
    13,  9, 12,  5, 11,  8, 13,  4, /* nj mf li ne */
    13,  3, 12,  9, 11,  5, 12, 10, /* nd mj lf mk */
    12,  4, 13,  5, 11,  7, 16,  9, /* me nf lh qj */
    10, 10,  8, 10,  9,  8,  6,  7, /* kk ik ji gh */
     7,  9,  6,  4,  7,  4,  5,  3, /* hj ge he fd */
     5,  2, 10,  8,  9,  9, 11,  9, /* fc ki jj lj */
    10,  7,  9,  7, 12, 11, 13, 10, /* kh jh ml nk */
    14, 11, 14, 10, 15, 10, 15, 11, /* ol ok pk pl */
    16, 10, 13, 11, 10,  9,  8,  8, /* qk nl kj ii */
    17, 10, 14, 12, 15,  6, 16, 11, /* rk om pg ql */
     2, 15,  2, 14, 14,  4, 17, 11, /* cp co oe rl */
    18, 10, 17,  9,  7,  6,  8,  9, /* sk rj hg ij */
    10, 12,  6,  8,  5,  9,  9, 11, /* km gi fj jl */
    10, 11,  6, 11,  5, 11,  6, 12, /* kl gl fl gm */
     2,  7,  4,  4,  4,  1,  1,  6, /* ch ee eb bg */
     3,  6,  4,  6,  4, 13,  5, 14, /* dg eg en fo */
     3,  5,  3,  7,  8, 12,  7, 10, /* df dh im hk */
     1, 13,  8,  5,  6,  3,  5,  4, /* bn if gd fe */
     7,  5,  8,  7,  1,  7,  2,  8, /* hf ih bh ci */
     7, 14,  6, 14, 14, 17, 17,  6, /* ho go or rg */
     3, 13,  2, 16, 15, 17, 16, 17, /* dn cq pr qr */
    17,  5, 16,  6, 16,  5,  9,  2, /* rf qg qf jc */
     6, 17, 18,  5, 18,  4, 18,  6, /* gr sf se sg */
    17,  3,  1, 11,  1, 10,  0, 10, /* rd bl bk ak */
     2, 11,  7, 13,  8, 13,  7, 15, /* cl hn in hp */
     5, 17,  4, 17,  4, 18,  3, 18, /* fr er es ds */
     0,  7,  0,  8, 10,  3,  8,  4, /* ah ai kd ie */
    10,  2, 10,  1,  6, 10,  8,  1, /* kc kb gk ib */
    16,  7, 17,  7, 16, 18, 17, 18, /* qh rh qs rs */
    14,  7, 18, 11, 14,  5, 18,  9, /* oh sl of sj */
    13,  8, 13,  9, 14, 14,  9, 15, /* ni nj oo jp */
};

#define DEMO_MOVE_COUNT (sizeof(demo_moves) / 2)

static uint16_t demo_index;
static uint8_t demo_timer;

uint8_t demo_step(game_t *g, uint16_t *queue, uint8_t *visited) {
    if (demo_index >= DEMO_MOVE_COUNT)
        return 0;

    if (++demo_timer < DEMO_FRAME_INTERVAL)
        return 0;
    demo_timer = 0;

    uint8_t col = demo_moves[demo_index * 2];
    uint8_t row = demo_moves[demo_index * 2 + 1];
    uint8_t color = game_color_to_play(g);

    game_play_move(g, col, row, color, queue, visited);
    demo_index++;
    return 1;
}

#endif /* DEMO_MODE */
