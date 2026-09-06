#!/usr/bin/env bash
#
# Reproduce every gem5 number in paper_experiments/09-libsodium-cio-parity, from
# a clean machine, in one command.
#
#   utils/dit_host_screening/cioparity/reproduce.sh
#
# It fetches nothing it can avoid and rebuilds nothing that is already correct,
# so a second run is cheap. Each stage prints what it produced and stops on the
# first failure; a stage that cannot pass its own gate is a hard stop, not a
# warning, because every number downstream of it would be meaningless.
#
# WHAT THIS PRODUCES, and where the README's tables come from:
#
#   data/gem5_switch_model.csv   the headline: 5 benchmarks x 8 arms x 2 switch
#                                models, unaligned. Every renamed-vs-serialising
#                                figure and every cycles-per-switch figure.
#   data/gem5_argon2id.csv       the 6th benchmark, run separately because one
#                                operation is 326M cycles.
#   data/gem5_align{16,64}.csv   the same sweep at two block-alignment settings.
#                                Only needed to reproduce the layout-spread
#                                claim (11.46 -> 2.93 points); skip with
#                                SKIP_ALIGN=1 and the headline is unaffected.
#   data/gem5_analysis.txt       analyze.py over the above.
#
# WALL CLOCK, on 160 cores with 40-way concurrency: ~15 min for the headline
# sweep, ~6 h for argon2id (12 cells, each one 326M-cycle operation, run
# concurrently so the wall time is one cell), ~30 min for the two alignment
# sweeps. Set SKIP_ARGON=1 and SKIP_ALIGN=1 for the headline alone.
#
# ---------------------------------------------------------------- PREREQUISITES
#
# 1. A gem5 carrying two capabilities this experiment needs:
#
#      PMULL at size=3 (64x64 -> 128).  gem5 implements PMULL only for size==0,
#      so libsodium's GHASH hits Unknown64 and the simulator PANICS. Without this
#      the two AES-GCM benchmarks cannot run at all - they are not slow, they are
#      impossible.
#
#      commit.ditCycles.  Cycles with PSTATE.DIT set. The five existing DIT
#      counters price the SWITCH; nothing priced the mode being ON. Without it
#      the dwell column is absent and two gates cannot run.
#
#    Both were written for this experiment on branches in a gem5-DIT worktree and
#    are now upstream: this repo's gem5-DIT submodule carries them at the pinned
#    commit, so the default G5 is that submodule and needs only a build. The two
#    checks below stay, because they are capability tests rather than a version
#    test - a gem5 without them would silently produce a 4-benchmark result that
#    looks complete. Build with:
#
#      git submodule update --init gem5-DIT
#      (cd gem5-DIT && scons build/ARM/gem5.opt -j<jobs>)
#
# 2. An aarch64 Linux host. This is NOT cross-compilation: the taint clang emits
#    native static ELF here. The host does not need FEAT_DIT - nothing executes
#    outside the simulator.
#
# 3. A counter-optimization/cio checkout, for their six eval_*.c drivers. They
#    are used byte-for-byte and verified by sha256; only their x86-only
#    eval_util.h is replaced.
#
# ENV
#   LLVM=<dir>   taint LLVM build   (default ~/Documents/llvm-data-independent-timing/build)
#   G5=<dir>     gem5-DIT tree      (default: this repo's gem5-DIT submodule)
#   BC=<dir>     whole-library bitcode tree (default ~/Documents/libsodium-wllvm-1.0.21)
#   CIO=<dir>    counter-optimization/cio checkout   (REQUIRED)
#   OUT=<dir>    scratch root       (default ~/Documents/libsodium-cioparity-repro)
#   JOBS=<n>     concurrent gem5 processes (default 40)
#   SKIP_ARGON=1 / SKIP_ALIGN=1
#
set -uo pipefail

R="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$R/../../.." && pwd)"
DATA="$REPO/paper_experiments/09-libsodium-cio-parity/data"
LLVM="${LLVM:-$HOME/Documents/llvm-data-independent-timing/build}"
G5="${G5:-$REPO/gem5-DIT}"
BC="${BC:-$HOME/Documents/libsodium-wllvm-1.0.21}"
OUT="${OUT:-$HOME/Documents/libsodium-cioparity-repro}"
JOBS="${JOBS:-40}"

