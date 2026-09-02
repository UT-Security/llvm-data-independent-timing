#!/usr/bin/env python3
"""Audit an aarch64 archive for tail calls taken out of DIT-carrying functions.

WHY THIS IS REQUIRED AND NOT DECORATIVE. A tail call has no epilogue. If DIT is
set when one is taken the mode is never restored, so every instruction after it
runs protected and the "selective" arm silently becomes blanket-plus-switches --
faster-looking placement that is really blanket, which is the worst possible
failure mode for this experiment because it flatters the result.

libsodium had 13 such sites at the shipped defaults before the fix, crypto_sign
among them. -ftaint-harden now stamps taint-no-tail-calls TU-wide, but "the flag
was passed" is a different claim from "no DIT-on exit tail-calls", and only the
second one licenses any number this rig produces. musttail bypasses the option
in SelectionDAGBuilder and MachineOutlinerTailCall runs downstream of the pass,
so survivors are expected -- what must hold is that every survivor sits in a
function carrying no `msr DIT` at all, where there is no mode to fail to restore.

HOW A TAIL CALL IS IDENTIFIED. These are RELOCATABLE objects, so a branch to an
external symbol is encoded with offset 0 and llvm-objdump annotates it as a
branch to the *current* function plus four -- indistinguishable from a loop
unless relocations are read. The discriminator is the relocation type:
R_AARCH64_JUMP26 on a `b` is a tail call, R_AARCH64_CALL26 on a `bl` is an
ordinary call. Matching on the disassembly text alone silently reports zero tail
calls on a binary full of them, which is a false PASS on the one gate that must
never give one.

Exit status is 1 if any function has BOTH a DIT write and a tail call out.

  ./audit_tailcalls.py <archive-or-elf> [more...]
"""
import re, subprocess, sys, os, collections

OBJDUMP = os.environ.get("OBJDUMP",
    os.path.expanduser("~/Documents/llvm-data-independent-timing/build/bin/llvm-objdump"))

SYM = re.compile(r'^[0-9a-f]+\s+<(?P<name>[^>]+)>:\s*$')
INSN = re.compile(r'^\s*([0-9a-f]+):\s+([0-9a-f]{8})\s+(?P<mn>\S+)\s*(?P<ops>.*)$')
# llvm-objdump -r interleaves relocations directly after the instruction they
# apply to. The literal form is tab-indented and carries its own address first:
#     "\t\t000000000000002c:  R_AARCH64_JUMP26\trandombytes_buf"
# Omitting that address prefix makes this match nothing, which reports zero tail
# calls on a binary holding 163 of them -- a false PASS on the one gate that
# must never give one. Verified against the plain -O2 control, which must find
# tail calls for a hardened PASS to mean anything.
RELOC = re.compile(r'^\s+[0-9a-f]+:\s+R_AARCH64_(?P<kind>\w+)\s+(?P<sym>\S+)')


def audit(path):
    out = subprocess.run([OBJDUMP, "-d", "-r", path],
                         capture_output=True, text=True).stdout
    fn, dit, tails, last_b = None, False, [], False
    bad, survivors, nfn = [], [], 0

    def flush():
        nonlocal fn, dit, tails
        if fn is not None and tails:
            (bad if dit else survivors).append((fn, sorted(set(tails))))
        fn, dit, tails = None, False, []

    for line in out.splitlines():
        m = SYM.match(line)
        if m:
            flush(); fn = m.group("name"); nfn += 1; last_b = False; continue
        if fn is None:
            continue
        mr = RELOC.match(line)
        if mr:
            # JUMP26 is only emitted for an unconditional branch to another
            # symbol, i.e. a tail call. Guarded by last_b so a stray relocation
            # cannot be attributed to a non-branch.
            if mr.group("kind") == "JUMP26" and last_b:
                tails.append(mr.group("sym"))
            continue
        mi = INSN.match(line)
        if not mi:
            continue
        mn, ops = mi.group("mn").lower(), mi.group("ops")
        last_b = (mn == "b")             # exactly `b`, not bl / b.cond / br
        if mn == "msr" and re.match(r'\s*dit\s*,', ops, re.I):
            dit = True
    flush()
    return nfn, bad, survivors


def main(argv):
    rc = 0
    for path in argv:
        nfn, bad, survivors = audit(path)
        print(f"\n=== {path}  ({nfn} symbols) ===")
        if bad:
            rc = 1
            print(f"  FAIL {len(bad)} function(s) carry `msr DIT` AND tail-call out:")
            for fn, ts in bad:
                print(f"    {fn}  ->  {', '.join(ts)}")
        else:
            print("  PASS no DIT-carrying function tail-calls out")
        if survivors:
            print(f"  {len(survivors)} surviving tail call(s), all in functions with NO msr DIT:")
            for fn, ts in survivors:
                print(f"    {fn}  ->  {', '.join(ts)}")
        else:
            print("  0 surviving tail calls")
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]) if len(sys.argv) > 1 else print(__doc__) or 2)
