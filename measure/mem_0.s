# Store one word per 4 bytes over 0 KiB starting at 0x10000000.
.text
main:
    li   t0, 0x10000000
    li   t1, 0
    add  t1, t1, t0
    beq  t0, t1, done
loop:
    sw   t0, 0(t0)
    addi t0, t0, 4
    bne  t0, t1, loop
done:
    li   a7, 10
    ecall
