---
title: "Assignment 1: minirubik on RV32I"
tags: computer-architecture, risc-v, ripes
---

# Assignment 1: Optimizations and RISC-V Assembly — minirubik on RV32I

> Fork: `https://github.com/zhonwei985/minirubik` (forked from `sysprog21/minirubik` at commit `231796c`, "Merge pull request #1 from yenslife/patch-1").
> Submitted tag: `<tag>` · HackMD revision: `<revision URL>`

**Toolchain, pinned.** Everything below was measured with:

| Tool | Version |
| :--- | :--- |
| Ripes | continuous prerelease `v2.2.6-106-g5b8a616` (Windows x86-64), provides `RV32_ISS` |
| Reference compiler | xPack `riscv-none-elf-gcc` 15.2.0-1, the same GCC as `riscv64-unknown-elf-gcc`, invoked as `-O2 -march=rv32i -mabi=ilp32` |
| Host compiler | GCC 15 (MSYS2 UCRT64), `-O2` |

**Measurement conventions**, used for every number in this note:

* *Retired instructions* = `Ripes --mode cli -t asm --proc RV32_ISS --iret` on `rv32/ripes_cli.s` (renderer compiled out) with **one query and no test list**, counted from the first instruction to the exit `ecall`. This includes parsing, ranking, the search, printing, and replaying the path through the cubie model, so it is the whole program, not the search alone. `rv32/measure.py iret STATE` produces it.
* *Code size* = bytes of linked `.text` of the same CLI build, assembled from `rv32/minirubik.S` by GNU `as` and linked at 0 (`rv32/size.sh`).
* *Static data* = `.data` + `.bss` of that link. Ripes has no `.rodata`, so read-only tables sit in `.data`.

---

## 0. Map of the work

| Path | Role |
| :--- | :--- |
| `tools/explore.c` | Stage 2 experiments: the exact BFS oracle, and IDA\* node counts for candidate heuristics on all 2,644 distance-11 states |
| `rv32/model.h` | Host cube model: `source`/`twist`, ranks, the facelet model for the LED net |
| `rv32/gen.c` | Builds every table, runs gates **H1, H2, H4**, emits `tables.h` (C) and `tables.inc` (asm) |
| `rv32/ida.c` | The final C algorithm: freestanding, no heap, no recursion, no multiply or divide |
| `rv32/check.c` | Gate **H3** over all 3,674,160 states, plus `--net` (expected LED frames) and `--list D` |
| `rv32/minirubik.S` | The hand-written RV32I program, the single source for both Ripes builds |
| `rv32/build.py` | Resolves `.macro`, `.if`, `.include` and numeric labels, which Ripes' assembler lacks, and writes `rv32/ripes_cli.s` and `rv32/ripes_gui.s` |
| `rv32/measure.py`, `size.sh`, `ref.sh` | Measurement: `--iret`, the 2,644-state sweep, code size, the GCC reference build |
| `measure/` | Stage 1 probes (memory slope, simulation rate) and the sweep results |

---

## 1. The state space

### 1.1 The group and its order

Label the eight corner positions of the 2×2×2 cube and hold the front-upper-left corner fixed. The moves that keep it fixed are the turns of the three opposite faces, R, B and D, and every position reachable from solved is a pair (σ, o):

* σ ∈ S₇ is where the seven moving cubies sit;
* o ∈ ℤ₃⁷ is each position's twist relative to the U/D axis, with Σ oᵢ ≡ 0 (mod 3).

The quarter turns act by `p'[i] = p[src_f(i)]` and `o'[i] = o[src_f(i)] + tw_f(i) (mod 3)` (`source[][]`, `twist[][]` in `solver.c`). The group generated is the semidirect product

$$G \;=\; \{\, (o,\sigma) \in \mathbb{Z}_3^{7} \rtimes S_7 \;:\; \textstyle\sum_i o_i \equiv 0 \pmod 3 \,\}, \qquad |G| = 3^6 \cdot 7! = 729 \cdot 5040 = 3{,}674{,}160 .$$

It is the stabiliser of the fixed corner inside the full corner group, which has 8!·3⁷ = 88,179,840 elements. The index is 24, the number of rotations of the whole cube. Fixing a corner picks one representative per rotation class, so the object `solver.c` enumerates is the **Cayley graph** Cay(G, S) with S = {R, R2, R′, B, B2, B′, D, D2, D′}, not a Schreier coset graph. Every vertex is a group element, every edge a right multiplication by a generator, and S = S⁻¹ (R′ = R⁻¹, R2 = R2⁻¹), so the graph is undirected and 9-regular.

### 1.2 The orientation-sum invariant

