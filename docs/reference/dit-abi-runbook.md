# Running the callee-saved PSTATE.DIT ABI

How to build it, how to invoke it, what to expect in the output, and what a gem5
run sees. The contract and the reasoning behind each decision are in
[`docs/design/dit-abi.md`](../design/dit-abi.md); this file is operational only.

**Status: opt-in, `-ftaint-dit-abi` defaults OFF.** Without it, codegen is what it
was before the ABI landed (verified: the non-LTO Bitcoin Core baseline is 95
switches, exactly the pre-ABI figure).

---

## 1. Build the compiler

```
ninja -C build
```

**Build everything, not a target list.** The taint analysis is linked into three
things you will use - `clang`, `llc`, and **`libLTO.dylib`** - and `ninja -C build
clang llc` leaves libLTO stale. It does not error; the LTO link silently runs the
*old* analysis. That cost two 50-minute measurement builds on 2026-08-30, twice
producing a verifier failure that had already been fixed.

A targeted build must therefore name `LTO` explicitly:

```
ninja -C build clang llc LTO llvm-ar llvm-ranlib llvm-objdump
```

macOS: create `build/bin/clang.cfg` once per build dir (see `CLAUDE.md`).

---

## 2. Compile with the ABI

### Non-LTO

```
build/bin/clang -O2 -ftaint-harden=<seeds.txt> -ftaint-dit-abi -c file.c -o file.o
```

`-ftaint-dit-abi` is a **driver flag**, not `-mllvm`. It does two things that must
stay in step: it turns on the backend option, and it implies
`-fno-optimize-sibling-calls`. `-mllvm -taint-dit-abi` alone gives you the ABI
*without* the tail-call disable, which is an incomplete configuration - a tail
call is an exit with no epilogue, so the callee cannot restore DIT there. Use the
driver flag unless you are deliberately A/B-ing the backend option.

### Full LTO

```
CC=build/bin/clang CXX=build/bin/clang++ cmake -S . -B build-lto -G Ninja \
  -DCMAKE_C_FLAGS="-flto -ftaint-harden=<seeds.txt> -ftaint-dit-abi" \
  -DCMAKE_CXX_FLAGS="-flto -ftaint-harden=<seeds.txt> -ftaint-dit-abi" \
  -DCMAKE_EXE_LINKER_FLAGS="-flto -Wl,-lto_library,$PWD/build/lib/libLTO.dylib" \
  -DCMAKE_AR:FILEPATH=$PWD/build/bin/llvm-ar \
  -DCMAKE_RANLIB:FILEPATH=$PWD/build/bin/llvm-ranlib
```

Three things are load-bearing and each one has cost a wasted build:

- **`llvm-ar` / `llvm-ranlib`, not `/usr/bin/ar`.** The system archiver does not
  index bitcode members, so the link dies in thousands of undefined symbols after
  the full compile. (The *non-LTO* build wants the system archiver; do not copy
  its settings.)
- **`-Wl,-lto_library,.../libLTO.dylib`** pointing at *this* build, or the linker
  uses the system one and the pass never runs.
- **The request travels in the module**, not in the flags. `-ftaint-dit-abi` is
  recorded as a module flag at compile time because codegen happens inside libLTO,
  driven by the linker, which never sees clang's CodeGenOptions. If you see a
  hardened LTO binary with **zero `mrs DIT`**, that is this channel broken - the
  build still pays the tail-call disable and gets no carrier.

LTO forces single-partition codegen (the analysis needs the whole module), so the
link is single-threaded. Measured on Bitcoin Core's `bench_bitcoin`: 29 min
compile at `-j9`, then a 20 min link.

---

## 3. Read the output

```
build/bin/llvm-objdump -d a.out | grep -icE '\bmsr\b.*\bdit\b'      # mode switches
build/bin/llvm-objdump -d a.out | grep -icE '\bmrs\b.*\bdit\b'      # carrier reads
build/bin/llvm-objdump -d a.out | grep -icE 'tbnz.*#0x18'           # guarded exits
build/bin/llvm-objdump -d a.out | grep -icE '\bmsr\b.*\bdit\b, *x'  # exact exits
```

