# Paper experiments

**Experiments 01 and 02 are the same experiment on different workloads:** a
secret-fraction crossover. Each one holds the public lane and the secret lane of
a real application inside one call, varies a knob that moves only the ratio
between them, and asks where selective `PSTATE.DIT` placement stops beating
blanket DIT.

**Experiment 03 asks a different question.** It sweeps REGION SIZE with the
secret fraction held roughly constant, and its headline is **region vs
whole-function placement** rather than pass vs blanket - a comparison 01 and 02
structurally could not make, because in both the public and secret work live in
different functions and `-taint-dit-placement=function` would have produced
identical coverage. 03 puts both in one function so the two policies diverge.

So the directory name is the **workload**, never the phenomenon. Numbered
prefixes follow the order the experiments appear in the paper, not the order they
were run.

| # | workload | public lane | secret lane | knob | status |
|---|---|---|---|---|---|
| 01 | [Bitcoin Core wallet](01-bitcoin-core-wallet/) | coin selection, 4 solvers | `CKey::Sign` per input | inputs per tx | **complete, both instruments; gem5 half re-taken on the current compiler 2026-09-03 and the flow added on gem5 (the pass wins through f = 54% under either switch; beyond that, inside the code-placement floor), silicon re-take pending on the M5 (`reproduce.sh`)** |
| 02 | [libsodium signed lookup](02-libsodium-signed-lookup/) | hashed pointer chase, 75% of critical-path loads LVP-predictable | chacha20-poly1305 AEAD per record (ed25519 until 2026-09-03) | lookups per record; also the LVP-predictable fraction, and the switch implementation | **gem5 complete, re-run 2026-09-05 on the compiler's new defaults (callee contract + twins: 49 -> 38 switches per request, serialising cost -25 to -35%, crossover moved up); silicon pending** |
| 03 | [mbedTLS record MAC](03-mbedtls-record-mac/) | per-record bookkeeping, in the SAME function | `mbedtls_md_hmac` per record | **bytes per record (region SIZE)** | **complete, silicon** |
| 04 | [libsecp256k1 soundness](04-libsecp256k1-soundness/) | - | ECDSA signing, key seeded | **none - measures PROTECTION, not cost** | **complete, gem5** |
| 05 | [nginx TLS 1.3](05-nginx-tls-deployed/) | request handling, cert verify | TLS 1.3 key schedule | **none - a DEPLOYED server, measures REACH** | **complete, silicon** |
| 06 | [switch-model generality](06-switch-model-generality/) | - | experiments 02 and 03's workloads | **MSR DIT implementation - measures TRANSFERABILITY** | **complete, gem5** |
| 07 | [annotation cost](07-annotation-cost/) | - | libsodium signing path | **seed DEPTH - measures DEVELOPER cost** | **complete, silicon** |
| 08 | [seed ground truth](08-seed-ground-truth/) | - | libhydrogen signing, their exact source | **none - compares our taint set against an INDEPENDENT one** | **complete** |
| 09 | [libsodium, CIO parity](09-libsodium-cio-parity/) | **none - the whole program is crypto** | CIO's own 6 benchmarks, their seeds | **none - the NEGATIVE CONTROL: measures where placement does NOT belong** | **complete, silicon x2 (M5 + M4) + gem5 switch model; percentages corrected 4-15x, conclusions unchanged** |
| 10 | [mbedTLS session ticket](10-mbedtls-session-ticket/) | ClientHello and record handling; the application behind the server | AES-GCM ticket decrypt, then the PARSE of the resumption secret in plain C (TLS 1.2 RSA premaster as the literature anchor) | resumption rate, records per connection | **gates G0/G1 passed 2026-09-03; re-run 2026-09-05 on the compiler's new defaults (section 16): serialising +252% -> +40%, coverage 99.955%, renamed +11.3% of which all is instruction fetch on the duplicated code; blanket still wins** |

## Compiler changes and experiment validity

The pass changes under these experiments, so each README records the compiler
that produced its numbers. When a change lands that could move them, the
question is which conclusions move - not whether the last digit does.

**2026-09-02, the phi fix** (`getCellFromMMO` now looks through PHI nodes to the
underlying object; `docs/design/frame-address-gap.md`). It is unflagged and
default-on, and it changes what the analysis sees, so every experiment was in
scope. Checked:

| # | workload | status |
|---|---|---|
| 04 | libsecp256k1 | **verified unaffected** - oracle re-run reproduces exactly: 4,647,778 protected, 40 clear, 2 sites, both in the harness, zero inside the library |
| 02, 07, 09 | libsodium | **verified unaffected** - 134 switches either side, and the whole-library disassembly differs by exactly one `msr DIT, #0` moved four instructions within one epilogue (8 lines of 60,911) |
| 08 | libhydrogen | **AFFECTED, already re-measured** - this is the experiment the fix came out of. Oracle 97.61% -> 80.85% unprotected on the natural seed, and the info-loss report's own repair line went from doing nothing to reaching 0.03% |
| **03** | mbedTLS | **AFFECTED, re-measured 2026-09-03.** 41 -> 49 switches, in exactly the path it measures. **Headline intact** - region still beats function everywhere, -13.6% at 16 KB. **The blanket crossover moved** from 1,024 B to between 1,024 and 4,096 B, the small-region regime being where switch count is the cost |
| **06** | mbedTLS | **AFFECTED, not re-run** - reuses 03's binaries, and its gem5 arms need rebuilding on the current compiler |
| 01 | Bitcoin Core | **gem5 half re-measured 2026-09-03 on the current compiler; silicon half pending** - see below |
| 05 | nginx + OpenSSL | **not settled** - the hardened objects are no longer on disk, so there is nothing to compare against; needs an OpenSSL rebuild |

