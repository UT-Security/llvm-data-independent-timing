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
