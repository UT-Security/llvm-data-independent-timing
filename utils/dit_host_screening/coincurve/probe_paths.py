"""Time BOTH secp256k1 signing entry points separately.
raw_sign_s in sign_workload.py used PrivateKey.sign() -> secp256k1_ecdsa_sign,
but eth-account uses sign_recoverable() -> secp256k1_ecdsa_sign_recoverable.
If the two diverge, the first is a bad proxy for the workload's real cost.
"""
import sys, time
from coincurve import PrivateKey
N = int(sys.argv[1]) if len(sys.argv) > 1 else 20000
pk = PrivateKey(bytes(range(1, 33)))
msgs = [(b"%032d" % i) for i in range(N)]
for m in msgs[:200]:
    pk.sign(m); pk.sign_recoverable(m)
t0 = time.perf_counter()
for m in msgs: pk.sign(m)
t1 = time.perf_counter()
for m in msgs: pk.sign_recoverable(m)
t2 = time.perf_counter()
print("PATHS n=%d sign=%.4f s  sign_recoverable=%.4f s" % (N, t1-t0, t2-t1))
