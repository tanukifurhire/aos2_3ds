#!/bin/sh
# Overlays texture variants (build/var/<name>/romfs/{gfx,stage/tex}, from make_tex_variant.sh or
# aos2mesh -texdiv) onto the original sets (saved once as build/romfs_gfx_base, build/romfs_stagetex_base)
# and rebuilds the profile .3dsx as build/aos2_3ds_<out>.3dsx.
#   tools/bench/use_tex_variant.sh <out> [variant...]     (no variant = the original textures)
set -eu
cd "$(dirname "$0")/../.."
OUTNAME=$1; shift
[ -d build/romfs_gfx_base ] || cp -r build/romfs/gfx build/romfs_gfx_base
[ -d build/romfs_stagetex_base ] || cp -r build/romfs/stage/tex build/romfs_stagetex_base
rm -rf build/romfs/gfx build/romfs/stage/tex
cp -r build/romfs_gfx_base build/romfs/gfx
cp -r build/romfs_stagetex_base build/romfs/stage/tex
for NAME in "$@"; do
	V=build/var/$NAME/romfs
	if [ -d "$V/gfx" ]; then
		for f in "$V"/gfx/*.idx; do
			rm -f "build/romfs/gfx/$(basename "$f" .idx)"_*.t3x
		done
		cp "$V"/gfx/* build/romfs/gfx/
	fi
	if [ -d "$V/stage/tex" ]; then
		rm -rf build/romfs/stage/tex
		cp -r "$V/stage/tex" build/romfs/stage/tex
	fi
done
PROFILE=1 SKIP_ASSETS=1 ./build.sh >/dev/null
cp build/aos2_3ds.3dsx "build/aos2_3ds_$OUTNAME.3dsx"
ls -la "build/aos2_3ds_$OUTNAME.3dsx"
