# minirubik on RV32I: an optimal 2x2x2 solver, IDA* with h = max(dP, dO).
#
# Single source of the Ripes program. Ripes' assembler has no .include, so
# rv32/build.py inlines pdb.inc (the tables rv32/pdb.c generates) and writes
# rv32/ripes_cli.s, the file to run:
#
#   Ripes --mode cli --src rv32/ripes_cli.s -t asm --proc RV32_ISS --iret
#
# Assemble-time switches. Ripes has no .if either; build.py resolves them.
    .equ MOD3_BRANCH, 1     # twist mod 3 in quarter_turn: 1 branch, 0 branchless

# A cube state is 14 bytes: the cubie at positions 0..6, then the twist at
# positions 0..6, all 0-based. The search works on two ranks, p (permutation,
# 0..5039) and o (twists, 0..728), as solver.c's rank_state() defines them.
# The exit status is the number of failed cases, so 0 means every check
# passed.

# ---------------------------------------------------------------------------
# Inputs. The query is any state, inlined at assembly time. Each test carries
# its exact distance (host gate H3), so the program checks optimality itself.
# ---------------------------------------------------------------------------
    .data
query:  .string "21345671111111"
    .align 2
# 16 bytes per test: 14 digits, NUL, exact distance. A zero byte ends the list.
tests:
    .string "12345671111111"   # solved
    .byte 0
    .string "24173562322133"   # short scramble R B' D2, solved by D2 B R'
    .byte 3
    .string "54721631111111"   # distance 11, the costliest for this search
    .byte 11
tests_end:
    .byte 0

    .align 2
# Quarter-turn model for verification, rows padded to 8 bytes so that the row
# offset is face << 3. Independent of the rank tables the search uses.
SOURCE: .byte 1, 4, 2, 0, 3, 5, 6, 0
        .byte 0, 1, 2, 4, 5, 6, 3, 0
        .byte 0, 2, 5, 3, 1, 4, 6, 0
TWIST:  .byte 1, 2, 0, 2, 1, 0, 0, 0
        .byte 0, 0, 0, 1, 2, 1, 2, 0
        .byte 0, 0, 0, 0, 0, 0, 0, 0
# Move names with their separator, 4 bytes each so the offset is move << 2.
NAMES:  .string "R "
        .byte 0
        .string "R2 "
        .string "R' "
        .string "B "
        .byte 0
        .string "B2 "
        .string "B' "
        .string "D "
        .byte 0
        .string "D2 "
        .string "D' "
s_colon:    .string ": "
s_invalid:  .string "invalid state"
s_ok:       .string "  ok\n"
s_fail:     .string "  FAIL\n"
s_open:     .string " ["
s_close:    .string "]"
s_pass:     .string "PASS "
s_failsum:  .string "FAIL "
s_slash:    .string "/"
s_nl:       .string "\n"

    .bss
    .align 2
# Search frames, 40 bytes each, depth 0..11:
#   0 p   4 o   8 skip (face of the move in, 3 at the root)   12 face
#   16 turns   20 cursor p   24 cursor o   28 PT row of face   32 OT row
FRAMES: .zero 480
CUBE:   .zero 16            # parsed state
WORK:   .zero 16            # replay copy for verification
TMP:    .zero 16
MOVES:  .zero 16

# ---------------------------------------------------------------------------
    .text
# main: solve the query, then every test. Exit status = failures.
#   s0 test pointer   s1 failures   s2 cases run
main:
    li   s1, 0
    li   s2, 0
    la   a0, query
    li   a1, 255                # distance unknown: check replay and <= 11
    jal  ra, run_case
    la   s0, tests
main_loop:
    lbu  t0, 0(s0)
    beqz t0, main_done
    mv   a0, s0
    lbu  a1, 15(s0)
    jal  ra, run_case
    addi s0, s0, 16
    j    main_loop
main_done:
    la   a0, s_pass
    beqz s1, main_print
    la   a0, s_failsum
