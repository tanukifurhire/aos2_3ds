# Runbook: installing and testing on a modded 3DS

First real-hardware pass. Nothing in this build has run on a console yet — expect surprises, and
collect the artifacts listed in §6 so they can be fixed.

## 0. What you need

- A 3DS / 3DS XL running **Luma3DS** with the **Homebrew Launcher** (any recent setup from
  3ds.hacks.guide qualifies). Check: hold SELECT while powering on → Luma config menu appears.
- An SD card (FAT32) with **~1 GB free** — the build is a single ~840 MB `.3dsx` (game assets are
  embedded). Use a card reader; copying 840 MB over FTP takes a very long time.
- For netplay tests: a **second console**, same setup, same build.
- Your PC with this repo (builds already in `build/`).

## 1. Build the two variants

```sh
./build.sh                                    # full build (only needed once / after asset changes)
cp build/aos2_3ds.3dsx build/aos2_3ds_play.3dsx
PROFILE=1 SKIP_ASSETS=1 ./build.sh            # profiler/bench build
cp build/aos2_3ds.3dsx build/aos2_3ds_prof.3dsx
SKIP_ASSETS=1 ./build.sh                      # put the normal build back
```

Both builds overwrite `build/aos2_3ds.3dsx`, so copy each one before building the next. Keep the play
build for normal testing. The profile build shows timings on the bottom screen and can run the benchmark.

## 2. One-time console setup: DSP firmware (needed for sound)

Homebrew audio on real hardware needs `sdmc:/3ds/dspfirm.cdc` (the emulator accepts a dummy file; a
console does not). If it's missing, the game runs silently.

1. Get **DSP1** (`DSP1.cia` or `DSP1.3dsx`, github.com/zoogie/DSP1/releases).
2. Run it once from the Homebrew Launcher (or install the CIA with FBI and launch it).
3. Check that `sdmc:/3ds/dspfirm.cdc` now exists.

## 3. Install

1. Power off, put the SD card in the PC.
2. Create `sdmc:/3ds/aos2_3ds/` and copy:
   - `build/aos2_3ds_play.3dsx` → `sdmc:/3ds/aos2_3ds/aos2_3ds.3dsx`
   - `build/aos2_3ds_prof.3dsx` → `sdmc:/3ds/aos2_3ds_prof/aos2_3ds_prof.3dsx` (separate folder, so they
     show as two entries)
3. Make sure `sdmc:/aos2_bench.txt`, `sdmc:/aos2_keys.txt`, `sdmc:/aos2_flow.txt` and `sdmc:/aos2_cap.txt`
   do **not** exist (left over from emulator work, they make profile builds auto-run scripts).
4. Eject safely and boot the console.

## 4. Launch

Use **full-memory mode**: start the Homebrew Launcher by taking over a real title, not as an applet.
- Rosalina menu (L + D-pad Down + SELECT) → *Miscellaneous options* → *Switch the hb. title to the
  current app* while in a game, then start that game from HOME; or
- start *Download Play* from HOME (Luma redirects it to the Homebrew Launcher by default).

Pick **"AoS2 3DS input lab"** (the play build). The first boot creates `sdmc:/3ds/aos2_3ds/config.ini`
and `progress.sav`.

## 5. End-to-end test checklist

Tick each item; note anything off (and the frame/situation) for §6.

**Boot and menus**
- [ ] Logo → title → main menu appear; menu background (3D stage) animates; BGM plays.
- [ ] D-pad and circle pad move the carousel; A confirms, B backs out; bottom-screen help line types out.
- [ ] Hold SELECT in a menu: debug readout shows frame time and free memory (write down the "lin …K" number = free linear memory).
- [ ] Config: change BGM/SE volume, back out, reboot the game → setting kept.

