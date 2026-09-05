# The libc model: external functions that never write PSTATE.DIT

**Measured 2026-09-05, gem5 NeoverseV2 FDP, libsodium 1.0.21, the shipped
defaults (callee contract, twins, round-11 fixpoint seeds, owned list) with
and without `-taint-dit-preserving-symbols=utils/dit_preserving_libc.txt`.**
Branch `dit-preserving-symbols`.

## 1. The problem

Under the callee contract every instrumented function clears DIT at its own
exit, so DIT-on code must re-assert after any call unless the callee is known
to hand the mode back: an in-TU callee with `PreservesDIT`, or a twin. A
callee the build does not define has no summary and got the blunt answer,
"may clear". For glibc that is false, nothing in libc writes PSTATE.DIT, and
it is expensive exactly where a secret loop calls a mover:

- argon2id: `fill_block` is three `copy_block`s compiled to `memcpy`, and the
  twin re-asserted after each, **395,758 executed switches per hash**, +7.58%
  serialising for a hash the old compiler ran unprotected at +1.97%.
- aes256-gcm decrypt: the AD pad copy and the 4-byte tail's `memcpy`/`memset`
  cost four of its six switches per call; `sodium_memzero`'s
  `__explicit_bzero_chk` a fifth.

## 2. The flag

`-taint-dit-preserving-symbols=<file>`: one external symbol per line (`#`
comments) that never writes PSTATE.DIT. `calleeLeavesDITSet` answers yes for
a call to one, so no re-assert follows it in any emitter and the region
verifier does not model it as a clear; step 3b does not retract
`PreservesDIT` through it, so a helper whose only calls are to libc keeps the
bit for its callers. The lookup is by the call's **symbol**, not by module
Function: the `bl memcpy` that lowers an `llvm.memcpy` intrinsic carries an
external-symbol operand and the module usually has no Function for it, which
is exactly the call this is for (the first version tested the Function and
missed every one of them). Two guards keep it sound: the file is consulted
only for a symbol this module does not define, and only when the owned list
does not name it, so a hardened `memcpy` of ours is never overridden. Nothing
that takes a callback belongs in the file. It removes re-asserts only; a
mover handed a secret is still an `external-call` obligation.

`utils/dit_preserving_libc.txt` is the glibc set: the movers and their
fortified forms, `explicit_bzero` and its `_chk`, the string and memory
scans, the allocators, the raw syscall wrappers (the kernel saves and
restores PSTATE across a syscall). Test
`clang/test/CodeGen/taint-dit-preserving.c`.

## 3. Static and oracle

| | shipped | + libc model |
|---|---|---|
| `msr DIT` sites in libsodium.a | 358 | 214 |
| functions carrying a site | 113 | 84 |
| `argon2_fill_segment_ref.dit` sites | 7 | 0 |
| `crypto_aead_aes256gcm_decrypt_detached_afternm.dit` sites | 8 | 0 |
| signing oracle, two signatures: protected / uncovered / wasted | 294,164 / 0 / 54,010 | 294,164 / 0 / 54,010 |

Coverage cannot move: a re-assert after a callee that does not touch the mode
restores a state that was never lost.

## 4. Timing

**Crypto matrix** (ed25519 50 x 1 KiB, AEAD 200 x 1400 B, each arm against
its own instruction-matched NOP twin):

| arm | ed25519 renamed / serialising | DIT writes | AEAD renamed / serialising | DIT writes |
|---|---|---|---|---|
| twins as shipped | +1.42% / +2.20% | 800 | +0.64% / +6.09% | 7,600 |
| blanket | +1.77% | 0 | +0.80% | 0 |
| **+ libc model** | **+0.53% / +0.57%** | **100** | **+0.54% / +4.77%** | **6,400** |

Two DIT writes per signature: the entry enable and exit clear of the public
function, the same two a hand-placed bracket executes.

**Experiment 09** (cycles per op vs base, renamed / serialising, switches per
op; `data/gem5_preserving.csv`):

| benchmark | blanket | API bracket (2 sw) | pass, shipped | pass + libc model |
|---|---|---|---|---|
| ed25519 sign | +0.22% | +0.73% / +1.14% | -3.75% / -2.60% (16) | -1.78% / -3.50% (**2**) |
| chacha20-poly1305 encrypt | +1.31% | +3.19% / +3.28% | +2.37% / +43.88% (38) | +4.13% / +36.37% (32) |
| chacha20-poly1305 decrypt | +0.70% | +1.50% / +4.33% | +3.29% / +42.42% (39) | +6.64% / +35.89% (32) |
| aes256-gcm encrypt | +0.41% | +0.25% / +3.78% | +0.25% / +12.42% (6) | -6.16% / +3.53% (**2**) |
| aes256-gcm decrypt | +8.39% | +9.13% / +23.75% | +10.06% / +36.06% (6) | +9.32% / +23.80% (**2**) |
| argon2id | +0.51% | +2.06% / +2.04% | +2.37% / +7.58% (395,758) | pending |

**Experiment 02** (IPC overhead vs unhardened, switches per request;
`data/gem5_arms_preserving.csv`):

| L | f | blanket | pass, shipped ren / ser | pass + libc model |
|---|---|---|---|---|
| 10 | 96.8% | +7.3% | -1.0 / +28.4 (38) | -2.4 / +23.4 (32) |
| 50 | 81.2% | +12.6% | -0.5 / +22.1 (38) | -0.1 / +18.0 (32) |
| 200 | 45.0% | +20.2% | -0.4 / +11.8 (38) | -1.0 / +9.9 (32) |
| 1000 | 13.1% | +27.7% | +0.2 / +3.2 (38) | -0.7 / +2.4 (32) |
| 5000 | 2.6% | +30.6% | +0.1 / +0.7 (38) | -0.1 / +1.0 (32) |
| 20000 | 0.6% | +31.3% | +0.4 / -0.1 (38) | +0.2 / +0.7 (32) |

## 5. Reading

- **Where the library's calls are direct, the pass now executes the
  bracket's two switches and pays the bracket's serialising cost.** ed25519,
  both AES-GCM rows, and the signing matrix (800 -> 100 writes).
- **Chacha keeps 32 switches** behind the Poly1305 and ChaCha20
  implementation tables: indirect calls no twin is reached through. The
  libc model took the six that were movers; the rest is a property of the
  library's dispatch, and the bracket (+3.3%) is the bound the pass would
  reach if those tables pointed at twins.
- **The renamed column moves with each binary's layout term, not with the
  switches**, as before: every difference between the shipped and model
  columns is matched by the arm's NOP twin (aes256-gcm encrypt's -6.16% is
  a -5.94% NOP twin). On renamed hardware the cost of the solution is dwell
  over secret work plus layout; on serialising hardware the libc model is
  worth 5 to 12 points per AEAD call and the whole of argon2id's cost.
- **Default or not.** The flag is opt-in with the file at
  `utils/dit_preserving_libc.txt`; the numbers say the glibc set should be
  on in every hardened build, and the open design question is whether that
  set belongs inside the compiler with the file as an extension.

## 6. Reproduce

Libraries: `build_libsodium_native.sh` with `EXTRA_CFLAGS="-mllvm
-taint-dit-contract=callee -mllvm -taint-owned-symbols=<list> -mllvm
-taint-dit-preserving-symbols=<llvm>/utils/dit_preserving_libc.txt"`; the
oracle and the crypto matrix as `run_clone_timing.sh`; experiment 02 via
`build_gem5_linux.sh` and experiment 09 via `build_arms.sh`, both with
`TAINT_EXTRA` set to the flag.
