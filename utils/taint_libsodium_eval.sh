#!/usr/bin/env bash
#
# End-to-end libsodium evaluation for the interprocedural taint + PSTATE.DIT pass.
#
# Reproduces the CIO head-to-head rig from scratch: fetches libsodium, applies the
# CIO rename patch, builds whole-library bitcode, derives a CIO-parity taint seed
# file, runs the analysis at several DIT placement policies, and produces linkable
# archives plus a correctness check.
#
# This exists because the original rig lived in an untracked home directory and was
# lost. Everything here is reproducible from a clean machine.
#
# USAGE
#   utils/taint_libsodium_eval.sh                  # all stages
#   utils/taint_libsodium_eval.sh analyze archives # only these stages
#   utils/taint_libsodium_eval.sh --list           # show stages
#
# STAGES        fetch patch build bitcode seed analyze archives check report
#
# ENV
#   WORK=<dir>        working dir            (default ~/Documents/libsodium-<VER>)
#   LLVM_BIN=<dir>    built tools            (default <repo>/build/bin)
#   SODIUM_VER=<v>    libsodium version      (default 1.0.21 -- matches recorded numbers)
#   JOBS=<n>          make parallelism       (default: cpu count)
#
# REQUIRES  wllvm (pip3 install --user wllvm), curl, make, and a built clang/llc/opt.
#           AArch64 host with FEAT_DIT to *run* anything (Apple M-series).
#
set -uo pipefail

SODIUM_VER="${SODIUM_VER:-1.0.21}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LLVM_BIN="${LLVM_BIN:-$REPO_ROOT/build/bin}"
WORK="${WORK:-$HOME/Documents/libsodium-$SODIUM_VER}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)}"
TARBALL="libsodium-$SODIUM_VER.tar.gz"
URL="https://download.libsodium.org/libsodium/releases/$TARBALL"
CIO_CFG_URL="https://raw.githubusercontent.com/counter-optimization/cio/HEAD/libsodium.uarch_checker.config"

ALL_STAGES="fetch patch build bitcode seed analyze archives check report"

# DIT placement policies to build. label:flags
#
# NOTE (2026-09-01): the pass DEFAULTS changed on 2026-08-24 (commit 47d34937f5df,
# "[taint] Settle the shipped defaults"). They are now region / switch-cyc=30 /
# loop-hoist=1 / mod-set gate ON, so 'hardened' IS the shipped configuration and
# 'tuned' below is now a no-op duplicate of it -- kept only because the archive
# name is referenced by older result docs. The pre-2026-08-24 default
# (switch-cyc=0, loop-hoist=0) is the 'fine' policy: it is what produced the
# +46%/+94% libsodium numbers in docs/results/dit-cost-model.md, and it is now a
# historical arm, not a default.
#
# Override the whole set for a one-off study:
#   POLICIES_OVERRIDE="pass:-taint-dit-placement=region;func:-taint-dit-placement=function"
POLICIES=(
  "hardened:-taint-dit-placement=region"
  "tuned:-taint-dit-placement=region -taint-dit-switch-cyc=30 -taint-dit-loop-hoist=1"
  "func:-taint-dit-placement=function"
)
if [[ -n "${POLICIES_OVERRIDE:-}" ]]; then
  POLICIES=()
  IFS=';' read -r -a POLICIES <<< "$POLICIES_OVERRIDE"
  printf '\033[1m==> policy set overridden: %s\033[0m\n' "${POLICIES[*]}"
fi

red()  { printf '\033[31m%s\033[0m\n' "$*" >&2; }
info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die()  { red "ERROR: $*"; exit 1; }

[[ "${1:-}" == "--list" ]] && { echo "$ALL_STAGES"; exit 0; }
STAGES="${*:-$ALL_STAGES}"
want() { [[ " $STAGES " == *" $1 "* ]]; }

export PATH="$HOME/Library/Python/3.9/bin:$HOME/.local/bin:$PATH"
export LLVM_COMPILER=clang
export LLVM_COMPILER_PATH="$LLVM_BIN"

for t in clang llc opt llvm-link llvm-ar llvm-dis llvm-objdump; do
  [[ -x "$LLVM_BIN/$t" ]] || die "missing $LLVM_BIN/$t -- build it first: ninja -C build clang llc opt"
