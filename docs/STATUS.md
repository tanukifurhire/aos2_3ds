# AoS2 → 3DS port: status (2026-11-09)

## Acceptance criteria

| Criterion | Status |
|---|---|
| Mechanics 1:1 | Engine core ported from the binary (tick order, common states 0–6, damage/heat/hyper/shield, barriers, status effects, combo counters, all 17 collision passes incl. literal quirks, solid push-out, enemy motion keyframes, RNG). **All 15 characters + Sumika's final-boss form** ported state-by-state from the decompiled code (`src/sim/chars/`). CPU AI ported for all of them, 3 difficulties each (`ai_*.c`; AI uses its own RNG stream like the original). Every character has a behaviour test (`tests/test_<name>.c`, all in run_tests.sh) asserting input→state, spawn frames, damage, costs, end/cancel frames, chains and conditional moves against the spec; all pass. Not yet compared frame-by-frame against the PC game. |
| Visuals 1:1 | All 4218 textures → ETC1A4 atlases; every character/object/effect draw handler ported; common effect library, particles, sparks, popups, afterimages, screen dim/blur/speedlines, camera + focus list. Character draws were checked with `tests/run_drawdump.sh` (host renders with real textures) and ported exactly from the draw code. Remaining approximations: textured ribbon/strip draws (Mira whips, Star Breaker/Nanako ribbons, Sham Delta triangle — citro2d has no textured 4-corner quads), speedlines (3D sphere drawn as 2D radial streaks), Nath transform flash. In-match HUD, round-start/end banners and screen transitions ported 1:1 (docs/re/hud.md; `tests/run_hudpreview.sh` renders them on the host); not drawn: font texts (BGM title, names). Stage backgrounds: docs/re/stages.md; 3D port in src/stage (portable, literal) + src/platform/stage.c (citro3d): all 16 stages ported (ids 4–19; menu background behind the main menu; host picks the stage in netplay, START byte 47, proto 3); Old-3DS-mode emulator: stages cost 0.3–4.8 ms; Capital in Flames uses the "Smoke cap" presentation option by default (exact mode ~30 FPS), stages.md §5. Missing: `$blur` previous-frame feedback. |
| Sounds 1:1 | All 61 SE ids via the original table with the original retrigger lockouts, per-id voice counts and dB volume table; BGM streamed with original ids/loop points, fades and volume law; VS BGM select (L/R, "????" random on the cosmetic RNG) and scene music per docs/re/music.md. No VS/result screens, so the jingles (ids 1, 2) are not played. |
| Multiplayer | Rollback netplay (input delay, prediction, snapshot ring, resim, time sync, checksum desync detection, rollback-safe SE) host-tested over a simulated lossy/jittery link; 3DS local wireless (UDS) transport + lobby built but **untested on hardware** (needs two consoles). PC↔3DS cross-play impossible (PC uses Steam P2P). |
| Controls | Done. L Dash, R Hyper, A/B weapons, X/Y specials (=S+A/S+B), L+R Shield, L,L = re-dash (AoS2 has no separate hyper dash). |

## Verified
- `./tests/run_tests.sh` (input mapping incl. 14M-frame fuzz, WAV, RNG vs MT19937 vectors, extractor)
- `./tests/run_sim.sh <a> <b> [frames] [cpuDifficulty]` — any pairing: determinism, snapshot/rollback replay, NaN, pools, KOs
- `./tests/run_rollback.sh` — two netplay peers over fake link == zero-latency reference
- `./tests/run_golden.sh [check|poison]` — golden trace of 48 matches; any optimisation must keep it identical
- `./tests/run_perf.sh` — machine-independent per-tick cost proxy (pairs tested, bytes moved, libm calls, draw cmds)
- Emulator (Azahar): menu, versus vs CPU at 60 FPS, effects/missiles/shields rendering, BGM/SE playback

## Known gaps / next steps
1. Frame-by-frame validation against the PC game (record PC inputs/replays, compare).
2. Stages: Dust Storm / Canyon of Wind / Guardian's Chamber cost 2.3–4.8 ms on Old 3DS (proposals in stages.md §5), br reflection clip planes omitted; story/arcade modes, menus, result screen; HUD heat label is illegible at 400×240 (hud.md §6).
3. 3DS performance: audited 2026-11-10 (docs/PERF.md): sim tick <0.5 ms avg / ~2 ms worst, compact rollback
   snapshots (~20-190 KB instead of 816 KB), BGM decode off the game core, linear-memory worst case ~5 MB free.
   Emulator (Old 3DS mode) holds 60 FPS except Tsih/Nath-heavy hyper scenes on Dust Storm (~52-55 FPS);
   still not measured on real hardware (`PROFILE=1 ./build.sh`, sdmc:/aos2_bench.txt).
4. Netplay on real hardware. (Round flow FUN_00608a00 now runs inside `aos_tick`, src/sim/flow.c.)
5. Test in the user's citra fork (never built here; Azahar used instead).
6. Sprites are small on 400×240 (faithful 0.4× of the 800×600 view) — `SCREEN_SCALE` in render.c.

## Layout
- `docs/re/` specs (HANDBOOK, core, objects, render, one per character, bgm table); `docs/PORTING.md`
- `src/sim/` engine; `src/sim/chars/` characters + `ai_*.c`; `src/net/` rollback + UDS
- `src/platform/` renderer, sprite bank, BGM; `src/app/` menu, match loop, netplay lobby, input lab, SE
- `tools/` extractor, packer, Ghidra wrapper + corpus, project-local toolchains

## Build
`./build.sh` (`SKIP_ASSETS=1` to skip asset steps) → `build/aos2_3ds.3dsx`. Emulator audio needs a
placeholder `sdmc:/3ds/dspfirm.cdc`. The .3dsx embeds game assets — personal use only.
