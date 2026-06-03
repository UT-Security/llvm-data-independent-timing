#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

find "${SCRIPT_DIR}" -maxdepth 1 -type f \( \
  -name "*.ll" -o \
  -name "*.annotated.ll" -o \
  -name "*.pe.mir" -o \
  -name "*.hardened.mir" -o \
  -name "*.hardened.o" -o \
  -name "*.hardened" -o \
  -name "*.tainted.txt" -o \
  -name "*.tainted_regions.txt" -o \
  -name "*.tainted_source_regions.txt" -o \
  -name "*.tainted_src.txt" -o \
  -name "*.tainted_stats.txt" -o \
  -name "*.tainted_trace.txt" -o \
  -name "*.hardened_source.c" -o \
  -name "*_tainted.txt" -o \
  -name "*_tainted_regions.txt" -o \
  -name "*_tainted_source_regions.txt" -o \
  -name "*_tainted_src.txt" -o \
  -name "*_tainted_stats.txt" -o \
  -name "*_tainted_trace.txt" -o \
  -name "*_hardened_source.c" -o \
  -name "*.o" \
  \) -print -delete
