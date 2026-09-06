#!/usr/bin/env bash
#
# Whole-library arms for the CIO-parity switch-model study.
#
# WHY THIS EXISTS ALONGSIDE build_arms.sh. That script hardens per translation
# unit with clang's -ftaint-harden, which is the shipped user-facing flag and is
# what experiment 09's gem5 ORACLE arms used (0 / 134 / 137 msr DIT). Measured
# under gem5, it commits about 3 DIT writes per ed25519 signature -- because the
# analysis cannot follow taint across a TU boundary, so it leaves the mode set
# and the callee inherits it. 46 static switches in the linked binary, almost
# none of which execute.
#
# That is useless for a switch-model experiment. At ~3 writes per 78,413 cycles
# the committed toggle rate is ~38 per million cycles, and experiment 06's
# sensitivity curve puts the serialising penalty at +0.66 points already at 86
# per million. There is nothing to serialise, so both switch models would read
# the same and the run would answer the question with a null it cannot
# distinguish from a real one.
#
# Experiment 09's TIMING arms took a different path: whole-library bitcode,
# linked with llvm-link, annotated once, lowered to post-prologepilog MIR, and
# the interprocedural pass run over the entire library at once. That yields 521
# switches and 109 EXECUTED per signature -- a committed toggle rate near 1,400
# per million cycles, squarely in the regime where the two switch models diverge.
# This script reproduces that path, so the arms here are comparable both to the
# silicon table and to each other.
#
# THE BASELINE IS THE MIR ROUND TRIP, NOT THE STOCK BUILD. Arm `base` is the same
# .pe.mir with the pass not run, so it controls for the 3-phase lowering by
# construction and no separate `rt` arm is needed -- the native rig's A arm is
# exactly this, and its README says so.
#
# USAGE  ./build_arms_wl.sh [mir] [arms] [link]
set -uo pipefail

R="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$R/../../.." && pwd)"
LLVM="${LLVM:-$HOME/Documents/llvm-data-independent-timing/build}"
G5="${G5:-$REPO/gem5-DIT}"
BC="${BC:-$HOME/Documents/libsodium-wllvm-1.0.21}"
WORK="${WORK:-$HOME/Documents/libsodium-cioparity-wl}"
CIO="${CIO:?set CIO to a counter-optimization/cio checkout}"
SEEDS="${SEEDS:-$G5/benchmarks/crypto/libsodium_secret.txt}"
INC="${INC:-$BC/src/libsodium/include}"
MARCH="${MARCH:-armv8.4-a}"
LB="$LLVM/bin"

# libm5.a supplies the ROI markers. It is the m5op ABI and is independent of any
# gem5 source change, so a patched WORKTREE will not have built one - and a
# missing -lm5 is a link error 40 lines into the arm build rather than a clear
# message here. Look in $G5 first, then fall back to a sibling gem5 checkout that
# has one, and say which was used.
M5LIB="${M5LIB:-}"
if [[ -z "$M5LIB" ]]; then
  for c in "$G5" "$REPO/gem5-DIT" "$G5/.."/gem5-DIT*; do
    [[ -f "$c/util/m5/build/arm64/out/libm5.a" ]] && { M5LIB="$c/util/m5/build/arm64/out"; break; }
  done
fi
[[ -n "$M5LIB" && -f "$M5LIB/libm5.a" ]] || die \
  "no libm5.a found. Build it once with: cd \$G5/util/m5 && scons build/arm64/out/libm5.a
   (it is ABI-only, so any gem5 checkout's copy works; set M5LIB to point at one)"
# Flags added to EVERY object-emission llc, in every arm including base, so any
# alignment policy applies identically and cannot itself become a confound.
# ALIGN=4 -> -align-all-nofallthru-blocks=4 (16B); 6 -> 64B. Empty = off.
ALIGN="${ALIGN:-}"
EMIT_FLAGS=()
[[ -n "$ALIGN" ]] && EMIT_FLAGS=(-align-all-nofallthru-blocks="$ALIGN")

BENCHES="${BENCHES:-ed25519 chacha20_poly1305_encrypt chacha20_poly1305_decrypt aesni256gcm_encrypt aesni256gcm_decrypt argon2id}"
ARMS="${ARMS:-base blanket nop taint taintnop taintfn taintfnnop fine finenop}"

info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[33m    %s\033[0m\n' "$*"; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
STAGES="${*:-mir arms link}"
want() { [[ " $STAGES " == *" $1 "* ]]; }

for t in opt llc llvm-ar llvm-objdump clang; do
  [[ -x "$LB/$t" ]] || die "missing $LB/$t"
done
[[ -f "$BC/libsodium-whole.bc" ]] || die "no whole-library bitcode at $BC/libsodium-whole.bc"
[[ -f "$SEEDS" ]] || die "no seed file at $SEEDS"
mkdir -p "$WORK/bin"

# policy label -> taint-insert-dit flags
policy_flags() {
  case "$1" in
    taint)   echo "-taint-dit-placement=region" ;;
    taintfn) echo "-taint-dit-placement=function" ;;
    fine)    echo "-taint-dit-placement=region -taint-dit-switch-cyc=0 -taint-dit-loop-hoist=0" ;;
    nop)     echo "-taint-dit-placement=region -taint-dit-nop-switches" ;;
    *) die "no policy for $1" ;;
  esac
}

