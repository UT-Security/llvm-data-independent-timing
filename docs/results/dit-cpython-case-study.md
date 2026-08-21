# CPython case study: what is protected, and where the switches landed

**Companion to `dit-pass-vs-oracle.md`.** That document reports the numbers; this
one shows the code, the exact placement the pass produced, and what is and is not
inside the protected region — so the result can be audited rather than trusted.

Binary: `host_cpython_region` / `host_cpython_hoist`, built 2026-08-14 with
`-ftaint-harden` on the `dit-tainter` branch.

---

## 1. The three layers

The workload is deliberately stratified so that the boundary between public and
secret is a single function call, and so the interpreter and the crypto are
separate translation units.

### Layer 1 — PUBLIC: the Python program (`work.py`)

This is the part that must keep its data-dependent hardware optimizations. It is
the pyperformance bodies that screened most DIT-sensitive (richards +11.11%, go
+7.70%), shaped as a request loop.

```python
import _secret
import richards
import go

acc = 0
# "requests": each does a chunk of interpreter work then signs once, which is
# the shape of a request handler issuing a signed session cookie.
for req in range(24):
    richards.Richards().run(5)
    if SIGS:
        acc += _secret.sign(SIGS)
    go.versus_cpu()
    if SIGS:
        acc += _secret.sign(SIGS)
```

**Nothing here is secret.** The interpreter, the object graph, the bytecode
dispatch loop — all public, and all of it is what always-on DIT was slowing down
by 9.99%.

### Layer 2 — BRIDGE: the C extension (`host_cpython.c`)

`_secret.sign()` is registered into the embedded interpreter with
`PyImport_AppendInittab`. It is a thin trampoline; it holds no secret itself.

```c
static PyObject *py_sign(PyObject *self, PyObject *args) {
    int n = 1;
    if (!PyArg_ParseTuple(args, "|i", &n)) return NULL;
    double t0 = now_s();
    unsigned long r = secret_sign_n(n);          /* <- into the secret layer */
    g_secret_s += now_s() - t0;
    return PyLong_FromUnsignedLong(r & 0x7fffffffUL);
}
```

### Layer 3 — SECRET: the payload (`secret_payload.c`)

The key is a static, and it is read in exactly one place.

```c
static unsigned char g_seckey[32];   /* THE SECRET */

unsigned long secret_sign_n(int n) {
    for (int i = 0; i < n; i++) {
        /* public input: the message hash */
        for (int j = 0; j < 32; j++)
            msg[j] = (unsigned char)(i * 31 + j * 17 + (int)g_signs);

        secp256k1_ecdsa_sign(g_ctx, &sig, msg, g_seckey, NULL, NULL);
        /*                                     ^^^^^^^^ arg 3 = the taint seed */

        /* DECLASSIFIED: the signature is published by definition of ECDSA.
         * Serializing it is public work and sits outside any protected region. */
        outlen = sizeof out;
        secp256k1_ecdsa_signature_serialize_der(g_ctx, out, &outlen, &sig);
        for (size_t j = 0; j < outlen; j++)
            acc = acc * 131 + out[j];
    }
    return acc;
}
```

---

## 2. The taint source is one line

```
secp256k1_ecdsa_sign,3,pointee
```

`3` is the 0-based index of `seckey`; `pointee` says the *pointer* is public but
the 32 bytes it addresses are secret. That is the entire annotation burden.

It is this small because libsecp256k1's amalgamated build is a **single
translation unit**, so interprocedural taint reaches the whole signing path
without re-declaration. Contrast SQLCipher, which needed 11 seed lines across 2
TUs because taint is TU-scoped and the key crossed library boundaries.

**Only `secp256k1.c` is compiled with `-ftaint-harden`.** `host_cpython.c`,
`secret_payload.c` and all of CPython are built stock.

---

## 3. Where the switches landed

151 `MSR DIT` in the whole `host_cpython_region` binary. By function:

| function | switches | reads the secret? |
|---|---|---|
| `secp256k1_ecdsa_verify` | **30** | **no** — see §6 |
| `secp256k1_ecdsa_sign` | 17 | **yes** |
| `nonce_function_rfc6979_impl` | 11 | **yes** — derives the nonce from the key |
| `secp256k1_ec_pubkey_tweak_mul` | 9 | no |
| `secp256k1_ecmult_gen_blind` | 8 | context setup only |
| `secp256k1_ec_pubkey_tweak_add` | 7 | no |
| `secp256k1_der_parse_integer` | 7 | no |
| `secp256k1_ecdsa_signature_parse_compact` | 6 | no |
| `secp256k1_ec_seckey_tweak_mul` | 6 | no |
| `secp256k1_ec_seckey_tweak_add` | 5 | no |
| `secp256k1_ec_pubkey_create` | 5 | no |
| `secp256k1_ecmult_gen_ge` | 4 | **yes** — the scalar multiplication |
| … | | |

### The number that matters

```
switches inside CPython symbols (_Py*, ceval, PyEval, PyObject): 0
```

**Zero.** The interpreter — the thing paying the 9.99% under always-on — carries
no protection at all. That is the whole mechanism of the win, and it is the same
property that made the QuickJS result work (`zero in JS_CallInternal`).

Enables outnumber disables, 99 to 52 across the binary and 14 to 3 inside
`secp256k1_ecdsa_sign`. That is not an imbalance bug: several control-flow paths
enter a protected region and each entry edge needs its own enable, while they
converge onto few exits. The pass also re-asserts DIT after returning from a
call rather than assuming the callee left it set — the callee-ownership rule.

