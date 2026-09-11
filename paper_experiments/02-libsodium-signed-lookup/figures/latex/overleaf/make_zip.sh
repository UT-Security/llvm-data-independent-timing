#!/usr/bin/env bash
# Build the Overleaf bundle: main.tex plus the three panel PDFs, flat, zipped.
#
#   bash paper_experiments/02-libsodium-signed-lookup/figures/latex/overleaf/make_zip.sh
#
# Then in Overleaf: New Project -> Upload Project -> the zip; set main.tex as
# the main document and compile. Flat paths, so there is nothing to configure.
#
# The PDFs are NOT committed here -- they would be a second copy of
# ../../crossover-panel-*.pdf, which is how two copies drift apart. This copies
# the real ones at build time, so the bundle cannot show stale panels.
set -euo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIG="$(cd "$D/../.." && pwd)"
PANELS=(crossover-panel-a-m4.pdf crossover-panel-b-gem5.pdf)
for f in "${PANELS[@]}"; do
  [[ -f "$FIG/$f" ]] || { echo "missing $FIG/$f -- run fig_exp02_silicon.py first" >&2; exit 1; }
done
OUT="${1:-$D/dit-crossover-overleaf.zip}"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
cp "$D/main.tex" "$TMP/"
for f in "${PANELS[@]}"; do cp "$FIG/$f" "$TMP/"; done
# Compile the STAGED copy before zipping. main.tex cannot be compiled where it
# lives -- the panels sit one directory up and are copied in here -- so this is
# the only place the bundle can be checked, and a bundle that does not build is
# worse than no bundle.
if command -v tectonic >/dev/null 2>&1; then
  if ( cd "$TMP" && tectonic -X compile main.tex >/dev/null 2>&1 ); then
    echo "bundle compiles (tectonic)"
  else
    echo "the staged bundle does NOT compile -- not writing a zip" >&2
    ( cd "$TMP" && tectonic -X compile main.tex 2>&1 | tail -20 ) >&2
    exit 1
  fi
  rm -f "$TMP/main.pdf"
else
  echo "note: tectonic not on PATH, bundle not compile-checked"
fi

rm -f "$OUT"
( cd "$TMP" && zip -q "$OUT" main.tex "${PANELS[@]}" )
echo "wrote $OUT"
unzip -l "$OUT" | tail -n +4 | head -5
