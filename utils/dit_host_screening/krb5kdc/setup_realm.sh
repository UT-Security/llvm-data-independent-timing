#!/usr/bin/env bash
# Experiment 13: a throwaway realm DIT.TEST on 127.0.0.1:8888 with the DB2 backend,
# N hammer principals (kdc5_hammer's naming and password convention), a user and a service.
set -uo pipefail
RIG="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
W="${W:-$HOME/Documents/dit-krb5}"; P="$W/krb5-base"; RD="$W/realm"; N="${N:-20}"; PORT="${PORT:-8888}"
LLVM_BUILD="${LLVM_BUILD:-$(cd "$RIG/../../.." && pwd)/build}"
info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
[[ -x "$P/sbin/krb5kdc" ]] || die "build base first"
rm -rf "$RD"; mkdir -p "$RD/db"
cat > "$RD/krb5.conf" <<CONF
[libdefaults]
    default_realm = DIT.TEST
    dns_lookup_kdc = false
    dns_lookup_realm = false
    rdns = false
    udp_preference_limit = 1
[realms]
    DIT.TEST = {
        kdc = 127.0.0.1:$PORT
        admin_server = 127.0.0.1:$PORT
    }
[logging]
    kdc = FILE:$RD/kdc.log
CONF
cat > "$RD/kdc.conf" <<CONF
[kdcdefaults]
    kdc_ports = $PORT
    kdc_tcp_ports = $PORT
[realms]
    DIT.TEST = {
        database_name = $RD/db/principal
        key_stash_file = $RD/db/stash
        acl_file = $RD/kadm5.acl
        max_life = 10h
        max_renewable_life = 7d
    }
[logging]
    kdc = FILE:$RD/kdc.log
CONF
export KRB5_CONFIG="$RD/krb5.conf" KRB5_KDC_PROFILE="$RD/kdc.conf"
info "kdb5_util create"; "$P/sbin/kdb5_util" -r DIT.TEST create -s -P masterpw > "$RD/create.log" 2>&1 || die "create failed: $(tail -3 "$RD/create.log")"
info "principals: $N hammer principals, user, host/svc"
{ for i in $(seq 1 "$N"); do echo "addprinc -pw hammer$i-DEPTH-1@DIT.TEST hammer$i-DEPTH-1"; done
  echo "addprinc -pw userpw user"; echo "addprinc -randkey host/svc.dit.test"; } | "$P/sbin/kadmin.local" > "$RD/addprinc.log" 2>&1 || die "addprinc failed"
grep -c 'created' "$RD/addprinc.log" | sed 's/^/    created: /'
info "tgs_loop"; "$LLVM_BUILD/bin/clang" -O2 -I"$P/include" "$RIG/tgs_loop.c" -o "$W/tgs_loop" -L"$P/lib" -lkrb5 -lk5crypto -lcom_err || die "tgs_loop build failed"
info "done; KRB5_CONFIG=$KRB5_CONFIG KRB5_KDC_PROFILE=$KRB5_KDC_PROFILE"
