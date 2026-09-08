#!/usr/bin/env python3
"""Experiment 14 under gem5: AWS-LC's shipped DIT bracket, with the switch model as an axis.

THE QUESTION SILICON CANNOT ANSWER. The M4 run prices one bracket entry at 154 cycles
(94 for a single-block AES entry) and reads AWS-LC's own claim, "the effect is mostly due
to setting and resetting the DIT flag", as confirmed because B - A is constant in CYCLES
across chunk sizes. But an M4 has exactly one `msr DIT` implementation and it serialises,
so "mostly the switch" cannot be turned off there, and the entries-per-operation column
(1,033 for CMAC at 16 KB, 3.4 for the EVP layer) is not measured -- it is B - A divided by
an assumed unit price, which reproduces the measurement by construction.

gem5 gives both. A flag on the decoder selects the serialising path; without it the write is
a renamed CC-register write, same binary, one mechanism changed. And commit.ditSetImm /
ditClearImm / ditRead COUNT the entries inside the ROI instead of inferring them.

THE COMPILER IS NOT INVOLVED. This is the vendor's bracket, built with the platform clang
the way AWS-LC's users build it. Nothing here runs the taint pass.

ARMS. The silicon rig's six, one for one, with `isb sy` where it uses `sb`: gem5 has no
FEAT_SB (the same substitution cioparity/api_bracket.c makes). No arm exists here that the
silicon rig does not have, so the two tables compare row for row.

  A    rel                     unhardened
  C    rel, AWSLC_BLANKET=1    blanket: DIT set before main by the linked constructor,
                               the library never touches it. SAME BINARY as A.
  B    dit                     the bracket as shipped: mrs; msr dit,#1 ... msr dit,#0
  Bi   ditisb                  B with `isb sy` after the enable
  H    dit, `-dit`             AWS's mitigation: DIT set once for the run; each entry still
                               mrs + msr dit,#1, and the enable no longer changes the mode
  Hi   ditisb, `-dit`          the same on the isb build

WHY NO REPS. gem5 is deterministic, so the silicon rig's 7 reps and medians are replaced by
exact gates. Host load and core count cannot move a simulated cycle.

ROI. One stats dump per benchmark row, covering M5_CALLS calls (not one: an m5-delimited
region carries a fixed 0-or-+400-instruction marker slop, and a 16-byte AES-GCM seal is
~190 cycles). The patched speed.cc stamps each row's dump index into its JSON as "roi", so
dumps and rows are joined by number.
"""
import argparse, csv, hashlib, json, os, pathlib, re, shutil, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

G5 = pathlib.Path(os.environ.get("G5", pathlib.Path(__file__).resolve().parents[3] / "gem5-DIT"))
CONFIG = G5 / "configs/example/arm/fdp_neoverse_v2_binary.py"
WORK = pathlib.Path(os.environ.get("W", pathlib.Path.home() / "Documents/dit-awslc-gem5"))

# The switch-model axis. Everything else about the machine is held fixed; the three
# optimization flags are the DIT-gated ones and must be ON, or there is nothing for the
# mode to suppress and every arm reads the same. Named by experiment 12's numbering of the
# four `MSR DIT` designs:
#   spec   = design 4, renamed with a deferred clear (the frontier default). The
#            counterfactual: no silicon implements it.
#   serdit = design 2, FLUSH AFTER, and THE SERIALISING MODEL THIS EXPERIMENT RUNS: the
#            front end runs on under the old mode and everything younger is refetched when
#            the MSR commits. `--dit-flush-after` implies --no-speculative-dit.
#   drain  = design 1, rename stalls until the ROB empties. NOT run by default, kept
#            selectable. Note 12-dit-clear-shadow/README.md calibrates DRAIN against
#            silicon (~27 cycles per executed write here, ~30 on the M5, 34.3 in
#            experiment 06), calls design 1 "the one that ships", and states that
#            flush-after is NOT a model of Apple's switch while measuring it as the more
#            expensive of the two (108 cycles per round against 87).
# Two switch models, which is the whole surface gem5 now exposes.
#   drain     the default, selected by NO flag: an immediate `msr dit, #imm`
#             serialises unless it writes the value already committed, which
#             changes nothing and so does not drain. The model that can be
#             checked against silicon.
#   expedite  ExpeDITe: the renamed switch with a deferred clear, and the
#             architectural-ready MRS read that belongs to that design.
CONFIGS = {
    "apple":    ["--apple", "--eves", "--dmp", "--comp-simp"],
    "expedite": ["--expedite", "--eves", "--dmp", "--comp-simp"],
}
CONFIG_LABEL = {"apple":    "Apple: the switch flushes after it (squash at commit)",
                "expedite": "ExpeDITe: renamed switch, deferred clear"}

