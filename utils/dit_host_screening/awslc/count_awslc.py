#!/usr/bin/env python3
"""Experiment 14 census: which rows of `bssl speed` enter AWS-LC's DIT bracket, and how often per call.

Runs the `ditcount` build (build_awslc.sh count) over EVERY row of the suite, single-threaded, short
windows: this is a count, not a timing. Each JSON row carries "ditEntries" over its timed loop, so
entries per call = ditEntries / numCalls, an integer for a deterministic path. Writes

  dit_census.json   every row: description, family, size, numCalls, ditEntries, entries_per_call
  dit_census.md     the families (rows grouped by description without the size), entries per call
                    across sizes, the ones that never enter the bracket listed separately, and the
                    source-level census: which source files carry SET_DIT_AUTO_RESET

usage: count_awslc.py [--out DIR] [--timeout-ms 20] [--tree ~/Documents/dit-awslc]
"""
import json, os, re, sys, argparse, subprocess, collections, math

ap = argparse.ArgumentParser()
ap.add_argument('--tree', default=os.path.expanduser('~/Documents/dit-awslc'))
ap.add_argument('--out', default=None, help='directory for dit_census.json/.md (default: <tree>/results)')
ap.add_argument('--timeout-ms', default='20')
ap.add_argument('--json-in', default=None, help='parse this speed JSON instead of running the tool')
a = ap.parse_args()
out = a.out or os.path.join(a.tree, 'results'); os.makedirs(out, exist_ok=True)
bssl = os.path.join(a.tree, 'build-ditcount', 'tool', 'bssl')

if a.json_in:
    txt = open(a.json_in).read()
else:
    if not os.access(bssl, os.X_OK): sys.exit(f'no census build at {bssl}: run build_awslc.sh count')
    # -threads 1: the refcount rows spawn threads otherwise, and the counter is a plain global
    cmd = [bssl, 'speed', '-json', '-timeout_ms', a.timeout_ms, '-threads', '1']
    sys.stderr.write('running ' + ' '.join(cmd) + '\n')
    p = subprocess.run(cmd, capture_output=True, text=True)
    txt = p.stdout
    if p.returncode: sys.stderr.write(f'bssl exit {p.returncode}: {p.stderr[-800:]}\n')
    open(os.path.join(out, 'dit_census_raw.json'), 'w').write(txt)
rows = json.loads(txt) if txt.strip().startswith('[') else [json.loads(l) for l in txt.splitlines() if l.strip().startswith('{')]
if not rows: sys.exit('no rows parsed')
missing = [r['description'] for r in rows if 'ditEntries' not in r]
if missing: sys.exit(f'{len(missing)} rows without ditEntries; the tool is not the census build')

def family(desc): return re.sub(r' \[.*\]$', '', desc)
def size(r):
    for k in ('bytesPerCall', 'primeSizePerCall'):
        if k in r: return r[k]
    return None
recs = []
for r in rows:
    n = r['numCalls']; e = r['ditEntries']
    recs.append(dict(description=r['description'], family=family(r['description']), size=size(r), numCalls=n, ditEntries=e,
                     entries_per_call=(e / n) if n else float('nan')))

# source-level census: which files carry the macro, and how many times
src = None
for cand in (os.path.join(a.tree, 'src', 'aws-lc-5.8.0'), os.path.join(a.tree, 'tree-rel')):
    if os.path.isdir(cand): src = cand; break
sites = collections.Counter()
if src:
    for dp, _, fs in os.walk(os.path.join(src, 'crypto')):
        for f in fs:
            if f.endswith(('.c', '.h')):
                t = open(os.path.join(dp, f), errors='replace').read()
                c = t.count('SET_DIT_AUTO_RESET')
                if c and not f.endswith('internal.h'): sites[os.path.relpath(os.path.join(dp, f), src)] += c

fams = collections.OrderedDict()
for x in recs: fams.setdefault(x['family'], []).append(x)
def fmt(v): return f"{v:,.2f}".rstrip('0').rstrip('.') if v == v else 'n/a'
using = [(f, xs) for f, xs in fams.items() if any(x['ditEntries'] for x in xs)]
never = [(f, xs) for f, xs in fams.items() if not any(x['ditEntries'] for x in xs)]
nrows_using = sum(len(xs) for _, xs in using)

L = [f"# Which `bssl speed` rows enter AWS-LC's DIT bracket\n",
     f"Census build (`ditcount`: the shipped bracket with a counter in `armv8_set_dit`), every row of the suite at "
     f"{a.timeout_ms} ms, one thread. {len(recs)} rows in {len(fams)} families; **{nrows_using} rows in {len(using)} families enter the "
     f"bracket at least once per timed loop, {len(recs) - nrows_using} rows in {len(never)} families never do**. Entries per call is "
     f"ditEntries / numCalls; a range means it changes with the input size.\n",
     "## Families that enter the bracket\n", "| family | rows | bracket entries per call | sizes |", "|---|---|---|---|"]
for f, xs in sorted(using, key=lambda fx: -max(x['entries_per_call'] for x in fx[1])):
    epc = [x['entries_per_call'] for x in xs]; lo, hi = min(epc), max(epc)
    sizes = ', '.join(str(x['size']) for x in xs if x['size'] is not None) or '-'
    L.append(f"| {f} | {len(xs)} | {fmt(lo) if lo == hi else fmt(lo) + ' to ' + fmt(hi)} | {sizes} |")
L += ["", "## Families that never enter the bracket\n",
      "These rows run the same instructions in every arm of the experiment; any difference measured on them is noise or a layout effect.\n",
      "| family | rows |", "|---|---|"]
for f, xs in never: L.append(f"| {f} | {len(xs)} |")
if sites:
    L += ["", "## Where the macro is in the source\n",
          f"`SET_DIT_AUTO_RESET` appears {sum(sites.values())} times in {len(sites)} files (the bracket is one macro at the top of each public entry point).\n",
          "| file | sites |", "|---|---|"]
    for f, c in sorted(sites.items(), key=lambda fc: (-fc[1], fc[0])): L.append(f"| `{f}` | {c} |")
open(os.path.join(out, 'dit_census.md'), 'w').write('\n'.join(L) + '\n')
json.dump(dict(rows=recs, source_sites=sites, timeout_ms=a.timeout_ms, threads=1,
               summary=dict(rows=len(recs), families=len(fams), rows_entering=nrows_using, families_entering=len(using),
                            families_never=[f for f, _ in never])), open(os.path.join(out, 'dit_census.json'), 'w'), indent=1)
print(f"{len(recs)} rows, {len(fams)} families; {nrows_using} rows / {len(using)} families enter the bracket; "
      f"{len(recs) - nrows_using} rows / {len(never)} families never do. wrote {out}/dit_census.md, dit_census.json")
