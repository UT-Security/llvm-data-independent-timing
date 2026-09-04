#!/usr/bin/env python3
"""Bucket an arm's under-taint (or the null arm's secret-op universe) by what
kind of code it sits in. usage: classify.py <binary> <err> [<err0 to subtract>] [RES]
With an n0 file and RES, reports per-resumption deltas."""
import bisect, re, subprocess, sys
from collections import defaultdict
NM='/home/rgangar/Documents/llvm-data-independent-timing/build/bin/llvm-nm'
binp, errp = sys.argv[1], sys.argv[2]
err0 = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] != '-' else None
RES = int(sys.argv[4]) if len(sys.argv) > 4 else 1
nm = subprocess.run([NM, '-n', '--defined-only', binp], capture_output=True, text=True).stdout
syms = [(int(p[0], 16), p[2]) for p in (l.split() for l in nm.splitlines()) if len(p) == 3 and p[1] in 'tTwW']
addrs = [a for a, _ in syms]
def fn(pc):
    i = bisect.bisect_right(addrs, pc) - 1; return syms[i][1] if i >= 0 else '?'
BUCKETS = [
 ('libc copy/alloc',  r'^(calloc|free|malloc|_int_malloc|_int_free|__memcpy|__memset|memcpy|memset|memmove|explicit_bzero|__libc_|_IO_|__GI_|unlink_chunk|sysmalloc|__default_morecore|__brk|sbrk)'),
 ('hash primitive',   r'^(mbedtls_internal_sha|mbedtls_sha\d|mbedtls_sha256|mbedtls_sha512|mbedtls_psa_hash|mbedtls_psa_mac|mbedtls_psa_hmac|psa_hash|psa_mac|mbedtls_md)'),
 ('aead primitive',   r'^(mbedtls_aes|mbedtls_aesce|mbedtls_gcm|gcm_|psa_aead|mbedtls_psa_aead|mbedtls_cipher|psa_cipher)'),
 ('ticket glue',      r'^(mbedtls_ssl_ticket_|ssl_ticket_|ssl_session_load|ssl_tls13_session_load|ssl_tls12_session_load|mbedtls_ssl_session_load|mbedtls_ssl_session_save|ssl_session_save|ssl_tls13_session_save|mbedtls_ssl_set_hs_psk|mbedtls_ssl_tls13_create_psk_binder|ssl_tls13_offered_psks|ssl_tls13_session_copy_ticket|ssl_tls13_parse_pre_shared_key_ext|mbedtls_ct_memcmp|ssl_tls13_write_new_session_ticket|ssl_tls13_prepare_new_session_ticket|ssl_tls13_write_identity|ssl_tls13_write_binder|mbedtls_ssl_tls13_export_handshake_psk|harness_ticket|mbedtls_ssl_session_copy|mbedtls_ssl_session_set_ticket|ssl_tls13_parse_new_session_ticket|mbedtls_ssl_tls13_key_schedule_stage_early|ssl_tls13_check_ticket)'),
 ('key schedule',     r'^(mbedtls_ssl_tls13_evolve_secret|mbedtls_ssl_tls13_hkdf|mbedtls_ssl_tls13_derive|ssl_tls13_derive|mbedtls_ssl_tls13_key_schedule|mbedtls_ssl_tls13_populate_transform|setup_psa_key_derivation|mbedtls_ssl_tls13_calculate|ssl_tls13_calc|mbedtls_ssl_tls13_compute|ssl_tls13_key_schedule|mbedtls_ssl_tls13_generate|mbedtls_ssl_tls13_labels|ssl_tls13_hkdf|mbedtls_ssl_tls13_make_traffic_keys|mbedtls_ssl_tls13_create_verify_data|ssl_tls13_create_verify|mbedtls_ssl_tls13_exporter|ssl_tls13_transcript|mbedtls_ssl_get_handshake_transcript|mbedtls_ssl_reset_transcript|mbedtls_ssl_add_hs|mbedtls_ssl_update_handshake_status|ssl_update_checksum)'),
 ('psa key mgmt',     r'^(psa_|mbedtls_psa_|psa_crypto_)'),
 ('record layer',     r'^(mbedtls_ssl_decrypt_buf|mbedtls_ssl_encrypt_buf|ssl_parse_record_header|mbedtls_ssl_read_record|mbedtls_ssl_fetch_input|mbedtls_ssl_flush_output|mbedtls_ssl_write_record|mbedtls_ssl_write_handshake_msg|mbedtls_ssl_finish_handshake_msg|mbedtls_ssl_start_handshake_msg|srv_send|cli_recv|cli_send|srv_recv|pipe_|mbedtls_ssl_handle_message_type|mbedtls_ssl_prepare_handshake_record|mbedtls_ssl_tls13_fetch_handshake_msg|ssl_tls13_parse_inner_plaintext|mbedtls_ssl_read|mbedtls_ssl_write|ssl_get_next_record|ssl_consume_current_message|mbedtls_ssl_tls13_write_handshake_msg|ssl_tls13_write_inner_plaintext|mbedtls_ssl_tls13_finish|mbedtls_ssl_tls13_start|mbedtls_ssl_tls13_hs|mbedtls_ssl_send_alert|ssl_buffer|mbedtls_ssl_tls13_check_read_records|ssl_tls13_process_server_finished|ssl_tls13_process_client_finished|mbedtls_ssl_tls13_process_finished|ssl_tls13_write_finished|mbedtls_ssl_tls13_write_finished)'),
 ('client side',      r'^(mbedtls_ssl_tls13_handshake_client_step|ssl_tls13_.*client|ssl_client_|mbedtls_ssl_client|ssl_tls13_parse_server_hello|ssl_tls13_write_client_hello|ssl_tls13_finalize_client|ssl_tls13_postprocess|ssl_tls13_parse_encrypted_extensions|ssl_tls13_parse_certificate|ssl_tls13_parse_hrr)'),
]
def bucket(f):
    for name, rx in BUCKETS:
        if re.match(rx, f): return name
    return 'server handshake / other'
def load(p):
    d = defaultdict(int)
    for pc, cnt in re.findall(r'^  UNDERTAINT pc=(0x[0-9a-f]+) count=(\d+)', open(p, errors='replace').read(), re.M):
        d[fn(int(pc, 16))] += int(cnt)
    return d
d1 = load(errp); d0 = load(err0) if err0 else defaultdict(int)
per = defaultdict(int); fnlist = defaultdict(list)
for f in set(d1) | set(d0):
    delta = (d1[f] - d0.get(f, 0)) / RES
    if delta: per[bucket(f)] += delta; fnlist[bucket(f)].append((delta, f))
tot = sum(per.values())
print(f"{'bucket':28s} {'ops/resumption':>16s} {'share':>7s}   top functions")
for b, v in sorted(per.items(), key=lambda kv: -kv[1]):
    top = ', '.join(f"{f} {int(c)}" for c, f in sorted(fnlist[b], reverse=True)[:4])
    print(f"{b:28s} {v:16.0f} {100*v/tot:6.1f}%   {top}")
print(f"{'TOTAL':28s} {tot:16.0f}")
