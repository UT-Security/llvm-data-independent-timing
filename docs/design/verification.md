# Verifying DIT hardening: a static verifier and a dynamic oracle

**Written 2026-08-28.** Every number and every failure described here was
measured; the two bugs in §5 were found by these instruments, in the order
described, and both are fixed on `m2-oracle-hooks`
(`b23816e314b0`, `4be92f73da7f`).

---

## 1. The claim, and the two ways it fails

A compiler that inserts timing-mode switches is asserting one property:

> **Every instruction that computes on a secret executes with `PSTATE.DIT` set.**

That assertion can fail in two independent ways, and they need different
instruments because neither can see the other's failure:

| # | failure | example | who catches it |
|---|---|---|---|
| **A** | the analysis was wrong: a secret reaches an instruction the pass never marked, so nothing was placed | a callee returns a *pointer* into a secret buffer and return taint is data-only | dynamic oracle (§4) |
| **B** | the placement was correct when emitted and was broken before it shipped | the post-RA scheduler hoists `MSR DIT, #0` above a secret-dependent multiply | static verifier (§3) |

The pass has always had a soundness verifier, but it validates the MIR *it has
just produced*, from inside `insertTaintDITSwitches`. Roughly a dozen machine
passes run afterwards. Failure B is invisible to it by construction, which is
exactly how the scheduler bug in §5.1 shipped.

**Neither instrument subsumes the other, and §5 shows this rather than asserting
it**: the oracle found a bug the pass's verifier could not see, and the verifier
then found a bug on a build the oracle had just called clean.

---

## 2. Modelling the mode as state

Both instruments rest on one change, so it is described once.

`PSTATE.DIT` is machine state, and AArch64 already models comparable state as
registers (`NZCV`, `FPCR`, `FPSR`, `FFR`). We add one more:

```
def DIT : AArch64Reg<0, "dit">;     // reserved, never allocated
```

It holds no value and is never read for its contents. It exists to carry an
ordering edge. Two operands are attached:

- **`insertTimingModeSwitch`** gives every emitted `MSR DIT, #imm` an
  `implicit-def $dit`.
- **`TargetInstrInfo::pinToTimingMode`** gives every instruction the pass
  decided requires the mode an `implicit use $dit`. The pinning walk lives in
  `insertTaintDITSwitches`, before the region/function placement choice, so both
  granularities get it.

```
MSRpstateImm4 26, 1, implicit-def $nzcv, implicit-def $dit
%2:gpr32 = ADDWrr %0, %1, implicit $dit
MSRpstateImm4 26, 0, implicit-def $nzcv, implicit-def $dit
```

The operand does two jobs.

**It is a dependence.** Ordinary register dependences now hold the switches in
place: RAW after an enable (a protected instruction cannot float above it), WAR
before a disable (the disable cannot sink above the last protected instruction).
Every pass that respects register dependences respects DIT placement, including
passes not yet written.

**It is a marker.** The verifier in §3 needs to know which instructions require
the mode. Because the pin is already there, it needs no taint analysis of its
own.

> **`hasSideEffects` is not a substitute, and it was already set.** TableGen
> infers `UnmodeledSideEffects` for `MSRpstateImm4`. It does not help: the
> machine scheduler's barrier (`ScheduleDAGInstrs::buildSchedGraph`, via
> `isGlobalMemoryObject`) chains only against loads, stores and FP exceptions,
> while DIT governs *data processing*. A `madd` appears in none of those maps,
> so no edge to the switch existed. Do not "fix" a future instance of this by
> setting a side-effects flag.

The same idea already exists on the simulator side: gem5's DIT-covered
instructions read a `DitCC` operand, which is what lets `dit::classify()` ask
whether a given instruction ran with the mode set.

---

## 3. The static verifier: placement integrity

`llvm/lib/Target/AArch64/AArch64DITVerifier.cpp`, scheduled from
`addPreEmitPass2` - **last**, after every other machine pass, so it sees exactly
what will be emitted.

### Mechanism

