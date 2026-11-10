#!/bin/sh
# Host preview of a stage background (tests/stagepreview.c) with the real assets.
# Usage: tests/run_stagepreview.sh <stageId> <ticks> <zoom> <out.png> [-3ds]
# Needs the converted assets: build/romfs/stage (tools/aos2mesh, run by ./build.sh)
# and build/stagehost/tex (aos2mesh -host, also run by ./build.sh).
set -e
cd "$(dirname "$0")/.."
ZIG=tools/zig/zig-x86_64-windows-0.14.1/zig.exe
mkdir -p build/tests
"$ZIG" cc -std=gnu11 -O2 -Wall -Wextra -Werror -o build/tests/stagepreview.exe \
	tests/stagepreview.c src/stage/*.c src/sim/rng.c -lm
build/tests/stagepreview.exe "$@"
