#!/bin/bash
# Reproduce experiment 01 from the committed sources.
#
# Two instruments, two hosts. The SILICON stages run on the Apple M5 that holds
# the Bitcoin Core tree and the three bench_bitcoin arms (the knobs live in an
# uncommitted patch to src/bench/wallet_create_tx.cpp there); the GEM5 stage
# runs on an aarch64 Linux host with gem5-DIT built. Each stage refuses to run
# on the wrong host rather than guessing.
#
#   ./reproduce.sh            every stage this host can run
#   ./reproduce.sh clang      rebuild the taint clang at the checked-out commit
#   ./reproduce.sh arms       rebuild the three bench_bitcoin arms with it        (macOS)
#   ./reproduce.sh sweep      the 20-rep, 8-point crossover sweep, ~1 h, EXCLUSIVE machine (macOS)
#   ./reproduce.sh ipc        the 10-rep, 4-point run ipc.md reads, ~25 min      (macOS)
#   ./reproduce.sh derive     Table 1 and the IPC tables from data/
#   ./reproduce.sh figures    figures/crossover.{pdf,png} from data/; asserts against table1.md
#   ./reproduce.sh gem5       build the gem5 arms, run coinsel / coinsel4 / sign  (Linux)
#   ./reproduce.sh flow       the two lanes in ONE flow under gem5, K swept, both
#                             switch models, 5 argv[0] offsets, ~2 h on 110 cores (Linux)
#   ./reproduce.sh flowfig    figures/gem5-flow-crossover.{png,pdf} from data/gem5/flow_*.csv
#
# Env: LLVM_BUILD  the taint clang build (default: ~/Documents/llvm-project/build-gfix on
#                  macOS, ~/Documents/llvm-data-independent-timing/build on Linux)
#      BTC         Bitcoin Core tree (default ~/Documents/bitcoin)
#      G5          gem5-DIT tree (default: this repo's gem5-DIT submodule)
#      BOOST_DIR   gem5 stage, first run only: a Boost CMake config dir, e.g.
#                  ~/Documents/boost-1.88-headers/lib/aarch64-linux-gnu/cmake/Boost-1.88.0
#      MPL         a python with matplotlib (default python3)
#      JOBS        build parallelism
#
# Every stage appends to data/provenance.txt: host, date, and the LLVM, Bitcoin
# Core and gem5-DIT commits it ran against. The Bitcoin Core commit the gem5
# arms are pinned to is BTC_PIN below; the silicon arms' tree is whatever the
# M5 holds, and the arms stage records its HEAD.
set -euo pipefail
E="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
R="$(cd "$E/../.." && pwd)"
RIG="$R/utils/dit_host_screening/btc"
BTC="${BTC:-$HOME/Documents/bitcoin}"
G5="${G5:-$R/gem5-DIT}"
MPL="${MPL:-python3}"
OS="$(uname -s)"
case "$OS" in
  Darwin) LLVM_BUILD="${LLVM_BUILD:-$HOME/Documents/llvm-project/build-gfix}"; JOBS="${JOBS:-$(sysctl -n hw.ncpu)}" ;;
  Linux)  LLVM_BUILD="${LLVM_BUILD:-$HOME/Documents/llvm-data-independent-timing/build}"; JOBS="${JOBS:-$(( $(nproc) * 3 / 4 ))}" ;;
  *) echo "unsupported host $OS" >&2; exit 1 ;;
esac
export LLVM_BUILD
# Bitcoin Core master, last commit of 2026-08-18: the day the first arm was
# built. The M5 tree's exact commit was not recorded; between this commit and
# 2026-09-03 master touched none of the coin-selection link closure except two
# lines in moneystr.cpp/time.cpp, and nothing in src/secp256k1.
BTC_PIN=15a7a4ed7c4d0952ce966087e55a9a3e2f28ec1d

STAGES="${*:-}"
if [[ -z "$STAGES" ]]; then
  case "$OS" in
    Darwin) STAGES="clang arms sweep ipc derive figures" ;;
    Linux)  STAGES="clang gem5 flow flowfig" ;;
  esac
