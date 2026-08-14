# Browser history sniffing: what was exploited, how it was patched, and whether new hardware reopens it

**Date:** 2026-08-09. Motivating question from the advisor lead: *the old Shravan
Narayan browser-history paper - what did it exploit, how did vendors patch it, and
can an emerging hardware optimization reopen that channel?* Secondary and real
goal: **find a workload where fine-grained DIT beats always-on DIT**
(see [[fastdit-thesis-status]] / `docs/results/dit-cost-model.md`).

Companion to `docs/research/value-timing-leaks.md` (which covers the SVG-filter
subnormal-FP anchor and FLOP/LVP). This doc covers the *history* half of the
browser story, which is a different secret with a very different taint shape.

---

## Bottom line (read this first)

Three findings, in order of how much they change what we do:

1. **The `:visited` mitigation browsers have shipped since 2010 rests, verbatim,
   on a data-operand-timing assumption.** The fix reduced the secret to a *color
   value* and then assumed "changing an element's color is too quick for
   JavaScript to detect." That is the browser-language spelling of "integer
   multiply is constant-time" - the exact assumption class this project attacks.
   Every history-sniffing attack since 2011 is an attack on that assumption.

2. **But the browser history channel is a poor DIT *demo* target, because every
   attack that actually broke it lives outside DIT's guarantee.** The four WOOT'18
   attacks are re-computation/event-loop channels; Pixel Thief is a cache/address
   channel; Hot Pixels is DVFS/power; GPU.zip is GPU framebuffer compression.
   `PSTATE.DIT` fixes none of them. Only the CPU-side value-dependent-arithmetic
   class (Andrysco subnormal FP, prospectively LVP / comp-simp / zero-skip) is
   DIT-coverable, and that one is about *pixels*, not history.

3. **The genuinely valuable result is the benchmark reframe, and it is a
   positive.** The reason libsodium and SQLCipher never showed a fine-grained win
   is that we were looking for headroom *inside the secret region*. That is the
   wrong place. Fine-grained DIT wins when the **public** code has headroom and
   the **secret** code has none - because then protection is free and the saving
   is the whole always-on cost. A browser is the first workload we have found with
   that shape. See "The reframe" below - this is the actionable part.

   **MEASURED 2026-08-09/10, two engines, Speedometer 3.1 on the M5: always-on
   DIT costs +2.61% (+/-0.51) in Firefox and +1.80% (+/-0.16) in Chromium, 20/20
   reps slower in both, against measured noise floors of 0.09% and 0.30%.** That
   is the budget fine-grained placement has to recover, on a workload whose
   secret fraction is tiny. Details in section 6.

---

## 1. What Narayan et al. actually exploited

**Paper:** Michael Smith, Craig Disselkoen, Shravan Narayan, Fraser Brown, Deian
Stefan, *Browser history re:visited*, USENIX WOOT'18. Four attacks, three on
`:visited` links plus one cache attack.

### The shared mechanism of the three visited-link attacks

All three are **re-paint detection**, stated by the authors as the common core:

> "by forcing the browser to re-paint according to a link's visited status and
> measuring when re-paint events occur, an attacker can learn whether or not the
> URL pointed to by the link has been visited."

The attacker plants a link to a dummy URL it knows is unvisited, then flips
`link.href` to the target URL. If the target is in history, `:visited` starts
matching, the computed style changes, and the browser must re-paint. **The secret
is not the color value - it is whether a re-paint happened at all.**

| # | Attack | Amplifier | Readout | Browsers |
|---|---|---|---|---|
| 3.1 | **CSS Paint API** (CVE-2018-6137, $2k bounty) | attacker JS runs *inside* the render pipeline as a paint worklet | event-loop timing (block 20 ms in `paint`), or the amplified `registerPaint` duplicate-identifier + `::after` width covert channel | Chrome only |
| 3.2 | **CSS 3D transforms** | `perspective()/rotateY()` + `filter: contrast/drop-shadow/saturate` + `text-shadow` + huge outline, so any re-paint is expensive | `requestAnimationFrame` callback count over a 100 ms window (4 vs 15) | Chrome, Firefox, Edge, IE |
| 3.3 | **SVG fill-coloring** | a 7,000-path SVG inside the `<a>`, with `fill` rules under `:visited` | same rAF frame-rate drop (2 vs 5 at 100 ms; 9 vs 41 at 1000 ms) | Chrome, Firefox, Edge, IE |
| 4 | **JS bytecode cache** | none - V8 persists compiled bytecode to disk keyed by script URL, shared across origins | Resource Timing (download done) vs first-global-set (execution start); cache hit is 2.5-10x faster boot | Chrome only |

