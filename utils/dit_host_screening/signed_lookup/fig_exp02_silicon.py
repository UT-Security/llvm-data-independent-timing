#!/usr/bin/env python3
"""Experiment 02's silicon figures: the same crossover on gem5 and on an Apple M4.

  crossover-gem5-vs-m4      the headline. Two panels, one per machine, same axes
                            and same two arms: x = secret fraction of the
                            request, y = IPC overhead vs unhardened. Both
                            machines cross; they cross in different places.
                            The orange arm is THE PASS. `ExpeDITe` is the gem5
                            Neoverse-V2 model's name, not the pass's, so it sits
                            in the left panel's title; the right panel is
                            hardware and has no ExpeDITe in it.
  predictability-gem5-vs-m4 why. Blanket's cost to the PUBLIC lane against q,
                            the fraction of iterations that read the record
                            header, for gem5 and for both Apple lanes.
  m4-predictor-width        the mechanism, in one step function: the same lane
                            with the header holding N one-bits. Everything up to
                            36 is predicted and costs blanket +34%; 37 and above
                            is not predicted and costs nothing.

Reads only the committed CSVs, so it regenerates from data/ with no intermediate:
  gem5_arms.csv, gem5_predictability_sweep.csv   the simulator half
  m4_arms.csv, m4_predictability_sweep.csv,      the silicon half, written by
  m4_header_width.csv                            silicon/derive_exp02_m4.py

Needs matplotlib:
  python3 utils/dit_host_screening/signed_lookup/fig_exp02_silicon.py
Writes into paper_experiments/02-libsodium-signed-lookup/figures/.

y is IPC overhead on every panel, which is what fig_exp02.py plots and is not
the cycles ratio: for blanket the two are the same number (identical instruction
stream), for the pass the IPC figure sits under the cycles one because its mode
writes are extra instructions that retire at full rate.
"""
import csv
import pathlib

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from matplotlib.ticker import FuncFormatter

R = pathlib.Path(__file__).resolve().parents[3]
G = R / "paper_experiments/02-libsodium-signed-lookup/data"
FIG = R / "paper_experiments/02-libsodium-signed-lookup/figures"

INK, MUTED, FAINT, GRID, BASE, SURF = "#11171C", "#5A6670", "#8695A0", "#E4E9EB", "#9AA6AE", "#FFFFFF"
BLUE, ORANGE, GREEN = "#2a78d6", "#eb6834", "#1f9e6e"
plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 9, "axes.labelcolor": MUTED,
                     "xtick.color": MUTED, "ytick.color": MUTED, "text.color": INK})


def style(ax):
    ax.set_facecolor(SURF)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    for s in ("left", "bottom"):
        ax.spines[s].set_color(GRID)
    ax.yaxis.grid(True, color=GRID, lw=0.7, zorder=0)
    ax.set_axisbelow(True)
    ax.tick_params(axis="both", length=0)


def rows(name):
    with open(G / name) as fh:
        return list(csv.DictReader(l for l in fh if not l.startswith("#")))


def secret_axis(ax):
    """Both machines share this one axis. The L behind each point is in the CSV."""
    ax.set_xlim(-2, 102)
    ax.set_xticks([0, 20, 40, 60, 80, 100])
    ax.set_xticklabels([f"{t}%" for t in (0, 20, 40, 60, 80, 100)])


def crossing(xs, a, b):
    """Where curve a meets curve b, linear in x. xs must be sorted ascending.

    Returned as the secret fraction at which selective placement stops being the
    cheaper arm -- the one number the figure exists to produce. None if the two
    curves never cross over the measured range, which is itself a result (the
    wide Apple lane).
    """
    d = [ai - bi for ai, bi in zip(a, b)]
    for k in range(len(d) - 1):
        if d[k] == 0:
            return xs[k]
        if d[k] * d[k + 1] < 0:
            t = d[k] / (d[k] - d[k + 1])
            return xs[k] + t * (xs[k + 1] - xs[k])
    return None


# ------------------------------------------------------------------ gem5 series
g = rows("gem5_arms.csv")
gL = sorted({int(r["L"]) for r in g})
gf = {int(r["L"]): float(r["f_secret_pct"]) for r in g if r["arm"] == "nodit"}
grow = lambda L, arm, sw: next(r for r in g if int(r["L"]) == L and r["arm"] == arm
                               and r["switch"] == sw)
gipc = lambda arm, sw: [(float(grow(L, "nodit", "-")["ipc"]) / float(grow(L, arm, sw)["ipc"]) - 1) * 100
                        for L in gL]

# ------------------------------------------------------------------ M4 series
m = rows("m4_arms.csv")


def m4(lane, arm):
    rs = [r for r in m if r["lane"] == lane and r["arm"] == arm]
    rs.sort(key=lambda r: int(r["L"]))
    return ([float(r["f_secret_pct"]) for r in rs],
            [float(r["ipc_ovh_pct"]) for r in rs])