# ------------------------------------------------------------------ mir
if want mir; then
  info "annotate whole-library bitcode with the CIO-parity seeds"
  "$LB/opt" -S "$BC/libsodium-whole.bc" -passes=taint-annotate \
      -taint-src="$SEEDS" -o "$WORK/annotated.ll" || die "taint-annotate failed"
  nf=$(grep -E '^define' "$WORK/annotated.ll" | grep -cE '"tainted')
  [[ "$nf" -gt 0 ]] || die "no attributes applied -- seeding is broken"
  info "    $nf functions carry taint attributes"

  # -disable-tail-calls is REQUIRED and goes HERE, upstream of every arm, so all
  # of them share one codegen configuration. A tail call has no epilogue: taken
  # with DIT on, the mode is never restored and the selective arm becomes blanket
  # plus switches. This library had 13 such sites before the fix, crypto_sign
  # among them. Survivors are audited below, not assumed away.
  info "lower to post-prologepilog MIR (tail calls disabled)"
  "$LB/llc" -O2 -disable-tail-calls -stop-after=prologepilog \
      "$WORK/annotated.ll" -o "$WORK/pe.mir" || die "llc -stop-after=prologepilog failed"
  perl -0pi -e 's/<mcsymbol >//g' "$WORK/pe.mir"   # MIR CFI serialization bug
  info "    $(grep -c 'TCRETURN' "$WORK/pe.mir") TCRETURN survivors, $(du -h "$WORK/pe.mir" | cut -f1)"
fi

