# Hand-placed `crypto_verify_16`: is the hoisted enable costing the pointer reloads?

Three assembly versions of libsodium's constant-time tag comparison, taken
from the pass binary's own code, each exposed as `__wrap_crypto_verify_16` and
linked with `-Wl,--wrap=crypto_verify_16` ahead of the UNHARDENED library into
CIO's aes256-gcm decrypt driver, so the compare is the only thing that runs
with DIT set:

- `v16_none.S`: no switch at all (the control for the wrapper itself);
- `v16_hoist.S`: what the pass emits, one enable before the loop, the whole
  body covered, one clear after;
- `v16_periter.S`: the loads reordered so the clean instructions (the two
  volatile pointer reloads, `i++`, the bound) run DIT-off and only the eight
  the analysis marks tainted are covered: two switches per iteration.

Build (from `build_arms.sh`'s staged driver and base library):

    clang -march=armv8.4-a -O2 -std=gnu18 -static -fomit-frame-pointer -DNO_DYN_HIT_COUNTS \
      -I<rig> -I<gem5>/include -I<work>/base/src/libsodium/include \
      <work>/src/eval_aesni256gcm_decrypt.c <rig>/blanket_ctor.c v16_<variant>.S \
      -Wl,--wrap=crypto_verify_16 <work>/base/src/libsodium/.libs/libsodium.a -lm5 -lm

Run as `run_cio_gem5.py` does (`50 10 <msg> 100 cc.txt`, both switch models).
Result 2026-09-05 in `paper_experiments/09-libsodium-cio-parity/README.md`.

Counterfactual: `--publishing-dit-clear` on the gem5 command line (gem5-DIT-pmull
branch `ditcycles`) makes `msr DIT, #0` take effect at rename instead of at
commit, so the per-iteration variant's DIT-off instructions are actually seen
DIT-off by the value predictor. Insecure by construction; measurement only.
Result: the public reloads are predicted on every iteration and the loop
costs the same (+8.09% against +8.19%).
