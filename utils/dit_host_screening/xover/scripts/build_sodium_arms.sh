#!/bin/bash
# Build the libsodium arms for the public+secret composite.
#
# Starts from libsodium-annotated.ll, which utils/taint_libsodium_eval.sh produced
# from a WLLVM whole-library bitcode link (926 functions) with the CIO-parity seed
# applied (48 pointee + 17 data attrs across 21 functions).
#
# THE BASELINE IS `nodit`: the same MIR round trip with the taint pass NOT run, so
# it carries the pipeline's own codegen perturbation and zero `msr DIT`. Comparing
# any arm against the stock build instead would charge that perturbation to DIT.
set -uo pipefail

W=${W:-$HOME/Documents/libsodium-1.0.21}
L=${L:-$HOME/Documents/llvm-project/build-gfix/bin}
OUT=${OUT:-$HOME/Documents/dit-crossover/build/sodium}
mkdir -p "$OUT"

say(){ printf '\n=== %s ===\n' "$*"; }

if [[ ! -f "$W/libsodium.pe.mir" ]]; then
    say "lower to post-prologepilog MIR"
    "$L/llc" -O2 -stop-after=prologepilog "$W/libsodium-annotated.ll" \
        -o "$W/libsodium.pe.mir" || exit 1
    perl -0pi -e 's/<mcsymbol >//g' "$W/libsodium.pe.mir"   # MIR CFI serialization bug
fi

# baseline: no taint pass at all, but the SAME round trip
if [[ ! -f "$OUT/nodit.o" ]]; then
    say "nodit (round-trip control)"
    "$L/llc" -start-after=prologepilog "$W/libsodium.pe.mir" -filetype=obj \
        -o "$OUT/nodit.o" || exit 1
fi

build_arm() {
    local name="$1"; shift
    [[ -f "$OUT/$name.o" ]] && { echo "  $name already built"; return; }
    say "$name: $*"
    "$L/llc" -enable-new-pm -run-taint-interproc -taint-insert-dit "$@" \
        -taint-dit-precision-report="$OUT/$name.prec.txt" \
        "$W/libsodium.pe.mir" -o "$OUT/$name.mir" || return 1
    "$L/llc" -start-after=prologepilog "$OUT/$name.mir" -filetype=obj \
        -o "$OUT/$name.o" || return 1
    rm -f "$OUT/$name.mir"
}

# THE CONFIRMATION-RUN ARMS (2026-08-24). The compiler defaults changed - region
# placement, loop-hoist=1, switch-cyc=30, and the call-site mod-set gate all ship
# ON - so `def30` is simply the shipped default and the ONLY knob varied against it
# is -taint-dit-switch-cyc. That is the comparison that was never made as one arm:
# every previous swcyc30 measurement was gate-OFF and every gated measurement was
# switch-cyc=0.
#
# nop0/nop30 are the alignment control, not a knob: every inserted `msr DIT` is
# emitted as `HINT #0` at the same address, so nop30-vs-nop0 is the pure LAYOUT
# delta and def30-vs-def0 minus it is the switch delta. Required here because the
# whole claim is about switch COUNT. NB the NOP is not perfectly neutral - it is
# ~0.25% slower than a real op at the same address - so it UNDERSTATES switch cost.
build_arm def30
build_arm def0    -taint-dit-switch-cyc=0
build_arm nop30   -taint-dit-nop-switches
build_arm nop0    -taint-dit-switch-cyc=0 -taint-dit-nop-switches

# Historical arms, kept so the published numbers stay reproducible against the
# compiler they were taken with. THEY NO LONGER BUILD: -taint-modset-callsite-gated
# and -taint-dit-relaxed-ownership were removed on 2026-08-24, and the flags below
# that survive now mean something different because the defaults moved.
#   build_arm hoist   -taint-dit-placement=region -taint-dit-loop-hoist=1
#   build_arm gated   ... -taint-modset-callsite-gated      (== def0 today)
#   build_arm hoist0  -taint-dit-placement=region -taint-dit-loop-hoist=0
#   build_arm func    -taint-dit-placement=function

say "switch counts"
for a in nodit def30 def0 nop30 nop0; do
    [[ -f "$OUT/$a.o" ]] || continue
    n=$("$L/llvm-objdump" -d "$OUT/$a.o" | grep -ci 'msr.*dit')
    f=$(wc -l < "$OUT/$a.prec.txt" 2>/dev/null || echo 0)
    printf '  %-8s %5s msr DIT   %4s instrumented functions\n' "$a" "$n" "$f"
    "$L/llvm-ar" rcs "$OUT/libsodium-$a.a" "$OUT/$a.o"
done
ls -la "$OUT"/libsodium-*.a 2>/dev/null | awk '{print $5, $9}'
