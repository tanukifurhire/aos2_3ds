# AoS2 → 3DS port: status (2026-11-09)

## Acceptance criteria

| Criterion | Status |
|---|---|
| Mechanics 1:1 | Engine core ported from the binary (tick order, common states 0–6, damage/heat/hyper/shield, barriers, status effects, combo counters, all 17 collision passes incl. literal quirks, solid push-out, enemy motion keyframes, RNG). **All 15 characters + Sumika's final-boss form** ported state-by-state from the decompiled code (`src/sim/chars/`). CPU AI ported for all of them, 3 difficulties each (`ai_*.c`; AI uses its own RNG stream like the original). Every character has a behaviour test (`tests/test_<name>.c`, all in run_tests.sh) asserting input→state, spawn frames, damage, costs, end/cancel frames, chains and conditional moves against the spec; all pass. Not yet compared frame-by-frame against the PC game. |
| Visuals 1:1 | All 4218 textures → ETC1A4 atlases; every character/object/effect draw handler ported; common effect library, particles, sparks, popups, afterimages, screen dim/blur/speedlines, camera + focus list. Character draws were checked with `tests/run_drawdump.sh` (host renders with real textures) and ported exactly from the draw code. Remaining approximations: textured ribbon/strip draws (Mira whips, Star Breaker/Nanako ribbons, Sham Delta triangle — citro2d has no textured 4-corner quads), speedlines (3D sphere drawn as 2D radial streaks), Nath transform flash. In-match HUD, round-start/end banners and screen transitions ported 1:1 (docs/re/hud.md; `tests/run_hudpreview.sh` renders them on the host); not drawn: font texts (BGM title, names). Stage backgrounds: docs/re/stages.md; 3D port in src/stage (portable, literal) + src/platform/stage.c (citro3d): all 16 stages ported (ids 4–19; menu background behind the main menu; host picks the stage in netplay, START byte 47; proto 4 adds the HP handicaps, bytes 48-49); Old-3DS-mode emulator: stages cost 0.3–4.8 ms; Capital in Flames uses the "Smoke cap" presentation option by default (exact mode ~30 FPS), stages.md §5. Missing: `$blur` previous-frame feedback. |
| Sounds 1:1 | All 61 SE ids via the original table with the original retrigger lockouts, per-id voice counts and dB volume table; BGM streamed with original ids/loop points, fades and volume law; VS BGM select (L/R, "????" random on the cosmetic RNG) and scene music per docs/re/music.md. Result jingle (1) and VS jingle (2) played by the Part B screens. |
| Multiplayer | Rollback netplay (input delay, prediction, snapshot ring, resim, time sync, checksum desync detection, rollback-safe SE) host-tested over a simulated lossy/jittery link; 3DS local wireless (UDS) transport + styled lobby/character select (src/app/netsel.c) built but **untested on hardware** (needs two consoles). PC↔3DS cross-play impossible (PC uses Steam P2P). |
| Controls | Done (revised 2026-11-10). L Dash, R Shield (L+R too), A/B weapons, X/Y specials (=S+A/S+B), L,L = re-dash (AoS2 has no separate hyper dash). Hypers = touch buttons on the bottom screen (S+H top, H left, B+H right, D+H bottom, Accel Hyper S+B+H centre; `src/input/hyper_touch.c` turns a touch into the exact button combination of the character's dispatch, both control schemes, sent as ordinary input so rollback netplay is unaffected; `tests/test_hyper_touch.c` fires every slot of every character through the real dispatch). |

## Menus (Part A of docs/re/modes.md, 2026-11-10)
- Scene machine `src/app/scene.[ch]` + `scene_core.c` (original ids, Init/Exit handover, prev scene, post-scene
  order announce → SE lockouts → BGM → transition, fades 1–16, help line, menu input with the 20/5 repeat,
  `g_setup` = §0.2 globals, `g_config`); API summary in modes.md §0.1.1. `main.c` only registers scenes.
- Logo → Title → Main menu (9-item carousel) → Difficulty / Match / Training (+ How to Play pages) / Config,
  per modes.md §A (states, SE ids, BGM 0x18, fades, white flash, help lines). Background = stage 0x13.
  Bottom screen: item list (touch), sub-menu subtitles, the config panel, help line (system font, typewriter).
- Arcade/Story → `SCENE_ARC_SELECT`, Match/Practice → `SCENE_VS_SELECT` (Part B),
  2P/Online → local-wireless lobby, Memory/Ranking → "coming soon" (no replays; ranking = Part D),
  Config → Controls opens the input lab, Exit → back to HOME. Config saved to `sdmc:/3ds/aos2_3ds/config.ini`.
- Text: BMFont v3 parser/layout (`bmfont.c`), `< KEY >` script tables (`strings.c`), citro2d renderer
  (`text_c2d.c`). tools/aos2pack `fonts.go` repacks 7 fonts (glyphs used by en/ptbr/sp, pre-scaled to the 3DS
  size, LA8, 480 KB total) and the menu textures (logo, logos, pills, arrows, 9 manual pages) as single t3x.
- Memory: menus use ~3 MB linear (stage 0x13 ~2.5 MB, fonts ≤ 384 KB, textures 144 KB + 512 KB manual page
  while shown), all freed before a match: in-match linear free identical to the previous dev menu
  (Suguri mirror, stage 5: 7407 KB vs 7408 KB). Menu frame in the Old-3DS emulator: cpu 2.2–3.5 ms, 60 FPS.
- Host tests: `tests/test_text.c`, `tests/test_menu.c` (in run_tests.sh). Hold SELECT in a menu for the
  frame/memory readout (in a match it also shows linear free).

## Selects, VS, result, continue, pause (Part B of docs/re/modes.md, 2026-11-10)
- `src/app/select.c` (portable logic, `tests/test_select.c`), `select_draw.c`, `result_draw.c`, `selgfx.c`:
  ArcSelect (8: command mode → character, cleared stars, touch grid), VS Select (6: per-player character /
  handicap / colour incl. SP / hyper control, CPU-slot hand-over, stage + BGM select, white fade, jingle 2,
  VS phase on the chosen stage), "NOW LOADING" frame, Result (3: winner art + report bands + rotated win quote
  with rival X-quotes, score rows, rank FUN_00617740, jingle 1) with the continue countdown (9→0, 100 f,
  B speeds up) → ladder continue / game over via Part D's `modes_flow_*`; pause menu in matches
  (Return / Move list / Retry / Exit; resume countdown) replacing "START = quit". Details + deviations:
  modes.md §B.8.
- Textures: tools/aos2pack `seltex.go` → 283 single t3x (`m3_b_*`, 2.7 MB romfs). Linear while shown:
  ArcSelect ≈ 1.6 MB, VS ≈ 2.4 MB, Result ≈ 2.1 MB (+ fonts); all freed in the scene Exits with the
  select's 3D stage, nothing resident in a match (arcade fight → result → continue → refight: linear
  free 8874 KB both times, no leak).
