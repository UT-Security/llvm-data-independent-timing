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
# ARMS come from PRESET (see below). The default, `gem5parity`, is the four-arm
# comparison of paper_experiments/09's 2026-09-05 headline, measured here on
# silicon at the compiler configuration the gem5 rig uses:
#   A base       unhardened                       -- the denominator
#   C blanket    unhardened + DIT on for the process
#   B bracket    Apple's prologue/epilogue with sb -- the sequence that ships,
#                and the only barrier form in the default set
#   T bracket    the instruction-matched NOP twin  -- layout control for B
#   P ExpeDITe   -ftaint-harden at the shipped defaults (callee contract, DIT
#                twins, intra-block placement, round-11 seeds, owned list)
#   Z ExpeDITe   its NOP twin                      -- layout control for P
# Archives for these come from utils/taint_libsodium_arms.sh, NOT from
# taint_libsodium_eval.sh, which cannot express the shipped defaults.
#
# PRESET=legacy restores the original set (baseline/blanket/hardened/func/fine/
# narrow on the wllvm archives), which is what the published M5/M4 numbers were
# measured with. All arms of a preset share one codegen configuration, so
# codegen differs only where DIT does.
#
# VALIDITY GATES, all enforced, all fatal to the affected row:
#   1. ditprobe reads ~4x between DIT off and on   -- the instrument can see DIT
#   2. ditprobe Perm stays flat                    -- negative control
#   3. CoreMHz ~4590 on every arm                  -- P-cluster residency
#   4. SHIM exit dit=1 for C, dit=0 for all others -- no leaked mode
#   5. within-config vs between-config spread      -- resolvability
#   6. arm order rotates every rep                 -- drift cannot fake an effect
set -uo pipefail

