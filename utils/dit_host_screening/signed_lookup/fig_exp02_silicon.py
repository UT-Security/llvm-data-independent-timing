#!/usr/bin/env python3
"""Experiment 02's silicon figures: the same crossover on gem5 and on an Apple M4.

  crossover-gem5-vs-m4      the headline. Two panels, one per machine, two arms:
                            blanket DIT against APPLE'S OWN BRACKET around the
                            crypto call -- read the previous DIT state, set it,
                            speculation barrier, the call, restore only if it was
                            clear, which is what AWS-LC ships and what Apple's
                            guidance tells a library author to write. x = secret
                            fraction of the request, y = IPC overhead vs
                            unhardened. Blanket falls with the secret fraction,
                            the bracket rises, and where they cross is the
                            decision. gem5 has no FEAT_SB so its bracket uses
                            `isb sy`; the M4's uses the real `sb`, and that is
                            most of the gap between the two panels.
                            The compiler pass is measured and lives in
                            data/m4_arms.csv, but it is deliberately NOT on this
                            figure: the comparison this draws is blanket against
                            hand placement at the API.
  predictability-gem5-vs-m4 why. Blanket's cost to the PUBLIC lane against q,
                            the fraction of iterations that read the record
                            header, for gem5 and for both Apple lanes.
  m4-predictor-width        the mechanism, in one step function: the same lane
                            with the header holding N one-bits. Everything up to
                            36 is predicted and costs blanket +34%; 37 and above
                            is not predicted and costs nothing.

Reads only the committed CSVs, so it regenerates from data/ with no intermediate:
  gem5_apple_arms.csv, gem5_predictability_sweep.csv   the simulator half
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
#
# gem5_aes_arms.csv. Three earlier files were tried and each was wrong for a
# reason worth keeping:
#
#   gem5_arms.csv        the bracket's barrier was `isb sy`, a substitute for
#                        the `sb` gem5 could not decode, and the two switch
#                        models were "renamed" and a bare --no-speculative-dit.
#   gem5_apple_arms.csv  the real `sb` under the real --apple/--expedite, but on
#                        the chacha20-poly1305 secret lane, where the bracket
#                        never rises above the model's own layout noise: chacha
#                        is a serial ARX chain that does not fill the reorder
#                        window until the message is ~4 KB, by which point the
#                        request is 46,000 cycles and a ~300-cycle flush-after
#                        switch is 0.6% of it. No crossover, for want of a
#                        denominator rather than for want of a cost.
#   ...secret_bytes      growing the chacha message does not fix that: the
#                        switch's cost saturates while the request grows without
#                        bound, and the sweep peaks at 0.64%.
#
# What fixes it is the secret OP, not its size. AES-256-GCM has independent
# round and GHASH work per block, so it saturates the window at 16-64 bytes:
# the bracket costs 206-351 cycles there against a ~1,100-cycle request, which
# is the same ballpark as the 455-510 measured on an M4. f_secret is derived
# from this sweep's own --nosecret arm, so each panel's x axis is its own
# instrument's measurement -- the two weight the lanes differently and the
# README's known limits say plainly they must not be swapped.
g = rows("gem5_aes_arms.csv")
gL = sorted({int(r["L"]) for r in g})
gf = {int(r["L"]): float(r["f_secret_pct"]) for r in g if r["arm"] == "base"}
grow = lambda L, arm, m: next(r for r in g if int(r["L"]) == L and r["arm"] == arm
                              and r["model"] == m)
gipc = lambda arm, m: [float(grow(L, arm, m)["ipc_ovh_pct"]) for L in gL]

# ------------------------------------------------------------------ M4 series
#
# TWO M4 sweeps, and the headline uses the AES one so that BOTH panels are the
# same microbenchmark -- same driver, same public lane, same secret op, same
# L points, same arms. The only thing that still differs between the panels is
# HDR_CONST, and it has to: the public lane's cost is the load-value predictions
# the mode suppresses, so the header has to be a value the machine under test
# can actually hold. gem5's EVES/VTAGE holds 62 bits; this M4 holds 36 (measured
# -- figures/m4-predictor-width). A shared constant would measure the mechanism
# on one machine and nothing at all on the other.
#
# m4_arms.csv is the chacha20-poly1305 sweep, kept because it is the published
# headline and because the pair is instructive: the same bracket crosses at 52%
# there and at 29% here, purely because AES-256-GCM on hardware AES is a much
# shorter secret lane, so a fixed ~440-cycle bracket is a bigger share of the
# request. It is the denominator, not the switch.
m = rows("m4_aes_arms.csv")
m_chacha = rows("m4_arms.csv")


def m4(lane, arm, src=None):
    rs = [r for r in (m if src is None else src)
          if r["lane"] == lane and r["arm"] == arm]
    rs.sort(key=lambda r: int(r["L"]))
    return ([float(r["f_secret_pct"]) for r in rs],
            [float(r["ipc_ovh_pct"]) for r in rs])


# ============================================================ fig 1: the headline
mf, mblanket = m4("narrow", "blanket")
_x, mbracket = m4("narrow", "bracket")
# The chacha lane, measured and committed, reported below but not on the figure:
# putting a second secret op on the same axes invites reading it as a third
# machine. The README carries both tables.
cf, cblanket = m4("narrow", "blanket", m_chacha)
_c, cbracket = m4("narrow", "bracket", m_chacha)
_c, cpass = m4("narrow", "pass", m_chacha)
# Each panel uses ITS OWN measured secret fraction. The two instruments weight
# the lanes differently -- gem5 reads 44.7% at L=200 where the M4 reads 32.6% --
# and paper_experiments/02's known limits say plainly they must not be swapped.

fig, axes = plt.subplots(1, 2, figsize=(8.0, 3.8), dpi=300)
fig.patch.set_facecolor(SURF)

for ax, title, sub, xs, blank, rising in [
    (axes[0], "gem5 Neoverse-V2 FDP",
     "EVES + VTAGE, FEAT_SB; AES-256-GCM secret lane, 64 B, real `sb`",
     [gf[L] for L in gL], gipc("blanket", "apple"),
     [("Apple's bracket, --apple", gipc("api", "apple"), GREEN, "-", GREEN),
      ("Apple's bracket, --expedite", gipc("api", "expedite"), GREEN, (0, (4, 2)), SURF)]),
    (axes[1], "Apple M4 — shipping silicon",
     "36-bit load value predictor, measured; AES-256-GCM secret lane, 64 B",
     mf, mblanket,
     [("Apple's bracket", mbracket, GREEN, "-", GREEN)]),
]:
    style(ax)
    ax.plot(xs, blank, color=BLUE, lw=2.2, marker="o", ms=5, mfc=BLUE, mec=BLUE, zorder=4)
    for _name, ys, col, ls, mfc in rising:
        ax.plot(xs, ys, color=col, lw=2, ls=ls, marker="o", ms=5, mfc=mfc, mec=col,
                mew=1.4, zorder=3)
    ax.axhline(0, color=BASE, lw=0.9, zorder=2)
    secret_axis(ax)
    ax.set_title(title, fontsize=9, color=INK, pad=16, fontweight="bold")
    ax.annotate(sub, xy=(0.5, 1.015), xycoords="axes fraction", fontsize=7.2,
                color=MUTED, ha="center", va="bottom")
    ax.set_xlabel("Secret fraction of the request", fontweight="bold")
    ax.set_ylabel("IPC overhead vs unhardened (%)", fontweight="bold")
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:+.0f}"))

    order = sorted(range(len(xs)), key=lambda k: xs[k])
    xo = [xs[k] for k in order]
    bo = [blank[k] for k in order]
    lo, hi = ax.get_ylim()
    ax.set_ylim(lo, hi + (hi - lo) * 0.08)
    lo, hi = ax.get_ylim()
    for _name, ys, col, ls, _mfc in rising:
        if ls != "-":
            continue
        f = crossing(xo, [ys[k] for k in order], bo)
        if f is None:
            continue
        ax.axvline(f, color=col, lw=1.0, ls=(0, (2, 2)), zorder=1, alpha=0.75)
        ax.annotate(f"blanket cheaper\nabove f = {f:.0f}%",
                    xy=(f, hi), xytext=(f - 2, hi - (hi - lo) * 0.02),
                    fontsize=7.6, color=col, ha="right", va="top",
                    fontweight="bold", linespacing=1.35)

handles = [
    Line2D([], [], color=BLUE, lw=2.2, marker="o", ms=5, label="blanket DIT"),
    Line2D([], [], color=GREEN, lw=2, marker="o", ms=5,
           label="Apple's bracket (mrs DIT, msr #1, sb, call, restore) — flush-after switch (--apple)"),
    Line2D([], [], color=GREEN, lw=2, ls=(0, (4, 2)), marker="o", ms=5, mfc=SURF,
           mec=GREEN, mew=1.4, label="the same bracket under the renamed switch (--expedite, gem5 only)"),
]
fig.legend(handles=handles, frameon=False, fontsize=7.4, ncol=1,
           loc="lower center", bbox_to_anchor=(0.5, -0.02))
fig.tight_layout(rect=(0, 0.15, 1, 1))
fig.savefig(FIG / "crossover-gem5-vs-m4.png", dpi=300, facecolor=SURF)
fig.savefig(FIG / "crossover-gem5-vs-m4.pdf", facecolor=SURF)

order = sorted(range(len(mf)), key=lambda k: mf[k])
mo = lambda v: [v[k] for k in order]
print("crossovers against blanket (secret fraction at which the placed arm stops winning):")
for lab, arm, m in (("gem5   Apple bracket, --apple   ", "api", "apple"),
                    ("gem5   Apple bracket, --expedite", "api", "expedite")):
    f = crossing([gf[L] for L in gL], gipc(arm, m), gipc("blanket", "apple"))
    print(f"  {lab} f* = " + (f"{f:.1f}%" if f is not None
                              else "none -- the bracket is cheaper at every measured point"))
mo = lambda v: [v[k] for k in sorted(range(len(mf)), key=lambda k: mf[k])]
f = crossing(mo(mf), mo(mbracket), mo(mblanket))
print(f"  M4     Apple bracket, AES-GCM 64 B  f* = "
      + (f"{f:.1f}%" if f is not None else "none")
      + "   <- the panel, matched to gem5's lane")
co = lambda v: [v[k] for k in sorted(range(len(cf)), key=lambda k: cf[k])]
for lab, ys in (("M4     Apple bracket, chacha 100 B  ", cbracket),
                ("M4     the pass, chacha 100 B       ", cpass)):
    f = crossing(co(cf), co(ys), co(cblanket))
    print(f"  {lab} f* = " + (f"{f:.1f}%" if f is not None else "none"))

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
