| # | op | A cyc/op | entries/op | C blanket | B bracket | Bs bracket+sb | H hoisted | Hs hoisted+sb | MAD |
|---|---|---|---|---|---|---|---|---|---|
| 1 | AES-128 single block | 34 | 1 | -0% | +280% | +366% | -3% | +144% | 0.04% |
| 2 | EVP AES-GCM encrypt, 16 B | 173 | 3 | +0% | +303% | +347% | +1% | +173% | 0.11% |
| 3 | AEAD AES-GCM seal, 16 B | 191 | 1 | -0% | +82% | +83% | +10% | +44% | 0.09% |
| 4 | AEAD AES-GCM open, 16 B | 205 | 1 | +0% | +89% | +112% | +20% | +77% | 0.02% |
| 5 | AEAD ChaCha20-Poly1305 seal, 16 B | 578 | 2 | +0% | +44% | +48% | +21% | +35% | 0.11% |
| 6 | AEAD AES-GCM seal, 1350 B (a TLS record) | 717 | 1 | -0% | +21% | +22% | +2% | +12% | 0.10% |
| 7 | AEAD AES-GCM seal, 16 KB | 5,787 | 1 | -0% | +3% | +3% | +1% | +2% | 0.02% |
| 8 | CMAC-AES-128, 16 KB | 40,774 | 1,014 | -1% | +236% | +286% | -4% | +125% | 0.01% |
| 9 | ECDSA P-256 sign | 42,710 | 5 | -4% | +2% | +2% | -3% | -3% | 0.03% |
| 10 | RNG, 16 B | 6,781 | 3 | -25% | +7% | +9% | -25% | -22% | 0.06% |

Entries per op = (B - A) / one entry's price: 157 cycles for an AEAD-level entry, 95 for a single-block AES entry (rows 1 and 8). Run only these rows with BENCH_TESTS="AES-128,AEAD-ChaCha20-Poly1305,ECDSA P-256,RNG" CHUNKS=16,1350,16384.
