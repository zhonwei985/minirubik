/* Screens every pair of three-cubie pattern databases, with and without the
 * permutation table, by worst-case IDA* nodes over all distance-11 states.
 * Pairs whose worst case exceeds 80,000 nodes are cut short and not printed.
 *   cc -O2 -I tools -o pairs tools/pairs.c && ./pairs
 */
#define main explore_main
#include "explore.c"
#undef main
static uint64_t d11(const heur_t *h, uint64_t cap, double *mean)
{
    uint64_t worst = 0, total = 0, cnt = 0;
    for (uint32_t x = 0; x < N; ++x) {
        if (dist[x] != 11) continue;
        nodes = 0; solve(h, x / NO, x % NO);
        total += nodes; ++cnt;
        if (nodes > worst) worst = nodes;
        if (worst > cap) break;
    }
    *mean = (double) total / cnt;
    return worst;
}
int main(void)
{
    build_tables();
    dist = bfs(N, 0, full_next, NULL);
    uint8_t *hp = bfs(NP, 0, perm_next, NULL);
    posc_t pcs[35]; uint8_t *oxs[35]; uint8_t sub[35][3]; int ns = 0;
    for (int a = 0; a < 7; ++a) for (int b = a + 1; b < 7; ++b) for (int c = b + 1; c < 7; ++c) {
        sub[ns][0] = a; sub[ns][1] = b; sub[ns][2] = c;
        posc_init(&pcs[ns], 3, sub[ns]);
        oxpos_ctx ctx = {&pcs[ns]};
        oxs[ns] = bfs(pcs[ns].n * NO, 0, oxpos_next, &ctx);
        ++ns;
    }
    uint64_t best = ~0ull;
    for (int i = 0; i < ns; ++i) for (int j = i + 1; j < ns; ++j) {
        heur_t h = {"", 2, {&pcs[i], &pcs[j]}, {oxs[i], oxs[j]}, hp, NULL};
        double mean; uint64_t w = d11(&h, 80000, &mean);
        if (w <= 80000) {
            heur_t h2 = {"", 2, {&pcs[i], &pcs[j]}, {oxs[i], oxs[j]}, NULL, NULL};
            double m2; uint64_t w2 = d11(&h2, ~0ull, &m2);
            printf("{%d%d%d}+{%d%d%d}: worst %llu mean %.0f | without perm: worst %llu mean %.0f\n",
                   sub[i][0], sub[i][1], sub[i][2], sub[j][0], sub[j][1], sub[j][2],
                   (unsigned long long) w, mean, (unsigned long long) w2, m2);
            fflush(stdout);
        }
    }
    return 0;
}
