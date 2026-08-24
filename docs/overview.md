# Taint-driven timing side-channel hardening in LLVM: overview

**Start here.** `README.md` is the annotated index; this doc is the map: what the
compiler is, what it does end to end, what is measured, and what is not implemented.

Target: **x86-64**. Everything is at the LLVM IR / MIR level; `clang/` is untouched.

---

## 1. What this is, in one paragraph

An LLVM fork that adds a **secret-tracking taint analysis and a fence-based timing
mitigation**. You declare which function arguments hold secrets in a CSV file. A
module-level analysis pass propagates taint forward from those arguments across the
whole module, reporting every instruction and every source line that depends on a
secret. A transform pass then inserts sequentially-consistent memory fences around each
of those instructions, so that secret-dependent computation cannot overlap in the
pipeline with its neighbours. A separate machine-level pass re-runs the analysis after
register allocation, seeded from per-function summaries the IR pass wrote to disk, so
that spills, stack slots, and the calling convention are visible; that pass reports and
does not mitigate.

## 2. Threat model

The target channel is **instruction timing that depends on secret values**: how long
secret-dependent work takes, and whether that duration is observable. Concretely the
analysis is built to find the three classic sites, which
`SensitiveReason` enumerates: a branch on a secret, a memory access at a
secret-dependent address, and a variable-latency operation (integer divide/remainder)
on a secret.

Out of scope, and not addressed anywhere in the implementation:

- **speculative execution** (Spectre and relatives). No speculation barriers are
  emitted and none are intended.
- **cache and TLB address timing as such.** The analysis *detects* secret-dependent
  addresses (that classifier is written), but a memory fence does not remove a
  data-dependent cache access, so detection is not mitigation here.
- **power, EM, and other physical channels.**

Read `design/fence-insertion.md` §3 before claiming what the mitigation guarantees. It
is coarse serialization, not a constant-time transformation.

## 3. Architecture

```
  sources.csv  (FunctionName,ArgumentIndex)
        |
        v
  +-----------------------------+
  | TaintAnalysis               |  module analysis, "taint-analysis"
  | llvm/lib/Analysis           |  forward closure over LLVM use lists
  +-----------------------------+
        |                |                        |
        | TaintInfo      | -taint-output-file     | -taint-summary-file
        | (in memory)    |   sensitive lines      |   per-function summaries
        v                v                        |
  +-----------------------------+                 |
  | TaintFenceInsertion         |                 |
  | "taint-fence-insertion"     |                 |
  | 2 x fence seq_cst around    |                 |
  | every tainted instruction   |                 |
  +-----------------------------+                 |
        |                                         |
        v  hardened IR -> codegen                 |
  ============================================    |
                                                  v
  +-----------------------------------------------------+
  | MachineTaintPruning     "machine-taint-pruning"     |
  | post-RA, after addPreEmitPass, -enable-taint-pruning|
  | re-seeds taint from the summary CSV, propagates     |
  | over physregs + frame indices, reports only         |
  +-----------------------------------------------------+
        |
        v  -taint-leaky-insts-file (CSV)
```

Two things about this picture are worth stating explicitly in a paper:

- **The IR-to-MIR interface is a file, not an in-memory analysis.** A
  `TaintAnalysisWrapperPass` exists for the in-memory handoff and is never used; the
  MIR pass re-parses the summary CSV. That makes the two halves independently runnable
  (and independently testable) at the cost of losing everything the summary does not
  carry, which is everything except tainted argument indices.
- **The two analyses are different analyses**, not one analysis at two levels. The IR
  one is an SSA use-list closure; the MIR one is a flow-insensitive physical-register
  and frame-index closure. They share only the argument-level summary.

## 4. How the analysis works, in brief

Full detail in `design/ir-taint-analysis.md`. The short version:

- **Lattice**: two points, `Notaint < Taint`, join is OR. No declassification, no
  "maybe".
- **Sources**: named function arguments from the CSV, matched exactly by symbol name.
- **Transfer function as implemented**: *every user of a tainted value is tainted*.
  There is no per-opcode discrimination on the live path.
- **Interprocedural**: at a call site with any tainted argument, tainted actuals flow
  into the callee's formals (direct calls with a body in the module only), and **every
  pointer argument is conservatively tainted** as a possible output buffer. That
  pointer rule is the only mechanism that recovers secret flow through memory.