# ------------------------------------------------------------------ arms
if want arms; then
  # base IS the MIR round-trip control: same .pe.mir, pass never run.
  info "arm 'base' (MIR round-trip control, 0 switches)"
  "$LB/llc" -start-after=prologepilog "${EMIT_FLAGS[@]}" "$WORK/pe.mir" -filetype=obj \
      -o "$WORK/base.o" || die "baseline object failed"
  "$LB/llvm-ar" rcs "$WORK/libsodium-base.a" "$WORK/base.o"

  for p in taint taintfn fine; do
    info "arm '$p'  $(policy_flags "$p")"
    # shellcheck disable=SC2086
    "$LB/llc" -enable-new-pm -run-taint-interproc -taint-insert-dit $(policy_flags "$p") \
        $( [[ "$p" == taint ]] && printf '%s ' \
             "-taint-uncovered-report=$WORK/uncovered.txt" \
             "-taint-info-loss-report=$WORK/infoloss.txt" \
             "-taint-callsite-report=$WORK/callsites.txt" ) \
        "$WORK/pe.mir" -o "$WORK/$p.mir" || die "taint analysis failed for $p"
    "$LB/llc" -start-after=prologepilog "${EMIT_FLAGS[@]}" "$WORK/$p.mir" -filetype=obj \
        -o "$WORK/$p.o" || die "object failed for $p"
    "$LB/llvm-ar" rcs "$WORK/libsodium-$p.a" "$WORK/$p.o"
  done

  # ONE LAYOUT CONTROL PER PLACEMENT POLICY, each emitted from its OWN .mir.
  #
  # Two things matter here.
  #
  # First, -taint-dit-nop-switches is read by AArch64AsmPrinter, i.e. at
  # EMISSION. Passing it to the -run-taint-interproc invocation instead does
  # nothing at all and the arm comes out byte-identical to its non-NOP twin.
  # That is how Result 2 of docs/results/dit-switch-cyc-confirmation.md was
  # retracted on 2026-08-30, and this rig reproduced the same mistake on its
  # first run - caught only because the switch counts are printed per arm.
  # Keep them printed.
  #
  # Second, and the reason there is now one per policy: a single `nop` arm
  # emitted from taint.mir controls for TAINT's layout only. fine and taintfn
  # have their own placement and therefore their own code layout, and with no
  # control for it a cross-policy comparison silently conflates placement with
  # the per-binary codegen lottery. Measured on aes256gcm_decrypt: with every
  # DIT-gated optimisation disabled, so the mode can suppress nothing, `fine`
  # still ran 6.69% faster than `base` and `taint` - and commit.ditCycles then
  # showed taint and fine dwelling within 0.7 points of each other (99.2% vs
  # 98.5%). The apparent win was layout, and `fine` is in fact strictly worse
  # placement: more executed switches at the same dwell. Without <policy>nop
  # that inverts the conclusion.
  #
  # Emitting from the policy's own .mir rather than re-running the analysis
  # makes identical placement a property of the construction: same regions, same
  # instruction count, same addresses, switches replaced in place.
  for p in taint taintfn fine; do
    info "arm '${p}nop' (${p}.mir re-emitted, MSR DIT -> HINT #0)"
    "$LB/llc" -start-after=prologepilog -taint-dit-nop-switches "${EMIT_FLAGS[@]}" \
        "$WORK/$p.mir" -filetype=obj -o "$WORK/${p}nop.o" || die "${p}nop object failed"
    "$LB/llvm-ar" rcs "$WORK/libsodium-${p}nop.a" "$WORK/${p}nop.o"
    nd=$("$LB/llvm-objdump" -d "$WORK/libsodium-${p}nop.a" | grep -icE '\bmsr\b[[:space:]]+dit,')
    [[ "$nd" -eq 0 ]] || die "${p}nop still carries $nd msr DIT -- substitution did not happen"
    nn=$("$LB/llvm-objdump" -d "$WORK/libsodium-${p}nop.a" | grep -cE '^[[:space:]]+[0-9a-f]+:.*\bnop\b')
    nt=$("$LB/llvm-objdump" -d "$WORK/libsodium-$p.a" | grep -icE '\bmsr\b[[:space:]]+dit,')
    info "    $nt msr DIT -> $nn nop, 0 remaining"
    # Object sizes must match EXACTLY: HINT #0 is 4 bytes like MSR DIT. A
    # mismatch means the pair differs by more than whether the switch executes,
    # and (policy - policynop) stops being a pure switch term.
    st=$(stat -c %s "$WORK/$p.o"); sn=$(stat -c %s "$WORK/${p}nop.o")
    [[ "$st" -eq "$sn" ]] || die "$p.o ($st) and ${p}nop.o ($sn) differ in size"
  done
  # `nop` kept as an alias for taintnop: existing recorded data uses that name.
  cp -f "$WORK/libsodium-taintnop.a" "$WORK/libsodium-nop.a"
  cp -f "$WORK/taintnop.o" "$WORK/nop.o"
  

  printf '\n  %-10s %10s %10s %12s\n' arm 'msr DIT' 'nop' bytes
  for a in base nop taint taintfn fine; do
    lib="$WORK/libsodium-$a.a"
    printf '  %-10s %10s %10s %12s\n' "$a" \
      "$("$LB/llvm-objdump" -d "$lib" 2>/dev/null | grep -icE '\bmsr\b[[:space:]]+dit,')" \
      "$("$LB/llvm-objdump" -d "$lib" 2>/dev/null | grep -cE '^\s+[0-9a-f]+:.*\bnop\b')" \
      "$(stat -c %s "$lib")"
  done
  echo
  info "tail-call audit (required: no DIT-carrying function may tail-call out)"
  python3 "$R/audit_tailcalls.py" "$WORK/libsodium-taint.a" "$WORK/libsodium-taintfn.a" \
      "$WORK/libsodium-fine.a" "$WORK/libsodium-nop.a" || die "tail-call audit FAILED"
fi

