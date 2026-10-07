#!/bin/sh
# Code size convention for the note: bytes of linked .text of the CLI build
# (RENDER = 0), assembled from rv32/minirubik.S by GNU as, and static data as
# .data + .bss (Ripes has no .rodata; the tables live in .data).
X=${RISCV:-/c/Users/User/tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin}
mkdir -p build
"$X/riscv-none-elf-as" -march=rv32i -mabi=ilp32 -I rv32 -o build/asm.o rv32/minirubik.S &&
"$X/riscv-none-elf-ld" -Ttext=0 -e main -o build/asm.elf build/asm.o &&
"$X/riscv-none-elf-size" -A build/asm.elf | awk '
    $1 == ".text" { t = $2 } $1 == ".data" { d = $2 } $1 == ".bss" { b = $2 }
    END { printf "asm  .text %d  .data %d  .bss %d  static data %d\n", t, d, b, d + b }'
