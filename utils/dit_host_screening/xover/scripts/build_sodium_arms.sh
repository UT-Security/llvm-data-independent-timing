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

    # WHICH STAGE GETS WHICH FLAG. -taint-dit-nop-switches is consumed by
    # AArch64AsmPrinter::emitInstruction, i.e. at EMISSION, so it belongs to the
    # SECOND llc. Given only to the analysis stage it is silently ignored and the
    # arm comes out byte-identical to its non-NOP twin - an inert control that
    # looks like a passing one, because "the NOP arm tracks the real build" is
    # exactly what identical binaries produce. The gate below catches it.
    local analysis=() emit=()
    local a
    for a in "$@"; do
        case "$a" in
            -taint-dit-nop-switches) emit+=("$a") ;;
            *)                       analysis+=("$a") ;;
        esac
    done

    say "$name: $*"
    "$L/llc" -enable-new-pm -run-taint-interproc -taint-insert-dit "${analysis[@]}" \
        -taint-dit-precision-report="$OUT/$name.prec.txt" \
        "$W/libsodium.pe.mir" -o "$OUT/$name.mir" || return 1
    "$L/llc" -start-after=prologepilog "${emit[@]}" "$OUT/$name.mir" -filetype=obj \
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

# CALIBRATION SWEEP. 30 is the best MEASURED point, not a derived optimum: the
# measured switch cost is 9.7-22.6 cyc against a dwell of 0.0039 cyc per
# suppressed op, so the true ratio is orders of magnitude above the 30:1 that
# switch-cyc=30 / dwell-per-instr=1 encodes. And since the win turned out to be
# fewer INSTRUCTIONS rather than cheaper mode switches, the right calibration may
# sit well above 30.
#
# The curve should TURN: merging keeps DIT on across the corridor, so as
# switch-cyc rises, switches fall (good) but dwell accumulates (bad) - blanket
# coverage costs +11-12% on this workload, which is what unbounded merging tends
# toward. Each def arm therefore gets a NOP twin: a NOP build has no dwell at all,
# so def-minus-nop AT EACH SETTING is the dwell term, and the setting where it
# starts to grow is where merging has gone too far.
build_arm def100  -taint-dit-switch-cyc=100
build_arm def300  -taint-dit-switch-cyc=300
build_arm nop100  -taint-dit-switch-cyc=100 -taint-dit-nop-switches
build_arm nop300  -taint-dit-switch-cyc=300 -taint-dit-nop-switches

# Historical arms, kept so the published numbers stay reproducible against the
# compiler they were taken with. THEY NO LONGER BUILD: -taint-modset-callsite-gated
# and -taint-dit-relaxed-ownership were removed on 2026-08-24, and the flags below
# that survive now mean something different because the defaults moved.
#   build_arm hoist   -taint-dit-placement=region -taint-dit-loop-hoist=1
#   build_arm gated   ... -taint-modset-callsite-gated      (== def0 today)
#   build_arm hoist0  -taint-dit-placement=region -taint-dit-loop-hoist=0
#   build_arm func    -taint-dit-placement=function


# GATE: a NOP arm must carry ZERO `msr DIT` and must still be the same size as
# its twin (that is the whole point - same layout, no mode switch). Both halves
# have to hold; either one alone can be satisfied by a broken build.
say "gate: NOP arms are actually NOPed"
gate_fail=0
for pair in "def0 nop0" "def30 nop30" "def100 nop100" "def300 nop300"; do
    d="${pair%% *}"; n="${pair##* }"
    [[ -f "$OUT/$d.o" && -f "$OUT/$n.o" ]] || continue
    dn=$("$L/llvm-objdump" -d "$OUT/$d.o" | grep -ci 'msr.*dit')
    nn=$("$L/llvm-objdump" -d "$OUT/$n.o" | grep -ci 'msr.*dit')
    ds=$(stat -c%s "$OUT/$d.o" 2>/dev/null || stat -f%z "$OUT/$d.o")
    ns=$(stat -c%s "$OUT/$n.o" 2>/dev/null || stat -f%z "$OUT/$n.o")
    if [[ "$nn" -ne 0 ]]; then
        printf '  FAIL %s: %s msr DIT survive (expected 0)\n' "$n" "$nn"; gate_fail=1
    elif [[ "$ds" -ne "$ns" ]]; then
        printf '  FAIL %s: size %s vs %s %s - layout not preserved\n' "$n" "$ns" "$d" "$ds"; gate_fail=1
    else
        printf '  ok   %s: %s msr DIT -> 0, size unchanged (%s)\n' "$n" "$dn" "$ns"
    fi
done
[[ "$gate_fail" -eq 0 ]] || { red_msg="NOP control is inert - do not measure with these arms"; echo "  $red_msg" >&2; exit 1; }

say "switch counts"
for a in nodit def0 def30 def100 def300 nop0 nop30 nop100 nop300; do
    [[ -f "$OUT/$a.o" ]] || continue
    n=$("$L/llvm-objdump" -d "$OUT/$a.o" | grep -ci 'msr.*dit')
    f=$(wc -l < "$OUT/$a.prec.txt" 2>/dev/null || echo 0)
    printf '  %-8s %5s msr DIT   %4s instrumented functions\n' "$a" "$n" "$f"
    "$L/llvm-ar" rcs "$OUT/libsodium-$a.a" "$OUT/$a.o"
done
ls -la "$OUT"/libsodium-*.a 2>/dev/null | awk '{print $5, $9}'
