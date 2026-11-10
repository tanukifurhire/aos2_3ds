# Star Breaker — complete moveset spec

Source: `aos2.exe` (Ghidra corpus `tools/re/dec/all.c`, constants read with `xref.exe rd`).
Structure follows `suguri.md`; shared helpers are referenced by the names used there
(`FUN_007807f0` ammo/hyper cost, `FUN_006b1b40` hyper start, `FUN_00780910` hyper spend,
`FUN_006b17d0` dash/dash-cancel, `p+0x158` hit-confirm, ammo `+0xe8/+0xec`). `t` = player state
timer `p+0x154`; the first T2 call of a state sees `t = 0` (suguri.md §1.1). Version global
`DAT_0089426c` = 0x5a2 in live play (objects.md §0.1) → **implement the ver > 0x5a0 branches**.
Object field names follow objects.md §2. "UNVERIFIED" items say what would settle them.

Engine character index **9** (`p+0x10`), handbook id 6. Object "type" = 9 for every object
Star Breaker spawns. Stats record `0x881f80 + 9*0x2dc = 0x88393c`.

---

## 0. Notes for the lead (affect more than this character)

1. **CModel colour call argument order is (a, r, g, b, mesh), not (r, g, b, a).** The vtable
   +0xdc implementation `FUN_0046ef70` builds a D3D material colour `{r=p3, g=p4, b=p5, a=p2}`
   (locals `local_4c..local_40`). Evidence in Star Breaker code: `COLOR(1.0, 0, 0, 0, -1)` is used
   for an opaque black silhouette (Accel body `FUN_00751240`, bomb/missile "not yet full size"
   tint in `FUN_0054e4c0`), and additive glow passes call `COLOR(pulse*alpha, 1, 1, 1)`.
   render.md's table (r,g,b,a) and the global-tint helper `FUN_004e8f50` (passes
   `DAT_00893da0..dac` in that order → `da0` is **alpha**) should be re-read with this order.
   Push order confirmed in disassembly at 0x751438 (first pushed-last arg = 1.0).
2. **Effects can be gameplay objects.** Star Breaker's effect family (type 9,
   `FUN_005094a0`/`FUN_0050bb10`) spawns **hitboxes** (effect 100 → hitbox 100 ×3, effect 0x96 →
   hitbox 0x96) and **modifies the opponent's heat** (effect 700 fire zone). The Effect list must
   be simulated deterministically, in order, in the sim (not the renderer). Several SB effects
   also draw from the game RNG at spawn/update (§5.6).
3. **Hitbox onHit may rewrite the hitbox before damage is applied.** Hitbox 100's callback sets
   `hb.angle(+0x28) = atan2(victim − hb.pos)` so the knockback is radial (objects.md §4.4 says the
   callback runs before Damage/Hitstun — this relies on that order).
4. **Status ailments are applied by `FUN_006b1a40(victim, k, frames)`**: sets flag `+0xa3+k`
   and timer `+0xac+4k` (= max(old, frames) if already set), plays SE 0x26 for k=1,2,7 (every
   call) and SE 0x15 for k=3 (first application only). Refused when victim state 6 or `+0xa2 == 0`. Star Breaker
   inflicts k=1 (heat +0.007/frame) and k=2 (hyper −0.001/frame) for 300 frames — tick functions
   are the shared table at 0x881530 (suguri.md §7).
5. **`FUN_00780a50(victim)` = "can be grabbed"**: `+0xf8 == 0 && state != 6 && +0xa2 != 0 &&
   +0xa9 != 1 && DAT_00885964 != 2`. Used by Star Breaker's two grabs; likely shared by other grab
   moves.
6. **Hyper states may override the hyper-aura timer `+0xf8`** after `FUN_006b1b40`: Star Breaker
   sets `+0xf8 = 200` (instead of 100) in two hypers. Since `FUN_00780950` blocks a new hyper while
   own `+0xf4==3 && +0xf8 > 80`, and objects.md §4.2/4.4 treat `+0xf8 > 0` as "shielded" (no
   damage, hurtbox replaced by the aura), this doubles the protection window.
7. **SE ids are table slots, not string order.** The SE table (all.c:464010..) writes
   `DAT_009b99a0 + 4*id`; several entries are assigned out of order (e.g. id 6 = up01.wav is the
   9th string). Use `id = (addr − 0x9b99a0)/4`. Table in §6.
8. Star Breaker's **D+A, D+H** test Dash with `+0x34 > 1` (held), whereas suguri.md records `> 0`
   for Suguri's D+A (the Saki spec found `> 1` as well) — worth a re-check of Suguri.
9. Copy-paste bug in the game: SB missile 200's onHit calls `DAT_008816c8` = effect spawn table
   index 12 (Iru) (`FUN_0067b5e0`). Harmless — missile 200 is never spawned (§5.8).

### Player fields used by Star Breaker's code (in addition to suguri.md's table)

| Off | Name | Notes / source |
|---|---|---|
| +0x5c..+0x64 | hurtbox shape {hw, hh, r} | reset each frame to {0,0,16}; zeroed during Accel t<80 (FUN_0074b5c0) |
| +0x68 | hurtbox extra | zeroed with the shape in Accel |
| +0x90 | charge speed (float) | Sea of Flame (0x11b) accumulator, +0.5/frame (FUN_0074a900) |
| +0xa2 | hittable (byte) | set 0 during Accel t<80; read by `FUN_00780a50` |
| +0xa3+k / +0xac+4k | status flag / timer k | written by `FUN_006b1a40` |
| +0xa9 | super-armour | blocks grabs (`FUN_00780a50`) |
| +0xaa | heat-lock status (k=7) | while set, fire-zone effect 700 does not add heat (FUN_0050bb10) |
| +0xe0 | heat | effect 700 adds 0.01/frame |
| +0xf8 | hyper aura timer | set to 200 by 0x119, 0x11b; halved (int /2) when 0x119 ends without a grab |

---

## 1. Shared mechanics

Identical to suguri.md §1 (frame model, dash-cancel `FUN_006b17d0(p,1)` heat +0.25, dash speed =
stats+4, `FUN_00612830/00612900` training-display only, helpers, RNG). Differences:

- **Dash speed** stats+4 = 12.0 (DAT_00813f54) as written by the registry (registry.c:1690), but
  the version patch `FUN_004a7270` overwrites it with **13.0** (DAT_00813f58) when ver ≥ 0x58b →
  **13.0** for live play. Walk speed stats+0 = 5.0 (DAT_00813f40).
- **Dash sparks** (cosmetic, consume RNG) appear only in 0x119: `if speed ≥ 3.0 (DAT_00813a50):
  r = rand_f(−0.3, 0.3); n = rand_i(0,100) % 8; generic effect 0xa28+n at pos, angle r + aim`.
- Every hyper start uses the flourish of suguri.md §1.4 with these parameters: SE 6;
  `FUN_004e9280(100, 0.75 (DAT_00813f3c))`; `FUN_004d7ac0(20, 0.5, 2)`; generic effect 0xfa at pos.
  `FUN_007bec20(100, pos, 0, 0)` (cut-in) is called in 0x10e, 0x111 and 0x119→0x11a transition but
  **not** in 0x119 itself or 0x11b. Accel uses its own values (§3).

### 1.1 Ammo / hyper cost table (`FUN_007807f0(p, id)`, entry `0x87c4f0 + id*0xc`)
Written by FUN_0049ae40 (registry.c:1731-1760), patched by `FUN_004a7270` for ver > 0x5a0.

| Entry id | Used by | costA | costB | hyperGain | note |
|---|---|---|---|---|---|
| 0x10c | D+A (per missile) | 0.15 (DAT_00813f20) → **0.09** (DAT_00813f0c, patch writes DAT_0087d180) | 0 | 0.015 (DAT_00813ed4) | |
| 0x10d | B tap (per volley) | 0 | 0.4 (DAT_00813f30) | 0.03 (DAT_00813ee0) | |
| 0x10f | B hold (per mine) | 0 | 0.15 (DAT_00813f20) | 0.05 (DAT_00813ef4) → **0.038** (DAT_00813ee8, patch DAT_0087d1ac) | |
| 0x110 | S+B | 0 | 0.35 (DAT_00813f2c) | 0.05 | |
| 0x113 | A-chain 1 **and** S+A-chain 1 (per hit) | 0.075 (DAT_00813efc) | 0 | 0.02 (DAT_00813ed8) | |
| 0x114 | chain 2 (both), per hit | 0.075 | 0 | 0.02 | |
| 0x115 | chain 3 (both), per hit | 0.1 (DAT_00853b10) | 0 | 0.025 (DAT_00813edc) | |
| 0x116/0x117/0x118 | — | same as 0x113/0x114/0x115 | | | **unused**: the S+A states call the 0x113..0x115 entries |

Hypers (0x10e, 0x111, 0x119/0x11a, 0x11b, 0x11d) never call it.

---

## 2. Input dispatch `FUN_0074b900` (standard scheme) — exact decode

