# The M4 half on gem5's own microbenchmark (2026-09-08)

The raw run behind the headline figure's right panel and `data/m4_aes_arms.csv`.
Everything the runner wrote, including the samples it rejected.

| file | what |
|---|---|
| `crossover-*.json` | the run: 32 rows, the manifest, the binaries' sha256, the measured switch cost |
| `runs-crossover-*.jsonl` | one entry per invocation, 900-odd of them, rejects included |
| `sweep.log` | the table as printed |

## What this run is

The same experiment as the chacha sweep in `data/m4_arms.csv`, with the secret
lane changed to the one gem5 can resolve, so the two panels of the headline
figure are the same microbenchmark:

```
SECRET_OP=aes SECRET_MLEN=64 ARMS="base bracket bracketnop" LANES="wide narrow" \
  OUT=<somewhere> utils/dit_host_screening/signed_lookup/silicon/build_silicon.sh link

utils/dit_host_screening/signed_lookup/silicon/run_crossover_m4.py \
  --bin <somewhere> --lane narrow \
  --arms nodit blanket bracket bracketnop \
  --L 10 30 50 100 200 1000 5000 20000 --reps 11

utils/dit_host_screening/signed_lookup/silicon/derive_exp02_m4.py <outdir> aes
```

`--arms` and `--bin` were added for this run; the `aes` argument to the derive
step is what keeps it out of `m4_arms.csv`. The vendored driver is still never
edited -- the op and the message length are `-D` parameters added by
`silicon/secret_op_param.patch` on a staged copy.

## Result

f* = **29.3%** against gem5's 60.0% on the same code. Two terms, both the same
direction: the bracket is ~2x dearer here (333-710 cyc/request against 206-351)
and the request is 3.6x shorter (314 cycles at L=10 against 1,139), because
AES-GCM on hardware AES is fast and gem5 sustains IPC 1.07 where this part
sustains 3.89.

## Gates that passed

- **public-lane checksum** identical across all four arms at every L
- **nodit vs blanket instruction count** inside 0.02% (they are one binary)
- **the bracket retired more instructions than nodit**, i.e. the preprocessor
  renames actually landed on the wrapper
- **DIT readback**: blanket exits with PSTATE.DIT set, every other arm clear
- **implied clock**: 3.4-5.0 GHz, 10 samples rejected across the whole sweep and
  counted in the `rejects` column, never dropped

The twin column reads -2.4% to +0.0%, so the bracket's own restructuring of the
binary is worth about a point and the rest of its column is the mode.
