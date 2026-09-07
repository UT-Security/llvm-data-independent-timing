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
  bracketed_filters.txt  one `bssl speed -filter` string per line, a set that selects every bracket-entering
                         row, as few other rows as the tool allows, and no row twice. The tool tests a filter
                         against a per-benchmark SELECTION name that is often shorter than the row description
                         ("AES-128" selects the block, EVP, AEAD and CMAC AES-128 rows; "ECDSA P-256" selects
                         signing AND verify; "ECDSA P-256 signing" selects nothing), so the selection names are
                         found by PROBING: every space-separated prefix of every entering family is run as a
                         filter at 1 ms and the rows it yields are recorded (filter_probe.json, cached). Rows
                         that never enter the bracket but cannot be separated from ones that do ride along and
                         are listed in the file's header as passengers. bench_awslc.py reads the file as
                         BENCH_TESTS_FILE; the default run of the experiment is these rows.
  filter_probe.json      the probe cache: filter string -> the row keys the tool produces for it
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
ap.add_argument('--reprobe', action='store_true', help='ignore the probe cache and run every candidate filter again')
ap.add_argument('--no-probe', action='store_true', help='do not run the tool to find selection names (uses cached probes only; the filter list may then be incomplete)')
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

# --- the filter set, from what the tool actually selects ---
# Candidates: every space-separated prefix of every entering family ("AEAD-AES-128-GCM seal init" gives itself,
# "AEAD-AES-128-GCM seal", "AEAD-AES-128-GCM"). Each is run as a filter at 1 ms and the rows it yields are
# recorded; the cache makes a rerun free.
entering_keys = {x['key'] for x in recs if x['ditEntries']}
never_keys = {x['key'] for x in recs if not x['ditEntries']}
cands = set()
for f, _ in using:
    w = f.split(' ')
    for i in range(len(w), 0, -1): cands.add(' '.join(w[:i]))
# ... plus every literal the tool itself compares the filter against (selected.find("trusttoken"), selected != "HRSS",
# ...): some benchmarks answer only to a token that appears in no row name, lowercase "trusttoken" and "pkcs8" among them
LITERALS = ['RSA', 'RSAKeyGen', 'SHAKE256-x4', 'Absorb', 'Squeeze', '25519', 'SPAKE2', 'scrypt', 'HRSS', 'hashtocurve', 'base64',
            'siphash', 'trusttoken', 'self-test', 'Jitter', 'dhcheck', 'pkcs8', 'CRYPTO_refcount_inc']
speed_cc = os.path.join(src, 'tool', 'speed.cc') if src else None
if speed_cc and os.path.exists(speed_cc):
    LITERALS = sorted(set(LITERALS) | set(re.findall(r'selected(?:\.find\(|\s*[!=]=\s*)"([^"]+)"', open(speed_cc).read())))
cands |= set(LITERALS)
probe_path = os.path.join(out, 'filter_probe.json')
probe = {} if a.reprobe or not os.path.exists(probe_path) else json.load(open(probe_path))
todo = [c for c in sorted(cands) if c not in probe]
if todo and not a.no_probe:
    if not os.access(bssl, os.X_OK): sys.exit(f'no census build at {bssl} to probe filters with')
    sys.stderr.write(f'probing {len(todo)} candidate filters on the tool (1 ms each row)\n')
    for i, c in enumerate(todo, 1):
        p = subprocess.run([bssl, 'speed', '-json', '-timeout_ms', '1', '-threads', '1', '-filter', c], capture_output=True, text=True)
        try: got = json.loads(p.stdout) if p.stdout.strip().startswith('[') else []
        except json.JSONDecodeError: got = []
        probe[c] = sorted(rowkey(r) for r in got)
        if i % 40 == 0: sys.stderr.write(f'  {i}/{len(todo)}\n')
    json.dump(probe, open(probe_path, 'w'), indent=0)
elif todo:
    sys.stderr.write(f'{len(todo)} candidates not in the probe cache and --no-probe given; the filter list may be incomplete\n')
sel_of = {c: set(k) for c, k in probe.items() if c in cands and k}
# Choose a set cover. Each filter is its own pass of the tool, so a row selected by two filters runs twice
# (extra time and samples, nothing worse) and a never-entering row that rides along costs its time for no
# information; both are costs, neither is forbidden. Greedy weighted set cover: at each step the filter with
# the lowest cost per newly covered entering row, cost = every row the filter selects (each runs again under it,
# covered or not) + 4 x the never-entering rows among them. Then any filter whose entering rows are all covered
# by the others is dropped. Rows that never enter the
# bracket but come along are passengers: run, kept, and marked in the census as never entering.
def greedy_cover(start):
    filters = list(start); selected = set().union(*(sel_of[c] for c in start)) if start else set()
    covered_ent = selected & entering_keys
    while covered_ent != entering_keys:
        best = None
        for c, k in sel_of.items():
            gain = len((k & entering_keys) - covered_ent)
            if not gain: continue
            cost = len(k) + 4 * len(k & never_keys)     # every selected row runs again under this filter; passengers cost extra
            score = (cost / gain, -gain, len(c), c)
            if best is None or score < best[0]: best = (score, c, k)
        if best is None: break
        _, c, k = best; filters.append(c); selected |= k; covered_ent |= k & entering_keys
    for c in list(filters):   # redundancy pass
        others = set().union(*(sel_of[g] for g in filters if g != c)) if len(filters) > 1 else set()
        if (sel_of[c] & entering_keys) <= others: filters.remove(c)
    sel_ = set().union(*(sel_of[c] for c in filters)) if filters else set()
    return sorted(filters), sum(len(sel_of[c]) for c in filters), len(sel_ & never_keys)
