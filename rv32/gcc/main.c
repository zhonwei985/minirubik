/* Driver for the gcc -O2 -march=rv32i reference build of rv32/ida.c.
 *
 * Does what rv32/minirubik.S does, case for case: parse, solve, print the
 * moves, replay them on an independent cubie model (T5) and check the
 * length, then print PASS/FAIL and exit with the number of failures. The
 * verification uses the same algorithms as the assembly (subtraction instead
 * of / and %), so the two programs differ only in who wrote the instructions.
 *
 * Freestanding: no libc, output through Ripes' ecalls.
 *   QUERY     the state to solve (default the assignment's query)
 *   NO_TESTS  drop the test list, for single-state measurements
 */
#include <stdint.h>

#include "../ida.h"

#ifndef QUERY
#define QUERY "21345671111111"
#endif

static void print_str(const char *s)
{
    register const char *a0 __asm__("a0") = s;
    register int a7 __asm__("a7") = 4;
    __asm__ volatile("ecall" : : "r"(a0), "r"(a7) : "memory");
}

static void print_int(int v)
{
    register int a0 __asm__("a0") = v;
    register int a7 __asm__("a7") = 1;
    __asm__ volatile("ecall" : : "r"(a0), "r"(a7));
}

static const struct {
    char state[15];
    uint8_t distance; /* exact, from host gate H3 */
} tests[] = {
#ifndef NO_TESTS
    {"12345671111111", 0},  /* solved */
    {"24173562322133", 3},  /* short scramble R B' D2, solved by D2 B R' */
    {"54721631111111", 11}, /* distance 11, the costliest for this search */
#endif
    {"", 0},
};

/* Quarter-turn model for verification, independent of the rank tables. */
static const uint8_t source[3][7] = {
    {1, 4, 2, 0, 3, 5, 6}, {0, 1, 2, 4, 5, 6, 3}, {0, 2, 5, 3, 1, 4, 6}};
static const uint8_t twist[3][7] = {
    {1, 2, 0, 2, 1, 0, 0}, {0, 0, 0, 1, 2, 1, 2}, {0, 0, 0, 0, 0, 0, 0}};
static const char names[9][4] = {"R ",  "R2 ", "R' ", "B ", "B2 ",
                                  "B' ", "D ",  "D2 ", "D' "};

static uint8_t work_p[7], work_o[7];

static void quarter_turn(int face)
{
    uint8_t p[7], o[7];
    for (int i = 0; i < 7; ++i) {
        p[i] = work_p[i];
        o[i] = work_o[i];
    }
    for (int i = 0; i < 7; ++i) {
        int src = source[face][i];
        int t = o[src] + twist[face][i]; /* at most 4 */
        work_p[i] = p[src];
        work_o[i] = (uint8_t) (t >= 3 ? t - 3 : t);
    }
}

static void apply_move(int m)
{
    int face = 0;
    while (m >= 3) {
        m -= 3;
        ++face;
    }
    for (int i = 0; i <= m; ++i)
        quarter_turn(face);
}

static int work_solved(void)
{
    for (int i = 0; i < 7; ++i)
        if (work_p[i] != i || work_o[i] != 0)
            return 0;
    return 1;
}

/* Returns 1 when the case passes. distance 255 means unknown. */
static int run_case(const char *s, int distance)
{
    uint8_t p[7], o[7], moves[IDA_MAX_MOVES];
    print_str(s);
    print_str(": ");
    if (!ida_parse(s, p, o)) {
        print_str("invalid state");
        print_str("  FAIL\n");
        return 0;
    }
    int n = ida_solve(p, o, moves);
    int ok = n >= 0;
    if (ok) {
        for (int i = 0; i < n; ++i)
            print_str(names[moves[i]]);
        print_str(" [");
        print_int(n);
        print_str("]");
        for (int i = 0; i < 7; ++i) {
            work_p[i] = p[i];
            work_o[i] = o[i];
        }
        for (int i = 0; i < n; ++i)
            apply_move(moves[i]);
        ok = work_solved() && (distance == 255 || n == distance) &&
             n <= IDA_MAX_MOVES;
    }
    print_str(ok ? "  ok\n" : "  FAIL\n");
    return ok;
}

int main(void)
{
    int cases = 1, failures = !run_case(QUERY, 255);
    for (int i = 0; tests[i].state[0]; ++i) {
        ++cases;
        failures += !run_case(tests[i].state, tests[i].distance);
    }
    print_str(failures ? "FAIL " : "PASS ");
    print_int(cases - failures);
    print_str("/");
    print_int(cases);
    print_str("\n");
    return failures;
}
