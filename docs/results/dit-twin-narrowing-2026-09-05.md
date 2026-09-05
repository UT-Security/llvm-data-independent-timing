# Narrowing twins: can a `.dit` twin give back the dwell it swallows?

**Measured 2026-09-05, gem5 NeoverseV2 FDP, libsodium 1.0.21, the callee
contract, the round-11 fixpoint seeds and the owned list.** Branch
`dit-twin-narrow`. The question, from the user: a twin is entered DIT-on and
never narrows, so it covers whatever public stretch the original's placement
would have left off; if the twin cleared DIT at its top and turned it on only
at its first secret instruction, would the renamed-model cost, which is the
cost of our solution on Apple silicon, come down?

**Answer: no, on this library.** At the shipped switch cost the admission
test merges every corridor a twin has and nothing changes; with switches
priced at zero the twins clear at their tops, coverage drops by 0.7% of the
wasted operations, and the renamed cost goes up on signing and nowhere down.
The switches are not the lever on these workloads: the renamed cost is dwell
over secret work plus layout, and the dwell the twins added is inside the
secret work, not around it.

## 1. What was built

`-taint-dit-twin-narrow` (default off): a twin runs the region emitter like
an original with the entry and exit inverted. It clears at entry when its
leading blocks hold no secret, cuts the corridors the admission test admits,
enables at each Off->On boundary as usual, and enables again before any
return it would otherwise reach DIT-off; the verifier checks every return is
reached On and falls the twin back to whole-function coverage if not. A
twin's leading and trailing corridors have a real toggle pair (the entry
clear and the first enable; the clear on entry and the enable before the
return), so they are priced like interior ones. `-taint-dit-twin-switch-cyc`
is the switch cost charged inside twins: -1 uses the global 30, 0 asserts
switches are free, which turns the twin into the maximal narrowing, DIT off
at the top of every twin whose entry block holds no secret. Test
`clang/test/CodeGen/taint-dit-twin-narrow.c`; the default build is
byte-identical (whole-archive disassembly of libsodium).

## 2. Two analysis holes the whole twin had been hiding

Both found by the signing oracle on the first narrowing build, both fixed
here, both invisible before because a whole-function twin covers everything
regardless of what the analysis knows.

- **A reached twin had no taint.** A twin has no callers while the fixed
  point runs (calls are redirected at emission), so the twin of a function
  that is reached by propagation rather than seed was analysed with none of
  the argument taint its original receives: `fe25519_invert.dit` ran its
  whole body DIT-off, 42,848 uncovered operations per two signatures. The
  twin now inherits its original's incoming argument sets and stack-argument
  bit (`propagateArgTaintToCallees`).
- **A NEON register tuple read clean.** The state never sets a tuple
  (`$q3_q4`) as a whole, so a tuple USE read untainted and a tuple-stored
  value never tainted its cells. In `ge25519_scalarmult_base` the `st2` that
  parks the scalar's nibbles on the frame left them public and the carry
  loop ran DIT-off in the maximal twin: 888 uncovered operations. A tuple use
  now reads any part's taint and a tuple def marks its parts
  (`regUseTainted`, `updateWithAliases`). The default libsodium build is
  byte-identical after the fix; the original's entry enable had covered the
  loop by accident. flowprobe's C4/C7 are NOT closed by this: its channel is
  the callee writing the secret through a pointer parameter, which the mod
  set has no per-argument precision for (P1), not the tuple.

## 3. Coverage, signing oracle, two signatures

| twins | protected | uncovered | wasted | executed DIT writes | `msr DIT` sites |
|---|---|---|---|---|---|
| as shipped | 294,164 | 0 | 54,010 | 41 | 358 |
| narrowing, default cost | 294,164 | 0 | 54,010 | 41 | 361 |
| narrowing, cost 0 | 294,164 | 0 | 53,624 | 127 | 587 |

At the default cost the three extra sites are in functions the signing path
does not run; nothing on the path is narrowed. At cost 0 the twins give back
386 of 54,010 wasted operations, 0.7%, for three times the switches.

## 4. Timing

**The crypto matrix** (ed25519 50 x 1 KiB, AEAD 200 x 1400 B, each arm
against its own NOP twin, the instruction-matched baseline):

| arm | ed25519 renamed / serialising | DIT writes | AEAD renamed / serialising | DIT writes |
|---|---|---|---|---|
| twins as shipped | +1.42% / +2.20% | 800 | +0.64% / +6.09% | 7,600 |
| blanket (on the shipped NOP library) | +1.77% | 0 | +0.80% | 0 |
| narrowing, default cost | +1.05% / +1.56% | 800 | +0.03% / +5.42% | 7,600 |
| narrowing, cost 0 | **+6.74% / +2.98%** | 4,300 | +0.16% / +6.06% | 8,800 |

