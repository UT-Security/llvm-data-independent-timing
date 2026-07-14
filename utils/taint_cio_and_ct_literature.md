# CIO and the constant-time / speculative-execution literature: what they do at calls

**Date:** 2026-07-14  
**Companion to:** `utils/taint_memory_summary_research.md` (general taint literature)  
**Status:** research complete; design NOT yet implemented.

Seeded on the paper the project lead identified as the closest prior work ("CIO", Kohlbrenner),
then snowballed. Fan-out research workflow: 101 agents, 18 sources, 89 claims extracted,
25 adversarially verified, 20 confirmed / 5 refuted.

---
## Executive summary

The anchor paper IS positively identified, but the user's premise about it is partly
wrong. "CIO" is the tool `cio` from Michael Flanders, Reshabh K Sharma, Alexandra E.
Michael, Dan Grossman, David Kohlbrenner, "Avoiding Instruction-Centric
Microarchitectural Timing Channels Via Binary-Code Transformations," ASPLOS '24 (DOI
10.1145/3620665.3640400; PDF https://homes.cs.washington.edu/~dkohlbre/papers/cio-
asplos24.pdf; artifact https://github.com/counter-optimization). The paper expands the
name itself: "cio—countering instruction-centric optimizations." It is NOT a
Spectre/speculation defense (the paper calls speculation "orthogonal" and assumes its
input is already classically constant-time); it targets two not-yet-deployed uarch
optimizations, silent stores and computation simplification — the same threat-model
family as the user's PSTATE.DIT mode, and a different one from the ISB/DSB mode. cio
DOES do interprocedural taint and DOES have post-register-allocation LLVM MIR passes
(chosen explicitly to catch register spills, positioned immediately before AsmPrinter,
with scratch registers reserved during RA), so it is the closest prior work on level —
but it sidesteps the per-function-summary problem entirely by running the taint in BAP
over the whole linked binary's callgraph, has NO function summary and NO memory-
effects/mod-set component, and emits instruction substitutions (arithmetic range-
widening, split-and-recombine, cmov, blinding stores) rather than barriers. Across the
verified literature the pattern is uniform — Serberus avoids calls by requiring "static
constant-time" input where all call/return arguments are public plus brute-force zeroing
of non-argument registers; CtChecker is LLVM-IR, detect-only, whole-program PDG + DSA
points-to, and its one relevant TASK 5 data point is a coarse TOP-like worst-case flow
rule for bodiless external functions, which CONFIRMS the recommended "TOP for external
declarations" default — and no verified source describes interprocedural taint at a
post-regalloc machine IR that inserts speculation/timing barriers around secret-
dependent regions, so that specific design point appears unoccupied.

---
## Caveats and limits (READ FIRST)

1) COVERAGE GAP — the biggest one. TASK 3 asked for the concrete call-handling of
SLH/Ultimate SLH, Blade (POPL'21), Pitchfork (PLDI'20), ct-verif (USENIX'16),
Binsec/Rel, Jasmin/FaCT/Vale/CryptOpt, Swivel/Venkman/retpoline, and any
PSTATE.DIT/CSDB/SSBS work. ZERO verified claims came back for ANY of them; only cio,
Serberus, and CtChecker were actually dissected. The TASK 4 novelty finding and the TASK
5 design finding are therefore PROVISIONAL: safe statements about those three systems,
not yet safe statements about the field. Ultimate SLH and Blade are the most likely
homes for contradicting prior art. 2) The user's framing of CIO is half right. It IS
closest on LEVEL (post-regalloc MIR, spills, pre-AsmPrinter) and it DOES have
interprocedural taint. It is NOT close on threat model (no speculation; assumes CT input
already), emitted mitigation (instruction substitution, not barriers/DIT), architecture
(x86_64), or analysis placement (BAP over the whole binary, not TU-scoped in the
compiler). 3) Two negative findings rest on grep-over-extracted-PDF-text ("no
summary"/"no indirect-call discussion" in cio; "no alias analysis" in Serberus). Absence
in a PDF extraction is weaker evidence than presence. The cio finding is independently
supported by a structural argument: whole-binary abstract interpretation obviates
summaries. 4) cio is internally inconsistent on one number: Table 3 says taint pruned 1
of 2,695 SS candidate stores; the S9.1 prose says 7. Cite the qualitative point (taint
is near-useless for SS pruning, ~37% effective for CS), not the exact figure. 5) "cio
has no points-to/alias analysis" is an overstatement. The Mine abstract-memory / value-
set domain IS its pointer-and-memory disambiguation mechanism (VSA a la Balakrishnan-
Reps is a binary points-to analysis). Correct phrasing: no SEPARATE points-to analysis;
disambiguation folded into the value domain. 6) Serberus does not check CTS — it assumes
it. Do not describe it as "enforcing" a type discipline on arbitrary C. 7) TIME
SENSITIVITY: cio's claim that only Apple M-series ships PSTATE.DIT is dated December
2023 and is likely stale (FEAT_DIT is Armv8.4+). Do not repeat that availability claim
as current fact — though it does explain why cio chose software substitution over DIT.
8) CtChecker's article number is 4, not 46 (LIPIcs ECOOP 2024); the Dagstuhl landing-
page URL is misleading.

