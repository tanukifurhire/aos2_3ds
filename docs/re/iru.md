# Iru — complete moveset spec

Source: `aos2.exe` (Ghidra corpus `tools/re/dec/all.c`, constants read with `xref.exe rd`, sprite ids
from `xref.exe dis` because the decompiler drops the `this` of `FUN_004a9ab0`). Every number cites its
address. `t` = player state timer `p+0x154`. Structure follows `suguri.md`; shared helpers
(`FUN_007807f0` cost table, `FUN_006b1b40` hyper start, `FUN_00780910` hyper spend, `FUN_00780950`
hyper permission, `FUN_006b17d0` dash/dash-cancel, `FUN_007804b0` aim, `FUN_0061b230` input angle,
`FUN_005f0e70` turn-toward, RNG `FUN_00459fd0`/`FUN_00459f80`, SE `FUN_007bc9c0`/`FUN_007bcaf0`) are
specified in suguri.md §1 and objects.md and are not re-derived here.
Ruleset version `DAT_0089426c` = 0x5a2 in live play (objects.md §0.1) → **implement the ≥0x5a1 /
>0x5a0 / ≥0x58c columns**; older values are listed only for replay compatibility.

**Engine index `p+0x10` = 12** (handbook id 9 + 3). States 0x16e–0x184, sprite prefix `ir_`,
colour folders `charaIruA`/`charaIruB` (text ids 0xc/0xd).

---

## 0. Notes for the lead (affect other characters / shared docs)

1. **CModel `+0xdc` (set colour) takes `(a, r, g, b, meshIdx)`, not `(r, g, b, a)`** as render.md
   says. `FUN_0046ef70` stores `param_2` into the D3DCOLORVALUE `.a` slot (`local_40`) and params
   3/4/5 into `.r/.g/.b`. Evidence that matters: Iru's field draw calls `+0xdc(1.0, k, k, k)` with
   k = 0 while the field deploys — read as (r,g,b,a) the body would be invisible-red, read as
   (a,r,g,b) it is an opaque black→white fade-in. `FUN_004e8f50` passes `DAT_00893da0..dac`, so
   `0x893da0` is the global **alpha**, `da4/da8/dac` = r,g,b.
2. **Hitboxes can be spawned with team 3** and then hit *both* players (and both players' missiles /
   enemies): Iru's Genocide Bomb blast (Effect type 12 id 200, `FUN_004f9540`) spawns its hitboxes
   with `push 0x3` as the team argument (disassembly 0x4f971b / 0x4f9755). The collision passes only
   reject `obj.team == player.team`, so team 3 is "neutral". The port's team field must allow 3.
3. **Lasers with no segments.** Iru's Laser type (index 12) never pushes segments (`FUN_004a9f10`
   clears the vector and nothing is added); the laser object is only a timer/visual that spawns
   *hitboxes* each frame. The laser-vs-player/missile/enemy passes therefore do nothing for it, and its
   template damage/knockback (10/10) are dead values. Don't assume every laser uses segment circles.
4. **`FUN_004a7270(DAT_0089426c)` (version patch, called at all.c:284655) overwrites many cost-table
   entries after `FUN_0049ae40`** — for Iru it rewrites 0x178–0x180 (§1.2). Every character spec must
   take its cost table *after* this patch, not from registry.c alone (suguri.md only noted 0x140).
5. **Per-character object families are per-character code**, not shared: index 12 has its own beam /
   missile / hitbox / laser / enemy / effect / whip functions (table in §7). Only the managers and the
   collision passes are shared.
6. **Alternate control scheme bug:** `FUN_006d3e60` triggers Wide-Area Electric Field (0x17e) on
   `S held && H held` with **no press edge** (verified in disassembly: `cmp [+0x40],1` / `cmp [+0x44],1`
   at 0x6d3f75/0x6d3f7e, no `==3` test). Holding S+H re-fires it whenever `FUN_00780950` allows
   (20 frames into the previous hyper) and ≥1.0 gauge remains. Check other characters' alt
   dispatchers for the same pattern.
7. The `+0x150 = 0x182` test inside Iru's laser update (`FUN_0061eff0`) and `FUN_00630880` (shared
   helper at 0x630880: sets `+0x4d = 1` on every Laser with given id/owner/team — "detach") show that
   object updates read the owner's *current state id*; the port must keep state ids identical.
8. Player `+0xf8` doubles as the hyper bubble timer (radius `+0x100*+0xfc*0.8`, main loop
   FUN_006f1720 lines ~334-361); Iru's Genocide Bomb **halves** it after `FUN_006b1b40` (100 → 50) and
   writes `+0x108 = 25` (meaning of +0x108 → core.md).

### Player fields used by Iru's code (in addition to suguri.md's table)
| Off | Use here |
|---|---|
| +0x18 | colour variant → sprite bank, whip texture slot (`0x87fe60 + v*0x3c`) and loader arg |
| +0x84/+0x88 | aim; 0x17c/0x17e set it to 0 or π only (face opponent horizontally) |
| +0x98 | set to 0 at the fire frame of 0x182/0x184 |
| +0x9c | ×0.5 every frame in 0x17d |
| +0xf8, +0x108 | 0x183: `+0xf8 /= 2`, `+0x108 = 25` (ver>0x58a) |
| +0x128 | (attacker) combo hit counter; field hitbox gains knockback 10 when > 11 |

---

## 1. Shared mechanics as used by Iru

### 1.1 Frame model
Identical to suguri.md §1.1: dispatch → `T2[state]`; first T2 call of a state sees `t = 0`; state end
writes `state = 0` without resetting `t`; tap→hold splits change only `state` (timer kept) and call
the new T2 immediately (0x179→0x178, 0x17b→0x17a). "Dash-cancel from N" = `if t >= N && Dash == 3 →
FUN_006b17d0(p,1)`, with `FUN_00612900` (training display only) on `t == N`. `FUN_00612830` calls are
training-display only.

### 1.2 Ammo / hyper cost table (`0x87c4f0 + id*0xc` = {costA(+0xe8), costB(+0xec), hyperGain})
Registry values (registry.c:1938-1961) are then patched by `FUN_004a7270` (all.c:130645). Effective
values for ver 0x5a2:

| State | Move | costA | costB | hyperGain (registry → **0x5a2**) |
|---|---|---|---|---|
| 0x178 | High Speed Photon Rifle | 0.3 (DAT_00813f28) | 0 | 0.15 → **0.11** (DAT_00813f18, ver>0x5a0) |
| 0x179 | Scatter-shot (per shot) | 0.08 (DAT_00813f00) | 0 | 0.005 → **0.01** (DAT_00813ec8, ver≥0x58b) |
| 0x17a | Rockets, tap (per rocket) | 0 | 0.03 (DAT_00813ee0) | 0.005 → **0.01** (DAT_00813ec8, ver>0x5a0) |
| 0x17b | Straight Rockets (per rocket) | 0 | 0.03 | 0.004 → **0.007** (DAT_00813eb4, ver>0x5a0) |
| 0x17c | Electromagnetic Field (also paid by 0x17d) | 0.3 → **0.6** (DAT_00813f34, ver>0x5a0) | 0 | 0.1 → **0.02** (DAT_00813ed8, ver>0x5a0) |
| 0x180 | Homing Missile Pod (per missile) | 0 | 0.1 → **0.03** (DAT_00813ee0, ver≥0x58c) | 0.01 → **0.0072** (DAT_00813eb8, ver≥0x58c) |
| 0x181, 0x182 | — | 0 | 0 | 0 (never called) |

