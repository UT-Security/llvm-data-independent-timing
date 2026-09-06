#!/bin/bash
# Reproduce experiment 02's gem5 numbers and figures from the committed sources.
#
#   ./reproduce.sh            all four stages
#   ./reproduce.sh build      the three arms (gem5-DIT/benchmarks/signed_lookup/build_gem5_linux.sh)
#   ./reproduce.sh sweep      canonical lane, pure-hash lane, q=0.5 lane, and the q sensitivity sweep
#   ./reproduce.sh derive     import into data/ with provenance headers (utils/.../derive_exp02.py)
#   ./reproduce.sh figures    figures/ from data/ (utils/.../fig_exp02.py)
#
# Env: LLVM_BUILD (the taint clang build, default: this repo's build/), G5 (gem5-DIT
#      root, default: this repo's submodule), WORK (run dir, default
#      ~/Documents/signed_lookup-gem5), MPL (a python with matplotlib, default
#      /tmp/mplvenv/bin/python; see fig_exp02.py for the venv), JOBS (gem5 processes
#      at once, default 80% of nproc).
#
# RESUME IS OFF BY DEFAULT, and that is deliberate. run_gem5.py's --resume skips any
# run whose stats.txt exists - it does not hash the binary or record the compiler - so
# resuming into a WORK dir filled by an older compiler silently mixes arms built weeks
# apart, and the arm LIST has changed too (the Apple bracket arms were added
# 2026-09-05, so a pre-09-05 WORK covers 7 of today's 10 arm/model combos and only the
# bracket would be re-run). The whole sweep is ~1,100 runs at a ~60 s median, which is
# minutes of wall clock at this concurrency, so there is nothing to save. Pass
# RESUME=1 only when you know the WORK dir was filled by THIS build.
#
# The sweeps share WORK/runs and pass --resume, so a rerun only does what is
# missing; the q sensitivity sweep reuses the q=0, 2, 3 runs at L=200 and 20000
# from the first three. gem5 is deterministic, and the runner roots the binary
# path at a constant-length /tmp path, so a reproduction on another machine
# differs from data/ only by the simulator and compiler builds it names in the
# provenance line. Three files in data/ are frozen evidence from driver versions
# that were never committed and are not regenerated here: gem5_arms_ed25519.csv,
# gem5_arms_constant_chain.csv, gem5_stack_offset_sensitivity.csv.
set -euo pipefail
E="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
R="$(cd "$E/../.." && pwd)"
export G5="${G5:-$R/gem5-DIT}"
export LLVM_BUILD="${LLVM_BUILD:-$R/build}"
export WORK="${WORK:-$HOME/Documents/signed_lookup-gem5}"
MPL="${MPL:-/tmp/mplvenv/bin/python}"
# 80% of the machine. It cannot know what else is running, so lower it by hand when
# sharing the host - `uptime` and `ps -eo comm` before starting.
JOBS="${JOBS:-$(( $(nproc) * 4 / 5 ))}"
RESUME="${RESUME:-0}"
[[ "$RESUME" == 1 ]] && RES=--resume || RES=
RIG="$G5/benchmarks/signed_lookup"
need_g5() { [[ -d "$RIG" ]] || { echo "no gem5-DIT at $G5 - run: git submodule update --init gem5-DIT (or set G5)" >&2; exit 1; }; }
STAGES="${*:-build sweep derive figures}"
want() { [[ " $STAGES " == *" $1 "* ]]; }
info() { printf '\033[1m==> %s\033[0m\n' "$*"; }

if want build; then
  need_g5
  [[ -x "$LLVM_BUILD/bin/clang" ]] || { echo "no clang at $LLVM_BUILD/bin/clang - build it (ninja -C $R/build clang) or set LLVM_BUILD" >&2; exit 1; }
  info "build arms"; "$RIG/build_gem5_linux.sh"
fi
if want sweep; then
  need_g5
  info "canonical lane (q4=3), 5 offsets";        python3 "$RIG/run_gem5.py" --offsets 5 --jobs "$JOBS" $RES
  info "pure hashed chase (q4=0), 5 offsets";     python3 "$RIG/run_gem5.py" --offsets 5 --jobs "$JOBS" $RES --pred 0
  info "q=0.5 lane (q4=2), 5 offsets";            python3 "$RIG/run_gem5.py" --offsets 5 --jobs "$JOBS" $RES --pred 2
  info "q sensitivity, L=200 and 20000";          python3 "$RIG/run_gem5.py" --offsets 5 --jobs "$JOBS" $RES --pred 0,1,2,3,4 --L 200,20000
fi
if want derive; then
  info "import into data/"; python3 "$R/utils/dit_host_screening/signed_lookup/derive_exp02.py"
fi
if want figures; then
  info "figures"; "$MPL" "$R/utils/dit_host_screening/signed_lookup/fig_exp02.py"
fi
info "done: $STAGES"