# ------------------------------------------------------------------ link
if want link; then
  info "stage CIO drivers (byte-identical copies; only eval_util.h is ours)"
  STAGE="$WORK/src"; mkdir -p "$STAGE"
  cp -f "$R/eval_util.h" "$STAGE/eval_util.h"
  : > "$WORK/driver_sha256.txt"
  for b in $BENCHES; do
    [[ -f "$CIO/eval_$b.c" ]] || continue
    cp -f "$CIO/eval_$b.c" "$STAGE/eval_$b.c"
    ( cd "$CIO" && sha256sum "eval_$b.c" ) >> "$WORK/driver_sha256.txt"
  done
  ( cd "$STAGE" && sha256sum -c "$WORK/driver_sha256.txt" >/dev/null 2>&1 ) \
    && info "    drivers byte-identical to the CIO checkout" \
    || die "staged driver differs from CIO source"

  info "link"
  for b in $BENCHES; do
    src="$STAGE/eval_$b.c"; [[ -f "$src" ]] || { warn "skip $b"; continue; }
    for arm in $ARMS; do
      # No -DDIT_READBACK on timing builds: a `mrs DIT` anywhere in the binary
      # decodes differently under the two switch models and shifts the measured
      # region. See blanket_ctor.c. The `gate` stage builds readback versions.
      case "$arm" in blanket) v=base; extra="-DBLANKET_DIT" ;; *) v="$arm"; extra="" ;; esac
      lib="$WORK/libsodium-$v.a"
      [[ -f "$lib" ]] || { warn "skip $b/$arm -- no $lib"; continue; }
      # Compiled from INSIDE the staging dir with a bare relative file name. CIO's
      # drivers use assert(), which embeds __FILE__; given an absolute path that
      # string is as long as $WORK, so two builds in differently named work dirs
      # differ in .rodata length and every address after it. Measured: an 8-char
      # longer WORK moved results 0.2-2.6% and flipped a gate. A bare name makes
      # __FILE__ "eval_ed25519.c" in every build, wherever WORK is.
      ( cd "$STAGE" && "$LB/clang" -march="$MARCH" -O2 -std=gnu18 -static -fomit-frame-pointer \
          -DNO_DYN_HIT_COUNTS $extra \
          -I"$G5/include" -I"$INC" \
          -o "$WORK/bin/eval_${b}.${arm}" "eval_$b.c" "$R/blanket_ctor.c" \
          "$lib" -L"$M5LIB" -lm5 -lm \
        >"$WORK/bin/.link_${b}_${arm}.log" 2>&1 ) \
        || { warn "link failed $b/$arm"; tail -8 "$WORK/bin/.link_${b}_${arm}.log" >&2; continue; }
    done
    printf '    %-34s %s arms\n' "$b" "$(ls "$WORK/bin/eval_${b}."* 2>/dev/null | wc -l)"
  done
fi
info "done: $STAGES"

# ------------------------------------------------------------------ gate
# Mode-at-exit check, on SEPARATE binaries. Never used for timing.
if want gate; then
  info "build readback binaries and check PSTATE.DIT at exit"
  b="${GATE_BENCH:-ed25519}"
  for arm in $ARMS; do
    case "$arm" in blanket) v=base; extra="-DBLANKET_DIT" ;; *) v="$arm"; extra="" ;; esac
    lib="$WORK/libsodium-$v.a"; [[ -f "$lib" ]] || continue
    "$LB/clang" -march="$MARCH" -O2 -std=gnu18 -static -fomit-frame-pointer \
        -DNO_DYN_HIT_COUNTS -DDIT_READBACK $extra \
        -I"$G5/include" -I"$INC" \
        -o "$WORK/bin/gate_${b}.${arm}" "$WORK/src/eval_$b.c" "$R/blanket_ctor.c" \
        "$lib" -L"$M5LIB" -lm5 -lm >/dev/null 2>&1 \
      || { warn "gate build failed $arm"; continue; }
  done
  fail=0
  for arm in $ARMS; do
    g="$WORK/bin/gate_${b}.${arm}"; [[ -x "$g" ]] || continue
    d="$WORK/gate_$arm"; mkdir -p "$d"
    "$G5/build/ARM/gem5.opt" --outdir="$d" \
        "$G5/configs/example/arm/fdp_neoverse_v2_binary.py" \
        --binary "$g" --arguments "2 1 abc cc.txt" --eves --dmp --comp-simp \
        > "$d/run.log" 2>&1 || true
    got=$(sed -n 's/.*CIOGEM5 exit dit=\([01]\).*/\1/p' "$d/run.log" | tail -1)
    case "$arm" in blanket) want_v=1 ;; *) want_v=0 ;; esac
    if [[ "$got" == "$want_v" ]]; then printf '    %-10s exit dit=%s  ok\n' "$arm" "$got"
    else printf '\033[31m    %-10s exit dit=%s expected %s  FAIL\033[0m\n' "$arm" "${got:-?}" "$want_v"; fail=1; fi
  done
  [[ "$fail" -eq 0 ]] || die "mode-at-exit gate failed"
fi
