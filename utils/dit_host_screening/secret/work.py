# PUBLIC half: the pyperformance bodies that screened most DIT-sensitive
# (richards +11.11%, go +7.70%). SECRET half: _secret.sign(), interleaved the
# way a web app signs one session cookie per request.
import sys
import types

_m = types.ModuleType("pyperf")
class _Runner:
    def __init__(self, *a, **k):
        self.metadata = {}
        self.argparser = None
_m.Runner = _Runner
import time as _time
_m.perf_counter = _time.perf_counter
sys.modules["pyperf"] = _m

import _secret
import richards
import go

SIGS = globals().get("SIGS", 1)

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

print("WORK cpython checksum", acc % 1000000)
