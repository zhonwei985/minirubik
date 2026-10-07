/* Host-side table generator and host gates H1, H2, H4.
 *
 * Emits the read-only data the RV32I solver links in, in two spellings that
 * describe the same bytes: rv32/tables.h for the C reference build and
 * rv32/tables.inc for the hand-written assembly.
 *
 * Search state: perm rank p (5,040), twist rank o (729), and two pattern
 * coordinates A and B, the positions of cubies {0,1,2} and {0,3,6} (210 each).
 * Heuristic: max(hp[p], A[o][a], B[o][b]), every term an exact distance in an
 * abstraction of the Cayley graph, hence admissible (checked below as H1).
 *
 * Layout, every coordinate stored as a byte offset so that no index on the
 * target is ever scaled at run time:
 *   PERM[5040]  { u16 next[3] = 8 * p', u16 h4 = 4 * hp[p] }        40,320 B
 *   ORI[729]    { u16 next[3] = 8 * o', u16 row = 53 * o }           5,832 B
 *   POSA[210]   { u16 next[3] = 8 * a', u8 byte = a >> 2,
 *                 u8 shift = 2 * (a & 3) }                            1,680 B
 *   POSB[210]   same layout for coordinate B                          1,680 B
 *   PDBA, PDBB  729 rows x 53 bytes, 2 bits per entry, dist mod 3   2 x 38,637 B
 *   HT4[40]     HT4[4h + v] = 4h', the h' in {h-1, h, h+1} with h' = v mod 3
 *   MAPA, MAPB  343-byte map from 49*pos0 + 7*pos1 + pos2 to a coordinate,
 *               used once per query to enter the coordinate at the root
 *
 *   cc -O2 -o gen rv32/gen.c && ./gen rv32
 */
#include "model.h"

enum { POS = 210, ROW = 53, KEYS = 343 };

static const uint8_t subset[2][3] = {{0, 1, 2}, {0, 3, 6}};

static uint16_t pt[3][PERMUTATIONS], ot[3][ORIENTATIONS];
static uint8_t hp[PERMUTATIONS];

typedef struct {
    uint8_t map[KEYS];    /* key -> id, 0xFF where positions collide */
    uint16_t key[POS];    /* id -> key */
    uint8_t next[POS][3]; /* quarter-turn transitions by id */
    uint8_t *pdb;         /* exact abstract distance, o * POS + id */
    uint8_t packed[ORIENTATIONS * ROW];
    int max;
} pattern_t;

static pattern_t pat[2];

static int fail(const char *what)
{
    fprintf(stderr, "FAIL: %s\n", what);
    exit(1);
}

static uint16_t positions_key(const state_t *s, const uint8_t *cubies)
{
    uint16_t key = 0;
    for (int j = 0; j < 3; ++j) {
        int at = 0;
        while (s->p[at] != cubies[j])
            ++at;
        key = (uint16_t) (key * 7 + at);
    }
    return key;
}

static void build_transitions(void)
{
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
}

/* Breadth-first over the 5,040 permutations alone. */
static void build_perm_table(void)
{
    static uint16_t q[PERMUTATIONS];
    unsigned head = 0, tail = 0;
    memset(hp, 0xFF, sizeof hp);
    hp[0] = 0;
    q[tail++] = 0;
    while (head < tail) {
        uint16_t x = q[head++];
        for (int f = 0; f < 3; ++f) {
            uint16_t y = x;
            for (int t = 0; t < 3; ++t) {
                y = pt[f][y];
                if (hp[y] == 0xFF) {
                    hp[y] = (uint8_t) (hp[x] + 1);
                    q[tail++] = y;
                }
            }
        }
    }
    if (tail != PERMUTATIONS)
        fail("H2: permutation table not fully populated");
}