`-i` is required: objdump prints the operand uppercase.

| count | meaning |
|---|---|
| `mrs DIT` | one per instrumented function - the carrier read |
| `tbnz ..., #0x18` | exits where DIT was provably set, so the cheap guarded clear applies |
| `msr DIT, x<N>` | exits where it was not, so the unconditional restore is used |
| `msr DIT, #1` / `#0` | entry enables and clears |

Reference figures, Bitcoin Core `bench_bitcoin`, 2026-08-30:

| build | switches | notes |
|---|---|---|
| non-LTO baseline | 95 | 76 set / 19 clear |
| non-LTO + ABI | **57** | 28 set / 21 guarded / 8 exact, 16 carrier reads |
| full-LTO baseline | 127,740 | 116,611 set / 11,129 clear |

**A `tbnz` on `w`, not `x`.** `TBNZX` hard-codes b5=1, so `TBNZX ..., 24` tests bit
**56** and the guard never fires - which makes the function clear unconditionally
and strip its caller. The asm printer shows the raw operand either way, so check
the disassembly, and pin the register *width* in any test.

### Reports

```
-mllvm -taint-nonlocal-report=<file>   # where the obligation degrades
-mllvm -taint-dit-reassert-report=<file>
-mllvm -taint-callsite-report=<file>   # ESCAPE / DITLEAK
```

`NONLOCAL` lines name the sites where DIT is simply left set: `setjmp`,
`musttail`, `unwind`, `noscratch` (no free scratch register at an exit), and
`nocarrier` (no usable frame slot - VLA functions, and the `llc` path). All are
dwell, never exposure. A `DITLEAK tailcall` line in an ABI build means `musttail`
or the MachineOutliner, both of which survive the tail-call disable.

---

## 4. What gem5 sees

Relevant if you are running this against a modified switch model.

**The common case is the instruction you already model.** Entry enables and
guarded clears are `MSRpstateImm4` - the immediate form, `msr DIT, #imm`. On the
Bitcoin non-LTO build that is 49 of 57 switches.

**The exception is `MSR DIT, Xt`**, the unconditional restore, used only at exits
where the analysis could not prove DIT was set (8 of 57 above). It decodes to
`Msr64`, which is `IsSerializeAfter, IsNonSpeculative` and **not renamed** in
stock gem5, so those exits will look far more expensive there than on silicon.
Measured on M5 the two forms cost the same to within 0.03 cyc, so a gem5 run
without a model change **overstates** the ABI's exit cost. If you have made the
register form renameable, say so when quoting numbers.

**New non-switch traffic**, which a switch-cost model will not capture: one `MRS`
per instrumented function (1.00 cyc on M5), one store and one reload through a
frame slot, and one `TBNZ` per guarded exit. The carrier slot also gives every
function in a hardened module a frame, so a previously frameless leaf gains a
`sub sp` / `add sp` pair.

**Shrink wrapping is disabled** under `-ftaint-dit-abi`, which changes early-exit
paths. **Tail calls are disabled TU-wide**, which converts tail calls into real
calls and returns and also disables tail-*recursion* elimination - a tail-recursive
function gets O(n) frames. Both are ABI-only; neither applies to a baseline arm.

**For a fair A/B**, build both arms with the same compiler and vary only
`-ftaint-dit-abi`. Do not compare against a historical number: the baseline moved
twice during development, and both times it was a bug in the arm, not a change in
the workload.

---

## 5. Known limitations

- `-taint-dit-abi` is opt-in and unproven on a real workload. On non-LTO Bitcoin
  Core signing it is within noise of the baseline: 40% fewer switches, no
  measurable time difference, because the carrier costs back what the deleted
  re-asserts save.
- A function that cannot carry (VLA, no free scratch register, the `llc` entry
  point which reserves no slot) falls back to whole-function coverage that never
  clears. Safe, costs dwell, reported as `NONLOCAL nocarrier`.
- `musttail` and `MachineOutlinerTailCall` survive the tail-call disable.
- EH unwind and `longjmp` cannot restore and are reported, not fixed.
