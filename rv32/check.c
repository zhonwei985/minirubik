/* Host gate H3 and search statistics.
 *
 *   check STATE      solve one state, print moves and node count
 *   check --d11      every distance-11 state: worst and mean node counts
 *   check --all      H3: every one of the 3,674,160 states, length must
 *                    equal the exact BFS distance and the path must solve
 *
 *   cc -O2 -o check rv32/check.c rv32/ida.c
 */
#include <time.h>

#include "ida.h"
#include "model.h"

static const char *const names[9] = {"R",  "R2", "R'", "B", "B2",
                                     "B'", "D",  "D2", "D'"};

static int verify(const state_t *s, const uint8_t *moves, int n, int want)
{
    state_t t = *s;
    if (n != want)
        return 0;
    for (int i = 0; i < n; ++i) {
        if (moves[i] >= 9 || (i && moves[i] / 3 == moves[i - 1] / 3))
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
        printf("\n%d moves, %u nodes\n", n, ida_nodes);
        return 0;
    }
    int all = argc == 2 && !strcmp(argv[1], "--all");
    if (!all && !(argc == 2 && !strcmp(argv[1], "--d11"))) {
        fputs("usage: check STATE | --d11 | --all\n", stderr);
        return 2;
    }
    uint8_t *dist = exact_distances();
    uint64_t nodes[12] = {0}, count[12] = {0}, worst[12] = {0};
    uint32_t worst_x[12] = {0};
    clock_t t0 = clock();
    for (uint32_t x = 0; x < STATES; ++x) {
        int d = dist[x];
        if (!all && d != 11)
            continue;
        state_t s;
        char str[15];
        unrank(x / ORIENTATIONS, x % ORIENTATIONS, &s);
        state_string(&s, str);
        state_t q;
        if (!ida_parse(str, q.p, q.o)) {
            printf("H3 FAIL: parse rejected %s\n", str);
            return 1;
        }
        int n = ida_solve(q.p, q.o, moves);
        if (!verify(&s, moves, n, d)) {
            printf("H3 FAIL: %s returned %d moves, distance %d\n", str, n, d);
            return 1;
        }
        nodes[d] += ida_nodes;
        ++count[d];
        if (ida_nodes > worst[d]) {
            worst[d] = ida_nodes;
            worst_x[d] = x;
        }
    }
    double secs = (double) (clock() - t0) / CLOCKS_PER_SEC;
    printf("%s ok: %s, every length equals the BFS distance and every path "
           "solves (%.1f s)\n",
           all ? "H3" : "d11", all ? "3674160 states" : "2644 states", secs);
    printf("depth  states    mean nodes  worst nodes  worst state\n");
    for (int d = 0; d < 12; ++d) {
        if (!count[d])
            continue;
        state_t s;
        char str[15];
        unrank(worst_x[d] / ORIENTATIONS, worst_x[d] % ORIENTATIONS, &s);
        state_string(&s, str);
        printf("%5d %8llu %12.1f %12llu  %s\n", d,
               (unsigned long long) count[d],
               (double) nodes[d] / (double) count[d],
               (unsigned long long) worst[d], str);
    }
    return 0;
}
