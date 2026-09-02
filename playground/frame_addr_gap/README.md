# Worked examples for the two frame-address gaps

Minimal, self-contained reproductions of the two defects in
`docs/design/frame-address-gap.md`. Both compile in one TU, so nothing here is
the cross-TU limit.

```
B=../../build/bin
$B/clang -O2 -ftaint-harden=gapA_seed.txt \
    -mllvm -taint-dit-precision-report=/tmp/p.txt -c gapA.c -o /dev/null
```

A function ABSENT from the precision report was never analysed - its
secret-dependent instructions run with `PSTATE.DIT` clear.

| file | shows |
|---|---|
| `gapA.c` | caller taints its own frame object and passes the address in |
| `gapB.c` | the same callee reached two ways, one that works and one that does not |
| `gapB_only.c` | gap B with the working caller deleted, so nothing masks it |
| `gapB_interior.c` | gap B as it appears in real code: an INTERIOR pointer into the caller's own argument (`&csig[32]`), which following copies alone does not reach |

`gapB.c` is the one to read: `via_local` and `via_argptr` call the *same*
`produce` and the *same* `consume`, and only the first is protected.

**Watch for masking.** In `gapB.c` with `-taint-frame-addr-args`, `consume` comes
out covered - but only because `via_local` instrumented the shared body. Delete
that caller (`gapB_only.c`) and it is uncovered again. A single well-analysed
caller hides the defect for every other caller of the same function, which is
why `gapB_only.c` exists.
