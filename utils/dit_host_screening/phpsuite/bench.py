#!/usr/bin/env python3
"""Experiment 11: php-src's benchmark workloads under six arms on Apple Silicon.

Every request is one php-cgi process in real CGI mode (env + stdin), opcache on
with the JIT off as the harness sets it, compiled scripts served from opcache's
file cache so the compile is amortised across processes the way php-fpm amortises
it. Arms rotate on every request; PSTATE.DIT is read back at exit by the injected
constructor and checked.

THE INSTRUMENT IS THE PERFORMANCE COUNTERS (changed 2026-09-06; the 2026-09-05
run used the CPU-time column below and nothing else). libditctl.dylib brackets
the whole php-cgi process with Apple's fixed PMCs read straight from EL0 --
cycles from PMC0, retired instructions from PMC1 -- which this kernel permits
(PacmanPatcher, PMCR0_USEREN_EN) with no root and no driver call. That buys three
things rusage cannot give:

  exactness   two `mrs` against a scheduler tick. Repeat runs of one workload
              agree to 0.05% on cycles and 0.01% on instructions.
  IPC         cycles AND instructions on the same window, so DWELL (same work,
              more cycles: the mode itself slowing execution down) separates from
              SWITCHES (more instructions: the placement's own cost). That
              distinction is the cost model the experiment argues, and CPU time
              cannot see it.
  a parity gate  blanket adds one `msr` per process, so A and C must retire the
              same instructions. If they do not, the arms are not running the
              same work and no cycle ratio between them means anything.

CPU time is still recorded, in the same run, and printed beside the cycles. It is
what the committed 2026-09-05 tables were computed from, and keeping both is how
the change of instrument stays auditable.

THE COUNTERS ARE PER-CORE, so a process that migrates differences two different
cores. Cycles are gated against CNTVCT over the same window: a sample whose
implied core clock falls outside CLK_LO..CLK_HI GHz migrated and is dropped,
counted, and reported per row. Pinning (root, kern.sched_thread_bind_cpu) removes
the hazard rather than filtering it and is used when the run has it.

  bench.py wordpress|symfony|zend|wpapi [row ...]   (default: every row)

Env: W (work dir, default ~/Documents/dit-phpsuite), WORDPRESS_DB_HOST (default 127.0.0.1:3307),
     WARMUP / MEASURED (default 50 / 100), BENCH_ARMS (comma list, default all six),
     METRIC (cyc | cpu, default cyc), CLK_LO / CLK_HI (GHz gate, default 2.0 / 5.5),
     wpapi only: WP_APP_PASSWORD (required), ROUNDS_LABEL, SKIP_ANON.
"""
import os, re, sys, time, json, resource, subprocess, statistics as st, urllib.parse as up

W = os.path.expanduser(os.environ.get('W', '~/Documents/dit-phpsuite'))
DYLIB = f'{W}/libditctl.dylib'
DB_HOST = os.environ.get('WORDPRESS_DB_HOST', '127.0.0.1:3307')
ARMS = [('A', 'phpf-base', 0), ('C', 'phpf-base', 1), ('B', 'phpf-bracket', 0), ('Bn', 'phpf-bracketnop', 0),
        ('P', 'phpf-taint', 0), ('Z', 'phpf-taintnop', 0)]
WARMUP, MEASURED = int(os.environ.get('WARMUP', 50)), int(os.environ.get('MEASURED', 100))
OUT = f'{W}/results'; os.makedirs(OUT, exist_ok=True)
if os.environ.get('BENCH_ARMS'):
    keep = os.environ['BENCH_ARMS'].split(',')
    ARMS = [a for a in ARMS if a[0] in keep]
SESS = f'{OUT}/sessions'; os.makedirs(SESS, exist_ok=True)
METRIC = os.environ.get('METRIC', 'cyc')          # 'cyc' = PMC cycles, 'cpu' = rusage CPU ms
CLK_LO, CLK_HI = float(os.environ.get('CLK_LO', 2.0)), float(os.environ.get('CLK_HI', 5.5))

which = sys.argv[1]
rows_wanted = sys.argv[2:]

APPS = {
    'wordpress': dict(root=f'{W}/apps/wordpress', host='wordpress.local'),
    'symfony':   dict(root=f'{W}/apps/symfony-demo/public', host='127.0.0.1'),
    'zend':      dict(root=f'{W}/phpf-base/Zend', host='127.0.0.1'),
}

