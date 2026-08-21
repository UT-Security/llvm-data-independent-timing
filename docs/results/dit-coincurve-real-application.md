# A real application: web3.py → eth-account → coincurve, built with the pass

**2026-08-17.** Replaces the constructed CPython composite
(`dit-cpython-case-study.md`) with a genuine deployed software stack. Nothing in
the signing path here was written by us.

---

## Bottom line

**The pass builds and instruments coincurve — the secp256k1 binding used by the
Python Ethereum ecosystem — and the result signs real Ethereum transactions
correctly.** One seed line, unchanged from the synthetic experiment.

**But the precision problem is far worse than the construct suggested.** In the
real library the pass emits **594** `MSR DIT`, of which only **38 (6.4%)** are on
the seeded secret's path. The other **93.6% is collateral** — including 88
switches in ElligatorSwift Diffie-Hellman, which has nothing to do with ECDSA.

---

## 1. Which application actually uses this

**Verified locally, from installed package metadata** in the build venv:

```
web3          requires  eth-account>=0.13.6
eth-account   requires  eth-keys>=0.4.0
eth-keys      requires  coincurve>=17.0.0
coincurve     22.0.0    <- built here, with -ftaint-harden
```

`web3.py` is the standard Python client for Ethereum. `eth-account` is its
account/signing library. `eth-keys` is the key-handling layer beneath it, and it
selects its cryptographic backend at import time. On this machine it chose:

```
eth-keys backend: CoinCurveECCBackend
```

So the chain is not hypothetical. **Every Python program that signs an Ethereum
transaction through web3.py runs through this code.**

### End-to-end proof

A real transaction, signed by `eth-account`, through our instrumented library:

```python
from eth_account import Account
acct = Account.from_key(bytes(range(1, 33)))
tx = {"nonce": 0, "gasPrice": 20_000_000_000, "gas": 21000,
      "to": "0x" + "11"*20, "value": 10**18, "chainId": 1}
signed = acct.sign_transaction(tx)
```

```
account address : 0x6370eF2f4Db3611D657b90667De398a2Cc2a370C
signed tx hash  : 23df0b12327fb2505f5599a7303521e8de ...
r,s,v           : 0x579e5e19aede53b4d3 0x71df1cd7dcc0642008 38
eth-keys backend: CoinCurveECCBackend
native ext      : _libsecp256k1.cpython-314-darwin.so   (594 MSR DIT)
```

Direct coincurve API also verified: `sign()`, `verify()`, recoverable signatures
with public-key recovery, and RFC 6979 determinism (same message → same
signature every time). All correct with the pass applied.

**Not verified here, only cited:** coincurve's own documentation describes it as
"the cryptographic backbone of many projects, including the entire Ethereum
Python community", and it is also used in Bitcoin tooling. Treat the Ethereum
chain above as established and the broader claim as the project's own marketing
until checked.

---

## 2. Why coincurve is instrumentable when OpenSSL is not

Three properties, all confirmed by reading its build:

- **It vendors libsecp256k1 from source.** `cm_vendored_library/CMakeLists.txt`
  uses `FetchContent` to pull
  `github.com/bitcoin-core/secp256k1/archive/<pinned-ref>.tar.gz` with a verified
  SHA256, then builds it with CMake as part of coincurve's own build. It is not a
  prebuilt blob in a wheel.
- **Vendoring is the default, not a fallback.** `pyproject.toml` sets
  `PROJECT_IGNORE_SYSTEM_LIB = { env = "COINCURVE_IGNORE_SYSTEM_LIB", default = "ON" }`,
  so a system libsecp256k1 is ignored even when present.
- **Portable C, no perlasm.** Unlike OpenSSL — whose hot paths are hand-written
  assembly (`_aes_v8_encrypt`, `_aes_v8_ctr32_encrypt_blocks_unroll12_eor3`) that
  never enters clang's IR pipeline — libsecp256k1's aarch64 build is
  `__int128` C, so every instruction is compiler-generated and reachable by an
  MIR-level pass.

It also builds `-fPIC` static into the extension `.so`
(`COINCURVE_SECP256K1_STATIC` default `ON`), so the whole signing path lands in
one instrumentable object.

---

## 3. Build

```sh
git clone --depth 1 https://github.com/ofek/coincurve.git
echo "secp256k1_ecdsa_sign,3,pointee" > seed.txt      # unchanged, one line

python3.14 -m venv venv
venv/bin/pip install "hatchling>=1.27.0" "cffi>=2.1.1" "scikit-build-core>=0.9.0"

cd coincurve
CC=~/Documents/llvm-project/build/bin/clang \
CFLAGS="-ftaint-harden=$PWD/../seed.txt" \
COINCURVE_IGNORE_SYSTEM_LIB=ON COINCURVE_SECP256K1_STATIC=ON \
  ../venv/bin/pip install --no-build-isolation .
```

Three environment obstacles, none of them the pass's fault, all worth recording:

