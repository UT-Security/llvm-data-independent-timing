#!/usr/bin/env bash
#
# ONE script for the whole libsodium DIT evaluation, runnable under sudo so that
# kperf cycle/instruction counters are available.
#
#   sudo -E bash utils/taint_libsodium_sudo_run.sh
#
# -E MATTERS: it preserves HOME, which perf.c uses to place its counter output,
# and PATH. Everything else this script needs it sets itself.
#
# WHY THIS EXISTS SEPARATELY from taint_libsodium_bench.sh / taint_cio_parity.sh:
# sudo strips DYLD_*, so the injected dylibs those two rely on for P-core
# residency and for the blanket-DIT arm silently do nothing under root. This
# script compiles that behaviour in instead (utils/cio_arm_shim.h, force-included
# into every binary in every arm) so a rooted run measures what it claims to.
#
# IT RUNS BOTH RIGS:
#   PART 1  our 13 primitives  (crypto-dit-benchmarks drivers, -O2)
#   PART 2  CIO's 6 benchmarks (their drivers, their iteration counts, -O0)
#
# ARMS (all six share one .pe.mir, so codegen differs only where DIT does):
#   A baseline   unhardened, DIT never set        -- the MIR round-trip control
#   C blanket    unhardened + DIT on              -- the coarse mitigation
#   P hardened   shipped defaults (region/30/hoist/gate)
#   F func       -taint-dit-placement=function
#   X fine       pre-2026-08-24 defaults (switch-cyc=0, loop-hoist=0)
#   N narrow     shipped defaults, on indirect-call-resolved IR
#
# VALIDITY GATES, all enforced, all fatal to the affected row:
#   1. ditprobe reads ~4x between DIT off and on   -- the instrument can see DIT
#   2. ditprobe Perm stays flat                    -- negative control
#   3. CoreMHz ~4590 on every arm                  -- P-cluster residency
#   4. SHIM exit dit=1 for C, dit=0 for all others -- no leaked mode
#   5. within-config vs between-config spread      -- resolvability
#   6. arm order rotates every rep                 -- drift cannot fake an effect
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LLVM_BIN="${LLVM_BIN:-$HOME/Documents/dit-toolchain-snap-20260901/bin}"
WORK="${WORK:-$HOME/Documents/libsodium-1.0.21}"
BENCH_DIR="${BENCH_DIR:-$HOME/Documents/crypto-dit-benchmarks}"
CIO_DIR="${CIO_DIR:-$HOME/Documents/cio-eval}"
OUT="${OUT:-$HOME/Documents/dit-sudo-run-$(date +%Y%m%d-%H%M%S)}"
REPS="${REPS:-25}"
CIO_REPS="${CIO_REPS:-15}"
# CIO build their eval drivers at -O0 on purpose ("for the eval code, don't
# optimize anything"). Faithful, but -O0 puts real work INSIDE the timed window
# -- the volatile start/end stores, unregistered argument setup -- which adds a
# roughly constant offset to every sample and therefore COMPRESSES every ratio.
# CIO_OPT=-O2 measures the same experiment the way an application would build it.
# The library archives are -O2 either way; only the driver changes.
CIO_OPT="${CIO_OPT:--O0}"
INC="$WORK/src/libsodium/include"
SHIM="$REPO_ROOT/utils/cio_arm_shim.h"

OURS="${OURS:-ditprobe ed25519 ed25519ph x25519 aead_chacha20poly1305 salsa20 xsalsa20 xchacha20 sha256 sha512 blake2b hmac_sha256 hmac_sha512}"
CIOB="${CIOB:-ed25519 chacha20_poly1305_encrypt chacha20_poly1305_decrypt argon2id aesni256gcm_encrypt aesni256gcm_decrypt}"
ARMS="${ARMS:-A:baseline:0 C:baseline:1 P:hardened:0 F:func:0 X:fine:0 N:narrow:0}"