mf, mblanket = m4("narrow", "blanket")
_mf2, mpass = m4("narrow", "pass")

# ============================================================ fig 1: the headline
fig, axes = plt.subplots(1, 2, figsize=(8.2, 3.7), dpi=300)
fig.patch.set_facecolor(SURF)

for ax, title, xs, blank, ser, ren in [
    (axes[0], "gem5 Neoverse-V2 FDP — the ExpeDITe model\nEVES + VTAGE: predicts the 62-bit header",
     [gf[L] for L in gL], gipc("blanket", "-"), gipc("pass", "serialising"),
     gipc("pass", "renamed")),
    (axes[1], "Apple M4 — shipping silicon\nload value predictor: 36 bits, measured",
     mf, mblanket, mpass, None),
]:
    style(ax)
    ax.plot(xs, blank, color=BLUE, lw=2, marker="o", ms=5, mfc=BLUE, mec=BLUE, zorder=3)
    ax.plot(xs, ser, color=ORANGE, lw=2, marker="o", ms=5, mfc=ORANGE, mec=ORANGE, zorder=3)
    if ren is not None:
        ax.plot(xs, ren, color=ORANGE, lw=2, ls=(0, (4, 2)), marker="o", ms=5,
                mfc=SURF, mec=ORANGE, mew=1.4, zorder=3)
    ax.axhline(0, color=BASE, lw=0.9, zorder=2)
    secret_axis(ax)
    # Two lines: the machine, then what its value predictor is. The second line
    # is the whole reason the two panels differ, so it is on the figure.
    head, _, sub = title.partition("\n")
    ax.set_title(head, fontsize=9, color=INK, pad=16, fontweight="bold")
    ax.annotate(sub, xy=(0.5, 1.015), xycoords="axes fraction", fontsize=7.6,
                color=MUTED, ha="center", va="bottom")
    ax.set_xlabel("Secret fraction of the request", fontweight="bold")
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:+.0f}"))
    # The crossing is the point of the figure, so it is ON the figure.
    order = sorted(range(len(xs)), key=lambda k: xs[k])
    xo = [xs[k] for k in order]
    fstar = crossing(xo, [ser[k] for k in order], [blank[k] for k in order])
    if fstar is not None:
        ax.axvline(fstar, color=FAINT, lw=0.9, ls=(0, (2, 2)), zorder=1)
        ax.annotate(f"crossover\nf* = {fstar:.0f}%", xy=(fstar, ax.get_ylim()[1]),
                    xytext=(fstar + 3, ax.get_ylim()[1] * 0.80),
                    fontsize=7.6, color=MUTED, ha="left", va="top")

# BOTH panels carry the y label, because they do NOT share a scale: the pass
# reaches +25% on gem5 and +113% here, since a serialising `msr DIT` costs 33.5
# cycles against a ~1,100-cycle AEAD. Hiding that behind one unlabelled axis
# would make the two curves look more alike than they are.
for ax in axes:
    ax.set_ylabel("IPC overhead vs unhardened (%)", fontweight="bold")
handles = [
    Line2D([], [], color=BLUE, lw=2, marker="o", ms=5, label="blanket DIT"),
    # The arm is THE PASS. `ExpeDITe` names the gem5 Neoverse-V2 model, not the
    # compiler pass, so it belongs in the left panel's title and nowhere near a
    # curve on the right one -- there is no ExpeDITe on an M4.
    Line2D([], [], color=ORANGE, lw=2, marker="o", ms=5,
           label="the pass — serialised MSR DIT"),
    Line2D([], [], color=ORANGE, lw=2, ls=(0, (4, 2)), marker="o", ms=5, mfc=SURF,
           mec=ORANGE, mew=1.4, label="the pass — renamed MSR DIT (gem5 only)"),
]
fig.legend(handles=handles, frameon=False, fontsize=7.6, ncol=3,
           loc="lower center", bbox_to_anchor=(0.5, -0.015))
fig.tight_layout(rect=(0, 0.07, 1, 1))
fig.savefig(FIG / "crossover-gem5-vs-m4.png", dpi=300, facecolor=SURF)
fig.savefig(FIG / "crossover-gem5-vs-m4.pdf", facecolor=SURF)

gstar = crossing([gf[L] for L in gL], gipc("pass", "serialising"), gipc("blanket", "-"))
order = sorted(range(len(mf)), key=lambda k: mf[k])
mstar = crossing([mf[k] for k in order], [mpass[k] for k in order],
                 [mblanket[k] for k in order])
print(f"crossover: gem5 f* = {gstar:.1f}%   Apple M4 f* = {mstar:.1f}%")

# ================================================ fig 2: cost vs predictable share
gq = rows("gem5_predictability_sweep.csv")
mq = rows("m4_predictability_sweep.csv")
GQL = 20000