Each row of `twist[][]` sums to 0 mod 3 (R: 1+2+0+2+1 = 6, B: 1+2+1+2 = 6, D: 0), and permuting positions does not change the sum. So Σ oᵢ mod 3 is invariant under every generator, and the solved state has sum 0. This is a **mod-3** invariant, not a parity. It rules out exactly the states that differ from a legal one by twisting a single corner in place by ±120°. That is two of every three assignments of the seven twists, which is why the 7th twist is determined by the other six and the orientation coordinate has 3⁶ = 729 values, not 3⁷.

Permutation parity is *not* constrained here. Each quarter turn is a 4-cycle, an odd permutation, so both parities are reachable. That is why the test vector `21345671111111`, a single transposition, is a legal state.

### 1.3 The diameter is 11, from both sides

Breadth-first search from solved over Cay(G, S) labels each vertex with its distance. Two facts give a diameter of exactly 11:

1. **A state at depth 11 exists:** level 11 is non-empty (2,644 states).
2. **None is deeper:** level 12 is empty *and* the union of levels 0–11 has 3,674,160 = |G| elements. The second part matters. An empty frontier alone only proves that everything reachable has been seen. The count proves everything has been reached, so no state is waiting at an unexplored depth.

Distance histogram (`rv32/gen.c`, reproduced by `tools/explore.c` and matching `report.md`):

| d | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| states | 1 | 9 | 54 | 321 | 1,847 | 9,992 | 50,136 | 227,536 | 870,072 | 1,887,748 | 623,800 | 2,644 |

Jaap Scherphuis' Pocket Cube page gives the same HTM distribution and the QTM diameter 14, an independent corroboration.

---

## 2. Stage 1 — Characterising the baseline

### 2.1 What `solver.c` computes and where the cost is

`solver.c` ranks a state as `p·729 + o` (Lehmer code of σ, base-3 code of the first six twists), builds factored quarter-turn tables (`permutation[3][5040]`, `orientation[3][729]`, 34,614 B), then runs one breadth-first search over all 3,674,160 ranks. For each state it stores the inverse of the move that discovered it. A query follows those moves home, at most 11 lookups. Its invariants are the bijection between valid states and ranks (checked exhaustively by `--self-test`), FIFO order (each first visit is along a shortest path), and the move-inversion check that makes `source`/`twist` describe real quarter turns.

Cost: 3,674,160 states × 9 moves = 33,067,440 edges, each advancing two tables, so 66,134,880 transition updates. At about 15 instructions each that is ≈ 1.0 × 10⁹ retired instructions, plus 18,405,414 B of peak memory (queue 79.8%, move table 20.0%, transition tables 0.2%).

### 2.2 Measurement 1: host bytes per guest byte

VSRTL's `AddressSpace` stores guest memory as `std::unordered_map<VSRTL_VT_U, uint8_t> m_data`, one map node per guest byte (`include/VSRTL/core/vsrtl_addressspace.h`). Its `readMem` also goes through `m_data[address++]`. `operator[]` inserts, so even *reading* an untouched address allocates a node.

Probe (`measure/mem_<KiB>.s`): a `sw` loop over N KiB from `0x10000000`, against a control that writes nothing. The Ripes process's `PeakWorkingSet64` was sampled every 50 ms (`measure/peak.ps1`, Windows; two runs each, the runs agree to 0.01%):

| Region written | Peak working set |
| ---: | ---: |
| 0 (control) | 9.2–14.2 MB |
| 4 MiB | 361.3 MB |
| 8 MiB | 697.9 MB |
| 16 MiB | 1,371.3 MB |

Slope between 8 and 16 MiB: (1,371,340,800 − 697,925,632) / 8,388,608 = **80.3 host bytes per guest byte**. Between 4 and 8 MiB it is 80.3 again, so the cost is linear, with no fixed per-page overhead hiding in it. On this MSVC build a node is a doubly linked list entry (two pointers plus the 5-byte payload, padded) plus heap header and bucket vector, consistent with ~80 B.

Projected onto the baseline's 18,405,414 B peak: 18,405,414 × 80.3 ≈ **1.48 GB** of host memory just for guest bytes, before the simulator itself. That is not impossible on a 16 GB host, but it is the wrong order of magnitude for a 128 KiB-class problem. Every table entry the baseline writes once costs 80 B on the host, and every one it reads later costs a hash lookup.

### 2.3 Measurement 2: simulation rate

Two probes, run with `--exectime`: a 4-instruction ALU loop (`measure/ips_N.s`) and a 6-instruction load/store loop over a 256-byte resident buffer (`measure/memloop_N.s`), two runs each:

| Ripes model | ALU loop (instr/s) | Memory loop (instr/s) | Idealised time for 10⁹ |
| :--- | ---: | ---: | ---: |
| RV32_ISS | 28.8 M | 26.1 M | 38 s |
| RV32_SS | 1.40 M | 1.44 M | 12 min |
| RV32_5S | 347 k | 333 k | 50 min |
| RV32_6S_DUAL | 114 k | 116 k | 2.4 h |

