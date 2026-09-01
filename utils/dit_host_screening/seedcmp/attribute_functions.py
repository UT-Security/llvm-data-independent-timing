#!/usr/bin/env python3
"""Map (file,line) taint locations onto enclosing C functions.

Line-level agreement between the two analyses is noisy for a reason that has
nothing to do with either being wrong: CryptoMPK analyses -O0 LLVM IR, we
analyse -O2 post-register-allocation MIR, so the same computation is attributed
to different lines. The enclosing FUNCTION is stable across that difference and
is the unit to compare.

libhydrogen's style puts the return type on its own line and the signature at
column 0, with `{` and `}` at column 0 delimiting the body.
"""
import argparse, collections, os, re, sys

SIG = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)\s*\(')

def functions(path):
    """-> list of (name, start_line, end_line), 1-indexed inclusive."""
    lines = open(path, errors="replace").read().split("\n")
    out, i, n = [], 0, len(lines)
    while i < n:
        if lines[i] == "{":
            # walk back over a possibly multi-line signature to the name
            j, name = i - 1, None
            while j >= 0 and i - j < 8:
                m = SIG.match(lines[j])
                if m:
                    name = m.group(1)
                    break
                j -= 1
            k = i + 1
            while k < n and lines[k] != "}":
                k += 1
            if name:
                out.append((name, j + 1, k + 1))
            i = k + 1
        else:
            i += 1
    return out

def build_index(root):
    idx = {}
    for dirpath, _, files in os.walk(root):
        for f in files:
            if not f.endswith((".h", ".c")):
                continue
            full = os.path.join(dirpath, f)
            rel = os.path.relpath(full, root)
            idx[rel.replace(os.sep, "/")] = functions(full)
    return idx

def lookup(idx, rel, line):
    for name, a, b in idx.get(rel, []):
        if a <= line <= b:
            return name
    return None

def norm(p, marker="impl/"):
    p = p.replace("\\", "/")
    i = p.rfind(marker)
    return p[i:] if i >= 0 else p

def read_theirs(fn):
    s = set()
    for l in open(fn):
        l = l.strip()
        if "," not in l:
            continue
        path, _, ln = l.rpartition(",")
        if ln.isdigit():
            s.add((norm(path), int(ln)))
    return s

def read_ours(fn):
    s = set()
    for l in open(fn):
        l = l.rstrip("\n")
        if not l or l.startswith("#"):
            continue
        loc = l.split("\t")[0]
        path, _, ln = loc.rpartition(":")
        if ln.isdigit():
            s.add((norm(path), int(ln)))
    return s

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root"); ap.add_argument("theirs"); ap.add_argument("ours")
    ap.add_argument("--csv")
    a = ap.parse_args()
    idx = build_index(a.root)
    def fns(pairs):
        c = collections.Counter()
        unmapped = 0
        for rel, ln in pairs:
            f = lookup(idx, rel, ln)
            if f: c[f] += 1
            else: unmapped += 1
        return c, unmapped
    T, tu = fns(read_theirs(a.theirs))
    O, ou = fns(read_ours(a.ours))
    both = set(T) & set(O)
    print(f"CryptoMPK  {len(T):3d} functions ({sum(T.values())} locations, {tu} unmapped)")
    print(f"ours       {len(O):3d} functions ({sum(O.values())} locations, {ou} unmapped)")
    print(f"agree      {len(both):3d}   "
          f"({100*len(both)/max(len(T),1):.0f}% of theirs, {100*len(both)/max(len(O),1):.0f}% of ours)")
    print()
    print(f"{'function':32s} {'theirs':>7s} {'ours':>6s}  verdict")
    for f in sorted(set(T) | set(O)):
        v = "both" if f in both else ("CryptoMPK only" if f in T else "ours only")
        print(f"{f:32s} {T.get(f,0):7d} {O.get(f,0):6d}  {v}")
    if a.csv:
        with open(a.csv, "w") as fh:
            fh.write("function,cryptompk_locations,our_locations,verdict\n")
            for f in sorted(set(T) | set(O)):
                v = "both" if f in both else ("cryptompk_only" if f in T else "ours_only")
                fh.write(f"{f},{T.get(f,0)},{O.get(f,0)},{v}\n")

if __name__ == "__main__":
    main()