**2026-09-05, the default flip** (`-taint-dit-contract=callee` and the DIT
twins on by default; `docs/design/dit-cloning.md`,
`docs/reference/harden-runbook.md`). This changes what every `-ftaint-harden`
build does, so every experiment is in scope, and two things follow for any
re-run. First, **the pre-contract seed files protect nothing under the
contract**: a build with `libsodium_secret.txt` under the new defaults
covers zero secret operations on the signing path (measured,
`docs/results/dit-callee-contract-2026-09-04.md` §4); experiments 02, 07 and
09 must use `libsodium_secret_contract.txt` and the owned list, as 02's
rig now does. Second, the pass arm's switch count drops wherever calls are
direct (libsodium signing 10,400 -> 41 executed writes per two signatures)
and not where they go through tables (the AEAD keeps 38 of 49). Status:

| # | workload | status |
|---|---|---|
| **02** | libsodium signed lookup | **re-run 2026-09-05**: serialising -25 to -35% at every point, renamed within 1%, blanket unchanged, oracle frontier unchanged; the crossover against blanket moved from ~50% to between 45% and 81% secret |
| 07, 09 | libsodium | **not re-run**; need the contract seed file, and 09's "blanket wins" verdict is where the twins change the least (everything is secret, so a twin's dwell is blanket's) |
| **10** | mbedTLS session ticket | **re-run 2026-09-05** (section 16): serialising +252% -> +40% (switches -91%), coverage 99.877% -> 99.955%, renamed +6.2% -> +11.3% and the NOP control puts all of it in instruction fetch on the duplicated code; blanket still wins on this workload |
| 03, 06 | mbedTLS | **not re-run**; the 727-seed contract file exists (`tls_resume/`); 10's re-run says what to expect on the same library |
| 04 | libsecp256k1 | **not re-run**; the contract seed file exists (`secp_seed_contract.txt`) |
| 01, 05, 08 | | **not re-run** |

**01 is a different problem from the other two.** Its recorded arm
(`build-hoist`) was built **2026-08-18**, and its `-ftaint-harden` seed lived in
a scratchpad that has since been deleted, so the binary is not reproducible as
built. It therefore predates far more than the phi fix - a comparison against it
would attribute weeks of change to one commit. What 01 needs is a **full
re-measurement on the current compiler**, not a diff. Its secret lane is
libsecp256k1, which experiment 04 verifies is unaffected, and its wallet lib
carries **0** switches, so the expected movement is small - but that is an
argument, not a measurement.

**2026-09-03: the gem5 half is re-measured, the silicon half is scripted,
and the flow now exists on gem5.** `01-bitcoin-core-wallet/reproduce.sh`
rebuilds the taint clang, the three `bench_bitcoin` arms and the gem5 arms
from the committed seed, and runs both instruments. The gem5 stages ran on a
Linux host on the current compiler, every number a median over 5 `argv[0]`
offsets (details in the experiment README): the coin-selection prize
reproduces at +6.6%, signing has no prize, every gate passes. New: the two
lanes in ONE flow under gem5 with K as the knob - **the pass beats blanket
through f = 54% under both switch models** (3.5 points renamed, 2.8
serialising at that f), so the crossover, if the flow has one, lies above 54%
where silicon's is at 45 to 50%; where it lies is a property of the switch
and of what DIT costs the secret lane, not of the lanes. Beyond f = 54% the
margins sit inside a **new rig trap**: gem5's model moves a pass-vs-base
delta on the signing kernel by up to 5 points from link placement alone
(pads of 0 to 8 KB; `argv[0]` offsets do not sample it), so any comparison
against a differently laid-out binary needs a placement sweep, which has not
been run. The silicon stages need the M5 and have not been run; until
they are, Table 1 is still the 2026-08-31 arm. Bitcoin Core is pinned to
`15a7a4ed7` (master, 2026-08-18) for the gem5 arms; the M5 tree's commit was
never recorded and should be, along with the uncommitted
`wallet_create_tx.cpp` knob patch that only exists there.

**The general lesson, worth more than the individual answers.** A switch-count
and disassembly diff against the recorded arm settles this cheaply *when the arm
is still on disk and reproducible*. Two of the four checks failed that
precondition. Keeping the arm, or at minimum the seed file and the compiler
hash, is what makes an experiment auditable later; `utils/dit_host_screening/btc/`
has 01's seed but the build tree's copy pointed at a temp path.

