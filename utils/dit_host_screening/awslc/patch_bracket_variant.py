#!/usr/bin/env python3
"""Experiment 14: the bracket variants, patched into AWS-LC's own DIT set/restore.

crypto/fipsmodule/cpucap/cpu_aarch64.c is where SET_DIT_AUTO_RESET does its work:
  armv8_get_dit:      mrs %0, s3_3_c4_c2_5        (read DIT)
  armv8_set_dit:      .inst 0xd503415f             (msr dit, #1)
  armv8_restore_dit:  .inst 0xd503405f             (msr dit, #0, only if it was 0 on entry)

  ditsb     Apple's recipe: `sb` (0xd50330ff) after the enable.  SILICON ONLY --
            gem5 does not implement FEAT_SB (see cioparity/api_bracket.c), so on
            that path the barrier arm is ditisb.
  ditisb    `isb sy` after the enable: the barrier arm on gem5.
  ditnop    THE LAYOUT TWIN, gem5 only. Every one of the three mode instructions
            becomes an inert one of the same size: the read a `mov %0, xzr`, both
            writes a `nop`. Instruction count, call graph and branch structure are
            the bracket's; no mode ever changes and no system register is touched,
            which is what the inert-arm gates require (a `mrs DIT` that merely
            EXISTS decodes differently under the two switch models -- measured at
            0.35-0.45% on ed25519, cioparity/blanket_ctor.c). Because the read now
            always yields 0, the restore takes the same branch B takes, so the twin
            walks B's path with B's instruction count. It is the control B lacked
            on silicon, where the layout term had to be argued from the constancy
            of B - A across chunk sizes instead of measured. NOT neutral: HINT #0
            is not a real issue slot, and CLAUDE.md prices that at ~0.25% in the
            direction that UNDERSTATES the switch.
  ditcount  the shipped bracket plus a counter: armv8_set_dit bumps OPENSSL_dit_entries (declared in
            cpucap/internal.h) so a patched speed tool can report bracket entries per benchmark row;
            the instructions are unchanged, so it is a census build, not a timing build
usage: patch_bracket_variant.py <aws-lc tree> ditsb|ditisb|ditnop|ditcount
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
elif v == 'ditisb':
    s = s.replace(SET, '".inst 0xd503415f\\n\\tisb sy"')                    # msr dit,#1 ; isb sy
elif v == 'ditnop':
    s = s.replace(MRS, '"mov %0, xzr"')                                     # the read, inert
    s = s.replace(SET, '"nop"')                                             # the enable, inert
    s = s.replace(CLR, '"nop"')                                             # the clear, inert
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
    raise SystemExit('variant: ditsb|ditisb|ditnop|ditcount')
s = s.replace('// Encoding of "msr dit, #1"', f'// experiment 14 variant {v}; was: Encoding of "msr dit, #1"', 1)
open(p, 'w').write(s)
print(f'cpu_aarch64.c: variant {v}')
