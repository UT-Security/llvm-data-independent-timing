#!/bin/bash
# Build a regtest chain with real transaction volume, so -reindex-chainstate
# exercises the LevelDB / coins-cache / deserialization paths under load rather
# than replaying empty blocks.
set -euo pipefail
D=${1:-$HOME/btcregtest}
BLOCKS=${2:-400}
OUTS=${3:-120}
BIN=$HOME/Documents/bitcoin/build/bin
rm -rf "$D"; mkdir -p "$D"
CLI="$BIN/bitcoin-cli -regtest -datadir=$D"
"$BIN/bitcoind" -regtest -datadir="$D" -daemon=1 -fallbackfee=0.0002 -printtoconsole=0
for i in $(seq 60); do $CLI getblockchaininfo >/dev/null 2>&1 && break; sleep 1; done
$CLI createwallet bench >/dev/null
ADDR=$($CLI getnewaddress)
$CLI generatetoaddress 300 "$ADDR" >/dev/null   # mature coinbases
echo "matured; building $BLOCKS blocks x ~$OUTS outputs"
for b in $(seq "$BLOCKS"); do
  # one sendmany with many outputs = many UTXOs and a fat block
  ARGS='{'; for o in $(seq $OUTS); do A=$($CLI getnewaddress); ARGS="$ARGS\"$A\":0.0001,"; done
  ARGS="${ARGS%,}}"
  $CLI sendmany "" "$ARGS" >/dev/null 2>&1 || true
  $CLI generatetoaddress 1 "$ADDR" >/dev/null
  if [ $((b % 50)) -eq 0 ]; then echo "  block $b/$BLOCKS  utxos=$($CLI gettxoutsetinfo 2>/dev/null | grep -o '"txouts": *[0-9]*' | grep -o '[0-9]*')"; fi
done
echo "final: height=$($CLI getblockcount) size=$(du -sh $D | cut -f1)"
$CLI stop; sleep 3
