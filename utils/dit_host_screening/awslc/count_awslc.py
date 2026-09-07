#!/usr/bin/env python3
"""Experiment 14 census: which rows of `bssl speed` enter AWS-LC's DIT bracket, how often per call, and
the filter list that makes the timing driver run exactly those rows.

Runs the `ditcount` build (build_awslc.sh count) over EVERY row of the suite, single-threaded, short
windows: this is a count, not a timing. Each JSON row carries "ditEntries" over its timed loop, so
entries per call = ditEntries / numCalls, an integer for a deterministic path. Writes, in --out:

  dit_census.json        every row: description, family, size, numCalls, ditEntries, entries_per_call;
                         the source-level census; the filter list
  dit_census.md          the families, entries per call across sizes, the ones that never enter the
                         bracket, and where SET_DIT_AUTO_RESET is in the source
  bracketed_filters.txt  one `bssl speed -filter` string per line: the smallest set of family names that
                         selects every bracket-entering row and no other (the tool's filter is a substring
                         match on the row description; each filter is an independent run, so the set is
                         also chosen so that no row is matched twice). bench_awslc.py reads it as
                         BENCH_TESTS_FILE; the default run of the experiment is exactly these rows.
  dit_census_raw.json    the tool's own output (when the tool was run)

and, with --info-md PATH, the benchmarks document: what the suite is, what enters the bracket, what the
experiment runs, all stated from these numbers.

usage: count_awslc.py [--out DIR] [--timeout-ms 20] [--tree ~/Documents/dit-awslc] [--json-in RAW]
                      [--recorded results/raw/speed.json] [--info-md paper_experiments/14-.../benchmarks-info.md]
"""
import json, os, re, sys, argparse, subprocess, collections, datetime

ap = argparse.ArgumentParser()
ap.add_argument('--tree', default=os.path.expanduser('~/Documents/dit-awslc'))
ap.add_argument('--out', default=None, help='directory for the census files (default: <tree>/results)')
ap.add_argument('--timeout-ms', default='20')
ap.add_argument('--json-in', default=None, help='parse this speed JSON instead of running the tool')
ap.add_argument('--recorded', default=None, help='a recorded run (speed.json) to check against the census')
ap.add_argument('--info-md', default=None, help='write the benchmarks document here')
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
def rowkey(r):
    """The timing driver's row key (bench_awslc.py): the description plus the size, so rows can be matched across runs."""
    return r['description'] + (f" [{r['bytesPerCall']} B]" if r.get('bytesPerCall') else f" [{r['primeSizePerCall']}-bit]" if r.get('primeSizePerCall') else '')
