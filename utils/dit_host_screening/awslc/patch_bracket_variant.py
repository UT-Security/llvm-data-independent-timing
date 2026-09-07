#!/usr/bin/env python3
"""Experiment 14: the bracket variants, patched into AWS-LC's own DIT set/restore.

crypto/fipsmodule/cpucap/cpu_aarch64.c is where SET_DIT_AUTO_RESET does its work:
  armv8_get_dit:      mrs %0, s3_3_c4_c2_5        (read DIT)
  armv8_set_dit:      .inst 0xd503415f             (msr dit, #1)
  armv8_restore_dit:  .inst 0xd503405f             (msr dit, #0, only if it was 0 on entry)

  ditsb     Apple's recipe: `sb` (0xd50330ff) after the enable
  ditcount  the shipped bracket plus a counter: armv8_set_dit bumps OPENSSL_dit_entries (declared in
            cpucap/internal.h) so a patched speed tool can report bracket entries per benchmark row;
            the instructions are unchanged, so it is a census build, not a timing build
usage: patch_bracket_variant.py <aws-lc tree> ditsb|ditcount
"""
import sys, os
tree, v = sys.argv[1], sys.argv[2]
p = os.path.join(tree, 'crypto', 'fipsmodule', 'cpucap', 'cpu_aarch64.c')
s = open(p).read()
MRS, SET, CLR = '"mrs %0, s3_3_c4_c2_5"', '".inst 0xd503415f"', '".inst 0xd503405f"'
for a in (MRS, SET, CLR):
    assert s.count(a) == 1, f'{a} found {s.count(a)}x'
if v == 'ditsb':
    s = s.replace(SET, '".inst 0xd503415f\\n\\t.inst 0xd50330ff"')          # msr dit,#1 ; sb
elif v == 'ditcount':
    # one relaxed increment per bracket entry, before the read; the counter is a plain global because the
    # speed tool is single-threaded (the -threads rows are run with 1 thread by the census driver)
    assert s.count('uint64_t armv8_set_dit(void) {') == 1
    s = s.replace('uint64_t armv8_set_dit(void) {',
                  'uint64_t OPENSSL_dit_entries = 0;   // experiment 14 census: bracket entries\n'
                  'uint64_t armv8_set_dit(void) {\n  __atomic_fetch_add(&OPENSSL_dit_entries, 1, __ATOMIC_RELAXED);', 1)
    h = os.path.join(tree, 'crypto', 'fipsmodule', 'cpucap', 'internal.h'); t = open(h).read()
    anchor = 'OPENSSL_EXPORT uint64_t armv8_set_dit(void);'
    assert t.count(anchor) == 1
    t = t.replace(anchor, anchor + '\n\n// experiment 14 census: number of bracket entries so far (ditcount variant only).\nOPENSSL_EXPORT extern uint64_t OPENSSL_dit_entries;', 1)
    open(h, 'w').write(t)
else:
    raise SystemExit('variant: ditsb|ditcount')
s = s.replace('// Encoding of "msr dit, #1"', f'// experiment 14 variant {v}; was: Encoding of "msr dit, #1"', 1)
open(p, 'w').write(s)
print(f'cpu_aarch64.c: variant {v}')
