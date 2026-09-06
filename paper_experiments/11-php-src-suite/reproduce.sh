#!/bin/bash
# Reproduce experiment 11 from the committed sources, on any Apple Silicon Mac.
#
#   ./reproduce.sh            every stage, in order
#   ./reproduce.sh deps       Homebrew packages (mariadb sqlite zlib oniguruma pkgconf), SDK, python3
#   ./reproduce.sh clang      build the taint compiler at the checked-out commit (skipped if LLVM_BUILD has one)
#   ./reproduce.sh build      PHP 8.4.25 x5 arms + libditctl.dylib          (~10 min; utils/dit_host_screening/phpsuite/build_php.sh)
#   ./reproduce.sh apps       WordPress 6.2 + Symfony Demo 2.2.3 + MariaDB   (setup_apps.sh all)
#   ./reproduce.sh run        Zend/bench.php, Symfony, WordPress, the sweep   (~25 min, IDLE machine; run_suite.sh)
#   ./reproduce.sh collect    copy results and reports into data/ with a provenance record
#   ./reproduce.sh stopdb     stop the MariaDB this started
#
# Env: LLVM_BUILD  the taint compiler's build dir (default <repo>/build)
#      W           work dir for sources, builds, apps, database, results (default ~/Documents/dit-phpsuite)
#      DB_PORT     MariaDB port (default 3307)
#      WARMUP / MEASURED   requests per row per arm (default 50 / 100, the numbers in data/)
#      JOBS        build parallelism (default 10)
#
# What it needs from you: Xcode command-line tools, Homebrew, network for the PHP tarball
# and the two application repositories. Nothing runs as root.
set -euo pipefail
E="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
R="$(cd "$E/../.." && pwd)"
RIG="$R/utils/dit_host_screening/phpsuite"
export W="${W:-$HOME/Documents/dit-phpsuite}"
export LLVM_BUILD="${LLVM_BUILD:-$R/build}"
export DB_PORT="${DB_PORT:-3307}" JOBS="${JOBS:-10}"
STAGES="${*:-deps clang build apps run collect}"
want() { [[ " $STAGES " == *" $1 "* ]]; }
info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
[[ "$(uname -s)/$(uname -m)" == Darwin/arm64 ]] || die "experiment 11 runs on Apple Silicon (PSTATE.DIT, macOS)"

if want deps; then
  info "deps"
  command -v brew >/dev/null || die "Homebrew is required (https://brew.sh)"
  xcrun --show-sdk-path >/dev/null 2>&1 || die "xcode-select --install"
  command -v python3 >/dev/null || die "python3"
  command -v cmake >/dev/null || brew install cmake
  command -v ninja >/dev/null || brew install ninja
  for p in mariadb sqlite zlib oniguruma pkgconf; do brew list --versions "$p" >/dev/null 2>&1 || brew install "$p"; done
  brew list --versions mariadb sqlite zlib oniguruma pkgconf
fi
if want clang; then
  if [[ -x "$LLVM_BUILD/bin/clang" && -f "$LLVM_BUILD/bin/llvm-objdump" ]]; then
    info "clang: using $LLVM_BUILD (delete it to rebuild)"
  else
    info "clang: configuring and building the taint compiler in $LLVM_BUILD (about an hour)"
    cmake -G Ninja -S "$R/llvm" -B "$LLVM_BUILD" -DLLVM_ENABLE_PROJECTS=clang -DLLVM_TARGETS_TO_BUILD=AArch64 \
      -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_ASSERTIONS=ON > "$LLVM_BUILD.cmake.log" 2>&1 || die "cmake failed: $LLVM_BUILD.cmake.log"
    ninja -C "$LLVM_BUILD" clang llvm-objdump llvm-nm LTO || die "ninja failed"
  fi
  # a from-source clang does not infer the macOS SDK (CLAUDE.md); the config file fixes every invocation
  printf -- '-isysroot %s\n' "$(xcrun --show-sdk-path)" > "$LLVM_BUILD/bin/clang.cfg"
  "$LLVM_BUILD/bin/clang" --version | head -1
fi
if want build; then info "build"; "$RIG/build_php.sh" all; fi
if want apps;  then info "apps";  "$RIG/setup_apps.sh" all; fi
if want run;   then info "run";   "$RIG/run_suite.sh"; fi
if want collect; then
  info "collect -> $E/data"
  for f in zend symfony wordpress wpapi_13 wpapi_16; do [[ -f "$W/results/$f.txt" ]] && cp "$W/results/$f.txt" "$E/data/$f.txt"; done
  for f in zend symfony wordpress 'wpapi2^13' 'wpapi2^16'; do [[ -f "$W/results/$f.json" ]] && cp "$W/results/$f.json" "$E/data/$(echo "$f" | tr '^' '_').json"; done
  cp "$W/seeds_php.txt" "$E/data/seeds_php_fixpoint.txt"; cp "$W/owned_php.txt" "$E/data/owned_php.txt"
  cp "$W/rpt/precision.txt" "$E/data/precision.txt"; cp "$W/rpt/infoloss.txt" "$E/data/infoloss.txt" 2>/dev/null || true
  for f in "$W"/rpt/obligations_round*.txt; do cp "$f" "$E/data/"; done
  cp "$W/switch_counts.txt" "$E/data/switch_counts.txt"
  ( cd "$W/phpf-base" && for f in main/dit_bracket.h ext/hash/hash.c ext/standard/md5.c ext/standard/crypt.c ext/standard/password.c; do
      diff -u <(tar -xOzf "$W/src/php-8.4.25.tar.gz" "php-8.4.25/$f" 2>/dev/null) "$f" | sed "1s|.*|--- php-8.4.25/$f|; 2s|.*|+++ $f|"; done ) > "$E/data/bracket.diff" || true
  {
    echo "== $(date '+%Y-%m-%d %H:%M %Z')  host $(sysctl -n hw.model) $(sysctl -n machdep.cpu.brand_string)  macOS $(sw_vers -productVersion)"
    echo "compiler: $(git -C "$R" rev-parse HEAD)$(git -C "$R" diff --quiet || echo ' +uncommitted')  $("$LLVM_BUILD/bin/clang" --version | head -1)"
    echo "php: $("$W/phpf-base/sapi/cli/php" -r 'echo PHP_VERSION;') from $(shasum -a 256 "$W/src/php-8.4.25.tar.gz" | cut -c1-16)...  wordpress: $(git -C "$W/apps/wordpress" rev-parse --short HEAD)  symfony-demo: $(git -C "$W/apps/symfony-demo" rev-parse --short HEAD)"
    echo "mariadb: $(brew list --versions mariadb)  warmup/measured: ${WARMUP:-50}/${MEASURED:-100}"
    echo "switch counts: $(tr '\n' ';' < "$W/switch_counts.txt")"
  } >> "$E/data/provenance.txt"
  tail -5 "$E/data/provenance.txt"
fi
if want stopdb; then "$RIG/setup_apps.sh" stopdb; fi
info "done: $STAGES"
