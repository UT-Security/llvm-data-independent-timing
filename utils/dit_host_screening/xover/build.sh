#!/bin/bash
# Build the crossover composite for native Apple silicon and for gem5.
#
# ARMS. Four binaries; the DIT mode of the first is chosen at RUNTIME from argv,
# so off/always/oracle/oracle_batch share one instruction stream and
# dit-measurement-traps trap 7b (the per-binary codegen lottery) cannot apply to
# the comparison that matters most.
#
#   nodit  -ftaint-harden=<EMPTY seed>   THE BASELINE. Zero msr DIT, but it has
#                                        been through the 3-phase MIR round trip,
#                                        so it carries the same codegen artifact
#                                        as the pass arms. Never baseline against
#                                        `plain`.
#   hoist  real seed, -taint-dit-loop-hoist=1
#   gated  real seed, + -taint-modset-callsite-gated (strict defaults on)
#   plain  stock -O2, no taint pipeline at all. Reference only - the delta
#          plain->nodit IS the round-trip artifact, and is worth reporting.
#
# Only secp256k1 is instrumented. The host, the payload shim and SQLite are
# compiled identically in every arm.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${OUT:-$HOME/Documents/dit-crossover}"
LLVM_BUILD="${LLVM_BUILD:-$(cd "$HERE/../../.." && pwd)/build}"
SECP="${SECP:-$HOME/Documents/bitcoin/src/secp256k1}"
SQLITE_C="${SQLITE_C:-$HOME/Documents/sqlcipher-4.6.1/sqlite3.c}"
SQLITE_DIR="$(dirname "$SQLITE_C")"
GEM5_ROOT="${GEM5_ROOT:-$(cd "$HERE/../../.." && pwd)/gem5-DIT}"
JOBS="${JOBS:-4}"

CLANG="$LLVM_BUILD/bin/clang"
[[ -x "$CLANG" ]] || { echo "no clang at $CLANG - build it (ninja -C <repo>/build clang) or set LLVM_BUILD" >&2; exit 1; }
XCC="$GEM5_ROOT/util/cross/taint-cross-cc"

SECP_DEFS="-DECMULT_WINDOW_SIZE=15 -DECMULT_GEN_KB=86 -I$SECP/src -I$SECP/include"
SQL_DEFS="-DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_TEMP_STORE=2"

SEED="$OUT/src/secp_seed.txt"
EMPTY="$OUT/src/empty_seed.txt"

mkdir -p "$OUT"/{src,build/native,build/gem5,out/gem5,out/native,log}
printf 'secp256k1_ecdsa_sign,3,pointee\n' > "$SEED"
: > "$EMPTY"

# macOS SDK for a from-source clang (CLAUDE.md sec Build).
[[ -f "$LLVM_BUILD/bin/clang.cfg" ]] || \
    printf -- '-isysroot %s\n' "$(xcrun --show-sdk-path)" > "$LLVM_BUILD/bin/clang.cfg"

say() { printf '\n=== %s ===\n' "$*"; }

# ---------------------------------------------------------------- native ----
build_native() {
    local name="$1"; shift
    local bd="$OUT/build/native/$name"
    mkdir -p "$bd"
    say "native/$name"

    # secp256k1: the only instrumented TU
    "$CLANG" -O2 -c $SECP_DEFS "$@" "$SECP/src/secp256k1.c" -o "$bd/secp256k1.o"
    # precomputed tables: large, no secret, never instrumented
    "$CLANG" -O2 -c $SECP_DEFS "$SECP/src/precomputed_ecmult.c"     -o "$bd/pre1.o"
    "$CLANG" -O2 -c $SECP_DEFS "$SECP/src/precomputed_ecmult_gen.c" -o "$bd/pre2.o"
    # public lane + shim: identical in every arm
    "$CLANG" -O2 -c $SQL_DEFS "$SQLITE_C" -o "$bd/sqlite3.o"
    "$CLANG" -O2 -c -I"$SECP/include" -I"$HERE" "$HERE/secret_payload.c" -o "$bd/payload.o"
    "$CLANG" -O2 -c -I"$SQLITE_DIR" -I"$HERE" "$HERE/host_sqlite.c" -o "$bd/host.o"

    "$CLANG" -O2 "$bd"/{host,payload,sqlite3,secp256k1,pre1,pre2}.o \
        -o "$OUT/build/native/xover_$name"
    printf 'msr DIT sites: %s\n' \
        "$("$LLVM_BUILD/bin/llvm-objdump" -d "$bd/secp256k1.o" | grep -ci 'msr.*dit' || true)"
}

