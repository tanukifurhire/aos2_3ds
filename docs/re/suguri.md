# Suguri — complete moveset spec (reference character)

Source: `aos2.exe` (Ghidra corpus `tools/re/dec/all.c`, constants read with `xref.exe rd`).
Every number cites its address. `t` = player state timer `p+0x154`. "UNVERIFIED" items say what
would settle them. Projectile/hitbox internals are described by their parameters; generic
collision/damage code is owned by objects.md / core.md.

---

## 0. Corrections to HANDBOOK.md (important for all 15 characters)

1. **Suguri's character index `p+0x10` is 10, not 7.** Every per-character table has 3 leading
   slots, so the real table bases are 12 bytes (3 entries) lower than the handbook says:
   - input dispatch is called as `(&DAT_00881310)[p+0x10]` (all.c:380548) → 0x881338 = index 10 = `FUN_0075a030`;
   - alternate-scheme dispatch `(&DAT_00881360)[p+0x10]` (all.c:380559) → 0x881388 = `FUN_0075a4b0`;
   - loader `(&DAT_008813b0)[p+0x10]` (all.c:284446) → 0x8813d8 = `FUN_00751e40`;
   - CPU-AI triple `0x881400 + idx*0x10 + level*4` (all.c:300706) → 0x8814a0..a8;
   - stats record `0x881f80 + 10*0x2dc` = **0x883c18** (record 7 at 0x883374 is Sham — it carries `win_font_SM`);
   - object families use `base + 4*10` (e.g. beam spawn 0x881878+0x28 = 0x8818a0).
   - The common T1 draw for state 0 (`FUN_006b07a0`) has `case 10:` → T1[0x128].
   So handbook "id" + 3 = engine index (Sora 3 … Sumika-2nd 18; 0x13 = 19 stats records).
2. `p+0x158` = **hit-confirm flag**: set to 1 by Suguri's hitbox on-hit callback (FUN_005ecfd0, variants 100, 0x6e, 200),
   cleared every frame by the main loop while `state < 7` (all.c:380785).
3. `p+0xe4` = hyper gauge (float, 1.0 = one level, max 3.0); `p+0xe8` / `p+0xec` = **ammo A / ammo B**
   (training menu strings `ANNOUNCE_PAUSE_INFINITEAMMOA/B`, refill code all.c:380724-380735).
4. `p+0x0c` = control scheme (0 = standard → 0x881310 table, 1 = alternate → 0x881360 table).
   `p+0x48` = Shield button. Button roles confirmed by Suguri's dispatch: `+0x34` Dash, `+0x38` A,
   `+0x3c` B, `+0x40` S(pecial), `+0x44` H(yper), `+0x48` Shield.

### Player fields used by Suguri's code

| Off | Name | Notes / source |
|---|---|---|
| +0x04 | owner handle | passed to every spawn |
| +0x0c | control scheme | 0 std / 1 alt (all.c:380547-380560) |
| +0x10 | char index (=10) | |
| +0x14 | side (1/2) | opponent = `FUN_00780480(side)` |
| +0x18 | player slot | sprite bank `0x885b80 + slot*0x3888` |
| +0x1c/+0x20/+0x24/+0x28 | dir x+ / x− / y+ / y− | `FUN_0061b230(p+0x1c)` → input angle (see §1.3) |
| +0x34..+0x48 | Dash, A, B, S, H, Shield | 3 = pressed this frame, ≥2 = held, `<1`/`<2` used as "released" (exact 0/1 meaning → core.md) |
| +0x6c,+0x70 | pos x,y | |
| +0x84 | aim angle (rad) | set by `FUN_007804b0` (angle to opponent) |
| +0x88 | copy of aim | |
| +0x8c | facing (byte) | 1 when opponent is to the right (all.c:380806) |
| +0x94 | move direction (rad) | |
| +0x98 | move speed (px/frame) | applied by main loop *after* the state handler: `pos += (cos,sin)(+0x94)*speed` (×0.25 if `+0xa7`), then `speed -= +0x9c` (to 0) (all.c:380590-380644) |
| +0x9c | friction | reset to 0.5 (DAT_00853b1c) every frame before dispatch (all.c:380545); several Suguri states multiply it by 0.5 → 0.25 |
| +0xe0 | heat | |
| +0xe4 | hyper gauge | |
| +0xe8/+0xec | ammo A / B | regen +0.01 (DAT_00813ec8) per frame in states 0/1/2 only, cap 1.0 |
| +0xf0 | hyper-gain multiplier | (core.md) |
| +0xf4/+0xf8 | hyper status kind / frames left | set by `FUN_006b1b40` (§1.4) |
| +0x150/+0x154 | state / timer | |
| +0x158 | hit-confirm | see above |

---

## 1. Shared mechanics used by every Suguri state

### 1.1 Frame model
- Each frame the main loop (FUN_006f1720) calls the dispatch fn, which ends by calling `T2[state](p)`.
  Afterwards it moves the player by `+0x94/+0x98`, decays speed and **then increments `t`**
  (all.c:380847). So the first T2 call of a state sees `t = 0`.
- Entering a state from dispatch: `state = id; t = 0; flag158 = 0; T2[id](p)` (same frame).
- **Internal switches 0x133→0x132 and 0x138→0x137 change only `state` (timer kept, flag kept)** and
  call the new T2 immediately (FUN_007571b0, FUN_00757e30).
