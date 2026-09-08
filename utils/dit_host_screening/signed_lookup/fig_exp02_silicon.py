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

# ------------------------------------------------- the MTE paper's figure style
#
# Matched to UT-Security/mte-paper figure 4 (fig:ampere-server-fix,
# figure/server/after/*.pdf), read off the PDFs since the repo ships no
# generator: matplotlib, a full box frame, grid on BOTH axes, a framed legend
# inside the axes carrying a title, plain (not bold) axis labels, no panel
# title, and a black dashed line at the no-overhead baseline.
#
# THE FONT IS THE ONE COMPROMISE. Those PDFs embed TeXGyreTermesX and NewTXMI,
# i.e. matplotlib with text.usetex=True against the paper's own newtx preamble.
# That needs latex + dvipng on PATH and this host has neither (tectonic cannot
# serve usetex). Times New Roman with STIX for math is the same design -- TeX
# Gyre Termes IS a Times clone -- and sits beside newtxtext body copy without
# announcing itself. If this ever runs somewhere with a TeX install, setting
# text.usetex=True and font.serif to Times is the exact thing.
PAPER_STYLE = {
    "font.family": "serif",
    "font.serif": ["Times New Roman", "STIXGeneral", "DejaVu Serif"],
    "mathtext.fontset": "stix",
    "font.size": 9,
    "axes.labelsize": 9,
    "axes.edgecolor": "black",
    "axes.linewidth": 0.8,
    "axes.labelcolor": "black",
    "xtick.color": "black",
    "ytick.color": "black",
    "xtick.labelsize": 8.5,
    "ytick.labelsize": 8.5,
    "xtick.direction": "out",
    "ytick.direction": "out",
    "text.color": "black",
    "grid.color": "#b0b0b0",
    "grid.linewidth": 0.5,
    "legend.fontsize": 8,
    "legend.title_fontsize": 8,
    "legend.framealpha": 0.92,
    "legend.edgecolor": "#b0b0b0",
    "legend.fancybox": True,
    "legend.borderpad": 0.4,
}

# --------------------------------------------------------------- arm styling
#
# ExpeDITe is the contribution, so it gets the ONLY saturated colour on the
# figure and the heaviest line. Coarse and Fine are what it is measured against
# and they recede: a desaturated steel blue and a neutral grey, no hue that
# competes with the green. The reader's eye should land on the flat green curve,
# because that is the result.
#
# Two side effects of spending colour on one arm, both wanted. Only ONE hue is
# load-bearing, so the figure survives red-green colour blindness; and the three
# arms differ in weight and dash as well as colour, so it survives greyscale
# printing. Fine is dashed rather than solid for the same reason -- it is the
# curve that crosses Coarse, and the crossing has to stay readable in ink.
COARSE_C = "#4E7396"   # desaturated steel blue
FINE_C   = "#7C868E"   # neutral grey, deliberately hueless
EXPED_C  = "#0E8F5E"   # the one saturated colour on the figure
ARM = {
    "coarse":   dict(label="Coarse",   color=COARSE_C, ls="-",         lw=1.6, ms=3.2, z=3),
    "fine":     dict(label="Fine",     color=FINE_C,   ls=(0, (5, 2)), lw=1.6, ms=3.2, z=4),
    "expedite": dict(label="ExpeDITe", color=EXPED_C,  ls="-",         lw=2.6, ms=4.2, z=6),
}


def draw(ax, xs, ys, key, scale=1.0):
    a = ARM[key]
    ax.plot(xs, ys, color=a["color"], ls=a["ls"], lw=a["lw"] * scale,
            marker="o", ms=a["ms"] * scale, mfc=a["color"], mec=a["color"],
            zorder=a["z"])


def arm_handles(keys, scale=1.0):
    return [Line2D([], [], color=ARM[k]["color"], ls=ARM[k]["ls"],
                   lw=ARM[k]["lw"] * scale, marker="o", ms=ARM[k]["ms"] * scale,
                   mfc=ARM[k]["color"], mec=ARM[k]["color"], label=ARM[k]["label"])
            for k in keys]
plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 9, "axes.labelcolor": MUTED,
                     "xtick.color": MUTED, "ytick.color": MUTED, "text.color": INK})


def style(ax):
    """The web/README look: light, spineless, y grid only."""
    ax.set_facecolor(SURF)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    for s in ("left", "bottom"):
        ax.spines[s].set_color(GRID)
    ax.yaxis.grid(True, color=GRID, lw=0.7, zorder=0)
    ax.set_axisbelow(True)
    ax.tick_params(axis="both", length=0)


