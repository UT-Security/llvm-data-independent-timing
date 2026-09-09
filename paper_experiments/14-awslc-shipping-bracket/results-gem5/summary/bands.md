# AWS-LC's bracket against coarse DIT, gem5

Geometric mean of cycles per operation relative to the Coarse arm (DIT on for the whole
process), so the ratio isolates the bracket's mode switches from the cost of running
under DIT. Rows ranked once by the Baseline bracket column.

| Benchmarks | Baseline bracket | Baseline hoisted | ExpeDITe bracket | ExpeDITe hoisted |
|---|---|---|---|---|
| Slowest 20% (71) | 1.75 | 1.42 | 1.05 | 1.06 |
| 20-40% (71) | 1.21 | 1.13 | 1.04 | 1.03 |
| 40-60% (71) | 1.04 | 1.03 | 1.01 | 1.01 |
| 60-80% (71) | 1.01 | 1.01 | 1.00 | 1.00 |
| Fastest 20% (71) | 0.99 | 0.99 | 0.99 | 1.00 |
| **All 355 bracketed benchmarks** | 1.17 | 1.10 | 1.02 | 1.02 |

The M4 side over the same 355 rows is 1.48 / 1.58 / 1.02 / 1.36; its published table is
381 rows including the 26 TrustToken rows, which sit at 0.96 and pull it to 1.44 / 1.52
/ 1.02 / 1.33. The two machines are not comparable in absolute terms -- see provenance.