fi
want() { [[ " $STAGES " == *" $1 "* ]]; }
info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
need_os() { [[ "$OS" == "$1" ]] || { echo "stage $2 runs on $1 only (this host: $OS)" >&2; exit 1; }; }
objdump_dit() { "$LLVM_BUILD/bin/llvm-objdump" -d "$1" | { grep -icE '\bmsr\b[[:space:]]+dit,' || true; }; }
provenance() {
  mkdir -p "$E/data"
  {
    printf '%s  stage=%s  host=%s (%s %s)\n' "$(date -u +%Y-%m-%dT%H:%MZ)" "$1" "$(hostname)" "$OS" "$(uname -m)"
    printf '    llvm-data-independent-timing %s\n' "$(git -C "$R" rev-parse HEAD)"
    printf '    clang: %s\n' "$("$LLVM_BUILD/bin/clang" --version | head -1)"
    [[ -d "$BTC/.git" ]] && printf '    bitcoin %s\n' "$(git -C "$BTC" rev-parse HEAD)"
    # -e, not -d: a submodule's .git is a FILE pointing into the parent .git dir,
    # so -d would silently drop the gem5 commit from the provenance record.
    [[ -e "$G5/.git" ]] && printf '    gem5-DIT %s\n' "$(git -C "$G5" rev-parse HEAD)"
  } >> "$E/data/provenance.txt"
}

if want clang; then
  info "rebuild the taint clang at $(git -C "$R" rev-parse --short HEAD)"
  ninja -C "$LLVM_BUILD" clang lld llvm-objdump llvm-ar llvm-ranlib llvm-nm
  "$LLVM_BUILD/bin/clang" --version | head -1
fi