# THE ARM SET DEPENDS ON THE SWITCH DESIGN, because a barrier is a property of the design
# rather than of the workload. Under ExpeDITe's renamed switch the clear is deferred until
# it provably cannot be squashed, so the region is safe with no barrier at all and a
# barrier arm would be measuring a recipe nobody would write: blanket, the shipped bracket
# and the vendor's hoisted mitigation are the three arms that mean anything.
#
# Under a serialising switch the barrier does matter, but only where the write ahead of it
# does NOT change the mode. In the shipped bracket the enable changes the mode and
# serialises, so a barrier after it finds the pipeline already empty (4 cycles on the M4).
# In the HOISTED arm the enable is redundant, changes nothing and therefore does not
# serialise, so the barrier has a full pipeline to drain (63 cycles on the M4) -- which is
# why hoisted-plus-barrier is an arm there and bracket-plus-barrier is not.
CONFIG_ARMS = {
    "apple":    ["A", "C", "B", "H"],
    "expedite": ["A", "C", "B", "H"],
}

# arm -> (build, blanket, extra argv to bssl speed)
# No barrier arm. gem5 has no FEAT_SB and `isb sy` in its place was only ever a
# stand-in, so the three arms that carry meaning are blanket, the shipped
# bracket and the vendor's hoisted mitigation, against the unhardened baseline.
# The ditisb build is no longer needed.
# ONE hardened build for both switch models: ditisb carries `isb sy` after the
# enable, which Apple's design needs and the renamed switch does not. gem5 drops
# the barrier's ORDERING at rename under --expedite while still fetching and
# retiring it, so both models run the same binary and the barrier's cost is
# measured with instruction count and layout held constant.
ARMS = {
    "A": ("rel", 0, []),
    "C": ("rel", 1, []),
    "B": ("ditisb", 0, []),
    "H": ("ditisb", 0, ["-dit"]),
}
ARM_ORDER = ["A", "C", "B", "H"]
# Arms in which no `msr DIT` ever executes: the switch model must not move them, and dwell
# must be exactly zero.
INERT = ("A",)
# Arms that must commit DIT writes inside the ROI, or the bracket is not running.
MUST_TOGGLE = ("B", "H")

STAT_RE = re.compile(r"^(\S+)\s+([-\d.]+(?:e[-+]?\d+)?|nan|inf|-inf)\s")


def all_dumps(path):
    """Every Begin/End block, in order. One block per benchmark row."""
    dumps, cur, inside = [], {}, False
    with open(path) as fh:
        for line in fh:
            if line.startswith("---------- Begin Simulation Statistics"):
                inside, cur = True, {}
                continue
            if line.startswith("---------- End Simulation Statistics"):
                if inside:
                    dumps.append(cur)
                inside = False
                continue
            if not inside:
                continue
            m = STAT_RE.match(line)
            if m:
                try:
                    cur[m.group(1)] = float(m.group(2))
                except ValueError:
                    pass
    return dumps


def pick(s, *keys):
    for k in keys:
        for full, v in s.items():
            if full.endswith(k):
                return v
    return None


def canon(src, *key):
    """Hard-link to a fixed-WIDTH path. gem5 SE writes the binary path onto the initial
    process stack as argv[0], so its LENGTH shifts stack alignment for the whole run: one
    byte-identical binary measured 287,318 / 285,068 / 284,936 cycles at a 1-, 36- and
    18-char name -- 0.84% from the file name alone (cioparity/run_cio_gem5.py)."""
    slot = hashlib.md5("/".join(map(str, key)).encode()).hexdigest()[:8]
    root = pathlib.Path("/tmp") / ("awslc_" + hashlib.md5(str(WORK).encode()).hexdigest()[:12])
    c = root / slot / "b"
    c.parent.mkdir(parents=True, exist_ok=True)
    if c.exists():
        c.unlink()
    try:
        os.link(src, c)
    except OSError:
        shutil.copy2(src, c)
    return c


