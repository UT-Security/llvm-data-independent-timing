# SQLCipher: there is no prize. A definitive negative.

> **RETRACTION 2026-08-12 (same day).** This doc first reported **+8.15%** of
> recoverable headroom and called it the project's first positive result on a
> real workload. **That was wrong.** The oracle wrapped only 2 of the provider's
> 3 secret-touching entry points - it missed the per-page **HMAC**. So `blanket`
> paid DIT on software HMAC-SHA that the oracle simply skipped, and the gap was
> counted as "fine-grained wins" when it was really "fine-grained protects less".
> With all 3 wrapped, headroom is **+0.89%** on libtomcrypt and **-0.08%
> (i.e. zero)** on the shipping OpenSSL build. Corrected tables below; the
> original numbers are kept only to show what the error looked like.

**Measured 2026-08-12**, Apple M5, native. 100 paired reps, interleaved and
rotated across five arms. All arms produce identical query checksums
(`26611496`). Metric is the driver's own ROI timer (the query loop), not wall
clock. Machine CoV 0.5-1.3%.

## Numbers

| arm | ROI median | `MSR DIT` sites |
|---|---|---|
| `plain` (straight -O2) | 14.46 ms | 0 |
| `blanket` (-O2 + DIT set before main) | 15.73 ms | 1 |
| `nodit` (taint pipeline, DIT insertion off) | 14.45 ms | 0 |
| `region` (taint placement, shipped default) | 22.66 ms | 76 |
| `hoist` (taint placement + `-taint-dit-loop-hoist=1`) | 19.88 ms | 71 |

| comparison | mean | 95% CI | reps slower |
|---|---|---|---|
| **always-on DIT** (`plain -> blanket`) | **+8.89%** | +/-0.21 | 100/100 |
| **fine-grained** (`nodit -> region`) | **+56.97%** | +/-0.43 | 100/100 |
| **fine + hoist** (`nodit -> hoist`) | **+37.81%** | +/-0.43 | 100/100 |
| hoist vs region | -12.20% | +/-0.19 | 0/100 |
| pipeline artifact (`plain -> nodit`) | **+0.06%** | +/-0.22 | 52/100 |

Compare placement variants to `nodit`, never to `plain` - though on this
workload it happens not to matter, because the round-trip artifact is a clean
zero. It was +0.58% on QuickJS, so it is **workload-dependent** and must be
measured per workload rather than assumed either way.

## The result

**The prize is the largest this project has measured: +8.89%.** Always-on DIT on
a real encrypted-database workload costs nearly 9%, versus ~1% on QuickJS+Octane
and 2.1-2.6% on browsers. There is genuinely something worth recovering here.

**Fine-grained placement costs 6.4x MORE than always-on.** Not 6.4x more than the
prize - 6.4x more than simply setting the bit process-wide and walking away.

**Loop hoisting is a real but insufficient improvement**: -12.20% (0/100 reps
slower, so completely consistent), yet still 4.3x worse than blanket.

## Why, and why hoisting cannot fix it

Taint precision is **not** the problem. In `sqlite3.c` (262,970 lines) the key
reaches exactly **2 functions** and **11 `MSR DIT`**, with no
context-insensitive spread into SQLite's core (see
`docs/design/context-insensitivity.md`). The annotation is tight and correct.

The problem is **granularity, and it is interprocedural**. `cbc_encrypt`'s loop
dispatches `ecb_encrypt` through the `cipher_descriptor[]` function-pointer table
**once per 16-byte block**, so a 4 KB page becomes ~256 DIT regions each wrapping
~300-500 cycles of AES. The independently measured crossover
(`docs/results/quickjs.md`) is **~1300 cycles of work per region**; SQLCipher sits
3-4x below it.

`-taint-dit-loop-hoist=1` hoists toggles out of loops **within a function**. It
removed only **5 switches (76 -> 71)**, because the toggles that matter live at
the entry and exit of `rijndael_ecb_encrypt`, a *callee*. No intraprocedural
transform can remove a toggle placed at a callee boundary reached through an
indirect call.

## What would actually fix it

Interprocedural hoisting: enter DIT once in `cbc_encrypt` before the block loop
and let the callees inherit it, rather than toggling per call. The machinery
already exists in the opposite direction - `AlwaysEnteredWithDIT`
(`docs/design/dit-callee-ownership.md`) proves a callee is entered with DIT
already set and suppresses redundant re-asserts. Applying that reasoning to
*place* the enable at the caller is the missing piece.

