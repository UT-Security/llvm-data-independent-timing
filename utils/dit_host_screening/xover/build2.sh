#!/bin/bash
# Build the crossover composite. Supersedes build.sh.
#
# Only secp256k1.c differs between arms, so SQLite, the precomputed tables, the
# payload shim and the host are compiled ONCE and shared. That is not just a
# speed-up: it guarantees the public lane is bit-identical in every arm, so any
# measured difference is attributable to the instrumented TU alone.
#
# ARMS
#   plain   stock -O2, no taint pipeline
#   nodit   -ftaint-harden=<EMPTY>   the round-trip control and THE BASELINE
#   hoist   real seed, loop-hoisted
#   gated   real seed, loop-hoisted, + call-site mod-set gate
#   hoist0  real seed, SHIPPED default placement (block-minimal)
#   nopctl  = gated, but every inserted `msr DIT` emitted as `HINT #0`
#           The alignment control (Marinaro et al., AsiaCCS'24). Same instruction
#           count, same size, every instruction at the same address - so
#           nopctl ~= nodit means a measured cost is SWITCHES, and nopctl ~= gated
#           means it was code LAYOUT and the headline is wrong.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${OUT:-$HOME/Documents/dit-crossover}"
LLVM_BUILD="${LLVM_BUILD:-$(cd "$HERE/../../.." && pwd)/build}"
SECP="${SECP:-$HOME/Documents/bitcoin/src/secp256k1}"
SQLITE_C="${SQLITE_C:-$HOME/Documents/sqlcipher-4.6.1/sqlite3.c}"
SQLITE_DIR="$(dirname "$SQLITE_C")"
GEM5_ROOT="${GEM5_ROOT:-$(cd "$HERE/../../.." && pwd)/gem5-DIT}"

CLANG="$LLVM_BUILD/bin/clang"
[[ -x "$CLANG" ]] || { echo "no clang at $CLANG - build it (ninja -C <repo>/build clang) or set LLVM_BUILD" >&2; exit 1; }
OBJDUMP="$LLVM_BUILD/bin/llvm-objdump"
XCC="$GEM5_ROOT/util/cross/taint-cross-cc"

SECP_DEFS=(-DECMULT_WINDOW_SIZE=15 -DECMULT_GEN_KB=86 -I"$SECP/src" -I"$SECP/include")
SQL_DEFS=(-DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_TEMP_STORE=2)

SEED="$OUT/src/secp_seed.txt"
EMPTY="$OUT/src/empty_seed.txt"
mkdir -p "$OUT"/{src,build/native,build/gem5,out/gem5,out/native,log}
printf 'secp256k1_ecdsa_sign,3,pointee\n' > "$SEED"
: > "$EMPTY"

[[ -f "$LLVM_BUILD/bin/clang.cfg" ]] || \
    printf -- '-isysroot %s\n' "$(xcrun --show-sdk-path)" > "$LLVM_BUILD/bin/clang.cfg"

say() { printf '\n=== %s ===\n' "$*"; }

# arm name -> extra flags for the secp256k1 TU
arm_flags() {
    case "$1" in
      plain)  echo "" ;;
      nodit)  echo "-ftaint-harden=$EMPTY" ;;
      hoist)  echo "-ftaint-harden=$SEED -mllvm -taint-dit-loop-hoist=1" ;;
      gated)  echo "-ftaint-harden=$SEED -mllvm -taint-dit-loop-hoist=1 -mllvm -taint-modset-callsite-gated" ;;
      hoist0) echo "-ftaint-harden=$SEED -mllvm -taint-dit-loop-hoist=0" ;;
      nopctl) echo "-ftaint-harden=$SEED -mllvm -taint-dit-loop-hoist=1 -mllvm -taint-modset-callsite-gated -mllvm -taint-dit-nop-switches" ;;
    esac
}

build_target() {                       # $1 = native|gem5
    local T="$1" cc bd shared g5=()
    bd="$OUT/build/$T"
    shared="$bd/_shared"
    mkdir -p "$shared"
    if [[ "$T" == native ]]; then
        cc=("$CLANG")
    else
        cc=(env "LLVM_BUILD=$LLVM_BUILD" "$XCC")
        g5=(-DUSE_M5 -DGEM5_NO_SELF_TIMING)
    fi

    say "$T: shared objects (identical in every arm)"
    [[ -f "$shared/sqlite3.o" ]] || "${cc[@]}" -O2 -c "${SQL_DEFS[@]}" "$SQLITE_C" -o "$shared/sqlite3.o"
    [[ -f "$shared/pre1.o" ]]    || "${cc[@]}" -O2 -c "${SECP_DEFS[@]}" "$SECP/src/precomputed_ecmult.c"     -o "$shared/pre1.o"
    [[ -f "$shared/pre2.o" ]]    || "${cc[@]}" -O2 -c "${SECP_DEFS[@]}" "$SECP/src/precomputed_ecmult_gen.c" -o "$shared/pre2.o"
    [[ -f "$shared/payload.o" ]] || "${cc[@]}" -O2 -c -I"$SECP/include" -I"$HERE" "${g5[@]}" "$HERE/secret_payload.c" -o "$shared/payload.o"
    [[ -f "$shared/host.o" ]]    || "${cc[@]}" -O2 -c -I"$SQLITE_DIR" -I"$HERE" "${g5[@]}" "$HERE/host_sqlite.c" -o "$shared/host.o"

    for arm in plain nodit hoist gated hoist0 nopctl; do
        local ad="$bd/$arm"
        mkdir -p "$ad"
        if [[ ! -f "$ad/secp256k1.o" ]]; then
            say "$T/$arm: secp256k1.c"
            # shellcheck disable=SC2046
            "${cc[@]}" -O2 -c "${SECP_DEFS[@]}" $(arm_flags "$arm") "$SECP/src/secp256k1.c" -o "$ad/secp256k1.o"
        fi
        local link=(-O2 "$shared/host.o" "$shared/payload.o" "$shared/sqlite3.o"
                    "$ad/secp256k1.o" "$shared/pre1.o" "$shared/pre2.o")
        [[ "$T" == gem5 ]] && link+=(-lm5)
        "${cc[@]}" "${link[@]}" -o "$bd/xover_$arm"
        printf '  %-7s %s msr DIT\n' "$arm" \
            "$("$OBJDUMP" -d "$ad/secp256k1.o" | grep -ci 'msr.*dit' || true)"
    done
}

for t in "${@:-native}"; do build_target "$t"; done

say "built"
ls -la "$OUT/build"/*/xover_* 2>/dev/null | awk '{print $5, $9}'
