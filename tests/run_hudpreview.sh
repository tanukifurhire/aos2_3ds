#!/bin/sh
# Render the HUD port (src/platform/hud.c) on the host with the original PC
# textures, at the PC layout (800x600) or the 3DS one (400x240).
# Usage: tests/run_hudpreview.sh <p1 id> <p2 id> <frames> <W> <H> <outdir> [setup] [zoom]
#   e.g. tests/run_hudpreview.sh 10 14 100,260,700 400 240 build/hud "hyper=2.6,hp=2000,combo" 2
# (setup keys: see tests/hudpreview.c)
set -e
cd "$(dirname "$0")/.."
ZIG=tools/zig/zig-x86_64-windows-0.14.1/zig.exe
mkdir -p build/tests/hudobj "$6"
OBJS=""
for f in src/sim/*.c src/sim/chars/*.c src/platform/hud.c tests/hudpreview.c; do
	o="build/tests/hudobj/$(echo "$f" | tr / _).o"
	"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -c "$f" -o "$o"
	OBJS="$OBJS $o"
done
"$ZIG" cc $OBJS -lm -o build/tests/hudpreview.exe
build/tests/hudpreview.exe "$1" "$2" "$3" "$4" "$5" "$6/hud.tsv" "${7:-}"
(cd tools/drawpng && go build -o drawpng.exe .)
tools/drawpng/drawpng.exe -hud -in "$6/hud.tsv" -tex extracted/texture -out "$6" -zoom "${8:-1}"
ls "$6" | grep -c png