Intermediate versions (for replays): ver<0x58b: 0x178 gain 0.15, 0x17a 0.005, 0x17b 0.004, 0x17c gain
0.1, 0x180 {0, 0.1, 0.01}; ver 0x58b: 0x179 gain 0.01, 0x17c gain 0.03 (DAT_00813ef8), 0x178 0.11
(DAT_00813f14 — UNVERIFIED value, not read), 0x17a DAT_00813eb0, 0x17b DAT_00813ea8; 0x58c..0x5a0:
0x17c gain DAT_00813eec, 0x178 0.15, 0x17a DAT_00813ec0, 0x17b DAT_00813eb0, 0x180 {0, 0.03, 0.0072}.
(Those intermediate DAT values were not read; only the 0x5a2 column is needed for live play.)
Hypers 0x17e/0x17f/0x183/0x184 and 0x17d never call `FUN_007807f0` except 0x17d which uses **0x17c's**
entry.

### 1.3 Hyper flourish (t = 0 of 0x17e, 0x17f, 0x181, 0x182)
`FUN_006b1b40(p,10)`; SE 6 (`up01.wav`); `FUN_004e9280(100, 0.75)` (DAT_00813f3c);
`FUN_007bec20(100, pos, aim, π/4)` (DAT_0081fa90) — **0x17e passes angle 0 and 0**;
`FUN_004d7ac0(20, 0.5, 2)`; effect `0x881698(0xfa, 0, pos, 0, owner, 0, side)`. Cosmetic except
`FUN_006b1b40`. 0x183 omits `FUN_007bec20`. 0x184 is the Accel version (§3).

---

## 2. Input dispatch `FUN_006d3910` (standard scheme) — exact decode

**A. Hypers** — only if `FUN_00780950(p)`; first match wins; on failed cost the next row is tried.

| # | Condition | Cost | State | Official move |
|---|---|---|---|---|
| 1 | S held (`+0x40>1`) && B held (`+0x3c>1`) && H pressed (`+0x44==3`) | 3.0 (DAT_00813a50) | 0x184 | **Destruction Ray** (Accel, S+B+H) |
| 2 | Dash held (`+0x34>1`) && S held && H pressed | 1.0 (DAT_00853b34) | 0x17f | **Wide-Area Electric Field**, dash version (D+S+H) |
| 3 | S held && H pressed | 1.0 | 0x17e | **Wide-Area Electric Field** (S+H) |
| 4 | B held && H pressed | 1.0 | 0x183 | **Genocide Bomb** (B+H) |
| 5 | Dash held && H pressed | 1.0 | 0x181 | **Full-Fire Bullet Curtain** (D+H) |
| 6 | H pressed | 1.0 | 0x182 | **High Output Linear Ray** (H) |

**B. Normal moves** — only if `state == 0 || state == 2`:

| # | Condition | Result | Move |
|---|---|---|---|
| 1 | Dash pressed (`+0x34==3`) | `FUN_006b17d0(p,0)` | Dash |
| 2 | else Shield pressed && `FUN_00780910(p,0.5)` && `FUN_007809e0(p)` | `FUN_006b1930(p,0)` | Shield |
| 3 | Dash held (`>1`) && S held && A pressed (`+0x38==3`) | 0x17d | **Electromagnetic Field**, projected (D+S+A) |
| 4 | S held && A pressed | 0x17c | **Electromagnetic Field**, placed (S+A) |
| 5 | S held && B pressed | 0x180 | **Homing Missile Pod** (S+B) |
| 6 | B pressed | 0x17b (→ 0x17a if released by t=10) | **Straight Rockets** (B hold) / rocket fan (B tap — unnamed in the manual) |
| 7 | A pressed | 0x179 (→ 0x178 if released by t=9) | **Scatter-shot Photon Rifle** (A hold) / **High Speed Photon Rifle** (A tap) |

"(D)" in the manual = "dash optional": with Dash held the move becomes a separate state (0x17d / 0x17f)
that throws the field(s) forward instead of placing them. Dash must be *held* (`>1`; a value of 3 also
satisfies it, but a fresh Dash press is consumed by row B1 first). Ends with `return T2[state](p) != 0`.

**Alternate scheme `FUN_006d3e60`** (`p+0x0c == 1`): normal rows identical; hyper rows become
1. H held && Shield pressed → 0x184 (3.0); 2. D held && H held && S pressed → 0x17f; 3. **S held && H
held (no edge, see §0.6)** → 0x17e; 4. H held && B pressed → 0x183; 5. H held && Dash pressed → 0x181;
6. H held && A pressed → 0x182.

---

## 3. States

Spawn calls (engine index 12 tables): beam `DAT_008818a8 = FUN_004abbd0(id,&pos,angle,owner,0,side)`;
missile `DAT_008819e8 = FUN_00665eb0(id,&pos,angle,owner,0,side,p+0x18)`; enemy
`DAT_00881588 = FUN_0051f210(id, owner, side, p+0x18)` → caller writes fields into
`FUN_004ef290(idx)`; laser `DAT_00881c68 = FUN_0061e3a0(id,&pos,angle,owner,side,p+0x18)`; whip
`DAT_00881da8 = FUN_007c50e0(id, p+0x18, enemy.id)`; hitbox `DAT_00881bc8 = FUN_005e2870`; Iru effect
`DAT_008816c8 = FUN_004f7a00`. Object parameters: §5.

### 0x16e — neutral movement (T2 FUN_006d0f50, via common state 0 T2 FUN_006af390 `case 0xc`)
As Suguri 0x128 with `speed = stats+0 = 4.0` (DAT_00853b68 → 0x8841d0): y+ held → y += 4, y− → y −= 4,
x− → x −= 4, x+ → x += 4 (diagonals not normalised). `+0x84 = facing==1 ? 0 : π`. Returns 1.

### 0x16f, 0x170, 0x171, 0x172 — stubs (T2 FUN_006d1040 / 006d1070 / 006d1050 / 006d1060, return 1)
Only their T1s are used by common states (§4.3). 0x173–0x177 have no handlers (sprite ids only).

### 0x179 — Photon Rifle press / Scatter-shot (T2 FUN_006d1180) — A press
| t | Action |
|---|---|
| 0 | aim |
| 0–9 | if A not held (`+0x38 < 1`) → `state = 0x178` (timer kept), run 0x178 now, return |
| 10, 13, 16, 19 (`9<t<20 && (t−10)%3==0`) | if `FUN_007807f0(p,0x179)` (A −0.08): re-aim; beam **0x96** at pos, angle `aim + rand_f(−0.15, 0.15)` (DAT_00815dc4, DAT_00813f20); SE 0xd |
| ≥20 | dash-cancel |
| >30 | state 0 |
Up to 4 shots. No movement code (dash momentum decays at the normal 0.5/frame).

