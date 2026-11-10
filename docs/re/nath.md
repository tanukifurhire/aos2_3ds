# Nath — complete moveset spec (three weapon phases + Bird Form)

Source: `aos2.exe` (Ghidra corpus `tools/re/dec/all.c`, constants read with `xref.exe rd`, sprite ids
from `xref.exe dis`). Every number cites its address. `t` = player state timer `p+0x154`.
Structure follows `suguri.md`; shared helpers (cost table `FUN_007807f0`, hyper start `FUN_006b1b40`,
hyper spend `FUN_00780910`, hit-confirm `+0x158`, ammo `+0xe8/+0xec`, dash cancel `FUN_006b17d0`,
training-only `FUN_00612830/00612900`) are used with the meanings given there and are not re-derived.
Ruleset version `DAT_0089426c` = 0x5a2 in live play (objects.md §0.1) → **implement the newest branches**
(all `>0x5a0`, `>=0x58a`, `>0x58a`, `>=0x58b` tests TRUE); old values are listed for replays only.

Engine character index (`p+0x10`) = **8** (handbook id 5 + 3). Object "type" = 8 in every family.

---

## 0. Notes for the lead (affect other docs / all characters)

1. **objects.md §4.5 mislabel:** "Saki type 8 `FUN_00673790`" is **Nath's** missile `onAbsorb`
   (type 8 = Nath; Saki is type 11). It is the Beam Reflector (missile ids 200/0xc9) → reflected beam 300 (§5.2/§5.3).
   objects.md §4.4's "Nath beam 200 never hits players" is confirmed: it is the Reflecting Beam, which
   damages only through a per-frame hitbox (§5.1).
2. **`p+0x164` is Nath's weapon phase** (0 = no weapon, 1/2/3 = weapon forms). Written by Player Init
   (`FUN_0069cc40` all.c:348069 `local_1c = 0` → +0x164; initial state `local_30 = 0xd2` → +0x150 for
   char 8, all.c:348070) and by states 0xca/0xd2/0xd3/0xd4/0xf1. The special case in `FUN_007804b0`
   (opponent `+0x10 == 5 && +0x164 == 1`) concerns **Tsih** (index 5), not Nath — `+0x164` is a
   per-character scratch field.
3. **Player init state is per character:** `FUN_0069cc40` switch on char index sets the starting state
   (Nath 0xd2, Nanako(0xd) 0x197, 0x11 0x21d, Sumika-2nd(0x12) 0x241 + HP override). core.md should own this.
4. **New player field `+0x174`** = frame number (`DAT_008858f8`) of the last Nath hitbox 0xfa hit on this
   player (per-victim 40-frame re-hit lockout, `FUN_005e75b0` case 0xfa, ver ≥ 0x58b).
5. **Owner ids:** missiles and lasers spawned *by Nath's weapon enemy* pass the **enemy's `+0` id** as
   `ownerId` (e.g. `(*DAT_008819d8)(100, e+0x40, e+0x58, *e, 0, e+0x14, e+0x18)`, FUN_0052eaf0), and the
   hitbox spawned by beam 200 passes the **beam serial `+0x1c`** (FUN_004b0ea0). Enemy ids come from
   `DAT_00885980` and player ids from elsewhere — any code that resolves `ownerId` with `FUN_007805f0`
   will not find a player for these. Lasers 200/0xfa/0x104 rely on this: they follow the enemy via
   `FUN_0055f060(ownerId)` (FUN_006249a0).
6. **Hitbox onHit can cancel damage:** Nath's hitbox callback (`FUN_005e75b0`) writes `hb+0 (dealsDamage) = 0`
   for juggled targets (victim in state 1 and `t<5` or speed > 2). Since onHit runs before damage
   (objects.md §4.4) this is how Nath's multi-frame hitboxes avoid juggles.
7. **Enemy-family attack timing rule** (important for the port): many Nath attacks are executed by the
   *weapon enemy* (Enemy family type 8), commanded by the player state writing `enemy+0x84 = cmd;
   enemy+0x80 = 0`. Enemies update after players in the same tick (objects.md §1.3), and `+0x80` is
   incremented by the manager *after* the update, so the enemy's command timer is **τ = t − t_cmd**
   (τ = 0 on the frame the player state issued the command).
   **Parts that read the player's timer directly** (the hand, kind 600, in 0xe9/0xea/0xeb/0xec/0xed, and the arm/hand
   motion switches) see it **after** `FUN_006f1720` incremented it: hand "t == 15" fires in the tick whose T2 saw t = 14.
   All hand-timed rows in §3 are in hand-seen t (= T2 t + 1).
8. Missile/laser/beam/hitbox/enemy family functions for type 8 live **outside** Nath's code block
   (0x4b0750.., 0x5e6f50.., 0x623170.., 0x671750.., 0x52d0f0..); they are Nath-only (switch on Nath ids)
   and fully documented here.

---

### Player fields used by Nath's code (in addition to suguri.md's table)

| Off | Name | Notes / source |
|---|---|---|
| +0x164 | weapon phase | 0 none (start / after KO), 1, 2, 3 (see §1.5) |
| +0x174 | last-hit frame for hitbox 0xfa | §0 note 4 |
| +0x5c..+0x68 | hurtbox shape + angle | zeroed every frame by 0xd2/0xd3/0xd4, 0xef (t<81), 0xf0, 0xf1 (t<100) |
| +0xa0 | arena clamp flag | 0 in 0xef (t<81), 0xf0, 0xf1 (lets Nath leave the arena) |
| +0xa2 | hittable | 0 in 0xd2/0xd3/0xd4, 0xf0, 0xf1 (t<100) |

---

## 1. Shared mechanics

### 1.1 Frame model / dash cancel
As suguri.md §1.1. "Dash-cancel from N" = `t >= N && Dash==3 → FUN_006b17d0(p,1)`; `FUN_00612900` is
called at t == N (training display only).

### 1.2 Cost table (`FUN_007807f0`, entries `0x87c4f0 + id*0xc`, written by FUN_0049ae40, registry.c:1646-1687)

| id | Move | costA | costB | hyperGain | used by |
|---|---|---|---|---|---|
| 0xd5 | Spread Beam (per shot) | 0.075 (DAT_00813efc) | 0 | 0.0075 (DAT_00813ebc) | 0xd5 |
| 0xd6 | — | 0.05 (DAT_00813ef4) | 0 | 0.005 (DAT_00813eac) | never called |
| 0xd7 | — | 0.03 (DAT_00813ee0) | 0 | 0.003 (DAT_00813ea0) | never called |
| 0xd8 | Homing Bombing (per bomb) | 0 | 0.085 (DAT_00813f08) | 0.0085 (DAT_00813ec4) | enemy cmd 200 |
| 0xda | B phase 2 (per missile) | 0 | **0.125** (ver 0x5a2 patch FUN_004a7270: DAT_0087cf2c = DAT_00813f1c; registry value 0.1) | 0.01 (DAT_00813ec8) | enemy cmd 0xfa |
| 0xdb | B phase 3 (per missile) | 0 | **0.083** (patch: DAT_0087cf38 = DAT_00813f04; registry value 0.075) | 0.0075 | enemy cmd 0xfa |
| 0xdd | Reflecting Beam | 0.3 (DAT_00813f28) | 0 | 0.05 (DAT_00813ef4) | 0xdd |
| 0xdf | Beam Reflector ph1 (pair) | 0 | 0.4 (DAT_00813f30) | 0.05 | enemy cmd 400 |
| 0xe0 | Beam Reflector ph2 (per pair) | 0 | 0.25 (DAT_00813f24) | 0.035 (DAT_00813ee4) | enemy cmd 400 |
| 0xe1 | Beam Reflector ph3 (per pair) | 0 | 0.2 (DAT_00853b14) | 0.02 (DAT_00813ed8) | enemy cmd 400 |
| 0xe3 | Laser bits (D+A ph2/3, per bit) | 0 | 0 | 0.025 (DAT_00813edc) | 0xe3 (**result ignored**) |
| 0xe7 | Metal Fist (per fist) | 0 | 0 | 0.02 | 0xe7 |
| 0xe9 | S+A ph2 | 0 | 0 | 0.04 (DAT_00813eec) | 0xe9 |
| 0xea | S+A ph3 laser (every 5 frames) | 0.1 (DAT_00853b10) | 0 | 0.02 | laser 200 |

All other Nath states have no entry (BSS 0 → free, no gain). Hypers never call it.

### 1.3 Hyper flourish (t=0 of most hypers)
`FUN_006b1b40(p,10)`; SE 6 (`up01.wav`); `FUN_004e9280(100, 0.75 DAT_00813f3c)`; `FUN_007bec20(100, pos, aim, X)`
with X = π/4 (DAT_0081fa90) or 0 (per state, cosmetic); `FUN_004d7ac0(20, 0.5, 2)`; generic effect 0xfa.
0xd3/0xd4 do the same minus effect 0xfa and plus enemy 9999 + SE 0x33.

### 1.4 The weapon enemy (Enemy family type 8)
Nath's weapon is a hierarchy of Enemy objects created by **enemy id 100** (`(*DAT_00881578)(100, p+4, team, p+0x18)`,
spawned by state 0xd2 at t=50). It is purely visual/positional (all parts `active=0`, shape 0, hp 9999, not
solid), but its parts are the **spawn points** of many attacks. Helpers:
- `FUN_0070f3e0(team)` → index of the enemy with `team`, `type 8`, `+0x10 == 100` (the **root**), or −1.
- `FUN_0070f460(team)` → index of the enemy with `+0x10 == 1000` (the part on the hand, phase 2+).
- `FUN_0052ea60(team)` → count of enemies with `team` and `+0x10 == 5000` (laser bits).
- Enemy fields used: `+0x10` spawn id, `+0x7c` **kind**, `+0x80` command timer τ, `+0x84` **command**,
  `+0x88` motion id, `+0x8c` motion frame, `+0x60/64/68` scale x/y/z, `+0x58` angle, `+0x5c` angle2,
  `+0x70` speed, `+0x74` misc float, `+0xbc/+0xc0` pivot, `+0xc8/+0xcc` offset (attachment: objects.md §3.5).
