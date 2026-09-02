#!/usr/bin/env bash
#
# CIO-parity evaluation: run the experiments CIO ran, with CIO's seeds, on our pass.
#
# WHY THIS IS A SEPARATE RIG FROM taint_libsodium_bench.sh. That one uses OUR
# benchmark drivers (crypto-dit-benchmarks, 13 primitives, 1000 iterations
# pre-averaged into one number per run). This one uses THEIRS, unmodified, at
# their parameters, so the head-to-head against their published overheads is a
# comparison and not an analogy:
#
#   driver                         iters  warmup  args                (Makefile)
#   eval_ed25519                    1000      25  msg=100 chars
#   eval_chacha20_poly1305_encrypt  1000      25  msg=100, AD=100
#   eval_chacha20_poly1305_decrypt  1000      25  msg=100, AD=100
#   eval_aesni256gcm_encrypt        1000      25  msg=100, AD=100     (x86 name;
#   eval_aesni256gcm_decrypt        1000      25  msg=100, AD=100      ARM may skip)
#   eval_argon2id                    100      25  out=100, opslimit/
#                                                 memlimit INTERACTIVE
#
# Their statistic is the arithmetic MEAN of the per-iteration cycle counts
# (process_eval_data.py, np.mean) and overhead is a ratio of means. We keep that
# and add reps + arm rotation on top, because a single run per arm in a fixed
# order cannot separate an effect from drift -- reported alongside their
# single-run statistic, never instead of it.
#
# DELIBERATE DEVIATIONS, all recorded in the report header:
#   1. libsodium 1.0.21, not their 1.0.18-RELEASE. Their seed config resolves
#      21/21 against it and 1.0.21 is what the rest of our results use.
#   2. AArch64 timer. Theirs is `cpuid;rdtsc` / `rdtscp;cpuid`; the equivalent
#      idiom here is an `isb`-bracketed read of CNTVCT_EL0, which counts TIME in
#      ~41.67 ns steps rather than cycles. Their mean-of-1000 absorbs the
#      quantisation; absolute "cycles" are not cycles and are not reported as such.
#   3. Blanket DIT is injected from OUTSIDE the program (libditctl.dylib) so
#      their driver sources stay byte-identical across every arm.
#   4. QoS instead of `taskset -c 0`, which has no macOS equivalent.
#
# USAGE   utils/taint_cio_parity.sh [fetch|build|run|report ...]
# ENV     WORK, LIBSODIUM, LLVM_BIN, REPS, ARMS, BENCHES
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="${WORK:-$HOME/Documents/cio-eval}"
LIBSODIUM="${LIBSODIUM:-$HOME/Documents/libsodium-1.0.21}"
LLVM_BIN="${LLVM_BIN:-$REPO_ROOT/build/bin}"
INC="$LIBSODIUM/src/libsodium/include"
OUT="${OUT:-$WORK/results}"
REPS="${REPS:-15}"
CIO_RAW="https://raw.githubusercontent.com/counter-optimization/cio/HEAD"

# label:archive:ENABLE_DIT
ARMS="${ARMS:-A:baseline:0 C:baseline:1 P:hardened:0 F:func:0 X:fine:0 N:narrow:0}"
BENCHES="${BENCHES:-ed25519 chacha20_poly1305_encrypt chacha20_poly1305_decrypt argon2id aesni256gcm_encrypt aesni256gcm_decrypt}"

# CIO's own iteration counts, from their Makefile. Do not "tune" these.
iters_for() { case "$1" in argon2id) echo "100 25";; *) echo "1000 25";; esac; }
# argv layout differs: ed25519 takes no size arg, the others do.
extra_for() { case "$1" in ed25519) echo "";; argon2id) echo "100";; *) echo "100";; esac; }

info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

STAGES="${*:-fetch build run report}"
want() { [[ " $STAGES " == *" $1 "* ]]; }

mkdir -p "$WORK" "$OUT"

