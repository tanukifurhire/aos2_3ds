# Tsih — complete moveset spec

Source: `aos2.exe` (Ghidra corpus `tools/re/dec/all.c`, constants read with `xref.exe rd`).
Structure follows suguri.md. Every number cites its address. `t` = player state timer `p+0x154`.
Engine character index `p+0x10` = **5** (handbook id 2 + 3). Object "type" for every Tsih object = 5.
Shared helpers (FUN_007807f0 ammo/hyper cost, FUN_006b1b40 hyper start, FUN_00780910 hyper spend,
FUN_006b17d0 dash, FUN_00612830/00612900 training-display only, RNG FUN_00459fd0/FUN_00459f80, SE
FUN_007bc9c0/FUN_007bcaf0) are as in suguri.md §1 and objects.md — not re-derived here.
Version global `DAT_0089426c` = 0x5a2 in live play (objects.md §0.1): **implement the ver ≥ 0x5a1 column**.

---

## 0. Notes for the lead (cross-character findings)

1. **render.md correction — CModel `+0xdc` colour argument order is (a, r, g, b, mesh)**, not (r,g,b,a).
   `FUN_0046ef70` writes `param_3→+0, param_4→+4, param_5→+8, param_2→+0xc` of the material
   (D3DCOLORVALUE r,g,b,a). Proof by use: the training "state colour" option (`DAT_0089437f`, code at
   all.c:356756-356800) zeroes `da4,da8` for dash → (a1,r0,g0,b1) = blue, `da8,dac` for hit → red,
   `dac` for guard → yellow; with (r,g,b,a) the hit colour would be invisible. Every `0xdc(...)` call
   in suguri.md §4 that passes all-1.0 is unaffected, but any non-white call must be re-read.
