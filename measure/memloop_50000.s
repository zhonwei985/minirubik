# Roughly 60000 retired instructions: 6 per iteration, a load and a
# store to a 256-byte buffer that is resident after the first pass.
.data
buf: .zero 256
.text
main:
    la   t3, buf
    li   t0, 10000
loop:
    andi t1, t0, 252
    add  t1, t1, t3
    lw   t2, 0(t1)
    sw   t0, 0(t1)
    addi t0, t0, -1
    bnez t0, loop
    li   a7, 10
    ecall
