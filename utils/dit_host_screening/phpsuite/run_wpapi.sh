#!/usr/bin/env bash
# Experiment 11, the call-density sweep on WordPress: REST requests authenticated by an
# application password (phpass runs on every request), at the shipped 2^13 rounds and at
# 2^16 via the mu-plugin. Each configuration rehashes the admin password and mints a fresh
# application password so both are stored at that round count; the site is left at 2^13.
# Env: W, DB_PORT, WARMUP, MEASURED (see bench.py)
set -uo pipefail
RIG="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
W="${W:-$HOME/Documents/dit-phpsuite}"
DB_PORT="${DB_PORT:-3307}"
export W WORDPRESS_DB_HOST="127.0.0.1:$DB_PORT"
PHP="$W/phpf-base/sapi/cli/php"; WP="$W/apps/wordpress"; MU="$WP/wp-content/mu-plugins/phpass-rounds.php"
info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
wp() { ( cd "$WP" && "$PHP" -d error_reporting=0 wp-cli.phar "$@" ); }

configure() {  # $1 = 13 | 16
  if [[ "$1" == 16 ]]; then mkdir -p "$(dirname "$MU")"; cp "$RIG/phpass-rounds.php" "$MU"; else rm -f "$MU"; fi
  wp user update wordpress --user_pass=wordpress --skip-email >/dev/null 2>&1 || die "rehash failed"
  local h; h=$(wp user get wordpress --field=user_pass 2>/dev/null)
  info "phpass 2^$1: stored hash starts $(echo "$h" | cut -c1-4) (B=13, E=16)"
  [[ "$(echo "$h" | cut -c4)" == "$( [[ $1 == 16 ]] && echo E || echo B )" ]] || die "unexpected hash prefix $h"
  for id in $(wp user application-password list wordpress --field=uuid 2>/dev/null); do wp user application-password delete wordpress "$id" >/dev/null 2>&1; done
  APP=$(wp user application-password create wordpress "bench$1" --porcelain 2>/dev/null | tr -d ' ')
  [[ -n "$APP" ]] || die "application password not created"
  export WP_APP_PASSWORD="$APP"
}

mkdir -p "$W/results"
configure 13
ROUNDS_LABEL="2^13" python3 "$RIG/bench.py" wpapi > "$W/results/wpapi_13.txt" 2>&1 || die "run 2^13 failed: $(tail -3 "$W/results/wpapi_13.txt")"
grep -E '^== |^auth check|failures' "$W/results/wpapi_13.txt"
configure 16
SKIP_ANON=1 ROUNDS_LABEL="2^16" python3 "$RIG/bench.py" wpapi > "$W/results/wpapi_16.txt" 2>&1 || die "run 2^16 failed: $(tail -3 "$W/results/wpapi_16.txt")"
grep -E '^== |^auth check|failures' "$W/results/wpapi_16.txt"
configure 13   # leave the site as shipped
info "done"