**A. Hypers** — only if `FUN_00780950(p)`; first match wins (failed cost → next row):

| # | Condition | Cost | State | Official move |
|---|---|---|---|---|
| 1 | S held (`+0x40>1`) && B held (`+0x3c>1`) && H pressed (`+0x44==3`) | 3.0 (DAT_00813a50) | 0x11d | **Witness the Final Moment of This Planet** (S+B+H, Accel) |
| 2 | S held && H pressed | 1.0 (DAT_00853b34) | 0x119 | **Accept Your Doom** (S+H) |
| 3 | B held && H pressed | 1.0 | 0x10e | **Take Your Dying Breath** (B+H) |
| 4 | Dash held (`+0x34>1`) && H pressed | 1.0 | 0x111 | **Suffer from the Inevitable Death Crisis** (D+H) |
| 5 | H pressed | 1.0 | 0x11b | **Let the Sea of Flame Embrace You** (H) |

**B. Normal moves** — only if `state == 0 || state == 2`:

| # | Condition | Result | Move |
|---|---|---|---|
| 1 | Dash pressed (`+0x34==3`) | `FUN_006b17d0(p,0)` | Dash |
| 2 | else Shield pressed && `FUN_00780910(p,0.5)` && `FUN_007809e0(p)` | `FUN_006b1930(p,0)` | Shield |
| 3 | S held && B pressed | 0x110 | **Be Charred in the Burning Death Crisis** (S+B) |
| 4 | S held && A pressed | 0x116 | **Melt in the Azure Flame** (S+A) |
| 5 | B pressed | 0x10f (→0x10d if released before t=12) | **Shiver in the Invisible Murderous Intent** (B hold) / **Count Your Last Moments** (B tap) |
| 6 | Dash held (`+0x34>1`) && A pressed | 0x10c | **Fear the Approaching Sparks** (D+A) |
| 7 | A pressed | 0x113 | **Burn in Crimson Flames** (A) |

Then returns `T2[state](p) != 0`.

**Alternate scheme `FUN_0074bdc0`** (diff vs FUN_0074b900): hyper rows become "H held (`+0x44>1`)
and X pressed": 1. Shield-button pressed → 0x11d (3.0); 2. S pressed → 0x119; 3. B pressed →
0x10e; 4. **Dash pressed** → 0x111; 5. A pressed → 0x11b. Normal rows identical.

Not in the manual: A/S+A hit-confirm chains (§3), B-hold re-fire loop, B release during the hold
loop converting into the tap volley.

---

## 3. States

Handler table (T2 = `0x8809a0+4*id`, T1 = `0x880030+4*id`, registry.c):

| id | T2 | T1 | role |
|---|---|---|---|
| 0x101 | FUN_00747710 | FUN_0074d450 | neutral movement (common state 0 `case 9`) |
| 0x102–0x105 | 747800 / 747830 / 747810 / 747820 (return 1) | 74d700 / 74e060 / 74db00 / 74dda0 | stubs: draws for common states 2 / 6 / 1,5 / 3,4 |
| 0x106–0x10a, 0x112 | — | — | unused ids (0x106–0x10a are sprite ids) |
| 0x10b | FUN_0074b1c0 | FUN_00750fa0 | **unreachable** (§5.8) |
| 0x10c | FUN_00748870 | FUN_0074f180 | D+A |
| 0x10d | FUN_00747840 | FUN_0074e360 | B tap |
| 0x10e | FUN_00747a80 | FUN_0074e620 | B+H |
| 0x10f | FUN_00747d80 | FUN_0074e8e0 | B press / hold |
| 0x110 | FUN_00748140 | FUN_0074ebc0 | S+B |
| 0x111 | FUN_007484a0 | FUN_0074eea0 | D+H |
| 0x113/0x114/0x115 | 748ed0 / 749210 / 749510 | 74f460 / 74f720 / 74f9e0 | A chain 1/2/3 |
| 0x116/0x117/0x118 | 7497d0 / 749b10 / 749e10 | 74fca0 / 74ff60 / 750220 | S+A chain 1/2/3 |
| 0x119 | FUN_0074a0d0 | FUN_007504e0 | S+H lunge |
| 0x11a | FUN_0074a5b0 | FUN_00750780 | S+H hold (after grab) |
| 0x11b | FUN_0074a900 | FUN_00750a20 | H charge |
| 0x11c | FUN_0074b160 | FUN_00750d00 | H wall-crash recovery |
| 0x11d | FUN_0074b5c0 | FUN_00751240 | Accel |

Spawn shorthands (all type 9): enemy `E(id) = (*DAT_0088157c = FUN_0054afe0)(id, p+4, side, slot)`
then the caller writes fields of the returned element (`FUN_004ef290(idx)`); missile
`M(id,&pos,ang) = (*DAT_008819dc = FUN_00679f00)(id, &pos, ang, p+4, 0, side, slot)`; hitbox
`H(id,&pos,ang) = (*DAT_00881bbc = FUN_005eb120)(id, &pos, ang, p+4, 0, side)`; laser
`L(id,&pos,ang,owner) = (*DAT_00881c5c = FUN_006261c0)(id, &pos, ang, ownerId, side, slot)`;
SB effect `X(id,col,&pos,ang,...) = (*DAT_008816bc = FUN_005094a0)`; generic effect
`G(...) = (*DAT_00881698)`. "aim" = `+0x84 = FUN_007804b0(p)`. "face-aim" = `+0x84 = +0x88 =
(opp.x <= p.x) ? π : 0` (used by 0x10f, 0x111). `dir(a)` = `(cos a, sin a)`.

### 0x101 — neutral movement (T2 FUN_00747710)
Identical to Suguri 0x128: each held direction moves 5.0 (stats+0) on that axis (not
normalised); `+0x84 = facing==1 ? 0 : π`. Returns 1.

### 0x113 — A chain 1 "Burn in Crimson Flames" (T2 FUN_00748ed0) — A
| t | Action |
|---|---|
| 0 | aim; lunge `+0x88 = +0x94 = aim`, `+0x98 = min(dist/12 (DAT_00813f54), 14 (DAT_00813a54))`; flame visual enemies **E(700)** with `+0x5c = aim + 0.5 (DAT_00853b1c)` and **E(0x2bd)** with `+0x5c = aim − 0.5`; SE 0x26; `FUN_004d7ff0(p+4)` (clear own afterimages) |
| 10, 13 | if `FUN_007807f0(p,0x113)`: hitbox **H(200, pos, aim)** |
| 16 | if `FUN_007807f0(p,0x113)`: hitbox **H(0xc9, pos, aim)** |
| ≥10 | flag158 == 1 && A pressed (`+0x38==3`) → state **0x114** (t=0, flag=0) — checked before dash-cancel |
| ≥25 | dash-cancel (612900 at 25) |
| >50 | state 0 |
No friction change; the lunge decays 0.5/frame (main loop).

### 0x114 — A chain 2 (T2 FUN_00749210)
| t | Action |
|---|---|
| 0 | aim; lunge `min(dist/15 (DAT_00853b88), 8 (DAT_008140d4))`; visual **E(0x2c6)** `+0x5c = aim`; SE 0x26 |
| 5 | visual **E(0x2c7)** `+0x5c = aim` |
| 10 | cost(0x114) → **H(0xd2, pos, aim)** |
| 15 | cost(0x114) → **H(0xd3, pos, aim)** |
| ≥10 | flag158 && A pressed → **0x115** |
| ≥25 | dash-cancel; >50 state 0 |

### 0x115 — A chain 3 (T2 FUN_00749510)
| t | Action |
|---|---|
| 0 | aim; lunge `min(dist/15, 8)`; visual **E(0x2d0)** `+0x5c = aim + 0.75 (DAT_00813f3c)`; SE 0x26 |
| 5 | visual **E(0x2d1)** `+0x5c = aim − 0.75` |
| 10 | cost(0x115) → **H(0xdc, pos, aim)** |
| 15 | cost(0x115) → **H(0xdd, pos, aim)** (inflicts **burn**, §5.3) |
| ≥30 | dash-cancel (612900 at 30); >50 state 0 |

### 0x116 / 0x117 / 0x118 — S+A chain "Melt in the Azure Flame" (T2 FUN_007497d0 / 00749b10 / 00749e10)
Byte-for-byte the same timings, lunges, chain rules and cost entries (**0x113/0x114/0x115**) as
0x113/0x114/0x115, with these substitutions:
- visuals: 0x116 → E(800) / E(0x321); 0x117 → E(0x32a) / E(0x32b); 0x118 → E(0x334) / E(0x335) (blue);
- hitboxes: 0x116 → 300, 300, 0x12d; 0x117 → 0x136, 0x137; 0x118 → 0x140, 0x141;
- chains go 0x116 → 0x117 → 0x118 (A pressed + hit-confirm; S need not be held);
- 0x141 inflicts **hyper drain** (status 2) instead of burn.
Hitbox parameters are identical pairwise (§5.3). 0x116 also calls `FUN_004d7ff0(p+4)` at t=0.

