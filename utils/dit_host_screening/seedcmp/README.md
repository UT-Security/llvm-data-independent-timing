# seedcmp - rig for paper experiment 08 (seed ground truth)

Compares our interprocedural taint set against **CryptoMPK**'s (Jin et al., IEEE
S&P 2022) on libhydrogen. Their artifact ships the derived taint set as
`(file, line)` pairs, which makes it a third-party ground truth for "where does
the secret actually go" - the one question our own seed files cannot answer
about themselves.

Results, claims and limits: `paper_experiments/08-seed-ground-truth/README.md`.
Assessment of their whole suite: `docs/research/related-work.md` §3a.

## Run

```
./run_seedcmp.sh
```

~4 minutes. Needs a built compiler at `../../../build/bin`.

## What is vendored here, and why

| path | provenance |
|---|---|
| `libhydrogen_cryptompk/` | libhydrogen exactly as shipped in CryptoMPK's `dataset.tar.gz` (sha256 `ab8125b6...`), ISC licensed, `LICENSE` retained |
| `ground_truth/cryptompk_*.txt` | their shipped taint reports, verbatim |

Their source rather than upstream libhydrogen because their report's line numbers
refer to *their* tree. **One edit**, marked in the file: their
`libhydrogen_vector.patch` inverts the `__SSE2__` test, which on their x86-64 host
selects `gimli-core/portable.h` - the file their report covers. On aarch64 the
unmodified patched form pulls in `<emmintrin.h>` and fails to compile, so
`impl/gimli-core.h` selects `portable.h` directly. Same code they measured.

## Pieces

| file | what |
|---|---|
| `run_seedcmp.sh` | builds the three seed arms and prints the comparison |
| `compare_taint_sets.py` | line-level set diff, with path normalisation |
| `attribute_functions.py` | maps `(file,line)` onto enclosing C functions - the honest unit, since they analyse `-O0` IR and we analyse `-O2` MIR |
| `reachable.py` | functions reachable from a driver, via `.o` relocations; used to separate a scope difference from a precision difference |
| `seed_*.txt` | the seed sets, one per arm |

## Traps

- **The taint reports APPEND.** Two runs double every file. `run_seedcmp.sh`
  clears them; a hand-run `clang` must too.
- **A zero `msr DIT` count in a function does not mean it is unprotected** - it
  can mean the caller owns DIT. Check `AlwaysEnteredWithDIT` in
  `-debug-only=taint-interproc` before calling anything an under-taint.
- **Do not compare raw totals.** Their set can only contain what their driver
  reaches; ours is the whole TU. `reachable.py` exists for this.
