#!/bin/sh
# Texture usage survey for the memory plan (tests/texusage.c). Usage: tests/run_texusage.sh <frames> <out.tsv> a b [a b ...]
set -e
cd "$(dirname "$0")/.."
ZIG=tools/zig/zig-x86_64-windows-0.14.1/zig.exe
FLAGS="-std=gnu11 -O2 -g"
OUT=build/tests/texuobj
mkdir -p "$OUT"
OBJS=""
for f in src/sim/*.c src/sim/chars/*.c tests/texusage.c; do
	o="$OUT/$(echo "$f" | tr / _ | sed 's/\.c$/.o/')"
	if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then "$ZIG" cc $FLAGS -c "$f" -o "$o"; fi
	OBJS="$OBJS $o"
done
"$ZIG" cc $FLAGS $OBJS -lm -o build/tests/texusage.exe
F=$1; O=$2; shift 2
build/tests/texusage.exe "$F" build/romfs/gfx "$O" "$@"
