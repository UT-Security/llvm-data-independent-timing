#!/bin/bash
# Build Lua 5.4.7 arms with the taint pass, for the INSTRUCTION-LEVEL regime.
#
# Everything measured so far has public and secret code ALTERNATING at call
# granularity - the public lane runs, then a crypto call runs. This is the other
# topology: the public code computes ON secret data, so the two interleave every
# few instructions and there is no call boundary to place a switch at.
#
# The seed is `lua_pushlstring,1,pointee`: the string handed to the interpreter is
# secret. Everything the VM subsequently does with those bytes - string objects,
# the value stack, arithmetic, table lookups, the dispatch loop's operands - is
# downstream of it.
#
# EXPECT A FLOOD, and that is the result rather than a failure. The project has
# seen this shape once already: on QuickJS an arbitrary taint source that flowed
# back through a return value produced 13,222 switches with 618 inside
# JS_CallInternal, against 6 when it did not. This build measures that regime
# deliberately instead of stumbling into it.
set -uo pipefail

L=${L:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)/build/bin}
[[ -x "$L/clang" ]] || { echo "no clang at $L/clang - build it (ninja -C <repo>/build clang) or set L" >&2; exit 1; }
W=${W:-$HOME/Documents/dit-crossover/build/luataint}
mkdir -p "$W"
say(){ printf '\n=== %s ===\n' "$*"; }

printf 'lua_pushlstring,1,pointee\n' > "$W/seed.txt"

if [[ ! -f "$W/lua-annotated.ll" ]]; then
    say "annotate"
    "$L/opt" -S "$W/lua-whole.bc" -passes=taint-annotate \
        -taint-src="$W/seed.txt" -o "$W/lua-annotated.ll" || exit 1
    printf '  %s tainted-pointee attrs\n' \
        "$(grep -c 'tainted-pointee' "$W/lua-annotated.ll" || true)"
fi

if [[ ! -f "$W/lua.pe.mir" ]]; then
    say "lower to post-prologepilog MIR"
    "$L/llc" -O2 -stop-after=prologepilog "$W/lua-annotated.ll" -o "$W/lua.pe.mir" || exit 1
    perl -0pi -e 's/<mcsymbol >//g' "$W/lua.pe.mir"
fi

# baseline: same MIR round trip, taint pass NOT run -> zero msr DIT
if [[ ! -f "$W/nodit.o" ]]; then
    say "nodit (round-trip control)"
    "$L/llc" -start-after=prologepilog "$W/lua.pe.mir" -filetype=obj -o "$W/nodit.o" || exit 1
fi

build_arm() {
    local name="$1"; shift
    [[ -f "$W/$name.o" ]] && { echo "  $name cached"; return; }
    say "$name: $*"
    "$L/llc" -enable-new-pm -run-taint-interproc -taint-insert-dit "$@" \
        -taint-dit-precision-report="$W/$name.prec.txt" \
        "$W/lua.pe.mir" -o "$W/$name.mir" || return 1
    "$L/llc" -start-after=prologepilog "$W/$name.mir" -filetype=obj -o "$W/$name.o" || return 1
    rm -f "$W/$name.mir"
}

build_arm hoist  -taint-dit-placement=region -taint-dit-loop-hoist=1
build_arm gated  -taint-dit-placement=region -taint-dit-loop-hoist=1 -taint-modset-callsite-gated
build_arm func   -taint-dit-placement=function

# alignment control: same placement as gated, switches emitted as HINT #0
if [[ ! -f "$W/nopctl.o" ]]; then
    say "nopctl (alignment control)"
    "$L/llc" -enable-new-pm -run-taint-interproc -taint-insert-dit \
        -taint-dit-placement=region -taint-dit-loop-hoist=1 -taint-modset-callsite-gated \
        "$W/lua.pe.mir" -o "$W/nopctl.mir" || exit 1
    "$L/llc" -start-after=prologepilog -taint-dit-nop-switches "$W/nopctl.mir" \
        -filetype=obj -o "$W/nopctl.o" || exit 1
    rm -f "$W/nopctl.mir"
fi

say "instrumentation census"
for a in nodit hoist gated func nopctl; do
    [[ -f "$W/$a.o" ]] || continue
    n=$("$L/llvm-objdump" -d "$W/$a.o" | grep -ci 'msr.*dit')
    f=$(wc -l < "$W/$a.prec.txt" 2>/dev/null | tr -d ' ' || echo 0)
    printf '  %-8s %6s msr DIT   %4s of 633 functions instrumented\n' "$a" "$n" "$f"
    "$L/llvm-ar" rcs "$W/liblua-$a.a" "$W/$a.o"
done

say "is the interpreter's own dispatch loop instrumented?"
for a in hoist gated func; do
    [[ -f "$W/$a.prec.txt" ]] || continue
    printf '  %-7s luaV_execute: %s\n' "$a" \
        "$(grep -c '^luaV_execute ' "$W/$a.prec.txt" || echo 0)"
done
