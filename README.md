# aos2_3ds

Personal-research port of a legally owned PC copy of *Acceleration of SUGURI 2* to the Nintendo 3DS.
No game data is in this repository: the build extracts it from your own copy of the game.

- Status, decisions and roadmap: [docs/STATUS.md](docs/STATUS.md)
- Installing and testing on a modded 3DS: [docs/RUNBOOK_3DS.md](docs/RUNBOOK_3DS.md)
- Performance notes and profiler: [docs/PERF.md](docs/PERF.md)
- Reverse-engineered data formats: [docs/FORMATS.md](docs/FORMATS.md)

## Building

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
              setup.sh (fresh-clone toolchain setup)
tests/        host tests (built with zig cc)
docs/         status, formats, perf, runbook; docs/re/ reverse-engineering specs
build.sh      -> build/aos2_3ds.3dsx
```