def gq_series(L):
    out = []
    for q4 in (0, 1, 2, 3, 4):
        nod = next(r for r in gq if int(r["L"]) == L and int(r["pred_q4"]) == q4
                   and r["arm"] == "nodit")
        bl = next(r for r in gq if int(r["L"]) == L and int(r["pred_q4"]) == q4
                  and r["arm"] == "blanket")
        out.append((float(nod["cycles"]) and
                    (float(bl["cycles"]) / float(nod["cycles"]) - 1) * 100))
    return out


def mq_series(lane, L):
    out = []
    for q4 in (0, 1, 2, 3, 4):
        r = next(r for r in mq if r["axis"] == "q" and r["lane"] == lane
                 and int(r["L"]) == L and int(r["value"]) == q4)
        out.append(float(r["blanket_pct"]))
    return out


qs = [0, 0.25, 0.5, 0.75, 1.0]
fig, ax = plt.subplots(figsize=(6.4, 3.6), dpi=300)
fig.patch.set_facecolor(SURF)
style(ax)
series = [
    ("gem5 Neoverse-V2, 62-bit header", gq_series(GQL), BASE, "-", BASE),
    ("Apple M4, 32-bit header", mq_series("narrow", GQL), ORANGE, "-", ORANGE),
    ("Apple M4, 62-bit header (the gem5 lane)", mq_series("wide", GQL), BLUE, (0, (4, 2)), SURF),
]
for name, ys, col, ls, mfc in series:
    ax.plot(qs, ys, color=col, lw=2, ls=ls, marker="o", ms=6, mfc=mfc, mec=col,
            mew=1.6, zorder=3)
ax.axhline(0, color=BASE, lw=0.9, zorder=2)
ax.set_xlim(-0.03, 1.03)
ax.set_xticks(qs)
ax.set_xticklabels(["0", "¼", "½", "¾", "1"])
ax.set_xlabel("q — fraction of lookups that read the record header", fontweight="bold")
ax.set_ylabel("Blanket DIT's cost to the public lane (%)", fontweight="bold")
ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:+.0f}"))
ax.legend(handles=[Line2D([], [], color=c, lw=2, ls=ls, marker="o", ms=6, mfc=mfc,
                          mec=c, mew=1.6, label=n) for n, _, c, ls, mfc in series],
          frameon=False, fontsize=7.6, loc="upper left")
fig.tight_layout()
fig.savefig(FIG / "predictability-gem5-vs-m4.png", dpi=300, facecolor=SURF)
fig.savefig(FIG / "predictability-gem5-vs-m4.pdf", facecolor=SURF)

# ================================================== fig 3: the predictor's width
hw = [r for r in rows("m4_header_width.csv") if r["axis"] == "hdr"]
fig, ax = plt.subplots(figsize=(6.4, 3.3), dpi=300)
fig.patch.set_facecolor(SURF)
style(ax)
for q4, col, mfc, lab in ((4, ORANGE, ORANGE, "q = 1"), (3, GREEN, SURF, "q = ¾")):
    pts = sorted(((int(r["label"].split("(")[1].rstrip("b)")), float(r["blanket_pct"]))
                  for r in hw if int(r["pred_q4"]) == q4), key=lambda t: t[0])
    ax.plot([p[0] for p in pts], [p[1] for p in pts], color=col, lw=2, marker="o",
            ms=5.5, mfc=mfc, mec=col, mew=1.6, zorder=3, label=lab)
ax.axhline(0, color=BASE, lw=0.9, zorder=2)
ax.axvline(36.5, color=FAINT, lw=0.9, ls=(0, (2, 2)), zorder=1)
ax.annotate("36 bits: the widest value\nthis predictor will hold",
            xy=(36.5, 24), xytext=(38.5, 30), fontsize=7.6, color=MUTED,
            ha="left", va="center",
            arrowprops=dict(arrowstyle="-", color=FAINT, lw=0.9))
ax.set_xlabel("Bits in the record header's constant value", fontweight="bold")
ax.set_ylabel("Blanket DIT's cost to\nthe public lane (%)", fontweight="bold")
ax.set_xticks([8, 16, 24, 32, 36, 40, 48, 56, 62])
ax.set_ylim(-3, 52)
ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:+.0f}"))
ax.legend(frameon=False, fontsize=7.6, loc="lower left", bbox_to_anchor=(0.02, 0.10))
fig.tight_layout()
fig.savefig(FIG / "m4-predictor-width.png", dpi=300, facecolor=SURF)
fig.savefig(FIG / "m4-predictor-width.pdf", facecolor=SURF)

print("wrote", FIG / "crossover-gem5-vs-m4.{png,pdf}",
      FIG / "predictability-gem5-vs-m4.{png,pdf}",
      FIG / "m4-predictor-width.{png,pdf}")