### 0x10c — D+A "Fear the Approaching Sparks" (T2 FUN_00748870)
| t | Action |
|---|---|
| 0 | aim |
| 10 | SE 0x26 |
| even t in 10..22 (ver ≤ 0x5a0: 10..20) | if `FUN_007807f0(p,0x10c)` (A −0.09): re-aim; draw `r = rand_f(10, 70)` (DAT_00813f4c, DAT_008145ec) and `a = rand_f(−0.25, 0.25)` (DAT_0081411c, DAT_00813f24) — **always drawn, then overridden** for ver > 0x5a0 by the table below; spawn missile (below) |
| every frame | friction `+0x9c *= 0.5` (not on the dash-cancel frame) |
| ≥20 | dash-cancel (612900 at 20) |
| >35 | state 0 |

ver > 0x5a0 offset table (switch on t): 10: (r 10, a −0.25); 12: (20, −0.15 DAT_00815dc4);
14: (30, −0.05 DAT_00814114); 16: (40, 0.05 DAT_00813ef4); 18: (50, 0.15 DAT_00813f20);
20: (60 DAT_00813a58, 0.25); 22: (0, 0).
Spawn: t==22 → **M(0x12e)** at `pos + dir(aim + a)*r` (= pos), angle aim. Otherwise if
`(t−10) % 4 == 0` (t=10,14,18) → **M(300)** at `pos + dir(aim + π/2 + a)*r`, angle `aim + 0.3`
(DAT_00813f28); else (t=12,16,20) → **M(0x12d)** at `pos + dir(aim − π/2 + a)*r`, angle `aim − 0.3`.
(ver ≤ 0x5a0: angle offset = `rand_f(0.1, 0.65)` instead of 0.3, random r/a.)
7 missiles max, 0.63 A total.

### 0x10f — B press / hold "Shiver in the Invisible Murderous Intent" (T2 FUN_00747d80)
| t | Action |
|---|---|
| 0 | face-aim |
| t < 12 && B released (`+0x3c < 1`) | state = **0x10d** (timer kept), aim, run T2[0x10d] now (the rest of this frame is skipped) |
| 12 | if `FUN_007807f0(p,0x10f)` (B −0.15): mine **M(100, pos, 0)**; target point `T = opp.pos + (rand_f(−160,160), rand_f(−160,160))` (DAT_00817e0c, DAT_00814168; x then y), then ±240 (DAT_00815584) per held direction (y+ → +y, y− → −y, x− → −x, x+ → +x); missile `+0x68 (moveAngle) = rand_f(−π, π)`, `+0x70 (speed) = rand_f(0,10) + 10`, `+0x4c/+0x50/+0x54 = T`; whip ribbon `(*DAT_00881d9c)(4, slot, missile.serial(+0x20))`; SE `FUN_007bcaf0(0x2f,3)` |
| 16 && B held (`+0x3c > 1`) | **t := 10** (loop: next mine at t=12 → one mine every 6 frames while held) |
| else ≥25 | dash-cancel (612900 at 25) |
| >35 | state 0 |
RNG order per mine: x, y, angle, speed. Note: B released at t=10/11 of a loop iteration also
satisfies the `t<12` test → switches to 0x10d with t=10/11 and fires the tap volley at t=14
(if 0.4 B ammo is available).

### 0x10d — B tap "Count Your Last Moments" (T2 FUN_00747840) — via 0x10f early release
| t | Action |
|---|---|
| 14 | if `FUN_007807f0(p,0x10d)` (B −0.4): two homing bombs **E(200)** at pos: `+0x58 = +0x5c = aim − 2.0` (DAT_00813a4c) and `aim + 2.0`, `+0x70 (speed) = 16` (DAT_008140d8); SE 0x22 |
| ≥20 | dash-cancel; >35 state 0 |

### 0x110 — S+B "Be Charred in the Burning Death Crisis" (T2 FUN_00748140)
| t | Action |
|---|---|
| 0 | aim |
| 10 | if `FUN_007807f0(p,0x110)` (B −0.35): two laser bits **E(100)**: bit 1 at `pos + dir(aim + π/2)*100` (DAT_00814104, DAT_00853b90), `+0x58 = aim + 0.25`, `+0x70 (turn rate) = −0.009` (DAT_00825fc8); bit 2 at `pos + dir(aim − π/2)*100`, `+0x58 = aim − 0.25`, `+0x70 = +0.009` (DAT_00825fc0). (`+0x5c` not written.) SE 0x2f |
| ≥25 | dash-cancel; >40 state 0 |

### 0x10e — B+H "Take Your Dying Breath" (T2 FUN_00747a80)
| t | Action |
|---|---|
| 0 | aim; `FUN_006b1b40(p,10)`; flourish incl. `FUN_007bec20(100,pos,0,0)` |
| t = 10,13,…,40 (`9<t<41`, `(t−10)%3==0` → **11 bombs**) | **E(200)** with `+0x7c = 0xfa` (variant "boomerang bomb"), pos = player pos, speed `+0x70 = 20` (DAT_008140fc); `+0x58 = (t−10)%6==0 ? aim − π + (t−10)*0.05 : aim + π − (t−10)*0.05` (DAT_00813ef4), wrapped (FUN_005f0c60); `+0x5c = +0x58`; SE `FUN_007bcaf0(0x22,6)` |
| ≥50 | dash-cancel; >80 state 0 |

### 0x111 — D+H "Suffer from the Inevitable Death Crisis" (T2 FUN_007484a0)
| t | Action |
|---|---|
| 0 | face-aim; `FUN_006b1b40(p,10)`; flourish incl. cut-in |
| 5 | for i = −4..3: **E(100)** with `+0x7c = 0x6e` (orbiting bit); even i: `+0x70 = 0.0065` (DAT_00825fbc), `+0x58 = (i/4)π − 0.2` (DAT_00853b68, DAT_00853b14); odd i: `+0x70 = −0.0065` (DAT_00825fc4), `+0x58 = (i/4)π + 0.2`; pos = `p.pos + dir(+0x58)*100`; orbit centre `+0xbc/+0xc0/+0xc4 = p.pos`. Then SE 0x2f |
| ≥40 | dash-cancel; >60 state 0 |
(Angles are absolute, independent of the opponent.)

### 0x119 — S+H lunge "Accept Your Doom" (T2 FUN_0074a0d0)
| t | Action |
|---|---|
| 0 | aim; `FUN_006b1b40(p,10)`; **`+0xf8 = 200`**; SE 6; `FUN_004e9280(100,0.75)`; `FUN_004d7ac0(20,0.5,2)`; effect 0xfa |
| 10 | re-aim; `+0x98 = 15` (DAT_00853b88), `+0x94 = aim`; SE 0x20 |
| 10..20 | if `dist < 70` (DAT_008145ec) && `FUN_00780a50(opp)` → **grab** (below), return |
| every frame | dash sparks if speed ≥ 3 (§1) |
| ≥40 | dash-cancel (also `+0xf8 /= 2`) |
| >60 | `+0xf8 /= 2`; state 0 |

Grab (same frame): opponent `state = 1, t = 0`, `opp.pos = p.pos + dir(aim)*60` (DAT_00813a58),
`opp+0x98 = 0`, `opp+0x84 = wrap(aim + π)`; `FUN_004d7ff0(opp+4)`; then
`state = 0x11a, t = 0, flag = 0`; `+0x98 *= 0.25` (DAT_00813f24);
**`+0x84 = atan2(−y, −x)`** (toward arena centre, FUN_00474240 with sign-flipped args);
run T2[0x11a] (t=0); then camera focus `FUN_005f8fc0(80, p+4, pos, 0.4)`, `FUN_004e9280(100,0.75)`,
`FUN_004d7ac0(20,0.5,2)`, `FUN_007bec20(100,pos,0,0)`, effect 0xfa. `+0xf8` is **not** halved.
(No `FUN_007807f0` — the `FUN_00612830` call is training display only.)

### 0x11a — S+H hold (T2 FUN_0074a5b0)
| t | Action |
|---|---|
| 0..80 | if `FUN_00780a50(opp)`: opp `state = 1, t = 0`, `opp.pos = p.pos + dir(+0x84)*60`, `opp+0x98 = 0`, `opp+0x84 = wrap(+0x84 + π)` (victim pinned between SB and the arena centre) |
| 0,5,…,30 (**7 missiles**) | `a = rand_f(−π,π)`, `d = rand_f(40,60)` (DAT_008140e0, DAT_00813a58): **M(0x96)** at `opp.pos + dir(a)*d`, angle 0; SE `FUN_007bcaf0(0x2f,3)` |
| 80 | generic effect 0xa8c at pos; `FUN_006b1d70(p, 120 (DAT_00814174), 18 (DAT_00814144))` — knocks the opponent away (state 1, speed 18, no damage; objects.md §4.6) |
| ≥110 | dash-cancel; >130 state 0 |
Damage comes from the missiles' explosions (§5.2 0x96 → §5.6 effect 0x96 → hitbox 0x96, 120 each).

