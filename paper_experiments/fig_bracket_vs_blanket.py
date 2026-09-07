#!/usr/bin/env python3
"""Blanket DIT and function-level DIT, both against the unhardened build.

One figure, two stacked panels, the same two bars on each:

  (a) libsodium primitives     experiment 09, 09-libsodium-cio-parity/results/m4/cio.csv
  (b) WordPress 6.2            experiment 11, 11-php-src-suite/results/m4/*.json

y is IPC overhead against the UNHARDENED build (arm A) for both series:

    baseline_IPC / arm_IPC - 1

so a taller bar is slower, and the two bars in a group are directly
comparable because they share a denominator. IPC itself FALLS under a mode
switch, so plotting arm/baseline would put a slowdown below zero and read as an
improvement; inverted, positive is cost.

**This is a common-denominator figure, and experiment 11's summary.txt is not.**
That table compares blanket against the baseline but the bracket against its own
instruction-matched NOP twin, on the grounds that emitting the switches moves
code and relinking alone moved layout 3.9% in its pilot. Both readings are in
the review page's table (`vs twin`); the chart uses the common denominator
because two bars over different denominators cannot be read against each other,
which is the one thing this figure exists to let a reader do. Where they differ
the twin reading is the more conservative one and the summary is the citation.

**Read the two cost models, not the individual bars.** Blanket is a fixed
per-process tax: it suppresses the data-dependent optimisations for the whole
program whether or not a secret is present, so on WordPress it is +2.1% to +2.6%
across a 600-fold change in crypto boundary crossings and does not care what the
request does. Function-level placement pays per crossing: 0.0% at 110 crossings,
+1.1% at 8,230, +8.4% at 65,600. Which is cheaper is a property of the WORKLOAD.

Panel (a) is the same picture at the other extreme. Every libsodium operation is
crypto and the operations are short, so blanket is nearly free (-1.3% to +4.8%)
while the same bracket costs +0.2% to +114%.

**y scales are independent between panels and must be.** (a) spans 115 points,
(b) spans 9. A shared axis would flatten (b) into a line.

Aggregation, per experiment, matching each one's own report:
  09  per rep samp_cyc/samp_n and samp_ins/samp_n, median across the 15 reps;
      IPC = samp_ins / samp_cyc. Cycles read bare from PMC0, instructions
      isb-ordered from PMC1, 1 region sampled in 64.
  11  the committed medians over 100 measured requests per arm, PMC0/PMC1 read
      around the whole php-cgi process; `median` is kilocycles and `median_ins`
      is instructions, so IPC = median_ins / (median * 1e3).

Arms are the same letters in both: A unhardened, C blanket, B the bracket,
Bn/T the bracket's instruction-matched NOP twin.

NOTE ON summary.txt's PERCENTAGES. Its TABLE 1 computes each change from the
2-decimal IPC values it prints rather than from full precision, which costs up
to 0.25 points: the shipped-configuration bracket rows read -1.0% there and
-1.23% / -0.94% computed from the raw medians. This script uses full precision.
The conclusion is unchanged -- both shipped rows still favour the bracket.

Panel (b) is TABLE 1's six WordPress rows, in its order. Symfony, the REST
anonymous row, the Zend micro-benchmark and the login-mix rows are the ones that
table leaves out, for the reasons it states.

Needs matplotlib. Some system Pythons are PEP 668-locked; if pip refuses:
  python3 -m venv /tmp/mplvenv && /tmp/mplvenv/bin/pip install matplotlib
  /tmp/mplvenv/bin/python paper_experiments/fig_bracket_vs_blanket.py

Lives in paper_experiments/ rather than inside either experiment because it
spans both, and resolves every path from its own directory -- nothing here goes
through the repo root or through utils/.

Writes into paper_experiments/figures/:
  bracket-vs-blanket-m4.png / .pdf   the paper figure (matplotlib)
  bracket-vs-blanket-m4.html         the review page, same rows, nothing typed twice

  --png-only / --html-only   emit one of them
  --machine <name>           a different results subdirectory (default m4)
"""
import argparse
import collections
import csv
import json
import pathlib
import statistics as st
import sys

HERE = pathlib.Path(__file__).resolve().parent
E09 = HERE / "09-libsodium-cio-parity"
E11 = HERE / "11-php-src-suite"
OUT = HERE / "figures"

# Two series, so the pair has to survive colour-vision deficiency: validated
# with the dataviz six-checks against #FFFFFF and #161C21, ALL PASS
# (worst adjacent dE 21.6 deutan light, 17.7 protan dark). Do not re-hue without
# re-running scripts/validate_palette.js. All TEXT stays ink.
BLANKET_L, BRACKET_L = "#1D6FA8", "#C1121F"
BLANKET_D, BRACKET_D = "#3F97D0", "#E4555F"
INK_L = "#11171C"

MINUS, DASH = "−", "—"

# The spread relink_null.sh produces by linking the UNHARDENED library at 12
# different addresses: same instructions, same committed instruction count, only
# the address moves. A difference under it is not a result. No ensemble exists
# for argon2id -- one cell is ~2.5 h of host time -- so it has no entry and
# nothing in that row is claimed to resolve.
RELINK_FLOOR = {
    "ed25519": 0.0540, "chacha20_poly1305_encrypt": 0.0453,
    "chacha20_poly1305_decrypt": 0.0704, "aesni256gcm_encrypt": 0.0050,
    "aesni256gcm_decrypt": 0.0194,
}

