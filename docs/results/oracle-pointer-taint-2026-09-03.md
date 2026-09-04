# Oracle pointer taint: two bugs fixed, one genuine flow found (2026-09-03)

The gem5 shadow-taint oracle was over-counting "operations with a secret
operand", inflating the denominator of every coverage figure. One mechanism
was an oracle bug, fixed in `gem5-DIT/src/cpu/o3/commit.cc` (with role
accessors in `src/arch/arm/insts/mem64.hh`); a second hypothesis was tested
and found not to apply on this target; the remainder is a real information
flow in mbedTLS's legacy bignum and is a finding, not a patch.

## How it surfaced

Sub-block DIT placement (`-taint-dit-sub-block`, default OFF) sinks a block's
entry enable to its first Need. On the mbedTLS TLS 1.3 resumption workload it
exposed 262 "unprotected secret operations" in `mbedtls_mpi_mul_mod` that
block placement had covered by accident. All 262 were register moves of
`mbedtls_mpi *` POINTER arguments and one spill of the same - `orr x2, xzr, x3`
and friends - not secret values. The compiler had classified them address-class
and correctly declined to protect them. The oracle called them data.

## Bug 1: a store's address registers tainted the written memory

`data_consumed = src_data` for a store OR'd every source register together, so
a pointer carrying data taint (legitimately - it was read out of memory the
oracle considers secret) tainted whatever was stored THROUGH it, and the next
pointer read out of that memory carried it on. A self-sustaining cycle with no
secret in it. Fixed: only the stored VALUE register(s) feed memory taint,
identified by the ISA class's `dest`/`dest2` fields (never by srcRegIdx order,
which the existing comment correctly calls incidental); address registers feed
the secret-ADDRESS channel, exactly as the load path already did.

Trap hit on the first attempt: the DitCC gate pseudo-operand (`dit_reg::Dit`,
its own register class, not excluded by `trackable()`) sits in every tagged
store's source list. Taken as "the first non-integer source" it became the
data class, no real register ever matched, every store wrote memory untainted,
and taint died at the first store after a load - protected went from 14.2M to
2. It is now excluded by explicit comparison.

Effect (same binaries, psk_dhe_ke, per resumption):

| arm | uncovered old -> fixed | protected old -> fixed | over-protected old -> fixed |
|---|---|---|---|
| null | 20,372,928 -> 12,086,838 | - | - |
| pass3 | 1,079,780 -> 625,612 | 19,265,633 -> 11,438,358 | 6,353,088 -> 14,180,364 |
| blanket | 0 | 20,372,928 -> 12,086,838 | 6,834,924 -> 15,121,013 |

**40.7% of the per-resumption "secret operations" were this artifact.** (14.4%
on a full handshake; resumption is ECDH-heavy, where pointer traffic through
`mpi` structs is densest.) Coverage RATIOS barely moved (94.69% -> 94.81%):
the fix corrects the denominator, it does not change what the compiler
protects. Null's uncovered equals blanket's protected under the fix - the
consistency check. Data: `paper_experiments/10-mbedtls-session-ticket/data/
oracle_store_rule_fix.csv`. **Every coverage figure quoted before 2026-09-03
uses the old denominator.**

## Not a bug after all: store pairs

Hypothesis: `stp x8, x9, [x1]` set 16 bytes tainted if EITHER register was,
so a secret-derived `n` packed next to `p` in `{ size_t n; mpi_uint *p; }`
would mark the pointer secret - the mirror of the load-pair slicing the
oracle already had. Per-register slicing was implemented and the whole
oracle sweep rerun (`fixed2_oracle`): **every number identical to the
store-rule build, to the last digit.** The reason is in the provenance trace
itself: gem5's ARM decoder splits `stp` into two single-register micro-ops
(`strxi_uop x20, [ureg0]` / `strxi_uop x19, [ureg0, #8]`) before commit, so
no pair ever reaches the oracle and each half already carries its own
register's taint. The slicing code is kept (correct, unreachable on this
target, commented as such) and the hypothesis is recorded here so nobody
re-derives it.

## The genuine flow: allocation sizes derived from secret values