main_print:
    li   a7, 4
    ecall
    sub  a0, s2, s1
    li   a7, 1
    ecall
    la   a0, s_slash
    li   a7, 4
    ecall
    mv   a0, s2
    li   a7, 1
    ecall
    la   a0, s_nl
    li   a7, 4
    ecall
    mv   a0, s1
    li   a7, 93
    ecall

# run_case(a0 = 14-digit string, a1 = exact distance, or 255 if unknown).
# Parse, solve, print, replay the moves on the cubie model (T5), check the
# length. Updates s1 (failures) and s2 (cases).
#   s3 string, then move pointer   s4 exact distance   s5 length   s6 end
run_case:
    addi sp, sp, -20
    sw   ra, 16(sp)
    sw   s3, 12(sp)
    sw   s4, 8(sp)
    sw   s5, 4(sp)
    sw   s6, 0(sp)
    mv   s3, a0
    mv   s4, a1
    addi s2, s2, 1
    li   a7, 4
    ecall                       # the state itself
    la   a0, s_colon
    ecall
    mv   a0, s3
    jal  ra, parse
    bnez a0, rc_valid
    la   a0, s_invalid
    li   a7, 4
    ecall
    j    rc_fail
rc_valid:
    la   a0, CUBE
    la   a1, MOVES
    jal  ra, solve
    mv   s5, a0                 # length, -1 if none within 11
    bltz s5, rc_fail
    la   s3, MOVES
    add  s6, s3, s5             # end of the move list
    li   a7, 4
rc_print:
    beq  s3, s6, rc_printed
    lbu  t0, 0(s3)
    slli t0, t0, 2
    la   a0, NAMES
    add  a0, a0, t0
    ecall
    addi s3, s3, 1
    j    rc_print
rc_printed:
    la   a0, s_open
    ecall
    mv   a0, s5
    li   a7, 1
    ecall
    la   a0, s_close
    li   a7, 4
    ecall
    # T5: replay the moves on a copy of the parsed state.
    la   a0, CUBE
    la   a1, WORK
    jal  t0, copy14
    la   s3, MOVES
rc_replay:
    beq  s3, s6, rc_replayed
    lbu  a0, 0(s3)
    jal  ra, apply_move
    addi s3, s3, 1
    j    rc_replay
rc_replayed:
    jal  ra, work_solved
    beqz a0, rc_fail
    li   t0, 255
    beq  s4, t0, rc_unknown
    bne  s5, s4, rc_fail        # must equal the exact distance
rc_unknown:
    li   t0, 11
    bgt  s5, t0, rc_fail        # never above the diameter
    la   a0, s_ok
    li   a7, 4
    ecall
    j    rc_out
rc_fail:
    addi s1, s1, 1
    la   a0, s_fail
    li   a7, 4
    ecall
rc_out:
    lw   ra, 16(sp)
    lw   s3, 12(sp)
    lw   s4, 8(sp)
    lw   s5, 4(sp)
    lw   s6, 0(sp)
    addi sp, sp, 20
    ret

# parse(a0 = string) -> a0 = 1 if valid, filling CUBE. The first seven digits
# must be a permutation of 1-7, the next seven in 1-3 summing to 0 mod 3, and
# byte 14 must be NUL.
parse:
    la   t0, CUBE
    li   t1, 0                  # cubies seen, one bit each
    li   t2, 0                  # twist sum
    li   t3, 7                  # digits left
    li   t6, 1
p_loop:
    lbu  t4, 0(a0)
    addi t4, t4, -49            # '1' -> 0
    li   t5, 7
    bgeu t4, t5, p_bad          # also catches anything below '1'
    sll  t5, t6, t4
    and  a1, t1, t5
    bnez a1, p_bad              # repeated cubie
    or   t1, t1, t5
    sb   t4, 0(t0)
    lbu  t4, 7(a0)
    addi t4, t4, -49
    li   t5, 3
    bgeu t4, t5, p_bad
    sb   t4, 7(t0)
    add  t2, t2, t4
    addi a0, a0, 1
    addi t0, t0, 1
    addi t3, t3, -1
    bnez t3, p_loop
    lbu  t4, 7(a0)              # a0 has advanced 7, so this is byte 14
    bnez t4, p_bad
    li   t5, 3