# Two starts: empty, and "essentials first" (a filter that is the only way to reach some entering row goes in
# before anything else, so a broad filter is not taken first and then overlapped by the narrow one that was
# needed anyway, e.g. "EVP" before "AES-128"). Keep the cover with fewer row-runs, then fewer passengers.
reach = collections.defaultdict(list)
for c, k in sel_of.items():
    for key in k & entering_keys: reach[key].append(c)
essential = sorted({cs[0] for cs in reach.values() if len(cs) == 1})
covers = [greedy_cover([]), greedy_cover(essential)]
filters, row_runs_, pas_ = min(covers, key=lambda t: (t[1], t[2], len(t[0])))
selected = set().union(*(sel_of[c] for c in filters)) if filters else set()
uncovered = entering_keys - selected
passengers = selected & never_keys
sel = [x for x in recs if x['key'] in selected]
sel_entering = [x for x in sel if x['ditEntries']]
row_runs = sum(len(sel_of[c]) for c in filters)                      # a row selected by two filters runs twice
twice = [x['key'] for x in recs if sum(1 for fl in filters if x['key'] in sel_of.get(fl, ())) > 1]
if uncovered:
    sys.stderr.write(f'filter set imperfect: {len(uncovered)} entering rows no candidate reaches {sorted(uncovered)[:5]}\n')
passenger_fams = sorted(set(family(k) for k in passengers))
hdr = [f"# bssl speed -filter strings for the timing run: {len(filters)} filters selecting {len(sel)} rows of the {len(recs)}-row suite,",
       f"# every one of the {len(entering_keys)} rows that enter the DIT bracket" + (f" and {len(passengers)} passenger rows that never do but cannot be" if passengers else ""),
       f"# separated from ones that do by the tool's filter ({', '.join(passenger_fams)})." if passengers else "# and no other.",
       f"# The tool matches a filter against a per-benchmark selection name, so these were found by probing it (filter_probe.json)."
       + (f" {len(twice)} rows are selected by two filters and run twice." if twice else ""),
       f"# census of {datetime.date.today().isoformat()}, count_awslc.py; one filter per line; bench_awslc.py reads this as BENCH_TESTS_FILE"]
open(os.path.join(out, 'bracketed_filters.txt'), 'w').write('\n'.join(hdr) + '\n' + '\n'.join(filters) + '\n')

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
     f"the {len(sel)} rows the timing run covers: every entering row" + (f" plus {len(passengers)} passengers the tool cannot separate from them" if passengers else "") + ".\n",
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
               passengers=sorted(passengers), recorded=rec, date=datetime.date.today().isoformat(),
               summary=dict(rows=len(recs), families=len(fams), rows_entering=nrows_using, families_entering=len(using),
                            rows_selected_by_filters=len(sel), passenger_rows=len(passengers), families_never=never_names)),
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
          f"**Default run (`reproduce.sh`, `reproduce.sh 3`)**: the {len(filters)} filters in `data/bracketed_filters.txt`. The tool "
          f"matches a filter against a per-benchmark selection name, not the row description (\"AES-128\" selects the block, EVP, "
          f"AEAD and CMAC AES-128 rows; \"ECDSA P-256\" selects signing and verify together; \"ECDSA P-256 signing\" selects nothing), "
          f"so the list is found by probing the tool with every prefix of every entering family and every literal its source compares "
          f"the filter against, then choosing a set cover of the entering rows that costs the fewest row-runs (a row a filter selects runs "
          f"under it whether or not another filter already covers it; a never-entering row costs extra). It selects **{len(sel)} rows: all "
          f"{len(sel_entering)} that enter the bracket"
          + (f", plus {len(passengers)} passengers that never do but share a selection name with rows that do ({', '.join(passenger_fams)}); "
             f"they are run and kept, and the census marks them, so the analysis can set them aside" if passengers else " and no other")
          + f"**" + (f", and {len(twice)} rows the tool selects under two filters run twice" if twice else ", with no row run twice")
          + f". The driver reads the file as `BENCH_TESTS_FILE`; `BENCH_TESTS=...` overrides it. Run length: "
          f"row-runs x 6 arms x 8 passes x the window, {row_runs} row-runs here, about {row_runs * 6 * 8 * 0.4 / 3600:.1f} h per run at 400 ms and "
          f"{row_runs * 6 * 8 * 0.05 / 60:.0f} min at 50 ms.\n",
          "**Paper stage (`reproduce.sh paper`)**: `BENCH_TESTS=\"AES-128,AEAD-ChaCha20-Poly1305,ECDSA P-256,RNG\"` with "
          "`CHUNKS=16,1350,16384`: the ten rows of the paper table plus the other rows those selection names reach (the ECDSA P-256 "
          "verify row among them, which never enters the bracket and is not in the table).\n"]
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
      f"{len(recs) - nrows_using} rows / {len(never)} families never do; {len(filters)} filters select {len(sel)} rows "
      f"({len(sel_entering)} entering + {len(passengers)} passengers; {len(twice)} run twice; {len(uncovered)} entering rows unreachable). "
      f"wrote {out}/dit_census.md, dit_census.json, bracketed_filters.txt" + (f", {a.info_md}" if a.info_md else ''))