### 0x178 — High Speed Photon Rifle (T2 FUN_006d1080) — only via 0x179 early release (enters at t = 1..9)
| t | Action |
|---|---|
| 14 | if `FUN_007807f0(p,0x178)` (A −0.3): re-aim; beam **100** at pos, aim; SE 0xd |
| ≥22 | dash-cancel |
| >40 | state 0 |

### 0x17b — Straight Rockets (T2 FUN_006d1680) — B press
| t | Action |
|---|---|
| 0 | aim (fixed for the whole move) |
| 0–10 | if B not held (`+0x3c<1`) → `state = 0x17a`, run it, return |
| 12..50 even (`11<t<=50`; ver≤0x5a0: ≤40) | if `FUN_007807f0(p,0x17b)` (B −0.03): missile **100** at pos, angle `aim + s·o` with s = `((t/2)%2==0) ? +1 : −1` and o by t: 12–15 → 0.2 (DAT_00853b14), 16–18 → 0.4, 20–22 → 0.6, 24–26 → 0.8, 28–30 → 1.0, 32–34 → 1.2, 36–38 → 1.4, 40–42 → 1.6, 44–46 → 1.8, 48–50 → 2.0 (DAT_00813f30, 813f34, 8145e8, 853b34, 817d78, 817dd0, 82533c, 81413c, 813a4c). Then `FUN_007bcaf0(0x22,4)` |
| ≥30 | dash-cancel (rockets stop when the state is left) |
| >60 | state 0 |
Sequence: t=12 +0.2, 14 −0.2, 16 +0.4, 18 −0.4, … 48 +2.0, 50 −2.0 → **20 rockets**, alternating sides,
widening. ver<0x5a1: angle `aim ± rand_f(0.1, 2.4)` (DAT_00853b10, DAT_008178cc) instead.

### 0x17a — rocket fan, B tap (T2 FUN_006d1340) — via 0x17b early release
| t | Action |
|---|---|
| 10..26 even | if `FUN_007807f0(p,0x17a)` (B −0.03): missile **100** at pos, angle `aim + s·o`, s as above, o: 10–11 → 0, 12–14 → 0.3 (DAT_00813f28), 16–18 → 0.6, 20–22 → 0.9 (DAT_00814178), 24–26 → 1.2. SE `FUN_007bcaf0(0x22,4)` |
| ≥30 | dash-cancel |
| >50 | state 0 |
t=10 → aim (−0), 12 +0.3, 14 −0.3, 16 +0.6, 18 −0.6, 20 +0.9, 22 −0.9, 24 +1.2, 26 −1.2 → **9 rockets**.
Aim comes from 0x17b's t=0. ver<0x5a1: `aim ± rand_f(−0.2, 1.2)` (DAT_00817c04, DAT_00817d78).

### 0x17c — Electromagnetic Field, placed (T2 FUN_006d1b00) — S+A
| t | Action |
|---|---|
| 0 | face opponent horizontally: `+0x84 = +0x88 = (opp.x <= x) ? π : 0` |
| 10 | if `FUN_007807f0(p,0x17c)` (A −0.6): enemy **100**, pos = player pos (+0x40..48), sub-id `+0x7c = 100` (template), speed `+0x70 = 0` → stationary field; SE 0x2f (`power35.wav`) |
| ≥15 | dash-cancel |
| >30 | state 0 |

### 0x17d — Electromagnetic Field, projected (T2 FUN_006d1c80) — D+S+A
| t | Action |
|---|---|
| 0 | aim |
| 10 | if `FUN_007807f0(p,0x17c)`: enemy **100** at pos; `+0x7c = 0x6e`, `+0x70 = 10.0` (DAT_00813f4c), `+0x5c = aim` → field slides along aim, speed ×0.95/frame (≈200 px total); SE 0x2f |
| every frame (except the cancel frame) | friction `+0x9c *= 0.5` (dash momentum decays at 0.25/frame) |
| ≥15 | dash-cancel |
| >30 | state 0 |

### 0x180 — Homing Missile Pod (T2 FUN_006d2770) — S+B
| t | Action |
|---|---|
| 0 | aim |
| 10..20 even | if `FUN_007807f0(p, ver>0x58b ? 0x180 : 0x17b)` (B −0.03): missile **200** at pos, angle `aim + s·o`, s = `((t/2)%2==0)?+1:−1`, o: t 10,12 → 1.4 (DAT_00817dd0); 14,16 → 1.2 (default); 18,20 → 1.0. SE `FUN_007bcaf0(0x22,4)` |
| ≥32 | dash-cancel |
| >60 | state 0 |
Order: 10 −1.4, 12 +1.4, 14 −1.2, 16 +1.2, 18 −1.0, 20 +1.0 → **6 missiles**. ver<0x5a1:
`aim ± rand_f(0.8, 1.6)`.

### 0x17e — Wide-Area Electric Field (T2 FUN_006d1e10) — S+H, 1 level
| t | Action |
|---|---|
| 0 | face opponent horizontally (as 0x17c); flourish §1.3 (cut-in angle 0) |
| 10 | 4× { enemy **100** at pos; centre `+0xc8/+0xcc/+0xd0 = pos`; base angle `+0x70` and sub-id `+0x7c`: (0, **0x96**), (π, **0x96**), (π/2, **0x97**), (−π/2, **0x97**) (DAT_00853b58, DAT_00814104, DAT_0081416c); then whip **6** `(*0x881da8)(6, p+0x18, enemy.id)` }; SE 0x2f. No cost check (gauge paid in dispatch) |
| ≥30 | dash-cancel |
| >60 | state 0 |

### 0x17f — Wide-Area Electric Field, dash version (T2 FUN_006d2330) — D+S+H, 1 level
| t | Action |
|---|---|
| 0 | aim; flourish |
| 10 | 4× enemy **100** at pos, `+0x7c = 0x6e`, `+0x70 = 12.0` (DAT_00813f54), `+0x5c` = aim − 0.35, aim + 0.35, aim − 0.75, aim + 0.75 (DAT_00813f2c, DAT_00813f3c), in that spawn order; SE 0x2f |
| ≥30 | dash-cancel |
| >60 | state 0 |
(Fields thrown in a fan; each travels 12/(1−0.95) = 240 px.)

### 0x181 — Full-Fire Bullet Curtain (T2 FUN_006d2b00) — D+H, 1 level
| t | Action |
|---|---|
| 0 | aim; flourish |
| 10..36 even | missile **200** at pos (no cost), angle `aim + s·o`, s as 0x180, o: 10,12 → 1.4; 14,16 → 1.3 (DAT_00817d7c); 18,20 → 1.2; 22,24 → 1.1 (DAT_00853b38); 26,28 → 1.0; 30,32 → 0.9; 34,36 → 0.8 (DAT_008145e8). SE `FUN_007bcaf0(0x22,4)` |
| ≥38 | dash-cancel |
| >60 | state 0 |
**14 missiles**. ver<0x5a1: `aim ± rand_f(1.2, 2.0)`.

