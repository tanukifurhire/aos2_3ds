#!/bin/sh
# Builds aos2_3ds.3dsx with the project-local devkitARM (no make/msys needed).
#   ./build.sh            full build (extract -> pack -> compile)
#   SKIP_ASSETS=1 ./build.sh   reuse build/romfs
set -eu
cd "$(dirname "$0")"
ROOT=$(pwd)
# Game install: $GAME, else the path in game_path.txt (untracked), else ../Acceleration of SUGURI 2
if [ -z "${GAME:-}" ] && [ -f "$ROOT/game_path.txt" ]; then GAME=$(head -n1 "$ROOT/game_path.txt" | tr -d '\015'); fi
GAME=${GAME:-"$ROOT/../Acceleration of SUGURI 2"}
export DEVKITPRO="$ROOT/tools/devkitpro/opt/devkitpro"
export DEVKITARM="$DEVKITPRO/devkitARM"
CC="$DEVKITARM/bin/arm-none-eabi-gcc"
TOOLS="$DEVKITPRO/tools/bin"
OUT="${AOS_OUT:-$ROOT/build}" # AOS_OUT: private output dir (parallel builds); default build/
mkdir -p "$OUT/obj"

if [ "${SKIP_ASSETS:-0}" != 1 ]; then
	if [ ! -d "$ROOT/extracted/texture" ]; then
		(cd tools/aos2extract && go build -o aos2extract.exe . && ./aos2extract.exe -game "$GAME" -out "$ROOT/extracted" -skip-bgm)
	fi
	[ -d "$ROOT/extracted/bgm" ] || (cd tools/aos2extract && ./aos2extract.exe -game "$GAME" -out "$ROOT/extracted")
	# Sprite atlases (docs/PERF.md "Texture plan"): VFX, system overlays and characters at 1/4 of the PC art, the
	# big magnified beam/laser frames at 1/2, everything the match renderer draws stored premultiplied (-pma, the
	# one-blend-state batcher src/platform/sprbatch.h; premultiplied folders get no mipmaps). The result-screen art
	# res_* in the character folders is not drawn from the atlases (seltex.go packs it separately): left out.
	PACK_SCALES="effectMdl=0.25,beamMdl=0.25,particleMdl=0.25,chara*=0.25,systemMdl/noise*=0.25,systemMdl/speedline=0.25,systemMdl/loading=0.25"
	PACK_SCALES="$PACK_SCALES,*/hm_laser=0.5,*/ir_laser=0.5,*/ky_lsr=0.5,*/nk_lsr=0.5,*/nt_laser0*=0.5,*/sr_laser=0.5,*/sb_laser=0.5,*/sg_lsr0*=0.5,*/sg_slash=0.5,*/sa_laser*=0.5,*/sab_laser*=0.5"
	PACK_PMA="effectMdl,beamMdl,particleMdl,msgMdl,chara*"
	(cd tools/aos2pack && go build -o aos2pack.exe . && ./aos2pack.exe -assets "$ROOT/extracted" -out "$OUT/romfs" -gfx "$OUT/gfx" -bgm "$ROOT/docs/re/bgm_table.tsv" 		-fscale "$PACK_SCALES" -pma "$PACK_PMA" -exclude "chara*/res_*")
	./tools/convert_pages.sh
	# --- story conversations (Part C; docs/FORMATS.md "Story conversations"): exe resources -> extracted/script_res,
	# scripts + story texts + portraits -> romfs/story
	[ -d "$ROOT/extracted/script_res" ] || (cd tools/aos2extract && go build -o aos2extract.exe . && ./aos2extract.exe -game "$GAME" -out "$ROOT/extracted" -conv-only)
	(cd tools/aos2story && go build -o aos2story.exe .) && ./tools/aos2story/aos2story.exe -assets "$ROOT/extracted" -romfs "$OUT/romfs" -work "$OUT/storygfx" -tex3ds "$TOOLS/tex3ds.exe"
	# --- end story conversations
	# stage backgrounds (docs/re/stages.md): .x meshes, 3dtex.dat, bgMdl textures, height maps
	(cd tools/aos2mesh && go build -o aos2mesh.exe .) && ./tools/aos2mesh/aos2mesh.exe -game "$GAME" 		-tex "$ROOT/extracted/texture" -romfs "$OUT/romfs" -host "$OUT/stagehost" -tex3ds "$TOOLS/tex3ds.exe" -work "$OUT/stagetex" -texdiv 2 # stage textures at 1/2 (PERF.md)
	"$TOOLS/smdhtool.exe" --create "AoS2 3DS input lab" "Acceleration of SUGURI 2 port - WIP" "personal research build" \
		"$OUT/gfx/icon.png" "$OUT/aos2_3ds.smdh" 2>/dev/null || \
		"$TOOLS/smdhtool.exe" --create "AoS2 3DS input lab" "Acceleration of SUGURI 2 port - WIP" "personal research build" \
		"$DEVKITPRO/libctru/default_icon.png" "$OUT/aos2_3ds.smdh"
