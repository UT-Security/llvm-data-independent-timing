# Workload candidates for the fine-grained DIT win

> **STEP 1 IS DONE. See `docs/results/dit-host-screening.md` (2026-08-14).**
> Five hosts screened on M5. Ranked prize: **lua +14.52%, cpython +7.00%,
> sqlite +6.09%, git +2.48%, quickjs +1.08%**. The rig reproduces the QuickJS
> number (+1.05% documented). **Four hosts beat the workload the only positive
> result was built on.** The predicted column in §2 below is superseded by that
> table; the reasoning is kept because it is what the screen tested.

**Written 2026-08-14.** Successor to the candidate table in
`browser-history-leaks.md` §5. That table scored the workloads we had; this one
proposes the ones we do not have yet, and - more importantly - proposes a
**cheap screening test** so we stop paying instrumentation cost to discover a
host has no prize in it.

---

## Bottom line (read this first)

1. **A candidate is a (host, payload) pair, and the two halves are
   independent.** Condition (d) - public code with headroom - is decided
   entirely by the host. Conditions (a) and (c) are decided entirely by the
   payload. We have been searching for single applications that happen to
   satisfy all four; we should instead pick a host for (d) and graft a payload
   chosen for (a)/(c).

2. **Screen hosts by always-on DIT cost before instrumenting anything.** That
   one number IS the prize, it needs **zero** taint work - just the blanket-DIT
   constructor we already have - and it directly tests (d). It is exactly what
   we did for Firefox/Chromium (+2.61% / +1.80%) before touching the pass. A
   batch of 6 hosts is an afternoon. Anything that screens under ~1% is dead on
   arrival: the noise floors are 0.1-0.6% and the MIR round-trip codegen lottery
   is 0.06-2.65% (`dit-measurement-traps` trap 7b), so a sub-1% prize cannot be
   measured credibly.

3. **The payload should be chosen for taint cleanliness, not speed.** Per
   `dit-granularity-crossover`, always-on and fine-grained both run DIT over the
   secret work identically, so that term cancels out of the comparison. The
   payload only has to be *contained* (no taint fan-out), *chunky* (>~3000 cyc
   per region), and *rarely entered*. Its own DIT cost is irrelevant to the win,
   which is why the safegcd +23.3% result does not disqualify signing - see
   `dit_inv_bench_README.md`.

4. **The ceiling does not move.** `dit-prize-is-one-to-two-percent` caps the
   whole prize at 1-2% on real code. No amount of workload hunting changes that.
   The realistic outcome of this search is a *second* host that scores like the
   browser, which makes the QuickJS result generalise rather than making it
   bigger.

---

## 1. Why the search kept failing, restated as constraints

Four hard structural constraints, each learned by burning a workload:

| Constraint | Learned from | Rules out |
|---|---|---|
| **Host must be AOT-compiled.** The pass runs at MIR level; it cannot instrument JIT-generated code. | QuickJS worked *because* it is a pure interpreter | Node/V8, Deno, LuaJIT, Ruby+YJIT, JVM, any browser JS tier above the interpreter |
| **Crypto must be built from source.** | SQLCipher on the shipping OpenSSL build: prebuilt `libcrypto.dylib`, 25 `MSR DIT`, **zero** on any cipher instruction | anything linking system OpenSSL / CommonCrypto |
| **Region boundary must be hoistable.** | SQLCipher's toggles sit at a callee reached through a **function pointer**; hoisting is intraprocedural, so it cannot fix them | provider/vtable-dispatched crypto |
| **Secret work must be chunky.** ~3000+ cyc per region; crossover ~1300 cyc | `dit-granularity-crossover`; SQLCipher was 300-500 cyc x 256 regions/page | per-block/per-page crypto, streaming ciphers |

And one soft one: **the secret must not fan out**, or context-insensitive
mod-sets flood the taint (169 of 199 FPs, `docs/design/context-insensitivity.md`).

---

## 2. Host candidates (they decide condition (d))

