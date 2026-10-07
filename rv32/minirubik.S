# minirubik on RV32I: optimal 2x2x2 solver by IDA* with pattern databases.
#
# Source of truth for the Ripes builds. rv32/build.py resolves .macro and
# .if RENDER (Ripes' assembler has neither) and inlines tables.inc, producing
#   rv32/ripes_cli.s   RENDER = 0, the CLI build measured with --iret
#   rv32/ripes_gui.s   RENDER = 1, animates on the LED matrix
# The two differ only in the renderer. This file also assembles unchanged with
# GNU as, which is how code size is measured.
#
# Conventions. A cube state is 14 bytes: cubie at positions 0..6, then twist
# at positions 0..6, all 0-based. Coordinates in the search are absolute
# addresses of 8-byte records (see rv32/gen.c), so one quarter turn of a
# coordinate is "lhu off(rec); add rec, BASE, off". Heuristic values are
# carried times four.

    .equ RENDER, 0          # build.py rewrites this line for the GUI build
    .equ DUMP, 0            # renderer self-test: print frames as text
    .equ DELAY, 150000      # GUI build: busy-wait iterations per frame
    .equ MOD3_BRANCH, 1     # 0: branchless mod 3 instead (measured slower)

# ---------------------------------------------------------------------------
# Inputs. The query is any state, inlined at assembly time; the tests carry
# their exact distance (from host gate H3) so the program checks optimality.
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
    .string "62345713133111"   # distance 8, from tests/solutions.txt
    .byte 8
    .string "25416373331111"   # distance 10, from tests/solutions.txt
    .byte 10
    .string "21345671111111"   # distance 11, the shared reference vector
    .byte 11
    .string "32156471111111"   # distance 11, the costliest state for this search
    .byte 11
tests_end:
    .byte 0

    .align 2
# Quarter-turn model for verification and rendering, rows padded to 8 bytes
# so the row offset is face << 3.
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
s_moves:    .string "  ["
s_close:    .string "]"
s_pass:     .string "PASS "
s_failsum:  .string "FAIL "
s_slash:    .string "/"
s_nl:       .string "\n"

    .bss
    .align 2
FRAMES: .zero 384           # 12 search frames of 32 bytes, see solve
CUBE:   .zero 16            # parsed query
WORK:   .zero 16            # replay copy for verification and rendering
TMP:    .zero 16
WHERE:  .zero 8             # WHERE[cubie] = position, for the pattern keys
MOVES:  .zero 16
.if RENDER
ROWS:   .zero 100           # LED row addresses, 25 words
.endif

# ---------------------------------------------------------------------------
    .text
# main: run the query, then every test. Exit status = number of failures.
#   s0 test pointer, s1 failures, s2 cases run
main:
    li   s1, 0
    li   s2, 0
.if RENDER
    jal  ra, render_init
.endif
    la   a0, query
    li   a1, 255                # distance unknown: only T5 and <= 11 apply
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
    beqz s1, 1f
    la   a0, s_failsum
1:  li   a7, 4
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

# run_case(a0 = 14-digit string, a1 = exact distance or 255).
# Parses, solves, prints, replays the moves on WORK (T5), checks the length.
#   s3 string, then move pointer   s4 exact distance   s5 length
#   s6 end of the move list. main keeps nothing in s3-s6, so run_case does
#   not preserve them; solve preserves s4 for it.
run_case:
    addi sp, sp, -4
    sw   ra, 0(sp)
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
    # print the moves
    la   s3, MOVES
    add  s6, s3, s5             # end of the move list
    li   a7, 4
rc_print:
    beq  s3, s6, rc_printed
    lbu  t2, 0(s3)
    slli t2, t2, 2
    la   a0, NAMES
    add  a0, a0, t2
    ecall
    addi s3, s3, 1
    j    rc_print
rc_printed:
    la   a0, s_moves
    ecall
    mv   a0, s5
    li   a7, 1
    ecall
    la   a0, s_close
    li   a7, 4
    ecall
    # T5: replay on a copy of the parsed state with the cubie model
    la   a0, CUBE
    la   a1, WORK
    jal  t0, copy14
.if RENDER
    jal  ra, render