def phpcgi(arm):
    return f'{W}/{arm[1]}/sapi/cgi/php-cgi'

def run(arm, app, method, script, uri, cookies=None, body=None, expect=200, https=False, auth=None):
    root, host = APPS[app]['root'], APPS[app]['host']
    cache = f'{OUT}/opcache-{arm[1]}'; os.makedirs(cache, exist_ok=True)
    env = {k: os.environ[k] for k in ('PATH', 'HOME') if k in os.environ}
    env.update(GATEWAY_INTERFACE='CGI/1.1', SERVER_PROTOCOL='HTTP/1.1', SERVER_SOFTWARE='bench11', REDIRECT_STATUS='200',
               REQUEST_METHOD=method, SCRIPT_FILENAME=f'{root}/{script}', SCRIPT_NAME=f'/{script}', REQUEST_URI=uri,
               DOCUMENT_ROOT=root, QUERY_STRING=uri.split('?', 1)[1] if '?' in uri else '',
               HTTP_HOST=host, SERVER_NAME=host, SERVER_PORT='80', REMOTE_ADDR='127.0.0.1', REMOTE_PORT='40000',
               HTTP_USER_AGENT='bench11', HTTP_ACCEPT='text/html', DYLD_INSERT_LIBRARIES=DYLIB, ENABLE_DIT=str(arm[2]),
               WORDPRESS_DB_HOST=DB_HOST)
    if cookies:
        env['HTTP_COOKIE'] = '; '.join(f'{k}={v}' for k, v in cookies.items())
    if https:
        env['HTTPS'] = 'on'; env['SERVER_PORT'] = '443'
    if auth:
        env['HTTP_AUTHORIZATION'] = auth
    data = None
    if body is not None:
        data = up.urlencode(body).encode()
        env['CONTENT_TYPE'] = 'application/x-www-form-urlencoded'; env['CONTENT_LENGTH'] = str(len(data))
    cmd = [phpcgi(arm), '-d', f'zend_extension={W}/phpf-base/modules/opcache.so', '-d', 'opcache.enable=1',
           '-d', 'opcache.jit=disable', '-d', 'opcache.jit_buffer_size=0', '-d', f'opcache.file_cache={cache}',
           '-d', 'opcache.file_cache_only=1', '-d', 'opcache.validate_timestamps=0', '-d', 'max_execution_time=0',
           '-d', 'cgi.force_redirect=0', '-d', f'session.save_path={SESS}', '-d', 'display_errors=0', '-d', 'log_errors=0']
    r0 = resource.getrusage(resource.RUSAGE_CHILDREN); t0 = time.perf_counter_ns()
    p = subprocess.run(cmd, input=data, cwd=root, env=env, capture_output=True)
    t1 = time.perf_counter_ns(); r1 = resource.getrusage(resource.RUSAGE_CHILDREN)
    out = p.stdout
    head, _, bodyb = out.partition(b'\r\n\r\n')
    if not _:
        head, _, bodyb = out.partition(b'\n\n')
    headers = {}
    setc = {}
    for line in head.decode(errors='replace').split('\n'):
        if ':' in line:
            k, v = line.split(':', 1); k = k.strip().lower(); v = v.strip()
            if k == 'set-cookie':
                name, _, rest = v.partition('='); setc[name] = rest.split(';', 1)[0]
            else:
                headers[k] = v
    status = int(headers.get('status', '200 OK').split()[0]) if headers.get('status') else 200
    m = re.search(rb'dit=([01])', p.stderr)
    # the counter fields are absent when libditctl predates them or DITCTL_PMC=0
    c = re.search(rb'pmc=(\d+) pinned=(-?\d+) cyc=(\d+) ins=(\d+) ns=(\d+) frq=(\d+)', p.stderr)
    cyc = ins = ns = frq = 0; pmc = 0; pinned = -1
    if c:
        pmc, pinned = int(c.group(1)), int(c.group(2))
        cyc, ins, ns, frq = (int(c.group(i)) for i in (3, 4, 5, 6))
    cpu = ((r1.ru_utime - r0.ru_utime) + (r1.ru_stime - r0.ru_stime)) * 1e3
    # IMPLIED CORE CLOCK, and it is measured against the process's OWN CPU TIME,
    # not against elapsed time. PMC0 counts while the thread is on the core; wall
    # time also counts every interval it was descheduled, so a wall-referenced
    # clock falls with load and rejects perfectly good samples -- 47% of them in
    # the pilot, taken while the machine was still building. rusage resolves to a
    # microsecond, 0.003% of a request, which is ample for a gate.
    #
    # It gates two different things at once, which is why it is also printed:
    #   too LOW  -- the deltas came from two different cores (a migration), or the
    #               thread ran on an efficiency core (~2.6 GHz against a P-core's
    #               ~4.4). This is experiment 09's P-core residency gate.
    #   too HIGH -- something else ran on this core inside the window, so the
    #               per-core counter charged us its cycles too.
    # CNTVCT's rate comes from CNTFRQ_EL0 (1 GHz on M4), never from hw.tbfrequency.
    ghz = (cyc / (cpu * 1e6)) if (cyc and cpu > 0) else 0.0
    return dict(wall=(t1 - t0) / 1e6, cpu=cpu,
                ghz_wall=(cyc * frq / ns / 1e9) if (ns and frq) else 0.0,
                cyc=cyc, ins=ins, ghz=ghz, pmc=pmc, pinned=pinned,
                dit=(m.group(1).decode() if m else '?'), status=status, headers=headers, setcookie=setc,
                body=bodyb, rc=p.returncode, stderr=p.stderr[-400:])

