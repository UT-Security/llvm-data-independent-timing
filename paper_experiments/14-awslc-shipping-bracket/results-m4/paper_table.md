| # | op | A cyc/op | entries/op | C blanket | B bracket | Bs bracket+sb | H hoisted | Hs hoisted+sb | MAD |
|---|---|---|---|---|---|---|---|---|---|
| 1 | AES-128 single block | 34 | 1 | -0% | +277% | +360% | -2% | +144% | 0.01% |
| 2 | EVP AES-GCM encrypt, 16 B | 173 | 3 | +0% | +304% | +346% | +1% | +173% | 0.13% |
| 3 | AEAD AES-GCM seal, 16 B | 190 | 1 | -0% | +81% | +83% | +10% | +44% | 0.11% |
| 4 | AEAD AES-GCM open, 16 B | 206 | 1 | +0% | +87% | +112% | +21% | +77% | 0.18% |
| 5 | AEAD ChaCha20-Poly1305 seal, 16 B | 579 | 2 | -0% | +44% | +48% | +4% | +35% | 0.14% |
| 6 | AEAD AES-GCM seal, 1350 B (a TLS record) | 716 | 1 | -0% | +21% | +22% | +2% | +12% | 0.38% |
| 7 | AEAD AES-GCM seal, 16 KB | 5,787 | 1 | -0% | +3% | +3% | +1% | +2% | 0.06% |
| 8 | CMAC-AES-128, 16 KB | 40,817 | 1,033 | -1% | +237% | +286% | -4% | +125% | 0.08% |
| 9 | ECDSA P-256 sign | 42,785 | 5 | -4% | +2% | +2% | -3% | -3% | 0.17% |
| 10 | RNG, 16 B | 6,792 | 3 | -25% | +7% | +8% | -25% | -22% | 0.07% |

Entries per op = (B - A) / one entry's price: 154 cycles for an AEAD-level entry, 94 for a single-block AES entry (rows 1 and 8). Run only these rows with BENCH_TESTS="AES-128,AEAD-ChaCha20-Poly1305,ECDSA P-256,RNG" CHUNKS=16,1350,16384.