### 0x11b — H "Let the Sea of Flame Embrace You" (T2 FUN_0074a900)
| t | Action |
|---|---|
| 0 | aim; `FUN_006b1b40(p,10)`; **`+0xf8 = 200`**; SE 6; `FUN_004e9280(100,0.75)`; `FUN_004d7ac0(20,0.5,2)`; SE 0x26; effect 0xfa |
| 10 | re-aim; `+0x90 = 0`; `FUN_004d7ac0(100, 0.5, 2)` |
| t > 9 | `+0x90 += 0.5`; `pos += dir(+0x84) * +0x90` (direct position write, before the main-loop motion) |
| t%3==0 | SB effect **X(600, 0, pos, rand_f(−π,π))** |
| t%10==1 | flame visual E(slot==0 ? 700 : 800), `+0x5c = rand_f(−π,π)` |
| t%10==6 | flame visual E(slot==0 ? 0x2bd : 0x321), `+0x5c = rand_f(−π,π)` |
| t > 9 && dist < 120 (DAT_00814174) && `FUN_00780a50(opp)` | carry: opp `state=1, t=0`, `opp.pos = p.pos + dir(+0x84)*40` (DAT_008140e0), speed 0, `+0x84 = wrap(aim+π)`; `FUN_004d7ff0(opp+4)`; if the camera-focus vector (0x895144) is empty: `FUN_005f8fc0(80, p+4, pos, 0.4)` |
| t ≥ 20 && pos outside the arena (`!FUN_0061ac10(pos, 0)`, radius 540) | **wall crash**: generic effect 0xa8c (colour 0xd); SE 0x1b; `FUN_004d7ac0(20, 0.75, 1)`; 17 blast effects: for i = −8..8: `ang = (i/12)π`, `v = (−cos(ang)*60, sin(ang)*160)` rotated by `b = aim − π`, plus `dir(b)*30 + pos` → generic effect 500 (colour 0xd, angle 0, age0 = `rand_i(−2,2) − 2`); hitbox **H(500, pos, aim)** with damage overwritten to **`(t − 20)*20 + 400`**; state **0x11c** (t=0), run it |
| (inside arena) ≥110 | dash-cancel; >130 state 0 |
Speed after n charge frames = 0.5n; distance = 0.25 n(n+1). A straight charge from the centre
reaches the wall (540) at n ≈ 46 (t ≈ 55 → 1100 damage). The order inside a frame is: move,
effects, carry, wall test.

### 0x11c — H recovery (T2 FUN_0074b160)
Dash-cancel ≥30 (612900 at 30); >50 state 0. Nothing else.

### 0x11d — Accel "Witness the Final Moment of This Planet" (T2 FUN_0074b5c0)
| t | Action |
|---|---|
| 0 | aim; `+0x88 = +0x94 = aim`; speed 0; `FUN_006b1b40(p,0x14)`; SE 7; `FUN_004e9280(150, 0.85)` (DAT_00814120); `FUN_007bec20(120, pos, aim, π/4)` (DAT_0081fa90); `FUN_004d7ac0(150, 0.75, 2)`; effect 0xfa; cannon **E(900)** at `pos − dir(aim)*200` (DAT_008147cc), `+0x58 = +0x5c = aim`; SE 0x33 |
| 20..70 | if a direction is held (input angle ≤ 2π): `FUN_005f0e70(&+0x84, inputAngle, 0.01)` (DAT_00813ec8) — steer the beam |
| t < 80 | hurtbox `+0x5c..+0x68 = 0`, `+0xa2 = 0` (untouchable) |
| ≥80 | dash-cancel (612900 at 80); >120 state 0 |
The beam itself is laser 300 owned by the cannon (§5.5).

### 0x102–0x105 — stubs
Return 1; only their T1 draws are used (§4.3).

---

## 4. Animation

### 4.1 Sprite registration (loader `FUN_00743b30`)
Arg = colour variant: 0 → folder text 0x23, 1 → 0x24, 2 → 0x25 (charaStarA/B/C); any other value
→ 0x23. Mirror fallback: if both players are char 9 and `FUN_007a2880(0)/(1)` are (2,0) or (0,2)
→ 0x24 (UNVERIFIED what FUN_007a2880 returns — colour choice per player). Quad size S×S, frame n =
`printf(pattern, n)` (as Suguri).

| Sprite id | Pattern | Frames | S | Used by |
|---|---|---|---|---|
| 0x101 | `sb_std0%d` | 3 | 96 (DAT_00814118) | neutral |
| 0x106 / 0x107 | `sb_fwd0%d` / `sb_bck0%d` | 3 / 3 | 96 | neutral moving toward / away |
| 0x103 | `sb_los0%d` | 6 | 96 | T1 0x103, 0x104 |
| 0x105 | `sb_sld0%d` | 4 | 96 | T1 0x105, 0x10b, 0x10d, 0x10e |
| 0x102 | `sb_dsh0%d` | 6 | 96 | dash |
| 0x108 | `sb_rng0%d` | 2 | 512 (DAT_008178b0) | stats +0x3c (HUD ring, UNVERIFIED) |
| 0x10a | `sb_system` | 1 | 512 | stats +0x44 |
| 0x10d | `sb_atka0%d` | 5 | 96 | 0x10c, 0x10f, 0x110, 0x111, 0x11d |
| 0x113 | `sb_atkb0%d` | 8 | 96 | 0x113–0x118, 0x11b (t<10) |
| 0x119 | `sb_catch0%d` | 1 | 96 | 0x119, 0x11a, 0x11b (t≥10), 0x11c |
| 0x11e | `sb_bit0%d` | 6 | 72 (DAT_008178ac) | laser bits (enemy kind 100) |
| 0x11f | `sb_bomb0%d`/`sb_bomb%d` | 18 | 64 (DAT_008140f8) | mines (missile 100/0x96) |
| 0x120 | `sb_missile0%d`/`%d` | 12 | 80 (DAT_00814164) | bombs (enemy 200/0xc9), D+A missiles |
| 0x121 / 0x122 | `sb_flamer%d` / `sb_flameb%d` | 6 / 6 | 64 | red / blue flame effects |
| 0x123 | `sb_laser` | 1 | 256 (DAT_00814160) | laser atlas (laser draw uses sub-rects) |
| 0x124 | `sb_laser%d` | 3 | 256 | no SB draw function references it (the only 0x124 immediate in FUN_00509ac0 is an `imul` stride) — loaded but unused |
| 0x125 | `sb_field`, `sb_field1` | 2 | 64 | effects 600, 700, 0x2c6 |
| 0x11d | `sb_starb0%d` | 3 | 320 (DAT_008178c0) | Accel cannon body |
| 0x126 | `sb_starb03` | 1 | 256 | Accel cannon glow |
| 0x127 | `sb_impct0%d` | 2 | 128 (DAT_00814134) | effect 0x352 |
| whip slot 4 | `sb_whip0` | 1 | — | whip ribbon texture (`0x87ff38 + 4*4 + variant*0x3c`) |
All 103 `sb_*.png` of `extracted/texture/charaStarA/` are covered (3 `res_*` files are UI).
`FUN_00746d90` builds the pause-menu command list (text ids 0x356..) — no gameplay.

### 4.2 Draw transforms
- **AIM transform** for every state except 0x101 — same as suguri.md §4.2 (rotZ(aim); if
  `|aim| ≥ π/2` rotX(180°); rotY(180°)). Normal alpha blend (no blend call), global tint
  `FUN_004e8f50` first.
- 0x101: no rotZ, rotY(180°) if facing != 0.
- Afterimage `FUN_004d7e00` queued **only in 0x101 and 0x102**.

### 4.3 Common states → SB draw handlers (FUN_006b07a0 … FUN_006b1580, `case 9`)
| Common state | SB T1 |
|---|---|
| 0 | 0x101 FUN_0074d450 |
| 1, 5 | 0x104 FUN_0074db00 |
| 2 | 0x102 FUN_0074d700 |
| 3, 4 | 0x105 FUN_0074dda0 |
| 6 | 0x103 FUN_0074e060 |

### 4.4 Per-state frames (`pp(n)` = `(t/n)&3`, 3→1; `cyc4` = `f=t/4; if f>1: f=((f−2)&3)+2; if f==5: f=3` → 0,0,0,0,1,1,1,1,2,2,2,2,3,…,4,…,3,…,2,… i.e. holds 2,3,4,3 cycling)
| State | Sprite | Frame |
|---|---|---|
| 0x101 | 0x101; overrides (later wins): y+→0x106; y−→0x107; facing0&x−→0x106; facing1&x−→0x107; facing0&x+→0x107; facing1&x+→0x106 | pp(5) |
| 0x102 | 0x102 | pp(3), +3 when moving against facing (facing1 && \|aim\|>π/2, or facing0 && \|aim\|<π/2) |
| 0x103 | 0x103 | pp(3) + (3 if global frame DAT_008858f8 even); **not drawn at t ≥ 100** |
| 0x104 | 0x103 | 0 |
| 0x105, 0x10d, 0x10e | 0x105 | min(t/5, 3) |
| 0x10b | 0x105 | 0 |
| 0x10c, 0x10f, 0x110, 0x111 | 0x10d | cyc4 |
| 0x113–0x118 | 0x113 | min(t/4, 7) |
| 0x119, 0x11a, 0x11c | 0x119 | 0 |
| 0x11b | t<10: 0x113 frame min(t/3,7); else 0x119 frame 0 | |
| 0x11d | 0x10d | cyc4; colour `(a=1, r=g=b=0)` (black, see §0.1); scale: t<20: `(1 − t/20, t/10, 1)`; 20 ≤ t ≤ 80: **not drawn**; 81..100: `((t−100)/20, 2 − (t−100)/20, 1)` (negative x-scale — reproduce); t>100: (1,1,1) |