def paper_style(ax):
    """The MTE paper's look: boxed, gridded on both axes, ticks out."""
    ax.set_facecolor("none")
    for sp in ax.spines.values():
        sp.set_visible(True)
        sp.set_color("black")
        sp.set_linewidth(0.8)
    ax.grid(True, which="major", axis="both", color="#b0b0b0", lw=0.5, zorder=0)
    ax.set_axisbelow(True)
    ax.tick_params(axis="both", direction="out", length=2.5, width=0.7,
                   color="black")


def rows(name):
    with open(G / name) as fh:
        return list(csv.DictReader(l for l in fh if not l.startswith("#")))


# The x axis says CYCLES, because "secret fraction" on its own does not say
# fraction of what, and the three candidates (cycles, instructions, bytes) give
# different numbers. It is
#
#     f = (cycles with the secret lane - cycles without) / cycles with
#
# measured on the UNHARDENED binary, the second term being the same binary run
# with --nosecret. So f is a property of the workload and not of any placement
# policy: every arm moves along one fixed axis. It is also why the two panels
# read different f at the same L -- same code, but gem5 sustains IPC 1.07 at
# L=10 where the M4 sustains 3.89, so the public lane costs relatively more
# there. The README's known limits say the two axes must not be swapped.
#
# WORKLOAD, not "request". The driver's docstring says request and the CSV
# columns are still requests / cyc_per_request -- that schema is published and
# stays -- but "request" implies a server's unit of work, and this public lane
# is a dependence chain built to have the right shape rather than any
# application's real public code, so the axis does not claim it. "Workload" is
# exact here for a reason worth writing down: the driver repeats ONE identical
# unit, so the secret share of one unit and the secret share of the whole run
# are the same number. On a driver with a mix of units they would not be, and
# this label would have to change with it.
#
# Title case and a parenthesised unit, matching "IPC overhead (%)" on the y.
XLABEL = "Secret Fraction of Workload (cycles)"


def secret_axis(ax):
    """Both machines share this one axis. The L behind each point is in the CSV."""
    ax.set_xlim(-2, 102)
    ax.set_xticks([0, 20, 40, 60, 80, 100])
    ax.set_xticklabels([f"{t}%" for t in (0, 20, 40, 60, 80, 100)])
    ax.set_xlabel(XLABEL, fontweight="bold")


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

for ax, title, sub, xs, coarse, others in [
    (axes[0], "gem5 Neoverse-V2 FDP",
     "EVES + VTAGE, FEAT_SB; AES-256-GCM secret lane, 64 B, real `sb`",
     [gf[L] for L in gL], gipc("blanket", "apple"),
     [("fine", gipc("api", "apple")), ("expedite", gipc("api", "expedite"))]),
    (axes[1], "Apple M4 — shipping silicon",
     "36-bit load value predictor, measured; AES-256-GCM secret lane, 64 B",
     mf, mblanket,
     [("fine", mbracket)]),
]:
    style(ax)
    draw(ax, xs, coarse, "coarse", 1.15)
    for key, ys in others:
        draw(ax, xs, ys, key, 1.15)
    ax.axhline(0, color=BASE, lw=0.9, zorder=2)
    secret_axis(ax)
    ax.set_title(title, fontsize=9, color=INK, pad=16, fontweight="bold")
    ax.annotate(sub, xy=(0.5, 1.015), xycoords="axes fraction", fontsize=7.2,
                color=MUTED, ha="center", va="bottom")
    ax.set_ylabel("IPC overhead vs unhardened (%)", fontweight="bold")
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:+.0f}"))

    order = sorted(range(len(xs)), key=lambda k: xs[k])
    xo = [xs[k] for k in order]
    bo = [coarse[k] for k in order]
    lo, hi = ax.get_ylim()
    ax.set_ylim(lo, hi + (hi - lo) * 0.08)
    lo, hi = ax.get_ylim()
    # The marker is for Coarse x Fine and is drawn in FINE's colour, not
    # ExpeDITe's: green marks ExpeDITe and nothing else on this figure.
    fine_ys = dict(others).get("fine")
    if fine_ys is not None:
        f = crossing(xo, [fine_ys[k] for k in order], bo)
        if f is not None:
            ax.axvline(f, color=FINE_C, lw=1.0, ls=(0, (2, 2)), zorder=1, alpha=0.8)
            ax.annotate(f"Coarse cheaper\nabove f = {f:.0f}%",
                        xy=(f, hi), xytext=(f - 2, hi - (hi - lo) * 0.02),
                        fontsize=7.6, color=FINE_C, ha="right", va="top",
                        fontweight="bold", linespacing=1.35)

fig.legend(handles=arm_handles(["coarse", "fine", "expedite"], 1.15), frameon=False,
           fontsize=8.2, ncol=3, loc="lower center", bbox_to_anchor=(0.5, -0.01),
           handlelength=2.2, columnspacing=2.4)
fig.tight_layout(rect=(0, 0.09, 1, 1))
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

