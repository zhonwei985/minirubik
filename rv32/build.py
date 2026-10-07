#!/usr/bin/env python3
"""Turns rv32/minirubik.S into single-file programs the Ripes assembler accepts.

Ripes (v2.2.6-106-g5b8a616) has no .include, .if, .macro or numeric local
labels, so this script does what GNU as would do with them and nothing else:

  .include "f"          inline rv32/f
  .equ NAME, value      recorded, so .if NAME can be decided; kept in output
  .if / .else / .endif  keep or drop the enclosed lines
  .macro / .endm        expand, substituting \\arg and \\@ (invocation count)
  1: / 1f / 1b          renamed to unique labels

Usage:
  build.py                    rv32/ripes_cli.s and rv32/ripes_gui.s
  build.py VARIANT OUT [--query STATE] [--no-tests] [--set NAME=VALUE ...]
    VARIANT is cli, gui or dump (renderer self-test in the CLI).
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(HERE, "minirubik.S")


def inline(path):
    out = []
    for line in open(path, encoding="utf-8"):
        m = re.match(r'\s*\.include\s+"([^"]+)"', line)
        if m:
            out.extend(inline(os.path.join(HERE, m.group(1))))
        else:
            out.append(line.rstrip("\n"))
    return out


def conditionals(lines, sets):
    values, out, stack = {}, [], []
    for line in lines:
        code = line.split("#", 1)[0].strip()
        m = re.match(r"\.equ\s+(\w+)\s*,\s*(\S+)", code)
        if m and all(stack):
            name, value = m.group(1), m.group(2)
            if name in sets:
                value = sets[name]
                line = re.sub(r"(\.equ\s+\w+\s*,\s*)\S+", r"\g<1>" + value, line)
            values[name] = int(value, 0)
        m = re.match(r"\.if\s+(\w+)$", code)
        if m:
            name = m.group(1)
            v = int(name) if name.isdigit() else values[name]
            stack.append(bool(v))
            continue
        if code == ".else":
            stack[-1] = not stack[-1]
            continue
        if code == ".endif":
            stack.pop()
            continue
        if all(stack):
            out.append(line)
    assert not stack, "unterminated .if"
    return out


def macros(lines):
    defs, counter = {}, [0]

    def expand(seq):
        out, i = [], 0
        while i < len(seq):
            code = seq[i].split("#", 1)[0].strip()
            m = re.match(r"\.macro\s+(\w+)\s*(.*)$", code)
            if m:
                params = [p.strip() for p in m.group(2).split(",") if p.strip()]
                body, i = [], i + 1
                while seq[i].split("#", 1)[0].strip() != ".endm":
                    body.append(seq[i])
                    i += 1
                defs[m.group(1)] = (params, body)
                i += 1
                continue
            m = re.match(r"(\w+)\s*(.*)$", code)
            if m and m.group(1) in defs:
                params, body = defs[m.group(1)]
                args = [a.strip() for a in m.group(2).split(",")] if m.group(2) else []
                n = str(counter[0])  # \@ counts invocations, as in GNU as
                counter[0] += 1
                text = []
                for b in body:
                    for p, a in zip(params, args):
                        b = b.replace("\\" + p, a)
                    text.append(b.replace("\\@", n))
                out.append("    # " + code)
                out.extend(expand(text))  # macros may invoke macros
            else:
                out.append(seq[i])
            i += 1
        return out

    return expand(lines)


def local_labels(lines):
    """Rename N: definitions to L<N>_<k> and resolve Nf / Nb references."""
    defs = []  # (line index, digit, new name)
    for idx, line in enumerate(lines):
        m = re.match(r"\s*(\d+):", line)
        if m:
            defs.append((idx, m.group(1), "L%s_%d" % (m.group(1), len(defs))))
    out = []
    for idx, line in enumerate(lines):
        code, sep, comment = line.partition("#")
        m = re.match(r"(\s*)(\d+):(.*)$", code)
        if m:
            name = [d[2] for d in defs if d[0] == idx][0]
            code = m.group(1) + name + ":" + m.group(3)

        def ref(mm):
            digit, direction = mm.group(1), mm.group(2)
            if direction == "f":
                target = [d for d in defs if d[1] == digit and d[0] > idx][0]
            else:
                target = [d for d in defs if d[1] == digit and d[0] <= idx][-1]
            return target[2]

        code = re.sub(r"\b(\d+)([fb])\b", ref, code)
        out.append(code + sep + comment)
    return out


def build(variant, out_path, query=None, tests=True, sets=None):
    sets = dict(sets or {})
    sets.setdefault("RENDER", "0" if variant == "cli" else "1")
    sets.setdefault("DUMP", "1" if variant == "dump" else "0")
    lines = inline(SOURCE)
    if variant == "dump":
        # The CLI has no LED matrix: give the renderer plain memory instead.
        lines = [".equ LED_MATRIX_0_BASE, 0x10100000",
                 ".equ LED_MATRIX_0_WIDTH, 35",
                 ".equ LED_MATRIX_0_HEIGHT, 25"] + lines
    lines = conditionals(lines, sets)
    lines = macros(lines)
    lines = local_labels(lines)
    if query:
        lines = [re.sub(r'^query:\s*\.string\s*"[^"]*"', 'query:  .string "%s"' % query, l)
                 for l in lines]
    if not tests:
        start = next(i for i, l in enumerate(lines) if l.startswith("tests:"))
        end = next(i for i, l in enumerate(lines) if l.startswith("tests_end:"))
        lines = lines[: start + 1] + lines[end:]
    header = ("# GENERATED by rv32/build.py from rv32/minirubik.S (%s build). "
              "Edit the source, not this file.\n" % variant)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(header + "\n".join(lines) + "\n")


def main(argv):
    if len(argv) == 1:
        build("cli", os.path.join(HERE, "ripes_cli.s"))
        build("gui", os.path.join(HERE, "ripes_gui.s"))
        return 0
    variant, out = argv[1], argv[2]
    query, tests, sets, i = None, True, {}, 3
    while i < len(argv):
        if argv[i] == "--query":
            query, i = argv[i + 1], i + 2
        elif argv[i] == "--no-tests":
            tests, i = False, i + 1
        elif argv[i] == "--set":
            k, v = argv[i + 1].split("=")
            sets[k], i = v, i + 2
        else:
            raise SystemExit("unknown option " + argv[i])
    build(variant, out, query, tests, sets)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