/* Ids are assigned in breadth-first order from solved, so id 0 is solved. */
static void build_pattern(pattern_t *pt_, const uint8_t *cubies)
{
    static uint32_t rep[POS];
    state_t s, solved;
    unrank(0, 0, &solved);
    memset(pt_->map, 0xFF, sizeof pt_->map);
    unsigned n = 0, head = 0;
    uint16_t k0 = positions_key(&solved, cubies);
    pt_->map[k0] = 0;
    pt_->key[0] = k0;
    rep[n++] = 0;
    while (head < n) {
        unsigned id = head++;
        unrank(rep[id], 0, &s);
        for (int f = 0; f < 3; ++f) {
            state_t t = quarter_turn(s, f);
            uint16_t k = positions_key(&t, cubies);
            if (pt_->map[k] == 0xFF) {
                if (n == POS)
                    fail("pattern coordinate larger than 210");
                pt_->map[k] = (uint8_t) n;
                pt_->key[n] = k;
                rep[n++] = perm_rank(&t);
            }
            pt_->next[id][f] = pt_->map[k];
        }
    }
    if (n != POS)
        fail("H2: pattern coordinate not 210 values");

    /* Breadth-first over the 153,090 abstract states (o, id). */
    const uint32_t size = ORIENTATIONS * POS;
    uint8_t *d = malloc(size);
    uint32_t *q = malloc(size * sizeof *q);
    uint32_t qh = 0, qt = 0;
    memset(d, 0xFF, size);
    d[0] = 0;
    q[qt++] = 0;
    while (qh < qt) {
        uint32_t x = q[qh++];
        uint32_t o = x / POS, a = x % POS;
        for (int f = 0; f < 3; ++f) {
            uint32_t no = o, na = a;
            for (int t = 0; t < 3; ++t) {
                no = ot[f][no];
                na = pt_->next[na][f];
                uint32_t y = no * POS + na;
                if (d[y] == 0xFF) {
                    d[y] = (uint8_t) (d[x] + 1);
                    q[qt++] = y;
                }
            }
        }
    }
    free(q);
    if (qt != size)
        fail("H2: pattern database not fully populated");
    pt_->pdb = d;
    pt_->max = 0;
    for (uint32_t i = 0; i < size; ++i)
        if (d[i] > pt_->max)
            pt_->max = d[i];

    /* Pack two bits per entry, distance mod 3, rows of 53 bytes. */
    memset(pt_->packed, 0, sizeof pt_->packed);
    for (uint32_t o = 0; o < ORIENTATIONS; ++o)
        for (uint32_t a = 0; a < POS; ++a)
            pt_->packed[o * ROW + (a >> 2)] |=
                (uint8_t) ((d[o * POS + a] % 3) << (2 * (a & 3)));
}

/* The accessor the target uses, written once more on the host for H4. */
static unsigned packed_get(const uint8_t *packed, unsigned o, unsigned a)
{
    return packed[o * ROW + (a >> 2)] >> (2 * (a & 3)) & 3;
}

static uint8_t ht4[40];

static void build_ht4(void)
{
    memset(ht4, 0xFF, sizeof ht4);
    for (int h = 0; h < 10; ++h)
        for (int v = 0; v < 3; ++v)
            for (int c = h - 1; c <= h + 1; ++c)
                if (c >= 0 && c % 3 == v)
                    ht4[4 * h + v] = (uint8_t) (4 * c);
}

static uint32_t pattern_id(const pattern_t *p, const state_t *s, int which)
{
    return p->map[positions_key(s, subset[which])];
}

