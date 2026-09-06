#!/usr/bin/env python3
"""Experiment 02 figures, from the canonical gem5 sweep (q=0.75 lane, 5 stack offsets).

  overhead-vs-secret-fraction   x = secret fraction, y = IPC overhead vs unhardened (IPC_unhardened / IPC_arm - 1), one line per arm
  predictions-suppressed-vs-L   same x, y = load-value predictions per request under each arm (symlog, so blanket's zero is on the plot)
Both figures share one x axis, secret fraction; the L behind each point is in the CSV.

Reads the repo's data files directly, so it regenerates from
paper_experiments/02-libsodium-signed-lookup/data/ with no intermediate:
  gem5_arms.csv                   the canonical sweep (median of 5 offsets)
  gem5_value_predictor_by_arm.csv what the value predictor did under each arm
Needs matplotlib. The system Python here is PEP 668-locked, so use a venv:
  python3 -m venv /tmp/mplvenv && /tmp/mplvenv/bin/pip install matplotlib
  /tmp/mplvenv/bin/python utils/dit_host_screening/signed_lookup/fig_exp02.py
Writes the four files under paper_experiments/02-libsodium-signed-lookup/figures/.
Figure 1 is in IPC: for blanket that equals the cycles ratio (same instruction
stream); for the pass it sits ~2 points under the cycles ratio in data/gem5_arms.csv
because its switches add instructions that run at full IPC. Both figures are median
of 5 stack offsets (figure 2: offset-0 predictor counts, which are deterministic
across offsets); figure 2 counts loads only, ALU predictions excluded; blanket makes none, and
selective placement keeps the public lane's and loses only the AEAD's ~117.
"""
import csv, pathlib, sys
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from matplotlib.ticker import FuncFormatter

R = pathlib.Path(__file__).resolve().parents[3]
G = R / "paper_experiments/02-libsodium-signed-lookup/data"
FIG = R / "paper_experiments/02-libsodium-signed-lookup/figures"

INK, MUTED, FAINT, GRID, BASE, SURF = "#11171C", "#5A6670", "#8695A0", "#E4E9EB", "#9AA6AE", "#FFFFFF"
BLUE, ORANGE = "#2a78d6", "#eb6834"          # validated categorical slots 1 and 2
plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 9, "axes.labelcolor": MUTED,
                     "xtick.color": MUTED, "ytick.color": MUTED, "text.color": INK})

def style(ax):
    ax.set_facecolor(SURF)
    for s in ("top", "right"): ax.spines[s].set_visible(False)
    for s in ("left", "bottom"): ax.spines[s].set_color(GRID)
    ax.yaxis.grid(True, color=GRID, lw=0.7, zorder=0); ax.set_axisbelow(True)
    ax.tick_params(axis="both", length=0)

def xaxis_secret_fraction(ax, f, Ls):
    """Both figures share this one axis. The knob L that produced each secret
    fraction is in data/gem5_arms.csv, not on the plot."""
    ax.set_xlim(-2, 102); ax.set_xticks([0, 20, 40, 60, 80, 100])
    ax.set_xticklabels([f"{t}%" for t in (0, 20, 40, 60, 80, 100)])
    ax.set_xlabel("Secret fraction of the request (cycles in the AEAD lane)")

rows = [r for r in csv.DictReader(l for l in open(G / "gem5_arms.csv") if not l.startswith("#"))]
Ls = sorted({int(r["L"]) for r in rows})
f = {int(r["L"]): float(r["f_secret_pct"]) for r in rows if r["arm"] == "nodit"}
row = lambda L, arm, sw: next(r for r in rows if int(r["L"]) == L and r["arm"] == arm and r["switch"] == sw)
# IPC overhead: unhardened IPC over the arm's IPC, minus one. Positive = slower.
# Same as the cycles ratio for blanket (identical instruction stream); ~2 points
# below it for the pass, whose switches add instructions that run at full IPC.
ov = lambda arm, sw: [(float(row(L, "nodit", "-")["ipc"]) / float(row(L, arm, sw)["ipc"]) - 1) * 100 for L in Ls]
req = {int(r["L"]): int(r["requests"]) for r in rows if r["arm"] == "nodit"}
q4 = int(rows[0]["pred_q4"])

