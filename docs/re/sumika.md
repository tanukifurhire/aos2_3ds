# Sumika (handbook id 14, engine 17 = 0x11) and Sumika 2nd form (handbook id 15, engine 18 = 0x12)

Source: `aos2.exe` (corpus `tools/re/dec/all.c`, constants via `xref.exe rd`). Structure follows
suguri.md; shared helpers are used exactly as documented there. `t` = player timer `p+0x154`.
Implement **ver ≥ 0x5a1** (live value 0x5a2). Part A = Sumika, Part B = second form ("SumikaB").

---

## 0. Notes for the lead

1. **The second form is the story-mode final boss**, not a transformation of the playable Sumika.
   `FUN_007befa0` (story sequence builder, cases 0..0xd = story chars 3..0x10) ends every route with a
   match vs character **0x12** on stage 0x13, BGM 0x13 (preceded by a Hime/Kyoko/… match on stage 0x10).
   Arcade (`FUN_004a7cc0`) never uses 0x12. Its textures live in `charaAdd` (`sab_*`, string id 4 for
   both colour variants), it has no S+B+H, HP 12000/15000/18000 by CPU level `DAT_0087fab0` 0/1/2
   (`FUN_0069cc40`), and it has **two phases**: at HP ≤ maxHP/3 it enters 0x24d → permanent 0x24e
   (bullet-hell). Whether it is selectable elsewhere (e.g. versus) is UNVERIFIED — no char-select code
   referencing 0x12 was found.
2. Zero-damage hitboxes still stun (see hime.md §0.2) — Angel Wing 0x137, laser odd frames.
3. Global counters shared between players: `DAT_0087faa4` (Heaven's/Eternal alternation),
   `DAT_009b0984` (SumikaB core pattern index).
4. SumikaB's dispatch returns immediately (no T2 at all) when `DAT_0087faac == 3` (a mode set by
   `FUN_00645530` / menu `FUN_00643150` — probably training; UNVERIFIED).
5. Player init: Sumika starts each round in state **0x21d** (intro, spawns 8 feathers), SumikaB in
   **0x241** (`FUN_0069cc40`). UNVERIFIED whether Player Init runs per round or per match (core.md).