.endif
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
    # length: equal to the exact distance when known, never above 11
    li   t0, 255
    beq  s4, t0, rc_unknown
    bne  s5, s4, rc_fail
rc_unknown:
    li   t0, 11
    bgt  s5, t0, rc_fail
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
    lw   ra, 0(sp)
    addi sp, sp, 4
    ret

# parse(a0 = string) -> a0 = 1 if valid. Fills CUBE. Digits 1-7 must be a
# permutation, twists 1-3 must sum to 0 mod 3, byte 14 must be NUL.
parse:
    la   t0, CUBE
    li   t1, 0                  # seen mask
    li   t2, 0                  # twist sum
    li   t3, 7                  # count
    li   t6, 1
p_loop:
    lbu  t4, 0(a0)
    addi t4, t4, -49            # '1'
    li   t5, 7
    bgeu t4, t5, p_bad
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
    lbu  t4, 7(a0)              # a0 now points at byte 7, so this is byte 14
    bnez t4, p_bad
    li   t5, 3
p_mod:                          # sum <= 14: subtract until below 3
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
# solve(a0 = cube, a1 = moves out) -> a0 = length, or -1.
# Preserves only what its callers keep live across it, s0-s2 (main) and s4
# (run_case); every other register is clobbered.
#
# Iterative IDA*. Registers during the search:
#   s0 frame        s1 limit4 (budget for a child, times 4)   s9 FRAMES
#   s2 PERM  s3 ORI  s4 POSA  s5 POSB  s6 PDBA  s7 PDBB  s8 HT4
#   s10 face of the move into this node (3 at the root)
#   s11 move code that ends the current face loop, t6 the move code
#   a2..a5 cursor: PERM, ORI, POSA, POSB record of the last child generated
#   a6, a7 HT4 + parent's ha4, HT4 + parent's hb4    a1 bound4    ra resume
# Frame (32 bytes): 0 p, 4 o, 8 a, 12 b (record addresses), 16 HT4 + ha4,
#   20 HT4 + hb4, 24 code = move << 2 | face of the move in, 28 the resume
#   address saved when this frame pushes a child.
# ---------------------------------------------------------------------------
solve:
    addi sp, sp, -24
    sw   ra, 20(sp)
    sw   s0, 16(sp)
    sw   s1, 12(sp)
    sw   s2, 8(sp)
    sw   s4, 4(sp)
    sw   a1, 0(sp)              # moves out
    la   s2, PERM
    la   s3, ORI
    la   s4, POSA
    la   s5, POSB
    la   s6, PDBA
    la   s7, PDBB
    la   s8, HT4
    la   s9, FRAMES


    # Lehmer rank by Horner, p = p * (7 - i) + c_i; the factor is at most 6,
    # so the product is a short run of adds (RV32I has no mul).
    li   t0, 0                  # rank
    li   t1, 0                  # i
sv_lehmer:
    add  t2, a0, t1
    lbu  t3, 0(t2)              # p[i]
    li   t4, 0                  # c_i
    addi t5, t1, 1
sv_count:
    li   t6, 7
    bge  t5, t6, sv_counted
    add  t6, a0, t5
    lbu  t6, 0(t6)
    sltu t6, t6, t3
    add  t4, t4, t6
    addi t5, t5, 1
    j    sv_count
sv_counted:
    li   t5, 7
    sub  t5, t5, t1             # factor 7 - i
    mv   t6, t0
sv_mul:
    addi t5, t5, -1
    beqz t5, sv_mulled
    add  t0, t0, t6
    j    sv_mul
sv_mulled:
    add  t0, t0, t4
    addi t1, t1, 1
    li   t6, 6
    blt  t1, t6, sv_lehmer
    slli t0, t0, 3
    add  a2, s2, t0             # PERM record of the root

    # Twist rank, base 3 over positions 0..5: o = 3o + d = (o << 1) + o + d.
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
    slli t0, t0, 3
    add  a3, s3, t0             # ORI record of the root

    # WHERE[cubie] = position, then keys 49 w0 + 7 w1 + w2 with 7x = 8x - x.
    la   t5, WHERE
    li   t1, 0
