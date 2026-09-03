#!/bin/bash
# Reproduce experiment 02's gem5 numbers and figures from the committed sources.
#
#   ./reproduce.sh            all four stages
#   ./reproduce.sh build      the three arms (gem5-DIT/benchmarks/signed_lookup/build_gem5_linux.sh)
#   ./reproduce.sh sweep      canonical lane, pure-hash lane, q=0.5 lane, and the q sensitivity sweep
#   ./reproduce.sh derive     import into data/ with provenance headers (utils/.../derive_exp02.py)
#   ./reproduce.sh figures    figures/ from data/ (utils/.../fig_exp02.py)
#
# Env: LLVM_BUILD (required for build), G5 (gem5-DIT root, default ~/Documents/gem5-DIT),
#      WORK (run dir, default ~/Documents/signed_lookup-gem5), MPL (a python with
#      matplotlib, default /tmp/mplvenv/bin/python; see fig_exp02.py for the venv),
#      JOBS (gem5 processes at once, default 150).
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
export G5="${G5:-$HOME/Documents/gem5-DIT}"
export WORK="${WORK:-$HOME/Documents/signed_lookup-gem5}"
MPL="${MPL:-/tmp/mplvenv/bin/python}"
JOBS="${JOBS:-150}"
RIG="$G5/benchmarks/signed_lookup"
STAGES="${*:-build sweep derive figures}"
want() { [[ " $STAGES " == *" $1 "* ]]; }
info() { printf '\033[1m==> %s\033[0m\n' "$*"; }

if want build; then
  info "build arms"; "$RIG/build_gem5_linux.sh"
fi
if want sweep; then
  info "canonical lane (q4=3), 5 offsets";        python3 "$RIG/run_gem5.py" --offsets 5 --jobs "$JOBS" --resume
  info "pure hashed chase (q4=0), 5 offsets";     python3 "$RIG/run_gem5.py" --offsets 5 --jobs "$JOBS" --resume --pred 0
  info "q=0.5 lane (q4=2), 5 offsets";            python3 "$RIG/run_gem5.py" --offsets 5 --jobs "$JOBS" --resume --pred 2
  info "q sensitivity, L=200 and 20000";          python3 "$RIG/run_gem5.py" --offsets 5 --jobs "$JOBS" --resume --pred 0,1,2,3,4 --L 200,20000
fi
if want derive; then
  info "import into data/"; python3 "$R/utils/dit_host_screening/signed_lookup/derive_exp02.py"
fi
if want figures; then
  info "figures"; "$MPL" "$R/utils/dit_host_screening/signed_lookup/fig_exp02.py"
fi
info "done: $STAGES"
