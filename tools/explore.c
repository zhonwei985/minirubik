/* Host-side design exploration for the RV32I solver (stage 2).
 *
 * Builds the exact BFS distance table as an oracle, then builds candidate
 * pattern databases over abstractions of the state space and measures how
 * many IDA* nodes each heuristic costs on every distance-11 state and on a
 * stratified sample of the rest. Nothing here runs on the target; it exists
 * so that the choice of heuristic is a measurement rather than a guess.
 *
 *   cc -O2 -o explore tools/explore.c && ./explore
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { C = 7, NP = 5040, NO = 729, N = NP * NO };

typedef struct {
    uint8_t p[C], o[C];
} state_t;

static const uint8_t source[3][C] = {
    {1, 4, 2, 0, 3, 5, 6},
    {0, 1, 2, 4, 5, 6, 3},
    {0, 2, 5, 3, 1, 4, 6},
};
static const uint8_t twist[3][C] = {
    {1, 2, 0, 2, 1, 0, 0},
    {0, 0, 0, 1, 2, 1, 2},
    {0, 0, 0, 0, 0, 0, 0},
};

static state_t qturn(state_t s, int f)
{
    state_t t;
    for (int i = 0; i < C; ++i) {
        int j = source[f][i];
        t.p[i] = s.p[j];
        t.o[i] = (uint8_t) ((s.o[j] + twist[f][i]) % 3);
    }
    return t;
}

static uint32_t prank(const state_t *s)
{
    uint32_t p = 0;
    for (int i = 0; i < C; ++i) {
        int n = 0;
        for (int j = i + 1; j < C; ++j)
            n += s->p[j] < s->p[i];
        p = p * (uint32_t) (C - i) + (uint32_t) n;
    }
    return p;
}

static uint32_t orank(const state_t *s)
{
    uint32_t o = 0;
    for (int i = 0; i < 6; ++i)
        o = o * 3 + s->o[i];
    return o;
}

static void unrank(uint32_t p, uint32_t o, state_t *s)
{
    uint8_t avail[C] = {0, 1, 2, 3, 4, 5, 6};
    uint32_t f = 720;
    int sum = 0;
    for (int i = 0; i < C; ++i) {
        uint32_t q = p / f;
        p %= f;
        s->p[i] = avail[q];
        for (uint32_t j = q; j + 1 < (uint32_t) (C - i); ++j)
            avail[j] = avail[j + 1];
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

static uint16_t PT[3][NP], OT[3][NO];
static uint8_t *dist; /* exact oracle */

static void build_tables(void)
{
    state_t s;
    for (uint32_t r = 0; r < NP; ++r) {
        unrank(r, 0, &s);
        for (int f = 0; f < 3; ++f) {
            state_t t = qturn(s, f);
            PT[f][r] = (uint16_t) prank(&t);
        }
    }
    for (uint32_t r = 0; r < NO; ++r) {
        unrank(0, r, &s);
        for (int f = 0; f < 3; ++f) {
            state_t t = qturn(s, f);
            OT[f][r] = (uint16_t) orank(&t);
        }
    }
}

/* Generic BFS over a coordinate space given quarter-turn tables. */
static uint8_t *bfs(uint32_t n, uint32_t start,
                    uint32_t (*next)(uint32_t, int, void *), void *ctx)
{
    uint8_t *d = malloc(n);
    uint32_t *q = malloc((size_t) n * 4);
    memset(d, 0xFF, n);
    uint32_t head = 0, tail = 0;
    d[start] = 0;
    q[tail++] = start;
    while (head < tail) {
        uint32_t x = q[head++];
        for (int f = 0; f < 3; ++f) {
            uint32_t y = x;
            for (int t = 0; t < 3; ++t) {
                y = next(y, f, ctx);
                if (d[y] == 0xFF) {
                    d[y] = (uint8_t) (d[x] + 1);
                    q[tail++] = y;
                }
            }
        }
    }
    free(q);
    return d;
}

static uint32_t full_next(uint32_t x, int f, void *ctx)
{
    (void) ctx;
    return (uint32_t) PT[f][x / NO] * NO + OT[f][x % NO];
}

/* Positions-of-a-cubie-subset coordinate, dense. */
typedef struct {
    int k;
    uint8_t cubies[C];
    uint32_t n;            /* dense size */
    int32_t *dense;        /* sparse key -> dense id */
    uint16_t (*T)[3];      /* dense transitions */
    uint32_t *from_perm;   /* perm rank -> dense id */
} posc_t;

