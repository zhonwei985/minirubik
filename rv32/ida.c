/* See ida.h. Every coordinate is carried as a byte offset into its record
 * table, so a transition is one add and one halfword load and no index is
 * ever scaled at run time. The heuristic values are carried times four for
 * the same reason: HT4 is indexed by 4h + v and returns 4h'.
 */
#include "ida.h"
#include "tables.h"

uint32_t ida_nodes;

typedef struct {
    uint16_t next[3]; /* byte offset of the quarter-turned coordinate */
    uint16_t aux;     /* PERM: 4 * hp, ORI: 53 * o (PDB row offset) */
} rec_t;

typedef struct {
    uint16_t next[3];
    uint8_t byte, shift; /* where this coordinate's 2 bits sit in a row */
} pos_t;

#define REC(table, off) ((const rec_t *) ((const uint8_t *) (table) + (off)))
#define POSR(table, off) ((const pos_t *) ((const uint8_t *) (table) + (off)))

static inline uint32_t pdb_get(const uint8_t *pdb, uint32_t row,
                               const pos_t *pos)
{
    return (uint32_t) (pdb[row + pos->byte] >> pos->shift) & 3U;
}

int ida_parse(const char *in, uint8_t p[7], uint8_t o[7])
{
    uint32_t seen = 0, sum = 0;
    for (int i = 0; i < 7; ++i) {
        uint32_t c = (uint32_t) (uint8_t) in[i] - '1';
        uint32_t t = (uint32_t) (uint8_t) in[i + 7] - '1';
        if (c >= 7 || t >= 3 || (seen >> c & 1U))
            return 0;
        seen |= 1U << c;
        p[i] = (uint8_t) c;
        o[i] = (uint8_t) t;
        sum += t;
    }
    if (in[14] != '\0')
        return 0;
    /* sum <= 14; subtracting 3 until below 3 is exact and needs no divide. */
    while (sum >= 3)
        sum -= 3;
    return sum == 0;
}

/* Exact abstract distance by greedy descent: from a state at distance d the
 * neighbour holding (d - 1) mod 3 is at distance d - 1, because the three
 * candidate distances d - 1, d, d + 1 differ mod 3. */
static uint32_t exact4(const uint8_t *pdb, const uint8_t *pos, uint32_t o8,
                       uint32_t a8)
{
    uint32_t h4 = 0;
    while (o8 | a8) {
        uint32_t v = pdb_get(pdb, REC(ORI, o8)->aux, POSR(pos, a8));
        uint32_t want = v ? v - 1 : 2;
        for (uint32_t f = 0; f < 3; ++f) {
            uint32_t no = o8, na = a8;
            for (int t = 0; t < 3; ++t) {
                no = REC(ORI, no)->next[f];
                na = POSR(pos, na)->next[f];
                if (pdb_get(pdb, REC(ORI, no)->aux, POSR(pos, na)) == want)
                    goto down;
            }
            continue;
        down:
            o8 = no;
            a8 = na;
            break;
        }
        h4 += 4;
    }
    return h4;
}

typedef struct {
    uint32_t p, o, a, b;     /* node at this depth */
    uint32_t ha4, hb4;       /* exact pattern distances, times four */
    uint32_t face, turn;     /* cursor: face being expanded, turns applied */
    uint32_t cp, co, ca, cb; /* cursor: last child generated on that face */
    uint32_t skip;           /* face of the move that led here */
} frame_t;

