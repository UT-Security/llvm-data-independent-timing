#!/usr/bin/env bash
#
# Stage CIO's evaluation drivers for BOTH libsodium DIT rigs.
#
# WHY THIS EXISTS. taint_libsodium_sudo_run.sh reads CIO's drivers from $CIO_DIR
# and force-includes utils/cio_arm_shim.h, whose comments say its region hooks
# are "called by the timer macros in eval_util.h" -- but nothing in the repo ever
# put them there. The port lived only in an untracked ~/Documents/cio-eval on the
# machine that produced paper_experiments/09, which is the same way the original
# CIO rig was lost (docs/overview.md 8b). This script is that port, tracked.
#
# It ports eval_util.h THREE ways from one file, because the two rigs need
# different cycle sources and the same header serves both:
#
#   aarch64 + shim   the shim owns the counter reads. ONE kpc read per boundary
#                    feeds both the driver's (end - start) and the timed-region
#                    cycle/instruction accumulators. Used by the rooted run.
#   aarch64, bare    isb-bracketed CNTVCT_EL0, ~41.67 ns steps. Identical to the
#                    port in taint_cio_parity.sh, for the non-root rig.
#   x86              CIO's original rdtsc/rdtscp, untouched, behind #else.
#
# USAGE   utils/taint_cio_eval_setup.sh
# ENV     WORK=<dir>    output           (default ~/Documents/cio-eval)
#         CIO_SRC=<dir> local CIO clone  (default ~/Documents/cio; falls back to
#                                         curl from GitHub if absent)
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="${WORK:-$HOME/Documents/cio-eval}"
CIO_SRC="${CIO_SRC:-$HOME/Documents/cio}"
CIO_RAW="https://raw.githubusercontent.com/counter-optimization/cio/HEAD"

FILES="eval_util.h eval_ed25519.c eval_argon2id.c
       eval_chacha20_poly1305_encrypt.c eval_chacha20_poly1305_decrypt.c
       eval_aesni256gcm_encrypt.c eval_aesni256gcm_decrypt.c
       Makefile eval.sh process_eval_data.py libsodium.uarch_checker.config"

info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

mkdir -p "$WORK"

info "stage CIO sources -> $WORK"
for f in $FILES; do
  [[ -f "$WORK/$f.orig" ]] && continue
  if [[ -f "$CIO_SRC/$f" ]]; then
    cp -f "$CIO_SRC/$f" "$WORK/$f.orig"
  else
    curl -sSL --max-time 60 -o "$WORK/$f.orig" "$CIO_RAW/$f" || die "cannot obtain $f"
  fi
done
# eval_util.h is the one file the port GENERATES; everything else is verbatim.
for f in $FILES; do
  [[ "$f" == eval_util.h ]] && continue
  cp -f "$WORK/$f.orig" "$WORK/$f"
done
info "    $(ls "$WORK"/eval_*.c | wc -l | tr -d ' ') drivers, seeds $(grep -c . "$WORK/libsodium.uarch_checker.config") lines"

info "port eval_util.h (aarch64+shim / aarch64 / x86)"
WORK="$WORK" python3 - <<'PY' || die "eval_util.h port failed"
import os, pathlib
w = os.environ['WORK']
s = pathlib.Path(w, 'eval_util.h.orig').read_text()
anchor = "#define EVAL_UTIL_H_SEED 172812\n"
assert anchor in s, "anchor missing - upstream eval_util.h changed"
arm = anchor + r'''
#if defined(__aarch64__)
/* The only thing in here that cannot cross architectures is the cycle timer.
   Theirs is serialise-then-read / read-then-serialise. updateStats and
   print_dynamic_hitcounts exist so THEIR instrumented libsodium can count x86
   opcode hits; ours never calls them and the x86 asm will not assemble, so they
   are stubbed. */
# ifdef CIO_ARM_SHIM_H
/* ROOTED RIG (utils/taint_libsodium_sudo_run.sh). cio_arm_shim.h is
   force-included and owns the counter reads: one kpc_get_thread_counters() per
   boundary, feeding BOTH the driver's (end - start) and the timed-region
   cycle/instruction accumulators. It falls back to CNTVCT_EL0 by itself when
   kperf is unavailable, so the same source works without root.

   DO NOT add a second counter read here. Two reads per boundary put the
   instruction window and the cycle window on different spans, which is what
   once made the timed-region "IPC" read 12-14 on an 8-wide core -- see the
   read2 comment in cio_arm_shim.h. */
/* t0_timer, NOT t0_cyc: with -DCIO_SHIM_CHEAP_TIMER the shim differences a
   CNTVCT_EL0 read here and keeps the kperf reads OUTSIDE this window, taking
   the per-region instrumentation offset from 3234 cycles to 21. Without the
   flag t0_timer IS t0_cyc and this is the original kperf timing. Either way
   the shim exit line reports which, as timer=. */
#  define START_CYCLE_TIMER ({ cio_shim_region_begin(); cio_shim_t0_timer; })
#  define STOP_CYCLE_TIMER  ({ cio_shim_region_end(); })
# else
/* NON-ROOT RIG (utils/taint_cio_parity.sh). CNTVCT_EL0 is the only cycle-ish
   counter readable from user space on Apple silicon (PMCCNTR needs kperf and
   root). It counts TIME, ~41.67 ns per step; CIO's mean over 1000 iterations
   absorbs the quantisation, but absolute "cycles" are not cycles. */
#  define START_CYCLE_TIMER ({ uint64_t c=0; \
    __asm__ volatile("isb\n\tmrs %[c], cntvct_el0" : [c]"=r"(c) :: "memory"); c; })
#  define STOP_CYCLE_TIMER  ({ uint64_t c=0; \
    __asm__ volatile("mrs %[c], cntvct_el0\n\tisb" : [c]"=r"(c) :: "memory"); c; })
# endif
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
print("    three-way branch added")
PY

info "build libditctl.dylib (non-root blanket arm + P-core QoS)"
cp -f "$REPO_ROOT/utils/cio_ditctl.c" "$WORK/ditctl.c" || die "cio_ditctl.c missing"
clang -O2 -dynamiclib -o "$WORK/libditctl.dylib" "$WORK/ditctl.c" \
  || die "libditctl build failed"

info "done -- CIO_DIR=$WORK"
