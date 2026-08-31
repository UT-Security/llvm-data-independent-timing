#!/bin/bash
# Build the SQLite TCE composite - SQLite public lane, per-column AEAD on the
# write path (the pgsodium Transparent Column Encryption shape).
#
# THE PUBLIC LANE IS COMPILED ONCE. sqlite3.o, the host and the payload shim are
# built a single time and linked against every libsodium arm, so the public lane
# is bit-identical everywhere and any difference measured is attributable to the
# instrumented library alone.
#
# Two flavours:
#   native  -DXOVER_NO_DIT, host toolchain. Validates the harness on a pre-8.4
#           machine. NOT for measurement.
#   gem5    static aarch64, -march=armv8.4-a, real `msr DIT`. What gets measured.
set -uo pipefail

X=${X:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
SQLITE_C=${SQLITE_C:-$HOME/Documents/sqlite-amalgamation-3530400/sqlite3.c}
SODIUM=${SODIUM:-$HOME/Documents/libsodium-static}
OUT=${OUT:-$HOME/Documents/dit-crossover/build/tce}
CC_NATIVE=${CC_NATIVE:-clang}
# On an aarch64 Linux host the fork's clang emits static ELF directly - no
# cross sysroot needed (util/cross/taint-cross-cc exists for the macOS box,
# where everything native is Mach-O). armv8.4-a is required for FEAT_DIT.
CC_GEM5=${CC_GEM5:-$HOME/Documents/llvm-data-independent-timing/build/bin/clang}
GEM5_FLAGS=${GEM5_FLAGS:--static -march=armv8.4-a}

SQL_DEFS="-DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_TEMP_STORE=2"
SODIUM_INC="-I$SODIUM/include"

mkdir -p "$OUT/native" "$OUT/gem5"
say(){ printf '\n=== %s ===\n' "$*"; }

say "native validation build (no DIT instructions)"
N=$OUT/native
[[ -f "$N/sqlite3.o" ]] || $CC_NATIVE -O2 -c $SQL_DEFS "$SQLITE_C" -o "$N/sqlite3.o" || exit 1
$CC_NATIVE -O2 -DXOVER_NO_DIT -c $SODIUM_INC -I"$X" "$X/tce_payload.c"     -o "$N/tce_payload.o" || exit 1
$CC_NATIVE -O2 -c -I"$(dirname "$SQLITE_C")" -I"$X" "$X/host_sqlite_tce.c" -o "$N/host.o"       || exit 1
$CC_NATIVE -O2 "$N/host.o" "$N/tce_payload.o" "$N/sqlite3.o" "$SODIUM/lib/libsodium.a" \
    -o "$N/xtce_native" -lm || exit 1
echo "  $N/xtce_native"

say "gem5 build (static aarch64, real msr DIT)"
G=$OUT/gem5
[[ -x "$CC_GEM5" ]] || { echo "  no cross cc at $CC_GEM5 - skipping"; exit 0; }
[[ -f "$G/sqlite3.o" ]] || $CC_GEM5 $GEM5_FLAGS -O2 -c $SQL_DEFS "$SQLITE_C" -o "$G/sqlite3.o" || exit 1
$CC_GEM5 $GEM5_FLAGS -O2 -c $SODIUM_INC -I"$X" "$X/tce_payload.c"     -o "$G/tce_payload.o" || exit 1
$CC_GEM5 $GEM5_FLAGS -O2 -c -I"$(dirname "$SQLITE_C")" -I"$X" "$X/host_sqlite_tce.c" -o "$G/host.o" || exit 1

# One binary per libsodium arm; off/always/field/row are argv modes of each.
ARMS=${ARMS:-plain}
for arm in $ARMS; do
    a="$SODIUM/lib/libsodium.a"
    [[ "$arm" == plain ]] || a="$OUT/../sodium/libsodium-$arm.a"
    [[ -f "$a" ]] || { echo "  missing $a"; continue; }
    $CC_GEM5 $GEM5_FLAGS -O2 "$G/host.o" "$G/tce_payload.o" "$G/sqlite3.o" "$a" \
        -o "$G/xtce_$arm" -lm || exit 1
    printf '  %-8s ok\n' "$arm"
done
ls -la "$G"/xtce_* 2>/dev/null | awk '{print $5, $9}'