LIBSODIUM = [
    ("aesni256gcm_decrypt",       "aes256-gcm", "decrypt"),
    ("aesni256gcm_encrypt",       "aes256-gcm", "encrypt"),
    ("chacha20_poly1305_encrypt", "chacha20-poly1305", "encrypt"),
    ("chacha20_poly1305_decrypt", "chacha20-poly1305", "decrypt"),
    ("ed25519",                   "ed25519", "sign"),
    ("argon2id",                  "argon2id", ""),
]

# Experiment 11 summary.txt TABLE 1, in its order: one application, six request
# types, 110 to 65,600 crossings. Holding WordPress constant makes it a
# controlled sweep -- same code, same database, same bootstrap, and the only
# variable that moves is how often the request enters a crypto builtin.
WORDPRESS = [
    ("wordpress", "anon (the suite's request)",
     "GET /", "anonymous", 110),
    ("wordpress", "logged-in pages",
     "GET /", "logged in", 128),
    ("wpapi_13", "logins only, phpass 2^13 rounds",
     "wp-login", "2^13", 8226),
    ("wpapi_13", "REST authenticated by application password, phpass 2^13 rounds",
     "wp-json", "2^13", 8230),
    ("wpapi_16", "logins only, phpass 2^16 rounds",
     "wp-login", "2^16", 65600),
    ("wpapi_16", "REST authenticated by application password, phpass 2^16 rounds",
     "wp-json", "2^16", 65600),
]


def mad_frac(v):
    m = st.median(v)
    return (st.median([abs(x - m) for x in v]) / m) if m else 0.0


def _row(label, sub, a, c, b, twin, worst_mad):
    ipc = lambda x: x["ins"] / x["cyc"]
    ipc_a, ipc_c, ipc_b, ipc_t = ipc(a), ipc(c), ipc(b), ipc(twin)
    return {
        "label": label, "sub": sub,
        "ipc_base": ipc_a, "ipc_blanket": ipc_c,
        "ipc_bracket": ipc_b, "ipc_twin": ipc_t,
        # what the chart plots: both against the unhardened build
        "ovh_blanket": ipc_a / ipc_c - 1,
        "ovh_bracket": ipc_a / ipc_b - 1,
        # summary.txt's reading of the bracket: against its own NOP twin
        "ovh_bracket_twin": ipc_t / ipc_b - 1,
        "cyc_blanket": c["cyc"] / a["cyc"] - 1,
        "cyc_bracket": b["cyc"] / a["cyc"] - 1,
        "d_ins_blanket": c["ins"] / a["ins"] - 1,
        "d_ins_bracket": b["ins"] / a["ins"] - 1,
        "base_cyc": a["cyc"],
        "reps": a["reps"], "worst_mad": worst_mad,
        "res_blanket": abs(c["cyc"] / a["cyc"] - 1) > worst_mad,
        "res_bracket": abs(b["cyc"] / a["cyc"] - 1) > worst_mad,
    }


def load_libsodium(run):
    path = run / "cio.csv"
    if not path.exists():
        sys.exit(f"no such run: {path}")
    per = collections.defaultdict(list)
    with open(path) as fh:
        for r in csv.DictReader(fh):
            n = float(r.get("samp_n") or 0)
            if n <= 0:                      # unsampled row (a kperf run)
                continue
            per[(r["benchmark"], r["arm"])].append(
                (float(r["samp_cyc"]) / n, float(r["samp_ins"]) / n))

    rows = []
    for key, label, sub in LIBSODIUM:
        arm = {}
        for a in ("A", "B", "C", "T"):
            v = per.get((key, a))
            if not v:
                sys.exit(f"{key}: arm {a} missing from {path}")
            cyc = [x[0] for x in v]
            arm[a] = {"cyc": st.median(cyc), "ins": st.median([x[1] for x in v]),
                      "mad": mad_frac(cyc), "reps": len(v)}
        # the report's gate is 3x the worst within-arm MAD
        r = _row(label, sub, arm["A"], arm["C"], arm["B"], arm["T"],
                 worst_mad=3 * max(arm[a]["mad"] for a in arm))
        # panel (a)'s x axis is operation length: a fixed switch cost is a large
        # fraction of a 298-cycle AEAD and a rounding error on a 34,000-cycle
        # signature, and that is the whole shape of the panel.
        rows.append(r)
    rows.sort(key=lambda d: -d["ovh_bracket"])
    return rows


