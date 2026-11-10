#!/bin/sh
# Captures top-screen frames of one deterministic CPU-vs-CPU bench match (profile build) at the given (N-1, N, N+1 for each N;
# the read-back can be a frame off) and converts them to PNG.
#   tools/bench/run_capture.sh <3dsx> "<p1 p2 stage frames hyper diff [flags]>" <outdir> <frame>...
set -u
cd "$(dirname "$0")/../.."
X=$1; SCEN=$2; OUT=$3; shift 3
SD="${AZ:-build/azahar_perf}/user/sdmc"
rm -rf "$SD/cap"; : > "$SD/aos2_cap.txt"
for f in "$@"; do echo "m $((f - 1)) 3" >> "$SD/aos2_cap.txt"; done
echo "$SCEN" > build/cap_bench.txt
mkdir -p "$(dirname "$OUT")"
tools/bench/run_azahar_bench.sh build/cap_bench.txt "$OUT.log" "$X" 900 >/dev/null
rm -f "$SD/aos2_cap.txt"
mkdir -p "$OUT"
python tools/capconv.py "$SD/cap" "$OUT" && ls "$OUT"
