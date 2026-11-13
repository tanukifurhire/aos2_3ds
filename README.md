# aos2_3ds

Personal-research port of a legally owned PC copy of *Acceleration of SUGURI 2* to the Nintendo 3DS.
No game data is in this repository: the build extracts it from your own copy of the game.

- Status, decisions and roadmap: [docs/STATUS.md](docs/STATUS.md)
- Installing and testing on a modded 3DS: [docs/RUNBOOK_3DS.md](docs/RUNBOOK_3DS.md)
- Performance notes and profiler: [docs/PERF.md](docs/PERF.md)
- Reverse-engineered data formats: [docs/FORMATS.md](docs/FORMATS.md)

## Building

### With the builder window (no Git Bash needed)

Requirements: Windows, **Go** on PATH (https://go.dev/dl/), internet access for the one-time setup, and an
installed copy of *Acceleration of SUGURI 2* (the folder with `aos2.exe`).

1. Double-click `tools/aosbuild/make_exe.cmd` once. It builds `aosbuild.exe` into the repo root.
2. Open `aosbuild.exe`. On a fresh clone press **Set up toolchain**. It downloads devkitARM, libctru,
   citro2d/3d, tremor, tex3ds, picasso and 3dstools into `tools/devkitpro/`, and makerom and bannertool into
   `tools/ctrtools/`. If Python 3.12+ is installed, it also sets up [pycgfx](https://github.com/skyfloogle/pycgfx)
   in a virtual env under `tools/pycgfx/` for the animated CIA banner. Nothing is installed system-wide.
3. Pick the game folder, choose **Homebrew (.3dsx)**, **CIA** or **Both**, and press **Build**. The first
   build extracts and packs all assets (a few minutes). Later builds reuse them, and re-pack on their own when
   an asset tool changed. Tick **Rebuild assets** to force a re-pack.
4. Copy the result to the SD card: `aos2_3ds.3dsx` into `/3ds/aos2_3ds/` (Homebrew Launcher), or
   `aos2_3ds.cia` anywhere, then install it with FBI. **Open output folder** opens the folder they are in.

**CIA banner and icon.** The HOME-menu banner is flat (bannertool, from `<output>/cia/banner.png`): the
transparent title logo (`menuMdl/ttl_logo`) with Suguri's upright dash frame (`charaSuguriA/sg_dsh06`) and an
afterimage beside it. It does not move. The icon is the game's own icon, read from `aos2.exe`. The banner
sound is `assets/cia/jingle.wav` (git-ignored, your own file; the HOME menu plays about 3 s), or else the menu
"decide" sound.

Ticking **Animated CIA banner** builds a 3D banner instead: Suguri drops in from above on her dash frames, brakes,
idles under the logo, then dashes off upward with her afterimage, and the next drop comes in from there (glTF in
`<output>/cia/anim/`, converted by pycgfx plus `tools/aosbuild/banner_cgfx.py`; rough preview in
`<output>/cia/banner_preview.gif`). The HOME menu turns every 3D banner model about the vertical axis once per
10 s; everything here stands on that axis and is billboarded (always faces the camera), so nothing turns or
orbits, and render layers fix the draw order so she stays in front of the title. Banner tests on a real console
found what its HOME menu accepts: top-level nodes with linear position keys, billboarding, and render layers 0, 1
and 3 play; stepped scale keys on child nodes crashed it, rotation keys (including a counter-rotation against
the spin) froze it, and so did a 29-node scene. `aosbuild bannertest` builds small test CIAs (own title IDs, no
game data) for trying a new banner idea; it currently builds the drop-in with her dash rings.

The status tiles show what is missing. Each run's log is also written to `<output>/aosbuild.log`. Settings
and the downloaded background image live in `.aosbuild/` (git-ignored). Assets are re-packed automatically
when an asset tool (aos2pack, aos2mesh, ...) changed since the last pack.

Commands, for scripting: `aosbuild setup`, `aosbuild 3dsx|cia|both [-rebuild] [-profile]` and
`aosbuild status`. From Git Bash, run them on `./aosbuild.exe`. cmd and PowerShell
don't wait for a windowed exe, so there use `go run ./tools/aosbuild <command>`.

### With Git Bash

Requirements: Windows with **Git Bash**, **Go** on PATH (https://go.dev/dl/), internet access for the
one-time setup, and an installed copy of *Acceleration of SUGURI 2* (the folder with `aos2.exe`).

```sh
git clone https://github.com/tanukifurhire/aos2_3ds.git
cd aos2_3ds

# 1. One-time: fetch the project-local toolchains (git-ignored, nothing installed system-wide)
#    tools/devkitpro/ (devkitARM, libctru, citro2d/3d, tremor, tex3ds, picasso, 3dstools) and tools/zig/
./tools/setup.sh

# 2. Tell the build where the game is (skip if it sits next to this folder as "../Acceleration of SUGURI 2")
echo "/c/Program Files (x86)/Steam/steamapps/common/Acceleration of SUGURI 2" > game_path.txt

# 3. Full build: extracts and converts all game assets, then compiles (about 3 minutes)
./build.sh

# Later builds when only code changed (reuses build/romfs):
SKIP_ASSETS=1 ./build.sh
```

Output: `build/aos2_3ds.3dsx` (~840 MB, assets embedded — personal use only).

Variants:
- `PROFILE=1 SKIP_ASSETS=1 ./build.sh` — profiler/benchmark build (see docs/PERF.md). It writes the same
  output file, so copy the normal build elsewhere first.
- `GAME=/path/to/game ./build.sh` overrides `game_path.txt`.
- If you already have devkitPro installed and skip `setup.sh`, the build uses `$DEVKITPRO` instead.

## Tests

```sh
./tests/run_tests.sh      # host unit tests (input, sim per character, modes, text, assets, ...)
./tests/run_golden.sh     # simulation must match the recorded golden trace ("identical")
./tests/run_rollback.sh   # netplay rollback over a simulated lossy link
./tests/run_sim.sh suguri tsih 6000 2   # CPU-vs-CPU smoke test for a pairing
```

## Running

- **Emulator:** open `build/aos2_3ds.3dsx` in Azahar (set Old 3DS mode to match the target hardware).
  For sound the emulated SD needs `3ds/dspfirm.cdc` (any file works under HLE DSP).
- **Console:** see [docs/RUNBOOK_3DS.md](docs/RUNBOOK_3DS.md).

## Layout

```
src/sim/      game simulation: near-literal C port of the PC engine and all characters (deterministic)
src/stage/    3D stage backgrounds (portable); src/platform/stage.c draws them with citro3d
src/platform/ renderer (sprite batcher, sprite bank, HUD, bottom panel), BGM streaming
src/app/      scenes: logo/title/menus, select, battle, result, conversation, staff roll, ranking, netplay lobby
src/modes/    arcade/story ladders, scoring/ranking/unlocks, save file, story conversation logic
src/net/      rollback netplay and 3DS local wireless (UDS) transport
src/input/    button mapping (L Dash, R Shield (L+R too), A/B weapons, X/Y specials, L,L Hyper Dash; hypers = touch buttons)
tools/        aos2extract (unpack/decrypt game data), aos2pack (atlases, fonts, menu art), aos2mesh
              (stage meshes), aos2story (story data), dkpfetch (portable devkitPro fetcher), re/ (RE helpers),
              setup.sh (fresh-clone toolchain setup), aosbuild (builder window/CLI: setup, build, CIA)
tests/        host tests (built with zig cc)
docs/         status, formats, perf, runbook; docs/re/ reverse-engineering specs
build.sh      -> build/aos2_3ds.3dsx
```
