# Real-application measurement: Django + PyJWT + cryptography

Answers "your composites are cherry-picked pairings". Nothing here is modelled -
Django 6.1, PyJWT 2.13 and cryptography 50.0 are installed unmodified from PyPI.

## Setup

```sh
python3 -m venv realapp && ./realapp/bin/pip install django pyjwt cryptography
clang -O2 -dynamiclib dit_ctl.c -o dit_ctl.dylib
python3 run_realapp.py 12 2
```

Use a Python that accepts `DYLD_INSERT_LIBRARIES` - Homebrew's works, the
SIP-protected `/usr/bin/python3` does not. (The `always` arm here sets DIT via
ctypes rather than injection, so it works either way, but the composite rig
needs injection.)

## Why the oracle wraps at the Python boundary

`jwt.encode` reaches OpenSSL through `cryptography`'s statically linked
extension module, so the crypto symbols are not dynamically bound and cannot be
interposed. Wrapping in Python instead protects the CFFI marshalling as well as
the signing - it can only **over**-protect, never under-protect, which is the
safe direction (`dit-measurement-traps` trap 8).

## Result

+1.21% always-on at 28% secret fraction; +1.18% always-on with ~0.64 points
recoverable by the oracle at a realistic 2.3% secret fraction. Full analysis:
`docs/research/real-world-instances.md` §6.

**This supersedes the CPython composite's +9.87%.** Same interpreter, same rig -
the composite used the two most DIT-sensitive pyperformance bodies, which
overstate a real application by ~7x.