A forward one-bit dataflow over the final MIR:

- **Facts.** A switch sets the state (`getTimingModeSwitch` reads the immediate).
  An instruction with `implicit use $dit` and no def requires the state.
- **Meet.** AND at joins: the mode must arrive set on *every* predecessor path.
  Blocks are initialised optimistically to `on`, so a loop carrying the mode in
  from outside converges rather than being pinned false by its backedge.
- **Entry.** `off`, except for a function the caller enters with the mode
  already set. Such a function emits no entry enable, so `insertTaintDITSwitches`
  records the assumption as a `"dit-entered-on"` function attribute for the
  verifier to read.
- **Calls are transparent.** Whether a callee clears DIT is decided by the taint
  pass from its summaries, which also emits the re-asserts. It is not something a
  downstream pass changes, and assuming otherwise would reject the correct
  elision of a re-assert after a preserving callee. This pass exists to catch
  damage done *after* placement, not to re-litigate placement.

A violation is `report_fatal_error`. A leaked secret is not a missed
optimisation.

```
LLVM ERROR: PSTATE.DIT placement is unsound in 'leaky': an instruction that
must execute with DIT set reaches bb.0 with it clear.
  $w0 = ADDWrr $w0, $w1, implicit $dit
The taint pass verified its own output, so this was introduced by a later
machine pass moving, duplicating or synthesising code.
```

It early-outs when nothing is pinned, so untainted functions cost one scan.

### What it does and does not guarantee

**Guarantees:** whatever the analysis decided needs protection gets it, on every
path, in the binary that ships.

**Does not guarantee:** that the analysis decided correctly. This is the
limitation to state plainly in any writeup.

- An instruction the analysis **missed** carries no pin, so the verifier is
  silent about it. Every under-taint in `context-insensitivity.md` is invisible
  here.
- An instruction the analysis **over-tainted** is pinned and covered, and the
  verifier is satisfied. That is correct behaviour: over-tainting is a
  performance problem, not a soundness one. It also cannot cause a spurious
  failure, because the same Need set drives both the placement and the pin, and
  coverage is monotone - loop hoisting and the admission test only ever *extend*
  the on-region.
- **Residuals are deliberately unpinned.** `needsDIT` excludes divides, secret
  branches and returns because DIT cannot protect them. A secret `SDIV` draws no
  complaint here; that is `classifyDITUncovered`'s report, a different
  instrument.

So the verifier is a **consistency** check, not a **soundness** check.

### Test

`llvm/test/CodeGen/AArch64/taint-analysis-dit-verifier.mir` pins both
directions: a disable moved above the instruction it protected must fail, the
correctly ordered form must not, and a second RUN line stopping *before* the
verifier shows that the verifier is what catches it.

---

## 4. The dynamic oracle: analysis soundness

Nothing faults when a secret is computed on with the mode off - the instruction
executes perfectly and leaks quietly. So failure A cannot be caught by watching
for a crash; it needs an instrument. Two tiers, because the cheap one has a
structural blind spot.

Home: `~/Documents/dit-crossover/oracle/`, acceptance suite `./run_tests.sh`
(18 checks).

### 4.1 Tier 1 - page protection on real hardware

Secrets are placed on pages of their own and the pages are made inaccessible, so
every access traps. The trick that makes this an instrument rather than a crash
detector:

> **On AArch64, `PSTATE.DIT` is bit 24 of the saved processor state.** The fault
> handler therefore learns not only *that* a secret was touched but *whether it
> was protected at that instant* - one bit, no instrumentation of the access
> itself.

Confirmed on Apple M5: `0x20000000` with the mode clear, `0x21000000` with it
set. Darwin raises `SIGBUS`, not `SIGSEGV`, for `PROT_NONE`.

