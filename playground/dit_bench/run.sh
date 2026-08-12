#!/bin/sh
# DIT cost-model microbenchmarks. Requires FEAT_DIT hardware (Apple M-series;
# Neoverse V1/N2, Graviton3+). On a core without FEAT_DIT every `MSR DIT` here
# SIGILLs. Check first:
#     sysctl hw.optional.arm.FEAT_DIT     # macOS
#     grep -o dit /proc/cpuinfo | head -1 # Linux
#
# Results recorded in docs/results/dit-cost-model.md.
set -e
cd "$(dirname "$0")"
CC=${CC:-cc}

echo "### (a) toggle cost, in context ###"
$CC -O2 dit_toggle_bench.c -o /tmp/dit_toggle_bench && /tmp/dit_toggle_bench

echo
echo "### (b) steady-state dwell cost: ALU ###"
$CC -O2 dit_bench.c -o /tmp/dit_bench && /tmp/dit_bench

echo
echo "### (c) steady-state dwell cost: memory / prefetcher ###"
$CC -O2 dit_mem_bench.c -o /tmp/dit_mem_bench && /tmp/dit_mem_bench

echo
echo "### (d) end-to-end: firefox_convolve_int, whole-program DIT vs none ###"
echo "(identical code in both binaries; dit_ctor.c just sets PSTATE.DIT before main)"
$CC -O2 ../firefox_convolve_int.c -o /tmp/conv_off
$CC -O2 ../firefox_convolve_int.c dit_ctor.c -o /tmp/conv_on
for b in /tmp/conv_off /tmp/conv_on; do
  best=999
  for i in 1 2 3 4 5; do
    s=$(python3 -c "import subprocess,time; t=time.perf_counter(); subprocess.run(['$b','--iter','200','--width','512','--height','512','--kernel','5'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL); print(time.perf_counter()-t)")
    best=$(python3 -c "print(min($best,$s))")
  done
  echo "  $b: ${best}s (min of 5)"
done