- Root command = what the root does this frame (§5.5). The player state writes the command at a fixed `t`
  (usually 5 or 1); "τ" below is the root's command timer (§0 note 7).

### 1.5 Phases (how the transformation works)
| Phase (`+0x164`) | Entered by | Visible weapon parts | Input table |
|---|---|---|---|
| 0 | Player Init (state 0xd2), KO (0xca t=0) | none until 0xd2 t=50 | normal moves of "else" rows (see §2 bugs) |
| 1 | 0xd2 t>70 (round start), 0xf1 t=100 (after Bird Form) | body, legs, shoulder/arm/hand, cannon | Phase 1 |
| 2 | **Extension I** 0xd3 (S+H in phase 1, 1 hyper level), at t>70 | + part 1000 on the hand; arm extends | Phase 2 |
| 3 | **Extension II** 0xd4 (S+H in phase 2, 1 level), at t>70 | + parts 700/0x2c6/0x2d0 and 800 | Phase 3 |
| (Bird) | 0xef (S+B+H, 3 levels) → 0xf0 → 0xf1 | + 900/0x38e; **returns to phase 1** | — |

Phases persist until KO (0xca sets 0) or Bird Form (resets to 1). The arm geometry depends on phase every
frame: part 500 pivot x `+0xbc` → 20 (DAT_008140fc) in phase 1 (−0.5/frame) else → 50 (DAT_008140ec, +0.5/frame);
part 600 offset x `+0xc8` → −20 (DAT_00814108) in phase 1 (+0.5/frame) else → −50 (DAT_008149e4, −0.5/frame)
(FUN_0052eaf0 kinds 500/600).

---

## 2. Input dispatch `FUN_00715cb0` (standard scheme) — exact decode

