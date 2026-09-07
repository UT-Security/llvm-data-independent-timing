#!/usr/bin/env bash
# Experiment 13: MIT krb5 1.22.2 (downloaded from kerberos.org if $W/src is empty) with builtin (all-C) crypto, built with the taint clang at
# -O2, the developer's bracket wrapped (inert) around libk5crypto's 29 computing entry points.
#   build_krb5.sh base      full build + install -> $W/krb5-base; hammer + mkdums; libditctl.dylib
#   build_krb5.sh bracket   rebuild lib/crypto/krb with -DDIT_BRACKET=1 (arm B), the NOP twin (arm Bn),
#                           and a counting diagnostic (bracketcount: executed entries printed at exit)
#                           -> $W/lib-bracket, $W/lib-bracketnop (libk5crypto only; DYLD_LIBRARY_PATH selects)
set -uo pipefail
RIG="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
W="${W:-$HOME/Documents/dit-krb5}"
REPO="$(cd "$RIG/../../.." && pwd)"
LLVM_BUILD="${LLVM_BUILD:-$REPO/build}"; CC="$LLVM_BUILD/bin/clang"; LB="$LLVM_BUILD/bin"
URL="https://kerberos.org/dist/krb5/1.22/$V.tar.gz"
JOBS="${JOBS:-10}"; V=krb5-1.22.2; SRC="$W/src/$V/src"; T="$W/build-base"
info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
[[ -x "$CC" ]] || die "no clang at $CC (set LLVM_BUILD)"
count() { "$LB/llvm-objdump" -d "$1" | grep -ciE '\bmsr\b\s+dit,'; }
case "${1:-all}" in
  base)
    mkdir -p "$W/src"; [[ -d "$SRC" ]] || { info "fetching $URL"; curl -fsSL -o "$W/src/$V.tar.gz" "$URL" && tar xzf "$W/src/$V.tar.gz" -C "$W/src" || die "download failed"; }
    info "libditctl.dylib"; cc -O2 -dynamiclib -o "$W/libditctl.dylib" "$REPO/utils/cio_ditctl.c" || die ditctl
    info "tree -> $T (patched, bracket inert)"; rm -rf "$T"; cp -R "$SRC" "$T"
    python3 "$RIG/patch_bracket.py" "$T" || die patch
    ( cd "$T" && CC="$CC" CFLAGS="-O2" ./configure --prefix="$W/krb5-base" --with-crypto-impl=builtin --with-tls-impl=no \
        --disable-pkinit --without-libedit --disable-nls --without-keyutils --without-lmdb --without-ldap > configure.log 2>&1 \
      && make -j"$JOBS" > build.log 2>&1 && make install > install.log 2>&1 \
      && make -C tests/hammer > hammer.log 2>&1 && make -C tests/create > create.log 2>&1 ) \
      || { tail -30 "$T"/build.log "$T"/configure.log 2>/dev/null | tail -40; die "krb5 build failed"; }
    ls "$W/krb5-base/sbin/krb5kdc" "$T/tests/hammer/kdc5_hammer" "$T/tests/create/kdb5_mkdums" >/dev/null || die "binaries missing"
    info "krb5kdc: msr DIT $(count "$W/krb5-base/sbin/krb5kdc"); libk5crypto: msr DIT $(count "$W"/krb5-base/lib/libk5crypto.*.dylib | head -1)"
    otool -L "$W/krb5-base/sbin/krb5kdc" | grep -E 'k5crypto|libkrb5' ;;
  bracket)
    for v in bracket bracketnop bracketcount; do
      defs="-DDIT_BRACKET=1"; [[ $v == bracketnop ]] && defs="$defs -DDIT_BRACKET_NOP=1"; [[ $v == bracketcount ]] && defs="$defs -DDIT_BRACKET_COUNT=1"
      B="$W/build-$v"; info "variant $v: rebuilding lib/crypto/krb with $defs"
      rm -rf "$B"; cp -R "$T" "$B"
      ( cd "$B" && rm -f lib/crypto/krb/*.o lib/crypto/krb/*.so lib/libk5crypto*.dylib \
        && make -C lib/crypto/krb CPPFLAGS="$defs" > "$W/$v.log" 2>&1 && make -C lib/crypto >> "$W/$v.log" 2>&1 ) || { tail -20 "$W/$v.log"; die "$v failed"; }
      mkdir -p "$W/lib-$v"; rm -f "$W/lib-$v"/*; cp "$B"/lib/libk5crypto*.dylib "$W/lib-$v/"
      lib=$(ls "$W/lib-$v"/libk5crypto.*.*.dylib | head -1)
      info "    $v: msr DIT $(count "$lib"), sb $("$LB/llvm-objdump" -d "$lib" | grep -cE '\tsb$'), mrs DIT $("$LB/llvm-objdump" -d "$lib" | grep -ciE '\bmrs\b.*\bdit\b')"
    done ;;
  all) "$0" base && "$0" bracket ;;
  *) die "usage: $0 base|bracket|all" ;;
esac
info "done ($1)"