---
## Verified findings

### 1. CIO = the tool `cio` in "Avoiding Instruction-Centric Microarchitectural Timing Channels
Via Binary-Code Transformations," Flanders, Sharma, Michael, Grossman, Kohlbrenner,
ASPLOS '24 (Vol. 2, pp. 120-136), DOI 10.1145/3620665.3640400. The acronym is defined IN
the paper as "countering instruction-centric optimizations" — not constant-time I/O,
obliviousness, or instruction ordering.

*confidence:* **high** — *vote:* 3-0 (five converging claims)

> Paper text: "Our approach is embodied in our tool, cio—countering instruction-centric
> optimizations—which serves as a foundation for building mitigations for instruction-
> centric optimizations." Kohlbrenner's homepage lists the paper as papers/cio-
> asplos24.pdf and adds "cio is available [here](https://github.com/counter-
> optimization)". DBLP's full list for Kohlbrenner (pid 131/5093) contains no paper
> titled or acronymed CIO — the name is a TOOL name, not a title acronym. Multiple
> verifiers independently downloaded and text-extracted the PDF; ACM DL DOI and DBLP
> corroborate authors/venue/year.

### 2. cio's threat model EXCLUDES speculative/transient execution and PRESUPPOSES a
classically constant-time input. It defends against instruction-centric uarch
optimizations — silent stores (SS) and computation simplification (CS) — that are not
yet deployed in shipping hardware.

*confidence:* **high** — *vote:* 3-0 (four converging claims)

> Threat Model (S3): "We do not consider transient instruction execution in our
> analyses. This means we may miss cases where secret inputs only ever reach
> instructions transiently." And: "We assume that the target program is constant-time if
> executed on a processor without the considered optimization(s). The program must not
> leak information from known channels such as secret-dependent memory accesses, secret-
> dependent branches, or known variable time instructions such as idiv... we expect that
> our input program is a cryptographic library, already hardened against known attack
> vectors." Related Work (S2.2): "Recent tools have included speculative versions of
> these properties, which is orthogonal to the problems cio considers." Limitations
> (S9.5): "Our checkers also do not consider transient inputs to instructions, and may
> prune instructions that can leak speculatively." CONSEQUENCE FOR THE USER: cio
> overlaps the DIT / data-operand-independent-timing threat model (it discusses Arm
> PSTATE.DIT and Intel DOIT directly) and shares NOTHING with the ISB/DSB speculation-
> barrier mode. cio's scope is a strict superset of what DIT fixes: DIT does not
> mitigate silent stores.

### 3. cio is a HYBRID: its checkers/taint analysis run on the compiled BINARY in BAP IR, while
its mitigation transforms run as LLVM Machine IR passes positioned immediately before
assembly printing (post-register-allocation), with scratch registers reserved during
register allocation. The MIR level was chosen explicitly to catch register spills that
source-level CT techniques cannot see.

