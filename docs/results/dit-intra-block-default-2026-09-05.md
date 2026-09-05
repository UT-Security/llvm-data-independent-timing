# Intra-block placement as the default: what it changes on libsodium

**Measured 2026-09-05, gem5 NeoverseV2 FDP, libsodium 1.0.21, the shipped
defaults (callee contract, twins, round-11 fixpoint seeds, owned list), block
placement against intra-block placement, each with and without
`-taint-dit-external-preserves`.** Branch `dit-preserving-symbols`;
`docs/design/dit-placement.md` §5.7 for the mechanism and the seeding bug
the flip exposed.

## 1. What changed

The unit of placement was the basic block: a block with any Need was covered
whole. `-taint-dit-sub-block`, built 2026-09-02 and shipped off, sinks a
block's entry enable to its first Need, hoists a pre-return clear past its
last, and cuts a DIT-off hole across any Need-free run of at least 8
instructions; block entry and exit states, loop coarsening, corridor merging
and the verifier are untouched. **It is the default since 2026-09-05, at the
user's direction; `-taint-dit-sub-block=0` is block placement.**

Turning it on found a real hole: a `tainted-pointee` argument passed on the
stack was seeded into its frame slot as a secret VALUE, so the pointer
loaded from the slot was data-tainted, the load through it came back public,
and the multiply on the secret was not a Need. Block placement had covered
it by accident. Incoming fixed frame objects are now seeded as both kinds
(`taint-analysis-stack-seeded-arg.mir`).

## 2. Static and oracle

| | block | intra-block | block + ext | intra-block + ext |
|---|---|---|---|---|
| `msr DIT` sites in libsodium.a | 358 | 365 | 214 | 221 |
| functions whose sites changed vs block | | 4 of 113 | | 4 |
| signing oracle, protected / uncovered / wasted | 294,164 / 0 / 54,010 | 294,164 / 0 / **53,990** | 294,164 / 0 / 54,010 | 294,164 / 0 / 53,990 |

Four functions change (`argon2_hash`, `blake2b_init`, `blake2b_init_key`,
`ge25519_from_hash`): a few holes cut, seven more sites. The signing path
runs almost entirely inside whole twins, which intra-block placement does
not touch, so its wasted coverage moves by 20 operations.

## 3. Timing

**Crypto matrix** (ed25519 50 x 1 KiB, AEAD 200 x 1400 B; each arm vs its
own NOP twin, renamed / serialising, DIT writes):

| arm | ed25519 | writes | AEAD | writes |
|---|---|---|---|---|
| block | +1.42% / +2.20% | 800 | +0.64% / +6.09% | 7,600 |
| **intra-block** | -0.34% / +0.61% | 800 | +0.41% / +6.66% | 7,600 |
| block + ext | +0.64% / +0.72% | 100 | -0.04% / +4.61% | 6,400 |
| **intra-block + ext** | -0.88% / -0.49% | 100 | -0.01% / +5.23% | 6,400 |

**Experiment 09** (cycles per op vs base, renamed / serialising, switches
per op; `data/gem5_intra_block{,_ext}.csv`):

| benchmark | blanket | block | intra-block | block + ext | intra-block + ext |
|---|---|---|---|---|---|
| ed25519 sign | +0.22% | -3.75 / -2.60 (16) | -1.44 / -1.04 (16) | -1.78 / -3.50 (2) | -1.78 / -1.50 (2) |
| chacha20-poly1305 encrypt | +1.31% | +2.37 / +43.88 (38) | +2.37 / +44.79 (38) | +4.13 / +36.37 (32) | +3.88 / +39.31 (32) |
| chacha20-poly1305 decrypt | +0.70% | +3.29 / +42.42 (39) | +3.43 / +45.63 (39) | +6.64 / +35.89 (32) | +6.67 / +38.03 (32) |
| aes256-gcm encrypt | +0.41% | +0.25 / +12.42 (6) | +0.25 / +13.39 (6) | -6.16 / +3.53 (2) | -6.16 / +3.40 (2) |
| aes256-gcm decrypt | +8.39% | +10.06 / +36.06 (6) | +9.87 / +36.34 (6) | +9.32 / +23.80 (2) | +9.32 / +24.36 (2) |

**Experiment 02** (IPC overhead vs unhardened, renamed / serialising,
switches per request; `data/gem5_arms_intra_block{,_ext}.csv`):

| L | blanket | block | block + ext | intra-block | intra-block + ext |
|---|---|---|---|---|---|
| 10 | +7.6% | -1.0 / +28.4 (38) | -2.1 / +23.8 (32) | +0.1 / +32.0 (38) | -1.7 / +24.8 (32) |
| 50 | +12.6% | -0.5 / +22.1 (38) | -0.1 / +18.0 (32) | -0.6 / +24.9 (38) | -0.1 / +19.2 (32) |
| 200 | +20.2% | -0.4 / +11.8 (38) | -1.0 / +9.9 (32) | +1.2 / +13.5 (38) | -1.1 / +10.0 (32) |
| 1000 | +27.7% | +0.2 / +3.2 (38) | -0.7 / +2.4 (32) | +0.1 / +3.8 (38) | -0.1 / +2.1 (32) |
| 5000 | +30.6% | +0.1 / +0.7 (38) | -0.1 / +1.0 (32) | +0.0 / +0.8 (38) | +0.0 / +0.1 (32) |
| 20000 | +31.3% | +0.4 / -0.1 (38) | +0.2 / +0.7 (32) | +0.2 / +0.5 (38) | +0.2 / +0.0 (32) |

## 4. Reading

- **On libsodium the flip is close to inert, and that is the expected
  shape.** Executed switch counts are identical on every row, the oracle is
  identical to 20 operations, and every timing difference is matched by the
  arm's NOP twin: the four changed functions shifted addresses and the
  numbers wobble by the layout band, up to 3 points on the serialising
  chacha rows in either direction. The hot code here runs in whole twins,
  which intra-block placement does not enter.
- **Where it acts** is a function whose secret work sits in an original
  behind a public preamble or ahead of a public tail: the mbedTLS resumption
  measurement of 2026-09-02 (2% less over-protection per resumption) is that
  case, and a mixed one-block function narrows from the whole block to the
  span between its first and last Need
  (`taint-analysis-dit-precision.mir`: precision 25 -> 100).
- **The scheduler bounds it.** A secret load hoisted into a public preamble
  is a Need and pins the enable there; the compiled code, not the source,
  decides where the first secret instruction is.
- **The seeding bug is the substantive result of the flip.** Whole-block
  cover had hidden a stack-passed pointee argument being seeded as a value;
  the fix applies to every build, block placement included.

## 5. Reproduce

As `docs/results/dit-external-preserves-2026-09-05.md` §6 with the rebuilt
compiler; `-mllvm -taint-dit-sub-block=0` for the block arms.
