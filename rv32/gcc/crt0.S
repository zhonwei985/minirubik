# Startup for the gcc -O2 reference build: no libc, so nothing else runs
# before main. Ripes sets sp itself and starts with zeroed memory, so .bss
# needs no clearing. gp is set so the linker may relax global accesses to
# one gp-relative instruction, which favours the compiler.
    .section .text.start
    .globl _start
_start:
    .option push
    .option norelax
    la   gp, __global_pointer$
    .option pop
    call main
    li   a7, 93                 # exit with status a0
    ecall
