# Experiment 14 analysis: AWS-LC's shipped DIT bracket on `bssl speed`

Source `/Users/rgangar/.treehouse/llvm-data-independent-timing-7b712d/1/llvm-data-independent-timing/paper_experiments/14-awslc-shipping-bracket/results-m4/speed.json`, analysed 2026-09-06. Arms: A = release, no DIT, C = blanket (DIT set before main), B = shipped bracket, Bs = bracket + sb, H = vendor hoisting (-dit), Hs = hoisting + sb.

## Validity

- pinned to CPU 9 (kern.sched_thread_bind_cpu); processes not reporting the bind: 0
- failures: 0; rows without PMC cycles: 0; samples flagged (kept) for implied clock outside band [3000, 4700]: 53
- implied clock of all samples: min 3905, p10 4091, median 4248, max 5629151382 MHz (2898 samples)
- A samples per row: min 7, median 7, max 7 (reps 7, window 50 ms)
- DIT readback gate: [['A', 0, '0'], ['B', 0, '0'], ['Bs', 0, '0'], ['C', 1, '1'], ['H', 0, '0'], ['Hs', 0, '0']]

## The three prices (one bracket entry per op: AEAD-AES-128-GCM seal, 16 B)

| what | cycles |
|---|---|
| two mode-changing writes (B - A) | 153 |
| read + one non-changing write (H - A) | 21 |
| sb after a changing write (Bs - B) | 7 |
| sb after a non-changing write (Hs - H) | 64 |
| same four, from the single-block AES row | 94 / -1 / 30 / 49 |

## Is the cost constant across sizes? (absolute B - A cycles per op)

| family | 16 B | 256 B | 1350 B | 8 KB | 16 KB | max/min |
|---|---|---|---|---|---|---|

## Density: how many bracket entries an operation pays for

One AEAD-level entry (two changing writes) costs 153 cycles on the seal row and one single-block AES entry 94; B - A divided by the applicable price is the entries per op.

| row | B - A cycles | unit price | implied entries/op | B vs A |
|---|---|---|---|---|
| CMAC-AES-128-CBC [16384 B] | 96,721 | 94 (single-block AES entry) | 1033 (1024 AES blocks) | +236.9% |
| CMAC-AES-128-CBC [16 B] | 228 | 94 (single-block AES entry) | 2 (1 AES blocks) | +406.8% |
| EVP-AES-128-GCM encrypt [16 B] | 525 | 153 (AEAD-level entry) | 3 | +304.2% |
| EVP-AES-128-GCM encrypt [16384 B] | 556 | 153 (AEAD-level entry) | 4 | +9.7% |
| EVP-AES-128-CBC decrypt [16 B] | 337 | 153 (AEAD-level entry) | 2 | +323.4% |
| AEAD-AES-128-GCM open [16 B] | 173 | 153 (AEAD-level entry) | 1 | +84.0% |

## Blanket DIT moving a row by more than 2% (C vs A, rows with MAD < 2%)

| row | A cyc/op | C | H | MAD |
|---|---|---|---|---|
| EVP-AES-128-CTR encrypt [16 B] | 103 | -49.3% | -34.5% | 0.21% |
| RNG [16 B] | 6,786 | -25.6% | -25.0% | 0.17% |
| RNG [1350 B] | 7,274 | -23.6% | -23.1% | 0.09% |
| RNG [16384 B] | 12,463 | -13.9% | -13.4% | 0.06% |
| AEAD-ChaCha20-Poly1305 seal init | 35 | -9.1% | -6.9% | 0.99% |
| ECDSA P-256 signing | 42,773 | -3.9% | -3.5% | 0.07% |
| CMAC-AES-128-CBC [1350 B] | 3,392 | -2.4% | -4.0% | 0.02% |
| EVP-AES-128-CBC decrypt init | 213 | +3.3% | +7.3% | 1.82% |
| AEAD-AES-128-CBC-SHA1 open init | 597 | +3.8% | +3.6% | 0.91% |
| AEAD-AES-128-CBC-SHA1 seal init | 602 | +4.3% | +3.9% | 1.04% |
| AEAD-AES-128-CBC-SHA1 seal [16 B] | 385 | +5.2% | +9.6% | 0.09% |
| EVP-AES-128-GCM decrypt init | 415 | +6.7% | +9.6% | 0.10% |
| EVP-AES-128-GCM encrypt init | 397 | +6.8% | +10.2% | 0.07% |
| AEAD-AES-128-GCM-SIV seal [16 B] | 402 | +6.9% | +6.9% | 0.57% |
| EVP-AES-128-GCM decrypt [16 B] | 137 | +7.9% | +18.8% | 0.13% |
| EVP-AES-128-CBC encrypt [16 B] | 93 | +12.4% | +11.2% | 1.08% |

