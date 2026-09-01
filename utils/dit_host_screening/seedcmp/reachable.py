#!/usr/bin/env python3
"""Functions reachable from a set of entry points, via .o relocations.

CryptoMPK analyse whole-program from a driver; we analyse the whole
translation unit. Their set can only contain functions their drivers reach, so
comparing raw totals conflates a scope difference with a precision difference.
This computes the reachable set so both can be restricted to it."""
import re, subprocess, sys, collections

def callgraph(obj, objdump):
    txt = subprocess.run([objdump, "-dr", obj], capture_output=True, text=True).stdout
    g, cur = collections.defaultdict(set), None
    for l in txt.split("\n"):
        m = re.match(r'^[0-9a-f]+ <([^>]+)>:', l)
        if m:
            cur = m.group(1); g.setdefault(cur, set()); continue
        m = re.search(r'ARM64_RELOC_BRANCH26\s+(\S+)', l)
        if m and cur:
            g[cur].add(m.group(1))
    return g

def reach(g, roots):
    seen, work = set(), list(roots)
    while work:
        f = work.pop()
        if f in seen: continue
        seen.add(f)
        work.extend(g.get(f, ()))
    return seen

if __name__ == "__main__":
    obj, objdump = sys.argv[1], sys.argv[2]
    roots = sys.argv[3:]
    g = callgraph(obj, objdump)
    for f in sorted(reach(g, roots)):
        print(f.lstrip("_"))