- Emulator (Old 3DS mode): selects/result at 60 FPS; full runs verified: ArcSelect → arcade fight → loss →
  result → continue → same opponent; pause → Exit → ArcSelect; VS Select → stage → VS → battle → result → VS.
  Evidence: build/preview/modes_B_*.png.
- Polish pass (2026-11-10, modes.md §B.8 "Polish pass"): select backdrop texture (bgMdlAdd sl_bgp), stage depth
  reset before 3D stages drawn over 2D (select planet/ring, VS-phase stage), VS player labels, result score
  table, pause overlay depth. Scripted emulator runs: `sdmc:/aos2_keys.txt` (profile builds, §D.7.1);
  menu → Match → VS → battle → pause → result → VS verified with it. Evidence: build/preview/polish_*.png.

## Arcade/story flow, staff roll, ranking, save (Part D of docs/re/modes.md, 2026-11-10)
- `src/app/modes_flow.[ch]`: run state + app-wide cosmetic MT (shared with the "????" BGM pick) + progress save;
  entry points for ArcSelect / Battle end / pause Retry-Quit / Result / Conversation / Endroll (modes.md §0.1.1,
  §D.7.1). The Battle scene hands every arcade/story match end to it. Result/Conversation steps pass through when
  those scenes are not registered, so a run is playable end to end with Battle + Endroll only.