int ida_solve(const uint8_t perm[7], const uint8_t ori[7], uint8_t *moves)
{
    frame_t st[IDA_MAX_MOVES + 1];
    uint32_t where[7];

    /* Enter the coordinates: Lehmer rank by Horner with constant factors,
     * base-3 rank, and the positions of the two cubie triples. */
    uint32_t c[7];
    for (int i = 0; i < 7; ++i) {
        uint32_t n = 0;
        for (int j = i + 1; j < 7; ++j)
            n += perm[j] < perm[i];
        c[i] = n;
        where[perm[i]] = (uint32_t) i;
    }
    uint32_t p = c[0];
    p = p * 6 + c[1];
    p = p * 5 + c[2];
    p = p * 4 + c[3];
    p = p * 3 + c[4];
    p = p * 2 + c[5];
    uint32_t o = 0;
    for (int i = 0; i < 6; ++i)
        o = o * 3 + ori[i];
    uint32_t ka = where[0] * 49 + where[1] * 7 + where[2];
    uint32_t kb = where[0] * 49 + where[3] * 7 + where[6];

    frame_t *fr = st;
    fr->p = p * 8;
    fr->o = o * 8;
    fr->a = (uint32_t) MAPA[ka] * 8;
    fr->b = (uint32_t) MAPB[kb] * 8;
    if ((fr->p | fr->o) == 0)
        return 0;
    fr->ha4 = exact4(PDBA, POSA, fr->o, fr->a);
    fr->hb4 = exact4(PDBB, POSB, fr->o, fr->b);
    fr->skip = 3;

    uint32_t h4 = REC(PERM, fr->p)->aux;
    if (fr->ha4 > h4)
        h4 = fr->ha4;
    if (fr->hb4 > h4)
        h4 = fr->hb4;

    ida_nodes = 0;
    for (uint32_t bound4 = h4; bound4 <= 4 * IDA_MAX_MOVES; bound4 += 4) {
        uint32_t limit4 = bound4 - 4; /* budget left for a child of fr */
        fr = st;
        fr->face = 0;
        for (;;) {
            if (fr->face == fr->skip)
                ++fr->face;
            if (fr->face >= 3) {
                /* Every face of this node exhausted: pop. */
                if (fr == st)
                    break;
                --fr;
                limit4 += 4;
                goto resume;
            }
            fr->turn = 0;
            fr->cp = fr->p;
            fr->co = fr->o;
            fr->ca = fr->a;
            fr->cb = fr->b;
            if (limit4 == 0) {
                /* Children of this node sit at the bound, where only the
                 * goal passes: test p and o, skip the pattern coordinates. */
                uint32_t f = fr->face, cp = fr->cp, co = fr->co;
                for (uint32_t t = 0; t < 3; ++t) {
                    ++ida_nodes;
                    cp = REC(PERM, cp)->next[f];
                    co = REC(ORI, co)->next[f];
                    if ((cp | co) == 0) {
                        fr->turn = t + 1;
                        goto found;
                    }
                }
                ++fr->face;
                continue;
            }
        resume:
            while (fr->turn < 3) {
                uint32_t f = fr->face;
                ++fr->turn;
                ++ida_nodes;
                const rec_t *pr = REC(PERM, fr->cp = REC(PERM, fr->cp)->next[f]);
                const rec_t *orr = REC(ORI, fr->co = REC(ORI, fr->co)->next[f]);
                const pos_t *ar = POSR(POSA, fr->ca = POSR(POSA, fr->ca)->next[f]);
                const pos_t *br = POSR(POSB, fr->cb = POSR(POSB, fr->cb)->next[f]);
                if (pr->aux > limit4)
                    continue;
                uint32_t row = orr->aux;
                uint32_t ha4 = HT4[fr->ha4 + pdb_get(PDBA, row, ar)];
                if (ha4 > limit4)
                    continue;
                uint32_t hb4 = HT4[fr->hb4 + pdb_get(PDBB, row, br)];
                if (hb4 > limit4)
                    continue;
                /* Push the child. */
                frame_t *ch = fr + 1;
                ch->p = fr->cp;
                ch->o = fr->co;
                ch->a = fr->ca;
                ch->b = fr->cb;
                ch->ha4 = ha4;
                ch->hb4 = hb4;
                ch->skip = f;
                ch->face = 0;
                fr = ch;
                limit4 -= 4;
                goto expand;
            }
            ++fr->face;
            continue;
        expand:;
        }
    }
    return -1;

found:
    for (frame_t *x = st; x <= fr; ++x)
        moves[x - st] = (uint8_t) (x->face * 3 + x->turn - 1);
    return (int) (fr - st) + 1;
}
