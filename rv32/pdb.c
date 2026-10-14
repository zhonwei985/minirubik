/* Step 1: two pattern databases for the 2x2x2 cube, built on the host.
 *
 * Solve two simplified cubes exactly, by the same breadth-first search
 * solver.c uses, and keep their distances as heuristic tables:
 *
 *   dP[p]  "cube A": only where each corner is (its permutation rank p,
 *          5,040 values); twists are invisible.
 *   dO[o]  "cube B": only how each corner is twisted (its twist rank o,
 *          729 values); positions are invisible.
 *
 * Any move sequence that solves the full cube also solves cube A and cube B,
 * so dP[p] <= d(p, o) and dO[o] <= d(p, o): neither overestimates, and
 * h = max(dP[p], dO[o]) does not either. This program checks that claim
 * (gate H1) against the exact distance of all 3,674,160 states, checks the
 * tables are complete (gate H2), and writes them as read-only data:
 *
 *   OUTDIR/pdb.h     C arrays, for the host and the C reference build
 *   OUTDIR/pdb.inc   .byte lines, for the RV32I assembly
 *
 *   cc -O2 -o pdb rv32/pdb.c && ./pdb rv32
 *
 * The cube model (source, twist, ranks) is solver.c's, unchanged, so p and o
 * are the same numbers solver.c's rank_state() combines as p * 729 + o.
 */
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

static state_t quarter_turn(state_t s, int face)
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
static state_t apply_move(state_t s, int m)
{
    for (int i = 0; i <= m % 3; ++i)
        s = quarter_turn(s, m / 3);
    return s;
}

/* Lehmer rank of the permutation, as in solver.c. */
static uint32_t perm_rank(const state_t *s)
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
static uint32_t ori_rank(const state_t *s)
{
    uint32_t o = 0;
    for (int i = 0; i < 6; ++i)
        o = o * 3 + s->o[i];
    return o;
}

/* Inverse of both ranks, as solver.c's unrank_state(). */
static void unrank(uint32_t p, uint32_t o, state_t *s)
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

/* Transition tables: the permutation after move m depends only on the
 * permutation, the twists only on the twists (report.md section 4). */
static uint16_t perm_move[PERMUTATIONS][MOVES];
static uint16_t ori_move[ORIENTATIONS][MOVES];

static void build_moves(void)
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

/* Breadth-first search from solved (rank 0) over a space of n values with a
 * move table of 9 columns. The first visit is along a shortest path, so
 * d[v] = d[u] + 1 is the exact distance. Returns the number reached. */
static uint32_t bfs(uint8_t *d, uint32_t n, uint16_t (*move)[MOVES])
{
    uint16_t *queue = malloc(n * sizeof *queue);
    uint32_t head = 0, tail = 0;
    memset(d, UNKNOWN, n);
    d[0] = 0;
    queue[tail++] = 0;
    while (head < tail) {
        uint16_t u = queue[head++];
        for (int m = 0; m < MOVES; ++m) {
            uint16_t v = move[u][m];
            if (d[v] == UNKNOWN) {
                d[v] = (uint8_t) (d[u] + 1);
                queue[tail++] = v;
            }
        }
    }
    free(queue);
    return tail;
}

/* Exact distance of every full state, rank p * 729 + o: the oracle for H1.
 * Host only; this is the table the target may not have. */
