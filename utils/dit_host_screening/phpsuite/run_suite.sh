#!/usr/bin/env bash
# Experiment 11: every row of the three workloads under the six arms, then the call-density
# sweep. Idle machine, nothing else running: the arms rotate per request, so drift cannot
# masquerade as an effect, but a busy host widens the MAD.
# Env: W, DB_PORT, WARMUP, MEASURED (see bench.py); ROWS (default "zend symfony wordpress wpapi")
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
