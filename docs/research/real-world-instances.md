# Real deployed software exhibiting each (host, secret) pairing

**Written 2026-08-14.** Answers the reviewer objection against
`docs/results/dit-oracle-composites.md`: that its four composite hosts are
pairings we invented, and therefore cherry-picked.

The claim this document supports is narrow and checkable: **for every composite,
there is named, widely deployed software that already puts that interpreter and
that class of secret in one address space.** The composite is then a model of a
real thing rather than an invention.

Where a pairing has NO good real-world instance, that is said plainly rather
than papered over - see QuickJS in §4.

---

## 1. CPython + per-request signing

The strongest case. Three independent, very widely deployed instances:

| instance | public work | secret work | notes |
|---|---|---|---|
| **Django signed sessions** | full request/response cycle | HMAC-SHA256 over the session cookie using `SECRET_KEY`, on **every request** with `SESSION_ENGINE = signed_cookies` | `django.core.signing`; Django's own docs describe the signing as using `SECRET_KEY` and warn it must stay secret because possession allows session forgery |
| **DRF + SimpleJWT / PyJWT** | DRF view + serializer stack | ES256/RS256 JWT signed per token issue | PyJWT + `cryptography` is the standard Python API-auth stack |
| **eth-account / web3.py** | RLP + ABI encoding, JSON-RPC marshalling - all interpreted | **secp256k1 ECDSA signing via `coincurve`**, which is CFFI bindings to **libsecp256k1**, the same library used by Bitcoin Core | this is *literally* the composite: CPython public work + libsecp256k1 secret. `eth-keys` exposes `CoinCurveECCBackend` for exactly this |
| **certbot** | ACME protocol driving, JSON handling | signs **every** ACME request with the account key (JWS) | |
| **paramiko / Ansible** | SSH protocol state machine in Python | Ed25519/RSA signing per authentication | |

**The eth-account instance deserves emphasis:** it is the same interpreter and
the same crypto library as our `host_cpython` composite, in software that
actually ships. A reviewer asking "who runs CPython next to libsecp256k1?" has a
one-word answer: Ethereum.

---

## 2. SQLite + per-record crypto

| instance | public work | secret work | notes |
|---|---|---|---|
| **Chrome / Chromium cookie store** | SQLite queries over the `Cookies` DB | **AES-256-GCM per cookie value** (`encrypted_value` column), key from OSCrypt | ubiquitous; per-*value* rather than per-page, so the region is larger than SQLCipher's 16-byte-block case |
| **Signal Desktop** | SQLite queries over the message DB | SQLCipher page-level AES | this is the case already measured as a negative in `docs/results/sqlcipher.md` |
| **Firefox** | `places.sqlite` history/bookmark queries | NSS key material in `key4.db` | |

Chrome's cookie store is the better instance of the two, precisely because it is
**per-value** crypto rather than SQLCipher's per-4KB-page crypto - which is what
put SQLCipher 3-4x below the `dit-granularity-crossover` threshold.

---

## 3. Lua + crypto in one process

Weaker than CPython, and with one important caveat.

| instance | public work | secret work | caveat |
|---|---|---|---|
| **Kong Gateway / OpenResty** | Lua request handling, routing, plugins - at very large scale | `lua-resty-jwt` + `lua-resty-hmac`: **JWT HMAC signing/verification per request** | ⚠️ **runs LuaJIT, not plain Lua.** A JIT means the taint pass cannot instrument the hot code, so this is evidence the *pattern* is real, not a workload we could use directly |
| **Prosody** (XMPP server) | connection and stanza handling, written in Lua | SASL **SCRAM-SHA** HMAC per authentication | plain Lua; the crypto is per-login rather than per-message, so the secret is rarer |
| **Nmap NSE** | Lua 5.4 scripts driving scans | compiled `openssl` NSE module | Lua 5.4 embedded exactly as in our composite, but the crypto is not a *secret-holding* operation |

**Honest reading:** the Kong/OpenResty pattern is real and enormous, but LuaJIT
puts it out of reach of an AOT pass. Plain-Lua instances exist (Prosody, Nmap)
but their crypto is rarer than per-request. **Lua should be presented as the
mechanism-isolation host, not as a deployment claim.**

---

## 4. QuickJS - no good real-world instance

Stated plainly because it matters. QuickJS's production footprint is small:
TheirStack lists only 8 companies, and MicroQuickJS (Dec 2025) is explicitly
too new for any validated deployment. There is no widely deployed
QuickJS-plus-signing system comparable to Django or Kong.

The real-world version of "JS engine + per-request signing" is **Node.js and
Cloudflare Workers**, and both run V8 - a JIT, so the pass cannot instrument the
hot path at all.

**Recommendation:** keep QuickJS in the paper as the *continuity* workload - it
is what the project's earlier result used, so it anchors the new numbers to the
old ones - and do **not** advance it as a deployment claim.

---

## 5. What this changes about the paper

- **CPython is the flagship.** It has the most real instances, the strongest
  single instance (eth-account = CPython + libsecp256k1), and the largest
  screened prize among hosts with a real secret.
- **SQLite is the second string**, with Chrome's cookie store as the instance
  and the useful property that it is per-value rather than per-page crypto.
