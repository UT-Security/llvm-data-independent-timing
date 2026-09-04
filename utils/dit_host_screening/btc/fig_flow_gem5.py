#!/usr/bin/env python3
"""The wallet flow under gem5: the crossover under two switch models, beside
silicon's.

    fig_flow_gem5.py <flow_derived.csv> <figures dir>

Reads btc_flow_gem5.py's derived table and writes gem5-flow-crossover.{pdf,png}
into the figures dir. Palette, typography and the silicon reference curve come
from plot_crossover.py in that same dir (its TABLE1 is asserted against
table1.md every time Figure 1 is drawn), so the two figures cannot drift apart
in style or in the silicon numbers they show.

(a) pass vs blanket against the measured secret fraction: gem5 with a renamed
    `msr DIT`, gem5 with a serialising one, and silicon (Table 1) for scale.
    Bars are the min..max over the argv[0] offsets, the same layout-noise
    bound experiment 02 reports.
(b) what each arm costs against no DIT, on gem5: blanket's cost dilutes as the
    secret lane grows (it protects the public lane it costs on and the secret
    lane it does not), the pass's cost is its switches, and only the
    serialising switch has a bill that grows with K.
"""
import csv, importlib.util, os, sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def load_module(figdir):
    p = os.path.join(figdir, "plot_crossover.py")
    spec = importlib.util.spec_from_file_location("plot_crossover", p)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def rows_of(path):
    out = []
    for r in csv.DictReader(open(path)):
        d = {"K": int(r["K"])}
        for k, v in r.items():
            if k != "K":
                try:
                    d[k] = float(v)
                except (TypeError, ValueError):
                    d[k] = None
        out.append(d)
    return sorted(out, key=lambda d: d["K"])


def ring(ax, x, y, color, marker, dashed=False, label=None, z=5):
    """A 2px surface ring keeps markers legible where lines cross (marks spec)."""
    ax.plot(x, y, color=color, linewidth=1.6, zorder=z - 1,
            linestyle=(0, (3.2, 1.6)) if dashed else "-",
            solid_capstyle="round", dash_capstyle="round", label=label)
    ax.plot(x, y, marker, color=color, markersize=5.2, zorder=z,
            markeredgecolor=SURFACE, markeredgewidth=1.1, linestyle="none")


