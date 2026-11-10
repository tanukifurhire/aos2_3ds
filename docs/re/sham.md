# Sham — complete moveset spec

Source: `aos2.exe` (Ghidra corpus `tools/re/dec/all.c`, constants read with `xref.exe rd`, sprite ids
from `xref.exe dis` immediates). Every number cites its address. `t` = player state timer `p+0x154`;
`t2` = enemy object timer `e+0x80`. Structure follows suguri.md; shared helpers (FUN_007807f0 cost
table, FUN_006b1b40 hyper start, FUN_00780910 spend, FUN_00780950 hyper permission, dash cancel
FUN_006b17d0, training-only FUN_00612830/FUN_00612900, RNG FUN_00459fd0/FUN_00459f80, SE
FUN_007bc9c0/FUN_007bcaf0) are used with the meanings given in suguri.md §1 and core.md, and are not
re-derived here. Version: implement **ver = 0x5a2** (core.md §0.3); old-version branches are noted
only where they exist.

Engine index **7** (handbook id 4). States 0xa7–0xbe. Sprite prefix `sm_`.

---

## Notes for the lead (affect other docs / all characters)

1. **render.md colour order is wrong.** CModel `+0xdc` = `FUN_0046ef70(a, r, g, b, meshIdx)`: it stores
   param 2 into `local_40` (= `.a` of a `{r,g,b,a}` D3DCOLORVALUE) and params 3..5 into r,g,b
   (FUN_0046ef70 body). So the **first float is alpha**. The global tint `FUN_004e8f50` therefore
   passes `DAT_00893da0` as alpha and `DAT_00893da4..dac` as r,g,b. Evidence in use: Sham's sphere
   draw passes `(pulse*bright, bright, bright, bright)` and `(0.5, b, b/2, b)` (FUN_00543860 case 400).
2. **suguri.md §0.1 address typo:** "record 7 at 0x883374 is Sham" — record 7 starts at
   `0x881f80 + 7*0x2dc = 0x883384` (registry.c:1558 writes walk speed there). `0x883374..0x88337c`
   (0xdc, 0x5a, 0x82) are **Mira's** RGB (+0x2cc..+0x2d4 of record 6, registry.c:1521-1523).
   core.md §7 already has the right values.
3. **Two global counters must be part of the deterministic sim state:**
   - `DAT_0087e7bc` — Sham's sprite-spin alternation counter. One global shared by *both* players,
     reset to 0 at t=0 of states 0xb1/0xb2/0xb3/0xb4, incremented per spawned sprite. It is saved
     in the rollback snapshot (all.c:288611 `DAT_0087e7bc = *(param_1 + 0x189c)`), confirming it is
     sim state.
   - `DAT_00885984` — Enemy "group id" counter (`FUN_0055efc0`, written to `e+0x04` of every part of
     one spawn). Global across both players and all characters. **Dwarf side selection uses the
     parity of this id** (§5.4), so every enemy spawn by anyone shifts it. objects.md calls `+0x04`
     "serial"; it is shared by all parts of one multi-part spawn and used to find the root
     (`FUN_0055eff0(groupId)` = first enemy with `parent == -1 && +4 == groupId`).
4. **Sham needs the solid-enemy push-out (objects.md §4.8, still UNVERIFIED there):** cubes 300/0x12d
   are solid **rectangles** {30,30} (§5.6), the Resistant Sphere (400) is a solid circle growing to
   r≈203.5, and the Arachne body (500) is a solid rect {30,120}+circle 60. The rect push geometry
   (`FUN_0060e110`, `FUN_005f15a0`, `FUN_005f32a0`) must be finished for Sham.
5. **Sham's Effect family is gameplay-relevant.** Type-7 effect update `FUN_00506640` spawns Beams
   (Beam Ribbon effect 500) and Hitboxes (Arachne trail 600, Arachne explosion 0x262). So the Effect
   list (std::list, update pre-increments age — objects.md §1.4) must be simulated deterministically,
   not treated as cosmetic.
6. Sham hitbox on-hit callbacks 400 / 0x19a / 500 **overwrite the hitbox angle** with
   `atan2(victim − hitbox)` before damage/hitstun are applied (FUN_005e96e0), giving radial
   knockback. This relies on objects.md §4.4 "callback runs before damage" — keep that order.
7. Status effect k=3 ("cannot dash", `+0xa6/+0xb8`) is applied by Sham's Delta Sprite via
   `FUN_006b1a40(opp, 3, 200)` (core.md §6.8).
8. `FUN_00617110` (post-round win-quote selection) calls `FUN_00459f80` (game RNG) and has Sham
   special cases (all.c:303852-303980). Not gameplay, but it consumes RNG — relevant only if RNG
   state is carried across rounds.

---

## 0. Player fields used by Sham's code (beyond suguri.md's table)

| Off | Use here |
|---|---|
| +0x04 | owner id → enemy `+0x08`, beam/hitbox owner |
| +0x14 | team → enemy `+0x14` |
| +0x18 | player slot → enemy `+0x18` (sprite bank for object draws) |
| +0x84 | at t=0 of most states set to **π if opp.x ≤ self.x else 0** ("face", below), or to aim for hypers / Dwarf Tackle |
| +0x88 | copy of +0x84 ("face") |
| +0x9c | friction ×0.5 each frame in 0xb2, 0xb9, 0xba (DAT_00853b1c) |
| +0xe8 | ammo A; Resistant Sphere zeroes it (§3 0xba) |

"face" = `opp = FUN_00780480(p+0x14); if (opp.x <= p.x) {+0x84 = π (DAT_00853b58)} else {+0x84 = 0}; +0x88 = +0x84`
(e.g. FUN_0072b740 t==0). "aim" = `+0x84 = FUN_007804b0(p)` (angle to opponent).

---

## 1. Shared mechanics

### 1.1 Ammo / hyper cost table entries (`0x87c4f0 + state*0xc`, registry.c:1591-1611, ver patch FUN_004a7270)

| State | Move | costA | costB | hyperGain |
|---|---|---|---|---|
| 0xb1 | Sprite Call (per sprite) | 0.2 (DAT_00853b14) | 0 | 0.02 (DAT_00813ed8) |
| 0xb3 | (unused 0x6e sprite call) | 0.2 | 0 | 0.025 (DAT_00813edc) |
| 0xb7 | Dwarf Tackle (per dwarf) | 0 | 0 | 0; **ver>0x5a0: 0.05** (DAT_0087cd8c = DAT_00813ef4, FUN_004a7270) |
| 0xb9 | Beam Ribbon (per shot) | 0.15 (DAT_00813f20) | 0 | 0.015 (DAT_00813ed4) |
| 0xba | Resistant Sphere | 0.5 (DAT_00853b1c) | 0 | 0.1 (DAT_00853b10); **ver>0x5a0: 0.05** (DAT_0087cdb0) |
| 0xbb | Mysterious Cube (tap) | 0 | 0.3 (DAT_00813f28) | 0.05 (DAT_00813ef4) |
| 0xbc | Mysterious Cube (hold) | 0 | 0.5 (DAT_00853b1c) | 0.1 (DAT_00853b10) |

Not charged at all: 0xb2 Sprite Release, **0xb6 Dwarf Call** (FUN_0072c8c0 never calls FUN_007807f0;
table entry 0x87cd78.. is never written = 0), and the hypers.

### 1.2 Hyper flourish (t=0 of hypers)
SE 6 (`up01.wav`), `FUN_004e9280(100, 0.75 DAT_00813f3c)`, `FUN_007bec20(100, pos, X, Y)`,
`FUN_004d7ac0(20, 0.5, 2)` — cosmetic screen effects (as suguri.md §1.4). X,Y = (0,0) for 0xb4, 0xb5, 0xbd;
(aim, π/4 DAT_0081fa90) for 0xb8. Accel 0xbe: `FUN_004e9280(150, 0.85 DAT_00814120)`,
`FUN_007bec20(100,pos,0,0)`, `FUN_004d7ac0(100, 0.6 DAT_00813f34, 2)`.

### 1.3 "Deployed" tests (how Dwarf Tackle / Sprite Release are gated)
There is **no player field**; dispatch scans the Enemy list:
- **Dwarfs present** — `FUN_0072a8a0(ownerId)`: index of the first enemy with `+0x08 == ownerId &&
  +0x0c == 7 && +0x10 == 200 && +0x7c == 200`, else −1. Matches a normal dwarf in **any** phase
  (growing, idle, following, tackling). Rush dwarfs (`+0x7c = 0xc9`) do not count.
- **Sprites present** — inline in FUN_0072e300: any enemy with `+0x08 == ownerId && (+0x10 == 100 ||
  +0x10 == 0x6e) && +0x84 == 10` (deployed/orbiting phase). Note: no `+0x0c` type check. Sprites still
  growing (phase 0), released (`+0x10 = 0xbe`) or Poly Sprites (phase 0x14) do not count.