if want arms; then
  need_os Darwin arms
  info "rebuild the three bench_bitcoin arms"
  grep -q BTC_BENCH_INPUTS "$BTC/src/bench/wallet_create_tx.cpp" || {
    echo "the sweep knobs (BTC_BENCH_INPUTS/CHAIN/SIGN) are not in $BTC/src/bench/wallet_create_tx.cpp;" >&2
    echo "they were an uncommitted patch on the M5 - restore it before rebuilding" >&2; exit 1; }
  for d in build-nodit-v2 build-gated-v2 build-nop-v2; do
    cache="$BTC/$d/CMakeCache.txt"
    [[ -f "$cache" ]] || { echo "no configured build dir at $BTC/$d (see README: -DENABLE_IPC=OFF, -DBUILD_BENCH=ON, C flags per arm)" >&2; exit 1; }
    cc="$(sed -n 's/^CMAKE_C_COMPILER:[A-Z]*=//p' "$cache")"
    [[ "$cc" == "$LLVM_BUILD"/* ]] || { echo "$d was configured with $cc, not $LLVM_BUILD - it would not carry the rebuilt pass" >&2; exit 1; }
    printf '  %-16s CC=%s\n' "$d" "$cc"
    printf '  %-16s CXX=%s\n' "" "$(sed -n 's/^CMAKE_CXX_COMPILER:[A-Z]*=//p' "$cache")"
    printf '  %-16s C_FLAGS=%s\n' "" "$(sed -n 's/^CMAKE_C_FLAGS:[A-Z]*=//p' "$cache")"
    printf '  %-16s CXX_FLAGS=%s\n' "" "$(sed -n 's/^CMAKE_CXX_FLAGS:[A-Z]*=//p' "$cache")"
    # ccache would serve objects compiled by the previous clang; the target
    # must be recompiled by the one just built.
    CCACHE_DISABLE=1 cmake --build "$BTC/$d" --clean-first --target bench_bitcoin -j"$JOBS"
  done
  info "static msr DIT counts (nodit 0, gated >0, nop 0 with HINT #0 in place)"
  for d in build-nodit-v2 build-gated-v2 build-nop-v2; do
    b="$BTC/$d/bin/bench_bitcoin"
    printf '  %-16s msr DIT=%s  hint #0=%s\n' "$d" "$(objdump_dit "$b")" \
      "$("$LLVM_BUILD/bin/llvm-objdump" -d "$b" | { grep -icE '\bhint\b[[:space:]]+#0' || true; })"
  done
  n="$(objdump_dit "$BTC/build-nodit-v2/bin/bench_bitcoin")"; g="$(objdump_dit "$BTC/build-gated-v2/bin/bench_bitcoin")"; p="$(objdump_dit "$BTC/build-nop-v2/bin/bench_bitcoin")"
  [[ "$n" == 0 && "$g" -gt 0 && "$p" == 0 ]] || { echo "arm switch counts are not nodit=0 / gated>0 / nop=0" >&2; exit 1; }
  provenance arms
fi

if want sweep; then
  need_os Darwin sweep
  info "20-rep crossover sweep, 8 knob points (nothing else may run on this machine)"
  BTC_SWEEP_INPUTS=1,4,10,25,50,100,200,400 BTC_OUT=wallet_sweep_20rep.csv \
    python3 "$RIG/run_wallet_sweep.py" 20 1
  cp "$RIG/wallet_sweep_20rep.csv" "$E/data/wallet_sweep_20rep.csv"
  provenance sweep
fi

if want ipc; then
  need_os Darwin ipc
  info "10-rep IPC run, 4 knob points (exclusive machine)"
  BTC_SWEEP_INPUTS=1,4,100,400 BTC_OUT=wallet_m5_ipc.csv \
    python3 "$RIG/run_wallet_sweep.py" 10 1
  cp "$RIG/wallet_m5_ipc.csv" "$E/data/wallet_m5_ipc.csv"
  provenance ipc
fi

if want derive; then
  info "Table 1 and the IPC tables (also written under data/derived/)"
  mkdir -p "$E/data/derived"
  python3 "$RIG/table_wallet_sweep.py" "$E/data/wallet_sweep_20rep.csv" | tee "$E/data/derived/table_wallet_sweep.txt"
  python3 "$RIG/table_wallet_sweep.py" "$E/data/wallet_sweep_20rep.csv" --latex > "$E/data/derived/table_wallet_sweep.tex"
  python3 "$RIG/ipc_wallet_sweep.py" "$E/data/wallet_m5_ipc.csv" | tee "$E/data/derived/ipc_wallet_sweep.txt"
fi

if want figures; then
  info "Figure 1 (fails if data/ no longer matches table1.md - update the table first)"
  "$MPL" "$E/figures/plot_crossover.py" "$E/data/wallet_sweep_20rep.csv"
fi

if want gem5; then
  need_os Linux gem5
  [[ "$(uname -m)" == aarch64 ]] || { echo "gem5 stage needs an aarch64 host (native static build)" >&2; exit 1; }
  info "gem5: Bitcoin Core at the pinned commit"
  if [[ ! -d "$BTC/.git" ]]; then
    git clone --filter=blob:none --no-checkout https://github.com/bitcoin/bitcoin.git "$BTC"
    git -C "$BTC" checkout --quiet "$BTC_PIN"
  fi
  head="$(git -C "$BTC" rev-parse HEAD)"
  [[ "$head" == "$BTC_PIN" ]] || echo "  NOTE: $BTC is at $head, not the pinned $BTC_PIN"
  CFG="${CFG:-$BTC/build-gem5cfg/src}"
  if [[ ! -f "$CFG/bitcoin-build-config.h" ]]; then
    info "gem5: configure Bitcoin Core once for its generated bitcoin-build-config.h"
    : "${BOOST_DIR:?set BOOST_DIR to a Boost CMake config dir (headers only; e.g. the extracted libboost-dev package)}"
    cmake -B "$BTC/build-gem5cfg" -S "$BTC" -G Ninja \
      -DCMAKE_C_COMPILER="$LLVM_BUILD/bin/clang" -DCMAKE_CXX_COMPILER="$LLVM_BUILD/bin/clang++" \
      -DCMAKE_BUILD_TYPE=Release -DBoost_DIR="$BOOST_DIR" \
      -DBUILD_BITCOIN_BIN=OFF -DBUILD_DAEMON=OFF -DBUILD_CLI=OFF -DBUILD_TESTS=OFF -DBUILD_TX=OFF \
      -DBUILD_UTIL=OFF -DBUILD_BENCH=OFF -DENABLE_WALLET=OFF -DENABLE_IPC=OFF -DWITH_ZMQ=OFF \
      -DENABLE_EXTERNAL_SIGNER=OFF -DBUILD_KERNEL_LIB=OFF > "$BTC/build-gem5cfg-configure.log"
  fi
  info "gem5: build the arms (sign: 4 arms; coinsel: base + blanket)"
  [[ -d "$G5/benchmarks/bitcoin" ]] || { echo "no gem5-DIT at $G5 - run: git submodule update --init gem5-DIT (or set G5)" >&2; exit 1; }
  BTC="$BTC" RIG="$RIG" "$G5/benchmarks/bitcoin/build_btc_arms.sh"
  BTC="$BTC" CFG="$CFG" "$G5/benchmarks/bitcoin/build_coinsel_arms.sh"
  info "gem5: coinsel 1/1/1, coinsel4 1/1/4, sign --iter 32 (the arguments ipc.md records), 5 argv[0] offsets each"
  OUT="${GEM5_OUT:-$HOME/Documents/dit-browser-bench/gem5-btc}"
  python3 "$RIG/btc_gem5.py" --bench coinsel --configs spec,serdit --iter 1 --warmup 1 --targets 1 --offsets 5 --resume --jobs 20 --out "$OUT" --tag coinsel_repro &
  python3 "$RIG/btc_gem5.py" --bench coinsel --configs spec,serdit --iter 1 --warmup 1 --targets 4 --offsets 5 --resume --jobs 20 --out "$OUT" --tag coinsel4_repro &
  python3 "$RIG/btc_gem5.py" --bench sign    --configs spec,serdit --iter 32 --offsets 5 --resume --jobs 40 --out "$OUT" --tag sign_repro &
  wait
  mkdir -p "$E/data/gem5"
  cp "$OUT/coinsel_repro/btc_gem5_coinsel.csv"  "$E/data/gem5/coinsel_repro.csv"
  cp "$OUT/coinsel4_repro/btc_gem5_coinsel.csv" "$E/data/gem5/coinsel4_repro.csv"
  cp "$OUT/sign_repro/btc_gem5_sign.csv"        "$E/data/gem5/sign_repro.csv"
  provenance gem5
fi

if want flow; then
  need_os Linux flow
  [[ "$(uname -m)" == aarch64 ]] || { echo "flow stage needs an aarch64 host (native static build)" >&2; exit 1; }
  CFG="${CFG:-$BTC/build-gem5cfg/src}"
  [[ -f "$CFG/bitcoin-build-config.h" ]] || { echo "run the gem5 stage first (it configures Bitcoin Core)" >&2; exit 1; }
  info "flow: build the arms (reuses the gem5 stage's objects)"
  [[ -d "$G5/benchmarks/bitcoin" ]] || { echo "no gem5-DIT at $G5 - run: git submodule update --init gem5-DIT (or set G5)" >&2; exit 1; }
  BTC="$BTC" CFG="$CFG" RIG="$RIG" "$G5/benchmarks/bitcoin/build_flow_arms.sh"
  info "flow: K = 0,1,4,10,25,50,100,200,400 x base/blanket/taint x spec/serdit x 5 offsets"
  OUT="${GEM5_OUT:-$HOME/Documents/dit-browser-bench/gem5-btc}"
  python3 "$RIG/btc_flow_gem5.py" --offsets 5 --jobs "${FLOW_JOBS:-110}" --out "$OUT" --tag flow --resume
  mkdir -p "$E/data/gem5"
  cp "$OUT/flow/flow_runs.csv"    "$E/data/gem5/flow_runs.csv"
  cp "$OUT/flow/flow_derived.csv" "$E/data/gem5/flow_derived.csv"
  provenance flow
fi

if want flowfig; then
  info "flow figure from data/gem5/flow_derived.csv"
  "$MPL" "$RIG/fig_flow_gem5.py" "$E/data/gem5/flow_derived.csv" "$E/figures"
fi
info "done: $STAGES"
