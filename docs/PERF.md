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