### 0x182 — High Output Linear Ray (T2 FUN_006d2f50) — H, 1 level
| t | ver ≥ 0x5a1 | (ver ≤ 0x5a0) |
|---|---|---|
| 0 | aim; flourish; (ver>0x589) `FUN_00630880(100, owner, side)` → older Linear Rays of this player stop following her (`+0x4d=1`) | same |
| fire | **t=6**: speed `+0x98 = 0`; laser **100** at pos, aim; SE 0x12 (`gun11_r.wav`) | t=10 |
| steer | **6 ≤ t ≤ 64**: if a direction is held (`FUN_0061b230(p+0x1c) <= 2π`): `FUN_005f0e70(&aim, inputAngle, 0.01)` (DAT_00813ec8) | 10..70 |
| dash-cancel | **≥64** | ≥70 |
| end | **>84** | >90 |
The laser follows Iru's position and aim every frame while she stays in 0x182 (§5.4).

### 0x183 — Genocide Bomb (T2 FUN_006d32b0) — B+H, 1 level
| t | ver ≥ 0x5a1 | (ver ≤ 0x5a0) |
|---|---|---|
| 0 | aim; `FUN_006b1b40(p,10)`; **`+0xf8 /= 2`** (100 → 50); ver>0x58a: `+0x108 = 25`; SE 6; `FUN_004e9280(100,0.75)`; `FUN_004d7ac0(20,0.5,2)`; effect 0xfa | same |
| fire | **t=15**: missile **400** at pos, aim; `FUN_007bcaf0(0x22,1)` | t=25 |
| dash-cancel | **≥25** | ≥50 |
| end | **>35** | >60 |

### 0x184 — Destruction Ray (Accel) (T2 FUN_006d3540) — S+B+H, 3 levels
| t | Action |
|---|---|
| 0 | aim; `FUN_006b1b40(p,0x14)`; SE 7 (`high_a.wav`); `FUN_004e9280(120, 0.85)` (DAT_00814120); `FUN_007bec20(100,pos,aim,π/4)`; `FUN_004d7ac0(120, 0.75, 2)`; effect 0xfa |
| **5** (ver≤0x5a0: 15) | speed 0; laser **200** at pos, aim (origin and angle fixed); SE 0x12 |
| 10..60 even | cosmetic: `r = rand_f(−0.25, 0.25)` (DAT_0081411c, DAT_00813f24), `n = rand_i(0,100) & 7` (sign-corrected %8) → effect `0xa82+n` (colour `DAT_008859f4+side*0x10`) at pos + dir(aim)·50 (DAT_008140ec), angle aim + r; spark `FUN_007bd5e0(1000, colour, 10, 1, aim+π, pos + dir·100, owner)` (DAT_00853b90). Consumes RNG |
| ≥80 | dash-cancel |
| >100 | state 0 |

---

## 4. Animation

### 4.1 Sprite registration (loader `FUN_006cd8b0`, arg = colour variant `p+0x18`)
Variant 0 → folder text id 0xc, else 0xd (`charaIruA` / `charaIruB`). Each sprite is an S×S quad
(`FUN_00467d00(S,S,2,2,1.0,0)`) stored at `0x885b80 + variant*0x3888 + id*0x18`.

| Sprite id | Pattern | Frames | S | Used by |
|---|---|---|---|---|
| 0x16e | `ir_std0%d` | 3 | 96 (DAT_00814118) | neutral |
| 0x173 | `ir_fwd0%d` | 3 | 96 | neutral, moving toward |
| 0x174 | `ir_bck0%d` | 3 | 96 | neutral, moving away |
| 0x170 | `ir_los0%d` | 6 | 96 | T1 0x170, 0x171 |
| 0x172 | `ir_sld0%d` | 4 | 96 | T1 0x172 (shield) and 0x17c–0x17f (field casts) |
| 0x16f | `ir_dsh0%d` | 6 | 96 | dash |
| 0x175 | `ir_rng0%d` | 2 | 512 (DAT_008178b0) | stats +0x3c (HUD ring?, UNVERIFIED) |
| 0x177 | `ir_system` | 1 | 512 | stats +0x44 (UI, UNVERIFIED) |
| 0x178 | `ir_rifle0%d` | 5 | 96 | 0x178, 0x179, 0x182, 0x184 |
| 0x17a | `ir_hand0%d` | 7 | 96 | 0x17a |
| 0x17b | `ir_handa0%d` | 4 | 96 | 0x17b, 0x180, 0x181 |
| 0x183 | `ir_bazz0%d` | 6 | 96 | 0x183 |
| 0x185 | `ir_bfire` (frame 0), `ir_bfireb` (frame 1) | 2 | 48 (DAT_008140e4) | missile 200/400 exhaust |
| 0x186 | `ir_hrckt0%d` | 5 | 64 (DAT_008140f8) | missile 100 (rocket) |
| 0x187 | `ir_hmis0%d` | 5 | 72 (DAT_008178ac) | missile 200 |
| 0x188 | `ir_hbit00..09`, `ir_hbit10..17` | 18 | 64 | field body (3 layers × 6 frames) |
| 0x189 | `ir_hbitf0%d` | 2 | 256 (DAT_00814160) | field ring |
| 0x18a | `ir_laser` | 1 | 256 | laser strips (blitted by source rect) |
| 0x18b | `ir_geno0%d` | 9 | 92 (DAT_008255bc) | missile 400 (3 layers × 3 frames) |
| 0x18c | `ir_exp0%d` | 3 | 256 | effects 200 / 0xd2 / 400 |
| whip tex 6 | `ir_whip0` | — | — | stored at `0x87ff38/0x87fe48 + 6*4 + variant*0x3c` (loader end) |
All 96 `ir_*.png` in `extracted/texture/charaIruA/` are covered (plus res_bgp/res_chr/res_sys).
Ids 0x171, 0x176, 0x179, 0x17c–0x182, 0x184 are not registered. `FUN_006d0640` builds the pause-menu
command list (text ids 0x30f–0x318) — no gameplay. `FUN_006d05c0(team)` counts the team's Iru
enemies (+0xc==12, +0x10==100) — used only by the CPU AI.

### 4.2 Draw transforms
- **AIM transform** (every Iru T1 except 0x16e): as suguri.md §4.2 (rotZ(aim); if |aim| ≥ π/2 also
  rotX(180°) then rotY(180°), else rotY(180°)).
- **Neutral 0x16e**: no rotZ; rotY(π) if facing != 0.
- Each body draw is preceded by the global tint `FUN_004e8f50`; default blend. Afterimage
  `FUN_004d7e00(model,1,p,{0,0,0},0)` only in 0x16e and 0x16f.

### 4.3 Common states → Iru draw handlers (FUN_006b07a0 … FUN_006b1580, `case 0xc`)
| Common state | Iru T1 |
|---|---|
| 0 neutral | 0x16e FUN_006d5240 |
| 1 hit, 5 | 0x171 FUN_006d5900 |
| 2 dash | 0x16f FUN_006d5500 |
| 3 shield, 4 | 0x172 FUN_006d5bd0 |
| 6 | 0x170 FUN_006d5e90 |