- **Lua and QuickJS are mechanism/continuity hosts**, not deployment claims.
  Presenting them as representative deployments is the thing a reviewer would
  correctly attack.

---

## 6. Measured: a real application, not a model

**This section corrects the composite results, and it is the most important
measurement of the day.**

Rig: `utils/dit_host_screening/realapp/`. Django 6.1 + PyJWT 2.13 +
cryptography 50.0, all unmodified from PyPI, on Homebrew CPython 3.14.6.
PUBLIC = the real Django request/response cycle (URL resolution, middleware,
view dispatch, JSON rendering). SECRET = an ES256 JWT signed per request. Four
arms with `off2` as the noise floor, arm order rotated, 12 reps, paired.

| signing rate | secret % | off s | always s | oracle s | **always-on** | oracle | noise | o<a |
|---|---|---|---|---|---|---|---|---|
| every request (token endpoint) | 27.9% | 3.459 | 3.501 | 3.513 | **+1.21%** | +1.35% | +0.05% | 5/12 |
| 1-in-20 (app-wide mix) | 2.3% | 2.454 | 2.487 | 2.471 | **+1.18%** | +0.54% | +0.48% | 11/12 |

### What this says

**1. A real application shows +1.2% always-on DIT, not the +9.87% the CPython
composite showed.** Same interpreter, same machine, same rig. The composite
overstated the prize by roughly **8x**.

**2. The cause is workload selection, not build configuration.** That confound
was tested directly - the same pyperformance bodies were run on both a
non-PGO source build and Homebrew's PGO+LTO build:

| CPython build | richards | go | float | nbody | **total** |
|---|---|---|---|---|---|
| source build, no PGO | +11.73% | +8.21% | +6.93% | +2.40% | **+7.20%** |
| Homebrew, PGO+LTO | +7.06% | +7.49% | +11.38% | +9.15% | **+8.72%** |

Both are high. So the gap is **not** the build - it is that `richards`/`go` are
pure-Python object-graph benchmarks, maximally bytecode-dispatch-bound, while a
real Django request spends much of its time in CPython's **C layer**: JSON
encoding, regex URL resolution, string and dict operations, header handling.
That C work is compiled, not dispatched, and is far less DIT-sensitive.

**Choosing the two most DIT-sensitive pyperformance bodies was itself a form of
cherry-picking.** The objection that prompted this document was correct, and it
applies to the benchmark selection inside each composite, not only to the
host/secret pairing.

**3. The fine-grained win survives, but it is small.** At a realistic app-wide
signing rate the oracle recovers **~0.64 points of a 1.18% cost**, consistent in
direction (11/12 reps) but only marginally above the rig's own +0.48% floor. At
the token-endpoint rate (28% secret) the oracle gives **nothing** - exactly what
`dit-granularity-crossover` predicts, since both arms pay DIT over the secret
work and that term cancels.

**4. This VINDICATES `dit-prize-is-one-to-two-percent`.** A real application
lands at +1.2%, squarely in the 1-2% band that memory records from gem5
feature-isolation on SPEC/gapbs. An earlier draft of
`docs/results/dit-host-screening.md` argued that the 1-2% figure should be
"scoped to its instrument" because interpreter benchmarks showed 6-14%. **That
reasoning was backwards** - the benchmarks were the outliers, and the two
independent instruments agree on real software. That paragraph is corrected in
place.

### Consequence for the paper

Quote **+1.2% always-on and ~0.6 points recoverable** for a real Python web
application. The 6-14% screening numbers are benchmark figures and must be
labelled as such. This is the same lesson as
`dit-prize-is-one-to-two-percent`'s "microbenchmarks overstate by ~200x", one
level up the stack: **pyperformance overstates by ~7x relative to a real
application on the same interpreter.**

---

## Sources

- Django signing / signed cookie sessions - https://docs.djangoproject.com/en/3.2/topics/signing/ , https://docs.djangoproject.com/en/3.2/topics/http/sessions/
- coincurve, CFFI bindings to libsecp256k1 - https://ofek.dev/coincurve/ , https://pypi.org/project/coincurve/
- eth-account backends (`CoinCurveECCBackend`) - https://github.com/ethereum/eth-account/blob/main/eth_account/account.py
- web3.py accounts - https://web3py.readthedocs.io/en/stable/web3.eth.account.html
- lua-resty-jwt (OpenResty JWT, requires LuaJIT) - https://github.com/SkyLothar/lua-resty-jwt
- Kong custom Lua plugins - https://konghq.com/blog/engineering/custom-lua-plugin-kong-gateway
- Prosody SCRAM implementation - https://github.com/bjc/prosody/blob/master/util/sasl/scram.lua
- Nmap NSE (embedded Lua 5.4, openssl module) - https://nmap.org/book/nse.html , https://nmap.org/book/nse-library.html
- Chrome cookie AES-256-GCM per value - https://gist.github.com/creachadair/937179894a24571ce9860e2475a2d2ec , https://github.com/xaitax/Chrome-App-Bound-Encryption-Decryption/blob/main/docs/RESEARCH.md
- QuickJS production footprint - https://theirstack.com/en/technology/quickjs , https://bellard.org/quickjs/