These are about 24× the assignment's figures on every model. My runs are the headless CLI on a different host, while a GUI run also redraws. The *ratios* between models agree (ISS ≈ 19× SS ≈ 78× 5S). A third probe that stores to fresh memory (`mem_16384.s`) runs ISS at only 1.76 M instr/s, because every store inserts four hash nodes. On RV32_ISS the hash map, not the instruction decode, sets the speed of a memory-hungry program.

### 2.4 Why `report.md` §7 does not survive the move

§7 keeps the full table "because the table is the verification artifact": building it proves that the nine generators reach exactly 3,674,160 states at diameter 11. On the host that proof costs 0.065 s and 18 MB. On Ripes the same argument fails for three independent reasons:

1. **Memory.** 1.48 GB of host memory from §2.2, for data whose only purpose is a proof that does not depend on the query.
2. **Time.** ~10⁹ instructions, minutes on RV32_ISS and days on the pipelined models, *per query*, again repeating a query-independent proof every run.
3. **Budget.** The assignment caps static data at 128 KiB and forbids precomputing the full distance table on the host. The table can no longer live on the target in any form.

The proof §7 values is still needed, but it belongs on the host, once: the BFS oracle in `gen.c`/`check.c` establishes the diameter (§1.3) and gates H1–H4 tie the target's tables to it. The target only has to *use* tables whose correctness was established elsewhere, and prove per query what matters per query: the returned path solves the state and is as short as the host says it must be.

---

## 3. Stage 2 — Representation and algorithm

### 3.1 Choice: IDA\* with three admissible tables

Without full enumeration the search must be depth-first, and to stay optimal it must be **iterative deepening A\*** (Korf 1985). Depth-first search to bound b = h(root), b+1, … returns the first solution found at the first bound that admits one. Every shorter path was ruled out by the previous complete iteration, provided h never overestimates. It needs no queue and no visited set, only one frame per ply: 12 × 32 B here.

The heuristic must be **admissible**, h(s) ≤ d(s). Pattern databases (Culberson & Schaeffer 1998) give admissibility by construction. Take an abstraction φ: G → X that commutes with the moves (φ(s·m) = φ(s)·m). Then every path s → solved maps to a path φ(s) → φ(solved) of the same length, so the BFS distance in X is a lower bound: d_X(φ(s)) ≤ d(s). The maximum of several such bounds is again a lower bound.

`report.md` §4 supplies the abstractions. σ and o evolve independently, so "forget σ" and "forget o" are projections. So is "keep o and the *positions* of a subset of cubies", because positions of chosen cubies move only with the permutation part. Each such projection has equal-sized fibres. Keeping the positions of 3 named cubies leaves the other 4 in 4! = 24 arrangements, so the abstract space has 3,674,160 / 24 = 153,090 states exactly.

### 3.2 Choosing the abstractions by measurement

`tools/explore.c` builds each candidate table and runs IDA\* on **all 2,644 distance-11 states** with the free same-face pruning described in the assignment. The table reports the worst and mean number of nodes. One sample says nothing; the spread across distance-11 states is about 50× for the better heuristics.

| Heuristic (max of) | Entries | Mean h | d=11 worst | d=11 mean |
| :--- | ---: | ---: | ---: | ---: |
| hp (permutation, 5,040) and ho (twist, 729) | 5,769 | 5.14 | 639,798 | 206,625 |
| hp and o × pos{0,1} | 35,658 | 6.10 | 228,036 | 45,632 |
| hp and o × pos{0,1,2} | 158,130 | 6.96 | 114,384 | 14,616 |
| o × pos{0,1,2,3} alone | 612,360 | 7.58 | 315,570 | 19,158 |
| **hp, o × pos{0,1,2}, o × pos{0,3,6}** | 311,220 | 7.39 | **39,273** | **4,215** |
| same two pattern tables without hp | 306,180 | | 144,291 | 7,012 |

Three findings drove the design:

* **Size is not strength.** The 612,360-entry table loses to a 153,090-entry one plus hp in the worst case. It knows nothing about the other four cubies, and the worst states are exactly the ones that exploit that.
* **hp is cheap and essential.** Dropping the 5,040-entry permutation table from the best pair multiplies the worst case by 3.7.
* **Pairs differ by subset.** All 595 pairs of 3-cubie subsets were screened (`tools/pairs.c`). The best worst cases are within 1% of each other (38,872–39,273). {0,1,2}+{0,3,6} has the best mean, so it was kept.

### 3.3 Fitting 128 KiB: two bits per entry

Two 153,090-entry tables at a byte each are 306 KB, and at a nibble each 153 KB, both over budget. They fit at **two bits per entry**:

* Distances in an undirected abstract graph are *consistent*: across any edge |d_X(x) − d_X(y)| ≤ 1.
* So a child's distance is one of h−1, h, h+1 for the parent's h, and those three are distinct mod 3. Storing d mod 3 (2 bits) and the parent's exact h recovers the child's exact h with one 40-byte lookup table `HT4[4h + v] = 4h'`.
* At the root, the exact h is recovered by **descent**: from a state at distance d, the neighbour whose stored value is (d−1) mod 3 is at distance d−1, so walking down to the abstract goal counts d (at most 9 steps).