sv_where:
    add  t2, a0, t1
    lbu  t3, 0(t2)
    add  t3, t3, t5
    sb   t1, 0(t3)
    addi t1, t1, 1
    li   t6, 7
    bne  t1, t6, sv_where
    lbu  t0, 0(t5)              # where[0]
    slli t1, t0, 3
    sub  t0, t1, t0
    lbu  t1, 1(t5)
    add  t0, t0, t1
    slli t1, t0, 3
    sub  t0, t1, t0
    lbu  t1, 2(t5)
    add  t0, t0, t1             # key A
    la   t1, MAPA
    add  t1, t1, t0
    lbu  t1, 0(t1)
    slli t1, t1, 3
    add  a4, s4, t1             # POSA record of the root
    lbu  t0, 0(t5)
    slli t1, t0, 3
    sub  t0, t1, t0
    lbu  t1, 3(t5)
    add  t0, t0, t1
    slli t1, t0, 3
    sub  t0, t1, t0
    lbu  t1, 6(t5)
    add  t0, t0, t1             # key B
    la   t1, MAPB
    add  t1, t1, t0
    lbu  t1, 0(t1)
    slli t1, t1, 3
    add  a5, s5, t1             # POSB record of the root

    # Solved root: nothing to search.
    bne  a2, s2, sv_search
    bne  a3, s3, sv_search
    li   a0, 0
    j    sv_return
sv_search:
    mv   s0, s9
    sw   a2, 0(s0)
    sw   a3, 4(s0)
    sw   a4, 8(s0)
    sw   a5, 12(s0)
    li   t0, 3
    sw   t0, 24(s0)             # root: no previous face
    # Exact pattern values at the root by descent.
    mv   a0, a3
    mv   a1, a4
    mv   t5, s4
    mv   t6, s6
    jal  ra, exact4
    mv   s10, a0                # ha4
    lw   a0, 4(s0)
    lw   a1, 12(s0)
    mv   t5, s5
    mv   t6, s7
    jal  ra, exact4             # a0 = hb4
    add  a6, s8, s10
    add  a7, s8, a0
    sw   a6, 16(s0)
    sw   a7, 20(s0)
    # bound4 = max(hp4, ha4, hb4)
    lw   t0, 0(s0)
    lbu  a1, 7(t0)
    bgeu a1, s10, 1f
    mv   a1, s10
1:  bgeu a1, a0, sv_iter
    mv   a1, a0

sv_iter:
    addi s1, a1, -4             # budget for the root's children
    mv   s0, s9
    li   s10, 3
    lw   a2, 0(s0)
    lw   a3, 4(s0)
    lw   a6, 16(s0)
    lw   a7, 20(s0)
    bnez s1, sv_full
    li   t6, 3                  # bound 1: the root is one move above it
    jal  ra, leaf
    j    sv_next
sv_full:
    lw   a4, 8(s0)
    lw   a5, 12(s0)
    j    expand
sv_next:                        # the iteration found nothing: deepen
    addi a1, a1, 4
    li   t0, 44
    bleu a1, t0, sv_iter
    li   a0, -1
sv_return:
    lw   ra, 20(sp)
    lw   s0, 16(sp)
    lw   s1, 12(sp)
    lw   s2, 8(sp)
    lw   s4, 4(sp)
    addi sp, sp, 24
    ret