*confidence:* **high** — *vote:* 3-0 (three converging claims)

> S4.3: "We position these transform passes late in LLVM's compilation pipeline as a
> Machine IR (MIR) pass. LLVM's MIR is a low-level wrapper around ISA-specific assembly
> instructions; it is the final intermediate representation of translation units that
> transformation passes can be run on. Writing passes at the MIR level lets us catch
> low-level details like register spills that cannot be mitigated in source code. We
> also position our passes immediately before the assembly printing pass to avoid the
> possibility of later compiler optimizations breaking our passes' security guarantees."
> S4.1: "We reserve these registers during register allocation." S4.2: "After the
> compilation step, cio runs static program analyses on the compiled binary... We
> implement our checkers and program analyses using the Binary Analysis Platform (BAP)."
> GitHub org repo blurb: "Fork of LLVM with secret arg annotations, scratch register
> reservations, CS and SS mitigation passes." x86_64-only. This is the strongest
> available independent endorsement of the user's choice of post-regalloc MIR as the
> hardening level — the rationale (spills; no later optimizer can undo it) is identical
> to the user's.

### 4. CENTRAL ANSWER ON CALLS: cio IS interprocedural, but it sidesteps the per-function-
summary problem by construction — the taint runs over the WHOLE LINKED BINARY's
callgraph in BAP, precisely because "the compiler only sees individual translation
units." There is NO per-function summary and NO memory-effects/mod-set component
anywhere in the paper; external-declaration and indirect/function-pointer call handling
is never discussed. Memory is handled by an interval analysis + trace partitioning +
Mine's abstract memory domain (a value-set analysis uniformly representing pointers,
registers, and memory contents as intervals) — disambiguation is folded into the value
domain rather than done by a separate points-to/alias analysis.

*confidence:* **high** — *vote:* 3-0 (two converging claims)

> S4.1: "Since the compiler only sees individual translation units, BAP will later use
> the annotations file to propagate secrets through the callgraph using an
> interprocedural taint analysis." S5.4: "Our CS checker first runs an interprocedural
> taint analysis, propagating developer-annotated secret arguments to all functions in
> the callgraph of the public API function... Next, the checker runs an interval
> analysis extended with machine-integer semantics, the trace partitioning domain, and
> the abstract memory domain from Mine... The abstract memory domain effectively runs a
> value-set analysis, uniformly representing pointers, register values, and memory
> contents as intervals with extra type information." Verifiers grepped the full
> extracted text: ZERO occurrences of "summar*", "mod-set", "modref", "memory effect";
> no discussion of indirect calls or external declarations (the Limitations section
> discusses only inline asm and .S files). cio's taint is also weak in practice: Table 3
> shows taint pruned only 1 of 2,695 candidate stores in the SS checker vs 4,940 of
> 13,198 for CS. IMPLICATION: cio neither confirms nor contradicts the proposed mod-set
> summary — it ESCAPES the question by abandoning TU-scoped compilation and analyzing
> the whole binary. That is itself decision-relevant: the known alternative to a memory-
> effects summary is whole-program/whole-binary analysis.

### 5. cio EMITS instruction substitution — arithmetic range-widening, split-and-recombine,
conditional moves, and blinding stores — NOT fences/barriers and NOT PSTATE.DIT toggles.
It treats hardware DIT/DOIT as unavailable in practice and positions itself as a
software-only backstop. Overheads are very large.

*confidence:* **high** — *vote:* 3-0

