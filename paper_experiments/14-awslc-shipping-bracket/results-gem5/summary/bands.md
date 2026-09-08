# AWS-LC's bracket against coarse DIT, gem5

Ratios of cycles per operation to the Coarse arm (DIT on for the whole process),
geometric mean per band. Rows ranked once by the Baseline bracket column.

| Benchmarks | Baseline bracket | Baseline hoisted | ExpeDITe bracket | ExpeDITe hoisted |
|---|---|---|---|---|
| Slowest 20% (68) | 2.11 | 1.59 | 1.05 | 1.06 |
| 20--40% (67) | 1.33 | 1.19 | 1.03 | 1.03 |
| 40--60% (68) | 1.06 | 1.04 | 1.01 | 1.01 |
| 60--80% (67) | 1.02 | 1.01 | 1.01 | 1.01 |
| Fastest 20% (68) | 1.00 | 1.00 | 1.00 | 1.00 |
| **All 338 bracketed benchmarks** | 1.25 | 1.15 | 1.02 | 1.02 |