**Controls in a match** (your mapping): L dash, R shield (L+R together too), A weapon A, B weapon B,
X special A, Y special B, L tapped twice quickly = hyper dash; hypers = the touch buttons on the bottom
screen (Accel Hyper in the centre; a button is dimmed while the gauge can't pay for it). Config → Controls
opens the input lab to check each one.

**Versus CPU (Match)**
- [ ] VS select: characters, handicap, colour, input mode, stage, BGM → white fade, "NOW LOADING", fight.
- [ ] Fight: 60 FPS feel, HUD rings/gauges, heat and combo counter on the bottom screen, sounds, music.
- [ ] START after the round begins → pause: Return (3-2-1), Move list, Retry, Exit.
- [ ] Match end → result screen (winner, quote, scores, rank) → back to VS select with choices kept.
- [ ] Try the heavy cases: Tsih vs Tsih, Sumika vs Sumika, Nanako on Capital in Flames, Tsih on Dust
      Storm, Suguri's Accel Burst (missiles + beams). Note any slowdown or audio stutter.

**Arcade**
- [ ] Arcade → difficulty → character select → 6 fights + boss. Lose one on purpose: continue screen
      counts 9→0, continue refights the same opponent.
- [ ] Clear it: staff roll → unlock popup (first clear) → name entry → ranking shows your entry.
- [ ] Reboot the game: Ranking still has the entry; character select shows the clear mark.

**Story**
- [ ] Story → character → conversations (portraits top, text bottom; A advances, hold X/Y fast-forward,
      START skips) → fights → Sumika 2nd form → ending.
- [ ] Main menu, press **Y**: conversation picker — spot-check a few scripts and languages.

**Netplay (two consoles, same build)**
- [ ] Console 1: Online (or 2P Game) → Host match. Console 2: Online → Join match.
- [ ] Pick characters/stage on the host; both start; play a full match. Note any stutter, desync
      message, or disconnect, and the input delay shown on the bottom screen.
- [ ] Repeat with the consoles a few metres apart.

**Stability**
- [ ] Play 5+ matches in a row without rebooting; watch the "lin …K" number (hold SELECT in a menu) — it should come
      back to the same value after each match.
- [ ] HOME button during a match and in a menu → resume works. Close the lid (sleep) → wake works.

## 6. Performance benchmark (profile build)

1. Copy `tools/bench/aos2_bench_practice.txt` to `sdmc:/aos2_bench.txt`.
2. Launch **aos2_3ds_prof**. It runs the listed CPU-vs-CPU matches by itself (several minutes) and quits.
3. Copy `sdmc:/aos2_prof.txt` back to the PC (`build/bench/hw_practice.txt`).
4. Optionally repeat with `tools/bench/aos2_bench_perf.txt` (all stages) — rename the log between runs.
5. **Delete `sdmc:/aos2_bench.txt`** afterwards, or the profile build will benchmark every launch.

Playing the profile build normally also shows live section timings on the bottom screen and appends
to `sdmc:/aos2_prof.txt` (including "SE … loaded on first use" lines worth reporting).

## 7. What to send back

- `sdmc:/aos2_prof.txt` from the benchmark run(s).
- Crash dumps if the console crashed: `sdmc:/luma/dumps/arm11/*.dmp` (Luma shows an exception screen —
  a photo of it helps too).
- Photos/videos of visual issues, with what was on screen (characters, stage, move).
- Your checklist notes: slowdowns (where), audio stutter, controls that feel wrong, netplay results.

## Known limits of this build

- Never run on hardware; GPU fill-rate cost is only estimated (emulator doesn't model it).
- Expect drops below 60 FPS in the densest Tsih mirror moments and possibly on Capital in Flames /
  Dust Storm stages.
- If music stutters: raise `BGM_SYSCORE_LIMIT` in `src/platform/bgm.c` (music decodes on the
  system core with a 40% time slice).
- Menus are English/Portuguese/Spanish; story conversations have all 8 languages.
- Memory (replays) shows "coming soon"; no online play (local wireless only).
