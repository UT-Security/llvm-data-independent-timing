import sys, time
from coincurve import PrivateKey
N=20000; k=bytes(range(1,33))
for _ in range(200): PrivateKey(k)
t0=time.perf_counter()
for _ in range(N): PrivateKey(k)
t1=time.perf_counter()
print("PrivateKey() x%d = %.4f s (%.2f us each)"%(N,t1-t0,(t1-t0)/N*1e6))