info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[33m    %s\033[0m\n' "$*"; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# The 20260901 snapshot is the toolchain the ORIGINAL run used; it predates the
# callee contract and cannot build the parity arms. Fall back to the repo's own
# build, which is what utils/taint_libsodium_arms.sh compiles against.
LLVM_BIN="${LLVM_BIN:-$HOME/Documents/dit-toolchain-snap-20260901/bin}"
[[ -x "$LLVM_BIN/llvm-objdump" ]] || LLVM_BIN="$REPO_ROOT/build/bin"
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
#
# CHEAP_TIMER=1 makes the driver's per-iteration samples come from CNTVCT_EL0
# instead of the kperf counter read. The kperf read is a call into the kperf
# driver and its cost lands INSIDE the timed region: 3234 cycles on M4, measured
# with utils/cio_offset_probe.c. That cancels in arm-vs-arm differences but not
# in ratios, and it is 72-87% of the measured baseline on chacha/AES, so it
# deflates those percentage rows by 3.6-14x. Percentages are unit-free, so
# CNTVCT (21-cycle offset) serves them better. OFF by default: the numbers in
# paper_experiments/09 came from the kperf timing and stay reproducible.
# NOTE the samples change UNITS when this is on -- CNTVCT ticks, 1 ns on M4.
#
# Both of these now DEFAULT from PRESET below, because a parity run needs the
# -O2 driver and the cheap timer together; either exported explicitly wins.
INC="$WORK/src/libsodium/include"
SHIM="$REPO_ROOT/utils/cio_arm_shim.h"

OURS="${OURS:-ditprobe ed25519 ed25519ph x25519 aead_chacha20poly1305 salsa20 xsalsa20 xchacha20 sha256 sha512 blake2b hmac_sha256 hmac_sha512}"
CIOB="${CIOB:-ed25519 chacha20_poly1305_encrypt chacha20_poly1305_decrypt argon2id aesni256gcm_encrypt aesni256gcm_decrypt}"
# lab:archive:dit. dit is 0 (nothing), 1 (the shim sets DIT before main: the
# blanket arm), or one of the Apple-bracket forms, where each public entry point
# CIO's driver calls is wrapped in Apple's prologue and epilogue (read the
# previous DIT state, msr DIT #1, speculation barrier, the call, clear only if
# it was clear; utils/dit_host_screening/cioparity/api_bracket.c):
#
#   api      Apple's real barrier, `sb`. What an M4 actually executes, and the
#            only rig that can measure it -- gem5 does not implement sb.
#   apiisb   `isb sy` in its place. NOT IN THE DEFAULT ARM SET and not a thing
#            we ship: it exists only because gem5 has no `sb`, so the gem5 rig
#            substitutes it. Measured against `api` on an M4 on 2026-09-05 the
#            two are indistinguishable -- +0.9 ns, +1.5 ns and -32.3 ns apart on
#            the three benchmarks that resolve it, all inside the arms' own
#            0.3-1.5% MAD -- which is what lets gem5's bracket column be read as
#            Apple's real sequence. Reach for it only to reproduce that check.
#   apinop   the instruction-matched layout control (-DAPI_NOP): every
#            instruction of the sequence kept at the same address, none of them
#            touching DIT. The bracket column is unreadable without it -- on
#            gem5 the wrapper's mere presence moved the pointer-chasing driver
#            by 2-3 points, more than its two switches cost.
#
# The interposition is a compile-time rename of the driver's extern prototypes,
# because ld64 has no --wrap. Part 2 only.
#
# PRESET picks the DEFAULT arm set, driver -O level and timer together, because
# those three are not independent -- a parity run needs all three changed at
# once. An explicitly set ARMS / CIO_OPT / CHEAP_TIMER always wins over it.
#
#   gem5parity (default)  The four arms of paper_experiments/09's 2026-09-05
#         headline -- base, blanket, Apple bracket, ExpeDITe at the shipped
#         defaults -- on archives built by utils/taint_libsodium_arms.sh at the
#         SAME compiler configuration the gem5 rig uses (callee contract, DIT
#         twins, intra-block placement, the round-11 fixpoint seeds, the owned
#         list). The bracket arm uses Apple's real `sb`; both NOP twins are
#         included, since neither the bracket nor the pass column can be read
#         without its own layout control. Drivers at -O2 and CHEAP_TIMER=1, both of which the
#         gem5 rig's equivalent choices imply: gem5 compiles the drivers at -O2,
#         and it reports cycles from stats.txt with no in-binary instrument at
#         all, which the 21-cycle CNTVCT timer approximates and the 3234-cycle
#         kperf read does not.
#
#   legacy  The arm set and settings this script shipped with, on the wllvm
#         archives from utils/taint_libsodium_eval.sh. This is what the original
#         M5/M4 numbers in paper_experiments/09 were measured with, so it stays
#         exactly as it was and those numbers stay reproducible.
PRESET="${PRESET:-gem5parity}"
case "$PRESET" in
  gem5parity)
    ARMS="${ARMS:-A:base:0 C:base:1 B:base:api T:base:apinop P:taint:0 Z:taintnop:0}"
    CIO_OPT="${CIO_OPT:--O2}"; CHEAP_TIMER="${CHEAP_TIMER:-1}" ;;
  legacy)
    ARMS="${ARMS:-A:baseline:0 C:baseline:1 B:baseline:api P:hardened:0 F:func:0 X:fine:0 N:narrow:0}"
    CIO_OPT="${CIO_OPT:--O0}"; CHEAP_TIMER="${CHEAP_TIMER:-0}" ;;
  *) die "unknown PRESET '$PRESET' (gem5parity | legacy)" ;;