## Every row (percent over A, cycles per op)

| row | A cyc/op | IPC A | C | B | B-A cyc | Bs | H | Hs | MAD |
|---|---|---|---|---|---|---|---|---|---|
| AEAD-AES-128-CBC-SHA1 open [1350 B] | 4,468 | 4.25 | -0.1% | +2.9% | 131 | +8.5% | +0.1% | +6.9% | 0.10% |
| AEAD-AES-128-CBC-SHA1 open [16 B] | 1,215 | 4.89 | +0.3% | +12.0% | 146 | +32.7% | +0.8% | +27.0% | 0.35% |
| AEAD-AES-128-CBC-SHA1 open [16384 B] | 26,971 | 2.63 | -0.1% | +0.3% | 82 | +1.3% | -0.2% | +1.0% | 0.02% |
| AEAD-AES-128-CBC-SHA1 open init | 597 | 5.31 | +3.8% | +28.1% | 168 | +41.7% | +3.6% | +27.9% | 0.91% |
| AEAD-AES-128-CBC-SHA1 seal [1350 B] | 3,970 | 1.68 | +0.7% | +6.3% | 251 | +16.3% | +2.8% | +14.6% | 0.02% |
| AEAD-AES-128-CBC-SHA1 seal [16 B] | 385 | 5.16 | +5.2% | +47.1% | 181 | +132.0% | +9.6% | +114.3% | 0.09% |
| AEAD-AES-128-CBC-SHA1 seal [16384 B] | 43,894 | 1.25 | +0.0% | +0.5% | 203 | +1.3% | +0.1% | +1.2% | 0.05% |
| AEAD-AES-128-CBC-SHA1 seal init | 602 | 5.29 | +4.3% | +28.2% | 170 | +42.3% | +3.9% | +28.2% | 1.04% |
| AEAD-AES-128-CCM-Bluetooth seal [1350 B] | 3,894 | 2.25 | +0.4% | +4.2% | 165 | +3.5% | +0.4% | +1.6% | 0.01% |
| AEAD-AES-128-CCM-Bluetooth seal [16 B] | 176 | 5.37 | +0.4% | +74.9% | 132 | +79.7% | +0.1% | +39.0% | 0.21% |
| AEAD-AES-128-CCM-Bluetooth seal [16384 B] | 45,567 | 2.08 | +0.5% | +0.6% | 271 | -1.0% | +0.4% | -1.2% | 0.07% |
| AEAD-AES-128-CCM-Bluetooth seal init | 63 | 5.48 | -0.9% | +315.1% | 200 | +334.5% | -0.4% | +185.8% | 0.20% |
| AEAD-AES-128-GCM open [1350 B] | 708 | 5.47 | -0.0% | +24.0% | 170 | +33.4% | +5.7% | +20.1% | 0.27% |
| AEAD-AES-128-GCM open [16 B] | 206 | 4.09 | +0.2% | +84.0% | 173 | +110.0% | +21.6% | +76.6% | 0.03% |
| AEAD-AES-128-GCM open [16384 B] | 5,727 | 6.25 | +0.0% | +2.9% | 163 | +4.2% | +0.7% | +2.8% | 0.01% |
| AEAD-AES-128-GCM open init | 241 | 2.79 | +0.8% | +65.9% | 158 | +76.7% | +0.6% | +36.0% | 0.20% |
| AEAD-AES-128-GCM seal [1350 B] | 711 | 5.36 | +0.3% | +21.7% | 154 | +22.1% | +3.2% | +12.1% | 0.18% |
| AEAD-AES-128-GCM seal [16 B] | 190 | 4.14 | -0.2% | +80.8% | 153 | +84.4% | +11.0% | +44.9% | 0.10% |
| AEAD-AES-128-GCM seal [16384 B] | 5,784 | 6.18 | -0.0% | +3.0% | 171 | +3.2% | +0.8% | +2.0% | 0.02% |
| AEAD-AES-128-GCM seal init | 240 | 2.79 | +0.8% | +66.3% | 159 | +76.7% | +0.7% | +36.5% | 0.09% |
| AEAD-AES-128-GCM-SIV open [1350 B] | 1,796 | 5.47 | +0.2% | +11.9% | 213 | +14.8% | -0.1% | +10.8% | 0.21% |
| AEAD-AES-128-GCM-SIV open [16 B] | 336 | 4.64 | +0.5% | +63.4% | 213 | +80.4% | +2.0% | +58.2% | 0.26% |
| AEAD-AES-128-GCM-SIV open [16384 B] | 17,259 | 5.81 | +0.0% | +1.3% | 222 | +1.6% | +0.0% | +1.2% | 0.01% |
| AEAD-AES-128-GCM-SIV open init | 80 | 4.82 | +0.5% | +221.8% | 178 | +253.4% | -0.1% | +134.6% | 0.17% |
| AEAD-AES-128-GCM-SIV seal [1350 B] | 1,794 | 5.45 | +0.2% | +7.1% | 128 | +8.3% | +0.4% | +4.1% | 0.06% |
| AEAD-AES-128-GCM-SIV seal [16 B] | 402 | 3.78 | +6.9% | +36.7% | 148 | +39.9% | +6.9% | +21.4% | 0.57% |
| AEAD-AES-128-GCM-SIV seal [16384 B] | 17,263 | 5.81 | +0.0% | +0.6% | 108 | +0.8% | -0.0% | +0.3% | 0.01% |
| AEAD-AES-128-GCM-SIV seal init | 80 | 4.82 | +0.5% | +220.6% | 177 | +252.4% | -0.3% | +134.8% | 0.19% |
| AEAD-ChaCha20-Poly1305 seal [1350 B] | 3,155 | 3.50 | -0.1% | +3.7% | 116 | +4.4% | +0.1% | +2.1% | 0.03% |
| AEAD-ChaCha20-Poly1305 seal [16 B] | 579 | 2.56 | -0.0% | +44.5% | 258 | +47.9% | +8.8% | +35.4% | 0.15% |
| AEAD-ChaCha20-Poly1305 seal [16384 B] | 31,324 | 3.67 | +0.0% | +0.4% | 124 | +0.4% | +0.0% | +0.2% | 0.00% |
| AEAD-ChaCha20-Poly1305 seal init | 35 | 4.44 | -9.1% | +281.0% | 98 | +366.7% | -6.9% | +166.7% | 0.99% |
| AES-128 decrypt | 34 | 1.94 | -0.2% | +277.7% | 94 | +366.0% | -2.6% | +143.7% | 0.06% |
| AES-128 decrypt setup | 73 | 3.41 | -0.0% | +238.4% | 174 | +247.3% | -3.1% | +144.0% | 0.04% |
| AES-128 encrypt | 34 | 1.94 | -0.1% | +277.7% | 94 | +366.4% | -1.8% | +144.0% | 0.10% |
| AES-128 encrypt setup | 40 | 4.97 | -0.1% | +482.3% | 192 | +506.7% | +5.3% | +335.1% | 0.06% |
| CMAC-AES-128-CBC [1350 B] | 3,392 | 2.55 | -2.4% | +241.6% | 8,195 | +290.7% | -4.0% | +127.7% | 0.02% |
| CMAC-AES-128-CBC [16 B] | 56 | 6.42 | -0.1% | +406.8% | 228 | +483.4% | +2.2% | +239.9% | 0.07% |
| CMAC-AES-128-CBC [16384 B] | 40,820 | 2.46 | +0.1% | +236.9% | 96,721 | +285.4% | -3.9% | +124.5% | 0.08% |
| CMAC-AES-128-CBC init | 300 | 5.55 | +0.2% | +115.7% | 347 | +138.5% | +2.9% | +73.2% | 1.12% |
| ECDSA P-256 signing | 42,773 | 3.64 | -3.9% | +1.7% | 717 | +1.9% | -3.5% | -2.9% | 0.07% |
| ECDSA P-256 verify | 105,064 | 4.98 | -0.0% | -0.1% | -81 | -0.3% | +0.0% | -0.1% | 0.33% |
| EVP-AES-128-CBC decrypt [1350 B] | 353 | 8.86 | -0.0% | +96.1% | 339 | +152.8% | +1.8% | +94.6% | 0.70% |
| EVP-AES-128-CBC decrypt [16 B] | 104 | 6.58 | -0.1% | +323.4% | 337 | +522.6% | +21.9% | +319.0% | 1.93% |
| EVP-AES-128-CBC decrypt [16384 B] | 3,311 | 9.38 | -0.0% | +9.1% | 301 | +15.6% | -0.6% | +9.4% | 0.02% |
| EVP-AES-128-CBC decrypt init | 213 | 6.34 | +3.3% | +78.8% | 168 | +84.1% | +7.3% | +50.6% | 1.82% |
| EVP-AES-128-CBC encrypt [1350 B] | 1,924 | 1.39 | +0.0% | +21.9% | 422 | +22.8% | +0.4% | +11.8% | 0.01% |
| EVP-AES-128-CBC encrypt [16 B] | 93 | 4.82 | +12.4% | +356.9% | 331 | +431.3% | +11.2% | +203.0% | 1.08% |
| EVP-AES-128-CBC encrypt [16384 B] | 22,238 | 1.22 | +0.0% | +3.3% | 734 | +2.5% | +0.1% | +4.1% | 0.01% |
| EVP-AES-128-CBC encrypt init | 200 | 6.48 | -1.4% | +86.8% | 174 | +90.3% | +8.8% | +52.9% | 3.39% |
| EVP-AES-128-CTR decrypt [1350 B] | 368 | 8.58 | +0.4% | +93.6% | 344 | +127.0% | +5.3% | +72.9% | 0.12% |
| EVP-AES-128-CTR decrypt [16 B] | 79 | 5.15 | -21.3% | +388.9% | 307 | +547.6% | +18.0% | +295.1% | 15.27% |
| EVP-AES-128-CTR decrypt [16384 B] | 3,853 | 8.52 | -0.0% | +8.4% | 322 | +10.9% | +0.2% | +5.7% | 0.02% |
| EVP-AES-128-CTR decrypt init | 201 | 6.31 | -2.5% | +82.4% | 166 | +86.4% | +7.2% | +53.9% | 3.40% |
| EVP-AES-128-CTR encrypt [1350 B] | 358 | 8.69 | +0.5% | +96.5% | 346 | +116.9% | +3.0% | +60.7% | 0.13% |
| EVP-AES-128-CTR encrypt [16 B] | 103 | 3.53 | -49.3% | +263.4% | 271 | +340.0% | -34.5% | +144.1% | 0.21% |
| EVP-AES-128-CTR encrypt [16384 B] | 3,843 | 8.53 | -0.1% | +8.3% | 319 | +10.3% | +0.1% | +4.4% | 0.05% |
| EVP-AES-128-CTR encrypt init | 205 | 6.17 | -5.1% | +78.8% | 162 | +82.7% | +4.5% | +50.1% | 2.41% |
| EVP-AES-128-GCM decrypt [1350 B] | 645 | 6.19 | -0.4% | +87.5% | 564 | +100.1% | +5.5% | +53.8% | 0.11% |
| EVP-AES-128-GCM decrypt [16 B] | 137 | 7.01 | +7.9% | +411.0% | 564 | +473.1% | +18.8% | +256.5% | 0.13% |
| EVP-AES-128-GCM decrypt [16384 B] | 5,674 | 6.33 | -0.1% | +9.7% | 548 | +11.4% | +0.4% | +6.1% | 0.01% |
| EVP-AES-128-GCM decrypt init | 415 | 4.74 | +6.7% | +39.1% | 163 | +43.2% | +9.6% | +24.9% | 0.10% |
| EVP-AES-128-GCM encrypt [1350 B] | 666 | 5.95 | +1.9% | +83.3% | 554 | +94.5% | +6.1% | +48.8% | 0.30% |
| EVP-AES-128-GCM encrypt [16 B] | 173 | 5.42 | +0.1% | +304.2% | 525 | +345.8% | +0.7% | +173.2% | 0.17% |
| EVP-AES-128-GCM encrypt [16384 B] | 5,753 | 6.24 | +0.0% | +9.7% | 556 | +11.0% | +0.6% | +5.9% | 0.03% |
| EVP-AES-128-GCM encrypt init | 397 | 4.46 | +6.8% | +40.9% | 162 | +45.4% | +10.2% | +26.1% | 0.07% |
| RNG [1350 B] | 7,274 | 2.61 | -23.6% | +6.1% | 442 | +7.6% | -23.1% | -20.4% | 0.09% |
| RNG [16 B] | 6,786 | 2.26 | -25.6% | +7.0% | 476 | +8.6% | -25.0% | -22.1% | 0.17% |
| RNG [16384 B] | 12,463 | 4.81 | -13.9% | +3.7% | 457 | +4.3% | -13.4% | -12.4% | 0.06% |

