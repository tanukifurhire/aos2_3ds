#!/bin/sh
# Runs a benchmark list in a private Azahar copy ($AZ, default build/azahar_perf) (Old 3DS mode) and waits for "bench: done".
#   tools/bench/run_azahar_bench.sh <bench.txt> <out_log> [3dsx] [timeout_s]
set -u
cd "$(dirname "$0")/../.."
AZ="${AZ:-build/azahar_perf}" # a private copy of the emulator (never the user's tools/azahar)
SD="$AZ/user/sdmc"
BENCH=$1; OUTLOG=$2; X=${3:-build/aos2_3ds.3dsx}; TMO=${4:-1800}
cp "$BENCH" "$SD/aos2_bench.txt"
rm -f "$SD/aos2_prof.txt"
"$AZ/azahar.exe" "$X" >/dev/null 2>&1 &
PID=$!
t=0
while [ $t -lt "$TMO" ]; do
	sleep 5; t=$((t+5))
	if grep -q "bench: done" "$SD/aos2_prof.txt" 2>/dev/null; then break; fi
done
powershell -NoProfile -Command 'Get-Process azahar -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "*azahar_perf*" } | Stop-Process -Force' >/dev/null 2>&1
wait $PID 2>/dev/null
cp "$SD/aos2_prof.txt" "$OUTLOG" 2>/dev/null
rm -f "$SD/aos2_bench.txt"
grep -c "^summary" "$OUTLOG" 2>/dev/null; grep "bench: done" "$OUTLOG" >/dev/null && echo DONE || echo "TIMEOUT after $t s"
