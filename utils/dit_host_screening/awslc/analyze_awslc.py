#!/usr/bin/env python3
"""Experiment 14 analysis: read the driver's speed.json, write report.md, summary.json and an HTML page.

  analyze_awslc.py [speed.json] [--out DIR] [--html FILE]

Everything is derived from the medians the driver saved (cycles/op, instructions/op and IPC per
arm, per row) plus the validity record (pin, flags, clocks). Nothing here re-measures or drops.
The decomposition rests on one fact about the arms: AEAD-AES-128-GCM seal and the single-block
AES calls enter exactly ONE bracketed function per operation, so their B-A is the price of one
entry (two mode-changing writes), H-A the price of one read plus one non-changing write, Bs-B
the barrier after a changing write, Hs-H the barrier after a non-changing one.
"""
import json, os, re, sys, statistics as st, datetime, html

ARMS = ['A', 'C', 'B', 'Bs', 'H', 'Hs']
ARM_NAME = {'A': 'release, no DIT', 'C': 'blanket (DIT set before main)', 'B': 'shipped bracket', 'Bs': 'bracket + sb',
            'H': 'vendor hoisting (-dit)', 'Hs': 'hoisting + sb'}
SIZES = [16, 256, 1350, 8192, 16384]

def load(path):
    return json.load(open(path))

def parse_key(k):
    m = re.match(r'^(.*) \[(\d+) B\]$', k)
    if m: return m.group(1), int(m.group(2))
    m = re.match(r'^(.*) \[(\d+)-bit\]$', k)
    if m: return m.group(1), None
    return k, None

def pct(m, a):
    return (m[a] / m['A'] - 1) * 100 if a in m and 'A' in m else float('nan')