# ---------------------------------------------------------------- fetch
if want fetch; then
  info "fetch CIO's evaluation sources"
  for f in eval_util.h eval_ed25519.c eval_argon2id.c \
           eval_chacha20_poly1305_encrypt.c eval_chacha20_poly1305_decrypt.c \
           eval_aesni256gcm_encrypt.c eval_aesni256gcm_decrypt.c \
           Makefile eval.sh process_eval_data.py libsodium.uarch_checker.config; do
    [[ -f "$WORK/$f.orig" ]] && continue
    curl -sSL --max-time 60 -o "$WORK/$f.orig" "$CIO_RAW/$f" || die "fetch failed: $f"
  done
  for f in "$WORK"/eval_*.c.orig; do cp -f "$f" "${f%.orig}"; done

  info "port eval_util.h to AArch64 (their x86 path kept behind #else)"
  WORK="$WORK" python3 - <<'PY' || die "eval_util.h port failed"
import os, pathlib
w = os.environ['WORK']
s = pathlib.Path(w, 'eval_util.h.orig').read_text()
anchor = "#define EVAL_UTIL_H_SEED 172812\n"
assert anchor in s, "anchor missing - upstream eval_util.h changed"
arm = anchor + '''
#if defined(__aarch64__)
/* The only thing that cannot cross architectures is the cycle timer. Theirs is
   serialise-then-read / read-then-serialise; the AArch64 idiom for that is an
   `isb` around a read of CNTVCT_EL0, the only cycle-ish counter readable from
   user space on Apple silicon (PMCCNTR needs kperf and root). It counts TIME,
   ~41.67 ns per step; their mean over 1000 iterations absorbs that.
   updateStats/print_dynamic_hitcounts let THEIR instrumented libsodium count
   x86 opcode hits; ours never calls them and the x86 asm will not assemble. */
#define START_CYCLE_TIMER ({ uint64_t c=0; \\
  __asm__ volatile("isb\\n\\tmrs %[c], cntvct_el0" : [c]"=r"(c) :: "memory"); c; })
#define STOP_CYCLE_TIMER  ({ uint64_t c=0; \\
  __asm__ volatile("mrs %[c], cntvct_el0\\n\\tisb" : [c]"=r"(c) :: "memory"); c; })
static inline uint64_t ciocc_eval_rdtsc(void)  { return START_CYCLE_TIMER; }
static inline uint64_t ciocc_eval_rdtscp(void) { return STOP_CYCLE_TIMER; }
void ciocc_eval_rand_fill_buf(unsigned char* buf, int n)
{ for (int i = 0; i < n; ++i) buf[i] = rand(); }
void updateStats(const long idx) { (void)idx; }
void print_dynamic_hitcounts(const char* f) { (void)f; }
#else
'''
s = s.replace(anchor, arm, 1)
s = s.replace("#endif // EVAL_UTIL_H",
              "#endif // defined(__aarch64__)\n#endif // EVAL_UTIL_H")
pathlib.Path(w, 'eval_util.h').write_text(s)
print("    aarch64 branch added")
PY

  info "build libditctl.dylib (blanket-DIT arm selector + P-core QoS)"
  # Source lives in the repo, not the work dir: the previous CIO rig was lost
  # exactly because it lived only in an untracked home directory (overview.md 8b).
  cp -f "$REPO_ROOT/utils/cio_ditctl.c" "$WORK/ditctl.c" || die "cio_ditctl.c missing"
  clang -O2 -dynamiclib -o "$WORK/libditctl.dylib" "$WORK/ditctl.c" \
    || die "libditctl build failed"
fi