static void gates(void)
{
    /* H2: every table populated, maxima and solved entries checked. */
    int mp = 0;
    for (int i = 0; i < PERMUTATIONS; ++i)
        mp = hp[i] > mp ? hp[i] : mp;
    if (hp[0] != 0 || mp != 7)
        fail("H2: permutation table solved entry or maximum");
    for (int k = 0; k < 2; ++k)
        if (pat[k].pdb[0] != 0 || pat[k].max != 9)
            fail("H2: pattern database solved entry or maximum");
    printf("H2 ok: hp[0]=0 max %d; A[0]=0 max %d; B[0]=0 max %d; "
           "all entries reached\n",
           mp, pat[0].max, pat[1].max);

    /* H4: packed accessor against the unpacked table, every index. */
    unsigned long checked = 0, even = 0, odd = 0;
    for (int k = 0; k < 2; ++k)
        for (unsigned o = 0; o < ORIENTATIONS; ++o)
            for (unsigned a = 0; a < POS; ++a) {
                unsigned want = pat[k].pdb[o * POS + a] % 3;
                if (packed_get(pat[k].packed, o, a) != want)
                    fail("H4: packed accessor disagrees");
                ++checked;
                if ((o * POS + a) & 1)
                    ++odd;
                else
                    ++even;
            }
    /* H4, second half: the mod-3 recovery must reproduce the exact value on
     * every abstract edge, which needs |h(x) - h(y)| <= 1 across each edge. */
    unsigned long edges = 0;
    for (int k = 0; k < 2; ++k)
        for (unsigned o = 0; o < ORIENTATIONS; ++o)
            for (unsigned a = 0; a < POS; ++a) {
                int h = pat[k].pdb[o * POS + a];
                for (int f = 0; f < 3; ++f) {
                    unsigned no = o, na = a;
                    for (int t = 0; t < 3; ++t) {
                        no = ot[f][no];
                        na = pat[k].next[na][f];
                        int c = pat[k].pdb[no * POS + na];
                        unsigned v = packed_get(pat[k].packed, no, na);
                        if (ht4[4 * h + v] != 4 * c)
                            fail("H4: mod-3 recovery wrong on an edge");
                        ++edges;
                    }
                }
            }
    printf("H4 ok: %lu packed entries (%lu even, %lu odd index) and %lu "
           "edges recovered exactly\n",
           checked, even, odd, edges);

    /* H1: admissibility over all 3,674,160 states against exact BFS. */
    uint8_t *dist = exact_distances();
    uint64_t sum_h = 0, sum_d = 0;
    unsigned long exact = 0;
    int hist[12] = {0};
    for (uint32_t p = 0; p < PERMUTATIONS; ++p) {
        state_t s;
        unrank(p, 0, &s);
        uint32_t a = pattern_id(&pat[0], &s, 0);
        uint32_t b = pattern_id(&pat[1], &s, 1);
        for (uint32_t o = 0; o < ORIENTATIONS; ++o) {
            int h = hp[p];
            int ha = pat[0].pdb[o * POS + a], hb = pat[1].pdb[o * POS + b];
            h = ha > h ? ha : h;
            h = hb > h ? hb : h;
            int d = dist[p * ORIENTATIONS + o];
            if (h > d)
                fail("H1: heuristic exceeds exact distance");
            exact += h == d;
            sum_h += (uint64_t) h;
            sum_d += (uint64_t) d;
            ++hist[d];
        }
    }
    printf("H1 ok: h <= d on all %d states; mean h %.3f vs mean d %.3f; "
           "h == d on %lu states (%.1f%%)\n",
           STATES, (double) sum_h / STATES, (double) sum_d / STATES, exact,
           100.0 * exact / STATES);
    printf("distance histogram:");
    for (int d = 0; d < 12; ++d)
        printf(" %d", hist[d]);
    printf("\n");
    free(dist);
}

/* ---- output ---------------------------------------------------------- */

static FILE *hf, *sf;
static size_t total_bytes;

static void put_u16_block(const char *name, const uint16_t *v, size_t n,
                          int width)
{
    fprintf(hf, "static const uint16_t %s[%zu] = {", name, n);
    fprintf(sf, "    .align 2\n%s:\n", name);
    for (size_t i = 0; i < n; ++i) {
        if (i % width == 0)
            fprintf(hf, "\n   ");
        fprintf(hf, " %u,", v[i]);
        if (i % width == 0)
            fprintf(sf, "    .half ");
        fprintf(sf, "%u%s", v[i], (i % width == (size_t) width - 1 || i == n - 1)
                                      ? "\n"
                                      : ",");
    }
    fprintf(hf, "\n};\n");
    total_bytes += n * 2;
}

