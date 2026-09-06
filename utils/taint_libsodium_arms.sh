#!/usr/bin/env bash
#
# Build the CIO-parity libsodium arms on Apple silicon at EXACTLY the compiler
# configuration the gem5 rig uses.
#
# This is the Apple-silicon counterpart of
# utils/dit_host_screening/cioparity/build_arms.sh. Everything about the library
# build -- the seed file, the placement, the callee contract, the twins, the
# owned-symbols list, -march, --disable-asm, the CIO rename patch -- is copied
# from that script on purpose, so that an M4 number and a gem5 number differ by
# the machine and not by the compiler. Keep the two in sync: if you change an
# arm's flags here, change them there.
#
# WHY THIS IS A SECOND BUILD PATH, and not a stage bolted onto
# utils/taint_libsodium_eval.sh. That script builds ONE whole-library bitcode
# module (wllvm -> llvm-link -> llc -run-taint-interproc) and hands the pass the
# entire program at once. The compiler's defaults since 2026-09-05 are
# per-translation-unit concepts and have no meaning on that path:
#
#   * the callee contract answers "what may I assume about a callee I cannot
#     see?" -- on whole-program IR there are no unseen callees;
#   * -taint-owned-symbols names the functions THIS BUILD defines so a per-TU
#     compile can tell our code from libc -- one module makes the list a no-op;
#   * the DIT twins (-taint-dit-clone-seeded) are emitted per TU and named
#     across TUs, which is the case the owned list exists to support.
#
# So the wllvm path cannot express the shipped configuration even in principle.
# It is still the right rig for what it was built for and its archives back the
# published numbers in paper_experiments/09; this script does not touch them.
# It writes NEW archive names (base, taint, taintnop, taintold, taintoldnop)
# alongside the old ones (baseline, hardened, func, fine, narrow, nopsw).
#
# USAGE
#   utils/taint_libsodium_arms.sh              # all stages
#   utils/taint_libsodium_arms.sh lib          # build the library variants
#   utils/taint_libsodium_arms.sh verify       # re-print the parity gate only
#
# ENV
#   LLVM_BIN=<dir>   taint toolchain bin/   (default <repo>/build/bin)
#   SRC=<dir>        pristine libsodium     (default ~/Documents/libsodium-1.0.21)
#   ARMS_WORK=<dir>  per-variant build root (default ~/Documents/libsodium-arms-m4)
#   WORK=<dir>       where archives install (default $SRC -- where the runner looks)
#   SEEDS=<file>     contract fixpoint seeds  (default: the gem5-DIT submodule,
#                    else fetched from the pinned commit with gh)
#   SEEDS_OLD=<file> CIO seeds for the taintold arm
#   VARIANTS=<list>  default "base taint taintnop taintold taintoldnop"
#   MARCH=<arch>     default armv8.4-a, as gem5 (FEAT_DIT is armv8.4)
#   JOBS=<n>
#
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LLVM_BIN="${LLVM_BIN:-$REPO_ROOT/build/bin}"
SRC="${SRC:-$HOME/Documents/libsodium-1.0.21}"
ARMS_WORK="${ARMS_WORK:-$HOME/Documents/libsodium-arms-m4}"
WORK="${WORK:-$SRC}"
MARCH="${MARCH:-armv8.4-a}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)}"
OWNED="$ARMS_WORK/owned.txt"
CC="$LLVM_BIN/clang"

# The gem5 rig takes both seed files from the gem5-DIT tree, which is correct
# there: that rig needs the simulator anyway. This one needs those two files and
# nothing else from gem5-DIT, and a multi-gigabyte simulator checkout is a bad
# price for building a libsodium archive on a Mac. So resolve in this order:
#
#   1. the gem5-DIT submodule, if it happens to be checked out (authoritative);
#   2. utils/dit_host_screening/cioparity/seeds/, copies taken at the commit the
#      submodule pins -- see the README there, and note they can drift;
#   3. a `gh` fetch at that pinned commit, for a tree with neither.
#
# Whichever wins, it is the same revision the gem5 numbers were produced at,
# which is what keeps the two rigs measuring the same program.
G5="${G5:-$REPO_ROOT/gem5-DIT}"
VENDORED="$REPO_ROOT/utils/dit_host_screening/cioparity/seeds"
pick_seed() {   # <basename> -> path, preferring the submodule then the vendored copy
  if [[ -f "$G5/benchmarks/crypto/$1" ]]; then echo "$G5/benchmarks/crypto/$1"
  else echo "$VENDORED/$1"; fi
}
SEEDS="${SEEDS:-$(pick_seed libsodium_secret_contract.txt)}"
SEEDS_OLD="${SEEDS_OLD:-$(pick_seed libsodium_secret.txt)}"