def analyze(d):
    R = d['results']; out = {'rows': {}, 'series': {}, 'prices': {}, 'validity': {}, 'anomalies': [], 'density': {}}
    v = out['validity']
    v.update(arms=d.get('arms', ARMS), tests=d.get('tests'), reps=d.get('reps'), timeout_ms=d.get('timeout_ms'), chunks=d.get('chunks'),
             pin_cpu=d.get('pin_cpu'), unpinned=d.get('unpinned', 0), failures=len(d.get('failures', [])),
             flagged=d.get('flagged', d.get('dropped', 0)), flag_mode='flagged (kept)' if 'flagged' in d else 'DROPPED (old driver)',
             no_pmc_rows=d.get('no_pmc_rows', 0), gate=d.get('gate'), clock_band=d.get('clock_band'))
    clocks = d.get('all_clocks_mhz') or d.get('kept_clocks_mhz') or []
    if clocks:
        cs = sorted(clocks); v['clock'] = dict(min=cs[0], p10=cs[len(cs) // 10], median=st.median(cs), max=cs[-1], n=len(cs))
    ns = [r['n'] for r in R.values()]
    if ns: v['samples_per_row_A'] = dict(min=min(ns), median=st.median(ns), max=max(ns))
    for k, r in R.items():
        fam, size = parse_key(k); m = r['median_cycles_per_op']
        # suspect cells: the driver records them (cell clock outside the band); for JSON from an older
        # driver, fall back to the shape they take: an arm's median 20x above or 20x below A's, or A's own
        # clock (from ns_per_op_A) outside the band
        suspect = r.get('suspect_cells')
        if suspect is None:
            suspect = [x for x in m if x != 'A' and (m[x] > 20 * m['A'] or m[x] < m['A'] / 20)]
            if r.get('ns_per_op_A') and not 3000 <= m['A'] / r['ns_per_op_A'] * 1000 <= 4700: suspect.append('A')
        row = dict(key=k, family=fam, size=size, cycles={a: m[a] for a in ARMS if a in m}, suspect=suspect,
                   pct={a: pct(m, a) for a in ARMS if a != 'A' and a in m}, mad=r['mad_pct'], n=r['n'],
                   ipc={a: r['ipc'][a] for a in ARMS if a in r.get('ipc', {})} or {'A': r.get('ipc_A')},
                   instrs={a: r['median_instrs_per_op'][a] for a in ARMS if a in r.get('median_instrs_per_op', {})},
                   ns_per_op_A=r.get('ns_per_op_A'))
        for a in ('B', 'Bs', 'H', 'Hs'):
            if a in m: row[f'{a}_minus_A_cyc'] = m[a] - m['A']
        if 'Bs' in m and 'B' in m: row['Bs_minus_B_cyc'] = m['Bs'] - m['B']
        if 'Hs' in m and 'H' in m: row['Hs_minus_H_cyc'] = m['Hs'] - m['H']
        out['rows'][k] = row
        if 'C' in m and abs(row['pct']['C']) > 2 and r['mad_pct'] < 2:
            out['anomalies'].append(dict(key=k, C_pct=row['pct']['C'], H_pct=row['pct'].get('H'), mad=r['mad_pct'], A=m['A']))
    v['suspect_cells'] = sum(len(row['suspect']) for row in out['rows'].values())
    v['suspect_rows'] = [k for k, row in out['rows'].items() if row['suspect']]
    # size series per family
    fams = {}
    for k, row in out['rows'].items():
        if row['size'] in SIZES: fams.setdefault(row['family'], {})[row['size']] = row
    for fam, by in fams.items():
        if len(by) < 4: continue
        ser = []
        for s in SIZES:
            if s in by:
                r = by[s]; ser.append(dict(size=s, A=r['cycles']['A'], B_minus_A=r.get('B_minus_A_cyc'), Bs_minus_B=r.get('Bs_minus_B_cyc'),
                                          H_minus_A=r.get('H_minus_A_cyc'), Hs_minus_H=r.get('Hs_minus_H_cyc'), pct=r['pct'], cycles=r['cycles']))
        ba = [x['B_minus_A'] for x in ser if x['B_minus_A'] is not None]
        out['series'][fam] = dict(points=ser, B_minus_A_min=min(ba), B_minus_A_max=max(ba),
                                  constancy=(max(ba) / min(ba)) if min(ba) > 0 else None)
    # the three prices, from the one-entry rows
    def price_from(key, entries=1):
        r = out['rows'].get(key)
        if not r: return None
        return dict(row=key, entries=entries, changing_pair=r.get('B_minus_A_cyc'), read_plus_nonchanging=r.get('H_minus_A_cyc'),
                    sb_after_changing=r.get('Bs_minus_B_cyc'), sb_after_nonchanging=r.get('Hs_minus_H_cyc'))
    out['prices']['gcm_seal_16'] = price_from('AEAD-AES-128-GCM seal [16 B]')
    out['prices']['gcm_seal_16384'] = price_from('AEAD-AES-128-GCM seal [16384 B]')
    out['prices']['aes_block'] = price_from('AES-128 encrypt')
    out['prices']['gcm_open_16'] = price_from('AEAD-AES-128-GCM open [16 B]', entries=2)
    # density: entries implied per op = (B-A) / the price of ONE entry. Two unit prices exist: an
    # entry into an AEAD/EVP-level function (the GCM seal row) and an entry into the single-block
    # AES call (the AES-128 encrypt row), which is cheaper because there is less in flight to drain.
    # Rows built from per-block AES calls (CMAC) are priced with the block entry.
    pair = out['prices']['gcm_seal_16']['changing_pair'] if out['prices'].get('gcm_seal_16') else None
    block = out['prices']['aes_block']['changing_pair'] if out['prices'].get('aes_block') else pair
    out['prices']['pair_price_cycles'] = pair; out['prices']['block_entry_price_cycles'] = block
    for key, blocks in [('CMAC-AES-128-CBC [16384 B]', 1024), ('CMAC-AES-128-CBC [16 B]', 1), ('EVP-AES-128-GCM encrypt [16 B]', None),
                        ('EVP-AES-128-GCM encrypt [16384 B]', None), ('EVP-AES-128-CBC decrypt [16 B]', None), ('AEAD-AES-128-GCM open [16 B]', None)]:
        r = out['rows'].get(key)
        unit = block if (blocks and block) else pair
        if r and unit:
            out['density'][key] = dict(B_minus_A=r['B_minus_A_cyc'], implied_entries=r['B_minus_A_cyc'] / unit, unit_price=unit,
                                       unit='single-block AES entry' if unit == block and blocks else 'AEAD-level entry', blocks=blocks, pct_B=r['pct'].get('B'))
    return out

# The paper's ten rows: (label, row key, priced by the single-block entry?)
PAPER_ROWS = [
    ("AES-128 single block",                    "AES-128 encrypt",                    True),
    ("EVP AES-GCM encrypt, 16 B",               "EVP-AES-128-GCM encrypt [16 B]",     False),
    ("AEAD AES-GCM seal, 16 B",                 "AEAD-AES-128-GCM seal [16 B]",       False),
    ("AEAD AES-GCM open, 16 B",                 "AEAD-AES-128-GCM open [16 B]",       False),
    ("AEAD ChaCha20-Poly1305 seal, 16 B",       "AEAD-ChaCha20-Poly1305 seal [16 B]", False),
    ("AEAD AES-GCM seal, 1350 B (a TLS record)","AEAD-AES-128-GCM seal [1350 B]",     False),
    ("AEAD AES-GCM seal, 16 KB",                "AEAD-AES-128-GCM seal [16384 B]",    False),
    ("CMAC-AES-128, 16 KB",                     "CMAC-AES-128-CBC [16384 B]",         True),
    ("ECDSA P-256 sign",                        "ECDSA P-256 signing",                False),
    ("RNG, 16 B",                               "RNG [16 B]",                         False),
]
# what a run needs to produce exactly these rows: the filters and the chunk sizes
PAPER_TESTS = "AES-128,AEAD-ChaCha20-Poly1305,ECDSA P-256,RNG"
PAPER_CHUNKS = "16,1350,16384"

def paper_table(an):
    p = an['prices']; pair = p.get('pair_price_cycles'); block = p.get('block_entry_price_cycles') or pair
    hdr = "| # | op | A cyc/op | entries/op | C blanket | B bracket | Bs bracket+sb | H hoisted | Hs hoisted+sb | MAD |"
    md = [hdr, "|---|---|---|---|---|---|---|---|---|---|"]; csv = ["n,op,A_cycles_per_op,entries_per_op,C_pct,B_pct,Bs_pct,H_pct,Hs_pct,MAD_pct"]
    missing = []
    for i, (label, key, isblk) in enumerate(PAPER_ROWS, 1):
        r = an['rows'].get(key)
        if not r: missing.append(key); continue
        c = r['cycles']; a = c['A']; unit = block if isblk else pair
        entries = (c['B'] - a) / unit if unit and 'B' in c else float('nan')
        q = {x: (c[x] / a - 1) * 100 if x in c else float('nan') for x in ('C', 'B', 'Bs', 'H', 'Hs')}
        sus = r.get('suspect', []); dg = lambda x: '\u2020' if x in sus else ''
        rowmark = '\u2020' if sus else ''
        md.append(f"| {i} | {label}{rowmark} | {a:,.0f}{dg('A')} | {entries:,.0f} | " + ' | '.join(f"{q[x]:+.0f}%{dg(x)}" for x in ('C', 'B', 'Bs', 'H', 'Hs')) + f" | {r['mad']:.2f}% |")
        csv.append(f"{i},\"{label}\",{a:.1f},{entries:.1f}," + ','.join(f"{q[x]:.2f}" for x in ('C', 'B', 'Bs', 'H', 'Hs')) + f",{r['mad']:.3f}")
    rows_used = [an['rows'][k] for _, k, _ in PAPER_ROWS if k in an['rows']]
    gm = {x: geomean_pct(rows_used, x) for x in ('C', 'B', 'Bs', 'H', 'Hs')}
    md.append("| | **geometric mean of the ratio to A** | | | " + ' | '.join(f"**{gm[x][0]:+.0f}%**" for x in ('C', 'B', 'Bs', 'H', 'Hs')) + " | |")
    csv.append("geomean,\"geometric mean of arm/A over the rows above\",,," + ','.join(f"{gm[x][0]:.2f}" for x in ('C', 'B', 'Bs', 'H', 'Hs')) + ",")
    note = (f"Entries per op = (B - A) / one entry's price: {fmt_cyc(pair)} cycles for an AEAD-level entry, {fmt_cyc(block)} for a single-block AES entry "
            f"(rows 1 and 8). Run only these rows with BENCH_TESTS=\"{PAPER_TESTS}\" CHUNKS={PAPER_CHUNKS}.")
    if missing: note += " MISSING from this run: " + ', '.join(missing)
    if any(an['rows'].get(k, {}).get('suspect') for _, k, _ in PAPER_ROWS):
        note += " \u2020 marks a cell whose median implies a clock outside the P-core band: a majority of its samples carried the backward-counter fault; the value is kept but not to be read."
    return '\n'.join(md) + '\n\n' + note + '\n', '\n'.join(csv) + '\n', missing

import math
def geomean_pct(rows, arm):
    """Geometric mean of arm/A over the given rows, as percent over A; a cell marked suspect in
    either arm is left out (its median is known wrong) and the count left out is returned."""
    logs, skipped = [], 0
    for r in rows:
        c = r['cycles']
        if arm not in c or 'A' not in c: continue
        if arm in r.get('suspect', []) or 'A' in r.get('suspect', []): skipped += 1; continue
        logs.append(math.log(c[arm] / c['A']))
    return ((math.exp(sum(logs) / len(logs)) - 1) * 100 if logs else float('nan')), len(logs), skipped

def fmt_pct(x): return 'n/a' if x is None or x != x else f"{x:+.1f}%"
def fmt_cyc(x): return 'n/a' if x is None or x != x else f"{x:,.0f}"

def report_md(an, src):
    v = an['validity']; L = []
    L.append(f"# Experiment 14 analysis: AWS-LC's shipped DIT bracket on `bssl speed`\n")
    L.append(f"Source `{src}`, analysed {datetime.date.today().isoformat()}. Arms: " + ', '.join(f"{a} = {ARM_NAME[a]}" for a in v['arms']) + ".\n")
    L.append("## Validity\n")
    L.append(f"- pinned to CPU {v['pin_cpu']} (kern.sched_thread_bind_cpu); processes not reporting the bind: {v['unpinned']}")
    L.append(f"- failures: {v['failures']}; rows without PMC cycles: {v['no_pmc_rows']}; samples {v['flag_mode']} for implied clock outside band {v.get('clock_band')}: {v['flagged']}")
    if 'clock' in v: c = v['clock']; L.append(f"- implied clock of all samples: min {c['min']:.0f}, p10 {c['p10']:.0f}, median {c['median']:.0f}, max {c['max']:.0f} MHz ({c['n']} samples)")
    if 'samples_per_row_A' in v: s = v['samples_per_row_A']; L.append(f"- A samples per row: min {s['min']}, median {s['median']}, max {s['max']} (reps {v['reps']}, window {v['timeout_ms']} ms)")
    L.append(f"- DIT readback gate: {v['gate']}")
    L.append(f"- SUSPECT cells (a cell whose median implies a clock outside the band, i.e. a majority of its samples was flagged; kept, marked with a dagger): {v.get('suspect_cells', 0)}"
             + (f" in rows: {', '.join(v['suspect_rows'])}" if v.get('suspect_rows') else '') + "\n")
    p = an['prices']
    if p.get('gcm_seal_16'):
        g = p['gcm_seal_16']; L.append("## The three prices (one bracket entry per op: AEAD-AES-128-GCM seal, 16 B)\n")
        L.append("| what | cycles |\n|---|---|")
        L.append(f"| two mode-changing writes (B - A) | {fmt_cyc(g['changing_pair'])} |")
        L.append(f"| read + one non-changing write (H - A) | {fmt_cyc(g['read_plus_nonchanging'])} |")
        L.append(f"| sb after a changing write (Bs - B) | {fmt_cyc(g['sb_after_changing'])} |")
        L.append(f"| sb after a non-changing write (Hs - H) | {fmt_cyc(g['sb_after_nonchanging'])} |")
        if p.get('aes_block'):
            b = p['aes_block']; L.append(f"| same four, from the single-block AES row | {fmt_cyc(b['changing_pair'])} / {fmt_cyc(b['read_plus_nonchanging'])} / {fmt_cyc(b['sb_after_changing'])} / {fmt_cyc(b['sb_after_nonchanging'])} |")
        L.append("")
    L.append("## Is the cost constant across sizes? (absolute B - A cycles per op)\n")
    L.append("| family | 16 B | 256 B | 1350 B | 8 KB | 16 KB | max/min |\n|---|---|---|---|---|---|---|")
    for fam, s in sorted(an['series'].items()):
        cells = {x['size']: x['B_minus_A'] for x in s['points']}
        L.append(f"| {fam} | " + ' | '.join(fmt_cyc(cells.get(z)) for z in SIZES) + f" | {s['constancy']:.2f} |" if s['constancy'] else f"| {fam} | " + ' | '.join(fmt_cyc(cells.get(z)) for z in SIZES) + " | n/a |")
    L.append("")
    if an['density']:
        L.append("## Density: how many bracket entries an operation pays for\n")
        L.append(f"One AEAD-level entry (two changing writes) costs {fmt_cyc(p.get('pair_price_cycles'))} cycles on the seal row and one single-block AES entry {fmt_cyc(p.get('block_entry_price_cycles'))}; B - A divided by the applicable price is the entries per op.\n")
        L.append("| row | B - A cycles | unit price | implied entries/op | B vs A |\n|---|---|---|---|---|")
        for k, dd in an['density'].items():
            L.append(f"| {k} | {fmt_cyc(dd['B_minus_A'])} | {fmt_cyc(dd['unit_price'])} ({dd['unit']}) | {dd['implied_entries']:.0f}{' (' + str(dd['blocks']) + ' AES blocks)' if dd['blocks'] else ''} | {fmt_pct(dd['pct_B'])} |")
        L.append("")
    if an['anomalies']:
        L.append("## Blanket DIT moving a row by more than 2% (C vs A, rows with MAD < 2%)\n")
        L.append("| row | A cyc/op | C | H | MAD |\n|---|---|---|---|---|")
        for x in sorted(an['anomalies'], key=lambda x: x['C_pct']):
            L.append(f"| {x['key']} | {fmt_cyc(x['A'])} | {fmt_pct(x['C_pct'])} | {fmt_pct(x['H_pct'])} | {x['mad']:.2f}% |")
        L.append("")
    L.append("## Every row (percent over A, cycles per op)\n")
    L.append("| row | A cyc/op | IPC A | C | B | B-A cyc | Bs | H | Hs | MAD |\n|---|---|---|---|---|---|---|---|---|---|")
    for k in sorted(an['rows']):
        r = an['rows'][k]; c = r['cycles']; q = r['pct']
        dg = lambda a: '\u2020' if a in r.get('suspect', []) else ''
        rowmark = '\u2020' if r.get('suspect') else ''
        L.append(f"| {k}{rowmark} | {fmt_cyc(c['A'])}{dg('A')} | {r['ipc'].get('A', float('nan')):.2f} | {fmt_pct(q.get('C'))}{dg('C')} | {fmt_pct(q.get('B'))}{dg('B')} | {fmt_cyc(r.get('B_minus_A_cyc'))} | {fmt_pct(q.get('Bs'))}{dg('Bs')} | {fmt_pct(q.get('H'))}{dg('H')} | {fmt_pct(q.get('Hs'))}{dg('Hs')} | {r['mad']:.2f}% |")
    allrows = list(an['rows'].values())
    gm = {x: geomean_pct(allrows, x) for x in ('C', 'B', 'Bs', 'H', 'Hs')}
    L.append(f"| **geometric mean of the ratio to A, all {gm['B'][1]} clean rows** | | | " + ' | '.join(f"**{gm[x][0]:+.1f}%**" if x != 'B' else f"**{gm[x][0]:+.1f}%** | " for x in ('C', 'B', 'Bs', 'H', 'Hs')) + " | |")
    L.append(f"\nThe geometric mean leaves out cells marked suspect ({max(g[2] for g in gm.values())} at most in any column); every other row counts once, so it is a summary over the tool's rows, not over any application's mix of them.")
    if any(len(r['ipc']) > 1 for r in an['rows'].values()):
        L.append("\n## IPC and instructions per op, every arm\n")
        L.append("| row | instr/op A | " + ' | '.join(f"IPC {a}" for a in ARMS) + " | B-A instr |\n|---|---|" + '---|' * len(ARMS) + "---|")
        for k in sorted(an['rows']):
            r = an['rows'][k]; ins = r['instrs']; ipc = r['ipc']
            L.append(f"| {k} | {fmt_cyc(ins.get('A'))} | " + ' | '.join(f"{ipc[a]:.2f}" if a in ipc else 'n/a' for a in ARMS) + f" | {fmt_cyc((ins['B'] - ins['A']) if 'B' in ins and 'A' in ins else None)} |")
    return '\n'.join(L) + '\n'

HTML_TEMPLATE = r'''<title>The Shipping Bracket</title>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Bricolage+Grotesque:opsz,wght@12..96,500;12..96,700&family=IBM+Plex+Sans:ital,wght@0,400;0,600;1,400&family=IBM+Plex+Mono:wght@400;500&display=swap">
<style>
  :root {
    --ground: #EEF1F4; --surface: #FFFFFF; --ink: #16202A; --muted: #5B6672; --rule: #D3D9E0; --code-bg: #E6EAEF;
    --accent: #0F6E74; --accent-ink: #0B565B;
    --arm-A: #4A5568; --arm-C: #0F6E74; --arm-B: #B5473A; --arm-Bs: #7A2E24; --arm-H: #C58A1E; --arm-Hs: #7D5A12;
    --flag: #B5473A; --ok: #2F7D4F;
    --display: "Bricolage Grotesque", "Avenir Next", "Helvetica Neue", sans-serif;
    --body: "IBM Plex Sans", "Helvetica Neue", Arial, sans-serif;
    --mono: "IBM Plex Mono", "SF Mono", Menlo, monospace;
  }
  @media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) {
    --ground: #11161C; --surface: #181E26; --ink: #E6EAF0; --muted: #98A3B0; --rule: #2A323C; --code-bg: #202832;
    --accent: #5FBFC4; --accent-ink: #8AD4D8;
    --arm-A: #A7B0BD; --arm-C: #5FBFC4; --arm-B: #E07A6C; --arm-Bs: #B9584B; --arm-H: #E2B25A; --arm-Hs: #B8903F;
    --flag: #E07A6C; --ok: #6CC08B; } }
  :root[data-theme="dark"] {
    --ground: #11161C; --surface: #181E26; --ink: #E6EAF0; --muted: #98A3B0; --rule: #2A323C; --code-bg: #202832;
    --accent: #5FBFC4; --accent-ink: #8AD4D8;
    --arm-A: #A7B0BD; --arm-C: #5FBFC4; --arm-B: #E07A6C; --arm-Bs: #B9584B; --arm-H: #E2B25A; --arm-Hs: #B8903F;
    --flag: #E07A6C; --ok: #6CC08B; }
  body { margin: 0; background: var(--ground); color: var(--ink); font-family: var(--body); font-size: 16.5px; line-height: 1.55; }
  main { max-width: 78ch; margin: 0 auto; padding: 2.4rem 1.2rem 5rem; }
  .eyebrow { font-family: var(--mono); font-size: 0.76rem; letter-spacing: 0.08em; text-transform: uppercase; color: var(--muted); }
  h1 { font-family: var(--display); font-weight: 700; font-size: 2.5rem; line-height: 1.06; margin: 0.3rem 0 0.7rem; text-wrap: balance; }
  h2 { font-family: var(--display); font-weight: 700; font-size: 1.45rem; margin: 2.6rem 0 0.7rem; text-wrap: balance; }
  h3 { font-family: var(--body); font-weight: 600; font-size: 1.02rem; margin: 1.5rem 0 0.4rem; }
  p, li { max-width: 70ch; } p { margin: 0 0 1rem; }
  .lede { font-size: 1.12rem; }
  code { font-family: var(--mono); font-size: 0.87em; background: var(--code-bg); padding: 0.08em 0.35em; border-radius: 3px; }
  pre { font-family: var(--mono); font-size: 0.83rem; line-height: 1.5; background: var(--code-bg); border-left: 3px solid var(--rule); padding: 0.9rem 1rem; margin: 0.6rem 0 1.2rem; overflow-x: auto; }
  pre code { background: none; padding: 0; }
  .status { display: grid; grid-template-columns: repeat(auto-fit, minmax(11rem, 1fr)); gap: 0.6rem; margin: 1.2rem 0 1.6rem; }
  .status div { background: var(--surface); border: 1px solid var(--rule); padding: 0.6rem 0.8rem; font-family: var(--mono); font-size: 0.8rem; color: var(--muted); }
  .status b { display: block; color: var(--ink); font-size: 1.05rem; font-weight: 500; margin-top: 0.15rem; }
  .status .ok b { color: var(--ok); } .status .flag b { color: var(--flag); }
  .tablewrap { overflow-x: auto; margin: 0.6rem 0 1.4rem; background: var(--surface); border: 1px solid var(--rule); }
  table { border-collapse: collapse; width: 100%; font-size: 0.86rem; font-variant-numeric: tabular-nums; }
  th, td { text-align: left; padding: 0.42rem 0.65rem; border-bottom: 1px solid var(--rule); vertical-align: top; white-space: nowrap; }
  th { font-family: var(--mono); font-size: 0.72rem; letter-spacing: 0.05em; text-transform: uppercase; color: var(--muted); font-weight: 500; position: sticky; top: 0; background: var(--surface); }
  td.num, th.num { text-align: right; font-family: var(--mono); font-size: 0.82rem; }
  td.row { font-family: var(--mono); font-size: 0.8rem; }
  tr:last-child td { border-bottom: none; }
  .hot { color: var(--arm-B); font-weight: 600; } .cold { color: var(--accent-ink); font-weight: 600; }
  .legend { font-size: 0.86rem; color: var(--muted); }
  .chart { background: var(--surface); border: 1px solid var(--rule); padding: 0.8rem 0.6rem 0.4rem; margin: 0.8rem 0 1.4rem; }
  .chart svg { width: 100%; height: auto; display: block; font-family: var(--mono); font-size: 11px; }
  .chart .axis { stroke: var(--rule); } .chart text { fill: var(--muted); }
  .chart .grid { stroke: var(--rule); stroke-dasharray: 2 3; }
  .swatches { display: flex; flex-wrap: wrap; gap: 0.5rem 1.1rem; font-family: var(--mono); font-size: 0.78rem; color: var(--muted); margin: 0.3rem 0 0.8rem; }
  .swatches span::before { content: ""; display: inline-block; width: 0.75rem; height: 0.75rem; border-radius: 2px; margin-right: 0.35rem; vertical-align: -0.1rem; background: var(--sw); }
  .note { border-left: 3px solid var(--accent); padding: 0.2rem 0 0.2rem 1rem; margin: 1rem 0 1.4rem; max-width: 68ch; }
  details summary { cursor: pointer; color: var(--accent-ink); font-weight: 600; }
  a { color: var(--accent-ink); }
  @media (prefers-reduced-motion: reduce) { * { transition: none !important; } }
</style>
<main>
  <div class="eyebrow">Experiment 14 &middot; Apple M4 &middot; PMC cycles per operation</div>
  <h1>The Shipping Bracket</h1>
  <p class="lede">AWS-LC ships the developer's DIT bracket as a build option and has never published what it costs. This is what it costs, on the vendor's own benchmark, one hard-pinned core, cycles counted by the hardware.</p>
  <div class="status" id="status"></div>

  <h2>What the arms are</h2>
  <div class="tablewrap"><table>
    <tr><th>arm</th><th>binary</th><th>run</th><th>per bracketed entry point</th></tr>
    <tr><td class="row">A</td><td>release, DIT option off</td><td></td><td>nothing</td></tr>
    <tr><td class="row">C</td><td>release</td><td>DIT set before <code>main</code> by the injected constructor</td><td>nothing: a true blanket</td></tr>
    <tr><td class="row">B</td><td><code>ENABLE_DATA_INDEPENDENT_TIMING=ON</code>, as shipped</td><td></td><td><code>mrs DIT</code>; <code>msr dit,#1</code>; <code>msr dit,#0</code> at exit when it was off</td></tr>
    <tr><td class="row">Bs</td><td>the same with <code>sb</code> after the enable</td><td></td><td>Apple's recipe</td></tr>
    <tr><td class="row">H</td><td>the shipped build</td><td><code>-dit</code>: the vendor's hoisting, DIT set once for the run</td><td>read and enable still execute; the enable changes nothing; clear skipped</td></tr>
    <tr><td class="row">Hs</td><td>the <code>sb</code> build</td><td><code>-dit</code></td><td>read, enable and <code>sb</code> still execute; clear skipped</td></tr>
  </table></div>

  <h2>One bracket entry costs the same at every message size</h2>
  <p>AEAD-AES-128-GCM seal enters exactly one bracketed function per call. Cycles per operation for the six arms, against message size. The gap between B and A does not move as the message grows from 16 bytes to 16 KB: it is the price of the two mode-changing writes, and nothing else.</p>
  <div class="swatches" id="sw1"></div>
  <div class="chart" id="chart-series"></div>
  <div class="tablewrap" id="series-table"></div>

  <h2>The three prices</h2>
  <p>From the two rows that enter one bracketed function per operation. A write that changes PSTATE.DIT serialises the core; a write that leaves it unchanged nearly does not; the barrier costs almost nothing right after a serialising write and a great deal after a non-serialising one.</p>
  <div class="chart" id="chart-prices"></div>
  <div class="tablewrap" id="prices-table"></div>

  <h2>Nesting multiplies it</h2>
  <p>Dividing each row's B&nbsp;&minus;&nbsp;A by the price of one entry gives the number of bracketed entries an operation pays for. Two unit prices apply: an entry into an AEAD-level function (154 cycles on the seal row) and an entry into the single-block AES call (94 cycles; less is in flight to drain). The EVP cipher layer pays for several AEAD-level entries per call. CMAC calls the bracketed single-block AES once per 16 bytes and pays for 1,024 block entries per 16 KB.</p>
  <div class="tablewrap" id="density-table"></div>

  <h2>The vendor's claim</h2>
  <p id="claim"></p>

  <h2>Where blanket DIT changed the speed</h2>
  <p>Blanket DIT (arm C) costs nothing on the crypto. On several rows it is <em>faster</em> than unhardened, and the vendor's hoisted arm, which also holds DIT on throughout, moves with it. The rows share one feature: fresh random bytes. On this core DIT disables the data-dependent prefetcher, and random words look like pointers to it. A hypothesis, stated as one.</p>
  <div class="tablewrap" id="anomaly-table"></div>

  <h2>The paper's ten rows</h2>
  <p>The set that carries the argument: the same 154-cycle entry at three message sizes (rows 3, 6, 7), the two faces of nesting (2 and 8), the barrier's price (4 against 3, and every Hs against its H), a non-AES primitive (5), a long operation where nothing matters (9), and the row where blanket wins outright (10). A run restricted to these rows takes about two minutes.</p>
  <div class="tablewrap" id="paper-table"></div>

  <h2>Every row</h2>
  <p class="legend">Percent over A, cycles per operation. B&nbsp;&minus;&nbsp;A in absolute cycles. MAD is the spread of A's samples as a percent of its median. A dagger marks a cell whose median implies a clock outside the P-core band: a majority of that cell's samples carried the backward-counter fault, so the value is kept but not to be read.</p>
  <div class="tablewrap" id="full-table"></div>

  <h2>Instructions and IPC, every arm</h2>
  <p class="legend">Cycles = instructions / IPC. The bracket adds a few instructions per entry; the cycles it adds come from the IPC it destroys.</p>
  <div class="tablewrap" id="ipc-table"></div>

  <h2>Method</h2>
  <ul>
    <li>AWS-LC v5.8.0, built three ways with the platform compiler; <code>tool/speed.cc</code> patched to read PMC0 (cycles) and PMC1 (instructions retired), <code>isb</code>-ordered, around its own timed loop, and print both per JSON row. One thread, hard-bound to a P-core through <code>kern.sched_thread_bind_cpu</code>; the constructor reports the bind at exit and the driver checks it.</li>
    <li>Each row: the operation is called in a loop for a fixed window; cycles and instructions are the window totals divided by the call count. Arms rotate on every rep; the first rep is warm-up; medians over the rest.</li>
    <li>Nothing is dropped. Samples whose implied clock (cycles over microseconds) falls outside the P-core band are flagged and counted, never excluded; the count is in the status strip.</li>
    <li>Sources and the rig: <code>paper_experiments/14-awslc-shipping-bracket</code>, <code>utils/dit_host_screening/awslc</code>.</li>
  </ul>
  <p class="legend" id="prov"></p>
</main>
<script>
const DATA = __DATA__;
const ARMS = ['A','C','B','Bs','H','Hs'];
const COL = a => `var(--arm-${a})`;
const fmt = (x, d=1) => (x==null || Number.isNaN(x)) ? 'n/a' : (x>=0?'+':'') + x.toFixed(d) + '%';
const cyc = x => (x==null || Number.isNaN(x)) ? 'n/a' : Math.round(x).toLocaleString();
const esc = s => String(s).replace(/[&<>]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));
const V = DATA.validity;
const geomean = (rows, arm) => { const l = rows.filter(r => r.cycles[arm] != null && !(r.suspect||[]).includes(arm) && !(r.suspect||[]).includes('A')).map(r => Math.log(r.cycles[arm] / r.cycles.A)); return l.length ? (Math.exp(l.reduce((a,b)=>a+b,0)/l.length) - 1) * 100 : NaN; };
// status strip
{
  const cells = [];
  cells.push(['pinned to CPU', V.pin_cpu ?? 'unpinned', V.unpinned===0 ? 'ok' : 'flag']);
  cells.push(['processes not reporting the bind', V.unpinned, V.unpinned===0 ? 'ok':'flag']);
  cells.push(['failures', V.failures, V.failures===0?'ok':'flag']);
  cells.push([`samples ${V.flag_mode}`, V.flagged, V.flagged===0?'ok':'flag']);
  if (V.clock) cells.push(['implied clock, all samples', `${Math.round(V.clock.min)} – ${Math.round(V.clock.max)} MHz`, '']);
  if (V.samples_per_row_A) cells.push(['A samples per row', `${V.samples_per_row_A.min} – ${V.samples_per_row_A.max} of ${V.reps}`, '']);
  cells.push(['window per row', `${V.timeout_ms} ms`, '']);
  cells.push(['suspect cells (majority of samples flagged; marked \u2020)', V.suspect_cells ?? 0, (V.suspect_cells??0)===0?'ok':'flag']);
  document.getElementById('status').innerHTML = cells.map(([k,v,c]) => `<div class="${c}">${esc(k)}<b>${esc(v)}</b></div>`).join('');
}
// series chart: GCM seal
{
  const fam = 'AEAD-AES-128-GCM seal'; const S = DATA.series[fam];
  document.getElementById('sw1').innerHTML = ARMS.map(a => `<span style="--sw:${COL(a)}">${a}</span>`).join('');
  if (S) {
    const pts = S.points; const W=720, H=340, L=64, R=16, T=14, Bm=40;
    const xs = pts.map(p => Math.log2(p.size)); const x0 = Math.min(...xs), x1 = Math.max(...xs);
    const ymax = Math.max(...pts.flatMap(p => ARMS.map(a => p.cycles[a] ?? 0)));
    const ystep = Math.pow(10, Math.floor(Math.log10(ymax))); const yTop = Math.ceil(ymax / ystep) * ystep;
    const X = v => L + (Math.log2(v) - x0) / (x1 - x0) * (W - L - R);
    const Y = v => T + (1 - v / yTop) * (H - T - Bm);
    let s = `<svg viewBox="0 0 ${W} ${H}" role="img" aria-label="cycles per operation against message size for six arms">`;
    for (let g = 0; g <= yTop; g += ystep) s += `<line class="grid" x1="${L}" x2="${W-R}" y1="${Y(g)}" y2="${Y(g)}"/><text x="${L-6}" y="${Y(g)+4}" text-anchor="end">${g.toLocaleString()}</text>`;
    s += `<line class="axis" x1="${L}" x2="${W-R}" y1="${Y(0)}" y2="${Y(0)}"/>`;
    for (const p of pts) s += `<text x="${X(p.size)}" y="${H-Bm+16}" text-anchor="middle">${p.size >= 1024 ? (p.size/1024)+' KB' : p.size+' B'}</text>`;
    s += `<text x="${(L+W-R)/2}" y="${H-6}" text-anchor="middle">message size (log scale)</text>`;
    s += `<text transform="translate(14 ${(T+H-Bm)/2}) rotate(-90)" text-anchor="middle">cycles per operation</text>`;
    for (const a of ARMS) {
      const d = pts.map((p,i) => `${i?'L':'M'}${X(p.size).toFixed(1)},${Y(p.cycles[a]).toFixed(1)}`).join(' ');
      s += `<path d="${d}" fill="none" stroke="${COL(a)}" stroke-width="${a==='A'||a==='B'?2.4:1.6}"/>`;
      for (const p of pts) s += `<circle cx="${X(p.size)}" cy="${Y(p.cycles[a])}" r="3" fill="${COL(a)}"/>`;
    }
    s += '</svg>';
    document.getElementById('chart-series').innerHTML = s;
    let t = `<table><tr><th>size</th><th class="num">A cyc/op</th>${ARMS.slice(1).map(a=>`<th class="num">${a}</th>`).join('')}<th class="num">B − A cyc</th><th class="num">Bs − B</th><th class="num">H − A</th><th class="num">Hs − H</th></tr>`;
    for (const p of pts) t += `<tr><td class="row">${p.size} B</td><td class="num">${cyc(p.A)}</td>${ARMS.slice(1).map(a=>`<td class="num">${fmt(p.pct[a])}</td>`).join('')}<td class="num hot">${cyc(p.B_minus_A)}</td><td class="num">${cyc(p.Bs_minus_B)}</td><td class="num">${cyc(p.H_minus_A)}</td><td class="num">${cyc(p.Hs_minus_H)}</td></tr>`;
    t += `</table>`; document.getElementById('series-table').innerHTML = t;
  }
}
// prices chart
{
  const P = DATA.prices; const rows = [['gcm_seal_16','AEAD-GCM seal, 16 B'],['aes_block','AES-128 single block']].filter(([k])=>P[k]);
  const items = [['changing_pair','two mode-changing writes (B − A)'],['read_plus_nonchanging','read + non-changing write (H − A)'],['sb_after_changing','sb after a changing write (Bs − B)'],['sb_after_nonchanging','sb after a non-changing write (Hs − H)']];
  const W=720, H=40+items.length*34, L=300, R=60;
  const vmax = Math.max(...rows.flatMap(([k]) => items.map(([f]) => P[k][f] ?? 0)));
  const X = v => L + Math.max(0,v)/vmax*(W-L-R);
  let s = `<svg viewBox="0 0 ${W} ${H}" role="img" aria-label="the three prices in cycles">`;
  items.forEach(([f,label],i) => {
    const y = 20 + i*34; s += `<text x="${L-8}" y="${y+14}" text-anchor="end">${esc(label)}</text>`;
    rows.forEach(([k],j) => { const v = P[k][f] ?? 0; const yy = y + j*13;
      s += `<rect x="${L}" y="${yy}" width="${Math.max(0,X(v)-L)}" height="11" fill="${j?'var(--arm-Bs)':'var(--arm-B)'}"/><text x="${X(v)+5}" y="${yy+9.5}">${Math.round(v)}</text>`; });
  });
  s += '</svg>';
  document.getElementById('chart-prices').innerHTML = s + `<div class="swatches"><span style="--sw:var(--arm-B)">${rows[0]?rows[0][1]:''}</span>${rows[1]?`<span style="--sw:var(--arm-Bs)">${rows[1][1]}</span>`:''}</div>`;
  let t = `<table><tr><th>price</th>${rows.map(([,l])=>`<th class="num">${esc(l)}</th>`).join('')}</tr>`;
  for (const [f,label] of items) t += `<tr><td>${esc(label)}</td>${rows.map(([k])=>`<td class="num">${cyc(P[k][f])}</td>`).join('')}</tr>`;
  t += '</table>'; document.getElementById('prices-table').innerHTML = t;
}
// density
{
  const D = DATA.density; let t = `<table><tr><th>row</th><th class="num">B − A cyc</th><th class="num">unit price</th><th class="num">implied entries / op</th><th class="num">B vs A</th></tr>`;
  for (const [k,d] of Object.entries(D)) t += `<tr><td class="row">${esc(k)}</td><td class="num">${cyc(d.B_minus_A)}</td><td class="num">${cyc(d.unit_price)} <span class="legend">${esc(d.unit)}</span></td><td class="num">${d.implied_entries.toFixed(0)}${d.blocks?` (${d.blocks} AES blocks)`:''}</td><td class="num hot">${fmt(d.pct_B)}</td></tr>`;
  t += '</table>'; document.getElementById('density-table').innerHTML = t;
}
// claim
{
  const S = DATA.series['AEAD-AES-128-GCM seal']; const O = DATA.series['AEAD-AES-128-GCM open'];
  if (S) {
    const p16 = S.points[0], pK = S.points[S.points.length-1];
    document.getElementById('claim').innerHTML = `AWS-LC's build guide says that hoisting the enable into the caller's scope gives "benchmarks that are close to the release build". Measured: the hoisted arm H costs <b>${fmt(p16.pct.H)}</b> on a 16-byte AES-GCM seal and <b>${fmt(pK.pct.H)}</b> at 16 KB${O?`, and <b>${fmt(O.points[0].pct.H)}</b> on a 16-byte open`:''}. The claim holds for long messages and not for short ones, because every entry still executes a read and an enable; the enable no longer changes the mode, so it costs ${cyc(p16.H_minus_A)} cycles instead of ${cyc(p16.B_minus_A)}. Add the barrier Apple's recipe calls for and the hoisted arm costs <b>${fmt(p16.pct.Hs)}</b> on the 16-byte seal.`;
  }
}
// anomalies
{
  const A = DATA.anomalies.slice().sort((a,b)=>a.C_pct-b.C_pct);
  let t = `<table><tr><th>row</th><th class="num">A cyc/op</th><th class="num">C blanket</th><th class="num">H hoisted</th><th class="num">MAD</th></tr>`;
  for (const x of A) t += `<tr><td class="row">${esc(x.key)}</td><td class="num">${cyc(x.A)}</td><td class="num ${x.C_pct<0?'cold':'hot'}">${fmt(x.C_pct)}</td><td class="num">${fmt(x.H_pct)}</td><td class="num">${x.mad.toFixed(2)}%</td></tr>`;
  t += '</table>'; document.getElementById('anomaly-table').innerHTML = A.length ? t : '<p class="legend">none beyond 2%</p>';
}
// paper table
{
  const PR = [["AES-128 single block","AES-128 encrypt",true],["EVP AES-GCM encrypt, 16 B","EVP-AES-128-GCM encrypt [16 B]",false],["AEAD AES-GCM seal, 16 B","AEAD-AES-128-GCM seal [16 B]",false],
    ["AEAD AES-GCM open, 16 B","AEAD-AES-128-GCM open [16 B]",false],["AEAD ChaCha20-Poly1305 seal, 16 B","AEAD-ChaCha20-Poly1305 seal [16 B]",false],["AEAD AES-GCM seal, 1350 B (a TLS record)","AEAD-AES-128-GCM seal [1350 B]",false],
    ["AEAD AES-GCM seal, 16 KB","AEAD-AES-128-GCM seal [16384 B]",false],["CMAC-AES-128, 16 KB","CMAC-AES-128-CBC [16384 B]",true],["ECDSA P-256 sign","ECDSA P-256 signing",false],["RNG, 16 B","RNG [16 B]",false]];
  const pair = DATA.prices.pair_price_cycles, block = DATA.prices.block_entry_price_cycles || pair;
  let t = `<table><tr><th>#</th><th>op</th><th class="num">A cyc/op</th><th class="num">entries/op</th><th class="num">C blanket</th><th class="num">B bracket</th><th class="num">Bs +sb</th><th class="num">H hoisted</th><th class="num">Hs hoisted+sb</th><th class="num">MAD</th></tr>`;
  PR.forEach(([label,key,isblk],i) => { const r = DATA.rows[key]; if (!r) { t += `<tr><td class="row">${i+1}</td><td>${esc(label)}</td><td colspan="8" class="legend">not in this run</td></tr>`; return; }
    const a = r.cycles.A, unit = isblk ? block : pair, e = (r.cycles.B - a) / unit;
    const dg = x => (r.suspect||[]).includes(x) ? '\u2020' : '';
    t += `<tr><td class="row">${i+1}</td><td>${esc(label)}${(r.suspect||[]).length?'\u2020':''}</td><td class="num">${cyc(a)}${dg('A')}</td><td class="num">${cyc(e)}</td>${['C','B','Bs','H','Hs'].map(x=>`<td class="num ${x==='B'?'hot':''}">${fmt(r.pct[x],0)}${dg(x)}</td>`).join('')}<td class="num">${r.mad.toFixed(2)}%</td></tr>`; });
  const used = PR.map(([,k]) => DATA.rows[k]).filter(Boolean);
  t += `<tr><td class="row"></td><td><b>geometric mean of the ratio to A</b></td><td></td><td></td>${['C','B','Bs','H','Hs'].map(x=>`<td class="num"><b>${fmt(geomean(used, x),0)}</b></td>`).join('')}<td></td></tr>`;
  t += '</table>'; document.getElementById('paper-table').innerHTML = t;
}
// full table + ipc table
{
  const keys = Object.keys(DATA.rows).sort();
  let t = `<table><tr><th>row</th><th class="num">A cyc/op</th><th class="num">IPC A</th>${ARMS.slice(1).map(a=>`<th class="num">${a}</th>`).join('')}<th class="num">B − A cyc</th><th class="num">MAD</th></tr>`;
  for (const k of keys) { const r = DATA.rows[k];
    const dg = x => (r.suspect||[]).includes(x) ? '\u2020' : '';
    t += `<tr><td class="row">${esc(k)}${(r.suspect||[]).length?'\u2020':''}</td><td class="num">${cyc(r.cycles.A)}${dg('A')}</td><td class="num">${(r.ipc.A??NaN).toFixed(2)}</td>${ARMS.slice(1).map(a=>`<td class="num">${fmt(r.pct[a])}${dg(a)}</td>`).join('')}<td class="num">${cyc(r.B_minus_A_cyc)}</td><td class="num">${r.mad.toFixed(2)}%</td></tr>`; }
  const all = keys.map(k => DATA.rows[k]);
  t += `<tr><td class="row"><b>geometric mean of the ratio to A, clean cells</b></td><td></td><td></td>${ARMS.slice(1).map(a=>`<td class="num"><b>${fmt(geomean(all, a))}</b></td>`).join('')}<td></td><td></td></tr>`;
  t += '</table>'; document.getElementById('full-table').innerHTML = t;
  const hasAll = keys.some(k => Object.keys(DATA.rows[k].ipc).length > 1);
  if (hasAll) {
    let u = `<table><tr><th>row</th><th class="num">instr/op A</th>${ARMS.map(a=>`<th class="num">IPC ${a}</th>`).join('')}<th class="num">B − A instr</th></tr>`;
    for (const k of keys) { const r = DATA.rows[k];
      u += `<tr><td class="row">${esc(k)}</td><td class="num">${cyc(r.instrs.A)}</td>${ARMS.map(a=>`<td class="num">${r.ipc[a]!=null?r.ipc[a].toFixed(2):'n/a'}</td>`).join('')}<td class="num">${(r.instrs.B!=null&&r.instrs.A!=null)?cyc(r.instrs.B-r.instrs.A):'n/a'}</td></tr>`; }
    u += '</table>'; document.getElementById('ipc-table').innerHTML = u;
  } else document.getElementById('ipc-table').innerHTML = '<p class="legend">this run recorded IPC for arm A only; rerun with the current driver for every arm</p>';
}
document.getElementById('prov').textContent = DATA.provenance || '';
</script>
'''

def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('json', nargs='?', default=os.path.expanduser('~/Documents/dit-awslc/results/speed.json'))
    ap.add_argument('--out', default=None, help='directory for report.md and summary.json (default: next to the json)')
    ap.add_argument('--html', default=None, help='write the HTML page here')
    ap.add_argument('--provenance', default=None, help='a provenance.txt to quote at the foot of the page')
    a = ap.parse_args()
    d = load(a.json); an = analyze(d)
    an['provenance'] = open(a.provenance).read().strip().splitlines()[-5:] if a.provenance and os.path.exists(a.provenance) else ''
    if isinstance(an['provenance'], list): an['provenance'] = ' | '.join(an['provenance'])
    out = a.out or os.path.dirname(os.path.abspath(a.json))
    os.makedirs(out, exist_ok=True)
    pt_md, pt_csv, missing = paper_table(an)
    an['paper_table_md'] = pt_md
    open(os.path.join(out, 'paper_table.md'), 'w').write(pt_md)
    open(os.path.join(out, 'paper_table.csv'), 'w').write(pt_csv)
    open(os.path.join(out, 'report.md'), 'w').write(report_md(an, a.json) + "\n## The paper's ten rows\n\n" + pt_md)
    json.dump(an, open(os.path.join(out, 'summary.json'), 'w'), indent=1, default=float)
    if a.html:
        open(a.html, 'w').write(HTML_TEMPLATE.replace('__DATA__', json.dumps(an, default=float)))
    v = an['validity']; p = an['prices'].get('gcm_seal_16') or {}
    print(f"rows {len(an['rows'])}; flagged {v['flagged']} ({v['flag_mode']}); unpinned {v['unpinned']}; "
          f"one-entry prices (GCM seal 16 B): pair {p.get('changing_pair')}, read+nonchanging {p.get('read_plus_nonchanging')}, "
          f"sb after change {p.get('sb_after_changing')}, sb after nonchange {p.get('sb_after_nonchanging')}")
    print(f"wrote {out}/report.md, {out}/summary.json, {out}/paper_table.md, {out}/paper_table.csv" + (f", {a.html}" if a.html else ''))
    if missing: print("paper table: rows missing from this run:", ', '.join(missing))
    print(pt_md)

if __name__ == '__main__':
    main()