## IPC and instructions per op, every arm

| row | instr/op A | IPC A | IPC C | IPC B | IPC Bs | IPC H | IPC Hs | B-A instr |
|---|---|---|---|---|---|---|---|---|
| AEAD-AES-128-CBC-SHA1 open [1350 B] | 18,972 | 4.25 | 4.25 | 4.14 | 3.93 | 4.26 | 3.99 | 78 |
| AEAD-AES-128-CBC-SHA1 open [16 B] | 5,938 | 4.89 | 4.87 | 4.42 | 3.73 | 4.91 | 3.90 | 78 |
| AEAD-AES-128-CBC-SHA1 open [16384 B] | 70,833 | 2.63 | 2.63 | 2.62 | 2.60 | 2.63 | 2.60 | 78 |
| AEAD-AES-128-CBC-SHA1 open init | 3,168 | 5.31 | 5.11 | 4.19 | 3.79 | 5.17 | 4.19 | 35 |
| AEAD-AES-128-CBC-SHA1 seal [1350 B] | 6,650 | 1.68 | 1.66 | 1.60 | 1.47 | 1.66 | 1.49 | 110 |
| AEAD-AES-128-CBC-SHA1 seal [16 B] | 1,985 | 5.16 | 4.90 | 3.67 | 2.33 | 4.92 | 2.52 | 95 |
| AEAD-AES-128-CBC-SHA1 seal [16384 B] | 54,831 | 1.25 | 1.25 | 1.25 | 1.24 | 1.25 | 1.24 | 95 |
| AEAD-AES-128-CBC-SHA1 seal init | 3,185 | 5.29 | 5.07 | 4.17 | 3.76 | 5.14 | 4.17 | 36 |
| AEAD-AES-128-CCM-Bluetooth seal [1350 B] | 8,769 | 2.25 | 2.24 | 2.16 | 2.18 | 2.25 | 2.22 | 18 |
| AEAD-AES-128-CCM-Bluetooth seal [16 B] | 948 | 5.37 | 5.34 | 3.13 | 3.05 | 5.44 | 3.92 | 18 |
| AEAD-AES-128-CCM-Bluetooth seal [16384 B] | 94,736 | 2.08 | 2.07 | 2.07 | 2.10 | 2.07 | 2.10 | 17 |
| AEAD-AES-128-CCM-Bluetooth seal init | 347 | 5.48 | 5.53 | 1.40 | 1.34 | 5.74 | 2.01 | 20 |
| AEAD-AES-128-GCM open [1350 B] | 3,871 | 5.47 | 5.47 | 4.46 | 4.15 | 5.22 | 4.61 | 49 |
| AEAD-AES-128-GCM open [16 B] | 841 | 4.09 | 4.08 | 2.35 | 2.05 | 3.51 | 2.44 | 49 |
| AEAD-AES-128-GCM open [16384 B] | 35,797 | 6.25 | 6.25 | 6.09 | 6.00 | 6.21 | 6.09 | 49 |
| AEAD-AES-128-GCM open init | 670 | 2.79 | 2.76 | 1.73 | 1.63 | 2.83 | 2.10 | 20 |
| AEAD-AES-128-GCM seal [1350 B] | 3,812 | 5.36 | 5.34 | 4.43 | 4.42 | 5.22 | 4.80 | 18 |
| AEAD-AES-128-GCM seal [16 B] | 785 | 4.14 | 4.13 | 2.34 | 2.31 | 3.81 | 2.90 | 18 |
| AEAD-AES-128-GCM seal [16384 B] | 35,740 | 6.18 | 6.18 | 6.00 | 5.99 | 6.14 | 6.06 | 18 |
| AEAD-AES-128-GCM seal init | 670 | 2.79 | 2.77 | 1.73 | 1.63 | 2.83 | 2.09 | 20 |
| AEAD-AES-128-GCM-SIV open [1350 B] | 9,817 | 5.47 | 5.46 | 4.91 | 4.79 | 5.50 | 4.96 | 49 |
| AEAD-AES-128-GCM-SIV open [16 B] | 1,560 | 4.64 | 4.62 | 2.93 | 2.66 | 4.68 | 3.02 | 49 |
| AEAD-AES-128-GCM-SIV open [16384 B] | 100,333 | 5.81 | 5.81 | 5.74 | 5.73 | 5.81 | 5.75 | 49 |
| AEAD-AES-128-GCM-SIV open init | 386 | 4.82 | 4.80 | 1.58 | 1.44 | 5.01 | 2.14 | 20 |
| AEAD-AES-128-GCM-SIV seal [1350 B] | 9,777 | 5.45 | 5.44 | 5.10 | 5.04 | 5.44 | 5.24 | 18 |
| AEAD-AES-128-GCM-SIV seal [16 B] | 1,520 | 3.78 | 3.53 | 2.80 | 2.74 | 3.57 | 3.14 | 18 |
| AEAD-AES-128-GCM-SIV seal [16384 B] | 100,293 | 5.81 | 5.81 | 5.77 | 5.77 | 5.81 | 5.79 | 18 |
| AEAD-AES-128-GCM-SIV seal init | 386 | 4.82 | 4.79 | 1.58 | 1.44 | 5.02 | 2.14 | 20 |
| AEAD-ChaCha20-Poly1305 seal [1350 B] | 11,054 | 3.50 | 3.51 | 3.39 | 3.36 | 3.51 | 3.44 | 18 |
| AEAD-ChaCha20-Poly1305 seal [16 B] | 1,479 | 2.56 | 2.56 | 1.79 | 1.75 | 2.37 | 1.91 | 20 |
| AEAD-ChaCha20-Poly1305 seal [16384 B] | 115,104 | 3.67 | 3.67 | 3.66 | 3.66 | 3.67 | 3.67 | 18 |
| AEAD-ChaCha20-Poly1305 seal init | 155 | 4.44 | 4.89 | 1.32 | 1.08 | 5.24 | 1.84 | 20 |
| AES-128 decrypt | 65 | 1.94 | 1.94 | 0.74 | 0.61 | 2.69 | 1.09 | 29 |
| AES-128 decrypt setup | 248 | 3.41 | 3.41 | 1.11 | 1.09 | 3.80 | 1.51 | 26 |
| AES-128 encrypt | 65 | 1.94 | 1.94 | 0.74 | 0.61 | 2.67 | 1.09 | 29 |
| AES-128 encrypt setup | 198 | 4.97 | 4.98 | 0.97 | 0.93 | 5.20 | 1.26 | 26 |
| CMAC-AES-128-CBC [1350 B] | 8,645 | 2.55 | 2.61 | 0.87 | 0.76 | 3.00 | 1.28 | 1,394 |
| CMAC-AES-128-CBC [16 B] | 360 | 6.42 | 6.43 | 1.42 | 1.24 | 6.85 | 2.07 | 44 |
| CMAC-AES-128-CBC [16384 B] | 100,619 | 2.46 | 2.46 | 0.85 | 0.75 | 2.91 | 1.25 | 16,430 |
| CMAC-AES-128-CBC init | 1,662 | 5.55 | 5.54 | 2.67 | 2.42 | 5.56 | 3.31 | 64 |
| ECDSA P-256 signing | 155,654 | 3.64 | 3.78 | 3.58 | 3.57 | 3.77 | 3.75 | 100 |
| ECDSA P-256 verify | 523,518 | 4.98 | 4.98 | 4.98 | 4.98 | 4.98 | 4.98 | -554 |
| EVP-AES-128-CBC decrypt [1350 B] | 3,126 | 8.86 | 8.87 | 4.65 | 3.61 | 8.91 | 4.67 | 89 |
| EVP-AES-128-CBC decrypt [16 B] | 685 | 6.58 | 6.59 | 1.76 | 1.20 | 5.97 | 1.75 | 89 |
| EVP-AES-128-CBC decrypt [16384 B] | 31,043 | 9.38 | 9.38 | 8.62 | 8.14 | 9.45 | 8.59 | 89 |
| EVP-AES-128-CBC decrypt init | 1,351 | 6.34 | 6.14 | 3.60 | 3.50 | 5.97 | 4.26 | 20 |
| EVP-AES-128-CBC encrypt [1350 B] | 2,666 | 1.39 | 1.39 | 1.16 | 1.16 | 1.40 | 1.26 | 62 |
| EVP-AES-128-CBC encrypt [16 B] | 446 | 4.82 | 4.28 | 1.20 | 1.04 | 4.79 | 1.77 | 62 |
| EVP-AES-128-CBC encrypt [16384 B] | 27,047 | 1.22 | 1.22 | 1.18 | 1.19 | 1.22 | 1.17 | 62 |
| EVP-AES-128-CBC encrypt init | 1,298 | 6.48 | 6.57 | 3.52 | 3.46 | 6.03 | 4.29 | 20 |
| EVP-AES-128-CTR decrypt [1350 B] | 3,154 | 8.58 | 8.54 | 4.53 | 3.87 | 8.29 | 5.06 | 74 |
| EVP-AES-128-CTR decrypt [16 B] | 407 | 5.15 | 6.55 | 1.25 | 0.95 | 4.99 | 1.50 | 74 |
| EVP-AES-128-CTR decrypt [16384 B] | 32,810 | 8.52 | 8.52 | 7.88 | 7.70 | 8.51 | 8.07 | 74 |
| EVP-AES-128-CTR decrypt init | 1,270 | 6.31 | 6.47 | 3.51 | 3.44 | 5.96 | 4.15 | 20 |
| EVP-AES-128-CTR encrypt [1350 B] | 3,110 | 8.69 | 8.65 | 4.51 | 4.09 | 8.56 | 5.49 | 62 |
| EVP-AES-128-CTR encrypt [16 B] | 363 | 3.53 | 6.96 | 1.14 | 0.95 | 6.08 | 1.64 | 62 |
| EVP-AES-128-CTR encrypt [16384 B] | 32,766 | 8.53 | 8.54 | 7.89 | 7.74 | 8.53 | 8.18 | 62 |
| EVP-AES-128-CTR encrypt init | 1,267 | 6.17 | 6.51 | 3.51 | 3.44 | 5.98 | 4.17 | 20 |
| EVP-AES-128-GCM decrypt [1350 B] | 3,989 | 6.19 | 6.21 | 3.36 | 3.16 | 5.95 | 4.09 | 77 |
| EVP-AES-128-GCM decrypt [16 B] | 962 | 7.01 | 6.50 | 1.48 | 1.33 | 6.24 | 2.09 | 77 |
| EVP-AES-128-GCM decrypt [16384 B] | 35,918 | 6.33 | 6.34 | 5.79 | 5.70 | 6.31 | 5.97 | 77 |
| EVP-AES-128-GCM decrypt init | 1,969 | 4.74 | 4.44 | 3.44 | 3.35 | 4.36 | 3.83 | 20 |
| EVP-AES-128-GCM encrypt [1350 B] | 3,960 | 5.95 | 5.84 | 3.31 | 3.12 | 5.69 | 4.07 | 81 |
| EVP-AES-128-GCM encrypt [16 B] | 936 | 5.42 | 5.42 | 1.46 | 1.33 | 5.74 | 2.12 | 81 |
| EVP-AES-128-GCM encrypt [16384 B] | 35,891 | 6.24 | 6.24 | 5.70 | 5.63 | 6.21 | 5.90 | 81 |
| EVP-AES-128-GCM encrypt init | 1,770 | 4.46 | 4.18 | 3.20 | 3.11 | 4.08 | 3.57 | 20 |
| RNG [1350 B] | 19,010 | 2.61 | 3.40 | 2.47 | 2.44 | 3.39 | 3.27 | 67 |
| RNG [16 B] | 15,328 | 2.26 | 3.01 | 2.12 | 2.09 | 3.00 | 2.89 | 66 |
| RNG [16384 B] | 59,934 | 4.81 | 5.58 | 4.64 | 4.62 | 5.55 | 5.48 | 67 |

