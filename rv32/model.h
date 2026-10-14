/* Host-side cube model shared by pdb.c and check.c. Never linked into the
 * target build.
 *
 * It is solver.c's model unchanged: seven moving corners, positions and
 * twists stored separately, the permutation ranked by its Lehmer code and the
 * first six twists in base 3, so p and o are the same numbers solver.c's
 * rank_state() combines as p * 729 + o.
 */
#ifndef MODEL_H
#define MODEL_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    CUBIES = 7,
    PERMUTATIONS = 5040, /* 7! */
    ORIENTATIONS = 729,  /* 3^6: the 7th twist is fixed by the other six */
    STATES = PERMUTATIONS * ORIENTATIONS,
    MOVES = 9, /* R R2 R' B B2 B' D D2 D' */
    UNKNOWN = 0xFF,
};

typedef struct {
    uint8_t p[CUBIES], o[CUBIES];
} state_t;

/* From solver.c: each destination takes a cubie from source[face][dest]. */
static const uint8_t source[3][CUBIES] = {
    {1, 4, 2, 0, 3, 5, 6},
    {0, 1, 2, 4, 5, 6, 3},
    {0, 2, 5, 3, 1, 4, 6},
};
static const uint8_t twist[3][CUBIES] = {
    {1, 2, 0, 2, 1, 0, 0},
    {0, 0, 0, 1, 2, 1, 2},
    {0, 0, 0, 0, 0, 0, 0},
};

static inline state_t quarter_turn(state_t s, int face)
{
    state_t t;
    for (int i = 0; i < CUBIES; ++i) {
        int from = source[face][i];
        t.p[i] = s.p[from];
        t.o[i] = (uint8_t) ((s.o[from] + twist[face][i]) % 3);
    }
    return t;
}

/* Move m is face m / 3 turned m % 3 + 1 quarter turns. */
static inline state_t apply_move(state_t s, int m)
{
    for (int i = 0; i <= m % 3; ++i)
        s = quarter_turn(s, m / 3);
    return s;
}

/* Lehmer rank of the permutation, as in solver.c. */
static inline uint32_t perm_rank(const state_t *s)
{
    uint32_t p = 0;
    for (int i = 0; i < CUBIES; ++i) {
        uint32_t smaller = 0;
        for (int j = i + 1; j < CUBIES; ++j)
            smaller += s->p[j] < s->p[i];
        p = p * (uint32_t) (CUBIES - i) + smaller;
    }
    return p;
}

/* Base-3 rank of the first six twists, as in solver.c. */
static inline uint32_t ori_rank(const state_t *s)
{
    uint32_t o = 0;
    for (int i = 0; i < 6; ++i)
        o = o * 3 + s->o[i];
    return o;
}

/* Inverse of both ranks, as solver.c's unrank_state(). */
static inline void unrank(uint32_t p, uint32_t o, state_t *s)
{
    uint8_t available[CUBIES] = {0, 1, 2, 3, 4, 5, 6};
    uint32_t f = 720;
    int sum = 0;
    for (int i = 0; i < CUBIES; ++i) {
        uint32_t q = p / f;
        p %= f;
        s->p[i] = available[q];
        for (uint32_t j = q; j + 1 < (uint32_t) (CUBIES - i); ++j)
            available[j] = available[j + 1];
        if (i < 5)
            f /= (uint32_t) (6 - i);
    }
    for (int i = 6; i-- > 0;) {
        s->o[i] = (uint8_t) (o % 3);
        sum += s->o[i];
        o /= 3;
    }
    s->o[6] = (uint8_t) ((3 - sum % 3) % 3);
}

static inline int is_solved(const state_t *s)
{
    for (int i = 0; i < CUBIES; ++i)
        if (s->p[i] != i || s->o[i])
            return 0;
    return 1;
}

/* The 14-digit form solver.c reads: positions 1-7, then twists 1-3. */
static inline void state_string(const state_t *s, char out[15])
{
    for (int i = 0; i < CUBIES; ++i) {
        out[i] = (char) ('1' + s->p[i]);
        out[i + CUBIES] = (char) ('1' + s->o[i]);
    }
    out[14] = '\0';
}

/* Transition tables for all nine moves. The permutation after a move
 * depends only on the permutation, the twists only on the twists
 * (report.md section 4), so the two are tabulated separately. */
static uint16_t perm_move[PERMUTATIONS][MOVES];
static uint16_t ori_move[ORIENTATIONS][MOVES];

static inline void build_moves(void)
{
    state_t s;
    for (uint32_t p = 0; p < PERMUTATIONS; ++p) {
        unrank(p, 0, &s);
        for (int m = 0; m < MOVES; ++m) {
            state_t t = apply_move(s, m);
            perm_move[p][m] = (uint16_t) perm_rank(&t);
        }
    }
    for (uint32_t o = 0; o < ORIENTATIONS; ++o) {
        unrank(0, o, &s);
        for (int m = 0; m < MOVES; ++m) {
            state_t t = apply_move(s, m);
            ori_move[o][m] = (uint16_t) ori_rank(&t);
        }
    }
}

/* Exact distance of every state, rank p * 729 + o, by breadth-first search
 * from solved: the oracle for gates H1 and H3. Host only; this is the table
 * the target may not have. Call build_moves() first. */
static inline uint8_t *exact_distances(void)
{
    uint8_t *d = malloc(STATES);
    uint32_t *queue = malloc((size_t) STATES * sizeof *queue);
    uint32_t head = 0, tail = 0;
    if (!d || !queue) {
        fputs("out of memory\n", stderr);
        exit(1);
    }
    memset(d, UNKNOWN, STATES);
    d[0] = 0;
    queue[tail++] = 0;
    while (head < tail) {
        uint32_t u = queue[head++];
        uint32_t p = u / ORIENTATIONS, o = u % ORIENTATIONS;
        for (int m = 0; m < MOVES; ++m) {
            uint32_t v = perm_move[p][m] * (uint32_t) ORIENTATIONS +
                         ori_move[o][m];
            if (d[v] == UNKNOWN) {
                d[v] = (uint8_t) (d[u] + 1);
                queue[tail++] = v;
            }
        }
    }
    free(queue);
    if (tail != STATES) {
        fprintf(stderr, "exact BFS reached %u of %d states\n", tail, STATES);
        exit(1);
    }
    return d;
}

#endif
