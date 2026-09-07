# 13 - MIT Kerberos KDC: blanket vs the developer's bracket

**Status: complete, silicon, NEGATIVE (a host screen that killed the candidate).**
Measured 2026-09-06 on Apple M4 (Mac16,10), macOS 15.7.3, no root: CPU time of the
`krb5kdc` process per run, medians of 15 runs per arm, arms rotating. Compiler
`2a379e65d04f` (dit-tainter). `data/provenance.txt` has the record.

**Rig:** `../../utils/dit_host_screening/krb5kdc/`. **Rerun:** `./reproduce.sh` on any
Apple Silicon Mac with a taint clang build (section "How to rerun").

---

## The claim

> A real network authentication server whose request handler has the textbook
> decrypt-then-parse shape (the ticket-granting key decrypts the ticket, the session
> key parsed out of the plaintext decrypts the authenticator and encrypts the reply)
> pays **nothing measurable for blanket DIT** (-0.8% on ticket-granting requests, +0.7%
> on a 1:1 mix with initial authentication, noise floor 0.5-0.7%), and **nothing
> measurable for the developer's bracket** on its crypto API (+0.5 / +0.1 points over
> the NOP twin) even though that bracket executes **101 times per ticket request**.
> There is no headroom, so no placement can win here; the shape the paper's argument
> needs is real, the prize is not.

This is the first candidate from the 2026-09-06 search for a workload where the
developer's bracket is bad (`../candidates/bracket-vs-fine-grained-2026-09-06.md`).
The memo listed three things that could kill it; the third did: the KDC's public
work, DB2 lookups and a table-driven ASN.1 codec over a few kilobytes per request, is
not DIT-sensitive on the M4. The two arms measured are the two a developer has
today: set the bit for the process, or bracket the crypto functions by hand.

## What the benchmark is

MIT Kerberos 5 release 1.22.2 (kerberos.org tarball), the reference KDC. Built with
the taint clang at plain `-O2`, `--with-crypto-impl=builtin` so every primitive is
krb5's own C (`lib/crypto/builtin`: AES, SHA-1/2, HMAC, PBKDF2, the CMAC/CTS modes;
the only assembly in that tree is x86), `--with-tls-impl=no`, PKINIT off, the DB2
database backend, single-threaded (the default). A throwaway realm `DIT.TEST` on
`127.0.0.1:8888`, TCP forced by `udp_preference_limit = 1`, 22 principals, the KDC
logging every request to a file as a real one does.

Two load generators, both from krb5's own tree or its API:

| row | load | KDC work per request |
|---|---|---|
| TGS-REQ only | `tgs_loop` (rig): one initial authentication into a memory cache, then 10,000 `krb5_get_credentials` calls with `KRB5_GC_NO_STORE`, so every one is a ticket-granting request to the KDC | `process_tgs_req`: decode the request, look up the server and the TGS keys in the database, decrypt the ticket with the TGS key, parse it, decrypt the authenticator with the ticket's session key, verify the body checksum, mint a session key, encrypt the service ticket and the reply, log |
| AS-REQ + TGS-REQ 1:1 | `tests/hammer/kdc5_hammer`, krb5's own load test: 20 principals x 60 rounds, each round an initial authentication (encrypted-timestamp preauth) and one service ticket per principal | the above plus `process_as_req`: decrypt the preauth timestamp with the client's long-term key, mint a TGT and reply |
| idle | start the KDC and stop it, no requests | the fixed cost inside every row: about 10 ms |

## What was changed, and what was not

**krb5's KDC, libraries, database and load tools: nothing.** The KDC binary is the
same file in every arm.

**libk5crypto: one source change, present in every arm, inert outside B and Bn.**
`patch_bracket.py` rewrites 15 files under `lib/crypto/krb/`: each of the 29 public
entry points that computes on key material (`krb5_c_encrypt`, `krb5_c_decrypt`, the
`_iov` forms, the `krb5_k_` key-handle forms, make/verify checksum, `krb5_c_prf`,
`prfplus`, `derive_prfplus`, `fx_cf2_simple`, string-to-key, random-to-key,
make-random-key, the four PRNG entry points) has its body renamed to a static
function and a wrapper with the original name and signature appended: enter, body,
leave (`data/bracket.diff`). Pure queries (lengths, validity, enctype comparison) and
key-handle bookkeeping are left alone. That is the set an annotator following Apple's
guidance would mark as "the crypto functions".