Gate H4 checks both halves exhaustively: the packed accessor against the unpacked table at all 306,180 indices (153,090 even, 153,090 odd), and `HT4` against the exact distances on all 2,755,620 abstract edges.

### 3.4 Memory, quantified

All figures are bytes from `rv32/gen.c`'s emitter and the GNU link (`rv32/size.sh`):

| Table | Layout | Bytes |
| :--- | :--- | ---: |
| `PERM` | 5,040 × {3 × u16 next, u8 cand, u8 4·hp} | 40,320 |
| `ORI` | 729 × {3 × u16 next, u16 row = 53·o} | 5,832 |
| `POSA`, `POSB` | 210 × {3 × u16 next, u8 byte, u8 shift}, each | 1,680 + 1,680 |
| `PDBA`, `PDBB` | 729 rows × 53 B, 2 bits/entry, each | 38,637 + 38,637 |
| `HT4`, `TARGET` | mod-3 recovery; twist of the 9 distance-1 states | 40 + 18 |
| `MAPA`, `MAPB` | 343-byte maps from 49·w₀ + 7·w₁ + w₂ to a pattern coordinate (root only) | 343 + 343 |
| **tables** | (with 2-byte alignment pads) | **127,530** |
| strings, tests, `SOURCE`/`TWIST`, names | | 265 |
| `.bss`: 12 frames × 32 B, cube buffers, moves | | 456 |
| **static data, CLI build** | `.data` 127,795 + `.bss` 456 | **128,251 = 97.9% of 131,072** |
| static data, GUI build | + renderer tables and LED row table | 128,583 |

Stack: `solve` 24 B, `run_case` 4 B, `apply_move` 4 B, so the peak is under 40 B. No heap. The Ripes process running the whole CLI program peaks at 42.9 MB against 15.3 MB for the empty control. About 10 MB of the difference is the ~130 KB of guest bytes at 80 B each (§2.2). The rest is Ripes holding the 460 KB assembly source.

Compared with the baseline: 18,405,414 → 128,251 bytes, a factor of 143. The complete distance table at 4 bits/state would still be 1,794 KiB. Packing alone could not have saved enumeration; dropping enumeration did.

### 3.5 Pruning that was measured and not kept

* **Skip a table lookup when the parent's value proves the child cannot be pruned.** Consistency means a child of a parent with h ≤ limit−1 can't exceed the limit. Measured on the host over all distance-11 queries, that is only 15.8% of A lookups and 13.6% of B lookups. The mask bookkeeping per expanded node would cost about as much as it saves. Not done.
* **Leaf scan versus candidate byte.** A node one move above the bound can only finish if it is one of the nine distance-1 states. Their permutations are distinct, so a spare byte in each `PERM` record names the one move that could finish, and `TARGET` holds the twist that decides it. This replaced a six-child scan (v1 → v2). It saved ~230 B of code and **almost no instructions** (290,965 → 290,965). Measured: almost no child one move above the bound passes all three tables, so the scan almost never ran. Kept for the code size.
* **Check order.** The cheap hp test (2 instructions) goes first. B before A because B prunes 61.9% of children that pass hp and A 59.6%. That is worth ≈0.2 instructions per child on average, and costs 0.6% on the single costliest state (v3 → v4).

---

## 4. Stage 3 — Efficiency in C first

The exploration code (`tools/explore.c`) is a correct IDA\*, but written for the host. It is recursive, indexes `ox[from_perm[p] * 729 + o]` (a multiply by 729 per lookup), stores coordinates as plain indices that every table access must scale, and computes `% 3` in the move model. On RV32I each of those costs: the multiplies become `__mulsi3` calls, which are forbidden, recursion needs a stack frame per ply, and every scaled index is a shift plus an add. `rv32/ida.c` removes them one at a time:

| Change in `ida.c` | Why it pays on RV32I (per child unless stated) |
| :--- | :--- |
| Coordinates stored as **byte offsets** into 8-byte records: `next = 8·index` | A transition is `lhu` + `add`. No shift, no multiply, ever. |
| Records interleave the three faces' transitions with the per-state data (hp, row, byte/shift) | The address computed for a transition is reused for the heuristic load: one `add` serves both |
| Pattern coordinates `POSA`/`POSB` tracked by their own 210-entry transition tables | The table index needs no permutation-to-pattern map (5,040 B) and no 729-multiply |
| Rows padded from 52.5 to **53 bytes**, row offset stored premultiplied (`53·o`) in `ORI` | Index = `row + byte`, with no multiply by 53 and no carry between half-bytes |
| Heuristic values carried **×4** | `HT4[4h + v]` needs no shift, and limits compare directly |
| Same-face pruning by an explicit `skip` field | 6 of 9 children per node, free |
| Lazy evaluation: hp (2 instr), then B, then A | 23% of children pruned by hp never touch a pattern table |
| Explicit frame stack, no recursion | One frame = 32 B. No call per node, no `ra` save per node |
| `% 3` removed: parse reduces a sum ≤ 14 by subtraction, twists use a conditional subtract | No `__umodsi3` |