# ---------------------------------------------------------------- request types
def wp_anon(arm, ctx):
    r = run(arm, 'wordpress', 'GET', 'index.php', '/')
    ok = r['status'] == 200 and len(r['body']) > 5000 and b'wp-admin-bar' not in r['body']
    return r, ok

def wp_loggedin(arm, ctx):
    r = run(arm, 'wordpress', 'GET', 'index.php', '/', cookies=ctx['wp_cookies'])
    ok = r['status'] == 200 and b'wp-admin-bar' in r['body']
    return r, ok

def wp_login(arm, ctx):
    r = run(arm, 'wordpress', 'POST', 'wp-login.php', '/wp-login.php',
            cookies={'wordpress_test_cookie': 'WP%20Cookie%20check'},
            body={'log': 'wordpress', 'pwd': 'wordpress', 'wp-submit': 'Log In',
                  'redirect_to': 'http://wordpress.local/wp-admin/', 'testcookie': '1'})
    ok = r['status'] == 302 and any(k.startswith('wordpress_logged_in_') for k in r['setcookie'])
    return r, ok

def sf_anon(arm, ctx):
    r = run(arm, 'symfony', 'GET', 'index.php', '/en/blog/')
    ok = r['status'] == 200 and len(r['body']) > 5000
    return r, ok

def sf_suite(arm, ctx):
    r = run(arm, 'symfony', 'GET', 'index.php', '/')
    ok = r['status'] == 200 and len(r['body']) > 2000
    return r, ok

def sf_login(arm, ctx):
    # the form first (a public page: session + CSRF token), then the POST that verifies the password
    g = run(arm, 'symfony', 'GET', 'index.php', '/en/login')
    m = re.search(rb'name="_csrf_token" value="([^"]+)"', g['body'])
    sess = g['setcookie'].get('PHPSESSID')
    if not (g['status'] == 200 and m and sess):
        return g, False
    r = run(arm, 'symfony', 'POST', 'index.php', '/en/login', cookies={'PHPSESSID': sess},
            body={'_username': 'jane_admin', '_password': 'kitten', '_csrf_token': m.group(1).decode()})
    loc = r['headers'].get('location', '')
    ok = r['status'] in (302, 303) and '/login' not in loc
    r['form_ms'] = g['cpu']
    return r, ok

def zend(arm, ctx):
    r = run(arm, 'zend', 'GET', 'bench.php', '/bench.php')
    ok = r['status'] == 200 and b'Total' in r['body']
    return r, ok

def wp_rest_anon(arm, ctx):
    # the public posts listing: REST bootstrap + query + JSON, no credential
    r = run(arm, 'wordpress', 'GET', 'index.php', '/wp-json/wp/v2/posts', https=True)
    ok = r['status'] == 200 and r['body'].startswith(b'[') and b'"id"' in r['body']
    return r, ok

def wp_rest_auth(arm, ctx):
    # the same listing in edit context, which requires the application password on THIS request:
    # HTTP Basic is stateless, so WordPress runs wp_check_password (phpass) every time
    r = run(arm, 'wordpress', 'GET', 'index.php', '/wp-json/wp/v2/posts?context=edit', https=True, auth=ctx['wp_basic'])
    ok = r['status'] == 200 and r['body'].startswith(b'[') and b'"raw"' in r['body']
    return r, ok

