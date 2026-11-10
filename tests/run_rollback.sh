#!/bin/sh
# Rollback netplay test (two peers over a fake lossy link, see tests/test_rollback.c).
# Usage: tests/run_rollback.sh [p1 engine id] [p2 engine id] [frames]
# Like run_sim.sh, character files that don't compile are skipped (weak stubs).
set -e
cd "$(dirname "$0")/.."
ZIG=tools/zig/zig-x86_64-windows-0.14.1/zig.exe
FLAGS="-std=gnu11 -Wall -Wextra -Werror -O2 -g -fsanitize=undefined -fno-sanitize-recover=all"
OUT=build/tests/rbobj
mkdir -p "$OUT"
OBJS=""
for f in src/sim/*.c src/net/rollback.c tests/test_rollback.c; do
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
"$ZIG" cc $FLAGS $OBJS -lm -o build/tests/test_rollback.exe
build/tests/test_rollback.exe "$@"