### 1.4 Enemy field names used below (Sham's use of objects.md §2.6)
`+0x10` **kind** (move id; also tested by dispatch/draw/onKilled), `+0x7c` **behaviour** (the update
switch key, FUN_0053e810 `param_1[0x1f]`), `+0x84` **phase**, `+0x80` `t2` (++ after update),
`+0x78` age (++ after update), `+0x58` heading/draw angle, `+0x5c` aux angle (orbit / travel
direction), `+0x60/+0x64` scaleX/Y (draw), `+0x68` brightness (draw colour r=g=b, 0→1 fade-in),
`+0x70` speed, `+0x74` turn rate, `+0x24` hp, `+0x2c` active, `+0x20` solid, `+0x21` invulnerable,
`+0x30/34/38` shape {halfW, halfH, r}, `+0x90` parent id, `+0x94..` child ids, `+0x88/+0x8c` motion
id/frame (objects.md §3.5).

---

## 2. Input dispatch `FUN_0072e300` (standard scheme) — exact decode

**A. Hypers** — only if `FUN_00780950(p)`. First match wins; if the spend fails, the next row is tried.

| # | Condition | Cost | New state | Official move |
|---|---|---|---|---|
| 1 | S held (`+0x40>1`) && B held (`+0x3c>1`) && H pressed (`+0x44==3`) | 3.0 (DAT_00813a50) | 0xbe | **Arachne Call** (Accel, S+B+H) |
| 2 | S held && H pressed | 1.0 (DAT_00853b34) | 0xb8 | **Dwarf Rush** (S+H) |
| 3 | B held && H pressed | 1.0 | 0xbd | **Bounding Cube** (B+H) |
| 4 | **Dash held** (`+0x34>1`) && H pressed | 1.0 | 0xb5 | **Delta Sprite** (D+H) |
| 5 | H pressed | 1.0 | 0xb4 | **Poly Sprite** (H) |

Row 1 sets `state=0xbe, t=0` but **does not clear `+0x158`** (rows 2-5 do) — harmless for Sham (nothing reads it).

**B. Normal moves** — only if `state == 0 || state == 2`:

| # | Condition | Result | Move |
|---|---|---|---|
| 1 | Dash pressed (`+0x34==3`) | `FUN_006b17d0(p,0)` | Dash |
| 2 | Shield pressed && `FUN_00780910(p,0.5)` && `FUN_007809e0(p)==1` | `FUN_006b1930(p,0)` | Shield |
| 3 | S held && B pressed | **0xb7 if dwarfs present (§1.3), else 0xb6** | **Dwarf Tackle** / **Dwarf Call** |
| 4 | S held && A pressed | 0xba | **Resistant Sphere** |
| 5 | B pressed | 0xbc (→ 0xbb on early release) | **Mysterious Cube** (hold / tap) |
| 6 | Dash held (`+0x34>1`) && A pressed | **0xb2 if sprites deployed (§1.3), else 0xb9** | **Sprite Release** / **Beam Ribbon** |
| 7 | A pressed | 0xb1 | **Sprite Call** (hold to repeat) |

Each sets `state, t=0, +0x158=0` and calls the new T2 immediately; otherwise `return T2[state](p) != 0`.
Note row 6 tests `+0x34 > 1` (held), unlike Suguri's `> 0`.

**Alternate scheme `FUN_0072e890`** (`p+0x0c == 1`, registry 0x88137c): identical except the hyper rows
are "H held (`+0x44>1`) && X pressed": 1. Shield (`+0x48==3`) → 0xbe (3.0); 2. S (`+0x40==3`) → 0xb8;
3. B (`+0x3c==3`) → 0xbd; 4. Dash (`+0x34==3`) → 0xb5; 5. A (`+0x38==3`) → 0xb4 (diff of the two fns).

---

## 3. States

Common structure of every attack state: at the end, `if (t >= C && Dash == 3) FUN_006b17d0(p,1)` (dash
cancel; `FUN_00612900` at t == C), `else if (t > E) state = 0` (timer not reset). C/E listed per state.

### 0xa7 — neutral movement (T2 FUN_0072b3b0, from common state 0 `case 7`, FUN_006af390)
Identical to Suguri 0x128: each held direction adds ±`stats+0` = 5.0 per axis (not normalised);
`+0x84 = 0` if facing==1 else π.

### 0xa8, 0xaa, 0xab — stubs (FUN_0072b4a0 / 0072b4b0 / 0072b4c0 return 1). Used only for their T1 (§4.3).

### 0xa9 — KO hook (T2 FUN_0072b4d0, called every frame of common state 6 via `case 7`, FUN_006b0110)
At **t == 0 only**, for every enemy with `+0x08 == p+4`:
- behaviour (`+0x7c`) 100 or 0x6e (sprites, incl. released ones), 200 (normal dwarfs),
  300/0x12d/0x15e/0x15f (cubes): `t2 = 0, active = 0, phase = 0x5a` (dismiss: shrink-out, or for dwarfs
  onKilled + remove, §5).
- behaviour 400 (sphere): `t2 = 0, active = 0, phase = 900`.
- **Not dismissed** (bug-compatible): Poly Sprites (`+0x7c = 0x78`), rush dwarfs (0xc9), Delta Sprite
  (0x96/0xa0) and Arachne (500) keep running.

### 0xb1 — Sprite Call (T2 FUN_0072b740) — A
| t | Action |
|---|---|
| 0 | face; `DAT_0087e7bc = 0` |
| 9 (ver≤0x5a0: 12) | if `FUN_007807f0(p,0xb1)` (A −0.2): spawn enemy **100** (§5.1) via `DAT_00881574`; `DAT_0087e7bc++`; pos = player pos + (dx, dy) where dx = +70 (DAT_008145ec) if Right held, then −70 (DAT_008256c8) if Left held (Left wins), dy = +70 if Up, then −70 if Down (Down wins), else 0 (ver≤0x5a0: random ±100 per axis instead); `e+0x58 = +0x84` (facing); `e+0x5c = rand_f(−π, π)` (DAT_00814100, DAT_00853b58); `e+0x70 = 5.0` (DAT_00813f40); `e+0x74 = 0.01` (DAT_00813ec8), **negated if `DAT_0087e7bc` is even** (×−1 DAT_00853bb4); SE `FUN_007bcaf0(0x2f,4)` (`power35.wav`) |
| 13 (ver≤0x5a0: 16) | if A held (`+0x38>1`): **t := 7** (ver≤0x5a0: 10) — hold loop: one sprite every 6 frames while A held and ammo lasts. The dash-cancel/end checks are skipped on this frame. |
| C=20, E=30 | |

### 0xb3 — unused Sprite Call variant (T2 FUN_0072bb80)
Same as 0xb1 but spawns enemy **0x6e** at pos + (rand_f(−100,100), rand_f(−100,100)) (DAT_00853bb8,
DAT_00853b90), `e+0x58 = rand_f(−π,π)`, `e+0x5c = rand_f(−π,π)`, `e+0x70 = 7.5` (DAT_00814150),
`e+0x74 = ±0.05` (DAT_00813ef4), cost entry 0xb3 (but it calls `FUN_007807f0(p,0xb1)`). **No code sets
state 0xb3** (only UI references at all.c:314400/314714/319563); it is still registered. Implement for
completeness only.

### 0xb2 — Sprite Release (T2 FUN_0072bf10) — D+A while sprites deployed
| t | Action |
|---|---|
| 0 | face; `DAT_0087e7bc = 0` |
| 10 | `c = 0`; for each enemy in list order with `+0x08 == p+4`, `+0x10 ∈ {100, 0x6e}`, `+0x84 == 10`: `+0x10 = 0xbe` (released kind), `+0x7c = 100`, `t2 = c*10`, `+0x84 = 0x14` (homing), `+0x74 = 0.025` (DAT_00813edc), `+0x70 = 20` (DAT_008140fc); `c++`. (No cost, no SE.) |
| every frame | friction ×0.5 |
| C=20, E=30 | |

### 0xb9 — Beam Ribbon (T2 FUN_0072db50) — D+A without deployed sprites
| t | Action |
|---|---|
| 0 | face |
| 15, 17, 19, 21 | if `FUN_007807f0(p,0xb9)` (A −0.15): Sham effect **500** (§5.8) via `DAT_008816b4(500, colour DAT_008859f4+team*0x10, &pos, rand_f(−π,π), p+4, 0, team)`; SE `FUN_007bcaf0(0x10,3)` (`laser03.wav`) |
| every frame | friction ×0.5 |
| C=25, E=35 | |
Up to 4 ribbons; each later fires 6 curving beams (§5.8).

### 0xbc — Mysterious Cube, hold (T2 FUN_0072d490) — B
| t | Action |
|---|---|
| 0 | face |
| t < 12 && B up (`+0x3c < 1`, i.e. value 0 — the frame *after* release) | `+0x84 = aim`; state := **0xbb** (t kept) and run T2[0xbb] now; nothing else this frame |
| 15 | if `FUN_007807f0(p,0xbc)` (B −0.5): for k = −6..5: enemy **300** (§5.6), `e+0x58 = 0`, `e+0x5c = k/6·π` (DAT_00813f44 = 6), pos = player pos + 100·(cos,sin)(e+0x5c) (DAT_00853b90) — a full ring of 12, independent of aim; SE `FUN_007bcaf0(0x2f,4)` |
| C=25, E=40 | |

