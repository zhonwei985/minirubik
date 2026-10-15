/* Host gate H3 and search measurements for rv32/ida.c.
 *
 *   check STATE   solve one state: moves, length, children generated
 *   check --d11   every distance-11 state: worst and mean children
 *   check --all   gate H3: all 3,674,160 states; each solution must be as
 *                 long as the exact BFS distance and must replay to solved
 *   check --list D   every state at exact distance D, one per line
 *
 *   cc -O2 -o rv32/check rv32/check.c rv32/ida.c
 */
#include <time.h>

#include "ida.h"
#include "model.h"

static const char *const names[MOVES] = {"R",  "R2", "R'", "B", "B2",
                                         "B'", "D",  "D2", "D'"};

/* Replays the moves on the cubie model, independent of the rank tables. */
static int replays(const state_t *s, const uint8_t *moves, int n)
{
    state_t t = *s;
    for (int i = 0; i < n; ++i) {
        if (moves[i] >= MOVES)
            return 0;
        t = apply_move(t, moves[i]);
    }
    return is_solved(&t);
}

int main(int argc, char **argv)
{
    uint8_t moves[16];
    if (argc == 2 && argv[1][0] != '-') {
        state_t s;
        if (!ida_parse(argv[1], s.p, s.o)) {
            fputs("invalid state\n", stderr);
            return 2;
        }
        int n = ida_solve(s.p, s.o, moves);
        for (int i = 0; i < n; ++i)
            printf("%s%s", i ? " " : "", names[moves[i]]);
        printf("\n%d moves, %u children generated\n", n, ida_nodes);
        return n < 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--list")) {
        /* Every state at one exact distance, one per line, for the Ripes
         * sweep in rv32/measure.py. */
        int want = atoi(argv[2]);
        build_moves();
        uint8_t *dist = exact_distances();
        for (uint32_t x = 0; x < STATES; ++x)
            if (dist[x] == want) {
                state_t s;
                char str[15];
                unrank(x / ORIENTATIONS, x % ORIENTATIONS, &s);
                state_string(&s, str);
                puts(str);
            }
        free(dist);
        return 0;
    }
    int all = argc == 2 && !strcmp(argv[1], "--all");
    if (!all && !(argc == 2 && !strcmp(argv[1], "--d11"))) {
        fputs("usage: check STATE | --d11 | --all\n", stderr);
        return 2;
    }

    build_moves();
    uint8_t *dist = exact_distances();
    uint64_t sum[12] = {0}, count[12] = {0}, worst[12] = {0};
    uint32_t worst_x[12] = {0};
    clock_t t0 = clock();
    for (uint32_t x = 0; x < STATES; ++x) {
        int d = dist[x];
        if (!all && d != 11)
            continue;
        /* Go through the same 14-digit string a user would type. */
        state_t s, q;
        char str[15];
        unrank(x / ORIENTATIONS, x % ORIENTATIONS, &s);
        state_string(&s, str);
        if (!ida_parse(str, q.p, q.o)) {
            printf("FAIL: %s rejected by ida_parse\n", str);
            return 1;
        }
        int n = ida_solve(q.p, q.o, moves);
        if (n != d || !replays(&s, moves, n)) {
            printf("FAIL: %s returned %d moves, exact distance %d\n", str, n,
                   d);
            return 1;
        }
        sum[d] += ida_nodes;
        ++count[d];
        if (ida_nodes > worst[d]) {
            worst[d] = ida_nodes;
            worst_x[d] = x;
        }
    }
    double secs = (double) (clock() - t0) / CLOCKS_PER_SEC;
    printf("%s ok: %llu states, every solution as long as the exact distance "
           "and replaying to solved (%.1f s)\n",
           all ? "H3" : "distance 11", (unsigned long long) (all ? STATES : count[11]),
           secs);
    printf("depth   states   mean children   worst children   worst state\n");
    for (int d = 0; d < 12; ++d) {
        if (!count[d])
            continue;
        state_t s;
        char str[15];
        unrank(worst_x[d] / ORIENTATIONS, worst_x[d] % ORIENTATIONS, &s);
        state_string(&s, str);
        printf("%5d %8llu %15.1f %16llu   %s\n", d,
               (unsigned long long) count[d], (double) sum[d] / count[d],
               (unsigned long long) worst[d], str);
    }
    free(dist);
    return 0;
}
