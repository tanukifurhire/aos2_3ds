#!/bin/sh
# Converts every build/gfx/<folder>/pageN.t3s into build/romfs/gfx/<folder>_N.t3x
# with tex3ds, 8 in parallel. Skips pages whose .t3x is newer than the .t3s.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TEX3DS="$ROOT/tools/devkitpro/opt/devkitpro/tools/bin/tex3ds.exe"
OUT="$ROOT/build/romfs/gfx"
mkdir -p "$OUT"

convert_one() {
	t3s="$1"
	dir=$(dirname "$t3s")
	folder=$(basename "$dir")
	page=$(basename "$t3s" .t3s)
	dst="$OUT/${folder}_${page#page}.t3x"
	if [ -f "$dst" ] && [ "$dst" -nt "$t3s" ]; then
		return 0
	fi
	if ! (cd "$dir" && "$TEX3DS" -i "$page.t3s" -o "$dst" >/dev/null 2>"$ROOT/build/gfx/${folder}_${page}.err"); then
		echo "FAIL $folder $page: $(cat "$ROOT/build/gfx/${folder}_${page}.err")"
		return 1
	fi
}

if [ "${1:-}" = "--one" ]; then
	convert_one "$2"
	exit
fi
find "$ROOT/build/gfx" -name 'page*.t3s' -print0 | xargs -0 -P 8 -n 1 "$0" --one