info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[33m    %s\033[0m\n' "$*"; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------- preflight
info "preflight"
[[ -f "$SHIM" ]] || die "missing $SHIM"
[[ -d "$INC" ]]  || die "libsodium headers not found: $INC"
for a in $ARMS; do r=${a#*:}; ar="$WORK/libsodium-${r%:*}.a"
  [[ -f "$ar" ]] || die "missing archive $ar -- run utils/taint_libsodium_eval.sh first"; done
[[ -x "$LLVM_BIN/llvm-objdump" ]] || die "no llvm-objdump at $LLVM_BIN (set LLVM_BIN)"

if [[ "$(id -u)" -eq 0 ]]; then
  info "    running as root: kperf cycle/instruction counters ARE available"
  [[ -n "${SUDO_USER:-}" ]] || warn "SUDO_USER unset -- output files will stay root-owned"
  [[ "$HOME" == /var/root* ]] && warn "HOME=$HOME -- you did not pass -E; perf.c will write counters under /var/root"
else
  warn "NOT root: kperf unavailable, falling back to cntvct_el0 (ratios still valid)."
  warn "Re-run as:  sudo -E bash utils/taint_libsodium_sudo_run.sh"
fi
mkdir -p "$OUT"
info "    output -> $OUT"

# record exactly what produced these numbers
{ echo "date: $(date -u +%FT%TZ)"; echo "host: $(sysctl -n hw.model) $(sysctl -n machdep.cpu.brand_string)"
  echo "root: $([[ $(id -u) -eq 0 ]] && echo yes || echo no)"; echo "llvm: $LLVM_BIN"
  "$LLVM_BIN/llc" --version 2>/dev/null | sed -n '2,3p'
  echo "reps: ours=$REPS cio=$CIO_REPS"; echo "cio driver opt: $CIO_OPT"; echo "arms: $ARMS"
  echo "seed: $(grep -c '^[a-z]' "$WORK/secret_m4_pointee.txt" 2>/dev/null) lines from CIO libsodium.uarch_checker.config"
  for a in $ARMS; do r=${a#*:}; arch=${r%:*}
    n=$("$LLVM_BIN/llvm-objdump" -d "$WORK/libsodium-$arch.a" 2>/dev/null | grep -cE 'msr[[:space:]]+DIT')
    echo "  ${a%%:*} $arch  msr_DIT=$n"; done
} > "$OUT/provenance.txt"
cat "$OUT/provenance.txt"

# ---------------------------------------------------------------- part 1
# OURS="ditprobe" keeps the instrument gates while skipping the 13-primitive
# sweep -- the gates are not optional, they are what makes a null result mean
# anything. OURS="" skips part 1 entirely and you lose them.
info "PART 1: our drivers, -O2  [$( [[ -z "${OURS// }" ]] && echo SKIPPED || echo "$OURS" )]"
: > "$OUT/ours.csv"; echo "benchmark,metric,arm,archive,rep,value,dit_exit" >> "$OUT/ours.csv"
for b in $OURS; do
  src="$BENCH_DIR/$b/$b.c"; [[ -f "$src" ]] || { warn "skip $b (no source)"; continue; }
  for a in $ARMS; do
    lab=${a%%:*}; r=${a#*:}; arch=${r%:*}
    clang -Wall -O2 -include "$SHIM" -I"$BENCH_DIR" -I"$INC" \
          -o "$OUT/$b.$arch" "$src" "$WORK/libsodium-$arch.a" -lm 2>/dev/null \
      || { warn "build failed $b/$lab"; continue; }
  done
done
read -r -a AR <<< "$ARMS"; NA=${#AR[@]}
for b in $OURS; do
  [[ -x "$OUT/$b.baseline" ]] || continue
  for rep in $(seq 1 "$REPS"); do
    for k in $(seq 0 $((NA-1))); do
      a=${AR[$(( (k + rep - 1) % NA ))]}
      lab=${a%%:*}; r=${a#*:}; arch=${r%:*}; dit=${r#*:}
      bin="$OUT/$b.$arch"; [[ -x "$bin" ]] || continue
      # our drivers wrap the measured region with their own dit_enable(); the shim
      # must NOT also set it, or "blanket" would mean the whole process here and
      # only the region in part 2.
      err="$OUT/.e"; ENABLE_DIT="$dit" "$bin" 2>"$err" \
        | awk -v L="$lab" -v A="$arch" -v R="$rep" -v B="$b" \
            '/^=== / && NF>=4 && $(NF-1) ~ /^[0-9]+$/ {
               k=""; for(i=2;i<=NF-2;i++) k=k (i>2?"_":"") $i;
               print B","k","L","A","R","$(NF-1)",PLACEHOLDER" }' \
        | sed "s/PLACEHOLDER/$(sed -n 's/.*SHIM exit dit=\([01]\).*/\1/p' "$err" | tail -1)/" \
        >> "$OUT/ours.csv"
    done
  done
  echo "    $b"
done

# ---------------------------------------------------------------- part 2
info "PART 2: CIO's 6 benchmarks, their parameters (drivers at $CIO_OPT)"
MSG="$(LC_ALL=C tr -dc '[:alnum:]' </dev/urandom | head -c 100)"; echo "$MSG" > "$OUT/msg.txt"
: > "$OUT/cio.csv"; echo "benchmark,arm,archive,rep,mean_ticks,n,dit_exit,cycle_src,tot_cyc,tot_ins,map_stall,flush,reg_cyc,reg_ins,reg_n" >> "$OUT/cio.csv"
for b in $CIOB; do
  src="$CIO_DIR/eval_$b.c"; [[ -f "$src" ]] || { warn "skip $b (no source)"; continue; }
  for a in $ARMS; do
    r=${a#*:}; arch=${r%:*}
    # -DCIO_SHIM_KPERF arms the shim's kperf cycle source (root only; it falls
    # back to CNTVCT_EL0 by itself otherwise). NOT used in part 1, whose drivers
    # #include perf.c themselves -- including it twice is a duplicate definition.
    clang -fomit-frame-pointer $CIO_OPT -std=c18 -DCIO_SHIM_KPERF \
          -I"$BENCH_DIR" -include "$SHIM" -I"$INC" \
          -o "$OUT/eval_$b.$arch" "$src" "$WORK/libsodium-$arch.a" -lm 2>/dev/null \
      || warn "build failed eval_$b/$arch"
  done
done
for b in $CIOB; do
  [[ -x "$OUT/eval_$b.baseline" ]] || continue
  case "$b" in argon2id) NI=100; EX=100;; ed25519) NI=1000; EX="";; *) NI=1000; EX=100;; esac
  for rep in $(seq 1 "$CIO_REPS"); do
    for k in $(seq 0 $((NA-1))); do
      a=${AR[$(( (k + rep - 1) % NA ))]}
      lab=${a%%:*}; r=${a#*:}; arch=${r%:*}; dit=${r#*:}
      bin="$OUT/eval_$b.$arch"; [[ -x "$bin" ]] || continue
      cc="$OUT/.cc"; err="$OUT/.e"
      # CIO's drivers have no DIT support of their own, so the blanket arm is the
      # shim's constructor -- their sources stay byte-identical across arms.
      SHIM_DIT="$dit" "$bin" "$NI" 25 "$MSG" $EX "$cc" >/dev/null 2>"$err"
      [[ -s "$cc" ]] || continue
      de=$(sed -n 's/.*SHIM exit dit=\([01]\).*/\1/p' "$err" | tail -1)
      cs=$(sed -n 's/.*cycles=\([a-z]*\).*/\1/p' "$err" | tail -1)
      tc=$(sed -n 's/.*tot_cyc=\([0-9]*\).*/\1/p' "$err" | tail -1)
      ti=$(sed -n 's/.*tot_ins=\([0-9]*\).*/\1/p' "$err" | tail -1)
      ms=$(sed -n 's/.*map_stall=\([0-9]*\).*/\1/p' "$err" | tail -1)
      fl=$(sed -n 's/.*flush=\([0-9]*\).*/\1/p' "$err" | tail -1)
      rc2=$(sed -n 's/.*reg_cyc=\([0-9]*\).*/\1/p' "$err" | tail -1)
      ri=$(sed -n 's/.*reg_ins=\([0-9]*\).*/\1/p' "$err" | tail -1)
      rn=$(sed -n 's/.*reg_n=\([0-9]*\).*/\1/p' "$err" | tail -1)
      python3 - "$cc" "$b" "$lab" "$arch" "$rep" "${de:-?}" "${cs:-?}" \
               "${tc:-0}" "${ti:-0}" "${ms:-0}" "${fl:-0}" \
               "${rc2:-0}" "${ri:-0}" "${rn:-0}" >> "$OUT/cio.csv" <<'PY'
import sys
p,b,lab,arch,rep,de,cs,tc,ti,ms,fl,rc,ri,rn = sys.argv[1:15]
v=[float(x) for x in open(p).read().split('\n')[1:] if x.strip().isdigit()]
if v: print(f"{b},{lab},{arch},{rep},{sum(v)/len(v):.3f},{len(v)},{de},{cs},{tc},{ti},{ms},{fl},{rc},{ri},{rn}")
PY
    done
  done
  echo "    $b"
done
rm -f "$OUT/.cc" "$OUT/.e"

# ---------------------------------------------------------------- report
info "REPORT"
OUT="$OUT" python3 "$REPO_ROOT/utils/taint_libsodium_sudo_report.py"

[[ -n "${SUDO_USER:-}" ]] && chown -R "$SUDO_USER" "$OUT" 2>/dev/null
info "done -- $OUT"