done

# A from-source clang on macOS cannot find the SDK by itself. Clang reads a default
# config file next to the binary; create it once so every wllvm-driven compile works.
if [[ "$(uname -s)" == "Darwin" && ! -f "$LLVM_BIN/clang.cfg" ]]; then
  if command -v xcrun >/dev/null; then
    printf -- '-isysroot %s\n' "$(xcrun --show-sdk-path)" > "$LLVM_BIN/clang.cfg"
    info "created $LLVM_BIN/clang.cfg (macOS SDK sysroot)"
  fi
fi

# ---------------------------------------------------------------- fetch
if want fetch; then
  info "fetch libsodium $SODIUM_VER"
  mkdir -p "$(dirname "$WORK")"
  if [[ ! -d "$WORK" ]]; then
    # The tarball always unpacks to libsodium-<VER>/, which need not match the
    # basename of WORK -- rename if the caller chose a different directory name.
    ( cd "$(dirname "$WORK")" && curl -sSL --max-time 300 -o "$TARBALL" "$URL" \
      && tar xzf "$TARBALL" ) || die "download/unpack failed"
    extracted="$(dirname "$WORK")/libsodium-$SODIUM_VER"
    if [[ "$extracted" != "$WORK" ]]; then
      [[ -d "$extracted" ]] || die "tarball did not unpack to $extracted"
      mv "$extracted" "$WORK" || die "could not rename $extracted -> $WORK"
    fi
  fi
  [[ -x "$WORK/configure" ]] || die "no configure script in $WORK"
  [[ -f "$WORK/cio.config" ]] || curl -sSL --max-time 60 -o "$WORK/cio.config" "$CIO_CFG_URL" \
    || die "could not fetch CIO seed config"
  info "    source: $WORK    seeds: $(grep -c . "$WORK/cio.config") lines"
fi

# ---------------------------------------------------------------- patch
# CIO seeds three symbols that only exist after THEIR rename patch. All three are
# statics in crypto_stream/chacha20/ref/chacha20_ref.c. Without this the seed file
# silently under-seeds (taint-annotate ignores unmatched names without warning).
if want patch; then
  info "apply CIO rename patch (chacha20_ref.c)"
  f="$WORK/src/libsodium/crypto_stream/chacha20/ref/chacha20_ref.c"
  [[ -f "$f" ]] || die "not found: $f"
  if grep -q 'chacha20_encrypt_bytes_ref' "$f"; then
    info "    already patched"
  else
    cp "$f" "$f.orig"
    # \b matters: stream_ref must NOT match inside stream_ref_xor_ic.
    perl -pi -e 's/\bstream_ref\b/stream_ref_ref/g;
                 s/\bstream_ref_xor_ic\b/stream_ref_xor_ic_ref/g;
                 s/\bchacha20_encrypt_bytes\b/chacha20_encrypt_bytes_ref/g' "$f"
    info "    renamed $(diff "$f.orig" "$f" | grep -c '^>') lines"
  fi
fi

# ---------------------------------------------------------------- build
# --disable-asm is REQUIRED: hand-written .S never enters the bitcode, so anything
# it contains would be invisible to the analysis. This is C-only libsodium.
if want build; then
  info "configure + build with wllvm (-O2, --disable-asm)"
  command -v wllvm >/dev/null || die "wllvm not found -- pip3 install --user wllvm"
  ( cd "$WORK"
    [[ -f Makefile ]] || ./configure CC=wllvm CFLAGS="-O2" \
        --disable-asm --disable-shared --disable-pie > config.log 2>&1 \
      || { tail -20 config.log >&2; exit 1; }
    make -j"$JOBS" > build.log 2>&1 || { tail -30 build.log >&2; exit 1; }
  ) || die "libsodium build failed (see $WORK/{config,build}.log)"
  cp -f "$WORK/src/libsodium/.libs/libsodium.a" "$WORK/libsodium.a.ORIG"
  info "    $(ls -la "$WORK/libsodium.a.ORIG" | awk '{print $5}') bytes"
fi

