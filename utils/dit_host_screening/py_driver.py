"""Run pyperformance benchmark bodies under our own timing, one process, no pyperf.

The benchmark sources are fetched verbatim from python/pyperformance; only the
pyperf Runner wrapper is bypassed, because pyperf does its own calibration and
spawns subprocesses, which fights the paired round-robin design the DIT sweep
needs. The workload code itself is unmodified.

Prints one line: PYBENCH <name> <seconds> <checksum>
"""
import importlib
import sys
import time
import types

# Stub pyperf so the benchmark modules import cleanly.
_m = types.ModuleType("pyperf")


class _Runner:
    def __init__(self, *a, **k):
        self.metadata = {}
        self.argparser = None


_m.Runner = _Runner
_m.perf_counter = time.perf_counter
sys.modules["pyperf"] = _m

richards = importlib.import_module("richards")
go = importlib.import_module("go")
float_bm = importlib.import_module("float")
nbody = importlib.import_module("nbody")

# Repeat counts chosen so each benchmark lands in the 1-3 s range on this
# machine, i.e. well clear of startup and long enough for a stable median.
BENCHES = {
    "richards": (lambda: richards.Richards().run(120), None),
    "go": (lambda: [go.versus_cpu() for _ in range(50)], None),
    "float": (lambda: [float_bm.benchmark(float_bm.POINTS) for _ in range(90)], None),
    "nbody": (lambda: nbody.bench_nbody(55, "sun", 20000), None),
}


def main():
    which = sys.argv[1] if len(sys.argv) > 1 else "all"
    names = list(BENCHES) if which == "all" else [which]
    for name in names:
        fn, _ = BENCHES[name]
        t0 = time.perf_counter()
        r = fn()
        t1 = time.perf_counter()
        print("PYBENCH %s %.6f %s" % (name, t1 - t0, type(r).__name__))
    sys.stdout.flush()


if __name__ == "__main__":
    main()
