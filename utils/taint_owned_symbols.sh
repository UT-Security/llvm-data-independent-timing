#!/bin/bash
# taint_owned_symbols.sh <object|archive>... > owned.txt
#
# The functions a build DEFINES, one per line, for -taint-owned-symbols. Pass
# the build's own objects or archives (not libc): every defined text symbol,
# global or local, since a seed matches a static by name inside its TU. Feed
# the result back into the next build; the callee contract's obligation report
# then files a callee outside this set as external (out of scope) instead of
# proposing a seed line for code you do not own.
#
#   LLVM_BUILD=<taint clang build dir>   (default: llvm-nm on PATH)
set -euo pipefail
[ $# -ge 1 ] || { echo "usage: $0 <object|archive>..." >&2; exit 1; }
NM=${LLVM_BUILD:+$LLVM_BUILD/bin/llvm-nm}; NM=${NM:-llvm-nm}
"$NM" --defined-only --no-demangle "$@" 2>/dev/null \
  | awk 'NF == 3 && $2 ~ /^[tTwW]$/ { print $3 }' \
  | sed 's/^\.L//' | sort -u
