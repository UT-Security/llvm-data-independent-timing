# ExpeDITe security microbenchmarks

## The result

| microbenchmark | gated optimization | readout | baseline | --apple | ExpeDITe |
|---|---|---|---:|---:|---:|
| `cs` | value prediction | `load_predictions` (less the out-of-region floor) | 1,023 | 482 | 0 |
| `hwcs` | computation simplification | multiplies run with operand-dependent latency | 32,768 | 32,768 | 0 |
| `mb3` | data memory-dependent prefetcher | `dmp_issued_ptr` | 64,516 | 8 | 0 |

Every gated optimization leaks on the baseline, leaks under `--apple`, and reads zero under ExpeDITe.

## The leak column

| microbenchmark | readout | base | apple | expedite | drain | writeback |
|---|---|---|---|---|---|---|
| mb1 | `predictions` | 4038 | 0 | 0 | - | - |
| mb2 | `cs_simplified` | 3552 | 0 | 0 | - | - |
| mb3 | `dmp_issued_ptr` | 64516 | 8 | 0 | - | - |
| mb4 | `in_sequence` | 0 | 0 | 0 | - | 2129 |
| mb4nt | `in_sequence` | 0 | 0 | 0 | - | 2271 |
| cs | `in_sequence` | 0 | 485 | 0 | - | - |
| hwcs | `cs_simplified` | 32768 | 63512 | 0 | - | - |

## Validity gates

| microbenchmark | gate | result | detail |
|---|---|---|---|
| mb1 | checksum identical across arms | **PASS** | 1 distinct: ['496316728891688568'] |
| mb1 | positive control leaks (base arm) | **PASS** | predictions=4038 |
| mb1 | expedite arm does not leak | **PASS** | predictions=0 |
| mb1 | ROI work identical once DIT writes are counted | **PASS** | base=53274 expedite=53277 writes=3 residual=0 (slop 400) |
| mb1 | hardened arm executes inside a region | **PASS** | dit_tagged_set=98225, committed DIT writes in ROI=3 |
| mb1 | base arm sets no DIT | **PASS** | committed DIT writes=0 |
| mb2 | checksum identical across arms | **PASS** | 1 distinct: ['2475239992942155744'] |
| mb2 | positive control leaks (base arm) | **PASS** | cs_simplified=3552 |
| mb2 | expedite arm does not leak | **PASS** | cs_simplified=0 |
| mb2 | ROI work identical once DIT writes are counted | **PASS** | base=45080 expedite=45082 writes=2 residual=0 (slop 400) |
| mb2 | hardened arm executes inside a region | **PASS** | cs_dit_suppressed=4096, committed DIT writes in ROI=2 |
| mb2 | base arm sets no DIT | **PASS** | committed DIT writes=0 |
| mb3 | checksum identical across arms | **PASS** | 1 distinct: ['34377768512'] |
| mb3 | positive control leaks (base arm) | **PASS** | dmp_issued_ptr=64516 |
| mb3 | expedite arm does not leak | **PASS** | dmp_issued_ptr=0 |
| mb3 | ROI work identical once DIT writes are counted | **PASS** | base=98332 expedite=98334 writes=2 residual=0 (slop 400) |
| mb3 | hardened arm executes inside a region | **PASS** | dmp_drop_dit=8191, committed DIT writes in ROI=2 |
| mb3 | base arm sets no DIT | **PASS** | committed DIT writes=0 |
| mb4 | checksum identical across arms | **PASS** | 1 distinct: ['378594930135363716'] |
| mb4 | positive control leaks (writeback arm) | **PASS** | in_sequence=2129 (entry 0, exit 2129) |
| mb4 | expedite arm does not leak | **PASS** | in_sequence=0 (entry 0, exit 0) |
| mb4 | ROI work identical once DIT writes are counted | **PASS** | base=238668 expedite=244812 writes=6144 residual=0 (slop 400) |
| mb4 | hardened arm executes inside a region | **PASS** | dit_tagged_set=351293, committed DIT writes in ROI=6144 |
| mb4 | base arm sets no DIT | **PASS** | committed DIT writes=0 |
| mb4 | apple is safe at the exit boundary (reads arch mode) | **PASS** | exit 0, entry 0, behind a squashed set 2086 |
| mb4nt | checksum identical across arms | **PASS** | 1 distinct: ['378594930135363716'] |
| mb4nt | positive control leaks (writeback arm) | **PASS** | in_sequence=2271 (entry 0, exit 2271) |
| mb4nt | expedite arm does not leak | **PASS** | in_sequence=0 (entry 0, exit 0) |
| mb4nt | ROI work identical once DIT writes are counted | **PASS** | base=238668 expedite=244812 writes=6144 residual=0 (slop 400) |
| mb4nt | hardened arm executes inside a region | **PASS** | dit_tagged_set=369986, committed DIT writes in ROI=6144 |
| mb4nt | base arm sets no DIT | **PASS** | committed DIT writes=0 |
| mb4nt | apple is safe at the exit boundary (reads arch mode) | **PASS** | exit 0, entry 0, behind a squashed set 1611 |
| cs | checksum identical across arms | **PASS** | 1 distinct: ['1456'] |
| cs | positive control leaks (apple arm) | **PASS** | in_sequence=485 (entry 485, exit 0) |
| cs | expedite arm does not leak | **PASS** | in_sequence=0 (entry 0, exit 0) |
| cs | hardened arm executes inside a region | **PASS** | dit_tagged_set=2176, committed DIT writes in ROI=128 |
| hwcs | checksum identical across arms | **PASS** | 1 distinct: ['0'] |
| hwcs | positive control leaks (apple arm) | **PASS** | cs_simplified=63512 |
| hwcs | expedite arm does not leak | **PASS** | cs_simplified=0 |
| hwcs | hardened arm executes inside a region | **PASS** | cs_dit_suppressed=61446, committed DIT writes in ROI=8192 |
| mb4 | the secret-dependent branch mispredicts | **PASS** | branchMispredicts=2183 |
| mb4 | predictions are issued in a clear's shadow at all | **PASS** | behindClear=475 |
| mb4 | clear-shadow oracle accounting closes | **PASS** | behindClear=475 vs parts=475 |
| mb4nt | the secret-dependent branch mispredicts | **PASS** | branchMispredicts=2228 |
| mb4nt | predictions are issued in a clear's shadow at all | **PASS** | behindClear=798 |
| mb4nt | clear-shadow oracle accounting closes | **PASS** | behindClear=798 vs parts=798 |

