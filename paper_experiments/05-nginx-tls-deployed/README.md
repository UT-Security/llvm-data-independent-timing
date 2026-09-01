# 05 - nginx TLS 1.3, deployed

**Status: complete on silicon.** Measured 2026-08-31 on Apple M5, nginx 1.28.0
against OpenSSL 3.5.4.

---

## The claim

> Experiments 01-04 run harnesses. This one runs a **deployed server**, and its
> result is as much about reach as about cost:
>
> On real TLS 1.3 traffic, hardening the code the pass **can** reach costs
> **+0.65% of server CPU**. But the pass reaches only the C key schedule -
> **every primitive TLS actually spends time in is hand-written aarch64
> assembly**, and `libcrypto.a` carries **zero** switches in every arm.
>
> So: cheap, and not "hardened TLS".

## Why this experiment exists

Every other experiment in this folder chooses a workload the pass can instrument.
That is the right way to measure a cost model and the wrong way to estimate what
deployment looks like. Here the workload was chosen first - the most widely
deployed TLS server there is - and the reach limit is the finding rather than a
caveat.

## Result

| comparison | server CPU | sign test |
|---|---|---|
| hardening cost | **+0.65%** | 14/15 slower |
| with the callee-saved ABI | **+0.21%** | 11/15 slower |
| what the ABI recovers | **-0.41%** | 13/15 faster |

Under 1% of server CPU, and about two thirds of it removable. For a deployment
already paying for TLS that is close to free - but read the next section before
taking it as a security result.

## Where the switches land

| arm | `libssl.a` | carriers | guarded exits | `libcrypto.a` |
|---|---|---|---|---|
| plain | 0 | 0 | 0 | **0** |
| base | 48 | 0 | 0 | **0** |
| abi | 15 | 6 | 10 | **0** |

All 48 are in `tls13_enc.o` and nowhere else. **`libcrypto.a` has zero in every
arm** - the assembly limit appearing as a measurement rather than an argument.

OpenSSL 3.5.4 ships **19 aarch64 perlasm generators** covering AES, AES-GCM,
ChaCha20-Poly1305, P-256, bignum and SHA. That is the bulk record cipher, both
TLS 1.3 AEADs, the key exchange, the signature arithmetic and the transcript
hash. Building from source does not help, because the assembly *is* the source,
and `no-asm` is a strawman: the C AES is the T-table version whose real leak is
cache timing, which DIT does not cover at all.

**This is the third and worst of the pass's reach limits** (`CLAUDE.md`):
cross-TU is fixable with a seed, prebuilt libraries are fixable by building from
source, and hand-written assembly is fixable by neither.

## What it does and does not show

**Does:** on a real server, hardening the reachable secret-handling code costs
under 1% of server CPU.

**Does not:** that TLS is hardened. The bulk record path runs with DIT off. A
complete answer needs a crypto library written in C - libsodium's primitives are,
which is exactly why experiments 02 and 03 work - or a pass operating below the
IR level.

It also does not settle whether the effect scales. +0.65% is small because the
*reachable slice* is small, so the ABI's two-thirds recovery is two thirds of a
small number. On libsodium, where the whole primitive is reachable, the same
mechanism is worth **5.3 points**.

## Method, and two traps it walked into

- **nginx is byte-identical across arms.** Same compiler, same flags; only the
  linked OpenSSL differs, so any difference is attributable to the library.
- TLS 1.3 only, session cache and tickets **off**, so every connection is a full
  handshake and the key schedule runs at the maximum rate the protocol allows.
- **Server CPU, not throughput.** The client verifies the certificate and burns
  **7x** the server's CPU (2211 ms against 278 ms per 600 handshakes). Wall-clock
  throughput would have buried a server-side effect entirely.
- **`ps -o time=` is too coarse.** At 10 ms granularity 280 ms is 28 ticks; the
  first attempt returned a per-rep spread of exactly one tick and a median
  difference of 0.00%. That was not "no effect", it was no resolution.
  `getrusage(RUSAGE_CHILDREN)` gives microseconds, and the client's own CPU is
  measured separately and subtracted.
- The ABI arm reports **zero** non-local exits, so it has not silently
  degenerated to blanket - the failure mode experiment 02 had to be rescued from.

## Contents

| path | what |
|---|---|
| `data/nginx_cost.csv` | server-CPU cost, three comparisons, with sign tests |
| `data/nginx_reach.csv` | where the switches land, and where they do not |
| `data/openssl_asm_limit.csv` | the 19 perlasm generators, and what each one is |