2. **Stealth affects every opponent aim.** `FUN_007804b0` (player aim) *and* `FUN_0055f110`
   (the Enemy-object aim helper, callers FUN_00528e20, FUN_00553760, FUN_0055add0 — other
   characters' summons) both test "opponent `+0x10 == 5` (Tsih) && opponent `+0x164 == 1`". In that
   case the aimer uses **its own player's directional input** (`FUN_0061b230(player+0x1c)`), or, with
   no direction held, π if Tsih.x ≤ aimer.x else 0 (all.c:438906, 198145). Projectiles that compute
   `atan2` to the opponent directly (e.g. Suguri's homing missile FUN_0067d2a0) still track Tsih.
3. **Player `+0x164` / `+0x168` are per-character scratch.** Tsih: `+0x164` byte = stealth on,
   `+0x168` int = stealth frames left. (Nath uses `+0x164` as an int.) Nothing in Tsih's code clears
   them in Tsih's code; player init `FUN_0069cc40` memsets the whole 0x178 player record (and writes
   0 to +0x164/+0x168 again via local_1c/local_18), so stealth is cleared every round.
4. **Player `+0x78/+0x7c/+0x80` = copy of pos taken at the end of the player update**, after
   movement and arena clamp, before the integer truncation of objects.md §1.3 step 3
   (all.c:380802-380804). Tsih's beam-ball and D+A spawns use `p+0x78` instead of `p+0x6c`.
5. **Common states call per-character T2 hooks** (core.md): state 0 (FUN_006af390) → T2[0x5b];
   state 1 (FUN_006af880) → T2[0x5e]; state 2 (FUN_006af5e0) → T2[0x5c]; state 6 (FUN_006b0110
   `case 5`) → T2[0x5d]. Common T1s: 0→0x5b, 1→0x5e, 2→0x5c, 3→0x5f, 4→0x5f, 5→0x5e, 6→0x5d
   (FUN_006b07a0/006b0c40/006b09f0/006b0e90/006b10e0/006b1330/006b1580 via DAT_0088019c..ac).
6. **Anti-juggle idiom in hitbox onHit callbacks** (Tsih FUN_005f01b0): `if (victim.state == 1 &&
   victim.speed(+0x98) > K) hb.dealsDamage = 0` (K = 2.0 DAT_00813a4c, or 3.6 DAT_0081fa8c for id
   400). Since Tsih spawns many boxes every frame, this is what limits re-hits. Expect the same in
   other characters.
7. `DAT_0089437f` (training "state colour" display) and `DAT_00894382` (another display toggle,
   **UNVERIFIED** meaning; menu code all.c:343273-343292) suppress Tsih's stealth tint.
8. Stats `+0x0c..+0x20` are colour **palettes** (objects.md §5.2): Tsih {10,8,6} and {3,7,11}.
9. `FUN_006b1d70(src, range, speed)` (proximity knock, objects.md §4.6), on success, also clears the
   camera focus list (`mov ecx,0x895144; call 0x4a9f10` at 0x6b1e7d; objects.md camera section).

### Player fields used by Tsih's code

| Off | Name | Notes / source |
|---|---|---|
| +0x04 | owner id | all spawns |
| +0x10 | char index (=5) | |
| +0x14 | team | opponent = FUN_00780480(team), self = FUN_00780450(team) |
| +0x18 | palette/bank | sprite bank `0x885b80 + bank*0x3888` |
| +0x1c..+0x28 | dir x+/x−/y+/y− | |
| +0x34..+0x48 | Dash, A, B, S, H, Shield | as suguri.md |
| +0x6c,+0x70 | pos | |
| +0x78,+0x7c | end-of-last-update pos | note 4 |
| +0x84 | aim | |
| +0x88 | copy of aim | |
| +0x8c | facing | |
| +0x94/+0x98/+0x9c | move dir / speed / friction | as suguri.md |
| +0xe8/+0xec | ammo A / B | via FUN_007807f0 |
| +0x128 | combo counter of this player as attacker (objects.md §4.6) | read by wire hitbox onHit |
| +0x150/+0x154 | state / timer | |
| +0x164 | stealth flag (byte) | set 0x65 t=10, cleared 0x66 t=10 / 0x5e |
| +0x168 | stealth timer (int) | 180 on enable; −1 per frame in states 0 and 2 only |
Tsih never reads the hit-confirm flag `+0x158`.

---

## 1. Shared mechanics

Frame model, dash-cancel (`t ≥ N && Dash==3 → FUN_006b17d0(p,1)`), ammo/hyper table, hyper permission
and hyper start: exactly suguri.md §1.1–§1.4.

**"Face-x"** (used at t=0 by most Tsih states): `opp = FUN_00780480(team); if (opp.x <= p.x) aim = π
(DAT_00853b58) else aim = 0; +0x88 = aim`. (Not FUN_007804b0 — it ignores the opponent's y.)

**"Aim"** = `+0x84 = FUN_007804b0(p)`.

**Hyper flourish (Tsih variant)** at t=0 of 0x65/0x69/0x6c/0x70 (e.g. FUN_00784130):
face-x; `FUN_006b1b40(p,10)`; SE 6; `FUN_004e9280(100, 0.75 DAT_00813f3c)`; `FUN_007bec20(100, pos, 0, 0)`;
`FUN_004d7ac0(20, 0.5, 2)`; effect 0xfa (common, colour 0, angle 0). Cosmetic (suguri.md §1.4).

**Dash sparks** (0x73/0x74/0x75, e.g. FUN_007862a0): each frame if `speed(+0x98) >= 5.0` (DAT_00813f40):
`r = rand_f(−0.15 DAT_00815dc4, 0.15 DAT_00813f20)` then `n = rand_i(0,100) % 8`; common effect
`0xa28+n`, colour 0xd, at pos, angle `aim + π/2 (DAT_00814104) + r`. Cosmetic but consumes RNG (rand_f first).

### 1.1 Ammo / hyper cost table entries (written by FUN_0049ae40, registry.c:1466-1489)
`entry = 0x87c4f0 + id*0xc = {costA, costB, hyperGain}`. Not patched by FUN_004a7270.

| State id used in call | Move | costA | costB | hyperGain |
|---|---|---|---|---|
| 0x67 (per beam) | Bendy Beam | 0.15 (DAT_00813f20) | 0 | 0.02 (DAT_00813ed8) |
| 0x68 (per beam) | A Lot of Bendy Beams | 0.125 (DAT_00813f1c) | 0 | 0.015 (DAT_00813ed4) |
| 0x6a | Beam Ball Scattering | 0.5 (DAT_00853b1c) | 0 | 0.05 (DAT_00813ef4) |
| 0x6b | Wire Laser | 0.35 (DAT_00813f2c) | 0 | 0.05 |
| 0x6d (also called by 0x6e/0x6f) | Throw a Rock | 0 | 0.4 (DAT_00813f30) | 0.075 (DAT_00813efc) |
| 0x73 / 0x74 / 0x75 | Swirl 1 / 2 / 3 | 0 | 0 | 0.05 each |
Hypers (0x65, 0x69, 0x6c, 0x70, 0x77) never call it.

---

## 2. Input dispatch `FUN_00786e70` (standard scheme) — exact decode

**A. Hypers** — only if `FUN_00780950(p)`. First match wins; on cost failure fall through to the next row.

| # | Condition | Cost | State | Official move |
|---|---|---|---|---|
| 1 | S held (`+0x40>1`) && B held (`+0x3c>1`) && H pressed (`+0x44==3`) | 3.0 (DAT_00813a50) | 0x77 | **Throw Huge Rocks** (Accel) |
| 2 | S held && H pressed | 1.0 (DAT_00853b34) | 0x65 | **Stealth On** |
| 3 | B held && H pressed | 1.0 | 0x70 | **A Lot of Rocks** |
| 4 | Dash held (`+0x34>1`) && H pressed | 1.0 | 0x6c | **Rounding Wire Laser** |
| 5 | H pressed | 1.0 | 0x69 | **A Whole Lot of Bendy Beams** |

**B. Normal moves** — only if `state == 0 || state == 2`:

| # | Condition | Result | Move |
|---|---|---|---|
| 1 | Dash pressed | `FUN_006b17d0(p,0)` | Dash |
| 2 | Shield pressed && `FUN_00780910(p,0.5)` && `FUN_007809e0(p)` | `FUN_006b1930(p,0)` | Shield |
| 3 | S held && B pressed | 0x73 | **Swirl, Swirl and Release** |
| 4 | B pressed | y+ held (`+0x24>1`) → 0x6e; else y− held (`+0x28>1`) → 0x6f; else 0x6d | **Throw a Rock** |
| 5 | S held && A pressed | 0x6b | **Wire Laser** |
| 6 | Dash held (`+0x34>1`) && A pressed | 0x68 | **A Lot of Bendy Beams** |
| 7 | A pressed | 0x6a (→ 0x67 if released before t=10) | **Beam Ball Scattering** (hold) / **Bendy Beam** (tap) |

Then returns `T2[state](p) != 0`. Note D+A and D+H test Dash **> 1** (held), unlike Suguri's `> 0`.

**Alternate scheme `FUN_00787360`** (`p+0xc==1`, table 0x881374): identical except hyper rows are
"H held (`+0x44>1`) and X pressed": Shield → 0x77, S → 0x65, B → 0x70, Dash → 0x6c, A → 0x69 (diff of the two fns).

States not reachable from dispatch: 0x66 (stealth expiry), 0x67 (from 0x6a), 0x74/0x75/0x76 (from 0x73
chain), 0x71 (never entered — §3 end).

---

## 3. States

### 0x5b — neutral (T2 FUN_00783470, via common state 0)
Walk exactly like suguri 0x128 but `speed = stats+0 = 6.0` (DAT_00813f44 → 0x882dcc); `+0x84 = 0` if facing
else π. **Stealth tick:** if `+0x164==1`: `+0x168 -= 1`; if `+0x168 < 1` → state **0x66** (t=0, flag=0) and run it.

### 0x5c — dash hook (T2 FUN_007835d0, called each frame from common state 2)
Stealth tick only (same as above → 0x66).

### 0x5e — hit hook (T2 FUN_00783650, from common state 1)
If `+0x164==1 && +0x168 < 1`: `+0x164 = 0`, SE 0x2c. (Only matters if hit during 0x66 before its t=10.)
The timer does **not** count down in hitstun, shield, attacks — only in states 0 and 2.

### 0x5d — KO hook (T2 FUN_007836a0, from common state 6)
At t=0: every Enemy with owner (`+8`) == p+4 and spawn id `+0x10` ∈ {100, 200, 0xd2}: `+0x84 = 900`,
`+0x80 = 0`, `+0x2c = 0` (rocks/swirl vanish). (+0x10 is never 0xd2 in practice; Accel rocks, +0x10=300, are untouched.)

### 0x5f — stub (FUN_00783690 returns 1); only its T1 is used (common 3/4).

### 0x65 — Stealth On (T2 FUN_00783790) — S+H, 1 level
| t | Action |
|---|---|
| 0 | face-x; hyper flourish (kind 10) |
| 5,10,15,20,25 (4<t<26, t%5==0) | common effect 0xaf0, colour `DAT_008859fc+team*0x10`, at pos, angle 0 |
| 10 | `+0x164 = 1`, `+0x168 = 180` (0xb4); SE 0x2b |
| >30 | state 0 (no dash-cancel) |
Re-entering while stealthed costs another level and resets the timer to 180.

### 0x66 — Stealth Off (T2 FUN_007839a0) — on timer expiry
t=0 face-x; t=5,10,15,20: effect 0xaf0 as above; **t=10: `+0x164 = 0`, SE 0x2c**; dash-cancel ≥25; >40 state 0.

### 0x6a — A press / Beam Ball Scattering (T2 FUN_00783b40)
| t | Action |
|---|---|
| 0 | face-x |
| 0–9 | if A not held (`+0x38 < 1`) → state = 0x67 (**timer kept**, flag kept) and run it |
| 16 | if `FUN_007807f0(p,0x6a)`: for k = −16..15: beam (k even → **100**, odd → **0x69**) at **p+0x78**, angle `k/16·π` (DAT_008140d8=16); SE 0x10 (32 beams in a ring) |
| ≥25 | dash-cancel |
| >30 | state 0 |
(Releasing A between t=10 and 16 does not cancel the ring.)

### 0x67 — Bendy Beam (T2 FUN_00783d80) — via 0x6a early release (timer continues)
| t | Action |
|---|---|
| 12 | if `FUN_007807f0(p,0x67)`: aim; beam **0xd2** at pos, angle aim + π/3 (DAT_008203bc = 1.0471976); SE 0xf |
| 14 | if cost ok: aim; beam **0xd7** at pos, aim − π/3; SE 0xf |
| ≥15 | dash-cancel |
| >25 | state 0 |

### 0x68 — A Lot of Bendy Beams (T2 FUN_00783f20) — Dash held + A
| t | Action |
|---|---|
| 0 | aim |
| 12, 15, 18, 21 | if `FUN_007807f0(p,0x68)`: aim (unused); beam id = ((t/3)%2==0 ? **200** : **0xcd**) → 200, 0xcd, 200, 0xcd; at **p+0x78**, angle `rand_f(−π, π)` (DAT_00814100, DAT_00853b58); SE 0xf (call order verified at 0x783fdd) |
| every frame | friction `+0x9c *= 0.5` |
| ≥25 | dash-cancel |
| >30 | state 0 |

### 0x69 — A Whole Lot of Bendy Beams (T2 FUN_00784130) — H, 1 level
t=0 flourish. **t=10**: for k = −8..7: beam (k even → **0xdc**, odd → **0xe1**) at pos, angle `k/8·π`
(DAT_008140d4=8); SE 0xf (16 beams). Dash-cancel ≥30; >50 state 0.

### 0x6b — Wire Laser (T2 FUN_00785b40) — S+A
t=0 aim. **t=5**: if `FUN_007807f0(p,0x6b)`: beam **300** at pos, angle aim; SE 0xf. Dash-cancel ≥20; >40 state 0.

### 0x6c — Rounding Wire Laser (T2 FUN_00785c40) — D+H, 1 level
t=0 flourish (aim = face-x, never re-aimed). For i = 0..9 at **t = 10 + 4i** (10,14,…,46):
`a = (i%5)/5·2π (DAT_00813f40=5, DAT_00853b78) + rand_f(−0.5 DAT_00853bb0, 0.5) + aim`, wrapped
(FUN_005f0c60); beam **0x136** at pos, angle a; SE 0xf. Dash-cancel ≥60; >80 state 0.

### 0x6d / 0x6e / 0x6f — Throw a Rock (shared T2 FUN_007843b0) — B (neutral / up / down)
| t | Action |
|---|---|
| 0 | face-x |
| 10 (ver ≤ 0x5a0: 6) | if `FUN_007807f0(p,0x6d)` (B −0.4): spawn Enemy **100** twice (rock R1, R2); `R.+0x88 = rand_i(0,100) % 4` for R1 then R2 (sprite variant). Position (ver ≥ 0x5a1, same for all three ids): `off = (x+ ? 70 : x− ? −70 : 0, y+ ? 70 : y− ? −70 : 0)` (DAT_008145ec / DAT_008256c8; later test wins: y− over y+, x− over x+); R1 = pos + off + (cos,sin)(2.8915927 DAT_008264c4)·100; R2 = pos + off + (cos,sin)(0.25 DAT_00813f24)·100 (DAT_00853b90). If B held (`+0x3c>1`): `R1.+0xea = R2.+0xea = 1`. Then laser **100** at pos/aim for each rock with `laser.+0x1c = rock.id(+0)` (wire). Effect 0x514 at each rock; SE 0x2f |
| 25 | **throw**: `FUN_00782900(p)` (§5.3) |
| [12,24] and ≥30 | dash-cancel (612900 at 12 and 30) — cancelling before 25 leaves the rocks idle |
| >40 | state 0 |
ver ≤ 0x5a0 rock angles instead: 0x6d {π−0.25, 0.25}, 0x6e {2.3916 DAT_008264c0, 0.75 DAT_00813f3c}, 0x6f {−2.8916 DAT_008264c8, −0.25 DAT_0081411c}, no direction offset, no `+0xea`.

### 0x70 — A Lot of Rocks (T2 FUN_00785030) — B+H, 1 level
t=0 flourish. **ver ≥ 0x5a1, t=4**: for i = 0..7: `rand_f(120, 240)` (DAT_00814174, DAT_00815584) is drawn and **discarded**;
`(off, r)` = i∈{0,1}: (−0.6 DAT_00826140, 140 DAT_00817004); {2,3}: (0.6 DAT_00813f34, 140); {4,5}: (0.2 DAT_00853b14, 200 DAT_008147cc);
{6,7}: (−0.2 DAT_00817c04, 200). Spawn Enemy **100**; aim (FUN_007804b0, re-evaluated each i);
`a = off + aim (+π if i odd)`, wrapped; rock pos = pos + (cos,sin)(a)·r; `+0x88 = rand_i(0,100) % 4`; laser **100** linked; effect 0x514; SE 0x2f.
**t=16** (ver ≤ 0x5a0: 42): throw (`FUN_00782900`). Dash-cancel ≥19 (old 45); **t ≥ 40** (old 70) state 0.
(Old version: one rock every 4 frames for 3<t<33 at random radius/angle.)

### 0x73 — Swirl 1 (T2 FUN_00785ed0) — S+B
| t | Action |
|---|---|
| 0 | aim; `+0x94 = aim`; speed 0; spawn Enemy **200** (swirl, §5.3); SE 0x2f |
| 10 | if `FUN_007807f0(p,0x73)` (always): aim; `+0x88 = +0x94 = aim`; speed **12** (DAT_00813f54); SE 0x20 |
| every frame | dash sparks (§1) |
| t > 19 && B pressed && `FUN_007807f0(p,0x74)` | → state **0x74** (t=0, flag 0), speed 12, `FUN_005f0e70(&aim, angleToOpp, π/3)`, dir = aim, SE 0x20, run T2 |
| t == 16 && B not held (`+0x3c < 2`) | → state **0x76** (t=0), SE 0x1f, run T2 |
| otherwise | dash-cancel ≥30; >45 (ver ≤ 0x5a0: 50) state 0; friction ×0.5 |

### 0x74 — Swirl 2 (T2 FUN_007862a0)
Sparks; `t > 11 && B pressed && cost(0x75)` → **0x75** (speed 12, aim turned ≤ π/3 toward opponent, dir = aim, SE 0x20);
`t == 10 && B < 2` → **0x76** (SE 0x1f); else dash-cancel ≥20; >35 (old 40) state 0; friction `×0.4` (DAT_00813f30).

### 0x75 — Swirl 3 (T2 FUN_00786560)
Sparks; **t == 12 → 0x76** (SE 0x1f) unconditionally; else dash-cancel ≥20; >35 state 0; friction ×0.3 (DAT_00813f28).

### 0x76 — Swirl release recovery (T2 FUN_00786720)
Dash-cancel ≥15 (old 20); >35 (old 40) state 0. The swirl object launches itself when it sees this state (§5.3).

### 0x77 — Throw Huge Rocks (Accel) (T2 FUN_007867c0) — S+B+H, 3 levels
| t | Action |
|---|---|
| 0 | face-x; `FUN_006b1b40(p,0x14)`; SE 7; `FUN_004e9280(120, 0.85 DAT_00814120)`; `FUN_007bec20(100,pos,aim,0)`; `FUN_004d7ac0(120,0.5,2)`; effect 0xfa |
| 0 (ver ≥ 0x5a1) | for i = 0..5: Enemy (i even → **300**, odd → **0x12d**); y = pos.y + 300 (DAT_008145f0); `+0x88 = (t/5)%4 = 0`; x = pos.x + d (even) / pos.x − d (odd) with d = 60 (DAT_00813a58), d += 120 (DAT_00814174) after each odd i → x offsets +60, −60, +180, −180, +300, −300; `+0x5c = −π/2` (DAT_0081416c); laser **300** at the rock's pos, angle −π/2, linked (`+0x1c = rock id`) |
| 75 | `FUN_006b1d70(p, 360 DAT_00817894, 20 DAT_008140fc)` (opponent within 360 px → hitstun, pushed away at 20, SE 0x19; no damage); effect 0xa8c at pos (always) |
| ≥80 | dash-cancel |
| >100 | state 0 |
(ver ≤ 0x5a0: one rock every 5 frames for t<26 at x ± rand_f(10,300).)

### 0x71 — unreachable (T2 FUN_00785810)
No code assigns state 0x71 (grep of all.c). It references Tsih Missile id 100 objects (never spawned —
DAT_008819cc has no caller) and Laser 200. Do not port; listed for completeness.

---

## 4. Animation

### 4.1 Sprite registration (loader `FUN_00780f20`)
Arg 0 → folder string 0x2c, else 0x2d (`charaTsihA` / `charaTsihB`; only two colour variants). Quads S×S as suguri §4.1.

| Sprite id | Pattern | Frames | S | Used by |
|---|---|---|---|---|
| 0x5b | `ts_std0%d` | 8 | 96 (DAT_00814118) | neutral |
| 0x5c | `ts_dsh0%d` | 6 | 96 | dash |
| 0x5d | `ts_los0%d` | 6 | 96 | T1 0x5d, 0x5e |
| 0x5f | `ts_sld0%d` | 4 | 96 | T1 0x5f, 0x65, 0x66, 0x6a, 0x6b, 0x6c, parts of 0x69/0x6d/0x70/0x77 |
| 0x60 | `ts_fwd0%d` | 3 | 96 | neutral moving toward |
| 0x61 | `ts_bck0%d` | 3 | 96 | neutral moving away |
| 0x62 | `ts_rng0%d` | 2 | 512 (DAT_008178b0) | stats +0x3c (dash-trail/graze sprite, objects.md §5.2) |
| 100 | `ts_system` | 1 | 512 | stats +0x44 |
| 0x73 | `ts_swing0%d` | 2 | 96 | 0x73–0x76 |
| 0x78 | `ts_acta0%d` | 6 | 96 | 0x67, 0x68 (0x71) |
| 0x79 | `ts_actb0%d` | 5 | 96 | second half of 0x69, 0x6d–0x6f, 0x70, 0x77 |
| 0x7a | `ts_rock0%d` | 8 | 96 | rocks (Enemy draw, §5.3) |
All 64 `ts_*.png` in `extracted/texture/charaTsihA/` are covered; 0x5e, 0x63 are not registered.
`FUN_00782af0` (called for index 5 at all.c:355441) builds the pause-menu command list — no gameplay (UNVERIFIED role, same pattern as Suguri FUN_00756600).

### 4.2 Draw transforms and the stealth look
- **AIM transform** (every T1 except 0x5b): identical to suguri §4.2 (rotZ(aim); if `|aim| ≥ π/2` rotX(180°); rotY(180°)).
- **Neutral 0x5b**: rotY(180°) if facing != 0.
- **Colour** (all player T1s): if neither `DAT_0089437f` nor `DAT_00894382`: stealth → `0xdc(a 0.75, r 0.5, g 1, b 1)`
  (DAT_00813f3c, DAT_00853b1c, DAT_00853b34), else white opaque; if either flag set → global tint `FUN_004e8f50`.
  (0x5b/0x5c call FUN_004e8f50 first, then overwrite.)
- **Stealth second pass**: if `+0x164==1`, after the normal draw: blend 2 (additive), same colour rule, same
  frame/transform, scale `s = 1 + 0.2·sin(((F%50)/50)·π)` (F = global frame `DAT_008858f8`; DAT_008140ec=50,
  DAT_00853b14=0.2) on all three axes; then blend 1.
- Heat afterimage `FUN_004d7e00(model,1,p,{0,0,0},0)` only in T1 0x5b, 0x5c, 0x73, 0x74, 0x75 (after the stealth pass).

### 4.3 Per-state frames (`pp(n)` = `(t/n)&3`, 3→1)
| State(s) | T1 | Sprite | Frame |
|---|---|---|---|
| 0x5b | FUN_007886a0 | 0x5b; override (later wins): y+→0x60; y−→0x61; facing0&x−→0x60; facing1&x−→0x61; facing0&x+→0x61; facing1&x+→0x60 | std: `(t/5)%8`; fwd/bck: pp(5) |
| 0x5c | FUN_00788d80 | 0x5c | pp(3); +3 if (facing1 && \|aim\|>π/2) or (facing0 && \|aim\|<π/2) |
| 0x5d | FUN_0078a6a0 | 0x5d | pp(3) + (3 if F even); **not drawn when t ≥ 100** |
| 0x5e | FUN_00789710 | 0x5d | pp(3) |
| 0x5f, 0x65, 0x66, 0x6a, 0x6b, 0x6c | FUN_00789ee0, 0078ae80, 0078b620, 0078bdc0, 0078f8f0, 00790090 | 0x5f | min(t/5, 3) |
| 0x67, 0x68 (0x71) | FUN_0078c560, 0078cd00 (0078f770) | 0x78 | min(t/3, 5) |
| 0x69 | FUN_0078d4a0 | t<20: 0x5f, min(t/5,3); else 0x79, min((t−20)/5, 4) | |
| 0x6d/0x6e/0x6f | FUN_0078dc80 | t<20: 0x5f, min(t/4,3); else 0x79, min((t−20)/4, 4) | |
| 0x70 | FUN_0078e760 | T = 15 (ver ≤ 0x5a0: 40); t<T: 0x5f, min(t/4,3); else 0x79, min((t−T)/4, 4) | |
| 0x77 | FUN_0078ef60 | t<20: 0x5f min(t/4,3); t<70: 0x79 min((t−20)/4,4); else 0x5f min((t−70)/5,3) | |
| 0x73 | FUN_00790830 | 0x73 | 1 for 13 ≤ t < 22, else 0 |
| 0x74, 0x75 | FUN_00791100, 007919d0 | 0x73 | 1 for 6 ≤ t < 15, else 0 |
| 0x76 | FUN_007922a0 | 0x73 | 1 |
(Integer divisions of non-negative t; the `>>2` forms in 0078dc80/0078e760/0078ef60 are t/4.)
Unregistered draw fns FUN_0078e460 / FUN_0078e5e0 (acta, min(t/3,5)) are dead code.

---

## 5. Spawned objects (type 5)

Tables: Beam spawn/update/draw/onHit `0x88188c/0x8818dc/0x88192c/0x88197c` = FUN_004ba600 / 004bc950 /
004baaa0 / 004bc6d0; Hitbox `0x881bac/0x881bfc` = FUN_005efd20 / 005f01b0; Laser `0x881c4c/0x881c9c/0x881cec/0x881d3c`
= FUN_006308f0 / 00630f70 / 00630a80 / 00630f50 (no-op); Enemy `0x88156c/0x88160c/0x8815bc/0x88165c` (spawn/update/draw/onKilled)
= FUN_0055f1e0 / 0055f6b0 / 00560760 / 005605f0; Effect `0x8816ac/0x8816fc/0x88174c` = FUN_005142a0 / 00514b70 / 005143e0.
Missile entries exist (FUN_00685d40 …) but nothing spawns Tsih missiles. Struct layouts: objects.md §2.

### 5.1 Beams (FUN_004ba600 template → FUN_004bc950 update)
Common init FUN_004ba490 (hitType 1 = dash-avoidable, destroyOnHit 1, active 1, stun 1.0, scale/alpha 1.0).
Spawn **normalises ids**: 0xcd→200, 0xd7→0xd2, 0xe1→0xdc, setting `curl = +0x6c = +1.0`; the base ids get `curl = −1.0` (DAT_00853bb4).

| Id (alias) | Used by | speed +0x68 | shape r (+0x7c) | dmg | knock | other init |
|---|---|---|---|---|---|---|
| 100 / 0x69 "beam ball" | 0x6a | 12 (DAT_00813f54) | 8 (DAT_008140d4) | 120 | 5 (DAT_00813f40) | scale +0x58 = 3.0 |
| 200 (0xcd) "bendy" | 0x68 | 20 (DAT_008140fc) | 8 | 240 | 10 (DAT_00813f4c) | curl −1 (+1) |
| 0xd2 (0xd7) "homing bendy" | 0x67 | 20 | 8 | 280 | 10 | curl −1 (+1) |
| 0xdc (0xe1) "hyper bendy" | 0x69 | 24 (DAT_00853b8c) | 8 | 300 | 10 | curl −1 (+1) |
| 300 "wire" | 0x6b | 16 (DAT_008140d8) | none | 0 | 0 | alpha 0; **ver > 0x5a0: `+0x64 += π`** |
| 0x136 "rounding wire" | 0x6c | 10 + rand_f(0, 12) (DAT_00813f4c, DAT_00813f54) | none | 0 | 0 | alpha 0 |
Dead beams: removal by returning 0 (no onHit).

**100 / 0x69 update.** Phase A (age < 1000 and speed > 1.0): `speed *= 0.925` (DAT_0081417c); if now `speed <= 1.0`:
`moveAngle += rand_f(−π/2, π/2) + (id==100 ? π : 0)`; `age = 1000` (→ phase B next frame). Phase B (age ≥ 1000 or speed ≤ 1):
`moveAngle += 0.005` (id 100) / `−= 0.005` (0x69) (DAT_00813eac), wrapped to (−π, π]; if `speed < 5`: `speed += 0.1`.
Then move `pos += (cos,sin)(moveAngle)·speed` (draw/collision angle +0x64 is never updated); `if scale > 1: scale −= 0.05`.
If `!inArena(pos, 20)`: id := **0x6e**, active 0, scale 3.0, effect 0x516 (colour `0x8859f4+team*0x10`) at pos.
**0x6e** (fade): `scale −= 0.5`; return 0 when ≤ 0 (6 frames, no movement).
(So id 100 balls stop after ~32 frames and drift back toward where they came from; 0x69 continue outward.)

**200 / 0xd2 update.** Phase A (age < 1000): `moveAngle += speed·0.01·curl·(age<10 ? age/10 : 1)` (DAT_00813ec8, DAT_00813f4c);
`+0x64 = moveAngle`; `if speed > 8: speed −= 0.5`; `if age > 150: age = 1000`.
Phase B: `speed += 0.1`; **id 0xd2 only, while age < 1050 (0x41a)**: `turnToward(&moveAngle, atan2(opp − pos), (1100 − age)/100·0.05)` (DAT_00814180, DAT_00853b90, DAT_00813ef4);
then `moveAngle += sin(((age%20)/20)·2π)·0.1·curl` (wobble); `+0x64 = moveAngle`.
Move; if `age > 1030 && !inArena(pos,20)` return 0. Every frame: Tsih effect **100** (colour `0x8859f8+team*0x10`) at pos, angle +0x64 (the visible bolt).

**0xdc update.** As 200 but curl factor 0.0075 (DAT_00813ebc), `if speed > 10: speed −= 0.5`;
phase B: `if speed >= 16: speed −= 0.1` (never true after phase A), wobble `sin(((age%40)/40)·2π)·0.15·curl` (DAT_008140e0, DAT_00813f20), no homing;
trail effect colour `0x8859fc+team*0x10`.

**300 (Wire Laser) update.** While age < 200: `alpha = min(alpha + 0.04, 1)` (DAT_00813eec); if age%10==0: two effects 0x516
(colour `0x8859fc+team*0x10`) at `pos ± (cos,sin)(+0x64 − π/2)·80·alpha` (DAT_00814164); every frame: hitbox (**100** if age%4==0
else **0x6e**) at pos, angle **+0x64**, then `hb.halfH(+0x30) = 80·alpha`; `touch = FUN_005f1c90(hb.pos, opp.pos, hb.shape, opp+0x5c, hb.angle, opp+0x84)`.
Age ≥ 200: `alpha −= 0.02` (DAT_00813ed8), return 0 at ≤ 0.
Movement (ver ≥ 0x5a1): if `touch && age > 29`: speed = 0 (stays 0 forever); else `speed *= 0.9` (DAT_00814178); then move along +0x60.
(Max travel 16·0.9/(1−0.9) = 144 px. Because +0x64 = aim+π, the wire's knockback (§5.2 id 100) pulls toward Tsih.)

**0x136 (Rounding Wire) update.** Same pattern: age < 300 alpha +0.04; age%10==0 end effects at ±120·alpha (DAT_00814174);
**age > 10**: hitbox 100/0x6e (by age%4) with `halfH = 120·alpha`; age ≥ 300: alpha −0.02 → die. Always `speed *= 0.9`. No touch test.

**onHit FUN_004bc6d0** (beam destroyed on a player): ids 100/0x69 → colour slot 0x8859f4; 200/0xd2/0xdc family → 0x8859f8; then common effect 1000 at pos
angle +0x64+π, particle `FUN_0068b8a0(1000,…,3)`, spark `FUN_007bd5e0(1000,c,10,5,angle,pos,owner)`, SE `FUN_007bcaf0(0x13,3)`. Cosmetic.

**Draw FUN_004baaa0** (beamsphere texture `DAT_0087fde4`, sprite call vtable+0x68, colour index c from the slot above):
100/0x69/0x6e: glow quad half-size 30·scale/zoom (DAT_008140e8), src cell `((c%10)&1)·100+50+(c/10)·250, ((c%10)/2)·100`, 100×100,
alpha 96·a (DAT_00814118), blend 1; core half 15·scale (DAT_00853b88), src `((c/10)·250, (c%10)·50)` 50×50, alpha 255·a (DAT_00853b94 = 255), blend 2.
300/0x136: bar half (10, 90·a) (DAT_00813f4c, DAT_00814170) / (10, 120·a), rotated −angle, plus core and two end orbs (half 20/6 ·scale) at ±80/±120 perpendicular.
200/0xd2/0xdc draw nothing (their bolt is effect 100). Exact rect arithmetic: transcribe from FUN_004baaa0.

### 5.2 Hitboxes (FUN_005efd20; onHit FUN_005f01b0)
All `hitType 3` (hit dashing targets), stun 1.0, type 5. Collision rules: objects.md §4.4.

| Id | Spawned by | dmg | shape {hx,hy,r} | knock | +0x01 oncePerFrame | +0x48 rehit | onHit (runs before damage) |
|---|---|---|---|---|---|---|---|
| 100 | wires (age%4==0) | 40 | {10 (DAT_00813f4c), 80·α or 120·α, 0} | 0; **10 if own `+0x128 > 7`** | 1 | 0 | if victim state 1 && t < 11 → no damage; else 6 streaks 0x456 at k/3·π+rand_f(−0.25,0.25) (k=−3..2), effect 0x515, SE `(0x15,4)` |
| 0x6e | wires (other frames) | 0 | same | same rule | 1 | 0 | if victim state 1 && t < 11 → no damage (no FX) |
| 200 | thrown rock, every frame | 280 | {0,0,50} (DAT_008140ec) | 10 | 0 | 1 | if victim state 1 && speed > 2 → no damage; else 8 streaks (k/4·π+rand), shake `FUN_004d7ac0(10,0.5,1)`, SE `(0x2d,4)` |
| 300 | swirl in 0x73 | 240 | {0,0,50} | 7.5 (DAT_00814150) | 0 | 1 | anti-juggle (speed>2); **angle := atan2(victim − Tsih)**; 8 streaks; shake; SE 0x18 |
| 0x12d | swirl in 0x74 | 360 | {0,0,50} | 9 (DAT_00813f48); ver ≤ 0x5a0: 15 | 0 | 1 | as 300 |
| 0x12e | swirl in 0x75 | 480 | {0,0,50} | 22.5 (DAT_0081fa84) | 0 | 1 | anti-juggle; angle from Tsih; 8 streaks; effect 0xa8d; shake (10,0.75,1); SE 0x19 |
| 0x136 | launched swirl (timer2 < 50) | 420 (ver ≤ 0x5a0: 320) | {0,0,50} | 22.5 | 0 | 1 | anti-juggle; **angle := atan2(victim − hitbox pos)**; effect 0xa8d; shake; SE 0x19 |
| 400 | Accel rocks, every frame of fall/rise | 250 | {0,0,90} (DAT_00814170) | 5 | 0 | 0 | anti-juggle with speed > **3.6** (DAT_0081fa8c); 8 streaks; effect 0xaf0; SE `(0x2d,4)` |
"No damage" = callback sets `hb.dealsDamage(+0x00) = 0` (the box still exists this frame). Knockback direction = `hb.angle` (+0x28) after the callback.
Streak RNG order: angle rand_f drawn per streak, inside the k loop.

### 5.3 Enemy objects (FUN_0055f1e0 spawn, FUN_0055f6b0 update, FUN_00560760 draw, FUN_005605f0 onKilled)
Init FUN_0055ee70 memsets 0 (so `+0x70` speed = 0, `+0x58/+0x5c` = 0, `+0x2c` active = 0, `+0xe9/+0xea` = 0), layer `+0x1c = 1`.
Caller writes pos (+0x40/+0x44) after spawn. `+0x10` = spawn id (never changes), `+0x7c` = behaviour id, `+0x84` = phase,
`+0x80` = phase timer (++ after each update, objects.md §1.4). Common **grow** (phase 0): `scaleY(+0x64) −= 0.2`, `scaleX(+0x60) += 0.1`;
when scaleX ≥ 1: both = 1, phase 100, `+0x80 = 0`, active = 1 (10 updates; init scaleX 0, scaleY 3.0, scaleZ 1).
Common **shrink** (phase 900): `scaleY += 0.2`, `scaleX −= 0.1`, return 0 when scaleX ≤ 0.

| Spawn id | HP / maxHP | invulnerable | shape | +0x74 spin |
|---|---|---|---|---|
| 100 rock (also accepts 0x65 → spin −0.05) | 250 | 0 | {0,0,40} (DAT_008140e0) | 0.05 (DAT_00813ef4) |
| 200 swirl | 400 | 0 | {0,0,40} | — |
| 300 / 0x12d huge rock | 999 | **1** | {0,0,80} (DAT_00814164) | 0.05 / −0.05 (DAT_00814114) |
Being Enemies, rocks and the swirl are hit by the opponent's beams/hitboxes/missiles/lasers when active (objects.md §4.5).

**Rock (`+0x7c` = 100).** Phase 0 grow. Phase 100 (idle, active): if `+0x80 > 500` → phase 900, inactive.
**Throw `FUN_00782900(p)`**: every Enemy with `+0x10==100 && +0x7c==100 && +0x84==100 && type==5 && owner==p+4`:
phase 200, `+0x80 = 0`, `+0x5c = atan2(opp − rock)`, SE 0x20 per rock.
Phase 200 (ver ≥ 0x5a1): `speed += 0.35` (DAT_00813f2c; old 0.25); if `+0xea == 1`: if `+0xe9 == 0`: `+0x5c = atan2(Tsih − rock)`
(FUN_00780450 = **own player**), then since |dx|,|dy| < 3000 (DAT_008147d8 / DAT_00817e10, checked as floats at 0x55f880) `+0xe9 = 1`
— i.e. a rock thrown with B held at spawn time flies at Tsih's position instead of the opponent. Move along +0x5c; hitbox **200** at pos,
angle +0x5c; if ver > 0x58b && `+0x80 > 200` → phase 900, inactive. No arena check.
onKilled (HP < 1): effect 0x514 (colour slot 0x8859f4), Tsih effect **200** (variant = +0x88) at pos, SE 0x1d; removed.

**Swirl (`+0x7c` = 200).** Phase 0 grow / 900 shrink as above. While phase != 900, every frame, from the owner's state:
| Owner state | r | a |
|---|---|---|
| 0x73 | −80 (DAT_00817e00) | t<10 ? 0 : min((t−10)·π/9, 2π) (computed as ((t−10)/3·π)/3) |
| 0x74 | −120 (DAT_0081525c) | min(t·π/9, 2π) |
| 0x75 | −160 (DAT_00817e0c) | min(t·π/9, 2π) |
| 0x76 | → **launch**: `+0x7c = 0xd2`, `+0x80 = 0`, speed 20 (DAT_008140fc; old 15), `+0x5c = owner aim`, active 1; return |
| other | active 0, phase 900; return |
Position = rotate((cos a·r, sin a·r·0.1), owner aim) + owner pos (FUN_005f1210; DAT_00853b10 = 0.1 flattens the ellipse);
`+0x58 = owner aim`. If active: hitbox **300** (0x73) / **0x12d** (0x74) / **0x12e** (0x75) at pos, angle +0x58.
(The swing starts behind Tsih and sweeps to the front over 9 frames per π.)

**Launched swirl (`+0x7c` = 0xd2), phase 100:** if `+0x80 > 30`: speed ×0.95 (DAT_00814124); move along +0x5c; `+0x58 += 0.1` wrapped;
if `+0x80 < 50`: hitbox **0x136** at pos, angle +0x5c; at `+0x80 == 100`: inactive, `+0x58 = 0`, phase 900.
onKilled: effect 0x514, Tsih effect 200 (variant 0), SE 0x1d.

**Huge rock (`+0x7c` = 300, spawn 300/0x12d).** Phase 0 grow. Phase 100: `speed += 1.0`; move along +0x5c (−π/2 = down);
`+0x58 += +0x74` wrapped to (−π, π]; hitbox **400** at pos, angle −π/2; if `+0x80 > 29`: speed 0, phase 900, `+0x80 = 0`, SE 0x19.
Phase 900: if `+0x80 < 21` and even: `r = rand_f(−0.25, 0.25)` then `n = rand_i(0,100)%8`; common effect 0xa82+n, colour 0xd, at (x, y−50), angle r+π/2,
effect `+0x40 = 0.5`. If `+0x80 < 20`: `x += rand_f(−2,2)`, `y += rand_f(−2,2)` (DAT_008178e0, DAT_00813a4c). If `+0x80 > 20`: if `< 40` hitbox **400**
at pos, angle **+π/2**; `speed −= 0.25`, move along +0x5c (speed negative → rises). If `+0x80 > 60`: `+0x58 = 0`, scaleY +0.2, scaleX −0.1, inactive.
Return 0 when scaleX ≤ 0. (Fall: 30 frames, 465 px total; shake 20 frames; rise from frame 21.)

**Draw FUN_00560760** (sprite 0x7a `ts_rock`, bank = enemy `+0x18`; immediates at 0x5607f3/0x560cd2/0x5611c3):
pulse `g = 0.75 + 0.25·sin(((age(+0x78)%50)/50)·2π)`.
Rock 100: body frame `2·v` (v = +0x88), colour (a1,1,1,1) if scaleX ≥ 1 else **(a1, r0, g0, b0)** (black silhouette while growing),
pos, scale (scaleX, scaleY, 1), rotZ(+0x58), blend 1; glow frame `2v+1`, blend 2, colour (a g, 1,1,1) only when scaleX ≥ 1 (otherwise no `+0xdc` call: the glow model keeps the material it was last given — draw-only state; ported as a per-frame static).
Swirl 200/0xd2: same as rock 100 but always frames **0/1 of 0x7a** (`ts_rock`, immediate at 0x560cd2), same colour rules.
Huge rock 300: like rock 100 but scale ×2 (DAT_00813a4c) and body always white.

### 5.4 Lasers (visual wires only) — FUN_006308f0 / FUN_00630f70 / FUN_00630a80
Spawn: init FUN_006307d0 then `blockMode(+8) = 0, hitType(+0xc) = 0, damage(+0x40) = 0, knock(+0x44) = 0, stun(+0x4c) = 0`.
Every segment has Shape {0,0,0}; the `+0x0c` float of each segment is a draw alpha (k<5: 0; 5≤k<15: (k−5)/10; else 1).
Zero shapes never overlap in FUN_005f1c90 (the circle test needs r>0 on both, the rect tests halfW>0), so the wires never collide or get cut.
- **100 (B rocks)**: each update clears segments; owner = FUN_007805f0(+0x18), rock = FUN_0055f060(+0x1c); return 0 if either is gone,
  or rock phase 200 && rock `+0x80 > 30`, or rock phase 900. origin = owner pos; `+0x30 = atan2(rock − owner)`, `+0x44 = dist`;
  3 strands × 20 segments at angle, angle−0.05, angle+0.05: seg k at origin + dir·(len/20)·k.
- **300 (Accel rocks)**: rock gone, or rock phase 900 && `+0x80 > 60` → return 0. origin fixed (spawn point); angle/len to rock;
  every 5 ages common effect 0x546 (colour slot 0x8859f4) at origin, angle −π/2; 3 vertical strands at x = origin.x + 10 / +0 / −10,
  y = origin.y + sin(angle)·(len/20)·k (x is not cos-scaled — reproduce).
- **200**: only from dead state 0x71.
- onHit FUN_00630f50 is empty. Draw FUN_00630a80: per segment a line of length len/40 each side, alpha 255·segAlpha, colour derived from
  X = (int)(sin(((age+k)%10)/10·2π)·50+50) (stored in `DAT_009b1528`): R = 0xe6 − X/2, G = 0xeb − X/3, B = 0xff. Primitive: untextured
  1-pixel screen-space line `FUN_00457ae0` between `seg ± R(len/40, 0)` (begin `FUN_004576d0(1)`, end `FUN_00457980`).

### 5.5 Tsih effects (FUN_005142a0 / 00514b70 / 005143e0) — cosmetic, no RNG
- **100** (bendy-beam bolt): scale 1, alpha 1, rotation = angle; age > 5: alpha −0.05; dies at ≤ 0 (~age 25). Drawn from beam textures
  `DAT_0087fdf4` / `DAT_0087fdf0` (beamemitb / beamemit).
- **200** (rock shatter): alpha −0.1, scale +0.1 per frame (10 frames); sprite 0x7a (`ts_rock`) frame `2·variant` (`DAT_00895c48 = 0x7a`, `DAT_00895c4c = variant<<1`), src (0,0)-(255,255), half 48·scale (DAT_008140e4), alpha 255·a, blend 1, bank = team's player. Effect 100's age<2 layer uses cell `DAT_00895c34`, which is only read (refs 0x51461d–0x514661) → always 0.
Common effects used: 0xfa, 0x456, 0x514, 0x515, 0x516, 0x546, 0xa28+n, 0xa82+n, 0xa8c, 0xa8d, 0xaf0, 1000 (cosmetic; RNG only where noted).

---

## 6. Sound ids used (objects.md §5.1 table)
| id | file | where |
|---|---|---|
| 6 | up01.wav | hyper start |
| 7 | high_a.wav | Accel start |
| 0xf | laser02.wav | Bendy Beam, D+A, H, S+A, D+H shots |
| 0x10 | laser03.wav | Beam Ball ring |
| 0x13 | don16.wav | beam hits (lock 3) |
| 0x15 | crash17.wav | wire hits (lock 4) |
| 0x18 | hit.wav | swirl 1/2 hits |
| 0x19 | hit_f.wav | swirl 3 / launched swirl hits, huge-rock landing, proximity knock |
| 0x1d | gun12.wav | rock/swirl destroyed |
| 0x1f | swing.wav | swirl release (→0x76) |
| 0x20 | swing_h.wav | swirl dashes, each rock thrown |
| 0x2b | tm2_sonic004.wav | stealth on |
| 0x2c | tm2_sonic005.wav | stealth off |
| 0x2d | rock.wav | rock hitbox 200 / huge-rock 400 hits (lock 4) |
| 0x2f | power35.wav | rock spawn, swirl spawn |
(0x24 swing_h only in dead state 0x71.)

---

## 7. Other per-character table entries (engine index 5)
| Table | Entry | Role |
|---|---|---|
| 0x881310+0x14 = 0x881324 | FUN_00786e70 | input dispatch (§2) |
| 0x881360+0x14 = 0x881374 | FUN_00787360 | alternate-scheme dispatch |
| 0x8813b0+0x14 = 0x8813c4 | FUN_00780f20 | sprite loader (§4.1) |
| 0x881400+0x50 (+4·level) = 0x881450/54/58 | FUN_00787fe0 (level 0), FUN_00787850 (levels 1,2) | CPU AI (button word); both read the stealth flag (all.c:442484, 442642). Not reversed. |
| families | §5 | |

---

## 8. Stats record (index 5, base 0x882dcc), written by FUN_0049ae40 (registry.c:1433-1465)
| Off | Value | Meaning |
|---|---|---|
| +0x00 | 6.0 (DAT_00813f44) | walk speed per axis per frame |
| +0x04 | 12.0 (DAT_00813f54) | dash speed |
| +0x08 | 11.0 (DAT_00813f50) | dash hold speed (speed while Dash is held in state 2, core.md) |
| +0x0c..+0x14 | 10, 8, 6 | colour palette 0 (objects.md §5.2) |
| +0x18..+0x20 | 3, 7, 11 | colour palette 1 |
| +0x3c/+0x40/+0x44 | 0x62, 99, 100 | sprite ids (rng / unregistered / system) |
| +0x48/+0x60 | text ids 0x2c/0x2d | colour-variant folders |
| +0xa8/+0x1a8 | `mdl\win_font_TS.lfd/.dat` | |
| +0x2a8 | `ts_res_bgp` | |
| +0x2c0/+0x2c4/+0x2c8 | 30, −30, 0.5 (float, DAT_00853b1c) | UNVERIFIED (portrait offset/scale?) |
| +0x2cc/+0x2d0/+0x2d4 | 130, 130, 220 | colour (RGB) |
| +0x2d8 | 0 | |
No Tsih entries in the version patch FUN_004a7270.

---

## 9. UNVERIFIED / open items
Resolved in the verify pass: stealth cleared per round (FUN_0069cc40 memset), glow colour rule, zero-shape
wires, wire line primitive/colour, effect 200 sprite, effect-100 cell static, 0x136 bar/orb constants,
stats +0x08 = dash hold speed (core.md; `st_dash` uses it).
- `DAT_00894382` meaning (display option toggled from a menu; only affects the stealth tint).
- Stats +0x2c0..+0x2c8 (30, −30, 0.5) — UI only.
- AI functions FUN_00787fe0 / FUN_00787850 not reversed in this doc.
- Behaviour tests: `tests/test_tsih.c` (run by tests/run_tests.sh).
