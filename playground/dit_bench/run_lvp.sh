#!/bin/sh
# Apple Load Value Predictor (LVP) x PSTATE.DIT measurements on Apple M3/M4/A17.
# Motivated by FLOP (Kim/Chuang/Genkin/Yarom, USENIX Security 2025): the LVP
# predicts constant load values and breaks RAW dependencies; DIT disables it.
# Requires FEAT_DIT hardware WITH an LVP (M3/M4/A17 Pro; NOT M2/M1/A15/A16).
set -e
cd "$(dirname "$0")"
CC=${CC:-cc}
echo "### lvp_dit: does the LVP exist on this core, and does DIT disable it? ###"
$CC -O2 lvp_dit.c -o /tmp/lvp_dit && /tmp/lvp_dit
echo
echo "### lvp_finegrain: fine-grain vs whole-DIT vs unprotected (crossover) ###"
$CC -O2 lvp_finegrain.c -o /tmp/lvp_finegrain && /tmp/lvp_finegrain
echo
echo "### lvp_gather: FLOP Listing 1 gather (NEGATIVE from userspace - see header) ###"
$CC -O2 lvp_gather.c -o /tmp/lvp_gather && /tmp/lvp_gather 100000 0