def load_libsodium_gem5(run, cfg="serdit"):
    """The gem5 rig's own CSV, not the silicon `cio.csv`.

    Arms map onto the same four letters: A base, C blanket, B the Apple bracket
    (`api`), T its instruction-matched NOP twin (`apinop` -- mrs -> mov, msr ->
    hint #0, the barrier -> hint #0, the conditional clear kept over a hint #0).

    `cfg` selects the switch model and defaults to `serdit`, the SERIALISING
    one, because that is what real silicon does and it is what makes this panel
    comparable to the M4 one. `spec` is the renamed counterfactual only a
    simulator can run.

    gem5 is deterministic, so there is one sample per cell and no MAD: a settled
    region is exact. The resolution question it replaces is the relink lottery
    (docs/results/dit-layout-lottery-2026-09-06.md) -- linking the UNHARDENED
    library at a different address moves these benchmarks 0.50 to 7.04 points,
    which is a floor no number of repetitions can see. `worst_mad` carries that
    per-benchmark floor instead, so `res_*` still means "clears the noise".
    """
    path = run / "results.csv"
    if not path.exists():
        sys.exit(f"no such run: {path}")
    per = {}
    with open(path) as fh:
        for r in csv.DictReader(l for l in fh if not l.startswith("#")):
            per[(r["bench"], r["arm"], r["cfg"])] = (
                float(r["cycles_per_op"]), float(r["insts_per_op"]))

    rows = []
    for key, label, sub in LIBSODIUM:
        arm = {}
        for a, name in (("A", "base"), ("C", "blanket"),
                        ("B", "api"), ("T", "apinop")):
            v = per.get((key, name, cfg))
            if v is None:
                # A benchmark whose arms are not all built is DROPPED with a
                # note, not defaulted: argon2id's twin is two cells of ~2.5 h,
                # so it can lag the rest, and a silently missing bar is worse
                # than a shorter figure.
                print(f"  skipping {key}: arm {name!r} not in {path.name}")
                arm = None
                break
            arm[a] = {"cyc": v[0], "ins": v[1], "mad": 0.0, "reps": 1}
        if arm is None:
            continue
        r = _row(label, sub, arm["A"], arm["C"], arm["B"], arm["T"],
                 worst_mad=RELINK_FLOOR.get(key, 0.0))
        rows.append(r)
    rows.sort(key=lambda d: -d["ovh_bracket"])
    return rows


def load_wordpress(run):
    cache, rows = {}, []
    for fname, key, label, sub, crossings in WORDPRESS:
        if fname not in cache:
            path = run / f"{fname}.json"
            if not path.exists():
                sys.exit(f"no such run: {path}")
            cache[fname] = json.load(open(path))
        scen = cache[fname].get(key)
        if scen is None:
            sys.exit(f"{fname}.json: no scenario {key!r}")
        cyc, ins = scen["median"], scen["median_ins"]
        # `median` is KILOcycles; `median_ins` is instructions.
        arm = {a: {"cyc": cyc[a] * 1e3, "ins": float(ins[a]), "reps": scen["n"][a]}
               for a in ("A", "C", "B", "Bn")}
        # experiment 11's own rule: "MAD is the median absolute deviation of A's
        # samples as a percent of its median. A value under it is zero."
        r = _row(label, sub, arm["A"], arm["C"], arm["B"], arm["Bn"],
                 worst_mad=scen["mad"] / 100.0)
        r["crossings"] = crossings
        rows.append(r)
    return rows


def tick_step(span):
    return 25 if span > 80 else 10 if span > 30 else 5 if span > 12 else 2


def pct(v):
    return f"{'+' if v >= 0 else MINUS}{abs(v) * 100:.2f}%"


# --------------------------------------------------------------------------- #
# the paper figure
# --------------------------------------------------------------------------- #
def tick_label(d, narrow):
    """Benchmark name only, wrapped so it fits its own group.

    At \\columnwidth the six groups get about 0.48 in each, which is roughly
    twelve characters at the tick size. Names that overrun that are broken at
    their own hyphen rather than abbreviated: `chacha20-poly1305` is the
    primitive's name and a reader looking for it in the text should find the
    same string.
    """
    parts = [d["label"]]
    if narrow and len(d["label"]) > 12 and "-" in d["label"]:
        head, _, tail = d["label"].partition("-")
        parts = [head + "-", tail]
    if d["sub"]:
        parts.append(d["sub"])
    return "\n".join(parts)


def value_label(v, narrow):
    """At \\columnwidth a group is ~35 pt and holds two of these.

    +113.75% needs 22 pt on its own, so the narrow render rounds and drops the
    sign -- the y axis already says the unit, and printing it twelve times is
    what makes the pair collide on the rows where both arms are near zero.
    """
    if narrow:
        t = f"{v:+.0f}" if abs(v) >= 10 else f"{v:+.1f}"
        if float(t) == 0:
            t = t[1:]
        return t.replace("-", MINUS)
    return f"{v:+.2f}%".replace("-", MINUS)