ROW_RE = re.compile(r'\{"description":.*?\}', re.S)


def rows_from_log(text):
    """Every JSON row object, out of gem5's own chatter on the same stream.

    Deliberately NOT "find the array and parse it". `Speed()` prints its rows as it goes and
    closes the array only at the very end, so a tool that exits early -- which it does on any
    benchmark returning false -- prints hundreds of valid rows and no `]`. Requiring the
    closing bracket threw away 99 cells of a 126-cell sweep that had each produced real
    measurements before aborting. Objects are self-delimiting; take them one at a time."""
    out = []
    for m in ROW_RE.finditer(text):
        try:
            out.append(json.loads(m.group(0)))
        except json.JSONDecodeError:
            pass
    return out


def row_key(r):
    size = r.get("bytesPerCall") or r.get("primeSizePerCall") or 0
    return f"{r['description']}" + (f" [{size} B]" if r.get("bytesPerCall")
                                    else (f" [{size}-bit]" if r.get("primeSizePerCall") else ""))


def slug(f):
    return re.sub(r"[^A-Za-z0-9]+", "-", f).strip("-")


def parse_filters(spec, calls, warm):
    """`name[:calls[:warm]]`, comma separated.

    The call count belongs to the FILTER, not to the sweep. gem5 is deterministic, so a
    settled region is exact and every extra call is simulated cycles for nothing -- the
    same reasoning run_cio_gem5.py applies when it gives ed25519 20 iterations and argon2id
    exactly 1. Measured here at 20 calls: the `P-256` filter is ten rows of 37k to 244k
    cycles PER OPERATION (EC POINT mul, ECDH, ECDSA sign and verify, three key
    generations), 1.17M cycles per call across the set, where an AEAD 16-byte seal is 426.
    A flat count that suits the seal is twelve hours for the curve."""
    out = []
    for item in spec.split(","):
        if not item.strip():
            continue
        parts = item.split(":")
        name = parts[0]
        c = int(parts[1]) if len(parts) > 1 and parts[1] else calls
        w = int(parts[2]) if len(parts) > 2 and parts[2] else warm
        out.append(dict(label=name, filter=name, calls=c, warm=w, calls_list=None, rows=None))
    return out


def load_slices(path, chunks):
    """Slices from gen_calls_list.py: one per gem5 process, each carrying a per-row call
    count list in which every row outside the slice is 0 (= not run at all)."""
    d = json.load(open(path))
    if d["chunks"] != chunks:
        raise SystemExit(f"slice file was generated for -chunks {d['chunks']}, not {chunks}: "
                         "row order and membership would not line up with the counts")
    return [dict(label=s["label"], filter=s["filter"], calls=None, warm=s["warm"],
                 calls_list=s["calls_list"], rows=s["rows"]) for s in d["slices"]]