# ---------------------------------------------------------------- bitcode
if want bitcode; then
  info "extract + link whole-library bitcode"
  ( cd "$WORK"
    extract-bc src/libsodium/.libs/libsodium.a >/dev/null 2>&1 || exit 1
    "$LLVM_BIN/llvm-link" src/libsodium/.libs/libsodium.bca -o libsodium-whole.bc
  ) || die "bitcode extraction failed"
  "$LLVM_BIN/llvm-dis" -o "$WORK/libsodium-whole.ll" "$WORK/libsodium-whole.bc"
  info "    $(grep -c '^define' "$WORK/libsodium-whole.ll") functions defined"
fi

# ---------------------------------------------------------------- seed
# Two transformations vs CIO's config, both mandatory:
#  * pointer args must be typed ",pointee" -- ours distinguishes a secret VALUE from
#    a pointer to secret memory; transcribing literally floods secret-address reports.
#  * pre-flight against IR names. llvm-nm prints Mach-O names with a leading '_', so
#    checking against llvm-nm reports 21/21 MISSING on a perfectly good seed file.
if want seed; then
  info "derive CIO-parity seed file (pointee-typed)"
  # Stages are independently runnable: regenerate the disassembly if absent.
  if [[ ! -f "$WORK/libsodium-whole.ll" ]]; then
    [[ -f "$WORK/libsodium-whole.bc" ]] || die "no libsodium-whole.bc -- run the 'bitcode' stage first"
    "$LLVM_BIN/llvm-dis" -o "$WORK/libsodium-whole.ll" "$WORK/libsodium-whole.bc" \
      || die "llvm-dis failed"
  fi
  [[ -f "$WORK/cio.config" ]] || curl -sSL --max-time 60 -o "$WORK/cio.config" "$CIO_CFG_URL" \
    || die "could not fetch CIO seed config"
  python3 - "$WORK" <<'PY' || die "seed generation failed"
import re, sys, os
work = sys.argv[1]
sigs = {}
pat = re.compile(r'^define\s+.*?@([A-Za-z0-9_.]+)\((.*)$')
for line in open(os.path.join(work, 'libsodium-whole.ll')):
    if not line.startswith('define'): continue
    m = pat.match(line)
    if not m: continue
    name, rest = m.group(1), m.group(2)
    depth, cur, parts = 1, '', []
    for ch in rest:
        if ch in '(<[': depth += 1
        elif ch in ')>]':
            depth -= 1
            if depth == 0: break
        if ch == ',' and depth == 1: parts.append(cur); cur = ''
        else: cur += ch
    if cur.strip(): parts.append(cur)
    sigs[name] = [p.strip().split()[0] for p in parts if p.strip()]

seeds = [l.strip().split(',') for l in open(os.path.join(work, 'cio.config'))
         if l.strip() and not l.startswith('#')]
lines, ptr, val, bad = [], 0, 0, 0
for fn, idx in seeds:
    i = int(idx); s = sigs.get(fn)
    if s is None or i >= len(s):
        lines.append(f'# UNRESOLVED {fn},{idx}'); bad += 1; continue
    if s[i] == 'ptr': lines.append(f'{fn},{idx},pointee'); ptr += 1
    else:             lines.append(f'{fn},{idx}');         val += 1

hdr = [
 '# libsodium taint seed -- CIO parity. GENERATED by utils/taint_libsodium_eval.sh.',
 '# Source: counter-optimization/cio libsodium.uarch_checker.config.',
 '# Transformations vs CIO verbatim:',
 '#  1. chacha20_ref.c rename patch applied (CIO seeds post-patch names).',
 '#  2. Pointer args typed ",pointee": ours distinguishes secret DATA from a pointer',
 '#     to secret memory; CIO has no pointee concept (their TOP=Taint makes it moot).',
 '#  3. The four crypto_aead_*,8 lines are LIVE here -- arg 8 is a real pointer param.',
 '#     In CIO arg_index counts SysV GPR arg regs, capped at 5, so those lines are',
 '#     DEAD there and never seed the AEAD key. We are strictly more complete;',
 '#     record this when comparing recall against their alert set.',
 f'# {ptr} pointee / {val} value / {bad} unresolved.', '']