**Re-arming.** After a fault the page must be made accessible for the access to
complete, then protected again. CryptoMPK (S&P'22), which does the same thing
for Intel MPK, single-steps over the faulting instruction. Neither of its
mechanisms is available here: XNU masks `PSTATE.SS` on `sigreturn`, and
`mprotect` on a code page returns `EACCES`, so a breakpoint cannot be planted
either. Instead **the compiler re-arms**: under `-taint-dit-oracle-hooks` a
three-instruction sled is stapled to every switch,

```
stp x30, xzr, [sp, #-16]!
bl  __dit_oracle_rearm
ldp x30, xzr, [sp], #16
```

shaped after XRay's custom-event sled - the only register it disturbs is LR,
which it saves, because the trampoline preserves x0-x17, NZCV and v0-v31 itself.
That is what makes it safe to insert after register allocation without knowing
what is live. Cost becomes O(regions) rather than O(accesses), and no debugger,
entitlement or text patching is involved.

**Blind spot, and it decides what may be claimed.** A page oracle sees loads and
stores of secret *memory*. It never sees `mul x1, x0, x2` where `x0` already
holds a loaded key byte, and it never sees a secret that lives on the stack. So:

- every **mode-clear hit is a true finding** - no false alarms are possible;
- **"never faulted" is only a candidate**, never a false-positive rate;
- stack secrets are invisible, which is why the positive controls in the
  acceptance rig stay on hand-placed probes.

Switch-driven re-arming also only fires where there *is* a switch, so several
secret accesses inside one stretch of uninstrumented code yield the first only.
A periodic re-arm covers that; it samples, so absence of a hit is weaker evidence
than presence.

### 4.2 Tier 2 - shadow taint in simulation

`gem5-DIT`: shadow state in `src/sim/taint_oracle.{hh,cc}`, propagation and the
check in `Commit::taintOracleCommit`.

Shadow taint over architectural registers and over memory (a byte mask per
64-byte line), seeded by the workload through m5 ops on the user-reserved
opcodes `0x55`/`0x56` - so no ISA decoder change was needed; the ARM decoder
already forwards every function byte to `pseudoInst`.

Propagation runs at **commit**, so it observes the real dynamic trace and not
speculation:

```
src  = OR over source registers of shadow[reg]
     | (isLoad ? memShadow.any(effAddr, size) : false)
if (isStore) memShadow.set(effAddr, size, src)
for each dest register: shadow[dest] = src

if (src && dit::classify(inst) == Why::Clear)   ->  under-taint
if (!src && dit::classify(inst) == Why::Set)    ->  wasted coverage
```

The check is one comparison because gem5 already carries the fact: `classify`
returns `Clear` **only** for instructions holding a `DitCC` operand, i.e.
exactly the Arm DIT-covered set. A tainted operand plus `Clear` therefore *is* a
covered instruction computing on a secret with the mode off. Misc registers are
excluded from tracking (they turn the whole machine secret within a few
instructions) and so is the zero register.

This tier sees the register and stack propagation Tier 1 cannot, which is what
makes it ground truth rather than a bug finder.

**Its own trap:** dynamic taint over-taints too. Propagate through everything for
long enough and the ciphertext, the signature and eventually the output buffer
are all secret-derived, and the oracle will report the pass's *correct* decisions
as under-taints. Read the per-function histogram, not the total, and give the
oracle the same declassification the pass gets before quoting a rate.

### What it does and does not guarantee

**Guarantees:** every finding is real.

**Does not guarantee:** absence. It observes one execution of one input.

---

## 5. The two bugs, and why the pair is the argument

### 5.1 The oracle found what the pass's own verifier could not

flowprobe's *positive controls* - functions that must be fully protected - each
showed one under-tainted instruction:

```
ldrb  w10, [x0, #0x1f]   ; last secret byte, DIT=1
msr   DIT, #0x0          ; the pass's own disable
madd  x0, x9, x8, x10    ; DIT-covered multiply on secret operands, DIT=0
ret
```

The pass emits `madd; msr; ret`; the post-RA scheduler hoists the disable above
the multiply. Established differentially rather than by eye: `-disable-post-ra`
moves exactly two sites from under-tainted to protected, and they are the two
controls (267 → 265 under-tainted ops, 150 → 152 protected). Present in both
placement modes.

Fixed by §2. The alternative - moving the MIR round-trip seam downstream of the
scheduler and the outliner - also works, but it places the pass on already
scheduled code and fragments the regions: libsecp256k1 signing goes from **26**
static switches to **111**. It is also merely positional, safe only because of
which passes happen to be downstream today.

### 5.2 The verifier then found what the oracle had called clean

Under `-enable-machine-outliner=always`, the verifier rejected the build:

```
fatal error: PSTATE.DIT placement is unsound in 'OUTLINED_FUNCTION_2'
```

The outliner lifts protected instructions into a function created *after* the
analysis ran. It has no entry enable, and whether it is safe depends on the DIT
state at every one of its call sites - which nothing checks. DIT-tied
instructions are now `Illegal` to outline; without that, hardening and the
outliner cannot be used together at all.

**The oracle had run on that same binary and reported no leak.** It was right -
on that input, every caller happened to have the mode set. The verifier was also
right: safety rested on an unchecked property of all paths. Neither was wrong,
and neither could have reached the other's conclusion.

| | static verifier | dynamic oracle |
|---|---|---|
| when | build time | run time |
| coverage | all paths | one input |
| catches | placement broken downstream | analysis errors (under-taints) |
| blind to | analysis errors | paths not executed |
| on failure | build fails | a report |

---

## 6. Running them

```bash
# static verifier: on automatically with -ftaint-harden, nothing to enable
clang -O2 -ftaint-harden=seed.txt prog.c        # fails the build if unsound

# dynamic oracle, tier 1 (hardware)
clang -O2 -ftaint-harden=seed.txt -mllvm -taint-dit-oracle-hooks \
      prog.c ditoracle.c sled.S -o prog
cd ~/Documents/dit-crossover/oracle && ./run_tests.sh        # 18 checks
cd m3 && ./run_m3.sh                                         # libsecp256k1

# dynamic oracle, tier 2 (simulation)
gem5.opt configs/example/arm/fdp_neoverse_v2_binary.py \
    --eves --dmp --comp-simp --binary benchmarks/taint_oracle/bin/flowprobe_gem5
```

**Never time an oracle build.** The traps sit in the loop and the sled changes
code layout; the runtime exports a `dit_oracle_present` symbol so a timing
harness can refuse one. Oracle runs and timing runs are different runs.

---

## 7. Traps

| trap | what it does | guard |
|---|---|---|
| an over-taint hides an under-taint | an over-tainted caller leaves the mode on, so the consumer reads as covered | verify **at the consumer**, with the caller proven untainted; record which region was live with every hit |
| stale artifacts | a failed build silently re-runs the previous binary and the suite reports PASS | `run_tests.sh` wipes its output directory and fails loudly; never `2>/dev/null` a build whose output you are about to measure |
| quoting a false-positive rate from Tier 1 | register-resident secrets are invisible, so "never faulted" is not "never secret" | no FP rate from Tier 1 at all; use Tier 2 |
| 16 KB pages on Apple silicon | a secret sharing a page with public data faults constantly | one object per page; page-exclusivity asserted at registration |
| threads | `mprotect` is process-wide but the live region is per-thread | single-threaded runs; the oracle voids a run where faults arrive on more than one thread |
| trusting an OS detail | cpsr bit 24 carrying DIT is an XNU implementation detail, not a documented ABI | a startup self-check faults once with the mode clear and once set and aborts if the bit disagrees |

---

## Sources

`design/dit-placement.md` (where switches go), `design/context-insensitivity.md`
(the under-taints Tier 2 exists to find), `reference/dit-spec.md` (what the
hardware guarantees). Implementation: `AArch64DITVerifier.cpp`,
`AArch64InstrInfo::pinToTimingMode`, `insertTaintDITSwitches`;
`~/Documents/dit-crossover/oracle/`; `gem5-DIT/src/sim/taint_oracle.cc` and
`Commit::taintOracleCommit`.
