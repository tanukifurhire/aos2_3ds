#!/bin/sh
# Host cost proxy (tests/perf_proxy.c): per-tick operation counts for CPU vs CPU
# matches. Usage: tests/run_perf.sh [frames] [a b mode]...
set -e
cd "$(dirname "$0")/.."
ZIG=tools/zig/zig-x86_64-windows-0.14.1/zig.exe
FLAGS="-std=gnu11 -Wall -Wextra -Werror -O2 -g -DAOS_PERF"
OUT=build/tests/perfobj
mkdir -p "$OUT"
OBJS=""
for f in src/sim/*.c src/sim/chars/*.c tests/perf_proxy.c; do
	o="$OUT/$(echo "$f" | tr / _ | sed 's/\.c$/.o/')"
	"$ZIG" cc $FLAGS -c "$f" -o "$o"
	OBJS="$OBJS $o"
done
"$ZIG" cc $FLAGS $OBJS -lm -o build/tests/perf_proxy.exe
build/tests/perf_proxy.exe "$@"