## Published pages

One artifact per experiment. Republish through the recorded URL (`Artifact` with
`url=...`); publishing the source file without it creates a second artifact.

| # | page | url |
|---|---|---|
| 01 | The Secret-Fraction Crossover | https://claude.ai/code/artifact/692f3b7d-18fe-4707-ab8a-3d5b84478c12 |
| 02 | Signed-Lookup Crossover (retired driver; current figures are `02-libsodium-signed-lookup/figures/*.png`) | https://claude.ai/code/artifact/52f2f6b1-8324-4907-a6d0-a3548558a895 |
| 03 | Fine Grain Crossover | https://claude.ai/code/artifact/a7949ca8-dba2-48c8-b583-9fdad41d8f8f |
| 04 | Soundness Ledger | https://claude.ai/code/artifact/f8e0b663-444d-450b-ad3c-1b31cffe44f0 |
| 05 | The Reach Limit | https://claude.ai/code/artifact/dc90173e-f6f9-431e-8b11-e34321eb2dd7 |
| 06 | Switch Model Transfer | https://claude.ai/code/artifact/b6c89530-d27f-455c-a7bc-e93b3ab7c952 |
| 07 | The Annotation Loop | https://claude.ai/code/artifact/ac6058f5-25ba-4a38-bf2e-6a385652ffb3 |
| 08 | Two Analyses, One Library | https://claude.ai/code/artifact/2a789196-2274-42fc-9922-b624f0808762 |
| 09 | Nothing to Recover | https://claude.ai/code/artifact/24709335-fc81-4a36-8eca-0c64fcc6cf8a |
| 09b | The Cost Is the Switch (gem5 switch model) | https://claude.ai/code/artifact/6b5dc30a-1296-4d02-a5e2-b723e6c8ed57 |
| 09c | DIT overhead on libsodium (paper figure, M4 + M5 + ExpeDITe) | `09-libsodium-cio-parity/figures/three-machines-region.png` |
| 10 | The Secret Leaves the Primitive (design page, no data yet) | https://claude.ai/code/artifact/d1ac0e15-a836-41b6-8b13-0d7c434e457b |

### Candidates, from `../docs/paper/evaluation-framework.md` §6

| workload | public lane | secret lane | knob | state |
|---|---|---|---|---|
| SQLCipher cache sweep | SQLite B-tree descent | AES-256-CBC + HMAC per page | `PRAGMA cache_size` | in progress |
| SQLite + ECDSA | SQLite queries | libsecp256k1 sign | signatures per batch | one gem5 point at f = 2.23%; curve missing |
| CPython + coincurve | interpreter + Django | libsecp256k1 via coincurve | signatures per request | both endpoints measured; middle missing |

Skia filters are **not** a crossover experiment - a control that fails the
framework's first question (blanket DIT is already free on it), kept in
`docs/results/`.

The decrypt-then-parse shape (a key-class secret leaving a primitive into
parsing glue) was researched 2026-09-03; the memos are
`../docs/research/decrypt-then-parse-{literature,libraries,applications}.md`
and the resulting design is experiment 10 above.

**libsodium is BOTH**, and it is now two experiments rather than a footnote: 02 is
the flow with a public lane; 09 is the library alone, run the way the closest
prior work (CIO, ASPLOS'24) ran it, where blanket wins on 5 of 6. In 02 (gem5,
rig of 2026-09-03, IPC overhead) blanket costs the public lane up to +31%,
renamed placement is within 1% of unhardened at every secret fraction, and
serialising placement crosses blanket near 50% secret - so which of the
three wins is decided by the
flow's public lane and by the switch implementation, not by the library. Its own primitives fail the first question: blanket costs
**+0.00% to +1.99% across all 13** (`09-libsodium-cio-parity/data/primitives_13_silicon.csv`,
19 measurements, ed25519 sign +0.19%), so there is no headroom and no placement
can win. (**Corrected 2026-09-02.** This sentence previously called ed25519 "a
*speedup* (-2.96%)". Nothing in the tree supports that figure; the measured
value is +0.19%, and the 13-primitive CSV it comes from was unreferenced.) But libsodium *inside a flow with a public
lane* is experiment 02, where blanket reaches +31% on gem5 and renamed placement
is free. The library is not the workload; the flow is. (02's earlier silicon
numbers, +32.64% and -21%, were measured on a lookup chain that collapsed to a
constant load and are retired; see its README.)

## Conventions

- `README.md` - the claim, the headline numbers, how to rerun, the known limits.
- `data/` - the raw CSVs the tables were computed from. Never edited by hand.
- `figures/` - published artifact source. Republish through the recorded URL, or
  a new artifact is created instead of updating the existing one.
- **Rigs do not live here.** The scripts that produce these numbers stay with the
  harness they belong to (`../utils/dit_host_screening/...`), because they
  resolve default data paths relative to their own directory. Each experiment's
  README names the exact command that regenerates it.
- Every experiment reports the same five quantities (`f_secret`, `C_public`,
  `C_secret`, work per region, toggles per unit work) and ships its validity
  gates with its numbers.
