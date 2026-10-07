#!/usr/bin/env python3
"""Retired-instruction measurements on the pinned Ripes build.

  measure.py iret STATE [PROC]     --iret of the CLI build with only STATE as
                                   input (no tests), default RV32_ISS
  measure.py sweep LISTFILE CSV    --iret for every state in LISTFILE (one per
                                   line, e.g. `check --list 11`), worst first
  measure.py elf ELF [PROC]        --iret of an ELF, e.g. the gcc reference

The convention for every count in the note: RV32_ISS, the CLI build
(renderer compiled out), one query, no test list, whole program from the
first instruction to the exit ecall, including parsing, printing and the
replay check.
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
RIPES = os.environ.get("RIPES", r"C:\Users\User\tools\ripes\Ripes.exe")

sys.path.insert(0, HERE)
import build  # noqa: E402


def ripes(src, kind="asm", proc="RV32_ISS", extra=()):
    run = subprocess.run(
        [RIPES, "--mode", "cli", "--src", src, "-t", kind, "--proc", proc,
         "--iret", "--cycles", *extra],
        capture_output=True, text=True, timeout=3600)
    # The program's console output is on stderr, with a NUL after each string.
    out = (run.stdout + run.stderr).replace("\x00", "")
    m = re.search(r"instructions retired\s+(\d+)", out)
    c = re.search(r"cycles\s+(\d+)", out)
    e = re.search(r"exited with code: (-?\d+)", out)
    return (int(m.group(1)) if m else None, int(c.group(1)) if c else None,
            int(e.group(1)) if e else None, out)


def iret_state(state, proc="RV32_ISS", variant="cli", sets=None):
    fd, path = tempfile.mkstemp(suffix=".s")
    os.close(fd)
    try:
        build.build(variant, path, query=state, tests=False, sets=sets)
        return ripes(path, proc=proc)
    finally:
        os.remove(path)


def main(argv):
    if argv[1] == "iret":
        proc = argv[3] if len(argv) > 3 else "RV32_ISS"
        n, cyc, code, out = iret_state(argv[2], proc)
        print(out.strip())
        print("state %s proc %s iret %s cycles %s exit %s" %
              (argv[2], proc, n, cyc, code))
        return 0 if code == 0 else 1
    if argv[1] == "sweep":
        # Optional 4th argument: the exact distance of every listed state;
        # each printed solution length must equal it.
        states = [l.strip() for l in open(argv[2]) if l.strip()]
        want = int(argv[4]) if len(argv) > 4 else None
        rows = []
        with open(argv[3], "w") as f:
            f.write("state,iret,length,exit\n")
            for i, s in enumerate(states):
                n, _, code, out = iret_state(s)
                m = re.search(r"\[\s*(-?\d+)\]", out)
                length = int(m.group(1)) if m else None
                if want is not None and length != want:
                    code = code or 99
                rows.append((n, s, code))
                f.write("%s,%s,%s,%s\n" % (s, n, length, code))
                f.flush()
                if (i + 1) % 100 == 0:
                    print("%d/%d, worst so far %d" %
                          (i + 1, len(states), max(r[0] for r in rows)),
                          flush=True)
        bad = [r for r in rows if r[2] != 0 or r[0] is None]
        rows.sort(reverse=True)
        mean = sum(r[0] for r in rows) / len(rows)
        print("%d states, %d failures; worst %d (%s), mean %.0f, best %d (%s)"
              % (len(rows), len(bad), rows[0][0], rows[0][1], mean,
                 rows[-1][0], rows[-1][1]))
        return 1 if bad else 0
    if argv[1] == "elf":
        proc = argv[3] if len(argv) > 3 else "RV32_ISS"
        n, cyc, code, out = ripes(argv[2], kind="elf", proc=proc)
        print(out.strip())
        print("elf %s proc %s iret %s cycles %s exit %s" %
              (argv[2], proc, n, cyc, code))
        return 0 if code == 0 else 1
    raise SystemExit(__doc__)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
