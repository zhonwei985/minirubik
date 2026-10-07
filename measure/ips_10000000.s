# Roughly 10000000 retired instructions: 4 per iteration.
.text
main:
    li   t0, 2500000
loop:
    addi t1, t1, 1
    xor  t2, t2, t1
    addi t0, t0, -1
    bnez t0, loop
    li   a7, 10
    ecall