After bug 1 the `mbedtls_mpi_mul_mod` count fell only 262 -> 238. A
`DIT_ORACLE_TRACE_REG=3` provenance trace shows NO instruction in any caller
sets the pointer register tainted; it arrives tainted from above. The top
sources across the run are `calloc` (4,967 events, one load from the malloc
arena) and `_int_malloc` bin loads: **the allocator's metadata is tainted.**

Root, from `library/bignum.c`, `mbedtls_mpi_mul_mpi`:

```c
for (i = A->n; i > 0; i--) if (A->p[i - 1] != 0) break;   /* topmost nonzero limb of A */
for (j = B->n; j > 0; j--) if (B->p[j - 1] != 0) break;
MBEDTLS_MPI_CHK(mbedtls_mpi_grow(X, i + j));               /* -> calloc(i + j, 8) */
```

The leading-zero-limb trim makes the allocation size a function of the
SECRET values. The arena's top pointer advances by a tainted size, is stored
(correctly, under the fixed rule - the VALUE is tainted), and every pointer
the allocator returns afterwards is secret-derived; `&grp->T[k].X` and the
rest follow by pointer arithmetic. This is the documented non-constant-time
behaviour of the legacy `mbedtls_mpi_*` API (the `mbedtls_mpi_core_*` layer is
the constant-time one); it is a real flow with two real channels - allocation
address (cache) and the `!= 0` branch - and **neither is one PSTATE.DIT can
close.** The compiler classifies the resulting pointers address-class and
excludes them from placement, which is correct under the threat model; the
oracle tracks them as data-derived, which is also correct, because they are.
The two disagree by definition, not by bug.

Consequences:
- The residual "uncovered" count on this workload has a floor made of
  secret-derived POINTER traffic that no DIT placement can lower. Coverage
  percentages against the raw oracle count therefore understate what DIT can
  do; the honest denominator excludes the secret-address class, which the
  oracle already reports separately ("ops consuming a secret ADDRESS").
- Whether to declassify the allocator's return value in the harness is a
  measurement-methodology decision, not an oracle fix: libc is not the code
  under test, but the flow through it is real. Not done; left to the project
  lead.
- For the compiler: nothing to change. This is the address-class exclusion
  working as designed.

Tooling: `gem5-DIT/benchmarks/tls_resume/{compare_fixed_oracle.py,
trace_mulmod.py, trace_reg_callers.py}`; traces in
`benchmarks/tls_resume/fixed_oracle_sb/`.

## Addendum: what the uncovered residual actually is, and that the compiler already said so

Classified by PC -> function -> instruction (fixed oracle, pass3, per
resumption, 625,612 uncovered DIT-list ops):

| share | what |
|---|---|
| ~90% | `mbedtls_mpi_core_sub/add`, `add_sub_mpi`, `mbedtls_mpi_sub_abs`: `ldr` of secret limbs (38%), `subs/adds/csinc/madd` on them (42%), `str` of results (10%). **Genuine, DIT-coverable, running with DIT off.** |
| ~9% | libc memcpy/memset/calloc + `mbedtls_mpi_copy`: the secret-derived-pointer floor. |
| ~0.6% | ASN.1 / X.509 parsing of secret-derived bytes: small, real, the decrypt-then-parse flow. |

Mechanism: `bignum.c` and `bignum_core.c` have ZERO seeds and ZERO switches.
Taint enters `ecp.c` through the scalar seed, the first bignum call writes the
secret result through a pointer in a TU the pass cannot see, the pass never
learns the result is secret, and every later call looks public. Verified in
the binary: in `ecp_add_mixed` DIT is OFF before all 30 of its bignum calls.

**The compiler reported this and nobody asked it to.** `-taint-info-loss-report`
on `ecp.c` alone yields 124 records - 53 of them severity `UNSOUND` - with 35
pasteable repair lines naming the `bignum.c` API (`mbedtls_mpi_mul_mpi,*,
pointee` and 21 others). `benchmarks/tls_resume/build.sh` requested only the
precision report; it now requests the info-loss and seed reports on every arm.
The ECDHE gap of 2026-09-02 and this bignum gap would both have been named by
the report on the first build.