6. objects.md's "Sumika type 15" (`FUN_0066d190`) is Kyoko; Sumika/SumikaB have no absorb handler.
7. Feather enemies keep a **slot number in the Enemy `maxHp` field +0x28** and use `hp` +0x24 as a
   use counter; Enemy `+0x84` is a phase, `+0x80` a phase timer (objects.md's "timer2").

---

# Part A — Sumika

## A1. Shared mechanics

### A1.1 Feathers (the ☆ condition)
Feathers are Enemy objects type 0x11, id 100 (red) / 0x6e (blue). `FUN_00768530(team)` counts own
feathers with phase `+0x84 != 900`. **☆ moves are chosen when the count ≥ 9** (dispatch §A2).
Sources: intro 0x21d (8 feathers, hp 9999, slots 1000..1007) and Over Limit 0x220 (8 feathers, hp 105,
slots 1100..1107). A launch decrements hp only if hp < 1000; a feather in phase 100 with hp < 101 dies
→ Over-Limit feathers survive 5 launches, intro feathers are permanent. So ☆ ≈ "Over-Limit feathers
still alive".

### A1.2 Barrel capture (the ★ condition)
Happy Barrel's enemy (id 300) captures the opponent and refreshes `FUN_006b1a40(opp, 8, 5)` each frame:
status flag `opp+0xab` with timer `+0xcc` = 5 (tick FUN_007516e0: timer−1; flag cleared when timer < 1 or
opp state ≠ 1). **★ = `opp+0xab == 1`.** On B+H with ★, dispatch also finds the barrel in capture phase
(`FUN_00768670`: type 0x11, id 300, phase 300) and sets its `+0x80 = 200` (extends the hold, §A5.4).

### A1.3 Cost table (registry.c:2285-2302; ver patch FUN_004a7270)
| State | costA | costB | gain |
|---|---|---|---|
| 0x21b Pretty Sword (per pair) | 0.2; **ver>0x5a0 0.25** | 0 | 0.025 |
| 0x21e Fantastic Feather (per launch) | 0 | 0 | 0.015 |
| 0x21f Shiny Feather (per launch) | 0 | 0 | 0.02 |
| 0x222 Heartful Arrow (per shot) | 0 | 0.05; **ver>0x5a0 0.03** | 0.01 |
| 0x224 Marble Ball (per ball) | 0 | 0.05 | 0.01 |
| 0x226 Happy Barrel | 0 | 1.0 | 0.15 (DAT_00813f20) |
Others are never charged.

### A1.4 Flourishes
Hyper (0x21c, 0x220, 0x221, 0x225, 0x227, 0x228): `FUN_006b1b40(p,10)`, SE 6, `FUN_004e9280(100,0.75)`,
`FUN_007bec20(100,pos,0,0)`, `FUN_004d7ac0(20,0.5,2)`, effect 0xfa. Accel (0x229, 0x22a): speed 0,
`FUN_006b1b40(p,0x14)`, SE 7, `FUN_004e9280(150,0.85)`, `FUN_007bec20(100,pos,aim[,π/4 for 0x22a])`,
`FUN_004d7ac0(150,0.6,2)`, effect 0xfa. "Face by x" = `+0x84 = +0x88 = (opp.x ≤ x) ? π : 0`.
"Half walk" = ±2.5 per held axis (stats+0 × 0.5).

## A2. Input dispatch `FUN_0077a950` (standard) / `FUN_0077aec0` (alternate: H held + X pressed, as Hime)
Hypers (if `FUN_00780950`), first match:
| # | Condition | Cost | State |
|---|---|---|---|
| 1 | S held, B held, H pressed | 3 | feathers < 9 → **0x22a Pretty Justice**; else **0x229 ☆ Eternal Feather** |
| 2 | S held, H pressed | 1 | **0x228 Angel Wing** |
| 3 | B held, H pressed | 1 | opp `+0xab == 1` → **0x21c ★ Coquettish Sword** (+ barrel `+0x80 = 200`); else **0x225 Tropical Ball** |
| 4 | Dash held, H pressed | 1 | feathers < 9 → **0x220 Over Limit**; else **0x221 ☆ Heaven's Feather** |
| 5 | H pressed | 1 | **0x227 Mystic Blast** |
Normals (state 0/2): Dash → dash; Shield → shield; S+B → **0x226 Happy Barrel**; S+A → **0x21f Shiny
Feather**; B → **0x224** (hold: Marble Ball; release before t=13 → **0x222 Heartful Arrow**);
Dash held(>1)+A → **0x21e Fantastic Feather**; A → **0x21b Pretty Sword**.

## A3. States

### 0x211 neutral (FUN_00776c10) — as Hime/Suguri (5.0/axis). 0x212–0x215 stubs.

### 0x21d — round intro (FUN_00776d40), initial state
At t = 50, 55, 60, 65 (k = 0..3): two Enemy spawns (id 100 if k even else 0x6e) at Sumika's pos,
`+0x58 = +0x84`, phase 0, `+0x80 = 0`, `+0x74 = 10`, slot `+0x28 = 1000+2k` and `1001+2k`, hp 9999;
SE 0x2f (`power35`). t > 100 → state 0. **Every frame: hurtbox `+0x5c..+0x68 = 0`** (untouchable intro).

### 0x21b — **Pretty Sword** (FUN_007781a0) — A (hold)
| t | Action |
|---|---|
| 0 | aim |
| 12 | if cost(0x21b): re-aim; missile **100** at pos, angle aim+0.75 (DAT_00813f3c), age preset 30; whip 9 (slot, missile serial); missile **0x96**, aim−0.75, age 30, whip 10; SE 0x10. Else t := 100 |
| 12 | if A not held (`+0x38 < 2`) → t := 100 |
| 17 | same with ±1.25 (DAT_00815dbc) (100 at aim−1.25, 0x96 at aim+1.25), age 15; fail → 100; then release check |
| 22 | same with ±1.75 (DAT_00826420), age 0; fail → 100; release check |
| 25 | t := 100 |
| ≥ 105 | dash-cancel; > 125 state 0 |
(Pair 1 always fires; pairs 2/3 require A held at 12/17.)

### 0x21e — **Fantastic Feather** (FUN_00777750) — D+A
t0 aim. 10<t<22 half walk. t=15: `FUN_00767630(team, 0)` launches one feather (§A5.1); none → t := 22;
else cost(0x21e), re-aim, SE (0x2f,4). t=21 && A held → t := 12 (repeat). Friction ×0.25 each frame.
≥ 25 dash-cancel; > 35 state 0.

### 0x21f — **Shiny Feather** (FUN_00777980) — S+A
t0 aim. t=10: `FUN_00767a70(team, 0)`; none → t := 20; else cost(0x21f), re-aim, SE (0x2f,4).
t=18 && A held → t := 8. ≥ 22 dash-cancel; > 30 state 0.

### 0x224 — B press / **Marble Ball** (FUN_00778b60)
t=0 and t=14: face by x. t<13 && B not held(<2) → aim, state **0x222** (timer kept).
13<t<22 every frame: if cost(0x224): missile **300** at pos, angle rand_f(−0.628, 0.628) + π/2
(DAT_00826428/18); SE (0x2f,3) (up to 8 balls). ≥ 25 dash-cancel; > 50 state 0.

### 0x222 — **Heartful Arrow** (FUN_007794f0)
last = 28 (ver ≤ 0x5a0) / **29**. 12..last every frame: if cost(0x222): offset (−rand_f(0,150),
rand_f(−180,180)) rotated by aim + pos; t even → missile **200** + Enemy **200** (decoy visual) at the
point, angle aim; t odd → missile **0xfa** + Enemy **0xfa**; SE (0xe,4). ≥ 30 dash-cancel; > 50 state 0.

### 0x226 — **Happy Barrel** (FUN_007790a0) — S+B
t0 face by x, speed 0. t=5: if cost(0x226): Enemy **300** at (x, y+100), angle `+0x84`; SE 0x39.
t=15: `FUN_007685e0(team)` (barrel with phase 100) → launch angle a: facing right (`+0x84 ≤ π/2`):
π/4, y+ held 3π/8 (DAT_0082641c), y− held π/6 (DAT_00817ddc); facing left: 3π/4 (DAT_0081fa94),
y+ 5π/8 (DAT_00826424), y− 5π/6 (DAT_00825e78) (y− wins if both). Barrel `+0x58 = +0x5c = a`,
`vy(+0x70) = sin a·10`, `vx(+0x74) = cos a·10`, phase 200, `+0x80 = 0`; SE 0x23.
≥ 25 dash-cancel; > 40 state 0.

### 0x227 — **Mystic Blast** (FUN_00779820) — H
t0 aim, flourish. t=10: lasers **100** and **0x96** at pos, aim; SE 0x10. 10<t<60 half walk.
≥ 60 dash-cancel; > 80 state 0.

### 0x225 — **Tropical Ball** (FUN_00778d70) — B+H
t0 aim, flourish. At t = 10+5k, k=0..5: missile **0x15e** at pos, angle (k%4)/3·2π + o_k, ver ≥ 0x5a1:
o = (−0.75, −0.5, −0.25, 0.75, 0.5, 0.25) (DAT_0081fa78, 00853bb0, 0081411c, 00813f3c, 00853b1c,
00813f24); ver ≤ 0x5a0: o = rand_f(−0.75,0.75). SE (0x2f,3). ≥ 50 dash-cancel; > 70 state 0.

### 0x21c — **★ Coquettish Sword** (FUN_00778790) — B+H with captured opponent
t0 face by x, flourish. 6..46 even: a = rand_f(−π,π), r = rand_f(100,240); missile **0x66** ((t/2) even)
/ **0x98** at opp.pos + rot((r,0),a), angle a+π; SE (0x10,4). (t%5==0 cos/sin calls are dead code.)
≥ 80 dash-cancel (never reached); > 60 state 0.

### 0x220 — **Over Limit** (FUN_00777040) — D+H, < 9 feathers
t0 face by x, flourish. t = 10,18,26,34 (k=0..3): two feathers as intro but slots 0x44c+2k/0x44d+2k,
hp **105**; SE 0x2f. ≥ 60 dash-cancel; > 80 state 0.

### 0x221 — **☆ Heaven's Feather** (FUN_00777470) — D+H, ≥ 9 feathers
t0 face by x, flourish; delete own whips of kind 7/8; `DAT_0087faa4 = 0`. 12..60 even: if
`DAT_0087faa4` even → `FUN_00767630(team,1)` else `FUN_00767a70(team,1)`; SE (0x2f,4);
`DAT_0087faa4++`. ≥ 60 dash-cancel; > 80 state 0.

### 0x228 — **Angel Wing** (FUN_00779f00) — S+H
t0 face by x, speed 0, flourish. 6..50 even: Sumika effects 700+0x2ef (t%4==0) / 0x2bd+0x2ee at pos,
−π/2. 15..40: t%5==0 → hitbox **0x136** (t%10==0) / **0x168** at (x±130, y−20) (DAT_0081ce58,
DAT_008140fc); other frames hitbox **0x137** (binding) at both points. Sparks 4..64 t%3==0 at (x,y−100)
with `+0x40 = 0.35`; particles 9..64 even (r 30..150) and 10..70 every frame (r 50..200) (RNG).
t=45: `FUN_006b1d70(p,240,20)` + effect 0xa8c. ≥ 55 dash-cancel; > 80 state 0.

### 0x22a — **Pretty Justice** (FUN_00779af0) — S+B+H, < 9 feathers
t0 aim, Accel flourish. t=20: lasers **200, 0xc9, 0xca, 0xfa, 0xfb, 0xfc** at pos, aim; SE 0x10.
10<t<100 half walk. ≥ 120 dash-cancel; > 150 state 0.

### 0x229 — **☆ Eternal Feather** (FUN_00777aa0) — S+B+H, ≥ 9 feathers
t0 face by x, speed 0, Accel flourish; delete own whips 7/8. 12..60 even: `FUN_007680a0(team,1)`,
SE (0x2f,4). Sparks 4..64 (x, y−50) `+0x40 = 0.25`; particles (RNG). ≥ 80 dash-cancel; > 120 state 0.

## A4. Animation

### A4.1 Sprites (loader `FUN_00764930`, variants → text ids 0x29/0x2a/0x2b = charaSumikaA/B/C)
| id | pattern | n | S |
|---|---|---|---|
| 0x211/0x216/0x217 | sa_std/fwd/bck0%d | 3 | 96 |
| 0x213 | sa_los0%d | 6 | 96 |
| 0x215 | sa_sld0%d | 5 | 96 |
| 0x212 | sa_dsh0%d | 6 | 96 |
| 0x218 | sa_rng0%d | 2 | 512 |
| 0x21a | sa_system | 1 | 512 |
| 0x228 | sa_acta0%d | 3 | 96 |
| 0x226 | sa_ball0%d | 6 | 96 |
| 0x22b / 0x22c | sa_fthrr / sa_fthrb (00..17) | 18 | 64 |
| 0x22d / 0x22e | sa_swordr%d / sa_swordb%d | 7 | 96 |
| 0x230 / 0x231 | sa_arrowr0%d / sa_arrowb0%d | 7 | 128 |
| 0x22f | sa_beachball (00..11) | 12 | 96 |
| 0x233 | sa_barrel0%d | 7 | 192 |
| 0x234 | sa_wing%d | 2 | 128 |
| 0x232 | sa_laser%d | 2 | 128 |
| whip tex 7/8 | sa_whipr / sa_whipb | 1 | (stored in 0x87fe48 table) |

### A4.2 Common states → T1: 0→0x211, 1/5→0x214, 2→0x212, 3/4→0x215, 6→0x213.

### A4.3 Frames (transform as Hime: neutral facing-flip, others AIM transform)
| State | T1 | sprite | frame |
|---|---|---|---|
| 0x211 | FUN_0077c540 | 0x211 / 0x216 / 0x217 by input (as Suguri) | pp(5) |
| 0x212 | FUN_0077c7f0 | 0x212 | pp(3); +3 when moving against facing |
| 0x213 | FUN_0077d150 | 0x213 | pp(3)+3 if global frame even; hidden t ≥ 100 |
| 0x214 | FUN_0077cbf0 | 0x213 | 0 |
| 0x215, 0x21b, 0x21d, 0x21e, 0x21f, 0x222, 0x224, 0x225 | 77ce90, 77e500, 77d450, 77dcb0, 77df70, 77f2f0, 77ea90, 77ed50 | 0x215 | min(t/4,4) |
| 0x21c, 0x220, 0x221, 0x228, 0x229 | 77e7c0, 77d710, 77d9e0, 77fbb0, 77e230 | 0x228 | pp(4) |
| 0x226 | FUN_0077f010 | 0x226 | t<15 ? 0 : min((t−15)/4+1, 5) |
| 0x227, 0x22a | 77f5b0, 77f8b0 | 0x226 | t<10 ? 0 : q=(t−10)/4, f=q+1, f>2 → ((q−2)&3)+3, f==6 → 4 |
Afterimage only in 0x211, 0x212.

## A5. Objects (type 0x11)
Tables: missile 0x8819fc FUN_00681e00 / upd 684d70 / draw 682540 / onHit 684620 / onKilled 684c90;
hitbox 0x881bdc FUN_005edcb0 / onHit 5ee310; laser 0x881c7c FUN_00630490 / upd 62f6f0 / draw 62d040 /
onHit 62f6d0 (no-op); enemy 0x88159c FUN_00551160 / draw 55d070 / upd 55add0 / killed 55cbf0;
beam 0x8818bc FUN_004b8d80 / upd 4b9b70 / draw 4b9050 / hit 4b96e0; effects 0x8816dc FUN_00511880;
whip 0x881dbc FUN_007c96f0 / upd 7c9f60 / draw 7c9880.

### A5.1 Feather enemies (id 100 red / 0x6e blue), update FUN_0055add0
Spawn: `+0x7c = 100`, phase set by caller, hp/slot by caller, inactive, no shape (never hit),
scale (3,0,0). Fields: `+0x40` pos, `+0x58` facing angle, `+0x5c` move angle, `+0x70` speed,
`+0x74` spin sign/param, `+0x80` phase timer (+1 per frame by the list), `+0x6c` byte mirror flag.

**Formation** `FUN_0055a0a0(e, s) → (pos, ang)`: owner O; slot q = `+0x28`; n = (q−1000)/2, r = 20s + 5n
if q < 1100, else n = (q−1100)/2, r = 25s + 6n (DAT_00813f44). w = sin(((DAT_008858f8 % 300)/300)·2π)
(global frame counter). front = (O.facing==1 && q even) || (O.facing==0 && q odd). A = O+0x84,
L = |A| ≥ π/2. ang (q<1100; for q≥1100 add the bracketed term to the base offset):
- O not dashing, front: L ? −0.15n + A + 0.1 [+0.15] − 0.2w : 0.15n + A − 0.1 [−0.15] + 0.2w
- O not dashing, back: L ? 0.15n + A + π − 0.1 [−0.15] + 0.2w : −0.15n + A + π + 0.1 [+0.15] − 0.2w
- O dashing (state 2), front: L ? 0.15n + A − 2.5 [−0.075] − 0.2w : −0.15n + A + 2.5 [+0.075] + 0.2w
- O dashing, back: L ? 0.15n + A − 3 [−0.075] − 0.2w : −0.15n + A + 3 [+0.075] + 0.2w
pos = O.pos + (cos,sin)(ang)·r. (Constants: 0.15 DAT_00813f20, 0.1, 0.2 DAT_00853b14, 2.5 DAT_008153e0,
3 DAT_00813a50, 0.075 DAT_00813efc, 300 DAT_008145f0.)

**Phases** (`+0x84`; "+1" odd variants behave identically, they mark hyper launches):
| phase | behaviour |
|---|---|
| 0 | scaleX −0.2, scaleY +0.1, Z +0.1; scaleY ≥ 1 → (1,1,1), phase 100 |
| 100 | idle; if hp < 101 or owner state 6 → 900 |
| 200/201 | pos += dir(+0x5c)·speed + rot((0, ±cos(t/50·π)·10), +0x5c) (sign flipped when angle<0 & mirror 0 or angle>0 & mirror 1); `+0x58 += 0.0628·+0x74`; t > 49 → 210/211, t=0, speed 0 |
| 210/211 | pos −= dir(+0x58)·speed, speed ×0.95; **t=5**: turn `+0x58` toward opponent ≤ 0.75 (`FUN_0055f110` = atan2 to opponent, Tsih special case as FUN_007804b0), `+0x5c = +0x58`; red → **beam 500**, blue → **missile 500** (owner arg = feather's enemy id) at pos, that angle; SE (0xd,3). t > 99 → 290, speed 20 |
| 290 (0x122) | return: (P,a) = formation(s = (50−t)/5 + 1); turn +0x58 toward a ≤ 0.05; move forward at speed; then pos += (P − pos)·(t/50)²; t > 49 → 100 |
| 300/301 | like 200 with period 25 (DAT_008140f0), spin 0.1257·`+0x74`; t > 24 → 310/311, aim at opponent (≤0.75), speed 20, SE (0x25,3), 5 sparks 0xa32+rand&3 (RNG) |
| 310/311 | move along +0x58; t>10: speed ×0.95. If opp `+0xf8==0 && +0xa2==1` and circle r=30 overlaps opp hurtbox: pivot `+0xbc/+0xc0 = 0`, angle = atan2 to opp, **`+0x08` (owner) := opp's id**, → 350/351, t=0. t > 79 → 290 (speed 20) |
| 350/351 (stuck) | t=1: hitbox **100** (red) / **0x96** at pos, angle, owner = `+0x08`. Victim V = player with id `+0x08`; if V KO'd or `V+0xf8 ≠ 0` → t := 200. pos = V.pos − dir·30. Even t: red: V heat +0.003 (DAT_00813ea0); blue: V hyper −0.00075 (floored 0); both: V `+0xdc −= 1` (not below `+0xd0`); particle. t == 200 → 290 |
| 400 | like 300 (period 25); t > 24 → 410, SE (0x21,4), aim at opp ≤0.75, speed 20, SE (0x39,3), sparks |
| 410/420 | move, decel after 10; 410: if opp hittable and circle r=50 overlaps → hitbox **600** (red) / **0x262**, phase 420. t > 79 → 290 |
| 900 / other | scaleX +0.2, scaleY −0.1, Z +0.1; scaleY ≤ 0 → removed |
After the phase code, phases 0/100/900 snap to formation(s = scaleX).

**Launchers** (all scan the Enemy list **from the end**, own team, type 0x11, id 100/0x6e):
- `FUN_00767630(team, hyper)` (Fantastic): eligible phase 100 (or, if hyper, phase%10 == 0). Sets 200
  (+1 if hyper), t=0, hp−1 if < 1000. Target P = opp.pos + (cos,sin)(rand_f(−π,π))·rand_f(80,320)
  (RNG order: distance, angle). `+0x58 = wrap(atan2(opp − P) + π)`, `+0x5c = atan2(P − pos)`,
  speed = |P − pos|/50, `+0x74 = (+0x58 ≤ 0) ? −1 : 1`; whip 7 (red) / 8 (blue) from owner slot.
- `FUN_00767a70(team, hyper)` (Shiny): same eligibility; phase 300(+1). P: d = rand_f(160,240),
  a = rand_f(−π,π) (always drawn); **ver>0x5a0 && !hyper**: (d,a) from Sumika's input — y+x+ (−200,
  5π/4 DAT_00826284), y+x− (200, 3π/4), y−x+ (−200, 3π/4), y−x− (200, 5π/4), y+ (200, π/2), y− (−200,
  π/2), x+ (−200, π), x− (200, π), none (0, π). Centre = opponent if hyper else **Sumika**.
  Speed = dist/25. Whip 7/8.
- `FUN_007680a0(team, hyper)` (Eternal): eligible = any phase except 400/410/420; phase 400; centre
  opp (hyper) or Sumika, d = rand_f(160,240), a random; speed dist/25; whip 7/8.

### A5.2 Other enemies
- **200 / 0xfa** (Heartful Arrow decoys, visual): phase 0: scaleX +0.2, scaleY −0.4 until scaleX ≥ 1;
  phase 100: age > 50 → 900; 900: shrink, removed. Inactive.
- **300 Barrel**: hp 1200, shape r=80 (DAT_00814164), `+0x2c` active set when grown.
  - phase 100: grow (scaleY +0.1); owner (by id) not in 0x226 → 900 (inactive).
  - phase 200 (launched): t=2 sparks; vy −= 0.2; x += vx; y += vy; spin ±0.1 by vx sign. vy > 0 and
    |x| > 540 → killed. vy < 0 and y < 0 and outside arena+50 → killed. dist(opp) < 80 and
    `FUN_00780a50(opp)` → **phase 300**, `+0x80 = 210`, angle π/2, draw layer 2, effect 0xaf0, SE 0x39.
  - phase 300 (capture): if `FUN_00780a50(opp)` and (ver > 0x58b: latch `+0xe0 == 0` or dist < 80):
    opp.pos = barrel pos, opp `+0x84 = 0`, state 1, t 0, speed 0, `FUN_006b1a40(opp,8,5)`, latch = 1.
    `+0x80 > 299` (90 frames; Coquettish resets it to 200 → 100 more) or (ver>0x5a0 and opp state 3) →
    hitbox **400** at barrel, angle π/2, killed callback, removed.
  - killed `FUN_0055cbf0`: effects (RNG) + SE 0x1a; removed. Projectiles can kill it (hp 1200) — no
    explosion hitbox then.
- 500, 600, 0x28a, 700/0x2c6/0x2ee exist in this spawn fn but are only spawned through SumikaB's table.

### A5.3 Missiles (`FUN_00681e00`)
| id | by | speed | shape | dmg | knock | hitType | notes |
|---|---|---|---|---|---|---|---|
| 100 / 0x96 | 0x21b | 16 | r16 | 150 | 10 | 1 / 2 | inactive until grow (+0.1) reaches 1 (active); age<50: speed ×0.95; **age 50**: aim at opponent, speed 16; age>80 & out of arena+10 → onKilled |
| 0x66 / 0x98 | 0x21c | −5 → +0.5/f to 16 | rect {30,16} | 60 | 5 | 1 / 2 | like Hime C: active at grow>1 (+0.1/f), age 5 graze effect 100, inactive at 40, fade, removed 50 |
| 200 / 0xfa | 0x222 | 0 → +2/f (from age 10) to 20 | rect {20,16} | 120 | 10 | 1 / 2 | stun 0.35; active at age 5 (grow +0.2); age 10 graze effect 100; inactive 40; removed 50 |
| 300 | 0x224 | vx,vy = (cos,sin)a·12 | r = 20·grow, **proj r same** | 50 → +5/f to 200 | 10 | 2 | gravity vy −0.2 (min −10); bounces off x = ±540 while vy ≥ 0; out of arena+500 (ver ≤ 0x5a0: +50) with vy<0 → onHit, removed |
| 0x15e | 0x225 | 16, −0.2/f to 4 | r = 50·grow, proj r 50 | 50 → +5/f to 300 | 10 | 2 | hp 120. Out of arena+50: if age < 2500: aim at opp, speed 14, **age += 500**, effects, SE (0x39,4); else if speed < 6 → onHit, removed |
| 500 | blue feather | 18 | rect {20,6} | 150 | 5 | 2 | stun 0.75, active; age>20 & out of arena+10 → removed |
| 600 | — | 10 | none | 0 | 0 | 2 | unused by Sumika (no update case — would live forever) |
All onHit/onKilled are visual and return false (destroyed).

### A5.4 Beam 500 (red feather shot, `FUN_004b8d80`)
speed 18, rect {20,6}, dmg 100, knock 5, stun 0.75, hitType 1 (dash passes through), alpha 0; straight;
age > 20 & out of arena+10 → removed; trail effect 500; hit SE (0x13,4). (Ids 100/600/700 unused here.)

### A5.5 Hitboxes (`FUN_005edcb0`, all hitType 3)
| id | dmg | shape | knock | used by |
|---|---|---|---|---|
| 100 / 0x96 | 150 | {60,20} | 5 | stuck feather |
| 200 / 0xc9 | 80 (ver ≤ 0x5a0: 50) | {580,30} | 10 | Mystic Blast lasers |
| 0xfa | 90, **oncePerFrame** | {580,40} | 10 | Pretty Justice lasers |
| 0x136 / 0x168 | 130 | r 120 | 0 | Angel Wing (0x136 on-hit falls through into 0x168 visuals) |
| 0x137 | 0 | r 120 | 0 | Angel Wing binding |
| 400 | 300 | r 120 | 20 | barrel release |
| 600 / 0x262 | 160 | r 60 | 5 | Eternal Feather |
| 500, 0x226, 0x230, 0x23a | see Part B | | | SumikaB only |
On-hits are visual only (SE 0x15/0x16/0x13/0x17).

### A5.6 Lasers (`FUN_00630490`: blockMode 1, hitType 3, dmg 0, knock 10; no segments)
- **100 / 0x96** (Mystic Blast): φ = (age%40)/40·2π (−π for 0x96); origin = owner.pos +
  rot((cos φ, sin φ)·age·2, angle) (spiralling); ages 10..50: hitbox **200** / **0xc9** at
  origin + dir·600; damage only when age%4 == 1. Muzzle every 4 frames (<50). Removed after age 69.
- **200, 0xc9, 0xca, 0xfa, 0xfb, 0xfc** (Pretty Justice): phase offsets 0, 2π/3, 4π/3, π/3, π, 5π/3
  (DAT_00817898, 008203c0, 008203bc, 00853b58, 008203c4). ψ = (age%40)/40·2π + off; origin = owner.pos
  + rot((cos ψ, sin ψ)·age·2, owner.aim) (live); angle = owner.aim + sin ψ·(age·0.5/90); ages 10..70:
  hitbox **0xfa** at origin + dir·600 (damage on age%4==1; oncePerFrame ⇒ ≤1 hit/frame for all six).
  Removed after age 89.

### A5.7 Whips (visual ribbons, `FUN_007c96f0`)
Kinds 7/8 trail a feather (die when it is back in phase 100 or gone), 9/10 a Pretty Sword missile (by
serial). 20 nodes, no collision.

## A6. Sounds
6, 7, 0xd (feather shots), 0xe (Heartful Arrow), 0x10 (laser03), 0x13, 0x14 (bom06, balls), 0x15, 0x16,
0x17, 0x19, 0x1a (barrel break), 0x21 (swing_d), 0x23 (swing_d, barrel throw), 0x25 (reflect.wav, Shiny
dash), 0x2f (power35, feathers), 0x39 (smoke02, barrel).

## A7. Per-character entries (index 17)
0x881354 FUN_0077a950 dispatch; 0x8813a4 FUN_0077aec0 alt; 0x8813f4 FUN_00764930 loader;
0x881510/14/18 AI FUN_0077bd50 / 0077b420 ×2; FUN_00768700 pause list (text 0x361–0x36e).

## A8. Stats (0x88501c)
+0 5.0, +4 14.0, +8 12.0, +0xc.. 0,0xb,5,2,3,7,0,0xb,5; sprites 0x218/0x219/0x21a; text 0x29/0x2a/0x2b;
win font `mdl\win_font_ADD`; `sa_res_bgp`; +0x2c0.. 0,0,0; colour 250,200,100.

---

# Part B — Sumika 2nd form (engine 0x12)

A near-copy of Sumika with its own tables (type 0x12). Below = differences only; everything not listed
behaves as Part A with ids mapped 0x21x→0x23x/0x24x.

## B1. Dispatch `FUN_00771d10` (used for both schemes; 0x8813a8 = same fn)
- If `DAT_0087faac == 3`: return true without running any T2 (§0.4).
- Hypers: S+H → **0x24c** Angel Wing; B+H → ★ (opp `+0xab`) **0x240** Coquettish (+ barrel
  `FUN_0076d1b0` `+0x80 = 200`) else **0x249** Tropical Ball; D+H → feathers (`FUN_0076d070`) < 9 →
  **0x244** Over Limit else **0x245** Heaven's Feather; H → **0x24b** Mystic Blast. **No S+B+H.**
- Normals: S+B 0x24a barrel, S+A 0x243 Shiny, B 0x248 (→0x246 tap), D+A 0x242 Fantastic, A 0x23f.
- State entry is the standard `state = X; t = 0; flag158 = 0; T2(p)` (as Sumika).

## B2. State differences
| State | vs Sumika | Notes |
|---|---|---|
| 0x235 neutral, 0x236 dash stub, 0x238 hit stub | + phase check | if `hp(+0xd0) ≤ maxHp(+0xd4)/3` → state **0x24d**, t 0, run it |
| 0x241 intro | = 0x21d | uses SumikaB table |
| 0x23f A | = 0x21b | cost 0.2/0.02 (not patched); whips 0xd/0xe; no 612830 |
| 0x242, 0x243 | = 0x21e, 0x21f | Shiny feather: no deterministic ver>0x5a0 placement (always random around self/opp) |
| 0x244, 0x245 | = 0x220, 0x221 | whips 0xb/0xc |
| 0x246 / 0x248 | = 0x222 / 0x224 | 0x246 window 12..28 only; cost B 0.05→0.03 patched (0x87e03c) |
| 0x249 Tropical Ball | difficulty-based | d = `DAT_0087fab0`: d=2: 8 balls at t=10+4k, angle (k%4)/4·2π + rand(−0.75,0.75); d=1: 6 at 10+5k, (k%4)/3·2π + rand; d=0: 5 at 10+6k, (k%4)/5·2π + rand |
| 0x24a | = 0x226 | barrel phase 100 checks owner state 0x24a; capture phase has **no latch/distance test and no shield-explode** |
| 0x24b | = 0x227 | lasers type 0x12 |
| 0x24c Angel Wing | hitboxes t **10..40** (Sumika 15..40); rest same |
| 0x240 | = 0x21c | |
| 0x237/0x239 | stubs (return 1) | |

## B3. Phase 2
### 0x24d — transition (FUN_007704e0)
t=0: `+0x88 = atan2(−y, −x)`, `+0x90 = |pos|/100`; speed 0, `+0x84 = 0`; `FUN_006b1b40(p,0x14)` then
**`+0xf8 = 400`**; SE 7; `FUN_004e9280(19999, 0.75)`; `FUN_007bec20(100,pos,0,0)`;
`FUN_004d7ac0(100,0.75,2)`; effect 0xfa; **`FUN_006b1a40(p, 6, 99999)`** (status 6 → `+0xa9` no-hitstun).
Every frame: pos += (cos,sin)(+0x88)·`+0x90` (reaches the arena centre at t=100); t<100: hurtbox
`+0x5c..+0x64 = 0`. t ≥ 100 → state **0x24e**, t 0, run.

### 0x24e — final pattern (FUN_00770750), never exits by itself
t=0: `+0x84 = 0`; Enemy **500** (core) at pos; SE 0x39 and 0x33. Then with d = `DAT_0087fab0` and
aimR = `FUN_007804b0(p)` recomputed each use:
- **Tentacles (missile 600)**: d=2: t ≥ 300 and t%300 ∈ {1,31,61,91}; else t%300 ∈ {1,51}. Angle =
  aimR + rand_f(0,0.75) for 1/61 (turn `+0x74 = −1`), − for 31/91/51 (turn +1); spawn at pos + dir·50;
  SE 0x22.
- **Tropical balls (missile 0x15e)**, random angle: d=1 t ≥ 1000 and t%250==1; d=2 t%200==1; d=0 none.
  SE (0x2f,3).
- **Lasers 300** at pos + dir·100 (+ effect 0xaf0): if hp > maxHp/6: random dir; d=0 every t%50==0;
  d=1 t%100 ≥ 70 and t%10==0; d=2 t%100 ≥ 70 and t%6==0. If hp ≤ maxHp/6: aimed ± rand: d=0 t%250 ≥ 200
  and t%10==0 (± rand_f(0,0.5), + when t%20==0); d=1 t%200 ≥ 150, t%10==0 (same); d=2 t%200 ≥ 150,
  t%6==0 (± rand_f(0,0.75), + when t%12==0).
- **Satellites**: t%1000 == 900: 4 (d=0) / 6 (d=1) / 8 (d=2) Enemies 700 (even k) / 0x2c6 (odd), `+0x5c
  = k/N·2π`.
Dispatch still runs every frame: a hyper press would leave 0x24e; afterwards neutral re-triggers 0x24d.

### B4. Phase-2 objects (type 0x12; spawn/update FUN_00551f70/00553760, missile 0067da60/00680ca0,
hitbox 005eee50, laser 0062af40/0062c7e0, beam 004b7660/004b8450)
- **Enemy 500 core**: inserted at the **front** of the Enemy list; solid (`+0x20 = 1`), active, hp 9999,
  r = 100·scaleY. Phase 0: scaleX −0.1, scaleY +0.05, Z +0.1 → 100. Phase 100: every 1000 frames picks
  the next pattern from table 0x86c030 (all entries 100, −1 terminator) with global index
  `DAT_009b0984` and resets `+0x80` (no visible effect). Owner KO → phase 900: SE 0x1b,
  `FUN_004e8fd0`, `FUN_004e9280(30,0.75)`, `FUN_004d7ac0(30,0.5,2)`, inactive; sinks 1 px/frame,
  shrinks; every 5 frames to 200: 2 explosion effects + SE (0x1d,20) (RNG); 200: effect 0xaf0, SE 0x39;
  > 200 fades, removed. As a solid enemy it pushes the opponent out of r=100 (objects.md §4.8) and is
  ignored by projectiles.
