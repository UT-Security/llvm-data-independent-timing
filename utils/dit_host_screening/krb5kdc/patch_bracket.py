#!/usr/bin/env python3
"""Wrap libk5crypto's computing API entry points in the developer's bracket.

Each `RET KRB5_CALLCONV\nname(params)\n{` listed below becomes
`static RET\nname_body(params)\n{`, and a wrapper with the original name and
signature is appended: ENTER; ret = name_body(args); LEAVE; return ret. The
macros are inert unless -DDIT_BRACKET=1, so one patched tree builds every arm.
The set is what an annotator would call "the crypto functions": every public
entry point that encrypts, decrypts, checksums, derives, hashes a password or
draws randomness. Pure queries (lengths, validity, enctype compare) and key
handle bookkeeping (krb5_k_create_key etc.) are left alone.
usage: patch_bracket.py <krb5 src dir>
"""
import os, re, shutil, sys

root = sys.argv[1]
d = os.path.join(root, 'lib', 'crypto', 'krb')
TARGETS = {
    'encrypt.c':             ['krb5_c_encrypt', 'krb5_k_encrypt'],
    'decrypt.c':             ['krb5_c_decrypt', 'krb5_k_decrypt'],
    'encrypt_iov.c':         ['krb5_c_encrypt_iov', 'krb5_k_encrypt_iov'],
    'decrypt_iov.c':         ['krb5_c_decrypt_iov', 'krb5_k_decrypt_iov'],
    'make_checksum.c':       ['krb5_c_make_checksum', 'krb5_k_make_checksum'],
    'verify_checksum.c':     ['krb5_c_verify_checksum', 'krb5_k_verify_checksum'],
    'make_checksum_iov.c':   ['krb5_c_make_checksum_iov', 'krb5_k_make_checksum_iov'],
    'verify_checksum_iov.c': ['krb5_c_verify_checksum_iov', 'krb5_k_verify_checksum_iov'],
    'prf.c':                 ['krb5_c_prf', 'krb5_k_prf'],
    'cf2.c':                 ['krb5_c_prfplus', 'krb5_c_derive_prfplus', 'krb5_c_fx_cf2_simple'],
    'string_to_key.c':       ['krb5_c_string_to_key', 'krb5_c_string_to_key_with_params'],
    'random_to_key.c':       ['krb5_c_random_to_key'],
    'make_random_key.c':     ['krb5_c_make_random_key'],
    'prng.c':                ['krb5_c_random_make_octets', 'krb5_c_random_add_entropy',
                              'krb5_c_random_seed', 'krb5_c_random_os_entropy'],
}
hdr = os.path.join(d, 'dit_bracket.h')
if not os.path.exists(hdr):
    shutil.copy(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'dit_bracket.h'), hdr)

total = 0
for rel, funcs in TARGETS.items():
    p = os.path.join(d, rel)
    s = open(p).read()
    if 'dit_bracket.h' in s:
        print(f'{rel}: already patched'); continue
    for f in funcs:
        pat = re.compile(r'^(?P<ret>[A-Za-z_][A-Za-z_0-9 ]*?) KRB5_CALLCONV\n' + re.escape(f) +
                         r'\((?P<params>[^{}]*?)\)\n\{', re.M)
        ms = pat.findall(s)
        if len(ms) != 1:
            raise SystemExit(f'{rel}: expected one definition of {f}, found {len(ms)}')
        m = pat.search(s)
        ret, params = m.group('ret').strip(), m.group('params')
        one = ' '.join(params.split())
        if one.strip() == 'void':
            args = ''
        else:
            names = []
            for prm in one.split(','):
                mm = re.search(r'([A-Za-z_][A-Za-z_0-9]*)\s*(\[\s*\])?\s*$', prm.strip())
                if not mm: raise SystemExit(f'{rel}: cannot name parameter {prm!r} of {f}')
                names.append(mm.group(1))
            args = ', '.join(names)
        s = s[:m.start()] + f'static {ret}\n{f}_body({params})\n{{' + s[m.end():]
        if ret == 'void':
            body = f'\tDIT_BRACKET_ENTER();\n\t{f}_body({args});\n\tDIT_BRACKET_LEAVE();\n'
        else:
            body = f'\t{ret} __ret;\n\tDIT_BRACKET_ENTER();\n\t__ret = {f}_body({args});\n\tDIT_BRACKET_LEAVE();\n\treturn __ret;\n'
        s += f'\n/* experiment 13: the developer\'s bracket around {f}() */\n{ret} KRB5_CALLCONV\n{f}({params})\n{{\n{body}}}\n'
        total += 1
    s = '#include "dit_bracket.h"\n' + s
    open(p, 'w').write(s)
    print(f'{rel}: wrapped {", ".join(funcs)}')
print(f'{total} entry points wrapped')