Rates: the amplified Paint API attack does **3,000 URLs/sec** - the whole Alexa
Top 100k in 30-40 seconds, invisibly. That is the fastest visited-link attack
since 2010 and comparable to the ones that made vendors break web compat.

Notable evaluation results: Site Isolation does not help (`:visited` and the
bytecode cache are shared across isolation boundaries). FuzzyFox/DeterFox only cut
bandwidth 10x, because these attacks do not need fine-grained timers. **Only the
Tor Browser is immune, and only because it keeps no history.** Also, Firefox's
`layout.css.visited_links_enabled=false` did not actually stop the attacks - they
filed that as a separate bug.

**Their proposed defense** is architectural, not timing-based: associate a
referring origin with every persistent URL record (history entries, cache
entries) and only expose it to same-origin lookups. That proposal is, seven years
later, exactly what shipped - see §3.

---

## 2. The invariant the 2010 patch rests on (this is the interesting part)

After Clover's 2002 `getComputedStyle` leak and the 2010 Janc/Olejnik mass
exploitation, Mozilla (Baron/Stamm) and the other vendors shipped a three-part
mitigation, and every part is worth reading as a *constant-time-programming*
decision:

1. **Lie to JavaScript.** `getComputedStyle` always returns the *unvisited* style.
   (Removes the explicit flow.)
2. **Restrict the secret's fan-out to a value, not a shape.** `:visited` may only
   affect "foreground, background, outline, border, SVG stroke and fill colors."
   No layout, no resource loads. In constant-time terms: **the secret may not
   influence control flow or memory addresses, only a data value.**
3. **Make the two paths structurally identical.** Mozilla's blog: they modified
   the layout engine so *style resolution follows identical code paths for visited
   and unvisited states*, and cached computed styles rather than recalculating,
   "in an effort to avoid timing attacks."

Steps 2 and 3 are precisely the constant-time discipline, arrived at
independently and a decade before anyone in browsers said the words. And then
comes the load-bearing assumption, stated in the WOOT paper as the reason the fix
was believed sound:

> "The fix works because **simply changing an element's color should be too quick
> for JavaScript to detect** under normal circumstances."

