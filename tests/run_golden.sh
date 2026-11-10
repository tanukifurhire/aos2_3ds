#!/bin/sh
# Golden-trace guard for optimisation work (tests/golden.c).
#   tests/run_golden.sh record   -> writes tests/golden.txt from the current sources
#   tests/run_golden.sh          -> compares the current sources against tests/golden.txt
#   tests/run_golden.sh live     -> compare only the live-state column
#   tests/run_golden.sh check    -> full compare + assert dead pool slots stay zero
#   tests/run_golden.sh poison   -> vacated slots filled with garbage; live state
#                                   must still match (no code reads dead slots)
# FRAMES (default 3000) frames per scenario.
set -e
cd "$(dirname "$0")/.."
ZIG=tools/zig/zig-x86_64-windows-0.14.1/zig.exe
EXTRA=""
[ "${1:-}" = poison ] && EXTRA="-DAOS_DEAD_FILL=0xA5"
FLAGS="-std=gnu11 -Wall -Wextra -Werror -O2 -g $EXTRA"
OUT=build/tests/goldobj${EXTRA:+_poison}
mkdir -p "$OUT"
OBJS=""
for f in src/sim/*.c src/sim/chars/*.c tests/golden.c; do
	o="$OUT/$(echo "$f" | tr / _ | sed 's/\.c$/.o/')"
	"$ZIG" cc $FLAGS -c "$f" -o "$o"
	OBJS="$OBJS $o"
done
EXE=build/tests/golden${EXTRA:+_poison}.exe
"$ZIG" cc $FLAGS $OBJS -lm -o $EXE
FRAMES=${FRAMES:-3000}
if [ "${1:-}" = record ]; then
	$EXE "$FRAMES" | tee tests/golden.txt
	exit 0
fi
$EXE "$FRAMES" ${1:-} > build/tests/golden_now.txt || { tail -3 build/tests/golden_now.txt; exit 1; }
if [ "${1:-}" = live ] || [ "${1:-}" = poison ]; then
	cut -d' ' -f1-5 tests/golden.txt > build/tests/golden_a.txt
	cut -d' ' -f1-5 build/tests/golden_now.txt > build/tests/golden_b.txt
	diff build/tests/golden_a.txt build/tests/golden_b.txt && echo "golden (live state): identical"
else
	diff tests/golden.txt build/tests/golden_now.txt && echo "golden: identical"
fi