p_mod:                          # sum <= 14: subtract 3 until below 3
    bltu t2, t5, p_sum
    addi t2, t2, -3
    j    p_mod
p_sum:
    seqz a0, t2
    ret
p_bad:
    li   a0, 0
    ret

# ---------------------------------------------------------------------------
# solve(a0 = cube, a1 = moves out) -> a0 = length, or -1 if none within 11.
#
# IDA*: depth-first search refusing any node with g + h > bound, for
# bound = h(root), h(root) + 1, ..., 11. h = max(dP[p], dO[o]) never
# overestimates, so the first solution found is a shortest one.
#
# Registers in the search:
#   s0 current frame       s1 limit = bound - g - 1, the largest h a child
#                          of this frame may have
#   s2 dP   s3 dO   s4 PT   s5 OT   a2 FRAMES   a3 10080 (PT row bytes)
#   a4 1458 (OT row bytes)  a7 bound
#   s6 face   s7 turns   s8 cursor p   s9 cursor o   s10 skip
#   s11 PT row of the face  a6 OT row of the face
# ---------------------------------------------------------------------------
solve:
    addi sp, sp, -56
    sw   ra, 52(sp)
    sw   s0, 48(sp)
    sw   s1, 44(sp)
    sw   s2, 40(sp)
    sw   s3, 36(sp)
    sw   s4, 32(sp)
    sw   s5, 28(sp)
    sw   s6, 24(sp)
    sw   s7, 20(sp)
    sw   s8, 16(sp)
    sw   s9, 12(sp)
    sw   s10, 8(sp)
    sw   s11, 4(sp)
    sw   a1, 0(sp)              # moves out

    # Lehmer rank by Horner's rule: p = p * (7 - i) + c_i, where c_i counts
    # the later cubies smaller than cubie i. The factor is at most 6, so the
    # product is a short run of adds: RV32I has no multiply.
    li   t0, 0                  # rank
    li   t1, 0                  # i
sv_lehmer:
    add  t2, a0, t1
    lbu  t3, 0(t2)              # p[i]
    li   t4, 0                  # c_i
    addi t5, t1, 1              # j
sv_count:
    li   t6, 7
    bge  t5, t6, sv_counted
    add  t6, a0, t5
    lbu  t6, 0(t6)
    sltu t6, t6, t3             # p[j] < p[i]
    add  t4, t4, t6
    addi t5, t5, 1
    j    sv_count
sv_counted:
    li   t5, 7
    sub  t5, t5, t1             # factor 7 - i
    mv   t6, t0
sv_mul:                         # t0 = t0 * factor, by repeated addition
    addi t5, t5, -1
    beqz t5, sv_mulled
    add  t0, t0, t6
    j    sv_mul
sv_mulled:
    add  t0, t0, t4
    addi t1, t1, 1
    li   t6, 6
    blt  t1, t6, sv_lehmer      # i = 6 adds 0 times 1: skipped
    mv   s8, t0                 # root p

    # Twist rank over positions 0..5: o = 3o + d = (o << 1) + o + d.
    li   t0, 0
    addi t1, a0, 7
    addi t2, a0, 13
sv_ori:
    lbu  t3, 0(t1)
    slli t4, t0, 1
    add  t0, t0, t4
    add  t0, t0, t3
    addi t1, t1, 1
    bne  t1, t2, sv_ori
    mv   s9, t0                 # root o

    la   s2, dP
    la   s3, dO
    la   s4, PT
    la   s5, OT
    la   a2, FRAMES
    li   a3, 10080
    li   a4, 1458
    or   t0, s8, s9
    bnez t0, sv_start
    li   a0, 0                  # already solved
    j    sv_return