VARIANTS="${VARIANTS:-base taint taintnop taintold taintoldnop}"
ALL_STAGES="seeds lib install verify"

info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[33m    %s\033[0m\n' "$*"; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

[[ "${1:-}" == "--list" ]] && { echo "$ALL_STAGES"; exit 0; }
STAGES="${*:-$ALL_STAGES}"
want() { [[ " $STAGES " == *" $1 "* ]]; }

# ---------------------------------------------------------------- preflight
[[ -x "$CC" ]]  || die "no clang at $CC (set LLVM_BIN)"
[[ -d "$SRC" ]] || die "no libsodium source at $SRC -- utils/taint_libsodium_eval.sh fetch"

# A from-source clang on macOS cannot find the SDK by itself; it reads a config
# file next to the binary. taint_libsodium_eval.sh writes the same file.
if [[ "$(uname -s)" == Darwin && ! -f "$LLVM_BIN/clang.cfg" ]] && command -v xcrun >/dev/null; then
  printf -- '-isysroot %s\n' "$(xcrun --show-sdk-path)" > "$LLVM_BIN/clang.cfg"
  info "created $LLVM_BIN/clang.cfg (macOS SDK sysroot)"
fi

# The shipped defaults are only in a toolchain built after 2026-09-05. Refusing
# here is the whole point: an older clang accepts -ftaint-harden, ignores the
# flags it does not know, and silently produces the PRE-contract arm under the
# name `taint`. That is a wrong number that looks like a right one.
if ! "$LLVM_BIN/llc" --help-hidden 2>/dev/null | grep -q -- '-taint-owned-symbols'; then
  die "$LLVM_BIN is the pre-2026-09-05 compiler: no -taint-owned-symbols.
     The callee contract, the twins and the owned list are what this script
     exists to build, and this toolchain cannot express them.
     Rebuild it:  ninja -C <llvm build dir> clang llc opt llvm-nm llvm-ar llvm-objdump"
fi

mkdir -p "$ARMS_WORK"

# ---------------------------------------------------------------- seeds
# Resolved whenever a library will be built, not only for the `seeds` stage:
# lib_cflags() reads $SEEDS, so `arms.sh lib` on its own must resolve it too.
if want seeds || want lib; then
  info "seed files"
  PIN="$(git -C "$REPO_ROOT" ls-tree HEAD gem5-DIT 2>/dev/null | awk '{print $3}')"
  fetch_seed() {   # <path-in-gem5-DIT> <dest>
    [[ -n "$PIN" ]] || return 1
    command -v gh >/dev/null || return 1
    gh api "repos/UT-Security/gem5-DIT/contents/$1?ref=$PIN" --jq '.content' 2>/dev/null \
      | base64 -d > "$2" 2>/dev/null && [[ -s "$2" ]]
  }
  if [[ ! -f "$SEEDS" ]]; then
    warn "no $SEEDS -- fetching from gem5-DIT at the pinned commit ${PIN:0:12}"
    SEEDS="$ARMS_WORK/libsodium_secret_contract.txt"
    fetch_seed benchmarks/crypto/libsodium_secret_contract.txt "$SEEDS" \
      || die "could not obtain the contract seed file.
     Either check the submodule out (git submodule update --init gem5-DIT)
     or set SEEDS=<path to libsodium_secret_contract.txt>."
  fi
  if [[ ! -f "$SEEDS_OLD" ]] && [[ " $VARIANTS " == *" taintold"* ]]; then
    SEEDS_OLD="$ARMS_WORK/libsodium_secret.txt"
    fetch_seed benchmarks/crypto/libsodium_secret.txt "$SEEDS_OLD" \
      || warn "no CIO seed file; the taintold arms will be skipped"
  fi
  # 188 non-comment lines is the round-11 fixpoint
  # (docs/results/dit-callee-contract-2026-09-04.md section 4). A different
  # count is not fatal -- it is a different seed round, and you should know.
  n=$(grep -v '^[[:space:]]*#' "$SEEDS" 2>/dev/null | grep -c .)
  info "    contract seeds: $SEEDS ($n lines)"
  [[ "$n" == 188 ]] || warn "expected 188 seed lines (round-11 fixpoint), found $n"
  [[ -f "$SEEDS_OLD" ]] && info "    CIO seeds:      $SEEDS_OLD ($(grep -v '^[[:space:]]*#' "$SEEDS_OLD" | grep -c .) lines)"
fi

# ---------------------------------------------------------------- lib
# The functions this build defines: what a twin may be named across TUs and what
# the obligation report may propose. gem5's build_arms.sh does this with one
# awk; Mach-O needs two corrections, and BOTH are silent if you get them wrong:
#
#   * every symbol carries a leading '_' ("_crypto_sign"), while the pass
#     matches the IR name it sees in the MachineInstr ("crypto_sign"). An
#     unstripped list matches NOTHING, so every one of our own cross-TU callees
#     is filed as external and the contract degrades to no ownership at all --
#     with no diagnostic.
#   * compiler-local labels (ltmp0, ltmp1) are lowercase-'t' text symbols and
#     pass the same filter. Requiring the leading '_' drops them for free.
derive_owned() {   # <archive>
  "$LLVM_BIN/llvm-nm" --defined-only --no-demangle "$1" 2>/dev/null \
    | awk 'NF == 3 && $2 ~ /^[tTwW]$/ && $3 ~ /^_/ { print substr($3, 2) }' \
    | sort -u > "$OWNED"
  local n; n=$(grep -c . "$OWNED")
  info "    owned symbols: $n"
  # The library defines ~912 functions (docs/results/dit-callee-contract-2026-09-04.md).
  # An order-of-magnitude miss means the strip above did not do what it should.
  [[ "$n" -gt 500 ]] || die "owned list has only $n entries -- symbol naming is wrong, not a smaller library"
}

# The arm -> CFLAGS table. Copied from cioparity/build_arms.sh lib_cflags();
# keep them identical.
lib_cflags() {
  case "$1" in
    # Unhardened. Also the blanket and API-bracket arms' library: those two are
    # this same codegen, in a different mode / with a wrapper linked in.
    base)        echo "-O2" ;;
    # Shipped defaults (2026-09-05): region placement inside the block, callee
    # contract, DIT twins, the contract's fixpoint seeds and the owned list.
    taint)       echo "-O2 -ftaint-harden=$SEEDS -mllvm -taint-owned-symbols=$OWNED ${TAINT_EXTRA:-}" ;;
    # Layout control: identical placement and instruction count at identical
    # addresses, every `msr DIT` emitted as HINT #0, so no mode switch ever
    # executes. (taint - taintnop) is DIT's real cost; the rest is layout.
    taintnop)    echo "-O2 -ftaint-harden=$SEEDS -mllvm -taint-owned-symbols=$OWNED ${TAINT_EXTRA:-} -mllvm -taint-dit-nop-switches" ;;
    # The pre-2026-09-05 compiler: inherit contract, no twins, the CIO seeds.
    taintold)    echo "-O2 -ftaint-harden=$SEEDS_OLD -mllvm -taint-dit-contract=inherit -mllvm -taint-dit-clone-seeded=0" ;;
    taintoldnop) echo "-O2 -ftaint-harden=$SEEDS_OLD -mllvm -taint-dit-contract=inherit -mllvm -taint-dit-clone-seeded=0 -mllvm -taint-dit-nop-switches" ;;
    *) die "unknown variant: $1" ;;
  esac
}