The GCC reference proves the C is division- and multiply-free. `rv32/ref.sh` refuses to report a build whose disassembly contains `mul`/`div`/`rem` or a libgcc arithmetic routine, and the build passes. The root setup multiplies only by constants (×6, ×5, ×4, ×3, ×2, ×49, ×7), which GCC turns into shifts and adds.

Measured by the stage-4 reference build of this C: about 48 retired instructions per child (524,013 retired for 10,814 children on `21345671111111`), against 29 for the hand-written assembly. §5.4 explains where the 19 go.

---

## 5. Stage 4 — RV32I assembly

### 5.1 The two builds, and why Ripes needed a build script

Ripes' assembler (this build) rejects `.if`, `.macro`, `.include`, `.space`, `.rodata` and numeric local labels (`1:`/`1f`), all checked with small probe files. The assignment's suggested `.equ RENDER, 0` / `.if RENDER` therefore cannot be used directly. `rv32/minirubik.S` is written in standard GNU syntax with exactly those directives. GNU `as` assembles it unchanged, which is how code size is measured. `rv32/build.py` performs the same expansion GNU `as` would and writes two Ripes-ready files:

* `rv32/ripes_cli.s`: `RENDER = 0`, for `--iret`;
* `rv32/ripes_gui.s`: `RENDER = 1`, animates on the LED matrix.

**The two builds differ only in the renderer:** `render_init`, `render`, the `ROWS` table, `render.inc`, and two `jal ra, render` call sites, all inside `.if RENDER`.

One Windows trap: `minirubik.s` and `minirubik.S` are the *same file* on NTFS. The first build overwrote its own source, so the outputs are named `ripes_*.s`.

### 5.2 Register allocation and the frame stack

```
s0 frame   s1 limit·4   s2 PERM s3 ORI s4 POSA s5 POSB s6 PDBA s7 PDBB s8 HT4
s9 FRAMES  s10 face of the move in   s11 end-of-face code   t6 move code
a2..a5 cursor (record addresses of the last child)   a6,a7 HT4 + parent's h·4
```

All seven table bases stay in registers for the whole search, so no `la` runs in the hot path. The cursor holds **record addresses**, not indices: one quarter turn is

```asm
    lhu  t0, 0(a2)      # 8 * p' for face R (offset 2f for face f)
    add  a2, s2, t0     # PERM + 8 * p' = the child's record
```

and the child's heuristic byte is then `lbu t0, 7(a2)`, using the address the transition just produced.

Recursion is replaced by 32-byte frames: node coordinates, `HT4 + h·4` for both tables, the move code, and the **resume address**. `push` stores `ra`, which `jal ra, push` set to the instruction after the call inside the face loop. `pop` reloads it and `jr ra` continues the parent's loop exactly where it stopped. The parent's position in its turn loop is the move code `t6`, which `pop` restores from the child's frame, since the child was entered by that move.

### 5.3 The child block

The hot path is one macro (`CHILD` in `minirubik.S`), 31 instructions when nothing prunes:

```asm
    lhu  t1, 6(a3)          # PDB row offset, 53 * o
    lbu  t2, 6(a5)          # byte of b within the row
    lbu  t3, 7(a5)          # bit offset of b within the byte
    add  t2, t2, t1
    add  t2, t2, s7         # PDBB + row + byte
    lbu  t2, 0(t2)
    srl  t2, t2, t3
    andi t2, t2, 3          # distance in B, mod 3
    add  t2, t2, a7         # HT4 + 4h(parent) + v
    lbu  t5, 0(t2)          # 4 * exact distance in B
    bltu s1, t5, \next      # prune
```

This is the nibble-read cost the assignment predicted, about seven instructions for a packed read plus two for the mod-3 recovery. The price of 2-bit packing is paid here; §3.3 is what it buys.

### 5.4 Iterative refinement, measured

Each row is a commit, except v3, which was measured and then committed together with v4. "Ref" is `21345671111111`, "worst" is `32156471111111` (the costliest distance-11 state for this search on the host, 39,264 nodes).