### 0xbb — Mysterious Cube, tap (T2 FUN_0072d210) — via 0xbc early release
| t | Action |
|---|---|
| 0 | face (only if entered at t=0 — never happens via 0xbc) |
| 12 | if `FUN_007807f0(p,0xbb)` (B −0.3): for k = −3..3: enemy **300**, `e+0x58 = 0`, `e+0x5c = +0x84 + k/8·π` (DAT_008140d4 = 8), pos = player + 100·(cos,sin)(e+0x5c) — a 7-cube fan toward the opponent (aim set by 0xbc); SE `FUN_007bcaf0(0x2f,4)` |
| C=20, E=30 | |

### 0xba — Resistant Sphere (T2 FUN_0072dd40) — S+A
| t | Action |
|---|---|
| 0 | face |
| 10 | if `FUN_007807f0(p,0xba)` (A −0.5): enemy **400** (§5.7) at player pos; `e+0x24 (hp = duration) = (int)(ammoA * 100) + 50` (ver>0x5a0, DAT_00853b90; older ×200 DAT_008147cc), using ammo A *after* the 0.5 cost; then **`p+0xe8 = 0`** (all remaining ammo A consumed). Two Sham lasers **100** (§5.9) at `&e.pos`, angles +π/6 (DAT_00817ddc) and −π/6 (DAT_00825d48), ownerId = **enemy id** `e+0`, team `e+0x14`, p18 `e+0x18`. Common effect 0xaf0 at player pos; SE 0xb (`eco03.wav`) |
| every frame | friction ×0.5 |
| C=25, E=35 | |

### 0xb6 — Dwarf Call (T2 FUN_0072c8c0) — S+B, no dwarfs present
| t | Action |
|---|---|
| 0 | face |
| 15 | enemy **200** (normal dwarf, §5.4) at player pos + (50, 0) (DAT_008140ec), `e+0x58 = +0x84`; SE `FUN_007bcaf0(0x2f,4)` |
| 20 | second dwarf at player pos − (50, 0), same |
| C=25, E=35 | |
No cost. (Spawn order matters: the two dwarfs get consecutive group ids → opposite follow sides, §5.4.)

### 0xb7 — Dwarf Tackle (T2 FUN_0072cb30) — S+B, dwarfs present
| t | Action |
|---|---|
| 0 | aim |
| 5 (ver≤0x5a0: 10) | **ver>0x5a0 only:** count enemies with `+0x10 == 200 && +0x08 == p+4` (any phase, includes rush dwarfs); if < 2: spawn enemy **200** at player pos, `e+0x58 = aim`, SE `FUN_007bcaf0(0x2f,4)`. Then `k = 0`; for each enemy (list order) with `+0x10 == 200 && +0x7c == 200 && (+0x84 == 10 || +0x84 == 99) && +0x08 == p+4`: if `FUN_007807f0(p,0xb7)` (free, +0.05 hyper): `+0x84 = 0x14`, `t2 = 90 − 20k` (so dwarf k charges 10+20k frames later), `k++`; and for its parts (enemies with same group id `+0x04`, owner p+4): kind 0xd2 → motion `+0x88 = 5, +0x8c = 0`; kind 0xdc → behaviour `+0x7c = 0xdc` (stop firing), `t2 = 0`, motion 6, frame 0. If k > 0: SE 0x2a (`A5_01029.wav`). |
| C=25, E=35 | |
Dwarfs in phase 0xb (the 100-frame "returning" phase after a tackle) or still growing are **not** tackled.

### 0xb4 — Poly Sprite (T2 FUN_0072c170) — H, 1 level
| t | Action |
|---|---|
| 0 | aim; `FUN_006b1b40(p,10)`; flourish (§1.2); `DAT_0087e7bc` is **not** reset here |
| 3 < t < 45 and t%4 == 0 (t = 4, 8, …, 44 → **11 sprites**) | enemy **0x78** (§5.2) at player pos + (rand_f(−150,150), rand_f(−150,150)) (DAT_008149ec, DAT_0081414c); `e+0x58 = rand_f(−π,π)`, `e+0x5c = rand_f(−π,π)`, `e+0x70 = 7.5`, `e+0x74 = 0.05` (×−1 if `DAT_0087e7bc` even after the ++); (0x70/0x74 are overwritten when it finishes growing); SE `FUN_007bcaf0(0x2f,4)` |
| C=45, E=80 | |

### 0xb5 — Delta Sprite (T2 FUN_0072c4b0) — D+H, 1 level
| t | Action |
|---|---|
| 0 | aim; `FUN_006b1b40(p,10)`; flourish |
| 10 | three times, with direction d = aim+π, aim+π/3 (DAT_008203bc), aim−π/3: enemy group **0x96** (§5.3) at player pos, centre `e+0x5c = d`, `e+0x70 = 10` (DAT_00813f4c); then Sham effect **400** `(400, 0, &e.pos, 0, owner = e.id(+0), 0, team = e+0x14)` (triangle visual). After the three: SE 0x2f |
| C=35, E=50 | |

### 0xb8 — Dwarf Rush (T2 FUN_0072cf80) — S+H, 1 level
| t | Action |
|---|---|
| 0 | aim; `FUN_006b1b40(p,10)`; flourish (cut-in angle = aim, π/4) |
| 10 + 3k, k = 0..9 (10, 13, …, 37) | enemy **200** with behaviour `e+0x7c = 0xc9` (rush dwarf, §5.5) at player pos + (rand_f(−150,150), rand_f(−150,150)); `e+0x58 = e+0x5c = aim` (aim from t=0). No SE. |
| C=50, E=80 | |

### 0xbd — Bounding Cube (T2 FUN_0072d770) — B+H, 1 level
| t | Action |
|---|---|
| 0 | face; `FUN_006b1b40(p,10)`; flourish |
| 10 | for k = −5..4: enemy **0x12d** (solid cube) with `e+0x58 = 0`, `e+0x5c = k/5·π` (DAT_00813f40 = 5), pos = player + 100·(cos,sin)(e+0x5c); and enemy **0x15f** (invulnerable cube) with `e+0x5c = k/5·π + π/10` (`_DAT_00825d44` = 0.31415927), same radius → 20 cubes alternating. No SE. |
| C=30, E=60 | |

### 0xbe — Arachne Call (T2 FUN_0072e070) — S+B+H, 3 levels
| t | Action |
|---|---|
| 0 | aim; `FUN_006b1b40(p, 0x14)` (Accel barrier; ver<0x58b: kind 10); SE 6; Accel flourish (§1.2) |
| 10 | enemy group **500** (§5.10) at player pos − 100·(cos,sin)(aim); `e+0x58 = e+0x5c = aim`; SE 0x33 (`synth-sweep1.wav`) |
| C=40, E=100 | |

---

## 4. Animation

### 4.1 Sprite registration (loader `FUN_00727e40`)
Arg = colour variant: 0 → folder string 0x1e, else 0x1f (charaShamA / charaShamB; both 106 files).
Same mechanism as suguri.md §4.1 (S×S quad, storage `0x885b80 + slot*0x3888 + id*0x18`).

