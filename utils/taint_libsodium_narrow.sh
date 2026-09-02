#!/usr/bin/env bash
#
# Build the 'narrow' arm (N): the shipped region placement on indirect-call-RESOLVED IR.
#
# WHY A SEPARATE SCRIPT. taint_libsodium_eval.sh builds every arm from ONE
# post-prologepilog MIR, which is what makes A/C/P/F/X byte-comparable. The
# narrow arm cannot come from that MIR: it needs the indirect calls resolved in
# the IR, BEFORE lowering, so the taint analysis can follow them. It therefore
# gets its own MIR (libsodium.nar.pe.mir) and its own object.
#
# READ THIS BEFORE QUOTING N AGAINST A. Because N is lowered from different IR,
# an N-vs-A ratio contains the devirtualisation as well as the DIT placement.
# The devirtualisation is small here -- 18 of 44 indirect calls become direct,
# and text moves by 0.27% -- but it is not zero, and N is an upper-bound study
# of what better call-graph resolution would buy, not a like-for-like arm.
#
# WHAT "RESOLVED" MEANS, precisely. libsodium dispatches each primitive through
# a table of function pointers:
#
#   @crypto_onetimeauth_poly1305_donna_implementation =
#       hidden local_unnamed_addr global %struct... { ptr @..._donna_init, ... }
#
#   %0 = load ptr, ptr getelementptr(i8, ptr @..._implementation, i64 16)
#   %call = tail call i32 %0(ptr %state, ptr %key)
#
# Ten such tables exist and NONE of them is ever stored to -- verified below, not
# assumed. They are nevertheless plain `global`, not `constant`, because each was
# compiled in its own TU where another unit could in principle have written it.
# On whole-library bitcode that possibility is gone, so marking them `constant`
# is sound and lets instcombine fold the load into a direct call. globalopt will
# NOT do this on its own: the tables are `hidden`, not `internal`, and globalopt
# declines to constify anything externally visible.
#
# The 26 indirect calls that remain are genuinely unresolved -- randombytes
# callbacks, the base64/argon2 encoders -- and are left alone. Resolving those
# would need a real points-to analysis, and over-resolving randombytes (whose
# table IS written, by randombytes_set_implementation) would be unsound.
#
# REPRODUCTION TARGET. Reconstructed 2026-09-02 on M4 and checked against the
# statics recorded from the M5 run in
# paper_experiments/09-libsodium-cio-parity/data/static_policies.csv:
#
#            msr_DIT  functions  __text   infoloss
#   M5        749      164        250136   16
#   M4 here   749      164        250136   16
#
# Codegen is host-independent, so these must match exactly. If they ever stop
# matching, this recipe has drifted from the arm the paper reports -- fix the
# recipe, do not re-baseline the table.
#
# USAGE   utils/taint_libsodium_narrow.sh
# ENV     WORK, LLVM_BIN   (same defaults as taint_libsodium_eval.sh)
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SODIUM_VER="${SODIUM_VER:-1.0.21}"
LLVM_BIN="${LLVM_BIN:-$REPO_ROOT/build/bin}"
WORK="${WORK:-$HOME/Documents/libsodium-$SODIUM_VER}"

info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

[[ -f "$WORK/libsodium-annotated.ll" ]] \
  || die "no $WORK/libsodium-annotated.ll -- run taint_libsodium_eval.sh seed first"
for t in opt llc llvm-ar llvm-objdump llvm-size; do
  [[ -x "$LLVM_BIN/$t" ]] || die "missing $LLVM_BIN/$t"
done

TABLE_RE='^@[A-Za-z0-9_.]*implementation[A-Za-z0-9_.]* = hidden (local_unnamed_addr )?global %struct'

# ------------------------------------------------- verify, then resolve
info "verify the dispatch tables are never written"
bad=0
while read -r g; do
  n=$(grep -cE "store .*, ptr ${g}(,| )" "$WORK/libsodium-annotated.ll")
  [[ "$n" -ne 0 ]] && { echo "  $g has $n stores -- NOT constant"; bad=1; }
done < <(grep -oE "$TABLE_RE" "$WORK/libsodium-annotated.ll" | awk '{print $1}')
[[ "$bad" -ne 0 ]] && die "a dispatch table is written; constifying it would be unsound"
ntab=$(grep -cE "$TABLE_RE" "$WORK/libsodium-annotated.ll")
info "    $ntab tables, 0 stores"

