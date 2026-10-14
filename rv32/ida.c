/* IDA* with h = max(dP[p], dO[o]); see ida.h.
 *
 * Iterative deepening: run a depth-first search that refuses any node with
 * g + h > bound, starting at bound = h(start) and raising it by one until a
 * search reaches solved. Because h never overestimates, no shorter solution
 * can be cut off, so the first solution found is a shortest one (Korf 1985).
 *
 * The depth-first search keeps its own stack of frames instead of recursing.
 * Each frame holds the node, which face it is expanding and how many quarter
 * turns of that face have been applied to its cursor. A face's three children
 * are one, two and three quarter turns of it (R, R2, R'), so each child is
 * one table lookup from the previous one.
 */
#include "ida.h"
#include "pdb.h"

uint32_t ida_nodes;

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
    /* The twists must sum to 0 mod 3. sum <= 14, so subtracting 3 until it is
     * below 3 is exact and needs no divide. */
    while (sum >= 3)
        sum -= 3;
    return sum == 0;
}

static inline uint32_t heuristic(uint32_t p, uint32_t o)
{
    uint32_t a = dP[p], b = dO[o];
    return a > b ? a : b;
}

typedef struct {
    uint32_t p, o;   /* the node at this depth */
    uint32_t skip;   /* face of the move that led here, 3 at the root */
    uint32_t face;   /* face being expanded */
    uint32_t turns;  /* quarter turns of that face applied so far */
    uint32_t cp, co; /* the last child generated on that face */
} frame_t;

int ida_solve(const uint8_t perm[7], const uint8_t ori[7], uint8_t *moves)
{
    frame_t st[IDA_MAX_MOVES + 1];

    /* Ranks, as solver.c computes them. The Lehmer code is evaluated by
     * Horner's rule with constant factors, which a compiler turns into
     * shifts and adds; the base-3 rank is o * 3 + digit. */
    uint32_t c[6];
    for (int i = 0; i < 6; ++i) {
        uint32_t n = 0;
        for (int j = i + 1; j < 7; ++j)
            n += perm[j] < perm[i];
        c[i] = n;
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

    ida_nodes = 0;
    if (p == 0 && o == 0)
        return 0;

    for (uint32_t bound = heuristic(p, o); bound <= IDA_MAX_MOVES; ++bound) {
        frame_t *fr = st;
        uint32_t g = 0; /* depth of fr */
        fr->p = fr->cp = p;
        fr->o = fr->co = o;
        fr->skip = 3;
        fr->face = 0;
        fr->turns = 0;
        for (;;) {
            /* Test for "all faces done" first: the root's skip is 3, and
             * testing the skip first would step its face past 3. */
            if (fr->face == 3) {
                /* Every face of this node is done: back to the parent. */
                if (fr == st)
                    break;
                --fr;
                --g;
                continue;
            }
            if (fr->face == fr->skip) {
                /* Never turn the face the node was reached by: R then R2
                 * is R', a path one move longer that the search already
                 * covers. Its cursor was never touched, so just move on. */
                ++fr->face;
                continue;
            }
            if (fr->turns == 3) {
                /* R, R2, R' all tried: next face, cursor back to the node. */
                ++fr->face;
                fr->turns = 0;
                fr->cp = fr->p;
                fr->co = fr->o;
                continue;
            }
            /* Next child: one more quarter turn of the cursor. */
            fr->cp = PT[fr->face][fr->cp];
            fr->co = OT[fr->face][fr->co];
            ++fr->turns;
            ++ida_nodes;
            if (g + 1 + heuristic(fr->cp, fr->co) > bound)
                continue; /* cannot reach solved within the bound */
            if (fr->cp == 0 && fr->co == 0) {
                /* Solved at depth g + 1. Frames st..fr hold the path. */
                for (uint32_t i = 0; i <= g; ++i)
                    moves[i] = (uint8_t) (st[i].face * 3 + st[i].turns - 1);
                return (int) g + 1;
            }
            /* Descend into the child. */
            frame_t *ch = fr + 1;
            ch->p = ch->cp = fr->cp;
            ch->o = ch->co = fr->co;
            ch->skip = fr->face;
            ch->face = 0;
            ch->turns = 0;
            fr = ch;
            ++g;
        }
    }
    return -1;
}