| Step | Change | `.text` B | Ref iret | Worst iret | Ref cycles, RV32_5S |
| :--- | :--- | ---: | ---: | ---: | ---: |
| v1 `0a83211` | first working version: 9 unrolled child blocks, resume addresses, six-child leaf scan | 3,516 | 290,965 | 952,644 | — |
| v2 `280cee2` | candidate byte instead of leaf scan (§3.5) | 3,272 | 290,965 | 952,632 | — |
| v3 | one loop per face (`t6` steps by 4, ends at `s11`); no cursor reload on the first face | 2,532 | 319,682 | 1,057,135 | — |
| v4 `7c60d0b` | B before A; `solve` saves only live registers; names carry the separator; shared copy | 2,376 | 313,974 | 1,063,427 | 437,520 |
| v5 `10059e2` | loads scheduled ahead of uses; branching mod 3 (§5.6) | **2,372** | **313,793** | **1,063,256** | **383,450** |
| GCC -O2 rv32i, same C (`ref.sh`) | | 2,884 | 524,013 | 1,786,067 | |

The trade at v3 is deliberate, and the table shows its price. Unrolling the 3×3 child blocks (v2) is the fastest form, but it is 388 B *larger* than GCC. The two-instruction latch of a rolled loop costs 9.9%, buys 740 B, and v4 recovers some of it. v5 is the submitted version: **1.67× fewer instructions than GCC on the reference vector, 1.68× on the worst state, and 18% less code.**

Where GCC's extra instructions go, read from its disassembly of `ida_solve` (`objdump -d build/ref.elf`). Its child loop is good code: the cursor lives in registers inside the turn loop, and the checks are in the same order. The differences are structural:

* GCC does not specialise the face, so the field offset `2f` is a register added to every transition address: **+4 `add`** per child. The hand-written version has one loop per face with the offset as an immediate (`lhu t0, 2(a2)`).
* The C carries coordinates as offsets, so each record address is rebuilt as `base + offset` for the transition and again for the heuristic load. The assembly carries record *addresses*, and one `add` serves both: **+3 to +4**.
* The parent's pattern values are reloaded from the frame for every child (`lw s4,16(a5)`, `lw t0,20(a5)`), and the `HT4` base is added separately: **+2 to +4**. The assembly keeps `HT4 + 4h` for both tables in `a6`/`a7` for the whole node.
* Push and pop go through `frame_t` fields one at a time, including the `face`/`turn` bookkeeping that the move code and stored resume address replace.

That accounts for most of the ~19 instructions per child between §4's 48 and the assembly's 29.

### 5.5 Pass condition: every distance-11 state on RV32_ISS

The pass condition is a worst case, so all 2,644 distance-11 states were run through Ripes, one assembly per state (`rv32/measure.py sweep`, results in `measure/d11_iret_v5.csv`). Each run also checks on the target that the path replays to solved (T5) and has length 11:

| | Retired instructions (whole program, RV32_ISS) |
| :--- | ---: |
| worst | **1,063,256** (`32156471111111`, the state the host search predicted) |
| mean | 136,153 |
| best | 44,776 (`54231673123121`) |
| failures (replay or length ≠ 11) | 0 of 2,644 |

The budget is 5 × 10⁷, so the worst state uses about 2% of it. The v1 sweep (`measure/d11_iret_v1.csv`) gave worst 952,644 on the same state, mean 126,274, also with no failures.

**Reported, not graded:** `21345671111111` retires **313,793** instructions (RV32_ISS, CLI build, single query).

### 5.6 Instruction sequences the base ISA forces

* **No multiply.** Every index in the hot path is a premultiplied table entry. The root setup's constants use shift-and-add: `7x = (x << 3) − x` for the 343-entry pattern keys `(7·w₀ + w₁)·7 + w₂`, `3o = (o << 1) + o` for the twist rank, and a run of at most 5 adds for the Lehmer factor `(7 − i)`, executed once per query. LED rows: `ROWS[y] = BASE + y·4·WIDTH` is built by repeated addition at start-up, so drawing never multiplies by `LED_MATRIX_0_WIDTH`. All strides chosen on purpose are powers of two: records of 8 bytes, `SOURCE`/`TWIST` rows padded from 7 to 8, `COLMAP[16c + 4o + k]`, names 4 bytes each.
* **No divide.** The move number's face and turn count (`m / 3`, `m % 3`) are found by subtraction in `apply_move`; at most 2 iterations. The parse checks the twist sum (≤ 14) mod 3 by subtraction.
* **Mod 3 of a sum ≤ 4**, both forms behind `.equ MOD3_BRANCH`:

  ```asm
  # branchless, 4 after the add           # branching, 2 if taken, 3 if not
  addi t5, t5, -3                         addi t5, t5, -3
  srai t6, t5, 31                         bgez t5, 2f
  andi t6, t6, 3                          addi t5, t5, 3
  add  t5, t5, t6                         2:
  ```

  | `21345671111111` | RV32_ISS iret | RV32_5S cycles | RV32_6S_DUAL cycles |
  | :--- | ---: | ---: | ---: |
  | branchless | 313,974 | 383,450 | 371,155 |
  | branching | 313,793 | 383,337 | 370,968 |

  The expectation that the branching form "loses on a pipelined model that has to flush" did not hold on Ripes. A taken branch costs 2 instructions plus at most a 2-cycle flush, which *ties* the 4-cycle branchless form, and the untaken path is 3. It can only lose if the flush is longer than 2 cycles. The branching form is the default; this is a negative result I measured rather than assumed.
