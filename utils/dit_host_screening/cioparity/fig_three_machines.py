#!/usr/bin/env python3
"""DIT overhead on libsodium: baseline, coarse-grain and fine-grain DIT, per machine.

The paper figure for experiment 09. Grouped bars, one group per CIO benchmark:
coarse-grain DIT (blanket: the mode set once for the whole program) on each
machine, then fine-grain DIT (region placement, the shipped pass) on each
machine. The baseline is the 1x line, not a bar - a bar that is 1.0 by
definition carries no information. Machines are Apple M4, Apple M5,
and ExpeDITe - the gem5 Neoverse-V2 model - under both MSR DIT implementations,
serialized and renamed. y is cycles per operation as a multiple of each
machine's own baseline.

Why cycles and not IPC: the pass adds only its switches (about 1-2% more
instructions, blanket adds none), so nearly all of the ratio is cycles, and an
IPC ratio would be this chart inverted. Absolute silicon IPC is not honest here
in any case - the kperf counter reads leave ~13,000 instructions inside every
region and that offset was never measured. Cycles were corrected (cntvct).

Reads the repo's data files directly, so it regenerates from
paper_experiments/09-libsodium-cio-parity/data/ with no intermediate:

  m4_results_ratios.csv, m5_results_ratios.csv   cntvct rows: C_blanket_x, P_pass_x
  gem5_switch_model.csv, gem5_argon2id.csv        cycles_per_op by arm and cfg

Needs matplotlib. The system Python here is PEP 668-locked, so use a venv:
  python3 -m venv /tmp/mplvenv && /tmp/mplvenv/bin/pip install matplotlib
  /tmp/mplvenv/bin/python utils/dit_host_screening/cioparity/fig_three_machines.py

Writes three-machines-region.{png,pdf} next to the other figures.
"""
import csv, os, sys, pathlib
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

R = pathlib.Path(__file__).resolve().parents[3]
D = R / "paper_experiments/09-libsodium-cio-parity/data"
OUT = R / "paper_experiments/09-libsodium-cio-parity/figures/three-machines-region"

BENCH = [("ed25519", "ed25519\nsign"), ("chacha20_poly1305_encrypt", "chacha20-poly1305\nencrypt"),
         ("chacha20_poly1305_decrypt", "chacha20-poly1305\ndecrypt"), ("aesni256gcm_encrypt", "aes256-gcm\nencrypt"),
         ("aesni256gcm_decrypt", "aes256-gcm\ndecrypt"), ("argon2id", "argon2id")]
# (key, legend label, colour, hollow). Hues are the validated categorical slots
# 1-3; the renamed switch is the same instrument with one mechanism removed and
# is drawn hollow in the same hue rather than given a fourth colour.
MACH = [("M4", "M4 silicon", "#2a78d6", False), ("M5", "M5 silicon", "#eb6834", False),
        ("ser", "ExpeDITe (serialized)", "#1baf7a", False), ("ren", "ExpeDITe (renamed)", "#1baf7a", True)]


def silicon(host):
    out = {}
    for r in csv.DictReader(open(D / f"{host}_results_ratios.csv")):
        if r["timer"] == "cntvct":
            out[r["benchmark"]] = {"blanket": float(r["C_blanket_x"]), "pass": float(r["P_pass_x"])}
    return out


def gem5():
    cyc = {}
    for f in ("gem5_switch_model.csv", "gem5_argon2id.csv"):
        p = D / f
        if not p.exists():
            continue
        for r in csv.DictReader(open(p)):
            if r.get("cycles_per_op"):
                cyc[(r["bench"], r["arm"], r["cfg"])] = float(r["cycles_per_op"])
    out = {}
    for b, _ in BENCH:
        try:
            out[b] = {
                "ser": {"blanket": cyc[(b, "blanket", "serdit")] / cyc[(b, "base", "serdit")],
                        "pass": cyc[(b, "taint", "serdit")] / cyc[(b, "base", "serdit")]},
                "ren": {"blanket": cyc[(b, "blanket", "spec")] / cyc[(b, "base", "spec")],
                        "pass": cyc[(b, "taint", "spec")] / cyc[(b, "base", "spec")]},
            }
        except KeyError as e:
            sys.exit(f"gem5 data missing for {b}: {e}")
    return out