| Sprite id | Pattern | Frames | S | Used by |
|---|---|---|---|---|
| 0xa7 | `sm_std0%d` | 3 | 96 (DAT_00814118) | neutral |
| 0xac | `sm_fwd0%d` | 3 | 96 | neutral, moving toward |
| 0xad | `sm_bck0%d` | 3 | 96 | neutral, moving away |
| 0xa9 | `sm_los0%d` | 6 | 96 | T1 0xa9 (KO), 0xaa (hit) |
| 0xab | `sm_sld0%d` | 5 | 96 | T1 0xab (shield), 0xbb, 0xbc, 0xbd |
| 0xa8 | `sm_dsh0%d` | 6 | 96 | dash |
| 0xae | `sm_rng0%d` | 2 | 512 (DAT_008178b0) | stats +0x3c (UI) |
| 0xb0 | `sm_system` | 1 | 512 | stats +0x44 (UI) |
| 0xb6 | `sm_atka0%d` | 4 | 96 | T1 0xb6, 0xb7, 0xb8, 0xba, 0xbe |
| 0xb1 | `sm_atkb0%d` | 5 | 96 | T1 0xb1–0xb5, 0xb9 |
| 0xbf | `sm_wisp0%d` (0–9), `sm_wisp%d` (10, 11) | 12 | 64 (DAT_008140f8) | sprites (enemy 100/0x6e/0x78/0xa0–0xa2/0xbe) |
| 0xc0 | `sm_wisp_fld` | 1 | 64 | Delta triangle (effect 400) |
| 0xc1 | `sm_dwarf0%d` / `sm_dwarf%d` | 16 | frames 0–11: 144 (DAT_00814138); 12–15: 72 (DAT_008178ac) | dwarf body 0–11, parts 12–15 |
| 0xc2 | `sm_impact%d` | 5 | 96 | sphere (frames 3, 4); effect 100 (frames 0, 1) |
| 0xc3 | `sm_cube0%d` | 6 | 72 | cubes 300/0x12d |
| 0xc4 | `sm_cubeb0%d` | 6 | 72 | cubes 0x15e/0x15f |
| 0xc6 | `sm_rcore0%d` | 5 | 48 (DAT_008140e4) | dwarf missile (0,1,2), dwarf beam (4,3) |
| 0xc5 | `sm_arach0%d` | 9 | 384 (DAT_0082524c) | Arachne body 0–2, legs 3–8 |
| 0xc7 | `sm_exp0%d` | 3 | 256 (DAT_00814160) | Arachne explosion (effect 0x262) |
| whip slots 2, 3 | `sm_whip0`, `sm_whip1` | — | — | ribbon textures `DAT_0087ff38 + slot*4 + variant*0x3c` (whip family §5.11) |
`sm_rng`, `sm_system`, `res_*` are UI. All 106 files of charaShamA are covered. 0xaf is a stats sprite id
(+0x40) with no loader entry (like Suguri's 0x130). `FUN_0072a930` builds the pause-menu command list
(text ids 0x325..) — no gameplay.

### 4.2 Draw transforms
- **AIM transform** (all Sham T1 except 0xa7), identical to suguri.md §4.2: pos = player pos; if
  `|+0x84| >= π/2`: rotZ(+0x84), rotX(180°), rotY(180°); else rotZ(+0x84), rotY(180°).
- **Neutral** 0xa7: no rotZ; rotY(180°) if facing (+0x8c) != 0.
- Every player draw is preceded by the global tint `FUN_004e8f50`; blend stays normal.
- Heat afterimage `FUN_004d7e00(model, 1, p, {0,0,0} / angles, 0)` only in 0xa7 and 0xa8.

### 4.3 Common states → Sham T1 (FUN_006b07a0 … FUN_006b1580, `case 7`)
| Common state | Sham T1 |
|---|---|
| 0 neutral | 0xa7 FUN_0072fca0 |
| 1 hit / 5 | 0xaa FUN_00730350 |
| 2 dash | 0xa8 FUN_0072ff50 |
| 3 shield / 4 | 0xab FUN_00730620 |
| 6 KO | 0xa9 FUN_00730900 |

### 4.4 Per-state frames
`pp(n)` = `(t/n)&3`, 3→1 (0,1,2,1). `loop3` = `f = t/3; if f > 1: f = ((f−2)&3)+2; if f == 5: f = 3`
→ 0,1,2,3,4,3,2,3,4,3,… (FUN_00730620). `ramp3` = `min(t/3, 3)`.

| State | T1 | Sprite | Frame |
|---|---|---|---|
| 0xa7 | FUN_0072fca0 | 0xa7; overrides in order (later wins): Up→0xac; Down→0xad; facing0&Left→0xac; facing1&Left→0xad; facing0&Right→0xad; facing1&Right→0xac (disasm 0x72fcc5–0x72fd95) | pp(5) |
| 0xa8 | FUN_0072ff50 | 0xa8 | pp(3), +3 if moving away (facing1 && \|+0x84\| > π/2, or facing0 && \|+0x84\| < π/2) |
| 0xa9 | FUN_00730900 | 0xa9 | pp(3) + 3 if global frame `DAT_008858f8` even; **not drawn when t ≥ 100** |
| 0xaa | FUN_00730350 | 0xa9 | pp(3) |
| 0xab | FUN_00730620 | 0xab | loop3 |
| 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb9 | FUN_00730c00 / 007311c0 / 00730ee0 / 007314a0 / 00731780 / 00732b40 | 0xb1 | loop3 |
| 0xbb, 0xbc, 0xbd | FUN_007322a0 / 00732580 / 00732860 | 0xab | loop3 |
| 0xb6, 0xb7, 0xb8, 0xba, 0xbe | FUN_00731a60 / 00731d20 / 00731fe0 / 00732e20 / 007330e0 | 0xb6 | ramp3 |

### 4.5 Enemy draw `FUN_00543860` (type 7, draw table 0x8815c4) — switch on **kind** `+0x10`
Sprite bank = enemy `+0x18` (player slot). Unless stated: pos = e.pos; colour `(a=1, r=g=b=+0x68)`;
scale (`+0x60`, `+0x64`, 1); "AIMe" = the AIM transform using `e+0x58`. Pattern **G(f0; f1, f2)** =
draw frame f0 with blend 1, then frames f1, f2 with blend 2 (additive), then blend 1 restored.
(The `FUN_0045a680(age%50…)` calls before the additive passes are dead code — results unused.)

| Kind | Sprite | Draw |
|---|---|---|
| 100, 0x6e, 0xa0–0xa2 | 0xbf | G((age/4)%4; 4, 5), no rotation |
| 0xbe (released) | 0xbf | G(6+(age/4)%4; 10, 11), AIMe |
| 200 (dwarf body, incl. rush) | 0xc1 | base b = 3 if phase 10; 6 if phase 0x14 && speed ≥ 5; 9 if phase 0x14 && speed < 5; else 0 → G(b; b+1, b+2), AIMe |
| 0xd2 (dwarf part) | 0xc1 | frame 12, blend 1, AIMe — **only if** root missing, or root phase ∉ {10} and not (root phase 0x14 && root speed < 5) (`FUN_0055eff0(e+4)`) |
| 0xdc (dwarf gun) | 0xc1 | same condition: G(13; 14, 15), AIMe |
| 300, 0x12d | 0xc3 | pass 1: frame (age/5)%3+3, blend 1, alpha = (sin((age%50)/50·2π)·0.35 + 0.65)·b (DAT_00813f2c, DAT_0081788c), scale ×1.15 (DAT_00817dfc), rotZ(+0x58); pass 2: frame (age/5)%3, blend 2, alpha 1, normal scale, rotZ |
| 0x15e, 0x15f | 0xc4 | as 300 |
| 400 (sphere) | 0xc2 | frame 3, blend 1, alpha = (sin((age%50)/50·2π)·0.15 + 0.6)·b (DAT_00813f20, DAT_00813f34), rgb b, scale `+0x60`·5 (both axes), rotZ(+0x58); frame 4, blend 2, colour (0.5, b, b/2, b), same scale/rot |
| 500 (Arachne body) | 0xc5 | G(0; 1, 2), AIMe |
| 0x1fe / 0x208 (legs) | 0xc5 | G(3; 4, 5); rotation uses e+0x58 but the X-flip test uses the *root's* `|+0x58| ≥ π/2`; 0x208 applies rotX *after* rotY in its flipped branch |
| 0x212 (tail) | 0xc5 | G(6; 7, 8), AIMe |
| 0x96, 0xc9, … | — | kinds not listed are not drawn (Delta centre is invisible; rush dwarfs keep kind 200) |

---

## 5. Spawned objects

All of Sham's object functions are type-7 entries in the shared family tables (registry), outside her
code block: Enemy spawn `FUN_0053d340` (0x881574), update `FUN_0053e810` (0x881614), draw `FUN_00543860`
(0x8815c4), onKilled `FUN_005434a0` (0x881664); Beam `FUN_004b2ed0`/`FUN_004b3a20`/`FUN_004b3200`/onHit
`FUN_004b3830` (0x881894/0x8818e4/0x881934/0x881984); Missile `FUN_00676e50`/`FUN_00677870`/`FUN_00676f70`/
`FUN_00677730`/`FUN_006777d0` (0x8819d4…0x881b14; absorb 0x881b64 = 0); Hitbox `FUN_005e92d0`/onHit
`FUN_005e96e0` (0x881bb4/0x881c04); Laser `FUN_00625190`/`FUN_00625660`/`FUN_00625290`/onHit `FUN_00625640`
(empty) (0x881c54/0x881ca4/0x881cf4/0x881d44); Effect `FUN_00504b80`/`FUN_00506640`/`FUN_00504f50`
(0x8816b4/0x881704/0x881754); Whip `FUN_007c71a0`/`FUN_007c7ce0`/`FUN_007c72c0` (0x881d94/0x881de4/0x881e34).

Enemy timing: the manager calls update, then `age++`, `t2++` (objects.md §1.4), so a fresh enemy's
first update sees t2 = 0. Every "grow" phase below is: `scaleX += 0.1` (DAT_00853b10),
`scaleY −= 0.2` (DAT_00853b14) from spawn values (0, 3); when `t2 > 9` → scale (1,1), next phase,
`t2 = 0` (10 frames). "Shrink" (phase 0x5a) is the reverse: `scaleX −= 0.1, scaleY += 0.2`, removed
when scaleX ≤ 0. Spawn records start from `FUN_0055ee70` defaults (scale 1,1,1, mirror 1, parent −1).

### 5.0 Enemy templates (`FUN_0053d340`; offsets recovered from stack locals)
| Spawn id | Parts (kind / behaviour) | hp | active | solid / invuln | shape {hx,hy,r} | other |
|---|---|---|---|---|---|---|
| 100 | 100/100 | 200 | 0 | — | {0,0,10} (DAT_00813f4c) | scale (0,3), bright 0, mirror 0 |
| 0x6e | 0x6e/0x6e | 100 | 0 | — | {0,0,10} | as 100 |
| 0x78 | **100**/0x78 | 200 | 0 | — | {0,0,10} | as 100 |
| 0x96 | centre 0x96/0x96 + children 0xa0, 0xa1, 0xa2 (behaviour 0xa0) | 200, **ver>0x5a0: 600** each | 0 | — | centre {0,0,0}; children {0,0,10} | children motion 2 (detached), scale (0,3) |
| 200 | body 200/200 + part 0xd2 (motion 4) + part 0xdc (motion 3) — chain body→0xd2→0xdc | body 800; parts 999 | body **1**; parts 0 | — | body {0,0,40} (DAT_008140e0); parts {0,0,0} | body mirrorChildren 1; 0xdc offset (+0xc8) = −20 (DAT_00814108), pivot (+0xbc) = 5 |
| 300 / 0x12d | kind=behaviour=id | 100 | 0 | **solid** | {30,30,0} (DAT_008140e8) | scale (0,3) |
| 0x15e / 0x15f | kind=behaviour=id | 100 | 0 | **invulnerable** | {30,30,0} | |
| 400 | 400/400 | 200 (then overwritten = duration) | **1** | **solid** | {0,0,0} (radius set by update) | scale (0,0), bright 0 |
| 500 | body 500 + legs 0x1fe (motion 8), 0x208 (motion 0xb), tail 0x212 (motion 3), all children of body | 999 | body 1, legs 0 | body solid+invuln; 0x1fe/0x208 solid+invuln; 0x212 invuln | body {30,120,60} (DAT_008140e8, DAT_00814174, DAT_00813a58); 0x1fe/0x208 {100,30,0} | scale 1; 0x1fe pivot (136, 0) offset (0, 82) (DAT_00817dd8, DAT_00817db4); 0x208 pivot (136,0) offset (0,−82) (DAT_00817dc4); 0x212 offset (120, 0) |

The spawner returns the index of the first object (root); states then write pos/angles into it.
Sham's Enemy list objects are hit by the generic Beam/Hitbox/Missile/Laser→Enemy passes (objects.md
§4.5) when `active` and not `solid`; solid ones are only used by the push-out pass.