info(){ printf '\033[1m==> %s\033[0m\n' "$*"; }
die(){ printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------- preflight
info "preflight"
[[ "$(uname -m)" == aarch64 ]] || die "this rig builds native aarch64; host is $(uname -m)"
[[ -n "${CIO:-}" && -d "${CIO:-}" ]] || die "set CIO to a counter-optimization/cio checkout"
[[ -x "$LLVM/bin/llc" ]] || die "no llc at $LLVM/bin (set LLVM)"
[[ -f "$BC/libsodium-whole.bc" ]] || die "no whole-library bitcode at $BC/libsodium-whole.bc (set BC)"
[[ -d "$G5/src" ]] || die "no gem5 sources at $G5 - run: git submodule update --init gem5-DIT (or set G5)"
[[ -x "$G5/build/ARM/gem5.opt" ]] || die "no gem5.opt at $G5/build/ARM - build it there with scons build/ARM/gem5.opt (or set G5)"

# The two patches, checked by capability rather than by branch name: a branch can
# be renamed, and a stock gem5 would otherwise run and quietly drop AES-GCM.
grep -q 'ditCycles' "$G5/src/cpu/o3/commit.hh" \
  || die "this gem5 has no commit.ditCycles - see PREREQUISITES at the top of this script"
# The 64-bit form is instantiated as a TYPE TUPLE, ("uint8_t", "uint64_t"), not
# as PmullX<uint64_t> - grepping for the template spelling reports a false
# negative on a correctly patched tree. Check both the instantiation and the
# decode arm, since either alone can be present without the other working.
grep -q 'uint64_t' <(grep -A2 'pmull' "$G5/src/arch/arm/isa/insts/neon64.isa" | grep -i 'uint8_t.*uint64_t') \
  && grep -q 'size == 3\|size==3\|size != 0' "$G5/src/arch/arm/isa/formats/neon64.isa" \
  || die "this gem5 has no PMULL at size=3 - AES-GCM will panic; see PREREQUISITES"
info "    gem5 carries both required patches"
info "    $(nproc) cores, load $(cut -d' ' -f1 /proc/loadavg), running $JOBS concurrent"

# ---------------------------------------------------------------- 1. arms
info "STAGE 1/4  build the 8 arms (unaligned) + tail-call audit"
CIO="$CIO" LLVM="$LLVM" G5="$G5" BC="$BC" WORK="$OUT/main" "$R/build_arms_wl.sh" \
  || die "arm build failed"

# ---------------------------------------------------------------- 2. headline
info "STAGE 2/4  headline sweep: 5 benchmarks x 8 arms x 2 switch models"
G5="$G5" WORK="$OUT/main" python3 "$R/run_cio_gem5.py" --jobs "$JOBS" --out "$OUT/main/out" \
  || die "headline sweep failed a gate -- do not use the numbers, read the gate output above"
cp -f "$OUT/main/out/results.csv"   "$DATA/gem5_switch_model.csv"
cp -f "$OUT/main/out/results.jsonl" "$DATA/gem5_switch_model.jsonl"

# ---------------------------------------------------------------- 3. argon2id
if [[ -z "${SKIP_ARGON:-}" ]]; then
  info "STAGE 3/4  argon2id, 12 cells (one 326M-cycle op per cell; hours, run concurrently)"
  # One measured op and one warmup: gem5 is deterministic, so a settled region is
  # exact and more ops only cost hours. NOT warmup=10 as the other benchmarks use;
  # that would be ten extra hours per cell for nothing.
  G5="$G5" WORK="$OUT/main" python3 "$R/run_cio_gem5.py" --benches argon2id \
      --jobs 12 --out "$OUT/main/out_argon" \
    || die "argon2id sweep failed"
  cp -f "$OUT/main/out_argon/results.csv" "$DATA/gem5_argon2id.csv"
else
  info "STAGE 3/4  argon2id SKIPPED (SKIP_ARGON set)"
fi

# ---------------------------------------------------------------- 4. alignment
if [[ -z "${SKIP_ALIGN:-}" ]]; then
  info "STAGE 4/4  alignment sweeps, for the layout-spread claim only"
  for A in 4 6; do
    lbl=$([[ "$A" == 4 ]] && echo 16 || echo 64)
    info "    block alignment ${lbl}B, applied identically to EVERY arm"
    CIO="$CIO" LLVM="$LLVM" G5="$G5" BC="$BC" ALIGN="$A" WORK="$OUT/a$lbl" \
      "$R/build_arms_wl.sh" > "$OUT/build_a$lbl.log" 2>&1 || die "a$lbl arm build failed"
    G5="$G5" WORK="$OUT/a$lbl" python3 "$R/run_cio_gem5.py" --jobs "$JOBS" \
      --out "$OUT/a$lbl/out" || die "a$lbl sweep failed a gate"
    cp -f "$OUT/a$lbl/out/results.csv" "$DATA/gem5_align$lbl.csv"
  done
else
  info "STAGE 4/4  alignment sweeps SKIPPED (SKIP_ALIGN set)"
fi

# ---------------------------------------------------------------- report
info "analysis"
python3 "$R/analyze.py" "$OUT/main/out/results.jsonl" | tee "$DATA/gem5_analysis.txt"

{ echo "date: $(date -u +%FT%TZ)"
  echo "host: $(uname -m) $(lscpu | sed -n 's/^Model name: *//p') $(nproc) cores, FEAT_DIT: $(grep -qw dit /proc/cpuinfo && echo yes || echo no)"
  echo "gem5: $(git -C "$G5" rev-parse --short HEAD) on $(git -C "$G5" branch --show-current)"
  echo "llvm: $(git -C "$REPO" rev-parse --short HEAD)"
  echo "libsodium: whole-library bitcode from $BC"
  echo "cio drivers: sha256-verified against $CIO"
  echo "jobs: $JOBS"
} > "$DATA/gem5_provenance.txt"

info "done"
echo
echo "  Regenerated in paper_experiments/09-libsodium-cio-parity/data/:"
echo "    gem5_switch_model.{csv,jsonl}   gem5_analysis.txt   gem5_provenance.txt"
[[ -z "${SKIP_ARGON:-}" ]] && echo "    gem5_argon2id.csv"
[[ -z "${SKIP_ALIGN:-}" ]] && echo "    gem5_align16.csv  gem5_align64.csv"
echo
echo "  Every gate passed. A gate failure is a hard stop above, not a warning,"
echo "  because a failed control invalidates every number after it."