static uint8_t *exact_distances(void)
{
    uint8_t *d = malloc(STATES);
    uint32_t *queue = malloc((size_t) STATES * sizeof *queue);
    uint32_t head = 0, tail = 0;
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

static void histogram(const char *name, const uint8_t *d, uint32_t n)
{
    uint32_t count[16] = {0};
    int max = 0;
    for (uint32_t i = 0; i < n; ++i) {
        ++count[d[i]];
        max = d[i] > max ? d[i] : max;
    }
    printf("%s:", name);
    for (int k = 0; k <= max; ++k)
        printf(" %u", count[k]);
    printf("  (distance 0..%d)\n", max);
}

static int table_max(const uint8_t *d, uint32_t n)
{
    int max = 0;
    for (uint32_t i = 0; i < n; ++i)
        max = d[i] > max ? d[i] : max;
    return max;
}

static void emit(const char *dir, const uint8_t *dP, const uint8_t *dO)
{
    char path[512];
    snprintf(path, sizeof path, "%s/pdb.h", dir);
    FILE *h = fopen(path, "w");
    snprintf(path, sizeof path, "%s/pdb.inc", dir);
    FILE *s = fopen(path, "w");
    if (!h || !s) {
        fprintf(stderr, "cannot write into %s\n", dir);
        exit(1);
    }
    const struct {
        const char *name, *what;
        const uint8_t *d;
        uint32_t n;
    } tables[2] = {
        {"dP", "distance with positions only, by permutation rank", dP,
         PERMUTATIONS},
        {"dO", "distance with twists only, by twist rank", dO, ORIENTATIONS},
    };
    fprintf(h, "/* Generated by rv32/pdb.c. Do not edit. */\n"
               "#include <stdint.h>\n");
    fprintf(s, "# Generated by rv32/pdb.c. Do not edit.\n");
    for (int t = 0; t < 2; ++t) {
        fprintf(h, "\n/* %s: %s */\nstatic const uint8_t %s[%u] = {",
                tables[t].name, tables[t].what, tables[t].name, tables[t].n);
        fprintf(s, "# %s: %s, %u bytes\n%s:\n", tables[t].name, tables[t].what,
                tables[t].n, tables[t].name);
        for (uint32_t i = 0; i < tables[t].n; ++i) {
            if (i % 24 == 0) {
                fprintf(h, "\n   ");
                fprintf(s, "    .byte ");
            }
            fprintf(h, " %u,", tables[t].d[i]);
            int last = i % 24 == 23 || i == tables[t].n - 1;
            fprintf(s, "%u%s", tables[t].d[i], last ? "\n" : ",");
        }
        fprintf(h, "\n};\n");
    }
    fclose(h);
    fclose(s);
    printf("wrote %s/pdb.h and %s/pdb.inc: %d + %d = %d bytes\n", dir, dir,
           PERMUTATIONS, ORIENTATIONS, PERMUTATIONS + ORIENTATIONS);
}

int main(int argc, char **argv)
{
    static uint8_t dP[PERMUTATIONS], dO[ORIENTATIONS];
    build_moves();

    /* H2: each table reaches its whole space, solved is 0, maxima known. */
    if (bfs(dP, PERMUTATIONS, perm_move) != PERMUTATIONS ||
        bfs(dO, ORIENTATIONS, ori_move) != ORIENTATIONS) {
        fputs("H2 FAIL: a table is not fully populated\n", stderr);
        return 1;
    }
    histogram("dP", dP, PERMUTATIONS);
    histogram("dO", dO, ORIENTATIONS);
    int maxP = table_max(dP, PERMUTATIONS), maxO = table_max(dO, ORIENTATIONS);
    if (dP[0] || dO[0] || maxP != 7 || maxO != 6) {
        fputs("H2 FAIL: solved entry or maximum\n", stderr);
        return 1;
    }
    printf("H2 ok: dP 5040 entries, max %d; dO 729 entries, max %d; "
           "solved entries 0\n",
           maxP, maxO);

    /* H1: h = max(dP, dO) never exceeds the exact distance. */
    uint8_t *d = exact_distances();
    uint64_t sum_h = 0, sum_d = 0;
    uint32_t exact = 0;
    for (uint32_t p = 0; p < PERMUTATIONS; ++p)
        for (uint32_t o = 0; o < ORIENTATIONS; ++o) {
            int h = dP[p] > dO[o] ? dP[p] : dO[o];
            int real = d[p * ORIENTATIONS + o];
            if (h > real) {
                fprintf(stderr, "H1 FAIL: p %u o %u: h %d > d %d\n", p, o, h,
                        real);
                return 1;
            }
            sum_h += (uint64_t) h;
            sum_d += (uint64_t) real;
            exact += h == real;
        }
    histogram("d (exact)", d, STATES);
    printf("H1 ok: max(dP, dO) <= d on all %d states; mean h %.3f, mean d "
           "%.3f; h == d on %u states (%.1f%%)\n",
           STATES, (double) sum_h / STATES, (double) sum_d / STATES, exact,
           100.0 * exact / STATES);
    free(d);

    emit(argc > 1 ? argv[1] : ".", dP, dO);
    return 0;
}