esac
CHEAP=""; [[ "$CHEAP_TIMER" == 1 ]] && CHEAP="-DCIO_SHIM_CHEAP_TIMER"
# PMC=1 reads Apple's fixed counters (PMC0 cycles, PMC1 instructions) straight
# from EL0, which a kernel patched with PMCR0_USEREN_EN allows
# (github.com/jprx/PacmanPatcher). It replaces a ~3400-cycle / ~17,700-instruction
# kperf call per region boundary with one `mrs`: measured on this M4, 1 cycle and
# 0 instructions. That matters because the kperf pair is 12x a 291-cycle AES-GCM
# op, so absolute IPC off those counters is the INSTRUMENT's IPC (5.06 where the
# truth is 4.10) and every percentage is compressed. With PMC the region counters
# need no offset correction, and it needs no root.
#
# OFF by default for two reasons: an unpatched kernel does not have it (the shim
# probes SIGILL-safely and falls back, so this is safe to leave on), and the PMCs
# are PER-CORE where kperf's are per-thread, so a migration mid-region gives a
# delta spanning two cores. The shim drops absurd deltas and reports the count as
# reg_drop; check it is 0 before trusting a PMC run.
PMC="${PMC:-0}"
PMCFLAG=""; [[ "$PMC" == 1 ]] && PMCFLAG="-DCIO_SHIM_PMC"
# The unhardened arm's archive: `baseline` under the legacy preset, `base` under
# the parity one. Both loops below use it as the "did this benchmark build at
# all?" probe, so it must follow the arm set -- hardcoding `baseline` made the
# whole of PART 1, ditprobe and gates 1-2 with it, silently skip under a preset
# whose archives are named differently.
BASE_ARCH="$(set -- $ARMS; r=${1#*:}; echo "${r%:*}")"
API_SRC="$REPO_ROOT/utils/dit_host_screening/cioparity/api_bracket.c"
api_syms() {   # the entry points a CIO driver calls, and the -DAPI_* group
  case "$1" in
    ed25519)      echo "API_SIGN crypto_sign_keypair crypto_sign crypto_sign_open" ;;
    chacha20*)    echo "API_CHACHA crypto_aead_chacha20poly1305_ietf_keygen crypto_aead_chacha20poly1305_ietf_encrypt crypto_aead_chacha20poly1305_ietf_decrypt" ;;
    aesni256gcm*) echo "API_AES crypto_aead_aes256gcm_keygen crypto_aead_aes256gcm_encrypt crypto_aead_aes256gcm_decrypt" ;;
    argon2id)     echo "API_PWHASH crypto_pwhash" ;;
    *) echo "" ;;
  esac
}


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
  if [[ "$PMC" == 1 ]]; then
    # Root was only ever needed for kperf. A kernel that exposes the PMCs to EL0
    # gives exact cycles and instructions without it, so this is not a fallback.
    info "    NOT root, and it does not matter: PMC=1 reads the counters from EL0"
  else
    warn "NOT root: kperf unavailable, falling back to cntvct_el0 (ratios still valid)."
    warn "Re-run as:  sudo -E bash utils/taint_libsodium_sudo_run.sh"
    warn "Or enable EL0 PMC access (see utils/cio_pmc_check.c) and pass PMC=1."
  fi
fi
mkdir -p "$OUT"
info "    output -> $OUT"

# The gate instrument is TRACKED IN THIS REPO and installed into the benchmark
# checkout on every run, because crypto-dit-benchmarks is not part of this repo
# and the original ditprobe was lost by living only inside it. Same reason
# taint_cio_parity.sh copies utils/cio_ditctl.c rather than reading it from the
# work dir. utils/ditprobe.c is the copy to edit; this one is a build artifact.
if [[ -f "$REPO_ROOT/utils/ditprobe.c" ]]; then
  mkdir -p "$BENCH_DIR/ditprobe"
  if ! cmp -s "$REPO_ROOT/utils/ditprobe.c" "$BENCH_DIR/ditprobe/ditprobe.c"; then
    cp -f "$REPO_ROOT/utils/ditprobe.c" "$BENCH_DIR/ditprobe/ditprobe.c" \
      && info "    installed ditprobe.c -> $BENCH_DIR/ditprobe/"
  fi
  if [[ ! -f "$BENCH_DIR/ditprobe/Makefile" ]]; then
    # the rigs compile directly; this only makes `cd ditprobe && make` work the
    # way it does in the twelve sibling driver directories
    printf 'CC = clang\nCFLAGS = -Wall -O2 -I..\nLDFLAGS = -lsodium\n\nTARGET = ditprobe\nSRC = ditprobe.c\n\nall: $(TARGET)\n\n$(TARGET): $(SRC)\n\t$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)\n\nclean:\n\trm -f $(TARGET)\n\n.PHONY: all clean\n' \
      > "$BENCH_DIR/ditprobe/Makefile"
  fi