TYPES = dict(wp_rest_anon=wp_rest_anon, wp_rest_auth=wp_rest_auth, wp_anon=wp_anon, wp_loggedin=wp_loggedin, wp_login=wp_login,
             sf_anon=sf_anon, sf_suite=sf_suite, sf_login=sf_login, zend=zend)

def mix(login_type, filler, share, n):
    seq = []
    for i in range(n):
        if share and i % share == 0:
            seq.append(login_type)
        else:
            seq.append(filler[i % len(filler)])
    return seq

if which == 'wordpress':
    ROWS = {
        'anon (the suite\'s request)': ['wp_anon'] * MEASURED,
        'logged-in pages': ['wp_loggedin'] * MEASURED,
        'login 1 in 100': mix('wp_login', ['wp_anon', 'wp_loggedin'], 100, MEASURED),
        'login 1 in 20': mix('wp_login', ['wp_anon', 'wp_loggedin'], 20, MEASURED),
        'login 1 in 5': mix('wp_login', ['wp_anon', 'wp_loggedin'], 5, MEASURED),
        'logins only': ['wp_login'] * MEASURED,
    }
elif which == 'symfony':
    ROWS = {
        'suite request (/)': ['sf_suite'] * MEASURED,
        'blog index (/en/blog/)': ['sf_anon'] * MEASURED,
        'login 1 in 100': mix('sf_login', ['sf_anon'], 100, MEASURED),
        'login 1 in 20': mix('sf_login', ['sf_anon'], 20, MEASURED),
        'login 1 in 5': mix('sf_login', ['sf_anon'], 5, MEASURED),
        'logins only': ['sf_login'] * MEASURED,
    }
elif which == 'wpapi':
    lab = os.environ.get('ROUNDS_LABEL', '?')
    ROWS = {
        f'REST anonymous, /wp-json/wp/v2/posts (0 md5 calls)': ['wp_rest_anon'] * MEASURED,
        f'REST authenticated by application password, phpass {lab} rounds': ['wp_rest_auth'] * MEASURED,
        f'logins only, phpass {lab} rounds': ['wp_login'] * MEASURED,
    }
    if os.environ.get('SKIP_ANON'):
        ROWS = {k: v for k, v in ROWS.items() if 'anonymous' not in k}
elif which == 'zend':
    ROWS = {'Zend/bench.php, opcache on, JIT off': ['zend'] * 50}
else:
    raise SystemExit('wordpress | symfony | zend | wpapi')
if rows_wanted:
    ROWS = {k: v for k, v in ROWS.items() if any(w in k for w in rows_wanted)}

# ---------------------------------------------------------------- context: cookies for logged-in pages
ctx = {}
if which == 'wpapi':
    import base64
    ctx['wp_basic'] = 'Basic ' + base64.b64encode(('wordpress:' + os.environ['WP_APP_PASSWORD']).encode()).decode()
    r, ok = wp_rest_auth(ARMS[0], ctx)
    if not ok:
        raise SystemExit(f'application-password auth failed: status {r["status"]} body {r["body"][:120]!r}')
    r2, ok2 = run(ARMS[0], 'wordpress', 'GET', 'index.php', '/wp-json/wp/v2/posts?context=edit', https=True), None
    print(f'auth check: with credential {r["status"]}, without {r2["status"]} (must be 401)')
if which == 'wordpress':
    r, ok = wp_login(ARMS[0], ctx)
    if not ok:
        raise SystemExit(f'WordPress login failed: status {r["status"]} cookies {list(r["setcookie"])} stderr {r["stderr"]!r}')
    ctx['wp_cookies'] = {k: v for k, v in r['setcookie'].items() if k.startswith('wordpress_')}
    print(f'logged in: {len(ctx["wp_cookies"])} cookies')

