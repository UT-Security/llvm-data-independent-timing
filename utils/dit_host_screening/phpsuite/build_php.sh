#!/usr/bin/env bash
#
# Experiment 11: PHP 8.4.25 built the way php-src's benchmark suite needs it (php-cgi,
# opcache, the extensions Symfony Demo and WordPress use), from the pristine tarball,
# with the developer's bracket wrapped around the seven crypto builtins
# (patch_bracket.py; the macros are inert unless -DDIT_BRACKET=1, so one patched tree
# builds every arm). Also builds libditctl.dylib, the injected constructor that sets
# blanket DIT from outside the program and reads PSTATE.DIT back at exit.
#
# variants (each a copy of the base tree with a few objects rebuilt and relinked):
#   base        -O2                                             arm A;  arm C = A + ENABLE_DIT=1
#   bracket     the 4 bracket TUs with -DDIT_BRACKET=1           arm B   (msr DIT,#1; sb ... msr DIT,#0)
#   bracketnop  ... -DDIT_BRACKET_NOP=1                          arm Bn  (hint #0 in each place)
#   taint       ext/hash + ext/standard crypto TUs under -ftaint-harden, sb after every enable   arm P
#   taintnop    ... -taint-dit-nop-switches                      arm Z
#
#   build_php.sh base | bracket | taint | all
#
# Env: W           work dir (default ~/Documents/dit-phpsuite); everything lands under it
#      LLVM_BUILD  the taint compiler's build dir (default <repo>/build)
#      SEEDS       seed file to start the seed loop from (default: a copy of seeds_php.txt,
#                  the 12 hand-written lines; the loop reaches the 28-line fixpoint in 3
#                  rounds. SEEDS=seeds_php_fixpoint.txt builds the pass arm in one round)
#      JOBS        make parallelism (default 10)
set -uo pipefail
RIG="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$RIG/../../.." && pwd)"
W="${W:-$HOME/Documents/dit-phpsuite}"
LLVM_BUILD="${LLVM_BUILD:-$REPO/build}"
CC="$LLVM_BUILD/bin/clang"; LB="$LLVM_BUILD/bin"
JOBS="${JOBS:-10}"
PHPV=php-8.4.25
PHP_URL="https://www.php.net/distributions/$PHPV.tar.gz"
MAXROUNDS="${MAXROUNDS:-6}"
info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
[[ -x "$CC" ]] || die "no clang at $CC (set LLVM_BUILD)"
[[ "$(uname -m)" == arm64 ]] || die "Apple Silicon only (PSTATE.DIT)"
SDK="$(xcrun --show-sdk-path 2>/dev/null)"; [[ -d "$SDK" ]] || die "no macOS SDK (xcode-select --install)"
for p in sqlite zlib oniguruma; do brew --prefix "$p" >/dev/null 2>&1 || die "brew install $p"; done
command -v pkg-config >/dev/null || die "brew install pkgconf"
what="${1:-all}"
mkdir -p "$W/src" "$W/rpt"
BASE="$W/phpf-base"
OWNED="$W/owned_php.txt"
SEEDS="${SEEDS:-$W/seeds_php.txt}"
[[ -s "$SEEDS" ]] || cp "$RIG/seeds_php.txt" "$SEEDS"

# the crypto TUs: what the bracket wraps, and what the pass hardens
BRACKET_OBJS="ext/hash/hash.lo ext/standard/md5.lo ext/standard/crypt.lo ext/standard/password.lo"
TAINT_OBJS="ext/hash/*.lo ext/standard/md5.lo ext/standard/crypt.lo ext/standard/crypt_blowfish.lo ext/standard/crypt_freesec.lo ext/standard/crypt_sha256.lo ext/standard/crypt_sha512.lo ext/standard/password.lo"

build_ditctl() {
  info "libditctl.dylib from utils/cio_ditctl.c"
  cc -O2 -dynamiclib -o "$W/libditctl.dylib" "$REPO/utils/cio_ditctl.c" || die "libditctl build failed"
}