if want lib; then
for v in $VARIANTS; do
  W="$ARMS_WORK/$v"
  lib="$W/src/libsodium/.libs/libsodium.a"

  if [[ -f "$lib" ]]; then
    info "libsodium '$v' already built -- skipping"
    [[ "$v" == base && ! -f "$OWNED" ]] && derive_owned "$lib"
    continue
  fi
  # base must go first: every other variant needs the owned list it derives.
  if [[ "$v" != base && ! -f "$OWNED" ]]; then
    die "no $OWNED -- build the 'base' variant first (it derives the owned list)"
  fi
  case "$v" in taintold*) [[ -f "$SEEDS_OLD" ]] || { warn "skip $v -- no CIO seed file"; continue; } ;; esac

  info "libsodium variant '$v'"
  rm -rf "$W"; mkdir -p "$W"
  ( cd "$SRC" && tar cf - --exclude=.git --exclude='*.o' --exclude='*.lo' \
      --exclude='*.a' --exclude='*.la' --exclude=.libs . ) | ( cd "$W" && tar xf - ) \
    || die "could not copy source for $v"
  # A stale configure-generated Makefile came across in the copy; drop it so the
  # variant's own configure runs. (Also drops a CIO rig's staged Makefile, the
  # trap taint_libsodium_eval.sh documents.)
  rm -f "$W/Makefile" "$W/config.status"

  # CIO seeds three symbols that only exist after THEIR rename patch, all
  # statics in crypto_stream/chacha20/ref/chacha20_ref.c. Without it the seed
  # file silently under-seeds: unmatched names are ignored without warning.
  # Idempotent: \b after stream_ref cannot match inside stream_ref_ref.
  f="$W/src/libsodium/crypto_stream/chacha20/ref/chacha20_ref.c"
  [[ -f "$f" ]] || die "not found: $f"
  if ! grep -q 'chacha20_encrypt_bytes_ref' "$f"; then
    perl -pi -e 's/\bstream_ref\b/stream_ref_ref/g;
                 s/\bstream_ref_xor_ic\b/stream_ref_xor_ic_ref/g;
                 s/\bchacha20_encrypt_bytes\b/chacha20_encrypt_bytes_ref/g' "$f"
  fi
  grep -q 'chacha20_encrypt_bytes_ref' "$f" || die "rename patch did not apply for $v"

  # --disable-asm is REQUIRED, not a tuning choice: hand-written .S never goes
  # through the pass, so anything in it would be invisible to the analysis.
  # gem5 also passes --disable-pie; omitted here because arm64 macOS cannot link
  # a non-PIE executable and configure's link probes would fail. It affects only
  # libsodium's own test binaries, never the archive this script installs.
  ( cd "$W" && CC="$CC" \
      ./configure --disable-shared --enable-static --disable-asm \
        CFLAGS="-march=$MARCH $(lib_cflags "$v")" > configure.log 2>&1 ) \
    || { tail -25 "$W/configure.log" >&2; die "configure failed for $v"; }
  ( cd "$W" && make -j"$JOBS" > build.log 2>&1 ) \
    || { tail -30 "$W/build.log" >&2; die "build failed for $v"; }
  [[ -f "$lib" ]] || die "no archive produced for $v"

  [[ "$v" == base ]] && derive_owned "$lib"
