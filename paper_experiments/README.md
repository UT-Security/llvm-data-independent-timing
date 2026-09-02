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
| 01 | [Bitcoin Core wallet](01-bitcoin-core-wallet/) | coin selection, 4 solvers | `CKey::Sign` per input | inputs per tx | **complete, both instruments** |
| 02 | [libsodium signed lookup](02-libsodium-signed-lookup/) | table lookups, value-dependent chain | `crypto_sign_ed25519` per request | lookups per signature | **complete, both instruments** |
| 03 | [mbedTLS record MAC](03-mbedtls-record-mac/) | per-record bookkeeping, in the SAME function | `mbedtls_md_hmac` per record | **bytes per record (region SIZE)** | **complete, silicon** |
| 04 | [libsecp256k1 soundness](04-libsecp256k1-soundness/) | - | ECDSA signing, key seeded | **none - measures PROTECTION, not cost** | **complete, gem5** |
| 05 | [nginx TLS 1.3](05-nginx-tls-deployed/) | request handling, cert verify | TLS 1.3 key schedule | **none - a DEPLOYED server, measures REACH** | **complete, silicon** |
| 06 | [switch-model generality](06-switch-model-generality/) | - | experiments 02 and 03's workloads | **MSR DIT implementation - measures TRANSFERABILITY** | **complete, gem5** |
| 07 | [annotation cost](07-annotation-cost/) | - | libsodium signing path | **seed DEPTH - measures DEVELOPER cost** | **complete, silicon** |
| 08 | [seed ground truth](08-seed-ground-truth/) | - | libhydrogen signing, their exact source | **none - compares our taint set against an INDEPENDENT one** | **complete** |
| 09 | [libsodium, CIO parity](09-libsodium-cio-parity/) | **none - the whole program is crypto** | CIO's own 6 benchmarks, their seeds | **none - the NEGATIVE CONTROL: measures where placement does NOT belong** | **complete, silicon x2 (M5 + M4); percentages corrected 4-15x, conclusions unchanged** |

## Published pages

One artifact per experiment. Republish through the recorded URL (`Artifact` with
`url=...`); publishing the source file without it creates a second artifact.

| # | page | url |
|---|---|---|
| 01 | The Secret-Fraction Crossover | https://claude.ai/code/artifact/692f3b7d-18fe-4707-ab8a-3d5b84478c12 |
| 02 | Signed-Lookup Crossover | https://claude.ai/code/artifact/52f2f6b1-8324-4907-a6d0-a3548558a895 |
| 03 | Fine Grain Crossover | https://claude.ai/code/artifact/a7949ca8-dba2-48c8-b583-9fdad41d8f8f |
| 04 | Soundness Ledger | https://claude.ai/code/artifact/f8e0b663-444d-450b-ad3c-1b31cffe44f0 |
| 05 | The Reach Limit | https://claude.ai/code/artifact/dc90173e-f6f9-431e-8b11-e34321eb2dd7 |
| 06 | Switch Model Transfer | https://claude.ai/code/artifact/b6c89530-d27f-455c-a7bc-e93b3ab7c952 |
| 07 | The Annotation Loop | https://claude.ai/code/artifact/ac6058f5-25ba-4a38-bf2e-6a385652ffb3 |
| 08 | Two Analyses, One Library | https://claude.ai/code/artifact/2a789196-2274-42fc-9922-b624f0808762 |
| 09 | Nothing to Recover | https://claude.ai/code/artifact/842a1394-e976-4587-861c-076657829a48 |

### Candidates, from `../docs/paper/evaluation-framework.md` §6

| workload | public lane | secret lane | knob | state |
|---|---|---|---|---|
| SQLCipher cache sweep | SQLite B-tree descent | AES-256-CBC + HMAC per page | `PRAGMA cache_size` | in progress |
| SQLite + ECDSA | SQLite queries | libsecp256k1 sign | signatures per batch | one gem5 point at f = 2.23%; curve missing |
| CPython + coincurve | interpreter + Django | libsecp256k1 via coincurve | signatures per request | both endpoints measured; middle missing |

Skia filters are **not** a crossover experiment - a control that fails the
framework's first question (blanket DIT is already free on it), kept in
`docs/results/`.

**libsodium is BOTH**, and it is now two experiments rather than a footnote: 02 is
the flow with a public lane, where the pass beats blanket by 21%; 09 is the
library alone, run the way the closest prior work (CIO, ASPLOS'24) ran it, where
blanket wins on 5 of 6. Same library, opposite verdicts - which is the point. Its own primitives fail the first question - blanket is free on aead,
x25519, sha512, salsa20 and hmac_sha512, and on ed25519 it is a *speedup*
(-2.96%), so no placement can win. But libsodium *inside a flow with a public
lane* is experiment 02, where blanket reaches +32.64% and the pass beats it by
21%. The library is not the workload; the flow is.

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