def run_one(job):
    arm, cfg, sl, a, outroot = job
    build, blanket, extra = ARMS[arm]
    filt = sl["filter"]
    tag = f"{arm}__{cfg}__{slug(sl['label'])}"
    d = outroot / tag
    d.mkdir(parents=True, exist_ok=True)
    src = WORK / f"build-{build}" / "tool" / "bssl"
    if not src.exists():
        return {"arm": arm, "cfg": cfg, "filter": sl["label"], "error": f"missing {src}"}
    binpath = canon(src, build, arm, cfg, sl["label"])

    # The filter goes in the ENVIRONMENT, never on argv. gem5 splits --arguments on
    # whitespace, so a filter naming "ECDSA P-256" arrives as two arguments and the tool
    # exits with "Missing argument for option"; an empty one collapses to a bare -filter
    # with the same result. M5_FILTER (patch_speed_m5.py) sidesteps the splitting entirely,
    # which is what makes segmenting the suite by filter possible at all.
    # -threads 1 pins the only multithreaded benchmark (CRYPTO_refcount_inc) to its
    # single-thread case, which the speed patch runs inline. gem5 SE on one CPU cannot
    # create a thread at all. Inert for every other benchmark: g_threads has one user.
    argv = ["speed", "-json", "-chunks", a.chunks, "-threads", "1"] + extra
    cmd = [str(G5 / "build/ARM" / a.gem5), f"--outdir={d}", str(CONFIG),
           "--binary", str(binpath), "--arguments", " ".join(argv),
           "--env", f"M5_WARM={sl['warm']}",
           "--env", f"AWSLC_BLANKET={blanket}"] + CONFIGS[cfg]
    if filt:
        cmd += ["--env", f"M5_FILTER={filt}"]
    if sl["calls_list"] is not None:
        cmd += ["--env", f"M5_CALLS_LIST={sl['calls_list']}"]
    else:
        cmd += ["--env", f"M5_CALLS={sl['calls']}"]

    t0 = time.time()
    timed_out = False
    if not ((d / "stats.txt").exists() and a.resume):
        # A cell that hangs would otherwise block the whole sweep indefinitely, which
        # matters most for an unattended run: a single benchmark that gem5 SE cannot
        # service (a spin on an unimplemented facility, say) has no other backstop. The
        # cell is killed and recorded as a failure; the rest of the sweep is unaffected.
        try:
            p = subprocess.run(cmd, capture_output=True, text=True, cwd=str(d),
                               timeout=a.cell_timeout)
            out = p.stdout + p.stderr
        except subprocess.TimeoutExpired as e:
            timed_out = True
            # TimeoutExpired carries the partial output as BYTES even under text=True, so
            # decode before touching it: concatenating a str to it raises TypeError inside
            # the worker, which propagates out of ex.map and kills the whole sweep before a
            # single result is written. That is exactly what it did.
            def _txt(x):
                return x.decode("utf8", "replace") if isinstance(x, (bytes, bytearray)) else (x or "")
            out = _txt(e.stdout) + _txt(e.stderr)
            out += f"\n\nCELL TIMED OUT after {a.cell_timeout}s and was killed\n"
        (d / "run.log").write_text(out if isinstance(out, str) else out.decode("utf8", "replace"))
        (d / "cmd.txt").write_text(" ".join(cmd) + "\n")
    wall = time.time() - t0

    log = (d / "run.log").read_text() if (d / "run.log").exists() else ""
    dumps = all_dumps(d / "stats.txt") if (d / "stats.txt").exists() else []
    jrows = rows_from_log(log)

    cell = {"arm": arm, "cfg": cfg, "filter": sl["label"], "want_rows": sl["rows"],
            "wall_s": round(wall, 1), "dumps": len(dumps), "timed_out": timed_out,
            "json_rows": len(jrows), "rows": {},
            # Whole-run instruction count for the cross-model determinism gate. The LAST
            # dump is gem5 teardown after the final reset, so sum every dump.
            "sim_insts_whole": sum(v for v in (pick(s, "commitStats0.numInsts", "simInsts")
                                               for s in dumps) if v is not None) or None,
            "failed_assert": ("FAILURE" in log) or ("Assertion" in log) or ("panic" in log)}

    for r in jrows:
        i = int(r.get("roi", -1))
        if not (0 <= i < len(dumps)):
            continue
        s, n = dumps[i], r.get("numCalls") or 0
        if not n:
            continue
        cyc = pick(s, "core.numCycles", "numCycles")
        ins = pick(s, "commitStats0.numInsts", "simInsts")
        g = lambda *k: (pick(s, *k) or 0.0)
        cell["rows"][row_key(r)] = {
            "roi": i, "calls": n,
            "cycles_total": cyc, "insts_total": ins,
            "cycles_per_op": round(cyc / n, 2) if cyc else None,
            "insts_per_op": round(ins / n, 2) if ins else None,
            "ipc": round(ins / cyc, 3) if cyc and ins else None,
            "dit_writes_per_op": round(g("commit.ditWrites") / n, 3),
            "dit_set_per_op": round(g("commit.ditSetImm") / n, 3),
            "dit_set_redundant_per_op": round(g("commit.ditSetImmRedundant") / n, 3),
            "dit_clear_per_op": round(g("commit.ditClearImm") / n, 3),
            "dit_read_per_op": round(g("commit.ditRead") / n, 3),
            "dit_writes": g("commit.ditWrites"), "dit_cycles": g("commit.ditCycles"),
            "dit_suppressed": g("ditSuppressed"),
            "dit_dwell_frac": round(g("commit.ditCycles") / cyc, 4) if cyc else None,
        }
    if timed_out:
        cell["error"] = f"timed out after {a.cell_timeout}s and was killed"
    elif not cell["rows"]:
        cell["error"] = (f"no rows joined (dumps={len(dumps)}, json rows={len(jrows)})")
    elif sl["rows"] and len(cell["rows"]) != sl["rows"]:
        # the slice asked for N rows and got fewer: those benchmarks did not run under
        # gem5 SE. This is what the feasibility screen is for.
        cell["error"] = (f"{len(cell['rows'])} of {sl['rows']} rows measured -- "
                         f"{sl['rows'] - len(cell['rows'])} did not run")
    print(f"  [{'ok ' if not cell.get('error') else 'ERR'}] {tag:14s} rows={len(cell['rows'])} "
          f"({wall:.0f}s)", flush=True)
    return cell