## Every collected stat

### mb1

| stat | base | apple | expedite |
|---|---|---|---|
| `checksum` | 496316728891688568 | 496316728891688568 | 496316728891688568 |
| `cycles` | 48219 | 48392 | 48262 |
| `insts` | 53274 | 53277 | 53277 |
| `predictions` | 4038 | 0 | 0 |
| `alu_predictions` | 4038 | 0 | 0 |
| `pred_correct` | 4037 | 3 | 0 |
| `dit_tagged_set` | 0 | 98384 | 98225 |
| `behind_clear_retired` | 0 | 0 | 3 |
| `branch_mispredicts` | 2 | 2 | 2 |
| `dit_set_committed` | 0 | 2 | 2 |
| `dit_clear_committed` | 0 | 1 | 1 |

### mb2

| stat | base | apple | expedite |
|---|---|---|---|
| `checksum` | 2475239992942155744 | 2475239992942155744 | 2475239992942155744 |
| `cycles` | 20528 | 20560 | 20530 |
| `insts` | 45080 | 45082 | 45082 |
| `branch_mispredicts` | 3 | 3 | 3 |
| `cs_candidates` | 4096 | 0 | 0 |
| `cs_simplified` | 3552 | 0 | 0 |
| `cs_mult_by_zero` | 3552 | 0 | 0 |
| `cs_dit_suppressed` | 0 | 4096 | 4096 |
| `dit_set_committed` | 0 | 1 | 1 |
| `dit_clear_committed` | 0 | 1 | 1 |

### mb3

| stat | base | apple | expedite |
|---|---|---|---|
| `checksum` | 34377768512 | 34377768512 | 34377768512 |
| `cycles` | 65663 | 49249 | 49174 |
| `insts` | 98332 | 98334 | 98334 |
| `branch_mispredicts` | 12 | 8 | 6 |
| `dmp_fills` | 8193 | 8192 | 8191 |
| `dmp_drop_dit` | 0 | 8191 | 8191 |
| `dmp_scan_attempts` | 8193 | 1 | 0 |
| `dmp_scans` | 8193 | 1 | 0 |
| `dmp_words_examined` | 65544 | 8 | 0 |
| `dmp_ptr_candidates` | 65537 | 8 | 0 |
| `dmp_gen_pointer` | 64516 | 8 | 0 |
| `dmp_issued_ptr` | 64516 | 8 | 0 |
| `dit_set_committed` | 0 | 1 | 1 |
| `dit_clear_committed` | 0 | 1 | 1 |

### mb4