**A. Hypers** — only if `FUN_00780950(p)` **and** (ver ≥ 0x58a ⇒ state ∉ {0xd3, 0xd4}) (Extensions
can't be interrupted). First match wins; if the cost check fails the next row is tried.

| # | Condition | Cost | ph 1 | ph 2 | ph 3 (or 0) | Official |
|---|---|---|---|---|---|---|
| 1 | S held && B held && H pressed | 3.0 (DAT_00813a50) | 0xef | 0xef | 0xef | **Bird Form** (Accel) |
| 2 | S held && H pressed | 1.0 (DAT_00853b34) | 0xd3 | 0xd4 | 0xec | **Extension** (manual lists it as "S+B" — code is S+H) / ph3 grab |
| 3 | B held && H pressed | 1.0 | 0xd9 | 0xee | 0xdc | **Carpet Bombing** / ph2 mines / ph3 homing barrage |
| 4 | Dash held (`+0x34>1`) && H pressed | 1.0 | 0xe4 | 0xe5 | 0xe6 | **Laser Bit Deployment** (3 variants) |
| 5 | H pressed | 1.0 | 0xe8 | 0xeb | 0xeb | **Metal Pounding** / ph2-3 hyper laser |

**B. Normal moves** — only in state 0 or 2:

| # | Condition | ph 1 | ph 2 | ph 3 | Official |
|---|---|---|---|---|---|
| 1 | Dash pressed | dash `FUN_006b17d0(p,0)` | | | |
| 2 | Shield pressed && `FUN_00780910(p,0.5)` && `FUN_007809e0(p)` | shield `FUN_006b1930(p,0)` | | | |
| 3 | S held && B pressed | 0xdf | 0xdf | 0xdf | **Beam Reflector** (more reflectors per phase) |
| 4 | S held && A pressed | 0xe7 | 0xe9 | 0xea | **Metal Fist** / ph2 punch / ph3 hand laser |
| 5 | B pressed | 0xd8 | 0xda | 0xda | **Homing Bombing** / ph2-3 homing missiles |
| 6 | Dash held (`>1`) && A pressed | 0xdd | 0xe3 | 0xe3 | **Reflecting Beam** / ph2-3 laser bits |
| 7 | A pressed | 0xd5 | 0xd5 | 0xd5 | **Spread Beam** |

Bug-compatible quirk: rows B4 and B6 have no `else` for phase 0 — the dispatch then writes `t = 0`,
`flag158 = 0` and calls `T2[state]` again **without changing state** (all.c, FUN_00715cb0). Only reachable in
phase 0 (0xd2 start state is not 0/2, so practically unreachable). Hyper row 2 in phase 0 → 0xec.
Finally returns `T2[state](p) != 0`.

**Alternate scheme `FUN_00716350`** (`p+0xc == 1`): normal moves identical; hypers are "H held (`+0x44>1`)
and X pressed": Shield→0xef (3.0), S→row 2, B→row 3, **Dash pressed**→row 4, A→row 5. The
"can't interrupt 0xd3/0xd4" guard uses `ver < 0x58b` instead of `< 0x58a`.

---

## 3. States

Common patterns: "aim" = `+0x84 = FUN_007804b0(p)`. "face-x" = `+0x84 = +0x88 = (opp.x <= x) ? π : 0`.
"cmd C at t=N" = at t==N: `root = FUN_0070f3e0(team); root+0x84 = C; root+0x80 = 0` (no null check
except 0xe9). "friction ×0.5" = `+0x9c *= 0.5` every frame (decel 0.25/frame).
Spawn tables: beam `DAT_00881898 = FUN_004b0750`, missile `DAT_008819d8 = FUN_00671750`,
hitbox `DAT_00881bb8 = FUN_005e6f50`, laser `DAT_00881c58 = FUN_00623170`, enemy `DAT_00881578 = FUN_0052d0f0`,
Nath effect `DAT_008816b8 = FUN_00500ae0`, generic effect `DAT_00881698`.

### 0xc8 — neutral movement (T2 FUN_0070fdd0, from common state 0)
y+ held → y += 4.0; y− → y −= 4.0; x− → x −= 4.0; x+ → x += 4.0 (speed = stats+0 = **4.0**, DAT_00853b68 →
0x883660); `+0x84 = facing==1 ? 0 : π`; **friction ×0.5** (unlike Suguri). Returns 1.

### 0xc9, 0xcb, 0xcc — stubs (FUN_0070fee0 / 0070fef0 / 0070ff00 return 1); used only for their T1 (§4.3).

### 0xca — KO hook (T2 FUN_0070ff10, called every frame by common state 6 `case 8`)
t==0 only: every own enemy (`+8 == p+4`) of kind 200/300/0x15e/400/500/600 → `+0x84 = 999` (fade: scaleZ −0.02/frame
to 0); kinds 1000/700/800/900 → `+0x84 = 900` (shrink), `+0x68 = 0`; then **phase = 0**.
(Kind 0x15e never matches since parts' kinds are 300 — the 0x15e test is dead code.)

### 0xd2 — weapon boot / round start (T2 FUN_00710290) — **initial state** (Player Init)
| t | Action |
|---|---|
| 40 | enemy **9999** (transform flash) at pos; SE 0x33 (`synth-sweep1.wav`) |
| 50 | enemy **100** (the weapon, §5.5) |
| >70 | phase = 1, state 0 |
| every frame | hurtbox `+0x5c..+0x68 = 0`, `+0xa2 = 0` (invulnerable, untargetable) |

### 0xd3 — Extension I (phase 1 → 2) (T2 FUN_007103a0) — S+H, phase 1
| t | Action |
|---|---|
| 0 | face-x; speed 0; flourish (§1.3, `FUN_007bec20` angle arg 0, no effect 0xfa); own kind-1000 parts with `+0x84==1000` → 900; enemy 9999 at pos; SE 0x33 |
| 10 | own enemies with **`+0x10 == 1000`** (the hand part): `+0x84 = 0` (grow), `+0x68 = 0`, `+0x60 = 3.0`, `+0x64 = 0` |
| >70 | phase = 2, state 0 |
| every frame | hurtbox zero, `+0xa2 = 0` |
No dash cancel.

### 0xd4 — Extension II (phase 2 → 3) (T2 FUN_007106c0) — S+H, phase 2
As 0xd3 but no kind-1000 hide; at t=10 own kind-700 parts with `+0x84==1000` and all kind-800 parts get
the grow reset (`+0x84=0, +0x68=0, +0x60=3.0, +0x64=0`); t>70 → phase 3, state 0.

### 0xd5 — Spread Beam (T2 FUN_00710a00) — A, all phases
| t | Action |
|---|---|
| 0 | aim |
| 5 | root cmd **100** (follow, ends τ=30) |
| 10,12,…,30 (9<t<31, even) | if `FUN_007807f0(p,0xd5)` (A −0.075): beam **100** at pos, angle `aim + rand_f(−0.5, 0.5)` (DAT_00853bb0, DAT_00853b1c); SE `FUN_007bcaf0(0xe,4)` (`tm2_gun000.wav`) → up to **11** shots |
| every frame | friction ×0.5 |
| ≥25 | dash-cancel; >35 state 0 |
(Beam call args confirmed by disassembly 0x710a93-0x710af7: `(100, p+0x6c, angle, p+4, 0, p+0x14)`.)

### 0xd8 — Homing Bombing (T2 FUN_00710be0) — B, phase 1
t=0 aim; t=5 root cmd **200**; friction ×0.5; dash-cancel ≥25; >35 state 0.
Root cmd 200 (FUN_0052eaf0): follows Nath; τ 12..36 with τ%3==0 and `FUN_007807f0(p,0xd8)` (B −0.085):
missile **100** at root pos (= Nath pos), angle root `+0x58` (= aim); `+0x70 = ±rand_f(1, 5)` (DAT_00853b34,
DAT_00813f40; + when (τ/3) even); SE `FUN_007bcaf0(0x22,4)` (`burst00.wav`). Cmd ends (→0) as soon as the
player is no longer in 0xd8. **Effective bombs: τ = 12,15,…,30 → 7** (player t 17..35; t=36 ends the state);
fewer if dash-cancelled.

### 0xd9 — Carpet Bombing (T2 FUN_00710cd0) — B+H, phase 1
t=0 aim, flourish (§1.3, effect 0xfa); t=5 root cmd **0xd2**: τ 9..45, τ%3==0 → **13** missiles **0x6e**
(no cost), at root pos, angle aim, `+0x70 = ±rand_f(1, 10)` (+ when (τ/3) even), SE `FUN_007bcaf0(0x22,4)`;
cmd ends when state ≠ 0xd9. No friction. Dash-cancel ≥50; >80 state 0.

### 0xda — B, phases 2/3 (T2 FUN_00710e50)
t=0 aim; t=5 root cmd **0xfa**; friction ×0.5. Dash-cancel from 25 (ver>0x5a0: phase 2 **30**, phase 3 **33**);
end >35 (ver>0x5a0: phase 2 **>40**, phase 3 **>41**).
Root cmd 0xfa (ends at τ==50 regardless of state):
- phase 3: τ 8..36 (ver>0x5a0; old 8..44), τ%4==0 → **8** missiles **0x96**, each `FUN_007807f0(p,0xdb)` (B −0.075);
- phase 2: τ 10..35, τ%5==0 → **6** missiles **0x96**, each `FUN_007807f0(p,0xda)` (B −0.1);
- each: at root pos; `+0x68 = +0x6c = wrap(aim ± rand_f(1.4, 2.6))` (DAT_00817dd0/dd4; + when (τ/4)%2==0 resp. (τ/5)%2==0);
  `+0x70 = 7` (DAT_0081789c), `+0x74 = 0.1`; SE 0x22 (`FUN_007bc9c0`).

### 0xdc — B+H, phase 3 (T2 FUN_00710fc0)
t=0 aim, flourish (angle π/4); t=5 root cmd **0x104**: τ 3..36, τ%3==0 → **12** missiles **0xa0** (no cost),
`+0x68 = +0x6c = wrap(aim ± rand_f(1.4, 2.6))` (+ when (τ/3) even), `+0x70 = 12` (DAT_00813f54), `+0x74 = 0.2`
(DAT_00853b14), SE 0x22; cmd ends τ==50. Friction ×0.5; dash-cancel ≥25; >35 state 0.

### 0xdd — Reflecting Beam (T2 FUN_00711170) — D+A, phase 1
t=0 aim; t=5 root cmd 300 (follow, ends τ=50); **t=15**: if `FUN_007807f0(p,0xdd)` (A −0.3): beam **200** at pos,
angle aim − π/4 and beam **200** at aim + π/4 (DAT_0081fa90), SE 0x11 (`tm2_laser000.wav`). Friction ×0.5;
dash-cancel ≥25; >35 state 0.

### 0xdf — Beam Reflector (T2 FUN_00711330) — S+B, all phases
t=0 aim; t=5 root cmd **400**; friction ×0.5; dash-cancel ≥25; >35 state 0.
Root cmd 400 (ends τ==50). Offsets are rotated by Nath's `+0x84` and added to root pos; missiles get angle root `+0x58` (= aim):

| phase | τ | cost (one check per pair) | pair offsets (ver>0x5a0) | old (random) |
|---|---|---|---|---|
| 1 | 10 | 0xdf (B −0.4) | 200 at (40, 30), 0xc9 at (40, −30) (DAT_008140e0, DAT_008140e8, DAT_00817c08) | x∈[20,50], y∈±[10,100] |
| 2 | 10, 14 | 0xe0 (B −0.25) each | τ10: (40, ±30); τ14: (10, ±100) (DAT_00813f4c, DAT_00853b90/bb8) | |
| 3 | 8, 12, 16 | 0xe1 (B −0.2) each | (40, ±30); (10, ±100); (−10, ±150) (DAT_00817c10, DAT_0081414c/DAT_008149ec) | |
Within a pair: id 200 gets +y, id 0xc9 gets −y. SE 0x2f (`power35.wav`) per pair. Old-version branch draws
4 RNG values per pair; the rand calls are made first and then overwritten when
ver>0x5a0, so **the 4 RNG draws per pair still happen** (keep them for RNG sync).

### 0xe3 — Laser bits (normal), phases 2/3 (T2 FUN_00712370) — D+A
t=0 aim; t=5 root cmd 500 (follow while state is 0xe3). For k = 0..3, at **t = 12 + 4k**: if
`FUN_0052ea60(team) > 3` stop (max 4 bits alive); `FUN_007807f0(p,0xe3)` (result ignored, +0.025 gauge);
- phase 3: r = rand_f(80, 320) (DAT_00814164, DAT_008178c0), centre = **opponent**;
- phase 2: r = rand_f(80, 240) (DAT_00815584), centre = **Nath**;
- θ = k/4·2π + aim + rand_f(−0.5, 0.5); target T = centre + (cos θ, sin θ)·r;
- a = wrap(atan2(opp − T) + π); spawn enemy **5000** (laser bit, §5.5.3) with `+0x84 = 100`, `+0x7c = 5000`,
  pos = Nath + (cos a, sin a)·100, `+0x58 = a`, `+0x5c = atan2(T − pos)`, `+0x70 = |T − pos| / 50`,
  `+0x74 = (a <= 0) ? −1 : +1`; SE `FUN_007bcaf0(0x2f,4)`.
Friction ×0.5; dash-cancel ≥30; >40 state 0.

### 0xe4 / 0xe5 / 0xe6 — Laser Bit Deployment (hyper, D+H) — phase 1 / 2 / 3
All: t=0 aim, flourish (angle π/4 for e4/e5, 0 for e6); dash-cancel ≥40; >70 state 0; 8 bits (enemy 5000,
same field setup as 0xe3, SE `FUN_007bcaf0(0x2f,4)` each, no cost, no cap).
| State (T2) | spawn t (k=0..7) | target point T | bit angle a |
|---|---|---|---|
| 0xe4 (FUN_00712930) | 10 + 3k | Nath + rotate((−rand_f(50,100), ∓rand_f(10,200)), aim) — y negative for even k, positive for odd (DAT_008140ec, DAT_00853b90, DAT_00813f4c, DAT_008147cc) | wrap(aim + π) |
| 0xe5 (FUN_00712e80) | 5 + 3k | Nath + dir(θ)·rand_f(120, 320), θ = k/8·2π + aim + rand_f(−0.5,0.5) (DAT_00814174, DAT_008178c0, DAT_008140d4) | wrap(atan2(opp − T) + π + rand_f(−0.25, 0.25)) |
| 0xe6 (FUN_007133f0) | 5 + 4k | **opponent** + dir(θ)·rand_f(120, 360), θ = k/8·2π + aim + rand_f(−0.75, 0.75) (DAT_00817894, DAT_0081fa78, DAT_00813f3c) | wrap(atan2(opp − T) + π) |
0xe5 additionally sets root cmd 500 at t=5 (cosmetic: follow). RNG order per bit: e4: r1, r2; e5: r, θ-jitter, a-jitter; e6: r, θ-jitter.

### 0xe7 — Metal Fist (T2 FUN_00711420) — S+A, phase 1
| t | Action |
|---|---|
| 0 | aim |
| 5 | root cmd **600** (arm/hand hidden: scaleX 0 while state 0xe7) |
| 5 | if `FUN_007807f0(p,0xe7)`: fist enemy **0x13ed** at Nath + dir(s₁)·50 (DAT_008140ec), s₁ = aim + π/2 if \|aim\| ≤ π/2 else aim − π/2; `+0x58 = +0x5c = atan2(opp − fist)` |
| 10 | if `FUN_007807f0(p,0xe7)`: fist **0x13ec** at Nath + dir(s₂)·50, s₂ = the **other** side; same aiming |
| ≥25 | dash-cancel; >35 state 0 |
Fists: §5.5.2 (8 frames of hitbox 200 each).

### 0xe8 — Metal Pounding (T2 FUN_00711a20) — H, phase 1
| t | Action |
|---|---|
| 0 | aim; flourish (angle π/4) |
| 1 | root cmd **0x262** (hides arm/hand) |
| 5,10,…,45 | fist enemy **0x13f6 + ((t−5)/5 % 2)**; d = rand_f(20, 100) (DAT_008140fc, DAT_00853b90); side: if \|aim\| ≥ π/2: t%10==0 → aim−π/2 else aim+π/2; if \|aim\| < π/2: t%10==0 → aim+π/2 else aim−π/2; pos = Nath + dir(side)·d; `+0x58 = +0x5c = aim` → **9 fists** |
| 50 | P = opp + dir(aim)·(−100) (DAT_00853bb8); if \|P − Nath\| ≤ 160 (DAT_00814168): big fist **0x1400** at P, angle aim |
| ≥48 (old 60) | dash-cancel; >68 (old >80) state 0 |

### 0xe9 — S+A, phase 2 (T2 FUN_00712150) — lunging punch
| t | Action |
|---|---|
| 0 | aim; lunge `+0x88 = +0x94 = aim`, `+0x98 = min(dist/16, 12)` (DAT_008140d8, DAT_00813f54) |
| every frame | dash sparks if speed ≥ 10 (DAT_00813f4c), dir = aim (suguri.md §1.3) |
| 5 | SE 0x20 (`swing_h.wav`); `FUN_007807f0(p,0xe9)`; if ok and root exists: root cmd **0x28a** |
| 6..24 | (hand part 600, while player in 0xe9): hitbox **0xfa** every frame at hand pos, angle hand `+0x5c`; Nath effect 0x15e on even t |
| ≥45 | dash-cancel; >70 state 0 |
Hand motion 0x17 / arm motion 0x16 from t=5 (§5.5.1). The hitbox is spawned by the hand regardless of the cost result.

### 0xea — S+A, phase 3 (T2 FUN_00713920) — hand laser
t=0 aim; t=1 root cmd **700**; hand motion 0x19 from t=1; **t=15**: hand spawns laser **200** at hand pos,
angle hand `+0x5c` (owner = hand enemy), SE 0x12 (`gun11_r.wav`). Laser 200 pays `FUN_007807f0(p,0xea)`
(A −0.1) every 5 ages from age 15 and stops when it fails (§5.4). Dash-cancel ≥40; >60 state 0.

### 0xeb — H, phases 2/3 (T2 FUN_00713a00) — hyper hand laser
t=0 aim, speed 0, flourish (angle π/4); t=1 root cmd **0x2c6**; hand t=1: motion 0x19, and (ver>0x589) kills its
previous lasers 200/0xfa/0x104 (`FUN_00630880`: laser `+0x4d = 1`); **t=11**: hand spawns laser **0xfa** (phase 2)
or **0x104** (phase 3), SE 0x12. Dash-cancel ≥60; >80 state 0.

### 0xec — S+H, phase 3 (T2 FUN_00713ba0) — grab
| t | Action |
|---|---|
| 0 | aim; lunge speed `min(dist/16, 12)` dir aim; flourish (π/4) |
| every frame | sparks if speed ≥ 10 |
| 1 | SE 0x20; root cmd **0x2ee**; hand motion 0x19 |
| 11..24 | hand: hitbox **400** every frame (no damage; on contact sets Nath's `+0x158`) |
| 10..30 with `+0x158 == 1` | `FUN_005f8fc0(50, opp+4, opp pos, 0.75)` (camera focus), `FUN_004e9280(100, 0.85)`, `FUN_004d7ac0(100, 0.6, 2)`, `FUN_007bec20(100, pos, 0, 0)`; **state 0xed** (t=0, flag=0, run now) |
| ≥60 | dash-cancel; >80 state 0 |

### 0xed — grab follow-up (T2 FUN_00713f60)
Player state only: dash-cancel ≥60; >80 state 0. The **hand** (kind 600, FUN_0052eaf0) does the work:
| t (player) | Action |
|---|---|
| 1 | hand motion 0x19; SE 0x30 (`synth-sweep2.wav`) |
| 0..59 | if `FUN_00780a50(opp)` (opp `+0xf8==0`, not state 6, hittable, `+0xa9 != 1`, round not decided): opp state=1, t=0, opp pos = hand + dir(hand `+0x5c`)·150 (DAT_0081414c), speed 0, opp `+0x84 = wrap(hand angle + π)` (held in front of the hand) |
| 0..30 | every frame: Nath effect 0x19a at hand + dir·150 + rotate((rand_f(240,640), 0), rand_f(−π,π)), angle rand_f(−π,π), `+0x3c = rand_f(10,30)` (4 RNG draws + 1 inside the effect spawn) |
| 10,20,…,60 | Nath effect 400 at hand + dir·150, `+0x2c = (t−10)·0.1 + 0.75` |
| 60 | hitbox **0x19a** at hand + dir·150, angle hand `+0x5c`; SE 0x1e (`tm2_bom003.wav`) |

### 0xee — B+H, phase 2 (T2 FUN_00713fc0) — mine launchers
t=0 aim, speed 0, flourish (π/4); t=1 root cmd **800**; **t=20**: if the hand part (`+0x10==1000`) exists: six enemies
**0x1450** at part1000.pos + dir(a)·50, `+0x58 = +0x5c = a`, for a = wrap of aim+π/6, aim−π/6, aim−π/2,
aim−π+π/6, aim+π−π/6, aim+π−π/2 (DAT_00817ddc = 0.5236). Dash-cancel ≥30; >60 state 0. (§5.5.4)

### 0xef — Bird Form, take-off (T2 FUN_007148f0) — S+B+H, 3 levels (Accel)
| t | Action |
|---|---|
| 0 | face-x; speed 0; `FUN_006b1b40(p,0x14)` (Accel); SE 7 (`high_a.wav`); `FUN_004e9280(700, 0.85)`; `FUN_007bec20(100,pos,aim,0)`; `FUN_004d7ac0(20,0.5,2)`; effect 0xfa; enemy 9999 + SE 0x33; root cmd **900** |
| 10 | reveal hidden (`+0x84==1000`) kinds 1000/700/800/900: `+0x84=0, +0x68=0, +0x60=3.0, +0x64=0` (900 also `+0x8c=0`) |
| 30 | `FUN_005f8fc0(680, opp+4, opp pos, 1.0)`; SE 0x32 (`nori_ge_j_jet01.wav`) |
| >30 | y += (t−30); x −= (t−30)·0.5 if \|aim\| ≤ π/2 else x += (t−30)·0.5 (flies up and back) |
| ≤80 | hurtbox zero; arena clamp `+0xa0 = 0` |
| 81 | → **0xf0** (t=0, flag not reset); `FUN_006b1b40(p,0x14)`; place for pass 1 (below); root cmd **0x38e** |
**Quirk (literal):** the t ≥ 81 branch does not clear `+0xa0`, so on the transition frame the end-of-update arena
clamp (FUN_006f1720) pulls the ±1500 placement back onto the 540-radius circle; pass 1 therefore starts on the arena
edge, passes 2/3 (placed inside 0xf0, which clears `+0xa0` the same frame) start at ±1500. Verified by tests/test_nath.c.
Pass placement (also used between passes): if \|aim\| ≥ π/2: aim = π, x = **1500** (DAT_008149d4); else aim = 0,
x = **−1500** (DAT_00814a24); y = opp.y. If y+ held: y = 400 (DAT_008145f4; old opp.y+300), aim = atan2(opp − pos);
if y− held: y = −400 (DAT_00815bc8; old opp.y−300), aim likewise (`+0x88 = +0x84`).

### 0xf0 — Bird Form, strafing passes (T2 FUN_00715190)
Let u = t % 1000 (t = 0..180, 1000..1180, 2000..2180).
| u / t | Action |
|---|---|
| every frame | pos += dir(aim)·u·0.25 (accelerating) |
| u < 151 | 2 cosmetic particles 0x7da (`FUN_007bd5e0`) — 1 RNG draw each |
| 60 ≤ u ≤ 120, u%3==0 | missile **0x6e** at pos, angle aim; `+0x70 = ±rand_f(1,10)` (+ when (t/3) even); SE `FUN_007bcaf0(0x22,6)` → **21 bombs per pass** |
| t%3 == 0 | hitbox **600** at pos, angle aim + rand_f(−0.25, 0.25) |
| u > 179 | t = (t/1000+1)·1000; re-place (pass placement above, the side flips because aim was reset) |
| t < 3000 | hurtbox zero, `+0xa2 = 0`, `FUN_006b1b40(p,0x14)` **every frame** (Accel kept alive), `+0xa0 = 0` |
| t ≥ 3000 (after pass 3) | → **0xf1** (t=0); SE 0x32; if opp.x ≤ x: aim π, x = 300 (DAT_008145f0) else aim 0, x = −300 (DAT_00815f44); **y = 1500**; root cmd 0x38e |

### 0xf1 — Bird Form, landing (T2 FUN_00715920)
| t | Action |
|---|---|
| < 101 | y *= 0.95 (`_DAT_00814124`) (descends from 1500) |
| 24..72, t%3==0 | cosmetic sparks 0xa82+rand%8 (2 RNG draws), effect `+0x40 = (t−25)/100 + 0.25` |
| 100 | own kinds 1000/700/800/900 → `+0x84=900`, `+0x68=0` (fold up); y = 0; **phase = 1**; `FUN_006b1b40(p,0x14)` |
| < 100 | hurtbox zero, `+0xa2 = 0` |
| always | `+0xa0 = 0` |
| > 130 | state 0 |

---

## 4. Animation

### 4.1 Sprite registration (loader `FUN_0070bf00`)
Arg = colour variant: 0 → folder text id 0x18, else 0x19 (only **2** variants; stats +0x48/+0x60). Quads
`FUN_00467d00(S,S,2,2,1,0)` unless noted. Storage `0x885b80 + slot*0x3888 + id*0x18`.
| id | pattern | frames | S | use |
|---|---|---|---|---|
| 200 (0xc8) | `nt_std0%d` | 3 | 96 (DAT_00814118) | neutral |
| 0xcd | `nt_fwd0%d` | 3 | 96 | neutral moving |
| 0xce | `nt_bck0%d` | 3 | 96 | neutral moving |
| 0xca | `nt_los0%d` | 6 | 96 | KO / hit |
| 0xcc | `nt_sld0%d` | 4 | 96 | shield + every attack state |
| 0xc9 | `nt_dsh0%d` | 6 | 96 | dash |
| 0xcf | `nt_rng0%d` | 2 | 512 (DAT_008178b0) | stats +0x3c |
| 0xd1 | `nt_system` | 1 | 512 | stats +0x44 |
| 0xf8 | `atch_body0%d` | 3 | 192 (DAT_0081415c) | weapon root (kind 100) |
| 0xf9 | `atch_legl00`, `atch_legr00` | 2 | 384 (DAT_0082524c) | legs (kinds 300/0x15e) |
| 0xfa | `atch_ovip0%d` | 3 | 192 | part 400 |
| 0xfb | `atch_arm` | 1 | 96 | arm (kind 500) |
| 0xfc | `atch_hand0%d` | 3 | 144 (DAT_00814138) | hand (kind 600), fists |
| 0xfe | `atch_wing0%d` | 9 | 192 | kinds 700/0x2c6/0x2d0 (3 draw blocks) |
| 0xff | `atch_gene0%d` | 3 | 96 | kind 800 |
| 0x100 | `atch_cowl0%d` | 6 | 192 | kinds 900/0x38e (2 draw blocks) |
| 0xfd | `atch_cannon0%d` | 3 | 384 | part 1000 (hand cannon, phase 2+); frame 0 also blitted by effect 0x15e |
| 0xf2 | `nt_extension%d` | 3 | frame 0: mesh `FUN_00468d80(256,256,256,30,30,0)`; frames 1-2: 320 (DAT_008178c0) | enemy 9999 (transform flash): frame 0 is a 3-D mesh, frames 1/2 quads |
| 0xf3 | `nt_exp0%d` | 3 | 256 (DAT_00814160) | explosions |
| 0xf4 | `nt_mis0%d` | 3 | 64 (DAT_008140f8) | missiles |
| 0xf5 | `nt_fannel0%d` | 1 | 64 | laser bit (enemy 5000) |
| 0xf6 | `nt_laser0%d` | 2 | 256 | lasers |
| 0xf7 | `atch_ref0%d` | 3 | 128 (DAT_00814134) | reflectors (missile 200/0xc9) |
| 0xee | `nt_fuse0%d` | 7 | 74 (DAT_0081cd9c) | mine launcher 0x1450 (frames 0-2), mine 0x1451 (3), wall mine 0x1452 (4-6) |
Weapon-part → sprite assignment (enemy draw `FUN_005336b0`, dispatch on `+0x10` at 0x5336d5…0x53383c, jump table
0x53d254 / byte table 0x53d26c for 700…900): **100 (root) and 0x2da draw nothing**; 200 → 0xf8 body; 300 → 0xf9
frame 0 (legl), 0x15e → 0xf9 frame 1 (legr); 400 → 0xfa ovip; 500 → 0xfb arm; 600 → 0xfc frame 0; 700/0x2c6/0x2d0 →
0xfe frames 0-2/3-5/6-8; 800 → 0xff; 900/0x38e → 0x100 frames 0-2/3-5; 1000 → 0xfd; 5000 → 0xf5; 0x1400 → 0xfc frame 0;
0x13ec/0x13f6 → 0xfc frame 1; 0x13ed/0x13f7 → 0xfc frame 2; 0x1450/0x1451/0x1452 → 0xee; 9999 → 0xf2.
`FUN_0070f4e0` = pause-menu command list (text ids 0x340–0x349), no gameplay.

### 4.2 Draw transforms
Same as suguri.md §4.2: AIM transform (rotZ(aim), plus rotX(180°) when \|aim\| ≥ π/2, rotY(180°)) for all
attack states and 0xc9/0xca/0xcb/0xcc; neutral 0xc8 uses rotY(180°) when facing != 0. Global tint
`FUN_004e8f50` on every draw; afterimage `FUN_004d7e00` only in 0xc8 and 0xc9. The weapon is drawn
separately by the enemy draw (enemy layer `+0x1c` = 1; mine launchers layer 2).

**Weapon/object draw rules (FUN_005336b0).** Every part: position `+0x40/+0x44`, scale (`+0x60`, `+0x64`, 1),
colour via +0xdc **(a, r, g, b)**, z = `+0x68` used as brightness/alpha.
- "3-layer" parts (200, 400, 700-family, 900/0x38e, 1000, 0x1450, 0x1452): base frame f0 with colour (z,z,z,z) for 200/400
  or (1,z,z,z) for the others, then blend 2 and frames f0+1, f0+2 with colour (z, g, g, g),
  g = sin((age%50)/50·2π)·0.25 + 0.75 resp. ·0.35 + 0.65 (DAT_00813f24/3c, DAT_00813f2c/0081788c), age = `+0x78`.
- Parts 700-family, 800, 900/0x38e, 1000 are skipped while `+0x64 == 0` (hidden).
- 300/0x15e/600: single frame, colour (z,z,z,z); legs get an extra RotX(180°) when their motion is 0x10/0x11 (walking).
- 500 (arm): 7 copies on the segment from E = hand − dir(hand angle)·60 (DAT_00813a58) to the parent (part 200):
  copy k at E + (parent − E)·k/6 (DAT_00813f44 = 6), scale ×0.5 except k = 6; colour (z,z,z,z).
- 800: frame (`DAT_008858f8`/5)%3 (match frame counter).
- fists/0x1400: black (1,0,0,0) while growing (`+0x84 == 0`), else (z,1,1,1).
- Transform: AIM transform about `+0x58` for body parts and fists; RotZ only for 5000, 0x1450/0x1451/0x1452.
- 9999: blend 2; frame 0 = mesh `FUN_00468d80(256,256,256,30,30)` scaled (s,s,s), colour (z,.25,.25,.25), RotZ(+0x58) then
  RotY(frame%360°); frames 1 and 2 drawn twice at y ± 120·s (DAT_00814174) with RotX(10°), RotY((frame%120)·3°),
  colours (z,1,1,1) / (z,.5,.5,.5).
Lasers (FUN_00623440, sprite 0xf6 bank `+0x14`, 2-D blits): 100 = per segment (except the last) frame 0: segment 0 a
60(+10 when (age/3) even) square, src (0x1c0,0xc0)-(0x200,0xff), white, blend 2; others two quads (50 × (50(+10)) colour
0x20ffffff src (0x140,0xc0)-(0x17f,0xff); 50 × 10 white src (0x100,0xc0)-(0x13f,0xff)), heights ×(30−age)/10 after age 20.
200/0xfa/0x104 = frame 1 centred at origin + dir·1000, half-length 1000: glow half-height 150/180 (200), 240/280 (0xfa),
280/320 (0x104) alternating every 2 ages, colour 0x40ffffff blend 1; ×0.75 colour 0x80ffffff blend 2 src (0,0)-(511,99);
core 40 / 60 / 70 colour 0xc0ffffff blend 2 src rows ((age/2)%3)·40+200; all heights ×(40−3·age)·0.1 before age 10 and
×(70−age)/20 after age 50.
Effects (FUN_00500fb0): 100/0x96 beamsphere glow (60 × 12/16, alpha 64·a, tint (r=(a&0x7f)·2,g,b=ff) for palette 0 else
(r,g=ff,b=(a&0x7f)·2), blend 1) + core (30/40 × 5, alpha 255·a, blend 2); 0xaa beamemitb 60×40 (alpha 32·a) +
beamemit 48×16 (age < 2) or beamemitb 40×8 (white < age 5, then 0x0080ff / 0xffff00 by palette); 200/400 explosion0/1
cells (180·s, alpha 128·a; second 200·s resp. 180·s ×0.85 on even ages, alpha 255·a / 200·a), 200 rotated by
−`+0x38`, 400 unrotated; 300/0x12d sprite 0xfc frame 1/2 and 0x15e sprite 0xfd frame 0 as mirrored blits (72·s / 192·s,
grey (a,a,a,a), flipped vertically too when |rot| ≥ π/2); 0x19a beamsphere glow 120·s (128·a·s) + core 40·s (255·a·s).
Beam 300 (FUN_004b0960): beamsphere glow 30·scale alpha 96·a blend 1 + core 15·scale alpha 255·a blend 2 while charging.

### 4.3 Common states → Nath draws
0 → 0xc8 (FUN_00717b30), 1 → 0xcb (FUN_007181e0), 2 → 0xc9 (FUN_00717de0), 3/4 → 0xcc (FUN_007184b0),
5 → 0xcb, 6 → 0xca (FUN_00718750) (call sites all.c:356161-356509, FUN_006b07a0…FUN_006b1580).

### 4.4 Per-state frames (`pp(n)` = `(t/n)&3`, 3→1)
| State | T1 | Sprite | Frame |
|---|---|---|---|
| 0xc8 | FUN_00717b30 | 0xc8; y+→0xcd; y−→0xce; then facing0&x−→0xcd, facing1&x−→0xce, facing0&x+→0xce, facing1&x+→0xcd (later wins; 0x717b55-0x717c25) | pp(5) |
| 0xc9 | FUN_00717de0 | 0xc9 | pp(3); +3 when moving against facing (facing1 && \|aim\|>π/2, or facing0 && \|aim\|<π/2) |
| 0xca | FUN_00718750 | 0xca | pp(3) + 3 if `DAT_008858f8` even; **not drawn when t ≥ 100** |
| 0xcb | FUN_007181e0 | 0xca | pp(3) |
| 0xcc | FUN_007184b0 | 0xcc | 0 |
| 0xd2, 0xd3, 0xd4 | FUN_00718a50 / 00718d30 / 00719010 | 0xcc | f = t/4; if f>0: f = ((f−1)&3)+1; f==4 → 2 |
| 0xd5 0xd8 0xd9 0xda 0xdc 0xdd 0xdf 0xe3 0xe4 0xe5 0xe6 0xe7 0xe8 0xe9 0xea 0xeb 0xec 0xed 0xee 0xef 0xf0 0xf1 | FUN_007192f0, 007195b0, 00719870, 00719b30, 00719df0, 0071a0b0, 0071a370, 0071ae70, 0071b130, 0071b3f0, 0071b6b0, 0071a630, 0071a8f0, 0071abb0, 0071b970, 0071bc30, 0071bef0, 0071c1b0, 0071c470, 0071c730, 0071c9f0, 0071ccb0 | 0xcc | min(t/4, 3) |
(0xf0's t includes the +1000 pass offsets → frame 3.)

---

## 5. Spawned objects (all type 8)

### 5.1 Beams — spawn FUN_004b0750, update FUN_004b0ea0, draw FUN_004b0960, onHit FUN_004b0cc0
Template FUN_004ba490 (suguri.md §5.1 / objects.md §2.2) then:
| id | speed | dmg | shape {hx,hy,r} | knock | other | behaviour |
|---|---|---|---|---|---|---|
| 100 Spread Beam | 12 (DAT_00813f54) | 120 | {24, 8, 0} (DAT_00853b8c, DAT_008140d4) | 10 | | straight; dies outside arena+10; Nath effect 100 each frame. onHit: effect 0x3f2, particles 1000×5, spark 1000, SE `FUN_007bcaf0(0x13,3)` |
| 200 Reflecting Beam | 10 (DAT_00813f4c) | 200 | {30, 10, 0} (DAT_008140e8; ver<0x58a: active=0) | 10 | destroyOnHit 0, rehitFlag 1 | **never hits players directly** (objects.md §4.4). Each frame: move; if age < 1000 and outside arena (margin 0): bounce — n = atan2(y,x), moveAngle = 2n − moveAngle + π (`FUN_005f0ce0` result discarded → **not wrapped**), pos = n-direction·540, `+0x64` = moveAngle, **age += 500**, SE `FUN_007bcaf0(0x25,5)` (`reflect.wav`), effect 0x516. Then dies outside arena+20; else hitbox **100** at pos, angle moveAngle, ownerId = beam serial; Nath effect 0x96. ⇒ 1 or 2 bounces (age 0..499 → 2 possible). |
| 300 reflected beam | 24 (DAT_00853b8c) | 160 | {20, 8, 0} | 10 | active 0 initially | age < 1000: stationary; (+0x58 +0.05/frame capped at 1.0 — no visible effect since init is 1.0); age > 49: age = 1000, active = 1, moveAngle = draw angle = atan2(opp − pos), SE `FUN_007bcaf0(0xf,5)` (`laser02.wav`), effect 0x515. Then: if speed > 12: speed ×0.975 (`_DAT_00814128`); move; dies outside arena+10; Nath effect 0xaa each frame. onHit as 100. |
Draw: only beam 300 during its charge (age<1000): two textured quads (half-size 30·scale and 15·scale /zoom,
alpha 96·a and 255·a; texture region from `DAT_008859f4 + team*0x10`) — cosmetic.

### 5.2 Hitboxes — spawn FUN_005e6f50, onHit FUN_005e75b0
Fields as suguri.md §5.2 / objects.md §2.4 (`+0x48 rehitFlag`). All hitType 3 except **100 (hitType 1, dodged by dash)**.
| id | spawned by | dmg | shape {hx,hy,r} | knock | rehit | onHit |
|---|---|---|---|---|---|---|
| 100 | beam 200 every frame | 200 | {30, 10, 0} (DAT_008140e8, DAT_00813f4c) | 10 | | **no damage** if victim state 1 && speed > 2 (DAT_00813a4c); else effect 0x3f2, particles, SE 0x13 |
| 200 | fists 0x13ec/0x13ed (τ 3..10) | 200 | {60, 40, 0} | 15 (DAT_00853b88) | 1 | no damage if victim state 1 && (t<5 or speed>2); else streak effects 0x456 ×6, shake `FUN_004d7ac0(10,0.75,1)`, SE 0x18 (`hit.wav`) |
| 0xd2 | fists 0x13f6/7 (τ==8) | **120** (old 90) | {100, 80, 0} (DAT_00853b90, DAT_00814164) | 2.5 (DAT_008153e0) | | streaks 0x456 ×6, SE `FUN_007bcaf0(0x18,3)` |
| 0xdc | big fist 0x1400 (τ==8) | **200** (old 120) | {100, 120, 0} | 20 (DAT_008140fc) | | effect 0xa8d, SE 0x19 (`hit_f.wav`), shake |
| 0xfa | hand, 0xe9 t 6..24 | 400 | {120, 40, 0} (DAT_00814174, DAT_008140e0) | 20 | 1 | no damage if victim state 1 && (t<5 or speed>2) && (ver<0x58b or `frame − victim+0x174 < 40`); else effect 0xa8d, SE 0x19, shake, `victim+0x174 = DAT_008858f8` |
| 300 | laser 200 tick | 40 | {800, 48, 0} (DAT_008149c8, DAT_008140e4) | 10 | | generic: streaks ×6, particles, SE `FUN_007bcaf0(0x13,4)` |
| 0x12d | laser 200 off-tick | 0 | {800, 60, 0} | 5 | | (none) |
| 0x136 | laser 0xfa tick | 70 | {800, 64, 0} | 10 | | generic |
| 0x140 | laser 0x104 tick | 90 | {800, 82, 0} (DAT_00817db4) | 10 | | generic |
| 0x137, 0x141 | laser 0xfa/0x104 off-ticks | — | **not in the spawn switch → returns −1, nothing spawned** | | | |
| 400 | hand, 0xec t 11..24 | 0 | {140, 50, 0} (DAT_00817004) | 0 | | sets **own** player's `+0x158 = 1` (`FUN_00780450(team)`), SE 0x18 |
| 0x19a | hand, 0xed t=60 | 900 | {0, 0, **320**} (DAT_008178c0; ver ≤ 0x58a: 160) | 30 (DAT_008140e8) | | hb angle := atan2(−victim.y, −victim.x) (**knockback toward arena centre**), effect 0xa8d |
| 500 | mines 0x1451/0x1452 | 300 | {0, 0, 40} | 5 | | angle := atan2(victim − hb); 5 effects 500 (2 RNG each); **`FUN_006b1a40(victim, 1, 500)`** = status k=1 (heat +0.007/frame, suguri.md §7) for 500 frames, SE 0x26 |
| 600 | Bird Form 0xf0 (every 3 t) | 120 | {160, 140, 0} (DAT_00814168, DAT_00817004) | 15 | | generic |
"generic" = streaks 0x456 ×6 (1 RNG each), particles 1000, spark 1000, SE `FUN_007bcaf0(0x13,4)`.

### 5.3 Missiles — spawn FUN_00671750, update FUN_00673870, draw FUN_00671b90, onHit FUN_006733d0, onKilled FUN_006735e0, onAbsorb FUN_00673790
Template FUN_00685bb0 (objects.md §2.3), then (offsets: `+0x70` speed, `+0x74` aux, `+0x78` hp, shapes `+0x80`/`+0x90`, `+0xa0` dmg, `+0xa4` knock):
| id | speed | hp | dmg | body / proj shape | knock | other |
|---|---|---|---|---|---|---|
| 100 bomb (Homing Bombing) | 0 | 60 | 150 | r 10 / r 10 | 10 | |
| 0x6e bomb (Carpet / Bird) | 0 | 120 | 200 | r 16 / r 16 | 10 | |
| 0x96 homing (B ph2/3) | 0 | 60 | 150 | r 10 / r 10 | 10 | |
| 0xa0 homing (B+H ph3) | 0 | 120 | 200 | r 16 / r 16 | 10 | |
| 200 / 0xc9 reflector | 0 | 120 | 100 | {10, 40, 0} / {10, 40, 0} | 10 | growScale 0, `+0x60 = 1`, phase `+0x64 = 0`, **absorbs = 1** |
| 300 mine carrier | 16 | 120 | 0 | none | 10 | growScale 0, `+0x60 = 0`, **active 0** |
Caller then overwrites `+0x70` (and `+0x68/+0x6c/+0x74`) as given in §3.

Updates (age `a` = `+0x08`, update sees 0 first; `a` is overwritten to 1000 to mark phase 2):
- **100**: a < 1000: pos += dir(`+0x6c`+π/2)·`+0x70` (sideways drift); pos −= dir(`+0x6c`)·cos(a/50·π)·8 (back then forward);
  a > 49: speed = 0, `+0x74 = 120 − rand_f(0, 50)`, a = 1000. Phase 2: `+0x74 −= 1`; if `+0x74 > 0` and speed < 10: speed += 0.1;
  if `+0x74 ≤ 0`: speed ×0.975; pos += dir(`+0x68`)·speed; on even a: particle 0x456 (cosmetic). Explodes (onHit, removed)
  when (a > 1200 and outside arena+10) or `+0x74 < −50`.
- **0x6e**: as 100 but period 30 (`cos(a/30·π)`), a > 29: speed = 5, `+0x74 = 50 − rand_f(0,30)`, `+0x68 += rand_f(−0.25,0.25)`;
  accel +0.2 up to 15; leaves arena margin 20; extra effect 0x547 on even a.
- **0x96** (a < 300) / **0xa0** (a < 200): `turnToward(+0x6c, angle to opponent, speed·0.0125)` (FUN_005f0e70, `_DAT_00815880`);
  v = dir(`+0x68`)·speed + dir(`+0x6c`)·`+0x74`; `+0x68 = atan2(v)`, speed = \|v\|; if a > 20: speed capped at 3 (0x96) / 7.5 (0xa0, DAT_00814150).
  Afterwards speed ×0.985 (`_DAT_00814148`, 0x96) / ×0.9 (DAT_00814178, 0xa0), explode at a > 400 / a > 250. Move pos += dir(`+0x68`)·speed.
- **200 / 0xc9**: phase 0: growScale += 0.1; when > 1: phase 100, a = 0, growScale 1, `+0x7c active = 1`. Phase 100: pos += dir(`+0x6c` ± π/2)·sin(a/30·2π)
  (+ for 200, − for 0xc9) — sideways oscillation; a > 150 → phase 900, active 0. Phase 900: growScale −= 0.1, removed at ≤ 0.
  (The template FUN_00685bb0 sets `+0x7c = 1` (line 20) and spawn id 200/0xc9 does not clear it, so the reflector is
  already active — absorbing beams — while it grows.)
- **300**: pos += dir(`+0x68`)·16; every a%3==0: enemy **0x1451** at pos, `+0x58 = +0x5c = +0x68`; if a > 9 and outside arena+20:
  enemy **0x1452** there, removed.
onHit (hit a player / expire): ids 100/0x6e/0x96/0xa0/200: Nath effect 200 + effect 0x514, SE 0x14 (`bom06.wav`); returns false (destroyed).
onKilled (shot down): effect 0x262 + 0x514, SE 0x1d (`gun12.wav`, bombs) / 0x1a (`tm2_bom002.wav`, reflectors); destroyed.
**onAbsorb** (enemy beam hits reflector 200/0xc9): effect 0x516 at the beam, beam **300** at the absorbed beam's position with the
reflector's `+0x6c` angle, owner and team; SE `FUN_007bcaf0(0x25,3)`; the incoming beam is erased (objects.md §4.5); the reflector is unharmed.

### 5.4 Lasers — spawn FUN_00623170, update FUN_006249a0, draw FUN_00623440, onHit FUN_00624850
Template FUN_006307d0; then `+0x0c hitType 3`, `+0x44 knock 5` (DAT_00813f40), `+0x4c stun 1`:
| id | blockMode `+8` | `+0x40` init | `+0x48` | behaviour |
|---|---|---|---|---|
| 100 (laser bit beam) | 2 (cut by bodies) | 10 | 200 | removed after age 29; age 1: effect 0x546; 60 circle segments at origin + dir·k·30 (k=0..59, DAT_008140e8) radius 20 (DAT_008140fc) while age ≤ 20, else 0; damage **50 on age%4==0**, else 0. onHit: on even age: spark 1000 + 5 streaks (RNG) |
| 200 (S+A ph3) | 1 | 0 | 999 | no segments. Owner player = `FUN_00780450(team)`: if it is not in 0xea and age < 51 → age = 50 (and ver>0x58a `+0x4d = 1` stop following). Ages 15,20,…,45 (10<a<50, a%5==0): `FUN_007807f0(owner,0xea)` fail → age = 50. If `+0x4d==0` follow enemy `FUN_0055f060(+0x18)` (pos, angle = enemy `+0x5c`). Removed after age 69. Ages 10..50: hitbox at origin+dir·800: a%5==0 → **300**, else **0x12d** |
| 0xfa (H ph2) | 1 | 0 | 999 | as 200 but checks owner state 0xeb, no ammo, no `+0x4d` write; hitboxes **0x136** on a%5==0 (others: none) → ages 10,15,…,50 = **9 hits** max |
| 0x104 (H ph3) | 1 | 0 | 999 | as 0xfa with hitbox **0x140** |
Laser age is pre-incremented (first update sees 1). Draw FUN_00623440 not reversed (cosmetic; sprite 0xf6).

### 5.5 Enemies — spawn FUN_0052d0f0, update FUN_0052eaf0, draw FUN_005336b0, onKilled FUN_005335b0
Init `FUN_0055ee70` (objects.md §2.6; `+0x70 = +0x74 = 0`, scale 1, mirror 1) then per id. `+4` = group serial `FUN_0055efc0()`.

#### 5.5.1 Weapon (id 100) — 14 parts pushed in this order (all hp/maxHp 9999, active 0, shape 0, not solid)
| order | `+0x10` id | kind `+0x7c` | parent | offset `+0xc8,+0xcc` | pivot `+0xbc,+0xc0` | motion `+0x88` | scale | `+0x84` |
|---|---|---|---|---|---|---|---|---|
| 1 | 100 | 100 (root) | −1 | — | — | 0 | 1 | 0 |
| 2 | 0x2c6 | 700 | 100 | 20, 36 | −32, 0 | 0x1b | 0 | 1000 (hidden) |
| 3 | 0x15e | 300 | 100 | 0, −36 | −82, 0 | 0xf | 1 | 0 |
| 4 | 200 | 200 | 100 | 0, 0 | 0, 0 | 3 | 1 | 0 |
| 5 | 400 | 400 | 100 | 10, −40 | −60, 0 | 0xd | 1 | 0 |
| 6 | 300 | 300 | 100 | 10, −36 | −90, 0 | 0xe | 1 | 0 |
| 7 | 900 | 900 | 100 | 0, 50 | 82, 0 | 0x1e | 0 | 1000 |
| 8 | 0x38e | 900 | 100 | 0, −50 | 82, 0 | 0x1f | 0 | 1000 |
| 9 | 500 | 500 (arm) | 200 | 0, 0 | 30 (→20/50 by phase), 0 | 0x12 | 1 | 0 |
| 10 | 600 | 600 (hand) | 500 | −30 (→−20/−50), 0 | 64, 0 | 0x13 | 1 | 0 |
| 11 | 700 | 700 | 100 | 30, 20 | −60, 0 | 0x1a | 0 | 1000 |
| 12 | 0x2d0 | 700 | 100 | 30, −20 | −60, 0 | 0x1c | 0 | 1000 |
| 13 | 800 | 800 | 100 | 30, 0 | 0, 0 | 3 | 0 | 1000 |
| 14 | 1000 | 1000 | 600 | 0, 0 | 0, 0 | 3 | 0 | 1000 |
(Values: DAT_00813f4c 10, DAT_00817dc0 −40, DAT_008149e8 −60, DAT_00817dbc −36, DAT_00817dc8 −90, DAT_00817dc4 −82,
DAT_008140e8 30, DAT_00817c08 −30, DAT_008140f8 64, DAT_008140fc 20, DAT_008140f4 36, DAT_00817db8 −32, DAT_00814108 −20,
DAT_008140ec 50, DAT_008149e4 −50, DAT_00817db4 82. A 15th record (id 0x2da, kind 700, motion 3) is initialised but **never pushed**.)
Attachment each frame after the root update (objects.md §3.5, `FUN_00528250`; root `+0x6c` mirror = 1 → flipped when \|angle\| > π/2).

**Motions used** (`FUN_00526670`, keys {t, angle, x, y} — all Nath keys have x = y = 0; len in frames, loop):
| id | len | loop | angle keys (t:angle) |
|---|---|---|---|
| 3 | 100 | 1 | 0:0, 1:0 |
| 0xd | 200 | 1 | 0:0.75, .25:0.85, .5:0.75, .75:0.65, 1:0.75 |
| 0xe, 0xf | 200 | 1 | 0:1.2, .5:1.3, 1:1.2 |
| 0x10, 0x11 | 200 | 1 | 0:2, .5:1.8, 1:2 |
| 0x12 | 200 | 1 | 0:−1.75, .5:−2, 1:−1.75 |
| 0x13 | 200 | 1 | 0:0.5, .5:1, 1:0.5 |
| 0x16 | 20 | 0 | 0:0.4, .5:−0.2, 1:−0.8 |
| 0x17 | 20 | 0 | 0:1.5, .5:1, 1:0.25 |
| 0x18 | 10 | 0 | 0:−0.4, .5:−0.2, 1:0 |
| 0x19 | 10 | 0 | 0:0.4, .5:0.2, 1:0 |
| 0x1a | 200 | 1 | 0:0, .5:−0.25, 1:0 |
| 0x1b | 200 | 1 | 0:0, .5:−0.2, 1:0 |
| 0x1c | 200 | 1 | 0:0, .5:0.25, 1:0 |
| 0x1e | 100 | 0 | 0:0.4, .5:0.1, 1:−0.2 |
| 0x1f | 100 | 0 | 0:−0.4, .5:−0.1, 1:0.2 |
(Loop flag = the write to `DAT_008855f0 + id*0x14` itself (e.g. `DAT_00885690 = 0` is slot 8's flag); the values above are re-checked against that rule and match src/sim/motion.c `slot_info`.)

**Per-kind update (FUN_0052eaf0):**
- kind 100 (root): every command first sets pos = Nath pos, `+0x58` = wrap(Nath `+0x84`); default (cmd 0/unknown): pos = Nath,
  `+0x58 = (|aim| ≤ π/2) ? 0 : π`. Commands: 100 (ends τ=30); 200, 0xd2, 0xfa, 0x104, 300 (ends τ=50), 400 (ends τ=50) — spawns as in §3;
  500/600/0x262/0x28a/700/0x2c6/0x2ee/800/900/0x38e/0x398 — follow only, end when Nath leaves 0xe3/0xe7/0xe8/0xe9/0xea/0xeb/(0xec,0xed)/0xee/0xef/0xf0/0xf1 respectively.
- kinds 200, 400: cmd 999 → scaleZ −0.02/frame to 0 (KO fade). 
- kind 300 (both legs): motion 0xe/0xf normally, 0x10/0x11 when "moving" (y− held, or x toward/away per facing: facing0&x− → false, facing1&x− → true, facing0&x+ → true, facing1&x+ → false; forced false unless Nath state 0); 999 fade.
- kind 500 (arm) / 600 (hand): pivot/offset by phase (§1.5); scaleX = 0 while Nath in 0xe7/0xe8, else 1; motion: 0xe9@t5 → 0x16 (arm) / 0x17 (hand);
  0xea/0xeb/0xec/0xed/0xee@t1 → 0x18 (arm) / 0x19 (hand); 0xef@t20 → 0x18/0x19; 0xef/0xf0/0xf1 otherwise unchanged; any other state → 0x12 / 0x13.
  The hand also performs 0xe9/0xea/0xeb/0xec/0xed actions (§3). 999 fade.
- kinds 700, 800, 900, 1000 (extensions): cmd 0: scaleX −0.1, scaleY +0.05 per frame until scaleY ≥ 1 → scale (1,1), cmd 100, τ=0;
  cmd 100: scaleZ +0.1 to 1; cmd 900: scaleX +0.2, scaleY −0.1; at scaleY ≤ 0 → scale 0, cmd 1000 (hidden).
- id 9999 (flash): follows Nath; age `+0x78` < 51: scaleX +0.1, scaleZ +0.05 (cap 1); then scaleZ −0.025, scaleX +0.02, removed at scaleZ ≤ 0.

#### 5.5.2 Fists
| id | spawned by | init | update |
|---|---|---|---|
| 0x13ec / 0x13ed | 0xe7 | kind 0x13ec, cmd 0, hp 999, active 0, shape 0, scale (3,0,0), not solid | cmd 0: scaleX −0.2, scaleY +0.1 until ≥1 → scale 1, speed 10, cmd 100, τ=0, SE 0x20, 5 sparks 0xa28+r (2 RNG each). cmd 100: τ 3..10 hitbox **200** at pos, angle `+0x5c`; even τ Nath effect 300 (0x13ec) / 0x12d (0x13ed); τ>9 → cmd 900. Else: speed ×0.85, scaleZ −0.1, removed at ≤ 0. Always pos += dir(`+0x5c`)·speed. |
| 0x13f6 / 0x13f7 | 0xe8 | kind 0x13f6, **solid 1** (shape 0 → never pushes), scale (3,0,0) | as above but speed **12**, 8 sparks 0xa82+r (effect `+0x40 ×= 0.25`); hitbox **0xd2** at τ==8 |
| 0x1400 | 0xe8 t=50 | kind 0x1400, solid 1 | as 0x13f6 (speed 12, sparks `+0x40 ×= 0.5`); hitbox **0xdc** at τ==8 |

#### 5.5.3 Laser bit (id 5000)
Init: kind 5000, cmd 100, **hp 200**, active 0, shape {0,0,10}, scale (3,0,1), mirror byte 0. Caller sets pos/angles/`+0x70`/`+0x74`.
cmd 100: scaleX −0.2, scaleY +0.1 → at ≥1: cmd 200, τ=0, **active = 1** (can be shot down by beams/hitboxes/missiles/lasers; onKilled does nothing → erased).
cmd 200 (τ = 1..50): pos += dir(`+0x5c`)·`+0x70`; w = cos(τ/50·π)·10, negated if `+0x5c` < 0; pos += rotate((0, w), `+0x5c`);
`+0x58 += 0.06283186·+0x74` (DAT_00817dcc; 50 steps = ±π, so a bit spawned facing away from its target ends facing it); τ > 49 → cmd 300, speed 3.
cmd 300: pos −= dir(`+0x58`)·speed, speed ×0.95; **τ == 10: laser 100** at pos, angle `+0x58`, SE `FUN_007bcaf0(0x12,3)`; τ > 99 → cmd 900, active 0.
cmd 900: scaleX +0.2, scaleY −0.1, removed at ≤ 0.

#### 5.5.4 Mines (B+H phase 2)
- **0x1450** (launcher): kind 0x1450, hp 999, active 0, scale (3,0,0), **draw layer `+0x1c` = 2** (`local_f98`). cmd 0: scaleX −0.2, scaleY +0.1; at scaleY ≥ 1:
  scale 1, cmd 100, τ=0, missile **300** at pos, angle `+0x58`, SE `FUN_007bcaf0(0x22,5)`. cmd 100: when τ > 200 → cmd 900, scaleZ 0. Else: scale shrink (+0.2/−0.1), removed at scaleY ≤ 0.
- **0x1451 / 0x1452** (mines, laid every 3 frames by missile 300 / at the wall): kind = id, hp 999, active 0. When `+0x80` > 149 (150 frames after
  being laid): hitbox **500** at pos, angle `+0x5c`; onKilled `FUN_005335b0` (effects 0x26c, 500, 0x515, SE `FUN_007bcaf0(0x1a,10)`); removed.

### 5.6 Nath effect family `0x8816b8` (FUN_00500ae0 / update FUN_00502f80 / draw FUN_00500fb0) — cosmetic
Ids 100 (spread-beam trail), 0x96 (reflecting beam trail), 0xaa (reflected beam trail), 200 (missile burst), 300/0x12d (fist trails),
0x15e (punch), 400 (grab glow), 0x19a (grab sparks; **spawn draws 1 RNG** rand_f(1,4)). Generic effects used: 0xfa, 0x262, 0x26c, 0x3f2, 0x456,
0x514, 0x515, 0x516, 0x546, 0x547, 0xa28–0xa2f, 0xa82–0xa89, 0xa8d, 0xaf0, 500, 0x7da (particles). All consume RNG where noted — keep call order.

---

## 6. Sound ids used (SE table all.c:464010; id = (addr − 0x9b99a0)/4)
| id | file | where |
|---|---|---|
| 6 | up01.wav | hyper start |
| 7 | high_a.wav | Bird Form |
| 0xe | tm2_gun000.wav | Spread Beam |
| 0xf | laser02.wav | reflected beam fires |
| 0x11 | tm2_laser000.wav | Reflecting Beam |
| 0x12 | gun11_r.wav | lasers (bits, hand) |
| 0x13 | don16.wav | beam/laser hits |
| 0x14 | bom06.wav | missile expire/hit |
| 0x18 | hit.wav | fist hits |
| 0x19 | hit_f.wav | big hits (0xdc, 0xfa) |
| 0x1a | tm2_bom002.wav | reflector shot down, mine explosion |
| 0x1d | gun12.wav | bomb shot down |
| 0x1e | tm2_bom003.wav | grab explosion |
| 0x20 | swing_h.wav | fists launch, lunges |
| 0x22 | burst00.wav | missile launches |
| 0x25 | reflect.wav | beam bounce / absorb |
| 0x26 | jya05.wav | burn status applied (via FUN_006b1a40) |
| 0x2f | power35.wav | bits / reflectors deployed |
| 0x30 | synth-sweep2.wav | grab |
| 0x32 | nori_ge_j_jet01.wav | Bird Form flight |
| 0x33 | synth-sweep1.wav | transform flash |

---

## 7. Other per-character table entries (index 8)
| Table | Entry | Role |
|---|---|---|
| 0x881330 | FUN_00715cb0 | input dispatch std (§2) |
| 0x881380 | FUN_00716350 | input dispatch alt (§2) |
| 0x8813d0 | FUN_0070bf00 | sprite loader (§4.1) |
| 0x881480 / 0x881484 / 0x881488 | FUN_007172e0 (level 0), FUN_00716a00 (levels 1, 2) | CPU AI (not reversed; reads `+0x164`, uses RNG) |
| 0x881898/0x8818e8/0x881938/0x881988 | 4b0750 / 4b0ea0 / 4b0960 / 4b0cc0 | beams |
| 0x8819d8/0x881a28/0x881a78/0x881ac8/0x881b18/0x881b68 | 671750 / 673870 / 671b90 / 6733d0 / 6735e0 / 673790 | missiles (spawn/update/draw/onHit/onKilled/onAbsorb) |
| 0x881bb8/0x881c08 | 5e6f50 / 5e75b0 | hitboxes |
| 0x881c58/0x881ca8/0x881cf8/0x881d48 | 623170 / 6249a0 / 623440 / 624850 | lasers |
| 0x881578/0x8815c8/0x881618/0x881668 | 52d0f0 / 5336b0 / 52eaf0 / 5335b0 | enemies (spawn/draw/update/onKilled) |
| 0x8816b8/0x881708/0x881758 | 500ae0 / 502f80 / 500fb0 | Nath effects |
| 0x881d98.. (whip) | — | none |
T2 table entries 0x880cc0…0x880d64 and T1 0x880350…0x8803f4: registry.c:583-656.

## 8. Stats record (index 8, base 0x883660), FUN_0049ae40 registry.c:1612-1645
| Off | Value | Meaning |
|---|---|---|
| +0x00 | 4.0 (DAT_00853b68) | walk speed per axis |
| +0x04 | 15.0 (DAT_00853b88) | dash speed |
| +0x08 | 9.0 (DAT_00813f48) | UNVERIFIED (Suguri: 12) |
| +0x0c..+0x20 | 6, 8, 10, 3, 7, 11 | UNVERIFIED ints |
| +0x3c/+0x40/+0x44 | 0xcf, 0xd0, 0xd1 | sprite ids (ring / unregistered / system) |
| +0x48/+0x60 | text 0x18 / 0x19 | colour folders |
| +0xa8/+0x1a8 | `mdl\win_font_NT.lfd/.dat` | |
| +0x2a8 | `nt_res_bgp` | |
| +0x2c0/+0x2c4/+0x2c8 | 50, 30, 0 | UNVERIFIED (portrait offset?) |
| +0x2cc/+0x2d0/+0x2d4 | 130, 220, 220 | colour RGB |
| +0x2d8 | 0 | |
HP: Player Init uses 9000 (0x1194 = 4500 when `DAT_0087fad4[slot] < 6`) for everyone except Sumika-2nd (all.c:348034-348080) — Nath has no HP override.

## 9. UNVERIFIED / open items
- (settled) Player Init runs every round: Game Init `FUN_005f4650` (called per round, all.c:301126) calls `FUN_0069cc40`.
- 9999 frame 0 is a 3-D mesh; the 2-D port approximates it (quad, RotY as x-scale). Sprite 0xf3 (`nt_exp`) is registered but not referenced by any Nath draw found.
- Beam 300 `+0x58` growth being a no-op (template sets 1.0 then the type writes `+0x5c`) — cosmetic.
- Enemy id vs player id namespace (§0 note 5): does anything resolve these ownerIds as players?
- AI functions FUN_007172e0 / FUN_00716a00 not reversed.
- Hyper input in phase 0 (during 0xd2) — maps to 0xec/0xdc/0xe6/0xeb; check whether common code blocks hypers at round start.
- Stats +0x08, +0x0c..+0x20, +0x2c0..+0x2c8.
