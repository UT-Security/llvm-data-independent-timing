# Integrating the Taint-Hardening Pass into a Firefox Compile

Interprocedural taint analysis inserts PSTATE.DIT mode switches around secret-dependent
code (AArch64). As of the `-ftaint-harden` work, a **single `clang -c` applies the
whole transformation** — no more multi-tool `opt`/`llc` pipeline in the build. This
doc is the integration guide; the legacy multi-step flow is kept at the end as a
fallback.

---

## 1. The one-flag model

```
clang -O2 -ftaint-harden=<taint-src-file> -c file.cpp -o file.o
```

That's it. When `-ftaint-harden=<file>` is present, clang:
1. runs the `taint-annotate` IR pass (marks tainted args from `<file>`), then
2. lowers to post-prologepilog MIR, runs the interprocedural taint pass (inserts
   PSTATE.DIT mode switches), and emits the object — all in one process.

When the flag is **absent**, codegen is byte-for-byte unchanged. The produced `.o`
is a normal object linked by Firefox's normal link step. **Only the compile of
chosen TUs changes; linking and everything else is untouched.**

Internally the flag drives a legacy-PM → in-memory-MIR-round-trip → legacy-PM flow
(equivalent to the old `llc -stop-after=prologepilog` / `-run-taint-interproc` /
`-start-after=prologepilog` steps), because the interprocedural pass needs every
MachineFunction of the TU resident at once. This is an implementation detail — the
build only sees one `clang -c`.

---

## 2. Toolchain requirement (critical)

`-ftaint-harden` exists only in **this LLVM tree's clang** (`<repo>/build/bin/clang`).
A stock/system clang does not have the flag. Firefox must use this clang as its
compiler — via `mozconfig` (`CC`/`CXX`, or a compiler wrapper, Section 5).

Verify the flag exists:
```
<repo>/build/bin/clang --help | grep taint-harden
# ftaint-harden=<file>  Path to a taint-source file ...
```

---

## 3. Taint-source file format

One entry per line, naming which function arguments carry secrets:
```
function_name,arg_index
function_name,arg_index,pointee
```
- `func,0`         → argument 0's *value* is secret.
- `func,0,pointee` → the pointer in arg 0 is public, but memory **loaded through it**
                     is secret (typical for buffers).

`arg_index` is 0-based. `#` starts a comment line. For a Firefox build this lists the
entry points where attacker/secret data enters the hardened TUs; the pass propagates
taint interprocedurally from there.

**C++ name mangling:** `taint-annotate` matches the symbol name in the IR, so C++
entries must use the **mangled** name (e.g. `_ZN3mozilla6FooBarEPKhj`), not the
source name. Get it from `clang -emit-llvm -S` output or `llvm-nm`/`c++filt`.
`extern "C"` functions keep their plain name.

Example (`firefox_taint_sources.txt`):
```
# image buffer contents are secret as they enter the convolution kernels
_ZN7mozilla3gfx19FilterNodeSoftware18RenderConvolve...EPKhj,1,pointee
```

---

## 4. The two correctness constraints

1. **Opt-level match.** Use the same `-O` level Firefox compiles that TU at (usually
   `-O2`). Taint seeding runs at OptimizerLast so attributes survive the middle-end;
   still, compile the hardened TU at its real opt level.
2. **Flags passthrough.** `-ftaint-harden` composes with all normal compile flags
   (`-I`, `-D`, `--target=`, `-std=`, sysroot, …) — just add it to the existing
   command line for that TU. Nothing else about the invocation changes.

**Protection mode.** PSTATE.DIT, function granularity, is the only mode (the
ISB/DSB speculation-barrier mode was removed 2026-07-14). It needs FEAT_DIT at
run time. Formerly selected with `-mllvm -taint-barrier-mode=dit` (added next to
`-ftaint-harden`) switches to **function-granularity PSTATE.DIT** — `MSR DIT, #1`
at entry / `MSR DIT, #0` before returns of any function containing taint. Note the
different threat model (data-independent *timing*, not anti-speculation) and the
hardware requirement: FEAT_DIT (Armv8.4+) at run time, or the instruction SIGILLs.

**Debug info (`-g`) is fully supported.** `-g -O2 -ftaint-harden` produces the same
barriers as the non-debug build, `llvm-dwarfdump --verify` is clean, and call-site
debug info (`DW_TAG_call_site`) is preserved — so Firefox's default debug-info
builds need no special handling. (This required a MIR-parser fix in this tree —
`callSites:` block-number resolution — one more reason the bundled clang from this
LLVM checkout is required, per Section 2.)

---

## 5. Recommended integration: a compiler wrapper

Set the wrapper as Firefox's `CC`/`CXX`. For each invocation:
- If the TU is **not** a hardening target → `exec` the real clang unchanged.
- If it **is** a target → append `-ftaint-harden=<file>` and `exec` the real clang.

That's the entire change — one appended flag. Skeleton:

