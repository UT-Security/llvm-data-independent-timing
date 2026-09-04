#!/usr/bin/env python3
"""Find taint seeds that applied NOWHERE in a build.

A seed is a parameter attribute stamped on a function DEFINITION, so a seed
line that names a function no translation unit defines - a typo, a static
helper that got inlined away, a C++ name that needed mangling, an index the
function does not have - applies to nothing, and the compiler used to say
nothing about it. Every TU now appends one record per seed line to the file
named by -mllvm -taint-seed-report=<file>:

    APPLIED  func=NAME arg=N kind=data|pointee|declassify src=TU
    DECLARED func=NAME arg=N kind=... src=TU      (only a declaration here)
    ABSENT   func=NAME arg=N kind=... src=TU      (not in this TU at all)

This script reads the seed file and that report and prints every seed line
with no APPLIED record across the whole build. Exit status 1 if any.

    utils/taint_seed_check.py seeds.txt seed_report.txt

The report APPENDS, so delete it before a build (a stale one from an earlier
build would show every seed as applied). Same rule as the info-loss report.
"""
import re
import sys
from collections import defaultdict


def parse_seeds(path):
    seeds = []
    with open(path) as f:
        for ln, line in enumerate(f, 1):
            s = line.split('#', 1)[0].strip()
            if not s:
                continue
            parts = [p.strip() for p in s.split(',')]
            if len(parts) not in (2, 3):
                print(f"{path}:{ln}: bad seed line: {line.rstrip()}",
                      file=sys.stderr)
                continue
            kind = parts[2] if len(parts) == 3 else 'data'
            seeds.append((parts[0], int(parts[1]), kind, ln))
    return seeds


def parse_report(path):
    status = defaultdict(set)   # (func, arg, kind) -> {statuses}
    tus = defaultdict(set)      # (func, arg, kind) -> {TUs where APPLIED}
    rx = re.compile(r'^(APPLIED|DECLARED|ABSENT)\s+func=(\S+)\s+arg=(\d+)'
                    r'\s+kind=(\S+)\s+src=(\S+)')
    with open(path) as f:
        for line in f:
            m = rx.match(line)
            if not m:
                continue
            st, fn, arg, kind, src = m.groups()
            key = (fn, int(arg), kind)
            status[key].add(st)
            if st == 'APPLIED':
                tus[key].add(src)
    return status, tus


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    seeds = parse_seeds(sys.argv[1])
    status, tus = parse_report(sys.argv[2])
    dead = []
    for fn, arg, kind, ln in seeds:
        key = (fn, arg, kind)
        st = status.get(key, set())
        if 'APPLIED' in st:
            continue
        if 'DECLARED' in st:
            why = 'declared but never defined in any seeded TU'
        elif 'ABSENT' in st:
            why = 'not present in any seeded TU'
        else:
            why = 'no record at all (was this TU built with the report flag?)'
        dead.append((ln, fn, arg, kind, why))
    live = len(seeds) - len(dead)
    print(f"seeds: {len(seeds)} total, {live} applied, {len(dead)} dead")
    for ln, fn, arg, kind, why in dead:
        spec = f"{fn},{arg}" + ("" if kind == 'data' else f",{kind}")
        print(f"  DEAD line {ln}: {spec}  - {why}")
    sys.exit(1 if dead else 0)


if __name__ == '__main__':
    main()