What the oracle can and cannot say: it gives every DIT-list instruction that
ran with a secret-derived operand and DIT clear, by PC - a superset. It cannot
by itself split a secret VALUE from a secret-derived POINTER (one data bit).
Function/instruction context resolves ~95% of the split here; the principled
fix is a pointer-ness dimension in the oracle (the same product-domain idea as
the Phase 1 compiler refactor), or a join with the compiler's
`-taint-uncovered-report` `secret-address` labels.

Next step (not done - it is a new measurement round): apply the 35 repair
lines (round 1, `bignum.c`), rebuild, rerun the report, expect it to name
`bignum_core.c` (round 2), rerun the oracle. This is the seed loop reaching
its fixpoint, as `docs/` describes for libsodium.

### The 53 UNSOUND sites, and what the frame-provenance flags do about them

The info-loss report's `UNSOUND` severity (the only record kind it calls an
UNDER-approximation) means: "a frame address is passed to the callee while
this frame holds a secret, but the pointer register carries no pointee taint,
so NOTHING is transferred and the callee is analysed clean." 53 such sites in
`ecp.c`; 22 of them in `ecp_mul_restartable_internal`, the scalar-multiply
driver passing its stack-allocated points down. This, not only the cross-TU
blindness, is why `ecp_add_mixed` runs its 30 bignum calls with DIT off.

The report's repair is "seed the callee directly on the argument that
receives the frame address, or re-enable per-object frame provenance". The
three provenance flags (`-taint-frame-addr-args`, `-taint-arg-provenance`,
`-taint-arg-pointee-args`) are all default OFF. Static diagnostic on `ecp.c`,
same seeds, flags off vs on:

| | UNSOUND | moderate | switches | TU need / covered | `ecp_add_mixed` need / covered |
|---|---|---|---|---|---|
| off (default) | 53 | 71 | 289 | 626 / 1,963 | 7 / 21 |
| on | 48 | 103 | 327 | 728 / 2,310 | 41 / 174 |

`ecp_add_mixed` goes from 8.9% to 74% covered, but 48 UNSOUND sites remain.
Consistent with `docs/design/frame-address-gap.md` (2026-09-02): A + B1 + B2
"close no leak by itself", a third mechanism is still unidentified. NOT
measured dynamically here. The order of attack the evidence supports: the 35
`bignum.c` repair seeds first (they reach the 90% residual directly, no
flag semantics involved), then the provenance flags, then the oracle.

## The seed round (2026-09-03): it fixed every named leak and opened a bigger one

`benchmarks/tls_resume/seed_round.sh` automates the loop: build, harvest the
info-loss report's repair lines, repeat. Four rounds took the seed set from 82
to 717 lines and put the bignum layer under instrumentation for the first time
(`bignum.c` 0 -> 413 switches, `bignum_core.c` 0 -> 88). Data:
`paper_experiments/10-mbedtls-session-ticket/data/seed_round.csv`, final seeds
`seed_round_r4.txt`.

Oracle, full handshake, same gem5: **every leak named above went to zero** -
`mbedtls_mpi_core_sub` 287,660 -> 0, `core_add` 118,823 -> 0, `add_sub_mpi`
82,273 -> 0, `sub_abs` 53,003 -> 0. And total uncovered went UP, 726,114 ->
1,161,229 (coverage 94.38% -> 91.03%), because ONE function went 0 ->
1,157,430: `ecp_mod_p256`, the P-256 fast reduction.

Mechanism, verified in the binaries. `ecp_mod_p256` is reached through the
`grp->modp` function pointer, so the analysis never sees taint enter it and it
has no switches of its own; it ran protected only because its caller,
`mbedtls_mpi_mul_mod`, holds DIT on across the `blr` (it still does, in both
builds). It calls exactly one function, `mbedtls_mpi_grow`. Round 1 seeded
`grow` (the report proposed it). Before: `grow` had no switches and passed DIT
through. After: `grow` is region-placed, 4 enables and 3 CLEARS, and returns
DIT off to a caller that entered with it on. The rest of the reduction runs
unprotected.

**The seed loop is not monotone in coverage.** Instrumenting a callee converts
"inherits the caller's DIT and runs whole" into "owns its DIT and clears on
exit", and if the caller's protection was inherited rather than analysed (an
indirect call edge, a frame-address gap), it is stripped. This is the
callee-ownership hazard `docs/design/dit-callee-ownership.md` describes; the
`AlwaysEnteredWithDIT` exception cannot fire because the analysis does not
know `ecp_mod_p256` is DIT-on (it is not tainted, per the analysis).