1. **Build isolation leaks CFLAGS.** `--no-binary :all:` makes pip build
   `cffi`/`hatchling` from source too, and `-ftaint-harden` is applied to them.
   Install build deps first, then use `--no-build-isolation`.
2. **`depot_tools/ninja` on `PATH` breaks CMake** — it is a shell script, not a
   binary, and `lipo` fails on it. Put `/opt/homebrew/bin` first.
3. **`pkg-config` is not installed on this machine** and coincurve's
   `CMakeLists.txt:35` has an unconditional `find_package(PkgConfig REQUIRED)`.
   Rather than install a system package, the build used a **stub `pkg-config`**
   that answers `--version` and reports every query as not-found — which is the
   correct answer anyway, since `COINCURVE_IGNORE_SYSTEM_LIB=ON` means the
   vendored library must be built regardless. Stub is in
   `utils/dit_host_screening/coincurve/pkg-config-stub`.

---

## 4. Where the switches landed

**594 `MSR DIT`** in `_libsecp256k1.cpython-314-darwin.so` (425 enables, 169
disables). Top of the census:

| function | switches | reached by the ECDSA seckey? |
|---|---|---|
| `secp256k1_ellswift_xdh` | **88** | **no** — ElligatorSwift ECDH |
| `secp256k1_musig_nonce_gen` | 45 | no — MuSig2, a different entry point |
| `secp256k1_musig_nonce_process` | 33 | no |
| `secp256k1_keypair_xonly_tweak_add` | 30 | no |
| `secp256k1_ecdsa_verify` | 27 | **no** — verification is all public |
| `secp256k1_tagged_sha256` | 26 | no — a public hashing utility |
| `secp256k1_ellswift_encode` | 25 | no |
| `secp256k1_schnorrsig_verify` | 24 | no |
| `secp256k1_musig_partial_sign` | 22 | no — separate secret, not seeded |
| **`secp256k1_ecdsa_sign`** | **22** | **yes** |
| `secp256k1_schnorrsig_sign32` | 21 | no — separate secret, not seeded |

**Only 38 of 594 switches (6.4%) sit on the plausible ECDSA-signing path**
(`ecdsa_sign`, `nonce_function_*`, `ecmult_gen*`, `modinv*`, `scalar_set_b32`,
`ge_set_gej`). The 455 in ellswift / musig / schnorr / verify / tagged-sha256
are collateral.

### Why, and why it is worse here

The seed names exactly one entry point, `secp256k1_ecdsa_sign` arg 3. Taint
should not reach MuSig or ElligatorSwift at all — those take *different* secrets
through *different, unseeded* entry points.

It reaches them through **context-insensitive mod-sets**
(`docs/design/context-insensitivity.md`). A shared helper — scalar/field
arithmetic, the SHA-256 core used by RFC 6979 — is called once with tainted data
from `ecdsa_sign`, its summary is marked "writes secret", and then *every* caller
of that helper inherits taint. `secp256k1_tagged_sha256` at 26 switches is the
clearest tell: a public hashing utility, tainted purely by sharing SHA-256
internals with nonce derivation.

The construct understated this because it built libsecp256k1 with default
options (148 switches). coincurve enables the full module set — ecdh, ellswift,
extrakeys, musig, recovery, schnorrsig — giving the over-approximation four times
as much code to spread into.

**Runtime cost is still near zero for this workload**, because an Ethereum signing
program never calls ellswift, musig or schnorr. The cost is code size, and a
latent bill for any application that *does* use those paths — a Bitcoin Taproot
wallet signs with Schnorr, and would execute the 21 switches in
`schnorrsig_sign32` plus whatever collateral sits in its callees.

---

## 5. What this changes

| | CPython construct | coincurve |
|---|---|---|
| interpreter | real (CPython + pyperformance) | n/a |
| crypto library | real (libsecp256k1) | real (libsecp256k1, as deployed) |
| the *reason to sign* | **written by us** | **real — signing an Ethereum transaction** |
| harness code in the signing path | `work.py`, `host_cpython.c`, `secret_payload.c` | **none** |
| seed | 1 line | 1 line, identical |
| switches emitted | 148 | 594 |
| on the secret's path | ~20% | **6.4%** |

The performance claim from `dit-pass-vs-oracle.md` is not re-measured here — this
establishes buildability, correctness and placement on real software, not timing.
**Measuring always-on vs pass-placed DIT on a web3.py signing workload is the
obvious next step**, and it is now a straightforward experiment rather than a
construction exercise.

---

## 6. Honest limits

- **Timing not measured.** Correctness and placement only.
- **`pkg-config` stub.** Harmless — it forces the vendored path that was wanted
  anyway — but the build is not byte-identical to what a user with a real
  `pkg-config` would get.
- **coincurve pins its own libsecp256k1 revision** (`VENDORED_UPSTREAM_REF`),
  which is not the same commit as the standalone clone used elsewhere in these
  results. Placement counts are not directly comparable across the two.
- **The broader "used everywhere" claim is coincurve's own**; only the
  web3 → eth-account → eth-keys → coincurve chain was verified here.

Rig: `utils/dit_host_screening/coincurve/`.