fi

# record exactly what produced these numbers
{ echo "date: $(date -u +%FT%TZ)"; echo "host: $(sysctl -n hw.model) $(sysctl -n machdep.cpu.brand_string)"
  echo "root: $([[ $(id -u) -eq 0 ]] && echo yes || echo no)"; echo "llvm: $LLVM_BIN"
  "$LLVM_BIN/llc" --version 2>/dev/null | sed -n '2,3p'
  echo "reps: ours=$REPS cio=$CIO_REPS"; echo "cio driver opt: $CIO_OPT"; echo "arms: $ARMS"
  echo "region timer: $([[ "$CHEAP_TIMER" == 1 ]] && echo 'cntvct_el0 (cheap; samples are TICKS not cycles)' || echo 'kperf counter read (carries the ~3234-cycle region offset)')"
  # CNTFRQ_EL0, read from the register, because it is the ONLY thing that sets
  # the units of the driver's samples under CHEAP_TIMER. Do NOT substitute
  # hw.tbfrequency: that is the Mach timebase behind mach_absolute_time and
  # clock_gettime, and on M4 the two disagree -- tbfrequency 24 MHz (41.67 ns,
  # which is what the older comments in this tree describe) against CNTFRQ_EL0
  # 1 GHz (1 ns). Confirmed empirically: a ~260 ns chacha call reads ~260 ticks.
  echo "cntfrq_el0: $(
    _cf=$(mktemp -t cntfrq).c; _cfb=${_cf%.c}
    printf '#include <stdio.h>\n#include <stdint.h>\nint main(void){uint64_t f;__asm__ volatile("mrs %%0, cntfrq_el0":"=r"(f));printf("%%llu",f);return 0;}\n' > "$_cf"
    if clang -O0 -o "$_cfb" "$_cf" 2>/dev/null; then "$_cfb"; else printf unknown; fi
    rm -f "$_cf" "$_cfb"
  ) Hz  (tbfrequency $(sysctl -n hw.tbfrequency 2>/dev/null || echo ?) Hz)"
  echo "seed: $(grep -c '^[A-Za-z_]' "$WORK/secret_m4_pointee.txt" 2>/dev/null) lines, $(sed -n '1p' "$WORK/secret_m4_pointee.txt" 2>/dev/null | cut -c1-110)"
  for a in $ARMS; do r=${a#*:}; arch=${r%:*}
    n=$("$LLVM_BIN/llvm-objdump" -d "$WORK/libsodium-$arch.a" 2>/dev/null | grep -cE 'msr[[:space:]]+DIT')
    echo "  ${a%%:*} $arch  msr_DIT=$n"; done
  # The CFLAGS the archives were built with. This rig CONSUMES prebuilt archives
  # and so cannot know them first-hand; taint_libsodium_arms.sh leaves the record
  # beside them. Say so loudly when it is missing rather than printing nothing:
  # a switch count identifies a configuration only if you already know them all.
  echo "build flags:"
  if [[ -f "$WORK/arm_flags.txt" ]]; then
    sed 's/^/  /' "$WORK/arm_flags.txt"
  else
    echo "  UNRECORDED -- $WORK/arm_flags.txt absent. The archives predate the"
    echo "  record, or were built by hand. Rebuild via taint_libsodium_arms.sh"
    echo "  before quoting these numbers anywhere they must be reproducible."
  fi
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
    lab=${a%%:*}; r=${a#*:}; arch=${r%:*}; dit=${r#*:}
    case "$dit" in api*) continue ;; esac   # our drivers bracket their own region
    clang -Wall -O2 $PMCFLAG -include "$SHIM" -I"$BENCH_DIR" -I"$INC" \
          -o "$OUT/$b.$arch" "$src" "$WORK/libsodium-$arch.a" -lm 2>/dev/null \
      || { warn "build failed $b/$lab"; continue; }
  done
done
read -r -a AR <<< "$ARMS"; NA=${#AR[@]}
for b in $OURS; do
  [[ -x "$OUT/$b.$BASE_ARCH" ]] || continue
  for rep in $(seq 1 "$REPS"); do
    for k in $(seq 0 $((NA-1))); do
      a=${AR[$(( (k + rep - 1) % NA ))]}
      lab=${a%%:*}; r=${a#*:}; arch=${r%:*}; dit=${r#*:}
      case "$dit" in api*) continue ;; esac
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
: > "$OUT/cio.csv"; echo "benchmark,arm,archive,rep,mean_ticks,n,dit_exit,cycle_src,timer_src,tot_cyc,tot_ins,map_stall,flush,reg_cyc,reg_ins,reg_n,samp_cyc,samp_ins,samp_n,samp_every,pmc_off,reg_drop,pinned" >> "$OUT/cio.csv"
for b in $CIOB; do
  src="$CIO_DIR/eval_$b.c"; [[ -f "$src" ]] || { warn "skip $b (no source)"; continue; }
  for a in $ARMS; do
    r=${a#*:}; arch=${r%:*}; dit=${r#*:}
    # -DCIO_SHIM_KPERF arms the shim's kperf cycle source (root only; it falls
    # back to CNTVCT_EL0 by itself otherwise). NOT used in part 1, whose drivers
    # #include perf.c themselves -- including it twice is a duplicate definition.
    renames=""; wrapobj=""; suffix=""
    case "$dit" in
      api|apiisb|apinop)
        set -- $(api_syms "$b"); grp="$1"; shift
        [[ -n "$grp" ]] || { warn "no API bracket table for $b"; continue; }
        for sy in "$@"; do renames="$renames -D$sy=expedite_api_$sy"; done
        # api: Apple's real sb. apiisb: isb sy, the form gem5 measures.
        # apinop: the instruction-matched twin, no DIT instruction at all.
        case "$dit" in
          api)    bar="-DAPI_BARRIER_SB" ;;
          apiisb) bar="" ;;
          apinop) bar="-DAPI_NOP" ;;
        esac
        wrapobj="$OUT/.api_bracket_$b.$dit.o"
        clang -O2 -std=gnu18 -c -DAPI_MACRO_RENAME -D$grp $bar -I"$INC" \
              -o "$wrapobj" "$API_SRC" 2>/dev/null || { warn "bracket build failed $b/$dit"; continue; }
        suffix="-$dit" ;;
    esac
    # shellcheck disable=SC2086
    clang -fomit-frame-pointer $CIO_OPT -std=c18 -DCIO_SHIM_KPERF $CHEAP $PMCFLAG $renames \
          -I"$BENCH_DIR" -include "$SHIM" -I"$INC" \
          -o "$OUT/eval_$b.$arch$suffix" "$src" $wrapobj "$WORK/libsodium-$arch.a" -lm 2>/dev/null \
      || warn "build failed eval_$b/$arch$suffix"
  done