before=$(grep -oE '(tail )?call [^@]*%[0-9]+\(' "$WORK/libsodium-annotated.ll" | wc -l | tr -d ' ')
info "mark tables constant + fold loads into direct calls"
sed -E 's/^(@[A-Za-z0-9_.]*implementation[A-Za-z0-9_.]* = hidden (local_unnamed_addr )?)global (%struct)/\1constant \3/' \
    "$WORK/libsodium-annotated.ll" > "$WORK/libsodium-annotated.nar.ll.tmp"
"$LLVM_BIN/opt" -S "$WORK/libsodium-annotated.nar.ll.tmp" -passes='function(instcombine)' \
    -o "$WORK/libsodium-annotated.nar.ll" || die "instcombine failed"
rm -f "$WORK/libsodium-annotated.nar.ll.tmp"
after=$(grep -oE '(tail )?call [^@]*%[0-9]+\(' "$WORK/libsodium-annotated.nar.ll" | wc -l | tr -d ' ')
info "    indirect calls $before -> $after ($((before - after)) resolved)"

# ------------------------------------------------- lower + analyze + emit
# -disable-tail-calls for the same reason as in taint_libsodium_eval.sh: a tail
# call has no epilogue, so one taken with DIT on never restores the mode.
info "lower to post-prologepilog MIR (tail calls disabled)"
"$LLVM_BIN/llc" -O2 -disable-tail-calls -stop-after=prologepilog \
    "$WORK/libsodium-annotated.nar.ll" -o "$WORK/libsodium.nar.pe.mir" \
  || die "llc -stop-after=prologepilog failed"
perl -0pi -e 's/<mcsymbol >//g' "$WORK/libsodium.nar.pe.mir"   # MIR CFI serialization bug

mkdir -p "$WORK/rpt"
rm -f "$WORK/rpt/infoloss.nar.txt"     # the report APPENDS
info "analyze + insert DIT [narrow] -taint-dit-placement=region"
"$LLVM_BIN/llc" -enable-new-pm -run-taint-interproc -taint-insert-dit \
    -taint-dit-placement=region \
    -taint-uncovered-report="$WORK/rpt/uncovered.nar.txt" \
    -taint-callsite-report="$WORK/rpt/callsites.nar.txt" \
    -taint-info-loss-report="$WORK/rpt/infoloss.nar.txt" \
    "$WORK/libsodium.nar.pe.mir" -o "$WORK/libsodium.narrow.mir" \
  || die "taint analysis failed for narrow"

"$LLVM_BIN/llc" -start-after=prologepilog "$WORK/libsodium.narrow.mir" -filetype=obj \
    -o "$WORK/libsodium.narrow.o" || die "narrow object failed"
"$LLVM_BIN/llvm-ar" rcs "$WORK/libsodium-narrow.a" "$WORK/libsodium.narrow.o"

# ------------------------------------------------- check against the paper
n=$("$LLVM_BIN/llvm-objdump" -d "$WORK/libsodium-narrow.a" | grep -cE 'msr[[:space:]]+DIT')
nf=$("$LLVM_BIN/llvm-objdump" -d "$WORK/libsodium-narrow.a" | awk '
  /^[0-9a-f]+ <.*>:$/ { fn=$0; next }
  /msr[[:space:]]+DIT/ { if (fn != "" && !(fn in seen)) { seen[fn]=1; c++ } }
  END { print c+0 }')
t=$("$LLVM_BIN/llvm-size" -m "$WORK/libsodium.narrow.o" \
    | sed -n 's/.*__text): *\([0-9]*\).*/\1/p' | head -1)
il=$(grep -cE '^\[[0-9]+\]' "$WORK/rpt/infoloss.nar.txt")
sev=$(grep -c 'severity  SEVERE' "$WORK/rpt/infoloss.nar.txt")

printf '\n  %-12s %8s %10s %9s %9s\n' '' msr_DIT functions __text infoloss
printf '  %-12s %8s %10s %9s %9s\n' 'M5 recorded' 749 164 250136 16
printf '  %-12s %8s %10s %9s %9s\n' 'this build' "$n" "$nf" "$t" "$il"
if [[ "$n" == 749 && "$nf" == 164 && "$t" == 250136 && "$il" == 16 ]]; then
  printf '\033[32m  MATCH -- this is the arm the paper reports\033[0m\n'
else
  printf '\033[31m  MISMATCH -- the recipe has drifted; do NOT report N as CIO-parity narrow\033[0m\n'
fi
[[ "$sev" -ne 0 ]] && printf '\033[31m  WARNING: %s SEVERE information-loss sites\033[0m\n' "$sev"
echo "  archive: $WORK/libsodium-narrow.a"
