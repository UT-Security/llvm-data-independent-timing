"""REAL APPLICATION benchmark: Django + PyJWT/cryptography, unmodified from PyPI.

This exists to answer the reviewer objection that the composite hosts in
docs/results/dit-oracle-composites.md are constructed pairings. Nothing here is
a model of a deployment - it IS the deployment pattern:

  PUBLIC  Django 6.1 request/response cycle: URL resolution, middleware stack,
          view dispatch, HttpRequest/HttpResponse construction, JSON rendering.
          Interpreted CPython work, exactly what a Django app spends its time on.
  SECRET  An ES256 (ECDSA P-256) JWT signed per request via PyJWT + cryptography.
          This is the standard Python API-auth stack - the same shape as
          djangorestframework-simplejwt issuing an access token.

Both libraries are installed from PyPI unmodified. Django, PyJWT and
cryptography are all top-downloaded packages; none of this is bespoke.

ARMS (all one process, DIT set at runtime):
  off     never set DIT
  always  DIT set once at startup, never cleared  (always-on reference)
  oracle  DIT set immediately before jwt.encode and cleared immediately after

The oracle wraps at the PYTHON boundary rather than inside the C crypto. That
is deliberately CONSERVATIVE: it protects the CFFI/marshalling around the sign
call as well as the sign itself, so it can only over-protect, never
under-protect. Under-protection is the failure mode that produced and retracted
SQLCipher's "+8.15%" (dit-measurement-traps trap 8).
"""
import ctypes
import json
import os
import sys
import time

import django
from django.conf import settings

MODE = os.environ.get("DIT_MODE", "off")

_lib = ctypes.CDLL(os.path.join(os.path.dirname(os.path.abspath(__file__)), "dit_ctl.dylib"))
_lib.dit_get.restype = ctypes.c_ulong


def dit_on():
    _lib.dit_on()


def dit_off():
    _lib.dit_off()


# ---------------------------------------------------------------- Django setup
settings.configure(
    DEBUG=False,
    SECRET_KEY="benchmark-only-not-a-real-secret",
    ALLOWED_HOSTS=["*"],
    ROOT_URLCONF=__name__,
    MIDDLEWARE=[
        "django.middleware.common.CommonMiddleware",
        "django.middleware.csrf.CsrfViewMiddleware",
        "django.middleware.clickjacking.XFrameOptionsMiddleware",
    ],
    DATABASES={},
    USE_TZ=True,
    LOGGING_CONFIG=None,
)
django.setup()

from django.http import JsonResponse  # noqa: E402
from django.urls import path  # noqa: E402
from django.test import Client  # noqa: E402

import jwt  # noqa: E402
from cryptography.hazmat.primitives.asymmetric import ec  # noqa: E402

# THE SECRET: an EC P-256 private key, exactly what an API server signs with.
SIGNING_KEY = ec.generate_private_key(ec.SECP256R1())

_counter = {"n": 0, "secret_s": 0.0, "toggles": 0}
request_counter = {"i": 0}
# 1 = every request issues a token (a login/refresh endpoint).
# 20 = app-wide realistic mix, where most requests do not mint a token.
SIGN_EVERY = int(os.environ.get("SIGN_EVERY", "1"))


def issue_token(sub):
    """The SECRET region: sign an ES256 JWT."""
    claims = {"sub": sub, "iat": 1700000000, "exp": 1900000000, "scope": "read write"}
    t0 = time.perf_counter()
    if MODE == "oracle":
        dit_on()
        _counter["toggles"] += 1
    tok = jwt.encode(claims, SIGNING_KEY, algorithm="ES256")
    if MODE == "oracle":
        dit_off()
        _counter["toggles"] += 1
    _counter["secret_s"] += time.perf_counter() - t0
    _counter["n"] += 1
    return tok


def api_view(request, uid):
    """PUBLIC: ordinary Django view work, then one signed token."""
    payload = {
        "user": uid,
        "items": [{"id": i, "name": "item-%d" % i, "qty": (i * 7) % 13} for i in range(40)],
        "meta": {"page": 1, "total": 40, "path": request.path},
    }
    if SIGN_EVERY and (request_counter["i"] % SIGN_EVERY) == 0:
        payload["token"] = issue_token(uid)
    request_counter["i"] += 1
    return JsonResponse(payload)


urlpatterns = [path("api/<str:uid>/", api_view)]


def main():
    requests = int(sys.argv[1]) if len(sys.argv) > 1 else 4000
    if MODE == "always":
        dit_on()
        _counter["toggles"] += 1

    client = Client()
    # warm: import caches, URL resolver cache, first crypto call
    for i in range(50):
        client.get("/api/warm%d/" % i)
    _counter.update(n=0, secret_s=0.0, toggles=_counter["toggles"])

    acc = 0
    t0 = time.perf_counter()
    for i in range(requests):
        r = client.get("/api/user%d/" % i)
        acc += r.status_code
    total = time.perf_counter() - t0

    print("WORK django checksum %d" % acc)
    print("HOST django mode=%s total_s=%.6f secret_s=%.6f secret_frac=%.4f%% "
          "signs=%d toggles=%d dit_now=%d"
          % (MODE, total, _counter["secret_s"], 100.0 * _counter["secret_s"] / total,
             _counter["n"], _counter["toggles"], _lib.dit_get()))


if __name__ == "__main__":
    main()