build_base() {
  [[ -s "$W/src/$PHPV.tar.gz" ]] || { info "fetching $PHP_URL"; curl -fsSL -o "$W/src/$PHPV.tar.gz" "$PHP_URL" || die "download failed"; }
  info "PHP full build -> $BASE"
  rm -rf "$BASE"; tar xzf "$W/src/$PHPV.tar.gz" -C "$W" && mv "$W/$PHPV" "$BASE"
  python3 "$RIG/patch_bracket.py" "$BASE" || die "bracket patch failed"
  ( cd "$BASE" && CC="$CC" CFLAGS="-O2" \
      PKG_CONFIG_PATH="$(brew --prefix sqlite)/lib/pkgconfig:$(brew --prefix zlib)/lib/pkgconfig:$(brew --prefix oniguruma)/lib/pkgconfig" \
      LIBXML_CFLAGS="-I$SDK/usr/include/libxml2" LIBXML_LIBS="-lxml2" \
      ./configure --prefix="$BASE/prefix" --enable-cli --enable-cgi --disable-phpdbg --without-pear \
        --enable-opcache --enable-mbstring --with-mysqli --with-pdo-mysql --with-pdo-sqlite --with-sqlite3 \
        --with-zlib --with-iconv="$SDK/usr" --enable-session --enable-tokenizer --enable-ctype --enable-filter \
        --enable-dom --enable-xml --enable-simplexml --enable-xmlreader --enable-xmlwriter \
        --enable-fileinfo --enable-posix --enable-phar --enable-pdo --enable-intl=no --without-openssl > configure.log 2>&1 \
      && make -j"$JOBS" > build.log 2>&1 ) || { tail -25 "$BASE/build.log" "$BASE/configure.log" 2>/dev/null | tail -40; die "php full build failed"; }
  [[ -x "$BASE/sapi/cli/php" && -x "$BASE/sapi/cgi/php-cgi" ]] || die "php or php-cgi missing"
  "$BASE/sapi/cli/php" -r 'echo "php ok: ", PHP_VERSION, " exts=", count(get_loaded_extensions()), " hmac=", substr(hash_hmac("sha256","x","k"),0,16), " md5=", md5("x"), "\n";'
  ls "$BASE"/modules/opcache.so >/dev/null 2>&1 && info "    opcache.so built" || die "opcache.so missing"
  "$LB/llvm-nm" --defined-only --no-demangle "$BASE"/ext/hash/*.o "$BASE"/ext/standard/{md5,crypt,crypt_blowfish,crypt_freesec,crypt_sha256,crypt_sha512,password}.o 2>/dev/null \
    | awk 'NF == 3 && $2 ~ /^[tTwW]$/ { print $3 }' | sed 's/^_//; s/^\.L//' | grep -vE '^ltmp[0-9]+$' | sort -u > "$OWNED"
  info "    owned (crypto TUs) symbols: $(wc -l < "$OWNED" | tr -d ' ')"
  n=$("$LB/llvm-objdump" -d "$BASE/sapi/cgi/php-cgi" | grep -ciE '\bmsr\b\s+dit,'); [[ "$n" == 0 ]] || die "base php-cgi carries $n msr DIT"
}

variant() {  # $1 name, $2 objects to rebuild (globs), $3 extra CFLAGS
  local v=$1 T="$W/phpf-$1" cf="-O2 $3"
  info "variant '$v': rebuilding [$2] with $cf" | cut -c1-220
  rm -rf "$T"; cp -R "$BASE" "$T"
  ( cd "$T" && for o in $2; do rm -f "$o" "${o%.lo}.o"; done \
      && make -j"$JOBS" CFLAGS_CLEAN="$cf" > build.log 2>&1 ) || { tail -20 "$T/build.log"; die "php $v build failed"; }
  local php="$T/sapi/cli/php" cgi="$T/sapi/cgi/php-cgi" n t s
  [[ -x "$cgi" ]] || die "$v: php-cgi missing"
  n=$("$LB/llvm-objdump" -d "$cgi" | grep -ciE '\bmsr\b\s+dit,'); s=$("$LB/llvm-objdump" -d "$cgi" | grep -cE '\tsb$'); t=$("$LB/llvm-nm" "$cgi" | grep -c ' [Tt] .*\.dit$')
  info "    php-cgi: msr DIT sites $n, sb $s, twins $t"
  echo "$v msr_dit=$n sb=$s twins=$t" >> "$W/switch_counts.txt"
  for kv in "k:x" "secretkey:hello world" "0123456789abcdef0123456789abcdef:$(printf 'a%.0s' {1..4096})"; do
    a=$("$BASE/sapi/cli/php" -r 'echo hash_hmac("sha256", $argv[2], $argv[1]), md5($argv[2]), crypt($argv[1], "$2y$04$abcdefghijklmnopqrstuu");' -- "${kv%%:*}" "${kv#*:}")
    b=$("$php" -r 'echo hash_hmac("sha256", $argv[2], $argv[1]), md5($argv[2]), crypt($argv[1], "$2y$04$abcdefghijklmnopqrstuu");' -- "${kv%%:*}" "${kv#*:}")
    [[ "$a" == "$b" ]] || die "$v: hash_hmac/md5/crypt differ from base"
  done
  info "    hash_hmac, md5, bcrypt match base on 3 vectors"
}

case "$what" in
  base) build_ditctl; : > "$W/switch_counts.txt"; build_base ;;
  bracket)
    variant bracket    "$BRACKET_OBJS" "-DDIT_BRACKET=1"
    variant bracketnop "$BRACKET_OBJS" "-DDIT_BRACKET=1 -DDIT_BRACKET_NOP=1" ;;
  taint)
    [[ -s "$OWNED" ]] || die "owned list missing: build base first"
    common="-ftaint-harden=$SEEDS -mllvm -taint-owned-symbols=$OWNED -mllvm -taint-dit-external-preserves=1 -mllvm -taint-dit-enable-barrier=sb"
    for round in $(seq 1 "$MAXROUNDS"); do
      info "seed round $round: $(grep -c '^[A-Za-z_]' "$SEEDS") seed lines"
      rm -f "$W/rpt/infoloss.txt" "$W/rpt/seed.txt" "$W/rpt/precision.txt"
      variant taint "$TAINT_OBJS" "$common -mllvm -taint-info-loss-report=$W/rpt/infoloss.txt -mllvm -taint-seed-report=$W/rpt/seed.txt -mllvm -taint-dit-precision-report=$W/rpt/precision.txt"
      python3 "$REPO/utils/taint_obligations.py" "$W/rpt/infoloss.txt" --owned "$OWNED" \
          --next-round "$W/seeds_php_next.txt" --seeds "$SEEDS" > "$W/rpt/obligations_round$round.txt" 2>&1
      grep -E '^(OWNED|INDIRECT|EXTERNAL)' "$W/rpt/obligations_round$round.txt" | sed 's/^/    /'
      owned=$(grep -oE '^OWNED: [0-9]+' "$W/rpt/obligations_round$round.txt" | grep -oE '[0-9]+')
      cp "$SEEDS" "$W/rpt/seeds_round$round.txt"
      if [[ "${owned:-0}" -eq 0 ]]; then info "    fixpoint at round $round"; break; fi
      cp "$W/seeds_php_next.txt" "$SEEDS"
    done
    variant taintnop "$TAINT_OBJS" "$common -mllvm -taint-dit-nop-switches" ;;
  all) "$0" base && "$0" bracket && "$0" taint ;;
  *) die "usage: $0 base|bracket|taint|all" ;;
esac
info "done ($what)"
