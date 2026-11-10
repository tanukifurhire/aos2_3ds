#!/bin/sh
# Builds a texture variant for the memory plan (docs/PERF.md): packs the given folders with extra
# aos2pack options into build/var/<name>/{gfx,romfs/gfx} and converts the pages with tex3ds.
#   tools/bench/make_tex_variant.sh <name> <folders,comma,separated> [aos2pack options...]
# Install into the 3dsx romfs with tools/bench/use_tex_variant.sh <name>.
set -eu
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
NAME=$1; ONLY=$2; shift 2
V="$ROOT/build/var/$NAME"
rm -rf "$V"; mkdir -p "$V/gfx" "$V/romfs"
(cd tools/aos2pack && go build -o aos2pack.exe .)
tools/aos2pack/aos2pack.exe -assets "$ROOT/extracted" -out "$V/romfs" -gfx "$V/gfx" -only "$ONLY" "$@" >/dev/null
rm -rf "$V/romfs/se"
TEX3DS="$ROOT/tools/devkitpro/opt/devkitpro/tools/bin/tex3ds.exe"
export TEX3DS V
find "$V/gfx" -name 'page*.t3s' -print0 | xargs -0 -P 8 -n 1 sh -c '
	t3s=$1; dir=$(dirname "$t3s"); folder=$(basename "$dir"); page=$(basename "$t3s" .t3s)
	(cd "$dir" && "$TEX3DS" -i "$page.t3s" -o "$V/romfs/gfx/${folder}_${page#page}.t3x" >/dev/null) || echo "FAIL $folder $page"' sh
ls "$V/romfs/gfx" | wc -l