- **Missile 600 tentacle**: speed 10, grow 1, inactive, dmg 0. heading += cos((age%30)/30·2π)·0.1·`+0x74`.
  Every 3 ages: Enemy **0x28a** segment at pos (`+0x24 = age/3+1000`, age preset `+0x78 = age + 800 +
  2·(age/3)`). On age%3==2 outside arena+10: Enemy **600** at pos, effects, hitbox **500** ({120,60},
  300 dmg, knock 10), SE 0x15, removed.
- **Enemy 0x28a**: no collision; fires **beam 600** at a random angle when `+0x80` == 50 (d=0) or 40/80
  (d≥1), SE (0xf,5); removed when `+0x78 > 999` or owner KO. **Enemy 600**: same lifetime, no attack.
- **Beam 600 orb**: scale 5 → −0.2/f to 1 (inactive), then active, speed +0.1/f to 5; r16, dmg 120,
  knock 5, stun 0.75; removed outside arena+20.
- **Laser 300**: SE (0x12,5) at age 50; ages 50..70: hitboxes **0x226** at origin+dir·300 ({300,16}),
  **0x230** at origin (r40), **0x23a** at origin+dir·450 (r70), each 60 dmg on age%4==1 else 0, knock 0;
  removed after 79.
- **Enemy 700/0x2c6 satellites** (+ child 0x2ee): hp 500, r 40. Phase 100: scale +0.02 → 200 (active).
  Angle `+0x5c += sin((age%800)/800·2π)·0.025`; pos = (cos,sin)(+0x5c)·200·scaleX (world origin).
  Phase 200: at `+0x80` 380/400/420 fires **beam 0x2c6** (from 700) / **700** along `+0x58`, SE (0xe,3);
  `+0x80 > 600` or owner KO → 900 (child too); 900 shrinks −0.02 → removed. Child 0x2ee: solid,
  r = 40·scale, attached (motion 3).
