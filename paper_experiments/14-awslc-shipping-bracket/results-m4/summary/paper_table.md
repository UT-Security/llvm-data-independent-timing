| # | op | unhardened cyc/op | entries/op | Coarse | AWS default | AWS default + sb | AWS hoist | AWS hoist + sb | MAD |
|---|---|---|---|---|---|---|---|---|---|
| 1 | AES-128 single block | 34 | 1 | -0% | +283% | +366% | -3% | +144% | 0.07% |
| 2 | EVP AES-GCM encrypt, 16 B | 173 | 3 | -0% | +302% | +348% | +1% | +173% | 0.03% |
| 3 | AEAD AES-GCM seal, 16 B | 191 | 1 | -0% | +82% | +84% | +10% | +44% | 0.15% |
| 4 | AEAD AES-GCM open, 16 B | 205 | 1 | +0% | +88% | +111% | +20% | +77% | 0.04% |
| 5 | AEAD ChaCha20-Poly1305 seal, 16 B | 579 | 2 | +0% | +44% | +48% | +14% | +35% | 0.08% |
| 6 | AEAD AES-GCM seal, 1350 B (a TLS record) | 719 | 1 | -0% | +21% | +21% | +2% | +11% | 0.07% |
| 7 | AEAD AES-GCM seal, 16 KB | 5,799 | 1 | -0% | +3% | +3% | +1% | +2% | 0.08% |
| 8 | CMAC-AES-128, 16 KB | 40,845 | 1,011 | -2% | +237% | +285% | -4% | +125% | 0.06% |
| 9 | ECDSA P-256 sign | 42,729 | 5 | -4% | +2% | +2% | -3% | -3% | 0.09% |
| 10 | RNG, 16 B | 6,797 | 3 | -26% | +7% | +8% | -25% | -22% | 0.16% |
| | **geometric mean of the ratio to A, all cells** | | | **-3%** | **+80%** | **+91%** | **+1%** | **+46%** | |

Entries per op = (AWS default - unhardened) / one entry's price: 156 cycles for an AEAD-level entry, 96 for a single-block AES entry (rows 1 and 8). Run only these rows with BENCH_TESTS="AES-128,AEAD-ChaCha20-Poly1305,ECDSA P-256,RNG" CHUNKS=16,1350,16384.
