#!/bin/bash
# Build the libsodium composite hosts as STATIC aarch64 for gem5 SE mode.
#
# The native counterpart is build_sodium_hosts.sh, which targets the M5. This one
# exists to re-run Result 2 of docs/results/dit-switch-cyc-confirmation.md on the
# one instrument available when no FEAT_DIT silicon is to hand: the def-minus-NOP
# term that run reported was measured with inert NOP arms, and needs redoing.
#
# Only libsodium differs between arms. The hosts, the payload shim, SQLite and Lua
# are compiled ONCE and shared, so the public lane is bit-identical everywhere and
# any measured difference is attributable to the instrumented library alone.
set -uo pipefail

X=${X:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
W=${W:-$HOME/Documents/libsodium-wllvm-1.0.21}
SOD=${SOD:-$HOME/Documents/dit-crossover/build/sodium}
LUA=${LUA:-$HOME/Documents/lua-5.4.7}
SQLITE_C=${SQLITE_C:-$HOME/Documents/sqlite-amalgamation-3530400/sqlite3.c}
CC=${CC:-$(cd "$X/../../.." && pwd)/build/bin/clang}
[[ -x "$CC" ]] || { echo "no clang at $CC - build it (ninja -C <repo>/build clang) or set CC" >&2; exit 1; }
OUT=${OUT:-$HOME/Documents/dit-crossover/build/sodium_gem5}
FLAGS=${FLAGS:--static -march=armv8.4-a}

SODIUM_INC="-I$W/src/libsodium/include -I$W/src/libsodium/include/sodium"
SQL_DEFS="-DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_TEMP_STORE=2"

S=$OUT/_shared
mkdir -p "$S"
say(){ printf '\n=== %s ===\n' "$*"; }

say "shared objects (compiled once, shared by every arm)"
[[ -f "$S/sqlite3.o"    ]] || $CC $FLAGS -O2 -c $SQL_DEFS "$SQLITE_C" -o "$S/sqlite3.o" || exit 1
[[ -f "$S/payload.o"    ]] || $CC $FLAGS -O2 -c $SODIUM_INC -I"$X" "$X/sodium_payload.c" -o "$S/payload.o" || exit 1
[[ -f "$S/host_sqlite.o" ]] || $CC $FLAGS -O2 -c -I"$(dirname "$SQLITE_C")" -I"$X" "$X/host_sqlite_sodium.c" -o "$S/host_sqlite.o" || exit 1
[[ -f "$S/host_lua.o"   ]] || $CC $FLAGS -O2 -c -I"$LUA/src" -I"$X" "$X/host_lua_sodium.c" -o "$S/host_lua.o" || exit 1

say "linking arms"
ARMS=${ARMS:-nodit def0 def30 nop0 nop30 abi30 abinop}
for arm in $ARMS; do
    a="$SOD/libsodium-$arm.a"
    [[ -f "$a" ]] || { echo "  missing $a"; continue; }
    $CC $FLAGS -O2 "$S/host_sqlite.o" "$S/payload.o" "$S/sqlite3.o" "$a" \
        -o "$OUT/xsod_sqlite_$arm" -lm || exit 1
    $CC $FLAGS -O2 "$S/host_lua.o" "$S/payload.o" "$LUA/src/liblua.a" "$a" \
        -o "$OUT/xsod_lua_$arm" -lm || exit 1
    printf '  %-8s ok\n' "$arm"
done
cp -f "$X/work_sodium.lua" "$OUT/" 2>/dev/null
ls -la "$OUT"/xsod_* 2>/dev/null | awk '{print $5, $9}'
