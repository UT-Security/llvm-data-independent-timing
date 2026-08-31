# The SQLite TCE composite: an encrypt-on-write benchmark, and what it caught

**Rig:** `utils/dit_host_screening/xover/host_sqlite_tce.c`, `tce_payload.{c,h}`,
`scripts/build_tce.sh`, `run_tce.py`.
**Built 2026-08-26, measured under gem5 (NeoverseV2 FDP).** 96-point sweep, all
gates passing.

A real public lane (SQLite) paired with a real crypto library (libsodium), with
the encryption on the **write** path, in a form that runs under gem5 SE mode: an
in-memory database, no sockets, no threads, no file locks.

---

## 1. Why encrypt-on-write

This is the design decision the whole benchmark rests on, so it goes first.

The CIO-parity seed declares the **plaintext and its length** secret, not just the
key. On a decrypt-on-read workload that is fatal without a declassification
mechanism, which the project does not have (`overview.md` open gap #10): the
plaintext leaves the library, everything downstream that touches it becomes
secret-dependent, and you land in the instruction-level interleaving regime where
region placement cost +2890% and the hand oracle degenerated into blanket.

On the write path the plaintext ENTERS at a named boundary and the AEAD output is
declassified by cryptographic semantics, exactly as an ECDSA signature is. The
index maintenance, page splits and journalling that follow are genuinely public
work. **The declassification boundary is the protocol's, not the harness's** --
which is the standard `dit-cpython-case-study.md` set and the objection a reviewer
raises first.

Deployment shape: PostgreSQL + pgsodium Transparent Column Encryption, where a
trigger encrypts one or more columns on INSERT/UPDATE. Postgres itself cannot run
under gem5 SE mode -- it forks per backend and `fork` is `fatal()` in the ARM
syscall table -- so this reproduces the SHAPE on SQLite, which runs unmodified.

## 2. The knob moves only f

Eight BLOB columns are stored on **every** row at **every** point. `enc_cols` of
them are produced by an AEAD call; the rest by a same-sized fill:

```c
for (int j = 0; j < NCOLS; j++) {
    if (j < enc_cols) tce_encrypt_field(field[j], i, j);   /* AEAD */
    else              tce_plain_field(field[j], i, j);     /* memset, same size */
}
```

So the row is byte-identical in SIZE at every setting: the B-tree descends and
splits identically, both indexes are maintained identically, and the only thing
that moves is how much crypto runs. A knob that also moved the public work would
confound the sweep and the curve would mean nothing -- which is the rejected-knob
row in `paper/evaluation-framework.md` §2.3.

Measured natively, f sweeps **0.14% -> 9.39%** while total time stays flat, the
whole rise being the crypto added. R holds at **~0.66 us** per 128-byte field,
above the ~1300-cycle crossover.

The read phase deliberately touches no ciphertext: it is an index descent over the
PUBLIC columns, which is what a real application does far more often than it
decrypts, and it keeps the encrypt-on-write property intact.

## 3. Code flow

```
main(mode, field_bytes, enc_cols, rows, batch, reads, time_secret)
 |
 +-- tce_init(mode, field_bytes, time_secret)          [tce_payload.c]
 |     sodium_init()
 |     fill g_key (THE SECRET) and g_nonce            <- once, before any timing
 |     fill g_plain                                    <- the secret plaintext
 |     if (time_secret) measure R: 256 warm + best-of-8 x 256 AEAD calls
 |     if (mode == DIT_ALWAYS) dit_on()
 |
 +-- CREATE TABLE accounts(id, email, created, c0..c7 BLOB)
 |   CREATE INDEX acc_email; CREATE INDEX acc_created   <- generated from NCOLS
 |
 +-- ===== measured region begins =====
 |
 +-- for i in 0..rows:
 |     if (i % batch == 0) BEGIN
 |     snprintf(email); created = ...                  <- PUBLIC work
 |     tce_row_begin()                                 <- DIT_ROW toggles here
 |     for j in 0..7:
 |         j < enc_cols ? tce_encrypt_field(...)       <- DIT_FIELD toggles here
 |                      : tce_plain_field(...)         <-   same size, no crypto
 |     tce_row_end()
 |     bind id/email/created/c0..c7; step; reset       <- PUBLIC: B-tree + 2 indexes
 |     checksum += every column's first and last byte
 |     if (end of batch) COMMIT
 |
 +-- for i in 0..reads:                                <- PUBLIC: index descent,
 |     SELECT count(*), sum(created) FROM accounts        no ciphertext touched
 |      WHERE email > ? AND created BETWEEN ? AND ?
 |
 +-- ===== measured region ends =====
 |
 +-- print WORK checksum, and HOST line with f, R, toggles, dit_now
```

`tce_encrypt_field` is `crypto_aead_chacha20poly1305_ietf_encrypt`. pgsodium uses
det-XChaCha20; the CIO-parity seed already covers the IETF ChaCha20 form, so this
measures the same shape without widening the annotation set. The nonce varies per
(row, column): replaying one input would manufacture its own predictability.

**Coverage audit** (trap 8 -- an under-protecting oracle looks exactly like a
win): the key and plaintext are filled once in `tce_init`, before any timing.
Inside the measured region the only thing that touches them is the libsodium call
itself, between the oracle's enable and disable. So the oracle covers 100% of
secret-touching work in the region by construction.

## 4. Arms and gates

DIT modes are chosen from argv, so `off`/`always`/`field`/`row` are the SAME
binary and no codegen difference can contaminate the comparison. The compiled arms
differ only in the instrumented libsodium archive; the host, payload and SQLite
are compiled ONCE and linked against each.

| arm | what it is |
|---|---|
| `off` | baseline, DIT never set |
| `always` | blanket: set once at startup, never toggled |
| `field` | oracle, one region per encrypted column |
| `row` | oracle, one region per row |
| `def30` / `def0` | the pass, shipped default and `switch-cyc=0` |
| `nop30` / `nop0` | the layout controls: same code, switches as `HINT #0` |

Three gates in `run_tce.py`, all exact because gem5 is deterministic:

- **`simInsts` identical across switch models** for one binary+input. `time_secret`
  MUST be 0 under gem5: SE mode returns SIMULATED time, so a timed value formatted
  into the output makes instruction counts differ and this fires.
- **`off` reports exactly ZERO `ditSuppressed`.** A non-zero reading means the
  baseline is silently running someone else's placement.
- **Checksums identical across arms** at a point. The checksum folds every column,
  not just the first -- one that witnessed only column 0 could not tell an arm that
  skipped the crypto on columns 1-7 from one that did it.

Sizing: 4000 rows / 400 reads is ~201 M instructions at `enc_cols=0` rising to
~360 M at 8, which is 30-60 min per point under `gem5.fast`. 96 points run
concurrently in one wave.

## 5. Results

Percent change against `off`. Negative is faster.

### Renamed `MSR DIT`

| enc | f | always | field | row | def30 | def0 | nop30 |
|---|---|---|---|---|---|---|---|
| 0/8 | 0.14% | +14.94% | +0.21% | -0.22% | **+14.77%** | +15.67% | -0.51% |
| 1/8 | 1.43% | +13.75% | -0.13% | -0.23% | +0.86% | +1.98% | -0.08% |
| 2/8 | 2.60% | +12.73% | +0.32% | +0.44% | +2.11% | +3.20% | +0.30% |
| 4/8 | 5.03% | +10.67% | +0.07% | +0.09% | +3.55% | +5.56% | +0.02% |
| 6/8 | 7.26% | +9.38% | -0.05% | +0.06% | +5.18% | +6.70% | +0.51% |
| 8/8 | 9.39% | +8.44% | +0.14% | +0.09% | +6.84% | +8.69% | +0.87% |

### Serializing `MSR DIT`

| enc | f | always | field | row | def30 | def0 | nop30 |
|---|---|---|---|---|---|---|---|
| 0/8 | 0.14% | +14.79% | +0.13% | +0.02% | **+14.64%** | +15.50% | -0.40% |
| 1/8 | 1.43% | +13.19% | -0.21% | -0.51% | +3.57% | +5.93% | -0.66% |
| 2/8 | 2.60% | +12.45% | +0.00% | -0.02% | +8.06% | +11.87% | -0.38% |
| 4/8 | 5.03% | +10.92% | +0.49% | +0.15% | +14.61% | +20.60% | +0.19% |
| 6/8 | 7.26% | +9.54% | +0.96% | +0.27% | +19.23% | +26.95% | +0.46% |
| 8/8 | 9.39% | +8.47% | +1.27% | +0.17% | **+23.41%** | +32.43% | +0.57% |

**The oracle is free.** `field` and `row` sit within +-1.3% at all 24 points. The
whole blanket cost is recoverable in principle; nothing about the workload makes
protection expensive.

**The switch model decides the verdict.** Serializing `def30` crosses `always`
between f = 2.6% and 5.0% and reaches +23.41% against blanket's +8.47%. Renamed
never crosses, topping out at +6.84%.

**Layout costs nothing.** `nop30` stays within +-0.9% everywhere, so every point of
`def` cost is switch execution rather than inserted instructions.

**`row` beats `field`** consistently (+0.17% vs +1.27% at enc=8): the granularity
effect, in the expected direction.

## 6. What the enc=0 point caught

At `enc_cols=0` there is **no AEAD call in the measured loop at all**, so the pass
should cost nothing. It costs the full blanket price:

| arm | cycles | vs `off` | `ditSuppressed` |
|---|---|---|---|
| `off` | 98,822,085 | - | 0 |
| `always` | 113,581,974 | +14.94% | 2,378,033 |
| **`def30`** | **113,421,292** | **+14.77%** | **2,383,257 = 100.2% of blanket** |
| `nop30` | 98,315,206 | -0.51% | 0 |
| `field` | 99,034,062 | +0.21% | 0 |

`simInsts` is 201,192,0xx for every arm, within ~30 instructions, so the arms run
identical work and the difference is entirely microarchitectural.
`rename.serializing` is 185, i.e. essentially no switches execute -- this is PURE
DWELL, which is why the two switch models differ by only 0.15 points.

**Cause:** `sodium_init()` -> `_sodium_alloc_init()` -> `randombytes_buf()`, filling
a 16-byte allocator canary. `randombytes_buf` raises DIT and exits through an
INDIRECT TAIL CALL into the randombytes implementation table, which has no epilogue
in which to lower it. Every libsodium program therefore enters `main` with
PSTATE.DIT set and nothing ever clears it. Full analysis:
`docs/design/dit-tailcall-gap.md` §7.

**This is why the benchmark earns its keep.** The regime it makes cheap to measure
-- secret fraction near zero -- is exactly the regime fine-grained placement exists
to win, and it is where a whole-program dwell leak is invisible to every workload
that does enough crypto to mask it. A sweep that started at f = 2% would have
missed it.

Corollary for anyone using this rig: **the f = 0 point cannot be used as a
baseline** while that leak exists. Use `off`.

## 7. Caveats

- **gem5, not silicon.** Ordering and ratios, not M5 magnitudes; gem5 understated
  always-on 4.6x on the one workload measured both ways.
- **SQLite stands in for PostgreSQL.** The shape is pgsodium's; the engine is not.
- **The primitive is IETF ChaCha20-Poly1305**, not pgsodium's det-XChaCha20 -- one
  hchacha20 call and a nonce apart, immaterial to what is being measured, and it
  keeps the seed unchanged.
- **Single-threaded, in-memory.** No WAL, no fsync, no concurrency. The public lane
  is B-tree and index work, not I/O.
