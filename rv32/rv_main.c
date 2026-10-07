/* Reference build of the final C algorithm for Ripes:
 *
 *   riscv-none-elf-gcc -O2 -march=rv32i -mabi=ilp32 -ffreestanding \
 *       -nostdlib -nostartfiles -o build/ref.elf rv32/rv_main.c rv32/ida.c
 *
 * It does what the hand-written main in minirubik.S does: solve the query
 * and every test, print each solution, replay it on the cubie model (T5),
 * check the length against the exact distance, exit with the failure count.
 * Output goes through the Ripes environment calls, not a C library.
 */
#include <stdint.h>

#include "ida.h"

#ifndef QUERY
#define QUERY "21345671111111"
#endif

static const char query[] = QUERY;

#ifndef NO_TESTS
static const struct {
    char state[15];
    uint8_t distance;
} tests[] = {
    {"12345671111111", 0},  {"24173562322133", 3},
    {"62345713133111", 8},  {"25416373331111", 10},
    {"21345671111111", 11}, {"32156471111111", 11},
};
#define NTESTS (sizeof tests / sizeof tests[0])
#else
#define NTESTS 0
#endif

static const char names[9][4] = {"R",  "R2", "R'", "B", "B2",
                                 "B'", "D",  "D2", "D'"};
static const uint8_t source[3][8] = {
    {1, 4, 2, 0, 3, 5, 6},
    {0, 1, 2, 4, 5, 6, 3},
    {0, 2, 5, 3, 1, 4, 6},
};
static const uint8_t twist[3][8] = {
    {1, 2, 0, 2, 1, 0, 0},
    {0, 0, 0, 1, 2, 1, 2},
    {0, 0, 0, 0, 0, 0, 0},
};

static void ecall1(int number, uintptr_t arg)
{
    register uintptr_t a0 asm("a0") = arg;
    register int a7 asm("a7") = number;
    asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

static void print_str(const char *s) { ecall1(4, (uintptr_t) s); }
static void print_int(int v) { ecall1(1, (uintptr_t) v); }
static void print_char(char c) { ecall1(11, (uintptr_t) c); }

static void quarter_turn(uint8_t p[7], uint8_t o[7], uint32_t face)
{
    uint8_t q[7], r[7];
    for (int i = 0; i < 7; ++i)
        q[i] = p[i], r[i] = o[i];
    for (int i = 0; i < 7; ++i) {
        uint32_t from = source[face][i];
        int32_t t = (int32_t) (r[from] + twist[face][i]) - 3;
        p[i] = q[from];
        o[i] = (uint8_t) (t + ((t >> 31) & 3));
    }
}

static int run_case(const char *state, uint32_t distance)
{
    uint8_t p[7], o[7], moves[IDA_MAX_MOVES + 1];
    print_str(state);
    print_str(": ");
    if (!ida_parse(state, p, o)) {
        print_str("invalid state  FAIL\n");
        return 1;
    }
    int n = ida_solve(p, o, moves);
    if (n < 0) {
        print_str("  FAIL\n");
        return 1;
    }
    for (int i = 0; i < n; ++i) {
        print_str(names[moves[i]]);
        print_char(' ');
    }
    print_str("  [");
    print_int(n);
    print_str("]");
    for (int i = 0; i < n; ++i) {
        uint32_t m = moves[i], face = 0;
        while (m >= 3)
            m -= 3, ++face;
        for (uint32_t t = 0; t <= m; ++t)
            quarter_turn(p, o, face);
    }
    int solved = 1;
    for (int i = 0; i < 7; ++i)
        solved &= p[i] == i && o[i] == 0;
    if (!solved || (distance != 255 && (uint32_t) n != distance) || n > 11) {
        print_str("  FAIL\n");
        return 1;
    }
    print_str("  ok\n");
    return 0;
}

int main(void)
{
    int failures = run_case(query, 255);
    unsigned runs = 1;
#ifndef NO_TESTS
    for (unsigned i = 0; i < NTESTS; ++i, ++runs)
        failures += run_case(tests[i].state, tests[i].distance);
#endif
    print_str(failures ? "FAIL " : "PASS ");
    print_int((int) runs - failures);
    print_str("/");
    print_int((int) runs);
    print_str("\n");
    return failures;
}

void _start(void) __attribute__((naked, section(".text.start")));
void _start(void)
{
    asm volatile("call main\n\t"
                 "li a7, 93\n\t"
                 "ecall");
}
