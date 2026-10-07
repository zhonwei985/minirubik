#!/bin/sh
# Reference build: the final C algorithm (rv32/ida.c) with the same driver
# behaviour as minirubik.S (rv32/rv_main.c), compiled by GCC for RV32I.
#   sh rv32/ref.sh            all cases, build/ref.elf
#   sh rv32/ref.sh STATE      one query, no tests, build/ref_STATE.elf
X=${RISCV:-/c/Users/User/tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin}
CFLAGS="-O2 -march=rv32i -mabi=ilp32 -ffreestanding -nostdlib -nostartfiles -Wl,--no-relax -Wl,-e,_start"
mkdir -p build
if [ -n "$1" ]; then
    out=build/ref_$1.elf
    "$X/riscv-none-elf-gcc" $CFLAGS -DNO_TESTS -DQUERY="\"$1\"" -o "$out" rv32/rv_main.c rv32/ida.c || exit 1
else
    out=build/ref.elf
    "$X/riscv-none-elf-gcc" $CFLAGS -o "$out" rv32/rv_main.c rv32/ida.c || exit 1
fi
if "$X/riscv-none-elf-objdump" -d "$out" | grep -Eq '\s(mul|div|rem)[a-z]*\s|__(mul|div|mod)'; then
    echo "reference build uses M or a libgcc arithmetic routine" >&2; exit 1
fi
"$X/riscv-none-elf-size" -A "$out" | awk -v f="$out" '
    $1 == ".text" { t = $2 } $1 == ".rodata" { r = $2 } $1 == ".data" || $1 == ".sdata" || $1 == ".bss" || $1 == ".sbss" { d += $2 }
    END { printf "ref  %s  .text %d  .rodata %d  data+bss %d\n", f, t, r, d }'