# ---------------------------------------------------------------- fig 1
fig, ax = plt.subplots(figsize=(6.4, 3.9), dpi=300); fig.patch.set_facecolor(SURF); style(ax)
xs = [f[L] for L in Ls]
# Legend vocabulary matches the experiment 09 figure: coarse = blanket DIT,
# fine = region placement; ExpeDITe is the gem5 model under each MSR DIT implementation.
series = [("coarse", ov("blanket", "-"), BLUE, "-", "o", BLUE),
          ("ExpeDITe (serialized)", ov("pass", "serialising"), ORANGE, "-", "o", ORANGE),
          ("ExpeDITe (renamed)", ov("pass", "renamed"), ORANGE, (0, (4, 2)), "o", SURF)]
for name, ys, col, ls, mk, mfc in series:
    ax.plot(xs, ys, color=col, lw=2, ls=ls, marker=mk, ms=6, mfc=mfc, mec=col, mew=1.6, zorder=3)
ax.axhline(0, color=BASE, lw=0.9, zorder=2)
xaxis_secret_fraction(ax, f, Ls)
ax.set_ylabel("IPC overhead vs unhardened")
ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:+.0f}%"))
# No title on the figure: the caption carries it in the paper, and a title baked
# into the image duplicates the caption and cannot be edited with the text.
handles = [Line2D([], [], color=c, lw=2, ls=ls, marker="o", ms=6, mfc=mfc, mec=c, mew=1.6, label=n) for n, _, c, ls, _, mfc in series]
ax.legend(handles=handles, frameon=False, fontsize=7.4, loc="upper right", bbox_to_anchor=(0.72, 1.0))
fig.tight_layout(); fig.savefig(FIG / "overhead-vs-secret-fraction.png", dpi=300, facecolor=SURF); fig.savefig(FIG / "overhead-vs-secret-fraction.pdf", facecolor=SURF)

# ---------------------------------------------------------------- fig 2
vp = [r for r in csv.DictReader(l for l in open(G / "gem5_value_predictor_by_arm.csv") if not l.startswith("#"))]
lp = lambda L, arm: next(int(r["load_predictions"]) / int(r["requests"]) for r in vp if int(r["L"]) == L and r["arm"] == arm)
made = {arm: [lp(L, arm) for L in Ls] for arm in ("nodit", "pass", "blanket")}
fig, ax = plt.subplots(figsize=(6.4, 3.9), dpi=300); fig.patch.set_facecolor(SURF); style(ax)
xs = [f[L] for L in Ls]
ax.plot(xs, made["nodit"], color=BASE, lw=2, marker="o", ms=6, mfc=BASE, mec=BASE, mew=1.6, zorder=3)
ax.plot(xs, made["pass"], color=ORANGE, lw=2, ls=(0, (4, 2)), marker="o", ms=6, mfc=SURF, mec=ORANGE, mew=1.6, zorder=4)
ax.plot(xs, made["blanket"], color=BLUE, lw=2, marker="o", ms=6, mfc=BLUE, mec=BLUE, mew=1.6, zorder=3)
ax.set_yscale("symlog", linthresh=100, linscale=0.6)
ax.set_yticks([0, 100, 1000, 10000]); ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:,.0f}")); ax.minorticks_off()
ax.set_ylim(-8, 40000)
xaxis_secret_fraction(ax, f, Ls)
ax.set_ylabel("Load-value predictions per request")
handles = [Line2D([], [], color=BASE, lw=2, marker="o", ms=6, mfc=BASE, mec=BASE, label="unhardened"),
           Line2D([], [], color=ORANGE, lw=2, ls=(0, (4, 2)), marker="o", ms=6, mfc=SURF, mec=ORANGE, label="ExpeDITe"),
           Line2D([], [], color=BLUE, lw=2, marker="o", ms=6, mfc=BLUE, mec=BLUE, label="coarse")]
ax.legend(handles=handles, frameon=False, fontsize=7.4, loc="upper right")
fig.tight_layout(); fig.savefig(FIG / "predictions-suppressed-vs-L.png", dpi=300, facecolor=SURF); fig.savefig(FIG / "predictions-suppressed-vs-L.pdf", facecolor=SURF)
print("wrote", FIG / "overhead-vs-secret-fraction.{png,pdf}", FIG / "predictions-suppressed-vs-L.{png,pdf}")
print("load predictions/req:", {a: [round(v) for v in made[a]] for a in made})