### 4.4 Per-state frames (`pp(n)` = `(t/n)&3`, 3→1 i.e. 0,1,2,1; sprite ids from disassembly immediates)
| State | T1 | Sprite | Frame |
|---|---|---|---|
| 0x16e | FUN_006d5240 | 0x16e; overrides (later wins): y+→0x173; y−→0x174; facing0&x−→0x173; facing1&x−→0x174; facing0&x+→0x174; facing1&x+→0x173 | pp(5) |
| 0x16f | FUN_006d5500 | 0x16f | pp(3); +3 if (facing1 && \|aim\|>π/2) or (facing0 && \|aim\|<π/2) |
| 0x170 | FUN_006d5e90 | 0x170 | pp(3) + (3 if `DAT_008858f8` even); **drawn only while t < 100** |
| 0x171 | FUN_006d5900 | 0x170 | pp(3) |
| 0x172, 0x17c, 0x17d, 0x17e, 0x17f | FUN_006d5bd0 / 6d6d20 / 6d6fe0 / 6d72a0 / 6d7580 | 0x172 | 0x17e: t<10 ? 0 : ((t−10)/3)%3+1; others min(t/3, 3) |
| 0x178 | FUN_006d6190 | 0x178 | t<15 ? 0 : min((t−15)/3+1, 4) |
| 0x179 | FUN_006d6470 | 0x178 | t<20 ? 0 : min((t−20)/3+1, 4) |
| 0x17a | FUN_006d6750 | 0x17a | t<12: 0; t<26: 1; else min((t−26)/3+2, 6) |
| 0x17b, 0x180, 0x181 | FUN_006d6a40 / 6d7840 / 6d7b20 | 0x17b | t<12 ? 0 : ((t−12)/3)%3+1 |
| 0x182 | FUN_006d7e00 | 0x178 | t<50 ? 0 : min((t−50)/5+1, 4) |
| 0x183 | FUN_006d80e0 | 0x183 | t<25 ? 0 : min((t−25)/5, 5) |
| 0x184 | FUN_006d83c0 | 0x178 | t<80 ? 0 : min((t−80)/5+1, 4) |
(The `if (f==4) f=2` after `%3+1` in 0x17b/0x17e/0x180/0x181 is dead code. Note 0x17e's formula differs
from the other field states.)

---

## 5. Spawned objects (engine type 12)

Generic manager / collision semantics: objects.md §1–§4. Struct offsets below refer to objects.md §2.

### 5.1 Beams: spawn FUN_004abbd0, update FUN_004abf90, draw FUN_004abd50 (empty), onHit FUN_004abd70
Template via `FUN_004ba490` (hitType 1, destroyOnHit 1, stun 1.0, active 1).

| id | Used by | speed +0x68 | shape {hx,hy,r} | dmg +0x84 | knock +0x88 |
|---|---|---|---|---|---|
| 100 Photon Rifle | 0x178 | 25 (DAT_008140f0) | {36 (DAT_008140f4), 16 (DAT_008140d8), 0} | 360 | 10 (DAT_00813f4c) |
| 0x96 Scatter-shot | 0x179 | 16 (DAT_008140d8) | {24 (DAT_00853b8c), 12 (DAT_00813f54), 0} | 180; **ver>0x5a0: 120** | 10 |
Update (both): `pos += (cos,sin)(moveAngle)·speed`; `if !inArena(pos, 10) → return 0` (silent). Visual
spawns every frame: id 100 → effect `0x8816c8(300, colour, pos, angle, owner, 0, side)` and, when
`age%3==0`, generic effect 0x548; id 0x96 → effect `0x8816c8(0x15e, …)` and, when age even, particle
`FUN_0068b8a0(0x456, colour, pos, angle+π, owner, 0, 1)`. `colour = DAT_008859f4 + side*0x10`.
Beams are dash-dodgeable (hitType 1).
onHit: 100 → effect 1000 (angle+π), 0x514, particle 1000 ×10, spark `FUN_007bd5e0(1000,c,10,10,…)`,
SE 0x13 **and** SE 0x15 (`crash17.wav`); 0x96 → effect 1000, particle ×10, spark (6,6), SE 0x13.

### 5.2 Missiles: spawn FUN_00665eb0, update FUN_00668010, draw FUN_00666160, onHit FUN_00667c90, onKilled FUN_00667ec0
Template via `FUN_00685bb0` (hitType **2** — dash does not dodge, growScale 1.0, stun 1.0). `absorbs`
not set (0x881b78 = 0).

| | 100 rocket | 200 homing missile | 400 Genocide Bomb |
|---|---|---|---|
| spawned by | 0x17a, 0x17b | 0x180, 0x181 | 0x183 |
| initial speed | **12** (DAT_00813f54) (ver<0x58c 15; 0x58c..0x5a0 17) | 25 (DAT_008140f0) | **5** (DAT_00813f40) (old 3) |
| hp +0x78 | **120** (old 60) | 180 | 540 |
| radius body / proj | **16 / 16** (DAT_008140d8) (old 12) | 16 / 16 | **26 / 26** (DAT_008178d0) (old 20) |
| damage +0xa0 | **150** (ver<0x58c 360, 0x58c..0x5a0 290) | 300; **ver>0x5a0: 170** | 400 |
| knockback | 10 | 15 (DAT_00853b88) | 10 |

Update (`a` = age, update sees 0 first; `turn(x)` = `FUN_005f0e70(&moveAngle, atan2(opp−pos), x)`):
- **100**: a < 20: if speed > 2 → speed −= 1.0. a ≥ 20: if speed < 12 → speed += 0.5 (ver≤0x5a0: < 18).
  Never turns (flies straight on its spawn angle). Trail effect `0x881698(0x802, 0, pos, angle, owner,
  0, side)` when a even or speed > 12. Move. If a > 10 and `!inArena(pos,10)`: onHit, remove.
- **200**: a < 20: turn(0.05) (DAT_00813ef4); if speed > 2 → speed ×= 0.9 (DAT_00814178).
  20 ≤ a < 40: turn(0.025) (DAT_00813edc); if speed < 24 → speed += 0.75; for a = 20..24 every frame a
  cosmetic spark `0xa28 + (rand_i(0,100)%8)` at pos, angle `drawAngle + rand_f(−0.2, 0.2)` (RNG).
  a ≥ 40: straight. Then `drawAngle(+0x6c) = moveAngle`; trail 0x802 at pos − dir·20 (DAT_008140fc)
  when a even or speed > 12. Move. If a > 50 and outside arena+10: onHit, remove.
- **400**: if speed < 16 → speed += 0.25 (DAT_00813f24); every frame smoke `0x881698(2000, 0, pos,
  rand_f(−π, π))` (RNG); move; if `!inArena(pos, 0)` → onHit, remove. Never turns.