# ------------------------------------------------------------------ gem5 ----
build_gem5() {
    local name="$1"; shift
    local bd="$OUT/build/gem5/$name"
    mkdir -p "$bd"
    say "gem5/$name"
    local G5="-DUSE_M5 -DGEM5_NO_SELF_TIMING"

    LLVM_BUILD="$LLVM_BUILD" "$XCC" -O2 -c $SECP_DEFS "$@" "$SECP/src/secp256k1.c" -o "$bd/secp256k1.o"
    LLVM_BUILD="$LLVM_BUILD" "$XCC" -O2 -c $SECP_DEFS "$SECP/src/precomputed_ecmult.c"     -o "$bd/pre1.o"
    LLVM_BUILD="$LLVM_BUILD" "$XCC" -O2 -c $SECP_DEFS "$SECP/src/precomputed_ecmult_gen.c" -o "$bd/pre2.o"
    LLVM_BUILD="$LLVM_BUILD" "$XCC" -O2 -c $SQL_DEFS "$SQLITE_C" -o "$bd/sqlite3.o"
    LLVM_BUILD="$LLVM_BUILD" "$XCC" -O2 -c -I"$SECP/include" -I"$HERE" $G5 "$HERE/secret_payload.c" -o "$bd/payload.o"
    LLVM_BUILD="$LLVM_BUILD" "$XCC" -O2 -c -I"$SQLITE_DIR" -I"$HERE" $G5 "$HERE/host_sqlite.c" -o "$bd/host.o"

    LLVM_BUILD="$LLVM_BUILD" "$XCC" -O2 "$bd"/{host,payload,sqlite3,secp256k1,pre1,pre2}.o \
        -lm5 -o "$OUT/build/gem5/xover_$name"
    printf 'msr DIT sites: %s\n' \
        "$("$LLVM_BUILD/bin/llvm-objdump" -d "$bd/secp256k1.o" | grep -ci 'msr.*dit' || true)"
}

TARGET="${1:-all}"

if [[ "$TARGET" == "native" || "$TARGET" == "all" ]]; then
    build_native plain
    build_native nodit -ftaint-harden="$EMPTY"
    build_native hoist -ftaint-harden="$SEED" -mllvm -taint-dit-loop-hoist=1
    build_native gated -ftaint-harden="$SEED" -mllvm -taint-dit-loop-hoist=1 \
                       -mllvm -taint-modset-callsite-gated
    # Shipped default: block-minimal. Same work, many more regions - the R
    # contrast that does not require changing the workload at all.
    build_native hoist0 -ftaint-harden="$SEED" -mllvm -taint-dit-loop-hoist=0
fi

if [[ "$TARGET" == "gem5" || "$TARGET" == "all" ]]; then
    build_gem5 plain
    build_gem5 nodit -ftaint-harden="$EMPTY"
    build_gem5 hoist -ftaint-harden="$SEED" -mllvm -taint-dit-loop-hoist=1
    build_gem5 gated -ftaint-harden="$SEED" -mllvm -taint-dit-loop-hoist=1 \
                     -mllvm -taint-modset-callsite-gated
fi

say "built"
ls -la "$OUT/build/native/" "$OUT/build/gem5/" 2>/dev/null | grep xover_ || true
