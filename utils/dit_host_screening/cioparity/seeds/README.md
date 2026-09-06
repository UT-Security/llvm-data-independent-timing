# Vendored CIO-parity seed files

Copies of `benchmarks/crypto/{libsodium_secret_contract,libsodium_secret}.txt`
from gem5-DIT at commit `4beef78deb4a5ccda3fce3bfb43dae09fd5a2ba1` -- the revision this repo's `gem5-DIT`
submodule pins, and the one the gem5 numbers in
`paper_experiments/09-libsodium-cio-parity` were produced at.

**Why a copy exists.** The gem5 rig
(`utils/dit_host_screening/cioparity/build_arms.sh`) reads these out of the
gem5-DIT tree, which is correct there: that rig cannot run without a simulator
anyway. The Apple-silicon rig (`utils/taint_libsodium_arms.sh`) needs the same
two files and nothing else from gem5-DIT, and requiring a multi-gigabyte
simulator checkout to build a libsodium archive on a Mac is a bad trade. So the
silicon path resolves seeds in this order: the submodule if it is checked out,
then this directory, then a `gh` fetch at the pinned commit.

**Checked against the pin, 2026-09-06.** The submodule moved from
\`9b05e6f51ce1\` to \`4beef78deb4a\` in PR #48; both seed files were re-fetched
and the 188 contract seeds and 65 CIO seeds are byte-identical across that
bump, only the contract file's header comments changed.

**These can drift.** gem5-DIT is authoritative. If the seed loop is run again
and the fixpoint moves, update these copies and the pin together, or the two
rigs stop measuring the same program. `libsodium_secret_contract.txt` is the
round-11 fixpoint (188 seed lines); `libsodium_secret.txt` is the CIO seed set
used by the `taintold` arms.