**Experiment 02** (IPC overhead vs unhardened, the pass arm; switches per
request in brackets; `data/gem5_arms_twin_narrow{,0}.csv`):

| L | f | blanket | shipped twins ren / ser | narrowing default | narrowing cost 0 |
|---|---|---|---|---|---|
| 10 | 96.8% | +7.6% | -1.0 / +28.4 (38) | +3.5 / +30.5 (38) | -2.0 / +33.0 (45) |
| 50 | 81.2% | +12.6% | -0.5 / +22.1 (38) | -1.2 / +23.7 (38) | -2.2 / +25.9 (45) |
| 200 | 45.0% | +20.2% | -0.4 / +11.8 (38) | +0.1 / +12.3 (38) | -1.4 / +14.0 (45) |
| 1000 | 13.1% | +27.7% | +0.2 / +3.2 (38) | +0.1 / +3.4 (38) | -0.6 / +3.9 (45) |
| 5000 | 2.6% | +30.6% | +0.1 / +0.7 (38) | -0.2 / +0.6 (38) | +0.0 / +1.0 (45) |
| 20000 | 0.6% | +31.3% | +0.4 / -0.1 (38) | +0.2 / +0.5 (38) | +0.7 / +0.5 (45) |

**Experiment 09** (cycles per op vs base, renamed / serialising, switches per
op, the arm's NOP twin in brackets; `data/gem5_twin_narrow{,0}.csv`):

| benchmark | blanket | shipped twins | narrowing, default cost | narrowing, cost 0 |
|---|---|---|---|---|
| ed25519 sign | +0.2% | -3.7% / -2.6% (16) [-3.7%] | -3.6% / -3.5% (16) [-4.0%] | -2.7% / -1.0% (56) [-3.3%] |
| chacha enc | +1.3% | +2.4% / +43.9% (38) [+2.4%] | +2.1% / +43.0% (38) [+2.3%] | +3.7% / +50.1% (45) [+5.1%] |
| chacha dec | +0.7% | +3.3% / +42.4% (39) [+4.1%] | +2.9% / +44.7% (39) [+4.7%] | +5.8% / +53.4% (50) [+6.1%] |
| aes-gcm enc | +0.4% | +0.2% / +12.4% (6) [-0.0%] | -0.7% / +12.2% (6) [-0.7%] | -0.1% / +49.3% (28) [-0.2%] |
| aes-gcm dec | +8.4% | +10.1% / +36.1% (6) [+5.7%] | +9.7% / +48.9% (8) [+1.3%] | +9.3% / +54.6% (13) [+1.0%] |

## 5. Reading

- **At the shipped cost there is nothing to narrow.** Every corridor a twin
  has on these paths is shorter than the two switches it would take to open
  it, so the admission test merges them all; the only differences from the
  shipped twins are layout jitter and one extra pair in aes-gcm decrypt.
- **With switches free the twins do clear at their tops, and it does not
  pay.** Signing: 4,300 executed writes against 800, +5.3 points on the
  renamed model over the shipped twins, 0.7% of wasted coverage bought back.
  The extra switches sit inside the field-arithmetic twins, entered
  thousands of times per signature, each with a few public instructions at
  its top. On the renamed model a clear takes effect at commit, so a twin's
  entry clear followed a few instructions later by its enable leaves the
  preamble in the enable's shadow anyway and only the writes are paid. On
  the serialising model the same writes cost their drains.
- **Where the renamed cost of the solution actually is.** With the twins the
  switch count is already 41 per two signatures; what remains on the renamed
  model is dwell over the secret work, which any placement that protects it
  pays, and layout, which the NOP twins put at a few points either way. The
  twins' own over-coverage was 5.6% of wasted operations, and section 3 says
  0.7% of it is reachable by narrowing at all.

The knob stays, default off, for a code base with real public stretches
inside seeded functions; the precision report's per-function first-enable
position says whether one exists.

## 6. Reproduce

Libraries: `build_libsodium_native.sh` with `EXTRA_CFLAGS="-mllvm
-taint-dit-contract=callee -mllvm -taint-owned-symbols=<list> -mllvm
-taint-dit-twin-narrow [-mllvm -taint-dit-twin-switch-cyc=0]"`; oracle
`sodium_oracle.sh`; the crypto matrix as `run_clone_timing.sh`; experiment 02
via `build_gem5_linux.sh` with `TAINT_EXTRA`, experiment 09 via
`build_arms.sh` with `TAINT_EXTRA` (both hooks added for this).