M4, M5, G5 = silicon("m4"), silicon("m5"), gem5()
V = {"M4": M4, "M5": M5, "ser": {b: G5[b]["ser"] for b in G5}, "ren": {b: G5[b]["ren"] for b in G5}}

INK, MUTED, FAINT, GRID, BASE, SURF = "#11171C", "#5A6670", "#8695A0", "#E4E9EB", "#9AA6AE", "#FFFFFF"
plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 9, "axes.labelcolor": MUTED,
                     "xtick.color": MUTED, "ytick.color": FAINT, "text.color": INK})
fig, ax = plt.subplots(figsize=(12.4, 4.8), dpi=300)
fig.patch.set_facecolor(SURF); ax.set_facecolor(SURF)

n = len(BENCH)
bw, g, G = 0.082, 0.012, 0.04           # bar width, gap within a sub-cluster, gap between sub-clusters
offs, x = [], 0.0
for i in range(4):
    offs.append(x); x += bw + (g if i < 3 else G)
for i in range(4):
    offs.append(x); x += bw + g
span = x - g
offs = [o - span / 2 + bw / 2 for o in offs]


def bar(xc, v, col, hollow):
    if hollow:
        return ax.bar([xc], [v], width=bw, color=SURF, edgecolor=col, linewidth=1.3, zorder=3)
    return ax.bar([xc], [v], width=bw, color=col, linewidth=0, zorder=3)


for gi, (b, lab) in enumerate(BENCH):
    for mi, (k, _, col, hol) in enumerate(MACH):
        bar(gi + offs[mi], V[k][b]["blanket"], col, hol)
    for mi, (k, _, col, hol) in enumerate(MACH):
        v = V[k][b]["pass"]
        bar(gi + offs[4 + mi], v, col, hol)
        ax.text(gi + offs[4 + mi], v + 0.08, f"{v:.2f}×", ha="center", va="bottom", rotation=90,
                fontsize=6.0, color=MUTED, family="DejaVu Sans Mono", zorder=4)
    ax.text(gi + (offs[0] + offs[3]) / 2, -0.22, "coarse grain", ha="center", va="top", fontsize=5.8, color=FAINT, clip_on=False)
    ax.text(gi + (offs[4] + offs[7]) / 2, -0.22, "fine grain", ha="center", va="top", fontsize=5.8, color=FAINT, clip_on=False)

ax.axhline(1.0, color=BASE, lw=0.9, zorder=2)
ax.set_ylim(0, 6.2); ax.set_yticks(range(0, 7))
ax.set_yticklabels([f"{t}×" for t in range(0, 7)], family="DejaVu Sans Mono", fontsize=7.5)
ax.set_xticks(range(n)); ax.set_xticklabels([lab for _, lab in BENCH], fontsize=7.8); ax.tick_params(axis="x", pad=22)
ax.set_xlim(-0.6, n - 0.4)
ax.yaxis.grid(True, color=GRID, lw=0.7, zorder=0); ax.set_axisbelow(True)
for s in ("top", "right", "left"):
    ax.spines[s].set_visible(False)
ax.spines["bottom"].set_color(GRID); ax.tick_params(axis="both", length=0)
ax.set_ylabel("Slowdown vs. baseline", fontsize=8.5, color=MUTED)
handles = [
    Patch(facecolor=SURF, edgecolor=c, linewidth=1.3, label=nm) if h else Patch(facecolor=c, label=nm)
    for _, nm, c, h in MACH]
ax.legend(handles=handles, frameon=False, fontsize=7.6, loc="upper left", bbox_to_anchor=(0.0, 1.02),
          ncol=4, handlelength=1.1, handleheight=1.0, columnspacing=1.5)
ax.set_title("DIT overhead on libsodium", loc="left", fontsize=10, fontweight="bold", pad=22, color=INK)
fig.tight_layout()
fig.savefig(str(OUT) + ".png", dpi=300, facecolor=SURF)
fig.savefig(str(OUT) + ".pdf", facecolor=SURF)
print(f"wrote {OUT}.png and .pdf")