# One child: advance the cursor by a quarter turn of the face whose transition
# sits at byte \off of each record, prune on hp, then B, then A, and push if
# the child survives. A pruned child branches to \next. hp goes first because
# it costs two instructions; B before A because it prunes slightly more often
# (61.9% against 59.6% of the children that pass hp, measured on the host).
#
# Scheduling: on the five-stage pipeline a load followed at once by its use
# stalls a cycle, so the four transition loads go first and their adds after,
# and the hp load sits two instructions ahead of its branch. Retired
# instructions are unchanged; RV32_5S cycles drop (see the note).
.macro CHILD off, next
    lhu  t0, \off(a2)
    lhu  t1, \off(a3)
    lhu  t2, \off(a4)
    lhu  t3, \off(a5)
    add  a2, s2, t0
    add  a3, s3, t1
    lbu  t0, 7(a2)              # 4 * hp of the child
    add  a4, s4, t2
    add  a5, s5, t3
    bltu s1, t0, \next
    lhu  t1, 6(a3)              # PDB row offset, 53 * o
    lbu  t2, 6(a5)              # byte of b within the row
    lbu  t3, 7(a5)              # bit offset of b within the byte
    add  t2, t2, t1
    add  t2, t2, s7
    lbu  t2, 0(t2)
    srl  t2, t2, t3
    andi t2, t2, 3              # distance in B, mod 3
    add  t2, t2, a7
    lbu  t5, 0(t2)              # 4 * exact distance in B
    bltu s1, t5, \next
    lbu  t2, 6(a4)
    lbu  t3, 7(a4)
    add  t2, t2, t1
    add  t2, t2, s6
    lbu  t2, 0(t2)
    srl  t2, t2, t3
    andi t2, t2, 3
    add  t2, t2, a6
    lbu  t4, 0(t2)              # 4 * exact distance in A
    bltu s1, t4, \next
    jal  ra, push
.endm

# The three children of one face, as a loop. t6 is the move code
# (move << 2 | face), stepped by 4 per quarter turn; the loop ends when it
# reaches \end, held in s11. A push returns here after its whole subtree,
# which clobbered s11, so the resume point reloads it.
.macro FACE off, first, end
    li   t6, \first
    li   s11, \end
face_loop_\@:
    CHILD \off, face_next_\@
    li   s11, \end
face_next_\@:
    addi t6, t6, 4
    bne  t6, s11, face_loop_\@
.endm

# Expand the node in frame s0. On entry the cursor equals the node, so the
# first face expanded needs no reload; later faces reload it from the frame.
expand:
    beqz s10, expand_f1         # move in was on face 0: start at face 1
    FACE 0, 0, 12
face1:
    li   t0, 1
    beq  s10, t0, face2
    lw   a2, 0(s0)
    lw   a3, 4(s0)
    lw   a4, 8(s0)
    lw   a5, 12(s0)
expand_f1:
    FACE 2, 13, 25
face2:
    li   t0, 2
    beq  s10, t0, pop
    lw   a2, 0(s0)
    lw   a3, 4(s0)
    lw   a4, 8(s0)
    lw   a5, 12(s0)
    FACE 4, 26, 38
pop:
    beq  s0, s9, sv_next        # root exhausted: deepen
    lw   a2, 0(s0)              # cursor = this node, the parent's last child
    lw   a3, 4(s0)
    lw   a4, 8(s0)
    lw   a5, 12(s0)
    lw   t6, 24(s0)             # and the parent's loop continues from its code
    addi s0, s0, -32
    lw   a6, 16(s0)
    lw   a7, 20(s0)
    lw   t0, 24(s0)
    andi s10, t0, 3
    lw   ra, 28(s0)
    addi s1, s1, 4
    jr   ra

# push: the child in the cursor survived. t4, t5 = its 4 * ha, 4 * hb,
# t6 = move code, ra = where the parent resumes. A child one move above the
# bound is not pushed: leaf decides it on the spot and returns to ra.
push:
    li   t0, 4
    beq  s1, t0, leaf
    sw   ra, 28(s0)
    addi s0, s0, 32
    sw   a2, 0(s0)
    sw   a3, 4(s0)
    sw   a4, 8(s0)
    sw   a5, 12(s0)
    add  a6, s8, t4
    add  a7, s8, t5
    sw   a6, 16(s0)
    sw   a7, 20(s0)
    sw   t6, 24(s0)
    andi s10, t6, 3
    addi s1, s1, -4
    j    expand

# leaf: the cursor a2, a3 is one move above the bound, so it finishes only if
# it is one of the nine states a single move solves. Their permutations are
# distinct: the PERM record's candidate byte names the one move that could
# finish (move + 1, or 0), and TARGET holds the twist that state must have.
# t6 = code of the move into the cursor. Returns to ra if it does not finish.
leaf:
    lbu  t0, 6(a2)
    bnez t0, leaf_try
    jr   ra
leaf_try:
    la   t1, TARGET
    slli t2, t0, 1
    add  t1, t1, t2
    lhu  t1, -2(t1)             # 8 * o of the state move t0 - 1 solves
    sub  t2, a3, s3
    beq  t1, t2, leaf_hit
    jr   ra
