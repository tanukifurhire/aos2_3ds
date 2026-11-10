# Hime — complete moveset spec (handbook id 13, engine index 16 = 0x10)

Source: `aos2.exe` (Ghidra corpus `tools/re/dec/all.c`, constants read with `xref.exe rd`). Structure
follows `suguri.md`; shared helpers (`FUN_007807f0` cost table, `FUN_006b1b40` hyper start,
`FUN_00780910` hyper spend, `FUN_006b17d0` dash/dash-cancel, `FUN_00612830/00612900` training-only
frame data, `+0x158` hit-confirm, ammo `+0xe8/+0xec`) are used exactly as described there and are not
re-derived. `t` = player timer `p+0x154`. Implement the **ver ≥ 0x5a1** columns (objects.md §0.1:
live matches run with `DAT_0089426c = 0x5a2`).

---

## 0. Notes for the lead

1. **Hime has no phase mechanic.** Nothing in her code (states 0x1f0–0x207, loader, input fns, object
   families of type 0x10, AI, common code) branches on a "phase". The command list (`COMMANDLIST_HIME_*`,
   12 strings; pause list `FUN_006c3eb0`, text ids 0x34a–0x355) has exactly 12 entries, all covered
   here. The "(Phase 1)" in the manual is a page heading only. **The character that really has two
   phases is Sumika's second form** (story-mode final boss, see sumika.md §B): at HP ≤ maxHP/3 it
   switches into a permanent bullet-hell state.
2. **Zero-damage hitboxes still stun.** `FUN_00780ab0` always does `+0x144 += stun` and returns
   `+0x144 >= 1.0` even for 0 damage, so a 0-damage hitbox with stun 1.0 re-applies hitstun (knockback
   = its `+0x40`, often 0) every frame it touches. Hime and Sumika build their "binding" moves on this
   (chains, God Knows/Angel Wing hitbox 0x137, odd laser frames). Port the damage fn literally.
3. **Global (not per-player) state:** Paradise of Faint Pain stores its "ammo paid" flag in the global
   byte `DAT_0086c23c` (FUN_006c6450). In a Hime mirror both players share it. Reproduce.
4. **Graze zones owned by objects.** Several Hime/Sumika projectiles spawn common effect **id 100**
   (the dash graze zone, objects.md §4.7) with owner = *missile serial* `m+0x20`, and Sadism (0x201)
   spawns zones owned by Hime at each hit point. What the graze pass does with an owner id that is not
   a player is UNVERIFIED (objects.md owner: check `FUN_0060bf30` lookup of zone owner).
5. Player init `FUN_0069cc40` has per-character overrides of the initial state `p+0x150`
   (Saki 8 → 0xd2, Nanako 0xd → 0x197, Sumika 0x11 → 0x21d, Sumika-2nd 0x12 → 0x241) and HP
   (`+0xd0/+0xd4/+0xdc` default 9000, 4500 when `DAT_0087fad4[i] < 6`; Sumika-2nd 12000/15000/18000).
   Hime has no override (core.md should own this table).
6. objects.md §4.5 calls `FUN_0066d190` "Sumika type 15" — type 15 is engine index 15 = **Kyoko**.
   Absorb table `0x881b48` entries for 16/17/18 are 0.

---

## 1. Shared mechanics

### 1.1 Player fields used
Same as suguri.md. Additionally read: `+0x24/+0x28/+0x1c/+0x20` (half-speed walking in several hypers),
`+0xec` ammo B is *drained by the Fever laser object* (§5.3), opponent `+0xe0` heat (Leash), opponent
`+0xa9`, `+0xf8`, `+0xa2` via `FUN_00780a50`.

