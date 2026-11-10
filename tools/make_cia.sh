#!/bin/sh
# Packages the last ./build.sh output (build/aos2_3ds.elf + build/romfs) as an installable CIA:
#   ./build.sh && ./tools/make_cia.sh   -> build/aos2_3ds.cia (install with FBI; HOME menu icon "AoS2 3DS")
# Installed titles always run with full application memory (no applet-mode limits).
# Tools (project-local, fetched once into tools/ctrtools/, gitignored):
#   makerom    https://github.com/3DSGuy/Project_CTR/releases  (makerom-*-win_x86_64.zip -> tools/ctrtools/makerom/)
#   bannertool https://github.com/carstene1ns/3ds-bannertool/releases (bannertool-*-windows.zip -> tools/ctrtools/bannertool/)
set -eu
cd "$(dirname "$0")/.."
ROOT=$(pwd)
OUT="${AOS_OUT:-$ROOT/build}"
CT="$ROOT/tools/ctrtools"
MAKEROM="$CT/makerom/makerom.exe"
BANNERTOOL=$(ls "$CT"/bannertool/*/bannertool.exe "$CT"/bannertool/bannertool.exe 2>/dev/null | head -n1 || true)
[ -x "$MAKEROM" ] || { echo "missing $MAKEROM (see header)"; exit 1; }
[ -n "$BANNERTOOL" ] || { echo "missing bannertool.exe under $CT/bannertool (see header)"; exit 1; }
[ -f "$OUT/aos2_3ds.elf" ] && [ -d "$OUT/romfs" ] || { echo "run ./build.sh first"; exit 1; }

# Windows tools get Windows paths (Git Bash: cygpath; elsewhere pass through)
win() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else echo "$1"; fi; }

W="$OUT/cia"
mkdir -p "$W"
python "$ROOT/assets/cia/make_art.py" "$(win "$ROOT/extracted")" "$(win "$W")"
"$BANNERTOOL" makebanner -i "$(win "$W/banner.png")" -a "$(win "$W/banner.wav")" -o "$(win "$W/banner.bnr")"
"$BANNERTOOL" makesmdh -s "AoS2 3DS" -l "Acceleration of SUGURI 2 port - WIP" -p "personal research build" \
	-i "$(win "$W/icon.png")" -o "$(win "$W/icon.smdh")"
"$MAKEROM" -f cia -o "$(win "$OUT/aos2_3ds.cia")" -target t -exefslogo \
	-elf "$(win "$OUT/aos2_3ds.elf")" -rsf "$(win "$ROOT/assets/cia/aos2_3ds.rsf")" \
	-icon "$(win "$W/icon.smdh")" -banner "$(win "$W/banner.bnr")" \
	-DAPP_ROMFS="$(win "$OUT/romfs")"
ls -la "$OUT/aos2_3ds.cia"