sv_start:
    sw   s8, 0(a2)              # the root stays in frame 0
    sw   s9, 4(a2)
    add  t0, s2, s8
    lbu  a7, 0(t0)              # bound = h(root) = max(dP[p], dO[o])
    add  t0, s3, s9
    lbu  t0, 0(t0)
    bgeu a7, t0, sv_iter
    mv   a7, t0

sv_iter:                        # one depth-first search with this bound
    addi s1, a7, -1
    mv   s0, a2
    li   s10, 3
    li   s6, 0
    li   s7, 0
    lw   s8, 0(s0)
    lw   s9, 4(s0)
    mv   s11, s4
    mv   a6, s5
sv_loop:
    li   t0, 3
    beq  s6, t0, sv_pop         # every face of this node done
    beq  s6, s10, sv_skip_face  # never turn the face we came by
    beq  s7, t0, sv_next_face   # R, R2, R' of this face all tried
    # Next child: one more quarter turn of the cursor.
    slli t1, s8, 1
    add  t1, t1, s11
    lhu  s8, 0(t1)              # p = PT[face][p]
    slli t1, s9, 1
    add  t1, t1, a6
    lhu  s9, 0(t1)              # o = OT[face][o]
    addi s7, s7, 1
    add  t1, s2, s8
    lbu  t1, 0(t1)              # dP[p]
    bltu s1, t1, sv_loop        # prune: g + 1 + dP > bound
    add  t2, s3, s9
    lbu  t2, 0(t2)              # dO[o]
    bltu s1, t2, sv_loop        # prune: g + 1 + dO > bound
    or   t1, s8, s9
    beqz t1, sv_found           # p = 0 and o = 0: solved
    # Push: save this frame's cursor, the child becomes the current node.
    sw   s10, 8(s0)
    sw   s6, 12(s0)
    sw   s7, 16(s0)
    sw   s8, 20(s0)
    sw   s9, 24(s0)
    sw   s11, 28(s0)
    sw   a6, 32(s0)
    mv   s10, s6                # the child skips this face
    addi s0, s0, 40
    sw   s8, 0(s0)
    sw   s9, 4(s0)
    li   s6, 0
    li   s7, 0
    mv   s11, s4
    mv   a6, s5
    addi s1, s1, -1
    j    sv_loop
sv_skip_face:                   # the cursor was never moved: just step
    addi s6, s6, 1
    add  s11, s11, a3
    add  a6, a6, a4
    j    sv_loop
sv_next_face:                   # cursor back to the node, next face
    addi s6, s6, 1
    li   s7, 0
    lw   s8, 0(s0)
    lw   s9, 4(s0)
    add  s11, s11, a3
    add  a6, a6, a4
    j    sv_loop
sv_pop:
    beq  s0, a2, sv_deepen      # the root is exhausted
    addi s0, s0, -40
    lw   s10, 8(s0)
    lw   s6, 12(s0)
    lw   s7, 16(s0)
    lw   s8, 20(s0)
    lw   s9, 24(s0)
    lw   s11, 28(s0)
    lw   a6, 32(s0)
    addi s1, s1, 1
    j    sv_loop
sv_deepen:
    addi a7, a7, 1
    li   t0, 11
    bleu a7, t0, sv_iter
    li   a0, -1
    j    sv_return

# Frames FRAMES .. s0 each hold the move to their current child:
# move = 3 * face + turns - 1, with 3x = (x << 1) + x.
sv_found:
    sw   s6, 12(s0)
    sw   s7, 16(s0)
    lw   t2, 0(sp)              # moves out
    mv   t1, a2
sv_copy:
    lw   t3, 12(t1)
    slli t4, t3, 1
    add  t3, t3, t4
    lw   t4, 16(t1)
    add  t3, t3, t4
    addi t3, t3, -1
    sb   t3, 0(t2)
    addi t2, t2, 1
    beq  t1, s0, sv_copied
    addi t1, t1, 40
    j    sv_copy
sv_copied:
    lw   t0, 0(sp)
    sub  a0, t2, t0
