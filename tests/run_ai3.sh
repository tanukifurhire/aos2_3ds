#!/bin/sh
# CPU-vs-CPU determinism / rollback test for the Tsih, Mira and Iru AI.
# Usage: tests/run_ai3.sh [frames]
set -e
cd "$(dirname "$0")/.."
ZIG=tools/zig/zig-x86_64-windows-0.14.1/zig.exe
FLAGS="-std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all"
OUT=build/tests/ai3obj
mkdir -p "$OUT"
OBJS=""
for f in src/sim/*.c tests/sim_ai3.c; do
	o="$OUT/$(basename "$f" .c).o"
	"$ZIG" cc $FLAGS -c "$f" -o "$o"
	OBJS="$OBJS $o"
done
# character files: in-progress ones that fail to compile are left out (weak stubs),
# except the three AI files under test, which must compile.
for f in src/sim/chars/*.c; do
	o="$OUT/char_$(basename "$f" .c).o"
	case "$f" in *ai_tsih.c|*ai_mira.c|*ai_iru.c) "$ZIG" cc $FLAGS -c "$f" -o "$o"; OBJS="$OBJS $o"; continue;; esac
	if "$ZIG" cc $FLAGS -c "$f" -o "$o" 2>"$OUT/$(basename "$f" .c).err"; then
		OBJS="$OBJS $o"
	else
		echo "SKIPPED (does not compile): $f"
	fi
done
"$ZIG" cc $FLAGS $OBJS -lm -o build/tests/sim_ai3.exe
build/tests/sim_ai3.exe "$@"