- **Sensitivity**: `SensitiveInsts` ends up holding *all* tainted instructions, tagged
  `TaintedValue`. The classifier that would distinguish tainted branches from tainted
  addresses from tainted divisions is implemented and **never called**.

That last point is the single most important gap between the code and the design the
headers describe, and it is what makes the measured cost what it is.

## 5. What is measured

`results/libsodium-fence-cost.md`. Ed25519 signing under libsodium, 1000 iterations,
`rdtsc`/`rdtscp` with `cpuid` serialization, median cycles per signature:

| configuration | median cycles | vs unfenced |
|---|---|---|
| our build, unfenced | 45,676 | 1.00x |
| our build, fenced | 4,110,384 | **90.0x** |

The system libsodium build is 34,922 (1.31x faster than our unfenced build), which is a
build-configuration artifact and must not be charged to the analysis.

**The 90x is the result to lead with, and it is a negative one.** It is the cost of
fencing every tainted instruction twice on a workload that is almost entirely
secret-dependent arithmetic. It bounds what unselective placement can ever be worth and
is the argument for the three follow-ups in `results/libsodium-fence-cost.md` §4:
classify before fencing, coalesce adjacent fences, and prefer a region-scoped
data-independent-timing mode over per-instruction barriers.

Not measured: the other five libsodium workloads (harnesses exist), fence counts, and
anything at all about the MIR pass.

## 6. How to run it

```
# 1. declare secrets
echo 'crypto_sign,4' > sources.csv

# 2. analyse
build/bin/clang -S -emit-llvm -g -O0 file.c -o file.ll
build/bin/opt -passes='print<taint-analysis>' -taint-sources-file=sources.csv \
  -taint-output-file=lines.txt -taint-summary-file=summaries.csv \
  -disable-output file.ll

# 3. harden
build/bin/opt -passes='taint-fence-insertion' -taint-sources-file=sources.csv \
  file.ll -o file.fenced.ll

# 4. machine-level report (needs summaries.csv from step 2)
build/bin/llc -enable-taint-pruning -taint-summary-file=summaries.csv \
  -taint-leaky-insts-file=leaky.csv file.ll -o file.s
```

Every flag, format, and API entry point is in `reference/interfaces.md`.

## 7. Current state

**Working end to end.** Sources are declared, taint propagates module-wide
interprocedurally, reports are emitted at both IR and machine level, fences are
inserted, and a fenced libsodium builds, links, passes its own per-iteration
signature-verification check, and has been benchmarked.

**Implemented but not on the live path** (compiled, never called):

- `computeInstructionTaint()` - the per-opcode transfer function.
- `checkSensitiveInstruction()` - the leak classifier, and with it four of the five
  `SensitiveReason` values.
- `TaintAnalysisWrapperPass` - the in-memory IR-to-MIR handoff.

**Not implemented:**

- any clang driver flag (`-mllvm` only);
- declassification / sanitization;
- alias analysis or any memory model beyond call-site pointer tainting (IR) and frame
  indices (MIR);
- indirect-call handling;
- subregister-aware taint on x86-64 (MIR);
- mitigation at the machine level (the MIR pass only reports);
- a cost model or placement budget;
- tests beyond one source-identification `FileCheck` test.

**Known soundness gaps** (a detector with blind spots, not a sound analysis):
same-function flow through memory, indirect calls, stack-passed arguments, x86-64
subregisters, and the argument-index-to-register mapping. All enumerated with their
mechanisms in `design/precision-and-soundness.md`.

## 8. If you are writing the design section of a paper

The honest framing, in order:

1. **Contribution**: a whole-module interprocedural taint analysis in LLVM driven by an
   out-of-line secret declaration file, plus a matching post-register-allocation
   analysis, plus a fence-based mitigation, plus a cycle-accurate evaluation harness on
   six real cryptographic workloads.
2. **Design point**: taint is a two-point lattice propagated as a forward use-list
   closure; interprocedural precision is traded away deliberately (context-, flow- and
   path-insensitive) in exchange for whole-module scale and a simple implementation.
   Memory is handled by one blunt rule: pointer arguments at tainted call sites are
   tainted.
3. **Result**: unselective mitigation costs 90x on Ed25519. This is the paper's
   quantitative core, and it motivates selective placement rather than undermining the
   approach.
4. **Limitations**: cite `design/precision-and-soundness.md` rather than paraphrasing,
   and keep the over-approximations (pointer and constant tainting) separate from the
   under-approximations (memory, indirect calls, subregisters), because only the second
   group affects security claims.