---

## 5. Spawned objects (parameters)

### 5.1 Enemy family (type 9): spawn FUN_0054afe0, update FUN_0054b870, draw FUN_0054e4c0, onKilled FUN_0054e170
Layout objects.md §2.6. SB uses `+0x10` = **kind** (draw and onKilled switch on it), `+0x7c` =
**variant** (update switches on it), `+0x84` = phase, `+0x70` = speed / turn rate, `+0x74` = spin.
`+0x78` age and `+0x80` timer2 are incremented by the manager after each update (objects.md).
Spawn templates (struct = `local_*` base, offsets computed from it):

| Spawn id | kind +0x10 | variant +0x7c | hp/maxHp | active +0x2c | shape r | scale (x,y,z) | phase +0x84 |
|---|---|---|---|---|---|---|---|
| 100 | 100 | 100 | 300 | 0 | 30 (DAT_008140e8) | (0, 4.0 DAT_00853b68, 1.0) | 0 |
| 200 | 200 | 200 | 200 | 0 | 30 | (3.0 DAT_00813a50, 0, 1.0) | 0 |
| 700, 0x2bd, 0x2c6, 0x2c7, 0x2d0, 0x2d1 | 700 | = spawn id | 999 | 0 | 0 | (0,0,0) | 0 |
| 800, 0x321, 0x32a, 0x32b, 0x334, 0x335 | 800 | = spawn id | 999 | 0 | 0 | (0,0,0) | 0 |
| 900 | 900 | 900 | 999 | 0 | 0 | (0,0,0) | 0 |
| 500 / 600 | 500 / 600 | 500 | 9999 | 0 | 0 | (0,0,0) | 10 | (unreachable, §5.8) |
`mirrorChildren +0x6c = 0`, `motionId +0x88 = 0` for all. Callers override `+0x7c` (0x6e, 0xfa).

**Variant 100 — S+B laser bit** (kind 100):
- phase 0 (opening): `sx += 0.1, sy −= 0.3 (DAT_00813f28), sz += 0.1`; when `sx ≥ 1.0`: sx=sy=sz=1,
  `active = 1` (now shootable, hp 300), phase 10, timer2 = 0, spawn **laser 100** (sight line) at its
  pos/`+0x58`, owner = this enemy's id (`+0`).
- phase 10: `+0x58 += +0x70` (sweep); timer2 == 19 → **laser 0x6e** + SE 0x11; timer2 ≥ 101 →
  `active = 0`, phase 0x14, timer2 = 0. Position does not change.
- phase 0x14 (closing): `sx −= 0.1, sy += 0.3`; removed when `sx ≤ 0`.
**Variant 0x6e — D+H orbiting bit** (kind 100): as variant 100 but opening `sy −= 0.2`;
in phase 10 `+0x58 += +0x70` and `pos = (+0xbc,+0xc0) + dir(+0x58)*100` (orbit around the stored
centre); timer2 == 19 → **laser 0x78** + SE 0x11; timer2 ≥ 181 → closing. (Calls
`FUN_007805f0(owner)` but ignores the result.)
Floating-point note: 0.1f added ten times reaches ≥1.0 on the 10th update (UNVERIFIED that x87
precision does not change this — settle by stepping in a debugger).

**Variants 200 (B tap bomb) / 0xfa (B+H boomerang bomb)** (kind 200), per frame:
- `sx/sy` "pop-in": if `sy ≥ 1`: sx = sy = 1, else `sx −= 0.2, sy += 0.1` (from (3,0): 10 frames).
- steering of `+0x5c` with `FUN_005f0e70`, then `+0x58 = +0x5c`:

| age | 200 | 0xfa |
|---|---|---|
| < 20 | toward opponent, step `speed*0.01`; `speed *= 0.9` (DAT_00814178) | same |
| 20..99 | toward opponent, step 0.025 (DAT_00813edc); `speed += 0.35` while `speed < 16`; ages 20..24: sparks (`rand_f(−0.2,0.2)`, `rand_i(0,100)%8` → G(0xa28+n) at pos, angle r+`+0x58`) | toward **its own player** (`FUN_00780450(side)`), step 0.025, no speed change |
| 100..149 | as 20..99 | toward opponent, step 0.01, `speed += 0.15` while `< 16` (spark code is unreachable for 0xfa) |
| 150..249 | straight | as 100..149 |
| ≥ 250 | straight | straight |
- trail: G(0x802) at pos, angle `+0x5c`, when age is even or speed > 12.
- move `pos += dir(+0x5c)*speed`.
- **attach to a player**: for each player with `+4 != owner` and `+0x14 != side`: if
  `FUN_005f1c90(pos, p.pos, shape, p.hurtbox, +0x58, p+0x84)` → `+0xbc = +0xc0 = 0`,
  `+0x5c = atan2(p − pos)`, **`+0x08 (owner) = victim id`**, kind = 0xc9, variant = 0xc9,
  timer2 = 0, scale (1,1); stop. (No hittable/dash/shield test.)
- **attach to an enemy object**: for each Enemy with a different `+0` id and different side,
  overlapping → same but owner = that enemy's id and variant **0xca**.
- **miss**: if age ≥ 21 (200) / ≥ 151 (0xfa) and outside the arena (radius 540): kind 0xc9,
  variant **0xcb**, timer2 = 0, scale (1,1).
Note `active` stays 0: bombs cannot be shot.

**Variant 0xc9 (bomb stuck on a player)**: victim = `FUN_007805f0(+0x08)`; removed silently if
none or victim in state 6; if victim `+0xf8 != 0` → timer2 = 100 (instant detonation);
`pos = victim.pos − dir(+0x5c)*30`; timer2 == 100 → onKilled (explode), removed.
**0xca (stuck on an enemy)**: follows `FUN_0055f060(+0x08).pos − dir(+0x5c)*30`; removed silently
when that enemy is gone; timer2 == 100 → explode. **0xcb (missed)**: stationary; timer2 == 100 →
explode.
**onKilled kind 200/0xc9** (FUN_0054e170): X(300) at pos, angle `+0x58+π`; G(0xaf0); hitbox
**H(0xb4, pos, 0)** with owner = `FUN_00780450(side)+4` (the SB player); SE 0x14.
**onKilled kind 100** (bit shot down): G(600) at pos; 5× G(0x834) at pos + (rand_f(−50,50),
rand_f(−50,50)) (DAT_008149e4, DAT_008140ec); 5× G(500) at pos + (rand_f(−30,30), rand_f(−30,30))
with age0 = −10 − 3i; SE 0x1a. (Its lasers die on their next update because the owner lookup fails.)

**Flame visuals 700-group / 800-group** (A / S+A chains, H): invisible carriers. Each frame:
`a = timer2/20 * 2π` (DAT_008140fc); if `|owner.aim| > π/2`: `a = 2π − a`. Local offset:
700/800: `(cos a * −80, sin a * 36)`; 0x2bd/0x321: `(cos a * 80, sin a * −36)`;
0x2c6/0x32a: `(cos a * −100, sin a * −20)`; 0x2c7/0x32b: `(cos a * 60, sin a * 90)`;
0x2d0/0x334: R = (2·timer2+80, 2·timer2+60): if `|owner.aim| ≤ π/2`: `(cos a * −Rx, sin a * Ry)` else
`(cos a * Rx, sin a * Ry)`; 0x2d1/0x335: `≤ π/2`: `(cos a * Rx, sin a * −Ry)` else
`(cos a * −Rx, sin a * −Ry)`. `pos = owner.pos + rotate(offset, +0x5c)`. If timer2 > 1:
`+0x58 = atan2(pos − prevPos(+0x4c))`; SB effects at pos **and** at `(pos + prevPos)/2`:
700-group X(500) + X(0x1f5); 800-group X(0x1fe) + X(0x1ff) (4 spawns → 4 RNG draws, §5.6).
Removed when timer2 > 20.

**Variant 900 — Accel cannon**: phase 0: `sx,sy,sz += 0.05`; when `sx ≥ 1`: all 1, phase 100,
timer2 = 0, **laser 300** at pos, angle `+0x5c`, SE 0x12. phase 100: timer2 > 120 → phase 900.
phase 900: `sx += 0.1, sy += 0.1, sz −= 0.1`; removed at `sz ≤ 0`. Every frame, if the owner's
state is 0x11d: `pos = owner.pos − dir(owner.aim)*200`, `+0x58 = +0x5c = owner.aim`.