static uint32_t pos_key(const posc_t *c, const state_t *s)
{
    uint32_t key = 0;
    for (int j = 0; j < c->k; ++j) {
        int at = 0;
        while (s->p[at] != c->cubies[j])
            ++at;
        key = key * 7 + (uint32_t) at;
    }
    return key;
}

static void posc_init(posc_t *c, int k, const uint8_t *cub)
{
    uint32_t keys = 1;
    c->k = k;
    memcpy(c->cubies, cub, (size_t) k);
    for (int j = 0; j < k; ++j)
        keys *= 7;
    c->dense = malloc(keys * sizeof *c->dense);
    for (uint32_t i = 0; i < keys; ++i)
        c->dense[i] = -1;
    c->from_perm = malloc(NP * sizeof *c->from_perm);
    c->n = 0;
    uint32_t *rep = malloc(NP * sizeof *rep);
    for (uint32_t r = 0; r < NP; ++r) {
        state_t s;
        unrank(r, 0, &s);
        uint32_t key = pos_key(c, &s);
        if (c->dense[key] < 0) {
            rep[c->n] = r;
            c->dense[key] = (int32_t) c->n++;
        }
        c->from_perm[r] = (uint32_t) c->dense[key];
    }
    c->T = malloc(c->n * sizeof *c->T);
    for (uint32_t i = 0; i < c->n; ++i)
        for (int f = 0; f < 3; ++f)
            c->T[i][f] = (uint16_t) c->from_perm[PT[f][rep[i]]];
    free(rep);
}

typedef struct {
    posc_t *pc;
} oxpos_ctx;

static uint32_t oxpos_next(uint32_t x, int f, void *v)
{
    posc_t *pc = ((oxpos_ctx *) v)->pc;
    uint32_t a = x / NO, o = x % NO;
    return (uint32_t) pc->T[a][f] * NO + OT[f][o];
}

static uint32_t perm_next(uint32_t x, int f, void *v)
{
    (void) v;
    return PT[f][x];
}

static uint32_t ori_next(uint32_t x, int f, void *v)
{
    (void) v;
    return OT[f][x];
}

/* Heuristic: max over a list of (pos-coordinate x orientation) PDBs plus
 * optional perm-only and ori-only tables. */
#define MAXH 4
typedef struct {
    const char *name;
    int nox;
    posc_t *pc[MAXH];
    uint8_t *ox[MAXH];
    uint8_t *hp, *ho;
} heur_t;

static inline int H(const heur_t *h, uint32_t p, uint32_t o)
{
    int v = 0;
    if (h->hp && h->hp[p] > v)
        v = h->hp[p];
    if (h->ho && h->ho[o] > v)
        v = h->ho[o];
    for (int i = 0; i < h->nox; ++i) {
        int w = h->ox[i][h->pc[i]->from_perm[p] * NO + o];
        if (w > v)
            v = w;
    }
    return v;
}

static uint64_t nodes;
static int path[16];

static int dfs(const heur_t *h, uint32_t p, uint32_t o, int g, int bound,
               int last)
{
    ++nodes;
    int hv = H(h, p, o);
    if (g + hv > bound)
        return 0;
    if (p == 0 && o == 0)
        return 1;
    for (int f = 0; f < 3; ++f) {
        if (f == last)
            continue;
        uint32_t np = p, no = o;
        for (int t = 0; t < 3; ++t) {
            np = PT[f][np];
            no = OT[f][no];
            path[g] = f * 3 + t;
            if (dfs(h, np, no, g + 1, bound, f))
                return 1;
        }
    }
    return 0;
}

static int solve(const heur_t *h, uint32_t p, uint32_t o)
{
    for (int bound = H(h, p, o);; ++bound)
        if (dfs(h, p, o, 0, bound, -1))
            return bound;
}

