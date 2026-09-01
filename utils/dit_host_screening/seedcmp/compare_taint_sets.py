#!/usr/bin/env python3
"""Compare our tainted-source-line set against CryptoMPK's shipped taint report.

Both analyses emit (source file, line). Theirs is `path,line` with an absolute
build path; ours is `<taint-output>_src` holding `path:line<TAB>function`.
Normalise both to repository-relative `file:line` and diff the sets.

Usage: compare_taint_sets.py THEIRS OURS [--strip PREFIX_MARKER]
"""
import argparse, collections, os, re, sys

def norm(path, marker):
    """Reduce a build-absolute path to one comparable across the two builds.

    Both analyses record the path their own build saw, and the two builds live
    in different trees (theirs `/home/jxc/lab/.../libhydrogen/./impl/x25519.h`,
    ours `libhydrogen_cryptompk/impl/x25519.h`). Keep the tail starting at the
    LAST occurrence of `marker`, which is the shared source root."""
    p = path.replace("\\", "/")
    i = p.rfind(marker)
    if i >= 0:
        p = p[i:]
    return p

def read_theirs(fn, marker):
    s = set()
    for line in open(fn):
        line = line.strip()
        if not line or "," not in line:
            continue
        path, _, ln = line.rpartition(",")
        if not ln.isdigit():
            continue
        s.add((norm(path, marker), int(ln)))
    return s

def read_ours(fn, marker):
    s, fnmap = set(), {}
    for line in open(fn):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        loc, _, func = line.partition("\t")
        path, _, ln = loc.rpartition(":")
        if not ln.isdigit():
            continue
        key = (norm(path, marker), int(ln))
        s.add(key)
        fnmap.setdefault(key, set()).add(func.strip())
    return s, fnmap

def by_file(s):
    c = collections.Counter(f for f, _ in s)
    return c

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("theirs"); ap.add_argument("ours")
    ap.add_argument("--marker", default="impl/",
                    help="substring after which the path becomes repo-relative")
    ap.add_argument("--only", default=None,
                    help="restrict both sets to files matching this prefix")
    ap.add_argument("--csv", default=None)
    a = ap.parse_args()

    T = read_theirs(a.theirs, a.marker)
    O, fnmap = read_ours(a.ours, a.marker)
    if a.only:
        T = {x for x in T if x[0].startswith(a.only)}
        O = {x for x in O if x[0].startswith(a.only)}

    both, only_t, only_o = T & O, T - O, O - T
    files = sorted({f for f, _ in T | O})

    print(f"CryptoMPK   {len(T):5d} distinct (file,line)")
    print(f"ours        {len(O):5d}")
    print(f"agree       {len(both):5d}   "
          f"({100*len(both)/len(T):.1f}% of theirs, {100*len(both)/max(len(O),1):.1f}% of ours)")
    print(f"theirs only {len(only_t):5d}")
    print(f"ours only   {len(only_o):5d}")
    print()
    bt, bo, bb = by_file(T), by_file(O), by_file(both)
    print(f"{'file':34s} {'theirs':>7s} {'ours':>7s} {'agree':>7s} {'theirs only':>12s} {'ours only':>10s}")
    for f in files:
        print(f"{f:34s} {bt[f]:7d} {bo[f]:7d} {bb[f]:7d} "
              f"{bt[f]-bb[f]:12d} {bo[f]-bb[f]:10d}")

    if a.csv:
        with open(a.csv, "w") as fh:
            fh.write("file,cryptompk_lines,our_lines,agree,cryptompk_only,ours_only\n")
            for f in files:
                fh.write(f"{f},{bt[f]},{bo[f]},{bb[f]},{bt[f]-bb[f]},{bo[f]-bb[f]}\n")
            fh.write(f"TOTAL,{len(T)},{len(O)},{len(both)},{len(only_t)},{len(only_o)}\n")

    print("\n--- lines only CryptoMPK marks (we miss or correctly exclude) ---")
    for f, l in sorted(only_t)[:40]:
        print(f"  {f}:{l}")
    if len(only_t) > 40:
        print(f"  ... {len(only_t)-40} more")

if __name__ == "__main__":
    main()