def png(panels, out, narrow=True):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    # Sized for a two-column paper: 3.35 in is ACM sigconf's \columnwidth
    # (241 pt) and just under IEEE's 3.5 in, so \includegraphics[width=
    # \columnwidth] lands at 1:1 or scales DOWN a hair. Every size below is in
    # points at that width -- do not author this wide and scale up in LaTeX,
    # which is what makes a figure's type smaller than its caption.
    F = dict(fig=(3.35, 3.9), tick=5.4, val=4.4, ylab=6.4, ytick=5.4,
             tag=6.6, legend=5.6, barw=0.38, hspace=0.46,
             left=0.145, bottom=0.115, top=0.945)
    if not narrow:
        F = dict(fig=(10.2, 8.4), tick=8.4, val=7.4, ylab=11, ytick=9,
                 tag=10, legend=9.5, barw=0.34, hspace=0.50,
                 left=0.085, bottom=0.09, top=0.95)

    plt.rcParams.update({
        "font.family": "sans-serif",
        # Helvetica registers only a 400 face on macOS, so fontweight="bold"
        # silently falls back to regular. Arial carries a real 700.
        "font.sans-serif": ["Arial", "DejaVu Sans", "Helvetica"],
        "font.size": F["tick"],
        "axes.edgecolor": "#D6DCDF",
        "text.color": INK_L, "axes.labelcolor": INK_L,
        "xtick.color": INK_L, "ytick.color": INK_L,
        "figure.facecolor": "none", "axes.facecolor": "none",
        "axes.linewidth": 0.6,
    })

    # One panel or two. The gem5 run has no experiment-11 counterpart, so its
    # figure is panel (a) alone; height scales with the panel count so a single
    # panel is not stretched to a two-panel box.
    n = len(panels)
    figsize = F["fig"] if n == 2 else (F["fig"][0], F["fig"][1] * 0.56)
    fig, axes = plt.subplots(n, 1, figsize=figsize, squeeze=False)
    axes = [a for row in axes for a in row]
    BW = F["barw"]

    for ax, panel in zip(axes, panels):
        rows = panel["rows"]
        blank = [d["ovh_blanket"] * 100 for d in rows]
        brack = [d["ovh_bracket"] * 100 for d in rows]
        x = range(len(rows))

        ax.bar([i - BW / 2 for i in x], blank, width=BW, color=BLANKET_L,
               zorder=3, label="Blanket DIT")
        ax.bar([i + BW / 2 for i in x], brack, width=BW, color=BRACKET_L,
               zorder=3, label="Function Level DIT")

        hi = max(max(blank), max(brack), 0)
        lo = min(min(blank), min(brack), 0)
        span = hi - lo
        # Pad each end for its own value labels, not by a fraction of the whole
        # span: panel (a) dips 1.3 below zero out of 115, and padding that by
        # 17% of 115 opened 20 units of dead space between the bars' baseline
        # and the tick labels underneath.
        pad = span * 0.13
        ax.set_ylim(lo - span * 0.075 if lo < 0 else 0, hi + pad)
        ax.set_xlim(-0.60, len(rows) - 0.40)

        for i in x:
            for off, v in ((-BW / 2, blank[i]), (BW / 2, brack[i])):
                up = v >= 0
                ax.text(i + off, v + (span * 0.025 if up else -span * 0.025),
                        value_label(v, narrow),
                        ha="center", va="bottom" if up else "top",
                        fontsize=F["val"], fontweight="bold", color=INK_L,
                        zorder=5)

        ax.axhline(0, color="#8695A0", lw=0.7, zorder=4)

        step = tick_step(ax.get_ylim()[1] - ax.get_ylim()[0])
        first = -(int(-min(lo, 0) // step)) * step
        ticks, t = [], first
        while t <= hi + pad:
            ticks.append(t)
            t += step
        ax.set_yticks(ticks)
        ax.set_xticks(list(x))
        ax.set_xticklabels([tick_label(d, narrow) for d in rows],
                           fontsize=F["tick"], fontweight="bold")
        ax.tick_params(axis="x", length=0, pad=2.5 if narrow else 7)
        ax.tick_params(axis="y", labelsize=F["ytick"], length=2, pad=2)
        ax.set_ylabel("IPC Overhead (%)", fontsize=F["ylab"], fontweight="bold",
                      labelpad=5 if narrow else 10, color=INK_L)
        ax.yaxis.set_major_formatter(
            lambda v, _: f"{v:.0f}".replace("-", MINUS))
        for lab in ax.get_yticklabels():
            lab.set_fontweight("bold")
        ax.grid(axis="y", color="#E4E9EB", lw=0.6, zorder=0)
        ax.set_axisbelow(True)
        for sp in ("top", "right", "left", "bottom"):
            ax.spines[sp].set_visible(False)

        # Stacked panels have to say which is which: an identifier, not a title.
        ax.text(0, 1.0, panel["tag"], transform=ax.transAxes, ha="left",
                va="bottom", fontsize=F["tag"], fontweight="bold", color=INK_L)

    # Boxed, bold, and unfilled: a white fill would be an opaque patch in a
    # figure whose whole point is that it has no background.
    leg = axes[0].legend(loc="upper right", frameon=True,
                         prop={"weight": "bold", "size": F["legend"]},
                         handletextpad=.6, labelspacing=.38, borderpad=.45,
                         handlelength=1.4, borderaxespad=.3,
                         bbox_to_anchor=(1.0, 1.0),
                         facecolor="none", edgecolor=INK_L)
    leg.get_frame().set_linewidth(0.6)
    leg.get_frame().set_boxstyle("square", pad=0.35)

    fig.subplots_adjust(left=F["left"], right=0.982,
                        top=F["top"] if n == 2 else F["top"] - 0.035,
                        bottom=F["bottom"] if n == 2 else F["bottom"] + 0.06,
                        hspace=F["hspace"])
    for ext in ("png", "pdf"):
        p = out.with_suffix("." + ext)
        # No bbox_inches="tight" in the narrow render: it crops to the
        # drawn content, so the file comes out under 3.35 in and
        # \includegraphics[width=\columnwidth] scales it UP -- which is
        # exactly how a figure ends up with type larger than its caption.
        # transparent=True: no white patch behind the axes, so the figure
        # takes the page's own background in LaTeX and a slide's on screen.
        fig.savefig(p, dpi=400 if ext == "png" else None, transparent=True,
                    **({} if narrow
                       else dict(bbox_inches="tight", pad_inches=0.24)))
        print(f"wrote figures/{p.name}")


# --------------------------------------------------------------------------- #
# the review page -- same rows, so no number on it is typed by hand
# --------------------------------------------------------------------------- #
HTML = r"""<title>Blanket Against Function Level</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Archivo:wght@500;600;700&family=IBM+Plex+Mono:wght@400;500;600&family=Source+Serif+4:opsz,wght@8..60,400;8..60,600&display=swap">
<style>
:root{
  --ground:#F2F4F5; --surface:#FFFFFF; --sunk:#E9EDEF;
  --ink:#11171C; --muted:#5A6670; --faint:#8695A0;
  --rule:#D6DCDF; --rule-soft:#E4E9EB;
  --blanket:__BLANKET_L__; --bracket:__BRACKET_L__; --null:#7C8B95;
}
@media (prefers-color-scheme:dark){
  :root:not([data-theme="light"]){
    --ground:#0E1316; --surface:#161C21; --sunk:#1D252B;
    --ink:#E6ECEF; --muted:#9BAAB4; --faint:#6F808B;
    --rule:#2A343B; --rule-soft:#222B31;
    --blanket:__BLANKET_D__; --bracket:__BRACKET_D__; --null:#77878F;
  }
}
:root[data-theme="dark"]{
  --ground:#0E1316; --surface:#161C21; --sunk:#1D252B;
  --ink:#E6ECEF; --muted:#9BAAB4; --faint:#6F808B;
  --rule:#2A343B; --rule-soft:#222B31;
  --blanket:__BLANKET_D__; --bracket:__BRACKET_D__; --null:#77878F;
}
*{box-sizing:border-box}
body{background:var(--ground);color:var(--ink);
  font-family:"Source Serif 4",Georgia,serif;font-size:17px;line-height:1.62;
  -webkit-font-smoothing:antialiased}
.wrap{max-width:1140px;margin:0 auto;padding:0 28px 104px}
h1,h2,h3,.eyebrow,.meta,.legend,th,.tag,.ylab,.xlab,.ytick,.panelno{
  font-family:Archivo,"Helvetica Neue",Arial,sans-serif}
h1{font-size:clamp(2.2rem,5.2vw,3.5rem);line-height:1.04;letter-spacing:-.03em;
  font-weight:700;margin:0 0 .6rem;text-wrap:balance}
h2{font-size:1.28rem;letter-spacing:-.01em;font-weight:600;margin:0}
h3{font-size:.98rem;font-weight:600;margin:0;letter-spacing:-.005em}
p{margin:0 0 1.05rem}
.eyebrow{font-size:.7rem;font-weight:600;letter-spacing:.13em;text-transform:uppercase;
  color:var(--faint);margin:0 0 1.3rem}
.mono,code{font-family:"IBM Plex Mono",ui-monospace,Menlo,monospace;
  font-variant-numeric:tabular-nums}
code{background:var(--sunk);padding:.1em .38em;border-radius:3px;font-size:.86em}
header{padding:70px 0 34px}
.lede{font-size:1.2rem;line-height:1.5;margin:0 0 1.4rem;max-width:66ch}
.lede b{font-weight:600}
.meta{display:flex;flex-wrap:wrap;gap:6px 24px;padding-top:18px;
  border-top:1px solid var(--rule);
  font-family:"IBM Plex Mono",monospace;font-size:.72rem;color:var(--faint)}
.meta span{white-space:nowrap}
section{padding-top:50px}
.sechead{display:flex;align-items:baseline;gap:14px;margin-bottom:6px;flex-wrap:wrap}
.tag{font-family:"IBM Plex Mono",monospace;font-size:.7rem;color:var(--faint);
  letter-spacing:.06em}
.sub{color:var(--muted);font-size:.95rem;margin:0 0 24px;max-width:72ch}
.sub b{color:var(--ink);font-weight:600}
.legend{display:flex;flex-wrap:wrap;gap:8px 24px;align-items:center;
  margin:0 0 20px;font-size:.8rem;color:var(--muted)}
.legend i{display:inline-block;width:22px;height:10px;border-radius:2px;
  margin-right:8px;vertical-align:-1px}
.chartwrap{background:var(--surface);border:1px solid var(--rule);border-radius:4px;
  padding:22px 26px 16px;overflow-x:auto;margin-bottom:2px}
.panelno{font-size:.95rem;font-weight:700;color:var(--ink);display:block;
  margin-bottom:14px}
.chart{display:grid;grid-template-columns:24px 52px minmax(620px,1fr);
  grid-template-rows:320px auto;gap:0 10px}
.ylab{grid-row:1;writing-mode:vertical-rl;transform:rotate(180deg);
  justify-self:center;align-self:center;font-size:.72rem;font-weight:700;
  letter-spacing:.06em;color:var(--ink)}
.ycol{grid-row:1;position:relative}
.ytick{position:absolute;right:0;transform:translateY(50%);
  font-family:"IBM Plex Mono",monospace;font-size:.7rem;color:var(--ink);
  font-weight:600;font-variant-numeric:tabular-nums}
.plot{grid-row:1;position:relative}
.gline{position:absolute;left:0;right:0;height:0;border-top:1px solid var(--rule-soft)}
.gline.zero{border-top:1px solid var(--faint)}
.bars{position:absolute;inset:0;display:flex}
.col{flex:1;height:100%;position:relative}
.bar{position:absolute;width:30%;max-width:44px;min-height:2px}
.bar.blanket{background:var(--blanket);right:52%}
.bar.bracket{background:var(--bracket);left:52%}
.bar.up{border-radius:3px 3px 0 0}
.bar.down{border-radius:0 0 3px 3px}
.v{position:absolute;font-family:"IBM Plex Mono",monospace;font-size:.66rem;
  font-weight:700;font-variant-numeric:tabular-nums;color:var(--ink);
  white-space:nowrap;width:46%;text-align:center;
  transform:translateY(-5px)}
.v.blanket{right:50%}
.v.bracket{left:50%}
.v.below{transform:translateY(calc(100% + 5px))}
.xaxis{grid-row:2;grid-column:3;display:flex;padding-top:10px;
  border-top:1px solid var(--rule)}
.xlab{flex:1;text-align:center;font-size:.72rem;font-weight:700;color:var(--ink);
  line-height:1.3;padding:0 3px}
.xlab small{display:block;font-weight:600;color:var(--muted);font-size:.66rem;
  font-family:"IBM Plex Mono",monospace;margin-top:3px}

#tip{position:fixed;z-index:9;pointer-events:none;opacity:0;transition:opacity .1s;
  background:var(--surface);border:1px solid var(--rule);border-radius:4px;
  padding:9px 12px;box-shadow:0 6px 22px rgba(0,0,0,.16);
  font-family:"IBM Plex Mono",monospace;font-size:.71rem;line-height:1.65;
  color:var(--ink);font-variant-numeric:tabular-nums;width:292px}
#tip .t{font-family:Archivo,sans-serif;font-weight:600;font-size:.72rem;
  margin-bottom:4px;display:block}
#tip .m{color:var(--muted)}
#tip .sw{display:inline-block;width:9px;height:9px;border-radius:2px;
  margin-right:5px}
.tablewrap{overflow-x:auto;border:1px solid var(--rule);border-radius:4px;
  background:var(--surface);margin-bottom:26px}
table{border-collapse:collapse;width:100%;font-family:"IBM Plex Mono",monospace;
  font-size:.74rem;font-variant-numeric:tabular-nums;white-space:nowrap}
th,td{padding:8px 14px;text-align:right;border-bottom:1px solid var(--rule-soft)}
th{font-family:Archivo,sans-serif;font-size:.62rem;font-weight:600;
  letter-spacing:.08em;text-transform:uppercase;color:var(--faint);
  border-bottom:1px solid var(--rule)}
th:first-child,td:first-child{text-align:left;position:sticky;left:0;
  background:var(--surface)}
tbody tr:last-child td{border-bottom:none}
td.c{color:var(--blanket);font-weight:600}
td.k{color:var(--bracket);font-weight:600}
td.n{color:var(--null)}
.notes{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));
  gap:26px 34px}
.note h3{margin-bottom:.32rem}
.note p{font-size:.93rem;color:var(--muted);margin:0}
.note p b{color:var(--ink);font-weight:600}
footer{margin-top:58px;padding-top:20px;border-top:1px solid var(--rule);
  font-family:"IBM Plex Mono",monospace;font-size:.7rem;color:var(--faint);
  line-height:1.8}
@media (prefers-reduced-motion:reduce){*{transition:none!important}}
</style>

<div class="wrap">
<header>
  <p class="eyebrow">Experiments 09 &amp; 11 &nbsp;&middot;&nbsp; Apple M4</p>
  <h1>Two Cost Models</h1>
  <p class="lede">Blanket <code>PSTATE.DIT</code> and function-level DIT, <b>both against
  the same unhardened build</b>. Blanket is a fixed per-process tax that does not care what
  the request does. Function-level placement pays per crypto boundary crossing. Which is
  cheaper is a property of the <b>workload</b>, not of the mitigation &mdash; and these two
  panels are the two ends of that.</p>
  <div class="meta">__META__</div>
</header>

<section>
  <div class="sechead"><h2>IPC overhead against the unhardened build</h2>
    <span class="tag">arms C and B against arm A &nbsp;&middot;&nbsp; one denominator</span></div>
  <p class="sub">Bar height is <b>baseline IPC &divide; arm IPC &minus; 1</b>, so taller is
  slower and the two bars in a group share a denominator. IPC itself falls under a mode
  switch, so plotting arm&nbsp;&divide;&nbsp;baseline would draw a slowdown below zero.
  <b>The panels have independent y scales</b> &mdash; (a) spans 115 points and (b) spans 9.</p>
  <div class="legend">
    <span><i style="background:var(--blanket)"></i>Blanket DIT</span>
    <span><i style="background:var(--bracket)"></i>Function Level DIT</span>
  </div>
  <div id="panels"></div>
</section>

<section>
  <div class="sechead"><h2>The numbers</h2>
    <span class="tag">medians &middot; PMC cycles, PMC instructions</span></div>
  <p class="sub">The <code>vs twin</code> column is experiment 11's own reading of the
  bracket: measured against its instruction-matched NOP twin rather than the baseline,
  because emitting the switches moves code and relinking alone moved layout 3.9% in its
  pilot. It is the more conservative number and it is what <code>summary.txt</code> quotes.
  The chart uses the common denominator instead, so that the two bars in a group can be read
  against each other at all.</p>
  <div id="tables"></div>
</section>

<section>
  <div class="sechead"><h2>Reading it</h2><span class="tag">and what not to claim</span></div>
  <div class="notes">
    <div class="note"><h3>Blanket's penalty is invariant</h3>
    <p>On WordPress it is <b>+2.1% to +2.6% across a 600-fold change in crossings</b>. It
    suppresses the data-dependent optimisations for the whole process whether or not a
    secret is present, so it does not care what the request does.</p></div>
    <div class="note"><h3>Function-level tracks crossings</h3>
    <p><b>0.0% at 110 crossings, +1.1% at 8,230, +8.4% at 65,600</b>, in step with the
    crossings column and with nothing else. Two different cost models &mdash; and the
    crossover sits at 12,000&ndash;14,100 crossings per request.</p></div>
    <div class="note"><h3>No shipped configuration reaches it</h3>
    <p>The most crypto-dense pattern WordPress supports at stock settings is
    application-password REST auth, which re-verifies with phpass on <b>every</b> request,
    and it sits at 8,230. Getting to 65,600 takes raising phpass to 2<sup>16</sup> rounds
    &mdash; a change security guidance recommends.</p></div>
    <div class="note"><h3>Blanket adds no instructions</h3>
    <p>Arms A and C are the <b>same binary</b>, differing only in whether an injected
    constructor runs one <code>msr DIT, #1</code> before <code>main</code>. Its cost has no
    instruction component, which is why an IPC reading and a cycle reading agree on it.</p></div>
  </div>
</section>

<footer>
  Generated by <span class="mono">paper_experiments/fig_bracket_vs_blanket.py</span> from
  <span class="mono">09-libsodium-cio-parity/results/m4/cio.csv</span> and
  <span class="mono">11-php-src-suite/results/m4/*.json</span>.<br>
  Panel (b) is <span class="mono">11-php-src-suite/results/m4/summary.txt</span> TABLE 1, in
  its order. Percentages here are computed from full-precision medians; that table computes
  its own from the 2-decimal IPC values it prints, which differs by up to 0.25 points.
</footer>
</div>
<div id="tip" role="status"></div>

<script>
const PANELS = __DATA__;
const MINUS = "−";
const pct = v => (v >= 0 ? "+" : MINUS) + Math.abs(v * 100).toFixed(2) + "%";

const host = document.getElementById("panels");
const tables = document.getElementById("tables");
const tip = document.getElementById("tip");

PANELS.forEach((panel, pi) => {
  const rows = panel.rows;
  const all = rows.flatMap(d => [d.ovh_blanket * 100, d.ovh_bracket * 100]);
  const hi = Math.max(...all, 0), lo = Math.min(...all, 0);
  const span = hi - lo, pad = span * 0.16;
  const top = hi + pad, bot = lo - (lo < 0 ? pad : 0);
  const range = top - bot;
  const y = v => (v - bot) / range * 100;
  const zero = y(0);

  const wrap = document.createElement("div");
  wrap.className = "chartwrap";
  wrap.innerHTML = '<span class="panelno">' + panel.tag + "</span>" +
    '<div class="chart"><span class="ylab">IPC Overhead (%)</span>' +
      '<div class="ycol"></div><div class="plot"><div class="bars"></div></div>' +
      '<div class="xaxis"></div></div>';
  host.appendChild(wrap);

  const ycol = wrap.querySelector(".ycol");
  const plot = wrap.querySelector(".plot");
  const bars = wrap.querySelector(".bars");
  const xaxis = wrap.querySelector(".xaxis");

  const step = range > 80 ? 25 : range > 30 ? 10 : range > 12 ? 5 : 2;
  for (let t = Math.ceil(bot / step) * step; t <= top; t += step) {
    const p = y(t);
    const lab = document.createElement("span");
    lab.className = "ytick";
    lab.style.bottom = p + "%";
    lab.textContent = String(t).replace("-", MINUS);
    ycol.appendChild(lab);
    const g = document.createElement("i");
    g.className = "gline" + (Math.abs(t) < 1e-9 ? " zero" : "");
    g.style.bottom = p + "%";
    plot.appendChild(g);
  }

  rows.forEach((d, i) => {
    const col = document.createElement("div");
    col.className = "col";
    col.dataset.p = pi;
    col.dataset.i = i;
    col.innerHTML = [["blanket", d.ovh_blanket], ["bracket", d.ovh_bracket]]
      .map(([cls, raw]) => {
        const v = raw * 100, h = Math.abs(v) / range * 100, up = v >= 0;
        return '<div class="bar ' + cls + " " + (up ? "up" : "down") +
          '" style="bottom:' + (up ? zero : zero - h) + "%;height:" +
          Math.max(h, 0.3) + '%"></div>' +
          '<span class="v ' + cls + (up ? "" : " below") + '" style="bottom:' +
          (up ? zero + h : zero - h) + '%">' + pct(raw) + "</span>";
      }).join("");
    bars.appendChild(col);

    const x = document.createElement("span");
    x.className = "xlab";
    x.innerHTML = d.label + (d.sub ? "<small>" + d.sub + "</small>" : "");
    xaxis.appendChild(x);
  });

  const t = document.createElement("div");
  t.className = "tablewrap";
  t.innerHTML = "<table><thead><tr><th>" + panel.tag +
    "</th><th>baseline IPC</th><th>blanket IPC</th><th>fn-level IPC</th>" +
    "<th>blanket ovh</th><th>fn-level ovh</th><th>fn-level vs twin</th>" +
    "<th>blanket cycles</th><th>fn-level cycles</th></tr></thead><tbody>" +
    rows.map(d =>
      "<tr><td>" + d.label + (d.sub ? " " + d.sub : "") + "</td><td>" +
      d.ipc_base.toFixed(3) + "</td><td>" + d.ipc_blanket.toFixed(3) + "</td><td>" +
      d.ipc_bracket.toFixed(3) + '</td><td class="' + (d.res_blanket ? "c" : "n") +
      '">' + pct(d.ovh_blanket) + (d.res_blanket ? "" : " *") +
      '</td><td class="' + (d.res_bracket ? "k" : "n") + '">' + pct(d.ovh_bracket) +
      (d.res_bracket ? "" : " *") + '</td><td class="n">' +
      pct(d.ovh_bracket_twin) + '</td><td class="n">' + pct(d.cyc_blanket) +
      '</td><td class="n">' + pct(d.cyc_bracket) + "</td></tr>").join("") +
    "</tbody></table>";
  tables.appendChild(t);
});

document.querySelectorAll(".col").forEach(col => {
  const panel = PANELS[+col.dataset.p], d = panel.rows[+col.dataset.i];
  const show = e => {
    tip.innerHTML = '<span class="t">' + d.label + (d.sub ? " " + d.sub : "") +
      "</span>" +
      'baseline IPC ' + d.ipc_base.toFixed(3) + "<br>" +
      '<i class="sw" style="background:var(--blanket)"></i>blanket ' +
      d.ipc_blanket.toFixed(3) + " &nbsp;<b>" + pct(d.ovh_blanket) + "</b><br>" +
      '<i class="sw" style="background:var(--bracket)"></i>fn-level ' +
      d.ipc_bracket.toFixed(3) + " &nbsp;<b>" + pct(d.ovh_bracket) + "</b><br>" +
      '<span class="m">fn-level vs its NOP twin ' + pct(d.ovh_bracket_twin) +
      "</span><br>" +
      '<span class="m">cycles ' + pct(d.cyc_blanket) + " / " + pct(d.cyc_bracket) +
      "</span><br>" +
      '<span class="m">' + d.reps + " samples &middot; noise floor " +
      (d.worst_mad * 100).toFixed(2) + "%</span>" +
      (d.crossings === undefined ? "" :
        '<br><span class="m">' + d.crossings.toLocaleString("en-US") +
        " crypto crossings/request</span>");
    tip.style.opacity = 1;
    tip.style.left = Math.min(e.clientX + 14, innerWidth - 306) + "px";
    tip.style.top = Math.min(e.clientY + 14, innerHeight - tip.offsetHeight - 12) + "px";
  };
  col.addEventListener("mousemove", show);
  col.addEventListener("mouseleave", () => { tip.style.opacity = 0; });
});
</script>
"""


def provenance():
    facts = ["Mac16,10 Apple M4", "rooted, pinned cpu 9", "PMC0 / PMC1",
             "09: 15 reps, 1 region in 64", "11: 100 requests per arm",
             "2026-09-06"]
    return "".join(f"<span>{f}</span>" for f in facts)


def html(panels, out):
    page = (HTML
            .replace("__BLANKET_L__", BLANKET_L).replace("__BRACKET_L__", BRACKET_L)
            .replace("__BLANKET_D__", BLANKET_D).replace("__BRACKET_D__", BRACKET_D)
            .replace("__META__", provenance())
            .replace("__DATA__", json.dumps(panels, separators=(",", ":"))))
    out.write_text(page, encoding="utf-8")
    print(f"wrote figures/{out.name}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--machine", default="m4",
                    help="m4 / m5 (silicon, two panels) or gem5 (one panel)")
    ap.add_argument("--cfg", default="serdit", choices=("serdit", "spec"),
                    help="gem5 only: serialising (default, what silicon does) "
                         "or the renamed counterfactual")
    ap.add_argument("--wide", action="store_true",
                    help="render for a screen instead of \\columnwidth")
    ap.add_argument("--png-only", action="store_true")
    ap.add_argument("--html-only", action="store_true")
    a = ap.parse_args()

    if a.machine == "gem5":
        # One panel: there is no experiment-11 gem5 run to pair it with. The
        # tag names the switch model, because on this rig that is a choice and
        # on silicon it is not.
        model = "serialising" if a.cfg == "serdit" else "renamed"
        panels = [
            {"tag": f"libsodium primitives, gem5, {model} msr DIT",
             "rows": load_libsodium_gem5(E09 / "results" / "gem5", a.cfg)},
        ]
    else:
        panels = [
            {"tag": "(a) libsodium primitives",
             "rows": load_libsodium(E09 / "results" / a.machine)},
            {"tag": "(b) WordPress 6.2",
             "rows": load_wordpress(E11 / "results" / a.machine)},
        ]

    for panel in panels:
        print(f"\n{panel['tag']}")
        for d in panel["rows"]:
            name = (d["label"] + " " + d["sub"]).strip()
            print(f"  {name:<34} IPC {d['ipc_base']:.3f}  "
                  f"blanket {pct(d['ovh_blanket']):>9}  "
                  f"fn-level {pct(d['ovh_bracket']):>9}  "
                  f"(vs twin {pct(d['ovh_bracket_twin']):>9})")

    stem = OUT / ("bracket-vs-blanket-gem5" + ("" if a.cfg == "serdit" else "-renamed")
                  if a.machine == "gem5" else f"bracket-vs-blanket-{a.machine}")
    OUT.mkdir(exist_ok=True)
    if not a.html_only:
        png(panels, stem, narrow=not a.wide)
    if not a.png_only:
        html(panels, stem.with_suffix(".html"))


if __name__ == "__main__":
    main()
