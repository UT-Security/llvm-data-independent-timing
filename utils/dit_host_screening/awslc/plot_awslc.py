#!/usr/bin/env python3
"""Experiment 14: bar charts from the analysis's summary.json.

  plot_awslc.py summary.json [--out DIR]

Writes into DIR (default: next to the JSON):
  paper_rows.png    the paper's ten rows, grouped bars for C, B, Bs, H, Hs as the ratio to A (1.00x = no cost), plus
                    the geometric mean group (clean cells); a suspect cell (median known wrong) is
                    hatched, capped at the axis top and labelled with its true value
  all_rows.png      every row, one panel per arm, horizontal bars in table order, symmetric-log
                    axis so a 16-byte AES block at +366% and a hash at -1% share a scale; suspect
                    cells hatched and labelled with their value
  geomeans.png      the geometric means: paper rows and all rows, every cell and clean cells
Nothing is left out of a chart; the hatch is the only thing a suspect cell gets.
"""
import json, os, sys, argparse, math
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter

ARMS = ['C', 'B', 'Bs', 'H', 'Hs']
# the arm names used in every figure (the user's terminology): "hoist", never "hoisted"
LABEL = {'C': 'C coarse', 'B': 'B AWS default', 'Bs': 'Bs AWS default + sb', 'H': 'H AWS hoist', 'Hs': 'Hs AWS hoist + sb'}
COLOR = {'C': '#0F6E74', 'B': '#B5473A', 'Bs': '#7A2E24', 'H': '#C58A1E', 'Hs': '#7D5A12'}
PAPER = [("AES-128 single block", "AES-128 encrypt"), ("EVP AES-GCM encrypt, 16 B", "EVP-AES-128-GCM encrypt [16 B]"),
         ("AEAD AES-GCM seal, 16 B", "AEAD-AES-128-GCM seal [16 B]"), ("AEAD AES-GCM open, 16 B", "AEAD-AES-128-GCM open [16 B]"),
         ("ChaCha20-Poly1305 seal, 16 B", "AEAD-ChaCha20-Poly1305 seal [16 B]"), ("AEAD AES-GCM seal, 1350 B", "AEAD-AES-128-GCM seal [1350 B]"),
         ("AEAD AES-GCM seal, 16 KB", "AEAD-AES-128-GCM seal [16384 B]"), ("CMAC-AES-128, 16 KB", "CMAC-AES-128-CBC [16384 B]"),
         ("ECDSA P-256 sign", "ECDSA P-256 signing"), ("RNG, 16 B", "RNG [16 B]")]
pctfmt = FuncFormatter(lambda v, _: f"{v:+.0f}%")
ratfmt = FuncFormatter(lambda v, _: f"{v:g}x")

def ratio(pct):
    return pct / 100 + 1 if pct == pct else float('nan')

def ratio_label(v):
    return f"{v:.2f}x" if abs(v) < 10 else f"{v:.1f}x" if abs(v) < 1e3 else f"{v:.2g}x"

def geomean(rows, arm, include_suspect):
    logs = [math.log(r['cycles'][arm] / r['cycles']['A']) for r in rows
            if arm in r['cycles'] and (include_suspect or (arm not in r.get('suspect', []) and 'A' not in r.get('suspect', [])))]
    return (math.exp(sum(logs) / len(logs)) - 1) * 100 if logs else float('nan')

def paper_chart(an, out):
    rows = [(lab, an['rows'].get(k)) for lab, k in PAPER]
    rows = [(lab, r) for lab, r in rows if r]
    labels = [lab for lab, _ in rows] + ['geometric mean\n(clean cells)']
    n, w = len(labels), 0.16
    fig, ax = plt.subplots(figsize=(14, 6.2))
    # bars are the ratio of cycles per op to the unhardened arm, drawn from 1.00x (no cost). The axis is set by
    # the clean cells; a suspect cell (its median corrupted by the counter fault) is drawn capped at the top,
    # hatched, and labelled with its true value, so it is visible without flattening the rest
    clean = [ratio(r['pct'][a]) for _, r in rows for a in ARMS if a not in r.get('suspect', []) and r['pct'].get(a) == r['pct'].get(a)]
    top = max(clean) * 1.12 if clean else 5
    lo = min(1.0, min(clean)) if clean else 1.0
    # a label is lifted one line when the neighbouring bar's label would overprint it (values within 4% of the axis)
    near = 0.04 * (top - lo)
    for i, arm in enumerate(ARMS):
        vals = [ratio(r['pct'].get(arm, float('nan'))) for _, r in rows] + [ratio(geomean([r for _, r in rows], arm, False))]
        sus = [arm in r.get('suspect', []) for _, r in rows] + [False]
        xs = [j + (i - 2) * w for j in range(n)]
        drawn = [min(v, top) if v == v else v for v in vals]
        bars = ax.bar(xs, [d - 1 for d in drawn], w, bottom=1, label=LABEL[arm], color=COLOR[arm], edgecolor='white', linewidth=0.5)
        for j, (b, v, d, s) in enumerate(zip(bars, vals, drawn, sus)):
            if s: b.set_hatch('////'); b.set_edgecolor('black')
            if v == v:
                prev = ratio(rows[j][1]['pct'].get(ARMS[i - 1], float('nan'))) if i and j < len(rows) else float('nan')
                lift = 8 if prev == prev and abs(prev - v) < near and not (i % 2) else 0
                txt = ratio_label(v) + (' \u2020' if s else '')
                ax.annotate(txt, (b.get_x() + b.get_width() / 2, d), ha='center', va='bottom' if v >= 1 else 'top',
                            fontsize=7, xytext=(0, 2 + lift if v >= 1 else -2 - lift), textcoords='offset points', color='#B5473A' if s else '#333')
    ax.set_ylim(lo - (top - lo) * 0.05, top * 1.04)
    ax.axhline(1, color='#888', linewidth=0.8)
    ax.set_xticks(range(n)); ax.set_xticklabels(labels, rotation=28, ha='right', fontsize=9)
    ax.yaxis.set_major_formatter(ratfmt); ax.set_ylabel('cycles per operation, ratio to unhardened (A); 1.00x = no cost')
    v = an['validity']
    ax.set_title(f"AWS-LC's DIT bracket on `bssl speed`: Apple M4, CPU {v.get('pin_cpu')} hard-bound, {v.get('timeout_ms')} ms windows, medians of {v.get('reps')} reps", fontsize=11)
    ax.legend(ncol=5, fontsize=9, frameon=False, loc='upper right')
    ax.grid(axis='y', color='#ddd', linewidth=0.6); ax.set_axisbelow(True)
    for s in ('top', 'right'): ax.spines[s].set_visible(False)
    fig.tight_layout(); fig.savefig(os.path.join(out, 'paper_rows.png'), dpi=160); plt.close(fig)

