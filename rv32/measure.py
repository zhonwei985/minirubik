#!/usr/bin/env python3
"""Retired-instruction measurements on the pinned Ripes build.

  measure.py iret STATE [PROC]       one query, no tests, default RV32_ISS
  measure.py gcc STATE [PROC]        the same for the gcc -O2 reference
                                     build (rv32/gcc/, built in WSL)
  measure.py sweep LIST CSV [D]      --iret for every state in LIST (one per
                                     line, from `check --list D`); with D,
                                     every printed length must equal D

Convention for every count: RV32_ISS, rv32/ripes_cli.s built with only the
query (no test list), counted from the first instruction to the exit ecall.
That includes parsing, ranking, the search, printing and the replay check.
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
RIPES = os.environ.get("RIPES", r"C:\Users\User\tools\ripes\Ripes.exe")
WSL_DISTRO = os.environ.get("WSL_DISTRO", "Ubuntu-24.04")
sys.path.insert(0, HERE)
import build  # noqa: E402


def wsl_path(path):
    drive, rest = os.path.splitdrive(os.path.abspath(path))
    return "/mnt/" + drive[0].lower() + rest.replace("\\", "/")


def build_gcc(out, state=None, tests=True):
    """rv32/gcc/build.sh in WSL; returns its size report."""
    args = [wsl_path(out)] + ([state] if state else [])
    if not tests:
        args.append("--no-tests")
    r = subprocess.run(
        ["wsl.exe", "-d", WSL_DISTRO, "--", "sh",
         wsl_path(os.path.join(HERE, "gcc", "build.sh"))] + args,
        capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(r.stdout + r.stderr)
    return r.stdout


def run(state, proc="RV32_ISS", sets=None, gcc=False):
    """Returns (retired, cycles, exit status, solution length, output).
    gcc=True runs the gcc -O2 reference build instead of the assembly,
    built under build/ so that WSL can reach it."""
    tmp = None
    if gcc:
        tmp = os.path.join(HERE, "..", "build")
        os.makedirs(tmp, exist_ok=True)
    fd, path = tempfile.mkstemp(suffix=".elf" if gcc else ".s", dir=tmp)
    os.close(fd)
    try:
        if gcc:
            build_gcc(path, state, tests=False)
        else:
            build.build(path, query=state, tests=False, sets=sets)
        r = subprocess.run(
            [RIPES, "--mode", "cli", "--src", path, "-t",
             "elf" if gcc else "asm", "--proc", proc, "--iret", "--cycles"],
            capture_output=True, text=True, timeout=3600)
    finally:
        os.remove(path)
    # The program's console output is on stderr, with a NUL after each string.
    out = (r.stdout + r.stderr).replace("\x00", "")

    def num(pattern):
        m = re.search(pattern, out)
        return int(m.group(1)) if m else None

    return (num(r"instructions retired\s+(\d+)"), num(r"cycles\s+(\d+)"),
            num(r"exited with code: (-?\d+)"), num(r"\[(-?\d+)\]"), out)


def main(argv):
    if len(argv) >= 3 and argv[1] in ("iret", "gcc"):
        proc = argv[3] if len(argv) > 3 else "RV32_ISS"
        n, cycles, code, length, out = run(argv[2], proc,
                                           gcc=argv[1] == "gcc")
        print(out.strip())
        print("state %s proc %s iret %s cycles %s length %s exit %s"
              % (argv[2], proc, n, cycles, length, code))
        return 0 if code == 0 else 1
    if len(argv) >= 4 and argv[1] == "sweep":
        states = [l.strip() for l in open(argv[2]) if l.strip()]
        want = int(argv[4]) if len(argv) > 4 else None
        rows = []
        with open(argv[3], "w") as f:
            f.write("state,iret,length,exit\n")
            for i, s in enumerate(states):
                n, _, code, length, _ = run(s)
                ok = code == 0 and n is not None and (want is None
                                                      or length == want)
                rows.append((n or 0, s, ok))
                f.write("%s,%s,%s,%s\n" % (s, n, length, code))
                f.flush()
                if (i + 1) % 100 == 0:
                    print("%d/%d, worst so far %d" % (
                        i + 1, len(states), max(r[0] for r in rows)),
                        flush=True)
        bad = sum(1 for r in rows if not r[2])
        rows.sort(reverse=True)
        print("%d states, %d failures; worst %d (%s), mean %.0f, best %d (%s)"
              % (len(rows), bad, rows[0][0], rows[0][1],
                 sum(r[0] for r in rows) / len(rows), rows[-1][0],
                 rows[-1][1]))
        return 1 if bad else 0
    raise SystemExit(__doc__)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