### 5.1 Sprite (spawn 100, behaviour 100) — Sprite Call
Phases (`+0x84`), FUN_0053e810 `case 100`:
- **0 grow** (10 frames), then phase **10**, `active = 1`, whip ribbon `DAT_00881d94(2, e+0x18, e.id)`.
- **10 deployed** — `bright += 0.1` (cap 1). Let `m = t2 % 200`:
  - `m < 30`: no movement. At **m == 1** (t2 = 1, 201, 401): `e+0x58 = atan2(opp − e)`; beams **100**
    (§5.12) at e.pos with angles `e+0x58 − π/6`, `e+0x58`, `e+0x58 + π/6` (DAT_00817ddc); common effect
    0x514 (team colour); SE `FUN_007bcaf0(0xf,4)` (`laser02.wav`). (ver≤0x5a0: one beam every 10
    frames while m < 30.)
  - `m ≥ 30`: orbit the owner: `e+0x5c += e+0x74` (wrap to (−π,π]); target = owner.pos +
    100·(cos,sin)(e+0x5c) (DAT_00853b90); `turnToward(&e+0x58, atan2(target − e), |e+0x74|·5)`
    (FUN_005f0e70, FUN_00492390); `pos += (cos,sin)(e+0x58)·e+0x70` (5.0).
  - `t2 > 598` (0x256) → phase **0x5a**, t2 = 0, active = 0.
- **0x14 homing** (after Sprite Release, or Delta remnants): speed `*= 0.975` (DAT_00814128) while
  speed > 5 (ver≤0x5a0: also when t2 > 200); orbit angle `e+0x5c += e+0x74`; target = opp.pos +
  (cos,sin)(e+0x5c)·speed·30 (DAT_008140e8); turn heading toward it by |rate|·5; move by speed;
  if `FUN_005f1c90(opp.pos, e.pos, opp.shape(+0x5c), e.shape, opp+0x84, e+0x58)` → `t2 = 250`.
  When `t2 > 249`: hitbox **0x96** (§5.13) at e.pos, angle `e+0x5c`; Sham effect 200; SE
  `FUN_007bcaf0(0x1d,4)` (`gun12.wav`); removed. (So a released sprite c homes for 250 − 10c frames max.)
- **0x5a shrink** → removed.
- Shot down (hp < 1 → onKilled FUN_005434a0, kinds 100/0x6e/0xbe): if phase 0x14 → **still explodes**
  (hitbox 0x96 + effect 200, angle `e+0x5c`); else common effect 600. Removed.

### 5.2 Poly Sprite (spawn 0x78, kind 100, behaviour 0x78)
Grow (10 frames) then: scale (1,1), `bright = 1`, rate `e+0x74 = 0.025`, speed `e+0x70 = 20`,
phase **0x14**, **t2 = 150**, active, whip ribbon. Phase 0x14 is identical to §5.1's homing → explodes
on contact or after 100 frames (t2 150→250). Kind is 100 but phase is 0x14, so it never counts as
"deployed". Not dismissed on KO (behaviour 0x78).

### 5.3 Delta Sprite (spawn 0x96: centre + 3 children)
Centre (behaviour 0x96; invisible, not collidable), each frame:
- phase **0**: `speed *= 0.95` (`_DAT_00814124`); move along `e+0x5c`; when t2 > 99: phase **100**,
  t2 = 0, speed 0, `e+0x5c = atan2(opp − e)`.
- phase **100**: speed += 0.025 (cap 2.0, DAT_00813a4c); move; when `!FUN_0061ac10(pos, 0)` (left the
  arena) → phase **200**, t2 = 0.
- phase **200** and t2 > 99: each live child → parent −1, kind 0xbe, behaviour 100, t2 0, phase 0x5a
  (shrink, harmless), bright 1; centre removed (return 0).
- Otherwise (all phases, after the above): look up the 3 children (`e+0x94..0x9c`); if fewer than 3
  exist → each survivor: parent −1, kind 0xbe, behaviour 100, t2 0, phase **0x14**, rate 0.025,
  speed 20, bright 1, `e+0x5c = atan2(child − centre)` (becomes a homing sprite, §5.1); centre removed.
  Else: if `FUN_005f1940(c0.pos, c1.pos, c2.pos, opp.pos)` (point in triangle, same-side-as-centroid
  test) → `FUN_006b1a40(opp, 3, 200)` (**no-dash status for 200 frames**, SE 0x15 `crash17.wav` on
  first application) and if opp state == 2 → opp state = 0 (dash cancelled). Then `e+0x58 += 0.025`
  (wrap above π).
Children (behaviour 0xa0): grow (10 frames, then active, phase 10, whip ribbon), then `bright += 0.1`
(cap 1); every frame pos = parent.pos + 100·(cos,sin)(parent+0x58 + δ), δ = 0 (kind 0xa0), +2π/3
(0xa1, DAT_00817898), −2π/3 (0xa2). Children are shootable (hp 600, r 10); losing one breaks the
triangle. Children update *after* the centre (list order), so the triangle test uses last frame's
child positions.
Visual: Sham effect 400 (§5.8) draws the triangle.

### 5.4 Dwarf (spawn 200, behaviour 200) — Dwarf Call / Tackle
Body (kind 200), FUN_0053e810 `case 200`:
- **0 grow** (10 frames) → phase **99**, t2 = 0; parts: 0xd2 → motion 5 (frame 0); 0xdc → behaviour
  **0xdd** (gun on), motion 6 (frame 0). (Body is active/shootable from spawn: hp 800, r 40.)
- **99 idle/shoot**: `e+0x58 = atan2(opp − e)`; bright += 0.1 (cap 1); `y += cos(((t2 % 100)/100)·π)`
  (bob). When t2 > 200 → phase **10**, t2 = 0; parts 0xd2 motion 5; 0xdc → behaviour 0xdc (gun off),
  part t2 = 0, motion 6.
- **10 / 0xb follow**: `e+0x58 = atan2(opp − owner)`; side = +π/2 if `e+0x04` (group id) is even,
  else −π/2 (DAT_00814104); target = owner.pos + 50·(cos,sin)(e+0x58 + side) (DAT_008140ec); each axis
  independently: `x −= 4` if target.x < x, `x += 4` if x < target.x (DAT_00853b68) (same for y).
  When t2 > 100 → phase **99**, t2 = 0; parts 0xd2 motion 5; 0xdc → 0xdd, part t2 = 0, motion 6.