> "These transforms substitute a leaky instruction with a sequence of non-leaky
> instructions that are semantically equivalent to the original instruction."
> "Unfortunately, at this time the only processors known to the authors to support
> PSTATE.DIT are the Apple M-series CPUs"; DOIT "can only be enabled by the kernel... No
> operating system as of December 2023 allows for unprivileged software to enable DOIT
> at run time." Hence "It is therefore critical to have a software-only approach."
> Numbers: worst-case runtime overhead 27.84x (argon2id, SS+CS), 20.32x/16.73x on
> ed25519; libsodium text 363 kB -> 588 kB (SS), 1,118 kB (CS), 1,327 kB (both); the
> final double-check still alerts "on 43 out of 184,659 instructions" (authors attribute
> this to analyzer bugs, not residual leakage). Grep finds no fence/ISB/DSB/LFENCE
> mitigation anywhere. NOTE: cio's own reason for NOT using DIT (hardware availability)
> bears directly on the user's FEAT_DIT requirement.

### 6. SERBERUS (S&P'24) AVOIDS THE CALL PROBLEM BY FIAT. It requires its input to satisfy
"static constant-time" (CTS): all variables have a static security type AND all call and
return arguments are PUBLIC — secrets may only be passed by reference (public pointer to
secret memory), never by value. All three of its passes (Fence Insertion, Function-
Private Stacks, Register Cleaning) are INTRAPROCEDURAL. A CALL/RET is treated as a
conservative SINK, and the Register Cleaning Pass zeroes every non-argument register
before each CALL/RET. There is no per-function summary and no memory-effects component —
none is needed, because the mitigation is secret-agnostic.

*confidence:* **high** — *vote:* 3-0 (two converging claims)