onHit (= explode, also called on player contact, arena exit; returns false → erased):
100/200 → Iru effect `0x8816c8(100, colour, pos, drawAngle+π)` + effect 0x514 + SE 0x14 (`bom06.wav`).
**400** → `FUN_004e9280(100, 0.75)`, `FUN_004d7ac0(50, 0.4, 2)`, `FUN_007bec20(100, pos, angle, 0)`,
**Iru effect 200 (the blast, §5.6) at the missile's pos, angle 0**, SE 0x1a (`tm2_bom002.wav`).
onKilled (shot down): 100/200 → effects 0x262 + 0x514(colour 1), SE 0x1d (`gun12.wav`); **400 → same
as its onHit (the blast still happens)**.

Draw (all: rotZ = drawAngle, blend 2 layers with colour (a = pulse, rgb = 1)):
`p1 = sin((a%50)/50·2π)·0.15 + 0.85`, `p2 = sin(same)·0.25 + 0.75`.
- 100: sprite 0x186 frame (a/3)%3 (normal), frame 3 additive alpha p1, frame 4 additive alpha p2. No exhaust.
- 200: exhaust at pos + rotate((42,0), angle+π) (DAT_00824488): 0x185 frame 1 additive, then frame 0
  additive with scale (1, 0.3, 1) when (a/2) even; then sprite 0x187 frame (a/3)%3, frame 3 (alpha
  p1, additive), frame 4 (alpha p2, additive).
- 400: same exhaust; then 0x18b frames 3k, 3k+1 (alpha p1, add), 3k+2 (alpha p2, add), k = (a/3)%3.

### 5.3 Hitboxes: spawn FUN_005e2870, onHit FUN_005e2c40
Record per objects.md §2.4; all hitType **3** (hit dashing targets), stun 1.0, oncePerFrame 0,
dealsDamage 1. Spawned with angle as given; `+0x0c` = 5th arg (0).

| id | Spawned by | dmg +0x3c | shape {hx,hy,r} | knock +0x40 | onHit |
|---|---|---|---|---|---|
| 100 | EM field, every 10th frame | 30 | {0,0,100} (DAT_00853b90) | 0 | see below; SE 0x15 lock 5 + streak fan |
| 0x6e | EM field, other frames | 0 | {0,0,100} | 0 | see below; SE 0x15 lock 5 |
| 200 | Genocide blast, every 3rd frame | 45; **ver>0x5a0: 60** | {0,0,280} (DAT_00814f14) | 0 | effect 500 at target ± rand(−30,30) (×2 rand), SE 0x13 lock 5 |
| 0xd2 | Genocide blast, other frames | 0 | {0,0,280} | 5 (DAT_00813f40) | `angle = atan2(hb − target)` → **knockback pulls toward the blast centre** |
| 300 | Linear Ray, every 5th frame | 60; **ver>0x5a0: 90** | {800, 60, 0} (DAT_008149c8, DAT_00813a58) | 5 | 6 streaks 0x456 at k·π/3 + rand(−0.25,0.25), particle 1000 ×4, spark, SE 0x13 lock 4 |
| 0x136 | Linear Ray, other frames | 0 | {800, 60, 0} | 5 | none |
| 400 | Destruction Ray, every 5th frame | 80; **ver>0x5a0: 140** | {800, 120, 0} (DAT_00814174) | 5 | as 300 |
| 0x19a | Destruction Ray, other frames | 0 | {800, 120, 0} | 5 | none |

**Field hitbox onHit (100 / 0x6e)**: if the target is in hit state 1 **and** its speed `+0x98 > 2.0`
(DAT_00813a4c) → `dealsDamage = 0` (no damage, no hitstun this frame — a target already being knocked
away is released). Otherwise `angle = atan2(target − hb)` (push outward), and if the field owner's combo
counter `FUN_00780450(team)+0x128 > 11` → `knock = 10` (combo ends by pushing the victim out); SE 0x15
(lock 5). id 100 additionally spawns 6 streak effects 0x456 (angles k·π/3 + rand(−0.25,0.25), k=−3..2)
and effect 0x515 (RNG). Zero-damage hits still add stun 1.0 and return "stun full"
(`FUN_00780ab0`: `+0x144 += stun` and the return value are computed before the `if (dmg < 1) return`
early-out), so a victim inside a field is put back into hitstun (knock 0, or 10 after 12 combo hits)
every frame; the early-out skips the attacker's combo counters, so only the 30-damage ticks count
toward `+0x128 > 11`. The same holds for the 0-damage ray/blast boxes (0x136, 0x19a, 0xd2).

### 5.4 Lasers: spawn FUN_0061e3a0, update FUN_0061eff0, draw FUN_0061e500, onHit FUN_0061eec0 (unreachable)
Template (`FUN_006307d0`): blockMode 1, hitType 3, damage 10, knock 10, +0x48 = 200, stun 1 — all
unused because **no segments are ever created** (§0.3). Laser update runs after `age++` (first update
sees age 1, i.e. the frame it was spawned).

**100 Linear Ray** (`own` = `FUN_00780450(team)`, the caster):
1. If `own.state != 0x182` and age < 51 → age = 50 (shorten).
2. If `own.state == 0x182`: if `+0x4d == 0` → origin = own pos, angle = own aim `+0x84` (follows the
   steering). If age%5 == 0 and age < 51: muzzle effects 0x546 + 0x547 at origin + dir·40 (DAT_008140e0).
3. age > 69 → remove.
4. 10 ≤ age ≤ 50: hitbox (age%5==0 ? **300** : **0x136**) at origin + dir·800, angle = laser angle,
   owner, side → a 1600×120 box starting at the origin.
Timeline with the 0x5a2 cast (spawn at state t=6 ⇒ age = t−5): damage ticks at t = 15,20,…,55
(**9 ticks × 90 = 810**), stun boxes every frame t=15..55; ray removed at t=75. If Iru is hit / leaves
0x182 before t=55, step 1 sets age 50 → one more *damaging* hitbox (age 50 %5 == 0) at the ray's last
position, then nothing. A new 0x182 detaches older rays (`+0x4d=1`) which then finish in place.

**200 Destruction Ray** (spawn at state t=5 ⇒ age = t−4; origin/angle fixed):
- 10 ≤ age ≤ 70, age%5==0: Iru effect **400** at origin + dir·(age·20) + perp·rand_f(50, 200)
  (perp = angle+π/2 if age%10==0 else angle−π/2), SE `FUN_007bcaf0(0x34,10)` (`bomb2.wav`).
- age > 89 → remove.
- 10 ≤ age ≤ 70: hitbox (age%5==0 ? **400** : **0x19a**) at origin + dir·800 → 1600×240 box.
  **13 ticks × 140 = 1820**, t = 14..74.

Draw (2-D blits of sprite 0x18a `ir_laser` frame 0 via texture vtable +0x68; screen-space via
`FUN_00619d90`, rotation −angle; dest half-length L, half-height H, both /zoom):
- 100: centre origin + dir·1000, L = 1000, H = 180 / 150 alternating with (age/2) parity;
  age < 10: H ×= (40 − 3·age)·0.1; age > 50: H ×= (70 − age)/20. Passes: src rows 100–199, ARGB
  0x40ffffff, blend 1; H×0.75, src rows 0–99, 0x80ffffff, blend 2; H = 60 (same age scaling), src rows
  200+40·((age/2)%3) … +39, 0xc0ffffff, blend 2.
