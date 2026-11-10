# Old 3DS performance and memory (audit 2026-11-10)

Target: Old 3DS / 2DS (ARM11 268 MHz, no L2, 32 MB linear heap, 6 MB VRAM), steady 60 FPS,
simulation bit-identical. Nothing here has run on real hardware yet.

## Tools
- **On-device profiler**: `PROFILE=1 ./build.sh` (`-DAOS_PROFILE`, src/app/prof.[ch]; plain builds compile it out).
  Bottom panel lines 3-5 show avg/max µs per 60 frames; everything is also logged to `sdmc:/aos2_prof.txt`.
  Sections: `tick` (aos_tick), `drawlist` (aos_draw_world), `stageUpd`/`stageDraw`, `2d` (draw-list submit +
  arena ring), `hud`, `audio` (SE triggers + BGM/SE frame updates), `panel` (bottom screen), `frameEnd`,
  `wait` (C3D_FrameBegin vsync/GPU wait), `cpu` (frame minus wait), `frame` (wall), `bgm` (Tremor decode time
  measured in the stream thread), linear/heap/VRAM free, sprite texture and SE PCM bytes.
- **Benchmark mode** (profile builds): if `sdmc:/aos2_bench.txt` exists the app runs each line
  `p1 p2 stage frames hyper [difficulty]` as CPU vs CPU (hyper 1 pins both gauges at 3.0), logs a summary per
  scenario and quits. Lists used here: `tools/bench/aos2_bench_perf.txt`, `tools/bench/aos2_bench_memory.txt`.
  Since the menu rework the app starts straight in the Battle scene when the file exists (no Logo/Title/menus,
  `main.c`); the bench branch of `battle_init` sets the match values the menus would (timer 180, 2 rounds,
  handicap 2 = 9000 HP, both CPU). Fixed 2026-11-10: with the menus skipped `g_setup` was all zero, so the round
  timer was 0 — every round ended by time-over at once and the runs only cycled round banners (looked hung, and
  the numbers were idle frames: Star Breaker ×2 tick 104 µs / drawlist 49 µs instead of ~286 / ~1000). Verified in
  Azahar Old 3DS mode: the full perf list runs and the app exits by itself (~4 min).
- **Host cost proxy** `tests/run_perf.sh` (built with `-DAOS_PERF`, counters in src/sim/perf.h): per tick
  objects, collision pairs (aos_shape_hit calls / full evaluations), libm trig calls, bytes moved by pool
  erases, texture-name compares, draw commands, compact snapshot bytes.
- **Golden trace** `tests/run_golden.sh`: 48 matches (random input, CPU, CPU + pinned hyper; every character),
  hashing the whole world and the live state every 600 frames. `check` also asserts the dead-slot invariant,
  `poison` fills vacated pool slots with garbage and requires the same live state.

Emulator caveat: Azahar (Old 3DS mode, `is_new_3ds=false`) advances the system tick per emulated
instruction, so its µs are an instruction-count proxy. Real ARM11 time is higher (cache misses, VFP
latency, FCRAM bandwidth for memmove/memcpy); budget ~1.5-2x.

## Host proxy, worst tick over 32 CPU matches (16 mirrors with pinned hyper + 16 pairings)

| per tick | before | after |
|---|---|---|
| texture-name strcmp in draw build | 1,548,710 | 2,790 (hash index) |
| bytes moved by pool erases | 2,247,000 (avg up to 92 KB) | 280,400 (avg ≤ 22 KB) |
| libm sin/cos calls | 11,641 | 866 (memo misses) |
| aos_shape_hit full evaluations | 18,739 | 543 (exact broad phase) |
| rollback snapshot copy | 835,512 B | 17-48 KB avg, 187 KB max |
| draw commands | 2,048 (cap hit, sprites dropped) | 3,026 (cap now 4,096) |

## Azahar Old 3DS mode, CPU vs CPU difficulty 2, 1800 frames each (avg/max µs; frame = wall time)

| scenario | tick | drawlist | 2d | hud | stageDraw | cpu | frame before → after |
|---|---|---|---|---|---|---|---|
| Star Breaker ×2, hyper, Capital in Flames | 441 → 250 | 2213 → 925 | 1392 → 1152 | 5325 → 2032 | 3147 → 2780 | 15119 → 9159 | 23465 → 16808 |
| Alte ×2, hyper, Dust Storm | 147 → 89 | 737 → 184 | 837 → 693 | 5164 → 1970 | 4930 → 4164 | 13306 → 8291 | 20313 → 17037 |
| Mira ×2, hyper, Capital in Flames | 154 → 101 | 1076 → 255 | 990 → 812 | 6186 → 2323 | 3266 → 2775 | 14252 → 8387 | 20862 → 16701 |
| Sumika 2nd ×2, hyper, Dust Storm | 745 → 378 | 5974 → 1062 | 2499 → 2096 | 7648 → 2823 | 4733 → 4177 | 23133 → 11758 | 32683 → 17012 |
| Nanako ×2, hyper, Capital in Flames | 566 → 384 | 8877 → 792 | 2317 → 1984 | 6203 → 2322 | 3212 → 2886 | 23455 → 10334 | 31544 → 16763 |
| Tsih ×2, hyper, Dust Storm | 1076 → 487 | 6229 → 928 | 3757 → 3283 | 6059 → 2316 | 4775 → 4164 | 23303 → 12367 | 31341 → 19340 |
| Suguri vs Sora, normal, stage 5 | 196 → 121 | 2171 → 216 | 1128 → 916 | 5272 → 1794 | 309 → 265 | 10554 → 4507 | 19487 → 16698 |