- **Beam 700/0x2c6**: speed 12, dmg 150, rect {24,8}, knock 10; age < 50 homes on the opponent with
  turn (50−age)·0.004·0.5; age < 30 speed ×0.95 else +0.25 to 20; removed outside arena+30 after 10.
- SumikaB hitbox table lacks 0xfa and 600/0x262 (no Pretty Justice / Eternal Feather); 200/0xc9 dmg 50.

## B5. Animation
Loader `FUN_00769460`, folder string 4 (`charaAdd`) for both variants. Ids: 0x235 std, 0x23a fwd, 0x23b
bck, 0x237 los, 0x239 sld, 0x236 dsh, 0x23c rng, 0x23e system, 0x24c acta, 0x24a ball, 0x24f/0x250
fthrr/b (64), 0x251/0x252 swordr/b, 0x254/0x255 arrowr/b (128), 0x253 = **mr_smkball** (12 frames),
0x257 barrel (192), 0x258 wing, 0x256 laser, 0x259 sab_tenta (10), 0x24e sab_bomb (3, S 420),
0x25a sab_shield (18, 128), whips 0xb/0xc sab_whipr/b.
T1 mapping (frame formulas as Sumika): 0x235 neutral (0x235/0x23a/0x23b), 0x236 dash, 0x237 KO,
0x238 hit, 0x239/0x23f/0x241/0x242/0x243/0x246/0x248/0x249 → 0x239 min(t/4,4); 0x240/0x244/0x245/
0x24c/0x24d/0x24e → 0x24c pp(4); 0x24a / 0x24b → ball formulas of 0x226 / 0x227.