open(os.path.join(work, 'secret_m4_pointee.txt'), 'w').write('\n'.join(hdr + lines) + '\n')
print(f'    {ptr} pointee / {val} value / {bad} unresolved')
if bad: sys.exit(f'ERROR: {bad} seed lines unresolved -- analysis would silently under-seed')
PY

  info "annotate + verify attributes landed"
  "$LLVM_BIN/opt" -S "$WORK/libsodium-whole.bc" -passes=taint-annotate \
      -taint-src="$WORK/secret_m4_pointee.txt" -o "$WORK/libsodium-annotated.ll" \
    || die "taint-annotate failed"
  np=$(grep -o '"tainted-pointee"' "$WORK/libsodium-annotated.ll" | wc -l | tr -d ' ')
  nv=$(grep -o '"tainted"'         "$WORK/libsodium-annotated.ll" | wc -l | tr -d ' ')
  nf=$(grep -E '^define' "$WORK/libsodium-annotated.ll" | grep -cE '"tainted')
  info "    $np pointee + $nv data attrs across $nf functions"
  [[ "$nf" -eq 0 ]] && die "no attributes applied -- seeding is broken"
fi

# ---------------------------------------------------------------- analyze
if want analyze; then
  # -disable-tail-calls is REQUIRED, not a tuning choice, and it goes here so
  # every arm (baseline included) shares one codegen configuration.
  #
  # A tail call has no epilogue. If DIT is on when one is taken the mode is never
  # restored, so every instruction after it runs protected and the "selective"
  # arm silently becomes blanket-plus-switches. Measured on this library at the
  # shipped defaults: 13 SEVERE `leak-tailcall` sites, among them `crypto_sign`
  # (the ed25519 entry point) and the whole poly1305/chacha20 chain that AEAD
  # goes through. Turning tail calls off takes the information-loss report from
  # 36 records / 13 severe to 23 records / 0 severe, leaving exactly the 19
  # indirect + 4 cross-TU stops, which are real and unrelated.
  #
  # Since 61158c8a599e, -ftaint-harden ITSELF disables tail calls for any build
  # that goes through clang (it stamps the taint-no-tail-calls module flag; it is
  # no longer tied to -ftaint-dit-abi, which is not the shipped configuration).
  # That does NOT cover this rig: the wrapper flow lowers with llc, and nothing
  # stamps the attribute on that path, so the codegen option stays REQUIRED here.
  # See docs/overview.md section 3.
  info "lower to post-prologepilog MIR (tail calls disabled)"
  "$LLVM_BIN/llc" -O2 -disable-tail-calls -stop-after=prologepilog \
      "$WORK/libsodium-annotated.ll" \
      -o "$WORK/libsodium.pe.mir" || die "llc -stop-after=prologepilog failed"
  perl -0pi -e 's/<mcsymbol >//g' "$WORK/libsodium.pe.mir"   # MIR CFI serialization bug

  mkdir -p "$WORK/rpt"
  rm -f "$WORK/rpt/infoloss.txt"   # the report APPENDS; stale records would mix in
  for p in "${POLICIES[@]}"; do
    label="${p%%:*}"; flags="${p#*:}"
    info "analyze + insert DIT [$label] $flags"
    # shellcheck disable=SC2086
    "$LLVM_BIN/llc" -enable-new-pm -run-taint-interproc -taint-insert-dit $flags \
        $( [[ "$label" == hardened ]] && printf '%s ' \
             "-taint-output=$WORK/rpt/tainted.txt" \
             "-taint-regions-output=$WORK/rpt/regions.txt" \
             "-taint-callsite-report=$WORK/rpt/callsites.txt" \
             "-taint-uncovered-report=$WORK/rpt/uncovered.txt" \
             "-taint-clobber-report=$WORK/rpt/clobber.txt" \
             "-taint-info-loss-report=$WORK/rpt/infoloss.txt" ) \
        "$WORK/libsodium.pe.mir" -o "$WORK/libsodium.$label.mir" \
      || die "taint analysis failed for $label"
  done
fi

# ---------------------------------------------------------------- archives
if want archives; then
  info "emit objects + archives"
  # baseline: same MIR, no DIT insertion at all -> byte-comparable control
  "$LLVM_BIN/llc" -start-after=prologepilog "$WORK/libsodium.pe.mir" -filetype=obj \
      -o "$WORK/libsodium.baseline.o" || die "baseline object failed"
  "$LLVM_BIN/llvm-ar" rcs "$WORK/libsodium-baseline.a" "$WORK/libsodium.baseline.o"
  for p in "${POLICIES[@]}"; do
    label="${p%%:*}"
    "$LLVM_BIN/llc" -start-after=prologepilog "$WORK/libsodium.$label.mir" \
        -filetype=obj -o "$WORK/libsodium.$label.o" || die "object failed for $label"
    "$LLVM_BIN/llvm-ar" rcs "$WORK/libsodium-$label.a" "$WORK/libsodium.$label.o"
  done