- 200: centre origin + dir·1150, L = 1200, H = 540 / 400 alternating; age > 70: ×(90 − age)/20; third
  pass H = 240, ARGB 0xffffffff. Blit = `CTexture::Draw` (vtable 0x8464c0 +0x68 = FUN_0045c470): `(destRect screen px, z, srcRect texels, angle bam clockwise-on-screen (callers pass −angle), D3DCOLOR ARGB, blend)`; the quad is rotated about the dest-rect centre.

### 5.5 Enemies (Electromagnetic Field): spawn FUN_0051f210, update FUN_0051f350, draw FUN_0051fcf0, onKilled FUN_0051fab0
Spawn id 100 only (template, offsets objects.md §2.6): `+0x0c` type 12, `+0x10` = 100, hp/maxHp 400,
active 0, shape {0, 0, 36} (DAT_008140f4), scale (0, 3.0, 0) (DAT_00813a50), `+0x6c` 0, sub-id
`+0x7c = 100`, phase `+0x84 = 0`, speed `+0x70 = 0`, `+0x5c = 0`, motion 0, root (parent −1), layer 1,
not solid. `+0x78` age and `+0x80` phase timer are incremented by the manager after each update.
Active fields are hit by enemy projectiles (radius 36): Beam/Hitbox/Missile/Laser → `hp -= damage`;
at hp < 1 onKilled → removed.

Update (switch on sub-id `+0x7c`):
- **100 (placed) / 0x6e (projected)**
  - phase 0: scaleX += 0.1, scaleY −= 0.2; when scaleX ≥ 1.0 (10th update, float32): active = 1,
    scale = (1,1,1), phase 100, timer `+0x80 = 0`.
  - phase 100: `+0x58 = rand_f(−π, π)` every frame (draw spin, RNG); if 20 ≤ timer ≤ 170 (ver ≤0x5a0:
    ≤350): hitbox (timer%10==0 ? **100** : **0x6e**) at pos, angle 0; if timer > 220 (old: >400):
    scaleZ = 0, active = 0, phase 900.
  - phase 900: scaleX −= 0.1, scaleY += 0.2; remove when scaleX ≤ 0.
  - every phase, afterwards: `pos += (cos,sin)(+0x5c)·speed; speed ×= 0.95` (DAT_00814124).
  ⇒ 16 damage ticks (30 each) at timer 20,30,…,170.