> "we introduce a strengthening of CT programming, called static constant-time (CTS),
> which requires that (i) all program variables have a static security type, and (ii)
> all call and return arguments are public. SERBERUS requires a CTS program as input."
> TYP.9: "requiring that all secrets arguments be passed by reference rather than by
> value; that is, one must pass a public pointer to a secret rather than the secret
> value itself." "SERBERUS consists of three intraprocedural passes." Register Cleaning:
> "It inserts instructions to zero out all non-argument registers... before each call
> and return." Thm 5.6 proof: "We publicly zero all non-argument registers before each
> CALL/RET, so NARG is never satisfied." CORRECTIONS to sloppier renderings: the five
> source-sink pair types are NCAL-XMIT, NCAL-ARG, NCAL-GLOB, NCAS-CAL, NCAS-CTRL (there
> is NO "NCAL-CTRL"); and zeroing breaks only NON-argument dataflow, not all of it.
> Serberus also does NOT verify CTS — it ASSUMES it ("SERBERUS does not require any
> program annotations whatsoever"), relying on CT code compiled with a curated flag set
> (e.g. argument promotion disabled because it violates TYP.9).

### 7. Serberus has NO points-to/alias analysis. Its memory abstraction is a purely syntactic
two-way partition: constant-address (CA) accesses based on SP (stack) or ZR (globals) vs
non-constant-address (NCA) accesses (computed pointers/heap). Its static DFG tracks only
registers and same-offset CA stack store/load pairs; every NCA access is conservatively
assumed to touch an arbitrary data address transiently. Notably, Serberus DOES implement
post-register-allocation MIR passes — but with zero interprocedural taint machinery.

*confidence:* **high** — *vote:* 3-0

> Def. 3.1: "A memory access I |-> LD/ST [ra + d], r is constant-address (CA) if ra in
> {ZR, SP}; otherwise, I is non-constant-address (NCA)." S5.1.2: the static DFG "models
> syntactic intraprocedural dependencies through CA stack accesses and registers...
> Stack dep: (r,I) ->dep (r',J+1) if I |-> ST [SP + d], r and J |-> LD [SP + d], r'."
> S5.1.3: "The source of each pair is an NCA load/store, which we conservatively assume
> may read/write a secret at an arbitrary data address when executed transiently." Grep
> of the full paper: zero body-text hits for "alias"/"points-to"/"heap". S A.9: "We
> implement Fence Insertion as a post-optimization IR pass, Function-Private Stacks as a
> post-register-allocation machine IR (MIR) pass that runs during frame lowering, and
> Register Cleaning as a post-register-allocation MIR pass that runs after call
> lowering." RELEVANCE: Serberus's SP-vs-ZR-vs-other partition is close to the user's
> register/stack-cell/global-cell model, and it is the one published system that both
> emits fences and touches post-regalloc MIR — yet it is entirely intraprocedural and
> secret-agnostic.

### 8. CtChecker (ECOOP'24) is LLVM-IR-level, DETECT-ONLY (reports source line numbers of CT
violations; emits no barriers, masking, or rewriting), interprocedural and context-
sensitive via a PIDGIN program-dependence graph plus a sound DSA points-to analysis
supplying the memory-block abstraction. It has no computed per-function taint summaries;
its known imprecision is that it creates only ONE context per callee even across
multiple call sites with different arguments.

*confidence:* **high** — *vote:* 3-0 (two converging claims)

> "CtChecker targets LLVM intermediate representation (IR)"; "CtChecker reports all
> locations in terms of the line number in the source code regarding violations of the
> constant-time discipline." It is positioned as a checker OF other tools' hardening:
> "CtChecker reveals that some repaired code generated by program rewriters supposedly
> remove timing channels are still not constant-time." "Second, CtChecker is a context-
> sensitive interprocedural analysis. However, when a callee function is invoked
> multiple times within the same caller function with different arguments, CtChecker
> only creates one context for all calls." DSA is "a field- and context-sensitive
> points-to analysis based on Steensgaard's algorithm"; 69% of CtChecker's false
> positives come from DSA imprecision. Its memcpy handling is directly on-point for the
> user's bug: "After calling memcpy, the content of p3 is tainted."

### 9. ANSWER TO TASK 5: the proposed coarse mod-set memory-effects summary with TOP for
external declarations is CONFIRMED (partially) by CtChecker and CONTRADICTED by nothing
verified. CtChecker is the only verified system carrying an explicit memory-effects rule
for bodiless callees, and it is exactly the worst-case/TOP rule the prior round
recommended. No verified CT/Spectre system carries a COMPUTED per-function memory-
effects (mod-set) summary — they all either analyze the whole binary/program (cio,
CtChecker) or forbid the situation by typing discipline and make the mitigation secret-
agnostic (Serberus).

*confidence:* **medium** — *vote:* derived from 3-0 claims on cio, Serberus, CtChecker

> CtChecker's external-function rule (Figure 2, quoted from the PDF): "The analyzed code
> often calls to external functions whose source code is either unavailable...
> Obviously, input arguments can flow to return values. Moreover, if an argument or the
> return value is a pointer, any value that is reachable from the pointer-argument might
> flow to all reachable values from the pointer-return... reachable memory from pointer-
> arguments are both sources and sinks of information flow, while reachable memory from
> pointer-return are sinks." CtChecker also documents the precision cost, which is a
> direct argument for the recommended refinements (libc model table + LLVM
> memory()/writeonly/argmemonly attributes): "both points-to analysis and information
> flow analysis remain very conservative without callee's implementation, making it hard
> to differentiate read/write effects on each individual [block]." Confidence is MEDIUM
> because it rests on one paper's external-call rule; the mod-set-summary design itself
> is unattested in the verified CT literature — neither confirmed nor refuted there.

### 10. ANSWER TO TASK 4 (NOVELTY): across everything verified, NO published system performs
interprocedural taint analysis at a post-register-allocation machine IR and inserts
speculation/timing barriers around secret-dependent regions. cio has post-regalloc MIR
transforms AND interprocedural taint, but the taint lives in a whole-binary BAP analysis
and it emits instruction substitutions, not barriers, for a non-speculative threat
model. Serberus has post-regalloc MIR passes and inserts fences, but is strictly
intraprocedural and secret-agnostic. CtChecker is interprocedural but LLVM-IR and
detect-only. The user's design point (TU-scoped interprocedural MIR taint -> ISB/DSB or
PSTATE.DIT) appears unoccupied.

*confidence:* **medium** — *vote:* derived (absence of evidence across verified claims; coverage-limited)

