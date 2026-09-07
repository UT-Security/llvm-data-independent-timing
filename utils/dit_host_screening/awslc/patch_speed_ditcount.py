#!/usr/bin/env python3
"""Experiment 14 census: make `bssl speed` report how many times the DIT bracket was entered during
each benchmark's timed loop. Applies on top of patch_speed_pmc.py, in the `ditcount` tree, whose
armv8_set_dit bumps OPENSSL_dit_entries (patch_bracket_variant.py ditcount). The JSON rows gain
"ditEntries" (the raw count over the timed loop); ditEntries / numCalls is bracket entries per call.
usage: patch_speed_ditcount.py <aws-lc tree>
"""
import sys, os
p = os.path.join(sys.argv[1], 'tool', 'speed.cc')
s = open(p).read()
assert 'bm_pmc(&pmc_c0, &pmc_i0);' in s, 'apply patch_speed_pmc.py first'
if 'dit_entries' in s:
    print('speed.cc: census patch already applied'); sys.exit(0)

def sub(a, b, count=1):
    global s
    assert s.count(a) == count, f'expected {count} of: {a[:70]!r}, found {s.count(a)}'
    s = s.replace(a, b)

# the field, after the PMC ones (aggregate-initialised, so no default initialiser)
sub('''  uint64_t cycles;
  uint64_t instrs;
  void PrintPMC() const {
    if (cycles && num_calls)''', '''  uint64_t cycles;
  uint64_t instrs;
  // experiment 14 census: bracket entries (armv8_set_dit calls) during the timed loop
  uint64_t dit_entries;
  void PrintPMC() const {
    if (num_calls)
      printf("  [%.2f bracket entries/op]\\n", static_cast<double>(dit_entries) / static_cast<double>(num_calls));
    if (cycles && num_calls)''')
sub('    const TimeResults results = {num_calls, us, 0, 0};', '    const TimeResults results = {num_calls, us, 0, 0, 0};')
# JSON, both overloads: always printed, a zero is the answer "this row never enters the bracket"
sub('''    if (cycles) {
      printf(", \\"cycles\\": %" PRIu64 ", \\"instructions\\": %" PRIu64, cycles, instrs);
    }
    printf("}");''', '''    if (cycles) {
      printf(", \\"cycles\\": %" PRIu64 ", \\"instructions\\": %" PRIu64, cycles, instrs);
    }
    printf(", \\"ditEntries\\": %" PRIu64, dit_entries);
    printf("}");''', count=2)
# the timed loop
sub('''  uint64_t pmc_c0, pmc_i0;
  bm_pmc(&pmc_c0, &pmc_i0);
''', '''  uint64_t pmc_c0, pmc_i0;
  bm_pmc(&pmc_c0, &pmc_i0);
  const uint64_t dit_e0 = OPENSSL_dit_entries;
''')
sub('''  results->cycles = 0; results->instrs = 0;
''', '''  results->cycles = 0; results->instrs = 0;
  results->dit_entries = OPENSSL_dit_entries - dit_e0;
''')
open(p, 'w').write(s)
print('speed.cc: ditEntries per row added (census)')