### 5.2 Missile family (type 9): spawn FUN_00679f00, update FUN_0067b9b0, draw FUN_0067a2e0, onHit FUN_0067b5e0, onKilled FUN_0067b970
Template FUN_00685bb0 (hitType 2, objects.md §2.3). Per id:

| id | Used by | speed +0x70 | +0x74 | hp | active | body r | proj r | dmg | knock | growScale +0x5c / +0x60 |
|---|---|---|---|---|---|---|---|---|---|---|
| 100 mine | B hold | caller: 10..20 | — | 180 | 0 (1 at age 100) | 40 | 40 | **0** | 10 | 0 / 1.0 |
| 0x96 | S+H | 0 | — | 180 | 0 | 40 | 40 | 0 | 10 | 1.5 (DAT_00814110) / 0 |
| 300, 0x12d, 0x12e | D+A (**id stored as 300 for all three**) | 10 | 300: −0.01 (DAT_008178dc); 0x12d: +0.01; 0x12e: 0 | 60 | 1 | 16 | **0** | 200 | 10 | 0 / 1.0 |
| 200 | — (never spawned) | 16 | — | 60 | 0 | 16 | 16 | 0 | 10 | 0 / 1.0 |

**Mine (100)**: each frame `s = sin((age/300)·π/2)` (DAT_008145f0, DAT_00813a4c) stored in
`+0x5c`; `pos += dir(+0x68)*speed`; `+0x6c += 0.1` (wrap > π); age == 100 → `active = 1`; then
`pos = T + (1 − s²)*(pos − T)` with `T = +0x4c/+0x50` (glides to the target point); age ≥ 250 →
onHit (explode) and removed. Once active: touching the opponent calls Damage(p, 0, 2, 1.0) — no
HP loss but a full stun point, so knockback 10 along `+0x68` — then onHit explodes it (objects.md
§4.4 missile pass); bullets can shoot it (hp 180) → onKilled = explode.
**0x96**: `+0x6c += 0.1`; `+0x60 += 0.1` (cap 1); age ≥ 5 (ver ≤ 0x5a0: 30) → onHit, removed.
**300 family**: `pos += dir(+0x68)*10`; `+0x68 += +0x74` (curve); age > 20 && outside arena →
onHit, removed. Trail every frame: if `DAT_00885a00 + side*0x10 == 0` X(500)+X(0x1f5) at pos,
angle `+0x68`, else X(0x1fe)+X(0x1ff) (per-side colour flag; 2 RNG draws). proj r = 0 → cannot be
shot/cancelled (UNVERIFIED that an all-zero shape never overlaps — objects.md §4.1).
**onHit** (FUN_0067b5e0, colour `DAT_008859fc+side*0x10`): 100 → X(100) at pos (explosion,
spawns hitboxes), SE `FUN_007bcaf0(0x1b,6)`, `FUN_004e9280(40, 0.75)`; 0x96 → X(0x96), SE 0x1b
(lock 6), `FUN_004d7ac0(10,0.75,1)`, `FUN_004e9280(40,0.75)`; 300 → G(0x515) at pos, SE
`FUN_007bcaf0(0x1d,3)`, **X(700)** (fire zone) at pos, angle `+0x6c`. All return false (erase).
**onKilled**: 100, 200 → same as onHit; others → nothing (erased).
Whip ribbon (whip family type 9, FUN_007c81e0/7c8660/7c82d0, id 4): 20-node ribbon following
mine 100 (found by serial, `FUN_00685ce0`), width 30·growScale — visual only, no RNG.

### 5.3 Hitbox family (type 9): spawn FUN_005eb120, onHit FUN_005eb6c0
Template: dealsDamage 1, oncePerFrame 0, stun 1.0, rehit 0 (objects.md §2.4).

| id | Spawned by | hitType | dmg | shape {hw,hh,r} | knock | onHit |
|---|---|---|---|---|---|---|
| 200, 0xd2, 0xdc | A chain hits 1-2 / 2-1 / 3-1 | 3 | 60 | {0,0,120} (DAT_00814174) | 0 | streaks; SE 0x18 (lock 3); 200, 0xd2: **hit-confirm** (0xdc: none) |
| 0xc9, 0xd3 | A chain 1-3 / 2-2 | 3 | 60 | {0,0,150} (DAT_0081414c) | 10 | streaks + G(0xaf0); SE 0x18; **hit-confirm** |
| 0xdd | A chain 3-2 | 3 | 80 | {0,0,170} (DAT_00819990) | 20 | G(0xa8c) + G(0xaf0); SE 0x19 (lock 3); shake(10,0.75,1); **`FUN_006b1a40(victim, 1, 300)` burn** |
| 300, 0x136, 0x140 | S+A chain (as 200/0xd2/0xdc) | 3 | 60 | {0,0,120} | 0 | as 200 (colour 0x8859f8); 300, 0x136: hit-confirm; 0x140: none |
| 0x12d, 0x137 | S+A (as 0xc9/0xd3) | 3 | 60 | {0,0,150} | 10 | as 0xc9; hit-confirm |
| 0x141 | S+A finisher | 3 | 80 | {0,0,170} | 20 | as 0xdd but **`FUN_006b1a40(victim, 2, 300)` hyper drain** |
| 100 | mine explosion (effect 100, ×3) | 3 | 150 | {0,0,100} (DAT_00853b90) | 5 | **sets `hb.angle = atan2(victim − hb.pos)`** (radial knockback); 4 streaks; G(0x515) |
| 0x96 | S+H missile explosion (effect 0x96) | 3 | 120 | {0,0,100} | 0 | none |
| 0xb4 | bomb explosion | **2** | 300 | {0,0,80} (DAT_00814164) | 0 | none |
| 400 | bit laser tick | **1** | 60 | {800, 16, 0} (DAT_008149c8, DAT_008140d8) | 0 | if dmg != 0: streaks; SE 0x13 (lock 3) |
| 500 | H wall crash | **2** | 900 → overwritten `(t−20)*20+400` | {0,0,160} (DAT_00814168) | 0 | SE 0x19 |
| 600 | Accel beam tick | 3 | **200** (ver ≤ 0x5a0: 110) | {800, 160, 0} | 2.5 (DAT_008153e0) | 5 streaks; SE 0x13 (lock 4) |
"hit-confirm" = `FUN_00780450(hb.side)+0x158 = 1`. Hitbox angle = spawn angle (aim) unless
noted; the knockback direction is that angle (objects.md §4.4). hitType 1 (400) is dodged by
dashing; 2/3 are not.
RNG in callbacks (cosmetic but shared engine), in order: "streaks" = `base = rand_f(−π,π)`, then
for k = −2..1: `rand_f(−0.25,0.25)` → G(0x456) at victim pos, angle `(k/2)π + r + base`
(5 draws); hitbox 100 has no `base` draw (4 draws, angle `(k/2)π + r + hb.angle`); 0xdd/0x141
draw one `rand_f(−π,π)` (angle of G(0xa8c)); 600: k = −2..2, `rand_f(−0.25,0.25)`, angle
`(k/5)·2π + r` (5 draws).

### 5.4 Laser family (type 9): spawn FUN_006261c0, update FUN_00627a30, draw FUN_006264f0, onHit FUN_00627a10 (empty)
Template FUN_006307d0 (age pre-incremented → first update sees 1). SB lasers deal **no segment
damage** — every segment shape is {0,0,0}; damage is done by hitboxes they spawn. Spawn values:
100: blockMode 0, hitType 0; 0x6e/0x78: blockMode 1, hitType 1; 200/300: blockMode 1,
hitType 3, knock 5; damage 0 and stun byte 0/1 as written (irrelevant without overlap).

| id | Owner | Lifetime | Geometry each frame | Damage |
|---|---|---|---|---|
| 100 sight line | bit (enemy) | removed when age > 19 or owner enemy gone | angle = owner `+0x58`; origin = owner.pos + dir*60; 40 segments at origin + dir·i·50, i=0..39 | none |
| 0x6e S+B beam | bit | removed when age > 59 or owner gone | angle = owner `+0x58`; origin = owner.pos + dir*30; G(0x516) every 5th age; segments i=0..39 at origin + dir·i·50 **until the first i>2 outside arena+10** → there: X(200) wall flash (+ G(0x515) when age%3==0), stop; SE `FUN_007bcaf0(9,5)` | age < 50: hitbox **H(400)** at origin + dir·800, angle = laser angle; damage zeroed unless age%5==0 → ticks at ages 5,10,…,45 (**9 × 60**) |
| 0x78 D+H beam | orbiting bit | removed when age > 149 or owner gone | as 0x6e, plus X(200) at the origin every frame (+G(0x515) age%3==0) | age < 130: H(400), ticks at 5,10,…,125 (**25 × 60**) |
| 300 Accel beam | cannon | removed when age > 79 | if cannon exists: origin = cannon.pos + dir(cannon `+0x58`)*250 (DAT_008149ac), angle = cannon `+0x5c`; X(0x352) every 5th age ≤ 50; SE (9, lock 5) | ages 10,15,…,60 (**11 hits**): hitbox **H(600)** at origin + dir·800 (+ training display on the owner); ages 11..39, %3==0: 15 particles `FUN_007bd5e0(0x7da, …)` at origin + dir·100k + dir(rand_f(−π,π))·rand_f(10,160) (k=1..15; 2 RNG draws each) |
| 200 | (effect 800 — never spawned) | age > 79 | no geometry | ages 10..60 %5: H(600) |
Hitbox 400 is not deduplicated (oncePerFrame 0): two beams crossing the opponent both tick.

