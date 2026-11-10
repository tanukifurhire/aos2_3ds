#!/bin/sh
# Render frames of a scripted scenario to PNGs with the real textures.
# Usage: tests/run_drawdump.sh <p1> <p2> <script> <frames> <outdir>
# (see tests/drawdump.c for the script syntax). Example:
#   tests/run_drawdump.sh suguri kae "near,230:-,1:a,60:-" 230-290 build/dd/sg_rifle
set -e
cd "$(dirname "$0")/.."
ZIG=tools/zig/zig-x86_64-windows-0.14.1/zig.exe
mkdir -p build/tests/ddobj "$5"
OBJS=""
for f in src/sim/*.c src/sim/chars/*.c tests/drawdump.c; do
	o="build/tests/ddobj/$(echo "$f" | tr / _).o"
	if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ src/sim/sim.h -nt "$o" ] || [ src/sim/objects.h -nt "$o" ]; then
		"$ZIG" cc -std=gnu11 -O1 -c "$f" -o "$o" 2>/dev/null || { echo "skip $f"; continue; }
	fi
	OBJS="$OBJS $o"
done
"$ZIG" cc $OBJS -lm -o build/tests/drawdump.exe
build/tests/drawdump.exe "$1" "$2" "$3" "$4" "$5/dump.tsv"
(cd tools/drawpng && [ -f go.mod ] || go mod init drawpng >/dev/null 2>&1; go build -o drawpng.exe .)
tools/drawpng/drawpng.exe -in "$5/dump.tsv" -tex extracted/texture -out "$5"
ls "$5" | grep -c png