done
fi

# ---------------------------------------------------------------- install
# Put the archives where taint_libsodium_sudo_run.sh looks: $WORK/libsodium-<v>.a
if want install; then
  info "install archives -> $WORK"
  for v in $VARIANTS; do
    src="$ARMS_WORK/$v/src/libsodium/.libs/libsodium.a"
    [[ -f "$src" ]] || { warn "no archive for $v"; continue; }
    cp -f "$src" "$WORK/libsodium-$v.a" || die "install failed for $v"
    printf '    %-14s %s\n' "$v" "$(du -h "$WORK/libsodium-$v.a" | cut -f1)"
  done
  # Record the exact CFLAGS each archive was built with, NEXT TO the archives,
  # so the record travels with the thing it describes. sudo_run.sh folds this
  # into provenance.txt. Without it a result names its arms but not the compiler
  # configuration that produced them, which is precisely what left experiment 01
  # unreproducible (see paper_experiments/README.md, "the general lesson").
  { echo "# libsodium arm CFLAGS -- written by taint_libsodium_arms.sh"
    echo "date: $(date -u +%FT%TZ)"
    echo "clang: $LLVM_BIN"
    "$LLVM_BIN/clang" --version 2>/dev/null | sed -n '1,2p' | sed 's/^/  /'
    echo "march: $MARCH"
    echo "SEEDS: $SEEDS"
    echo "SEEDS_OLD: $SEEDS_OLD"
    echo "OWNED: $OWNED"
    # Empty is the shipped configuration and is worth stating explicitly: a blank
    # line here means no extra -mllvm reached the pass, not that nobody looked.
    echo "TAINT_EXTRA: ${TAINT_EXTRA:-<unset>}"
    for v in $VARIANTS; do
      [[ -f "$WORK/libsodium-$v.a" ]] || continue
      echo "  $v: $(lib_cflags "$v")"
    done
  } > "$WORK/arm_flags.txt"
  info "flags recorded -> $WORK/arm_flags.txt"