- **0x14 tackle** (set by 0xb7 with t2 = 90 − 20k): at **t2 == 100**: `e+0x58 = e+0x5c = atan2(opp − e)`,
  speed 20, SE 0x20 (`swing_h.wav`). For t2 ≥ 100: move along `e+0x5c` by speed, then speed ×0.95; at
  t2 == 101: 5 cosmetic spark effects (0xa32 + rand%4, RNG); every frame with speed > 5: spark
  `FUN_007bd5e0(0x7da…)` (RNG); **if speed > 3 && t2%4 == 0: hitbox 200** (§5.13) at e.pos, angle
  `e+0x5c` → hits at t2 = 100, 104, …, 132 (**9 hits**; speed after t2=135 drops below 3 — float
  boundary UNVERIFIED). When t2 > 200 → phase **0xb** (follow), t2 = 0.
- **0x5a** (KO dismissal): `onKilled[7](e)` then removed.
Gun part (behaviour 0xdd, during the dwarf's phase 99): let `m = t2 % 200`; if `164 ≤ m < 194`
(ver≤0x5a0: < 184) and `(t2 − 164) % 3 == 0` → **10 shots** per idle cycle at m = 164…191: one
`a = part+0x58 + rand_f(−0.25, 0.25)` (DAT_0081411c, DAT_00813f24), then **beam 200** (§5.12) and
**missile 100** (§5.14) both at part pos with angle a, common effect 0x547 (colour `DAT_008859f8`),
SE `FUN_007bcaf0(0x10,5)`. The part's angle comes from the hierarchy attachment (parent angle ± motion
keyframe, objects.md §3.5; keyframes 3/4/5/6 in `FUN_00526670`, UNVERIFIED values).
Parts in behaviour 0xd2/0xdc/0xdd: grow → phase 10; phase 10: bright up, same cosine bob.
Dwarf killed (body hp < 1): onKilled FUN_005434a0 case 200: common effect 600 + 5×0x834 + 5×500
cosmetic bursts (RNG: 2 rand each), SE 0x1a (`tm2_bom002.wav`); removed; parts removed by the
orphan sweep FUN_00523370.

### 5.5 Rush dwarf (spawn 200, behaviour set to 0xc9 by state 0xb8)
- **0 grow** → speed 0, phase **0x14**, t2 = 0; parts 0xd2 motion 5, 0xdc motion 6 (gun stays 0xdc = off).
- **0x14**: at t2 == 0 (ver≤0x5a0: 10): speed 10 (DAT_00813f4c), SE `FUN_007bcaf0(0x20,10)`. For
  0 ≤ t2 < 20, even t2: cosmetic spark (RNG). Move along `e+0x5c` by speed (no decay); bright up;
  **if speed > 0 && t2 % 5 == 0: hitbox 0xd2** (§5.13) at pos, angle `e+0x5c`. When t2 > 50 and
  `!FUN_0061ac10(pos, 100)` → scale (1,1), bright 0, speed 0, phase **1**, t2 = 0; parts: 0xd2 motion 4,
  0xdc motion 0, both phase 1, bright 0.
- **1**: scaleX −0.1, scaleY +0.2; removed when t2 > 9.
Kind stays 200 → counts for the 0xb7 "< 2 dwarfs" check, and onKilled 200 applies if shot down.

### 5.6 Cubes (spawn 300 / 0x12d / 0x15e / 0x15f)
Phase **0 grow**: `bright = 1`, scaleX += 0.05 (DAT_00813ef4), scaleY −= 0.1; when t2 ≥ 13 (ver≤0x5a0:
20): scale (1,1), phase 10 (300, 0x15e) or **0x14** (0x12d, 0x15f), t2 0, speed 0.1, active 1,
`e+0x58 = e+0x5c`. (ver≤0x5a0: if outside arena−10 at that moment → phase 0x5a, angle 0.)
- phase **10**: if speed < 5: speed ×1.05 (DAT_00817de4); phase **0x14**: if speed < 5: speed ×1.025
  (`_DAT_00817de0`). Move along `e+0x5c`.
- When `!FUN_0061ac10(pos, −10)` (DAT_00817c10; i.e. |pos| ≥ 530): hitbox **300** (300/0x12d) or
  **0x96** (0x15e/0x15f) at pos, angle `e+0x5c`; Sham effect 300 and common effect 0x514 (colour
  `DAT_008859f8+team*0x10`); removed.
- 0x15e/0x15f in phase 0x14 also explode immediately on contact with the opponent's hurtbox
  (ver>0x5a0, FUN_005f1c90 with angle e+0x58).
- phase 0x5a: shrink → removed.
300/0x12d are **solid** (push the opponent; ignored by projectiles; cannot be destroyed). 0x15e/0x15f
are **invulnerable** non-solid: projectiles collide (beams are consumed, objects.md §4.5) but HP never
drops. Cube speed reaches 5 after ⌈ln 50/ln 1.05⌉ = 81 frames (phase 10) — float-exact count UNVERIFIED.

### 5.7 Resistant Sphere (spawn 400)
Each frame (FUN_0053e810 `iVar3 == 400`):
- phase **0**: bright (+0x68) += 0.1; scaleX (+0x60) += 0.1; `radius (+0x38) = scaleX · 185`
  (`_DAT_00817df0`; ver<0x58a: 168, cap 1.0); when scaleX ≥ 1.1 (DAT_00853b38): bright 1, scaleX 1.1,
  phase **100**, t2 0, hitbox **500** (§5.13) at e.pos, angle 0, owner = e+0x08, team.
  (≈11 frames; float-exact count UNVERIFIED.)
- phase **100**: when `hp (+0x24) < t2` → phase **900**, t2 0, active 0. (hp is never reduced because
  the sphere is solid, so **duration = 50 + ammoA·100 frames**.)
- phase **900**: bright −= 0.02 (DAT_00813ed8); removed when ≤ 0.
- Every frame: pos = `FUN_00780450(team)` pos (= Sham herself); `e+0x58 = rand_f(−π, π)` (**1 RNG draw
  per frame**).
Effect while active: solid circle pushes the opponent out (objects.md §4.8).

### 5.8 Sham effects (FUN_00504b80 / update FUN_00506640 / draw FUN_00504f50)
Effect record: `+0 type 7, +4 id, +8 colour, +0xc ownerId, +0x10 team, +0x14 1, +0x18 pos, +0x24 1,
+0x28 age (manager pre-increments → first update sees age = age0+1), +0x2c size, +0x30 alpha, +0x34 angle,
+0x38, +0x3c`.

| id | init (+0x2c, +0x30, +0x34, +0x38, +0x3c) | update | draw |
|---|---|---|---|
| **500** Beam Ribbon | 0, 0.5, angle arg, rand_f(−0.25, 0.25), rand_f(10, 15) (DAT_00813f4c, DAT_00853b88) | while age < 85 (ver≤0x5a0: 100): alpha += 0.005 (DAT_00813eac), size += 0.01, speed(+0x3c) ×0.95, spin(+0x38) ×0.95, angle += spin, pos += (cos,sin)(angle)·speed; **at age == 84**: `+0x38 = atan2(opp − pos)`; for k = −3..2: beam **300** (k even) or **0x15e** (k odd) at pos, angle `+0x38 + k·π/3` (DAT_00813a50 = 3); common effect 0x515; SE `FUN_007bcaf0(0xf,3)`. Then size −= 0.05/frame; removed at ≤ 0 | team-coloured bullet atlas glow (`DAT_0087fde4`, 2 quads: half-size 40·size (DAT_00814170) alpha 128·a blend 1; 30·size alpha 255·a blend 2) |
| **600** Arachne trail | 0, 2.0, angle | age < 100: alpha +0.1, size −0.1; when size ≤ 1 → size = alpha = 1, age := 100, `+0x14 = 0`, **hitbox 400** (§5.13) at pos, angle `+0x38`. age > 119: alpha, size −0.05; removed at size ≤ 0 | explosion atlas `DAT_0087fccc[age/8]` |
| **0x262** Arachne exit blast | 1.0, 0, angle, angle, 0.175 (DAT_00817888) | age < 50: size += `+0x3c`, `+0x3c *= 0.8` (DAT_008145e8); else alpha −0.01, removed ≤ 0. **age < 21 && age%5 == 0: hitbox 0x19a** at pos (ages 5, 10, 15, 20 → 4 hits, given the pre-increment; UNVERIFIED in-game). age < 11: 2 particles 1000 (RNG ×4); 11 ≤ age < 50: common effect 200/0xd2 alternating (RNG) | sprite 0xc7 frames 0/1 |
| 300 / 200 | 0, 2.0, angle | cosmetic burst: alpha +0.1 / size −0.1 to 1, then age 100; age > 119 fade | explosion atlas |
| 400 Delta triangle | 0, 0 (+0x30 alpha) | removed when its owner enemy (centre id) no longer exists; alpha +0.05 to 1 | 3 textured quads of sprite 0xc0 through the 3 children, alpha (sin((frame%100)/100·2π)·0.25+0.75)·DAT_008147c8·alpha, blend 2 |
| 100 | 0, 0 | size/alpha +0.05 to 1; angle random each frame (RNG); follows enemy `+0xc` (index via FUN_0055f0c0); removed unless that enemy's phase == 10 | sprite 0xc2 frames 0, 1 additive. Spawned only by unused 0x6e sprites |
| 0x226 | 1, 1, angle | alpha −0.05; removed ≤ 0 | Beam Ribbon trail (spawned by beams 300/0x15e) |