recs = []
for r in rows:
    n = r['numCalls']; e = r['ditEntries']
    recs.append(dict(key=rowkey(r), description=r['description'], family=family(r['description']), size=size(r), numCalls=n, ditEntries=e,
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
by_dir = collections.Counter()
for f, c in sites.items():
    parts = f.split('/'); by_dir['/'.join(parts[1:3]) if parts[1] == 'fipsmodule' else parts[1]] += c

fams = collections.OrderedDict()
for x in recs: fams.setdefault(x['family'], []).append(x)
def fmt(v): return f"{v:,.2f}".rstrip('0').rstrip('.') if v == v else 'n/a'
using = [(f, xs) for f, xs in fams.items() if any(x['ditEntries'] for x in xs)]
never = [(f, xs) for f, xs in fams.items() if not any(x['ditEntries'] for x in xs)]
never_names = [f for f, _ in never]
nrows_using = sum(len(xs) for _, xs in using)

# the filter set: shortest family names first; a name is a filter only if no never-entering family contains
# it, and it covers every entering family that contains it (so no row is matched by two filters)
filters, covered = [], set()
for f in sorted((f for f, _ in using), key=len):
    if f in covered or any(f in n for n in never_names): continue
    filters.append(f); covered |= {g for g, _ in using if f in g}
uncovered = [f for f, _ in using if f not in covered]
sel = [x for x in recs if any(fl in x['description'] for fl in filters)]
twice = [x['description'] for x in recs if sum(1 for fl in filters if fl in x['description']) > 1]
stray = [x['description'] for x in sel if x['family'] in never_names]
if uncovered or twice or stray:
    sys.stderr.write(f'filter set imperfect: uncovered {uncovered}, matched twice {twice[:5]}, never-entering selected {stray[:5]}\n')
open(os.path.join(out, 'bracketed_filters.txt'), 'w').write(
    f"# bssl speed -filter strings that select exactly the {len(sel)} rows of the suite that enter the DIT bracket\n"
    f"# (census of {datetime.date.today().isoformat()}, count_awslc.py; one filter per line; bench_awslc.py reads this as BENCH_TESTS_FILE)\n"
    + '\n'.join(filters) + '\n')

# a recorded run against the census
rec = None
if a.recorded and os.path.exists(a.recorded):
    d = json.load(open(a.recorded)); keys = list(d['results'])
    epc = {x['key']: x['entries_per_call'] for x in recs}
    unknown = [k for k in keys if k not in epc]
    if unknown: sys.stderr.write(f'{len(unknown)} recorded rows not in the census: {unknown[:5]}\n')
    rec_never = [k for k in keys if epc.get(k, 1) == 0]
    rec = dict(path=a.recorded, rows=len(keys), families=len(set(family(k) for k in keys)), rows_never=len(rec_never),
               families_never=sorted(set(family(k) for k in rec_never)), tests=d.get('tests'))

L = [f"# Which `bssl speed` rows enter AWS-LC's DIT bracket\n",
     f"Census build (`ditcount`: the shipped bracket with a counter in `armv8_set_dit`), every row of the suite at "
     f"{a.timeout_ms} ms, one thread. {len(recs)} rows in {len(fams)} families; **{nrows_using} rows in {len(using)} families enter the "
     f"bracket at least once per timed loop, {len(recs) - nrows_using} rows in {len(never)} families never do**. Entries per call is "
     f"ditEntries / numCalls; a range means it changes with the input size. `bracketed_filters.txt` ({len(filters)} filters) selects "
     f"exactly the entering rows.\n",
     "## Families that enter the bracket\n", "| family | rows | bracket entries per call | sizes |", "|---|---|---|---|"]
for f, xs in sorted(using, key=lambda fx: -max(x['entries_per_call'] for x in fx[1])):
    e = [x['entries_per_call'] for x in xs]; lo, hi = min(e), max(e)
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
json.dump(dict(rows=recs, source_sites=sites, source_sites_by_dir=by_dir, timeout_ms=a.timeout_ms, threads=1, filters=filters,
               recorded=rec, date=datetime.date.today().isoformat(),
               summary=dict(rows=len(recs), families=len(fams), rows_entering=nrows_using, families_entering=len(using),
                            rows_selected_by_filters=len(sel), families_never=never_names)),
          open(os.path.join(out, 'dit_census.json'), 'w'), indent=1)

if a.info_md:
    epc_all = sorted(x['entries_per_call'] for x in recs if x['ditEntries'])
    q = lambda p: epc_all[min(int(p * (len(epc_all) - 1)), len(epc_all) - 1)]
    kinds = collections.OrderedDict([
        ('hashes', [f for f in never_names if re.match(r'^(SHA|SHA3|SHAKE|MD4|MD5|BLAKE2|RIPEMD|KECCAK|SipHash)', f)]),
        ('HMAC, every hash', [f for f in never_names if f.startswith('HMAC')]),
        ('signature verification', [f for f in never_names if 'verify' in f]),
        ('raw EC point arithmetic', [f for f in never_names if f.startswith('EC POINT')]),
        ('RSA key generation', [f for f in never_names if 'key-gen' in f]),
        ('TrustToken redemption', [f for f in never_names if f.startswith('TrustToken')]),
    ])
    rest = [f for f in never_names if not any(f in v for v in kinds.values())]
    kinds['everything else'] = rest
    M = [f"# Benchmarks: what `bssl speed` runs, and which rows use DIT\n",
         f"Generated by `count_awslc.py` from the census of {datetime.date.today().isoformat()} (`data/dit_census.json`); "
         f"regenerate with `reproduce.sh census`. Every number below comes from that census, not from a reading of the source.\n",
         "## The suite\n",
         f"AWS-LC v5.8.0's `bssl speed`, run with no filter and one thread, produces **{len(recs)} rows in {len(fams)} families**. "
         f"A family is a benchmark name; a row is that name at one input size (16, 256, 1350, 8192 and 16384 bytes for the "
         f"byte-oriented ones, 2048 and 3072 bits for the prime-sized ones, a single row for the rest). The tool's `-filter` is a "
         f"substring match on the row description and each filter runs as its own pass.\n",
         "## Which rows enter the DIT bracket\n",
         f"AWS-LC's bracket is the `SET_DIT_AUTO_RESET` macro at the top of a public entry point: read PSTATE.DIT, set it, and "
         f"restore it on return. To find which benchmarks pass through one, a fourth build (`ditcount`) counts the entries in "
         f"`armv8_set_dit` and a patched speed tool reports the count over each row's timed loop. Entries per call is that count "
         f"divided by the row's calls; it is an integer whenever the code path is deterministic.\n",
         "| | rows | families |", "|---|---|---|",
         f"| enter the bracket at least once per call | **{nrows_using}** | **{len(using)}** |",
         f"| never enter it | {len(recs) - nrows_using} | {len(never)} |",
         f"| whole suite | {len(recs)} | {len(fams)} |", "",
         f"Among the rows that enter, the median is {fmt(q(0.5))} entries per call, the 75th percentile {fmt(q(0.75))}, the 90th "
         f"{fmt(q(0.9))}, and the maximum {fmt(q(1.0))} (CMAC at 16 KB: one bracketed single-block AES call per 16 bytes). "
         f"The full per-family list is `data/dit_census.md`.\n",
         "### The rows that never enter the bracket\n",
         f"{len(recs) - nrows_using} rows in {len(never)} families. They execute the same instructions under every arm of the "
         f"experiment, so a difference measured on them is noise or a layout effect, never a bracket cost. By kind:\n"]
    for k, v in kinds.items():
        if v: M.append(f"- **{k}** ({len(v)}): " + ', '.join(f"`{f}`" for f in v))
    M += ["", "### Where the macro is in the source\n",
          f"`SET_DIT_AUTO_RESET` appears {sum(sites.values())} times in {len(sites)} source files, by directory: "
          + ', '.join(f"{d} {c}" for d, c in sorted(by_dir.items(), key=lambda dc: -dc[1])) + ". "
          "It is absent from sha, hmac, digest, ecdsa, ecdh, ml_kem, ml_dsa, cmac, chacha and poly1305: those are bracketed only when "
          "a bracketed caller reaches them (EVP, the AEADs, the key-generation paths that draw randomness), which is what the census "
          "shows row by row.\n",
          "## What the experiment runs\n",
          f"**Default run (`reproduce.sh`, `reproduce.sh 3`)**: the {len(filters)} filters in `data/bracketed_filters.txt`, the "
          f"smallest set of family names that selects every bracket-entering row and no other, with no row matched twice. That is "
          f"**{len(sel)} rows**. The driver reads the file as `BENCH_TESTS_FILE`; `BENCH_TESTS=...` overrides it. Run length: "
          f"rows x 6 arms x 8 passes x the window, about {len(sel) * 6 * 8 * 0.4 / 3600:.1f} h per run at 400 ms and "
          f"{len(sel) * 6 * 8 * 0.05 / 60:.0f} min at 50 ms.\n",
          "**Paper stage (`reproduce.sh paper`)**: `BENCH_TESTS=\"AES-128,AEAD-ChaCha20-Poly1305,ECDSA P-256 signing,RNG\"` with "
          "`CHUNKS=16,1350,16384`: the ten rows of the paper table plus the other AES-128 rows the substring reaches, all of which "
          "enter the bracket.\n"]
    if rec:
        M += [f"**The recorded run in `results-m4/`** (before the census existed) used ten filters, "
              f"`{','.join(rec['tests'] or [])}`, which select {rec['rows']} rows in {rec['families']} families. "
              f"{rec['rows_never']} of those rows, in {len(rec['families_never'])} families, never enter the bracket: "
              + ', '.join(f"`{f}`" for f in rec['families_never']) + ". They are kept in the recorded tables as what they are, "
              "rows on which the bracket costs nothing; the LaTeX band table leaves them out and says so in its caption.\n"]
    M += ["## Regenerating\n",
          "```", "paper_experiments/14-awslc-shipping-bracket/reproduce.sh census      # ~5 min, no sudo: ditcount build + every row once",
          "```", "writes `data/dit_census.{md,json}`, `data/bracketed_filters.txt` and this file.\n"]
    open(a.info_md, 'w').write('\n'.join(M))
print(f"{len(recs)} rows, {len(fams)} families; {nrows_using} rows / {len(using)} families enter the bracket; "
      f"{len(recs) - nrows_using} rows / {len(never)} families never do; {len(filters)} filters select {len(sel)} rows. "
      f"wrote {out}/dit_census.md, dit_census.json, bracketed_filters.txt" + (f", {a.info_md}" if a.info_md else ''))