## B6. Stats (0x8852f8)
5.0 / 14.0 / 12.0; ints 0,0xb,5,2,3,7 (+0x24..+0x2c unset); sprites 0x23c/0x23d/0x23e; two name
strings (id 4); win font ADD; sa_res_bgp; colour 250,200,100. HP from Player Init (§0.1).
AI: 0x881520 FUN_00772ae0 (lvl 0), 0x881524/28 FUN_007721f0.

---

## UNVERIFIED
- SumikaB selectability outside story mode; meaning of `DAT_0087faac == 3`.
- Whether Player Init (intro states 0x21d/0x241) runs every round.
- Graze zones owned by missile serials (hime.md §0.4).
- RESOLVED (draws, ported 1:1 in sumika.c; sprite ids from the disassembly immediates):
  - enemies: core 500 = sab_bomb 0..2 (half 210, sin wobble); satellites 700/0x2c6 = sab_shield
    ((age/3)%3)*3 (+9 for 0x2c6), half 64; tentacles 600/0x28a = sab_tenta (age/4)%3 (+6), glow +3 / 9.
  - missiles: swords 0x22d/0x22e (SB 0x251/0x252) frame (age/2)%6, half 48, colour (1,k,k,k), scale (3-2g, g);
    arrows 0x230/0x231 frame (age/2)%3+4, half 64, + sword frame 6 additive glow once grown (palette colours);
    balls beachball / mr_smkball odd frame + even frame additive pulse.
  - whips: per-segment strip sa_whipr/b (sab_), src rows k*12.8, rgba 0xffffff80.
  - lasers: sa_laser0 / sab_laser0 rows, beamsphere muzzle (see sumika.c laser_draw).
  - beams FUN_004b9050 / FUN_004b7930: 100 (age < 1000) beamsphere big cell k0 half 24*scale alpha 96*a blend 1
    + small cell half 10*scale alpha 255*a additive; 600 cell k2 half 40/15 * scale, both additive; 500/700 none.
  - effects FUN_00511e00 / FUN_0050f3e0 (identical but sprite ids): 300/500/0x1fe beamemitb glow (cell v/8, v%8
    of 240x64, half 60 x 40 (300: 80), alpha 32*a, blend 1) + core (half 40 x 8 (300: 16)), first 2 frames
    beamemit flash (48 x 15 / 32), later tinted (500 0xff80e1, 0x1fe 0x0080ff, 300 colour 0/1 0x8080ff/0xff8080);
    0x96/0xa0 explosion0/1 atlas (half 180/270 glow, 200/300 core x0.85 on even age, rotation un-negated);
    200..0xd7 sword frame id%10 (never spawned by Sumika's code); 700/750/0x2be/0x2f0 wing 0x234 (SB 600) frame
    0/1, half (scale/2, aux); effect 100 has no draw case.
- Stats int block meaning; AI.
