"""A real web3.py signing workload: eth-account signing Ethereum transactions.

PUBLIC work is everything eth-account does around the signature - RLP encoding,
keccak hashing, field validation, hex/bytes conversion, dict manipulation, all
of it interpreted Python. SECRET work is the ECDSA signature itself, inside
coincurve's vendored libsecp256k1, which is the only thing the taint pass
instrumented.

Nothing here is written for the benchmark: this is the ordinary way a wallet or
a backend service signs a transaction.

Prints:  SIGNRUN n=<N> total_s=<s> raw_sign_s=<s> secret_frac=<%> checksum=<x>
"""
import sys
import time

from eth_account import Account
from coincurve import PrivateKey

N = int(sys.argv[1]) if len(sys.argv) > 1 else 400

KEY = bytes(range(1, 33))
acct = Account.from_key(KEY)
TO = "0x" + "11" * 20


def build_tx(i):
    return {
        "nonce": i,
        "gasPrice": 20_000_000_000 + i,
        "gas": 21000,
        "to": TO,
        "value": 10**18 + i,
        "chainId": 1,
    }


# warm: import paths, first-call caches, keccak tables
for i in range(5):
    acct.sign_transaction(build_tx(i))

checksum = 0
t0 = time.perf_counter()
for i in range(N):
    signed = acct.sign_transaction(build_tx(i))
    checksum = (checksum ^ signed.r) & 0xFFFFFFFFFFFFFFFF
t1 = time.perf_counter()

# Separately measure the raw ECDSA signature alone, so the secret's share of the
# run is reported rather than assumed. Same key, same count, no eth-account.
pk = PrivateKey(KEY)
msgs = [(b"%032d" % i) for i in range(N)]
t2 = time.perf_counter()
for m in msgs:
    pk.sign(m)
t3 = time.perf_counter()

total = t1 - t0
raw = t3 - t2
print("SIGNRUN n=%d total_s=%.6f raw_sign_s=%.6f secret_frac=%.4f%% checksum=%d"
      % (N, total, raw, 100.0 * raw / total, checksum))
