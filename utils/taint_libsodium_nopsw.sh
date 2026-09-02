#!/usr/bin/env bash
#
# Build the 'nopsw' arm (Z): the shipped placement with every MSR DIT emitted as
# a NOP instead. The measurement control for arm P.
#
# WHY IT BEATS 'baseline' AS A CONTROL. Arm A differs from P by 521 instructions
# and therefore by every address after the first switch, so A-vs-P confounds
# three things: the mode switching, the extra instructions, and the code
# movement. Z differs from P in NONE of those -- same instruction count, same
# addresses, byte-identical but for 521 opcodes -- so P-vs-Z is the switch
# EXECUTION cost alone, and Z-vs-A is the layout cost alone.
#
# Measured on M4 2026-09-02, chacha20-poly1305 encrypt, CNTVCT timing:
#   baseline  260.6 ns          nopsw  262.3 ns  (+0.65%)   pass  891.7 ns (+242%)
# So the layout term is 1.7 ns and essentially all of the cost is the switches.
# Worth having said with a control rather than assumed.
#
# THE FLAG GOES ON THE OBJECT-EMISSION llc, NOT THE ANALYSIS llc.
# -taint-dit-nop-switches is implemented in AArch64AsmPrinter::emitInstruction,
# deliberately: the comment there records that substituting at INSERTION time
# perturbed region placement, the whole-function fallback (which finds switches
# by opcode) and the coverage verifier, and produced a 400-instruction drift.
# Passing it to `llc -run-taint-interproc` is therefore a silent no-op -- the
# MIR and the object come out byte-identical to the hardened arm, with no
# warning. That mistake was made while writing this script.
#
# It also reuses libsodium.hardened.mir rather than re-running the analysis, so
# arm Z provably carries the same placement decisions as arm P.
#
# USAGE   utils/taint_libsodium_nopsw.sh
# ENV     WORK, LLVM_BIN   (same defaults as taint_libsodium_eval.sh)
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SODIUM_VER="${SODIUM_VER:-1.0.21}"
LLVM_BIN="${LLVM_BIN:-$REPO_ROOT/build/bin}"
WORK="${WORK:-$HOME/Documents/libsodium-$SODIUM_VER}"

info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

[[ -f "$WORK/libsodium.hardened.mir" ]] \
  || die "no $WORK/libsodium.hardened.mir -- run taint_libsodium_eval.sh analyze first"

info "emit hardened MIR with switches substituted (asm-printer stage)"
"$LLVM_BIN/llc" -start-after=prologepilog -taint-dit-nop-switches \
    "$WORK/libsodium.hardened.mir" -filetype=obj -o "$WORK/libsodium.nopsw.o" \
  || die "nopsw object failed"
"$LLVM_BIN/llvm-ar" rcs "$WORK/libsodium-nopsw.a" "$WORK/libsodium.nopsw.o"

# ------------------------------------------------- validate the control
sw=$("$LLVM_BIN/llvm-objdump" -d "$WORK/libsodium-nopsw.a" | grep -cE 'msr[[:space:]]+DIT')
nop=$("$LLVM_BIN/llvm-objdump" -d "$WORK/libsodium-nopsw.a" | grep -cE '\bnop\b')
psw=$("$LLVM_BIN/llvm-objdump" -d "$WORK/libsodium-hardened.a" | grep -cE 'msr[[:space:]]+DIT')
tz=$("$LLVM_BIN/llvm-size" -m "$WORK/libsodium.nopsw.o"    | sed -n 's/.*__text): *\([0-9]*\).*/\1/p' | head -1)
tp=$("$LLVM_BIN/llvm-size" -m "$WORK/libsodium.hardened.o" | sed -n 's/.*__text): *\([0-9]*\).*/\1/p' | head -1)
# Addresses only: the object NAME appears in the objdump header and always differs.
addr_same=no
diff <("$LLVM_BIN/llvm-objdump" -d "$WORK/libsodium-hardened.a" | grep -oE '^ +[0-9a-f]+:') \
     <("$LLVM_BIN/llvm-objdump" -d "$WORK/libsodium-nopsw.a"    | grep -oE '^ +[0-9a-f]+:') \
     >/dev/null 2>&1 && addr_same=yes

printf '\n  %-26s %10s %10s\n' '' 'arm P' 'arm Z'
printf '  %-26s %10s %10s\n' 'msr DIT'      "$psw" "$sw"
printf '  %-26s %10s %10s\n' 'nop'          '0'    "$nop"
printf '  %-26s %10s %10s\n' '__text bytes' "$tp"  "$tz"
printf '  %-26s %10s\n'      'addresses identical' "$addr_same"
if [[ "$sw" -eq 0 && "$nop" -eq "$psw" && "$tz" == "$tp" && "$addr_same" == yes ]]; then
  printf '\033[32m  VALID CONTROL: %s switches substituted, layout preserved\033[0m\n' "$psw"
else
  printf '\033[31m  NOT A VALID CONTROL -- do not use arm Z.\033[0m\n'
  printf '\033[31m  Most likely the flag reached the wrong llc; see the header.\033[0m\n'
  exit 1
fi
echo "  archive: $WORK/libsodium-nopsw.a"
echo
echo "  Add to the rooted run with:"
echo "    ARMS=\"A:baseline:0 C:baseline:1 P:hardened:0 F:func:0 X:fine:0 N:narrow:0 Z:nopsw:0\""