# ---------------------------------------------------------------- build
if want build; then
  info "build CIO's drivers against each arm  (their CFLAGS: -O0, unoptimised)"
  for b in $BENCHES; do
    src="$WORK/eval_$b.c"
    [[ -f "$src" ]] || { echo "  skip $b (no source)"; continue; }
    for a in $ARMS; do
      lab=${a%%:*}; rest=${a#*:}; arch=${rest%:*}
      ar="$LIBSODIUM/libsodium-$arch.a"
      [[ -f "$ar" ]] || { echo "  skip $b/$lab (no $ar)"; continue; }
      # -O0 and -fomit-frame-pointer are CIO's; -Werror is theirs too but their
      # code does not build clean under our clang, so it is dropped and said so.
      clang -fomit-frame-pointer -O0 -std=c18 -I"$INC" \
            -o "$OUT/eval_$b.$arch" "$src" "$ar" -lm 2>"$OUT/build_$b.$arch.log" \
        || { echo "  BUILD FAILED $b/$lab (see $OUT/build_$b.$arch.log)"; continue; }
    done
  done
fi

# ---------------------------------------------------------------- run
if want run; then
  # One message for the whole run, exactly as eval.sh does it (tr -dc '[:alnum:]').
  MSG="${MSG:-$(LC_ALL=C tr -dc '[:alnum:]' </dev/urandom | head -c 100)}"
  [[ ${#MSG} -eq 100 ]] || die "message is ${#MSG} chars, expected 100"
  echo "$MSG" > "$OUT/msg.txt"
  info "run: reps=$REPS msg=100 chars  arms=$(echo $ARMS | wc -w | tr -d ' ')"

  read -r -a ARR <<< "$ARMS"; NA=${#ARR[@]}
  : > "$OUT/parity.csv"
  echo "benchmark,arm,archive,rep,mean_ticks,n,dit_exit" >> "$OUT/parity.csv"
  for b in $BENCHES; do
    read -r NI NW <<< "$(iters_for "$b")"; EX="$(extra_for "$b")"
    any=0
    for r in $(seq 1 "$REPS"); do
      for k in $(seq 0 $((NA-1))); do
        a=${ARR[$(( (k + r - 1) % NA ))]}
        lab=${a%%:*}; rest=${a#*:}; arch=${rest%:*}; dit=${rest#*:}
        bin="$OUT/eval_$b.$arch"; [[ -x "$bin" ]] || continue
        cc="$OUT/.cc_$b.$lab.txt"
        ENABLE_DIT="$dit" DYLD_INSERT_LIBRARIES="$WORK/libditctl.dylib" \
          "$bin" "$NI" "$NW" "$MSG" $EX "$cc" 2>"$OUT/.err_$b.$lab" >/dev/null
        [[ -s "$cc" ]] || continue
        de=$(sed -n 's/.*DITCTL exit dit=\([01]\).*/\1/p' "$OUT/.err_$b.$lab" | tail -1)
        python3 - "$cc" "$b" "$lab" "$arch" "$r" "${de:-?}" >> "$OUT/parity.csv" <<'PY'
import sys
p,b,lab,arch,rep,de = sys.argv[1:7]
v=[float(x) for x in open(p).read().split('\n')[1:] if x.strip().isdigit()]
if v: print(f"{b},{lab},{arch},{rep},{sum(v)/len(v):.3f},{len(v)},{de}")
PY
        any=1
      done
    done
    [[ $any -eq 1 ]] && echo "  $b done" || echo "  $b produced nothing"
  done
fi

# ---------------------------------------------------------------- report
if want report; then
  info "CIO-parity results (mean of per-iteration counts, their statistic)"
  OUT="$OUT" python3 - <<'PY'
import csv, os, statistics as st, collections
rows=list(csv.DictReader(open(os.path.join(os.environ['OUT'],'parity.csv'))))
if not rows: raise SystemExit("no rows - run the 'run' stage first")
d=collections.defaultdict(list); dit=collections.defaultdict(set)
for r in rows:
    d[(r['benchmark'],r['arm'])].append(float(r['mean_ticks']))
    dit[(r['benchmark'],r['arm'])].add(r['dit_exit'])
arms=[]
for (_,a) in d:
    if a not in arms: arms.append(a)
order=['A','C','P','F','X','N']; arms=[a for a in order if a in arms]
benches=[]
for (b,_) in d:
    if b not in benches: benches.append(b)
print(f"\n{'benchmark':<30}{'arm':<5}{'mean':>12}{'vs A':>9}{'vs blanket':>12}{'dit@exit':>10}")
print('-'*80)
for b in benches:
    base=st.median(d[(b,'A')]) if (b,'A') in d else None
    bl=st.median(d[(b,'C')]) if (b,'C') in d else None
    for a in arms:
        if (b,a) not in d: continue
        m=st.median(d[(b,a)])
        va=f"{(m/base-1)*100:+.2f}%" if base else '-'
        vb=f"{(m/bl-1)*100:+.2f}%" if bl else '-'
        print(f"{b:<30}{a:<5}{m:>12.1f}{va:>9}{vb:>12}{'/'.join(sorted(dit[(b,a)])):>10}")
    print()
print("GATE: arm C must show dit@exit=1 and every other arm dit@exit=0.")
print("An arm exiting with DIT set that should not have leaked the mode past an")
print("unbalanced exit and is blanket in disguise -- discard the row, do not report it.")
PY
fi