fi

mkdir -p "$OUT/romfs/stage"
"$TOOLS/picasso.exe" -o "$OUT/romfs/stage/stage.shbin" src/platform/stage.v.pica
mkdir -p "$OUT/romfs/shader"
"$TOOLS/picasso.exe" -o "$OUT/romfs/shader/sprite.shbin" src/platform/sprite.v.pica # 2D batcher (sprbatch.c)

ARCH="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft"
PROF=""
[ "${PROFILE:-0}" = 1 ] && PROF="-DAOS_PROFILE" # on-device profiler + sdmc:/aos2_bench.txt runner (src/app/prof.h)
CFLAGS="-g -O2 $PROF -Wall -Wextra -Werror -Wno-stringop-overread -std=gnu11 -ffunction-sections $ARCH -D__3DS__ \
 -I$DEVKITPRO/libctru/include -I$DEVKITPRO/portlibs/3ds/include -I$OUT"
SRCS="src/net/rollback.c src/net/uds_link.c src/app/netplay.c src/input/aos_input.c src/app/wav.c src/app/audio.c src/app/music.c src/app/lab.c src/app/prof.c src/app/main.c src/app/scene.c src/app/scene_core.c src/app/strings.c src/app/bmfont.c src/app/text_c2d.c src/app/ui.c src/app/menu.c src/app/menu_draw.c src/platform/spritebank.c src/platform/bgm.c src/platform/render.c src/platform/sprbatch.c src/platform/stage.c $(echo src/stage/*.c) src/platform/hud.c src/platform/hud_c2d.c src/platform/panel.c $(echo src/sim/*.c src/sim/chars/*.c) src/modes/ladder.c src/modes/score.c src/modes/save.c src/app/modes_flow.c src/app/endroll.c src/app/ranking.c src/app/ranking_draw.c src/modes/conv.c src/app/conv_scene.c src/app/select.c src/app/select_draw.c src/app/result_draw.c src/app/selgfx.c"
OBJS=""
for s in $SRCS; do
	o="$OUT/obj/$(echo "$s" | tr / _ | sed 's/\.c$/.o/')"
	"$CC" $CFLAGS -c "$s" -o "$o" || exit 1
	OBJS="$OBJS $o"
done
"$CC" -specs=3dsx.specs -g $ARCH -Wl,-Map,"$OUT/aos2_3ds.map" $OBJS \
	-L"$DEVKITPRO/portlibs/3ds/lib" -L"$DEVKITPRO/libctru/lib" -lcitro2d -lcitro3d -lvorbisidec -logg -lctru -lm -o "$OUT/aos2_3ds.elf"
"$TOOLS/3dsxtool.exe" "$OUT/aos2_3ds.elf" "$OUT/aos2_3ds.3dsx" --smdh="$OUT/aos2_3ds.smdh" --romfs="$OUT/romfs"
ls -la "$OUT/aos2_3ds.3dsx"