- **0x96 / 0x97 (wide-area, spiralling)**
  - phase 0: scaleX += 0.01 (cap 1), scaleY −= 0.02 (set to 1 when scaleX hits 1);
    `f = ((100 − age)/100)²`; `r = 200 − 200·f` (DAT_008147cc);
    `θ = base(+0x70) + s·(1 − f)·2π`, s = +1 for 0x97, −1 for 0x96;
    `pos = centre(+0xc8,+0xcc) + (cos θ, sin θ)·r`. When timer == 100: active, scaleZ = 1, phase 100,
    timer 0. (Four fields unwind from Iru's position to the four points at radius 200, one full turn.)
  - phase 100: as above but **no version branch**: hitboxes for 20 ≤ timer ≤ 350, phase 900 at
    timer > 400. ⇒ 34 damage ticks of 30. No movement (speed field holds the base angle).
  - phase 900: as above.

onKilled (all, `+0x10 == 100`): effect 600 at pos; 5× effect 0x834 at pos + (rand(−50,50), rand(−50,50));
5× effect 500 at pos + (rand(−30,30), rand(−30,30)) with age0 = −10, −13, …, −22 (delayed); SE 0x1a.
Returns false (erase). Note the RNG order: x then y per effect.

Draw (`a` = age, blend 1 unless noted):
`pa = sin((a%50)/50·2π)·0.25 + 0.75`, `pb = sin(same)·0.35 + 0.65` (DAT_0081788c).
- If phase 100 (ring): `s = sin((a%75)/75·2π)·0.05 + 1` (DAT_00815254); timer < 20: s ×= timer/20;
  350 ≤ timer ≤ 369: s ×= (370 − timer)/20; timer ≥ 370: s = 0. Sprite 0x189 frame 1, colour
  (a = pb·0.75, 1,1,1), scale s, rotZ `+0x58`; then blend 2, frame 0, alpha pa·0.75.
- Body: f = (a/3)%6; k = sin((a%100)/100·2π)·0.15 + 1 (DAT_00813f20); sprite 0x188 frame f, colour
  (a = 1, rgb = `+0x68`) (black while deploying), scale (scaleX·k, scaleY·k, 1), rotZ `+0x5c`;
  blend 2: frame f+6 alpha pa, frame f+12 alpha pb, same scale/rotation.

### 5.6 Iru effects: spawn FUN_004f7a00, update FUN_004f9540, draw FUN_004f7d20
Record 0x44: +0 type 12, +4 id, +8 colour, +0xc owner, +0x10 team, +0x14 draw layer (1; 0 for id 200 and for id 100 once grown),
+0x18 pos, +0x24 1, +0x28 age (pre-incremented by the list update: first update sees age0+1),
+0x2c scale, +0x30 alpha, +0x34/+0x38 angle, +0x3c rate, +0x40.
- **200 Genocide blast (gameplay)**: init scale 0, alpha 1, rate 0.175 (DAT_00817888), layer 0.
  age < 50: scale += rate, rate ×= 0.8 (DAT_008145e8) (scale → 0.875); else alpha −= 0.01, remove at
  alpha ≤ 0. **2 ≤ age ≤ 40: hitbox (age−2)%3==0 ? 200 : 0xd2, at pos, angle +0x38 (0), owner, team 3**
  (§0.2) → 13 damage ticks of 60 (780) pulling the victim inward in between; hits Iru too, unless her
  bubble `+0xf8` is still up (hitbox pass skips players with `+0xf8 ≥ 1`; 0x183 leaves 50 frames).
  Cosmetic (RNG): age < 11: 2× particle 1000 at pos + polar(rand(−π,π), rand(0,100)); age ≤ 40 and
  age%5==0: Iru effect 0xd2 at pos, angle rand(−π,π) and `return`; else age < 50: generic effect
  200 (age even) / 0xd2 (odd) at pos, angle rand(−π,π).
- 0xd2 (blast puff, cosmetic): spawn draws `+0x34 = rand_f(0.25, 0.75)` (height/width ratio of its quad), `+0x3c = 0.175`, `+0x40 = 1`; scale += `+0x3c`, `+0x3c` ×0.8/frame (min 0.02) until age 20, then alpha −0.05. Draw sprite 0x18c.
- 300 / 0x15e (beam bolts), 100 (rocket/missile explosion, scale 2→1 by −0.1 then hold to age 119,
  fade −0.05), 400 (Destruction Ray side bursts, scale +0.2 to 1, fade after age 20): cosmetic; drawn
  from shared atlases `DAT_0087fdf4`, `DAT_0087fdf0`, `DAT_0087fccc[]` and sprite 0x18c. Exact blit
  rectangles are in FUN_004f7d20 — ported 1:1 in iru.c `fx12_draw`. Note effect 100 picks atlas `DAT_0087fccc[colour/8]` (explosion0/1) and cell `colour%8` from the **colour** field +0x08, not the age; effect 300/0x15e use `beamemitb`/`beamemit` cells by colour, the flash cell from a draw-static left by the last 0xd2 draw (DAT_00895840).

### 5.7 Whip (field trail): spawn FUN_007c50e0, update FUN_007c5a80, draw FUN_007c56f0 (cosmetic)
Spawned only by 0x17e, id 6, `+8 = variant`, `+0x10 = enemy id`. Update: enemy gone
(`FUN_0055f060` = 0) → remove. `d = atan2(pos − prevPos)` of the enemy; node = { pos −
(cos,sin)(d+π/2)·10, pos + (cos,sin)(d+π/2)·10 }. 20 nodes of 0x1c bytes from +0x14: fill the
highest free index first (19 → 0); when full, shift node[k] = node[k−1] (k = 19..1) and write node 0.
Draw: for k = 0..18 with both nodes used, a quad between them, ARGB 0x80ffffff, texture `ir_whip0`,
vertices A_k (tex 0, 12.8k), A_k+1 (0, 12.8(k+1)), B_k+1 (256, …), B_k (256, …): texture x runs across the ribbon, y along it (DAT_00828324, DAT_00814160). A stationary field produces zero-area quads (nothing drawn).

### 5.8 RNG consumers (order matters for sync)
Gameplay: 0x179 spread. Cosmetic but same engine: field spin (every field, every active frame),
field-hit streaks, missile-200 sparks, missile-400 smoke, Accel sparks, Destruction Ray burst offsets,
blast particles/puffs, onKilled/onHit effects. All old-version (≤0x5a0) rocket/missile angles were
random; 0x5a2 uses the fixed tables.

---

## 6. Sound ids used (SE table all.c:464010, id = (DAT − 0x9b99a0)/4)
| id | file | where |
|---|---|---|
| 6 | up01.wav | hyper start |
| 7 | high_a.wav | Accel start |
| 0xd | _sen_mi_b_raifuru.wav | Photon Rifle, Scatter-shot |
| 0x12 | gun11_r.wav | Linear Ray / Destruction Ray start |
| 0x13 | don16.wav | beam hits, ray hits, blast hits |
| 0x14 | bom06.wav | rocket/missile explosion |
| 0x15 | crash17.wav | Photon Rifle hit (with 0x13), field hits |
| 0x1a | tm2_bom002.wav | Genocide blast, field destroyed |
| 0x1d | gun12.wav | rocket/missile shot down |
| 0x22 | burst00.wav | rocket/missile launch (lock 4; bomb lock 1) |
| 0x2f | power35.wav | field deploy |
| 0x34 | bomb2.wav | Destruction Ray side bursts (lock 10) |

---

## 7. Other per-character table entries (engine index 12)
| Table (real base) | Iru entry | Role |
|---|---|---|
| 0x881310 | FUN_006d3910 | input dispatch, standard (§2) |
| 0x881360 | FUN_006d3e60 | input dispatch, alternate (§2) |
| 0x8813b0 | FUN_006cd8b0 | sprite loader (§4.1) |
| 0x881400 + 0xc0 + 4·level | FUN_006d4b60 (level 0), FUN_006d43b0 (levels 1, 2) | CPU AI (not reversed; uses RNG and FUN_006d05c0) |
| 0x881878/0x8818c8/0x881918/0x881968 | 4abbd0 / 4abf90 / 4abd50 / 4abd70 | beams §5.1 |
| 0x8819b8/0x881a08/0x881a58/0x881aa8/0x881af8/0x881b48 | 665eb0 / 668010 / 666160 / 667c90 / 667ec0 / 0 | missiles §5.2 |
| 0x881b98/0x881be8 | 5e2870 / 5e2c40 | hitboxes §5.3 |
| 0x881c38/0x881c88/0x881cd8/0x881d28 | 61e3a0 / 61eff0 / 61e500 / 61eec0 | lasers §5.4 |
| 0x881558/0x8815a8/0x8815f8/0x881648 | 51f210 / 51fcf0 / 51f350 / 51fab0 | enemies (spawn/draw/update/onKilled) §5.5 |
| 0x881698/0x8816e8/0x881738 | 4f7a00 / 4f9540 / 4f7d20 | effects §5.6 |
| 0x881d78/0x881dc8/0x881e18 | 7c50e0 / 7c5a80 / 7c56f0 | whip §5.7 |

---

## 8. Stats record (index 12, base 0x8841d0), FUN_0049ae40 (registry.c:1905-1937)
| Off | Value | Meaning |
|---|---|---|
| +0x00 | 4.0 (DAT_00853b68) | walk speed per axis |
| +0x04 | 14.0 (DAT_00813a54) | dash speed |
| +0x08 | 12.0 (DAT_00813f54) | UNVERIFIED (same as Suguri) |
| +0x0c / +0x10 | 2 / 6 | UNVERIFIED ints (Suguri: 0 / 5) |
| +0x14..+0x20 | 0 | |
| +0x3c/+0x40/+0x44 | 0x175, 0x176, 0x177 | sprite ids (rng / unregistered / system) |
| +0x48 / +0x60 | text 0xc / 0xd strings | colour folders |
| +0xa8 / +0x1a8 | `mdl\win_font_IR.lfd/.dat` | |
| +0x2a8 | `ir_res_bgp` | |
| +0x2c0..+0x2c8 | 0, 0, 0 | (Suguri 30, −70, 0) |
| +0x2cc..+0x2d4 | 140, 230, 160 (0x8c, 0xe6, 0xa0) | colour RGB |
| +0x2d8 | 0 | |

---

## 9. UNVERIFIED / open items
- RESOLVED: `+0xdc` order (a,r,g,b) (render.md corrected); texture blit +0x68 = FUN_0045c470 (§5.4); effect draws ported 1:1 (§5.6); whip vertex layout (§5.7).
- Verified by tests/test_iru.c (dispatch both schemes, spawn frames, costs, damage totals 360 / 810 / 1820, field ticks + push-out after 12 combo hits, team-3 self-hit).
- RESOLVED: effect `+0x14` = draw layer (`FUN_004f4de0(n)` draws effects whose +0x14 == n; layer 0 before players, 1 after).
- Player `+0x108` meaning (set to 25 by Genocide Bomb) — core.md.
- Whether Iru really takes her own Genocide blast in practice depends only on the bubble timing (50
  frames from t=0 vs bomb flight time) — code says she can.
- Intermediate-version cost/gain DATs listed in §1.2 (not needed for 0x5a2).
- CPU AI (FUN_006d4b60 / FUN_006d43b0) not reversed.
- Stats +0x08, +0x0c, +0x10 meaning (core.md).