- **State end**: handlers write `state = 0` *without resetting `t`* — the timer keeps counting in
  neutral (only matters for neutral's ping-pong animation phase, §4).
- "Dash cancel at N": `if t >= N && Dash == 3 → FUN_006b17d0(p, 1)` (cancel dash, heat +0.25
  DAT_00813f24, effect FUN_0065dff0(0,…); normal dash = +0.3 DAT_00813f28; refused if `p+0xa6==1`).
  Dash sets state 2, speed `+0x98 = stats+4` (15.0), direction = input angle (or facing) (FUN_006b17d0).
- `FUN_00612830(p,…)` / `FUN_00612900(p)` only record frame-data for the training-mode display
  (DAT_00885908 / DAT_00885914 …) — **no gameplay effect**. 612900 is called on the first dash-cancellable frame.

### 1.2 Ammo / hyper cost table (`FUN_007807f0(p, stateId)`)
`entry = 0x87c4f0 + stateId*0xc = {costA, costB, hyperGain}`; succeeds iff `ammoA >= costA && ammoB >= costB`,
then `ammoA -= costA; ammoB -= costB; hyper += hyperGain * p+0xf0`. On failure the shot is simply not
fired (state continues). Values are written at runtime by FUN_0049ae40 (registry.c:1803-1838):

| State | Move | costA | costB | hyperGain |
|---|---|---|---|---|
| 0x132 | Beam Rifle | 0.25 (DAT_00813f24) | 0 | 0.04 (DAT_00813eec) |
| 0x133 | Charged Rifle | 0 | 0 | 0.1 (DAT_00853b10) |
| 0x134 | Sword→Rifle | 0.4 (DAT_00813f30) | 0 | 0.1 |
| 0x135 | Rapid-fire (per shot) | 0.15 (DAT_00813f20) | 0 | 0.02 (DAT_00813ed8) |
| 0x137 | Homing Missile (per missile) | 0 | 0.075 (DAT_00813efc) | 0.02 |
| 0x138 | Sidewinder (per missile) | 0 | 0.1 (DAT_00853b10) | 0.015 (DAT_00813ed4) |
| 0x139 | Sword 1 | 0 | 0 | 0.05 (DAT_00813ef4) |
| 0x13a | Sword 2 | 0 | 0 | 0.025 (DAT_00813edc) |
| 0x13b | Sword 3 | 0 | 0 | 0.025 |
| 0x13c / 0x13d / 0x13e | Sword Arrow 1/2/3 | 0.25 | 0 | 0.025 |
| 0x140 | Sword Break | 0 | 0 | 0.05; **ver>0x5a0: 0.1** (FUN_004a7270, all.c:130696) |

(0x136, 0x13f, 0x141, 0x143 never call it — hypers give no gauge.)

### 1.3 Helpers
| Function | Behaviour |
|---|---|
| `FUN_007804b0(p)` | aim = `atan2(opp.y−y, opp.x−x)` (FUN_00474240). Special case: if opponent `+0x10==5` and opp `+0x164==1`, aim = input angle, or π / 0 toward the opponent's x side when no direction held. |
| `FUN_00780570(p)` | distance to opponent (sqrt via FUN_00458320). |
| `FUN_0061b230(p+0x1c)` | input angle: none → 4π (12.566, `_DAT_0082025c`, i.e. "> 2π = no input", tested against DAT_00853b78 = 2π); x+ → 0; x− → π; y+ → π/2, y+&x+ → π/4, y+&x− → 3π/4; y− → −π/2, y−&x+ → −π/4, y−&x− → −3π/4. |
| `FUN_005f0e70(&a, target, step)` | rotate angle `a` toward `target` by ≤ step (snap if closer). |
| `FUN_005f1210(&v, ang)` | rotate 2-D vector by ang. |
| `FUN_00467690` / `FUN_0045a680` | cos / sin. |
| `FUN_00459fd0(a,b)` | uniform float in [a,b] from MT19937 (one draw). `FUN_00459f80(lo,hi)` uniform int incl. (see objects/RNG notes in §3.6). |
| `FUN_007bc9c0(id)` / `FUN_007bcaf0(id,n)` | play SE id (2-frame / n-frame retrigger lockout). |

**Dash sparks (cosmetic but consume RNG):** several states do, each frame, `if speed(+0x98) >= K:
r = rand_f(-0.3, 0.3) (DAT_0081fa88, DAT_00813f28); n = rand_i(0,100) % 8; effect 0xa28+n
(DAT_00881698) at pos, angle (r + dir)`. K and dir per state are given below.

### 1.4 Hyper start (`FUN_006b1b40(p, kind)`) and hyper permission
- kind 10 (normal hyper): `+0xf4=3, +0xf8=100, +0xfc=160 (DAT_00814168), +0x100=+0x104=0, +0x108=50`.
- kind 0x14 (Accel hyper): `+0xf4=4, +0xf8=100, +0xfc=160`, same.
- While `+0xf8 > 0` the main loop counts it down and drives a hyper "aura" radius (core.md; all.c:380815-380845).
- `FUN_00780950(p)` (hyper allowed): false if state 6 or 1, or (own `+0xf4==3 && +0xf8 > 80`), or
  (own `+0xf4==4 && +0xf8 != 0`), or (opponent `+0xf4==4 && +0xf8 >= 1`). So a new hyper can be
  started 20 frames into your own normal hyper, never during either player's Accel.
- `FUN_00780910(p, cost)`: if `hyper >= cost` → `hyper -= cost`, true.
- Hyper flourish at t=0 of 0x136/0x13f/0x141: SE 6 (`up01.wav`), `FUN_004e9280(100, 0.75)`, `FUN_007bec20(100, pos, aim, π/4)`,
  `FUN_004d7ac0(20, 0.5, 2)`, effect `0xfa` (DAT_00881698). These three FUN_ calls are screen effects
  (background dim / cut-in / shake) — cosmetic, exact visuals UNVERIFIED.

---

## 2. Input dispatch `FUN_0075a030` (standard scheme) — exact decode

Evaluated every frame in every state (it is the per-player update entry).

**A. Hypers** — only if `FUN_00780950(p)` (§1.4). Checked in this order, first match wins:

| # | Condition | Cost (hyper gauge) | New state | Official move |
|---|---|---|---|---|
| 1 | S held (`+0x40>1`) && B held (`+0x3c>1`) && H pressed (`+0x44==3`) | 3.0 (DAT_00813a50) | 0x143 | **Accel Burst** (S+B+H) |
| 2 | S held && H pressed | 1.0 (DAT_00853b34) | 0x141 | **Hyper Sword** (S+H) |
| 3 | B held && H pressed | 1.0 | 0x13f | **Hyper Arrow** (B+H) |
| 4 | H pressed | 1.0 | 0x136 | **Hyper Rifle** (A+H — A is *not* checked; plain H also gives it) |

If the cost check fails, the next row is tried (e.g. S+B+H with 1–2 levels → Hyper Sword). Hypers can
interrupt **any** Suguri state (and dash/shield), since dispatch runs every frame.

**B. Normal moves** — only if `state == 0` (neutral) or `state == 2` (dash):

| # | Condition | Result | Move |
|---|---|---|---|
| 1 | Dash pressed (`+0x34==3`) | `FUN_006b17d0(p,0)` → state 2 | Dash |
| 2 | else Shield pressed (`+0x48==3`) && `FUN_00780910(p, 0.5)` (DAT_00853b1c) && `FUN_007809e0(p)` | `FUN_006b1930(p,0)` → state 3, SE 0xb | Shield (costs 0.5 hyper) |
| 3 | S held && B pressed | 0x140 | **Sword Break** (S+B) |
| 4 | S held && A pressed | dist ≥ 300 (DAT_008145f0) → 0x13c else 0x139 | **Sword Arrow** (long) / **Sword** (close) |
| 5 | B pressed | 0x138 (→0x137 if released early) | **Missile** Sidewinder / Homing |
| 6 | Dash held (`+0x34>0`) && A pressed | 0x135 | **Beam Rifle (Rapid-fire)** (D+A) |
| 7 | A pressed | 0x133 (→0x132 if released early) | **Beam Rifle** / **Charged Rifle** |

Notes: row 6 tests `+0x34 > 0` (any non-zero dash value), row 1 has priority when dash is pressed the same
frame. `FUN_007809e0` refuses shield while the opponent is in Accel (`+0xf4==4 && +0xf8>=1`), when
`DAT_00885964 == 2`, or (ver>0x5a0 && scheme 1 && H held). Finally the dispatch returns `T2[state](p) != 0`.

**Alternate scheme `FUN_0075a4b0`** (`p+0x0c == 1`, table 0x881360): identical except the hyper rows
are "H held (`+0x44>1`) and X pressed": 1. Shield-button pressed → 0x143 (cost 3); 2. S pressed → 0x141;
3. B pressed → 0x13f; 4. A pressed → 0x136 (cost 1 each). (diff of the two functions.)

Not in the manual: the hit-confirm cancels (§3: Sword 1→2→3, Sword 2→Rifle 0x134, Charged/0x134 →
Sword Break on B), Sword Arrow 1→2→3 chain on A, and the short dash on entering Sword Arrow 2/3.

---

## 3. States

`ver` = DAT_0089426c (ruleset version). The current build's own version constant is 0x5a2
(DAT_00881e68 = 0x5a2, all.c:153513); **UNVERIFIED** that 0x89426c holds that value in normal play
(no direct writer found by grep — settle by watching 0x89426c at runtime). Where behaviour differs,
both are given; **implement the ver ≥ 0x5a1 column**.

Common hyper-free patterns:
- "aim" = `+0x84 = FUN_007804b0(p)`.
- Spawn calls: beam `DAT_008818a0 = FUN_004b52a0(variant, &pos, angle, p+4, 0, side)`; missile
  `DAT_008819e0 = FUN_0067c2b0(variant, &pos, angle, p+4, 0, side, slot)`; hitbox ("sword")
  `DAT_00881bc0 = FUN_005eca70(variant, &pos, angle, p+4, 0, side)`; laser `DAT_00881c60 = FUN_006289c0(variant, &pos, angle, p+4, side, slot)`;
  accel unit `DAT_00881580 = FUN_00550660(100, p+4, side, slot)`; Suguri effect `DAT_008816c0 = FUN_0050c820`;
  generic effect `DAT_00881698 = FUN_004ef2c0(id, layer, &pos, angle, owner, 0, side)`.
  Parameters of every variant: §5.

### 0x128 — neutral movement (T2 FUN_00756f80, called by common state 0's T2 FUN_006af390 `case 10`)
Each frame: y+ held (`+0x24>1`) → y += speed; y− → y −= speed; x− → x −= speed; x+ → x += speed,
`speed = stats+0 = 5.0` (DAT_00813f40 → 0x883c18). Diagonals are **not normalised** (5 on each axis).
`+0x84 = 0` if facing==1 else π (DAT_00853b58). Returns 1.

### 0x129, 0x12a, 0x12b, 0x12c — stubs (T2 FUN_00757070 / 007570a0 / 00757080 / 00757090 return 1)
Only their T1 draw handlers are used, by common states 2, 6, 1/5, 3/4 (§4). 0x12d–0x131 and 0x142 have no handlers (unused ids).

### 0x133 — Beam Rifle press / Charged Rifle (T2 FUN_007571b0) — entered on A press
| t | Action |
|---|---|
| 0 | aim |
| 0–10 | if A not held (`+0x38 < 1`) → state = 0x132 (timer kept) and run 0x132 now |
| 13–39 | if Dash not held (`+0x34<2`): free movement exactly as 0x128 (5.0/axis); re-aim every frame |
| 15–24 | if A released (`+0x38 < 2`): if `FUN_007807f0(p,0x132)` → beam **100** at pos, aim + SE 0xd (`_sen_mi_b_raifuru.wav`); **t := 100** (even if out of ammo) |
| 29 | effect `FUN_0065dff0(2, pos, owner)` (charge-ready flash) + SE 0x2a (`A5_01029.wav`) |
| 25–39, t%3==0 | effect 0x514 (charge particles) at pos, angle 0 |
| 30–39 | if A released && `FUN_007807f0(p,0x133)` (always succeeds, +0.1 hyper): beam **200** at pos, aim; effects 0x456 at aim±π/2 (DAT_00814104) and 0x515 at aim (all with texture `DAT_008859f4+side*0x10`); SE 0xe (`tm2_gun000.wav`); **t := 100** |
| 40 | t := 30 (hold loop: charged state can be held forever, shot fires on release) |
| ≥100 | recovery. B pressed && flag158 → FUN_00612830, state **0x140** (t=0, flag=0). |
| 115 | first dash-cancel frame (t ≥ 115 && Dash==3 → cancel) |
| >150 | state = 0 |

(Release during 11–14 fires the normal beam at t=15; release at 25–29 fires the charged shot at t=30.)
Order inside a frame: release-fire checks (15–24) run before the t==29 / 30–39 / loop checks; if the
normal beam fires, t becomes 100 and the later checks see 100.

### 0x132 — Beam Rifle quick shot (T2 FUN_007570b0) — via 0x133 early release
| t | Action |
|---|---|
| 12 | if `FUN_007807f0(p,0x132)` (A −0.25): re-aim, beam **100** at pos, aim; SE 0xd |
| ≥20 | dash-cancel (612900 at t==20) |
| >30 | state 0 |
No movement (speed decays normally).

### 0x134 — Sword combo rifle finisher (T2 FUN_007576f0) — from 0x13a (A without S, after hit-confirm)
| t | Action |
|---|---|
| 0 | aim |
| 5 | if `FUN_007807f0(p,0x134)` (A −0.4): re-aim, beam **200** (charged) at pos, aim; SE 0xe |
| any | B pressed && flag158 → state **0x140** |
| ≥30 | dash-cancel |
| >40 | state 0 |

### 0x135 — Beam Rifle (Rapid-fire) (T2 FUN_00757860) — Dash held + A
| t | Action |
|---|---|
| 0 | aim |
| 5, 12, 19 | re-aim; if `FUN_007807f0(p,0x135)` (A −0.15): beam **0x96** at pos, angle `aim + rand_f(−0.1, 0.1)` (DAT_00814130, DAT_00853b10); SE 0xd |
| every frame | if a direction is held: move dir `+0x94` rotates toward input angle by ≤ 0.08 rad (DAT_00813f00) (keeps the dash momentum steerable) |
| every frame | friction `+0x9c *= 0.5` (→ 0.25/frame decel of the inherited dash speed) |
| ≥35 | dash-cancel |
| >45 | state 0 |
3 shots max; each needs 0.15 ammo A.

### 0x138 — Missile (Sidewinder) / press (T2 FUN_00757e30) — B press
| t | Action |
|---|---|
| 0 | aim |
| 0–10 | if B not held (`+0x3c<1`) → state 0x137 (timer kept), run it |
| 12..30, t%3==0 (12,15,18,21,24,27,30 → up to **7 missiles**) | if `FUN_007807f0(p,0x138)` (B −0.1): offset `(rand_f(−60,60), rand_f(−120,120))` (DAT_008149e8/DAT_00813a58, DAT_0081525c/DAT_00814174) rotated by aim, added to pos → missile **200** at that point, angle aim; effect `0x8816c0(100, 0, &pt, rand_f(−π,π))` (launch flash); SE `FUN_007bcaf0(0x22,3)` (`burst00.wav`) |
| every frame | friction ×0.5 |
| ≥35 | dash-cancel |
| >45 | state 0 |
Aim is fixed at t=0 (no re-aim).

### 0x137 — Missile (Homing) (T2 FUN_00757c00) — via 0x138 early release
| t | Action |
|---|---|
| 0 | aim (if entered fresh; normally already set) |
| 15,18,21,24 (14<t<25, t%3==0 → **4 missiles**) | if `FUN_007807f0(p,0x137)` (B −0.075): re-aim; `r = rand_f(0.6, 1.2)` (DAT_00813f34, DAT_00817d78); `sign = ((t/3)%2)*2 − 1` (t=15:+1, 18:−1, 21:+1, 24:−1); angle = aim + sign*r; **ver>0x5a0: angle = aim + sign*0.9** (double `_DAT_00826138`; the rand is still drawn); missile **100** at pos; SE `FUN_007bcaf0(0x22,3)` |
| every frame | friction ×0.5 |
| ≥30 | dash-cancel |
| >45 | state 0 |

### 0x139 — Sword (close range) (T2 FUN_00758790) — S+A, distance < 300
| t | Action |
|---|---|
| 0 | aim; lunge: `+0x94 = +0x88 = aim`, `+0x98 = min(dist/14, 14)` (DAT_00813a54) |
| every frame | sparks if speed ≥ 3.0 (DAT_00813a50), dir = aim |
| 5 | SE 0x1f (`swing.wav`) |
| 10 | if `FUN_007807f0(p,0x139)`: hitbox **100** at `pos + (cos,sin)(aim)*30` (DAT_008140e8), angle aim |
| ≥10 | flag158 && S held && A pressed → FUN_00612830, state **0x13a** |
| ≥30 | dash-cancel |
| >40 | state 0 |

### 0x13a — Sword 2nd slash (T2 FUN_00758ac0)
| t | Action |
|---|---|
| 0 | aim; lunge `+0x98 = min(dist/15, 10)` (DAT_00853b88, DAT_00813f4c), dir aim |
| 5 | SE 0x1f |
| 10 | if cost ok: hitbox **0x6e** at pos + aim*30, angle aim |
| ≥10 | flag158 && S held && A pressed → **0x13b**; else flag158 && A pressed → **0x134** (rifle finisher) |
| ≥30 | dash-cancel; >40 state 0 |
(No spark effect in this state.)

### 0x13b — Sword 3rd slash (T2 FUN_00758db0)
| t | Action |
|---|---|
| 0 | aim; lunge `min(dist/15, 10)` |
| 8 | if cost ok: hitbox **0x78** at **pos (no offset)**, angle aim (no SE in T2; hit SE from callback) |
| ≥30 | dash-cancel; >40 state 0 |

### 0x13c — Sword Arrow 1 (T2 FUN_007580d0) — S+A, distance ≥ 300
| t | ver ≤ 0x5a0 | ver ≥ 0x5a1 |
|---|---|---|
| fire frame | 8 | **10** |
| chain window (A pressed && `FUN_007807f0(p,0x13d)`) → 0x13d | 12–41 | **16–26** |
| dash-cancel from | 30 | **32** |
| end (state 0) | >40 | >40 |
t=0 aim. Fire: if `FUN_007807f0(p,0x13c)` (A −0.25): beam **300** at pos, aim; SE 0x1f.
Note the chain's ammo is paid at transition time; 0x13d/0x13e fire without another check.

### 0x13d — Sword Arrow 2 (T2 FUN_007582c0)
| t | Action |
|---|---|
| 0 | if a direction is held: `+0x94 = input angle`, `+0x98 = 16` (DAT_008140d8), heat += 0.3 (DAT_00813f28), SE 8 (`air01.wav`) — a short dash; then aim, `+0x88 = aim` |
| every frame | sparks if speed ≥ 3.0, dir = `+0x94` |
| 8 | beam **300** at pos, aim; SE 0x1f (no cost check) |
| chain: t in [12,41] (ver≥0x5a1: **[14,24]**) && A pressed && `FUN_007807f0(p,0x13e)` | → **0x13e** |
| ≥25 | dash-cancel; >40 state 0 |

### 0x13e — Sword Arrow 3 (T2 FUN_00758580)
Same entry dash as 0x13d; sparks; **t=6** beam **300** (no cost check), SE 0x1f; dash-cancel ≥25; >40 state 0.

### 0x140 — Sword Break (T2 FUN_00758f20) — S+B, or B after hit-confirm in 0x133/0x134
| t | Action |
|---|---|
| 0 | aim; `+0x88 = +0x94 = aim`; speed 0 |
| 10 | ver≤0x5a0: re-aim, dir = aim, speed 20 (DAT_008140fc), heat +0.3, SE 0x20 (`swing_h.wav`). ver≥0x5a1: if `FUN_007807f0(p,0x140)` (no cost, +0.1 hyper): same but **no heat cost** |
| 10–22 | if dist < 80 (DAT_00814164): speed := 4 (DAT_00853b68) (stops in front of the target) |
| 12,15,18,21,24 (11<t<25, t%3==0 → **5 hits**) | hitbox **0x82** at pos + aim*30, angle aim (no cost check). On t==24 the hitbox's knockback (`+0x40`) is set to 15.0 (DAT_00853b88) via `FUN_004b2eb0(idx)` |
| every frame | sparks if speed ≥ 6.0 (DAT_00813f44), dir = aim; friction ×0.5 (→0.25 decel) |
| ≥40 | dash-cancel; >50 state 0 |

### 0x136 — Hyper Rifle (T2 FUN_00757a50) — H (or A+H), 1 level
| t | Action |
|---|---|
| 0 | aim; `FUN_006b1b40(p,10)`; hyper flourish (§1.4) |
| 10 | re-aim; speed 0; laser **100** at pos, aim |
| ≥30 | dash-cancel (laser keeps going, but deals half damage once Suguri leaves 0x136 — §5.3) |
| >80 | state 0 |

### 0x13f — Hyper Arrow (T2 FUN_007596d0) — B+H, 1 level
t=0: aim (+0x88, +0x94), speed 0, `FUN_006b1b40(p,10)`, flourish. t=10: beam **0x15e** at pos, aim (aim from t=0), SE 0x20.
Dash-cancel ≥50; >70 state 0.

### 0x141 — Hyper Sword (T2 FUN_00759360) — S+H, 1 level
| t | Action |
|---|---|
| 0 | aim, speed 0, `FUN_006b1b40(p,10)`, flourish |
| 10 | re-aim; dir = aim; speed 20 (DAT_008140fc); SE 0x20 |
| every frame | sparks if speed ≥ 5.0 (DAT_00813f40), dir = `+0x94` (decel 0.5/frame, no extra friction) |
| 15 | hitbox **0x96** at pos + aim*30, angle = normalize(aim + π) (FUN_005f0c60) — knockback is directed back along Suguri's path |
| ≥50 | dash-cancel; >70 state 0 |

### 0x143 — Accel Burst (T2 FUN_007598a0) — S+B+H, 3 levels
| t | Action |
|---|---|
| 0 | aim; speed 0; `FUN_006b1b40(p,0x14)`; SE 7 (`high_a.wav`); `FUN_004e9280(150, 0.85)`; `FUN_007bec20(100,pos,aim,0)`; `FUN_004d7ac0(150, 0.65, 2)`; effect 0xfa |
| 10 | re-aim, speed 0. Spawn 4 accel units, offsets `off` = −1.2 (DAT_00824e14), −0.6 (DAT_00826140), 0.6 (DAT_00813f34), 1.2 (DAT_00817d78): unit pos = pos + rotate((cos(off)*50, sin(off)*100), aim) (DAT_008140ec, DAT_00853b90); unit `+0x70 = off`; `+0x5c = +0x58 = aim + off*0.5`. Then laser **200** at pos, aim |
| ≥80 | dash-cancel; >100 state 0 |

---

## 4. Animation

### 4.1 Sprite registration (loader `FUN_00751e40`)
Arg = colour variant 0/1/2 (folder string ids 0x26/0x27/0x28 = charaSuguriA/B/C; in a Suguri mirror with
variant 2 it may fall back to 0x27 — UNVERIFIED). Frame n of a group = `printf(pattern, n)`. Sprites are
drawn as a S×S quad (`FUN_00467d00(S,S,2,2,1.0,0)`) with UV 0..1 — the PNG is stretched to S.
Storage: `0x885b80 + slot*0x3888 + id*0x18`.

| Sprite id | Pattern | Frames | S | Used by |
|---|---|---|---|---|
| 0x128 | `sg_std0%d` | 3 | 96 (DAT_00814118) | neutral |
| 0x129 | `sg_dsh0%d` | 9 | 96 | dash |
| 0x12a | `sg_los0%d` | 6 | 96 | T1 0x12a, 0x12b |
| 0x12c | `sg_sld0%d` | 6 | 96 | T1 0x12c, 0x137, 0x138 |
| 0x12d | `sg_fwd0%d` | 3 | 96 | neutral, moving toward |
| 0x12e | `sg_bck0%d` | 3 | 96 | neutral, moving away |
| 0x12f | `sg_rng0%d` | 2 | 512 (DAT_008178b0) | stats +0x3c (HUD/ring?, UNVERIFIED) |
| 0x131 | `sg_system` | 1 | 512 | stats +0x44 (UI?, UNVERIFIED) |
| 0x132 | `sg_rfl0%d` | 6 | 96 | T1 0x132–0x136, 0x143 |
| 0x139 | `sg_swda0%d` | 6 | 96 | 0x139, 0x13c, 0x13f body |
| 0x13a | `sg_swdb0%d` | 5 | 96 | 0x13a, 0x13d body |
| 0x13b | `sg_swdc0%d` | 6 | 96 | 0x13b, 0x13e, 0x141 body |
| 0x140 | `sg_swdd0%d` | 6 | 96 | 0x140 body |
| 0x141 | `sg_slash` | 1 | 96 | effect (UNVERIFIED) |
| 0x144 | `sg_bfire` / `sg_bfireb` | 2 | 48 (DAT_008140e4) | missile exhaust |
| 0x145 | `sg_msl0%d` | 5 | 64 (DAT_008140f8) | missile body/glow |
| 0x146 | `sg_artl0%d` | 3 | 64 | object (UNVERIFIED which) |
| 0x147 | `sg_moon0%d` | 2 | 64 | Sword/Hyper Arrow beam, effect 200/0xd2 |
| 0x148 | `sg_blade0%d` | 4 | 64 | effects 100 / 0x6e |
| 0x149 | `sg_lsr0%d` | 2 | 256 (DAT_00814160) | laser (UNVERIFIED) |
| 0x14a | `sg_rflc0%d` | 4 | 256 | (UNVERIFIED; accel unit?) |
| 0x14b–0x14e | `sg_swdas/bs/cs/ds0%d` | 2 each | 192 (DAT_0081415c) | slash layers |
| 0x14f | `sg_swdab0%d` | 2 | **96** | blade layer (swda) |
| 0x150–0x152 | `sg_swdbb/cb/db0%d` | 2 each | 192 | blade layers |
All 101 `sg_*.png` in `extracted/texture/charaSuguriA/` are covered. ids 0x12b, 0x130, 0x133–0x138 are not registered.
`FUN_00756600` builds the pause-menu command list (text ids 0x2b7–0x2c2) — no gameplay.

### 4.2 Draw transforms
- **AIM transform** (all states except 0x128): position = pos; if `|aim| >= π/2`: rotZ(aim), rotX(180°), rotY(180°), else rotZ(aim), rotY(180°). Art faces −x; result points along aim and stays upright. Angles in 16-bit units (`rad*65536/2π`). Exact in 2D regardless of composition order: RotX(π)·RotY(π) = RotZ(π) and Z rotations commute, so `|aim| >= π/2` is an unmirrored RotZ(aim+π); otherwise mirror (art faces −x) then RotZ(aim).
- **Neutral** (0x128): no rotZ; rotY(180°) if facing != 0.
- Every body/blade draw is preceded by the global tint `FUN_004e8f50`; blend = normal alpha (`FUN_004637d0(1)`) everywhere.
- Heat afterimage `FUN_004d7e00` queued only in 0x128, 0x129, 0x140, 0x141 (core.md for ghost rules).

### 4.3 Common states → Suguri draw handlers
| Common state | Suguri T1 |
|---|---|
| 0 neutral | 0x128 FUN_0075bc90 |
| 1 (hit) | 0x12b FUN_0075c360 |
| 2 dash | 0x129 FUN_0075bf40 |
| 3 shield / 4 | 0x12c FUN_0075c600 |
| 5 | 0x12b |
| 6 | 0x12a FUN_0075c8c0 |

### 4.4 Per-state frames (`pp(n)` = `(t/n)&3` with 3→1, i.e. 0,1,2,1)
| State | T1 | Sprite | Frame |
|---|---|---|---|
| 0x128 | FUN_0075bc90 | 0x128; overridden (later check wins): y+→0x12d; y−→0x12e; facing0&x−→0x12d; facing1&x−→0x12e; facing0&x+→0x12e; facing1&x+→0x12d | pp(5) |
| 0x129 | FUN_0075bf40 | 0x129 | `(t/2)%6`; if moving against facing (facing1 && \|aim\|>π/2, or facing0 && \|aim\|<π/2): pp(3)+6 |
| 0x12a | FUN_0075c8c0 | 0x12a | pp(3) + (3 if global frame `DAT_008858f8` even) |
| 0x12b | FUN_0075c360 | 0x12a | 0 |
| 0x12c, 0x137, 0x138 | FUN_0075c600 / 0075da70 / 0075dd30 | 0x12c | min(t/4, 5) |
| 0x132 | FUN_0075cbc0 | 0x132 | t<15 ? 0 : min((t−15)/3+1, 4) |
| 0x133 | FUN_0075cea0 | 0x132 | t<100 ? 0 : min((t−100)/3+1, 4) |
| 0x134 | FUN_0075d180 | 0x132 | t<5 ? 0 : min((t−5)/3+1, 4) |
| 0x135 | FUN_0075d460 | 0x132 | t<5: 0; 5≤t<12: (t−5)/2 (0 if >2); 12≤t<19: (t−12)/2 (0 if >2); t≥19: (t−19)/2; cap 4 |
| 0x136 | FUN_0075d790 | 0x132 | t<60 ? 0 : min((t−15)/3+1, 4) (jumps 0→4 at t=60 — reproduce) |
| 0x143 | FUN_00764650 | 0x132 | t<80 ? 0 : min((t−80)/4+1, 4) |

Sword family: draw order back blade → body → slash layer → front blade. `f` = body frame, `b` = (f==0 ? 0 : 1).
| State | Body/blade/slash ids | f | Back blade | Front blade | Slash frame |
|---|---|---|---|---|---|
| 0x139 | 0x139/0x14f/0x14b | t<6 ? 0 : min((t−6)/3+1, 5) | frame 1 if b | frame 1 if b (drawn twice) | f1,2→0; f3,4→1 |
| 0x13a | 0x13a/0x150/0x14c | t<6 ? 0 : min((t−6)/3+1, 4) | — | always, frame b | f1→0; f2→1 |
| 0x13b | 0x13b/0x151/0x14d | as 0x139 | frame 0 if !b | frame 1 if b | f1,2→0; f3,4→1 |
| 0x13c | 0x139/0x14f/0x14b | as 0x139 | if b | if b | f1→0; f2→1 |
| 0x13d | 0x13a/0x150/0x14c | as 0x13a | — | always, frame b | f1→0; f2→1 |
| 0x13e | 0x13b/0x151/0x14d | as 0x139 | if !b | if b | f1,2→0; f3,4→1 |
| 0x13f | 0x139/0x14f/0x14b | as 0x139 | if b | if b | f1→0; f2→1 |
| 0x140 | 0x140/0x152/0x14e | t<10:0; <13:1; <16:2; else ((t−16)/3)%3+3 | if !b | if b | f1→0; f2→1 (+ghost) |
| 0x141 | 0x13b/0x151/0x14d | t<12 ? 0 : min((t−12)/3+1, 5) | if !b | if b | f1,2→0; f3,4→1 (+ghost) |
(Slash layer is not tinted. Layer ids confirmed from immediates, e.g. FUN_0075dff0 `imul …,0x14f` @0x75e0ad, `mov …,0x14b` @0x75e644.)

---

## 5. Spawned objects (parameters)

### 5.1 Beam family (class 10): spawn FUN_004b52a0, update FUN_004b67a0, draw FUN_004b56e0, hit-callback FUN_004b6570
Record 0x98 bytes; common init FUN_004ba490. Fields: +0x00 class 10, +0x04 variant, +0x08 age (update sees 0 first, incremented after), +0x0c owner, +0x10 hitType (1), +0x14 side, +0x24..2c pos, +0x55 destroy-on-hit (1), +0x58 scale (1.0), +0x5c alpha (1.0), +0x60 move angle, +0x64 draw/collision angle, +0x68 speed, +0x70 collision enabled (1), +0x74/78/7c shape {halfX, halfY, radius}, +0x84 damage, +0x88 knockback, +0x8c stagger (1.0).

Beams hit only targets on the other side with `+0xa2 != 0`; **a dashing target (state 2, or state 0 with speed>0) is passed through**; a target with `+0xf8 > 0` destroys the beam but takes no damage. Damage call: `FUN_00780ab0(target, dmg, hitType, stagger)` (heat-scaled for hitType 1/3; returns true when stagger accum ≥ 1.0) → then `FUN_006b19a0(target, angle, knockback)` (hit state 1). Arena = circle radius 540 (DAT_0081412c), `FUN_0061ac10(pos, margin)`.

| Variant | Used by | speed | shape {hx,hy,r} | dmg | knock | stagger | collides | lifetime / behaviour |
|---|---|---|---|---|---|---|---|---|
| 100 Beam Rifle | 0x132, 0x133 | ver<0x58a 16 (DAT_008140d8); else **21** (DAT_00814154) | {24, 8, 0} (DAT_00853b8c, DAT_008140d4) | ver<0x58a 180; else **216** | 10 (DAT_00813f4c) | 1.0 | yes | straight until it leaves arena+10 or hits |
| 0x96 Rapid-fire | 0x135 | ver<0x58a 15; else **20** | {24, 8, 0} | ver<0x58a 120; else **144** | 10 | **0.7** (DAT_00813f38) | yes | as 100 |
| 200 Charged Rifle | 0x133, 0x134 | 17 (`_DAT_00813f5c`) | {0, 0, 30} (DAT_008140e8) | 120 | 10; ver>0x5a0 **5** | 1.0 | yes | leaves arena+(ver>0x58a ? 30 : 10) → die. On hit: spawns beam **0xd2** at its pos/angle, SE 0x13 |
| 0xd2 charge burst | 200's hit callback | 7.5 (DAT_00814150) (×0.1 once outside arena+5, ver≥0x58b) | none | 0 | 0 | — | no | ages 0,4,8,12,16: hitbox **200** at pos (**5 hits**); dies after age 20 |
| 300 Sword Arrow | 0x13c–0x13e | 16 | none | 0 | — | — | no | age>10: speed×0.95/frame (`_DAT_00814124`); age>20: alpha −0.05/frame, dies at alpha≤0 (~age 40, float accumulation); ages 6,9,12,15,18: hitbox **300** at pos (**5 hits**) |
| 0x15e Hyper Arrow | 0x13f | 16 | none | 0 | — | — | no | age>30: speed×0.95; age>40: alpha −0.05 (~age 60); ages 3,6,…,39: hitbox **0x136** (**13 hits**) |

Visuals: 100/0x96 draw nothing themselves; their visible bolt is effect `0x8816c0(300 / 0x136)` spawned every frame (+ muzzle 0x547/0x548 at age 1, particles FUN_0068b8a0 ages<10). 200/0xd2: 2D sprite **0x14a** (`sg_rflc`, bank `DAT_00885a00[team]`; `imul edx,ecx,0x14a` @0x4b5d74) frame 0 alpha 128·a blend 2, then frame 3 alpha 255·a blend 1; src (0,0)-(127,127), half-size (60,28)/zoom. 300/0x15e: sprite **0x147** (`sg_moon`) half-size 192 (300) / (192,256) (0x15e) ×scale/zoom, alpha 255·a. 300 and 0x15e also spawn particle 0x7da each (even/every) frame at a random offset (consumes RNG: angle rand_f(−π/2, π/2), offset (cos·80−20, sin·100 or ·160) rotated by angle).
Hit callback visuals: 100 → effect 1000 + particles, SE 0x13; 0x96 → effect 0x3f2, SE 0x13.

### 5.2 Hitbox ("sword") family: spawn FUN_005eca70, on-hit FUN_005ecfd0
One-frame hitboxes (record 0x4c, list 0x895184, cleared every frame by FUN_005e0150) — no update/draw of their own; all sword visuals come from the player draw (§4). Fields: +0x00 deals-damage (1), +0x04 class 10, +0x08 variant, +0x10 owner, +0x14 hitType, +0x18 side, +0x1c pos, +0x28 angle (rect rotation and knockback direction), +0x2c/30/34 shape, +0x3c damage, +0x40 knockback, +0x44 stagger 1.0. Collision loop FUN_0060f5a0: skips own side, `+0xa2==0`, `+0xf8>=1`; **hitType 1 misses dashing targets, hitType 3 hits them**. Callback runs before damage. Each hitbox hits each opponent at most once.

| Variant | Spawned by | hitType | dmg | shape {hx,hy,r} | knock | on-hit (SE / extra) |
|---|---|---|---|---|---|---|
| 100 | Sword 1 (0x139) | 3 | 180 | {100, 80, 0} (DAT_00853b90, DAT_00814164) | 5 | SE 0x16 (lock 3); **sets hit-confirm** |
| 0x6e | Sword 2 (0x13a) | 3 | 60 | {100, 80, 0} | 5 | SE 0x16; **sets hit-confirm** |
| 0x78 | Sword 3 (0x13b) | 3 | 120 | {100, 80, 0} | 22.5 (DAT_0081fa84) | SE 0x17 (`hit_f.wav`); FUN_004d7ac0(10, 0.75, 1) shake |
| 0x82 | Sword Break ×5 | 3 | 90 | {80, 40, 0} | 5 (last hit 15) | SE 0x16 |
| 0x96 | Hyper Sword | 3 | 720 | {120, 60, 0} | 15 | SE 0x17; shake |
| 200 | charge burst ×5 | **1** | 70; ver>0x5a0 **56** | {0, 0, 40} (DAT_008140e0) | 10; ver>0x5a0 **5** | SE 0x13; **sets hit-confirm** |
| 300 | Sword Arrow ×5 | 3 | 80 | {0, 0, 100} | 5 | SE 0x13 (lock 5) |
| 0x136 | Hyper Arrow ×13 | 3 | 120 | {60, 120, 0} | 10 | SE 0x13 (lock 5) |
| 400 | Accel Burst laser wall blast | 3 | 70 (0 when laser t%4==0) | {0,0,r}, r = (t+5)*12*0.35 set by laser | 2.5 (DAT_008153e0) | SE 0x13 (lock 5) |
| 0x1c2 | Accel-unit beam ×12 | 3 | 50 | {800, 40, 0} (DAT_008149c8) centred 800 px along beam | 5 | SE 0x13 (lock 4) |
Callback effects (cosmetic, consume RNG in the order written in FUN_005ecfd0): 0x9c4/0x9c5 slash sparks, 0xaf0, 0x547, 0x456 streak fans, 0x516 burst.

### 5.3 Missile family: spawn FUN_0067c2b0, update FUN_0067d2a0, draw FUN_0067c440, expire FUN_0067d040, shot-down FUN_0067d170
Record 0xb8. +0x08 timer (update sees 0 first), +0x10 hitType **2** (not heat-scaled), +0x5c grow-scale (starts 0, +0.2/frame to 1.0 — DAT_00853b14), +0x68 heading, +0x6c draw angle, +0x70 speed, +0x78 missile HP 60 (vs enemy bullets/hitboxes), +0x80 shape vs player {0,0,r}, +0x90 shape vs bullets (same r), +0xa0 damage, +0xa4 knockback 10 (DAT_00813f4c), +0xa8 stagger 1.0.

| | Homing (100) | Sidewinder (200) |
|---|---|---|
| initial speed | 14 (DAT_00813a54) | **−4** (DAT_00824aa0, flies backwards first) |
| radius | 8 (DAT_008140d4) | 12 (DAT_00813f54) |
| damage | 180 | 250 |
| t < 50 | if speed > 2 (DAT_00813a4c): speed −= 0.5; turn toward opponent ≤ 0.02 rad/frame (DAT_00813ed8) | every 10 frames: w = min(t/50, 1); heading ∓= rand_f(0, w) (− when (t/10) even); speed += 0.5 until 12 (DAT_00813f54) |
| 50 ≤ t < 100 | speed += 0.5 until 10; turn ≤ 0.015 rad/frame (DAT_00813ed4) | same wobble/accel |
| t ≥ 100 | straight | same |
| dies | t > 50 and outside arena+10 → explode | same |
Move: `pos += (cos,sin)(heading)*speed` (after steering). Cosmetic: smoke 2000 every even t (random angle), thruster sparks (homing t<6 even; sidewinder t<10, t%3==0) — consume RNG.
Collision vs player (FUN_00610b80): **not dodged by dash**; if target `+0xf8==0` → damage, stagger/knockback along heading; missile always destroyed. Vs enemy bullets/hitboxes: HP −= their damage, destroyed when HP < 0. Expire/destroy: effects 0x262 + 0x514, SE 0x14 (`bom06.wav`).
Draw: sprite 0x145 (`sg_msl`) body frame (t/3)%3, glow frames 3/4 pulsing; exhaust 0x144 (`sg_bfire`) 42 px behind (DAT_00824488); body scale (3−2s, s).

### 5.4 Laser family: spawn FUN_006289c0, update FUN_0062a540, draw FUN_00628c00, on-hit FUN_0062a2c0
Fields: +0x0c hitType 3 (dash does not dodge), +0x10 side, +0x18 owner, +0x20 timer (pre-incremented → **first update sees t=1**), +0x24 origin (fixed), +0x30 angle (fixed for 100/200), +0x40 damage this frame, +0x44 knockback (5.0 for 100, 10.0 for 200/0xd2), +0x4c stagger 1. Collision FUN_006100b0: segment circles; on contact with `+0xf8==0`: **always** `FUN_006b19a0(target, angle, knock)` then `FUN_00780ab0(target, +0x40, 3, 1.0)`.

| Variant | Behaviour |
|---|---|
| 100 Hyper Rifle | SE 0x12 (`gun11_r.wav`) at t=16; removed after t=99. 29 circles at origin + dir·k·60 (k=1..29; DAT_00813a58), radius 50 (DAT_008140ec) for 16 ≤ t ≤ 60, else 0. Damage: on t%4==0 → **60** if owner still in state 0x136, else **30**; other frames 0 (contact still knocks back). While owner in 0x136 and t%3==0: muzzle effects 0x547/0x548 at origin+dir·40. |
| 200 Accel Burst beam | SE 0x12 at t=16, SE 0x1b (`explosion1.wav`) at t=80; removed after 99. Same 29×60 segments radius 50 (16..60) **but only up to the first segment outside the arena** (k<10 always kept; else needs inside arena+30). At that wall point on even t>3: wall-blast hitbox **400** (§5.2) + sparks 0xa82+rand%8; t==80: effect `0x8816c0(600)`. **+0x40 always 0** — the beam itself only pushes (knock 10) and staggers; damage comes from wall blasts and the units. Owner in 0x143 && t<80 && t%3==0: 5 particles `0x8816c0(500)`. |
| 0xd2 accel-unit beam | angle follows its unit (`unit+0x58`); removed after t=89; t=5,10,…,60: hitbox **0x1c2** at origin + dir·800 (**12 hits**); muzzle 0x546 when t%5==2, t<61. No segment hitboxes. |
Draw: textured strip, length = segCount·60, glow height 160/120 alternating, ×0.25 while t<15, fade (100−t)/20 after t>80 (texture file UNVERIFIED, probably `sg_lsr`).

### 5.5 Accel units: spawn FUN_00550660, update FUN_005507d0, draw FUN_00550aa0
Not hittable (`+0x2c=0`). Caller sets pos/angle/offset (§3 0x143). Phase 100 (open): scaleX −0.2, scaleY +0.1 per frame; when scaleY ≥ 1 (10 frames): phase 200, timer 0, spawn laser **0xd2** at its pos/angle, SE 0x12 (lock 5). Phase 200: frozen while t ≤ 50; t>50: `off *= 0.975` (DAT_00814128) per frame and reposition around the owner with the owner's current aim (beams converge toward the aim); t>80 → phase 900. Phase 900: scaleX +0.2, scaleY −0.1; removed at scaleY ≤ 0. Initial scale (3.0, 0) (DAT_00813a50).

### 5.6 Suguri effect family `0x8816c0 = FUN_0050c820` (cosmetic): 100 (missile launch flash, sprite 0x148), 0x6e (laser hit flash), 200/0xd2/300/0x136/0x140 (beam bodies; 0x147 for 200/0xd2), 500 (laser particles), 600 (accel wall). Generic `FUN_004ef2c0` ids: 0xfa hyper flash, 0x262 missile explosion, 0x456 streak, 0x514 charge particle, 0x515 charge muzzle, 0x516 hit burst, 0xa28–0xa2f dash sparks, 0xa82–0xa89 wall sparks, 2000 missile smoke, 0x546–0x548 muzzle flashes. (Names inferred from context — UNVERIFIED; no gameplay effect except RNG consumption.)

### 5.7 RNG
`FUN_00459fd0(a,b)` = `a + (u32/2^32)*(b−a)` from MT19937 (engine 0x875b68, or 0x876ef0 when byte 0xa50ef0 != 0); `FUN_00459f80(lo,hi)` = inclusive uniform int (rejection). Gameplay-relevant draws: Rapid-fire spread, Homing angle, Sidewinder offsets and wobble. Cosmetic draws (sparks, smoke, hit effects, AI) share the engine, so a replay/netplay-exact port must replicate them in order.

---

## 6. Sound ids used (SE table built at all.c:464010)
| id | file | where |
|---|---|---|
| 6 | up01.wav | hyper start |
| 7 | high_a.wav | Accel Burst start |
| 8 | air01.wav | dash / Sword Arrow 2-3 entry dash |
| 0xb | eco03.wav | shield |
| 0xd | _sen_mi_b_raifuru.wav | Beam Rifle, Rapid-fire |
| 0xe | tm2_gun000.wav | Charged Rifle, 0x134 |
| 0x12 | gun11_r.wav | lasers start |
| 0x13 | don16.wav | beam / charged / arrow / laser hits |
| 0x14 | bom06.wav | missile explosion |
| 0x16 | hit_s.wav | sword hits |
| 0x17 | hit_f.wav | Sword 3, Hyper Sword hits |
| 0x1b | explosion1.wav | Accel beam t=80 |
| 0x1f | swing.wav | sword swings, Sword Arrow |
| 0x20 | swing_h.wav | Sword Break, Hyper Sword, Hyper Arrow |
| 0x22 | burst00.wav | missile launch |
| 0x2a | A5_01029.wav | charge complete |

---

## 7. Other per-character table entries (index 10)

| Table (real base) | Suguri entry | Role |
|---|---|---|
| 0x881310 | FUN_0075a030 | input dispatch, standard scheme (§2) |
| 0x881360 | FUN_0075a4b0 | input dispatch, alternate scheme (`p+0xc==1`) (§2) |
| 0x8813b0 | FUN_00751e40 | sprite loader (§4.1) |
| 0x881400 + 0x10*idx + 4*level | FUN_0075b420 (level 0), FUN_0075a930 (levels 1, 2) | CPU AI: fills a virtual button word for CPU players (`p+8==10`), level = DAT_0087fab0 (all.c:300706). Uses the shared RNG (FUN_00459f80). Not reversed further. |
| 0x881878/0x8818c8/0x881918/0x881968 | 4b52a0 / 4b67a0 / 4b56e0 / 4b6570 | beam family (§5.1) |
| 0x8819b8/0x881a08/0x881a58/0x881aa8/0x881af8 | 67c2b0 / 67d2a0 / 67c440 / 67d040 / 67d170 | missile family (§5.3) |
| 0x881b98/0x881be8 | 5eca70 / 5ecfd0 | hitbox family (§5.2) |
| 0x881c38/0x881c88/0x881cd8/0x881d28 | 6289c0 / 62a540 / 628c00 / 62a2c0 | laser family (§5.4) |
| 0x881558/0x8815a8/0x8815f8/0x881648 | 550660 / 550aa0 / 5507d0 / 550a80 | accel units (spawn/draw/update/destroy) (§5.5) |
| 0x881698/0x8816e8/0x881738 | 50c820 / 50e9e0 / 50cd50 | Suguri effects (index 0 of these = generic FUN_004ef2c0 / 4f3e70 / 4eff30) |

**0x881530..0x881550 is NOT a per-character table.** It is a 9-entry table of *status-effect tick
functions* indexed by flag `p+0xa3+k` (main loop all.c:380647-380650), shared by all characters (they
just happen to be linked inside Suguri's block). For core.md:
| k | fn | flag / timer | effect per frame |
|---|---|---|---|
| 0 | (null) | a3 | — |
| 1 | FUN_00751b20 | a4 / b0 | heat += 0.007 (DAT_00813eb4); effect 3000 |
| 2 | FUN_00751920 | a5 / b4 | hyper −= 0.001 (DAT_00814bd8), +0xdc decays; effect 0xbc2 |
| 3 | FUN_00751ce0 | a6 / b8 | effect 0xc1c every 10 frames; while set, dash is refused (FUN_006b17d0 checks `+0xa6`) |
| 4 | FUN_00751dc0 | a7 / bc | timer only; while set, movement speed ×0.25 (all.c:380589) |
| 5 | (null) | a8 | — |
| 6 | FUN_00751cd0 | a9 | no-op (a9 blocks FUN_006b19a0 hit-state) |
| 7 | FUN_00751750 | aa / c8 | heat held ≥ 2.01 (DAT_00825fcc); on expiry heat = 1.0 |
| 8 | FUN_007516e0 | ab / cc | timer counts while state==1 |
Each clears its flag when its timer < 1 or state == 6 (k=8: when state != 1).

---

## 8. Stats record (index 10, base 0x883c18), written by FUN_0049ae40 (registry.c:1761-1803)

| Off | Value | Meaning |
|---|---|---|
| +0x00 | 5.0 (DAT_00813f40) | walk speed per axis per frame |
| +0x04 | 15.0 (DAT_00853b88) | dash speed (`DAT_00881f84` used by FUN_006b17d0) |
| +0x08 | 12.0 (DAT_00813f54) | UNVERIFIED (another speed — compare other chars; core.md) |
| +0x0c..+0x2c | 0,5,0,2,1,0,0,5,0 | UNVERIFIED (ints; possibly AI/HUD params) |
| +0x3c/+0x40/+0x44 | 0x12f, 0x130, 0x131 | sprite ids (ring / ? / system) |
| +0x48/+0x60/+0x78 | text ids 0x26/0x27/0x28 strings | colour-variant folder names |
| +0xa8 / +0x1a8 | `mdl\win_font_SG.lfd/.dat` | win-screen font |
| +0x2a8 | `sg_res_bgp` | |
| +0x2c0/+0x2c4/+0x2c8 | 30, −70, 0 | UNVERIFIED (portrait offset?) |
| +0x2cc/+0x2d0/+0x2d4 | 160, 180, 255 | colour (RGB) |
| +0x2d8 | 0 | |
**HP is not in this record** (no Suguri-specific HP value was found; HP fields `+0xd0/+0xd4/+0xd8/+0xdc`
are handled in common code — core.md). Ver patch FUN_004a7270 touches no other Suguri value besides
the 0x140 hyper gain.

---

## 9. UNVERIFIED / open items
- ~~DAT_0089426c value~~ SETTLED (core.md §0.3): live matches set header[1] = 0x5a2 (`DAT_00894268` struct), so the ver ≥ 0x5a1 columns apply.
- ~~Button encoding~~ SETTLED (core.md / FUN_0061bc70): 0 up, 1 released this frame, 2 held, 3 pressed.
- Hyper screen-effect functions FUN_004e9280 / FUN_007bec20 / FUN_004d7ac0 exact visuals.
- ~~Update vs collision order~~ SETTLED (core.md §1.3): players → enemies → beams → missiles → lasers → effects → collisions; the laser damage set in its update is the one tested that frame.
- ~~Textures~~ SETTLED: laser 2D sprite 0x149 `sg_lsr00/01` (imul 0x149 @0x628e9a), accel units model 0x146 `sg_artl` (@0x550b5d), charged beam 0x14a `sg_rflc`, effect 600 0x141 `sg_slash`; missile body 0x145 `sg_msl`, exhaust 0x144 `sg_bfire`.
- ~~Stats +0x08 / +0x0c..+0x2c~~ SETTLED (core.md, objects.md §5.2): +0x08 = dash-hold speed 12 (state 2), +0x0c..+0x2c = 3 palettes × 3 colour-table indices ({0,5,0},{2,1,0},{0,5,0}). +0x2c0..+0x2c8 UI only.
- Up/down meaning of `+0x24`/`+0x28`.

## 10. Port verification notes (src/sim/chars/suguri.c, tests/test_suguri.c)

Settled from the decompile/disassembly during the 1:1 pass:
- **Rapid-fire input** really tests Dash ≥ 1 (`cmp [eax+0x34],1; jl` @0x75a3e3, and @0x75a863 in FUN_0075a4b0), not "held".
- **Effect 600** (Accel beam wall) draws 2D sprite **0x141** (`sg_slash`) (`imul eax,edx,0x141` @0x50e8c9); src (0,256)-(255,511), half 1600·scale, alpha 255·a (even age) / 192·a (odd), blend 2.
- **2D sprite bank** for object blits is `0x885b8c + bank*0x3888 + id*0x18` (a separate vector beside the 3D models at `0x885b80`, same ids).
- **Laser body block** (FUN_00610360, engine `pass_laser_block`): the iterator to the touching segment k is post-incremented (FUN_00610a80 @0x61056c, +0x1c = one segment) before `erase`, so segments **[k+1, end)** are erased and the touching one is kept. Without this a body-blocked laser (Hyper Rifle, blockMode 2) can never hit.
- **Spawn timing as seen from the owner's timer**: an object spawned on owner t=T has its first update in the same tick at owner t=T+1 (players update before objects, core.md §1.3), so object age a ↔ owner timer T+1+a (e.g. Hyper Arrow hitboxes 0x136 at owner t 14,17,…,50).

Behaviour covered by tests/test_suguri.c: every §2 input row (both schemes, the
"spend 0.5 before may_shield" quirk, hyper fall-through, Accel lockout), spawn frames of
every move, beam/missile/hitbox parameters, damage (beam 216, sword 180, Hyper Sword 720),
ammo costs and hyper gains (§1.2), hit-confirm chains (0x139→0x13a→0x13b/0x134, none without a
hit), Sword Arrow chain windows, the 0x13d entry dash, Accel units + laser, and the dash-cancel
and end frame of all 16 moves.
