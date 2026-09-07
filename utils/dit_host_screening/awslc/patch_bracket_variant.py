#!/usr/bin/env python3
"""Experiment 13: the bracket variants, patched into AWS-LC's own DIT set/restore.

crypto/fipsmodule/cpucap/cpu_aarch64.c is where SET_DIT_AUTO_RESET does its work:
  armv8_get_dit:      mrs %0, s3_3_c4_c2_5        (read DIT)
  armv8_set_dit:      .inst 0xd503415f             (msr dit, #1)
  armv8_restore_dit:  .inst 0xd503405f             (msr dit, #0, only if it was 0 on entry)

  ditsb     Apple's recipe: `sb` (0xd50330ff) after the enable
usage: patch_bracket_variant.py <aws-lc tree> ditsb
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
else:
    raise SystemExit('variant: ditsb')
s = s.replace('// Encoding of "msr dit, #1"', f'// experiment 13 variant {v}; was: Encoding of "msr dit, #1"', 1)
open(p, 'w').write(s)
print(f'cpu_aarch64.c: variant {v}')
