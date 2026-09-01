# Paper experiments

**Every experiment here is the same experiment on a different workload:** a
secret-fraction crossover. Each one holds the public lane and the secret lane of
a real application inside one call, varies a knob that moves only the ratio
between them, and asks where selective `PSTATE.DIT` placement stops beating
blanket DIT.

So the directory name is the **workload**, never the phenomenon - the phenomenon
is the constant. Numbered prefixes follow the order the experiments appear in
the paper, not the order they were run.

| # | workload | public lane | secret lane | knob | status |
|---|---|---|---|---|---|
| 01 | [Bitcoin Core wallet](01-bitcoin-core-wallet/) | coin selection, 4 solvers | `CKey::Sign` per input | inputs per tx | **complete, both instruments** |
| 02 | [libsodium signed lookup](02-libsodium-signed-lookup/) | table lookups, value-dependent chain | `crypto_sign_ed25519` per request | lookups per signature | **complete, both instruments** |

### Candidates, from `../docs/paper/evaluation-framework.md` §6

| workload | public lane | secret lane | knob | state |
|---|---|---|---|---|
| SQLCipher cache sweep | SQLite B-tree descent | AES-256-CBC + HMAC per page | `PRAGMA cache_size` | in progress |
| SQLite + ECDSA | SQLite queries | libsecp256k1 sign | signatures per batch | one gem5 point at f = 2.23%; curve missing |
| CPython + coincurve | interpreter + Django | libsecp256k1 via coincurve | signatures per request | both endpoints measured; middle missing |

Skia filters are **not** a crossover experiment - a control that fails the
framework's first question (blanket DIT is already free on it), kept in
`docs/results/`.

**libsodium is BOTH**, which is worth stating rather than filing under one
heading. Its own primitives fail the first question - blanket is free on aead,
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