* **Branch-against-zero.** RV32I has no compare-with-immediate branch, so `x0` carries the common cases: `beqz`, `bnez`, `bgez`. Comparisons against a non-zero constant (the leaf limit 4, the face numbers) need a `li` first, which is why the end-of-face code lives in a register (`s11`).

---

## 6. Correctness gates

| Gate | How | Result |
| :--- | :--- | :--- |
| **H1** admissibility | `gen.c`: max(hp, A, B) against the exact BFS distance for all 3,674,160 states | h ≤ d everywhere; mean h 7.394 vs mean d 8.756; h = d on 17.2% |
| **H2** tables populated | `gen.c`: every BFS reaches its whole space; solved entry 0; maxima hp 7, A 9, B 9; 9 distinct distance-1 permutations | pass |
| **H3** optimal everywhere | `check --all`: `ida.c` on every state, length must equal the BFS distance, path replayed through the cubie model | pass, all 3,674,160 states, **11.2 s** wall clock (`-O2`, one thread) |
| **H4** packed accessor | `gen.c`: all 306,180 entries, even and odd indices; mod-3 recovery on all 2,755,620 abstract edges | pass |
| **T5** path solves | on target: replay through `SOURCE`/`TWIST` (independent of the rank tables), `work_solved` | pass on every case run, including all 2,644 distance-11 states |
| **T6** reference vector | on target: `21345671111111` returns `R B' D2 R' B R' B' R D2 R B`, 11 moves | pass |
| **T7** both models | `ripes_cli.s` (query + 6 tests: solved, 3-move scramble, distances 8, 10, 11, 11) | `PASS 7/7` on RV32_ISS and RV32_5S, identical output |

The test list carries each state's exact distance from H3, so the program checks *optimality* itself, not just that the path works. For the query, whose distance the program cannot know, it checks the replay and ≤ 11. The exit status is the number of failures.

H3 and H1 are not redundant. A heuristic could be inadmissible on a state whose search happens to find an optimal path anyway; H1 would catch that and H3 would not.

---

## 7. The LED matrix

### 7.1 Mapping

The net is 4×3 face slots (U on top, L F R B across, D below), each face 2×2 facelets of 4×3 pixels, one separator pixel between face slots: 8·4 + 3 = 35 columns, 6·3 + 2 = 20 rows. A facelet at face slot (c, r), facelet (i, j) has its top-left pixel at x = 9c + 4i, y = 7r + 3j. The peripheral is row-major, one word per LED, so the address is `ROWS[y] + 4x`. The peripheral's own GUI text gives a column-major formula, which is wrong; `examples/C/leds.c` agrees with row-major. Only `LED_MATRIX_0_BASE` and `LED_MATRIX_0_WIDTH` are used, never a literal address.

**Which sticker colour goes where** is not in `solver.c`, which only knows twists. `rv32/model.h` derives it. It places the eight corners at (±1, ±1, ±1), realises R, B and D as 90° rotations about x, z and y, and orders each corner's three stickers as U/D sticker first, then the other two in a fixed rotational sense. `facelet_model()` searches both senses and both rotation directions and accepts only a combination that reproduces **both** `source[][]` and `twist[][]` exactly. Exactly one does (hand +1; R −90°, B +90°, D +90°). This also independently confirms that `twist[][]` describes a physical cube, beyond the move-inversion argument of `report.md` §6. A cubie with twist o shows its own sticker s in position slot (o + s) mod 3. That becomes a 112-byte table `COLMAP[16·cubie + 4·twist + slot]`, so the renderer never computes a mod.

### 7.2 Driven by the solver's output

`run_case` draws the parsed state, then replays the returned moves one quarter turn at a time, calling `render` after each (`apply_move`), followed by `DELAY` busy-wait iterations. The frames *are* the T5 replay, so what is shown is exactly the path being verified. R2 and R′ are shown as two and three quarter turns, so cubies are seen travelling.

### 7.3 Testing the renderer without a GUI

The CLI has no LED peripheral, so `build.py dump` builds a third variant. It sets `LED_MATRIX_0_*` to plain memory and adds `DUMP`, which prints each frame as one glyph per LED. `check --net STATE` prints the frames the host facelet model predicts. They compare **byte-identical** on every frame of 24173562322133 (7 frames), 21345671111111 (22) and 32156471111111 (22).

> **[Screenshot: LED matrix 35×25 in Ripes during the solution of 21345671111111]**

---

## 8. Instruction-level walkthrough on RV32_5S

The walkthrough follows the start of one child (`CHILD 0, …`) in the 5-stage processor with forwarding and hazard detection (`RV32_5S`). It compares v4 and v5 to show the one pipeline effect that changed the cycle count.