### 5.9 Laser 100 — Resistant Sphere orbit rings (cosmetic)
Spawn `FUN_00625190`: template `FUN_006307d0` then `blockMode(+8) = 0, hitType(+0xc) = 0, damage 0,
knockback 0, +0x48 = 0, stun 0`. Update `FUN_00625660` (age pre-incremented, objects.md §1.4):
- sphere = `FUN_0055f060(+0x18)` (ownerId = sphere enemy id). If age < 1000 and (sphere gone or sphere
  phase == 900) → age = 1000. Removed when age > 1049.
- origin = sphere pos (if it exists). `s = 1`; `s = age/10` if age < 10; `s = (1050 − age)/50` if age > 1000.
- 12 segments k = −6..5: θ = k·π/6 + ((frame `DAT_008858f8` % 10)/10·π)/6; point = rotate((cos θ·s·176,
  sin θ·s·44), +0x30) + origin (DAT_00817ab8, DAT_0081c69c; ver<0x58a: 160, 40); segment shape
  **{0, s, 0}** — halfW 0 and r 0 ⇒ never collides (objects.md §4.1). Pure visual.
Draw `FUN_00625290`: per segment two atlas quads (half-size 24·s alpha 64·s blend 1; 10·s alpha 144·s
colour 0xff80ff blend 2).

### 5.10 Arachne (spawn 500) — Arachne Call
Body (behaviour 500) each frame:
- bright += 0.05 (cap 1). Speed rule (ver>0x58a): at t2 == 10: speed = 0.15 (DAT_00813f20); while
  speed < 7.5 (DAT_00814150): speed ×1.575 (DAT_00817de8); clamp to 7.5. (ver≤0x58a: 0.1 ×1.05 to 5.)
  Move along `e+0x5c`.
- 2 cosmetic sparks per frame (`FUN_007bd5e0(0x7da…)`, 1 RNG each).
- **t2 ≥ 14 (ver≤0x58a: 21) and t2 % 2 (ver≤0x58a: 3) == 0**: offset = (rand_f(−240,240) [drawn
  second], rand_f(−280,280) [drawn first]) (DAT_00817df4/DAT_00815584, DAT_00817df8/DAT_00814f14)
  rotated by `e+0x5c`, plus 360·(cos,sin)(e+0x5c) (DAT_00817894) + pos → Sham effect **600** there
  (angle `e+0x5c`, colour `DAT_008859f8`); SE `FUN_007bcaf0(0x14,0x10)`.
- t2 == 100: legs 0x1fe motion 9, 0x208 motion 0xc (frame 0).
- 2 < t2 < 49, t2%3 == 0: cosmetic spark 0xa82+rand%8 behind the body (−100 along heading); SE 0x20 at t2 == 4.
- **t2 ≥ 100 (ver≤0x58a: 0x96) and outside arena+50**: Sham effect **0x262** at pos (angle e+0x5c);
  `FUN_004e9280(100,0.75)`, `FUN_004d7ac0(50, 0.4, 2)`, `FUN_007bec20(100,pos,e+0x58,0)`; 5 cosmetic
  sparks (RNG); SE 0x1a; removed (children removed by orphan sweep).
Legs/tail (0x1fe/0x208/0x212): bright += 0.05; positioned by the hierarchy (motions 8/0xb/3 → 9/0xc).
Body is solid+invulnerable: pushes the opponent, never takes damage, ignored by projectiles.

### 5.11 Whip ribbons (cosmetic)
`DAT_00881d94(2, p18, enemyId)` at the end of sprite growth (kinds 100, 0x6e(behaviour), 0x78, 0xa0).
Whip type 2 update `FUN_007c7ce0`: removed when the enemy is gone; records 20 node pairs at enemy pos ±
5·(cos,sin)(e+0x58 + π/2) (shift-register trail). Draw `FUN_007c72c0` draws the strip with texture
`sm_whip0/1`. Whip type 3 (beam-attached) exists but is never spawned.

### 5.12 Beams (spawn FUN_004b2ed0; template FUN_004ba490: hitType 1, destroyOnHit 1, stun 1.0)
| id | Spawned by | speed | scale(+0x58) | dmg | shape {hx,hy,r} | knock | update (FUN_004b3a20) |
|---|---|---|---|---|---|---|---|
| 100 | sprite volleys | 18 (DAT_00814144) | 3.0 | 120 | {0,0,10} | 5 | speed ×0.95 while > 6; scale −0.2/frame to 1; when age > 30 and outside arena+20 → id **0x6e** (inactive), scale 3.0, common effect 0x516 |
| 0x6e | (from 100) | — | — | — | — | — | scale −0.5/frame, removed at ≤ 0 |
| 200 | dwarf gun | 12 (DAT_00813f54) | 2.0 | 120; **ver>0x5a0: 80** | {0,0,10} | 5 | speed ×0.975 while > 5; scale −0.2 to 1; removed outside arena+10 |
| 300 / 0x15e | Beam Ribbon effect 500 | 10 | (1) | 120 | {15,5,0} (DAT_00853b88, DAT_00813f40) | 10 | speed ×0.985 (DAT_00814148) while > 5; `c = max(0, speed·0.0075·(150 − age)/150)` (DAT_00813ebc, DAT_0081414c); angle **+= c** (300) / **−= c** (0x15e); draw angle = move angle; Sham effect 0x226 trail on even ages; removed when age > 30 and outside arena+20 |
onHit (FUN_004b3830): 100 → effect 1000 + particles + sparks, SE `FUN_007bcaf0(0x13,4)`; 300/0x15e →
same visuals, SE 0x13. Draw (FUN_004b3200): 100/0x6e = team-coloured atlas glow (24·scale alpha
96·a, blend 1; 10·scale alpha 255·a, blend 2); 200 = sprite 0xc6 frames 4 (alpha 128) and 3 (alpha
255), rotated −angle; 300/0x15e draw nothing (trail effect is the visual).

### 5.13 Hitboxes (spawn FUN_005e92d0; dealsDamage 1, oncePerFrame 0, stun 1.0, **all hitType 3** = hit dashing targets; circles)
| id | Source | dmg | r | knock | onHit (FUN_005e96e0) |
|---|---|---|---|---|---|
| 100 | (unused 0x6e sprite) | 30 | 30 (DAT_008140e8) | 10 | — |
| 0x96 | sprite explosion, cubes 0x15e/0x15f | 250; **ver>0x5a0: 200** | 60 (DAT_00813a58) | 10 | 5 streaks 0x456 (RNG), SE `FUN_007bcaf0(0x13,4)` |
| 200 | Dwarf Tackle | 60 | 60 | 10 | 6 streaks + sparks (RNG), SE `(0x18,4)` (`hit.wav`) |
| 0xd2 | Dwarf Rush | 40 | 60 | 5 | as 200 |
| 300 | cube 300/0x12d explosion | 250 | 60 | 10 | effect 0xaf0, SE `(0x1d,4)` |
| 400 | Arachne trail (effect 600) | 130 | 80 (DAT_00814164) | 5 | **angle := atan2(victim − hb)**; 3 cosmetic bursts (RNG), SE `(0x1d,4)` |
| 0x19a | Arachne exit (effect 0x262) | 260 | 280 (DAT_00814f14) | 5 | **angle := atan2(victim − hb)**; 7 bursts (RNG), SE `(0x1d,5)` |
| 500 | Resistant Sphere full size | 100 | 187 (DAT_0081fa80; ver<0x58a 170) | 20 (DAT_008140fc) | **angle := atan2(victim − hb)**; effect 0xaf0, SE `(0x19,3)` (`hit_f.wav`), shake `FUN_004d7ac0(10,0.75,1)` |
No Sham hitbox sets the player's hit-confirm `+0x158`. (Achievement hook in FUN_0060f5a0 for Mira KO'd
by Sham hitbox 500 — not gameplay.)

### 5.14 Missile 100 — dwarf gun shell (spawn FUN_00676e50, template FUN_00685bb0: hitType 2)
growScale 1.0, speed 12, hp 60, shapeBody r 8, shapeProj r 8 (DAT_008140d4), dmg 150 (**ver>0x5a0:
100**), knock 10. Update FUN_00677870: speed ×0.975 while > 5; move along heading; when age > 20 and
outside arena+10 → onHit + removed. onHit/onKilled (FUN_00677730/FUN_006777d0): common effects
0x262 + 0x514, SE 0x14 (`bom06.wav`); return false (removed). Missiles hit dashing players. Draw
FUN_00676f70: sprite 0xc6 frame 0 (blend 1, rgb 1), frame 1 (blend 2, alpha sin(age%50/50·2π)·0.15+0.85),
frame 2 (blend 2, alpha sin·0.25+0.75), rotZ(heading), scale growScale.
Each dwarf "shot" is therefore **two** objects on the same path: a dash-avoidable beam (80) and a
non-avoidable, shootable missile (100).

