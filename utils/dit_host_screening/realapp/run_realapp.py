#!/usr/bin/env python3
"""Paired measurement of the REAL Django + PyJWT application.

Same discipline as the composite rig: four arms with `off2` as the noise floor,
arm order rotated every rep, burn-in discarded, paired by rep. Two signing
rates: every request (a token/refresh endpoint) and 1-in-20 (an app-wide mix
where most requests do not mint a token).
"""
import csv
import os
import re
import statistics as st
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PY = os.path.join(os.path.dirname(HERE), "realapp", "bin", "python")
SCRIPT = os.path.join(HERE, "django_jwt_bench.py")

ARMS = [("off", "off"), ("always", "always"), ("oracle", "oracle"), ("off2", "off")]
RATES = [("every", "1", "40000"), ("1in20", "20", "40000")]

reps = int(sys.argv[1]) if len(sys.argv) > 1 else 12
burnin = int(sys.argv[2]) if len(sys.argv) > 2 else 2
out = os.path.join(HERE, "realapp.csv")

rx = re.compile(r"total_s=([0-9.]+) secret_s=([0-9.]+) secret_frac=([0-9.]+)% "
                r"signs=(\d+) toggles=(\d+)")

with open(out, "w", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(["rep", "rate", "arm", "total_s", "secret_s", "secret_frac",
                "signs", "toggles", "checksum"])
    for r in range(reps + burnin):
        keep = r >= burnin
        for rate, every, nreq in RATES:
            order = ARMS[r % len(ARMS):] + ARMS[:r % len(ARMS)]
            for arm, mode in order:
                env = dict(os.environ)
                env["DIT_MODE"] = mode
                env["SIGN_EVERY"] = every
                p = subprocess.run([PY, SCRIPT, nreq], cwd=HERE, env=env,
                                   capture_output=True, text=True)
                if p.returncode != 0:
                    print("  !!", rate, arm, p.stderr[:200], flush=True)
                    continue
                m = rx.search(p.stdout)
                ck = [l for l in p.stdout.split("\n") if l.startswith("WORK")]
                if m and keep:
                    w.writerow([r - burnin, rate, arm, m.group(1), m.group(2),
                                m.group(3), m.group(4), m.group(5),
                                ck[0] if ck else ""])
            fh.flush()
        print(f"rep {r - burnin if keep else 'burnin'} done", flush=True)

# ---- summary ----
rows = list(csv.DictReader(open(out)))
d, meta, ck = {}, {}, {}
for x in rows:
    d.setdefault((x["rate"], x["arm"]), {})[int(x["rep"])] = float(x["total_s"])
    meta[(x["rate"], x["arm"])] = (float(x["secret_frac"]), int(x["signs"]), int(x["toggles"]))
    ck.setdefault(x["rate"], set()).add(x["checksum"])


def pr(rate, a, b):
    x, y = d[(rate, a)], d[(rate, b)]
    reps_ = sorted(set(x) & set(y))
    return [y[i] / x[i] for i in reps_]


print()
print("=" * 96)
print("REAL APP: Django 6.1 + PyJWT + cryptography (all unmodified from PyPI)")
print("=" * 96)
print(f"{'rate':7s} {'off s':>8s} {'always s':>9s} {'oracle s':>9s} | "
      f"{'always-on':>10s} {'oracle':>9s} | {'NOISE':>7s} {'secret%':>8s} {'o<a':>7s}")
for rate, _, _ in RATES:
    a = (st.median(pr(rate, "off", "always")) - 1) * 100
    o = (st.median(pr(rate, "off", "oracle")) - 1) * 100
    n = (st.median(pr(rate, "off", "off2")) - 1) * 100
    beat = pr(rate, "always", "oracle")
    frac = meta[(rate, "oracle")][0]
    print(f"{rate:7s} {st.median(d[(rate,'off')].values()):8.3f} "
          f"{st.median(d[(rate,'always')].values()):9.3f} "
          f"{st.median(d[(rate,'oracle')].values()):9.3f} | {a:+9.2f}% {o:+8.2f}% | "
          f"{n:+6.2f}% {frac:7.2f}% {sum(1 for p in beat if p<1):3d}/{len(beat)}")
print()
for rate in ck:
    print(f"  checksum stable, {rate:7s}: {'PASS' if len(ck[rate])==1 else 'FAIL'}")