The fix this scenario was designed for is the callee-saved ABI
(`-ftaint-dit-abi`: restore the entry value at every exit, guarded clear when
provably entered on). It was retired on 2026-09-01 on PERFORMANCE grounds
("only helps in a configuration nobody should pick"). This is a SOUNDNESS
argument that decision did not have. Measured as a third arm
(`passfixabi`), results below.

The provenance-flags arm (round-4 seeds + `-taint-frame-addr-args
-taint-arg-provenance -taint-arg-pointee-args`) is a null result on the
oracle: 1,161,211 uncovered vs 1,161,229 for seeds alone on the full
handshake. The static gain (`ecp_add_mixed` 9% -> 74% covered, UNSOUND 255 ->
131) protected instructions that were already covered by inheritance at run
time, and the flags do not touch the `grow`-clears-DIT mechanism. Consistent
with `frame-address-gap.md`'s "closes no leak by itself".

### The ABI arm: 91.03% -> 99.99%

Round-4 seeds plus `-ftaint-dit-abi`, full handshake, fixed oracle:

| arm | uncovered | coverage | `ecp_mod_p256` |
|---|---|---|---|
| pass3 (82 seeds) | 726,114 | 94.38% | 0 (inherited) |
| round-4 seeds | 1,161,229 | 91.03% | 1,157,430 |
| round-4 + provenance flags | 1,161,211 | 91.03% | 1,157,430 |
| **round-4 + callee-saved ABI** | **1,859** | **99.99%** | **0** |
| blanket | 0 | 100% | 0 |

The mechanism is confirmed: every operation the seed round exposed was
inherited coverage stripped by an instrumented callee's exit clear, and
restoring the entry value on exit recovers all of it. Switch count under the
ABI is 946 against 2,811 for the same seeds without it (the after-call
re-asserts are gone by construction, as `docs/design/dit-abi.md` says).

This reframes the ABI. It was retired 2026-09-01 as a performance choice
that "only helps in a configuration nobody should pick". On a properly seeded
build it is the difference between 91% and 99.99% coverage, and without it
the seed loop is not monotone: any new seed can strip protection somewhere,
and only the oracle will say so. The cost side is unmeasured here (no timing
arm was run); the earlier measurement was "no measurable time change" for
non-LTO on M5.

### Round 5: the function-pointer targets, no ABI - 99.97%

The info-loss report cannot propose a seed for an edge it cannot see, and
`grp->modp` is an indirect edge. Round 5 = round-4 seeds + `ecp_mod_p*,0,
pointee` for all ten reduction functions (727 lines; the seed checker reports
714 applied, the 13 dead being libc declarations the harvest filter now
excludes). Full handshake, fixed oracle:

| arm | uncovered | coverage | over-protected |
|---|---|---|---|
| round-4 seeds | 1,161,229 | 91.03% | 23,736,004 |
| round-4 + ABI | 1,859 | 99.99% | 27,348,337 |
| **round-5 seeds, no ABI** | **3,799** | **99.97%** | **24,189,658** |
| blanket | 0 | 100% | 22,847,714 |

**Seed completeness substitutes for the ABI on this workload**, at lower
over-protection. The residual is 1,497 ops in `mbedtls_mpi_mul_mpi` (one
path not reached), the libc/allocator floor, and a few hundred loads in
ssl/psa glue. The rule that makes the loop monotone: grep the target for
function-pointer assignments in the secret path (`->[a-z_]+ = [a-z_0-9]+`)
and seed the targets; the report will never name them.

### Where the regression lives: the one-time precomputation, not the resumption

Per resumption (`(n2-n0)/2`), round-4 seeds are already at **99.84%**
(19,547 uncovered of 12.09M) and `ecp_mod_p256` contributes 727, not 1.16M.
The stripped coverage is confined to the FULL handshake: the first scalar
multiplication precomputes the comb table (`grp->T`, cached in the group) and
that one-time work is where `ecp_mod_p256` runs thousands of reductions under
DIT stripped by `grow`. Resumptions reuse the table. So on the workload the
experiment is about, the naive loop was already sound to 0.16%; the
full-handshake number is what exposed the mechanism.

