#!/usr/bin/env bash
# Reproduce experiment 09 (libsodium, CIO parity) on either instrument.
#
#   ./reproduce.sh                 pick the rig from the host and run it
#   ./reproduce.sh silicon         force the Apple-silicon rig (M4/M5)
#   ./reproduce.sh gem5            force the gem5 rig
#   ./reproduce.sh blanket         measure ONLY blanket DIT (no toolchain)
#   ./reproduce.sh silicon counters   run just these stages (--list shows them)
#   ./reproduce.sh --help          what each one needs
#
# The two rigs answer different questions and BOTH are the experiment:
#
#   silicon  what the mitigation costs on hardware people ship. It is the only
#            rig that can measure Apple's real `sb` barrier, and the only one
#            whose cycles are cycles.
#   gem5     what the cost WOULD be if `MSR DIT` were renamed instead of
#            serialising -- the counterfactual silicon cannot run, and the whole
#            reason experiment 09 has a simulator arm at all.
#
# Same arms, same seeds, same drivers, same compiler configuration on both, so
# a number that differs between them differs because of the machine.
#
# `blanket` is NOT a third instrument. It is the silicon question narrowed to
# the one arm that needs no compiler pass at all: blanket DIT is `msr dit, #1`
# before main, so the two arms are ONE binary run twice and the taint toolchain
# drops out entirely. It exists because the placement rigs read counters once
# per REGION -- ~3,400 kperf cycles against a ~275-cycle AES-GCM op, which
# compresses every percentage. Blanket needs no per-region attribution, so it
# reads them once around a long loop and lets the iteration count amortise
# them. Use it when you want the blanket number and nothing else. See
# CLAUDE.md in this directory before changing anything here.
set -uo pipefail
E="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
R="$(cd "$E/../.." && pwd)"

info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

case "${1:-}" in
  --help|-h)
    # The header block, however long it grows: every comment line after the
    # shebang, stopping at the first line of code. A hardcoded range silently
    # starts printing the script itself the moment the comment changes length.
    awk 'NR == 1 { next } /^#/ { sub(/^# ?/, ""); print; next } { exit }' "$0"
    cat <<'EOF'

SILICON (M4/M5, or any FEAT_DIT arm64 Mac)
  ./reproduce.sh silicon
  Needs: Xcode CLT. Builds the toolchain if it has to (hours) -- pass
  LLVM_BIN=<dir>/bin to reuse one. Prompts for sudo (kperf counters).
  Knobs: SKIP_ARGON=1 drops ~60 of the ~70 minutes; NO_SUDO=1 runs unrooted;
         CIO_REPS=<n> (default 15, the paper's protocol).

BLANKET ONLY (any FEAT_DIT arm64 Mac -- no LLVM build, no seeds, no arms)
  sudo -E ./reproduce.sh blanket
  Needs: a configured libsodium tree (WORK=<dir>, default
  ~/Documents/libsodium-arms-silicon/base) and perf.c (BENCH=<dir>). Root is
  only for kperf -- unrooted it reports ns/op and the clock gate cannot fire.
  Knobs: REPS=<n> (21), ITERS=<n> (200000), PRIMS="control aes_enc ...".

GEM5 (aarch64 Linux, a gem5-DIT build with the PMULL and ditCycles patches)
  CIO=<counter-optimization/cio checkout> ./reproduce.sh gem5
  Knobs: SKIP_ARGON=1 drops the six-hour argon2id stage; SKIP_ALIGN=1 drops
         the two alignment sweeps. The headline needs neither, ~15 min on 160
         cores.
EOF
    exit 0 ;;
esac

RIG="${1:-}"
[[ $# -gt 0 ]] && shift        # anything after the rig is passed through as stages
if [[ -z "$RIG" ]]; then
  if [[ "$(uname -s)" == Darwin && "$(uname -m)" == arm64 ]]; then RIG=silicon
  elif [[ "$(uname -s)" == Linux ]]; then RIG=gem5
  else die "cannot tell which rig this host is for; pass 'silicon' or 'gem5'"; fi
  info "host looks like the $RIG rig (override by passing it explicitly)"
fi

case "$RIG" in
  silicon)
    exec bash "$R/utils/taint_libsodium_silicon_reproduce.sh" "$@" ;;
  gem5)
    [[ -n "${CIO:-}" ]] || die "set CIO to a counter-optimization/cio checkout (the drivers live there)"
    exec bash "$R/utils/dit_host_screening/cioparity/reproduce.sh" "$@" ;;
  blanket)
    exec bash "$R/utils/dit_blanket/run_blanket.sh" "$@" ;;
  *) die "unknown rig '$RIG' (silicon | gem5 | blanket)" ;;
esac
