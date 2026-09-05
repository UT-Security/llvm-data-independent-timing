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

**Done 2026-09-04:** the summary bit landed, with the rules it needs and a
return-gate fix that also closes C5 - `returns-pointee-2026-09-04.md`.

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

## U4: the mod-set gate (`-taint-no-modset-gate`), measured

U4 is the one flip with a known cost (+51.20% on Bitcoin Core's
`ConnectBlockAllEcdsa`). The flag already existed; no code change. Same
compiler, r5 seeds, gate on (base) vs off (U4):

| | base | U4 |
|---|---|---|
| need (static, all TUs) | 107,888 | 172,613 (+60%) |
| switches | 2,865 | 4,393 (+53%) |
| objects differing | - | 33 / 108 |
| functions newly instrumented | - | 79 |
| full handshake, uncovered | 3,799 | 2,819 |
| full handshake, over-protected | 24,189,658 | 24,215,037 (+0.1%) |

The static flood and the dynamic cost disagree by two orders of magnitude,
and the reason is where the flood went: `gcm` +16.7K need (the self-test),
`x509` +14K (subject-alt-name parsing), `aes` +7.2K (CBC/CFB/CTR modes),
`rsa` +2.6K (key generation), `ssl_msg` +11.8K. With the gate off, a callee
that was ever seen writing a secret clobbers at EVERY call site, so every
public-data path through a shared helper goes tainted - but on this flow
those paths mostly do not execute. On a workload where they do (Bitcoin
Core's block validation), that is the +51%.

**What U4 closed (980 ops on the full handshake):** ~60% is libc allocator
and memcpy traffic (`calloc` -233, `__memcpy_generic` -218, `_int_malloc`
-90, `__memset_zva64` -47) - the secret-derived-POINTER floor, now covered
only because callers stay DIT-on across those calls. ~20% is record-layer
loads (`mbedtls_ssl_prepare_handshake_record`, `mbedtls_ssl_read_record`,
`mbedtls_ssl_handle_message_type`, `psa_key_derivation_input_bytes`). No
secret-value arithmetic was among them.

**What U4 did not touch, and what it is:** 1,535 ops in
`mbedtls_mpi_mul_mpi`, 819 + 620 of them two instructions:
`cmp x19, x21` and `cmp x19, x20` - the `X == A` and `X == B` POINTER
comparisons at the top of the function, before the region's enable. The
compiler classifies a comparison of two addresses as address-class, not a
Need, which is correct; the oracle counts them because those pointers carry
allocator-derived taint (the genuine flow documented in
`oracle-pointer-taint-2026-09-03.md`). `classify_residual.py` put them in the
"value arithmetic" bucket because `subs` looks arithmetic - a limitation of
classifying by mnemonic, and the reason the oracle needs a pointer-ness
dimension. **Corrected reading of the round-5 residual: essentially all of
it is the pointer floor.** The compiler's coverage of secret VALUES on this
workload is complete at round 5.

Per-resumption oracle and the four-arm timing sweep (round-trip control,
blanket, gated pass, U4; both switch models; five path lengths) follow.

### U4 per resumption

| arm | uncovered | protected | coverage | over-protected |
|---|---|---|---|---|
| gated pass, r5 (base) | 8,610 | 12,058,868 | 99.93% | 15,952,932 |
| U4: no mod-set gate | 6,910 | 12,060,568 | 99.94% | 15,973,586 (+0.13%) |
| blanket | 0 | 12,086,838 | 100.00% | 15,121,013 |

U4 closes 1,700 per resumption: `calloc` -628, `__memcpy_generic` -304,
`psa_key_derivation_input_bytes` -161, `srv_recv` (the harness's pipe read)
-77, `free` -72, `_int_free_chunk` -70. Allocator floor and KDF input
handling; no secret-value arithmetic. What remains is `mbedtls_mpi_mul_mpi`
3,588 (the pointer comparisons), `__memcpy_generic` 552,
`ctr_drbg_update_internal` 496 (the seeded RNG churning its own state),
`_int_malloc` 310, `malloc_consolidate` 278. **Coverage-wise U4 is inert on
this workload: +0.01 points, all of it floor.** The cost side is the timing
sweep below.

### U4 cost, and what the sweep actually measured

Four arms under ONE compiler, `--resumptions 5 --kex dhe`, median over five
argv[0] path lengths (spreads 0.00-0.20%), both switch models. Data:
`paper_experiments/10-mbedtls-session-ticket/data/timing_u4.csv`.

| arm | renamed | serialising | DIT writes / run |
|---|---|---|---|
| round-trip control (empty seed, 0 switches) | +0.00% | +0.00% | 0 |
| blanket (same binary, DIT process-wide) | -1.40% | -1.24% | 0 |
| gated pass, r5 seeds | +2.90% | **+252.91%** | 12,109,416 |
| U4: no mod-set gate, r5 seeds | +4.53% | +253.32% | 12,113,991 |
| U4 over the gated pass | **+1.58%** | +0.12% | +4,575 |

**U4 verdict.** +1.58% on renamed hardware, within noise on serialising, for
+0.01 points of coverage that is entirely pointer floor. Not worth taking on
this workload. The +51% on Bitcoin Core is the same mechanism (every call
site clobbers, so shared helpers on public paths go tainted) on a workload
where those paths execute; here they mostly do not, so the static flood
(+60% need, 33 objects) costs 1.6% dynamically. Keep the gate on.

**What the sweep really found.** The 727-seed build that reaches 99.93%
coverage executes **12.1 million mode switches per five resumptions**: the
seeds reached the bignum core, whose inner loops toggle per iteration.
Against 74,880 for the 82-seed build (94.69%) and 1.36 million for the
ECDHE-seeded one, that is the price of the last five points of coverage:

| build | coverage / resumption | DIT writes / run | renamed | serialising |
|---|---|---|---|---|
| pass3, 82 seeds | 94.81% | 74,880 | +1.49% | +3.31% |
| pass3 + ECDHE | (full handshake 94.69%) | 1,364,408 | -1.49% | +31.26% |
| round-5, 727 seeds | 99.93% | 12,109,416 | +2.90% | +252.91% |
| blanket | 100% | 0 | -1.40% | -1.24% |

On renamed-switch hardware complete seeding costs 2.9% and blanket is still
4.3 points better. On serialising hardware complete seeding is a 3.5x
slowdown (IPC 2.13 -> 0.67) and blanket is 254 points better. The frontier
conclusion of experiment 10 stands in its sharpest form: on this workload
blanket beats every selective arm under both switch models by running
faster than the unprotected control, and the selective pass's only viable
configuration is renamed hardware. The switch implementation, not the
placement policy, decides the outcome; the placement policy decides only how
many switches there are to implement.