done
for b in $CIOB; do
  [[ -x "$OUT/eval_$b.$BASE_ARCH" ]] || continue
  case "$b" in argon2id) NI=100; EX=100;; ed25519) NI=1000; EX="";; *) NI=1000; EX=100;; esac
  for rep in $(seq 1 "$CIO_REPS"); do
    for k in $(seq 0 $((NA-1))); do
      a=${AR[$(( (k + rep - 1) % NA ))]}
      lab=${a%%:*}; r=${a#*:}; arch=${r%:*}; dit=${r#*:}
      suffix=""; shimdit="$dit"
      case "$dit" in api*) suffix="-$dit"; shimdit=0 ;; esac   # the bracket is in the binary; the shim stays out
      bin="$OUT/eval_$b.$arch$suffix"; [[ -x "$bin" ]] || continue
      cc="$OUT/.cc"; err="$OUT/.e"
      # CIO's drivers have no DIT support of their own, so the blanket arm is the
      # shim's constructor -- their sources stay byte-identical across arms.
      SHIM_DIT="$shimdit" "$bin" "$NI" 25 "$MSG" $EX "$cc" >/dev/null 2>"$err"
      [[ -s "$cc" ]] || continue
      de=$(sed -n 's/.*SHIM exit dit=\([01]\).*/\1/p' "$err" | tail -1)
      cs=$(sed -n 's/.*cycles=\([a-z]*\).*/\1/p' "$err" | tail -1)
      tm=$(sed -n 's/.*timer=\([a-z]*\).*/\1/p' "$err" | tail -1)
      tc=$(sed -n 's/.*tot_cyc=\([0-9]*\).*/\1/p' "$err" | tail -1)
      ti=$(sed -n 's/.*tot_ins=\([0-9]*\).*/\1/p' "$err" | tail -1)
      ms=$(sed -n 's/.*map_stall=\([0-9]*\).*/\1/p' "$err" | tail -1)
      fl=$(sed -n 's/.*flush=\([0-9]*\).*/\1/p' "$err" | tail -1)
      rc2=$(sed -n 's/.*reg_cyc=\([0-9]*\).*/\1/p' "$err" | tail -1)
      ri=$(sed -n 's/.*reg_ins=\([0-9]*\).*/\1/p' "$err" | tail -1)
      rn=$(sed -n 's/.*reg_n=\([0-9]*\).*/\1/p' "$err" | tail -1)
      # The sampled PMC series: exact cycles AND instructions from 1 region in
      # samp_every, so the isb drain perturbs 1/64th of the run instead of all
      # of it. pmc_off is the null-region floor measured in that same process;
      # rd counts samples dropped by the migration guard and MUST be 0.
      sc=$(sed -n 's/.*samp_cyc=\([0-9]*\).*/\1/p' "$err" | tail -1)
      si=$(sed -n 's/.*samp_ins=\([0-9]*\).*/\1/p' "$err" | tail -1)
      sn=$(sed -n 's/.*samp_n=\([0-9]*\).*/\1/p' "$err" | tail -1)
      se=$(sed -n 's/.*samp_every=\([0-9]*\).*/\1/p' "$err" | tail -1)
      po=$(sed -n 's/.*pmc_off=\([0-9]*\).*/\1/p' "$err" | tail -1)
      rd=$(sed -n 's/.*reg_drop=\([0-9]*\).*/\1/p' "$err" | tail -1)
      # Which core the thread was bound to, or -1 if unpinned. Pinning needs
      # root (kern.sched_thread_bind_cpu is EPERM otherwise) and only matters
      # for the per-core PMCs, so an unrooted PMC run is legitimately -1.
      pn=$(sed -n 's/.*pinned=\(-*[0-9]*\).*/\1/p' "$err" | tail -1)
      python3 - "$cc" "$b" "$lab" "$arch" "$rep" "${de:-?}" "${cs:-?}" "${tm:-?}" \
               "${tc:-0}" "${ti:-0}" "${ms:-0}" "${fl:-0}" \
               "${rc2:-0}" "${ri:-0}" "${rn:-0}" \
               "${sc:-0}" "${si:-0}" "${sn:-0}" "${se:-0}" "${po:-0}" "${rd:-0}" "${pn:--1}" \
               >> "$OUT/cio.csv" <<'PY'
import sys
p,b,lab,arch,rep,de,cs,tm,tc,ti,ms,fl,rc,ri,rn,sc,si,sn,se,po,rd,pn = sys.argv[1:23]
v=[float(x) for x in open(p).read().split('\n')[1:] if x.strip().isdigit()]
if v: print(f"{b},{lab},{arch},{rep},{sum(v)/len(v):.3f},{len(v)},{de},{cs},{tm},{tc},{ti},{ms},{fl},{rc},{ri},{rn},{sc},{si},{sn},{se},{po},{rd},{pn}")
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
