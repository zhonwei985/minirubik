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

/* ---- Facelet model, for the LED renderer -------------------------------
 *
 * Axes: x right, y up, z front. Position i (internal 0..6) is the corner at
 * corner_xyz[i]; index 7 is the fixed front-upper-left corner. Faces are
 * numbered U R F D L B, which is also the palette order. Every position has
 * three sticker slots: slot 0 is its U or D sticker, slots 1 and 2 follow in
 * the cyclic order fixed by `hand`, which facelet_model() determines by
 * requiring that real 90-degree rotations reproduce source[][] and twist[][].
 * A cubie with twist o shows its own slot s in position slot (o + s) mod 3.
 */
static const int corner_xyz[8][3] = {
    {1, 1, 1},   {1, -1, 1},  {-1, -1, 1}, {1, 1, -1},
    {1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {-1, 1, 1},
};
static const int face_normal[6][3] = {
    {0, 1, 0}, {1, 0, 0}, {0, 0, 1}, {0, -1, 0}, {-1, 0, 0}, {0, 0, -1},
};

typedef struct {
    int slot_face[8][3]; /* face (U R F D L B) of each sticker slot */
    int hand;            /* +1 or -1: orientation of the slot cycle */
    int dir[3];          /* rotation sign that realises R, B, D */
} facelets_t;

static inline int face_of(const int *n)
{
    for (int f = 0; f < 6; ++f)
        if (face_normal[f][0] == n[0] && face_normal[f][1] == n[1] &&
            face_normal[f][2] == n[2])
            return f;
    return -1;
}

static inline int position_of(const int *v)
{
    for (int i = 0; i < 8; ++i)
        if (corner_xyz[i][0] == v[0] && corner_xyz[i][1] == v[1] &&
            corner_xyz[i][2] == v[2])
            return i;
    return -1;
}

/* Quarter rotation about axis 0 (x), 1 (y) or 2 (z), sign s (right-hand). */
static inline void rotate(const int *v, int axis, int s, int *out)
{
    int x = v[0], y = v[1], z = v[2];
    if (axis == 0) {
        out[0] = x, out[1] = -s * z, out[2] = s * y;
    } else if (axis == 1) {
        out[0] = s * z, out[1] = y, out[2] = -s * x;
    } else {
        out[0] = -s * y, out[1] = s * x, out[2] = z;
    }
}

static inline void slots_for(int pos, int hand, int out[3])
{
    const int *c = corner_xyz[pos];
    int ny[3] = {0, c[1], 0}, nx[3] = {c[0], 0, 0}, nz[3] = {0, 0, c[2]};
    /* det(ny, nx, nz) = -c0*c1*c2 */
    int det = -c[0] * c[1] * c[2];
    out[0] = face_of(ny);
    if (det == hand)
        out[1] = face_of(nx), out[2] = face_of(nz);
    else
        out[1] = face_of(nz), out[2] = face_of(nx);
}

static inline int slot_index(const int slots[3], int face)
{
    for (int k = 0; k < 3; ++k)
        if (slots[k] == face)
            return k;
    return -1;
}

/* Returns 1 when some handedness and rotation senses reproduce both tables
 * exactly; fills *m. Face f turns the layer where coordinate axis[f] equals
 * side[f]: R is x = +1, B is z = -1, D is y = -1. */
static inline int facelet_model(facelets_t *m)
{
    static const int axis[3] = {0, 2, 1}, side[3] = {1, -1, -1};
    for (int hand = -1; hand <= 1; hand += 2) {
        int ok = 1;
        for (int f = 0; f < 3 && ok; ++f) {
            int found = 0;
            for (int s = -1; s <= 1 && !found; s += 2) {
                int good = 1;
                for (int i = 0; i < 7 && good; ++i) {
                    int j = source[f][i];
                    if (corner_xyz[j][axis[f]] != side[f]) {
                        good = i == j && twist[f][i] == 0;
                        continue;
                    }
                    int to[3], n[3], rn[3], si[3], sj[3];
                    rotate(corner_xyz[j], axis[f], s, to);
                    if (position_of(to) != i) {
                        good = 0;
                        break;
                    }
                    slots_for(j, hand, sj);
                    slots_for(i, hand, si);
                    memcpy(n, face_normal[sj[0]], sizeof n);
                    rotate(n, axis[f], s, rn);
                    good = slot_index(si, face_of(rn)) == twist[f][i];
                }
                if (good) {
                    found = 1;
                    m->dir[f] = s;
                }
            }
            ok = found;
        }
        if (ok) {
            m->hand = hand;
            for (int i = 0; i < 8; ++i)
                slots_for(i, hand, m->slot_face[i]);
            return 1;
        }
    }
    return 0;
}

/* Unfolded net: faces in a 4 x 3 grid of slots, each face 2 x 2 facelets
 * of 4 x 3 pixels, one separator pixel between face slots. */
typedef struct {
    int x, y;   /* top-left pixel */
    int pos, k; /* corner position (7 = fixed) and sticker slot */
} netcell_t;

static inline void net_cells(const facelets_t *m, netcell_t cell[24])
{
    /* For faces U R F D L B: slot column and row, on-screen right and up. */
    static const int col[6] = {1, 2, 1, 1, 0, 3}, row[6] = {0, 1, 1, 2, 1, 1};
    static const int right[6][3] = {{1, 0, 0},  {0, 0, -1}, {1, 0, 0},
                                    {1, 0, 0},  {0, 0, 1},  {-1, 0, 0}};
    static const int up[6][3] = {{0, 0, -1}, {0, 1, 0}, {0, 1, 0},
                                 {0, 0, 1},  {0, 1, 0}, {0, 1, 0}};
    int n = 0;
    for (int f = 0; f < 6; ++f)
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c) {
                int v[3];
                for (int a = 0; a < 3; ++a)
                    v[a] = face_normal[f][a] + right[f][a] * (2 * c - 1) +
                           up[f][a] * (1 - 2 * r);
                cell[n].x = col[f] * 9 + c * 4;
                cell[n].y = row[f] * 7 + r * 3;
                cell[n].pos = position_of(v);
                cell[n].k = slot_index(m->slot_face[cell[n].pos], f);
                ++n;
            }
}

/* Face colour shown by a cell for a given state. */
static inline int cell_face(const facelets_t *m, const netcell_t *cell,
                            const state_t *s)
{
    if (cell->pos == 7)
        return m->slot_face[7][cell->k];
    int cubie = s->p[cell->pos], o = s->o[cell->pos];
    return m->slot_face[cubie][(cell->k - o + 3) % 3];
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
