#!/usr/bin/env python3
"""Experiment 11: php-src's benchmark workloads under six arms on Apple Silicon.

Every request is one php-cgi process in real CGI mode (env + stdin), opcache on
with the JIT off as the harness sets it, compiled scripts served from opcache's
file cache so the compile is amortised across processes the way php-fpm amortises
it. Arms rotate on every request; wall and CPU time of the php-cgi process are
recorded; PSTATE.DIT is read back at exit by the injected constructor and checked.

  bench.py wordpress|symfony|zend|wpapi [row ...]   (default: every row)

Env: W (work dir, default ~/Documents/dit-phpsuite), WORDPRESS_DB_HOST (default 127.0.0.1:3307),
     WARMUP / MEASURED (default 50 / 100), BENCH_ARMS (comma list, default all six),
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
    return dict(wall=(t1 - t0) / 1e6, cpu=((r1.ru_utime - r0.ru_utime) + (r1.ru_stime - r0.ru_stime)) * 1e3,
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
results = {}
for rname, seq in ROWS.items():
    data = {a[0]: [] for a in ARMS}; bytype = {a[0]: {} for a in ARMS}; gate = set(); bad = []
    full = seq[:WARMUP] + seq
    for i, t in enumerate(full):
        order = ARMS[i % len(ARMS):] + ARMS[:i % len(ARMS)]
        for arm in order:
            r, ok = TYPES[t](arm, ctx)
            if not ok:
                bad.append((arm[0], t, r['status'], r['rc'], r['stderr'][-120:]))
                continue
            gate.add((arm[0], arm[2], r['dit']))
            if i >= WARMUP:
                data[arm[0]].append(r['cpu'])
                bytype[arm[0]].setdefault(t, []).append(r['cpu'])
    med = {a: st.median(v) for a, v in data.items() if v}
    if 'A' not in med:
        print(f'\n{rname}: NO DATA ({len(bad)} failures) e.g. {bad[:2]}'); continue
    if 'C' not in med: med['C'] = med['A']
    mad = st.median([abs(x - med['A']) for x in data['A']]) / med['A'] * 100
    gate_ok = all(seen == str(d) for _, d, seen in gate)
    print(f"\n== {which}: {rname}   ({len(data['A'])} measured requests/arm, MAD(A) {mad:.2f}%, gate {'ok' if gate_ok else sorted(gate)}, failures {len(bad)})")
    print(f"{'arm':<5}{'cpu ms':>10}{'vs A':>9}{'vs C':>9}")
    for a in med:
        print(f"{a:<5}{med[a]:>10.2f}{(med[a]/med['A']-1)*100:>+8.2f}%{(med[a]/med['C']-1)*100:>+8.2f}%")
    if 'Bn' in med and 'Z' in med:
        print(f"  executed-switch terms: B-Bn {(med['B']/med['Bn']-1)*100:+.2f} pts   P-Z {(med['P']/med['Z']-1)*100:+.2f} pts   (relative to A: B-Bn {(med['B']-med['Bn'])/med['A']*100:+.2f}, P-Z {(med['P']-med['Z'])/med['A']*100:+.2f})")
    types = sorted({t for a in bytype for t in bytype[a]})
    if len(types) > 1 or types != [seq[0]]:
        print("  per request type, median cpu ms: " + ' | '.join(
            f"{t}: " + ' '.join(f"{a}={st.median(bytype[a][t]):.1f}" for a in med if t in bytype[a]) for t in types))
    if bad:
        print(f"  failures: {bad[:3]}")
    results[rname] = dict(median=med, mad=mad, gate=sorted(gate), bytype={a: {t: st.median(v) for t, v in d.items()} for a, d in bytype.items()}, failures=len(bad))
    sys.stdout.flush()
json.dump(results, open(f"{OUT}/{which}{os.environ.get('ROUNDS_LABEL','')}.json", 'w'), indent=1)