def gates(cells, arms, cfgs):
    """Exact gates. gem5 is deterministic, so any of these failing is a real defect."""
    fails, notes = [], []
    # each filter is its own process, so an (arm, model) pair is spread over several
    # cells: merge them before the gates that reason about a whole arm
    by = {}
    for c in cells:
        if c.get("error"):
            continue
        m = by.setdefault((c["arm"], c["cfg"]),
                          {"arm": c["arm"], "cfg": c["cfg"], "rows": {},
                           "sim_insts_whole": 0})
        m["rows"].update(c["rows"])
        m["sim_insts_whole"] += c["sim_insts_whole"] or 0
    keys = sorted({k for c in cells for k in c.get("rows", {})})

    for c in cells:
        if c.get("error"):
            fails.append(f"{c['arm']}/{c['cfg']}/{c.get('filter','')}: {c['error']}")
            continue
        if c["failed_assert"]:
            fails.append(f"{c['arm']}/{c['cfg']}: the simulator reported a failure or panic")
        if c["dumps"] and c["json_rows"] and c["json_rows"] > c["dumps"]:
            fails.append(f"{c['arm']}/{c['cfg']}: {c['json_rows']} rows but only {c['dumps']} "
                         "dumps -- a row ran outside an ROI")
        for k, r in c["rows"].items():
            if c["arm"] in INERT:
                if r["dit_writes"]:
                    fails.append(f"{c['arm']}/{c['cfg']}/{k}: {r['dit_writes']} DIT writes in "
                                 "an arm that must have none")
                if r["dit_cycles"]:
                    fails.append(f"{c['arm']}/{c['cfg']}/{k}: ditCycles={r['dit_cycles']} with "
                                 "the mode never set")
            # NOT A GATE: zero DIT writes on a row. AWS-LC brackets its SECRET-HANDLING
            # entry points, not its API, so a benchmark that touches only public material
            # enters no bracketed function and correctly commits nothing. Measured: ECDSA
            # P-256 *verify*, EC POINT add and dbl are all 0/0/0, while signing is 3/3/3
            # and EVP ECDH is 34/34/16. That is coverage information, reported below, and
            # as a gate it failed 24 cells on correct behaviour. The real check that the
            # build is live is per ARM, further down.

    # The bracket must be live SOMEWHERE in each bracketed arm. This is the check the
    # per-row version was trying to be: if no row in a whole arm commits a DIT write, the
    # option is off or HWCAP DIT is missing, which is a real defect.
    for (arm, cfg), m in sorted(by.items()):
        if arm in MUST_TOGGLE and not sum(r["dit_writes"] for r in m["rows"].values()):
            fails.append(f"{arm}/{cfg}: not one DIT write in {len(m['rows'])} rows -- the "
                         "bracket is not running (HWCAP DIT missing, or the option is off)")
    # Which rows the vendor's bracket does not reach, and the check that every bracketed
    # arm agrees on that set. A row uncovered in B but covered in Bi would mean the variant
    # builds disagree about placement, which they must not.
    uncovered = {}
    for arm in MUST_TOGGLE:
        for cfg in cfgs:
            m = by.get((arm, cfg))
            if not m:
                continue
            uncovered[(arm, cfg)] = {k for k, r in m["rows"].items() if not r["dit_writes"]}
    if uncovered:
        sets = list(uncovered.values())
        if any(u != sets[0] for u in sets):
            odd = {f"{a}/{c}": len(u) for (a, c), u in uncovered.items()}
            fails.append(f"the bracketed arms disagree about which rows are uncovered {odd} "
                         "-- the variant builds do not share the vendor's placement")
        elif sets[0]:
            notes.append(f"{len(sets[0])} row(s) enter no bracketed entry point in any arm, so "
                         f"the vendor's bracket does not cover them: {sorted(sets[0])}")

    # One binary, one input, two machine configs: the instruction stream must be identical.
    # If it is not, the tool is timing itself -- the simulated-time trap.
    for arm in arms:
        vals = {c: by[(arm, c)]["sim_insts_whole"] for c in cfgs if (arm, c) in by}
        if len({v for v in vals.values() if v is not None}) > 1:
            fails.append(f"{arm}: simInsts differs across switch models {vals} -- the run "
                         "depends on its own timing")
    # An arm in which no `msr DIT` ever executes must be UNAFFECTED by the switch model:
    # identical cycles, not merely close. This is the control for the axis itself.
    for arm in arms:
        if arm not in INERT:
            continue
        for k in keys:
            vals = {c: by[(arm, c)]["rows"].get(k, {}).get("cycles_total")
                    for c in cfgs if (arm, c) in by}
            vs = [v for v in vals.values() if v is not None]
            if len(set(vs)) > 1:
                # Not exactly zero, and the cause is known: DIT instructions that EXIST in
                # .text and are decoded on squashed paths stall rename under
                # --no-speculative-dit even though none ever commits. Proved by a probe
                # binary with zero DIT instructions, which is bit-identical across the two
                # models on every dump. Arm A carries five (AWS-LC's out-of-line
                # armv8_{get,set,restore}_dit plus the blanket constructor's own write), so
                # the residual is reported with its size rather than asserted away. A real
                # divergence in the axis would scale with the workload; this does not.
                drift = (max(vs) - min(vs)) / min(vs)
                msg = (f"{arm}/{k}: cycles differ across switch models by {drift:.3%} {vals} "
                       "-- no DIT commits here; see the zero-DIT probe")
                (fails if drift > 0.005 else notes).append(msg)
    return fails, notes


