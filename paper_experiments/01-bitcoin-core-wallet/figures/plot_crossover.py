#!/usr/bin/env python3
"""Figure 1 - the secret-fraction crossover, Bitcoin Core wallet case study.

Two panels sharing one x-axis (the measured secret fraction):

  (a) the result     - pass vs blanket, crossing zero inside one wallet call
  (b) the mechanism  - a flat public-lane prize meeting a growing cost

CORRECTED 2026-09-02. Panel (b)'s second curve was labelled "switch cost" and
described as the toggle bill. It is `pass/passnop`, and `passnop` emits HINT #0
in place of every inserted MSR DIT, so PSTATE.DIT is never set and that arm
loses the PROTECTION as well as the switch. The curve is switch cost PLUS DIT
dwell over the regions the pass protects, and dwell grows with the secret lane.
This experiment's own gem5 flow isolates serialisation on one binary under two
switch models and measures +1.21% at K=400 against the +5.98% this curve
reports. The panel
still shows a fixed prize meeting a growing cost -- that part held -- but the
growing cost is not toggles. Panel (a) is unaffected: pass vs blanket compares
two arms that both protect, so dwell is in both and cancels.

Panel (b) explains panel (a): the two lines in (b) cross at roughly the f where
(a) crosses zero, which is the claim ("a fixed prize against a linearly growing
cost") drawn rather than asserted.

The per-cell statistics are recomputed here with the same definitions
table_wallet_sweep.py uses, then ASSERTED against the numbers committed in
../table1.md, so the figure cannot silently drift from the table.

Usage: plot_crossover.py [csv]        # writes crossover.pdf and crossover.png
"""
import csv, math, os, statistics as st, sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_CSV = os.path.join(HERE, os.pardir, "data", "wallet_sweep_20rep.csv")

# ---- palette (dataviz reference instance; validated light, surface #fcfcfb) --
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
SECOND = "#52514e"
MUTED = "#898781"
GRID = "#e1e0d9"
AXIS = "#c3c2b7"
SERIES_1 = "#2a78d6"   # slot 1, blue    - the prize / "pass wins" pole
SERIES_2 = "#eb6834"   # slot 2, orange  - the pass's own cost (switch + dwell)
POLE_HI = "#e34948"    # slot 8, red     - "blanket wins" pole of the diverging pair

# ---- statistics, identical definitions to table_wallet_sweep.py -------------
def load(path):
    return list(csv.DictReader(open(path)))


def series(rows, K, arm):
    return {int(r["rep"]): float(r["ns_per_op"])
            for r in rows if int(r["inputs"]) == K and r["arm"] == arm}


def binom_two_sided(n, N):
    """Exact two-sided sign-test p-value for n successes in N at p=0.5."""
    pmf = [math.comb(N, k) / 2.0 ** N for k in range(N + 1)]
    return min(1.0, sum(p for p in pmf if p <= pmf[n] + 1e-12))