The bracket is the **save/restore form**, not a plain clear, because these entry
points nest: `krb5_c_decrypt` calls `krb5_k_decrypt`, and a plain `msr DIT,#0` at the
inner exit would strip the outer function's protection for the rest of its body.

```
entry:  mrs xN, DIT ; msr DIT, #1 ; sb
exit:   msr DIT, xN
```

The NOP twin replaces each of those four instructions with `hint #0`, with the same
register constraint, so the two libraries have identical symbol addresses and
instruction counts (verified in `data/provenance.txt`). The compiler inlined three
wrappers into callers, so the library carries 32 bracket sites for 29 entry points.

## The arms

| arm | what runs | how it is selected |
|---|---|---|
| A | unhardened | base `libk5crypto` |
| C | blanket: the injected constructor (`utils/cio_ditctl.c`) sets DIT before `main`, never cleared | base library, `ENABLE_DIT=1` |
| B | the developer's bracket on the 29 entry points | `DYLD_LIBRARY_PATH` -> the bracket `libk5crypto` |
| Bn | B's NOP twin | `DYLD_LIBRARY_PATH` -> the twin |

Every KDC process runs with the constructor injected; it raises the scheduling class
for P-core residency and reads `PSTATE.DIT` back at exit. C must exit with the bit set
and every other arm clear, on every run, or the row is not a result. Every row passed.
The clients (`tgs_loop`, `kdc5_hammer`) always run the base library; the arm is the
server's.

## Method

Per run: start `krb5kdc -n` under one arm, wait until it accepts a TCP connection,
run the load, `SIGTERM` it (krb5kdc exits through `exit(0)`, so the readback
destructor runs), and take its CPU time from `wait4`'s rusage. Arms rotate on every
run; 2 warm-up runs per arm are discarded, 15 are kept. The metric is the server's CPU
time, which excludes the clients and the kernel's network path on their side; the
clients' wall time is recorded as a sanity check and tracks it.

## Results

`data/run.txt`. Percent over A; MAD is the median absolute deviation of A's runs as a
percent of its median, the noise floor.

| row | A, KDC CPU | C blanket | B bracket | Bn twin | B - Bn | MAD |
|---|---|---|---|---|---|---|
| idle (start + stop) | 10.1 ms | -0.9% | +0.6% | +1.1% | -0.4 | 4.7% |
| TGS-REQ only, 10,000 requests | 1,464 ms | **-0.8%** | +0.4% | -0.1% | **+0.5** | 0.7% |
| AS-REQ + TGS-REQ 1:1, 1,200 + 1,200 | 301 ms | **+0.7%** | +0.6% | +0.4% | **+0.1** | 0.5% |

Per request, from the TGS row after subtracting the idle cost: **145 us of KDC CPU
per ticket-granting request**. The bracket's cost, +0.5 points, is about 0.7 us per
request.

**How often the bracket executes.** A counting build of the same library
(`-DDIT_BRACKET_COUNT=1`, entries tallied by entry point and printed at exit) gives
the executed bracket entries per request (`data/entries.txt`):

| entry point | per TGS-REQ | per AS-REQ |
|---|---|---|
| `krb5_c_random_to_key` | 34 | 13 |
| `krb5_c_prf` + `krb5_k_prf` | 8 + 8 | 0 |
| `krb5_c_decrypt` + `krb5_k_decrypt` | 6 + 6 | 3 + 3 |
| `krb5_c_random_make_octets` | 6 | 4 |
| make checksum (`c`/`k`, plain and `_iov`) | 13 | 6 |
| `krb5_c_encrypt` + `krb5_k_encrypt` | 3 + 3 | 2 + 2 |
| verify checksum (`c` + `k`) | 3 + 3 | 0 |
| `krb5_c_prfplus`, `krb5_c_fx_cf2_simple`, `krb5_c_make_random_key` | 4, 2, 2 | 0, 0, 1 |
| **total** | **101** | **34** |

