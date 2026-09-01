#!/bin/bash
# Experiment 08 - seed ground truth.
#
# Compare our interprocedural taint set against CryptoMPK's (IEEE S&P 2022) on
# libhydrogen, using their exact shipped source and their exact shipped taint
# report as the third-party ground truth.
#
# Why libhydrogen and not one of their other six targets: the whole library is
# a single translation unit, so our per-TU analysis and their whole-program LTO
# analysis see the same code. On libsodium (8 relevant TUs) the cross-TU limit
# would dominate and the comparison would measure that instead of precision.
#
# Prereqs: a built compiler at ../../../build/bin (see docs/reference/dit-abi-runbook.md).
# Runtime: ~4 min for the three seed arms, longer with -taint-output.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
B="${B:-$(cd ../../../build/bin && pwd)}"
SRC=libhydrogen_cryptompk
GT=ground_truth/cryptompk_libhydrogen_lib.txt
mkdir -p arms out

for a in create_only keygen_only both; do
    sf="seed_$a.txt"; [ "$a" = both ] && sf=seed_hydro_sign.txt
    # The taint reports APPEND. Two runs double the file, so clear first.
    rm -f "arms/$a.txt" "arms/${a}_src.txt" "arms/${a}_stats.txt" \
          "arms/$a.prec.txt" "arms/$a.loss.txt" "arms/$a.o"
    echo "[build] $a  ($sf)"
    "$B/clang" -O2 -g -I "$SRC" -ftaint-harden="$sf" \
        -mllvm -taint-output="$PWD/arms/$a.txt" \
        -mllvm -taint-dit-precision-report="$PWD/arms/$a.prec.txt" \
        -mllvm -taint-info-loss-report="$PWD/arms/$a.loss.txt" \
        -c "$SRC/hydrogen.c" -o "arms/$a.o" 2>"arms/$a.stderr"
    printf '         msr DIT=%s  info-loss=%s\n' \
        "$("$B/llvm-objdump" -d "arms/$a.o" | grep -icE '\bmsr\b.*\bdit\b')" \
        "$(grep -c '^\[' "arms/$a.loss.txt" 2>/dev/null || echo 0)"
done

echo
for a in create_only keygen_only both; do
    echo "===== seed set: $a"
    ./compare_taint_sets.py   "$GT" "arms/${a}_src.txt" | head -6
    ./attribute_functions.py "$SRC" "$GT" "arms/${a}_src.txt" | head -4
done
./attribute_functions.py "$SRC" "$GT" arms/both_src.txt --csv out/function_agreement.csv >/dev/null
./compare_taint_sets.py   "$GT" arms/both_src.txt --csv out/file_agreement.csv >/dev/null
echo
echo "CSVs refreshed in out/. Copy to paper_experiments/08-seed-ground-truth/data/."
