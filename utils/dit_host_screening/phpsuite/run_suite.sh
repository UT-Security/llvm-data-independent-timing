#!/usr/bin/env bash
# Experiment 11: every row of the three workloads under the six arms, then the call-density
# sweep. Idle machine, nothing else running: the arms rotate per request, so drift cannot
# masquerade as an effect, but a busy host widens the MAD.
#
# AN IDLE MACHINE IS NOW A HARD REQUIREMENT, not advice. The metric is Apple's
# fixed performance counters read from EL0 (bench.py), and those are PER-CORE:
# anything else scheduled on the core inside a request is charged to that
# request. bench.py gates every sample on its implied core clock and drops what
# is out of range, so contention costs samples rather than corrupting results --
# but it does cost them. Measured while Spotlight was indexing the freshly built
# PHP trees: 47% of samples dropped. On an idle host the rate is near zero, and
# each row prints its own count, so check it before believing a row.
#
# Spotlight is the usual culprit right after build_php.sh, since it wakes up to
# index five PHP trees. build_php.sh drops a .metadata_never_index marker in W;
# if mds is still running, let it finish before starting the run.
#
# Env: W, DB_PORT, WARMUP, MEASURED, METRIC, CLK_LO/CLK_HI (see bench.py);
#      ROWS (default "zend symfony wordpress wpapi")
set -uo pipefail
RIG="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
W="${W:-$HOME/Documents/dit-phpsuite}"
DB_PORT="${DB_PORT:-3307}"
export W WORDPRESS_DB_HOST="127.0.0.1:$DB_PORT"
info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
for v in base bracket bracketnop taint taintnop; do [[ -x "$W/phpf-$v/sapi/cgi/php-cgi" ]] || die "arm $v not built (build_php.sh all)"; done
[[ -f "$W/libditctl.dylib" ]] || die "libditctl.dylib missing (build_php.sh base)"
mkdir -p "$W/results"
for which in ${ROWS:-zend symfony wordpress wpapi}; do
  case "$which" in
    wpapi) info "call-density sweep (run_wpapi.sh)"; "$RIG/run_wpapi.sh" || die "wpapi failed" ;;
    *) info "$which"; python3 "$RIG/bench.py" "$which" > "$W/results/$which.txt" 2>&1 || die "$which failed: $(tail -3 "$W/results/$which.txt")"
       grep -E '^== |failures' "$W/results/$which.txt" ;;
  esac
done
info "done: results in $W/results"
