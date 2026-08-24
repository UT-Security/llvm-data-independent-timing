#!/bin/bash
# Build the libsodium composite hosts: SQLite lane and Lua lane, six arms each.
#
# Only libsodium differs between arms. The host, the payload shim, SQLite and Lua
# are compiled ONCE and shared, so the public lane is bit-identical everywhere and
# any measured difference is attributable to the instrumented library alone.
set -uo pipefail

X=${X:-$HOME/.treehouse/llvm-project-18cdea/2/llvm-project/utils/dit_host_screening/xover}
W=${W:-$HOME/Documents/libsodium-1.0.21}
OUT=${OUT:-$HOME/Documents/dit-crossover}
SOD=$OUT/build/sodium
LUA=${LUA:-$HOME/Documents/lua-5.4.7}
SQLITE_C=${SQLITE_C:-$HOME/Documents/sqlcipher-4.6.1/sqlite3.c}
CC=${CC:-$HOME/Documents/llvm-project/build-gfix/bin/clang}

B=$OUT/build/sodium_native
S=$B/_shared
mkdir -p "$S"

SODIUM_INC="-I$W/src/libsodium/include -I$W/src/libsodium/include/sodium"
SQL_DEFS="-DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_TEMP_STORE=2"

say(){ printf '\n=== %s ===\n' "$*"; }

say "shared objects"
[[ -f "$S/sqlite3.o" ]] || $CC -O2 -c $SQL_DEFS "$SQLITE_C" -o "$S/sqlite3.o" || exit 1
[[ -f "$S/payload.o" ]] || $CC -O2 -c $SODIUM_INC -I"$X" "$X/sodium_payload.c" -o "$S/payload.o" || exit 1
[[ -f "$S/host_sqlite.o" ]] || $CC -O2 -c -I"$(dirname "$SQLITE_C")" -I"$X" "$X/host_sqlite_sodium.c" -o "$S/host_sqlite.o" || exit 1
[[ -f "$S/host_lua.o" ]] || $CC -O2 -c -I"$LUA/src" -I"$X" "$X/host_lua_sodium.c" -o "$S/host_lua.o" || exit 1

say "linking arms"
for arm in nodit def30 def0 nop30 nop0; do
    a="$SOD/libsodium-$arm.a"
    [[ -f "$a" ]] || { echo "  missing $a"; continue; }
    $CC -O2 "$S/host_sqlite.o" "$S/payload.o" "$S/sqlite3.o" "$a" \
        -o "$B/xsod_sqlite_$arm" || exit 1
    $CC -O2 "$S/host_lua.o" "$S/payload.o" "$LUA/src/liblua.a" "$a" \
        -o "$B/xsod_lua_$arm" -lm || exit 1
    printf '  %-8s ok\n' "$arm"
done

cp -f "$X/work_sodium.lua" "$B/" 2>/dev/null
say "built"
ls -la "$B"/xsod_* 2>/dev/null | awk '{print $5, $9}'
