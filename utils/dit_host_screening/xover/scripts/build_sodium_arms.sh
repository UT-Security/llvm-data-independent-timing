#!/bin/bash
# Build the libsodium arms for the public+secret composite.
#
# THROUGH THE CLANG DRIVER, since 2026-08-31. The pass used to be run by a
# two-stage `llc`: lower to post-prologepilog MIR, run the taint pass, then emit.
# That path can no longer represent how the pass ships. Two reasons, either one
# sufficient:
#
#   * `4fb7600db532` moved the pass into the codegen pipeline, so the MIR round
#     trip -- and the codegen perturbation that made `nodit` necessary as a
#     control -- is gone.
#   * The callee-saved DIT ABI reserves its carrier slot PRE-PEI, and
#     `setPrePrologEpilogCallback` is wired only when the post-PEI taint callback
#     is set, which only the driver does. Under `llc -run-taint-interproc` no slot
#     is ever reserved and `-taint-dit-abi` silently takes the no-slot fallback
#     (llvm/test/CodeGen/AArch64/taint-analysis-dit-abi-no-slot.mir). Arms built
#     the old way cannot exercise the ABI at all, and look like it worked.
#
# Input is the whole-library bitcode plus the seed, which is the user-facing
# contract: the driver does the annotation and the analysis.
set -uo pipefail

W=${W:-$HOME/Documents/libsodium-wllvm-1.0.21}
L=${L:-$HOME/Documents/llvm-data-independent-timing/build/bin}
OUT=${OUT:-$HOME/Documents/dit-crossover/build/sodium}
BC=${BC:-$W/libsodium-whole.bc}
SEED=${SEED:-$W/secret_m4_pointee.txt}
CFLAGS=${CFLAGS:--O2 -march=armv8.4-a}
mkdir -p "$OUT"

say(){ printf '\n=== %s ===\n' "$*"; }
[[ -f "$BC"   ]] || { echo "no bitcode at $BC (run taint_libsodium_eval.sh bitcode)"   >&2; exit 1; }
[[ -f "$SEED" ]] || { echo "no seed at $SEED (run taint_libsodium_eval.sh seed)"       >&2; exit 1; }

# THE BASELINE IS `nodit`: the same driver, the same pipeline, an EMPTY seed. No
# taint source means no analysis result and no switch, so anything the pass costs
# by merely being in the pipeline is charged to the baseline rather than to DIT.
EMPTY="$OUT/seed_empty.txt"
: > "$EMPTY"

build_arm() {
    local name="$1"; shift
    [[ -f "$OUT/$name.o" ]] && { echo "  $name already built"; return; }
    say "$name: $*"
    # ONE invocation: there is no stage for a flag to be dropped between. The
    # previous two-stage form silently ignored -taint-dit-nop-switches, which is
    # consumed at emission, and produced NOP arms byte-identical to their twins.
    "$L/clang" -x ir $CFLAGS -c "$BC" "$@" \
        -mllvm -taint-dit-precision-report="$OUT/$name.prec.txt" \
        -o "$OUT/$name.o" || return 1
}

build_arm nodit  -ftaint-harden="$EMPTY"
build_arm def30  -ftaint-harden="$SEED"
build_arm def0   -ftaint-harden="$SEED" -mllvm -taint-dit-switch-cyc=0
build_arm nop30  -ftaint-harden="$SEED" -mllvm -taint-dit-nop-switches
build_arm nop0   -ftaint-harden="$SEED" -mllvm -taint-dit-switch-cyc=0 -mllvm -taint-dit-nop-switches

# The callee-saved ABI (docs/design/dit-abi.md). Region placement, i.e. the
# default -- the ABI supports it; the flag's help text saying otherwise is stale.
build_arm abi30  -ftaint-harden="$SEED" -ftaint-dit-abi \
                 -mllvm -taint-nonlocal-report="$OUT/abi30.nonlocal.txt"
build_arm abinop -ftaint-harden="$SEED" -ftaint-dit-abi -mllvm -taint-dit-nop-switches

# The gate is on the IMMEDIATE form. -taint-dit-nop-switches substitutes what
# getTimingModeSwitch matches, which is `MSR DIT, #imm`; the ABI's UNCONDITIONAL
# exit is `MSR DIT, Xt`, the register form, and survives. That is a real hole in
# the control rather than a build error -- a def-minus-NOP term on an ABI arm
# UNDERSTATES by however many unconditional exits the build has -- so it is
# counted and reported, not failed on. The guarded exits carry no MSR at all when
# not taken, so they are not affected.
say "gate: NOP arms are actually NOPed"
gate_fail=0
for pair in "def0 nop0" "def30 nop30" "abi30 abinop"; do
    d="${pair%% *}"; n="${pair##* }"
    [[ -f "$OUT/$d.o" && -f "$OUT/$n.o" ]] || continue
    dn=$("$L/llvm-objdump" -d "$OUT/$d.o" | grep -ci 'msr.*dit')
    imm=$("$L/llvm-objdump" -d "$OUT/$n.o" | grep -cE 'msr[[:space:]]+DIT, #')
    reg=$("$L/llvm-objdump" -d "$OUT/$n.o" | grep -cE 'msr[[:space:]]+DIT, x')
    ds=$(stat -c%s "$OUT/$d.o"); ns=$(stat -c%s "$OUT/$n.o")
    if   [[ "$imm" -ne 0 ]]; then printf '  FAIL %s: %s immediate-form msr DIT survive\n' "$n" "$imm"; gate_fail=1
    elif [[ "$ds" -ne "$ns" ]]; then printf '  FAIL %s: size %s vs %s %s\n' "$n" "$ns" "$d" "$ds"; gate_fail=1
    else
        printf '  ok   %s: %s msr DIT -> 0 immediate, size unchanged (%s)\n' "$n" "$dn" "$ns"
        [[ "$reg" -gt 0 ]] && printf '       NOTE %s register-form (unconditional ABI exit) NOT substituted - def-minus-NOP understates here\n' "$reg"
    fi
done
[[ "$gate_fail" -eq 0 ]] || { echo "  NOP control is inert - do not measure with these arms" >&2; exit 1; }

say "switch counts"
for a in nodit def0 def30 nop0 nop30 abi30 abinop; do
    [[ -f "$OUT/$a.o" ]] || continue
    n=$("$L/llvm-objdump" -d "$OUT/$a.o" | grep -ci 'msr.*dit')
    c=$("$L/llvm-objdump" -d "$OUT/$a.o" | grep -ci 'mrs.*dit')
    g=$("$L/llvm-objdump" -d "$OUT/$a.o" | grep -c 'tbnz.*#0x18')
    f=$(wc -l < "$OUT/$a.prec.txt" 2>/dev/null || echo 0)
    printf '  %-7s %4s msr DIT  %3s carriers  %3s guarded  %4s instrumented fns\n' "$a" "$n" "$c" "$g" "$f"
    "$L/llvm-ar" rcs "$OUT/libsodium-$a.a" "$OUT/$a.o"
done