```bash
#!/usr/bin/env bash
set -euo pipefail
REAL=<repo>/build/bin/clang
SECRET=/path/to/firefox_taint_sources.txt
TARGETS=/path/to/harden_targets.txt   # newline list of source paths to harden

# Find the source file in the invocation.
src=""
for a in "$@"; do case "$a" in *.c|*.cc|*.cpp|*.cxx|*.m|*.mm) src="$a";; esac; done

if [[ -n "$src" ]] && grep -qxF "$src" "$TARGETS"; then
  exec "$REAL" "$@" -ftaint-harden="$SECRET"
fi
exec "$REAL" "$@"
```

Wire it up in `mozconfig`:
```
export CC=/path/to/taint_cc_wrapper.sh
export CXX=/path/to/taint_cxx_wrapper.sh   # same script; C++ mangled names in SECRET
```

(You already have `~/clang-dispatch.sh` doing the filename dispatch; its custom
branch just needs to add `-ftaint-harden="$SECRET"` to the real-clang exec.)

Alternative without a wrapper: pass `-ftaint-harden=<file>` through `moz.build`
`SOURCES['FilterNodeSoftware.cpp'].flags` (or `CXXFLAGS` for the whole TU list) for
the specific files you moved out of `UNIFIED_SOURCES`.

---

## 6. Selecting which TUs to harden

Only TUs containing (or reached from) a taint-source function need the flag;
everything else compiles normally. Keep a `harden_targets.txt` list of source paths.
Start narrow (the specific secret-handling TUs) and expand as needed.

**One TU at a time.** The analysis is interprocedural *within a TU's module*.
Cross-TU taint flow (secret defined in TU A, used in TU B) is not tracked across
object boundaries — annotate the entry function in each TU that receives the secret.

**Unified sources.** Firefox's `UNIFIED_SOURCES` concatenates several `.cpp` into one
TU. To harden a single file precisely, move it to `SOURCES` so it's its own TU (as
was done for `gfx/2d/moz.build` → `FilterNodeSoftware.cpp`).

**LTO.** `-ftaint-harden` lowers the TU to object eagerly, so it is incompatible with
LTO *for that TU*. Build hardened TUs non-LTO (they still link fine), or disable LTO
for that subset.

---

## 7. Verifying the result

```
# Barriers landed in the object:
<repo>/build/bin/llvm-objdump -d file.o | grep -E '\bmsr\b.*\bdit\b'

# Per-symbol barrier counts (to compare against the reference wrapper):
<repo>/build/bin/llvm-objdump -d file.o \
  | awk '/^[0-9a-f]+ </{s=$0} /\bmsr\b.*\bdit\b/{n[s]++} END{for(k in n)print n[k],k}'
```

Differential parity against the trusted multi-tool flow (`utils/taint_harden_c.sh`)
matches exactly on the reference input (`playground/firefox_convolve_int.c`): same
per-symbol barrier placement. The single-`clang` path is a drop-in for that wrapper.

---

## 8. Quick checklist

- [ ] Build this LLVM tree; confirm `clang --help | grep taint-harden` (Section 2).
- [ ] Write `firefox_taint_sources.txt` with **mangled** C++ names (Section 3).
- [ ] Write `harden_targets.txt` listing the source paths to harden (Section 6).
- [ ] Point `mozconfig` CC/CXX at the wrapper that appends `-ftaint-harden` (Section 5).
- [ ] Move any target file out of `UNIFIED_SOURCES` into `SOURCES` (Section 6).
- [ ] Disable LTO for hardened TUs (Section 6).
- [ ] Build; spot-check a hardened `.o` for `msr dit` (Section 7).

---

## Appendix: legacy multi-tool flow (fallback / debugging)

The original per-TU pipeline still works and is useful for inspecting intermediates.
`-ftaint-harden` performs exactly these steps in-process. Driver: `utils/taint_harden_c.sh`.

```bash
BIN=<repo>/build/bin
# 1. source -> IR (forward all of Firefox's CFLAGS here)
"$BIN/clang" -g $OPT -S -emit-llvm -fno-asynchronous-unwind-tables \
  -fno-unwind-tables $CFLAGS "$IN" -o "$STEM.ll"
# 2. annotate tainted args
"$BIN/opt" -S "$STEM.ll" -passes=taint-annotate -taint-src="$SECRET" -o "$STEM.annotated.ll"
# 3. lower to post-PEI MIR
"$BIN/llc" $OPT -stop-after=prologepilog "$STEM.annotated.ll" -o "$STEM.pe.mir"
# 3b. CFI serialization workaround (done automatically by -ftaint-harden)
perl -0pi -e 's/<mcsymbol >//g' "$STEM.pe.mir"
# 4. interproc taint + barrier insertion
"$BIN/llc" -enable-new-pm -run-taint-interproc -taint-insert-dit \
  -taint-region-merge-gap=2 "$STEM.pe.mir" -o "$STEM.hardened.mir"
# 5. hardened MIR -> object
"$BIN/llc" -start-after=prologepilog "$STEM.hardened.mir" -filetype=obj -o "$STEM.o"
```

Inspect region spacing: `utils/taint_region_distance.py "$STEM.hardened.mir"`.
Reports (`-taint-output`, `-taint-regions-output`, `-taint-source-regions-output`)
are available in the manual flow; `-ftaint-harden` currently leaves them unset.
