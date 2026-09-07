#!/usr/bin/env python3
"""Experiment 14: make `bssl speed` report Apple PMC cycles and retired instructions.

tool/speed.cc times each function with gettimeofday. This adds an isb-ordered read of
PMC0 (cycles) and PMC1 (instructions retired) around the same measured loop, SIGILL-probed
once so an unpatched kernel simply reports zeros, and prints them as "cycles" and
"instructions" in the JSON rows (and cycles/op, IPC in the text rows). The counters are
per-core: the driver checks the implied clock (cycles / microseconds) is plausible and
drops samples that spanned a migration.  usage: patch_speed_pmc.py <aws-lc tree>
"""
import sys, os
p = os.path.join(sys.argv[1], 'tool', 'speed.cc')
s = open(p).read()
if 'bm_pmc0' in s:
    print('speed.cc: already patched'); sys.exit(0)

def sub(a, b, count=1):
    global s
    n = s.count(a)
    assert n == count, f'anchor found {n}x, expected {count}: {a[:60]!r}'
    s = s.replace(a, b)

sub('#include <sstream>\n', '''#include <sstream>

// experiment 14: Apple PMC reads (PMC0 cycles, PMC1 instructions retired) from EL0, which
// a kernel patched with PMCR0_USEREN_EN allows. Reads are isb-ordered (a bare mrs floats
// above the code it measures), 48-bit, probed once SIGILL-safe: zero when unavailable.
#if defined(__aarch64__) && defined(__APPLE__)
#include <setjmp.h>
#include <signal.h>
static int g_pmc_ok = -1;
static sigjmp_buf g_pmc_jb;
static void bm_pmc_ill(int) { siglongjmp(g_pmc_jb, 1); }
static inline uint64_t bm_pmc0(void) { uint64_t v; __asm__ volatile("isb\\n\\tmrs %0, S3_2_c15_c0_0" : "=r"(v) : : "memory"); return v & ((1ULL << 48) - 1); }
static inline uint64_t bm_pmc1(void) { uint64_t v; __asm__ volatile("isb\\n\\tmrs %0, S3_2_c15_c1_0" : "=r"(v) : : "memory"); return v & ((1ULL << 48) - 1); }
static void bm_pmc_probe(void) {
  struct sigaction sa, old; memset(&sa, 0, sizeof(sa)); sa.sa_handler = bm_pmc_ill; sigaction(SIGILL, &sa, &old);
  if (sigsetjmp(g_pmc_jb, 1) == 0) { uint64_t a = bm_pmc0(), b = bm_pmc1(); g_pmc_ok = (a != 0 || b != 0); }
  else { g_pmc_ok = 0; }
  sigaction(SIGILL, &old, NULL);
}
static inline bool bm_pmc(uint64_t *c, uint64_t *i) {
  if (g_pmc_ok < 0) bm_pmc_probe();
  if (!g_pmc_ok) { *c = 0; *i = 0; return false; }
  *c = bm_pmc0(); *i = bm_pmc1(); return true;
}
#else
static inline bool bm_pmc(uint64_t *c, uint64_t *i) { *c = 0; *i = 0; return false; }
#endif
''')

sub('''  // us is the number of microseconds that elapsed in the time period.
  uint64_t us;
''', '''  // us is the number of microseconds that elapsed in the time period.
  uint64_t us;
  // experiment 14: PMC0 cycles and PMC1 instructions over the same period (0 if unavailable).
  // No default initialisers: speed.cc aggregate-initialises TimeResults ({num_calls, us}) and
  // the tool is C++11, where that would stop compiling; the aggregate zero-fills these two.
  uint64_t cycles;
  uint64_t instrs;
  void PrintPMC() const {
    if (cycles && num_calls)
      printf("  [%.1f cycles/op, %.1f instr/op, IPC %.2f]\\n",
             static_cast<double>(cycles) / static_cast<double>(num_calls),
             static_cast<double>(instrs) / static_cast<double>(num_calls),
             static_cast<double>(instrs) / static_cast<double>(cycles));
  }
''')

# text output: Print, PrintWithBytes, PrintWithPrimes
sub('''          (static_cast<double>(num_calls) / static_cast<double>(us)) * 1000000);
    }
  }

  void PrintWithBytes''', '''          (static_cast<double>(num_calls) / static_cast<double>(us)) * 1000000);
      PrintPMC();
    }
  }

  void PrintWithBytes''')
sub('''              static_cast<double>(us));
    }
  }

  void PrintWithPrimes''', '''              static_cast<double>(us));
      PrintPMC();
    }
  }

  void PrintWithPrimes''')
sub('''          (static_cast<double>(num_calls) / static_cast<double>(us)) * 1000000);
    }
  }

 private:''', '''          (static_cast<double>(num_calls) / static_cast<double>(us)) * 1000000);
      PrintPMC();
    }
  }

 private:''')

# JSON output, both overloads
sub('''    printf("}");
    first_json_printed = true;''', '''    if (cycles) {
      printf(", \\"cycles\\": %" PRIu64 ", \\"instructions\\": %" PRIu64, cycles, instrs);
    }
    printf("}");
    first_json_printed = true;''', count=2)

# the measured loop in TimeFunction
sub('''  start = time_now();
  uint64_t done = 0;
''', '''  start = time_now();
  uint64_t done = 0;
  uint64_t pmc_c0, pmc_i0;
  bm_pmc(&pmc_c0, &pmc_i0);
''')
sub('''  results->us = now - start;
  results->num_calls = done;
''', '''  results->us = now - start;
  results->num_calls = done;
  uint64_t pmc_c1, pmc_i1;
  results->cycles = 0; results->instrs = 0;
  // the PMCs are 48-bit counters: subtract modulo 2^48 so a wrap inside the window does not
  // read as a 2^64-sized delta (the reads themselves are masked to 48 bits)
  if (bm_pmc(&pmc_c1, &pmc_i1)) {
    results->cycles = (pmc_c1 - pmc_c0) & ((1ULL << 48) - 1);
    results->instrs = (pmc_i1 - pmc_i0) & ((1ULL << 48) - 1);
  }
''')
# the tool's one aggregate initialiser of TimeResults must name the two new fields:
# it is compiled with -Wmissing-field-initializers -Werror
sub('    const TimeResults results = {num_calls, us};', '    const TimeResults results = {num_calls, us, 0, 0};')
open(p, 'w').write(s)
print('speed.cc: PMC cycles/instructions added to TimeFunction and both output paths')