Worst single frames: `cpu` max 98-182 ms → 9-39 ms (the old spikes were SE files and BGM tracks loaded on the
game thread). BGM decode (~3-4.5 ms per frame) moved off the game core. Tick max 5-17 ms → ~2 ms.

## Linear memory (32 MB; the binding budget; main heap use is ~2 MB + 3.2 MB netplay ring)

| item | size |
|---|---|
| citro2d/3d buffers, framebuffers, HUD system atlases, BGM buffers, stage particles (no stage) | ~7.5 MB |
| stage textures + geometry | 0.3 MB (stage 5) … ~3.6 MB (Dust Storm, Rain) |
| common atlases (effect/system/beam/particle/msg/charaCmn, 5 MB) | 1 MB linear + 4 MB **VRAM** |
| two character atlases | 4.1 MB (Sumika 2nd mirror, shared) … 9.2 MB (Suguri A+B) |
| SE PCM (only this match's sounds, see audio.c) | 2.3 … 9.7 MB (Nath+Alte on Rain) |
| **measured minimum free during a match** | **5.0 MB** (Nath vs Alte, Rain); Suguri mirror 5.4-10.5 MB on all 15 stages |

Repeated matches return to the same free amount (no leak). Before: free memory fell match after match
(SE cache never freed, plus a 4 MB atlas leak whenever both players used the same folder) down to
0.9 MB after six matches; VRAM was unused (5.1 MB free, now 1 MB).

## What changed
Simulation (bit-identical; golden trace, per-character tests, determinism and rollback tests unchanged):
- `aos_tex` hash index + `aos_texf` (fmt, frame) cache (draw.c).
- `aos_shape_hit`: exact distance pre-check from each shape's reach (radius / 1.5·max half-extent + 1),
  skipped for |coords| ≥ 1e5 or NaN; cos/sin hoisted out of the corner loop (objects.c).
- `aos_cos`/`aos_sin` memo table (pure functions; amath.c).
- Effects and beams: one-pass compaction instead of per-record erase. Exact because their update handlers never
  scan their own pool; the pool-full check uses the logical count; arrays have 2× physical slots so a handler
  can never run out (objects.c comment; `tests/test_effect_pool.c` compares with the original loop incl. at the
  cap and with a forced mid-walk compaction). Particles and sparks: same one-pass compaction (no handlers).
- Dead pool slots are kept zero (`AOS_CLEAR_DEAD`), which makes compact snapshots exact.
Netplay: compact snapshots (fixed part + live pool prefixes), restore re-zeroes slots past the snapshot's
counts; checkpoint hash streams the same bytes (rollback.c). Snapshot ring 9.2 MB → 3.2 MB per session.
Platform: HUD ring/arc and arena-ring trig tables (identical output, hudpreview diffed); BGM thread on core 2
(New 3DS) / system core with a 40% time limit (Old 3DS) and track open + first decode moved to that thread;
per-match SE residency (preload what the two characters + stage can play, free the rest); common atlases
in VRAM; draw-list cap 4096; fixed the shared-folder atlas leak.

## Remaining risks on real Old 3DS
- Absolute timing: emulator µs are instruction counts. Heavy hyper scenes reach `cpu` 10-12 ms emulated
  (Tsih/Nath on Dust Storm) → plausibly 15-24 ms real: expect drops below 60 there. Biggest remaining items
  are stage draw (2.8-4.2 ms; Capital in Flames/Dust Storm), the 2D submit (citro2d flushes on every texture or
  blend change; order can't change) and the HUD (~1650 small triangles).
- System-core BGM: 40% of core 1 vs ~20% needed per the emulator; Tremor on real cache may need more. If music
  stutters, raise `BGM_SYSCORE_LIMIT` (bgm.c) or the buffer count. UDS netplay also runs services on core 1.
- memmove/memcpy are bandwidth-bound on FCRAM: worst erase tick 280 KB, worst snapshot 187 KB (+ restore) per
  frame in netplay; the 1 MB world memset at each round start is a one-off.
- SE preload set comes from host simulation (tests/se_usage.c); a sound outside it still loads lazily (logged as
  "SE … loaded on first use" in profile builds).
- Pool sizes grew (beam/effect 2× slots): aos_world 816 → 1066 KB; fingerprint changes, so both netplay peers
  need this build.

## Practice-stage benchmark: Sumika / Tsih mirrors (2026-11-10, branch perf-bench)

"Practice scene, the 3D cube" = stage 5 *Before the War* (wireframe room, st_tr.c); Guardian's Chamber (17,
the one with actual cubes) was run too. Azahar Old 3DS mode (`is_new_3ds=false`, 100 % speed limit), profile
build, CPU vs CPU at difficulty 2 (Hard), both hyper gauges pinned at 3.0, 3600 frames (1 min, 2+ rounds incl.
round transitions) per row. The simulation is deterministic (CPU RNG seeded), so rows with the same characters
are **the same match** frame for frame (same quads, flushes, pools); they differ only in the removed piece.
List: `tools/bench/aos2_bench_practice.txt`; raw log: `build/bench/practice_A.txt` (not committed);
`python tools/bench/summarize.py <log>` prints these tables. Emulated µs (instruction-count proxy, see above).

Bench tooling added: 7th column = flags (prof.h `BENCH_*`: 1 no stage draw, 2 no HUD, 4 build the draw list but
don't submit it, 8 half-size sprite quads, 16 no draw list at all, 32 one blend mode for all sprites); per-frame
counters (sprite quads, blend flushes, texture switches, covered sprite pixels split alpha/additive, stage draw
calls + vertices, GPU command-buffer use, pool maxima); cpu P50/P95/P99 and frames over budget; maxima skip the
first 30 frames (match load). `tools/bench/run_azahar_bench.sh <list> <log> [3dsx]` runs a list in the
emulator copy and kills only that copy; `tools/bench/run_capture.sh` grabs frames by match frame
(`m <frame> <count>` lines in sdmc:/aos2_cap.txt). The bench now sets the VS-CPU match defaults itself
(handicap 2, CPU inputs, 180 s, 2 rounds): with the menus skipped `g_setup` was all zero, which gave a 0 s round
timer and idle players (rounds ended by time-out after ~300 frames, 0 objects) — that was the "bench hangs".

### Per-section cost (avg / max µs per frame)

| match | stage | tick | draw list | stage draw | 2D submit | HUD | panel | cpu avg | cpu P95 / P99 / max | frames with cpu > 16.7 ms |
|---|---|---|---|---|---|---|---|---|---|---|
| Sumika ×2 | 5 | 399 / 2152 | 1014 / 5231 | 280 / 377 | 2464 / 10683 | 2604 / 3070 | 794 / 1773 | **8682** | 13.0 / 18.0 / 27.7 ms | 1.3 % |
| Tsih ×2 | 5 | 1045 / 12680 | 1266 / 6644 | 269 / 376 | 4171 / 21816 | 2344 / 3167 | 809 / 1745 | **10529** | 26.0 / 31.0 / 37.9 ms | 19.1 % |
| Sumika 2nd ×2 | 5 | 498 / 2043 | 1231 / 4439 | 271 / 377 | 3174 / 11253 | 2824 / 3109 | 867 / 1871 | **9494** | 16.0 / 20.0 / 163.5 ms¹ | 3.9 % |
| Sumika vs Tsih | 5 | 661 / 6766 | 1008 / 3673 | 268 / 377 | 2419 / 9164 | 2410 / 3074 | 823 / 1797 | **8259** | 15.5 / 20.5 / 21.6 ms | 4.1 % |
| Sumika ×2 | 17 | 398 / 2108 | 1014 / 5232 | 1998 / 3259 | 2464 / 10703 | 2604 / 3059 | 810 / 1797 | **10130** | 14.0 / 19.0 / 25.7 ms | 1.8 % |
| Tsih ×2 | 17 | 1043 / 12625 | 1266 / 6705 | 2007 / 3260 | 4170 / 21816 | 2346 / 3167 | 826 / 1768 | **12439** | 28.0 / 32.5 / 39.6 ms | 22.7 % |
| Sumika 2nd ×2 | 17 | 498 / 2043 | 1231 / 4416 | 1986 / 3074 | 3175 / 11252 | 2826 / 3112 | 893 / 1905 | **11412** | 18.0 / 21.5 / 163.5 ms¹ | 6.7 % |

Other sections: stage update 10 µs (stage 17: 140-165), audio 5-15 µs, C3D_FrameEnd 297 µs, ~0.3-0.8 ms of
scene-loop overhead (target clears, scene begin). BGM decode thread 5.5-7 ms per frame on the system core
(not in `cpu`). `gpu` (C3D_GetDrawingTime) reads a constant 0.67 ms in Azahar: the emulator does not time the
GPU. ¹ one 153 ms stall: `explosion1.wav` was not in Sumika 2nd's SE preload set and loaded from SD mid-match
(also `hit_f`/`swing_d` for Sumika: 13.5 ms); fixed in audio.c `se_char_mask` on this branch (+1.3 MB SE PCM).

Render load (per frame avg / max): Sumika ×2 196 / 1017 sprite quads, 77 / 363 blend flushes, 30 / 119 texture
switches, 216k / 1.69M covered sprite pixels (130k / 850k additive); Tsih ×2 208 / 1186 quads, **177 / 1137
blend flushes**, 19 / 67 texture switches, 236k / 1.72M px; Sumika 2nd ×2 275 / 1053 quads, 114 / 433 flushes,
439k / **2.45M px** (2.0M additive). Draw list up to 1696 / 2401 / 1745 commands. Pools (max): Sumika ×2
59 enemies (adds), 46 missiles, 113 effects, 105 particles; Tsih ×2 593 effects, 41 beams, 26 lasers, 26 enemies;
Sumika 2nd ×2 166 enemies, 194 beams. Stage draws: stage 5 = 4-7 draw calls / 3.5-6k vertices; stage 17 =
10-23 / 12k. GPU command buffer ≤ 29 % used. The screen is 96k pixels, so the peaks are 18-25 layers of overdraw.

### One piece removed at a time (stage 5; cpu avg / P95 / frames over 16.7 ms)

| removed | Sumika ×2 | Tsih ×2 | what changed |
|---|---|---|---|
| nothing | 8.68 / 13.0 / 1.3 % | 10.53 / 26.0 / 19.1 % | |
| stage draw | 7.95 / 12.5 / 1.2 % | 10.28 / 25.5 / 18.6 % | stage 5 costs 0.27 ms |
| HUD | 5.62 / 10.0 / 0.5 % | 8.17 / 23.0 / 11.7 % | HUD 2.3-2.6 ms every frame |
| 2D submit (list still built) | 5.93 / 8.0 / 0.0 % | 6.52 / 10.5 / 1.1 % | 2d 4.2 → 0.16 ms, max 21.8 → 0.2 |
| draw list + submit | 4.90 / 5.5 / 0.0 % | 5.25 / 7.5 / 0.4 % | + draw-list build 1.0-1.3 ms |
| one blend mode (no flushes) | 7.41 / 11.0 / 0.4 % | **8.34 / 15.0 / 1.9 %** | 2d 4.17 → 1.99 ms, max 21.8 → 7.4 |
| half-size sprite quads (¼ fill) | 8.17 / 13.0 / 1.3 % | 10.42 / 25.5 / 18.8 % | covered px 236k → 66k; nothing else |

### Bottleneck
**CPU, in the 2D submit, and there mostly the citro2d flush on every blend-mode change.** Tsih's effects
interleave alpha-blended and additive sprites (blend 1 ↔ 2) in draw order, so a heavy frame issues up to
1137 `C2D_Flush` + `C3D_AlphaBlend` (each a separate GPU draw call plus state/uniform upload), ≈ 12.7 µs each
(emulated): 14 ms of the 21.8 ms worst submit. Keeping a single blend mode cuts Tsih's frames over budget from
19 % to 1.9 % and P95 from 26 to 15 ms. The rest of the submit is ~6 µs of citro2d CPU work per sprite quad
(C2D_DrawImage vertex math), 2 ms avg / 7.4 ms max. Second: the HUD, a constant 2.3-2.8 ms (row-strip quads
`hg_quad`: one C2D image per scanline of a slanted gauge, plus ~1650 small triangles). Third: draw-list build
1.0-1.3 ms avg, 4.4-6.7 ms max. The simulation is not the problem on average (0.4-1.0 ms) but Tsih has rare
12.7 ms ticks (host proxy: 24k collision pair tests, 13k trig calls, 307 KB pool erase in the worst tick). The
stage is small on stage 5 (0.27 ms) and 2.0 ms on Guardian's Chamber. Fill rate does not move anything in the
emulator (expected: Azahar doesn't model GPU time) — see the estimate below.

### Real Old 3DS estimate
- CPU: emulated µs × 1.5-2 (cache misses, VFP, FCRAM). Tsih ×2 on stage 5: avg 16-21 ms, P95 39-52 ms → the
  game runs below 60 FPS through most of Tsih's hyper-heavy stretches; Sumika ×2: avg 13-17 ms, P95 20-26 ms →
  mostly 60 with drops during summons/hypers; Sumika 2nd ×2 avg 14-19 ms.
- GPU fill (estimate, unmeasured): PICA200 is specified at 4 px/clock (~1 Gpx/s at 268 MHz); with alpha
  blending and texture reads from FCRAM assume 25-50 % of that. Peak frames (1.7-2.45M sprite px + ~0.2M for the
  stage-5 room + HUD) → ~5-12 ms of GPU, average frames (0.2-0.45M px) ~1-2 ms. The GPU works on frame N while
  the CPU builds N+1 (C3D_FRAME_SYNCDRAW), so fill only limits when it exceeds 16.7 ms: it is not the limiter
  while the CPU needs 16-52 ms for those same frames, but it is the next wall once the CPU side is fixed for
  Sumika 2nd's 25× overdraw peaks. On hardware the profile build's `gpu` column is real: run
  `tools/bench/aos2_bench_practice.txt` there to replace this estimate.
- Memory bandwidth: sprite atlases sampled from FCRAM (linear heap) compete with the CPU for the FCRAM bus during
  exactly the heavy frames; VRAM-resident atlases avoid that (texture plan below puts all sprites in VRAM).

### Recommended fixes (in order of payoff)
1. **One blend state for alpha + additive sprites** (premultiply in the texture combiner, no asset change):
   blend `ONE, ONE_MINUS_SRC_ALPHA`; tint RGB pre-multiplied by tint alpha on the CPU; combiner stage 1
   `rgb = prev.rgb × tex.alpha`, `alpha = prev.alpha`; tint alpha = a for blend 1 and **0** for blend 2.
   Both formulas are then exact (`src·a + dst·(1−a)` and `src·a + dst`) and only texture changes flush.
   Measured upper bound: the `one blend` row. Needs citro2d's texenv to be overridden (or a small custom
   sprite batcher, which would also remove most of the ~6 µs/quad C2D_DrawImage cost).
2. HUD: draw the slanted gauges as single textured quads (custom vertices) instead of per-scanline strips,
   and/or cache static HUD parts in an off-screen texture: up to −2.5 ms every frame.
3. Bottom-screen panel (0.8 ms) only when its content changes.
4. Tsih worst tick: broad phase for the many beam/laser × effect pairs (perf proxy: 24k pairs in one tick).

## Texture memory plan (prototype on perf-bench, measured 2026-11-10)

Inputs. `tests/run_texusage.sh` (tests/texusage.c) replays 32 CPU-vs-CPU matches (16 mirrors + 16 pairings,
20000 frames each, hyper pinned) through the renderer's sizing and records, per atlas frame, its blend modes and
its magnification m = screen pixels per stored texel (atlases are already stored at aos2pack `-scale 0.5` of the
PC art; the 3DS draws at 0.4/zoom, zoom 0.65-2.0):
- Character bodies are minified even at the closest zoom (Tsih body frames m ≤ 0.46, Suguri `sg_std` ≤ 0.44,
  Sumika feathers 0.5-1.2): half-resolution body sprites are at most ~1:1 on screen.
- Effects are already magnified: effectMdl m P50 1.4 / P90 4 (explosions, halo, hyper: up to 12-18), the long
  beams/lasers inside character atlases 4-16 (`sg_slash`, `sg_lsr`, `sa_laser0`, `sab_laser1`). Halving makes
  them 2× blurrier; they are soft glows, so the loss is in edge crispness, not shape.
- Only blend 1 (alpha) and 2 (additive with alpha) occur in the draw list. 336 frames are only ever drawn with
  blend 2 (10-50 % of a folder's drawn area), the rest use alpha or both.

Variants (`tools/bench/make_tex_variant.sh`, aos2pack `-fscale/-mip/-premul`, aos2mesh `-texdiv`;
`tools/bench/use_tex_variant.sh` builds a .3dsx per variant). Memory measured at match start in the emulator
(profile log); visual = same deterministic match frames captured in each build (`tools/bench/run_capture.sh`),
`build/preview/perf_<match>_<frame>.png` (grid of all variants + 3× zoom of the most-changed region; mean
|diff| per pixel vs current printed in each label).

| option | atlas sizes | linear free at match start (Sumika×2 / Tsih×2 / Sumika2nd×2 / Suguri×2 on Rain) | VRAM free | visual cost | frame time (emu) |
|---|---|---|---|---|---|
| current | effect 2560, beam 256, particle 64, system 2048 KB (VRAM); chars 4.1-6.1 MB each, mipmapped (linear) | 11.9 / 13.3 / 14.9 / **2.3 MB** | 1.0 MB | — | — |
| (a1) VFX ¼ scale (effect/beam/particle at 0.25 of PC) | 2880 → 592 KB | 13.6 / 14.3 / 16.9 / 4.7 (+1.0…2.4) | 0.9-2.3 MB | low: thin rings and explosion edges softer (diff 0.15-1.9) | none |
| (a2) character atlases ¼ scale, no mipmaps | 4.1-6.1 MB → 1.0-1.3 MB each | 18.7 / 19.4 / 18.0 / 12.3 (+3.1…10.0) | 1.0 MB | low on bodies (≈1:1 at closest zoom); medium on the big magnified beams (Suguri's laser heads, Sumika lasers lose their hard core) (diff 0.6-3.6) | none |
| (a3) same with mipmaps | 1.4-1.7 MB each | 18.0 / 18.7 / 18.0 / 11.2 | 1.0 MB | identical to (a2) in the captures: at ¼ scale sprites are rarely minified > 1.4×, mipmaps buy nothing | none |
| (b) ETC1 (no alpha, premultiplied) for blend-2-only frames | effect ±0, beam +32 K, Tsih −512 K, Suguri −171 K, Sora +336 K (own pages round up) | — (not worth building) | | exact by construction | — |
| (c) stage textures ½ (aos2mesh `-texdiv 2`, mip level 0 dropped) | all stages 19.5 → 4.9 MB; Rain/Equator 2389 → 597 KB, Dust Storm 1365 → 341, Guardian's 213 → 53, stage 5 31 → 7 | +1.8 MB on Rain/Equator, ~0 on stage 5 | | low-medium on close-up stage surfaces only (mipmaps were already used at distance) | none |
| (c') systemMdl: noise/speedline/loading ¼, gauge/damage kept | 2048 → 1024 KB | VRAM | +1 MB | none seen (noise and streak overlays) | none |
| **(d) a1 + a2 + c + c' ("lowmem")** | common 1.8 MB, chars 1.0-1.3 MB → **every sprite atlas fits VRAM** (Suguri×2: 256 KB page left in linear) | **21.8 / 22.5 / 20.0 / 17.0** (+7.6…14.7) | 1.0-2.3 MB | sum of the above (diff 2.2-5.2); overall look intact at 1×, softer glows | none (cpu −0.2…−0.6 ms in the captures, noise-level) |

The emulator cannot show the GPU-side benefit; on hardware (d) moves all sprite sampling from FCRAM to VRAM
(no CPU/GPU bus contention on heavy frames) and shrinks texel footprint 4× (better texture-cache hit rate for
the magnified effects). It does not reduce fill (same screen coverage).

Recommendation:
1. Ship (a1) + (c') now: VFX and system overlays at ¼, all common atlases stay in VRAM with 2.3 MB to spare
   (−2.3 MB VRAM, +1-2.4 MB linear), nearly invisible.
2. Character atlases ¼ (a2) **without** mipmaps, but keep the magnified beam/laser frames at ½ via a frame
   override (`-fscale "chara*/..."` style list from texusage: frames with m P90 > 2) — that removes the only
   visible loss, costs ~0.2-0.4 MB per character, and both characters still fit VRAM. The mipmaps (+2.9 MB) can
   go: at ¼ the shimmer source (2.5× minified missiles) is gone. Check Suguri's missiles on hardware: the
   captures did not catch a missile volley.
3. Stage textures ½ (c): −1.8 MB worst case, only close-up stage detail softens.
4. Skip (b): page rounding eats the savings. Premultiplication is worth it only as the blend fix
   (bottleneck section, fix 1), which needs no asset change.
Result: worst case (Suguri×2 on Rain) 2.3 → ~16-17 MB linear free, which also leaves room to preload every SE.

Open item seen in the captures: in Tsih×2 a P2 sprite is present in one texture set and absent in another at
the same match frame (3 consecutive frames, HUD numbers identical, so the simulation agrees) — e.g.
`build/preview/perf_tsih_820.png` (absent with the current mipmapped atlases) vs `perf_tsih_2620`
(absent with ¼ atlases). Either an Azahar texture-cache artefact or a draw that depends on the atlas; worth
a look on hardware. **Resolved:** a renderer bug with vertically mirrored sprites (see "Render pipeline" below).

Golden trace in this worktree: identical (`run_golden.sh`'s plain `diff` reports all 48 lines because the
Windows exe writes CRLF; `diff --strip-trailing-cr` is clean). `tests/run_tests.sh` passes.

## Render pipeline + texture plan (2026-11-10, implemented in the main tree)

Same method as the practice benchmark above (Azahar Old 3DS mode, profile build, CPU vs CPU Hard, `cpu` = frame
minus vsync wait, emulated µs). List: `tools/bench/aos2_bench_perf2.txt` (stage 5 mirrors with pinned hyper from
the practice list + the heavy-stage rows of `aos2_bench_perf.txt`); logs `build/bench/p2_base.txt` (before),
`p2_pma2.txt` (step 2 only), `p2_plan7.txt` (everything); memory at match start from 240-frame runs
(`p2_memb.txt` / `p2_mem7.txt`). `python tools/bench/summarize.py <log>` prints the tables. Private emulator copy:
`build/azahar_perf` (the bench scripts take `AZ=`); `build.sh` takes `AOS_OUT=` for a private build directory
(`AOS_OUT=$PWD/build/p2 PROFILE=1 ./build.sh`). Captures: `tools/bench/run_caps_perf2.sh <name> <3dsx>`.

### Before → after

| scenario | cpu avg (ms) | cpu P95 | cpu max | frames cpu > 16.7 ms | GPU draws per frame avg / max (before: blend flushes) | linear free at match start | VRAM free | sprite atlases linear / VRAM (MB) |
|---|---|---|---|---|---|---|---|---|
| Sumika ×2, stage 5 | 8.80 → **5.84** | 13.0 → 9.0 | 21.6 → 14.1 | 1.3 % → **0** | 77 / 363 → 30 / 96 | 11.9 → 21.0 MB | 1.0 → 1.3 MB | 7.5 / 4.0 → **0 / 3.7** |
| Tsih ×2, stage 5 | 10.67 → **6.73** | 26.5 → 12.5 | 38.2 → 27.4 | 19.3 % → **1.3 %** | 177 / 1137 → 33 / 82 | 13.3 → 21.8 | 1.0 → 2.3 | 7.0 / 4.0 → 0 / 2.7 |
| Sumika 2nd ×2, stage 5 | 9.63 → **6.27** | 16.0 → 10.0 | 21.3 → 12.9 | 4.0 % → **0** | 114 / 433 → 22 / 47 | 14.9 → 19.3 | 1.0 → 2.3 | 5.0 / 4.0 → 0 / 2.7 |
| Sumika vs Tsih, stage 5 | 8.36 → **5.69** | 15.5 → 10.0 | 21.8 → 15.9 | 4.2 % → **0** | 83 / 439 → 26 / 87 | 12.1 → 21.0 | 1.0 → 1.8 | 7.3 / 4.0 → 0 / 3.2 |
| Suguri ×2, stage 5 | 5.75 → **3.94** | 7.5 → 5.0 | 8.4 → 5.7 | 0 → 0 | 25 / 139 → 27 / 67 | 7.4 → 20.0 | 1.0 → 1.3 | 10.0 / 4.0 → 0 / 3.7 |
| Star Breaker ×2, Capital in Flames | 11.11 → **8.94** | 16.0 → 13.5 | 19.7 → 17.1 | 3.1 % → **0.1 %** | 16 / 85 → 28 / 54 | 9.2 → 18.9 | 1.0 → 2.3 | 7.5 / 4.0 → 0 / 2.7 |
| Nanako ×2, Capital in Flames | 13.70 → **10.01** | 18.0 → 12.5 | 22.4 → 14.8 | 11.7 % → **0** | 104 / 413 → 38 / 68 | 11.9 → 21.0 | 1.0 → 2.3 | 7.0 / 4.0 → 0 / 2.7 |
| Tsih ×2, Dust Storm | 13.90 → **10.02** | 28.0 → 15.0 | 30.7 → 16.4 | 20.4 % → **0** | 143 / 955 → 32 / 82 | 9.4 → 18.9 | 1.0 → 2.3 | 7.0 / 4.0 → 0 / 2.7 |
| Suguri vs Sora, stage 5 | 6.16 → **4.07** | 7.0 → 5.0 | 10.3 → 7.7 | 0 → 0 | 25 / 123 → 30 / 65 | 8.1 → 20.0 | 1.0 → 1.3 | 9.5 / 4.0 → 0 / 3.7 |
| Suguri ×2, Rain (memory worst case) | | | | | | 2.3 → **16.5** | 1.0 → 1.3 | 10.0 / 4.0 → 0 / 3.7 |
| Nath vs Alte, Rain | | | | | | 2.8 → **13.9** | 1.0 → 1.8 | 7.8 / 4.0 → 0 / 3.2 |

Per section, Tsih ×2 stage 5 (avg / max µs): 2D submit 4189 / 21986 → 1428 / 5394; HUD 2358 / 3166 → 1235 / 1867;
draw list 1266 and tick 1045 / 12638 unchanged; panel 823 → 969 (see 5. below). After step 2 alone
(`p2_pma2.txt`, old character atlases): cpu 7.99 ms, P95 13.5, 1.4 % over budget — the blend unification is most
of the win. The texture plan moves no CPU time in the emulator (Azahar does not model FCRAM/VRAM bandwidth) but
frees 8.5-14 MB of linear memory and puts every sprite atlas of a match in VRAM.

### What changed
1. **2D batcher** `src/platform/sprbatch.[ch]` + `sprite.v.pica` replaces citro2d for the match (arena ring, draw
   list, HUD, bottom-panel counters): own vertex stream (20-byte vertices, static index buffer, 8192 quads per frame,
   ~750 KB linear), citro2d's projection matrices and depths (identical vertices), `C2D_Prepare()` hands back at the
   end of a pass. **One blend state for PC blend 1 and 2**: premultiplied alpha, GPU blend (ONE,
   ONE_MINUS_SRC_ALPHA); the vertex colour carries tint·a in RGB and a (blend 1) or 0 (blend 2) in alpha; texenv 0 =
   texture × vertex colour, texenv 1 = rgb × tex.a for straight-alpha textures (systemMdl, systemAddMdl). Exact algebra
   for both modes; only texture changes start a new draw. Solid primitives ($tri, $white, rings, rects, lines) use
   an 8×8 white texture. Modes 0/4/5/6/7 keep their own blend state with the raw tint (exact on straight textures and
   solids; on premultiplied atlases texels with a < 1 differ — the 32-match texture-usage replay saw none of them).
2. **Premultiplied atlases offline** (aos2pack `-pma`): RGB × the alpha the GPU samples ((a>>4)·17: tex3ds stores
   ETC1A4 alpha as a>>4), so blend 1 matches the straight-alpha result where alpha is flat and edge filtering is now
   correct (before, transparent texels' RGB was black: dark bilinear fringes). Premultiplied folders get no mipmaps
   (tex3ds' ImageMagick box filter weights colour by alpha: wrong for premultiplied input).
3. **Texture plan** (build.sh): effect/beam/particle and character atlases at ¼ of the PC art; the big magnified
   laser/beam frames at ½ (`-fscale "*/sg_lsr0*=0.5,…"`; new `*/name` key = that frame in every folder); systemMdl
   noise / speedline / loading at ¼ (gauge, damage, digits kept at ½); stage textures ½ (aos2mesh `-texdiv 2`); no
   character mipmaps; result art `res_*` left out of the character atlases (seltex.go packs it separately); no ETC1
   variant. Every match's sprite atlases fit VRAM now (reserve 256 KB; pages that don't fit still go to linear).
   aos2pack re-scales a staged PNG when its size or form changes (stamp file) and drops stale pages.
4. **HUD** (`hud_c2d.c`, `hud.c`): each slanted gauge is one quad split like the PC's own triangle fan
   (TL-TR-BR / TL-BR-BL) instead of one strip per row; weapon rings and hyper arcs go out as triangle pairs
   (`hg_tri2` / `hg_tri_tex2`, the same triangles; ring points computed once per frame). `tests/run_hudpreview.sh`
   is pixel-identical before/after. HUD 2.3-2.8 → 1.1-1.4 ms.
5. **Bottom panel** (`panel.c`): background, separators and the status lines (system font) render into a 512×256
   (Superseded: the panel is now touch Hyper buttons; its cache texture lives in VRAM, allocated at match start, ~0-37 µs/frame.)
   RGB565 texture only when a line changes; the counters are drawn over it each frame. A battle frame skips the bottom
   screen entirely (no clear, draw or transfer: scene_def `bottom_same`, scene.c) when the panel would draw the same
   primitives (hash of the counters' primitive stream + lines) and no fade / announce band / pause is up. With pinned
   hyper the heat counters change almost every frame, so `panel` stays ~1 ms in this benchmark: ~0.35 ms is the
   panel's own work (timed piece by piece), the rest is emulator time charged to that window in every frame
   (`spr_begin` measured 650 µs there and 3 µs inside its own body).
6. Fixed on the way:
   - **"P2 sprite missing" / white or black squares — a real renderer bug, not the emulator**: vertically mirrored
     sprites (negative sy) swapped the sub-texture's top/bottom, which Tex3DS/citro2d read as a *tex3ds-rotated* frame
     (top < bottom), so they sampled a transposed region of their atlas page: invisible or a solid square depending on
     the atlas layout. That is the practice benchmark's open item (`perf_tsih_820`), the white square near Nath at the
     start of a story fight (reported by the menu agent, `build/P/story/png/s03_top.png`) and a dark rotated square
     under Sora. Mirroring is now applied per corner (render.c). Azahar's software renderer showed the same squares.
     Evidence: `build/preview/perf2_mirror_bug_sora900.png` (old / ¼ atlases before the fix / after),
     `perf2_nath_banner_panel.png` (old vs new: Nath's round start, banner, bottom panel).
   - The round banner's noise strip was drawn one strip height below its rect (negative quad height in citro2d's
     C2Di_CalcQuad); now in place (`perf2_final_tsih_2620.png`).
   - Nath's transform flash approximated the 3-D sphere mesh with one textured 256 quad (a flashing square); now the
     sphere's front view as 12 textured slices (draw code in nath.c only; golden trace identical).
   - The batcher first detected a new frame with `C3D_FrameCounter`, which advances asynchronously on the screen
     transfer: it once reset the vertex buffer between the top and bottom passes and the panel's vertices overwrote
     the top screen's (grey boxes in some captures). `render_world` resets it now.

### Visual checks (`build/preview/perf2_*.png`: both frames + 3× zoom of the most changed region; mean |diff| per
pixel 0-255 vs the old renderer at the same deterministic match frame)
- Step 2 with the original atlases (straight alpha through texenv 1; isolates the batcher + blend unification,
  `perf2_s2_*`): Tsih 3530 0.09, 3500 0.21, Suguri 0.08-0.11, Sumika 2nd 0.20-0.21, Sumika 2070 0.14, Star Breaker
  0.25-0.39, Nanako 0.64 (rounding of the premultiplied tint, ≤ 1-2 LSB). The rest are the fixes: Tsih 820 0.53 and
  Sumika 2130 0.81 = a P2 sprite that the old renderer lost to the mirroring bug is drawn; Tsih 2620 9.9 = banner.
- Step 2 with premultiplied VFX / msg / charaCmn atlases (`perf2_step2_*`): Tsih 3500/3530 0.17 / 0.08, Sumika
  2070/2130 0.27 / 0.10, Suguri 0.19-0.20, Star Breaker 0.30-0.61, Nanako 0.58 (bilinear edges of the premultiplied
  atlases). Outliers there are the bugs above: Tsih 2620 (banner), Tsih 820 and Sumika 2nd (grey boxes = the
  frame-counter race, fixed afterwards).
- Final (`perf2_final_*`): 1.4-5.3, in line with the plan's prototype (softer glows and explosion edges, bodies ≈ 1:1);
  Tsih 820 3.5 (P2 is drawn now), Tsih 2620 11.4 (banner fix).
- Missiles (`perf2_missiles.png`, 8 consecutive frames of a rocket/missile volley around Suguri in Suguri vs Sora,
  rows alternate old / new): rotation is continuous, no 8-direction stepping; at ¼ the missiles are ~1:1 on screen,
  so the dropped mipmaps cost nothing visible.

### Remaining bottlenecks (Tsih ×2 stage 5, avg)
Draw-list build 1.27 ms (max 6.7), 2D submit 1.43 (33 draw calls + ~5 µs/quad of vertex math), HUD 1.24 (~1500
ring/arc triangles at ~0.6 µs), panel ~1.0 (above), stage draw 0.35 (2.9 Capital in Flames, 4.2 Dust Storm), tick
1.0 avg but **12.6 ms worst** (Tsih: ~24k collision pre-checks in one tick, perf proxy) — now the main cause of the
remaining over-budget frames (1.3 % in Tsih ×2). Not done: a tighter collision broad phase (task 6).

### Real Old 3DS estimate (emulated µs × 1.5-2)
Tsih ×2 stage 5: avg 10-13.5 ms, P95 19-25 ms (was 16-21 / 40-53): 60 FPS most of the time, drops in the densest
Tsih stretches. Sumika ×2 9-12 ms, Sumika 2nd ×2 9.5-12.5, Nanako ×2 on Capital in Flames 15-20 (stage 2.9 ms),
Tsih ×2 on Dust Storm 15-20 (stage 4.2 ms) — the stage draw is now the item to look at on those stages. GPU: all
sprite sampling from VRAM with a ¼ texel footprint; fill unchanged (1.7-2.5 Mpx peaks ≈ 5-12 ms of GPU, overlapped
with the CPU's next frame). Only hardware can confirm.

## Atlas bleed (2026-11-10, after the first real Old 3DS test)
Reported on hardware: thin lines / fringes along sprite edges that should be transparent. Two causes, both in the
-pma atlases (decoded with a Python ETC1A4 decoder from the .t3x, not tex3ds' preview PNG, which hides the second):
1. **Atlas neighbours.** Frames are packed edge to edge; `spritebank_clamp_sub` clamps texture coordinates to the
   frame's edge texel centres, which leaves zero margin. With a texture-coordinate error of e texels (the PICA's
   fixed-point coordinates / filter weights; the emulator samples in float and shows nothing) the neighbour leaks in
   at e × its colour: over 10 match pages, 760 of 2352 frame sides change by >= 8/255 already at e = 1/8 (worst
   32/255; 64/255 at e = 1/4) — a thin bright or dark line along the quad edge.
2. **ETC1 colour under alpha 0.** ETC1 codes colour per 4x4 block independently of the 4-bit alpha, so in a
   premultiplied page transparent texels next to bright ones decode to colour up to 73/255, which the (ONE,
   ONE_MINUS_SRC_ALPHA) blend adds to the screen (1385 such texels > 8/255 in 7 pages), and 4541 texels with
   a < 1 decode brighter than their alpha.

Fix (aos2pack `-edge`, used for every folder that was `-pma`): straight alpha, the colour of invisible texels
dilated from the nearest visible ones (smooth ETC1 blocks, no dark bilinear edges), premultiplied in the texture
combiner (sprbatch texenv 1: transparent texels give exactly 0); every frame gets a 1px border repeating its edge
texels (.idx 9th field; spritebank.c insets the sub-texture by one texel). Frames of 32+ texels are resampled to
(w-2)x(h-2) inside the border so the power-of-two frames still pack edge to edge: atlas sizes unchanged except
particleMdl 128x128 -> 128x256 (+16 KB VRAM; its 8/16-texel frames grow by the border instead). Same metrics
after: 0 frame sides with bleed at any e < 1, no colour under alpha 0 reaches the screen. Emulator captures of
the same match frames differ by 0.35-2.0 mean |diff| (the 2-texel resample; the shield bubble accounts for the
rest).

Also found: the barrier bubble (shield, burst shield, hyper/accel barriers: charaCmnA shield00-03,
FUN_006b2200) was never ported; now drawn in `aos_draw_world` right after the players.
