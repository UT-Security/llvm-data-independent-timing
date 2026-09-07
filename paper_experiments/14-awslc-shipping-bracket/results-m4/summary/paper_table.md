| # | op | A cyc/op | entries/op | C coarse | B AWS default | Bs AWS default + sb | H AWS hoist | Hs AWS hoist + sb | MAD |
|---|---|---|---|---|---|---|---|---|---|
| 1 | AES-128 single block | 34 | 1 | -0% | +277% | +366% | -3% | +144% | 0.15% |
| 2 | EVP AES-GCM encrypt, 16 B | 173 | 3 | +0% | +303% | +348% | +1% | +173% | 0.06% |
| 3 | AEAD AES-GCM seal, 16 B | 191 | 1 | -0% | +82% | +83% | +10% | +44% | 0.11% |
| 4 | AEAD AES-GCM open, 16 B | 205 | 1 | +0% | +88% | +113% | +20% | +77% | 0.02% |
| 5 | AEAD ChaCha20-Poly1305 seal, 16 B | 579 | 2 | +0% | +44% | +48% | +22% | +35% | 2.97% |
| 6 | AEAD AES-GCM seal, 1350 B (a TLS record) | 718 | 1 | -0% | +22% | +22% | +2% | +12% | 0.04% |
| 7 | AEAD AES-GCM seal, 16 KB | 5,795 | 1 | -0% | +3% | +3% | +1% | +2% | 0.02% |
| 8 | CMAC-AES-128, 16 KB | 40,796 | 1,028 | -1% | +236% | +286% | -4% | +127% | 0.10% |
| 9 | ECDSA P-256 sign | 42,742 | 5 | -4% | +2% | +2% | -3% | -3% | 0.11% |
| 10 | RNG, 16 B | 6,795 | 3 | -25% | +7% | +8% | -25% | -21% | 0.14% |
| | **geometric mean of the ratio to A, all cells** | | | **-3%** | **+79%** | **+91%** | **+1%** | **+46%** | |

Entries per op = (B - A) / one entry's price: 156 cycles for an AEAD-level entry, 94 for a single-block AES entry (rows 1 and 8). Run only these rows with BENCH_TESTS="AES-128,AEAD-ChaCha20-Poly1305,ECDSA P-256,RNG" CHUNKS=16,1350,16384.