def main():
    derived, figdir = sys.argv[1], sys.argv[2]
    m = load_module(figdir)
    global SURFACE
    SURFACE = m.SURFACE
    rows = [r for r in rows_of(derived) if r["K"] > 0 and r.get("f_secret_pct") is not None]
    if not rows:
        sys.exit("no K>0 rows with f_secret in the derived table (was K=0 run?)")

    f = [r["f_secret_pct"] for r in rows]
    spec = [r["pass_vs_blanket_spec_pct"] for r in rows]
    serd = [r["pass_vs_blanket_serdit_pct"] for r in rows]
    spec_lo = [r["pass_vs_blanket_spec_pct"] - r["pass_vs_blanket_spec_min"] for r in rows]
    spec_hi = [r["pass_vs_blanket_spec_max"] - r["pass_vs_blanket_spec_pct"] for r in rows]
    serd_lo = [r["pass_vs_blanket_serdit_pct"] - r["pass_vs_blanket_serdit_min"] for r in rows]
    serd_hi = [r["pass_vs_blanket_serdit_max"] - r["pass_vs_blanket_serdit_pct"] for r in rows]
    sil_f = [m.TABLE1[K][0] for K in sorted(m.TABLE1)]
    sil_y = [m.TABLE1[K][1] for K in sorted(m.TABLE1)]

    m.style()
    fig, (axa, axb) = plt.subplots(1, 2, figsize=(7.0, 3.05), sharex=True)

    # ---- (a) the crossover, three instruments/models on one axis ------------
    ax = axa
    ax.axhline(0, color=m.AXIS, linewidth=0.9, zorder=2)
    ax.plot(sil_f, sil_y, color=m.SECOND, linewidth=1.2, linestyle=(0, (1.2, 1.6)),
            zorder=3, label="silicon, Apple M5 (Table 1)")
    ax.plot(sil_f, sil_y, "D", color=SURFACE, markersize=4.6, zorder=4,
            markeredgecolor=m.SECOND, markeredgewidth=1.0, linestyle="none")
    ax.errorbar(f, spec, yerr=[spec_lo, spec_hi], fmt="none", ecolor=m.MUTED,
                elinewidth=0.8, capsize=2.0, capthick=0.8, zorder=4)
    ax.errorbar(f, serd, yerr=[serd_lo, serd_hi], fmt="none", ecolor=m.MUTED,
                elinewidth=0.8, capsize=2.0, capthick=0.8, zorder=4)
    ring(ax, f, spec, m.SERIES_1, "o", label="gem5, renamed $\\mathtt{msr\\ DIT}$")
    ring(ax, f, serd, m.SERIES_2, "s", dashed=True, label="gem5, serialising $\\mathtt{msr\\ DIT}$")
    ax.set_xlabel("secret fraction of the flow, $f$ (%)")
    ax.set_ylabel("pass vs blanket DIT (%)")
    ax.set_title("(a)  the crossover on gem5, both switch models",
                 loc="left", color=m.INK, pad=24)
    ax.legend(loc="lower right", frameon=False, handlelength=2.2, borderaxespad=0.2)

    # ---- (b) each arm against no DIT, on gem5 --------------------------------
    ax = axb
    ax.axhline(0, color=m.AXIS, linewidth=0.9, zorder=2)
    cw = [r["C_whole_pct"] for r in rows]
    pb_spec = [r["pass_vs_base_spec_pct"] for r in rows]
    pb_serd = [r["pass_vs_base_serdit_pct"] for r in rows]
    ax.plot(f, cw, color=m.SECOND, linewidth=1.4, zorder=3, label="blanket DIT")
    ax.plot(f, cw, "D", color=m.SECOND, markersize=4.6, zorder=4,
            markeredgecolor=SURFACE, markeredgewidth=1.0, linestyle="none")
    ring(ax, f, pb_spec, m.SERIES_1, "o", label="pass, renamed switch")
    ring(ax, f, pb_serd, m.SERIES_2, "s", dashed=True, label="pass, serialising switch")
    ax.set_xlabel("secret fraction of the flow, $f$ (%)")
    ax.set_ylabel("cost vs no DIT (%)")
    ax.set_title("(b)  each arm against no DIT, on gem5",
                 loc="left", color=m.INK, pad=24)
    ax.legend(loc="upper right", frameon=False, handlelength=2.2, borderaxespad=0.2)

    # ---- shared chrome: recessive grid, K on a top axis ----------------------
    for ax in (axa, axb):
        m.recede(ax)
        ax.set_xlim(-1, max(85, max(f) + 4))
        ax.set_xticks([0, 20, 40, 60, 80])
        top = ax.secondary_xaxis("top")
        top.set_xticks(f)
        top.set_xticklabels([str(r["K"]) for r in rows])
        top.tick_params(labelsize=6.5, colors=m.MUTED, length=2, width=0.6,
                        labelcolor=m.MUTED, pad=1.5)
        top.spines["top"].set_visible(False)
        top.set_xlabel("K (inputs signed, hence signatures)", fontsize=6.5,
                       color=m.MUTED, labelpad=2.5)

    fig.tight_layout(pad=0.6, w_pad=2.0)
    for ext in ("pdf", "png"):
        out = os.path.join(figdir, f"gem5-flow-crossover.{ext}")
        fig.savefig(out, dpi=300, bbox_inches="tight", pad_inches=0.02)
        print(f"wrote {out}")


if __name__ == "__main__":
    main()