### 5.5 Accel summary (for porting)
SB t=0 spawns the cannon behind itself; the cannon pops in over 20 updates, then fires laser 300
(origin 50 px in front of SB: −200 + 250), which ticks 11 × 200 = 2200 max (hitType 3, knock
2.5, rect 1600×320 centred 800 px out). The aim can be steered at 0.01 rad/frame during t 20..70;
the cannon tracks SB's position and aim while SB is in 0x11d. SB is untouchable for t < 80.
Exact frame offset between SB t and laser age depends on same-tick update of newly spawned
enemies/lasers (UNVERIFIED — objects.md §1.3 frame order).

### 5.6 SB effect family (type 9): spawn FUN_005094a0, update FUN_0050bb10, draw FUN_00509ac0
Record 0x44: +0x04 id, +0x08 colour, +0x0c owner, +0x10 side, +0x14 flag (1), +0x18 pos,
+0x24 = 1, +0x28 age (= age0 arg), +0x2c scale, +0x30 alpha, +0x34 dir angle, +0x38 rotation,
+0x3c param. Spawn values (scale, alpha, rot, param; **R** = draws `rand_f(−π,π)` at spawn):
100/0x96/200: (1,1, angle, 0); 300: (2.0, 0, angle, 0); 500: (0.1, 1, R, 0), flag 0; 0x1f5:
(0.3, 1, R, 0); 0x1fe: (0.1, 1, R, 0), flag 0; 0x1ff: (0.3, 1, R, 0); 600: (1,1, angle, 0.05);
700: (1,1, angle, 0), flag 0; 0x2c6: (1,1, angle, 0), flag 0; 800: (0.1,1,R,0), flag 0;
0x352: (0, 1, angle, 0.2).

Update (return 0 = remove). Gameplay-relevant rows in bold:
| id | Update |
|---|---|
| **100** (mine explosion) | scale += 0.02 (+0.15 more while < 1); age > 5: alpha −= 0.02; rot = rand_f(−π,π) **every frame**; ages <15 with %3==0, or <30 with %5==0: particle `FUN_0068b8a0(1000, colour, pos, rand_f(−π,π))`; remove at alpha ≤ 0; **ages 2, 4, 6: hitbox H(100, pos, +0x34)** |
| **0x96** | same visuals; **age == 2: hitbox H(0x96, pos, +0x34)** |
| 200 (wall flash) | removed at age 2 |
| 300 (bomb burst) | age < 100: alpha += 0.1, scale −= 0.1; when scale ≤ 1: scale = alpha = 1, age := 100, flag 0. Then age ≥ 120: scale, alpha −= 0.05; remove at scale ≤ 0 |
| 500 / 0x1fe (flame puff) | age > 8: alpha −= 0.03, flag 0; scale += 0.01 × (age/10 + 1); age > 5: scale += rand_f(−0.05, 0.05); age > 20: scale −= rand_f(−0.01, 0.075); pos += dir(+0x34)*2; rot = rand_f(−π,π); remove at alpha ≤ 0 |
| 0x1f5 / 0x1ff (flame spark) | age > 8: alpha −= 0.04, flag 1; scale += 0.005 × (age/10+1); age > 5: + rand_f(−0.05,0.05); age > 20: − rand_f(−0.01,0.075); age > 10: rot = rand_f(−π,π); pos += dir*2 + (rand_f(−2,2), rand_f(−2,2)); remove at alpha ≤ 0 |
| 600 (H trail) | scale += param; param *= 0.95; age > 10: alpha −= 0.025 |
| **700 (fire zone)** | alpha += 0.05 (cap 1) while age ≤ 80, else −= 0.05; removed when age > 80 && alpha ≤ 0; **opponent (`FUN_00780480(side)`) with `+0xaa == 0` and distance < 50 (DAT_008140ec): heat `+0xe0 += 0.01`**; age%10==0: X(0x2c6, pos, rand_f(−π,π)); age%3==0: `a = rand_f(−π,π)`, `d = rand_f(5,50)` → G(3000 if `DAT_00885a00+side*0x10 == 0` else 0xbc2) at pos + dir(a)*d, angle π/2 |
| 0x2c6 | alpha −= 0.025, scale += 0.1 |
| 0x352 | age < 10: scale += param, alpha −= 0.02; else param *= 0.95, scale += param, alpha −= 0.025 |
| 800 | (never spawned) age ≥ 51: laser 200 toward the opponent ± rand_f(−0.25,0.25), removed |
Effects whose age0 is negative are delayed (generic effects; objects.md). Age increment rule:
assumed `++` after update like the other families (UNVERIFIED — objects.md §5 pending).
Draw sprites (FUN_00509ac0): 500/0x1f5 → 0x121; 0x1fe/0x1ff → 0x122; 600/700/0x2c6 → 0x125;
0x352 → 0x127; 100/0x96/200/300/800 use textured quads (atlas rects) — exact visuals UNVERIFIED.

### 5.7 Object visuals (cosmetic; no RNG in any draw)
- Enemy draw FUN_0054e4c0 (by kind): **100** (bits) sprite 0x11e: body frame `base` (base = 3 in
  phase 10 else 0), colour (1, s, s, s) with s = scaleZ, rot = `+0x58` (0 when not phase 10), scale
  (sx, sy); then additive (blend 2) frames base+1 and base+2 with alpha
  `(0.75 + 0.25·sin(2π(age%50)/50))·s` and `(0.65 + 0.35·sin(…))·s`. **200** (flying bombs) sprite
  0x120 frames `3k, 3k+1, 3k+2`, k = (age/3)%3, colour white if sy == 1 else black, same additive
  pair. **0xc9** (stuck/missed bombs) sprite 0x120 frames 9 / 10 / 11, scale × (1 + 0.2·sin pulse).
  **900** cannon: sprite 0x126 colour `(a=sz, r=0.25+0.25 sin(2π((g+250)%500)/500), g=0,
  b=0.25+0.25 sin(2π(g%500)/500))` (g = global frame DAT_008858f8), scale (sx·p, p, sy·p),
  p = 1 + 0.05·sin pulse, rotZ(`+0x58`+π/2), rotX(45°), vtable +0xc4 scroll `(g%200)/200`; then
  sprite 0x11d body at pos + dir(`+0x58`)·180·sx with normal + additive passes.
  Kinds 500/600/700/800 are not drawn.
- Missile draw FUN_0067a2e0 (ported exactly): only ids 100/0x96 (and the unused 200) are drawn —
  **the D+A missiles (300) draw nothing themselves** (their look is the flame-effect trail).
  Mine 100: `s = clamp(growScale·4, 0.5, 2)`, + `0.2·sin(2π(age%10)/10)` once age > 150; sprite
  0x11f frame `f = (age/3)%6` scale s, rotZ `+0x6c`, no colour call; then blend 2: frame f+6
  alpha `0.85 + 0.15·sin(2π(age%50)/50)`, frame f+12 alpha `0.75 + 0.25·sin(…)`, back to blend 1.
  0x96: same three passes with scale growScale, first pass colour `(a=+0x60, 1,1,1)`.
- Laser draw FUN_006264f0 (ported exactly): screen-space blits of sprite 0x123 (`sb_laser`, 512²)
  rotated by the laser angle, blend 2, ARGB per pass. 100: every segment src (0,256)-(63,319)
  half (100,10) a=0x20, then segments with `age%3 != i%4` src (256,256)-(319,319) half (60,2) a=0xff.
  0x6e/0x78: segment 0 src (448,256)-(511,319) half 48, others src (0,256)-(63,319) half (100,60)
  a=0x20; second pass segments ≥1 src (256,256)-(319,319) half (60,16) a=0xff; all half-heights ×
  `(60−age)/10` after age 50 (0x6e) / `(150−age)/20` after age 130 (0x78). 300: 20 blits at
  origin + dir·100i, cell `c = 3 − (age/2 + i)%4` src (64c,256)-(64c+63,319), half (400, 300) a=0x30
  and half (400, 200·(0.85 on even global frames)) a=0x40; heights × age/10 for age<10 and
  × (80−age)/30 after age 50.
