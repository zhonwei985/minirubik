#!/bin/sh
# gcc -O2 -march=rv32i reference build of rv32/ida.c, run on Ripes with -t elf.
#   rv32/gcc/build.sh OUT.elf [STATE [--no-tests]]
# Needs riscv64-unknown-elf-gcc (Ubuntu 24.04: apt install
# gcc-riscv64-unknown-elf, GCC 13.2.0). Prints the section sizes of the
# program and of each object.
set -e
cd "$(dirname "$0")/../.."
out=$1
dir=$(dirname "$out")
mkdir -p "$dir"
defs=
[ -n "$2" ] && defs="-DQUERY=\"$2\""
[ "$3" = "--no-tests" ] && defs="$defs -DNO_TESTS"
CC=riscv64-unknown-elf-gcc
ARCH="-march=rv32i -mabi=ilp32"
CFLAGS="-O2 $ARCH -ffreestanding -Wall -Wextra"
$CC $CFLAGS -c rv32/ida.c -o "$dir/ida.o"
$CC $CFLAGS $defs -c rv32/gcc/main.c -o "$dir/main.o"
$CC $ARCH -c rv32/gcc/crt0.S -o "$dir/crt0.o"
$CC $ARCH -nostdlib -static -o "$out" "$dir/crt0.o" "$dir/main.o" \
    "$dir/ida.o" -lgcc
riscv64-unknown-elf-size -A "$out" "$dir/crt0.o" "$dir/main.o" "$dir/ida.o"
