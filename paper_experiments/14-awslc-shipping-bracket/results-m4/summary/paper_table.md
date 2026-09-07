| # | op | A cyc/op | entries/op | C blanket | B bracket | Bs bracket+sb | H hoisted | Hs hoisted+sb | MAD |
|---|---|---|---|---|---|---|---|---|---|
| 1 | AES-128 single block | 34 | 1 | -0% | +277% | +365% | -3% | +143% | 0.15% |
| 2 | EVP AES-GCM encrypt, 16 B | 173 | 3 | +0% | +303% | +349% | +1% | +173% | 0.06% |
| 3 | AEAD AES-GCM seal, 16 B | 191 | 1 | -0% | +82% | +83% | +10% | +44% | 0.11% |
| 4 | AEAD AES-GCM open, 16 B | 205 | 1 | +0% | +88% | +113% | +20% | +77% | 0.02% |
| 5 | AEAD ChaCha20-Poly1305 seal, 16 B† | 598† | 2 | -3% | +40% | +43% | +6161232%† | +31% | 2.97% |
| 6 | AEAD AES-GCM seal, 1350 B (a TLS record) | 718 | 1 | -0% | +22% | +22% | +2% | +12% | 0.04% |
| 7 | AEAD AES-GCM seal, 16 KB | 5,795 | 1 | -0% | +3% | +3% | +1% | +2% | 0.02% |
| 8 | CMAC-AES-128, 16 KB | 40,800 | 1,029 | -1% | +236% | +286% | -4% | +126% | 0.10% |
| 9 | ECDSA P-256 sign | 42,748 | 5 | -4% | +2% | +2% | -3% | -3% | 0.11% |
| 10 | RNG, 16 B | 6,794 | 3 | -25% | +7% | +8% | -25% | -21% | 0.14% |
| | **geometric mean of the ratio to A, all cells** | | | **-4%** | **+79%** | **+90%** | **+199%** | **+46%** | |
| | **geometric mean, clean cells only** | | | **-4%** | **+84%** | **+96%** | **-1%** | **+48%** | |

Entries per op = (B - A) / one entry's price: 156 cycles for an AEAD-level entry, 94 for a single-block AES entry (rows 1 and 8). Run only these rows with BENCH_TESTS="AES-128,AEAD-ChaCha20-Poly1305,ECDSA P-256,RNG" CHUNKS=16,1350,16384. † marks a cell whose median implies a clock outside the P-core band: a majority of its samples carried the backward-counter fault; the value is kept but not to be read.
