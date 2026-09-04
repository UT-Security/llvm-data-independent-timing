# Cio (counter-optimization) - how their taint / dataflow analysis actually works

Research report. Everything below is read from source unless marked **unverified**.

Paper: Michael Flanders, Reshabh K. Sharma, Alexandra E. Michael, Dan Grossman, David
Kohlbrenner. *Avoiding Instruction-Centric Microarchitectural Timing Channels Via
Binary-Code Transformations*. ASPLOS 2024. https://doi.org/10.1145/3620665.3640400

Local clones used for all file references:
`$ROOT = <scratchpad>/cio_research/{checker,cio,cio-artifact}`

Permalink base for the analysis:
`https://github.com/counter-optimization/checker/blob/71d0300/bap/interval/<file>`

---

## 1. Repo inventory

Five public repos in https://github.com/counter-optimization (all last touched Jan-Apr 2024):

| Repo | What it is | Language | Contains the analysis? |
|---|---|---|---|
| **`checker`** | The BAP plugin (`uarch-checker`) - **this is the analysis** - plus the Rosette/Serval rewrite-rule verification and the test suite | OCaml (~13.5k lines in `bap/interval/`), Racket (~240 `.rkt`) | **YES** |
| **`cio`** | Top-level driver: a 553-line bash script `cio`, a Makefile, the libsodium annotation file, eval harness and captured eval data. Submodules: `libsodium`, `checker`, `llvm-project` | Bash / C / Python | No (orchestration + data) |
| **`cio-artifact`** | ASPLOS'24 AEC Dockerfile + README. Prebuilt image at zenodo.org/records/10594315 | Dockerfile | No |
| **`llvm-project`** | "Fork of LLVM with secret arg annotations, scratch register reservations, and CS and SS mitigation passes" - the X86 MIR rewriter | C++ | No (the *rewriter*) |
| **`.github`** | org profile | - | No |

