#!/bin/sh
# Generates the stage-1 probe programs. Ripes has no .if, so each region size
# is its own file. mem_N.s writes N bytes of guest memory with word stores;
# ips_N.s retires roughly N instructions in a tight ALU loop.
for kib in 0 4096 8192 16384; do
    cat >mem_$kib.s <<EOT
# Store one word per 4 bytes over $kib KiB starting at 0x10000000.
.text
main:
    li   t0, 0x10000000
    li   t1, $((kib * 1024))
    add  t1, t1, t0
    beq  t0, t1, done
loop:
    sw   t0, 0(t0)
    addi t0, t0, 4
    bne  t0, t1, loop
done:
    li   a7, 10
    ecall
EOT
done
for n in 50000 1000000 10000000; do
    cat >ips_$n.s <<EOT
# Roughly $n retired instructions: 4 per iteration.
.text
main:
    li   t0, $((n / 4))
loop:
    addi t1, t1, 1
    xor  t2, t2, t1
    addi t0, t0, -1
    bnez t0, loop
    li   a7, 10
    ecall
EOT
done
for n in 50000 1000000 10000000; do
    cat >memloop_$n.s <<EOT
# Roughly $((n * 6 / 5)) retired instructions: 6 per iteration, a load and a
# store to a 256-byte buffer that is resident after the first pass.
.data
buf: .zero 256
.text
main:
    la   t3, buf
    li   t0, $((n / 5))
loop:
    andi t1, t0, 252
    add  t1, t1, t3
    lw   t2, 0(t1)
    sw   t0, 0(t1)
    addi t0, t0, -1
    bnez t0, loop
    li   a7, 10
    ecall
EOT
done
