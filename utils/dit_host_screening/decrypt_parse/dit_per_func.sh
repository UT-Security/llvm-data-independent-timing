#!/bin/bash
# usage: dit_per_func.sh <object or archive>  -> "count function" lines, sorted
OBJDUMP=/home/rgangar/Documents/llvm-data-independent-timing/build/bin/llvm-objdump
"$OBJDUMP" -d --no-show-raw-insn "$1" | awk '
  /^[0-9a-f]+ <.*>:$/ { fn=$2; gsub(/[<>:]/,"",fn); next }
  tolower($0) ~ /msr[ \t]+dit/ { c[fn]++ }
  END { for (f in c) printf "%6d %s\n", c[f], f }' | sort -rn
