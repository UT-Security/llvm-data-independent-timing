#!/usr/bin/env python3
"""Wrap PHP's crypto builtins in the developer's bracket.

For each PHP_FUNCTION(name) listed, the original body becomes
`static void zif_name_body(INTERNAL_FUNCTION_PARAMETERS)` and a new
PHP_FUNCTION(name) is appended that does ENTER; body; LEAVE. One entry, one
exit, whatever the body returns through (every RETURN_* macro is a plain
`return` from the body). The macros are inert unless -DDIT_BRACKET=1, so the
patched tree builds every arm.  usage: patch_bracket.py <php-src tree>
"""
import os, re, sys

root = sys.argv[1]
TARGETS = {
    'ext/hash/hash.c':         ['hash', 'hash_hmac', 'hash_equals'],
    'ext/standard/md5.c':      ['md5'],
    'ext/standard/crypt.c':    ['crypt'],
    'ext/standard/password.c': ['password_verify', 'password_hash'],
}
hdr = os.path.join(root, 'main', 'dit_bracket.h')
if not os.path.exists(hdr):
    import shutil; shutil.copy(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'dit_bracket.h'), hdr)

for rel, funcs in TARGETS.items():
    p = os.path.join(root, rel)
    s = open(p).read()
    if 'dit_bracket.h' in s:
        print(f'{rel}: already patched'); continue
    for f in funcs:
        pat = re.compile(r'^PHP_FUNCTION\(' + re.escape(f) + r'\)\s*\n\{', re.M)
        n = len(pat.findall(s))
        if n != 1:
            raise SystemExit(f'{rel}: expected one PHP_FUNCTION({f}), found {n}')
        s = pat.sub(f'static void zif_{f}_body(INTERNAL_FUNCTION_PARAMETERS)\n{{', s, count=1)
        s += (f'\n/* experiment 11: the developer\'s bracket around {f}() */\n'
              f'PHP_FUNCTION({f})\n{{\n\tDIT_BRACKET_ENTER();\n'
              f'\tzif_{f}_body(INTERNAL_FUNCTION_PARAM_PASSTHRU);\n\tDIT_BRACKET_LEAVE();\n}}\n')
    # the header only defines macros: put it first, outside any #ifdef block
    s = '#include "dit_bracket.h"\n' + s
    open(p, 'w').write(s)
    print(f'{rel}: wrapped {", ".join(funcs)}')