static void put_u8_block(const char *name, const uint8_t *v, size_t n,
                         int width)
{
    fprintf(hf, "static const uint8_t %s[%zu] __attribute__((aligned(4))) = {",
            name, n);
    fprintf(sf, "    .align 2\n%s:\n", name);
    for (size_t i = 0; i < n; ++i) {
        if (i % width == 0)
            fprintf(hf, "\n   ");
        fprintf(hf, " %u,", v[i]);
        if (i % width == 0)
            fprintf(sf, "    .byte ");
        fprintf(sf, "%u%s", v[i], (i % width == (size_t) width - 1 || i == n - 1)
                                      ? "\n"
                                      : ",");
    }
    fprintf(hf, "\n};\n");
    total_bytes += n;
}

static void emit(const char *dir)
{
    char path[512];
    snprintf(path, sizeof path, "%s/tables.h", dir);
    hf = fopen(path, "w");
    snprintf(path, sizeof path, "%s/tables.inc", dir);
    sf = fopen(path, "w");
    if (!hf || !sf)
        fail("cannot open output");
    fprintf(hf, "/* Generated by rv32/gen.c. Do not edit. */\n"
                "#include <stdint.h>\n");
    fprintf(sf, "# Generated by rv32/gen.c. Do not edit.\n"
                "# Read-only tables; Ripes has no .rodata, so they live in "
                ".data.\n");

    static uint16_t perm[PERMUTATIONS * 4], ori[ORIENTATIONS * 4];
    for (int p = 0; p < PERMUTATIONS; ++p) {
        for (int f = 0; f < 3; ++f)
            perm[4 * p + f] = (uint16_t) (8 * pt[f][p]);
        perm[4 * p + 3] = (uint16_t) (4 * hp[p]);
    }
    for (int o = 0; o < ORIENTATIONS; ++o) {
        for (int f = 0; f < 3; ++f)
            ori[4 * o + f] = (uint16_t) (8 * ot[f][o]);
        ori[4 * o + 3] = (uint16_t) (ROW * o);
    }
    put_u16_block("PERM", perm, PERMUTATIONS * 4, 16);
    put_u16_block("ORI", ori, ORIENTATIONS * 4, 16);
    for (int k = 0; k < 2; ++k) {
        static uint8_t rec[POS * 8];
        for (int a = 0; a < POS; ++a) {
            for (int f = 0; f < 3; ++f) {
                uint16_t n = (uint16_t) (8 * pat[k].next[a][f]);
                rec[8 * a + 2 * f] = (uint8_t) (n & 0xFF);
                rec[8 * a + 2 * f + 1] = (uint8_t) (n >> 8);
            }
            rec[8 * a + 6] = (uint8_t) (a >> 2);
            rec[8 * a + 7] = (uint8_t) (2 * (a & 3));
        }
        put_u8_block(k ? "POSB" : "POSA", rec, sizeof rec, 16);
    }
    put_u8_block("PDBA", pat[0].packed, sizeof pat[0].packed, 32);
    put_u8_block("PDBB", pat[1].packed, sizeof pat[1].packed, 32);
    put_u8_block("HT4", ht4, sizeof ht4, 20);
    put_u8_block("MAPA", pat[0].map, KEYS, 49);
    put_u8_block("MAPB", pat[1].map, KEYS, 49);
    fprintf(hf, "#define TABLE_BYTES %zu\n", total_bytes);
    fprintf(sf, "# total table bytes: %zu\n", total_bytes);
    fclose(hf);
    fclose(sf);
    printf("tables: %zu bytes written to %s/tables.{h,inc}\n", total_bytes,
           dir);
}

int main(int argc, char **argv)
{
    build_transitions();
    build_perm_table();
    for (int k = 0; k < 2; ++k)
        build_pattern(&pat[k], subset[k]);
    build_ht4();
    gates();
    emit(argc > 1 ? argv[1] : "rv32");
    return 0;
}