Six decryptions, three encryptions, seven checksums and eight PRF calls per
ticket-granting request is the protocol; the count is three times that because the
API is layered and every layer is a public entry point. `krb5_c_decrypt` creates a
key handle and calls `krb5_k_decrypt`; the DK enctypes then derive a usage-specific
key for the cipher and another for the integrity check, and derivation ends in the
public `krb5_c_random_to_key` (`derive.c:127`). Derived keys are cached on the
`krb5_key` handle, but the keyblock-based `krb5_c_` API the KDC uses makes a fresh
handle per call, so every operation re-derives: 34 `random_to_key` entries per
request is that re-derivation. A developer bracketing "the crypto functions"
therefore pays three or four nested entries per operation without knowing it.

At 0.7 us for 101 entries that is about 7 ns, 30 cycles, per entry: one serialising
`msr` pair and an `sb` on code with little in flight to drain. The switches are not
free, they are just few relative to 145 us of request.

## What it says

- **Blanket DIT is free on this server.** -0.8% and +0.7% are inside a 0.5-0.7%
  noise floor. The KDC's public work is database lookups over a 22-principal DB2 file
  and ASN.1 encode/decode of a few hundred bytes; neither has the pointer-chasing or
  interpreter character that makes DIT cost 2-24% on PHP, Bitcoin coin selection or
  a hashed chase. So there is nothing for any selective placement to recover.
- **The developer's bracket is also free here**, at 101 entries per request. That
  is the phpass lesson in reverse: cost is entries times the switch price, and
  101 x 30 cycles is 0.5% of a 145 us request, where phpass's 65,537 x 200 cycles was
  8% of a 39 ms one.
- **The shape the paper wants is present but worthless here.** The leaf bracket is
  unsound in the strict sense: the session key parsed out of the decrypted ticket is
  handled by ASN.1 code that runs DIT-off in arm B. The sound bracket would enclose
  `process_tgs_req`. But since blanket costs nothing, the wide bracket would cost
  nothing either, and the fine-grained pass could save nothing. The candidate dies
  on the first of the evaluation's four questions, not on the fourth.
- **For the candidates memo:** a decrypt-then-parse handler is only a candidate if
  its independent public work is DIT-expensive. Screen blanket first, always.

## How to rerun

```
paper_experiments/13-kerberos-kdc/reproduce.sh          # clang build realm run collect
```

Stages: `clang` (uses `LLVM_BUILD`, default `build/`, or builds one), `build`
(downloads krb5 1.22.2, builds it with the bracket wrapped inert, rebuilds
`libk5crypto` for B, Bn and the counting diagnostic; about 5 minutes), `realm`
(creates `DIT.TEST`, the principals, compiles `tgs_loop`), `run` (the three rows,
about 15 minutes on an idle machine), `collect`. No root; the KDC binds
`127.0.0.1:8888` only. Env: `W`, `LLVM_BUILD`, `REPS`, `WARM`, `TGS_N`, `HAMMER_N`,
`HAMMER_R`.

## Limits

- One host (M4). Each `msr DIT` is serialising on Apple silicon; a renamed
  implementation would make the bracket cheaper still.
- A 22-principal database fits in cache; a KDC with a large realm does more B-tree
  work per lookup. The direction of that effect is unknown; DB2 descent over a large
  file would be the thing to screen.
- Metric is the KDC process's CPU time from `wait4`, not cycles or instructions. The
  M4 exposes its PMCs to user mode (`utils/cio_pmc_check.c`; experiment 09's manual),
  but those are per-core registers read inside the measured process, so per-request
  cycles and IPC for a server need region markers in its dispatch loop linked with
  `utils/cio_arm_shim.h` (`-DCIO_SHIM_PMC`). That is the upgrade if this row is ever
  re-run; at a 0.5-0.7% noise floor on a 145 us request it would not change the verdict.
- The pass was not run: with no headroom the comparison has no content. The rig
  supports it (the crypto is C, the owned-symbols list is `libk5crypto`'s), and the
  function-pointer dispatch in `krb5_keytypes[]` / the enc providers would need a
  seed per primitive, as in libtomcrypt.

## Files

- `data/run.txt`, `data/kdc_*.json` - the driver's output and per-run samples.
- `data/entries.txt` - executed bracket entries per entry point per request type,
  solved from a 1,000-TGS run and a 100+100 hammer run after the idle run's 9.
- `data/bracket.diff` - the only source change (15 files, 29 wrappers, the header).
- `data/provenance.txt` - host, compiler, tarball hash, switch counts, twin check.