| stat | base | apple | expedite | writeback |
|---|---|---|---|---|
| `checksum` | 378594930135363716 | 378594930135363716 | 378594930135363716 | 378594930135363716 |
| `cycles` | 204079 | 344449 | 199712 | 200293 |
| `insts` | 238668 | 244812 | 244812 | 244812 |
| `predictions` | 51332 | 91004 | 3733 | 45566 |
| `load_predictions` | 42803 | 72262 | 1567 | 35021 |
| `alu_predictions` | 8529 | 18742 | 2166 | 10545 |
| `pred_correct` | 44889 | 80708 | 2984 | 41916 |
| `dit_tagged_set` | 0 | 311663 | 351293 | 225613 |
| `behind_clear` | 0 | 42574 | 475 | 38804 |
| `behind_clear_retired` | 0 | 0 | 245 | 32924 |
| `in_sequence` | 0 | 0 | 0 | 2129 |
| `behind_clear_squashed_younger` | 0 | 2 | 190 | 1641 |
| `behind_clear_squashed_samepath` | 0 | 40485 | 40 | 2109 |
| `behind_set_squashed` | 0 | 2086 | 0 | 0 |
| `branch_mispredicts` | 2399 | 5600 | 2183 | 2265 |
| `value_mispredicts` | 180 | 118 | 107 | 115 |
| `dit_set_committed` | 0 | 4096 | 4096 | 4096 |
| `dit_clear_committed` | 0 | 2048 | 2048 | 2048 |

### mb4nt

| stat | base | apple | expedite | writeback |
|---|---|---|---|---|
| `checksum` | 378594930135363716 | 378594930135363716 | 378594930135363716 | 378594930135363716 |
| `cycles` | 204079 | 344209 | 200934 | 200177 |
| `insts` | 238668 | 244812 | 244812 | 244812 |
| `predictions` | 51332 | 86505 | 5868 | 43168 |
| `load_predictions` | 42803 | 67976 | 2987 | 33428 |
| `alu_predictions` | 8529 | 18529 | 2881 | 9740 |
| `pred_correct` | 44889 | 80637 | 5526 | 41933 |
| `dit_tagged_set` | 0 | 312408 | 369986 | 225339 |
| `behind_clear` | 0 | 38447 | 798 | 37657 |
| `behind_clear_retired` | 0 | 0 | 666 | 34010 |
| `in_sequence` | 0 | 0 | 0 | 2271 |
| `behind_clear_squashed_younger` | 0 | 0 | 123 | 1358 |
| `behind_clear_squashed_samepath` | 0 | 36835 | 9 | 17 |
| `behind_set_squashed` | 0 | 1611 | 0 | 0 |
| `branch_mispredicts` | 2399 | 5645 | 2228 | 2304 |
| `value_mispredicts` | 180 | 117 | 112 | 116 |
| `dit_set_committed` | 0 | 4096 | 4096 | 4096 |
| `dit_clear_committed` | 0 | 2048 | 2048 | 2048 |

### cs

| stat | base | apple | expedite |
|---|---|---|---|
| `checksum` | 1456 | 1456 | 1456 |
| `cycles` | 2063 | 5572 | 2448 |
| `insts` | 6115 | 6251 | 6251 |
| `predictions` | 2316 | 1616 | 1134 |
| `load_predictions` | 2046 | 1505 | 1023 |
| `alu_predictions` | 270 | 111 | 111 |
| `pred_correct` | 2313 | 1130 | 1133 |
| `dit_tagged_set` | 0 | 3828 | 2176 |
| `behind_clear` | 0 | 485 | 1134 |
| `behind_clear_retired` | 0 | 0 | 1134 |
| `in_sequence` | 0 | 485 | 0 |
| `in_sequence_entry` | 0 | 485 | 0 |
| `branch_mispredicts` | 4 | 3 | 3 |
| `value_mispredicts` | 1 | 1 | 1 |
| `dit_set_committed` | 0 | 64 | 64 |
| `dit_clear_committed` | 0 | 64 | 64 |

### hwcs

| stat | base | apple | expedite |
|---|---|---|---|
| `cycles` | 65597 | 270274 | 53304 |
| `insts` | 143370 | 147466 | 147466 |
| `branch_mispredicts` | 5 | 8 | 5 |
| `cs_candidates` | 32768 | 63512 | 0 |
| `cs_simplified` | 32768 | 63512 | 0 |
| `cs_mult_by_zero` | 32768 | 63512 | 0 |
| `cs_dit_suppressed` | 0 | 63612 | 61446 |
| `dit_set_committed` | 0 | 4096 | 4096 |
| `dit_clear_committed` | 0 | 4096 | 4096 |