fi

# ---------------------------------------------------------------- check
# Correctness gate. The BASELINE run is the control: it proves the whole-bitcode
# round-trip is lossless, so a hardened failure can be blamed on the pass itself.
if want check; then
  command -v wllvm >/dev/null || die "wllvm needed for make check"
  for label in baseline hardened; do
    info "make check against '$label' library"
    cp -f "$WORK/libsodium-$label.a" "$WORK/src/libsodium/.libs/libsodium.a"
    ( cd "$WORK/test/default" && rm -f ./*.trs ./*.log ) 2>/dev/null
    ( cd "$WORK" && env PATH="$PATH" LLVM_COMPILER=clang LLVM_COMPILER_PATH="$LLVM_BIN" \
        make check -j"$JOBS" > "check_$label.log" 2>&1 )
    pass=$(cat "$WORK"/test/default/*.trs 2>/dev/null | grep -c ':test-result: PASS')
    fail=$(cat "$WORK"/test/default/*.trs 2>/dev/null | grep -c ':test-result: FAIL')
    info "    PASS=$pass FAIL=$fail"
    [[ "${fail:-1}" -ne 0 || "${pass:-0}" -eq 0 ]] && die "$label failed make check (see $WORK/check_$label.log)"
  done
  cp -f "$WORK/libsodium.a.ORIG" "$WORK/src/libsodium/.libs/libsodium.a"
  info "    restored pristine archive into build tree"
fi

# ---------------------------------------------------------------- report
if want report; then
  info "static summary"
  base_text=$("$LLVM_BIN/llvm-size" -m "$WORK/libsodium.baseline.o" 2>/dev/null \
              | sed -n 's/.*__text): *\([0-9]*\).*/\1/p' | head -1)
  printf '\n  %-22s %10s %10s %9s\n' policy switches __text 'vs base'
  printf '  %-22s %10s %10s %9s\n' baseline 0 "${base_text:-?}" '-'
  for p in "${POLICIES[@]}"; do
    label="${p%%:*}"
    n=$("$LLVM_BIN/llvm-objdump" -d "$WORK/libsodium-$label.a" 2>/dev/null \
        | grep -cE 'msr[[:space:]]+DIT')
    t=$("$LLVM_BIN/llvm-size" -m "$WORK/libsodium.$label.o" 2>/dev/null \
        | sed -n 's/.*__text): *\([0-9]*\).*/\1/p' | head -1)
    d=$(python3 -c "print('%+.2f%%'%(($t-$base_text)/$base_text*100))" 2>/dev/null || echo '?')
    printf '  %-22s %10s %10s %9s\n' "$label" "$n" "${t:-?}" "$d"
  done
  if [[ -f "$WORK/rpt/infoloss.txt" ]]; then
    sev=$(grep -c 'severity  SEVERE' "$WORK/rpt/infoloss.txt")
    rec=$(grep -cE '^\[[0-9]+\]' "$WORK/rpt/infoloss.txt")
    printf '  %-22s %s records, %s SEVERE\n' 'info-loss report' "$rec" "$sev"
    [[ "$sev" -ne 0 ]] && red "  WARNING: $sev severe information-loss sites -- selective placement has degenerated to blanket somewhere. See $WORK/rpt/infoloss.txt"
  fi
  for r in callsites uncovered clobber; do
    [[ -f "$WORK/rpt/$r.txt" ]] && printf '  %-22s %s lines\n' "$r report" "$(grep -c . "$WORK/rpt/$r.txt")"
  done
  cat <<EOF

  Archives ready for benchmarking:
    $WORK/libsodium-baseline.a   (control, 0 switches)
$(for p in "${POLICIES[@]}"; do echo "    $WORK/libsodium-${p%%:*}.a"; done)

  Next: utils/taint_libsodium_bench.sh
EOF
fi

info "done: $STAGES"
