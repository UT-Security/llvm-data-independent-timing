#!/usr/bin/env python3
"""taint_obligations.py <info-loss-report> [--owned <file> | <object|archive>...]
                        [--next-round <out.txt> [--seeds <in.txt>]]

The callee contract's obligation list, split by ownership, from one build's
-taint-info-loss-report and the symbols the build defines (either the file
utils/taint_owned_symbols.sh writes, or the objects themselves, via llvm-nm).

  OWNED     callees this build defines but has not seeded: the seed lines to
            paste. --next-round writes <in seeds> + these lines, deduplicated,
            which is the next build's seed file. Includes the frame-address
            records (`taint-stop memory`, UNSOUND): a secret staged on the
            caller's frame and passed by address is not a secret-passing call
            in the analysis's view, so the callee is never an `uncovered`
            record, and the only way it gets covered is its own seed.
  INDIRECT  call sites through a pointer: seed the targets by name.
  EXTERNAL  callees not defined by this build (libc, other libraries): out of
            scope; listed by class so the count is honest, not proposed.

Records the pass already filed as `external-call` (a build run with
-taint-owned-symbols) are external regardless of the object list."""
import os, re, subprocess, sys

args = sys.argv[1:]
if not args or args[0].startswith("-"):
    print(__doc__); sys.exit(1)
report = args.pop(0)
owned_file = None; next_round = None; seeds_in = None; objs = []
while args:
    a = args.pop(0)
    if a == "--owned": owned_file = args.pop(0)
    elif a == "--next-round": next_round = args.pop(0)
    elif a == "--seeds": seeds_in = args.pop(0)
    else: objs.append(a)

owned = set()
if owned_file:
    owned = {l.strip() for l in open(owned_file) if l.strip() and not l.startswith("#")}
elif objs:
    nm = os.path.join(os.environ["LLVM_BUILD"], "bin", "llvm-nm") if "LLVM_BUILD" in os.environ else "llvm-nm"
    out = subprocess.run([nm, "--defined-only", "--no-demangle", *objs], capture_output=True, text=True).stdout
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3 and p[1] in "tTwW": owned.add(p[2])
else:
    print("need --owned <file> or the build's objects", file=sys.stderr); sys.exit(1)

MOVERS = {"memcpy", "memmove", "mempcpy", "memset"}
ALLOC = {"malloc", "calloc", "realloc", "free", "aligned_alloc", "posix_memalign"}
rec = re.compile(r"^\[\d+\] taint-stop (\S+)\s+in=(\S+)(?: src=(\S+))? callee=(\S+)")
seedline = re.compile(r"^\s+([A-Za-z_0-9.$]+,\d+(?:,pointee)?)\s*$")
memseed = re.compile(r"i\.e\. `([A-Za-z_0-9.$]+,\d+(?:,pointee)?)`")
records = []; cur = None
for line in open(report, errors="replace"):
    m = rec.match(line)
    if m:
        cur = {"kind": m.group(1), "in": m.group(2), "src": m.group(3) or "", "callee": m.group(4), "seeds": []}
        records.append(cur); continue
    if cur:
        m = seedline.match(line)
        if m: cur["seeds"].append(m.group(1))
        m = memseed.search(line)
        if m and cur["kind"] == "memory": cur["seeds"].append(m.group(1))

owned_seeds = {}; indirect = {}; external = {}
for r in records:
    k, c = r["kind"], r["callee"]
    if k == "uncovered-indirect":
        indirect.setdefault(f'{r["src"]} {r["in"]}', 0); indirect[f'{r["src"]} {r["in"]}'] += 1
    elif k in ("uncovered-callee", "memory"):
        if c == "<indirect>": continue
        if c in owned:
            for sl in r["seeds"]: owned_seeds.setdefault(sl, set()).add(f'{r["src"]}:{r["in"]}')
        else:
            external.setdefault(c, 0); external[c] += 1
    elif k == "external-call":
        external.setdefault(c, 0); external[c] += 1

print(f"OWNED: {len(owned_seeds)} seed line(s) for callees this build defines")
for sl in sorted(owned_seeds):
    print(f"  {sl:60s} <- {', '.join(sorted(owned_seeds[sl]))}")
print(f"INDIRECT: {sum(indirect.values())} site(s) - seed the pointer's targets by name")
for k in sorted(indirect): print(f"  {k}")
print(f"EXTERNAL (out of scope): {sum(external.values())} site(s), {len(external)} callee(s)")
def cls(c): return "mover" if c in MOVERS else "allocator" if c in ALLOC else "other"
for c in sorted(external, key=lambda x: (cls(x), x)):
    print(f"  {cls(c):10s} {c} x{external[c]}")

if next_round:
    have = []
    if seeds_in:
        have = [l.rstrip("\n") for l in open(seeds_in)]
    present = {l.strip() for l in have if l.strip() and not l.strip().startswith("#")}
    new = [sl for sl in sorted(owned_seeds) if sl not in present]
    with open(next_round, "w") as f:
        for l in have: f.write(l + "\n")
        if new:
            f.write(f"# --- next round: {len(new)} owned obligation(s) from {os.path.basename(report)} ---\n")
            for sl in new: f.write(sl + "\n")
    print(f"wrote {next_round}: {len(present)} existing + {len(new)} new seed line(s)")