> This is a negative/novelty claim, only as strong as search coverage. It is well
> supported for cio, Serberus, and CtChecker (all read at primary-source level). It is
> NOT supported for the TASK 3 systems that returned zero verified claims: SLH /
> Ultimate SLH, Blade, Pitchfork, ct-verif, Binsec/Rel, Jasmin/FaCT/Vale/CryptOpt,
> Swivel/Venkman/retpoline, and binary rewriters. Prior-art risk is highest for Ultimate
> SLH and Blade (both LLVM-based hardening emitters) and for any Arm-targeted CSDB/SSBS
> work. Do not publish the novelty claim until those are checked.

### 11. CORRECTION FLAG / SOURCE DISAGREEMENT: three plausible-sounding claims were REFUTED in
verification — namely that cio operates ONLY on binaries and therefore has no post-
regalloc compiler-IR component. That reading follows from the paper's title ("Via
Binary-Code Transformations") but is contradicted by the paper's body, which places the
transforms in an LLVM MIR pass. Anyone citing cio from title or abstract alone will get
this wrong — including, potentially, a reviewer of the user's work.

*confidence:* **high** — *vote:* 0-3 on the title-only reading; 3-0 on the body-text reading

> Title: "...Via Binary-Code Transformations." Body S4.3: "We position these transform
> passes late in LLVM's compilation pipeline as a Machine IR (MIR) pass." S9: "we
> manually combine SS transforms with their dependent CS transforms in our LLVM MIR
> pass." The title refers to checking/analysis being done on compiled binaries (BAP) and
> to transforms operating on machine-level code; it does NOT mean cio is a binary
> rewriter.

---
## Refuted in verification — DO NOT REUSE

Note: every refuted claim below is the *title-only* reading of CIO — that it is a binary
rewriter with no compiler-IR component. The paper's body contradicts its own title. A
reviewer citing CIO from the title or abstract alone will make this mistake.

- CIO operates via BINARY-CODE TRANSFORMATIONS, not on LLVM IR or post-register-
  allocation machine IR, and targets INSTRUCTION-CENTRIC timing channels (e.g. data-
  dependent instruction timing such as subnormal FP / variable-latency ops) rather
  than speculative execution. This is stated in the title itself and bears directly on
  the user's Task 1 questions about level-of-operation and threat model.

- CIO's analysis/rewriting operates at the BINARY level, not at a post-register-
  allocation compiler IR: the artifact repository (ASPLOS'24 artifact evaluation,
  evaluated on libsodium) is built around the Binary Analysis Platform (BAP). This
  matches the paper title's "Binary-Code Transformations" and means CIO does not
  perform post-regalloc MIR-level interprocedural taint analysis.

- CIO operates on BINARY CODE (binary-code transformations), not on LLVM IR, not on
  machine IR / post-register-allocation MIR, and not on source — per the paper's own
  title as listed in DBLP. This means it is NOT a post-regalloc compiler-IR
  interprocedural taint pass, and therefore does not directly pre-empt the user's
  design point.

- The only David Kohlbrenner paper in ASPLOS 2024 Volume 2 is "Avoiding Instruction-
  Centric Microarchitectural Timing Channels Via Binary-Code Transformations," co-
  authored with Michael Flanders, Reshabh K. Sharma, Alexandra E. Michael, and Dan
  Grossman (pp. 120-136) — the strongest candidate for the paper the user calls "CIO"
  (a binary-level, not compiler-IR-level, timing-channel defense).

- The candidate CIO paper operates on BINARY CODE via binary-code transformations, not
  on LLVM IR or post-register-allocation machine IR, per its own title as indexed in
  the ASPLOS 2024 proceedings.

---
## Open questions (highest-value first)