# ======================================= fig 1b: the same pair, as paper panels
#
# The two panels again, each as its OWN file, for a LaTeX side-by-side with
# subcaptions (a) and (b). What the surrounding document does instead:
#
#   * no panel title and no subtitle -- the subcaption carries both
#   * no in-panel legend. Both panels are full (two curves that cross, plus a
#     third in gem5's), and a box placed to miss the data in one lands on it in
#     the other. So the legend is a THIRD file, a wide strip listing all three
#     arms once, which LaTeX puts above the pair.
#   * transparent background, so the page colour shows through
#   * "IPC overhead (%)" and not "... vs unhardened (%)": the long form does not
#     fit the height of a two-column panel and the caption can say it.
#
# SILICON IS (a) AND COMES FIRST. The hardware is the measurement and the
# simulator is the comparison, so the reader meets the hardware first. The
# LaTeX is written beside them in figures/latex/crossover.tex.
#
# The two y axes do NOT share a range (about +105 against about +35) and must
# not be forced to: on a shared axis gem5's curves flatten into the bottom third
# and stop being readable. Both panels label their own axis, and the caption
# says the ranges differ -- that difference is a result, not a plotting artifact.
# Aspect follows the MTE figure's proportions -- theirs is 6.9 x 2.3in for a
# full-column strip; ours is half that width because two sit side by side, so
# 3.35 x 2.3 keeps a comparable line-to-height feel rather than copying the
# number. Legend INSIDE the axes with a title, as theirs is; its corner differs
# per panel because the data does, which is what theirs does too.
PANEL = (3.35, 2.3)


def paper_panel(path, xs, coarse, others, legend_loc):
    with plt.rc_context(PAPER_STYLE):
        fig, ax = plt.subplots(figsize=PANEL, dpi=300)
        paper_style(ax)
        draw(ax, xs, coarse, "coarse")
        for key, ys in others:
            draw(ax, xs, ys, key)
        # The no-overhead baseline, black and dashed: the MTE figure marks its
        # own (normalised 1.0) the same way, and it is the line every arm is
        # being read against.
        ax.axhline(0, color="black", lw=1.0, ls=(0, (5, 3)), zorder=5)
        secret_axis(ax)
        # Bold axis labels. This is the one place the panels depart from the
        # MTE figure's typography, whose labels are plain -- asked for, and it
        # reads well against Times body copy since the label is then clearly
        # not part of the running text.
        ax.set_xlabel(XLABEL, fontweight="bold")
        ax.set_ylabel("IPC Overhead (%)", fontweight="bold")
        ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:+.0f}"))

        order = sorted(range(len(xs)), key=lambda k: xs[k])
        xo, bo = [xs[k] for k in order], [coarse[k] for k in order]
        lo, hi = ax.get_ylim()
        ax.set_ylim(lo, hi + (hi - lo) * 0.04)
        lo, hi = ax.get_ylim()
        fine_ys = dict(others).get("fine")
        if fine_ys is not None:
            f = crossing(xo, [fine_ys[k] for k in order], bo)
            if f is not None:
                # FINE's colour, not ExpeDITe's: the marker is for Coarse x Fine,
                # and green means ExpeDITe and nothing else on this figure.
                ax.axvline(f, color=FINE_C, lw=0.9, ls=(0, (2, 2)), zorder=1)
                ax.annotate(f"$f^*$ = {f:.0f}%", xy=(f, hi),
                            xytext=(f + 2.0, hi - (hi - lo) * 0.03),
                            fontsize=8.5, color=FINE_C, ha="left", va="top")
        keys = ["coarse"] + [k for k, _ in others]
        ax.legend(handles=arm_handles(keys), loc=legend_loc,
                  title="DIT placement", ncol=1, handlelength=1.9,
                  labelspacing=0.32, borderaxespad=0.5)
        fig.tight_layout(pad=0.25)
        for ext in ("pdf", "png"):
            fig.savefig(FIG / f"{path}.{ext}", transparent=True,
                        bbox_inches="tight", pad_inches=0.02)
        plt.close(fig)


# Legend corners: (a) has an empty upper-left above Coarse's flat start, (b) has
# an empty band on the left between Coarse coming down and Fine going up.
paper_panel("crossover-panel-a-m4", mf, mblanket, [("fine", mbracket)], "upper left")
paper_panel("crossover-panel-b-gem5", [gf[L] for L in gL], gipc("blanket", "apple"),
            [("fine", gipc("api", "apple")), ("expedite", gipc("api", "expedite"))],
            "center left")
print("wrote " + str(FIG / "crossover-panel-{a-m4,b-gem5}.{pdf,png}"))
print("wrote " + str(FIG / "crossover-panel-{a-m4,b-gem5,legend}.{pdf,png}"))

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
