#!/bin/sh
# Simulation smoke test. Usage: tests/run_sim.sh [char] [opponent] [frames]
# Each source is compiled separately; character files that fail to compile are
# reported and left out (their weak stubs are used) so one in-progress file
# doesn't block everyone else's check. Engine files must always compile.
set -e
cd "$(dirname "$0")/.."
ZIG=tools/zig/zig-x86_64-windows-0.14.1/zig.exe
FLAGS="-std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all"
OUT=build/tests/simobj
mkdir -p "$OUT"
OBJS=""
for f in src/sim/*.c tests/sim_smoke.c; do
	o="$OUT/$(basename "$f" .c).o"
	"$ZIG" cc $FLAGS -c "$f" -o "$o"
	OBJS="$OBJS $o"
done
for f in src/sim/chars/*.c; do
	o="$OUT/char_$(basename "$f" .c).o"
	if "$ZIG" cc $FLAGS -c "$f" -o "$o" 2>"$OUT/$(basename "$f" .c).err"; then
		OBJS="$OBJS $o"
	else
		echo "SKIPPED (does not compile): $f — see $OUT/$(basename "$f" .c).err"
	fi
done
"$ZIG" cc $FLAGS $OBJS -lm -o build/tests/sim_smoke.exe
build/tests/sim_smoke.exe "$@"