- What do Ultimate SLH, Blade (POPL'21), and LLVM's SLH pass concretely do AT A CALL —
  do any carry a memory-effects/mod-set summary, or do they all harden secret-
  agnostically? This is the single unfilled hole and the most likely location of
  contradicting prior art for the novelty claim.

- Does ANY published system carry a COMPUTED per-function memory-effects (mod-set) taint
  summary of the form {writes-secret-through-arg i} / {writes-secret-to-global g} /
  {writes-secret-to-unknown-memory}? Nothing verified does. Is that genuinely novel in
  the CT/Spectre setting, or merely standard practice imported from the general taint
  literature (IFDS/FlowDroid-style summaries, LLVM ModRef) that the CT community never
  needed because it inlines or goes whole-program?

- cio explicitly went to whole-binary BAP analysis BECAUSE 'the compiler only sees
  individual translation units.' Is the user's TU-scoped design a liability reviewers
  will attack, and is the right answer a memory-effects summary at all — versus an
  LTO/link-time or binary post-pass phase?

- How does anyone handle indirect/function-pointer calls and external declarations in CT
  hardening? cio is silent; CtChecker uses a TOP-like rule. If no CT system has a
  principled answer, the user's TOP-for-unresolved-indirect default is defensible but
  unvalidated — and is TOP even sound enough for the speculative threat model (e.g. a
  callee leaving a secret in a callee-saved register, or DIT cleared on callee exit)?

---
## Sources

- [primary] https://homes.cs.washington.edu/~dkohlbre/  
  *angle:* anchor identification (primary sources) — 5 claims
- [primary] https://homes.cs.washington.edu/~dkohlbre/papers/cio-asplos24.pdf  
  *angle:* anchor identification (primary sources) — 5 claims
- [primary] https://github.com/counter-optimization  
  *angle:* anchor identification (primary sources) — 5 claims
- [primary] https://dl.acm.org/doi/10.1145/3620665.3640400  
  *angle:* anchor identification (primary sources) — 5 claims
- [primary] https://dblp.org/pid/131/5093.html  
  *angle:* anchor identification (primary sources) — 5 claims
- [primary] https://dblp.org/db/conf/asplos/asplos2024-2.html  
  *angle:* anchor identification (primary sources) — 4 claims
- [primary] https://arxiv.org/abs/2309.05174  
  *angle:* interprocedural call handling & memory-effects summaries — 5 claims
- [primary] https://users.cs.duke.edu/~dz132/pub/ecoop24.pdf  
  *angle:* interprocedural call handling & memory-effects summaries — 5 claims
- [primary] https://arxiv.org/pdf/2309.05174  
  *angle:* named Spectre/CT hardening tools and their call semantics — 5 claims
- [primary] https://cseweb.ucsd.edu/~dstefan/pubs/vassena:2021:blade.pdf  
  *angle:* named Spectre/CT hardening tools and their call semantics — 5 claims
- [primary] https://www.usenix.org/system/files/conference/usenixsecurity16/sec16_paper_almeida.pdf  
  *angle:* named Spectre/CT hardening tools and their call semantics — 5 claims
- [primary] https://arxiv.org/abs/1912.08788  
  *angle:* named Spectre/CT hardening tools and their call semantics — 5 claims
- [primary] https://arxiv.org/abs/2311.14246  
  *angle:* ARM DIT / CSDB / ISB-DSB hardening primitives — 5 claims
- [primary] https://trippel-lab.stanford.edu/pubs/mosier_SP24.pdf  
  *angle:* ARM DIT / CSDB / ISB-DSB hardening primitives — 5 claims
- [primary] https://www.usenix.org/system/files/sec23fall-prepub-278-zhang-zhiyuan.pdf  
  *angle:* ARM DIT / CSDB / ISB-DSB hardening primitives — 5 claims
- [primary] https://llvm.org/docs/SpeculativeLoadHardening.html  
  *angle:* ARM DIT / CSDB / ISB-DSB hardening primitives — 5 claims
- [primary] https://arxiv.org/html/2312.09770v1  
  *angle:* ARM DIT / CSDB / ISB-DSB hardening primitives — 5 claims
- [forum] https://lore.kernel.org/lkml/CAMj1kXGY5P_gnYpeMiucZvEHW-3_tcj4nr9XjgMcZFJXuLB9kw/T/  
  *angle:* ARM DIT / CSDB / ISB-DSB hardening primitives — 5 claims