Why "the caller re-asserts DIT after every call" did not save it: the
re-assert is emitted in INSTRUMENTED callers only. `ecp_mod_p256` is not
instrumented (no taint edge reaches it through the function pointer), so it
has no re-assert after its own call to `grow`; its caller `mbedtls_mpi_mul_mod`
does re-assert after the `blr` returns, but by then the reduction has already
run. An uninstrumented frame between two instrumented ones is exactly the hole
inherited coverage leaves, and round 5 closes it by instrumenting that frame.

## Final: per resumption, all arms (fixed oracle, psk_dhe_ke)

| arm | uncovered | protected | coverage | over-protected |
|---|---|---|---|---|
| pass3 (82 seeds) | 625,612 | 11,438,358 | 94.81% | 14,180,364 |
| round-4 seeds (717) | 19,547 | 12,047,930 | 99.84% | 15,886,265 |
| round-4 + provenance flags | 19,538 | 12,047,940 | 99.84% | 15,886,281 |
| round-4 + callee-saved ABI | 3,312 | 12,064,218 | 99.97% | 18,363,718 |
| **round-5: + `ecp_mod_p*` fn-ptr targets** | **8,610** | **12,058,868** | **99.93%** | **15,952,932** |
| blanket | 0 | 12,086,838 | 100.00% | 15,121,013 |

Data: `paper_experiments/10-mbedtls-session-ticket/data/oracle_seed_round.csv`.

**Verdict for the workload.** The seed loop, plus one rule the report cannot
supply (seed the indirect-dispatch targets), takes the pass from 94.81% to
99.93% per resumption at over-protection 5.5% above blanket's. The ABI
reaches 99.97% at 21% above blanket's. Neither is needed to reach the
allocator floor: round 5's 8,610 residual is 3,072 ops on one path in
`mbedtls_mpi_mul_mpi`, 2,806 of libc/allocator pointer traffic, and 1,744
limb loads in ssl/psa glue. Over-protection is only indicative across arms
(the binaries differ); coverage is the comparable number.

**What changed in the story since the morning.** The pass was reported at
94.69% coverage with a 1.08M-op residual "the pass cannot reach". After the
oracle fix (denominator -40.7%), the seed loop (every named leak to zero), and
the function-pointer rule (the one regression to zero), the residual is
8,610 and almost all of it is the floor. The compiler was sound throughout;
what was incomplete was the seed set, and the tool that names the missing
seeds was never being run.

## Correction (2026-09-04): the libsodium seed file was never broken

The Phase 0 seed checker reported 12 of 65 lines in
`gem5-DIT/benchmarks/crypto/libsodium_secret.txt` as naming functions that do
not exist (`stream_ref_ref`, `stream_ref_xor_ic_ref`,
`chacha20_encrypt_bytes_ref`), and a "corrected" file was written alongside.
That check was run on an UNPATCHED libsodium 1.0.21. The experiment 09 rig
(`gem5-DIT/benchmarks/taint_oracle/build_sodium.sh` and the cioparity build
scripts) applies a rename patch to `crypto_stream/chacha20/ref/chacha20_ref.c`
so that exactly those names exist, and the reason is not cosmetic:
`stream_ref` and `stream_ref_xor_ic` are `static` functions defined in FOUR
translation units (the ChaCha20 reference, the Salsa20 reference, and two
Dolbeau variants), and a seed matches by name in every TU that defines it.
Seeding the unpatched name would taint Salsa20's unused code (measured: +16
switches there) while the direct seed on the ChaCha20 core goes dead
(`chacha20_encrypt_bytes` need 297 -> 116).

So: the shipped file is correct for the rig it is used with; the "corrected"
file has been withdrawn; the seed checker must be run against the SOURCE THE
RIG BUILDS, not a pristine tarball; and the general limitation stands and is
worth fixing at the format level - **a name-keyed seed cannot address a
`static` whose name is reused across TUs.** A `file:function` or
TU-qualified seed key would remove the need for the rename patch.

Found by the experiment 02 rerun of 2026-09-04
(`paper_experiments/02-libsodium-signed-lookup/rerun-2026-09-04.md`).