Ranked by expected always-on DIT cost. **All of these are predictions except
where a measured number is cited.** The point of §4 is to replace this column
with data.

### Tier 1 - interpreters

Interpreters are the most DIT-sensitive code we have measured: QuickJS's timed
sections cost **7.4%**, and the whole-process figure was 1.05%. Serial dispatch
with a load-to-address dependency on every opcode is exactly the shape
`dit-headroom-needs-serial-chains` says is required.

| Host | Benchmark | Why (d) should hold | Feasibility |
|---|---|---|---|
| **CPython** | `pyperformance` (40 benchmarks, standard) | `ceval` dispatch is documented as "a massive amount of pointer chasing": deref operand → `ob_type` → function-pointer tables, per bytecode. Strictly more indirection than QuickJS. | **Best new candidate.** AOT by default (3.13+ JIT is opt-in via `--enable-experimental-jit` - confirm it is OFF). Builds on macOS arm64. |
| **PHP / Zend VM** | `Zend/bench.php`, or a real app | Same dispatch shape; and PHP web apps sign session cookies natively, so the payload is *already there* rather than grafted | medium; large build |
| **Lua 5.4** | standard Lua benchmarks | clean, tiny, easy | small effect likely; low prestige |

### Tier 2 - tree / index descent

| Host | Benchmark | Why (d) might hold | Caveat |
|---|---|---|---|
| **SQLite** (plain, no cipher) | `speedtest1` (SQLite's own) | VDBE is a bytecode interpreter **and** B-tree descent is pointer chasing | ⚠️ **A figure needs re-establishing first.** An older session recorded "unencrypted SQLite still pays +4.4% always-on", but that number is **not in `docs/results/sqlcipher.md`** and I could not find it anywhere in `docs/`. The current doc measures an *encrypted* workload (always-on +8.89%, 100 reps) and reports recoverable headroom of +0.89% (libtomcrypt) / **zero** (OpenSSL). If the +4.4% is real, a plain-SQLite host has 5x the prize the SQLCipher ROI showed and something in the oracle analysis needs revisiting; if it is not, SQLite is dead. **Re-measure it in step 1 - it is one arm on a rig we already have.** |
| **Redis** | `redis-benchmark`, large keyspace | hash tables + skiplists, in-memory | individual ops may be too short to matter; server/client split complicates the rig |
| **LevelDB / RocksDB** | `db_bench` | LSM + skiplist descent | no natural secret |

### Tier 3 - real applications with genuine crypto

| Host | Why it is attractive | Why it might fail |
|---|---|---|
| **Bitcoin Core** | **Already vendors libsecp256k1 as source** - the single biggest feasibility win available. Public work = `EvalScript` (a Forth-like bytecode interpreter) + UTXO cache (hash map) + LevelDB. Secret = wallet signing, rare and chunky. Declassification is protocol-defined. "A node validating blocks while the wallet signs" is a genuinely real deployment, and GoFetch targeted exactly this key class. | ⚠️ **(d) is at risk.** A significant fraction of validation time is *signature verification*, which is crypto, and `libsodium`'s 13 primitives measured **+0.1%** - i.e. crypto is DIT-insensitive. If verify dominates, the public region has no headroom and Bitcoin Core fails (d) the same way SQLCipher did. Note the cuckoo signature cache cuts verify work substantially, which helps. **Screen before committing.** |
| **Git** (signed commits) | object-graph traversal, delta chains, pack-index binary search | I/O and zlib heavy; signing shells out to gpg/ssh = separate process, so no taint path |

---

## 3. Payload candidates (they decide (a) and (c))

| Payload | Region size | Declassification | Taint cleanliness | Verdict |
|---|---|---|---|---|
| **ECDSA / Schnorr sign, libsecp256k1** | ~1.1-1.4 us measured (`dit_inv_bench`) - **3-4x above crossover** | `s = k^-1(h+rd)`; **the signature is published by definition of the protocol** | one seed: `secp256k1_ecdsa_sign`, arg `seckey`. Self-contained C, no vtables, direct calls throughout | **best.** Fixes the QuickJS "constructed workload" objection: the declassification is real cryptographic semantics, not a write-to-sink harness trick |
| **Ed25519 sign, libsodium** | similar | signature published | we have already instrumented libsodium (13 primitives, +0.1%) | good fallback; less novel |
| **HMAC session cookie** | ~500 ns-1 us | MAC published | trivial | ⚠️ near/below the crossover - risky, and it is the most realistic payload for a web host. Measure the region size first |
| **Argon2 / PBKDF2 password verify** | ms - enormous | result is 1 bit | excellent | superb on (c), but so large it threatens (a) if the host is login-dominated |

---

## 4. What to actually run (cheapest first)

**Step 1 - the screening sweep. No taint instrumentation at all.**

For each host, build stock, run its standard benchmark under paired round-robin
with three arms: `base`, `null` (harness present, DIT never set), `dit` (blanket
DIT). Reuse `benchmarks/taint_convolve/dit_blanket.c` for a linkable
constructor, or the `DYLD_INSERT_LIBRARIES` + `pthread_create` interposer from
`utils/taint_browser_dit_bench.sh` for anything multithreaded.

Gates, non-negotiable (`dit-measurement-traps`):
- run the **null arm** - it cost +0.30% on Chromium and 0.00% on Firefox, so one
  engine's null does not license skipping another's;
- run `lvp_chase --mode const` **in the same harness and session** - it must read
  ~4.0x on a P-core or the rig is not measuring DIT;
- any DIT-on ratio below 1.00x is an artifact.

Output: a ranked table of always-on DIT cost. **That table is a publishable
result on its own** - "we screened N real applications for DIT sensitivity, here
is the distribution" is the characterization framing that
`expedite-thesis-status` says survives review, and it does not depend on the
fine-grained claim landing.

**Step 2 - instrument only the winner**, with libsecp256k1 signing as payload.
Predicted toggle count: 2 per signature. Verify with
`objdump_dit.sh` and the per-function `MSR DIT` census that **zero** toggles
land in the host's dispatch loop - that was the make-or-break check on QuickJS.

**Step 3 - resolve the SQLite tension** (§2, Tier 2) in parallel. It is cheap,
uses data and rigs we already have, and could turn a recorded negative into a
positive.

---

## 5. Honest assessment

The realistic best case is a second host that behaves like the browser: always-on
costs ~1-3%, fine-grained recovers most of it, secret fraction is tiny. That
would make the QuickJS result **generalise**, which is what the paper needs -
one constructed workload is an anecdote, two workloads where one is real is a
finding.

It will not produce a large number. Anyone hoping for the 4x from `lvp_chase` is
reading a microbenchmark that overstates the prize ~200x
(`dit-prize-is-one-to-two-percent`).

---

## Sources

- CPython dispatch pointer chasing - https://blog.codingconfessions.com/p/are-function-calls-still-slow-in-python
- pyperformance benchmark list - https://pyperformance.readthedocs.io/benchmarks.html
- Bitcoin Core script validation / `EvalScript`, cuckoo signature cache -
  https://bitcoincore.academy/validating-scripts.html ,
  https://bitcoin.org/en/release/v0.15.0
- Intel DOIT guidance (the x86 analogue; "Intel expects the performance impact of
  this mode may be significantly higher on future processors" - supports the
  forward-looking framing in `docs/overview.md`) -
  https://www.intel.com/content/www/us/en/developer/articles/technical/software-security-guidance/best-practices/data-operand-independent-timing-isa-guidance.html
- FLOP (LVP on M3/M4, DIT disables it) - https://www.usenix.org/system/files/usenixsecurity25-kim-jason.pdf
- GoFetch (DMP, and DIT disables it on M3) - https://gofetch.fail/

See `browser-history-leaks.md` §5 for the scored table of workloads already
tested, and `dit_inv_bench_README.md` for why BEEA/safegcd is a payload and not
a benchmark.