def table(cells, arms, cfgs):
    by = {}
    for c in cells:
        if c.get("error"):
            continue
        by.setdefault((c["arm"], c["cfg"]), {"rows": {}})["rows"].update(c["rows"])
    keys = sorted({k for c in cells for k in c.get("rows", {})})
    for cfg in cfgs:
        a_cell = by.get(("A", cfg))
        if not a_cell:
            continue
        print(f"\n=== {cfg}: {CONFIG_LABEL.get(cfg, cfg)} ===")
        cfg_arms = [x for x in CONFIG_ARMS.get(cfg, arms) if x in arms]
        print(f"{'row':34s}{'A cyc/op':>10s}{'IPC A':>7s}" +
              "".join(f"{a:>9s}" for a in cfg_arms if a != "A") +
              f"{'B ent/op':>10s}{'B rd/op':>9s}")
        for k in keys:
            ra = a_cell["rows"].get(k)
            if not ra or not ra["cycles_per_op"]:
                continue
            def pct(arm):
                r = by.get((arm, cfg), {}).get("rows", {}).get(k) if (arm, cfg) in by else None
                if not r or not r["cycles_per_op"]:
                    return float("nan")
                return (r["cycles_per_op"] / ra["cycles_per_op"] - 1) * 100
            rb = by.get(("B", cfg), {}).get("rows", {}).get(k, {}) if ("B", cfg) in by else {}
            print(f"{k[:34]:34s}{ra['cycles_per_op']:>10.1f}{ra['ipc'] or 0:>7.2f}" +
                  "".join(f"{pct(a):>+8.1f}%" for a in cfg_arms if a != "A") +
                  f"{rb.get('dit_writes_per_op', float('nan')):>10.2f}"
                  f"{rb.get('dit_read_per_op', float('nan')):>9.2f}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--filter", default="AEAD-AES-128-GCM",
                    help="comma-separated `name[:calls[:warm]]`; each name becomes its own "
                         "gem5 process per (arm, model), with its own call count")
    ap.add_argument("--chunks", default="16")
    ap.add_argument("--calls", type=int, default=1000, help="measured calls per row (the ROI)")
    ap.add_argument("--warm", type=int, default=50, help="calls before the reset")
    ap.add_argument("--arms", default=",".join(ARM_ORDER))
    ap.add_argument("--configs", default="drain,expedite")
    ap.add_argument("--slices", default=None,
                    help="slice file from gen_calls_list.py: one gem5 process per slice per "
                         "(arm, model), each with its own per-row call counts. Overrides --filter.")
    ap.add_argument("--jobs", type=int, default=14)
    ap.add_argument("--cell-timeout", type=int, default=7200,
                    help="kill a cell that exceeds this many seconds (default 2 h); a hung "
                         "benchmark must not be able to stall an unattended sweep")
    ap.add_argument("--gem5", default=os.environ.get("GEM5_BIN", "gem5.fast"))
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--out", default=None)
    a = ap.parse_args()

    arms = [x for x in a.arms.split(",") if x]
    cfgs = [x for x in a.configs.split(",") if x]
    outroot = pathlib.Path(a.out or (WORK / "out"))
    outroot.mkdir(parents=True, exist_ok=True)
    filts = load_slices(a.slices, a.chunks) if a.slices else parse_filters(a.filter, a.calls, a.warm)
    jobs = [(arm, cfg, f, a, outroot) for cfg in cfgs
            for arm in [x for x in CONFIG_ARMS.get(cfg, arms) if x in arms] for f in filts]
    print(f"{len(jobs)} runs, {a.jobs} at a time, {a.gem5}; chunks={a.chunks}")
    for cfg in cfgs:
        keep = [x for x in CONFIG_ARMS.get(cfg, arms) if x in arms]
        print(f"    {cfg:10s} {CONFIG_LABEL.get(cfg, ''):46s} arms {','.join(keep)}")
    for f in filts:
        if f["calls_list"] is not None:
            print(f"    {f['label']:28s} {f['rows']:>5d} rows, per-row counts, {f['warm']:>3d} warm-up")
        else:
            print(f"    {f['label']:28s} {f['calls']:>5d} calls, {f['warm']:>3d} warm-up")
    print(flush=True)

    def guarded(job):
        # A sweep must survive a defect in one cell. ex.map re-raises a worker exception
        # from the iterator, so without this an unhandled error anywhere discards every
        # result, including the cells that finished perfectly.
        try:
            return run_one(job)
        except Exception as e:                                    # noqa: BLE001
            arm, cfg, sl, _, _ = job
            print(f"  [ERR] {arm}/{cfg}/{sl['label']}: driver exception {e!r}", flush=True)
            return {"arm": arm, "cfg": cfg, "filter": sl["label"], "rows": {},
                    "error": f"driver exception: {e!r}"}

    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        cells = list(ex.map(guarded, jobs))

    with open(outroot / "results.jsonl", "w") as fh:
        for c in cells:
            fh.write(json.dumps(c) + "\n")
    cols = ["arm", "cfg", "filter", "row", "calls", "cycles_per_op", "insts_per_op", "ipc",
            "dit_writes_per_op", "dit_set_per_op", "dit_set_redundant_per_op",
            "dit_clear_per_op", "dit_read_per_op", "dit_dwell_frac", "dit_suppressed", "wall_s"]
    with open(outroot / "results.csv", "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        for c in sorted(cells, key=lambda c: (c["arm"], c["cfg"], c.get("filter", ""))):
            for k, r in sorted(c.get("rows", {}).items()):
                w.writerow(dict(arm=c["arm"], cfg=c["cfg"], filter=c.get("filter", ""),
                                row=k, wall_s=c["wall_s"], **r))
    print(f"\nwrote {outroot}/results.{{jsonl,csv}}")

    table(cells, arms, cfgs)
    print("\n=== gates ===")
    fails, notes = gates(cells, arms, cfgs)
    for n in notes:
        print(f"  note {n}")
    for f in fails:
        print(f"  FAIL {f}")
    print("  all gates pass" if not fails else f"  {len(fails)} gate failure(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