print(f'{which}: {len(ARMS)} arms, {WARMUP} warm-up + {MEASURED} measured requests per row, arms rotate per request')
print(f'metric: {METRIC}  (PMC cycles; CPU time recorded alongside)  migration gate {CLK_LO}-{CLK_HI} GHz')
results = {}
for rname, seq in ROWS.items():
    # every sample is kept whole: cycles, instructions and CPU ms come off the
    # same request, so the two instruments are never compared across runs
    data = {a[0]: [] for a in ARMS}; bytype = {a[0]: {} for a in ARMS}
    gate = set(); bad = []; migrated = {a[0]: 0 for a in ARMS}; pmcstate = set()
    full = seq[:WARMUP] + seq
    for i, t in enumerate(full):
        order = ARMS[i % len(ARMS):] + ARMS[:i % len(ARMS)]
        for arm in order:
            r, ok = TYPES[t](arm, ctx)
            if not ok:
                bad.append((arm[0], t, r['status'], r['rc'], r['stderr'][-120:]))
                continue
            gate.add((arm[0], arm[2], r['dit']))
            if i < WARMUP:
                continue
            pmcstate.add((r['pmc'], r['pinned']))
            # a per-core counter read across a migration differences two cores.
            # It shows up as an implied clock nowhere near the part's range --
            # usually absurd, since the cores' counters sit seconds apart.
            if r['pmc'] and not (r['cyc'] and CLK_LO <= r['ghz'] <= CLK_HI):
                migrated[arm[0]] += 1
                continue
            data[arm[0]].append(r)
            bytype[arm[0]].setdefault(t, []).append(r)
    if not data.get('A'):
        print(f'\n{rname}: NO DATA ({len(bad)} failures) e.g. {bad[:2]}'); continue

    have_pmc = all(p for p, _ in pmcstate) and all(s['cyc'] for s in data['A'])
    key = METRIC if (METRIC != 'cyc' or have_pmc) else 'cpu'
    unit = {'cyc': 'kcycles', 'cpu': 'cpu ms'}[key]
    scale = 1e-3 if key == 'cyc' else 1.0
    val = lambda s: s[key] * scale
    med = {a: st.median([val(s) for s in v]) for a, v in data.items() if v}
    medi = {a: st.median([s['ins'] for s in v]) for a, v in data.items() if v}   # instructions
    medc = {a: st.median([s['cpu'] for s in v]) for a, v in data.items() if v}   # the old instrument
    if 'C' not in med: med['C'] = med['A']
    mad = st.median([abs(val(s) - med['A']) for s in data['A']]) / med['A'] * 100
    gate_ok = all(seen == str(d) for _, d, seen in gate)
    pinned = sorted({p for _, p in pmcstate})
    print(f"\n== {which}: {rname}   ({len(data['A'])} measured requests/arm, MAD(A) {mad:.2f}%, "
          f"gate {'ok' if gate_ok else sorted(gate)}, failures {len(bad)}, "
          f"migrated {sum(migrated.values())}, pmc {'on' if have_pmc else 'OFF'}, pinned {pinned})")
    print(f"{'arm':<5}{unit:>12}{'vs A':>9}{'vs C':>9}{'instr':>15}{'ins vs A':>10}{'IPC':>7}{'GHz':>6}{'cpu ms':>9}")
    for a in med:
        mcyc = st.median([s['cyc'] for s in data[a]]) if data.get(a) else 0.0
        ipc = (medi[a] / mcyc) if mcyc else 0.0
        ghz = st.median([s['ghz'] for s in data[a]]) if data.get(a) else 0.0
        ins_va = (medi[a] / medi['A'] - 1) * 100 if medi.get('A') and a in medi else 0.0
        print(f"{a:<5}{med[a]:>12.1f}{(med[a]/med['A']-1)*100:>+8.2f}%{(med[a]/med['C']-1)*100:>+8.2f}%"
              f"{medi.get(a, 0):>15,.0f}{ins_va:>+9.2f}%{ipc:>7.2f}{ghz:>6.2f}{medc.get(a, 0):>9.2f}")
    # every one of the four, not just the twins: a row where an arm lost all its
    # samples to the migration gate used to die here with a KeyError after the
    # table had already printed, which loses the rows that DID survive
    if all(k in med for k in ('B', 'Bn', 'P', 'Z')):
        print(f"  executed-switch terms: B-Bn {(med['B']/med['Bn']-1)*100:+.2f} pts   P-Z {(med['P']/med['Z']-1)*100:+.2f} pts   (relative to A: B-Bn {(med['B']-med['Bn'])/med['A']*100:+.2f}, P-Z {(med['P']-med['Z'])/med['A']*100:+.2f})")
    if 'C' in medi and medi.get('A') and data.get('A'):
        # Blanket sets a mode bit and cannot change the instruction stream, so A
        # and C must retire the same work -- otherwise no cycle ratio between
        # them means anything.
        #
        # WHAT "THE SAME" MEANS DEPENDS ON THE WORKLOAD, which a fixed threshold
        # gets wrong in both directions. Zend/bench.php is a closed compute loop
        # and reproduces to 0.001%; a Symfony request carries sessions, CSRF
        # tokens, a database and a filesystem, and its own instruction count
        # moves by a few tenths of a percent between two runs of the SAME arm.
        # A 0.5% rule called that a failure while passing anything Zend could
        # ever do wrong. So the gate is scaled by the spread the workload
        # actually shows: A's own instruction MAD, which is measured in the same
        # run, from the same requests, with the arms rotating between them.
        ins_mad = st.median([abs(s['ins'] - medi['A']) for s in data['A']]) / medi['A'] * 100
        d = (medi['C'] / medi['A'] - 1) * 100
        cyc_d = (med['C'] / med['A'] - 1) * 100
        # THE QUESTION IS NOT "is there a difference" BUT "can it explain the
        # cycles". A difference under the workload's own instruction noise is
        # nothing. One above it is real and still harmless as long as it is small
        # against the cycle effect it would have to account for -- and it is
        # information, not an error: on this suite blanket consistently retires
        # ~0.17% fewer instructions while costing 2.4% more cycles, which makes
        # the dwell reading conservative rather than doubtful.
        detected = abs(d) > max(0.10, 3 * ins_mad)
        material = abs(cyc_d) > 0 and abs(d) > 0.25 * abs(cyc_d)
        verdict = ('ok' if not detected else
                   'INVALID - the instruction gap is a large share of the cycle gap, so the '
                   'arms may not be running the same work' if material else
                   f'real but immaterial: {abs(d)/abs(cyc_d)*100:.0f}% of the {cyc_d:+.2f}% cycle effect')
        print(f"  instruction parity A vs C: {d:+.3f}%  (workload instruction MAD {ins_mad:.3f}%: {verdict})")
    types = sorted({t for a in bytype for t in bytype[a]})
    if len(types) > 1 or types != [seq[0]]:
        print(f"  per request type, median {unit}: " + ' | '.join(
            f"{t}: " + ' '.join(f"{a}={st.median([val(s) for s in bytype[a][t]]):.1f}" for a in med if t in bytype[a]) for t in types))
    if bad:
        print(f"  failures: {bad[:3]}")
    medg = {a: st.median([s['ghz'] for s in v]) for a, v in data.items() if v}
    raw = {a: [dict(cyc=s['cyc'], ins=s['ins'], cpu=round(s['cpu'], 3), ghz=round(s['ghz'], 3)) for s in v]
           for a, v in data.items()}
    json.dump(raw, open(f"{OUT}/{which}{os.environ.get('ROUNDS_LABEL','')}-{re.sub(r'[^a-z0-9]+', '-', rname.lower()).strip('-')}-raw.json", 'w'))
    results[rname] = dict(metric=key, median=med, median_ins=medi, median_cpu=medc, median_ghz=medg, mad=mad,
                          ins_mad={a: st.median([abs(s['ins'] - medi[a]) for s in v]) / medi[a] * 100
                                   for a, v in data.items() if v and medi.get(a)},
                          gate=sorted(gate), pmc=have_pmc, pinned=pinned, migrated=migrated,
                          n={a: len(v) for a, v in data.items()},
                          bytype={a: {t: st.median([val(s) for s in v]) for t, v in d.items()} for a, d in bytype.items()},
                          bytype_ins={a: {t: st.median([s['ins'] for s in v]) for t, v in d.items()} for a, d in bytype.items()},
                          failures=len(bad))
    sys.stdout.flush()
# A PARTIAL RUN MUST NOT LAND ON THE FULL RUN'S FILENAME. Selecting arms or rows
# is how you debug the rig, and doing that after a real run used to overwrite its
# .json with the diagnostic's -- silently, since the .txt is written by the shell
# redirection in run_suite.sh and survives. That is how the WordPress .json from
# the 2026-09-06 counter run was lost.
tag = ''
if os.environ.get('BENCH_ARMS') or rows_wanted:
    tag = '-partial-' + '+'.join([a[0] for a in ARMS]) + ('-' + '+'.join(rows_wanted) if rows_wanted else '')
json.dump(results, open(f"{OUT}/{which}{os.environ.get('ROUNDS_LABEL','')}{tag}.json", 'w'), indent=1)