def all_rows_chart(an, out):
    keys = sorted(an['rows']); rows = [an['rows'][k] for k in keys]
    fig, axes = plt.subplots(1, len(ARMS), figsize=(22, 0.22 * len(keys) + 2), sharey=True)
    ys = list(range(len(keys)))
    for ax, arm in zip(axes, ARMS):
        vals = [r['pct'].get(arm, float('nan')) for r in rows]
        bars = ax.barh(ys, vals, color=COLOR[arm], height=0.75)
        for b, r, val in zip(bars, rows, vals):
            if arm in r.get('suspect', []) or 'A' in r.get('suspect', []):
                b.set_hatch('////'); b.set_edgecolor('black'); b.set_facecolor('#eee')
                ax.annotate(f"{val:+.3g}% suspect", (0, b.get_y() + b.get_height() / 2), fontsize=6, va='center', ha='left', color='#B5473A')
        ax.set_xscale('symlog', linthresh=10); ax.xaxis.set_major_formatter(pctfmt)
        ax.axvline(0, color='#888', linewidth=0.8); ax.set_title(LABEL[arm], fontsize=10)
        ax.grid(axis='x', color='#ddd', linewidth=0.6); ax.set_axisbelow(True)
        for s in ('top', 'right'): ax.spines[s].set_visible(False)
        ga, gc = geomean(rows, arm, True), geomean(rows, arm, False)
        ax.set_xlabel(f"geomean {ga:+.1f}% every cell, {gc:+.1f}% clean", fontsize=8)
    axes[0].set_yticks(ys); axes[0].set_yticklabels(keys, fontsize=6.5); axes[0].invert_yaxis()
    fig.suptitle('every row, percent over A (symmetric-log axis); hatched = a cell whose median the backward-counter fault corrupted, kept and shown', fontsize=11)
    fig.tight_layout(); fig.savefig(os.path.join(out, 'all_rows.png'), dpi=140); plt.close(fig)

def geomean_chart(an, out):
    paper = [an['rows'][k] for _, k in PAPER if k in an['rows']]; allr = list(an['rows'].values())
    sets = [('paper rows, all cells', paper, True), ('paper rows, clean cells', paper, False),
            ('all rows, every cell', allr, True), ('all rows, clean cells', allr, False)]
    fig, ax = plt.subplots(figsize=(10, 4.6)); w = 0.16
    for i, arm in enumerate(ARMS):
        vals = [ratio(geomean(rs, arm, inc)) for _, rs, inc in sets]
        xs = [j + (i - 2) * w for j in range(len(sets))]
        bars = ax.bar(xs, [v - 1 for v in vals], w, bottom=1, label=LABEL[arm], color=COLOR[arm])
        for b, v in zip(bars, vals): ax.annotate(ratio_label(v), (b.get_x() + w / 2, v), ha='center', va='bottom' if v >= 1 else 'top', fontsize=7, xytext=(0, 2 if v >= 1 else -2), textcoords='offset points')
    ax.set_xticks(range(len(sets))); ax.set_xticklabels([s for s, _, _ in sets], fontsize=9)
    hi = max(ratio(geomean(rs, a, inc)) for _, rs, inc in sets for a in ARMS); lo = min(1.0, min(ratio(geomean(rs, a, inc)) for _, rs, inc in sets for a in ARMS))
    ax.set_ylim(lo - (hi - lo) * 0.06, lo + (hi - lo) * 1.22)   # headroom for the legend above the tallest bar
    ax.axhline(1, color='#888', linewidth=0.8); ax.yaxis.set_major_formatter(ratfmt); ax.set_ylabel('geometric mean of arm / A (1.00x = no cost)')
    ax.legend(ncol=5, fontsize=8, frameon=False); ax.grid(axis='y', color='#ddd', linewidth=0.6); ax.set_axisbelow(True)
    for s in ('top', 'right'): ax.spines[s].set_visible(False)
    ax.set_title('geometric means of the ratio to A', fontsize=11)
    fig.tight_layout(); fig.savefig(os.path.join(out, 'geomeans.png'), dpi=160); plt.close(fig)

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('json'); ap.add_argument('--out', default=None); a = ap.parse_args()
    an = json.load(open(a.json)); out = a.out or os.path.dirname(os.path.abspath(a.json)); os.makedirs(out, exist_ok=True)
    paper_chart(an, out); all_rows_chart(an, out); geomean_chart(an, out)
    print(f"wrote {out}/paper_rows.png, {out}/all_rows.png, {out}/geomeans.png")

if __name__ == '__main__':
    main()