The obstacle is the same one that blocks the re-assert optimisation: the call is
**indirect**, through `cipher_descriptor[]`, so the pass cannot prove which callee
runs. That makes SQLCipher a concrete, motivating case for the deferred runtime
`MRS` mode (`MRS DIT` = 1.00 cyc vs `MSR DIT` = 30.34 cyc measured), which is the
only mechanism that fixes indirect and cross-TU calls.

## Standing conclusion

SQLCipher is the project's strongest negative, and it is a **policy** failure,
not a technique failure. Precision is excellent, the prize is large, and the
placement policy loses by 4-6x purely because it cannot make regions coarse
enough through an indirect call made 256 times per page.


---

## CORRECTED RESULTS (oracle wraps cipher + kdf + hmac), 100 paired reps each

### libtomcrypt (software AES + software HMAC) - the DEPRECATED provider

| comparison | mean | 95% CI | reps slower |
|---|---|---|---|
| always-on (`plain -> blanket`) | +8.81% | +/-0.21 | 100/100 |
| **oracle dwell** (`plain -> oracle2`) | **+7.85%** | +/-0.23 | 100/100 |
| **headroom** (`oracle2 -> blanket`) | **+0.89%** | +/-0.19 | 85/100 |

### OpenSSL (hardware AES) - the DEFAULT, shipping provider

| comparison | mean | 95% CI | reps slower |
|---|---|---|---|
| always-on (`plain -> blanket`) | +1.76% | +/-0.39 | 79/100 |
| oracle dwell (`plain -> oracle2`) | +1.87% | +/-0.44 | 77/100 |
| **headroom** (`oracle2 -> blanket`) | **-0.08%** | +/-0.38 | **48/100 = zero** |

## The finding

**Protecting the secret correctly costs what protecting everything costs.**
libtomcrypt: oracle +7.85% vs blanket +8.81%. OpenSSL: oracle +1.87% vs blanket
+1.76% - the oracle is marginally *worse*.

Almost all of always-on's cost is DIT **on the crypto itself**, which any correct
fine-grained placement must also pay. SQLite's public B-tree/pager code is barely
DIT-sensitive, consistent with the trap-4 finding that real scattered-query
descent costs +1.3%, not the +9.5% a repeating probe suggested.

**So SQLCipher has no recoverable headroom at any placement quality.** This is not
a policy failure - it is the absence of a prize.

## Why hardware AES removes even the 0.89%

libtomcrypt's round function is `Te0(byte(s0,3)) ^ Te1(...) ^ ...` - **table
lookups indexed by data**, 4x ~1 KB L1-resident tables, hammered 16x per block.
That is exactly the shape DMP/LVP/comp-simp accelerate, so DIT has real
optimizations to disable and costs a lot.

OpenSSL's `_aes_v8_encrypt` is `aese.16b`/`aesmc.16b` - one instruction per round,
**zero data-dependent loads**. Nothing for DIT to switch off.

**The headroom is not a property of the application. It measures how much
DIT-gated optimization the code was getting.**

Corollary worth stating in any writeup: software AES's actual vulnerability is
the **T-table cache-timing channel**, which is address-domain and therefore
**not covered by DIT** at all (`docs/reference/dit-spec.md`). So on the software
path DIT is expensive *and* ineffective; on the hardware path it is unnecessary
because `AESE` is already constant-time. **AES is close to the worst possible
motivating workload for this project.** The value is where secrets flow through
general-purpose code never designed to be constant-time.


---

## gem5 corroboration (2026-08-13): toggle cost isolated

Full study: `gem5-DIT/docs/dit/studies/sqlcipher-dit-placement-2026-08-13.md`.

