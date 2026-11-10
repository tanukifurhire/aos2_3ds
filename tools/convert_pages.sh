#!/bin/sh
# Converts every build/gfx/<folder>/pageN.t3s into build/romfs/gfx/<folder>_N.t3x
# with tex3ds, 8 in parallel. Skips pages whose .t3x is newer than the .t3s.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/tools/devkitpro_env.sh" # fresh clone: run ./tools/setup.sh first
TEX3DS="$TOOLS/tex3ds.exe"
[ -x "$TEX3DS" ] || TEX3DS="$TOOLS/tex3ds"
BOUT="${AOS_OUT:-$ROOT/build}"
OUT="$BOUT/romfs/gfx"
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
	if ! (cd "$dir" && "$TEX3DS" -i "$page.t3s" -o "$dst" >/dev/null 2>"$BOUT/gfx/${folder}_${page}.err"); then
		echo "FAIL $folder $page: $(cat "$BOUT/gfx/${folder}_${page}.err")"
		return 1
	fi
}

if [ "${1:-}" = "--one" ]; then
	convert_one "$2"
	exit
fi
# drop pages a repack no longer has (aos2pack rewrote the folder's t3s set)
for t3x in "$OUT"/*.t3x; do
	[ -e "$t3x" ] || continue
	b=$(basename "$t3x" .t3x); folder=${b%_*}; n=${b##*_}
	case $n in '' | *[!0-9]*) continue ;; esac
	[ -d "$BOUT/gfx/$folder" ] && [ ! -f "$BOUT/gfx/$folder/page$n.t3s" ] && rm -f "$t3x"
done
find "$BOUT/gfx" -name 'page*.t3s' -print0 | xargs -0 -P 8 -n 1 "$0" --one