- CModel rotation composition (settled from FUN_0046ce70): each Rotate pre-multiplies,
  `M = R·M`, with D3D row vectors → the **last call is applied first** to the vertex. So
  "rotZ(a), rotX(180°), rotY(180°)" = mirror x, flip y, then rotate by a (the 2D port's
  flipX + sy=−1 + rotZ), and the cannon's "rotZ(a+π/2), rotX(45°)" foreshortens local y by cos45°
  before the rotation.
- Whip ribbon FUN_007c82d0: triangle strip of the 20 nodes, texture `sb_whip0` (slot `+0x18`),
  u = k·12.8, v 0..256, vertex colour 0x80ffffff.

### 5.8 Unreachable / unused code (do not port unless a caller is found)
- State **0x10b** (T2 FUN_0074b1c0, T1 FUN_00750fa0): spawns enemy 500 at t = 15, 30, 45 with
  random angles and `+0x74 = 0.075` (×−1 at t%20==0). Nothing sets state 0x10b (grep of all.c
  for writes of 0x10b to +0x150; the only xref to FUN_0074b1c0 is the registry at 0x49de03).
  `FUN_0074b3c0` is an unregistered twin spawning enemy 600 (no references).
- Enemy 500/600 (orbiting flame wheels around the owner; 600 uses the smoke family
  `0x881788 = FUN_007bb450`, update `0x8817d8 = FUN_007bbb20`, draw `0x881828 = FUN_007bb5b0`,
  registered only for type 9) — only reachable from 0x10b.
- Beam family type 9 (`FUN_004b5130`, id 100: speed 12, dmg 120, …; update/draw/onHit stubs
  FUN_004b5280/4b5240/4b5260) — never spawned (no reference to DAT_0088189c besides the registry).
- Missile 200, SB effect 800 and laser 200 — no spawner.

---

## 6. Sound ids used (SE table all.c:464010, id = (addr − 0x9b99a0)/4)
| id | file | where |
|---|---|---|
| 6 | up01.wav | hyper start (0x10e, 0x111, 0x119, 0x11b) |
| 7 | high_a.wav | Accel start |
| 9 | power36.wav | lasers 0x6e/0x78/200/300 every frame (lock 5) |
| 0x11 | tm2_laser000.wav | bit beam fire (enemy timer2 19) |
| 0x12 | gun11_r.wav | Accel cannon fires |
| 0x13 | don16.wav | hitbox 400 (lock 3), 600 (lock 4) hits |
| 0x14 | bom06.wav | bomb explosion |
| 0x18 | hit.wav | chain hits (lock 3) |
| 0x19 | hit_f.wav | chain finishers (lock 3), wall crash hitbox |
| 0x1a | tm2_bom002.wav | bit destroyed |
| 0x1b | explosion1.wav | mine / 0x96 explosion (lock 6), H wall crash |
| 0x1d | gun12.wav | D+A missile impact (lock 3) |
| 0x20 | swing_h.wav | S+H lunge |
| 0x22 | burst00.wav | B tap volley; B+H bombs (lock 6) |
| 0x26 | jya05.wav | chain starts, D+A t=10, H start; also status burn/drain applied |
| 0x2f | power35.wav | mines (lock 3), S+B, D+H, S+H missiles (lock 3) |
| 0x33 | synth-sweep1.wav | Accel cannon spawn |

---

## 7. Other per-character table entries (index 9)

| Table (real base) | SB entry | Role |
|---|---|---|
| 0x881310 | FUN_0074b900 | input dispatch, standard (§2) |
| 0x881360 | FUN_0074bdc0 | input dispatch, alternate (§2) |
| 0x8813b0 | FUN_00743b30 | sprite loader (§4.1) |
| 0x881400+0x90 (+4·level) | FUN_0074cb90 (level 0), FUN_0074c280 (levels 1, 2) | CPU AI (not reversed; uses shared RNG) |
| 0x881558/0x8815a8/0x8815f8/0x881648 | 54afe0 / 54e4c0 / 54b870 / 54e170 | enemy family spawn / draw / update / onKilled (§5.1) |
| 0x881698/0x8816e8/0x881738 | 5094a0 / 50bb10 / 509ac0 | SB effects (§5.6) |
| 0x881788/0x8817d8/0x881828 | 7bb450 / 7bbb20 / 7bb5b0 | smoke family — SB only, unreachable (§5.8) |
| 0x881878/0x8818c8/0x881918/0x881968 | 4b5130 / 4b5280 / 4b5240 / 4b5260 | beam family — unused (§5.8) |
| 0x8819b8/0x881a08/0x881a58/0x881aa8/0x881af8 | 679f00 / 67b9b0 / 67a2e0 / 67b5e0 / 67b970 | missile family (§5.2); absorb 0x881b48 = 0 |
| 0x881b98/0x881be8 | 5eb120 / 5eb6c0 | hitbox family (§5.3) |
| 0x881c38/0x881c88/0x881cd8/0x881d28 | 6261c0 / 627a30 / 6264f0 / 627a10 | laser family (§5.4) |
| 0x881d78/0x881dc8/0x881e18 | 7c81e0 / 7c8660 / 7c82d0 | whip ribbon (§5.2) |

---

## 8. Stats record (index 9, base 0x88393c), FUN_0049ae40 (registry.c:1689-1730)

| Off | Value | Meaning |
|---|---|---|
| +0x00 | 5.0 (DAT_00813f40) | walk speed per axis |
| +0x04 | 12.0 (DAT_00813f54); **13.0** (DAT_00813f58) for ver ≥ 0x58b (FUN_004a7270) | dash speed |
| +0x08 | 10.0 (DAT_00813f4c) | UNVERIFIED (Suguri: 12.0) |
| +0x0c..+0x2c | 1,0,1,1,0,0,1,0,1 (DAT_00883948..883968) | UNVERIFIED ints |
| +0x3c/+0x40/+0x44 | 0x108, 0x109, 0x10a | sprite ids (ring / unregistered / system) |
| +0x48/+0x60/+0x78 | text ids 0x23/0x24/0x25 | colour-variant folder names |
| +0xa8 / +0x1a8 | `mdl\win_font_SB.lfd/.dat` | win-screen font |
| +0x2a8 | `sb_res_bgp` | |
| +0x2c0/+0x2c4/+0x2c8 | 0, 70, 0 | UNVERIFIED (portrait offset?) |
| +0x2cc/+0x2d0/+0x2d4 | 80, 170, 180 | colour (RGB) |
| +0x2d8 | 0 | |

---

## 9. Port verification (tests/test_starbreaker.c)
Asserted against this spec: every input→state row of both schemes (§2) incl. hyper costs; A and
S+A chains (hits at t=10/13/16, 60 each, 3×0.075 A, hit-confirm → stage 2 → stage 3, finisher
burn / hyper drain 300 frames, end at t>50, dash-cancel from 25, no chain without confirm);
B tap (switch on B==0 before t=12, bombs at t=14, 0.4 B, attach + explosion 300 dmg 100 frames
later); B hold (mine at t=12 of every 6-frame loop, one whip each, mine template); D+A
(7 missiles at t=10..22 step 2, 7×0.09 A); S+H (grab, victim pinned in state 1, 7 missiles,
7×120); H wall crash (state 0x11c, damage (t−20)·20+400); Accel (untouchable t<80, 11 ticks ×
200); S+B bits (2 bits, 0.35 B, beam damage in 60-ticks); 20000-frame random play with full
hyper on both schemes deterministic and without pool overflow.
Observed (not a bug): the B-tap bombs fan out at aim±2 rad and turn only 0.025 rad/frame after
age 20, so against a stationary target ~400 px away both pass ~±70 px wide and become 0xcb at
the wall.

## 10. UNVERIFIED / open items
- Same-tick update of objects spawned during the player update (decides exact frame offsets of
  bits, cannon, effects vs the SB timer) — objects.md §1.3.
- Effect age-increment convention and negative age0 semantics (objects.md §5 pending).
- Float accumulation of 0.1f / 0.05f scale steps (10 / 20 updates assumed).
- `FUN_007a2880` meaning in the loader's mirror fallback.
- ~~All-zero shapes~~ settled: `FUN_005f1c90` only tests circles with r>0 and rects with
  halfW>0, so missile 300's shapeProj and the laser segments never overlap anything.
- ~~Per-side colour flag 0x885a00~~ settled: written as the player's +0x18 at round init
  (all.c:284493).
- CModel vtable +0xc4 = FUN_0046e470 (forwards `(g%200)/200` to each mesh's vtable +0x68,
  probably a texture offset) — axis UNVERIFIED; not reproduced by the port (no UV scroll).
- Screen effects FUN_004e9280 / FUN_007bec20 / FUN_004d7ac0: owned by the engine fx port.
- Stats +0x08, +0x0c..+0x2c, +0x2c0..+0x2c8; HUD use of sprites 0x108/0x10a.
- `FUN_0068b8a0` / `FUN_007bd5e0` (particles/sparks): now ported by the engine (fx.c); SB calls them.
- CPU AI FUN_0074cb90 / FUN_0074c280.
