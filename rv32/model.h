/* Host-only cube model shared by the table generator and the checker.
 *
 * This is the solver.c model unchanged: seven moving corners, positions and
 * twists factored, Lehmer rank for the permutation and base-3 rank for the
 * first six twists. Nothing in here is linked into the target build.
 */
#ifndef MODEL_H
#define MODEL_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    CUBIES = 7,
    PERMUTATIONS = 5040,
    ORIENTATIONS = 729,
    STATES = PERMUTATIONS * ORIENTATIONS,
};

typedef struct {
    uint8_t p[CUBIES], o[CUBIES];
} state_t;

/* Each destination takes a cubie from source[face][destination]. */
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

static inline state_t apply_move(state_t s, int move)
{
    for (int i = 0; i <= move % 3; ++i)
        s = quarter_turn(s, move / 3);
    return s;
}

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

static inline uint32_t ori_rank(const state_t *s)
{
    uint32_t o = 0;
    for (int i = 0; i < 6; ++i)
        o = o * 3 + s->o[i];
    return o;
}

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

static inline void state_string(const state_t *s, char out[15])
{
    for (int i = 0; i < CUBIES; ++i) {
        out[i] = (char) ('1' + s->p[i]);
        out[i + CUBIES] = (char) ('1' + s->o[i]);
    }
    out[14] = '\0';
}

/* Exact BFS distance over the factored ranks: the oracle for H1 and H3. */
static inline uint8_t *exact_distances(void)
{
    static uint16_t pt[3][PERMUTATIONS], ot[3][ORIENTATIONS];
    state_t s;
    for (uint32_t r = 0; r < PERMUTATIONS; ++r) {
        unrank(r, 0, &s);
        for (int f = 0; f < 3; ++f) {
            state_t t = quarter_turn(s, f);
            pt[f][r] = (uint16_t) perm_rank(&t);
        }
    }
    for (uint32_t r = 0; r < ORIENTATIONS; ++r) {
        unrank(0, r, &s);
        for (int f = 0; f < 3; ++f) {
            state_t t = quarter_turn(s, f);
            ot[f][r] = (uint16_t) ori_rank(&t);
        }
    }
    uint8_t *d = malloc(STATES);
    uint32_t *q = malloc((size_t) STATES * sizeof *q);
    if (!d || !q) {
        fputs("out of memory\n", stderr);
        exit(1);
    }
    memset(d, 0xFF, STATES);
    uint32_t head = 0, tail = 0;
    d[0] = 0;
    q[tail++] = 0;
    while (head < tail) {
        uint32_t x = q[head++];
        uint32_t p = x / ORIENTATIONS, o = x % ORIENTATIONS;
        for (int f = 0; f < 3; ++f) {
            uint32_t np = p, no = o;
            for (int t = 0; t < 3; ++t) {
                np = pt[f][np];
                no = ot[f][no];
                uint32_t y = np * ORIENTATIONS + no;
                if (d[y] == 0xFF) {
                    d[y] = (uint8_t) (d[x] + 1);
                    q[tail++] = y;
                }
            }
        }
    }
    free(q);
    if (tail != STATES) {
        fputs("oracle BFS incomplete\n", stderr);
        exit(1);
    }
    return d;
}

#endif