def median_ci_rank(N, level=0.95):
    """Largest k with a distribution-free CI [x_(k), x_(N+1-k)] at >= level.

    Order-statistic CI for the median, coverage 1 - 2*P(Bin(N,1/2) < k). This is
    the interval the sign test in the table already implies, so the error bars
    and the stars cannot disagree.
    """
    best = 1
    for k in range(1, N // 2 + 1):
        tail = sum(math.comb(N, i) for i in range(k)) / 2.0 ** N
        if 1 - 2 * tail >= level:
            best = k
    return best


def paired(rows, K, a, b):
    """Median per-rep ratio b/a in percent, its CI, n slower, N, sign-test p."""
    A, B = series(rows, K, a), series(rows, K, b)
    reps = sorted(set(A) & set(B))
    rat = sorted(B[r] / A[r] for r in reps)
    N = len(rat)
    n = sum(1 for x in rat if x > 1.0)
    k = median_ci_rank(N)
    return dict(med=(st.median(rat) - 1) * 100,
                lo=(rat[k - 1] - 1) * 100, hi=(rat[N - k] - 1) * 100,
                n=n, N=N, p=binom_two_sided(n, N))


def build(rows):
    out = []
    for K in sorted({int(r["inputs"]) for r in rows}):
        base, pub = series(rows, K, "baseline"), series(rows, K, "pub_base")
        reps = sorted(set(base) & set(pub))
        out.append(dict(
            K=K,
            f=st.median([(base[r] - pub[r]) / base[r] for r in reps]) * 100,
            xover=paired(rows, K, "always", "pass"),
            cpub=paired(rows, K, "pub_base", "pub_always"),
            sw=paired(rows, K, "passnop", "pass"),
        ))
    return out


# ---- the committed table, as a regression gate ------------------------------
# K: (f_secret, pass-vs-blanket, C_public, pass-vs-nop) from ../table1.md
TABLE1 = {
    1:   (3.7,  -3.52, 3.94, -0.64),
    4:   (5.8,  -2.81, 4.37, +0.95),
    10:  (10.2, -3.18, 3.95, +0.16),
    25:  (19.2, -2.06, 4.23, +0.81),
    50:  (29.8, -1.93, 2.96, +1.77),
    100: (45.0, -0.48, 3.36, +3.53),
    200: (61.2, +0.74, 3.55, +5.10),
    400: (75.0, +1.65, 3.09, +5.98),
}


def check(tab):
    for t in tab:
        want = TABLE1[t["K"]]
        got = (t["f"], t["xover"]["med"], t["cpub"]["med"], t["sw"]["med"])
        for name, w, g in zip(("f_secret", "pass/blanket", "C_public", "switch"),
                              want, got):
            tol = 0.05 if name == "f_secret" else 0.005
            assert abs(w - g) <= tol, \
                f"K={t['K']} {name}: table1.md says {w:+.2f}, CSV gives {g:+.2f}"
    print(f"table1.md regression gate: PASS ({len(tab)} knob points x 4 quantities)")


# ---- figure -----------------------------------------------------------------
def style():
    plt.rcParams.update({
        "font.family": "sans-serif",
        "font.sans-serif": ["Helvetica Neue", "Helvetica", "Arial", "DejaVu Sans"],
        "font.size": 8,
        "axes.labelsize": 8.5,
        "axes.titlesize": 9,
        "xtick.labelsize": 7.5,
        "ytick.labelsize": 7.5,
        "legend.fontsize": 7.5,
        "figure.facecolor": SURFACE,
        "axes.facecolor": SURFACE,
        "savefig.facecolor": SURFACE,
        "axes.edgecolor": AXIS,
        "axes.linewidth": 0.6,
        "xtick.color": MUTED,
        "ytick.color": MUTED,
        "xtick.major.size": 2.5,
        "ytick.major.size": 2.5,
        "xtick.major.width": 0.6,
        "ytick.major.width": 0.6,
        "axes.labelcolor": SECOND,
        "grid.color": GRID,
        "grid.linewidth": 0.6,
        "pdf.fonttype": 42,
        "ps.fonttype": 42,
    })


def recede(ax):
    """Grid and axes are chrome, not data - keep them behind and quiet."""
    ax.set_axisbelow(True)
    ax.grid(axis="y", which="major")
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    ax.spines["left"].set_color(AXIS)
    ax.spines["bottom"].set_color(AXIS)
    ax.tick_params(labelcolor=SECOND)


def panel_a(ax, tab):
    f = [t["f"] for t in tab]
    y = [t["xover"]["med"] for t in tab]
    lo = [t["xover"]["med"] - t["xover"]["lo"] for t in tab]
    hi = [t["xover"]["hi"] - t["xover"]["med"] for t in tab]

    # the diverging fill: which side of "blanket DIT" the workload sits on
    ax.fill_between(f, y, 0, where=[v <= 0 for v in y],
                    color=SERIES_1, alpha=0.13, linewidth=0, interpolate=True)
    ax.fill_between(f, y, 0, where=[v >= 0 for v in y],
                    color=POLE_HI, alpha=0.13, linewidth=0, interpolate=True)

    ax.axhline(0, color=AXIS, linewidth=0.9, zorder=2)
    ax.plot(f, y, color=SECOND, linewidth=1.5, zorder=3, solid_capstyle="round")
    ax.errorbar(f, y, yerr=[lo, hi], fmt="none", ecolor=MUTED,
                elinewidth=0.9, capsize=2.2, capthick=0.9, zorder=4)

    # marker face carries the verdict; hollow means the sign test cannot resolve it
    for t in tab:
        x = t["xover"]
        sig = x["p"] < 0.05
        face = (SERIES_1 if x["med"] < 0 else POLE_HI) if sig else SURFACE
        edge = SURFACE if sig else MUTED
        ax.plot([t["f"]], [x["med"]], "o", markersize=5.5, zorder=5,
                markerfacecolor=face, markeredgecolor=edge, markeredgewidth=1.2)

    ax.set_xlabel("secret fraction of the call, $f$ (%)")
    ax.set_ylabel("pass vs blanket DIT (%)")
    ax.set_title("(a)  the result: the verdict flips inside one call",
                 loc="left", color=INK, pad=24)
    ax.set_ylim(-5.15, 3.0)
    ax.set_yticks([-4, -3, -2, -1, 0, 1, 2])

    # direct labels instead of a one-swatch legend; a single series names itself
    # the labels name the shaded regions, so the fill needs no legend
    ax.text(15.5, -0.72, "selective placement wins", color=SERIES_1,
            fontsize=7.5, fontweight="bold", va="center", ha="center")
    ax.text(76, 2.45, "blanket DIT wins", color=POLE_HI, fontsize=7.5,
            fontweight="bold", va="center", ha="right")
    tie = next(t for t in tab if t["xover"]["p"] >= 0.05)
    ax.annotate(f"tie at $f$ = {tie['f']:.0f}%\n({tie['xover']['n']}/{tie['xover']['N']}, n.s.)",
                xy=(tie["f"], tie["xover"]["med"]), xytext=(tie["f"] - 7, -3.5),
                color=SECOND, fontsize=7, ha="center",
                arrowprops=dict(arrowstyle="-", color=MUTED, linewidth=0.7,
                                shrinkA=0, shrinkB=4))
    return f


def panel_b(ax, tab):
    f = [t["f"] for t in tab]
    cpub = [t["cpub"]["med"] for t in tab]
    sw = [t["sw"]["med"] for t in tab]

    ax.axhline(0, color=AXIS, linewidth=0.9, zorder=2)
    ax.plot(f, cpub, "-o", color=SERIES_1, linewidth=1.6, markersize=5,
            markeredgecolor=SURFACE, markeredgewidth=1.1, zorder=4,
            label="$C_{public}$  (public lane, blanket)")
    ax.plot(f, sw, "--s", color=SERIES_2, linewidth=1.6, markersize=4.6,
            markeredgecolor=SURFACE, markeredgewidth=1.1, zorder=4,
            dashes=(4, 2), label="pass / NOP arm  (switch + DIT dwell)")

    ax.set_xlabel("secret fraction of the call, $f$ (%)")
    ax.set_ylabel("cost vs no DIT (%)")
    ax.set_title("(b)  the mechanism: a fixed prize, a growing cost",
                 loc="left", color=INK, pad=24)
    ax.set_ylim(-2.7, 8.6)
    ax.set_yticks([0, 2, 4, 6, 8])

    ax.text(1.5, cpub[0] + 2.1, "the prize: flat in $f$", color=SERIES_1,
            fontsize=7.5, fontweight="bold", ha="left")
    ax.text(f[-1] + 2.5, sw[-1] + 1.35, "the cost: mostly DIT dwell\nover the growing secret lane",
            color=SERIES_2, fontsize=7.5, fontweight="bold", ha="right",
            linespacing=1.25)
    # The switch/dwell split is a caption matter: it comes from a different
    # instrument (this experiment's gem5 flow) and would be a third encoding here.

    leg = ax.legend(loc="lower left", frameon=False, handlelength=2.2,
                    borderaxespad=0.3, labelspacing=0.4)
    for txt in leg.get_texts():
        txt.set_color(SECOND)


def top_axis(ax, tab):
    """K is the knob the experimenter turns; f is what it moves. Same points."""
    top = ax.secondary_xaxis("top")
    top.set_xticks([t["f"] for t in tab])
    top.set_xticklabels([str(t["K"]) for t in tab])
    top.tick_params(labelsize=6.5, colors=MUTED, length=2, width=0.6,
                    labelcolor=MUTED, pad=1.5)
    top.spines["top"].set_visible(False)
    top.set_xlabel("K (inputs signed, hence signatures)", fontsize=6.5,
                   color=MUTED, labelpad=2.5)
    return top


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_CSV
    rows = load(path)
    tab = build(rows)
    check(tab)

    style()
    fig, (axa, axb) = plt.subplots(1, 2, figsize=(7.0, 3.05), sharex=True)
    panel_a(axa, tab)
    panel_b(axb, tab)
    for ax in (axa, axb):
        recede(ax)
        ax.set_xlim(-1, 80)
        ax.set_xticks([0, 20, 40, 60, 80])
        top_axis(ax, tab)

    fig.tight_layout(pad=0.6, w_pad=2.0)
    for ext in ("pdf", "png"):
        out = os.path.join(HERE, f"crossover.{ext}")
        fig.savefig(out, dpi=300, bbox_inches="tight", pad_inches=0.02)
        print(f"wrote {out}")


if __name__ == "__main__":
    main()
