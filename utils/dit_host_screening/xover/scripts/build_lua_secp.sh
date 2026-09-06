#!/bin/bash
# Build the Lua public lane + libsecp256k1 secret lane composite, for gem5.
#
# The SECOND public lane on the switch-model axis. Only secp256k1.c differs
# between arms; Lua, the payload shim and the precomputed tables are compiled
# ONCE and shared, so the public lane is bit-identical in every arm and any
# measured difference is attributable to the instrumented TU alone - the same
# guarantee build2.sh gives the SQLite composite.
#
# Lua is built WITHOUT LUA_USE_LINUX on purpose: that macro pulls in dlopen and
# POSIX time, neither of which a deterministic gem5 SE replay wants. Plain ANSI
# C Lua has no dynamic loading and no clock reads.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${OUT:-$HOME/Documents/dit-crossover}"
LLVM_BUILD="${LLVM_BUILD:-$(cd "$HERE/../../.." && pwd)/build}"
SECP="${SECP:-$HOME/Documents/bitcoin/src/secp256k1}"
LUA="${LUA:-$HOME/Documents/lua-5.4.7}"
GEM5_ROOT="${GEM5_ROOT:-$(cd "$HERE/../../.." && pwd)/gem5-DIT}"
XCC="$GEM5_ROOT/util/cross/taint-cross-cc"
OBJDUMP="$LLVM_BUILD/bin/llvm-objdump"
[[ -x "$LLVM_BUILD/bin/clang" ]] || { echo "no clang at $LLVM_BUILD/bin/clang - build it (ninja -C <repo>/build clang) or set LLVM_BUILD" >&2; exit 1; }

SECP_DEFS=(-DECMULT_WINDOW_SIZE=15 -DECMULT_GEN_KB=86 -I"$SECP/src" -I"$SECP/include")

SEED="$OUT/src/secp_seed.txt"
EMPTY="$OUT/src/empty_seed.txt"
B="$OUT/build/gem5lua"
S="$B/_shared"
mkdir -p "$S" "$OUT/src" "$OUT/out/gem5" "$OUT/log"
printf 'secp256k1_ecdsa_sign,3,pointee\n' > "$SEED"
: > "$EMPTY"

cc() { env "LLVM_BUILD=$LLVM_BUILD" "$XCC" "$@"; }
say() { printf '\n=== %s ===\n' "$*"; }

arm_flags() {
    case "$1" in
      plain)  echo "" ;;
      nodit)  echo "-ftaint-harden=$EMPTY" ;;
      hoist)  echo "-ftaint-harden=$SEED -mllvm -taint-dit-loop-hoist=1" ;;
      gated)  echo "-ftaint-harden=$SEED -mllvm -taint-dit-loop-hoist=1 -mllvm -taint-modset-callsite-gated" ;;
      hoist0) echo "-ftaint-harden=$SEED -mllvm -taint-dit-loop-hoist=0" ;;
      swcyc30) echo "-ftaint-harden=$SEED -mllvm -taint-dit-loop-hoist=1 -mllvm -taint-dit-switch-cyc=30" ;;
      nopctl) echo "-ftaint-harden=$SEED -mllvm -taint-dit-loop-hoist=1 -mllvm -taint-modset-callsite-gated -mllvm -taint-dit-nop-switches" ;;
    esac
}

# ---- shared: Lua, the precomputed tables, the payload shim, the host ----
say "shared objects (identical in every arm)"
LUA_SRCS=$(ls "$LUA"/src/*.c | grep -vE '/(lua|luac)\.c$')
if [[ ! -f "$S/liblua.a" ]]; then
    rm -rf "$S/lua"; mkdir -p "$S/lua"
    for f in $LUA_SRCS; do
        cc -O2 -c -I"$LUA/src" "$f" -o "$S/lua/$(basename "${f%.c}").o" || exit 1
    done
    "$LLVM_BUILD/bin/llvm-ar" rcs "$S/liblua.a" "$S"/lua/*.o || exit 1
fi
[[ -f "$S/pre1.o" ]]    || cc -O2 -c "${SECP_DEFS[@]}" "$SECP/src/precomputed_ecmult.c"     -o "$S/pre1.o" || exit 1
[[ -f "$S/pre2.o" ]]    || cc -O2 -c "${SECP_DEFS[@]}" "$SECP/src/precomputed_ecmult_gen.c" -o "$S/pre2.o" || exit 1
[[ -f "$S/payload.o" ]] || cc -O2 -c -I"$SECP/include" -I"$HERE" -DUSE_M5 -DGEM5_NO_SELF_TIMING \
                              "$HERE/secret_payload.c" -o "$S/payload.o" || exit 1
[[ -f "$S/host.o" ]]    || cc -O2 -c -I"$LUA/src" -I"$HERE" -DUSE_M5 -DGEM5_NO_SELF_TIMING \
                              "$HERE/host_lua_secp.c" -o "$S/host.o" || exit 1

# ---- per-arm: only secp256k1.c ----
for arm in "${@:-plain nodit hoist gated hoist0 swcyc30 nopctl}"; do
    ad="$B/$arm"; mkdir -p "$ad"
    if [[ ! -f "$ad/secp256k1.o" ]]; then
        say "$arm: secp256k1.c"
        # shellcheck disable=SC2046
        cc -O2 -c "${SECP_DEFS[@]}" $(arm_flags "$arm") \
            "$SECP/src/secp256k1.c" -o "$ad/secp256k1.o" || exit 1
    fi
    cc -O2 "$S/host.o" "$S/payload.o" "$ad/secp256k1.o" "$S/pre1.o" "$S/pre2.o" \
        "$S/liblua.a" -lm5 -lm -o "$B/xover_$arm" || exit 1
    printf '  %-8s %s msr DIT\n' "$arm" \
        "$("$OBJDUMP" -d "$ad/secp256k1.o" | grep -ci 'msr.*dit' || true)"
done

say "built"
ls -la "$B"/xover_* 2>/dev/null | awk '{print $5, $9}'