fi

# ---------------------------------------------------------------- verify
# The parity gate. These counts are a property of the COMPILER CONFIGURATION,
# not of the machine, so they must match what the gem5 rig reports for the same
# arm. If they do not, the two rigs are not running the same experiment and no
# amount of careful timing will fix it.
if want verify; then
  info "parity gate: static counts per arm"
  # NOT 'hint #0' as gem5's build_arms.sh greps for: the AArch64 disassembler
  # prints HINT #0 by its canonical alias, `nop`. Counting the literal string
  # reports 0 for a NOP arm that is in fact perfectly NOPed, which reads as
  # "the control did not build" when nothing is wrong.
  # bash 3.2 (which is what /bin/bash is on macOS) has no associative arrays, so
  # the per-variant counts go to a scratch file and come back with awk.
  counts="$ARMS_WORK/.verify_counts"; : > "$counts"
  printf '    %-14s %10s %10s %8s\n' variant 'msr DIT' nop twins
  for v in $VARIANTS; do
    a="$WORK/libsodium-$v.a"; [[ -f "$a" ]] || continue
    n=$("$LLVM_BIN/llvm-objdump" -d "$a" 2>/dev/null | grep -icE '\bmsr\b[[:space:]]+dit,')
    h=$("$LLVM_BIN/llvm-objdump" -d "$a" 2>/dev/null | grep -cE '^[[:space:]]+[0-9a-f]+:.*\bnop\b')
    t=$("$LLVM_BIN/llvm-nm" "$a" 2>/dev/null | grep -c ' [TtWw] .*\.dit$')
    printf '%s %s %s\n' "$v" "$n" "$t" >> "$counts"
    printf '    %-14s %10s %10s %8s\n' "$v" "$n" "$h" "$t"
  done
  # A NOP arm is only a layout control if it is the SAME build with the switches
  # turned into nops: same twins, no mode write left. Anything else and the
  # "real minus NOP" column is comparing two different programs.
  msr_of()  { awk -v v="$1" '$1 == v { print $2 }' "$counts"; }
  twin_of() { awk -v v="$1" '$1 == v { print $3 }' "$counts"; }
  for pair in "taint taintnop" "taintold taintoldnop"; do
    set -- $pair; r=$1; z=$2
    rm=$(msr_of "$r"); zm=$(msr_of "$z"); rt=$(twin_of "$r"); zt=$(twin_of "$z")
    [[ -n "$rm" && -n "$zm" ]] || continue
    [[ "$zm" == 0 ]] || die "$z still has $zm 'msr DIT' -- not a NOP control"
    [[ "$zt" == "$rt" ]] || die "$z has $zt twins but $r has $rt -- not the same build"
  done
  bm=$(msr_of base); [[ -z "$bm" || "$bm" == 0 ]] || die "base has $bm 'msr DIT' -- it must be unhardened"
  tt=$(twin_of taint); [[ -z "$tt" || "$tt" -gt 0 ]] || die "taint has no .dit twins -- the twins did not run"
  cat <<'EOF'

    Expected for the shipped defaults, from the gem5 run this mirrors
    (paper_experiments/09 "The pass arm"): 364 switch sites and 85 twins in
    the library for `taint`; `base` must have 0 of both; `taintnop` must have
    the same twin count as `taint` and 0 executed switches (its `msr DIT`s are
    HINT #0). Small drift in the switch-site count across libsodium point
    releases is expected; a twin count of 0 is not -- that means the twins did
    not run and the arm is mislabelled.
EOF
fi

info "done: $STAGES"
