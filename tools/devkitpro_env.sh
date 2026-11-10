# Sourced by build.sh and tools/*.sh: sets DEVKITPRO / DEVKITARM / TOOLS.
# Uses the project-local toolchain (tools/devkitpro, created by tools/setup.sh); falls back to an
# existing devkitPro install in $DEVKITPRO (e.g. C:/devkitPro) when the local one is absent.
_aos_root=${ROOT:-$(cd "$(dirname "$0")" && pwd)}
_aos_local="$_aos_root/tools/devkitpro/opt/devkitpro"
if [ -x "$_aos_local/tools/bin/tex3ds.exe" ] || [ -x "$_aos_local/tools/bin/tex3ds" ]; then
	DEVKITPRO="$_aos_local"
elif [ -n "${DEVKITPRO:-}" ] && command -v cygpath >/dev/null 2>&1 && [ -d "$(cygpath -u "$DEVKITPRO" 2>/dev/null)/devkitARM" ]; then
	DEVKITPRO=$(cygpath -u "$DEVKITPRO")
elif [ -n "${DEVKITPRO:-}" ] && [ -d "$DEVKITPRO/devkitARM" ]; then
	:
else
	echo "error: no 3DS toolchain found (tools/devkitpro missing). Run ./tools/setup.sh first." >&2
	exit 1
fi
export DEVKITPRO
export DEVKITARM="$DEVKITPRO/devkitARM"
TOOLS="$DEVKITPRO/tools/bin"