---

## 4. Inside `secp256k1_ecdsa_sign`

The function is 493 instructions. Mapping every switch to its position and the
call it follows:

| position | instruction | context |
|---|---|---|
| 20 / 493 | `msr DIT, #0x1` | immediately after the `cbz` null-check on `seckey` |
| 37 | `msr DIT, #0x1` | after `secp256k1_scalar_set_b32` |
| 64 | `msr DIT, #0x1` | after `secp256k1_scalar_set_b32` |
| **91** | **`msr DIT, #0x0`** | early-exit path (invalid seckey) |
| 93 | `msr DIT, #0x1` | re-enter |
| 102, 104, 112 | `msr DIT, #0x1` | after `nonce_function_rfc6979_impl` |
| 118 | `msr DIT, #0x1` | after `secp256k1_scalar_set_b32` |
| 131 | `msr DIT, #0x1` | after `secp256k1_ecmult_gen_ge` — the scalar multiply |
| 263, 268 | `msr DIT, #0x1` | after `secp256k1_scalar_mul` |
| 330 | `msr DIT, #0x1` | after `secp256k1_modinv64` — the inversion |
| 348, 457 | `msr DIT, #0x1` | after `secp256k1_scalar_mul` |
| **471** | **`msr DIT, #0x0`** | immediately before `RET` at 472 |
| **491** | **`msr DIT, #0x0`** | the other exit |

Read it as a shape rather than a list: **DIT goes on at instruction 20 and comes
off only at the function's exits.** Everything between — nonce derivation from
the key, the scalar multiplication, the modular inversion, the final scalar
multiplies — runs protected. The three disables are the two returns and one
early-exit path.

Here is the entry, verbatim:

```asm
mov   x24, x3                 ; x3 = seckey, the seeded argument
cbz   x3, <L10>               ; null check, still unprotected
msr   DIT, #0x1               ; <-- protection begins
mov   x22, x5
mov   x23, x4
str   x1, [sp, #0x8]
movi.2d v0, #0000000000000000
stp   q0, q0, [sp, #0x80]
...
```

The `hoist` variant is the same shape with fewer re-assertions — 14 switches in
this function instead of 17, 139 in the binary instead of 151 — because
`-taint-dit-loop-hoist=1` lifts enables out of loop bodies to the preheader.
That difference is worth 0.91% → −0.08% on the whole program.

---

## 5. What is protected, and what is deliberately not

**Protected** (inside DIT):

- nonce derivation, `nonce_function_rfc6979_impl` — RFC 6979 derives the nonce
  deterministically *from the private key*, so it is as sensitive as the key
- the scalar multiplication `k·G`, via `secp256k1_ecmult_gen_ge`
- the modular inversion `k⁻¹`, via `secp256k1_modinv64` (constant-time safegcd)
- the final `s = k⁻¹(h + r·d)` scalar multiplies

**Deliberately outside** (declassified):

- **DER serialization of the signature.** The signature is published by
  definition of the protocol — that is what a signature is *for*. Protecting it
  would be pure cost with no security benefit.
- the message hash, which is public input
- the whole Python program

This is the part that answers the standing reviewer objection to the QuickJS
result. That experiment needed the secret to be written to a sink instead of
returned, because a returned value would taint the caller and flood the
interpreter with switches. Here **no trick is required**: the value that flows
back to the caller is a signature, and signatures are public. **The
declassification boundary is the protocol's, not the harness's.**

---

## 6. The over-approximation, stated plainly

`secp256k1_ecdsa_verify` carries **30 switches — more than `ecdsa_sign` itself —
and verification takes no secret at all.** Verification consumes a public key, a
public message and a public signature.

It is free in this workload only because `work.py` never verifies anything, so
those 30 switches are never executed. The cost is zero here and would not be
zero for, say, a blockchain node, which verifies far more signatures than it
produces.

The other false positives — `ec_pubkey_tweak_mul` (9), `der_parse_integer` (7),
`ecdsa_signature_parse_compact` (6), `ec_pubkey_create` (5) — have the same
character: reachable from a seeded argument in the call graph, but not carrying
the secret on any path this program takes.

This is a **precision** problem, not a placement problem: context-insensitive
mod-sets (`docs/design/context-insensitivity.md`). It is currently the
highest-value target in the pass.

---

## 7. What it costs

Against an unprotected baseline of 3.823 s, 16 paired reps, arm order rotated:

| arm | overhead | reps slower |
|---|---|---|
| noise floor (baseline re-run) | +0.11% | 9/16 |
| **pass + `-taint-dit-loop-hoist=1`** | **−0.08%** | 7/16 |
| hand oracle | +0.01% | 8/16 |
| pass, `region` (shipped default) | +0.91% | 13/16 |
| pass, `placement=function` | +1.18% | 14/16 |
| **always-on DIT** | **+9.99%** | 16/16 |

With loop-hoist, protection is free — the arm is inside the rig's own noise
floor and only 7 of 16 reps were slower, which is a coin flip. Against always-on
that is a **9.12% speedup** (1.100×).

And per `dit-pass-vs-oracle.md` §7, gem5 confirms the pass covers **101.3%** of
the secret work the hand oracle covers, so "free" is not bought by protecting
less.

---

Sources: `utils/dit_host_screening/secret/` (composite),
`utils/dit_host_screening/pass/` (pass build + data),
`utils/dit_host_screening/gem5cov/` (coverage check).