## The paper's ten rows

| # | op | A cyc/op | entries/op | C blanket | B bracket | Bs bracket+sb | H hoisted | Hs hoisted+sb | MAD |
|---|---|---|---|---|---|---|---|---|---|
| 1 | AES-128 single block | 34 | 1 | -0% | +278% | +366% | -2% | +144% | 0.10% |
| 2 | EVP AES-GCM encrypt, 16 B | 173 | 3 | +0% | +304% | +346% | +1% | +173% | 0.17% |
| 3 | AEAD AES-GCM seal, 16 B | 190 | 1 | -0% | +81% | +84% | +11% | +45% | 0.10% |
| 4 | AEAD AES-GCM open, 16 B | 206 | 1 | +0% | +84% | +110% | +22% | +77% | 0.03% |
| 5 | AEAD ChaCha20-Poly1305 seal, 16 B | 579 | 2 | -0% | +45% | +48% | +9% | +35% | 0.15% |
| 6 | AEAD AES-GCM seal, 1350 B (a TLS record) | 711 | 1 | +0% | +22% | +22% | +3% | +12% | 0.18% |
| 7 | AEAD AES-GCM seal, 16 KB | 5,784 | 1 | -0% | +3% | +3% | +1% | +2% | 0.02% |
| 8 | CMAC-AES-128, 16 KB | 40,820 | 1,033 | +0% | +237% | +285% | -4% | +125% | 0.08% |
| 9 | ECDSA P-256 sign | 42,773 | 5 | -4% | +2% | +2% | -4% | -3% | 0.07% |
| 10 | RNG, 16 B | 6,786 | 3 | -26% | +7% | +9% | -25% | -22% | 0.17% |

Entries per op = (B - A) / one entry's price: 153 cycles for an AEAD-level entry, 94 for a single-block AES entry (rows 1 and 8). Run only these rows with BENCH_TESTS="AES-128,AEAD-ChaCha20-Poly1305,ECDSA P-256,RNG" CHUNKS=16,1350,16384.
