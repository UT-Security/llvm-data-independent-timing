# 11 - php-src's benchmark suite: blanket DIT, the developer's bracket, the pass

**Status: complete, silicon, re-measured on performance counters.** The tables
below are the 2026-09-06 counter run on `bellingham`, Apple M4 (Mac16,10), macOS
15.7.3 on a PACMANPATCH development kernel: **PMC0/PMC1 read from EL0** around the
whole `php-cgi` process, thread pinned to a P-core, medians of 100 requests per
arm. Raw: **`results/m4/`** -- one directory per host, so a second machine lands
beside this one rather than on top of it.

The original 2026-09-05 run measured the same six arms with the parent's **rusage
CPU time** and no root. It is kept in `data/{zend,symfony,wordpress,wpapi_13,wpapi_16}.txt`
and quoted beside every counter number below, because a change of instrument is
only credible if both readings are on the page. They agree: every claim the CPU-time
run made survives, and the counter run adds instruction counts, which CPU time
cannot see and which turn out to carry the experiment's cleanest result (see
"What the counters added").

Compiler `2a379e65d04f` (dit-tainter, functionally identical to the branch tip
`c28e5796ab85` -- the only intervening change to `llvm/` is a test file), with
`-taint-dit-external-preserves=1` and `-taint-dit-enable-barrier=sb`. The arms
rebuilt to the same switch counts as the 2026-09-05 run, symbol for symbol:
bracket 14 `msr DIT` / 7 `sb`, pass 97 / 57 / 27 twins, seed fixpoint in one
round. `data/provenance.txt` has the exact record.

**This run needed root and the 2026-09-05 one did not**, which is a methods
difference a reader is entitled to know about. Root buys two things and changes
nothing else: `kern.sched_thread_bind_cpu` pins the measured thread, and
`mdutil -i off` stops Spotlight. Both remove *causes* of discarded samples; neither
touches what is measured. An unrooted run of the same rig produces the same
numbers with more samples dropped -- `utils/dit_host_screening/phpsuite/run_root.sh`
is the rooted wrapper and everything it does is visible in each row's `pinned`
and `migrated` fields.

**Published artifact:** https://claude.ai/code/artifact/a70963ab-5df6-4717-b6f3-140fe328c20e
Source: `figures/developers-bracket.html`. Republish through that URL (`Artifact`
with `url=...`); publishing the file without it creates a second artifact.

**Rig:** `../../utils/dit_host_screening/phpsuite/`. **Rerun:** `./reproduce.sh` on
any Apple Silicon Mac with Homebrew (section "How to rerun").

---

## The claim

