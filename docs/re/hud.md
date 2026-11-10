# In-match HUD, round banners and round flow (hud.md)

Port: `src/platform/hud.c` (layout/logic, platform independent) drawing through
`src/platform/hud_gfx.h` (backends: `hud_c2d.c` on 3DS, `tests/hudpreview.c` on the host),
round flow in `src/sim/flow.c` (state `aos_world.flow`). Host preview:
`tests/run_hudpreview.sh <p1> <p2> <frames> <W> <H> <outdir> [setup] [zoom]` renders the HUD with
the original textures (`tools/drawpng -hud`, `tools/drawpng/hud.go`).

## 1. Where the HUD is drawn

Battle scene frame `FUN_006124b0`: (Game Init if `DAT_00885878`) → input `FUN_006116c0` → tick
`FUN_006129b0` → draw `FUN_005f91d0` → `FUN_005e0150` → transition update `FUN_00561c70`.

`FUN_005f91d0` after the gameplay layer (objects.md §1.4), in order:

| Fn | What | Port |
|---|---|---|
| `FUN_006b2730` | debug distance markers (`DAT_0089437f`) | no |
| `FUN_006b29c0` | **weapon gauges A/B** as half rings around each player (world space) | `weapon_rings` |
| `FUN_005f8a40` | switch to a W×H orthographic 2D projection (origin centre, y up) | — |
| `FUN_006bafd0` | **the HUD**, only if `!paused(DAT_00894330) && !DAT_0087faf8 && DAT_0087fba4 == 0.0` (replays: `DAT_00894352`) | `hud_main` (`aos_hud_visible`) |
| `FUN_005f9e30` | FPS counter (option) | no |
| `FUN_00617c90` | **round-start banner** | `start_banner` |
| `FUN_00608ee0` | **round-end banner** | `end_banner` |
| `FUN_006080f0` | netplay "waiting" banner (`DAT_009b11c0`) | no |
| `FUN_005622a0` | **screen transition** (fades, tile wipes) | `transition` |
| `FUN_006911c0` | pause menu | no |
| `FUN_00495170` | message box | no |

Before the players (same visibility condition) `FUN_006ba550`/`FUN_006b6ec0` draw the
training/frame-data display (config `DAT_00881f64`, netplay, replays) — not ported.

## 2. Coordinates, scale, primitives

The HUD is laid out for the back buffer: `W,H = DAT_009b0fe4/fe8` (= window `DAT_009b0fdc/fe0`),
`s = _DAT_009b1000 = min(W/800, H/600)`, `s4 = DAT_009b1004 = W/800` (`DAT_008149c8` = 800),
`s8 = DAT_009b1008 = H/600` (`DAT_008149b8`). Every offset is `(int)(const * s)` (float multiply,
truncation) — the port does the same (`SC()`), so the 3DS top screen is exactly what the PC game
shows at 400×240 (`s = 0.4`, `s4 = 0.5`, `s8 = 0.4`, aspect 1.667 ≥ 1.6 → widescreen branches).

| PC call | Meaning |
|---|---|
| `FUN_005957d0(rect, cx, cy, hw, hh)` | rect `cx∓(int)(hw*s), cy∓(int)(hh*s)` |
| `FUN_0045bf30(tex; dst, z, src, ARGB, blend)` | blit; u = src.l/texW .. (src.r+1)/texW (src inclusive); dst may be mirrored (r < l) |
| CTexture vtable `0x8464c0` +0x70 = `FUN_0045d2e0(dst[8], z, src, ARGB, blend)` | 4-corner textured fan TL,TR,BR,BL, u = src.l .. src.r (not +1) |
| `FUN_0045c7d0` | blit rotated about the rect centre (tile wipe) |
| `FUN_004789e0` vertex buffers, +0x2c | triangles (x, y, z, ARGB, u, v) in the 2D projection (pixels, origin centre, y up) |
| `FUN_00457c60(rect, ARGB)` / `FUN_00457ae0(x0,y0,x1,y1,ARGB)` | solid rect / line |
| `FUN_005931d0` | font text (not ported) |
| `FUN_005f1b50(v, n)` | n-th decimal digit of v (1 = ones) |
| `FUN_0045a680` / `FUN_00467690` | `(float)sin((double)x)` / `cos` |

Textures (`FUN_005f3510`, `FUN_0065dba0`, `FUN_00561bb0`):

