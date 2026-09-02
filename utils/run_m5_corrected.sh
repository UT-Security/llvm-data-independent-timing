#!/usr/bin/env bash
#
# Experiment 09 on Apple M5, following the protocol in the experiment README
# ("Running this on another host"). One command:
#
#   sudo -E bash utils/run_m5_corrected.sh
#
# WHY. The published M5 headline table timed each sample between two
# kpc_get_thread_counters() calls -- calls into the kperf driver, not register
# reads, with the counter sampled part-way through, so their cost lands inside
# the timed region. On M4 that offset measured 3,234 cycles: 72% of the chacha
# baseline and 87% of the AES one. It cancels in arm-vs-arm DIFFERENCES (the
# switch decomposition was always sound) but sits in the DENOMINATOR of every
# percentage. The correction was measured on M4; this re-measures the ORIGINAL
# host so the before/after is same-machine.
#
# THE PROTOCOL IS THREE RUNS, NOT ONE. The README is explicit that a new host
# needs all three, and that neither of the first two carries across machines:
#
#   1. offset probe   the offset is a property of the INSTRUMENT and differs per
#                     host. M4's 3,234 cycles must not be assumed for M5.
#   2. kperf-timed    the instrument as the original run used it.
#   3. cntvct-timed   CHEAP_TIMER=1, ~21-cycle offset instead of thousands.
#
# Having 2 and 3 from the same machine makes the offset visible in the results
# rather than asserted from a probe alone.
#
# OUTPUT. Files land under $OUT_ROOT with an m5_ prefix, matching the M4 names.
# The unprefixed files in the experiment's data/ are the ORIGINAL M5 run that the
# published headline was computed from; this must not overwrite them.
#
# -E is REQUIRED: it preserves HOME (perf.c writes counter files there) and PATH.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export LLVM_BIN="${LLVM_BIN:-$HOME/Documents/dit-toolchain-snap-20260901/bin}"
export WORK="${WORK:-$HOME/Documents/libsodium-1.0.21}"
export CIO_DIR="${CIO_DIR:-$HOME/Documents/cio-eval}"
BENCH_DIR="${BENCH_DIR:-$HOME/Documents/crypto-dit-benchmarks}"
OUT_ROOT="${OUT_ROOT:-$HOME/Documents/dit-m5-$(date +%Y%m%d-%H%M%S)}"
CIO_REPS="${CIO_REPS:-15}"
ARMS_ALL="A:baseline:0 C:baseline:1 P:hardened:0 F:func:0 X:fine:0 N:narrow:0 Z:nopsw:0"

red()  { printf '\033[31m%s\033[0m\n' "$*" >&2; }
info() { printf '\033[1m==> %s\033[0m\n' "$*"; }

[[ "$(id -u)" -eq 0 ]] || {
  red "not root: kperf counters need sudo, and this run is about the counters."
  red "  sudo -E bash utils/run_m5_corrected.sh"; exit 1; }
[[ "$HOME" == /var/root* ]] && {
  red "HOME=$HOME -- you dropped -E. Re-run: sudo -E bash utils/run_m5_corrected.sh"; exit 1; }
mkdir -p "$OUT_ROOT"

# ---- the machine must be idle ------------------------------------------------
# Every number here is a ratio between arms run seconds apart. Competing load
# both depresses the clock and makes it drift between arms, which no amount of
# interleaving or rotation removes.
BUSY=$(ps -Ao pcpu= -r 2>/dev/null | awk '$1>50' | wc -l | tr -d ' ')
if [[ "${BUSY:-0}" -gt 1 ]]; then
  red "$BUSY processes are above 50% CPU. Native timing needs an exclusive machine."
  ps -Ao pcpu,comm -r 2>/dev/null | head -6 >&2
  red "Stop them, or set ALLOW_BUSY=1 to measure anyway (the numbers will not be usable)."
  [[ "${ALLOW_BUSY:-0}" == 1 ]] || exit 1
fi

# ---- the CIO drivers must be REGENERATED, not assumed -----------------------
# They live in an untracked $CIO_DIR, and eval_util.h's timer macros have to
# agree with cio_arm_shim.h about which variable START_CYCLE_TIMER returns. A
# stale hand-edited header returning cio_shim_t0_cyc against a shim that sets
# cio_shim_t0_timer subtracts a kperf count from a CNTVCT count -- two clocks
# with different origins. It does not error: every sample comes back as the raw
# CNTVCT value (~3e15 ticks, the machine's uptime) and every arm reads 0.00%.
# That void run happened on 2026-09-02. Regenerating is seconds and idempotent.
info "regenerate CIO's drivers and the eval_util.h port"
bash "$REPO_ROOT/utils/taint_cio_eval_setup.sh" >/dev/null \
  || { red "CIO driver staging failed"; exit 1; }
grep -q 'cio_shim_region_begin(); cio_shim_t0_timer;' "$CIO_DIR/eval_util.h" \
  || { red "$CIO_DIR/eval_util.h does not return cio_shim_t0_timer -- it cannot"
       red "agree with cio_arm_shim.h and the samples will be nonsense"; exit 1; }

info "rebuild arm Z (nop-switch control) and verify it is a valid control"
bash "$REPO_ROOT/utils/taint_libsodium_nopsw.sh" || { red "arm Z build failed"; exit 1; }