leaf_hit:
    addi a0, t0, -1
# found: a0 = last move, t6 = code into the leaf's parent (3 when that parent
# is the root), frames s9 + 32 .. s0 hold the codes of the moves before it.
found:
    lw   t2, 0(sp)              # moves out
    addi t1, s9, 32
f_copy:
    bgtu t1, s0, f_tail
    lw   t3, 24(t1)
    srli t3, t3, 2
    sb   t3, 0(t2)
    addi t2, t2, 1
    addi t1, t1, 32
    j    f_copy
f_tail:
    li   t3, 3
    beq  t6, t3, f_last
    srli t3, t6, 2
    sb   t3, 0(t2)
    addi t2, t2, 1
f_last:
    sb   a0, 0(t2)
    addi t2, t2, 1
    lw   t0, 0(sp)
    sub  a0, t2, t0
    j    sv_return

# exact4(a0 = ORI record, a1 = POS record, t5 = POS base, t6 = PDB base)
#   -> a0 = 4 * exact distance in that pattern database.
# From distance d, the neighbour holding (d - 1) mod 3 is at d - 1: the three
# candidate distances d - 1, d, d + 1 are distinct mod 3. Walk down to 0.
exact4:
    li   a5, 0
ex_loop:
    bne  a0, s3, ex_step
    beq  a1, t5, ex_done        # abstract goal: o = 0 and pattern solved
ex_step:
    lhu  t0, 6(a0)              # value here
    lbu  t1, 6(a1)
    lbu  t2, 7(a1)
    add  t0, t0, t1
    add  t0, t0, t6
    lbu  t0, 0(t0)
    srl  t0, t0, t2
    andi t0, t0, 3
    addi a4, t0, -1             # want (v - 1) mod 3
    bgez a4, 1f
    li   a4, 2
1:  li   t3, 0                  # face offset 0, 2, 4
ex_face:
    mv   t1, a0
    mv   t2, a1
    li   a3, 3
ex_turn:
    add  t0, t1, t3
    lhu  t0, 0(t0)
    add  t1, s3, t0
    add  t0, t2, t3
    lhu  t0, 0(t0)
    add  t2, t5, t0
    lhu  t0, 6(t1)
    lbu  t4, 6(t2)
    add  t0, t0, t4
    add  t0, t0, t6
    lbu  t0, 0(t0)
    lbu  t4, 7(t2)
    srl  t0, t0, t4
    andi t0, t0, 3
    beq  t0, a4, ex_take
    addi a3, a3, -1
    bnez a3, ex_turn
    addi t3, t3, 2
    j    ex_face
ex_take:
    mv   a0, t1
    mv   a1, t2
    addi a5, a5, 4
    j    ex_loop
ex_done:
    mv   a0, a5
    ret

# ---------------------------------------------------------------------------
# Verification model: the 14-byte cubie state, independent of the rank tables.
# ---------------------------------------------------------------------------
# copy14(a0 = source, a1 = destination): one cube state. Called with the
# link in t0, so leaf routines can use it without saving ra.
copy14:
    addi t2, a0, 14
1:  lbu  t3, 0(a0)
    sb   t3, 0(a1)
    addi a0, a0, 1
    addi a1, a1, 1
    bne  a0, t2, 1b
    jr   t0

# apply_move(a0 = move 0..8): face = move / 3 and turns = move % 3 + 1, by
# subtraction, then that many quarter turns of WORK. Uses s7 and s8, which
# no caller keeps live.
apply_move:
    addi sp, sp, -4
    sw   ra, 0(sp)
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
.if RENDER
    jal  ra, render
.endif
    addi s8, s8, -1
    bnez s8, am_loop
    lw   ra, 0(sp)
    addi sp, sp, 4
    ret

# quarter_turn(a0 = face): WORK.p[i] = p[src], WORK.o[i] = (o[src] + tw) mod 3
# with src = SOURCE[face][i]. The sum is at most 4, so one conditional
# subtract is exact; done branchless: (s - 3) >> 31 is all ones exactly when
# no reduction was needed, and masked with 3 it adds the modulus back.
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
    bgez t5, 2f                 # reduction needed: done in two
    addi t5, t5, 3              # otherwise add the modulus back: three
