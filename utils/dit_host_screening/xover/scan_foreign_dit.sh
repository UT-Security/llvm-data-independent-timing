#!/bin/bash
# Check a linked binary for PSTATE.DIT writes that we did not put there.
#
# WHY. docs/design/dit-abi.md §4 scopes the callee-saved convention as "soundness
# against a hardened build plus arbitrary NON-DIT-WRITING code". That is an
# assumption about everything else in the link, and until now it was argued once
# in prose rather than checked per binary. The four systems known to write DIT --
# Apple timingsafe_*, corecrypto, Go crypto/subtle, OpenSSL PR #28764 -- all raise
# and restore, so they satisfy the one-directional guarantee; but a binary that
# links something else that writes DIT is outside what the ABI claims, and nobody
# would find out.
#
# This turns that into a per-binary check: every function containing an MSR/MRS
# DIT must come either from the instrumented library or from a file we name.
# Anything else is reported.
#
#   scan_foreign_dit.sh <binary> <ours.o|ours.a> [more...]
#
# Exit 1 if an unaccounted-for DIT write is found.
set -uo pipefail

L=${L:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/build/bin}
[[ -x "$L/llvm-objdump" ]] || { echo "no llvm-objdump at $L - build it (ninja -C <repo>/build llvm-objdump) or set L" >&2; exit 1; }
[[ $# -ge 2 ]] || { echo "usage: $0 <binary> <ours.o|ours.a> [more...]" >&2; exit 2; }
BIN=$1; shift

# Symbols defined by everything we compiled ourselves. -a to catch local symbols
# too: the pass instruments static functions, which are not in the dynamic table.
# Validate BEFORE the pipeline: a `for` loop piped into sort runs in a subshell,
# where `exit 2` kills only the subshell and the scan carries on with an empty
# symbol set -- which reports every DIT site as foreign.
for f in "$@"; do
    [[ -f "$f" ]] || { echo "missing: $f" >&2; exit 2; }
done

OURS=$(mktemp); FOUND=$(mktemp); trap 'rm -f "$OURS" "$FOUND"' EXIT
for f in "$@"; do
    "$L/llvm-nm" --defined-only "$f" 2>/dev/null | awk '{print $NF}'
done | sort -u > "$OURS"
[[ -s "$OURS" ]] || { echo "no symbols from: $* -- every site would look foreign" >&2; exit 2; }

# Every function in the LINKED binary that writes or reads DIT.
"$L/llvm-objdump" -d --no-show-raw-insn "$BIN" | awk '
    /^[0-9a-f]+ <.+>:/ { s=$2; gsub(/[<>:]/,"",s); next }
    /(msr|mrs)[[:space:]]+DIT/ { c[s]++ }
    END { for (k in c) printf "%s %d\n", k, c[k] }' | sort > "$FOUND"

total=$(awk '{t+=$2} END{print t+0}' "$FOUND")
printf 'DIT sites in %s: %s across %s functions\n' "$(basename "$BIN")" "$total" "$(wc -l < "$FOUND")"

foreign=0
while read -r fn n; do
    # Strip the local-symbol suffixes the linker adds (foo.1, foo.llvm.123).
    base=${fn%%.llvm.*}; base=${base%.[0-9]*}
    if ! grep -qxF "$fn" "$OURS" && ! grep -qxF "$base" "$OURS"; then
        printf '  FOREIGN %-48s %s DIT instruction(s)\n' "$fn" "$n"
        foreign=$((foreign+1))
    fi
done < "$FOUND"

if [[ "$foreign" -eq 0 ]]; then
    echo "  ok: every DIT site belongs to a file we compiled"
    exit 0
fi
cat >&2 <<MSG

  $foreign function(s) outside the hardened objects write PSTATE.DIT.
  The ABI's guarantee is one-directional, so a callee that RAISES and restores is
  still conformant -- check each one before treating this as a failure. What is
  NOT conformant is a callee that lowers DIT below what it received.
MSG
exit 1
