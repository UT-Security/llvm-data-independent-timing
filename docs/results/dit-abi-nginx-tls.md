# The ABI on a deployed server: nginx TLS 1.3 handshakes

**Measured 2026-08-31, Apple M5.** The first measurement of this pass on a deployed
server application, and the first non-LTO case where the ABI both pays and is
shippable.

**Read `dit-openssl-asm-limit.md` first.** The scope here is narrow by necessity:
OpenSSL's bulk cipher and bignum/EC arithmetic are hand-written aarch64 assembly
and cannot be instrumented at all. What the pass reaches is the C TLS 1.3 key
schedule. This measures **handshake key handling**, not "hardened TLS".

## Result

| comparison | server CPU | sign test |
|---|---|---|
| hardening cost (`base` vs unhardened) | **+0.65%** | 14/15 slower |
| hardening cost **with the ABI** | **+0.21%** | 11/15 slower |
| **what the ABI recovers** | **-0.41%** | **13/15 faster** |

**The ABI removes about two thirds of the cost of hardening** (0.65 -> 0.21), and
what remains is at the edge of measurability.

Switch counts, `libssl.a`:

| arm | msr DIT | carriers | guarded exits |
|---|---|---|---|
| plain | 0 | 0 | 0 |
| base | 48 | 0 | 0 |
| abi | **15** | 6 | 10 |

All 48 are in `tls13_enc.o` and nowhere else; `libcrypto.a` has **zero** in every
arm, which is the assembly limit showing up as a measurement. The ABI arm reports
**zero** non-local exits, so it has not silently degenerated to blanket.

## Method, and two traps it walked into

- **nginx is byte-identical across arms.** Same compiler, same flags; only the
  linked OpenSSL differs. Any difference is attributable to the library.
- TLS 1.3 only, session cache and tickets **off**, so every connection is a full
  handshake and the key schedule runs at the maximum rate the protocol allows.
- **Server CPU, not throughput.** The client verifies the certificate and burns
  **7x** the server's CPU (2211 ms against 278 ms per 600 handshakes). Wall-clock
  throughput would have buried a server-side effect entirely.
- **`ps -o time=` is too coarse.** At 10 ms granularity, 280 ms is 28 ticks; the
  first attempt returned a per-rep spread of exactly one tick and a median
  difference of 0.00%. `getrusage(RUSAGE_CHILDREN)` gives microseconds. The client's
  own CPU is measured separately and subtracted.
- 600 handshakes x 15 reps, arm order rotating every rep.

## What it does and does not show

It **does** show that on a real server, hardening the reachable secret-handling
code costs under 1% of server CPU, and that the ABI takes two thirds of that away.
For a deployment already paying for TLS, that is close to free.

It **does not** show that TLS is hardened. The bulk record path is assembly and
runs with DIT off. A complete answer for TLS needs either a crypto library written
in C (libsodium's primitives are, which is why that workload works) or a pass that
operates below the IR level.

It also does not settle whether the effect scales: +0.65% is small because the
reachable slice is small, so the ABI's two-thirds recovery is two thirds of a small
number. The libsodium sweep, where the whole primitive is reachable, shows the same
mechanism worth **5.3 points** at high secret fraction.
