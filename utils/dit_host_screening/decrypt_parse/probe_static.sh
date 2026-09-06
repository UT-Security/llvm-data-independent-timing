#!/bin/bash
# Static probe for experiment 10 (the secret leaves the primitive): build the
# mbedTLS 3.6 library under two seed sets and count, per function, where
# PSTATE.DIT switches land and how many instructions the analysis says need
# DIT. No harness, no gem5, no silicon: this is gate G0 of
# paper_experiments/10-mbedtls-session-ticket/README.md, and it runs in
# well under a minute on an aarch64 host.
#
#   seed_prim.txt   the developer's first annotation: the primitives only
#   seed_glue.txt   plus the points where the secret leaves the primitive
#
# Config, as the design specifies: MBEDTLS_USE_PSA_CRYPTO on (the ticket AEAD
# and all of TLS 1.3 then dispatch through PSA's switch tables, not the cipher
# layer's function pointers), MBEDTLS_HAVE_ASM off (bignum's inline asm sits
# inside the RSA primitive), MBEDTLS_HAVE_TIME off (no clock under gem5 SE).
#
# Usage: MBEDTLS_SRC=~/Documents/mbedtls-3.6.2 probe_static.sh [outdir]
# LLVM_BUILD defaults to this repo's build/.
set -euo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MBEDTLS_SRC="${MBEDTLS_SRC:-$HOME/Documents/mbedtls-3.6.2}"
LLVM_BUILD="${LLVM_BUILD:-$(cd "$D/../../.." && pwd)/build}"
OUT="${1:-$PWD/decrypt_parse_probe}"
JOBS="${JOBS:-24}"
[[ -d "$MBEDTLS_SRC" ]] || { echo "no mbedTLS at $MBEDTLS_SRC" >&2; exit 1; }
[[ -x "$LLVM_BUILD/bin/clang" ]] || { echo "no clang at $LLVM_BUILD/bin/clang" >&2; exit 1; }
mkdir -p "$OUT/reports"

# Per-TU report files: the pass truncates a report on open, so one path per
# translation unit or the last file compiled wins.
cat > "$OUT/cc-wrap.sh" <<WRAP
#!/bin/bash
src=""; prev=""
for a in "\$@"; do [[ "\$prev" == "-c" ]] && src="\$a"; prev="\$a"; done
extra=()
if [[ -n "\${SEED:-}" && -n "\$src" ]]; then
  b=\$(basename "\$src" .c)
  extra=(-ftaint-harden="\$SEED"
         -mllvm -taint-info-loss-report="\$REPORTS/\$b.loss"
         -mllvm -taint-clobber-report="\$REPORTS/\$b.clobber"
         -mllvm -taint-dit-precision-report="\$REPORTS/\$b.prec"
         \${EXTRA_MLLVM:-})
fi
exec "$LLVM_BUILD/bin/clang" "\${extra[@]}" "\$@"
WRAP
chmod +x "$OUT/cc-wrap.sh"

build() {   # build <variant> <seed file or empty> [extra -mllvm flags]
  local v="$1" seed="$2" extra="${3:-}" W="$OUT/mbedtls-$v"
  rm -rf "$W"; mkdir -p "$W" "$OUT/reports/$v"
  ( cd "$MBEDTLS_SRC" && tar cf - --exclude=.git --exclude='*.o' --exclude='*.a' . ) | ( cd "$W" && tar xf - )
  ( cd "$W" && python3 scripts/config.py set MBEDTLS_USE_PSA_CRYPTO \
            && python3 scripts/config.py unset MBEDTLS_HAVE_ASM \
            && python3 scripts/config.py unset MBEDTLS_HAVE_TIME_DATE \
            && python3 scripts/config.py unset MBEDTLS_HAVE_TIME )
  local t0=$(date +%s)
  SEED="$seed" REPORTS="$OUT/reports/$v" EXTRA_MLLVM="$extra" \
    make -C "$W/library" -j"$JOBS" CC="$OUT/cc-wrap.sh" AR="$LLVM_BUILD/bin/llvm-ar" CFLAGS="-O2" > "$OUT/build-$v.log" 2>&1
  echo "$v built in $(( $(date +%s) - t0 )) s"
}

build base ""
build prim "$D/seed_prim.txt"
build glue "$D/seed_glue.txt"
build nogate "$D/seed_glue.txt" "-mllvm -taint-no-modset-gate"

echo; echo "msr DIT per archive:"
for v in base prim glue nogate; do
  for a in libmbedcrypto libmbedtls libmbedx509; do
    printf "  %-7s %-14s %5d\n" "$v" "$a" "$("$LLVM_BUILD/bin/llvm-objdump" -d "$OUT/mbedtls-$v/library/$a.a" | grep -ci 'msr[[:space:]]*dit')"
  done
done
echo; echo "per-function counts: $D/dit_per_func.sh $OUT/mbedtls-<variant>/library/<tu>.o"
echo "precision reports:    $OUT/reports/<variant>/<tu>.prec  (need / underdit / collateral / switches)"