gem5 can model `MSR DIT` two ways - serializing (what ARM silicon does) or as a
renamed CC-register write (the fork's default, and the design being argued for).
Running the identical binary from the identical checkpoint under both isolates
**toggle cost with dwell held constant**, which silicon cannot do:

| placement | `MSR DIT` sites | toggle cost |
|---|---|---|
| oracle (hand-placed) | 6 | **+0.08%** |
| `-taint-dit-loop-hoist=1` | 54 | **+12.8%** |
| region (shipped default) | 63 | **+19.1%** |

**It reproduces the silicon ordering**: M5 measured region +54.3% / hoist +35.8%,
a 1.52x ratio; gem5 gives +19.1% / +12.8%, a 1.49x ratio - same ordering, same
ratio, about a third the magnitude. So the granularity result no longer rests on
one machine.

**And it measures the prize.** The total value of every DIT-gated optimization on
this ROI is **~1.4%**, carried entirely by value prediction (DMP, SIP and
comp-simp are inert or negative here). So the shipped placement spends **19% to
protect something worth 1.4%**.

Two further points that bear on every number in this doc:

- **Microbenchmarks overstate the prize ~200x.** `lvp_chase --mode const`
  measures 4.0x; real workloads measure 1-2%. Any cost/benefit argument built on
  the microbenchmark figure is wrong by that factor.
- **The MIR round-trip is a per-binary codegen lottery, not a constant.** The
  `nodit` control - zero `MSR DIT` - is the *slowest* binary in the gem5 table
  (+2.65% vs plain), exceeding the entire dwell effect. Measured at +0.58%
  (QuickJS), +0.06% (SQLCipher native), +2.65% (gem5). "Baseline against the
  round-trip control" is necessary but **not sufficient** at these effect sizes.

---

## Placement re-derivation, 2026-08-27: region wins, the old advice was `switch-cyc=0`

**No new runs.** Re-derived from the existing `gem5-sqlc3` placement sweep
(`~/Documents/dit-browser-bench/gem5-sqlc3/`, arms in
`utils/dit_host_screening/sqlc_gem5.py`, log `gem5-sqlc3-placement.log`), whose own
comment states its purpose: *"these two ask whether the coverage can be kept without
the toggle bill."* It can.

Overhead vs the `nodit` baseline, matched config and cache:

| cache / switch model | `hmacfix` region sw=0 | `hmacsw30` region sw=30 | `hmacfn` function |
|---|---|---|---|
| 16 serializing | 38.11% | **16.10%** | 17.25% |
| 1024 serializing | 37.21% | **15.74%** | 16.89% |
| 1792 serializing | 31.85% | **13.23%** | 14.75% |
| 1920 serializing | 23.76% | **10.30%** | 10.42% |
| 16 renamed | 4.55% | **1.17%** | 1.19% |
| 1024 renamed | 3.17% | 1.45% | **-0.10%** |
| 1792 renamed | 2.24% | 1.41% | **1.01%** |
| 1920 renamed | 1.01% | 0.97% | **-0.02%** |

Coverage is a wash, so the comparison is apples to apples: `ditSuppressed` at cache 16
is 67,023 (sw30 renamed) vs 67,141 (function renamed), and 66,052 vs 66,576
serializing - within 0.2-0.8%, both 97-99% of the oracle's 67,572/67,646.

**Three conclusions.**

1. **Region with the shipped `switch-cyc=30` beats function placement at every
   serializing cache point** and ties at cache 16 renamed. It loses 0.41-1.55 pp at
   the three other renamed points. The earlier guidance to prefer
   `-taint-dit-placement=function` on this TU was derived from `hmacfix`, which is
   `switch-cyc=0` - the pre-2026-08-24 default that asserted toggles are free.
   **+38.11% is not region's cost; 16.10% is.**

2. **A compression round needs no special case, because region already degenerates to
   whole-function coverage there.** Every block in a loop whose body is secret end to
   end is a need-block, so `admitOffCorridors` returns at its `all_of(On)` guard and no
   corridor decision is ever made. Verified on a synthetic compression-round MIR (hot
   loop, secret `MADD`, non-preserving call in the body): `region` and
   `-taint-dit-placement=function` emit **byte-identical** code, and `switch-cyc` from
   0 to 100,000 changes nothing. The old 888,967-vs-224,289 switch gap was `switch-cyc=0`
   splitting blocks that 30 now merges.

3. **The admission test is the wrong lever for what remains.**
   `TaintAnalysis.cpp:2826` refuses one-sided corridors (`!HasOnPred || !HasOnSucc`) at
   any switch cost, and that is correct - a leading preamble or trailing epilogue has no
   toggle pair to save, so merging it moves the switch rather than removing it. The
   residual is therefore entry enables, exit clears, and post-call re-asserts. On this
   workload the post-call re-assert is the per-iteration cost and **both** policies pay
   it identically. Removing it needs callee ownership (cloning or Mode 2), not a cost-model
   change.

**Consequence for policy.** `-taint-dit-placement=function` can be dropped as a
recommendation. It remains load-bearing as *code*: the `!OwnsDIT` route
(`AlwaysEnteredWithDIT`, `.dit` clones) and the per-function verifier fallback both go
through `emitFunctionGranularityDIT`.

**Caveat, stated because it is the one place region loses.** On renamed hardware at
caches 1024/1792/1920, function placement is 0.41-1.55 pp cheaper, and at two of those
points it is slightly *negative* vs `nodit`. If renamed-switch silicon becomes the
target, this is the case to re-examine - and per `CLAUDE.md`'s NOP-control finding the
gap there is block-splitting codegen, not switches, so it would not be fixed by a
cheaper switch.