sv_return:
    lw   ra, 52(sp)
    lw   s0, 48(sp)
    lw   s1, 44(sp)
    lw   s2, 40(sp)
    lw   s3, 36(sp)
    lw   s4, 32(sp)
    lw   s5, 28(sp)
    lw   s6, 24(sp)
    lw   s7, 20(sp)
    lw   s8, 16(sp)
    lw   s9, 12(sp)
    lw   s10, 8(sp)
    lw   s11, 4(sp)
    addi sp, sp, 56
    ret

# ---------------------------------------------------------------------------
# Verification model: the 14-byte cubie state, independent of the rank tables.
# ---------------------------------------------------------------------------

# copy14(a0 = source, a1 = destination). Link register t0.
copy14:
    addi t2, a0, 14
c14_loop:
    lbu  t3, 0(a0)
    sb   t3, 0(a1)
    addi a0, a0, 1
    addi a1, a1, 1
    bne  a0, t2, c14_loop
    jr   t0

# apply_move(a0 = move 0..8): face = move / 3 and turns = move % 3 + 1, found
# by subtraction, then that many quarter turns of WORK.
apply_move:
    addi sp, sp, -12
    sw   ra, 8(sp)
    sw   s7, 4(sp)
    sw   s8, 0(sp)
    li   s7, 0                  # face
    li   t0, 3
am_div:
    blt  a0, t0, am_turns
    addi a0, a0, -3
    addi s7, s7, 1
    j    am_div
am_turns:
    addi s8, a0, 1
am_loop:
    mv   a0, s7
    jal  ra, quarter_turn
    addi s8, s8, -1
    bnez s8, am_loop
    lw   ra, 8(sp)
    lw   s7, 4(sp)
    lw   s8, 0(sp)
    addi sp, sp, 12
    ret

# quarter_turn(a0 = face): WORK.p[i] = p[src], WORK.o[i] = (o[src] + tw) mod 3
# with src = SOURCE[face][i]. The sum is at most 4, so one conditional
# subtract of 3 is exact.
quarter_turn:
    slli a2, a0, 3              # rows are 8 bytes
    la   a0, WORK
    la   a1, TMP
    jal  t0, copy14             # TMP = WORK
    la   t0, SOURCE
    add  t0, t0, a2
    la   t1, TWIST
    add  t1, t1, a2
    la   t2, TMP
    la   t3, WORK
    addi a1, t3, 7
qt_loop:
    lbu  t4, 0(t0)              # src
    add  t4, t4, t2
    lbu  t5, 0(t4)              # p[src]
    sb   t5, 0(t3)
    lbu  t5, 7(t4)              # o[src]
    lbu  t6, 0(t1)
    add  t5, t5, t6
    addi t5, t5, -3
.if MOD3_BRANCH
    bgez t5, qt_reduced         # sum >= 3: the subtract was right (2 instr)
    addi t5, t5, 3              # otherwise undo it (3 instr)
qt_reduced:
.else
    srai t6, t5, 31             # all ones exactly when sum < 3
    andi t6, t6, 3
    add  t5, t5, t6             # add 3 back in that case (4 instr)
.endif
    sb   t5, 7(t3)
    addi t0, t0, 1
    addi t1, t1, 1
    addi t3, t3, 1
    bne  t3, a1, qt_loop
    ret

# work_solved() -> a0 = 1 if WORK is the identity with zero twist.
work_solved:
    la   t0, WORK
    li   t1, 0
    li   t3, 7
ws_loop:
    lbu  t2, 0(t0)
    bne  t2, t1, ws_no
    lbu  t2, 7(t0)
    bnez t2, ws_no
    addi t0, t0, 1
    addi t1, t1, 1
    bne  t1, t3, ws_loop
    li   a0, 1
    ret
ws_no:
    li   a0, 0
    ret

# ---------------------------------------------------------------------------
# Read-only tables from rv32/pdb.c: dP, dO, PT, OT (40,383 bytes).
# Ripes has no .rodata, so they live in .data.
# ---------------------------------------------------------------------------
    .data
    .align 2
.include "pdb.inc"