| Global | Texture | Used for |
|---|---|---|
| `DAT_00893dc4` | systemMdl/gauge (1024²) | timer, HP bar, orbs, heat, round markers, portrait disc |
| `DAT_00893dc8` | systemMdl/damage | combo counter |
| `DAT_00893dcc` | systemMdl/bgm_name (64²) | music icon on the round-start banner |
| `DAT_00893dd0/dd4/dd8` (buffers `ddc/de0/de4`) | systemMdl/hyperb/g/r | hyper arcs |
| `DAT_00893e08..e24` | noise0..7 (in buffers) | banner noise strip |
| `DAT_00893dbc` / `dc0` | systemAddMdl/system, system2 | banner titles/letters |
| `DAT_0087fe3c` / `fe40` | msgMdl/msg_leadercrownw / l | HP lead crowns |
| `DAT_0087fa94` | systemMdl/fade | transition tiles |
| `0x885b8c + color*0x3888 + stats(+0x44)*0x18` | `<xx>_system` of the player's palette folder | portrait (src 0,0,511,511) |

Constants below are given as value (address). `cx = W/2`, `cy = H/2 − (int)(275 s)` (0x82537c).

## 3. `FUN_006bafd0` — HUD layout

**Timer block** (centre `cx,cy`):
- disc: gauge src (0x212,0x262,0x33d,0x38d), half (0x39,0x30), ARGB 0x80000000, blend 1.
- 2 sparkles, src (0x2d0,200,0x317,0x10f) half (10,10), 0x80ffffff, blend 2 (additive):
  a = (frame%160)/160·2π (160 0x814168): offset ((int)(cos a·16s), (int)(sin a·32s)) (16 0x8140d8,
  32 0x8140dc); a' = −(frame%120)/120·2π (120 0x814174): ((int)(cos a'·32s), (int)(sin a'·16s)).
- timer 999: ∞ src (800,0xa0,0x3af,0xe7) half (0x24,0x12). Else 3 digits i=0..2 (ones first):
  x = cx + (int)(24s) − (int)(i·24·s) (24 0x853b8c), half (0x12,0x12), digit d src
  ((d%5)·72+600, (d/5)·72, (d%5)·72+0x29f, (d/5)·72+0x47); a leading hundreds 0 when timer < 100 is
  0xff808080. (Config `DAT_00881f70 == 1` in versus tints the timer 0xffff8c00/0xff804500 — default
  0, not exposed.)

**Lead crowns** (msg_leadercrownw/l, src (0,0,0x40,0x40), half (10,10)): aspect = W/H ≥ 1.6
(0x82533c) → centres (cx∓(int)(64s), cy+(int)(34s)) (64 0x8140f8, 34 0x81cc6c), else
(cx∓(int)(150s), cy+(int)(64s)) (150 0x81414c). Higher HP gets W, the other L; tie: both L.

**Per player** i (P1 left; P2 mirrors — `x → W − x`, mirrored dst rects, mirrored src for half orbs):
- Round markers (if roundsToWin `DAT_0087faf0` > 1): y0 = H/2 − (int)(285s) (0x81eeb4);
  P1 x0 = W/2 − (int)(32s) − (int)(20s)·rtw, marker k at x0 + (int)(20s)·k (P2: x0 = W/2 + 32s +
  20s·rtw, minus). k < rtw−1: half (int)(10s), won (k < roundsWon) src (0x397,0xff,0x3be,0x126) else
  (0x3c3,0xff,0x3ea,0x126). Last: centre ±(int)(3s), half (int)(14s), won (0x325,0xf1,0x35c,0x128)
  else (0x35d,0xf1,0x394,0x128).
- Gradient strip: x = (int)(15s)+(int)(90s), y = (int)(20s)+(int)(45s); dst (x, y−8s, x+240s, y+8s)
  (240 0x815584), src (0x200,0x200,0x3df,0x227), 0x80ffffff.
- Portrait disc: centre ((int)(15s), (int)(20s)) half 0x80: src (0,0x200,0x1ff,0x3ff), 0x80ffffff;
  then hyper arcs (below); then the portrait `<xx>_system` src (0,0,0x1ff,0x1ff), white. (Steam name
  text `0x893e38[i]` at (60s+96s, 36s−22s): empty offline.)
- HP bar: x0 = (int)(60s) (P2 W−60s), y0 = (int)(36s), length L = W/2 − (int)(100s).
  Frame quad src (0,200,0x2cf,0x117): TL (x0−(int)(2s4)−(int)(30s4), y0−2·8s), TR (x0−2s4+L+30s4, …),
  BR (x0+L+(int)(12s4)+30s4, y0+2·8s), BL (x0+12s4−30s4, …) — the bar stretches with s4 (widescreen).
  HP is 3 segments of seg = maxHp/3 (k = 0 yellow, 1 green, 2 blue, drawn in that order on top of each
  other). For k: fr = (redHp(+0xd8) − seg·k)/seg, wr = (int)(fr·L); skip if wr < 0.
  - if wr < L: recoverable part, fc = (recovHp(+0xdc) − seg·k)/seg, wc = min((int)(fc·L), L),
    srcW = min((int)(fc·600), 600) (599→600 clamp as the PC), quad src (0,0xa0,srcW,199) [red row]:
    TL (x0, y0−8s), TR (x0+wc, …), BR (x0+wc+(int)(10s), y0+8s), BL (x0+10s, …). Else wr = L.
  - segment: srcW = min((int)(fr·600),600), src (0, 40k+40, srcW, 40k+79), same shape with wr.
    Colour: 0xffe1e1e1 if (int)(fr·L) > L (segment above not empty); else on k = 0 with wr < L a red
    pulse 0xffff00ff|v<<8|v, v = (int)(sin((frame%50)/50.0·2π)·50.0)+0xcd (doubles 0x825348/50);
    else white.
  - k ≠ 0 and (wr < L or k = 2): white glow src (0,0,srcW,0x27), alpha (sin((frame%180)/180·2π)·0.15+
    0.15)·255 (180 0x815258, 0.15 0x813f20), blend 2.
  The bar reads redHp (the damage trail, falls 10/frame toward hp) and recovHp — not hp directly.
- Hyper orbs j = 1..3 (half 0x18): x = x0 + (int)(90s) + (int)((j·32)·s) − (int)(30s) (P2:
  x0 − 90s − (int)(32s)·j + 30s), y = y0 + (int)(30s). lv = (int)hyper; lv = −1 if lv = 0 and
  hyper ≥ 0.5; half2 = lv=1 ∧ hyper ≥ 1.5 (0x814110); half3 = lv=2 ∧ hyper ≥ 2.5 (0x8153e0).
  lv ≥ j: src (lv·0x60, 0x118, +0x5f, 0x177) (all filled orbs show the current level's colour) and
  glow (lv·0x60, 0x178, +0x5f, 0x1d7). lv < j: half orb for (j=1, lv=−1) (0x180,0x118..) /
  (j=2, half2) (0x340,0x288..) / (j=3, half3) (0x3a0,0x288..), glow 0x60 below; else empty orb
  (0,0x118,0x5f,0x177) white. Base colour grey v = (sin((frame%200)/200·2π)·0.15+0.85)·255, glow
  alpha (sin((frame%50)/50·2π)·0.25+0.75)·255, blend 2.
- Heat: v = (int)(heat·100); colour R=255, G=B=255 if v ≤ 100 else B = max(0, 255−(2v−200)),
  G = 255−(v−100). Label "HEAT %" src (0x208,0x398,0x307,0x3c7) at (x0+(int)(240s), y0+(int)(30s))
  half (0x40,0xc) (P2 x0 − (int)(230s), 230 0x824fa8); 3 digits k: x = x0 + (int)((260−15k)·s)
  (P2 x0 − 240s − (int)(15k·s) + 30s), half (8,10), src (32d+0x208, 0x3c8, 32d+0x227, 0x3ef).
- (`DAT_00894386` training HP text — not ported.)

**Combo counter** `FUN_006b5b50(p)` (damage.png, after both player blocks), fade fB = +0x13c,
fA = +0x134, e = 1−fA, colour a = (int)(255 fB) as `a<<24|a<<16|a<<8|a`:
X = 80s + 40s·fB (P2: W − 80s − 40s·fB), Y = 120s.
- "HIT": centre ((int)X−(int)(35s), (int)Y+(int)(45s)), half ((int)((1−fB)·30s+30s), (int)(20s·fB)),
  black (0x348,0xa0,0x3bf,0xef) then white (0x2d0,0xa0,0x347,0xef).
- "DAMAGE": centre (X+(int)(40s), Y+(int)(40s)), half ((int)((1−fB)·60s+60s), (int)(20s·fB)),
  black (0x2d0,0x50,0x3bf,0x9f) then white (0x2d0,0,0x3bf,0x4f).
- 5 damage digits (+0x11c, leading zeros shown) i: centre ((int)X − (int)(36s)·i + (int)(72s), Y),
  half ((int)((24e+36fB)s), (int)((24e+36+36(1−fB))s)); all 5 black (144-px cells at +0x120) then all 5
  white.
- 2 hit digits (+0x128) i: centre (X − (int)(20s)·i − (int)(60s), Y + (int)(40s)), half
  ((int)((10e+25fB)s), (int)((10e+25+25(1−fB))s)); black row 0x2a4, white row 0x240 (100-px cells).
- Tail (ver > 0x5a0): if fB == 0 → +0x128 = 1. +0x128 is read by character hit logic, so the port
  applies this in the simulation (`aos_flow_frame_end`, only on frames the HUD is visible).

**Hyper arcs** `FUN_006b6830(p, centre)`: r_in = 60s, r_out = 100s (floats). For stock k = 0..2,
n = min((int)((hyper−k)·50), 50) segments j, each 2 textured triangles (hyperb/g/r; gradient along v =
j/50, constant along u). P1 angle a(j) = j/50·2.1 − 1.8 (0x825340, 0x825384); P2 a(j) = −1.3415928 −
j/50·2.1 (0x825380). Stocks overlap (same angles), higher stocks on top.

**Weapon rings** `FUN_006b29c0` (world space, both players, only while `DAT_00885964 != 2`):
pulse p = sin((frame%100)/100·2π); f9 = 10 − 5p; alpha ia = (int)(32p)+0x5c. Rings around (x,y):
outer radius 77 (0x81ee98), inner 73 (0x825374). 72 segments, angle j/72·2π; gauge A uses j = 0..35
(upper half), B j = 36..71. Per segment: glow quads 77..77+f9 and 73−f9..73 (alpha 0 → ia), black band
73..77 0x80000000, fill band if j' < (int)(37·gauge) (37 0x81c6f8). Colours (ver ≥ 0x58b: P2 has its own):
P1 A 0x4080ff fill 0x8080c0ff/0x9080e1ff, B 0xff8040 fill 0x80ffc080/0x90ffe180; P2 A 0xff4080 fill
0x80ff80c0/0x90ffa0c0, B 0x40ff80 fill 0x8080ffc0/0x90a0ffc0; the fill alternates every 2 frames.

## 4. Banners

### 4.1 Common frame (both banners; alpha α = fb2c / fba4, timer t = fafc / fb5c)
- Ring (blend 2): 120 segments around the screen centre, rotating (frame%360)/360·2π; inner radius
  b4, width c8: t < 100: b4 = (100−t)²·0.1+280, c8 = (100−t)·2+5; t ≤ 200: 280, 5; else
  b4 = (t−200)²·2+280. Colour (int)(128α)<<24 | 0xe1e1ff. All ·s.
- Letterbox: rects (0,0,W,H/2−(int)(220s)) and (0,H/2+220s,W,H), (int)(192α)<<24 (220 0x817008,
  192 0x81415c).
- Noise strip: half height f10 = ((1−α)·50+50)·s, tiles (int)(256s) wide across W, texture
  noise[(frame/4)%8], vertex colour gray 0x404040, alpha start (int)(255·α·fb30), end (int)(128α);
  texture addressed in texels (u 0..256s, v 128s∓f10) — UNVERIFIED (the buffer's +0x38/+0x3c set the
  addressing; the port maps texel units of the 512² noise).
- Lines at H/2∓f10 across the screen: alpha start (int)(255α), end (int)(192α).

### 4.2 Round start `FUN_00617c90` (system.png, text alpha (int)(255·fb04))
- `fb00` = 0 (one-round match): "STAND BY" src (0,0x200,0x3ff,0x27f), half w (int)(((int)((1−fb04)·256)+
  256)·s), half h (int)((int)(32·fb04)·s). 99: "FINAL" (0,3,0x1e7,0x82) at cx−(int)(130s) half w from
  122/0x7a, "ROUND" (0,0x7c,0x1f7,0xfb) at cx+(int)(126s) (126/0x7e). n: "ROUND" at cx−(int)(34s) and
  digit n (112n,0x104,112n+0x6f,0x183) at cx+(int)(128s) (28/0x1c).
- When the text alpha is 255: 5 glitch scanlines (cosmetic RNG).
- "IGNITION" letters k = 0..7 (cells (k%8)·128, 0x280+(k/8)·128): x = cx − 3·(int)(64s) + k·(int)(64s) −
  (int)(32s); y = H/2 ± (int)((int)((1−f)·32)·s) (k even +); half ((int)((int)(32f)·s),
  (int)((int)((1−f)·64+32)·s)); at f ≥ 1 four 1-px glitch slices from 64-px cells at y 0x140.
- Music icon bgm_name (0,0,0x3f,0x3f) at (cx+(int)(240s)+(int)(112s), H/2+(int)(200s)) half 16, alpha
  (int)(255·fb2c). The BGM title text (font, right-aligned before the icon) is not drawn.

### 4.3 Round end `FUN_00608ee0`
Letters per kind (`FUN_0060b310` arg): 0 KO "FINISH!" cells 9..15, 1 "TIMEOUT" cells 16..22 (system.png,
x = cx − (int)(192s) + (int)(64k·s), y = H/2 ± (int)((1−f)·32·s), half ((int)(32f·s),
(int)(((1−f)·64+32)·s)) — no inner truncation, unlike the start banner); 2 "SURRENDERED" (11) /
3 "DISCONNECTED" (12) from system2.png 100-px cells (netplay). Glitch slices always sample the
"IGNITION" row (as the PC).

## 5. Round flow (`src/sim/flow.c`)

**Start banner** `FUN_006179d0(n)` (from Game Init): `faf8=1, fafc=0, fb00=n, fb2c=fb04=0, fb30=1,
fb08[9]=0, fb34[i]=150+2i`. n = 0 if roundsToWin = 1; 99 if not the first round and both players have
roundsToWin−1 wins; else roundIndex+1 (`DAT_00885a70`, 0 at match start, +1 each Game Init).
**Update** `FUN_00617a60` (tick step 8): fafc < 200: fb2c += 0.02 (≤1); else fb2c −= 0.1 and when
≤ 0 the banner ends (fafc ≈ 209). fb04 += 0.05 for 51 ≤ fafc ≤ 149, −0.1 after 149. Letters:
delay−−; fafc < 201: once delay < 0, +0.1 (≤1); else −0.1. fb30 −= 0.0025 while > 0.5. fafc++.
The round timer only counts while neither banner is up (core.md §6.10); inputs are locked by frame
(< 100 / < 200, core.md §2.3).

**End banner** `FUN_0060b310(kind)`: KO state t==0 (kind 0), time over (kind 1, end of tick).
`fb58=1, fb5c=0, fba4=0`; letter delays 50+3i (kind 0) / 50+2i (others), other sets 9999.
**Update** `FUN_00608a00` (tick step 8): fb5c < 200: fba4 += 0.02; else fba4 −= 0.05 (≥ 0) and, once
fba4 = 0 and fb5c ≥ 301 and not yet done: match decided (`FUN_00619c60`: any roundsWon ≥ roundsToWin or
`DAT_0088596c`) → results (`FUN_00619cb0`), transition 0x12, BGM stop, `fb58 = 0`; else transition 2
(fade to black 0.01/frame) and `fb64 = 1`. With fb64 and fb5c > 349: `DAT_00885878 = 1` → Game Init at
the start of the next frame. Letters as above with threshold 201. fb5c++.

Timeline: KO in tick K → KO state t==0 in K+1 (fb5c 0) → fade to black from K+302 → `nextRound` in
K+351 → Game Init at the start of tick K+352 (fade-in 6: black → clear at 0.01/frame). Time over:
the banner starts after the tick (fb5c 0 is processed one tick later); a time over that decides the
match also sets `DAT_0088596c` (matchFinished: input lock).
Match end: transition 0x12 (20×15 black tiles, tile (c,r) starts after c+r frames, +0.01/frame;
136 ticks until fully covered) → port sets `flow.matchEnd` (the PC goes to the result screen);
the app/rollback calls `aos_match_restart` (new match; first Game Init uses transition 0x13, tiles
shrinking at 0.025/frame).

**Transitions** `FUN_00561fc0(type)` / update `FUN_00561c70` (after the draw each frame; the port runs it
at the start of the next tick, `fadePending`): types 1–16 alpha += rate (1/9: 0.05, 2/10/6/14 0.01,
3/11/7/15 0.005, 4/12/8/16 0.0025) until 1, done → off next update. Draw `FUN_005622a0`: 1–4 black α,
5–8 black 1−α, 9–12 white α, 13–16 white 1−α; 0x11–0x14 tiles: fade.png (0,0,0x20,0x20) rect centre
((int)(40s4)·c+(int)(20s4), (int)(40s8)·r+(int)(20s8)) half (int)(a·20·s4) × (int)(a·20·s8), rotated by
(1−a) rad, colour (int)(255a)<<24.

Verified by `tests/test_flow.c` (start banner 209–211 ticks with the timer frozen, KO → next round 352
ticks, FINAL ROUND at 1–1, time-over decision, 0x12 wipe 136 ticks, restart).

## 6. 400×240 legibility (3DS top screen, native size)

| Element | Size at 400×240 | Verdict |
|---|---|---|
| HP bars (L = 160 px, 6 px tall, frame 12 px) | | clear |
| Timer digits | 14×14 px | clear |
| Portraits | 102 px disc, quarter visible in the corner | clear |
| Hyper orbs | 18 px | clear |
| Combo counter | digits up to 28×48 px | clear |
| Banners (ROUND/FINAL/IGNITION/FINISH!/TIMEOUT) | 40–200 px | clear |
| Round markers | 8 / 10 px dots | small but distinguishable |
| Lead crowns | 8 px | recognisable as icons only |
| Heat digits | 6×8 px | borderline |
| "HEAT" label | glyphs ≈3 px | **illegible** |

Proposal (not implemented): show heat % (and optionally the combo counter) on the bottom screen, or
draw the heat block with a minimum scale of ~0.6 on the top screen.

## 7. Not ported / approximated

- Font text: BGM title on the round-start banner, Steam names, training/replay/netplay texts.
- Noise strip texture addressing (§4.1) UNVERIFIED; glitch slices use a local RNG (the PC uses the
  cosmetic MT stream — visual only).
- 3DS backend: citro2d has no textured quads/triangles: HP parallelograms are drawn as one textured
  strip per screen row (exact affine mapping at pixel granularity); hyper arcs use per-vertex colours
  sampled from the 256-row gradient textures (`hud_grad.h`, the textures are constant along u).
- Orange timer variant (config `DAT_00881f70`), result-screen bookkeeping (`FUN_00619cb0`), replay
  vectors, achievements (`FUN_007ca890`), BGM stop at match end (app side).

## 8. 3DS bottom-screen match panel (port addition)

Because the heat readout is illegible on the 400×240 top screen (§6), matches also show
a panel on the bottom screen. The top HUD stays exactly as the original.
- `hud_panel_draw` (hud.c): for each player, a 56 px portrait, heat as the HUD's "HEAT %"
  label + digits at 0.75× with the original colour ramp, and the combo counter at 0.62×.
  The combo counter uses the same `combo_at` body as `FUN_006b5b50`, so the show/fade
  rules are identical (`comboShowDmg/Hits`, `comboFadeA/B`).
- `src/platform/panel.c` sets the bottom framebuffer to BGR8 and double buffering on a
  citro2d target. It draws six status lines in the system font (`C2D_TextParse`,
  re-parsed only when they change). While the panel is up, printf goes to an off-screen
  console copy. `panel_end` re-initialises the console for the menu, lab and lobby.
- Versus: characters/controls/stage/warnings/winner, plus the SELECT-held stage cost readout.
  Netplay: link, ping, advantage, frame, rollbacks, packets, checksums, sync state and score
  (the full stats still print to the console after a disconnect).
- Preview: `tests/run_hudpreview.sh 10 14 400 320 240 out "panel,combo,heat=1.7" 2`.

Note (render.c `draw_cmd`): this citro2d builds the quad from `-center` with `|w|`.
Mirrored sprites drawn with a negative width and centre were therefore displaced by a
full sprite width, and rotated ones orbited their position. Sprites are now drawn with
positive extents, centred, and mirrored through the texture coordinates (as the HUD
backend does).
