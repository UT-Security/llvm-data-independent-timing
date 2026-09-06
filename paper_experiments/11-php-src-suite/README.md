# 11 - php-src's benchmark suite: blanket DIT, the developer's bracket, the pass

**Status: complete, silicon.** Measured 2026-09-06 on Apple M4 (Mac16,10), macOS
15.7.3, no root: CPU time of the `php-cgi` process, medians of 100 requests per arm,
not cycle counters. Compiler `ef71e700b826` (dit-tainter) with `-taint-dit-external-preserves=1`
passed explicitly (its default flip landed upstream the same day as `7c330a5e4bf2`) and the
new `-taint-dit-enable-barrier=sb`, committed just before this experiment. `data/provenance.txt`
has the exact record.

**Published artifact:** https://claude.ai/code/artifact/a70963ab-5df6-4717-b6f3-140fe328c20e
Source: `figures/developers-bracket.html`. Republish through that URL (`Artifact`
with `url=...`); publishing the file without it creates a second artifact.

**Rig:** `../../utils/dit_host_screening/phpsuite/`. **Rerun:** `./reproduce.sh` on
any Apple Silicon Mac with Homebrew (section "How to rerun").

---

## The claim

> On php-src's own benchmark suite, blanket `PSTATE.DIT` costs a real PHP
> application **0.4 to 2.4 percent** (WordPress about 2, Symfony under 1), while
> the suite's interpreter micro-benchmark reads **+10.8**. Apple's recipe written
> by hand at the entry of the seven crypto builtins (the developer's bracket) and
> the pass cost **nothing on any page request** and the same **1.4 to 2.0 points on
> a WordPress login**. Both selective placements pay per crypto *call*, not per
> unit of secret work: at 65,537 `md5` calls per request (phpass at 2^16 rounds)
> the bracket costs **7 to 8 points**, the pass **11.5**, and blanket still 2.4.
> Blanket becomes the cheaper option above roughly ten thousand crypto calls per
> request.

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

None of the harness's requests carries a secret. The suite as shipped is a host
screen for blanket DIT; the request types below add the paths on which the
applications compute on secrets, using the applications' own code.

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
| instrument | callgrind instruction counts (Linux) | CPU time of the process, `rusage`; no valgrind on Apple Silicon |
| JIT | off and tracing | off |
| counts | 50 warm-up, 50 measured | 50 warm-up, 100 measured (Zend: 50) |
| arms | one binary | six, rotating on every request; DIT read back at exit of every process |
| requests | the front page | the front page, plus the secret-bearing types below |

The per-process design is what makes arm rotation and the readback gate possible:
every process is started with the injected constructor library (`utils/cio_ditctl.c`),
which raises the scheduling class for P-core residency and reads `PSTATE.DIT` back
at exit. C must exit with the bit set and every other arm clear, on every request,
or the row is not a result. Every row here passed.

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

*Anonymous page*: routing, queries, templates, HTML. No secret is live at any
point. *Logged-in page* (WordPress): two cookie validations, each an HMAC-MD5 key
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

Percent over A, CPU time of php-cgi, medians of 100 measured requests per arm
after 50 warm-up, arms rotating per request. B - Bn and P - Z are the executed-switch
terms in points of A. MAD is the median absolute deviation of A's samples as a
percent of its median, the noise floor: a value under it is zero. Raw:
`data/{zend,symfony,wordpress}.txt`.

| WordPress 6.2 | A | C blanket | B - Bn | P - Z | MAD |
|---|---|---|---|---|---|
| anonymous front page (the suite's request) | 37 ms | +2.1% | +0.1 | +0.1 | 0.9% |
| logged-in front page | 38 ms | +2.0% | -0.1 | +0.3 | 1.0% |
| login 1 in 100 | | +2.3% | +0.1 | 0.0 | 1.6% |
| login 1 in 20 | | +2.1% | -0.1 | +0.3 | 1.6% |
| login 1 in 5 | | +1.5% | -0.1 | -0.1 | 1.7% |
| logins only (8,193 md5 calls each) | 32 ms | +1.3% | **+1.4** | **+1.6** | 1.1% |

| Symfony Demo 2.2.3 | A | C blanket | B - Bn | P - Z | MAD |
|---|---|---|---|---|---|
| the suite's request (/) | 23 ms | +0.4% | -0.3 | -0.1 | 1.2% |
| blog index (/en/blog/) | 40 ms | +1.0% | -0.3 | +0.5 | 1.2% |
| login 1 in 100 | | +0.5% | +0.8 | -0.9 | 1.5% |
| login 1 in 20 | | +0.9% | 0.0 | +0.1 | 2.6% |
| login 1 in 5 | | +0.9% | -0.6 | -0.1 | 1.8% |
| logins only (bcrypt cost 13) | 370 ms | +0.1% | 0.0 | 0.0 | 0.1% |

| Zend/bench.php | A | C blanket | B - Bn | P - Z | MAD |
|---|---|---|---|---|---|
| as the suite runs it, opcache on, JIT off, 50 runs | 237 ms | **+10.8%** | -0.1 | +1.0 | 1.5% |

The login rows mix one login into the stated number of page requests (anonymous
and logged-in alternating) and report the whole mix, so a login's cost is diluted
by its share; the "logins only" rows are the undiluted number.

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

| WordPress 6.2 | md5 calls | A | C blanket | B - Bn | P - Z | MAD |
|---|---|---|---|---|---|---|
| REST anonymous, `/wp-json/wp/v2/posts` | 0 | 32.6 ms | +1.6% | -0.3 | +0.6 | 1.4% |
| REST authenticated, phpass 2^13 (shipped) | 8,193 | 33.6 ms | +1.9% | +0.7 | +1.4 | 1.6% |
| logins only, phpass 2^13 | 8,193 | 33.5 ms | +1.9% | +1.4 | +2.0 | 1.4% |
| REST authenticated, phpass 2^16 | 65,537 | 39.0 ms | +2.4% | **+8.0** | **+11.7** | 1.1% |
| logins only, phpass 2^16 | 65,537 | 40.3 ms | +2.0% | **+6.9** | **+11.5** | 1.2% |

Per call, from the difference between the two round counts on the same request
type (57,344 extra `md5` calls):

| per md5 call | ns | cycles at 4.4 GHz | what it is |
|---|---|---|---|
| the md5 work itself | 95-119 | 420-520 | one block, plus the interpreter's call and return |
| bracket, B - Bn | 40-50 | 180-220 | enable, `sb` drain, clear, and the overlap lost across the boundary |
| pass, P - Z | 69-72 | 300-320 | two region entries per call: `PHP_MD5Update` and `PHP_MD5Final` are each seeded |
| blanket, C | 0 | 0 | a fixed 0.5-0.9 ms per request, whatever the call count |

**The crossover.** Blanket's fixed cost per request equals the bracket's per-call
cost at roughly 10,000 to 19,000 crypto calls per request, and the pass's at 7,000
to 13,000. Every page request and a shipped-phpass login sit below it, where
selective placement wins outright. A hardened phpass sits above it. The switch is
the cost, and the application sets its count; this is the silicon form of
experiment 09's finding.

## What it says

- **Blanket DIT costs a real PHP application 0.4 to 2.4 percent.** Zend/bench.php
  reads +10.8 on the same binary, so the micro-benchmark overstates the prize five
  to twenty-five times, the same gap as Django against pyperformance.
- **The developer's bracket costs nothing on page requests** and 1.4 points on a
  shipped-phpass login. At one login in twenty that is below the noise floor.
- **The pass matches the bracket on this suite.** PHP's crypto builtins are leaves
  with no public work inside, so the analysis has nothing to carve out that the
  bracket does not already exclude. The pass's advantage is in libraries whose
  regions the bracket cannot see (experiments 02, 03, 10), not here.
- **Cost scales with crypto calls per request and crosses blanket** near 10^4
  calls: 180 to 220 cycles per bracketed call, about 300 per pass-placed pair of
  regions, against a flat 2 percent for blanket.
- **The pass costs more than the bracket where the boundary dominates** because it
  seeds `PHP_MD5Update` and `PHP_MD5Final` separately and enters two regions per
  `md5` where the bracket enters one. Seeding the builtin's entry instead would
  close that gap; it is a placement policy, not an analysis limit.
- **bcrypt is indifferent to DIT**: 370 ms per Symfony login, +0.1 percent under
  blanket, zero switch cost for either selective arm.

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
`MEASURED`, `JOBS`. Nothing runs as root.

`reproduce.sh` was verified end to end on 2026-09-05 in a fresh work directory on this
M4 (tarball downloaded, repositories cloned, MariaDB on a second port, `WARMUP=3
MEASURED=5`): identical switch counts at every round of the seed loop, all 18 rows
gate ok, and even at 5 requests the 2^16 rows reproduce the effect (B - Bn +4.7 /
+6.7, P - Z +11.2 / +10.1, blanket +2.1 / +2.2).

Reading the output: each row prints the six arms' median CPU ms, percent against
A and against C, then `B-Bn` and `P-Z`. Numbers within the MAD of zero are noise.
A `gate` other than `ok` means an arm exited with the wrong DIT state and the row
is invalid.

## Limits

- Both selective arms treat every call of a crypto builtin as secret-bearing,
  including hashing of public strings; neither can see userland taint.
- WordPress on MariaDB is two processes. The arms differ only in php-cgi, and CPU
  time of php-cgi is the number that excludes the database.
- Instruction counts, the harness's metric, are not available on macOS; time on an
  idle machine with rotating arms and the readback gate is the substitute, as in
  every silicon experiment here.
- One host so far (M4). Each `msr DIT` is serialising on Apple silicon; the
  per-call costs above are that machine's.
- The pilot on a minimal PHP + ext/hash build (`docs/results/` is not the record;
  `~/Documents/dit-silicon-candidates/RESULTS.md` sections 1-3 on the M4) found
  that each executed `sb` costs its pipeline drain; the suite numbers include it in
  both selective arms.

## Files

- `data/{zend,symfony,wordpress,wpapi_13,wpapi_16}.txt` - the driver's output, and
  the `.json` per row (medians per arm and per request type, MAD, gate, failures).
- `data/bracket.diff` - the only source change to PHP.
- `data/seeds_php_fixpoint.txt`, `owned_php.txt`, `obligations_round*.txt`,
  `infoloss.txt`, `precision.txt`, `switch_counts.txt` - the pass arm's seed loop
  and its reports.
- `data/provenance.txt` - host, compiler, sources, counts; `collect` appends a line
  per run.
- `figures/developers-bracket.html` - the published page, design written before
  the run and results added after.