static void evaluate(const heur_t *h)
{
    /* Admissibility (H1) and quality. */
    uint64_t hsum = 0;
    for (uint32_t x = 0; x < N; ++x) {
        int v = H(h, x / NO, x % NO);
        if (v > dist[x]) {
            printf("%s: INADMISSIBLE at %u\n", h->name, x);
            return;
        }
        hsum += (uint64_t) v;
    }
    uint64_t worst = 0, total = 0, cnt = 0;
    uint32_t worst_x = 0;
    for (uint32_t x = 0; x < N; ++x) {
        if (dist[x] != 11)
            continue;
        nodes = 0;
        int len = solve(h, x / NO, x % NO);
        if (len != 11) {
            printf("%s: suboptimal\n", h->name);
            return;
        }
        total += nodes;
        ++cnt;
        if (nodes > worst) {
            worst = nodes;
            worst_x = x;
        }
    }
    /* Sample of depth 9/10 states. */
    uint64_t t9 = 0, c9 = 0, t10 = 0, c10 = 0;
    for (uint32_t x = 7; x < N; x += 997) {
        if (dist[x] != 9 && dist[x] != 10)
            continue;
        nodes = 0;
        solve(h, x / NO, x % NO);
        if (dist[x] == 9)
            t9 += nodes, ++c9;
        else
            t10 += nodes, ++c10;
    }
    nodes = 0;
    {
        state_t s = {{1, 0, 2, 3, 4, 5, 6}, {0}};
        solve(h, prank(&s), orank(&s));
    }
    printf("%-34s mean h %.3f | d11 worst %7llu (x=%u) mean %8.1f | "
           "d10 %7.1f d9 %6.1f | sample %llu\n",
           h->name, (double) hsum / N, (unsigned long long) worst, worst_x,
           (double) total / (double) cnt, (double) t10 / (double) c10,
           (double) t9 / (double) c9, (unsigned long long) nodes);
    fflush(stdout);
}

int main(void)
{
    build_tables();
    dist = bfs(N, 0, full_next, NULL);
    int hist[16] = {0};
    for (uint32_t x = 0; x < N; ++x)
        ++hist[dist[x]];
    for (int d = 0; d < 12; ++d)
        printf("d=%d %d\n", d, hist[d]);

    uint8_t *hp = bfs(NP, 0, perm_next, NULL);
    uint8_t *ho = bfs(NO, 0, ori_next, NULL);
    int mp = 0, mo = 0;
    for (int i = 0; i < NP; ++i)
        mp = hp[i] > mp ? hp[i] : mp;
    for (int i = 0; i < NO; ++i)
        mo = ho[i] > mo ? ho[i] : mo;
    printf("perm-only max %d, ori-only max %d\n", mp, mo);

    heur_t h0 = {"zero", 0, {0}, {0}, NULL, NULL};
    (void) h0;
    heur_t h1 = {"perm", 0, {0}, {0}, hp, NULL};
    heur_t h2 = {"ori", 0, {0}, {0}, NULL, ho};
    heur_t h3 = {"max(perm, ori)", 0, {0}, {0}, hp, ho};
    evaluate(&h3);
    (void) h1;
    (void) h2;

    /* ori x positions of k cubies, for a few subsets */
    static const uint8_t subsets[][4] = {
        {0, 1}, {0, 4}, {3, 6}, {0, 1, 2}, {0, 1, 3}, {0, 1, 4}, {0, 2, 4},
        {1, 3, 5}, {0, 4, 6}, {3, 5, 6}, {2, 4, 6}, {1, 4, 6}, {0, 1, 2, 3},
    };
    static const int sizes[] = {2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4};
    posc_t pcs[16];
    uint8_t *oxs[16];
    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; ++i) {
        posc_init(&pcs[i], sizes[i], subsets[i]);
        oxpos_ctx ctx = {&pcs[i]};
        oxs[i] = bfs(pcs[i].n * NO, 0, oxpos_next, &ctx);
        int m = 0;
        for (uint32_t j = 0; j < pcs[i].n * NO; ++j)
            m = oxs[i][j] > m ? oxs[i][j] : m;
        char *name = malloc(64);
        snprintf(name, 64, "ori x pos{%d%s%d%s%d%s%d} n=%u max=%d",
                 subsets[i][0], ",", subsets[i][1], sizes[i] > 2 ? "," : "",
                 sizes[i] > 2 ? subsets[i][2] : 0, sizes[i] > 3 ? "," : "",
                 sizes[i] > 3 ? subsets[i][3] : 0, pcs[i].n * NO, m);
        heur_t h = {name, 1, {&pcs[i]}, {oxs[i]}, hp, NULL};
        if (sizes[i] < 4)
            evaluate(&h);
        else {
            heur_t hh = {name, 1, {&pcs[i]}, {oxs[i]}, NULL, NULL};
            evaluate(&hh);
        }
    }
    /* Pairs of 3-subsets plus perm. */
    int pairs[][2] = {{3, 9}, {3, 10}, {5, 10}, {4, 9}, {6, 9}, {7, 8}};
    for (unsigned i = 0; i < sizeof pairs / sizeof *pairs; ++i) {
        int a = pairs[i][0], b = pairs[i][1];
        char *name = malloc(64);
        snprintf(name, 64, "perm+ox[%d]+ox[%d]", a, b);
        heur_t h = {name, 2, {&pcs[a], &pcs[b]}, {oxs[a], oxs[b]}, hp, NULL};
        evaluate(&h);
    }
    return 0;
}