`FUN_00780a50(opp)` = `opp+0xf8 == 0 && opp state != 6 && opp+0xa2 != 0 && opp+0xa9 != 1 &&
DAT_00885964 != 2` ("can be affected").
`FUN_006b1d70(p, range, speed)` = proximity knock (objects.md §4.6), SE 0x19.
`FUN_00780450(team)` = player of that team (= the owner for Hime's own objects).

### 1.2 Cost table (`0x87c4f0 + id*0xc`, registry.c:2203-2241; ver patch FUN_004a7270)
| State | Move | costA | costB | hyperGain |
|---|---|---|---|---|
| 0x1fa | Faint Breeze (per missile) | 0.1 (DAT_00853b10) | 0 | 0.02 (DAT_00813ed8) |
| 0x1fb | Needle Fruit | 0.7 (DAT_00813f38) | 0 | 0.1 |
| 0x1fc | Faint Breeze Plenty (per missile) | 0.075 (DAT_00813efc) | 0 | 0.015 (DAT_00813ed4) |
| 0x1fd | Paradise of Faint Pain | 0 | 0.06 (DAT_00813ef8); **ver>0x5a0: 0.5** (DAT_00853b1c) | 0.012; **ver>0x5a0: 0.102** (DAT_00813f10) |
| 0x1fe | Fever | 0 | 0.2 (DAT_00853b14) | 0.05 (DAT_00813ef4) |
| 0x1ff | Delicate Binding | 0 | 0 | 0.05 |
| 0x200 | (DB follow-up, per missile) | 0 | 0 | 0.02 |
| 0x201 | Sadism (per hit) | 0 | 0 | 0.02 |
| 0x202–0x206 | hypers | 0 | 0 | 0 (never called) |

### 1.3 Hyper flourish (t=0 of 0x202–0x206)
`FUN_006b1b40(p,10)`; SE 6; `FUN_004e9280(100, 0.75)` (DAT_00813f3c); `FUN_007bec20(100, pos, A, B)`
with (A,B) = (aim, π/4 DAT_0081fa90) for 0x202/0x205, (0,0) for 0x203/0x204, (facing,0) for 0x206;
`FUN_004d7ac0(20, 0.5, 2)`; common effect 0xfa at pos. God Knows (0x207): `FUN_006b1b40(p,0x14)`, SE 7,
`FUN_004e9280(150, 0.85)` (DAT_00814120), `FUN_007bec20(100,pos,facing,0)`, `FUN_004d7ac0(150, 0.6, 2)`
(DAT_00813f34), effect 0xfa, speed 0.
"Face by x" (0x206, 0x207): `+0x84 = +0x88 = (opp.x <= x) ? π : 0`.

---

## 2. Input dispatch `FUN_006c85d0` (standard) / `FUN_006c8ad0` (alternate)

**A. Hypers** (only if `FUN_00780950(p)`), first match wins, each needs `FUN_00780910(p,cost)`:
| # | Standard condition | Alternate (`H held` + …) | Cost | State | Move |
|---|---|---|---|---|---|
| 1 | S held, B held, H pressed | Shield pressed | 3.0 (DAT_00813a50) | 0x207 | **God Knows** (Accel) |
| 2 | S held, H pressed | S pressed | 1.0 | 0x206 | **Untouchable** (S+H) |
| 3 | B held, H pressed | B pressed | 1.0 | dist ≥ 240 (DAT_00815584) → **0x205** else **0x204** | **Life-long Burning** (far / close) |
| 4 | Dash held, H pressed | Dash pressed | 1.0 | 0x203 | **Leash** (D+H) |
| 5 | H pressed | A pressed | 1.0 | 0x202 | **Amber** (H) |

**B. Normals** (only in state 0 or 2), identical in both schemes:
| # | Condition | Result |
|---|---|---|
| 1 | Dash pressed | `FUN_006b17d0(p,0)` |
| 2 | Shield pressed && hyper ≥ 0.5 && `FUN_007809e0` | `FUN_006b1930(p,0)` |
| 3 | S held && B pressed | 0x201 **Sadism** |
| 4 | S held && A pressed | 0x1ff **Delicate Binding** |
| 5 | B pressed | 0x1fe (→ 0x1fd if released before t=11) **Fever** / **Paradise of Faint Pain** |
| 6 | Dash held (>1) && A pressed | 0x1fc **Faint Breeze (Plenty)** |
| 7 | A pressed | 0x1fb (→ 0x1fa if released before t=12) **Needle Fruit** / **Faint Breeze** |

State entry = `state=id, t=0, flag158=0, T2(p)`; returns `T2[state](p)`. Note row 6 tests `+0x34 > 1`
(Suguri tests `> 0`).

Not in the manual: Delicate Binding hit-confirm → 0x200 (A) or Sadism (S+B); 0x200 hit-confirm →
Fever (B) or Sadism (S+B).

---

## 3. States

### 0x1f0 — neutral (T2 FUN_006c48d0)
Identical to Suguri 0x128: ±`stats+0` (5.0) per held axis (no normalisation); `+0x84 = facing ? 0 : π`.
0x1f1–0x1f4: T2 stubs returning 1 (FUN_006c49c0/49f0/49d0/49e0); only their T1s are used.

### 0x1fb — A press / **Needle Fruit** (T2 FUN_006c4c90)
| t | Action |
|---|---|
| 0 | aim |
| < 12 and A not held (`+0x38 < 1`) | state = 0x1fa (timer kept), run it, return |
| 15 | if cost(0x1fb): re-aim; for i = −12..11: a = (i/12)·π (DAT_00813f54=12); pos + (cos a, sin a)·120 (DAT_00814174) → missile **0x6e** (i even) / **0xa0** (i odd), angle a; then SE 0x10 once. (Both version branches identical.) |
| ≥ 30 | dash-cancel (612900 at 30) |
| > 50 | state 0 |

### 0x1fa — **Faint Breeze** (T2 FUN_006c4a00) — via 0x1fb early release
| t | Action |
|---|---|
| 12 | if cost(0x1fa): missile **100** at pos + rot((−10, rand_f(60,120)), aim) (DAT_00817c10, DAT_00813a58, DAT_00814174), angle aim, SE 0x10 |
| 12 | (second block) if cost(0x1fa): missile **0x96** at pos + rot((−10, −rand_f(60,120)), aim), angle aim, SE 0x10 |
| ≥ 15 | dash-cancel |
| > 30 | state 0 |
No re-aim (uses the aim from 0x1fb t=0).

### 0x1fc — **Faint Breeze (Plenty)** (T2 FUN_006c5020) — D+A
| t | Action |
|---|---|
| 0 | aim |
| 10,14,18,22,26,30 ((t−10)%4==0, 9<t<34) | if cost(0x1fc): `r = rand_f(60,160)` (DAT_00814168), `a = rand_f(−π,π)`, point = pos + rot((r,0), a); re-aim; missile **0x69** if ((t−10)/4) even else **0x9b**, angle aim; SE 0x10 |
| every frame (not cancelled) | friction `+0x9c *= 0.5` |
| ≥ 35 | dash-cancel; > 50 state 0 |
RNG order per shot: r, then a.

### 0x1fe — B press / **Fever** (T2 FUN_006c6080)
| t | Action |
|---|---|
| 0 | aim |
| < 11 and B not held (`+0x3c < 1`) | state = 0x1fd (timer kept), run it |
| 10 | if cost(0x1fe): laser **100** at pos, aim (`DAT_00881c78`); SE 0x12 |
| ≥ 50 | dash-cancel; > 60 state 0 |

### 0x1fd — **Paradise of Faint Pain** (T2 FUN_006c6450) — via 0x1fe early release
ver ≥ 0x5a1:
- t=12: `DAT_0086c23c = FUN_007807f0(p,0x1fd)` (global, see §0.3).
- 12 ≤ t ≤ 28 and flag: offset (x,y) by t, y negated when t odd, rotated by aim, + pos; spawn missile
  per jump table @0x6c6af0 (verified in disassembly); SE `FUN_007bcaf0(0xe,4)` every one of these frames:

| t | offset (x, ±y) | missile |
|---|---|---|
| 12 | (0, 0) | 0xfa |
| 13/14 | (−18.75, 157.5) DAT_00825488/84 | 200 |
| 15/16 | (−37.5, 22.5) DAT_0082548c/0081fa84 | 0xfa |
| 17/18 | (−56.25, 135) | **none** |
| 19/20 | (−75, 45) DAT_00825494/0081ce48 | 200 |
| 21/22 | (−93.75, 112.5) | 200 |
| 23/24 | (−112.5, 67.5) | 200 |
| 25/26 | (−131.25, 90) | 200 |
| 27/28 | (−150, 85) | **none** |
(missile angle = aim). 13 missiles total.

ver ≤ 0x5a0: even t in 12..28 each with its own cost check: offset (−rand_f(0,150), rand_f(−180,180))
rotated by aim; 200 if (t/2) even else 0xfa.

Both: ≥ 30 dash-cancel; > 50 state 0.

### 0x1ff — **Delicate Binding** (T2 FUN_006c52b0) — S+A
| t | Action |
|---|---|
| 0 | aim |
| 5 | if no own missile 600/0x259 exists (`FUN_006c3e20(team)`: scans missiles with team, type 0x10, id 600 or 0x259) **and** cost(0x1ff): speed 0; point = pos + aim·30 (DAT_008140e8); Hime effect 0x1fe at point; missile **600** at point, angle aim. Otherwise **t := 20**. |
| > 5, flag158, S held, B pressed | → **0x201** (612830) |
| > 5, flag158, A pressed | → **0x200** |
| ≥ 40 | dash-cancel; > 80 state 0 |
| every frame (no cancel/transition) | friction `+0x9c *= 1.5` (DAT_00814110) |
flag158 is set by hitbox 100's on-hit while the owner is in 0x1ff (§5.2).

### 0x200 — Delicate Binding follow-up (T2 FUN_006c55e0)
| t | Action |
|---|---|
| 0 | aim |
| 5,10,…,30 (4<t<31, t%5==0) | if cost(0x200): speed 0; x = rand_f(−80,−40) (DAT_00817e00, DAT_00817dc0), y = rand_f(20,160) (DAT_008140fc, DAT_00814168), y = −y when (t/5) even; rotate by aim, + pos; a = atan2(opp − point); Hime effect 0x1fe; missile **0x262** at point, angle a |
| > 5, flag158, S held, B pressed | → 0x201 |
| > 5, flag158, B pressed | → 0x1fe |
| ≥ 70 | dash-cancel; > 100 state 0 |
flag158 set by hitbox 0x6e on-hit while owner is in 0x200.

### 0x201 — **Sadism** (T2 FUN_006c72b0) — S+B
`last` = 21 (ver ≤ 0x5a0) / **24**; dash-cancel frame 30 / **35** (ver > 0x58a); end > 40 / **45**.
| t | Action |
|---|---|
| 0 | aim |
| 12,15,18,21,24 (11<t≤last, t%3==0) | if cost(0x201): speed 0; rand x = rand_f(70,100) (DAT_008145ec, DAT_00853b90), y = rand_f(−60,60) (both drawn always); ver>0x5a0: x = 85 (DAT_00824f98), y = {12:0, 15:20, 18:40, 21:−20, 24 (default):−40}. point = pos + rot((x,y),aim). If t == last: tgt = opp.pos − aim·100 (DAT_00853bb8); if dist(tgt, pos) ≤ 200 (DAT_008147cc) point = tgt. Hime effect 200 ((t−12)%6==0) else 0xfa at point; **common effect 100 (graze zone, owner = Hime)** at point, angle aim; hitbox **200** ((t−12)%6==0) else **0xfa** at point, angle aim; if t == last: that hitbox's knockback := 20 (DAT_008140fc). SE `FUN_007bcaf0(0xb,3)` |
| ≥ cancel frame | dash-cancel; > end → state 0 |

### 0x202 — **Amber** (T2 FUN_006c61c0) — H
t=0 aim + flourish. t=10: laser **0x96** at pos, aim; SE 0x12. 10<t<60: walk at `stats+0 × 0.5` = 2.5/axis.
≥ 60 dash-cancel; > 80 state 0.

### 0x203 — **Leash** (T2 FUN_006c5970) — D+H
| t | Action |
|---|---|
| 0 | aim, flourish |
| 25 | re-aim; three centres c_j = pos + (cos,sin)(aim + δ_j)·120, δ = 0, +2π/3, −2π/3 (DAT_00817898); for each, k = 0..4: angle = aim + δ_j + (k/5)·2π; Hime effect 500 + missile **500** at c_j with that angle (15 chains; 612830 each) |
| 10,20,30,40 | Hime effect 0x230 at pos, angle rand_f(−π,π) (RNG) |
| 25..40 | if dist < 320 (DAT_008178c0) and `FUN_00780a50(opp)`: opp heat `+0xe0 += 0.1`, SE `FUN_007bcaf0(0x26,4)` |
| 60 | `FUN_006b1d70(p, 240, 20)`; common effect 0xa8c |
| ≥ 60 dash-cancel; > 80 state 0 |
While Hime is in 0x203 the chain hitboxes of all her 500/0x1f5/0x259 missiles deal **double damage** (§5.1).

### 0x204 — **Life-long Burning (close)** (T2 FUN_006c6ee0) — B+H, dist < 240
| t | Action |
|---|---|
| 0 | aim, flourish |
| 6,8,…,46 (even, 5<t<47) | a = rand_f(−π,π), r = rand_f(100,240); point = opp.pos + rot((r,0),a); missile **0xca** if (t/2) even else **0xfc**, angle a+π (toward opponent); SE `FUN_007bcaf0(0xe,4)` (21 missiles) |
| t%5==0, t<60 | Hime effect 300 at pos + aim·(−100) |
| ≥ 80 dash-cancel (**never reached**); > 60 state 0 |

### 0x205 — **Life-long Burning (far)** (T2 FUN_006c6b10) — dist ≥ 240
| t | Action |
|---|---|
| 0 | aim, flourish |
| 5..45 **every frame** (the `t & 0x80000000` test is always 0) | x = −rand_f(−50,100) (DAT_008149e4), y = rand_f(−200,200) (DAT_00814600, DAT_008147cc); point = pos + rot((x,y),aim); missile **0xc9** (t even) / **0xfb** (t odd), angle aim; SE (0xe,4) (41 missiles) |
| t%5==0, t<60 | Hime effect 300 at pos + aim·(−150) (DAT_008149ec) |
| ≥ 60 dash-cancel; > 80 state 0 |

### 0x206 — **Untouchable** (T2 FUN_006c77c0) — S+H
t=0: face by x; flourish; for i = −3..2: Hime enemy **100** (i even) with `+0x74 = 0.1` or **0x96** (i odd)
with `+0x74 = −0.1` (DAT_00814130); `+0x5c = (i/3)·π` (6 wings at −π, −2π/3 … 2π/3).
10<t<80: walk at 2.5/axis. Dash-cancel ≥ 90 (ver ≤ 0x5a0: 100); end > 110 (120).

### 0x207 — **God Knows** (T2 FUN_006c7b80) — S+B+H, Accel
| t | Action |
|---|---|
| 0 | face by x, speed 0, Accel flourish (§1.3) |
| 6..60 even | t%4==0: Hime effects 700 + 0x2ef; else 0x2bd + 0x2ee; at pos, angle −π/2 |
| 10..50, t%5==0 | hitbox **0x136** (t%10==0) / **0x168** at (x+210, y−30) and (x−210, y−30) (DAT_008178d8, DAT_008140e8), angle 0 (612830) |
| 10..50 other frames | hitbox **0x137** (0 damage, binds) at the same two points |
| 4..72, t%3==0 | spark 0xa82 + (rand_i(0,100) & 7) at (x, y−150), angle rand_f(−0.25,0.25)+π/2, colour slot 0x8859f4 (t%6==0) / 0x8859f8; spark `+0x40 = 0.5` (RNG) |
| 9..75 even | particle `FUN_0068b8a0(1000, …)` at (x + cos a·r, y + sin a·r·0.25 − 100), a = rand_f(−π,π), r = rand_f(50,200) |
| 10..70 every frame | particle `FUN_0068b8a0(0x44c, …)` same formula (RNG) |
| 65 | `FUN_006b1d70(p, 360, 20)` (DAT_00817894); effect 0xa8c |
| ≥ 100 dash-cancel; > 120 state 0 |

---

## 4. Animation

### 4.1 Sprites (loader `FUN_006c1340`)
Variant 0/1/2 → folder string ids 9/10/11 (charaHimeA/B/C). Same quad construction as Suguri.
| id | pattern | frames | S | used by |
|---|---|---|---|---|
| 0x1f0 | hm_std0%d | 3 | 96 (DAT_00814118) | neutral |
| 0x1f5 | hm_fwd0%d | 3 | 96 | neutral |
| 0x1f6 | hm_bck0%d | 3 | 96 | neutral |
| 0x1f2 | hm_los0%d | 6 | 96 | KO, hit |
| 0x1f4 | hm_sld0%d | 5 | 96 | shield, A moves, 0x200, 0x204 |
| 0x1f1 | hm_dsh0%d | 9 | 96 | dash |
| 0x1ff | hm_atka0%d | 7 | 96 | B moves, 0x1ff, 0x202, 0x203, 0x205 |
| 0x201 | hm_atkb0%d | 6 | 96 | 0x201 |
| 0x206 | hm_atkc0%d | 5 | 96 | 0x206, 0x207 |
| 0x1f7 | hm_rng0%d | 2 | 512 | stats +0x3c |
| 0x1f9 | hm_system | 1 | 512 | stats +0x44 |
| 0x208 / 0x209 | hm_lncer%d / hm_lnceb%d | 6 | 128 (DAT_00814134) | red / blue lance missiles |
| 0x20a / 0x20b | hm_swordr%d / hm_swordb%d | 6 | 160 | missiles 200 / 0xfa |
| 0x20c / 0x20d | hm_wingr / hm_wingb | 1 | 320 (DAT_008178c0) | Untouchable wings (effects) |
| 0x20e | hm_chain0%d | 5 | 72 (DAT_008178ac) | chain missiles |
| 0x20f | hm_aura%d | 2 | 128 | effects |
| 0x210 | hm_laser | 1 | 128 | lasers |
All 81 PNGs of `charaHimeA` are covered.

### 4.2 Common states → Hime T1 (from FUN_006b07a0…6b1580 `case 0x10`)
0 → 0x1f0, 1 → 0x1f3, 2 → 0x1f1, 3/4 → 0x1f4, 5 → 0x1f3, 6 → 0x1f2.

### 4.3 Per-state frames (`pp(n)` = (t/n)&3, 3→1)
Transforms: neutral = Suguri neutral (no rotZ, rotY 180° if facing); every other state = Suguri's AIM
transform (rotZ(aim) [+rotX 180° if |aim| ≥ π/2] + rotY 180°). Sprite ids from disassembly immediates.
| State | T1 | sprite | frame |
|---|---|---|---|
| 0x1f0 | FUN_006ca1e0 | 0x1f0, overridden by input exactly as Suguri (y+→0x1f5, y−→0x1f6, facing0&x−→0x1f5, facing1&x−→0x1f6, facing0&x+→0x1f6, facing1&x+→0x1f5) | pp(5) |
| 0x1f1 | FUN_006ca480 | 0x1f1 | (t/2)%6; moving against facing → pp(3)+6 |
| 0x1f2 | FUN_006cae30 | 0x1f2 | pp(3) + 3 if `DAT_008858f8` even; **not drawn when t ≥ 100** |
| 0x1f3 | FUN_006ca8a0 | 0x1f2 | pp(3) |
| 0x1f4, 0x1fa, 0x1fb, 0x1fc, 0x200, 0x204 | 6cab70, 6cb130, 6cb3f0, 6cb6b0, 6cbcf0, 6ccd70 | 0x1f4 | min(t/4, 4) |
| 0x1fd, 0x1fe, 0x1ff | 6cc7f0, 6cc270, 6cba30 | 0x1ff | min(t/3, 6) |
| 0x202, 0x203, 0x205 | 6cc530, 6cbfb0, 6ccab0 | 0x1ff | min(t/4, 6) |
| 0x201 | FUN_006cd030 | 0x201 | min(t/4, 5) |
| 0x206, 0x207 | 6cd2f0, 6cd5d0 | 0x206 | f = t/4; f>1 → f = ((f−2)&3)+2; f==5 → 3 |
Afterimage `FUN_004d7e00` only in 0x1f0, 0x1f1, 0x1fc. Global tint, blend 1 throughout.

---

## 5. Spawned objects (all type 0x10, character-specific)

Table entries (base + 4·16): beam 0x8818b8 FUN_004ab9a0 (upd 4abbb0, draw 4abaa0, hit 4abac0);
missile 0x8819f8 FUN_00662fb0 (upd 6647a0, draw 663610, onHit 6643d0, onKilled 664780);
hitbox 0x881bd8 FUN_005e1690 (onHit 5e1b40); laser 0x881c78 FUN_0061d050 (upd 61e030, draw 61d8d0,
onHit 61e010 = no-op); enemy 0x881598 FUN_0051e8c0 (draw 51efd0, upd 51eb10, killed 51efb0);
effect 0x8816d8 FUN_004f4ea0 (upd 4f70c0, draw 4f55f0). No whip, no absorb.
**Beam 100** (`FUN_004ab9a0`: speed 16, {24,8}, dmg 120, knock 10; update returns 1 without moving) is
never spawned by Hime.

### 5.1 Missiles (`FUN_00662fb0` template, init FUN_00685bb0)
Common: hp 60, `+0x5c` grow 0 (except chains), **shapeProj (+0x90) never set → cannot be shot down or
collide with other missiles**. All onHit return false → destroyed on hit; onKilled (FUN_00664780) does
nothing and returns false. onHit visuals: 0x9c4 + spark `FUN_007bd5e0` + SE 0x15 (`crash17`).
"red" ids use colour slot 0x8859f8 and **hitType 1** (heat-scaled damage); "blue" ids slot 0x8859f4 and
**hitType 2**.

| id(s) | spawned by | speed | shape body | dmg | knock | hitType | update |
|---|---|---|---|---|---|---|---|
| 100 / 0x69 (red), 0x96 / 0x9b (blue) | 0x1fa, 0x1fc | 0 | r 16 | 120 | 10 | 1 / 2 | **A** |
| 0x6e/0x6f/0x70 (red), 0xa0/0xa1/0xa2 (blue) | 0x1fb (0x6e, 0xa0 only) | 1.0, turn `+0x74` = +0.075 / −0.075 | r 16 | 60 (120 from age 60) | 10 | 1 / 2 | **B** |
| 200, 0xc9→200, 0xca→200 (red); 0xfa, 0xfb→0xfa, 0xfc→0xfa (blue) | 0x1fd, 0x204, 0x205 | −8 (DAT_0081410c) | rect {30,16} | 0xca/0xfc: 60; others: 75 (ver ≤ 0x5a0: 120) | 200/0xfa: 10; others 5 | 1 / 2 | **C** |
| 500, 600 (also 0x1fe, 0x262) | 0x203 (500), 0x1ff (600), 0x200 (0x262) | 32 (DAT_008140dc) | none | 0 | — | 2 | **D** |
(The id stored after spawn of 0xc9/0xca is rewritten to 200, 0xfb/0xfc to 0xfa; only damage/knock differ.)

**A (100/0x69/0x96/0x9b)**: grow += 0.1 to 1. Age 20: heading = atan2(opp − pos); if id%10 == 5
(0x69, 0x9b) heading += rand_f(−0.2, 0.2); speed 17 (`_DAT_00813f5c`); active. Age 10: common effect 100
(graze zone, owner = serial). Ages 20–24: dash spark 0xa28+rand%8 (RNG). Move. Age > 20 even: Hime effect
(id − id%10) trail. Age ≥ 31 and outside arena+10 → onKilled, removed. (Stationary for 20 frames, then
fired at the opponent.)

**B (ring)**: grow += 0.1 to 1; if speed < 18 (DAT_00814144) speed += 0.2; if speed < 14: heading +=
turn (wrap > π only), draw angle = heading. Age 10: active. Age 60: damage 120. Even ages: Hime effect
id−10. Age 70: 4 sparks (RNG), SE (0xe,5). Move. Age > 100 and outside arena+10 → onKilled, removed.

**C (swords)**: age < 40: grow += 0.2; when grow > 1 → 1 and active (float32 accumulation makes this
age 5). Speed: if < 20, += 1. Age 5: common effect 100 (owner = serial). Move. Age 40: inactive;
age > 39: grow −= 0.1; age 50 → removed.

**D (chains)** — the hit test is not the missile itself but hitboxes between Hime and the tip:
- 500 / 600 (extending): effect 0x259 (500) / 0x263 (600) at age 0 else 600 / 0x262; move 32/frame;
  Hime effect 0x23a. If age > 10: id 500 → **0x1f5**, 600 → **0x259**, age = 0, return. Else hitbox
  **100** at the midpoint of tip and owner, angle = wrap(heading + π), `halfW = age·16 + 40`, `halfH = 15`
  (DAT_00853b88); damage ×2 (`<<1`) if owner state == 0x203; damage 0 if age%5 ≠ 0; knockback 0 if
  dist(owner, opp) ≤ 120 (ver ≤ 0x5a0: 180). SE (0x28,3) (`chain.wav`).
- 0x1f5 / 0x259 (holding, not moving): age%3==0 (0x259 only while age < 21): hitbox **100** at midpoint,
  `halfW = 200` fixed, `halfH = 15`, ×2 in 0x203, knock 0 if close (as above). 0x1f5 dies at age 20,
  0x259 at age 120 (particle 0x44c + onHit, removed). 0x259's 120-frame life is what blocks re-use of
  Delicate Binding (§3 0x1ff).
- 0x1fe / 0x262 (extending, from 0x200): same movement/effects; at ages 6 and 9: hitbox **0x6e** at
  tip − dir·100, angle heading+π, `halfW 200, halfH 15`, knockback 0. Age > 10 → 0x1ff / 0x263.
- 0x1ff / 0x263: ages 0,3,6,9: hitbox **100** at tip − dir·100, {200,15}, **damage 0, knock 0** (pure
  binding). 0x1ff dies at age 20, 0x263 at 120.

### 5.2 Hitboxes (`FUN_005e1690`, on-hit `FUN_005e1b40`) — all hitType 3, stun 1.0
| id | dmg | shape {hx,hy,r} | knock | on-hit |
|---|---|---|---|---|
| 100 (chains) | 25 (shape/dmg patched by caller) | caller | 10 | if dmg ≠ 0: if victim state 1 && victim t < 2 → **dealsDamage := 0** (no double hit); else effects 0x9c4/0x515, **owner flag158 = 1 if owner in 0x1ff**, SE (0x15,3) |
| 0x6e (0x200 chains) | 20 | caller | 10 (0 set by caller) | if dmg ≠ 0: effects, flag158 if owner in 0x200, SE (0x15,3) |
| 0x96 | 40 | {0,0,0}? (not set) | 0 | — (unused) |
| 200 / 0xfa (Sadism) | 80 | {150, 36} (DAT_0081414c, DAT_008140f4) | 5 (last hit 20) | effects 0x9c4/0x514, SE (0x16,3) |
| 300 / 0x15e (wings) | 20 | {140, 36} (DAT_00817004) | 2.5 (patched) | if dmg ≠ 0: 3× 0x456 fan, sparks, SE (0x16,3) |
| 0x136 / 0x168 (God Knows) | 210 (ver ≤ 0x5a0: 170) | r 200 | 0 | 0x136 **falls through into 0x168** (both effect sets, 2 SEs, extra RNG) |
| 0x137 | 0 | r 200 | 0 | — |
| 400 / 0x191 (Fever) | 50 | {580, 40} (DAT_0081fa6c, DAT_008140e0) | 10 | if dmg ≠ 0: fan, sparks, SE (0x13,3) |
| 0x1c2 / 0x1c3 (Amber) | 40 | {580, 75} (DAT_00815254) | 10 | same |

### 5.3 Lasers (`FUN_0061d050`; blockMode 1, hitType 3, damage 0, knock 10, stun 1; age pre-incremented)
Neither laser has segments (`FUN_004a9f10` clears them): all damage comes from hitboxes it spawns.
Origin is re-copied from the owner every frame; angle fixed at spawn.
- **100 (Fever)**: ages 10..20: owner ammo B `-= 0.05` (DAT_00813ef4) — if ≤ 0, age := 21 (ends early;
  B may go negative); then (if still in range) hitbox **400 + ((age/2)&1)** at origin + dir·600
  (DAT_008149b8), angle; odd ages: damage 0 (still stuns). Removed after age 39.
- **0x96 (Amber)**: ages 10..40: hitbox **0x1c2 + ((age/2)&1)** at origin + dir·600; odd ages damage 0.
  Removed after age 49.
Draw `FUN_0061d8d0`: textured rect centred at origin + dir·960, half-length 1000/zoom, half-height
100 (Fever) / 160 (Amber) × age/10 ramping in, ×1.25 on even ages, ramp-out over the last 10 frames;
two passes (blend 1 tinted 0xc0ffffff, then blend 2 at 0.95/0.5 size); texture rows (age/2&1)·100
(src width 0x1ff) (texture hm_laser — UNVERIFIED binding).

### 5.4 Enemies — Untouchable wings (`FUN_0051e8c0` ids 100 / 0x96, update `FUN_0051eb10`)
Spawn: `+0x10 = +0x7c = id`, hp 300, inactive, shape 0, scale (3, 0, 0), phase `+0x84 = 0`.
Update (owner = player of team):
- phase 0: scaleX −0.2, scaleY +0.1 per frame; scaleY ≥ 1 → (1,1), phase 100, `+0x80 = 0`.
- phase 100: angle `+0x5c += +0x74` (wrap); `+0x80 > 69` → phase 900, `+0x80 = 0`.
- phase 900: `+0x74 *= 0.95` (_DAT_00814124), angle += `+0x74`, scaleY −= 0.05; ≤ 0 → removed.
- every frame: `+0x58 = +0x5c`; radius = scaleX·160; pos = owner.pos + (cos,sin)(angle)·radius; Hime
  effect 0xca (100) / 0xfc with `+0x2c = scaleX`, `+0x3c = scaleY`.
- in phase 100: hitbox **300** (100) / **0x15e** (0x96) at pos, angle; damage 0 when `+0x80` even;
  knock 15 if `+0x80 > 63`; then knock 10 if player distance `FUN_00619e20(0)` < 120.
Draw `FUN_0051efd0` is a no-op sprite reset (visual comes from the effects). killed (51efb0) no-op.

### 5.5 Effects (Hime effect family, cosmetic)
Ids 100–0x98 trails, 200/0xfa (sadism slashes, self-respawning trail), 0xc9/0xfb, 0xca/0xfc (wings),
300, 500 (chain anchor; spawns 0x226 every 3 frames for 30 frames), 0x1fe (same, 130 frames),
0x226, 0x230, 0x23a, 600/0x259 & 0x262/0x263 (chain heads, particle on death), 700/0x2ee (God Knows
pillars, spawn +2 ids), 0x2be/0x2f0. No gameplay effect; particle bursts consume RNG.

### 5.6 Missile draw `FUN_00663610`
Chains (500…0x263): sprite 0x20e frame (age/2)%3+2, rotZ(angle). Lances 100…0x70 → 0x208, 0x96…0xa2
→ 0x209, frame (age/2)%3; swords 200 → 0x20a, 0xfa → 0x20b, frame (age/2)%6 (0 while growing).
Colour `+0xdc(a=1, r=c, g=c, b=c)` with c = 1 when grow ≥ 1 else 0 (render.md order a,r,g,b: drawn
black while growing); scale (3 − 2·grow, grow, 1).

---

## 6. Sounds
| id | file | where |
|---|---|---|
| 6 / 7 | up01 / high_a | hyper / Accel start |
| 0xb | eco03 | Sadism hits |
| 0xe | tm2_gun000 | Paradise, Life-long Burning, ring age 70 |
| 0x10 | laser03 | Faint Breeze, Needle Fruit, Plenty |
| 0x12 | gun11_r | Fever, Amber |
| 0x13 | don16 | laser hitboxes on-hit |
| 0x15 | crash17 | missile/chain hits |
| 0x16 | hit_s | Sadism, wings, God Knows hits |
| 0x19 | hit_f | proximity knock |
| 0x26 | jya05 | Leash heat drain |
| 0x28 | chain.wav | chain extension |
(SE table at all.c:464010; id = (addr − 0x9b99a0)/4.)

---

## 7. Other per-character entries (index 16)
| Table | Entry | Role |
|---|---|---|
| 0x881350 | FUN_006c85d0 | dispatch (standard) |
| 0x8813a0 | FUN_006c8ad0 | dispatch (alternate) |
| 0x8813f0 | FUN_006c1340 | loader |
| 0x881500/04/08 | FUN_006c98f0 (lvl 0), FUN_006c8fd0 (lvl 1, 2) | CPU AI (not reversed) |
| — | FUN_006c3eb0 | pause command list (text 0x34a–0x355), no gameplay |
| — | FUN_006c3e20 | "no live chain 600/0x259" check for Delicate Binding |

## 8. Stats record (0x884d40, registry.c:2160-2202)
| Off | Value |
|---|---|
| +0x00 | 5.0 walk |
| +0x04 | 14.0 dash speed (DAT_00813a54) |
| +0x08 | 12.0 |
| +0x0c..+0x2c | 0, 0xb, 5, 2, 3, 7, 0, 0xb, 5 (UNVERIFIED meaning) |
| +0x3c/+0x40/+0x44 | 0x1f7, 0x1f8, 0x1f9 |
| +0x48/+0x60/+0x78 | text ids 9/10/11 |
| +0xa8 / +0x1a8 | mdl\win_font_HM.lfd / .dat |
| +0x2a8 | hm_res_bgp |
| +0x2c0..+0x2c8 | 0, 0, 0 |
| +0x2cc..+0x2d4 | 250, 200, 100 |
| +0x2d8 | 0 |

## 9. UNVERIFIED
- Graze-zone effects owned by a missile serial (§0.4).
- Hime effect-sprite details beyond what hime.c ports (missile blend inherited from the common pattern).
- Stats +0x0c..+0x2c meaning; AI functions.
- RESOLVED: `FUN_006b1d70` ends with `FUN_004a9f10` on `this = 0x895144` (`mov ecx,0x895144` @0x6b1e7d) =
  camera focus clear (`aos_camera_focus_clear`).
- RESOLVED: enemy (Untouchable wing) draw `FUN_0051efd0` = hm_wingr/hm_wingb, half 160 (DAT_008178c0 = 320
  full), scale (+scale[0], +scale[1]), rotation = angle, white, alpha blend.
- Exact meaning of hitType 1 vs 2 in the damage fn beyond heat scaling (core.md).