**v4 order** (load immediately followed by its use):

```asm
lhu  t0, 0(a2)      # (1)
add  a2, s2, t0     # (2) needs t0
lhu  t1, 0(a3)      # (3)
add  a3, s3, t1     # (4) needs t1
```

| cycle | IF | ID | EX | MEM | WB |
|---|---|---|---|---|---|
| 1 | (1) | | | | |
| 2 | (2) | (1) | | | |
| 3 | (3) | (2) | (1): ALU a2+0 | | |
| 4 | (3) | (2) | bubble | (1): read | |
| 5 | (4) | (3) | (2): t0 forwarded MEM/WB→EX | bubble | (1) |

* **IF:** the PC mux selects PC+4; instruction memory supplies the word.
* **ID:** the decoder sets the register-file read addresses (a2 for (1); s2, t0 for (2)). The **hazard unit** compares ID's source `t0` with EX's destination of a *load* (`MemRead = 1`) and stalls: the PC and IF/ID write-enables drop to 0 and a bubble enters EX.
* **EX:** for (1) the ALU-B mux selects the immediate and the ALU adds. For (2), one cycle later, the forwarding mux on ALU operand B selects the MEM/WB value, the loaded halfword.
* **MEM:** for (1) data-memory read enable is 1, width halfword, zero-extended (`lhu`).
* **WB:** register write enable is 1 and the write-back mux selects the memory output for (1), and the ALU result for (2).

Each such pair costs one bubble: 4 per child in v4.

**v5 order** puts the four loads first and the four adds after. Each add's operand comes from a load at least two instructions earlier, so the forwarding paths deliver it with no stall. Measured: 437,520 → 383,450 cycles for the same 313,974 retired instructions (CPI 1.39 → 1.22).

**Branch.** `bltu s1, t0, next` is resolved in EX. When a child is pruned (taken), the two younger instructions in IF and ID are flushed (IF/ID and ID/EX clear signals asserted), and the PC mux selects the branch target.

**Memory update, and why it is correct.** In `push`, `sw a2, 0(s0)` writes the child's PERM-record address into the new frame. In MEM, data-memory write enable is 1 and the address is `s0` from EX. `s0` was advanced by `addi s0, s0, 32` two instructions earlier and arrives by forwarding. On the matching `pop`, `lw a2, 0(s0)` reads it back before `s0` is decremented. That restores the parent's cursor exactly as it was when the child was generated, which is why the parent's face loop resumes on the right sibling.

> **[Screenshots to add from the Ripes GUI: RV32_5S datapath at cycles 3–5 of the v4 sequence showing the stall and the forwarding mux; a taken `bltu` showing the flush; `sw a2, 0(s0)` in MEM with write enable high.]**

---

## 9. Reflection and use of AI

Parts of this work were developed with an AI assistant (Claude), as the commit trailers record. Every design decision in this note is backed by a measurement I can reproduce with the scripts listed in §0. Accepted, rejected and modified suggestions:

* *Accepted after measurement:* two 2-bit tables with mod-3 recovery (§3.3); resume addresses in frames (§5.2); load scheduling (§5.4, v5).
* *Rejected after measurement:* the consistency-based lookup skip (§3.5, 15.8% applicability); keeping the unrolled child blocks as the final version (fastest, but it loses to GCC on code size).
* *Corrected:* the expectation that branchless mod 3 wins on pipelines (§5.6); a version-1 draft that wrote a placeholder byte to low memory, caught in review before the first run.

## References

1. Waterman & Asanović (eds.), *The RISC-V Instruction Set Manual, Vol. I*, ch. 2 (RV32I).
2. *RISC-V Assembly Programmer's Manual.*
3. M. B. Petersen, Ripes, `docs/cli.md`, `docs/mmio.md`, `docs/ecalls.md`; VSRTL `vsrtl_addressspace.h`.
4. R. E. Korf, "Depth-First Iterative-Deepening: An Optimal Admissible Tree Search," *Artificial Intelligence* 27(1), 1985.
5. J. C. Culberson & J. Schaeffer, "Pattern Databases," *Computational Intelligence* 14(3), 1998.
6. R. E. Korf, "Finding Optimal Solutions to Rubik's Cube Using Pattern Databases," AAAI-97.
7. S. Russell & P. Norvig, *Artificial Intelligence: A Modern Approach*, 4th ed., ch. 3.
8. J. Scherphuis, *Pocket Cube* (distance distributions, HTM and QTM).
9. A. Valmari, "What the small Rubik's cube taught me about data structures, information theory, and randomisation," *STTT* 8(3), 2006.
10. H. S. Warren, *Hacker's Delight*, 2nd ed., ch. 8 (multiplication by constants).
11. R. E. Bryant & D. R. O'Hallaron, *CS:APP*, 3rd ed., §2.3, §3.6.