# ---- 1. this host's own instrumentation offset ------------------------------
info "measure THIS host's kperf region offset (M4's 3,234 does not carry over)"
if clang -fomit-frame-pointer -O2 -std=c18 -DCIO_SHIM_KPERF \
      -I"$BENCH_DIR" -include "$REPO_ROOT/utils/cio_arm_shim.h" \
      -I"$CIO_DIR" -o "$OUT_ROOT/cio_offset_probe" \
      "$REPO_ROOT/utils/cio_offset_probe.c" -lm 2>"$OUT_ROOT/offset_build.log"; then
  "$OUT_ROOT/cio_offset_probe" 2>&1 | tee "$OUT_ROOT/m5_offset_probe.txt" | tail -14
  # The probe discards any sweep taken at a depressed clock. If it discarded
  # ALL of them the P-cluster was not available, and every timing number that
  # follows would be measured on the same contended machine. Fatal, not a
  # warning: on 2026-09-02 this printed NO VALID PASSES because another session
  # was running a gem5 sweep at --jobs 9, and the script carried on regardless.
  if grep -q "NO VALID PASSES" "$OUT_ROOT/m5_offset_probe.txt"; then
    red ""
    red "ABORTING: the offset probe found no valid pass -- the P-cluster is not"
    red "available, so nothing measured now is trustworthy. Check for other load:"
    red "    ps -Ao pcpu,comm -r | head"
    red "Native timing runs need an exclusive machine (gem5 is exempt; native is not)."
    exit 1
  fi
else
  red "offset probe build failed -- see $OUT_ROOT/offset_build.log"
  tail -5 "$OUT_ROOT/offset_build.log" >&2
fi

# ---- 2 and 3. the two timers, same binaries, same arms ----------------------
for mode in kperf cntvct; do
  cheap=0; [[ "$mode" == cntvct ]] && cheap=1
  info "run: $mode-timed  (CHEAP_TIMER=$cheap), 7 arms, $CIO_REPS reps"
  OUT="$OUT_ROOT/$mode" \
  CHEAP_TIMER="$cheap" CIO_OPT=-O2 OURS=ditprobe CIO_REPS="$CIO_REPS" \
  ARMS="$ARMS_ALL" \
    bash "$REPO_ROOT/utils/taint_libsodium_sudo_run.sh" \
      > "$OUT_ROOT/m5_report_${mode}_timed.txt" 2>&1
  # name the artefacts the way the README's convention requires
  for f in cio ours provenance; do
    src="$OUT_ROOT/$mode/$f.csv"; [[ -f "$src" ]] || src="$OUT_ROOT/$mode/$f.txt"
    [[ -f "$src" ]] || continue
    case "$f" in
      cio)        cp "$src" "$OUT_ROOT/m5_cio_benchmarks_${mode}_timed.csv" ;;
      ours)       cp "$src" "$OUT_ROOT/m5_ditprobe_gates_${mode}_timed.csv" ;;
      provenance) cp "$src" "$OUT_ROOT/m5_provenance_${mode}_timed.txt" ;;
    esac
  done
  tail -3 "$OUT_ROOT/m5_report_${mode}_timed.txt"
done

# ---- plausibility gate -------------------------------------------------------
# The void run produced a table of 0.00% deltas on a 3.03e15 baseline: internally
# consistent, obviously absurd, and caught only by looking at the magnitude.
# Assert the magnitude rather than leaving it to the reader.
for mode in kperf cntvct; do
  f="$OUT_ROOT/m5_cio_benchmarks_${mode}_timed.csv"
  [[ -f "$f" ]] || continue
  F="$f" M="$mode" python3 - <<'PLAUS'
import csv, os, statistics as st, collections
rows=list(csv.DictReader(open(os.environ['F'])))
d=collections.defaultdict(list)
for r in rows: d[(r['benchmark'],r['arm'])].append(float(r['mean_ticks']))
bad=[]
for (b,a),v in d.items():
    if a!='A': continue
    m=st.median(v)
    lo,hi=(20,2_000_000_000) if b!='argon2id' else (1_000_000,2_000_000_000)
    if not (lo<=m<=hi): bad.append(f"{b}: baseline {m:,.0f} outside [{lo:,}, {hi:,}]")
print(f"  PLAUSIBILITY [{os.environ['M']}]: " + ("PASS" if not bad else
      "FAIL\n    " + "\n    ".join(bad) +
      "\n    A baseline near 3e15 is the raw CNTVCT value: the timer is returning\n"
      "    an absolute count, not a delta. Do not read the percentages."))
PLAUS
done

cat <<NOTE

  output: $OUT_ROOT
    m5_offset_probe.txt                    this host's own region offset
    m5_cio_benchmarks_{kperf,cntvct}_timed.csv
    m5_ditprobe_gates_{kperf,cntvct}_timed.csv
    m5_provenance_{kperf,cntvct}_timed.txt
    m5_report_{kperf,cntvct}_timed.txt     full console output

  READING IT
    cntvct P vs A    total pass overhead with the instrument out of the denominator
    Z vs A           LAYOUT alone. On M4 this was noise except aes256-gcm encrypt
                     at +8.70%, ~6x that row's MAD.
    P vs Z           switch EXECUTION alone.
    kperf vs cntvct  the same arms under both timers: the gap between them IS this
                     host's offset, visible rather than asserted.
NOTE
