# PR #21 (product-lattice domain refactor) - independent verification, 2026-09-03

Verified from worktree 4 against the pre-PR compiler (`build-subblock`, tip
`d6c6157973e1` + Phase 0 changes), per the protocol in
`docs/design/taint-domain-refactor-handoff.md`, with one addition the PR
author could not have: the 727-line round-5 seed set
(`gem5-DIT/benchmarks/tls_resume/seed_pass_r5.txt`), which instruments
`bignum.c`, `bignum_core.c`, `ecp_curves.c` and the RSA/X.509 TUs that the
82-seed baseline never touches.

## Differential, mbedTLS 3.6.2, 108 objects, debug info and `.comment` stripped

| PR commit | objects identical to baseline | precision reports identical |
|---|---|---|
| `7baad0bce413` refactor only | 108 / 108 | 108 / 108 |
| `f649a3e2a040` + provenance join at bottom | 108 / 108 | 108 / 108 |
| `c0f63d4576fc` + instrumentation gate | **107 / 108** | 107 / 108 |

**The refactor is byte-identical under the hard seed set**, which is a
stronger statement than the PR's (82 seeds). The provenance-join fix changes
nothing on mbedTLS. The gate fix changes exactly one function:

```
size_t mbedtls_rsa_get_len(const mbedtls_rsa_context *ctx) { return ctx->len; }
```

Seed line 508 (`mbedtls_rsa_get_len,0`, harvested by the seed loop) makes
`ctx` data-tainted. The load through it is a Need (`AddressSensitive`) and
redefines `x0`, so the exit state is clean: `TR.Merged` empty, old gate skips
the function, the Need runs with DIT off. New gate: `need=1 underdit=1
switches=2`. This is precisely the shape the commit message describes, and the
direction the handoff requires (more need, never less). The PR body's "neither
library exhibits them" is true at 82 seeds and false at 727 - a real instance,
worth adding to the PR's evidence.

## Also checked
- Design doc S2 (`Address ⊆ Data` by induction over the fixed point): the
  argument is sound as written; the base case (seed, Address empty) and the
  ALU rule collapse are the load-bearing steps.
- Design doc S3 (pointee as a MAY component, provenance as MUST): correct, and
  a genuine improvement over the handoff sketch, which would have under-tainted
  at joins.
- Not re-run: libsodium 129/129 (PR's own result, same seed set as theirs -
  nothing to add), lit suites (PR's own result on both Release and asserts).

## Verdict
Mergeable on the evidence. The one behavioural change is intended, explained,
tested, and now has a real-world instance.

Tooling: `gem5-DIT/benchmarks/tls_resume/diff_compilers.sh <buildA> <buildB>
<seed> <tag>`; bisect by checking out each commit in a worktree and rebuilding
`clang` incrementally (~2 min each with ccache).
