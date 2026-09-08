# AWS-LC's bracket against coarse DIT, gem5

Geometric mean of cycles per operation relative to the Coarse arm (DIT on for the whole
process), so the ratio isolates the bracket's mode switches from the cost of running
under DIT. Rows ranked once by the Baseline bracket column.

| Benchmarks | Baseline bracket | Baseline hoisted | ExpeDITe bracket | ExpeDITe hoisted |
|---|---|---|---|---|
| Slowest 20% (71) | 2.08 | 1.58 | 1.05 | 1.05 |
| 20-40% (71) | 1.30 | 1.18 | 1.03 | 1.03 |
| 40-60% (71) | 1.05 | 1.03 | 1.00 | 1.01 |
| 60-80% (71) | 1.01 | 1.01 | 1.01 | 1.01 |
| Fastest 20% (71) | 0.99 | 1.00 | 0.99 | 0.99 |
| **All 355 bracketed benchmarks** | 1.23 | 1.14 | 1.02 | 1.02 |

The M4 side over the same 355 rows (its published table is 381 rows including the 26
TrustToken rows, which sit at 0.96 and pull it down to 1.44 / 1.52 / 1.02 / 1.33):

| | AWS default | + sb | AWS hoist | + sb |
|---|---|---|---|---|
| Apple M4, same 355 rows | 1.48 | 1.58 | 1.02 | 1.36 |