Note `cio/.gitmodules` points `llvm-project` at `git@github.com:Flandini/llvm-project.git`
(the author's personal fork), not the org copy.

Inside `checker`:
- `bap/interval/*.ml` - **the whole analysis** (58 files, 13,531 lines).
- `synth/*.rkt` - Rosette code that *verifies the rewrite rules*, plus dead synthesis code.
- `serval/` - a vendored, modified copy of Serval (x86 semantics for the Rosette proofs).
- `test/` - C and asm test cases (`comp-simp/`, `silent-stores/`, `interproc/`, `pathsensitivity/`).

There is no separate "silent-store-checker" repo; the silent-store checker is
`$ROOT/checker/bap/interval/silent_stores.ml`.

---

## 2. The analysis itself

**Framework: BAP** (Binary Analysis Platform), OCaml, as a `Project.register_pass'`
plugin named `uarch-checker`. Entry point
`$ROOT/checker/bap/interval/uarch_checker.ml:70-88`. It operates on BAP's BIL/BIR
(lifted x86-64), not on raw instructions.

**Technique: classical abstract interpretation over a reduced product domain, solved by
a `Graphlib.fixpoint` worklift with widening** - plus a *trace-partitioned* (path-sensitive)
state, plus an optional Z3 symbolic-execution stage used only as a last-ditch alert filter.
It is not primarily symbolic execution and not SMT-based.

### The product domain (`driver.ml:18-21`)

```ocaml
module ProdIntvlxTaint = DomainProduct(Wrapping_interval)(Checker_taint.Analysis)
module WithTypes       = DomainProduct(ProdIntvlxTaint)(Type_domain)
module FinalDomain : Numeric_domain.Sig = DomainProduct(WithTypes)(Bases_domain)
```

Four components, every one of them a full `Numeric_domain.Sig` (`numeric_domain.ml`)
with `add/sub/mul/.../extract/concat` transfer functions:

1. **`Wrapping_interval`** (`wrapping_interval.ml`, 812 lines) - wrapping integer intervals.
   Does the real work: proves an operand *cannot* be 0/1/all-ones.
2. **`Checker_taint.Analysis`** (`checker_taint.ml`, 133 lines) - **the taint domain**.
3. **`Type_domain`** (`type_domain.ml`) - `Ptr | Scalar | Unknown | Undef`.
4. **`Bases_domain`** (`bases_domain.ml`) - a set over `Region = Global | Heap | Stack`.

`DomainProduct` (`abstract.ml:202-232`) is a plain componentwise product; the components
are addressed by a Frama-C/EVA-style runtime domain key (`domain_key.ml`, the comment
says "This is the naive external domain key from EVA in the Frama-C framework").

### The taint domain in full

`$ROOT/checker/bap/interval/checker_taint.ml` - the entire lattice is:

```ocaml
type t = Notaint | Taint
let bot = Notaint
let top = Taint
let make_top _width _signed = top
let join x y = match x, y with
  | Taint, _ -> Taint | _, Taint -> Taint | _ -> Notaint
let binop = join
let add = binop  let sub = binop  let mul = binop  ... let logxor = binop
let ident = fun x -> x
let neg = ident   let lnot = ident
let extract exp hi lo = exp
let concat = join
let of_int ?(width = 64) _ = Notaint
let of_word _ = Notaint
```

That is it. **One bit. Every binary operator is join. Constants are untainted.
`extract` (sub-register access) is the identity.** 133 lines including boilerplate.

The single most consequential line is:

```ocaml
let make_top _width _signed = top   (* = Taint *)
```

Because `DomainProduct.make_top width signed = X.make_top .., Y.make_top ..`
(`abstract.ml:232`), **every `N.top` / `N.make_top` anywhere in the analysis is a
tainted value.** "I don't know" and "secret" are the same element. This is what lets
the rest of the system be as crude as it is and still over-approximate.

### Key files and functions

| Concern | File | Key names |
|---|---|---|
| Plugin entry / CLI | `uarch_checker.ml` | `pass`, `register_passes` |
| Whole-program driver | `driver.ml` | `check_config`, `run_analyses`, `propagate_taint`, `loop` |
| Abstract interpreter | `abstract.ml` | `AbstractInterpreter`, `denote_exp`, `denote_def`, `denote_jmp` |
| Trace partitioning | `trace.ml` (983 lines) | `Directives`, `Env`, `AbsInt.Make`, `combine_partitions` |
| Memory domain | `abstract_memory.ml` (782) | `load_of_bil_exp`, `store_of_bil_exp`, `init_arg`, `havoc_on_call` |
| Interproc taint | `uc_inargs.ml` (545) | `TaintSummary`, `TaintContext`, `Analyzer.analyze_ctxt`, `denote_exp` |
| Taint lattice | `checker_taint.ml` (133) | `Analysis` |
| Comp-simp checker | `comp_simp.ml` (304) | `check_binop`, `check_elt` |
| Silent-store checker | `silent_stores.ml` (323) | `check_elt` |
| DMP checker | `dmp.ml` (99) | `check_elt` |
| Z3 stage | `symbolic.ml` (789) | `Executor`, `Solver.mk_solver_s ctxt "QF_BV"` |
| Alerts / output | `alert.ml` (860) | `csv_header`, `RemoveUnsupportedMirOpcodes`, ... |

`symbolic.ml:20-26` creates a Z3 context with `"timeout", "500"` (ms) and a `QF_BV`
solver. It is only reachable from `silent_stores.ml` and is disabled by
`--uarch-checker-no-symex` / `--skip-double-check`.

---

## 3. What is tainted, and how is it seeded

**A flat text file of `function_name,secret_argument_index` lines.** Nothing else.
No source annotations, no types, no memory regions, no command line secrets.

Grammar, verbatim from `$ROOT/checker/bap/interval/config.ml:16-26`:

```
config_file ::= ( init_fn )* ( analyze_fn ) ( analyze_fn )*
init_fn     ::= init func_symbol_name '\n'
analyze_fn  ::= func_symbol_name,secret_arg_idx '\n'
func_symbol_name ::= <symbol_name_id_string> // no commas in the name
secret_arg_idx   ::= <integer >= 0>
```

Parsed by `Config.Parser.parse_config_file` into
`{ init_fns; target_fns; secret_args : int Set.t String.Map.t }`.

The `target_fns` are simultaneously (a) the analysis entry points - the worklist is
seeded with exactly these (`driver.ml:702`, `check_config`) - and (b) the taint sources.

### How coarse is it? The whole of libsodium is 64 lines.

`$ROOT/cio/libsodium.uarch_checker.config` is **64 lines covering 24 distinct functions**.
Verbatim opening:

```
crypto_pwhash_argon2id,0
crypto_pwhash_argon2id,2
crypto_pwhash_argon2id,3
crypto_pwhash_argon2id,4
crypto_sign,0
...
crypto_onetimeauth_poly1305_donna_verify,3
```

The `init <fn>` form is not used at all in the libsodium config.

### The seeding is register-level, and there is no pointee seeding

Index `N` is mapped straight to a SysV AMD64 argument **register**:

`uc_inargs.ml:79-85`
```ocaml
let taintedargs = Int.Set.to_list taintedidxs
                  |> List.map ~f:ABI.arg_from_idx     (* 0->RDI, 1->RSI, 2->RDX, ... *)
                  ...
KB.provide tainted_regs st taintedargs
```

and in the main pass, `abstract_memory.ml:422-453` (`init_arg`):

```ocaml
let init_val = N.make_top init_val_width signed in
let init_w_bases_set = set_based init_val init_bases in
let should_be_tainted = ... Config.get_taint_arg_indices subname config ...
let tainter = if should_be_tainted then set_taint else set_untaint in
set name (tainter init_w_bases_set) mem
```

So `crypto_onetimeauth,3` - where argument 3 is `const unsigned char *k`, a **pointer to
the key** - literally means "**RCX holds Taint at function entry**". The 32 bytes of key
material behind that pointer are never seeded, marked, or modelled. There is no
`pointee` concept anywhere in the codebase.

That works only because of the memory model in section 5.

### There *is* a source-level attribute, but it was not used for libsodium

The LLVM fork does define a real Clang attribute
(`clang/include/clang/Basic/Attr.td:624` in `counter-optimization/llvm-project@d9643d8`):

```
def Secret : InheritableAttr {
  let Spellings = [Clang<"secret">];
  let Subjects = SubjectList<[Function, Var, Field, ParmVar]>;
```

so a developer can write `__attribute__((secret))` / `[[clang::secret]]` on a parameter,
with a matching IR attribute `def Secret : EnumAttr<"secret", [FnAttr, ParamAttr]>`
(`Attributes.td:303`). `llvm/lib/Analysis/HandlesSecrets.cpp` walks `F.args()` and, for
each argument carrying `Attribute::Secret`, appends `<FuncName>,<ArgIdx>` to
`<FuncName>.ciocc.secrets.csv` - **exactly the grammar `config.ml` parses**. The compiler
does not emit binary metadata; it emits the checker's config file.

**But for the evaluation this path is bypassed.** The `cio` driver defines
`ALL_SECRETS_CSV` and never passes it to `bap`; only the hand-written
`--uarch-checker-config-file=./libsodium.uarch_checker.config` is used. So the answer to
"how many annotations does a real target need" is: **64 lines, hand-written, no source
changes to libsodium at all.**

---

## 4. Data vs address: they do not distinguish. THE ANSWER TO YOUR CRUX QUESTION.

**There is exactly one taint bit and it is not qualified by channel.** There is no
"data taint" vs "address taint", no `PointerTaint`/`ValueTaint` split, no secret-address
tracking. `grep`-ing the whole analysis for a second taint kind finds nothing:
`checker_taint.ml` defines `type t = Notaint | Taint` and that type is the only taint
type in the tree.

They *do* carry two adjacent facts that a reader might mistake for an address channel,
and it is worth being precise about what each one is:

- **`Type_domain`** (`Ptr | Scalar | Unknown | Undef`) records *pointer-ness*, not
  pointer-taint. Its transfer functions are pointer arithmetic typing rules
  (`type_domain.ml:74-95`): `Ptr + Scalar = Ptr`, `Ptr - Ptr = Scalar`,
  `Ptr + Ptr = Unknown`, and `let general_binop x y = s` - **every other operation
  (mul, div, shifts, and, or, xor) returns `Scalar`**. It exists to drive the memory
  model, not to classify leaks.
- **`Bases_domain`** is a may-point-to set over exactly three abstract regions,
  `Global | Heap | Stack` (`common.ml:288-295`). Not per-allocation, not per-frame-object,
  not per-field. `bases_domain.ml:70-94` makes every arithmetic operator `join`,
  so bases propagate through arithmetic unconditionally.

Neither is a taint channel. Taint is the separate, single, unqualified bit.

### Do they over-taint? Yes, massively, and by construction.

Two lines do most of it.

**(a) In the interprocedural pass, every load is tainted, unconditionally.**
`$ROOT/checker/bap/interval/uc_inargs.ml:280-283`:

```ocaml
let rec denote_exp (e : Bil.exp) (st : env) : (T.t * env) =
  match e with
  | Bil.Load (_, _, _, _) -> (T.Taint, st)
  | Bil.Store (_, _, _, _, _) -> (T.Notaint, st)
```

Not "tainted if the address is tainted", not "tainted if the pointed-to region is
tainted" - **`Taint`, always.** Reading anything from memory yields a secret. That single
line subsumes every pointee-seeding, points-to and provenance question in the
interprocedural phase.

**(b) In the main pass, every load that is not a precisely-resolved small stack access
returns top, and top is tainted.** `abstract_memory.ml:508-540` (`load_of_bil_exp`):

```ocaml
let is_stack_load = Bases_domain.Bases.equal regions Bases_domain.stacks in
let max_ptd_to_elts = Z.of_int 64 in
if is_stack_load && Z.lt offs_size max_ptd_to_elts
then (* load from stack: join the tracked cells *) ...
else
  (* else, return top *)
  let numbits = bap_size_to_int size in
  Ok (N.make_top numbits signed, m)      (* <-- tainted *)
```

So a load through a heap pointer, a global pointer, a pointer whose region set is not
exactly `{Stack}`, or a stack pointer whose offset interval spans 64+ elements, produces
a **tainted** value regardless of what the pointer's own taint was.

Also `denote_def` in the interprocedural analyzer never kills taint
(`uc_inargs.ml:322-330`):

```ocaml
let result, env' = denote_exp rhs env in
if T.is_tainted result
then String.Set.add env' varname, st
else env, st                    (* note: returns env, does NOT remove varname *)
```

The `else` branch returns the *original* `env`, so a variable that was tainted stays
tainted even after being overwritten with a constant. Taint is gen-only, never killed.

### Do they care about over-tainting? Their own numbers split sharply by checker.

The checker instruments itself (`uc_stats.ml`). The captured run in
`$ROOT/cio/silent-store-pruning-all-indices-cio-build/bap.log:553099-553106` reports,
for a silent-store-only build of all of libsodium:

```
ss stats:
{
"total_considered" : "4229"
"taint_pruned" : "38"
"interval_pruned" : "267"
"interproc_pruned" : "0"
"symex_pruned" : "80"
"unsupported_pruned" : "0"
"interval_verified" : "0"
"symex_verified" : "0"}
```

**Taint pruned 38 of 4,229 stores - 0.9%.** The interval domain pruned 7x more (267),
the Z3 stage 2x more (80).

The paper's Table 3 gives the same breakdown for both checkers on libsodium, and the
picture is **not uniform**:

| Checker | Total considered | Taint pruned | Memory domain pruned | Sym. comp. pruned | Transformed |
|---|---|---|---|---|---|
| **SS** | 2,695 | **1** (0.04%) | 428 (16%) | 23 (0.9%) | 1,879 |
| **CS** | 13,198 | **4,940 (37%)** | 2,144 (16%) | n/a | 4,858 |

So the honest conclusion is checker-dependent:

- **For silent stores, taint is essentially a no-op** (1 instruction out of 2,695 in the
  paper; 38 of 4,229 in the captured log). Every store in crypto code touches something
  the analysis considers secret.
- **For computation simplification, taint does real work** - 37% of candidate binops
  pruned. This is because CS considers *every* arithmetic operation in every reachable
  function, including plenty of loop counters, lengths and pointer arithmetic that are
  genuinely public, whereas SS only considers stores, which in crypto are almost always
  of secret-derived data.

Either way, **the coarsest possible taint analysis - one bit, no channels, no
provenance, no declassification, `top = secret` - is sufficient to deliver that 37%.**
Nothing in the pruning story requires precision that a richer taint model would add. And
across both checkers the value domain prunes a comparable or larger share.

Corroborating evidence of over-tainting in the alert data: the flagged-function list in
`$ROOT/cio/build_dir_for_transform_eval/checker.alerts.csv` includes
`sodium_bin2base64` (2,516 alerts) - base64 encoding of already-public output - and the
log tail shows alerts inside statically linked libc (`strchr`, `strlen`, `sysconf`,
`bap.log:553085-553087`).

The paper never uses the term "over-tainting" and reports no taint-precision numbers
beyond Table 3. They do not treat it as a defect.

### The precision bug that actually bit them is *under*-tainting

`$ROOT/checker/bap/interval/abi.ml:97` maps argument indices to SysV registers for
indices **0-5 only** (RDI, RSI, RDX, RCX, R8, R9) and returns `None` beyond that;
`uc_inargs.ml:79-83` then silently filters the `None`s out
(`List.filter ~f:Option.is_some`). But **4 of the 64 lines in
`libsodium.uarch_checker.config` use index 8** (`crypto_aead_aes256gcm_encrypt,8`,
`..._decrypt,8`, `crypto_aead_chacha20poly1305_ietf_encrypt,8`, `..._decrypt,8`) - the
stack-passed key arguments. Those annotated secrets are **never seeded at all**, with no
warning. `abstract_memory.ml:370-377` has the matching gap and at least fails loudly
there ("arg_idx is either on the stack (not yet handled) or invalid"), but the path
actually used does not.

That is the opposite failure from the one you are worried about: their design is so
over-approximate that a *dropped seed* is the realistic precision bug, and it went
unnoticed.

Corroborating evidence of over-tainting in the alert data: the flagged-function list in
`$ROOT/cio/build_dir_for_transform_eval/checker.alerts.csv` includes
`sodium_bin2base64` (2,516 alerts) - base64 encoding of already-public output - and the
log tail shows alerts inside statically linked libc (`strchr`, `strlen`, `sysconf`,
`bap.log:553085-553087`).

They do not appear to consider this a defect to fix. There is no false-positive
reduction machinery aimed at taint; all four pruning stages except the first are
value-based.

---

## 5. Memory model

**A three-region, stack-focused cell model with pessimistic loads and dropped
imprecise stores.** No points-to analysis in any conventional sense.

The design intent is stated as a comment at the end of
`$ROOT/checker/bap/interval/abstract_memory.ml:774-782`:

```ocaml
(** new memory domain:
    - intraprocedural
      - don't actually track globals
      - if the memory hasn't been initialized yet, then it is TOP and tainted
      - on widening, forget all memory cells
      - it might be a pointer to stack, really for now, just focus on handling the
        stack well
  *)
```

Concretely:

- **State** is `{ cells : C.Set.t; env : N.t String.Map.t; bases : basemap }`. A cell is
  `(region, offset-interval, width)` and its contents live in `env` under a synthesized
  name. Regions are only `Global | Heap | Stack`.
- **Loads** (`load_of_bil_exp`, `:508-540`): precise only for `{Stack}`-based pointers
  with an offset interval spanning fewer than 64 elements; otherwise `N.make_top` =
  **tainted top**. An address the analysis cannot pin down yields a secret. A missing
  cell also reads as top (`load`, `:487-506`).
- **Stores** (`store_of_bil_exp`, `:620-647`): if the address range is unconstrained,
  the store is **silently dropped**:

  ```ocaml
  (match ensure_offs_range_is_ok ~offs ~width with
   | Error err -> Ok m                       (* store to unknown address: no-op *)
   | Ok _ -> ... store to each (base, offs) pair ...)
  ```

  `ensure_offs_range_is_ok` (`:546-562`) errors when the offset interval covers more
  than 64 members ("probably an unconstrained pointer"). And `store` (`:467-478`) does a
  **strong update** - it removes overlapping cells and installs the new one - even when
  the base set has several members. Both choices are locally unsound; both are covered
  by loads defaulting to tainted top.
- **Join** intersects cell sets (`merge`, `:719-729`: `Set.inter cells1 cells2`). The
  authors justify this at `:753-762`: a cell absent from the set reads as top during a
  load, so intersection is the over-approximating direction. On widening they forget all
  cells.
- **Secret-derived addresses**: nothing special. A tainted index is not propagated into
  the loaded value on the precise stack path (`load` returns the cell's own value, not
  joined with `idx_res`), which is a genuine hole; in practice a secret-derived index has
  a wide interval, fails the `< 64 elements` test, and falls into the `N.make_top`
  branch, so the loaded value comes back tainted anyway. The DMP checker is the only
  place an address property is examined, and it examines a *bitvector* fact (is bit 60
  set) rather than taint - see section 7.

---

## 6. Interprocedural

Two separate mechanisms, both crude.

### (a) A register-level interprocedural taint pre-pass

`driver.ml:196-260` (`propagate_taint`) runs `Uc_inargs.Analyzer` to a fixpoint *before*
the main analysis, and its entire output is **"which argument registers are tainted at
each function's entry"**, stashed via `Uc_preanalyses.set_tainted_args` and read back at
`driver.ml:359`.

The summary type is (`uc_inargs.ml:160-178`):

```ocaml
type t = { input : String.Set.t; output : String.Set.t }   (* sets of REGISTER NAMES *)
let default =
  let top_input = ABI.gpr_arg_names |> String.Set.of_list in
  make ~input:top_input ~output:String.Set.empty
```

**Register names only. No mod-set, no memory effects, no return-value/heap summary.**
The context is `{subname; argvals}` where `argvals = env ∩ gpr_arg_names`
(`TaintContext.of_state`, `:141-144`), so it is context-sensitive in principle - but
`results_for` (`:388-450`) looks a previous result up by `subname` alone and *unions* the
input sets (`:425`), collapsing contexts back together. Effectively context-insensitive.

Recursion is handled by `currently_analyzing_sub` returning the in-progress summary
(`:445-449`), and a caller worklist re-queues callers when a summary grows
(`update_worklist`, `:364-372`).

### (b) The main pass is intraprocedural with total memory havoc at every call

`abstract.ml:503-521` (`denote_jmp`):

```ocaml
match Jmp.kind j with
| Call c ->
  (match Call.target c with
   | Indirect exp -> set_smalloc_return exp
   | _ -> E.havoc_on_call st)
| Goto (Indirect exp) -> set_smalloc_return exp
| Goto _ -> st | Ret _ -> st | Int _ -> st
```

and `havoc_on_call` (`abstract_memory.ml:679-691`) sets **every memory cell and RAX to
`N.top`** - i.e. to tainted top. There is no inlining and no memory summary; a call
simply destroys all memory knowledge and taints it.

The whole-program structure (`driver.ml:740-787`) is a **worklist over functions**:
start from the config's `target_fns`, analyze each one intraprocedurally, collect its
callees via `Callees.Getter`, add them to the worklist. Each function is analyzed once
(`processed` set), with no calling context beyond the tainted-arg-register set.

### Indirect calls

`callees.ml:78-121` (`get_callee_of_indirect`) evaluates the target expression in the
interval domain and **requires it to resolve to exactly one address**, else it returns
an error:

```
"in get_callee_of_indirect, jmp indirect exp %a points to more than one location (%s)"
```

`driver.ml:679-682` then does `List.filter callee_analysis_results ~f:Or_error.is_ok` -
**unresolvable indirect callees are silently discarded and never analyzed.**

To make libsodium's runtime-dispatch tables resolvable at all they wrote a hardcoded,
target-specific hack: `global_function_pointers.ml`, module
`Libsodium.Analysis`, with `let toplevel_init_fn_name = "sodium_init"` and
`let which_implementation_idx_to_pick = 0`. It walks `sodium_init` and its direct
callees for constant stores of function pointers into globals and pre-seeds them
(`driver.ml:733`). That is what the `init <fn>` config form is for.

### Uninstrumented code / libc

- Functions whose name contains `plt` or `interrupt`, plus a hardcoded list, are skipped:
  `common.ml:262-285` (`AnalysisBlackList`), blacklisting `sodium_init`,
  `get_cpu_features`, and `Dmp_helpers.checker_blacklisted_fns` =
  `smalloc, sodium_smalloc, sfree, updateStackAddr, unsetBit, smemcpy, handle64BitStore`.
- `sub_is_not_linked sub = Term.enum blk_t sub |> Seq.is_empty` - bodyless (dynamically
  linked) functions are skipped.
- But libsodium is analyzed as a **statically linked "jammed together" object**
  (`jammed.together.o`), so libc *is* walked when present - and duly produces alerts in
  `strchr`, `strlen`, `sysconf` (`bap.log:553085-553087`).
- Any call at all still havocs memory to tainted top, so a skipped callee is
  conservative on the memory side, though not on its own instructions (they are never
  checked, hence never protected - a false negative).

---

## 7. What they protect and how

### Correction to a common framing: it is *not* binary rewriting.

The paper title says "binary-code transformations", but the shipped pipeline
(`$ROOT/cio/cio`, the 553-line bash driver) is **analyze the binary, rewrite in the
compiler backend**:

1. Compile the target with `-mllvm --x86-gen-idx` (`cio:279`) so each machine instruction
   carries a stable reverse-postorder index. Link into `jammed.together.o`.
2. Run BAP on that object (`cio:348-378`):
   ```
   bap --pass=uarch-checker --uarch-checker-config-file=$CONFIG_FILE \
       --uarch-checker-output-csv-file=$CHECKER_ALERTS_CSV \
       --uarch-checker-taint-cache=$TAINT_CACHE [--uarch-checker-ss] [--uarch-checker-cs] ...
   ```
3. **Recompile from source** with the alert CSV fed back into X86 MIR passes
   (`cio:409`):
   ```
   -mllvm --x86-gen-idx -mllvm --x86-ss -mllvm --x86-ss-csv-path=${CHECKER_ALERTS_CSV}
   -mllvm --x86-cs -mllvm --x86-cs-csv-path=${CHECKER_ALERTS_CSV}
   -mllvm --x86-gen-deidx -mllvm -global-scratch -mllvm -gs-size=8
   ```
4. Optionally re-run BAP on the mitigated binary with `--uarch-checker-double-check`
   (`cio:466-505`), expecting no alerts.

**This should matter to you: their analysis/rewriter interface is a CSV keyed by
`(function name, MIR opcode, RPO instruction index)`, and the rewriter is an X86 MIR
pass - exactly the level you work at.** Header, `alert.ml:542`:

```
subroutine_name,mir_opcode,addr,rpo_idx,tid,problematic_operands,left_operand,
right_operand,live_flags,is_live,alert_reason,description,flags_live_in
```

Note `problematic_operands` (which operand indices are dangerous, so the rewriter only
neutralizes those) and `flags_live`/`flags_live_in` (so the rewriter knows whether its
replacement sequence must preserve EFLAGS).

**The index trick that makes binary alerts bind to MIR instructions.**
`--x86-gen-idx` (`llvm/lib/Target/X86/X86MitigationIdx.cpp`) inserts a marker
`sbb r11, <sequence-index>` before every non-pseudo, non-call `MachineInstr`. On the
binary side, `checker/bap/interval/idx_calculator.ml` recognizes an `sbb` whose
destination and first source are r11 with an immediate operand, and recovers the index.
The CS/SS MIR passes then re-read the alert CSV and match on `(SubName, InsnIdx)`,
asserting the recorded opcode agrees. `--x86-gen-deidx`
(`X86MitigationDeIdx.cpp`) strips the markers from the final build. This is how a
*binary* analysis of a *linked* object drives a rewrite in a *second compile* of the
source.

**The rewriter, in LLVM (verified from the fork):**
- `llvm/lib/Target/X86/X86CompSimpHardening.cpp` - **12,788 lines**
- `llvm/lib/Target/X86/X86SilentStoreHardening.cpp` - 3,866 lines
- `X86MitigationIdx.cpp` (116), `X86MitigationDeIdx.cpp` (109), `X86CompSimpMap.csv`
- `llvm/lib/Transforms/Scalar/InsertScratchGlobals.cpp` - emits N global `i64`s
  (`-global-scratch -gs-size=8`) as scratch *memory*
- `X86RegisterInfo.cpp::getReservedRegs` unconditionally reserves **R10, R11, R12, R13
  and XMM/YMM12-15** (escape hatch `-x86-do-not-reserve-reg`). Comments in the fork: R11
  for comp-simp, R10 "for silent store so that we can save and restore the eflags",
  R12/R13 "for shift". That is the "scratch register reservation" in the repo
  description - it exists because some transforms cannot spill to memory.

The SS mitigation inserts a **blinding store** of a value `Z` distinct from both the old
and the new value (high bits of the old value concatenated with low bits of the new,
inverted) immediately before the real store, so the real store is never silent. SS passes
must run **before** CS passes (paper §7).

Note the size of `X86CompSimpHardening.cpp`: **the hard part of this system is not the
analysis, it is the per-opcode rewrite table.** The analysis is 13.5k lines of OCaml for
*all three* checkers plus the whole abstract interpreter; the comp-simp rewriter alone is
12.8k lines of C++.

### The three targeted optimizations, and how the decision is made

All three checkers use the taint bit as a **first-stage filter only**, then value
reasoning.

**1. Computation simplification** (operand-value-dependent early-out / zero-shortcut
circuits). `comp_simp.ml:124-240` (`check_binop`). Two-stage:

```ocaml
let untainted = (not binop_is_sub && not tl && not tr) || (binop_is_sub && not tr) in
if untainted then (estats_incr_taint_pruned st; emp)
else begin
  (match binop with
   | Bil.PLUS  -> left_bad := WI.contains left_zero wl; right_bad := WI.contains right_zero wr
   | Bil.MINUS -> right_bad := WI.contains right_zero wr
   | Bil.TIMES -> left_bad := WI.contains left_zero wl || WI.contains left_one wl; ...
   | Bil.DIVIDE -> left_bad := WI.contains left_zero wl; right_bad := WI.contains right_one wr
   | Bil.LSHIFT | Bil.RSHIFT | Bil.ARSHIFT -> (* zero on either side *)
   | Bil.AND | Bil.OR | Bil.XOR -> (* zero or all-ones on either side *)
   | Bil.EQ | Bil.NEQ | Bil.LT | Bil.LE | Bil.SLT | Bil.SLE | Bil.MOD | Bil.SMOD -> ());
  if interval_pruned then (estats_incr_interval_pruned st; emp) else (* alert *) ...
```

The dangerous operand values are exactly **0, 1, and all-ones** - nothing else. Note the
asymmetry for `MINUS`: only the right operand is checked, because `x - 0 = x` is the
shortcut and `0 - y` is not. Comparisons and modulo are considered but never flagged.

**2. Silent stores** (a store that writes the value already present is dropped by the
cache, leaking value equality). `silent_stores.ml:236-315`. **Three** stages:

```ocaml
| Bil.Store (mem, idx, new_data, endian, size) ->
  let new_data  = Interp.denote_exp st.tid new_data in
  let load_of_prev_data = Bil.Load (mem, idx, endian, size) in
  let prev_data = Interp.denote_exp st.tid load_of_prev_data in
  ... if is_tainted prev_data || is_tainted new_data
      then if could_be_eq prev_data new_data          (* interval equality *)
           then if do_symex then (* Z3 QF_BV on a bounded backward slice *) ...
```

Stage 3 builds a backward dependency slice bounded by `dep_bound` instructions via
`Reachingdefs`, recovers rough operand types with `Type_determination`, and asks Z3
(500 ms timeout) whether old and new values can be equal. This is the "double-check"
phase, off by default in the artifact.

**3. DMP - data memory-dependent prefetcher** (the Apple M-series / "GoFetch"-class
channel: the prefetcher dereferences values that look like pointers). `dmp.ml:44-95`.
This is the only checker that looks at the address separately from the data, and it does
so with the *bitvector* domain, not taint:

```ocaml
(* When is a store unsafe?:
   if it is a store of tainted data to a pointer without
   bit 60 set in the pointer AND it is not between a LAHF and SAHF *)
...
if could_be_tainted denoted_store_exp &&
   could_have_bit_60_unset denoted_pointer_exp
then (* alert: "tainted value reaching store without 60th bit set" *)
```

The mitigation is an allocator (`smalloc`) that returns pointers with bit 60 set, so the
DMP will not chase them; `Abstract_bitvector` tracks that bit through the program and
`Dmp_helpers.FindSafePtrBitTestPass` finds the guards. This is a **partial, ad-hoc
address channel** - one bit of one address property, bolted on for one microarchitecture.

**Load value prediction is not targeted** by any checker in this tree (**unverified**
whether the paper discusses it).

### The decision is taint-gated but instruction-class-driven

The instruction must be (a) of a targeted class (a `BinOp` in a non-flag `Def` for CS; a
`Store` for SS/DMP), (b) touching something tainted, and (c) not provably safe by
interval (and optionally Z3) reasoning. Given that taint prunes ~1%, in practice
**(a) and (c) do nearly all the selection work.**

### Post-processing that silently drops alerts (`driver.ml:797-820`)

`RemoveSpuriousCompSimpAlerts`, `RemoveAlertsForCallInsns` (call instructions are "not
supported by us right now", `alert.ml:574-597`), `RemoveAndWarnEmptyInsnIdxAlerts`,
`CombinedTransformFixerUpper` (reclassifies read-modify-write memory opcodes like
`add64mr` from CS to SS), `RemoveDuplicateAlerts`, and:

`alert.ml:600-626`, `RemoveUnsupportedMirOpcodes` - a hardcoded prefix list whose alerts
are **dropped with a warning**:

```ocaml
let unsupported_mir_opcode_prefixes =
  ["xorps"; "adc"; "shld"; "shrd"; "sbb"; "psub"; "psrl"; "punpck"; "psll";
   "por"; "psbu"; "pand"; "pxor"; "pshuf"; "shrd"; "rol"; "ror"; "div"]
```

logged as "Num unsupported opcode alerts removed (to be implemented in compiler)".
These are acknowledged, deliberate false negatives - and they cover the entire SSE/AVX
surface, plus carry chains (`adc`/`sbb`) and rotates, which is most of what optimized
crypto actually runs.

---

## 8. Soundness posture

**They verify the rewrite rules. They do not verify the analysis. There is no
soundness claim for the taint analysis anywhere in the code.**

### What the Rosette/Serval verification actually proves

`$ROOT/checker/synth/verify.rkt:18-30`:

```racket
(define (comp-simp-verify attempt attempt-cpu spec spec-cpu)
  (comp-simp:assume-all-regs-equiv spec-cpu attempt-cpu)
  (comp-simp:assume-all-flags-equiv spec-cpu attempt-cpu)
  (comp-simp:run-x86-64-impl #:insns attempt #:cpu attempt-cpu #:assert-cs false ...)
  (comp-simp:run-x86-64-impl #:insns spec #:cpu spec-cpu)
  (comp-simp:assert-all-regs-but-scratch-equiv spec-cpu attempt-cpu)
  (when (check-flags) (comp-simp:assert-all-flags-equiv spec-cpu attempt-cpu)))
```

driven by `(verify (comp-simp-verify ...))` at `verify.rkt:40-41`, with x86 semantics
from a vendored Serval (`$ROOT/checker/serval/serval/x86/interp/*.rkt`, 39 opcode files;
symbolic CPU at `serval/x86/base.rkt:82-88`), discharged by Boolector.

The theorem is **"the replacement sequence is functionally equivalent to the original
instruction on all non-scratch GPRs, for all symbolic inputs"** - scratch = r10-r13,
excluded. Flags are only compared with `-f`, and the offline driver
`$ROOT/cio/offline-verification/verify_transforms.sh:79` passes only `-v`, so **flags
equivalence is not checked in the shipped run**.

Critically, `#:assert-cs false` on line 22 means the *leak-freedom* half - "no
instruction in the replacement sequence ever sees a special operand value" - is
**disabled inside `verify`**. The predicate exists
(`synth-comp-simp-defenses-macrod.rkt:440-450`, specials are exactly
`zero-for-bw`/`one-for-bw`/`ones-for-bw` at `:377-379`) but in the diagnosis path it
takes the `(sat? model)` branch and merely *prints* "ASSERT FAIL". The
`(assert (! special-cond))` form is used for *synthesis*, not for the shipped
verification. So leak-freedom of the replacement sequences is assumed by construction,
not SMT-checked.

Coverage: `$ROOT/cio/offline-verification/all_insns.txt` lists **134 opcodes**;
`transform-list.rkt` provides transforms for **58** of them; **76 have none** (all the
vector/SSE/AVX forms, `CMP*`, `TEST*`, `ADC*`, `SBB32rr`, `LEA64r`). Per sequence the
check is exhaustive over inputs; over the opcode set it is a subset. `MUL64`/`IMUL64`
were too large to verify monolithically and are split into five separately-verified
partial products whose **composition is not SMT-checked** (`mul-transforms.rkt:1532+`).
The silent-store Rosette file `synth/silent-stores.rkt` is **dead and broken** - it
references an unbound `spec` at `:52` and never asserts the disequality it was meant to;
nothing in `verify.rkt` or `verify_transforms.sh` references it. The shipped silent-store
logic is the OCaml checker, unverified.

`grep -rin taint $ROOT/checker/synth $ROOT/checker/serval/serval/x86` returns **zero
hits**. There is no machine-checked link between "this instruction was flagged" and
"this rewrite suffices", and no proof that the flagging is complete.

### Their empirical stand-in: the double-check

`cio:466-505` re-runs the whole checker on the *mitigated* binary with
`--uarch-checker-double-check` and expects no alerts. This is a fixpoint sanity check,
not a soundness proof - it re-uses the same possibly-incomplete analysis. It is
**disabled by default** in the artifact (`--skip-double-check`, and the README notes
enabling it roughly doubles a 3-hour run).

### What they say about residual leaks

Paper §9.6, verbatim:

> "cio currently raises alerts while double-checking the safety of the transformed
> binary, alerting on **43 out of 184,659 instructions** in our transformed libsodium.
> These remaining alerts are caused by known bugs in our analyzers and checkers."

That is their entire soundness claim: an empirical residual, attributed to bugs, not a
proof. §8.3 additionally admits precision loss from BAP's lifting - `sub` lifted as `add`
forces both operands to be checked - and estimates that filtering those first would let
SS and CS prune **323** and **6,159** more instructions respectively.

### Acknowledged false negatives, from the code

1. Unsupported MIR opcodes dropped wholesale - the entire SSE/AVX surface plus
   `adc`/`sbb`/`rol`/`ror`/`div` (`alert.ml:607-624`).
2. Alerts on call instructions dropped - "these are not supported by us right now"
   (`alert.ml:574-575`).
3. Alerts with no recoverable instruction index dropped (`alert.ml:556-572`).
4. Unresolvable indirect callees never analyzed (`driver.ml:679-682` filters the errors).
5. Blacklisted functions never analyzed (`common.ml:266-279`).
6. Stores to unconstrained addresses dropped from the memory state
   (`abstract_memory.ml:621`), and strong updates applied to may-alias base sets
   (`abstract_memory.ml:467-478`).
7. `denote_phi` is `failwith "denote_phi not implemented yet"` in the main interpreter
   (`abstract.ml:501`) and a logged error in the interprocedural one
   (`uc_inargs.ml:332-335`).
8. Taint of an index is not joined into the value on the precise stack-load path.

They over-approximate deliberately in exactly one place - `make_top = Taint` - and that
one decision is doing all the soundness work, offsetting several locally unsound
shortcuts.

---

## 9. Scale and results

### From the repo's own captured data (verified)

- **`$ROOT/cio/build_dir_for_transform_eval/checker.alerts.csv`: 62,978 alerts** =
  **59,684 comp-simp + 3,294 silent-stores**, across **132 distinct functions**. Top
  opcodes: `PADDDrr` 8,043, `PSRLDri` 6,047, `PSLLDri` 5,003, `PADDQrr` 3,303,
  `XORPSrr` 2,983, `ADD64rr` 2,598, `VPADDDrr` 2,375, `PXORrr` 2,260. (Note this dump
  still contains the vector opcodes that `RemoveUnsupportedMirOpcodes` later drops, so
  it is a pre-filter count.) Top functions: `salsa20_encrypt_bytes` 8,209,
  `blockmix_salsa8_xor` 7,314, `chacha20_encrypt_bytes_avx2` 5,521,
  `blake2b_compress_sse41` 3,895.
- **SS-only build**: 3,361 silent-store alerts
  (`silent-store-pruning-all-indices-cio-build/checker.alerts.csv`), from 4,229 stores
  considered.
- **Pruning effectiveness** (`bap.log:553099-553106`): taint 38, interval 267, symex 80,
  out of 4,229 - discussed at length in section 4.
- **`implementation-testing/libna.ref.alerts.csv`**: 24,587 alerts on the reference
  (non-SIMD) build.
- **Analysis running time**: the driver itself warns the BAP step should take **>= 10
  minutes** (`cio:383`); the artifact README puts the full libsodium evaluation at
  **~3 hours with 32 GB**, and **>6-8 hours with double-checking enabled**.
- **Annotation effort: 64 lines / 24 functions** for all of libsodium
  (`libsodium.uarch_checker.config`).
- **Build cost**: ~115 GB to build the image from the Dockerfile, "because the image
  downloads and builds LLVM (twice!)".
- CFLAGS used for libsodium: `-g -O2 -pthread -fvisibility=hidden -fPIC -fPIE
  -fno-strict-aliasing -fno-strict-overflow -fstack-protector -ftls-model=local-dynamic`.

### Overhead (paper Figure 4)

Paper PDF: https://homes.cs.washington.edu/~dkohlbre/papers/cio-asplos24.pdf
Benchmarks are the six in `$ROOT/cio/Makefile:83-119` / `eval.sh`; cycle counts via
`eval_cycle_counts.sh` + `process_eval_data.py`.

| Mitigation | argon2id | ed25519 | aesni256gcm | chacha20-poly1305 |
|---|---|---|---|---|
| **SS and CS** | **27.84x** | 20.32x | 4.03x | 8.22x |
| CS only (all) | 26.08x | 16.73x | 2.72x | 7.08x |
| SS only | 2.97x | 3.76x | 1.98x | 1.85x |
| CS 64-bit multiply | 15.71x | 8.79x | 0.94x | 2.31x |
| CS LEA | 3.17x | 2.24x | 1.27x | 1.11x |
| CS vector | 1.06x | 1.18x | 1.54x | 1.06x |
| CS other 64-bit | 8.78x | 6.51x | 1.42x | 2.67x |
| CS other (<=32-bit) | 1.06x | 1.05x | 0.95x | 3.37x |
| Registers reserved only | 1.05x | 1.03x | 0.94x | 0.98x |

**Your 27.8x figure is confirmed: precisely 27.84x, argon2id with SS+CS - the single
worst bar in the paper.** The ablation is the interesting part:

- **Reserving R10-R13 and XMM/YMM12-15 costs essentially nothing** (0.94-1.05x). Register
  pressure is not the problem.
- **SS alone is cheap** (1.85-3.76x). The blinding store is one extra store.
- **CS is the whole cost**, and within CS, **64-bit multiply alone accounts for
  15.71x of argon2id's 27.84x** and 8.79x of ed25519's 20.32x. The transform for a
  64-bit multiply is enormous (see section 8: `MUL64` had to be split into five
  separately verified partial products).
- CS on vector ops costs almost nothing (1.06-1.54x) - because most vector alerts are
  dropped as unsupported opcodes, so they are not actually mitigated.

**Code size**: text section 363 kB baseline -> 588 kB (SS), 1,118 kB (CS),
**1,327 kB (SS+CS) - a 3.66x blowup.**

### Analysis running time (paper Table 2, mean of 3, Xeon Gold 6312U, single job)

| | Compile | **Check** | Mitigate | **Double-check** |
|---|---|---|---|---|
| SS | 388s | 390s | 381s | 477s |
| CS | 397s | **265s** | 393s | **11,836s** |
| SS+CS | 387s | **391s** | 455s | **13,293s** |

**Checking all of libsodium takes 4-7 minutes.** The abstract interpretation is cheap.
The 3.3-hour figure is *double*-checking with the Z3 stage on, which is why the artifact
ships with `--skip-double-check`. From `$ROOT/cio/symex-profiling-data.csv` (25,488
records): 173 SS symbolic checks totalling ~6.35s, mean 36.7ms, max 2.23s.

---

## 10. The lesson: what is the minimal analysis that does this job?

### What Cio's analysis is, stripped to essentials

1. A **one-bit taint lattice** (`Notaint | Taint`), 133 lines, every binop = join,
   constants untainted, `top = Taint`.
2. Seeded from a **flat `function,arg_index` text file** mapping to argument
   **registers**. 64 lines for all of libsodium.
3. **`make_top = Taint`** - every unknown is a secret. This single choice replaces
   pointee seeding, points-to analysis, provenance, and most of a memory model.
4. A **coarse cell memory** that models the stack well and returns tainted top for
   everything else.
5. **Havoc-all-memory at every call**, plus a *separate*, register-only interprocedural
   pre-pass whose entire summary is "which argument registers are tainted".
6. A **value domain (intervals)** doing the actual precision work of deciding which
   flagged instructions really need rewriting.

That is the whole thing. Everything else in the 13.5k lines is BAP plumbing, trace
partitioning for interval precision, the three checkers, alert post-processing, and the
Z3 filter.

### What they simply do not have - mapped onto your list

| Your machinery | Cio equivalent | Verdict |
|---|---|---|
| **Separate data-taint vs address-taint channels** | **None.** One unqualified bit. | Not present. The DMP checker's bit-60 test is a bitvector fact about one address, not a taint channel. |
| **Per-argument "pointee" seeds** | **None.** `secret arg 3` = "RCX is tainted". | Not present. Made unnecessary by `Load -> Taint` (`uc_inargs.ml:282`) and `load -> make_top` (`abstract_memory.ml:538-540`). |
| **Interprocedural summaries with mod-sets** | Summaries are **sets of register names**; calls **havoc all memory** to tainted top. | No mod-sets at all. |
| **Frame-object provenance tracking** | Three regions: `Global | Heap | Stack`. Stack cells are offset intervals, not objects. | Radically coarser. |
| **Pointer-base tracking** | `Bases_domain`: a may-set over those same three regions, every operator = join. | Present but ~150 lines and 3 elements. |
| **Declassification** | **None.** `grep -i "declassif\|endorse\|sanitiz"` returns nothing. Taint is never killed even by overwriting with a constant (`uc_inargs.ml:328-330`). | Not present. |
| **Context-insensitivity workarounds** | Contexts are keyed `(subname, tainted-arg-regs)` but collapsed by `subname` and unioned in `results_for` (`uc_inargs.ml:388-450`). | They gave up on it and did not notice a cost. |
| **A soundness verifier** | Rosette/Serval verifies the **rewrite rules**, not the analysis - and only functional equivalence, with the leak-freedom assertion disabled in the shipped run. | Different target entirely. Their analysis has no verifier. |

### Do they suffer for it?

**Not in analysis quality, and not in analysis cost.** A one-bit taint analysis with no
channels, no provenance and no declassification still prunes **37% of comp-simp
candidates** (paper Table 3) - and the whole check of libsodium runs in **4-7 minutes**
(Table 2). There is no evidence in their data that a richer taint model would prune more;
the remaining candidates are genuinely secret-touching.

They pay in **runtime overhead of the protected binary**, and the ablation shows the cost
is not driven by taint imprecision at all - it is driven by the **expense of individual
transforms**, above all 64-bit multiply (15.71x of argon2id's 27.84x on its own). Halving
the flagged-instruction count would not have fixed that; a cheaper multiply transform
would.

They also pay in **unsoundness they chose not to fix**: the entire SSE/AVX opcode surface
is dropped as unsupported, and 43 instructions still alert in the double-check. Their
gaps are engineering gaps in the *rewriter*, not modelling gaps in the *analysis*.

### The concrete takeaway for your MIR pass

1. **Test the hypothesis before you refactor.** Instrument your pass the way they did
   (`uc_stats.ml`: total / taint_pruned / <value>_pruned, reported per checker, per run)
   and measure how many instructions each of your mechanisms actually prunes. Cio's
   numbers differ by 37x between their two checkers, so a single aggregate number would
   have misled them; measure per instruction class. If address-taint vs data-taint
   separates <1% of your DIT switches, it is not paying for its complexity. This is the
   single highest-value item in this report and it costs you an afternoon.
2. **`top = secret` is the load-bearing invariant.** If you adopt it, pointee seeding,
   mod-sets and frame provenance mostly stop mattering: any value you cannot account for
   is already secret. You have *more* information than they do (types, SSA,
   frame objects), so you can be strictly more precise than "every load is tainted" -
   but you should be able to delete the machinery that exists purely to *avoid*
   over-tainting, and keep only what demonstrably reduces switch count.
3. **You are probably solving the wrong precision problem.** Their expensive stages are
   value-based (intervals, then Z3), because the question that reduces work is "can this
   instruction actually leak?" not "is this bit secret?". For DIT, your analogue is
   "does this instruction class have data-dependent timing on this core at all?" - an
   instruction-class question, not a dataflow one. A coarse taint bit plus a good
   instruction-class model may beat a precise taint analysis plus a coarse one.
4. **Declassification is optional.** They ship a real, evaluated tool on real crypto with
   zero declassification and never kill taint. If yours exists to fight false positives
   that a `top = secret` design would not have created, it may be removable.
5. **Their analysis/rewriter interface is worth stealing**: a CSV of
   `(function, MIR opcode, RPO index, problematic_operands, flags_live)`. Decoupling
   "decide" from "rewrite" via instruction indices let them analyze a *linked binary*
   and rewrite in the *compiler*, and it makes the decision independently auditable and
   replayable. If your pass currently fuses analysis and DIT insertion, splitting them
   would let you diff decisions across runs and A/B the analysis without rebuilding.
6. **Be honest about the holes, in code.** Their `RemoveUnsupportedMirOpcodes` list and
   `RemoveAlertsForCallInsns` are false negatives written down as data, logged with
   counts at every run. That is a better posture than a soundness verifier that proves a
   property the dropped-opcode list already violates.

---

## 11. Notes on verification status

**Verified from source or captured data files in the repos:**
everything in sections 1-8, the repo-derived alert counts and pruning stats in section 9,
the LLVM fork's file list / `Secret` attribute / reserved registers / index markers, the
64-line annotation count, and the arg-index>=6 under-tainting gap.

**From the paper PDF** (https://homes.cs.washington.edu/~dkohlbre/papers/cio-asplos24.pdf,
recovered by coordinate extraction from Figures/Tables and cross-checked against prose
and `eval.sh`): the Figure 4 overhead table, the Table 3 prune breakdown, the Table 2
timing table, text-section sizes, and the 43/184,659 residual-alert figure.

**Discrepancy in the paper itself**: §9.1 prose says "the 7 taint-pruned instructions"
and "the 21 symbolic-pruned instructions" for SS, but Table 3's SS row reads 1 and 23.
The prose and the table disagree. Both are far below the CS taint-prune count either way,
so the qualitative conclusion in section 4 is unaffected.

**Unverified:** `dl.acm.org` returned 403 and `uwplse.org` timed out, so the paper text
comes solely from the Kohlbrenner copy. The evaluation was not rebuilt or re-run, so
Figure 4 is as-published rather than reproduced. Whether the vendored Serval x86
interpreter was itself extended by the Cio authors is unverified (single squashed import
commit `7773c74`). Whether the paper discusses load-value prediction is unverified.

---

## Appendix: quick file index for follow-up

```
$ROOT/checker/bap/interval/checker_taint.ml        # the entire taint lattice (133 lines)
$ROOT/checker/bap/interval/config.ml               # the annotation file grammar
$ROOT/checker/bap/interval/uc_inargs.ml            # interproc taint; Load -> Taint at :282
$ROOT/checker/bap/interval/abstract_memory.ml      # memory model; load :508, store :620, havoc :679
$ROOT/checker/bap/interval/abstract.ml             # DomainProduct :202; denote_jmp/havoc :503
$ROOT/checker/bap/interval/driver.ml               # domains :18-21; seeding :359-390; worklist :740
$ROOT/checker/bap/interval/comp_simp.ml            # check_binop :124 (taint then interval)
$ROOT/checker/bap/interval/silent_stores.ml        # check_elt :236 (taint, interval, Z3)
$ROOT/checker/bap/interval/dmp.ml                  # bit-60 pointer check :44-95
$ROOT/checker/bap/interval/alert.ml                # CSV header :542; dropped opcodes :607
$ROOT/checker/synth/verify.rkt                     # the rewrite-rule theorem :18-30
$ROOT/cio/cio                                      # the pipeline (bash, 553 lines)
$ROOT/cio/libsodium.uarch_checker.config           # all 64 annotations
$ROOT/cio/*/checker.alerts.csv                     # captured alert data
$ROOT/cio/silent-store-pruning-all-indices-cio-build/bap.log:553099   # the pruning stats
```
