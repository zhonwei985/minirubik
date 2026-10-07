/* Final C algorithm: IDA* over factored coordinates with three admissible
 * tables. Freestanding: no libc, no heap, no recursion, no multiply or
 * divide, so the same file builds for the host gate H3 and for the
 * riscv -O2 -march=rv32i reference build.
 */
#ifndef IDA_H
#define IDA_H

#include <stdint.h>

#define IDA_MAX_MOVES 11

/* Parses "PPPPPPPOOOOOOO" (digits 1-7, then 1-3) into internal values
 * 0-based. Returns 1 when the string is a valid reachable state. */
int ida_parse(const char *in, uint8_t p[7], uint8_t o[7]);

/* Writes an optimal move sequence (move = 3 * face + turns - 1, faces R, B,
 * D) and returns its length, or -1 if no solution within 11 moves exists.
 * ida_nodes counts the children generated, for host instrumentation. */
int ida_solve(const uint8_t p[7], const uint8_t o[7], uint8_t *moves);

extern uint32_t ida_nodes;

#endif