2:
.else
    srai t6, t5, 31
    andi t6, t6, 3
    add  t5, t5, t6
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
1:  lbu  t2, 0(t0)
    bne  t2, t1, 2f
    lbu  t2, 7(t0)
    bnez t2, 2f
    addi t0, t0, 1
    addi t1, t1, 1
    bne  t1, t3, 1b
    li   a0, 1
    ret
2:  li   a0, 0
    ret

.if RENDER
# ---------------------------------------------------------------------------
# LED matrix renderer (GUI build only). The peripheral is row-major, one word
# per LED: address = BASE + 4 * (y * WIDTH + x). ROWS[y] = BASE + 4 * y * WIDTH
# is built once by repeated addition, so no multiply is ever needed.
# ---------------------------------------------------------------------------
render_init:
    li   t0, LED_MATRIX_0_BASE
    li   t1, LED_MATRIX_0_WIDTH
    slli t1, t1, 2              # row stride in bytes
    la   t2, ROWS
    li   t3, 25
1:  sw   t0, 0(t2)
    add  t0, t0, t1
    addi t2, t2, 4
    addi t3, t3, -1
    bnez t3, 1b
    ret

# render: draw the 24 facelets of WORK as 4 x 3 pixel blocks of the net,
# then wait DELAY iterations so each quarter turn is visible.
render:
    la   t0, FACELETS
    li   t6, 24
    la   a1, WORK
    la   a2, COLMAP
    la   a3, PAL
    la   a4, ROWS
    li   a5, LED_MATRIX_0_WIDTH
    slli a5, a5, 2
r_cell:
    lbu  t1, 0(t0)              # x
    lbu  t2, 1(t0)              # y
    lbu  t3, 2(t0)              # position
    lbu  t4, 3(t0)              # slot, or palette offset for the fixed corner
    li   t5, 7
    beq  t3, t5, r_colour
    add  t3, a1, t3
    lbu  t5, 0(t3)              # cubie
    lbu  t3, 7(t3)              # twist
    slli t5, t5, 4
    slli t3, t3, 2
    add  t5, t5, t3
    add  t5, t5, t4
    add  t5, t5, a2
    lbu  t4, 0(t5)              # 4 * face colour
r_colour:
    add  t4, t4, a3
    lw   t4, 0(t4)              # RGB
    slli t2, t2, 2
    add  t2, t2, a4
    lw   t2, 0(t2)              # row address
    slli t1, t1, 2
    add  t1, t1, t2             # top-left LED
    li   t5, 3
r_row:
    sw   t4, 0(t1)
    sw   t4, 4(t1)
    sw   t4, 8(t1)
    sw   t4, 12(t1)
    add  t1, t1, a5
    addi t5, t5, -1
    bnez t5, r_row
    addi t0, t0, 4
    addi t6, t6, -1
    bnez t6, r_cell
.if DUMP
    j    dump
.endif
    li   t0, DELAY
1:  addi t0, t0, -1
    bnez t0, 1b
    ret

.if DUMP
# dump: renderer self-test in the CLI, where build.py maps the matrix to plain
# memory. Prints rows 0..19 as one glyph per LED, matching `check --net`.
dump:
    la   a4, ROWS
    li   t6, 20
d_row:
    lw   t1, 0(a4)
    li   t5, 35
d_px:
    lw   t2, 0(t1)
    li   a0, 46                 # '.'
    beqz t2, d_put
    la   t3, PAL
    la   t4, GLYPHS
d_find:
    lw   a0, 0(t3)
    addi t3, t3, 4
    addi t4, t4, 1
    bne  a0, t2, d_find
    lbu  a0, -1(t4)
d_put:
    li   a7, 11
    ecall
    addi t1, t1, 4
    addi t5, t5, -1
    bnez t5, d_px
    li   a0, 10
    ecall
    addi a4, a4, 4
    addi t6, t6, -1
    bnez t6, d_row
    li   a0, 10
    ecall
    ret
    .data
GLYPHS: .string "WRGYOB"
    .text
.endif

    .data
.include "render.inc"
.endif

    .data
.include "tables.inc"