### 5.15 RNG consumption (gameplay-relevant)
Sprite orbit start angle (0xb1), Poly Sprite offsets/angles (4 draws each), Dwarf Rush offsets (2 each),
Beam Ribbon effect (angle in state + 2 in effect init), dwarf gun spread, sphere angle (every frame),
Arachne trail offsets (2 per spawn). Cosmetic draws (sparks, bursts, particles) share the engine RNG
and must be replicated in order (suguri.md §5.7).

---

## 6. Sound ids used (SE table at all.c:464010, id n = `DAT_009b99a0 + 4n`)
| id | file | where |
|---|---|---|
| 6 | up01.wav | hyper start |
| 0xb | eco03.wav | Resistant Sphere |
| 0xf | laser02.wav | sprite volley; Beam Ribbon burst |
| 0x10 | laser03.wav | Beam Ribbon cast; dwarf gun |
| 0x13 | don16.wav | beam 100/300 hits, hitbox 0x96 |
| 0x14 | bom06.wav | dwarf missile explode; Arachne trail |
| 0x15 | crash17.wav | Delta Sprite dash-lock applied (FUN_006b1a40 k=3) |
| 0x18 | hit.wav | dwarf hitboxes |
| 0x19 | hit_f.wav | sphere hitbox 500 |
| 0x1a | tm2_bom002.wav | dwarf destroyed; Arachne exit |
| 0x1d | gun12.wav | sprite explosion; cube/Arachne hitboxes |
| 0x20 | swing_h.wav | dwarf tackle start, rush start, Arachne t2=4 |
| 0x2a | A5_01029.wav | Dwarf Tackle |
| 0x2f | power35.wav | summons (sprites, dwarfs, cubes, Delta) |
| 0x33 | synth-sweep1.wav | Arachne Call |

---

## 7. Other per-character table entries (index 7)
| Table (real base) | Sham entry | Role |
|---|---|---|
| 0x881310 | FUN_0072e300 | input dispatch, standard (§2) |
| 0x881360 | FUN_0072e890 | input dispatch, alternate (§2) |
| 0x8813b0 | FUN_00727e40 | sprite loader (§4.1) |
| 0x881400 + 0x70 + 4*level | FUN_0072f5a0 (level 0), FUN_0072ee20 (levels 1, 2) | CPU AI (virtual buttons). Reads `DAT_0087cd3c`/`DAT_0087cd7c` (cost entries) against ammo (all.c:406418-406428). Not reversed further. |
| enemy / beam / missile / hitbox / laser / effect / whip | §5 header | |
| 0x88154c (status tick table, k=7) | FUN_00751750 | shared status table (suguri.md §7), not Sham-specific |

---

## 8. Stats record (index 7, base 0x883384), registry.c:1558-1590
| Off | Value | Meaning |
|---|---|---|
| +0x00 | 5.0 (DAT_00813f40) | walk speed per axis |
| +0x04 | 12.0 (DAT_00813f54) | dash initial speed |
| +0x08 | 10.0 (DAT_00813f4c) | dash hold speed (FUN_006af5e0) |
| +0x0c..+0x20 | 5, 8, 6, 3, 7, 3 | colour-variant ints (core.md §7) |
| +0x3c/+0x40/+0x44 | 0xae, 0xaf, 0xb0 | UI sprite ids |
| +0x48 / +0x60 | strings 0x1e / 0x1f | folder names (charaShamA/B) |
| +0xa8 / +0x1a8 | `mdl\win_font_SM.lfd/.dat` | win font |
| +0x2a8 | `sm_res_bgp` | |
| +0x2c0/+0x2c4/+0x2c8 | 50, 0, 0 | UI offsets |
| +0x2cc..+0x2d4 | 100, 230, 90 | colour RGB |
| +0x2d8 | 0 | |
FUN_004a7270 changes only the 0xb7/0xba hyper gains (§1.1) for Sham.

---

## 9. UNVERIFIED / open items
- Float-exact frame counts where a float accumulator crosses a threshold: sphere growth (scaleX ≥ 1.1,
  ~11 frames), effect 600 (size ≤ 1.0 after ~10 updates), cube speed cap (~81 frames), dwarf tackle
  last hitbox (speed > 3 at t2 = 132 vs 136). Settle by simulating in IEEE single precision (x87
  80-bit temporaries may matter — check `xref dis` of the compare sites).
- Effect manager pre-increment ⇒ effect 0x262 fires 4 (ages 5,10,15,20) not 5 hitboxes, and effect 500
  fires at its 84th update — confirm with objects.md §1.4 / in-game.
- ~~Hierarchy motion keyframes~~ — done: src/sim/motion.c (FUN_00526670 all 32 slots) + exact
  FUN_00528250 in objects.c attach(). ~~Solid push-out geometry~~ — done: FUN_0060d690 ported.
- `FUN_005f1940` triangle test edge cases (degenerate triangle returns false; `FUN_005f1390` side
  function sign convention).
- CPU AI (FUN_0072f5a0 / FUN_0072ee20) not reversed.
- Exact visuals of FUN_004e9280 / FUN_007bec20 / FUN_004d7ac0 (screen effects; engine fx.c).
  Effect atlases resolved (§10).

---

## 10. Port verification notes (src/sim/chars/sham.c, tests/test_sham.c)

Resolved from the decompile during the visual pass:
- **Bullet atlas `DAT_0087fde4` = `beamsphere`** (loaded at all.c:132423). Cells by colour index c
  (`DAT_008859f4 + team*0x10`): glow src x = (c%10&1)*100+0x32+(c/10)*0xfa, y = (c%10/2)*100, 100x100;
  core src x = (c/10)*0xfa, y = (c%10)*0x32, 50x50. Used by beam 100 (24/10 × scale, alpha 96/255),
  laser 100 rings (24/10 × s, alpha 64·s / 144·s, core tint 0xff80ff), effect 500 (90/30 × size,
  alpha 128/255, `DAT_00814170` = 90) and effect 0x226 (glow 80×12, core 30×6, glow ARGB
  `a<<24|0xff0000|(a&0x7f)<<9|0xff`).
- **Explosion atlas `DAT_0087fccc[c/8]` = explosion0/explosion1, cell chosen by the effect's colour**
  (+0x08), not its age: page c/8, column (c%8)/4, row (c%8)&3 (128x128 cells); quad 1 half 180·size
  (`DAT_00815258`) alpha 128; quad 2 (right half-cell) half 200·size, ×0.85 on even age, alpha 255;
  both blend 2, rotation passed un-negated (world angle = −rotation).
- **Arachne exit 0x262**: sm_exp frame 0 half 720·size (`DAT_008178c4`) alpha 128; frame 1 half
  960·size (`DAT_008178c8`, ×0.85 on even age) alpha 225 (even, `DAT_00817890`) / 192 (odd,
  `DAT_0081415c`), tint P1 `a<<24|a<<16|0xff00|(0xff−a/2)`, P2 `a<<24|a<<16|(0xff−a/2)<<8|0xff`.
- **Delta triangle (effect 400)**: vtable +0x70 maps sm_wisp_fld (a horizontal gradient) onto the
  degenerate quad (c[j], c[j+1], c[j+2], c[j]) for j = 0..2 → u = fraction from corner j to the
  opposite edge; alpha (sin(frame%100/100·2π)·0.25+0.75)·164·alpha, blend 2. Ported as 16 bands of
  constant u per pass (draw.h has no 4-corner primitive).
- **Sprite ribbons (whip type 2)**: textured strip with whip slot 2 (`sm_whip0`), colour 0x80ffffff,
  u = 0 (left edge) / 256 (right edge), v = k·12.8 (`_DAT_00828324`); ported as one rotated quad per
  node pair; degenerate (zero-length) segments draw nothing.
- **Arachne legs**: 0x1fe applies RotX(180°) when the root's |angle| ≥ π/2, **0x208 the opposite**
  (RotX when not flipped) — FUN_00543860 case 0x208.
- Sham's hypers spawn no common effect 0xfa (unlike Suguri); flourish = SE 6, FUN_004e9280,
  FUN_007bec20, FUN_004d7ac0 only.

Behaviour confirmed by tests/test_sham.c (all citations in the test):
- Released sprites (and Poly Sprites) chase a point `opp + (cos,sin)(orbit)·speed·30`; for
  ver > 0x5a0 speed stops decaying at 5, so against a standing opponent they circle at ~150 px and
  explode only on the t2 > 249 timeout (hitbox 0x96, r 60) — they hit when the opponent moves into
  them. (Older versions kept decaying the speed after t2 > 200, shrinking the orbit.)
- Held A spawns at t = 9, 15, 21, … (every 6 frames) until ammo A runs out (0.2 each; no regen
  in 0xb1).