- Endroll (scene 11, `endroll.c`): literal timings, skip, BGM (Sham's story 0x15, else 4); staff pages drawn
  squeezed vertically as the PC code does (§D.7.4); 1 MB L8 textures only while the roll runs.
- Menu → Ranking (9 pages: Easy/Normal/Hard tables + 6 tournament pages), arcade name entry (original letter set,
  7 slots), unlock popups, all in `ranking.c` / `ranking_draw.c` (§D.7.3). Clear marks for ArcSelect:
  `modes_arcade_cleared` / `modes_story_cleared` on `modes_save()`.
- Save: `progress.sav` (unlocks, clears, wins, ranking) separate from `config.ini`; loaded at boot, written by
  `modes_save_poll()` outside the Battle scene (§D.7.2).
- Profile builds: the benchmark (`sdmc:/aos2_bench.txt`) works again (zeroed match setup → timer 0, PERF.md);
  `sdmc:/aos2_flow.txt` "mode char difficulty frames [skip]" auto-plays an arcade/story run (every fight a fake
  P1 win after `frames`) for testing the flow and screenshots.
- Host tests: `tests/test_flow_glue.c`, `test_menu.c` `test_ranking`.

## Verified
- `./tests/run_tests.sh` (input mapping incl. 14M-frame fuzz, WAV, RNG vs MT19937 vectors, extractor, story conversations: `test_conv` runs all 56 scripts x 8 languages through parser, text resolution, wrap and the scene state machine)
- `./tests/run_sim.sh <a> <b> [frames] [cpuDifficulty]` — any pairing: determinism, snapshot/rollback replay, NaN, pools, KOs
- `./tests/run_rollback.sh` — two netplay peers over fake link == zero-latency reference
- `./tests/run_golden.sh [check|poison]` — golden trace of 48 matches; any optimisation must keep it identical (re-recorded 2026-11-10 for the HP fix: max HP is the handicap table value, 9000 by default, not 4500 — core.md §6.1; with handicap 0 the old trace is reproduced byte for byte)
- `test_modes` (in run_tests.sh): arcade/story ladders of every character vs docs/re/modes_ladders.tsv, full runs with continues/retries, round records + match score from real matches, ranking, unlocks, save file
- `./tests/run_perf.sh` — machine-independent per-tick cost proxy (pairs tested, bytes moved, libm calls, draw cmds)
- Emulator (Azahar): menu, versus vs CPU at 60 FPS, effects/missiles/shields rendering, BGM/SE playback

## Known gaps / next steps
1. Frame-by-frame validation against the PC game (record PC inputs/replays, compare).
2. Stages: Dust Storm / Canyon of Wind / Guardian's Chamber cost 2.3–4.8 ms on Old 3DS (proposals in stages.md §5), br reflection clip planes omitted; story/arcade modes (Parts B–D), result screen; HUD heat label is illegible at 400×240 (hud.md §6).
3. 3DS performance: audited 2026-11-10 (docs/PERF.md): sim tick <0.5 ms avg / ~2 ms worst (Tsih up to 12.6 ms),
   compact rollback snapshots, BGM decode off the game core. Render pipeline (PERF.md "Render pipeline"): own 2D
   batcher with one premultiplied blend state for alpha + additive sprites (`src/platform/sprbatch.c`), HUD gauges as
   single quads, cached bottom panel; atlases at ¼ (big lasers ½), stage textures ½, no character mipmaps, every
   match's sprite atlases in VRAM. Emulator (Old 3DS mode): Tsih ×2 cpu 10.7 → 6.7 ms avg, frames over budget
   19 % → 1.3 %; every other benchmark row 0-0.1 %; linear free at match start ≥ 13.9 MB (was 2.3 MB worst case).
   Still not measured on real hardware (`PROFILE=1 ./build.sh`, sdmc:/aos2_bench.txt).
4. Netplay on real hardware. (Round flow FUN_00608a00 now runs inside `aos_tick`, src/sim/flow.c.)
5. Test in the user's citra fork (never built here; Azahar used instead).
6. Sprites are small on 400×240 (faithful 0.4× of the 800×600 view) — `SCREEN_SCALE` in render.c.
   Sprite filtering (2026-11-10): atlas pages are sampled bilinearly (were point-sampled); draws clamp UVs to the
   frame's texel centres (no atlas bleed). Character atlases carried mipmaps against shimmering rotated projectiles;
   since the ¼-scale atlases (PERF.md texture plan) sprites are ~1:1 on screen and the mipmaps are gone (missile
   volley check: build/preview/perf2_missiles.png). Atlases are stored straight-alpha with a 1 px extruded border per frame and colour bled into transparent texels (aos2pack -edge; the combiner premultiplies), see PERF.md "Atlas bleed".
   Fixed with it: rotated-quad off-screen cull (long Accel Burst beams vanished), Accel Burst wall flare uses
   `sg_slash` (0x141), not the beam texture. Profile builds can dump frames: `sdmc:/aos2_cap.txt` = "first count"
   → `sdmc:/cap/f<frame>.raw` (raw tiled RGBA8 target; prof.c). Evidence: build/preview/*_before_after.png.

## Layout
- `docs/re/` specs (HANDBOOK, core, objects, render, one per character, bgm table); `docs/PORTING.md`
- `src/sim/` engine; `src/sim/chars/` characters + `ai_*.c`; `src/net/` rollback + UDS
- `src/modes/` arcade/story ladder sequencer (`ladder.h`), score/ranking/unlocks (`score.h`), progress save `sdmc:/3ds/aos2_3ds/progress.sav` (`save.h`) — pure C; app glue in `src/app/modes_flow.c`
- `src/platform/` renderer, sprite bank, BGM; `src/app/` scenes (logo/menu/battle), text, netplay lobby, input lab, SE
- `src/modes/conv.c` story conversation scene, data + logic (modes.md §C, §C.6): exe-resource scripts (extracted by
  `aos2extract`, packed with texts + portraits into `romfs:/story` by `tools/aos2story`), literal wrap/typewriter/
  fades, per-frame draw description `conv_view`. Scene 5 on the 3DS: `src/app/conv_scene.c` (modes.md §C.7) — top
  screen = the original composition at s = 0.4 (stage backdrop, 黒地 fill, letterbox, noise bands, mirrored/dimmed
  portraits, cross-fades), bottom screen = both text slots (16 px dialogue font, typewriter, noise bands); A next,
  hold X/Y fast-forward, START skip (port); exit → `modes_flow_conv_exit()`. All 8 languages: per-language dialogue
  fonts (jp/chs/cht/ko 512 KB, ru 64 KB, loaded only during the scene; `tests/test_conv_font.c`). Debug: main menu
  **Y** = picker for any of the 56 scripts in any language. Old-3DS emulator: 60 FPS, cpu ≤ 1.6 ms, linear free
  ≥ 23.6 MB during a conversation. Screenshots `build/preview/modes_C_*.png`.
- `tools/` extractor, packer, Ghidra wrapper + corpus, project-local toolchains

## Build
`./build.sh` (`SKIP_ASSETS=1` to skip asset steps) → `build/aos2_3ds.3dsx`. Emulator audio needs a
placeholder `sdmc:/3ds/dspfirm.cdc`. The .3dsx embeds game assets — personal use only.
