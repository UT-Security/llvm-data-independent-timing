#!/usr/bin/env bash
#
# THE NULL CONTROL FOR THE LAYOUT TERM.
#
# Every hardened-vs-unhardened figure in experiment 09 contains a term nobody
# chose: inserting `msr DIT` grows the text, everything downstream moves, and
# the binary runs at a different speed for reasons that have nothing to do with
# DIT. The rig prices that term with a per-policy NOP twin, which is exact but
# says nothing about how big the term would be if the pass had done nothing at
# all. This script answers that.
#
# It links the UNHARDENED library K bytes further along in .text, for a list of
# K. The padding is unreachable `nop`s in their own section ahead of everything
# else: not one instruction of the program changes, not one instruction of the
# padding executes, and the committed instruction count is identical for every
# K (run_cio_gem5.py's own gate checks that). Every difference between two of
# these binaries is address placement and nothing else.
#
# On libsodium 1.0.21 under gem5 NeoverseV2 FDP the spread over K = 0..256 is
# 0.50 to 7.04 points depending on the benchmark, which is the same size as the
# layout term the rig attributes to the pass. See
# docs/results/dit-layout-lottery-2026-09-06.md.
#
# USAGE
#   LIB=<dir>   libsodium build tree to link (default: the exp09 `base` variant)
#   OUT=<dir>   where to put pad_*.S and bin/                (default below)
#   PADS="..."  offsets in bytes                             (default 0..256)
#   BENCHES=".."
#   ALIGN=<n>   also pass -mllvm -align-all-nofallthru-blocks=<n> to the link
#               (the library must have been built with the same value)
#
# Then run the binaries exactly like any other arm:
#   WORK=$OUT run_cio_gem5.py --arms pad0,pad4,... --configs spec --out $OUT/out
set -uo pipefail

R="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$R/../../.." && pwd)"
LLVM="${LLVM:-$REPO/build}"
G5="${G5:-$REPO/gem5-DIT}"
WORK="${WORK:-$HOME/Documents/libsodium-cioparity}"
LIB="${LIB:-$WORK/base}"
OUT="${OUT:-$HOME/Documents/libsodium-relink-null}"
CC="$LLVM/bin/clang"
MARCH="${MARCH:-armv8.4-a}"
BENCHES="${BENCHES:-ed25519 chacha20_poly1305_encrypt chacha20_poly1305_decrypt aesni256gcm_encrypt aesni256gcm_decrypt}"
PADS="${PADS:-0 4 8 12 16 24 32 48 64 96 128 256}"
ALIGN="${ALIGN:-}"
AFLAGS=()
[[ -n "$ALIGN" ]] && AFLAGS=(-mllvm -align-all-nofallthru-blocks="$ALIGN")

die() { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
[[ -x "$CC" ]] || die "no clang at $CC"
[[ -f "$LIB/src/libsodium/.libs/libsodium.a" ]] || die "no library at $LIB"
[[ -d "$WORK/src" ]] || die "no staged CIO drivers at $WORK/src -- run build_arms.sh link first"
M5LIB="${M5LIB:-$G5/util/m5/build/arm64/out}"
[[ -f "$M5LIB/libm5.a" ]] || die "no libm5.a under $M5LIB (it is ABI-only; any gem5 checkout's copy works)"

mkdir -p "$OUT/bin"

# The pad is its own section so the linker places it as a unit, and it is named
# to sort ahead of .text.* in any linker that sorts by name. It carries a symbol
# so `llvm-nm` can confirm where it landed and how far the library moved.
for K in $PADS; do
  cat > "$OUT/pad_$K.S" <<EOF
        .section .text.aaapad,"ax",@progbits
        .globl  __dit_layout_pad
        .type   __dit_layout_pad,%function
__dit_layout_pad:
        .rept $((K / 4))
        nop
        .endr
        .size   __dit_layout_pad, . - __dit_layout_pad
EOF
done

# Assemble each pad to a FIXED object name. Handing the .S straight to the link
# line makes clang assemble it to a temp file with a random suffix, and that
# name ends up in .symtab: the binaries then differ in five bytes of the string
# table between runs. It is behind .text and cannot move a single instruction,
# but a byte-identity check is worth more than an explanation of why it failed.
for K in $PADS; do
  [[ -f "$OUT/pad_$K.o" ]] || "$CC" -c -o "$OUT/pad_$K.o" "$OUT/pad_$K.S" \
    || die "could not assemble pad_$K.S"
done

for b in $BENCHES; do
  [[ -f "$WORK/src/eval_$b.c" ]] || { echo "skip $b -- no staged driver"; continue; }
  for K in $PADS; do
    o="$OUT/bin/eval_${b}.pad$K"
    [[ -f "$o" ]] && continue
    "$CC" -march="$MARCH" -O2 -std=gnu18 -static -fomit-frame-pointer \
        "${AFLAGS[@]}" -DNO_DYN_HIT_COUNTS \
        -I"$R" -I"$G5/include" -I"$LIB/src/libsodium/include" \
        -o "$o" "$OUT/pad_$K.o" "$WORK/src/eval_$b.c" "$R/blanket_ctor.c" \
        "$LIB/src/libsodium/.libs/libsodium.a" \
        -L"$M5LIB" -lm5 -lm \
      > "$OUT/bin/.link_${b}_$K.log" 2>&1 \
      || { echo "LINK FAILED $b pad=$K"; tail -5 "$OUT/bin/.link_${b}_$K.log"; exit 1; }
  done
  # Where the padding landed, and how far a library symbol moved because of it.
  # Under 64 B block alignment the shift QUANTISES: several K give one address.
  for K in $PADS; do
    printf '%-28s K=%-6s pad@0x%s  crypto_verify_16@0x%s\n' "$b" "$K" \
      "$("$LLVM/bin/llvm-nm" "$OUT/bin/eval_${b}.pad$K" | awk '$3=="__dit_layout_pad"{print $1}')" \
      "$("$LLVM/bin/llvm-nm" "$OUT/bin/eval_${b}.pad$K" | awk '$3=="crypto_verify_16"{print $1}')"
  done
done
echo "linked $(ls "$OUT/bin"/eval_* 2>/dev/null | wc -l) binaries into $OUT/bin"