That is a **data-operand timing assumption**, in exactly the same position as
Kohlbrenner & Shacham's 2017 certification of Firefox's integer filter rewrite
("standard 32-bit integer add/sub/multiply have no known timing side channels
based on operands"). Both say: *the secret now only selects a value; values are
timing-free; therefore we are done.*

The whole history-sniffing literature since is the demolition of that sentence.
Two independent ways to break it:

- **Amplify the transition.** WOOT'18 §3.2/§3.3 do not attack the value at all -
  they make the *act of changing* the value ruinously expensive (3D transforms,
  7,000-path SVG) so the re-paint is visible in the frame rate. This defeats
  assumption 3 (identical code paths) without touching assumption 2.
- **Attack the value itself.** Hot Pixels §6.3 sets `color: black` for unvisited
  and `color: white` for visited, then measures how long the *same* filter takes
  on the two colors. This is the pure data-operand attack, and it is where
  hardware comes in.

---

## 3. How it was finally patched, and by whom

| Year | Fix | Kind | Does hardware reopen it? |
|---|---|---|---|
| 2010 | `getComputedStyle` lies; `:visited` restricted to colors; identical style-resolution paths | information-flow + constant-time | **Yes** - the secret still reaches the render pipeline as a value |
| 2018 | Chrome 67 disables CSS Paint API on `<a>` and children (CVE-2018-6137 interim fix) | feature disable, one attack | n/a |
| 2018-2024 | FTZ/DAZ CPU flags enabled for filter FP; Firefox rewrites SVG filters in fixed-point integer | constant-time | **Yes** - this is the `docs/research/value-timing-leaks.md` anchor |
| 2023 | Safari: no cookies for cross-origin iframes (Intelligent Tracking Protection); Firefox: cookie partitioning | information-flow, **pixel stealing only** | n/a - but Pixel Thief notes it "does not affect the history-sniffing attack because the way links are rendered as visited or not is independent of any cookies" |
| **2025** | **Chrome 132 dev / 136 ship: `:visited` partitioned by `<link URL, top-level site, frame origin>` triple key** | **information-flow - the real fix** | **No.** The secret never enters the pipeline for a cross-site link, so no timing channel of any kind can extract it |

**Chrome's partitioning is the fix that ends the arms race**, and it is exactly
the WOOT'18 authors' proposed defense. Residual, by their own explainer: a site
may still style links to its own pages as visited (`<URL, URL, URL>` self-links),
which extends to same-origin subframes; and non-link navigations (typed URLs,
bookmarks) are simply not recorded.

**Status elsewhere as of today:** Firefox has given a positive standards signal
and named `:visited` partitioning in its privacy goals, but has not shipped.
**WebKit is on record as "no signal."** So on Safari and Firefox the 2010
constant-time-style mitigation is still the only thing standing, and the
assumption in §2 is still load-bearing.

---

## 4. Where hardware reopens browser channels - and which ones DIT covers

This is the part the lead was really asking about. Four demonstrated
hardware-driven reopenings, and the DIT verdict is negative on three of four.

| Attack | Hardware optimization | Channel class | DIT-covered? |
|---|---|---|---|
| **Andrysco et al., S&P'15** - subnormal FP in `feConvolveMatrix` | FP denormal assist path (200+ cyc vs 4) | **data-operand latency** | **YES** - FP is in DIT's covered set |
| **Hot Pixels, USENIX Sec'23** - history sniffing on Safari | DVFS: power/frequency respond to the *data* being processed | value-dependent, but analog | **No.** DIT equalizes instruction timing, not power draw |
| **GPU.zip, S&P'24** - cross-origin SVG-filter pixel stealing in Chrome | GPU lossless framebuffer compression, "data dependent, software transparent, present in nearly all modern GPUs" | value-dependent, but on the GPU | **No.** `PSTATE.DIT` is a CPU PSTATE bit |
| **Pixel Thief, USENIX Sec'24** - 267 bit/s history sniffing | data-dependent *memory access pattern* in `feComponentTransfer` (`lut[pixel]`) | **address/cache** | **No** - explicitly out of scope, see `docs/design/dit-placement.md` G2 |

Hot Pixels is the one to quote, because its history-sniffing setup is the pure
value-dependent form of the attack:

> "we use the visited selector to set the color of hyperlinks to **black for
> unvisited and white for visited** links, and subsequently apply our filter stack
> to apply stress on the CPU."

Secret bit -> color *value* -> data-dependent hardware behaviour -> timing. That
is our threat model's exact shape. Their numbers: 88.8% accuracy on M1 MacBook
Air, 94.8% on M2, 99.0-99.3% on iPhone 12/13, at ~0.1 bit/s. They note the CPU
takes much longer than the GPU to show a frequency difference (SNR), but lands at
higher accuracy. Their mechanism is DVFS, not instruction latency - **but the
identical black-vs-white color pair would drive an LVP / comp-simp / zero-skip
channel on the CPU, and Andrysco already showed on Intel that "white pixels take
longer than black pixels."**

Two quotes that matter for us, both from Pixel Thief §2.2/§9:

> "Andrysco et al. has since suggested the use of cryptographic constant-time
> programming in SVG filters to ensure that the execution time remains
> constant-time, **although no vendor seems to have employed such an approach.**"

> "For these reasons, we only recommend this solution [constant-time programming]
> to browser vendors **for any fallback filters that are executed on the CPU**."

That is a top-tier-venue recommendation for exactly what FastDIT automates, aimed
at exactly the code we can compile (CPU-path filters), on a non-crypto target,
noting that nobody has done it because doing it by hand is impractical. That is a
citation worth having in the intro.

**Honest scoping statement for the thesis:** of the browser channels that have
actually been exploited, DIT closes one class - CPU value-dependent arithmetic.
It does not close DVFS, GPU compression, or cache/address channels. Saying
otherwise is the "silent false assurance" failure mode we already flagged for
`SDIV`/FP in `docs/design/dit-placement.md` G2, and reviewers will find it.

---

## 5. The reframe: why this is still the best benchmark lead we have

**The mistake so far.** Every workload we tested (libsodium, SQLCipher, the SVG
filters, `firefox_convolve_int`) was evaluated by asking *how much headroom is
there inside the secret region?* The answer kept being "none" -
[[dit-headroom-needs-serial-chains]] explains why: filters are embarrassingly
parallel, so OoO already hides the load latency and LVP/DMP have nothing to
recover.

**That is the wrong question.** Fine-grained DIT does not win by making the secret
region fast. It wins by **not covering the public region**. Restated as an
admission test on the whole program:

> Fine-grained DIT beats always-on iff
> **(a)** the secret-touching code is a small fraction of the dynamic instruction
> stream, **(b)** the secret-touching code is itself DIT-insensitive (no headroom
> to lose, so protection is nearly free), **(c)** it is entered and exited rarely
> (few toggles), and **(d)** the *public* code is DIT-sensitive (serial
> value-dependent chains: pointer chasing, tree descent, interpreter dispatch).

Score the workloads we have:

| Workload | (a) small secret | (b) secret DIT-insensitive | (c) few toggles | (d) public has headroom | Result |
|---|---|---|---|---|---|
| libsodium primitives | no - secret is ~everything | yes (+0.1%) | n/a | n/a | no win, measured |
| SQLCipher | **yes** - key reaches only 2 functions | yes | no - ~256 DIT regions/page | **NO - headroom is +0.89% (software crypto) and ZERO on the shipping hardware-AES build** | no win, measured: there is no prize to collect - see `docs/results/sqlcipher.md` |
| `firefox_convolve_int` | no | yes | no - 19 `MSR DIT`/pixel | **no** (0.968x whole-program) | no win, measured |
| **Browser + `:visited` / password field** | **yes** - one bit per link, one field | **yes** - paint is parallel, so DIT there is free | **yes** - a repaint, not an inner loop | **yes - FLOP measured 4.5% on Speedometer 3.0** | **untested, and the shape is right** |

The browser is the first workload that scores yes on all four, and every "yes"
comes from a published measurement rather than a hope.

**Why the `:visited` secret is unusually good for *taint*, specifically.** The
reason SQLCipher's ratio collapses is taint spread: the key flows into buffers,
page caches, and then through context-insensitive mod-sets into everything
(`docs/design/context-insensitivity.md`: 169 of 199 FPs). The visited bit is the
opposite - **1 bit, produced by one lookup, consumed immediately into a color,
never stored, never fanning out.** It is the most taint-friendly secret this
project has encountered. Same for a password-field buffer. That is a structural
argument, not a hope, and it is worth making in the paper even independent of the
measurement.

---

## 6. What to actually run (in order, cheapest first)

**Step 1 - reproduce the always-on number on our own silicon. DONE 2026-08-09,
and it reproduced.**

**Always-on PSTATE.DIT costs a real several percent on Speedometer 3.1 on the M5,
in BOTH engines tested.** Rig: `utils/taint_browser_dit_bench.sh`. Each engine:
20 reps x 3 interleaved arms, 60/60 runs good, paired by rep.

| engine | DIT cost (null -> dit) | reps slower | harness floor (base -> null) |
|---|---|---|---|
| **Firefox** Nightly 153.0a1 | **+2.61% +/- 0.51** | **20/20** | -0.09% +/- 0.58 (10/20) |
| **Chromium** 153.0.8001.0 (r1676489) | **+1.80% +/- 0.16** | **20/20** | +0.30% +/- 0.14 (17/20) |

Per-arm medians, Firefox: base 41.369, null 41.323, dit 40.277.
Chromium: base 46.621, null 46.464, dit 45.634.

**The two engines genuinely differ** (Welch t = 3.17 on the paired
distributions; CIs [2.10, 3.11] vs [1.64, 1.96] do not overlap). So the cost is
not a fixed property of the machine - it depends on how much DIT-gated
optimization the engine's own code was getting. Both are nonetheless far above
their measured noise floors.

**The null arm earned its place on Chromium.** Chromium shows a real +0.30%
harness cost (17/20 reps), where Firefox's was a clean null. Without a null arm
Chromium's headline would have read +2.11% instead of +1.80% - a sixth of the
effect would have been the interposer's own trampoline and malloc traffic.

Why this is trustworthy:
- **20/20 reps slower in both engines** - sign test p ~ 1e-6 each. Not marginal.
- **Every content process covered, every run**: 8 `plugin-container` for Firefox,
  4 `Chromium Helper (Renderer)` for Chromium, all `main_dit=1`. Speedometer runs
  there, so that is the coverage that matters. Thread coverage 100% / 97%.
- The harness cost is measured rather than assumed (see the null arm note above).
- **No thermal drift**: canary spread 1.2% across the sweep; first-vs-last-10
  medians move -0.15%/+0.05%/-0.56%.
- FLOP's Safari/Speedometer 3.0 figure was 4.5%. Neither of ours is a
  reproduction of that number - different engines, different Speedometer version
  - but two independent engines both landing in the 1.8-2.6% band is a stronger
  claim than either alone: always-on DIT costs a real, several-percent amount on
  browser workloads, and FLOP's 4.5% is the same order.

**One real caveat: libdispatch worker threads never get DIT.** They come from
`_pthread_workqueue`, not `pthread_create`, so the interposer misses them
(verified directly: a `dispatch_async` worker reads `DIT=0` under injection).
Measured coverage is ~97-100% of threads in Firefox, so the effect is small, but
**both numbers are therefore lower bounds.**

**A second "caveat" was reported on the first pass and is RETRACTED.** The
original canary appeared to show `MSR DIT, #1` intermittently failing to gate
the LVP (~9% of rounds). It does not. Chased down 2026-08-09:

- `ISB` after the `MSR` changes nothing (bare 2/200 and 1/200 vs isb 2/200 and
  4/200), so it is not a context-synchronization problem. Worth knowing anyway,
  because `insertTimingModeSwitch` emits a bare `MSR DIT` with no barrier and
  that turns out to be fine.
- Across **800 measured rounds in 40 separate processes, the DIT-on time never
  once fell below 0.868 ns/hop** - DIT slowed the chase every single time.
- The DIT-off *baseline*, meanwhile, was seen as high as 0.40 ns/hop against a
  0.217 floor. The apparent flake was entirely noise in the ratio's denominator.
- Root cause in our own code: `dit_effect` divided by a per-round baseline. Fixed
  by taking the minimum for the fast baseline (noise only ever inflates) and by
  adding `dit_lands_on_perm`, which checks the invariant that actually matters -
  with DIT set the const chase should land on the plain L1 load-to-use line.
  Measured **0.997 to 1.003 across 25 processes.**

**Lesson, worth generalising: gate on absolute times, never on a ratio whose
denominator is the fast measurement.** A noisy denominator manufactures exactly
the kind of alarming intermittent "security" result that is hardest to
disbelieve.

**Step 1b - other engines. Why not Safari, and what we do instead.**

**Shipped Safari cannot be measured this way, for two independent reasons.**
Both were checked on this machine (2026-08-09):
1. `Safari.app` carries codesign `flags=0x2000(library-validation)`, so it
   refuses to load an ad-hoc-signed dylib no matter what. SIP being disabled
   (it is, on this machine) does not help; that would need an
   `amfi_get_out_of_my_way=1` boot-arg, or re-signing Safari, which strips the
   private entitlements it needs to run.
2. More fundamental: Speedometer runs in `com.apple.WebKit.WebContent`, which
   WebKit launches as an **XPC service via launchd**, not `fork`+`exec` from the
   UI process. XPC services do not inherit the parent environment, so
   `DYLD_INSERT_LIBRARIES` would never reach the process that matters - unlike
   Firefox's `plugin-container` and Chromium's renderers, which do.

The apples-to-apples route to Safari's engine is therefore a **WebKit source
build driven through MiniBrowser** (`BROWSER=minibrowser`, already wired up).
That is what FLOP did - they patched WebKit, not the shipped app. Cost here is
high because `/Applications/Xcode.app` is a **0-byte stub** on this machine and
`build-webkit` requires full Xcode: ~15 GB Xcode + ~15 GB WebKit clone + a 1-3 h
build. Deferred.

**Chromium is the cheap third engine and needs no build at all.** Prebuilt
arm64 snapshots (`commondatastorage.googleapis.com/chromium-browser-snapshots/
Mac_Arm/`) are ~170 MB and **ad-hoc/linker-signed**, so unlike shipped Chrome
they have no library-validation flag; renderers are fork+exec'd and inherit the
injection. Verified working: 13 processes, 4 `Chromium Helper (Renderer)`, all
`main_dit=1`, 90% thread coverage. `BROWSER=chromium`, fetched by
`utils/taint_browser_dit_bench.sh fetch-chromium`.

Chromium also answers a question Safari would not: whether +2.61% is
engine-specific or a general property of browser workloads. And it is the engine
that actually shipped `:visited` partitioning (section 3).

**Step 1d - is the cost a parallelism artifact? No (measured 2026-08-10).**

| condition | DIT cost | base median | throughput vs baseline |
|---|---|---|---|
| Firefox, default | **+2.61% +/- 0.51** | 41.37 | - |
| Firefox, `SINGLE=1` | **+2.35% +/- 0.45** | 33.47 | **-19.1%** |
| Chromium, default | +1.80% +/- 0.16 | 46.62 | - |

Welch t = 0.78 between the two Firefox conditions: **statistically
indistinguishable**. `SINGLE=1` removed about a fifth of Firefox's throughput
worth of parallelism and the DIT cost did not move. So the cost sits on the
renderer's own critical path, not in cross-thread or scheduling effects - which
is exactly where taint-driven placement would have to recover it.

**Caveat on what `SINGLE=1` actually did to Firefox.** It is "much less
parallel", not "single-threaded". `dom.ipc.processCount=1` did NOT collapse the
content processes - still 8, same as the default run. The 19.1% throughput drop
came from the intra-process knobs: `STYLO_THREADS=1` (Gecko's parallel style
system) and disabling off-thread JIT compilation and parallel parsing. Chromium
needed `--single-process` for the same reason: `--renderer-process-limit=1` is a
soft cap that it ignored (still 4 renderers), and `--process-per-site` made it
worse (7).

**Step 1e - renderer-only, the FLOP-faithful methodology. MEASURED 2026-08-10.**

| condition | DIT cost | noise floor |
|---|---|---|
| Firefox, all processes | +2.61% +/- 0.51 | -0.09% |
| Firefox, all processes, `SINGLE=1` | +2.35% +/- 0.45 | -0.08% |
| **Firefox, renderer only** | **+2.45% +/- 0.33** | -0.05% |
| Chromium, all processes | +1.80% +/- 0.16 | +0.30% |
| **Chromium, renderer only** | **+2.12% +/- 0.11** | +0.02% |

All five: 60/60 runs good, 20/20 reps slower, canary `dit_effect` 4.00 with zero
samples below threshold.

**Confining DIT to the renderer does not reduce the cost.** Firefox
all-process vs renderer-only is indistinguishable (Welch t = 0.54). Chromium's
renderer-only reads *higher* (+2.12 vs +1.80, t = -3.40), i.e. the opposite of
the expected direction - see the caveat below before believing that difference.

That is the substantive result for this project: **essentially all of the cost is
inside the renderer**, so process-granular narrowing buys nothing. Recovering any
of it requires narrowing *within* the renderer, which is exactly what taint-driven
placement does and what FLOP's own follow-on recommendation asks for ("setting the
DIT bit when executing user-supplied JavaScript or WebAssembly, and on sensitive
DOM operations such as password fields").

It also makes our numbers methodologically comparable to FLOP's for the first
time: they measured Safari renderer-only at 4.5%; we measure 2.45% (Firefox) and
2.12% (Chromium) renderer-only. Different engines, same order, ours lower.

**Caveat on cross-sweep comparisons.** The within-sweep CIs (+/-0.11 to +/-0.51)
do NOT license comparing two separately-run sweeps at that precision: they
capture rep-to-rep variation only, not sweep-to-sweep machine state. Direct
evidence that state differed between the two Chromium sweeps: the `base` arm's
median moved 46.62 -> 46.37 (-0.5%), which is larger than the +0.32 point
difference being interpreted. Treat the Chromium all-vs-renderer gap as
suggestive, not established. Within a single sweep, pairing by rep makes the
arm comparison sound; across sweeps it does not.

**Original methodology note.** 
FLOP section 7, verbatim: *"we patched Safari to set the DIT bit **in the
rendering process** and observed that such a mitigation results in an overhead of
4.5% on the Speedometer 3.0 benchmark"*. Our default arms set DIT in **every**
process, so they are an upper bound relative to that. `RENDERER_ONLY=1` matches
the paper: `DIT_ONLY_PROG` confines DIT to `plugin-container` /
`Chromium Helper (Renderer)` / `WebContent`. Verified on Firefox: 8 renderers at
`main_dit=1`, parent UI process / GPU helper / crashhelper all at `main_dit=0`.
Processes excluded this way report `applies=0` so the report can tell a
deliberate opt-out from a coverage failure.

**Why shipped Safari still cannot be measured (refined 2026-08-10).** The earlier
note blamed library validation and XPC. The real picture, tested:
- The first blocker is **architecture**, not signing: Apple platform binaries are
  arm64e and reject a plain arm64 dylib ("incompatible architecture"). Rebuilding
  the dylib `-arch arm64e` fixes that, and it **does** then inject into a platform
  binary - verified on `/bin/echo` (`flags=0x0`), which set DIT successfully.
- The remaining blocker is real: **`Safari.app` and
  `com.apple.WebKit.WebContent` both carry `flags=0x2000(library-validation)`**,
  which AMFI enforces independently of SIP (SIP is already disabled here and it
  is not sufficient). Bypassing needs the `amfi_get_out_of_my_way=1` boot-arg and
  a reboot.
So Safari is now a **one-boot-arg question**, not a WebKit-build question - much
cheaper than previously recorded, though it is a machine-wide security change.
`setup` pre-builds `dit_{on,off}_arm64e.dylib` for that path.

**Step 1c - core type: the LVP is a P-core feature (measured 2026-08-10).**
On the `canary.c` const chase, setting DIT slows the predictable chase by:

| core type | DIT slowdown on the const chase | notes |
|---|---|---|
| P-core | **+300%** (0.217 -> 0.870 ns/hop) | the 4.01x LVP ratio |
| E-core | **+18.6%** median, [+1.4, +43.2] over 6 runs | 6/6 slower, but noisy |

So E-cores have far less DIT-gated optimization, not none. Matches FLOP scoping
their LVP claim to "the M3 CPU's P-cores".

**Consequence: the E-core browser arm is NOT worth running.** It is a negative
control whose answer is already known (less optimization to disable -> less
cost), it costs ~1 hour because E-cores are ~4-5x slower, and E-core timings are
noisy enough to blunt it. The `null` arm already controls for the thing that
actually needed controlling - harness overhead. `ECORE=1` remains implemented
(`utils/browser_dit/ecore_exec.c`) if a negative control is ever wanted.

One trap it did surface, worth keeping: **`taskpolicy -b <browser>` silently
defeats the whole measurement.** `/usr/sbin/taskpolicy` is an Apple platform
binary, so dyld strips `DYLD_INSERT_LIBRARIES` across it and the browser runs
uninjected - observed as `procs=0` and a timeout. Without the timeout it would
have read as "DIT is free on E-cores" when DIT was never enabled. Same root cause
as the shipped-Safari blocker. `ecore_exec` is ad-hoc signed so the insert
survives the exec.

**Step 2 - the tractable proxy, if the full-browser build is too heavy
(days).** `-ftaint-harden` cannot instrument a browser as-is: interproc scope is
one TU, cross-TU taint is not tracked, and the flag is LTO-incompatible. A
browser has thousands of TUs. The proxy that keeps the *shape* while sidestepping
the limitation is **QuickJS**: the interpreter is one enormous translation unit
(`quickjs.c`), so whole-program taint works with no cross-TU story at all, and
interpreter dispatch plus property-lookup chains are precisely the serial
value-dependent code where LVP/DMP pay. Run a JS benchmark with a small
secret-handling routine embedded, and compare always-on vs taint-driven region
DIT. This is the honest stand-in for "browser-shaped workload."

**Step 3 - the security demo, if we want the offense half.** Take Hot Pixels'
setup verbatim (`a { color: black } a:visited { color: white }` + CPU-path filter
stack) and show it leaks through an *instruction-timing* channel rather than
DVFS, on M4/M5 where the LVP exists, then show FastDIT closes it. Note the risk:
per [[dit-headroom-needs-serial-chains]] the filter is parallel, so the LVP
probably will not bite there - the more likely carrier is comp-simp / zero-skip on
the black (all-zero operand) vs white blend, which we would have to model in gem5
rather than observe on silicon. Treat this as the gem5 arm, not the silicon arm.

---

## 7. What this rules out

- **Do not target the WOOT'18 attacks themselves.** They are re-paint /
  event-loop channels. DIT is irrelevant to them, and Chrome's partitioning has
  already killed the class.
- **Do not claim DIT mitigates history sniffing.** It mitigates one CPU-side
  value-dependent-arithmetic sub-case. Hot Pixels (DVFS), GPU.zip (GPU
  compression) and Pixel Thief (cache) all survive DIT untouched.
- **Do not port more filter code hoping for headroom.** Settled in
  [[dit-headroom-needs-serial-chains]]; this doc does not change it. The browser's
  headroom is in DOM/JS, not in paint.

---

## Sources

Primary:
- Smith, Disselkoen, **Narayan**, Brown, Stefan, *Browser history re:visited*,
  USENIX WOOT'18 - https://cseweb.ucsd.edu/~dstefan/pubs/smith:2018:browser.pdf
- Stamm, *Plugging the CSS history leak*, Mozilla Security Blog, 2010-03-31 -
  https://blog.mozilla.org/security/2010/03/31/plugging-the-css-history-leak/
  (and Baron, *Preventing attacks on a user's history through CSS :visited
  selectors* - https://dbaron.org/mozilla/visited-privacy)
- Taneja, Kim, Xu, van Schaik, Genkin, Yarom, *Hot Pixels*, USENIX Sec'23 -
  https://www.usenix.org/system/files/usenixsecurity23-taneja.pdf (§6.3 is the
  history-sniffing attack; §8 the mitigations)
- O'Connell et al., *Pixel Thief: Exploiting SVG Filter Leakage in Firefox and
  Chrome*, USENIX Sec'24 -
  https://www.usenix.org/system/files/usenixsecurity24-oconnell.pdf
- Wang, Taneja, Genkin et al., *GPU.zip*, IEEE S&P'24 -
  https://www.hertzbleed.com/gpu.zip/
- Chrome, *Partitioning :visited links history* explainer -
  https://github.com/explainers-by-googlers/Partitioning-visited-links-history
  (ship: blink-dev Intent to Ship, Chrome 132 dev / 136 stable, April 2025)
- WebKit standards position (no signal) -
  https://github.com/WebKit/standards-positions/issues/363 ; Mozilla position
  (positive) - https://github.com/mozilla/standards-positions/issues/1040

Already catalogued in `docs/research/value-timing-leaks.md`: Andrysco et al.
S&P'15 (subnormal FP), Kohlbrenner & Shacham USENIX'17 (the integer rewrite and
its certification), FLOP USENIX Sec'25 (LVP on M3/M4, DIT disables it, 4.5% on
Speedometer 3.0 in Safari).
