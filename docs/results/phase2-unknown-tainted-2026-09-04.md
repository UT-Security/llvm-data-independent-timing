# Phase 2, first experiment: flipping U1, U2 and U5 to "unknown means tainted"

Phase 2 of the 2026-09-03 plan is CIO's `make_top = Taint` applied to our
analysis: every place an UNKNOWN reads as CLEAN (`docs/design/taint-domain.md`
S5) is a candidate to flip, measure, and either keep or document. This is the
first three, chosen because they are one-line flips and because the round-5
residual (8,610 uncovered ops per resumption, 3,072 of them on one path in
`mbedtls_mpi_mul_mpi`) looked like where they might live.

## The flips

Three `cl::opt`s in `TaintAnalysis.cpp`, all default OFF, plus one accessor
`TaintState::anyMemTaint(K)` ("does the state hold ANY memory-resident
taint"):

| flag | S5 entry | what it changes |
|---|---|---|
| `-taint-unknown-load-tainted` | U1 | a load whose object is `CellInfo::Unknown` is Data-tainted whenever the state holds any memory-resident secret, not only when a pointee-tainted base, an AA-connected unknown store or a TOP bit reaches it |
| `-taint-no-mmo-load-tainted` | U2 | same, for a load with no memory operand at all |
| `-taint-call-result-pointee` | U5 | a call result that is Data-tainted is also Pointee-tainted |

With all three off the compiler is byte-identical to the merged `dit-tainter`
tip (ecp.c precision report identical to the PR #21 build's).

## Positive controls (the flags are live)

`u1(secret, p)`: secret into a frame cell, then `p[0] * 7` through an
unrelated public pointer. Base: `need=2` (the load reads clean). U1 flag:
`need=5` - the load and the multiply after it are Needs, and the emitted
code shows the enable moved to cover them.

`u5(key)` with an EXTERNAL callee handed the secret and returning a pointer:
`need=4` with and without the flag. The external call that received a secret
already sets TOP, so the load through the returned pointer was already
tainted. U5 is subsumed there.

## Whole-library result (mbedTLS 3.6.2, 727 seeds, 108 objects)

| arm | need | switches | objects differing from base |
|---|---|---|---|
| base | 107,888 | 2,865 | - |
| U1 | 107,894 | 2,865 | **0** |
| U2 | 107,888 | 2,865 | **0** |
| U5 | 107,892 | 2,865 | **0** |
| all three | 107,898 | 2,865 | **0** |

Ten extra Needs out of 107,888, every one inside a region that was already
covered, and not one object changes. **On a completely seeded build these
three unknowns essentially never read clean**: the existing rules (blunt TOP
after a secret-receiving external call, pointee-tainted bases, alias analysis
on located unknown stores) already reach them. No oracle run is needed - the
binaries are the same bytes. The 8,610 residual is NOT from U1, U2 or U5.

## U5 is the wrong lever for flowprobe C1

The in-TU control, the real C1 shape:

```c
static unsigned long *into(const unsigned long *k) { return (unsigned long *)k + 3; }
unsigned long c1(const unsigned long *key) { unsigned long *q = into(key); return q[0] * 11; }
```

`c1 need=1` with and without any flag. `into`'s return value carries POINTEE
taint (it is `k + 3` with `k` pointee-tainted), but the summary bit
`ReturnsTainted` tracks Data only, so `taintCallResultDefs` - where the U5
flip lives - is never called. Closing C1 needs a `ReturnsPointeeTainted`
summary bit carried through the fixed point, not a flag. (Here block
placement covered the load and the multiply incidentally: `need=1` is the
secret-passing call, and the block is On, so the leak is in what the
analysis KNOWS, not in what ran protected. That incidental cover is exactly
what the seed-round finding says cannot be relied on.)

## What this leaves for Phase 2

- U1 and U2: keep the flags as instruments (default off, byte-neutral,
  measured inert on mbedTLS at full seeding). Re-run on a workload where
  unresolved loads sit in tainted functions without a TOP or pointee path -
  hand-written heap code, not a library that calloc's through a wrapper the
  seeds cover.
- U5: replace the flag with a `ReturnsPointeeTainted` summary bit (fixed-point
  change, then measured on flowprobe C1/C5).
- U4 (the mod-set gate) is the remaining flip with a known cost, +51.20% on
  Bitcoin Core's `ConnectBlockAllEcdsa`. That is the real Phase 2 decision.
- The round-5 residual's 3,072 ops in `mbedtls_mpi_mul_mpi` are a reach
  question (one path not tainted), not an unknown-reads-clean question.

Tooling: the five library builds are in `gem5-DIT/benchmarks/tls_resume/
phase2_p2{base,u1,u2,u5,all}/` (ignored outputs); controls in the session
scratchpad were one-file C programs and are reproduced above.
