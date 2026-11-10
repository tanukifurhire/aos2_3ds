#!/bin/sh
# One-time setup on a fresh clone (Windows + Git Bash). Recreates the git-ignored, project-local
# toolchains the build and tests expect:
#   tools/devkitpro/   devkitARM, libctru, citro2d/3d, tremor/ogg, tex3ds, picasso, 3dstools (via dkpfetch)
#   tools/zig/         zig 0.14.1 (host C compiler for tests/*.sh)
# and checks for Go and the game install path. Safe to re-run; finished steps are skipped.
#
# Requirements: Go (https://go.dev/dl/) on PATH, internet access.
set -eu
cd "$(dirname "$0")/.."
ROOT=$(pwd)

command -v go >/dev/null 2>&1 || { echo "error: Go is not on PATH (install from https://go.dev/dl/)" >&2; exit 1; }

# 1. devkitPro packages (project-local; nothing installed system-wide)
DKP="$ROOT/tools/devkitpro/opt/devkitpro"
if [ -x "$DKP/tools/bin/tex3ds.exe" ] && [ -x "$DKP/devkitARM/bin/arm-none-eabi-gcc.exe" ] && [ -f "$DKP/portlibs/3ds/lib/libvorbisidec.a" ]; then
	echo "devkitPro: already in tools/devkitpro"
else
	echo "devkitPro: fetching packages into tools/devkitpro (a few hundred MB)..."
	(cd tools/dkpfetch && go run . -root "$ROOT/tools/devkitpro" \
		devkitARM libctru citro2d citro3d 3ds-libogg 3ds-libvorbisidec tex3ds picasso 3dstools general-tools)
fi

# 2. zig (host compiler used by the test scripts)
ZIGV=0.14.1
ZIGDIR="$ROOT/tools/zig/zig-x86_64-windows-$ZIGV"
if [ -x "$ZIGDIR/zig.exe" ]; then
	echo "zig: already in tools/zig"
else
	echo "zig: downloading $ZIGV..."
	mkdir -p "$ROOT/tools/zig"
	curl -fL -o "$ROOT/tools/zig/zig.zip" "https://ziglang.org/download/$ZIGV/zig-x86_64-windows-$ZIGV.zip"
	powershell.exe -NoProfile -Command "Expand-Archive -Force -Path '$(cygpath -w "$ROOT/tools/zig/zig.zip")' -DestinationPath '$(cygpath -w "$ROOT/tools/zig")'"
	rm -f "$ROOT/tools/zig/zig.zip"
fi

# 3. game install path (assets are extracted from your own copy of the game)
if [ -f "$ROOT/game_path.txt" ]; then
	echo "game: $(head -n1 "$ROOT/game_path.txt")"
elif [ -d "$ROOT/../Acceleration of SUGURI 2" ]; then
	echo "game: using ../Acceleration of SUGURI 2"
else
	echo "game: create game_path.txt containing the path to your Acceleration of SUGURI 2 install"
	echo "      (folder with aos2.exe), e.g. /c/Program Files (x86)/Steam/steamapps/common/Acceleration of SUGURI 2"
fi

# 4. emulator (optional): the project uses a portable Azahar in tools/azahar for testing.
[ -d "$ROOT/tools/azahar" ] || echo "emulator (optional): unzip a portable Azahar build into tools/azahar/"

echo "setup done. Next: ./build.sh (first run extracts and packs all assets), then ./tests/run_tests.sh"