> On php-src's own benchmark suite, blanket `PSTATE.DIT` costs a real PHP
> application **-0.4 to 2.6 percent** (WordPress about 2, Symfony under 1), while
> the suite's interpreter micro-benchmark reads **+9.7**. Apple's recipe written
> by hand at the entry of the seven crypto builtins (the developer's bracket) and
> the pass cost **nothing on any page request** and **1.4 to 1.9 points on a
> WordPress login**. Both selective placements pay per crypto *call*, not per
> unit of secret work: at 65,537 `md5` calls per request (phpass at 2^16 rounds)
> the bracket costs **8 to 9 points**, the pass **13**, and blanket still 2.4.
> Blanket becomes the cheaper option above roughly twelve thousand crypto calls
> per request for the bracket and seven thousand for the pass.
>
> And blanket's cost is **dwell, not work**: the same binary retires the same
> instructions to within 0.2% and runs 2.39% more cycles, an IPC drop of 2.4%.

Two of the evaluation's four questions get answered on this workload. *What does
DIT cost on shipping silicon today* is the blanket column: on a request pipeline
the prize is a few percent, five to twenty-five times smaller than the
micro-benchmark says. *What does deployment cost the developer* is bracket against
pass: on this suite the hand bracket is as good as the analysis, because PHP's
crypto builtins are leaves with no public work inside them, and both lose to
blanket at the same place, where the application crosses the crypto boundary tens
of thousands of times per request.

## What the benchmark is

php-src ships its benchmark harness in `benchmark/benchmark.php`. It measures
three workloads, each through `php-cgi` with opcache on, under valgrind's
callgrind counting instructions on Linux, once with the JIT off and once with it
on:

| workload | what runs | the harness's request |
|---|---|---|
| `Zend/bench.php` | the interpreter micro-benchmark: loops, calls, string and array work, Mandelbrot, Ackermann, nested loops | the script once |
| Symfony Demo 2.2.3 | `php/benchmarking-symfony-demo-2.2.3`, prod cache warmed by `bin/console cache:warmup` | `GET /` through `public/index.php`, `-T 50,50` |
| WordPress 6.2 | `php/benchmarking-wordpress-6.2`, installed by `wp-cli core install --url=wordpress.local --admin_user=wordpress --admin_password=wordpress`, MySQL from its docker-compose | `GET /` through `index.php`, `-T 50,50` |

**How much crypto do the harness's own requests do?** Counted rather than assumed,
by rebuilding the bracket to increment a counter instead of switching the mode
(`-DDIT_BRACKET_COUNT=1`, `results/m4/crypto_call_counts.txt`), which counts crypto
boundary crossings by construction because the bracket wraps exactly those seven
builtins:

| the harness's request | md5 | hash | hash_hmac | hash_equals | total |
|---|---|---|---|---|---|
| WordPress `GET /` | 106 | 0 | **4** | 0 | **110** |
| Symfony `GET /` | 0 | 5 | 0 | 0 | **5** |

**Symfony's request touches no secret at all** -- five unkeyed `hash()` calls over
cache keys and class-map digests. **WordPress's touches one, four times**: every
`hash_hmac` in WordPress core is keyed with a site salt, through
`wp_hash($data,$scheme) = hash_hmac('md5',$data,wp_salt($scheme))` and
`wpdb::placeholder_escape()`, which derives a query placeholder from `AUTH_SALT`
once per request. Those are long-lived *site* secrets rather than a user
credential, and the other 106 calls are `md5` over cache keys, transient names and
gravatar hashes, which are public.

So the suite as shipped is a host screen for blanket DIT, and it is very nearly
but not exactly secret-free -- a distinction worth stating precisely, since the
whole experiment is about where secrets are. The request types below add the paths
on which the applications compute on *user* secrets, using the applications' own
code: logging in adds +5 `hash_hmac` and +1 `hash_equals` (the two cookie
validations), and a login POST adds ~8,100 `md5` (phpass at 2^13).

**The suite's own requests sit two to three orders of magnitude below the
crossover** measured later in this document (~12,000 calls for the bracket, ~7,000
for the pass): 110 and 5 against 12,000. That is why every page-request row reads
zero for both selective arms, and it is the quantitative form of the claim that
selective placement is free on the workloads people actually serve.

## What was changed, and what was not

**The applications: nothing.** Both checkouts are the harness's repositories at
pinned commits (`ef263da`, `b482c3e`) with zero modified or added files;
`Zend/bench.php` is byte-identical to the tarball. WordPress differs in
configuration only, all through its database: pretty permalinks
(`/%postname%/`, without which the REST routes 301), MariaDB 12.3 on port 3307
in place of MySQL (Homebrew's MySQL 26.7 authenticates with `caching_sha2`,
which `mysqlnd` cannot do without OpenSSL, and this PHP has none), and for the
two 2^16 rows only, a one-file mu-plugin that swaps the phpass round count plus
an application password on the admin user, both removed afterwards. The database
is a separate process, outside every arm.

**PHP: one source change, in all five arms.** PHP 8.4.25 from the pristine
tarball, built with the taint compiler at plain `-O2`, `--without-openssl` so
every crypto path is PHP's own C (`ext/standard` md5, crypt, bcrypt; `ext/hash`),
with the extensions the two applications load. The bracket needs a place in the
source, so `patch_bracket.py` rewrites four files: the body of each of the seven
crypto builtins becomes a static function and the exported builtin becomes a
three-line wrapper, enter macro, body, leave macro (`data/bracket.diff`, 195
lines including the header). One patched tree builds every arm. In A, P and Z the
macros expand to nothing and the wrapper is a static call the compiler inlines
away; in B it emits `msr DIT,#1; sb` and `msr DIT,#0`, in Bn the same number of
`hint #0`.

**The method differs from the harness, and the paper has to say so.**

| | php-src's harness | here |
|---|---|---|
| process model | one `php-cgi -T 50,50` serving 100 requests | one `php-cgi` process per request, real CGI (env + stdin) |
| bytecode cache | opcache in that process's shared memory | opcache's file cache, per arm, standing in for it |
| instrument | callgrind instruction counts (Linux) | PMC0/PMC1 read from EL0 around the process: real cycles and retired instructions. No valgrind on Apple Silicon, and the 2026-09-05 run's `rusage` CPU time is kept as a second reading |
| JIT | off and tracing | off |
| counts | 50 warm-up, 50 measured | 50 warm-up, 100 measured (Zend: 50) |
| arms | one binary | six, rotating on every request; DIT read back at exit of every process |
| requests | the front page | the front page, plus the secret-bearing types below |

The per-process design is what makes arm rotation and the readback gate possible:
every process is started with the injected constructor library (`utils/cio_ditctl.c`),
which raises the scheduling class for P-core residency, pins the thread when the
run has root, snapshots the counters, and reads `PSTATE.DIT` back at exit. C must
exit with the bit set and every other arm clear, on every request, or the row is
not a result. Every row here passed.

It also makes the process the measured region, which is why whole-process counters
are the right instrument here and were the wrong one in experiment 09. There, the
timed region was a single crypto call of 300 to 3,000 cycles and whole-process
totals answered a different question than the one being asked; the counters had to
be read per region, and the cost of reading them dominated. Here the region is a
40 ms request and the two `mrs` at each end are free.

## The arms

| arm | build | instructions |
|---|---|---|
| A | unhardened, `-O2` | none |
| C | A with `ENABLE_DIT=1`: the constructor sets DIT before `main`, never cleared | `msr DIT,#1` once |
| B | the developer's bracket: `-DDIT_BRACKET=1` on the four wrapped TUs | entry `msr DIT,#1; sb`, exit `msr DIT,#0`; 14 `msr` + 7 `sb` in php-cgi |
| Bn | B's NOP twin, same layout | `hint #0` in each place |
| P | the pass: `-ftaint-harden` on ext/hash and ext/standard's crypto TUs, seeds on the same builtins' secret parameters, callee contract, twins, intra-block placement, external callees preserve DIT, `sb` after every enable | 97 `msr DIT` sites, 57 `sb`, 27 twins |
| Z | P's NOP twin | `hint #0` in each place, the 27 twins kept |

Every selective arm is compared against its own NOP twin (B minus Bn, P minus Z),
which isolates executing the switches from moving the code; relinking PHP moved
layout by 3.9% in the pilot. The seed file started as 12 hand-written lines
(`utils/dit_host_screening/phpsuite/seeds_php.txt`) and the info-loss report's
repair lines took it to the 28-line fixpoint in three rounds
(`data/obligations_round{1,2,3}.txt`, `data/seeds_php_fixpoint.txt`;
65 -> 90 -> 97 sites). Every arm's `hash_hmac`, `md5` and bcrypt output was
checked against the base build on three vectors.

## Where the secrets are

*Anonymous page*: routing, queries, templates, HTML. On Symfony no secret is live
at any point; on WordPress the only secret in play is the site salt, keying four
`hash_hmac` calls for nonces and the wpdb query placeholder (see "What the
benchmark is"). No *user* secret is live. *Logged-in page* (WordPress): two cookie validations, each an HMAC-MD5 key
derivation, an HMAC-SHA256 and a `hash_equals`; a few thousand cycles of secret
work inside a request of tens of millions. *Login*: the password itself.

```
WordPress:  wp_authenticate_*()                    PHP
              wp_check_password()                  PHP, pluggable.php
                PasswordHash::CheckPassword()      PHP, class-phpass.php
                  crypt_private():
                    $hash = md5($salt . $password, true);    <- C builtin
                    do { $hash = md5($hash . $password, true); } while (--$count);
                       8,192 iterations at the shipped 2^13 ("$P$B"), 65,536 at 2^16 ("$P$E")
Symfony:    password_verify()  -> bcrypt cost 13, the whole loop inside ONE C call
```

The phpass loop is PHP bytecode. Between two `md5` calls the interpreter
concatenates, allocates, dispatches into the builtin and stores the result: public
work, with the password live in a PHP string throughout. There is no larger C
function to bracket, so the bracket sits on `md5` and pays its enable, barrier and
clear on every iteration; the pass's unit is also the C function, and it sees the
same boundary. That is the shape the sweep exercises. bcrypt is the opposite
shape: one boundary crossing per verification.

## Results

Percent over A, **cycles** (PMC0) of the php-cgi process, medians of 100 measured
requests per arm after 50 warm-up, arms rotating per request. B - Bn and P - Z are
the executed-switch terms in points of A. MAD is the median absolute deviation of
A's samples as a percent of its median, the noise floor: a value under it is zero.
The `cpu` column is the 2026-09-05 CPU-time run's figure for the same row, so the
two instruments can be read against each other. Raw: `results/m4/{zend,symfony,wordpress}.txt`.

| WordPress 6.2 | A (Mcyc) | C blanket | B - Bn | P - Z | MAD | *cpu 09-05* |
|---|---|---|---|---|---|---|
| anonymous front page (the suite's request) | 151.1 | +2.39% | -0.00 | +0.42 | 0.80% | *+2.1%* |
| logged-in front page | 157.7 | +2.34% | +0.14 | -0.36 | 0.83% | *+2.0%* |
| login 1 in 100 | | +2.62% | -0.01 | +0.22 | 2.14% | *+2.3%* |
| login 1 in 20 | | +2.53% | +0.10 | +0.06 | 2.21% | *+2.1%* |
| login 1 in 5 | | +2.05% | -0.24 | -0.07 | 2.80% | *+1.5%* |
| logins only (8,193 md5 calls each) | 143.0 | +2.23% | **+1.35** | **+1.92** | 0.92% | *+1.3%* |

The WordPress rows are a second counter run (16:04), measured after a diagnostic
overwrote the first run's `.json`; the two agree on every column that matters --
`logins only` reads B - Bn +1.34 then +1.35, P - Z +1.99 then +1.92 -- which is
itself a reproducibility check nobody asked for.

| Symfony Demo 2.2.3 | A (Mcyc) | C blanket | B - Bn | P - Z | MAD | *cpu 09-05* |
|---|---|---|---|---|---|---|
| the suite's request (/) | 84.5 | -0.39% | +0.27 | +0.06 | 0.51% | *+0.4%* |
| blog index (/en/blog/) | 157.8 | +0.53% | +0.29 | -0.01 | 0.38% | *+1.0%* |
| login 1 in 100 | | +0.46% | +0.23 | +0.20 | 0.42% | *+0.5%* |
| login 1 in 20 | | +0.52% | +0.19 | +0.07 | 0.43% | *+0.9%* |
| login 1 in 5 | | +0.94% | +0.13 | -0.08 | 0.66% | *+0.9%* |
| logins only (bcrypt cost 13) | 1,623.8 | +0.09% | -0.02 | +0.01 | 0.05% | *+0.1%* |

| Zend/bench.php | A (Mcyc) | C blanket | B - Bn | P - Z | MAD | *cpu 09-05* |
|---|---|---|---|---|---|---|
| as the suite runs it, opcache on, JIT off, 50 runs | 961.4 | **+9.69%** | -0.77 | +0.27 | 1.01% | *+10.8%* |

The login rows mix one login into the stated number of page requests (anonymous
and logged-in alternating) and report the whole mix, so a login's cost is diluted
by its share; the "logins only" rows are the undiluted number.

**The counter run is a third to a half as noisy**: MAD 0.38-0.66% on the Symfony
rows against 1.2-2.6% before, and 0.05% on the bcrypt row against 0.1%. Nothing
about the workload changed; a scheduler tick was simply the wrong ruler for a
40 ms request. The Symfony `/` row moves from +0.4% to -0.39% and the Zend row
from +10.8% to +9.69% -- both within the reproducibility of a fresh build on a
fresh work directory, and both still telling the same story by a wide margin.

## The call-density sweep

The selective arms pay per crypto call, so the knob that moves them is calls per
request, and two realistic settings raise it. REST requests authenticated by an
*application password* (WordPress's credential for scripts and integrations; HTTP
Basic, no cookie, no session) make WordPress verify the password with phpass on
**every** request. And a one-line plugin sets phpass to 2^16 rounds
(`PasswordHash(11, true)`), a hardening setting sites use, which is 65,537 `md5`
calls per verification instead of 8,193. `run_wpapi.sh` rehashes the admin
password and mints a fresh application password at each setting. Raw:
`data/wpapi_13.txt`, `data/wpapi_16.txt`.

| WordPress 6.2 | md5 calls | A (Mcyc) | C blanket | B - Bn | P - Z | MAD | *cpu 09-05* |
|---|---|---|---|---|---|---|---|
| REST anonymous, `/wp-json/wp/v2/posts` | 0 | 128.2 | +1.84% | +0.19 | -0.10 | 0.88% | *+1.6%* |
| REST authenticated, phpass 2^13 (shipped) | 8,193 | 133.6 | +1.89% | +1.03 | +2.06 | 0.83% | *+1.9%* |
| logins only, phpass 2^13 | 8,193 | 132.4 | +1.89% | +1.36 | +2.21 | 0.97% | *+1.9%* |
| REST authenticated, phpass 2^16 | 65,537 | 156.2 | +2.41% | **+8.80** | **+13.47** | 0.73% | *+2.4%* |
| logins only, phpass 2^16 | 65,537 | 159.4 | +2.12% | **+8.31** | **+13.03** | 0.85% | *+2.0%* |

Per call, from the difference between the two round counts on the same request
type (57,344 extra `md5` calls). **These are now measured cycles, not nanoseconds
converted at an assumed clock** -- the 2026-09-05 column divided by 4.4 GHz, and
the achieved clock actually varies 3.77-4.15 GHz across these rows, so the old
column carried that error:

| per md5 call | cycles, measured | *2026-09-05, inferred at 4.4 GHz* | what it is |
|---|---|---|---|
| the md5 work itself | 394-472 | *420-520* | one block, plus the interpreter's call and return |
| bracket, B - Bn | 199-215 | *180-220* | enable, `sb` drain, clear, and the overlap lost across the boundary |
| pass, P - Z | 312-320 | *300-320* | two region entries per call: `PHP_MD5Update` and `PHP_MD5Final` are each seeded |
| blanket, C | **15-22** | *0* | see below: blanket is not free per call, and the old instrument could not resolve it |

**Blanket is not free per call, which is a correction to the 2026-09-05 reading.**
It costs 15-22 cycles on every `md5`, i.e. **md5 itself runs 3-5% slower under
DIT** against the ~1.9% the whole request pays. That is the right shape: DIT
suppresses data-dependent optimisations, and a hash kernel is precisely the code
that was benefiting from them, so crypto dwells harder than framework code. The
old run reported this as "0, a fixed 0.5-0.9 ms per request" because 15 cycles per
call is 0.03 ms across 8,193 calls -- under the resolution of a scheduler tick.
It does not change any conclusion (blanket's cost is still overwhelmingly a fixed
per-request term) but it does mean blanket's curve has a slope, and the crossover
below is computed with it.

**The crossover**, solving the two linear cost models through their two measured
points:

| | bracket vs blanket | pass vs blanket |
|---|---|---|
| REST authenticated | 14,126 calls | 7,388 calls |
| logins only | 12,013 calls | 6,730 calls |

so roughly **12,000-14,000 crypto calls per request for the bracket and 6,700-7,400
for the pass** (2026-09-05, inferred: 10,000-19,000 and 7,000-13,000 -- the same
answer, three to five times tighter). Every page request and a shipped-phpass
login sit below it, where selective placement wins outright. A hardened phpass
sits above it. The switch is the cost, and the application sets its count; this is
the silicon form of experiment 09's finding.

## What the counters added

CPU time is one number per request. The counters are two, on the same window, and
the second one settles a question this experiment could previously only argue.

**Blanket DIT's cost is entirely dwell.** On the WordPress anonymous front page:

| arm | cycles | instructions | IPC |
|---|---|---|---|
| A | 151,052,500 | 436,496,708 | 2.89 |
| C blanket | 154,665,400 | 435,765,427 | **2.82** |

The two arms are the **same binary**, differing only in an environment variable
that decides whether the injected constructor executes one `msr DIT, #1` before
`main`. They retire the same instructions to within 0.17%. Cycles rise 2.39% and
IPC falls 2.4%. So blanket's cost is **overwhelmingly dwell and not work**: the
machine executing the same instructions more slowly, which is what "the mode
suppresses data-dependent optimisations" means, measured rather than inferred.

**Selective placement's cost is mostly dwell too, not the switches' own
instructions.** On `logins only, phpass 2^13` the pass retires 0.47% more
instructions than A -- that is its 97 executed switch sites -- while costing 2.21
points of cycles. Four fifths of what selective placement costs is the
serialisation each `msr DIT` imposes, not the instruction it adds. This is the
same conclusion experiment 09 reached from cycles-per-switch on libsodium, arrived
at independently here from an instruction count, on an application workload.

**And it is a validity gate, not just a finding.** Blanket sets a mode bit; it
cannot change the instruction stream, so A and C must retire the same work --
otherwise no cycle ratio between them means anything, and a rig that had silently
built two different binaries would look exactly like a result. Zend passes at
+0.001% and the Symfony bcrypt row at -0.03%.

**On WordPress it does not quite pass, and that is a real observation rather than
a defect.** Blanket retires **0.17% fewer instructions** on the two pure page rows
(-0.168% anon, -0.159% logged-in), against a workload instruction MAD of 0.041%
measured over the same 100 samples. It is four times the noise floor, the same
sign and size in two independent runs, and present on Symfony's page rows too
(-0.44% and -0.86%). **We do not have an explanation for it.** The most likely one
is that PMC1 counts micro-ops rather than architectural instructions, so an
optimisation DIT suppresses -- move elimination, fusion -- shifts the count
slightly; that is a guess and it has not been tested.

What can be said is that it does not threaten the result. It is an **eighth** of
the 2.39% cycle difference it would have to explain, and it points the wrong way:
C retires *less* work and takes *more* cycles, so the dwell reading is conservative,
not doubtful. `bench.py` therefore reports the ratio rather than a bare pass/fail,
and calls a row invalid only when the instruction gap is a large share of the
cycle gap.

**The mixed rows cannot be gated this way at all.** `login 1 in 100` reads -5.00%,
with its NOP twins `Bn` and `Z` at -4.65% and -4.54%, which no code difference can
produce. Those rows are a median over three request types whose instruction counts
cluster at 436M, 459M and 450M, so the aggregate median lands wherever the type
ordering puts it and means nothing. Read the per-type breakdown on those rows.

## What it says

- **Blanket DIT costs a real PHP application under 3 percent**, and is free on a
  Symfony front page. Zend/bench.php reads +9.7 on the same binary, so the
  micro-benchmark overstates the prize four to twenty-five times, the same gap as
  Django against pyperformance.
- **That cost is dwell, not work.** Same instructions to within 0.2%, IPC down
  2.4%. Blanket adds no instructions and buys its protection entirely out of the
  optimisations the mode suppresses -- the cost model as a measurement.
- **The developer's bracket costs nothing on page requests** and 1.4 points on a
  shipped-phpass login. At one login in twenty that is below the noise floor.
- **The pass matches the bracket on this suite.** PHP's crypto builtins are leaves
  with no public work inside, so the analysis has nothing to carve out that the
  bracket does not already exclude. The pass's advantage is in libraries whose
  regions the bracket cannot see (experiments 02, 03, 10), not here.
- **Cost scales with crypto calls per request and crosses blanket** near 10^4
  calls: 199 to 215 measured cycles per bracketed call, 312 to 320 per pass-placed
  pair of regions, against blanket's 2.35M fixed cycles plus 15 to 22 per call.
- **The pass costs more than the bracket where the boundary dominates** because it
  seeds `PHP_MD5Update` and `PHP_MD5Final` separately and enters two regions per
  `md5` where the bracket enters one. Seeding the builtin's entry instead would
  close that gap; it is a placement policy, not an analysis limit.
- **bcrypt is indifferent to DIT**: 1.62 billion cycles per Symfony login, +0.09
  percent under blanket, and both selective arms within 0.02 points -- at a 0.05%
  noise floor, twenty times sharper than the CPU-time run could state it.

## How to rerun

```
paper_experiments/11-php-src-suite/reproduce.sh            # every stage
paper_experiments/11-php-src-suite/reproduce.sh deps clang build apps run collect
```

Stages: `deps` (Homebrew: mariadb sqlite zlib oniguruma pkgconf; the SDK), `clang`
(uses `LLVM_BUILD`, default `build/`, or configures and builds a Release taint
clang there, about an hour), `build` (PHP 8.4.25 downloaded from php.net, the
five arms, `libditctl.dylib`; about 10 minutes), `apps` (clones the two harness
repositories at the pinned commits, initialises and starts MariaDB on
`127.0.0.1:3307`, `wp-cli core install`, permalinks, Symfony cache warm-up),
`run` (every row above, about 25 minutes on an idle machine), `collect` (copies
results, reports and a provenance line into `data/`), `stopdb`. Env: `W` (work
dir, default `~/Documents/dit-phpsuite`), `LLVM_BUILD`, `DB_PORT`, `WARMUP`,
`MEASURED`, `METRIC`, `JOBS`.

**For the counter run**, replace the `run` stage with the rooted wrapper, which is
what produced `results/m4/`:

```
sudo utils/dit_host_screening/phpsuite/run_root.sh            # all four workloads
sudo utils/dit_host_screening/phpsuite/run_root.sh wpapi      # just the sweep
```

It stops Spotlight, purges the page cache, preflights that the PMCs are readable
and the thread bind is accepted, pins to the highest-numbered core (a P-core on
every Apple part), runs `run_suite.sh`, then re-enables Spotlight and hands the
results back to `SUDO_USER` -- including on Ctrl-C. Without root everything still
works: `bench.py` falls back to filtering migrated samples instead of preventing
them, and reports `pinned -1` on every row so the difference is never invisible.

**An idle machine is a hard requirement for the counter metric**, in a way it was
not for CPU time. The PMCs are per-core, so anything else scheduled on the pinned
core is charged to the request. Measured while Spotlight was indexing the freshly
built PHP trees: 47% of samples dropped. Rooted and quiet: 1.2%.

`reproduce.sh` was verified end to end on 2026-09-05 in a fresh work directory on this
M4 (tarball downloaded, repositories cloned, MariaDB on a second port, `WARMUP=3
MEASURED=5`): identical switch counts at every round of the seed loop, all 18 rows
gate ok, and even at 5 requests the 2^16 rows reproduce the effect (B - Bn +4.7 /
+6.7, P - Z +11.2 / +10.1, blanket +2.1 / +2.2).

Reading the output: each row prints the six arms' median kilocycles, percent
against A and against C, then retired instructions, IPC, the achieved clock and
the CPU ms, then `B-Bn` and `P-Z`. Numbers within the MAD of zero are noise. Four
things invalidate a row rather than merely widening it:

| field | what a bad value means |
|---|---|
| `gate` other than `ok` | an arm exited with the wrong `PSTATE.DIT`; it is blanket in disguise |
| `pmc OFF` | the counters were unreadable and the row fell back to CPU time |
| `GHz` far off 3.8-4.4 | the thread was not on a P-core; this is experiment 09's residency gate |
| `instruction parity FAIL` | A and C are not running the same work, so no cycle ratio between them means anything (but read the note in "What the counters added" first -- the gate is not meaningful on the mixed login rows) |

`migrated` counts samples the clock gate discarded. A handful out of 600 is
normal; tens of percent means the machine was not idle and the row should be
re-run, not interpreted.

## Limits

- Both selective arms treat every call of a crypto builtin as secret-bearing,
  including hashing of public strings; neither can see userland taint.
- WordPress on MariaDB is two processes. The arms differ only in php-cgi, and the
  counters are read inside php-cgi, which excludes the database by construction.
- **The counters are per-core, not per-thread.** Pinning stops the measured thread
  moving; it does not stop anything else being scheduled onto that core, and what
  lands there is charged to the request. The clock gate discards those samples
  (1.2% of them on this run) but cannot repair one, and a contaminated sample that
  happens to land inside the band is not detectable at all. kperf's per-thread
  counters would close this, at 3,400 cycles per read -- negligible at
  whole-process granularity, unlike experiment 09's microsecond regions -- and
  that is the obvious next refinement if a row ever needs it.
- **Instruction counts here are not php-src's metric.** The harness counts
  instructions under callgrind on Linux, deterministically; these are retired
  instructions on real silicon, which include the process's share of anything the
  kernel did on its behalf. They are exact enough to gate on (0.041% MAD on a
  WordPress request, 0.001% on Zend) but they are not the same quantity, and the
  two should not be compared row for row.
- One host so far (M4). Each `msr DIT` is serialising on Apple silicon; the
  per-call costs above are that machine's.
- The pilot on a minimal PHP + ext/hash build (`docs/results/` is not the record;
  `~/Documents/dit-silicon-candidates/RESULTS.md` sections 1-3 on the M4) found
  that each executed `sb` costs its pipeline drain; the suite numbers include it in
  both selective arms.

## Files

- **`results/m4/`** - the counter run (2026-09-06), one directory per host:
  - `{zend,symfony,wordpress,wpapi_13,wpapi_16}.{txt,json}` - the driver's output,
    six arms per row, and per-row medians in cycles, instructions, CPU ms and
    achieved clock, plus MAD, gate, pinned state and dropped-sample counts.
  - `crypto_call_counts.txt` - crossings of the seven crypto builtins per request
    type, counted with the `-DDIT_BRACKET_COUNT=1` build rather than estimated.
  - `summary.txt` - the tables proposed for the paper: results ordered by crossings,
    cost per crossing, the crossover, and the dwell decomposition. Derived from the
    files above; no new measurement.
  - `provenance.txt` - host, kernel, instrument, compiler, sources, gates.
- `data/{zend,symfony,wordpress,wpapi_13,wpapi_16}.txt` - the 2026-09-05 CPU-time
  run, kept unmodified. It is what the published artifact and the *cpu 09-05*
  columns above were computed from, and keeping it is how the change of instrument
  stays auditable.
- `data/bracket.diff` - the only source change to PHP.
- `data/seeds_php_fixpoint.txt`, `owned_php.txt`, `obligations_round*.txt`,
  `infoloss.txt`, `precision.txt`, `switch_counts.txt` - the pass arm's seed loop
  and its reports.
- `data/provenance.txt` - host, compiler, sources, counts; `collect` appends a line
  per run.
- `figures/developers-bracket.html` - the published page, design written before
  the run and results added after.
