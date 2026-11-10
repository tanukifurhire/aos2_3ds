# Alte — complete moveset spec

Source: `aos2.exe` (Ghidra corpus `tools/re/dec/all.c`, constants read with `xref.exe rd`).
Every number cites its address. `t` = player state timer `p+0x154`. Template / shared names:
**suguri.md** (§1 helpers, cost table FUN_007807f0, hyper start FUN_006b1b40, FUN_00780910 hyper
spend, `+0x158` hit-confirm, ammo `+0xe8/+0xec`) and **objects.md** (object families, frame
order, collision passes, Enemy struct). `ver` = `DAT_0089426c` = 0x5a2 in live play
(objects.md §0.1) — **implement the ver ≥ 0x5a1 column**; old values are listed where they differ.

Alte = handbook id 1, **engine index 4** (`p+0x10 == 4`), states 0x32–0x4e, sprite prefix `at_`.
Her object "type" in every family is 4.

---

## 0. Notes for the lead (cross-character findings)

1. **The player main loop FUN_006f1720 contains Alte-only movement code** (all.c:380574-380631).
   When `ver > 0x58a && p+0x10 == 4 && ((state 0x41 && 15 ≤ t ≤ 40) || (state 0x44 && 15 ≤ t ≤ 34))`,
   the generic `pos += (cos,sin)(+0x94)*speed` step is followed by: let `d = (vx,vy)` be the step just
   added; if `|dx| < 8 && |dy| < 8` (DAT_008140d4 = 8) and `speed > 0.1` (DAT_00853b10): if
   `|dx| <= |dy|` add `(vx*1, vy*3)` else add `(vx*3, vy*1)` (DAT_00853b34 = 1, DAT_00813a50 = 3).
   (The speed used is `+0x98`, ×0.25 if `+0xa7`.) The core movement port must keep this hook.
2. **Hyper-start FUN_006b1b40 and the main loop touch Alte's panel** (all.c:356703, 380823): when
   `ver > 0x58a` and the player is char 4, every call of FUN_006b1b40 (any kind — hypers *and* the
   shield, see 3) sets the Searchlight Panel's `invulnerable(+0x21) = 1`; the main loop sets it back
   to 0 on frames where `0 < +0xf8 < 21` (0x15). See §5.4.
3. FUN_006b1b40 kinds not listed in suguri.md: kind 0 `+0xf4=1, +0xf8=60, +0xfc=120 (DAT_00814174)`;
   kind 1 `+0xf4=2, +0xf8=120, +0xfc=200 (DAT_008147cc)`; kind 2 `+0xf4=5, +0xf8=60, +0xfc=160
   (DAT_00814168)`; all set `+0x100=+0x104=0`, `+0x108=50`. **The Shield (FUN_006b1930(p,0)) calls
   kind 0** (state 3, speed 0, SE 0xb).
4. **Common state 6 (KO) T2 FUN_006b0110 calls a per-character hook** before its own logic:
   `switch(p+0x10)`: 4→DAT_00880a70 (= Alte T2[0x34]), 5→0x880b14, 6→0x880b98, 7→0x880c44,
   8→0x880cc8, 0xd→0x880fdc, 0xf→0x8810ec. Alte's hook kills her panel/claymores (§3, 0x34).
   Common state 0 T2 FUN_006af390 calls `DAT_00880a68` (T2[0x32]) for char 4.
5. **An Effect object has gameplay side effects**: Alte effect 300 (FUN_004edf10) drains ammo A
   through FUN_007807f0(owner, 0x4a), spawns damaging hitboxes and toggles the panel's
   invulnerability (§5.5). Effects (type 4) therefore must be simulated in order in the Effect pass
   (objects.md §1.3 step 4), not treated as cosmetic.
6. **Enemy objects read the owner's buttons directly** (panel reads `owner+0x40` S and `+0x150`).
7. Alte's dispatch enters normal moves with `state = id; t = 0` but **does not clear `+0x158`**
   (hypers do clear it). Harmless only because the main loop clears it while `state < 7` — port the
   main-loop clear.
8. Per-family fields with Alte-specific meaning: Beam `+0x6c` = homing factor; Missile `+0x64` =
   phase, `+0x74` = homing factor, byte `+0x59` = "has been inside arena after age 100"; Enemy
   `+0x70` = orbit radius / speed, `+0x74` = old-version spin rate, `+0x7c` = id copy, `+0x80` =
   phase timer, `+0x84` = phase, `+0xd4/+0xd8` = shot counter / last-shot age, `+0xdc` = shield
   lockout frame stamp.
9. Bullets fired by a Claymore are spawned with `ownerId = claymore's Enemy +0 id`, not the
   player's `+4` (all.c:179923-179927).
10. Missile ids 400 and 500 and hitbox id 0x19a exist in Alte's tables but **nothing spawns them**
    (grep of all `DAT_008819c8)(` / `DAT_00881ba8)(` call sites) — old-version leftovers; skip.

### Player fields used by Alte's code (in addition to suguri.md's table)

| Off | Name | Notes / source |
|---|---|---|
| +0x40 | S button | held S in neutral = Searchlight Shield (read by the panel, §5.4) |
| +0xa2 | hittable | Alte clears it for t ≤ 80 of 0x4e (FUN_006a71d0) |
| +0xe4 | hyper gauge | 0x44/0x45 chain spends 0.5 directly (`+0xe4 -= 0.5`) |
| +0xf8 | hyper/shield status timer | panel invulnerability while ≥ 21 (§0.2) |
| +0x128 | combo hit counter (attacker side) | written by FUN_00780ab0 (all.c:439235-439248; reset to 1 when `+0x130 < 1`, cap 99). Shield hitbox damage boost when > 9 (§5.5) |

---

## 1. Shared mechanics

Everything in suguri.md §1 applies unchanged (frame model, dash-cancel FUN_006b17d0(p,1), training
calls FUN_00612830/FUN_00612900 = no gameplay effect, cost table FUN_007807f0, hyper permission
FUN_00780950, hyper spend FUN_00780910, sparks pattern, RNG).

### 1.1 Panel query `FUN_006a0e50(team)` — "is the Searchlight Panel deployed?"
Linear scan of the Enemy vector (objects.md §1.1, accessor FUN_004ef290, count FUN_0051b1a0):
returns the **first index i with `e.team(+0x14) == team && e.type(+0x0c) == 4 && e.id(+0x10) == 100`**,
else −1. There is **no player field** — the panel's existence in the Enemy list *is* the state.
The panel is removed from the list when killed (HP < 1, onKilled FUN_0051cbb0 returns 0) or when its
fade-out finishes (§5.4). Callers: dispatch (H → 0x49 vs 0x4b), neutral 0x32, 0x3e/0x3f/0x40, 0x4b,
draw 0x32, panel/shock-ball/beam-panel updates, effects 100/0xfa/0x104/300/0x15e, main loop,
FUN_006b1b40. (Note: matches *any* type-4 panel of that team — in a mirror match the two Altes are on
different teams, so no clash.)

### 1.2 Ammo / hyper cost table entries (`0x87c4f0 + state*0xc`, registry.c:1411-1425, ver patch FUN_004a7270)

| Entry | Move | costA (+0xe8) | costB (+0xec) | hyperGain |
|---|---|---|---|---|
| 0x3c | SMG (per bullet) | 0.025 (DAT_00813edc) | 0 | 0.005 (DAT_00813eac) |
| 0x3d | SMG, D+A (per bullet) | 0.03 (DAT_00813ee0) | 0 | 0.005 |
| 0x3e | Deploy Beam Panels | 0 | 0.4 (DAT_00813f30) | 0.075 (DAT_00813efc) |
| 0x3f | Missile Shower | 0 | 0.3 (DAT_00813f28) | 0.05 (DAT_00813ef4) |
| 0x41 | Shock Rod | 0 | 0 | 0.2 (DAT_00853b14); **ver>0x5a0: 0.15** (DAT_00813f20, FUN_004a7270) |
| 0x4a | Searchlight Shield (per frame, called by effect 300) | 0.02 (DAT_00813ed8); **ver>0x5a0: 0.03** (DAT_00813ee0, FUN_004a7270) | 0 | 0 |
| 0x4c | Claymore | 0 | 1.0 (DAT_00853b34); **ver>0x5a0: 0.7** (DAT_00813f38, FUN_004a7270) | 0.1 (DAT_00853b10) |

Hypers (0x40, 0x44–0x49, 0x4b, 0x4d, 0x4e) never call it. 0x4a has no state handler.

### 1.3 Hyper flourish (t=0 of 0x40, 0x44, 0x48, 0x49, 0x4b)
`FUN_006b1b40(p,10)`; SE 6; `FUN_004e9280(100, 0.75)` (DAT_00813f3c); `FUN_007bec20(100, pos, aim, X)`
with X = 0 (0x40, 0x48, 0x49) or π/4 (DAT_0081fa90; 0x44, 0x4b); `FUN_004d7ac0(20, 0.5, 2)`;
generic effect 0xfa at pos. Cosmetic except the hyper start (same as suguri.md §1.4).

### 1.4 Grab helper pattern ("pin opponent")
Used by 0x42, 0x47, 0x4d, 0x4e: `opp = FUN_00780480(p+0x14)`; if `FUN_00780a50(opp)` (true iff
`opp+0xf8 == 0 && opp state != 6 && opp+0xa2 != 0 && opp+0xa9 != 1 && DAT_00885964 != 2`):
`opp.state = 1; opp.t = 0; opp.pos = p.pos + (cos,sin)(p+0x84)*D; opp.speed(+0x98) = 0;
opp+0x84 = wrap(p+0x84 + π)` (FUN_005f0c60). Repeated every frame of the window, so the opponent is
held in hitstun at distance D.

---

## 2. Input dispatch `FUN_006a7440` (standard scheme) — exact decode

**A. Hypers** — only if `FUN_00780950(p)`. First match wins; cost via FUN_00780910 (if it fails, next row):

| # | Condition | Cost | New state | Official move |
|---|---|---|---|---|
| 1 | S held (`+0x40>1`) && B held (`+0x3c>1`) && H pressed (`+0x44==3`) | 3.0 (DAT_00813a50) | 0x4d | **Alte Desperation Attack** (S+B+H, Accel) |
| 2 | S held && H pressed | 1.0 (DAT_00853b34) | 0x44 | **Shock Rod Punisher** (S+H) |
| 3 | B held && H pressed | 1.0 | 0x40 | **Salvo Fire** (B+H) |
| 4 | Dash held (`+0x34>1`) && H pressed | 1.0 | 0x48 | **Shock Ball** (D+H) |
| 5 | H pressed | 1.0 | panel absent (`FUN_006a0e50(team) < 0`) → **0x49**, else **0x4b** | **Searchlight Panel** / ★**SLP Spark** (H) |

Hyper entries write `state, t=0, +0x158=0` and run T2 immediately.

**B. Normal moves** — only if `state == 0 || state == 2`:

| # | Condition | Result | Move |
|---|---|---|---|
| 1 | Dash pressed (`+0x34==3`) | `FUN_006b17d0(p,0)` | Dash |
| 2 | else Shield pressed (`+0x48==3`) && `FUN_00780910(p, 0.5)` (DAT_00853b1c) && `FUN_007809e0(p)` | `FUN_006b1930(p,0)` → state 3 | Shield |
| 3 | S held && A pressed (`+0x38==3`) | 0x41 | **Shock Rod** (S+A) |
| 4 | S held && B pressed (`+0x3c==3`) | 0x4c | **Claymore** (S+B) |
| 5 | B pressed | 0x3f (→ 0x3e if released before t 9) | **Missile Shower** (B hold) / **Deploy Beam Panels** (B tap) |
| 6 | Dash held (`+0x34>1`) && A pressed | 0x3d | SMG (dash variant) |
| 7 | A pressed | 0x3c | **Submachine Gun** |

Normal entries write `state, t=0` only (see §0.7). Rows 3–7 are inside the `else` of the shield test,
so a failed shield attempt falls through to them. Returns `T2[state](p) != 0`.

★ **Searchlight Shield (S hold) is not a state.** It happens while Alte is in neutral (state 0)
holding S with the panel deployed — the *panel object* reacts (§5.4). Neutral T2 0x32 additionally
re-aims (§3), and draw 0x32 switches to the shield pose (§4).

**Alternate scheme `FUN_006a7900`** (`p+0x0c == 1`, table 0x881360): hypers are "H held (`+0x44>1`)
and X pressed": Shield-button pressed → 0x4d (cost 3); S pressed → 0x44; B pressed → 0x40;
**Dash pressed (`+0x34==3`) → 0x48**; **A pressed → 0x49/0x4b**. Normal moves identical. (diff of
the two functions.)

---

## 3. States

Common T2/T1 routing: state 0 T2 → 0x32; state 6 T2 → 0x34 (§0.4); 0x33, 0x35, 0x36 T2 are
`return 1` stubs (FUN_006a1be0 / 006a1bf0 / 006a1c00). 0x37–0x3b, 0x43, 0x4a have no handlers.

### 0x32 — neutral (T2 FUN_006a1ab0)
y+ → y += s; y− → y −= s; x− → x −= s; x+ → x += s; `s = stats+0 = 5.0` (DAT_00813f40 → 0x882af0),
not normalised. `+0x84 = 0` if facing (`+0x8c`)==1 else π (DAT_00853b58). Then **if panel exists &&
S held (`+0x40 > 1`)**: `+0x84 = FUN_007804b0(p)` (aim at opponent). Returns 1.
(Ghidra shows `__thiscall(param_1, param_2)`; the player is the 1st stack arg = `param_2`.)

### 0x34 — KO hook (T2 FUN_006a1c10), called from common state 6 T2
At t == 0 only: for every Enemy with `ownerId(+8) == p+4`: if `id == 100` (panel) or `id == 200`
(claymore): `phase(+0x84) = 900, timer(+0x80) = 0, active(+0x2c) = 0`; claymore also `+0x68 = 0`.
They fade out (§5.4/5.6). Returns 1.

### 0x3c — Submachine Gun (T2 FUN_006a1d50) — A press
| t (ver ≥ 0x5a1) | t (ver ≤ 0x5a0) | Action |
|---|---|---|
| 0 | 0 | aim |
| 4–15 | 10–21 | free movement at **0.5 ×** walk speed (DAT_00853b1c → 2.5/axis) |
| 4 | 10 | SE 0x2a |
| 6,9,12,15 (5<t<16, t%3==0) | 12,15,18,21 | if `FUN_007807f0(p,0x3c)` (A −0.025): re-aim; `pt = pos + (cos,sin)(aim)*30` (DAT_008140e8); r = `rand_f(−0.1, 0.1)` (DAT_00814130, DAT_00853b10); **missile 100** (SMG bullet) at pt, angle aim + r; Alte effect 500 at pt; on the first shot frame (t==6 / 12) generic effect 0x547; spark FUN_007bd5e0(1000, …, aim+π, pt); SE `FUN_007bcaf0(0x29,6)` |
| 15 && A held (`+0x38>1`) | 21 && A held | **t := 6** (/12) — hold loop; next frame sees 7, so continuous fire every 3 frames |
| ≥19 | ≥25 | dash-cancel (FUN_00612900 at t==19 / 25) |
| >29 | >35 | state 0 |

Order in the frame: movement, SE, fire, loop/end checks.

### 0x3d — SMG with Dash held (T2 FUN_006a2570) — D + A
Identical to 0x3c except: movement at **1.0 ×** walk speed (DAT_00853b34 → 5/axis); cost entry 0x3d
(A −0.03); old-version spread `rand_f(−0.2, 0.2)` (DAT_00817c04, DAT_00853b14); ver ≥ 0x5a1 spread
`rand_f(−0.1, 0.1)`.

### 0x3f — B press / Missile Shower (T2 FUN_006a33c0)
| t | Action |
|---|---|
| 0 | aim |
| t < 9 (ver ≤ 0x5a0: < 11) && B not held (`+0x3c < 2`) | state := **0x3e** (timer kept), run it now; nothing else this frame |
| 16 | if `FUN_007807f0(p,0x3f)` (B −0.3): 6 × **missile 300** at **pos**, angle `aim + (k/3)·π` for k = −3..2 (DAT_00813a50 = 3); if panel deployed each gets `+0x74` = **0.0035** (DAT_00814d6c; ver < 0x58c: 0.005 DAT_00813eac) instead of the template 0.0025; SE 0x22 |
| ≥25 | dash-cancel |
| >40 | state 0 |

### 0x3e — Deploy Beam Panels (T2 FUN_006a2d90) — via 0x3f early release
| t | Action |
|---|---|
| **8** (ver ≤ 0x5a0: 14) | if `FUN_007807f0(p,0x3e)` (B −0.4): for k = −3..2: `a = aim + (k/3)·π`, **+ π/6** (DAT_00817ddc) if the panel is deployed; **missile 200** (beam panel) at `pos + (cos,sin)(a)*100` (DAT_00853b90), angle a; then SE 0x2f |
| ≥20 | dash-cancel |
| >40 | state 0 |
Aim comes from 0x3f's t=0 (no re-aim). If the switch from 0x3f happens at t = 8 the panels deploy in that same frame.

### 0x41 — Shock Rod (T2 FUN_006a4180) — S + A
| t | Action |
|---|---|
| 0 | aim; `+0x88 = +0x94 = aim`; speed 0 |
| 12 | if `FUN_007807f0(p,0x41)` (free, +0.15 hyper): re-aim; `v = dist/10` (DAT_00813f4c), cap 18 (DAT_00814144); ver > 0x58a: `v = max(v, 14)` (DAT_00813a54); `+0x88 = +0x94 = aim; +0x98 = v`; SE 0x1f |
| every frame | sparks (suguri.md §1.3 pattern) if speed ≥ 15 (DAT_00853b88), dir = `+0x84` |
| 15–46 && flag158 | → **0x42** (state, t=0, flag=0), `speed *= 0.5`, run 0x42, generic effect 0xaf0 at pos, SE 0x1e. Nothing else this frame. |
| 15–46 (else) | steer: `FUN_005f0e70(&+0x84, angleTo(opp), speed*0.0025)` (DAT_008145e0); `+0x88 = +0x94 = +0x84` |
| **15–46** (ver ≤ 0x5a0: 20–46) | Alte effect 600 at `pos + dir*60` (DAT_00813a58; its `+0x2c` ends as 0.75 DAT_00813f3c — the 1.1 write on even t is overwritten); **hitbox 300** at pos, angle `+0x84` (0 dmg, r 60 → sets hit-confirm); every t%3==0: Alte effect 400 at `pos + dir*90` (DAT_00814170), angle `rand_f(−π, π)` |
| ≥45 (ver ≤ 0x5a0: ≥50) | dash-cancel |
| >55 (>60) | state 0 |
| every frame (not on the 0x42 branch) | friction `+0x9c *= 0.5` |
Plus the main-loop movement hook for 15 ≤ t ≤ 40 (§0.1).

### 0x42 — Shock Rod hold (T2 FUN_006a4ab0) — after Shock Rod hit-confirm
| t | Action |
|---|---|
| 0–19 | Alte effect 600 at pos+dir*60 (`+0x2c` = (t even ? 1.1 : current) × 0.75); every t%3==0 effect 400 at pos+dir*60, random angle; **t%4==0 (0,4,8,12,16 → 5 hits): hitbox 0x136** at pos, angle aim (80 dmg, r 60); pin opponent at D = 60 (DAT_00813a58) (§1.4) |
| 20 | **hitbox 0x140** at pos, angle aim (80 dmg, r 60, knockback 25 DAT_008140f0) |
| ≥30 | dash-cancel |
| >60 | state 0 |
Aim is kept from 0x41 (no re-aim). Speed inherited ×0.5, normal friction.

### 0x44 — Shock Rod Punisher (T2 FUN_006a4f60) — S + H, 1 level
| t | Action |
|---|---|
| 0 | aim; `v = min(dist/14, 10)` (DAT_00813a54, DAT_00813f4c); `+0x88 = +0x94 = aim; +0x98 = v`; flourish (§1.3, X = π/4) |
| 12 | re-aim; `v = clamp(dist/10, ver>0x58a ? 14 : −, 18)`; `+0x88 = +0x94 = aim; +0x98 = v`; SE 0x1f |
| every frame | sparks if speed ≥ 16 (DAT_008140d8) |
| 12–40 | Alte effect 600 at `pos + dir*80` (DAT_00814164) (`+0x2c` = (t even ? 1.2 DAT_00817d78 : cur) × 1.5 DAT_00814110); **hitbox 0x15e** at pos, angle aim (0 dmg, r 80 → hit-confirm); every even t: effect 400 at `pos + dir*100`, random angle |
| 12–40 && flag158 | → **0x47**, speed ×0.5, run it, effect 0xaf0, SE 0x1e (checked after the spawns) |
| else t ≥ 25 && B pressed (`+0x3c==3`) && `+0xe4 > 0.5` | `+0xe4 -= 0.5` (DAT_00853b1c) → **0x45** (t=0, flag=0), run it, SE 0x1e, effect 0xaf0 |
| ≥50 | dash-cancel |
| >80 | state 0 |
No steering (unlike 0x41). Main-loop movement hook for 15 ≤ t ≤ 34 (§0.1). No hitstop on hit.

### 0x45 / 0x46 — Punisher follow-ups (T2 FUN_006a56b0 / FUN_006a5cc0)
Same as 0x44 except: t=0 only aims (no lunge, no hyper start/flourish); t=12 lunge `v = min(dist/10, 18)`
**without** the 14 minimum. 0x45 chains to **0x46** on (t ≥ 25, B pressed, hyper > 0.5, −0.5);
0x46 has no B chain. Both go to 0x47 on hit-confirm (12–40). Dash-cancel ≥50, end >80.
So S+H can be followed by up to two extra lunges for 0.5 gauge each.

### 0x47 — Punisher grab (T2 FUN_006a61d0)
| t | Action |
|---|---|
| 0–39 | effect 600 at pos+dir*80 (`+0x2c` = (even ? 1.25 DAT_00815dbc : cur) × 1.5); even t: effect 400 (random angle); t%5==0: generic effect 0x514; even t: 4 debris effects 0x262/0x263 (`rand_i(0,100) < 50` picks 0x262) at angle `rand_f(0, π/2) + (k/4)·2π`; **t%4==0 (0,4,…,36 → 10 hits): hitbox 0x168** at pos (60 dmg, r 90); pin opponent at D = 80 (DAT_00814164) |
| 40 | **hitbox 0x172** at pos (80 dmg, r 90, knockback 25) |
| ≥50 | dash-cancel |
| >80 | state 0 |

### 0x40 — Salvo Fire (T2 FUN_006a2fa0) — B + H, 1 level
| t | Action |
|---|---|
| 0 | aim; `+0x88 = +0x94 = aim`; speed 0; flourish (X = 0) |
| 10, 30 | 6 × **missile 200** (beam panel) at `pos + (cos,sin)(a)*100`, `a = aim + (k/3)π` (k = −3..2), **+π/6 at t = 10** (not at 30). No cost. No SE. |
| 20, 40 | 6 × **missile 300** at pos, angle `aim + (k/3)π` (+π/6 at t = 40); `+0x74 = 0.0035` (ver<0x58c 0.005) if panel deployed; SE 0x22 |
| ≥50 | dash-cancel |
| >70 | state 0 |
Aim fixed at t=0.

### 0x48 — Shock Ball (T2 FUN_006a67e0) — D + H, 1 level
| t | Action |
|---|---|
| 0 | `+0x84 = +0x88 =` π if `opp.x <= x` else 0; speed 0; flourish (X = 0); extra SE 0x20 |
| 10,15,20,25,30 (k = 0..4, t = 10+5k) | two **Enemy 300** (shock balls): angles `a1 = aim − π/2 + (k/5)π`, `a2 = aim + π/2 + (k/5)π` (DAT_00814104, DAT_00813f40 = 5); each: `pos(+0x40,+0x44) = p.pos + (cos,sin)(a)*160` (DAT_00814168), `+0x5c = a`, `+0x58 = aim`; SE `FUN_007bcaf0(0x10,5)` |
| even t < 40 | spark effect `0xa32 + rand_i(0,100)%8` at (x, y−30), angle `aim + π/2 + rand_f(−0.15, 0.15)` (cosmetic, RNG) |
| ≥40 | dash-cancel |
| >55 | state 0 |
→ 10 balls total.

### 0x49 — Searchlight Panel (T2 FUN_006a35e0) — H, no panel present
| t | Action |
|---|---|
| 0 | aim; flourish (X = 0) |
| 10 | **Enemy 100** (panel): `pos(+0x40..+0x48) = p.pos (x,y,z)`; `+0x58 = aim`; `+0x70 = 100` (DAT_00853b90); SE 0x2f |
| ≥30 | dash-cancel |
| >60 | state 0 |
(`+0x5c`, the orbit angle, is left 0 by FUN_0055ee70's memset — §5.4.)

### 0x4b — SLP Spark (T2 FUN_006a37d0) — H, panel present
| t | Action |
|---|---|
| 0 | aim; flourish (X = π/4) |
| 5 | if the panel still exists: `phase(+0x84) = 30, timer(+0x80) = 0, scaleX(+0x60) = scaleY(+0x64) = 1.0, active(+0x2c) = 1` |
| ≥60 | dash-cancel |
| >80 | state 0 |
The spark itself is run by the panel (§5.4 phase 30). If the panel dies between dispatch and t=5, the gauge is spent for nothing.

### 0x4c — Claymore (T2 FUN_006a39d0) — S + B
| t (ver ≥ 0x5a1) | t (old) | Action |
|---|---|---|
| 0 | 0 | aim |
| **10** | 15 | if `FUN_007807f0(p,0x4c)` (B −0.7): (ver > 0x58d) every Enemy with `id == 200 && team == p.team` gets `timer(+0x80) = 1001` (old claymores expire next update); then two **Enemy 200**: for s = −1, +1: `pos = p.pos + (cos,sin)(aim + s·2.0)*80` (DAT_00813a4c = 2.0 rad, DAT_00814164), `+0x48` = uninitialised stack value (z, unused), `+0x58 = +0x5c = aim + s·0.5`, `+0x74 = s·0.01` (DAT_008178dc = −0.01 for s=−1, DAT_00813ec8 = 0.01); SE 0x2f |
| ≥20 | ≥25 | dash-cancel |
| >30 | >35 | state 0 |

### 0x4d — Alte Desperation Attack, dash (T2 FUN_006a6dc0) — S+B+H, 3 levels
| t | Action |
|---|---|
| 0 | aim; `FUN_006b1b40(p, 0x14)` (Accel); SE 7; `FUN_004e9280(50, 0.75)`; `FUN_004d7ac0(50, 0.75, 2)` |
| **15** (old 10) | re-aim; speed **50** (DAT_008140ec; old 15 DAT_00853b88); `+0x94 = aim`; SE 0x20 |
| 15–40 (old 10–20) | if `dist < 70` (DAT_008145ec) and `FUN_00780a50(opp)`: pin opponent at D = 40 (DAT_008140e0) (§1.4); → **0x4e** (t=0, flag=0); `speed *= 0.25` (DAT_00813f24); `+0x84 = +0x88 =` π if `opp.x <= x` else 0; run 0x4e; `FUN_005f8fc0(120, p+4, pos, 1.25)` (camera focus, DAT_00815dbc); `FUN_004e9280(120, 0.9)`; `FUN_004d7ac0(120, 0.6, 2)`; `FUN_007bec20(100, pos, 0, 0)`; effect 0xfa |
| ≥60 | dash-cancel |
| >80 | state 0 |
Normal friction 0.5/frame applies to the 50 px/frame dash.

### 0x4e — Desperation Attack, grab (T2 FUN_006a71d0)
| t | Action |
|---|---|
| 0 | **Enemy 400** at p.pos (visual, §5.6); ver > 0x5a0: `FUN_006b1b40(p, 0x14)` again |
| 0–70 | pin opponent at D = 20 (DAT_008140fc) along `+0x84` (0 or π) |
| 0–80 | Alte `hittable(+0xa2) = 0` |
| 80 | **hitbox 500** at p.pos, angle 0 (**1500 dmg**, ver ≤ 0x5a0: 1400; r 120; knockback 0); old versions start the Accel status here instead |
| ≥90 | dash-cancel |
| >100 | state 0 |

---

## 4. Animation

### 4.1 Sprite registration (loader `FUN_0069d5c0`)
Arg = colour variant: 0 → folder text id 5, else id 6 (charaAlteA / charaAlteB; only two variants
exist in `extracted/texture`). Same mechanics as suguri.md §4.1 (S×S quad, storage
`0x885b80 + slot*0x3888 + id*0x18`).

| Sprite id | Pattern | Frames | S | Used by |
|---|---|---|---|---|
| 0x32 | `at_std0%d` | 3 | 96 (DAT_00814118) | neutral |
| 0x33 | `at_dsh0%d` | 6 | 96 | dash (state 2), 0x4d |
| 0x34 | `at_los0%d` | 6 | 96 | KO (state 6), hit (1/5) |
| 0x36 | `at_sld0%d` | 4 | 96 | shield (3/4), 0x49, 0x4c, Searchlight-Shield pose |
| 0x37 | `at_fwd0%d` | 3 | 96 | neutral moving toward |
| 0x38 | `at_bck0%d` | 3 | 96 | neutral moving away |
| 0x39 | `at_rng0%d` | 2 | 512 (DAT_008178b0) | stats +0x3c |
| 0x3b | `at_system` | 1 | 512 | stats +0x44 |
| 0x3c | `at_smg0%d` | 5 | 96 | 0x3c, 0x3d |
| 0x3e | `at_spin0%d` | 5 | 96 | 0x3e, 0x3f, 0x40, 0x4b |
| 0x41 | `at_rod0%d` | 5 | 96 | 0x41, 0x42, 0x44–0x47 |
| 0x44 | `at_roll0%d` | 6 | 96 | 0x48 |
| 0x4d | `at_mboss` | 1 | 384 (DAT_0082524c) | Enemy 400 |
| 0x4e | `at_hld0%d` | 1 | 96 | 0x4e |
| 0x4f | `at_bfire` / `at_bfireb` | 2 | 48 (DAT_008140e4) | missile 300 exhaust |
| 0x50 | `at_gunfire0%d` | 2 | 128 (DAT_00814134) | Alte effect 500 (muzzle): frame 0 alpha blend, frame 1 additive, half 48·scale (FUN_004eb540) |
| 0x51 | `at_almond` | 1 | 54 (DAT_008178a8) | SMG bullet (missile 100) |
| 0x52 | `at_msl%d` | 9 | 64 (DAT_008140f8) | missile 300 |
| 0x53 | `at_panel%d` | 3 | 54 | beam panel (missile 200) |
| 0x54 | `at_slight0%d`/`%d` | 12 | 72 (DAT_008178ac) | Searchlight Panel (Enemy 100) |
| 0x55 | `at_psld` | 1 | 72 | Alte effect 200 (panel shield ring, half 36·scale, additive) |
| 0x56 | `at_clmr0%d` | 9 | 72 | Claymore (Enemy 200) |
| 0x57 | `at_shock0%d` | 3 | 96 | not referenced by any type-4 draw (FUN_004eb540 / 0051d000 / 0065ecf0 / 004ab0e0 sprite immediates checked) — loaded but unused |
| 0x58 | `at_elect0%d` | 4 | 72 | Alte effect 400 (frame age%4, half 24·scale, additive) |
| 0x59 | `at_impact%d` | 3 | 128 | Shock Ball (Enemy 300) |
| 0x5a | `at_exp0%d` | 3 | 256 (DAT_00814160) | Alte effects 0x168 (frame 2), 0x15e (frames 0, 1), 700 (frame 0) |
Loader line numbers: scratch decompile of FUN_0069d5c0 (`local_160` = id, `local_164` = size, loop
bound = frames). ids 0x35, 0x3a, 0x3d, 0x3f, 0x40, 0x42, 0x43, 0x45–0x4c not registered (0x3a is
referenced by stats +0x40 but never loaded). `at_fire00/01.png` exist but are never loaded.

### 4.2 Draw transforms
Identical to suguri.md §4.2: **AIM transform** (`|+0x84| >= π/2`: rotZ(aim), rotX(180°), rotY(180°);
else rotZ(aim), rotY(180°)) for every state except neutral's normal pose (no rotZ; rotY(180°) if
facing != 0). Global tint FUN_004e8f50 before every body draw. No T1 handler calls FUN_004637d0
(blend stays normal alpha: the previous draw in the frame always restores mode 1).
**2D equivalence (verified):** rotX(180°)·rotY(180°) on a flat quad = rotation by π, so the AIM
transform is exactly `flipX = 1, sy = −1 (if |aim| ≥ π/2), rotZ = aim` when the renderer applies
flip/scale in local space before rotating (tools/drawpng and the platform layer do).
Afterimage FUN_004d7e00 is queued in: 0x32 normal pose, 0x33 (always), and 0x41, 0x42, 0x44–0x47,
0x4d only while `speed(+0x98) > 2` (DAT_00813a4c).

### 4.3 Common states → Alte draw handlers (from common T1s FUN_006b07a0…)
| Common state | Alte T1 |
|---|---|
| 0 neutral | 0x32 FUN_006a8f10 |
| 1 hit, 5 | 0x35 FUN_006a98c0 |
| 2 dash | 0x33 FUN_006a94c0 |
| 3 shield, 4 | 0x36 FUN_006a9b60 |
| 6 KO | 0x34 FUN_006a9e20 |

### 4.4 Per-state frames (`pp(n)` = `(t/n)&3`, 3→1)
| State | T1 | Sprite | Frame |
|---|---|---|---|
| 0x32 | FUN_006a8f10 | default 0x32; overrides (later wins): y+→0x37; y−→0x38; facing0&x−→0x37; facing1&x−→0x38; facing0&x+→0x38; facing1&x+→0x37 (disasm 0x6a8f75-0x6a9005) | pp(5). **Searchlight-Shield pose** when `S held && panel && (ver ≤ 0x58a || frame − panel+0xdc ≥ 101)`: sprite **0x36**, frame min(t/5, 3), AIM transform, no afterimage |
| 0x33 | FUN_006a94c0 | 0x33 | pp(3); +3 if (facing1 && \|aim\|>π/2) or (facing0 && \|aim\|<π/2) |
| 0x34 | FUN_006a9e20 | 0x34 | pp(3) + (3 if global frame DAT_008858f8 even); drawn only while t < 100 |
| 0x35 | FUN_006a98c0 | 0x34 | 0 |
| 0x36 | FUN_006a9b60 | 0x36 | min(t/4, 3) |
| 0x3c, 0x3d | FUN_006aa120 / 006aa430 | 0x3c | t<3 → 0; t<6 → 1; else (t−6)%3 + 2 (ver ≤ 0x5a0: thresholds 9 / 12) |
| 0x3e, 0x3f, 0x4b | FUN_006aa740 / 006aacd0 / 006ab280 | 0x3e | min(t/4, 4) |
| 0x40 | FUN_006aaa00 | 0x3e | f = t/5; if f>4: f −= 5; if f>4: f = 4 |
| 0x41 | FUN_006ab800 | 0x41 | t<12 → 0; t<15 (ver ≤ 0x5a0: 20) → 1; else ((t−15)/3)%3 + 2 |
| 0x42, 0x47 | FUN_006abbf0 / 006acb00 | 0x41 | (t/3)%3 + 2 |
| 0x44, 0x45, 0x46 | FUN_006abf90 / 006ac360 / 006ac730 | 0x41 | t<12 → 0; t<20 → 1; else ((t−20)/3)%3 + 2 |
| 0x48 | FUN_006acea0 | 0x44 | body: t<10 → 0 else ((t−10)/3)%3 + 1; then a 2nd layer frame 4 rotated by `aim + ((t/3)%6)/6·2π` (DAT_00813f44 = 6) while t ≤ 32, else aim (flip choice still from \|aim\|) |
| 0x49 | FUN_006aaf90 | 0x36 | t<10 → 0 else ((t−10)/4)%4 + 1, with 4 → 2 |
| 0x4c | FUN_006ab540 | 0x36 | min(t/4, 3) |
| 0x4d | FUN_006ad470 | 0x33 | pp(3) |
| 0x4e | FUN_006ad810 | 0x4e | 0 |
(Sprite ids from the `mov [ebp-x], id` immediates feeding `imul …, 0x18` in each handler.)

---

## 5. Spawned objects (all type 4; character-specific code)

### 5.1 Beam family: spawn FUN_004aa870, update FUN_004ab7b0, draw FUN_004ab0e0, onHit FUN_004ab6c0
Template FUN_004ba490 (objects.md §2.2).
| id | speed | shape {hx,hy,r} | dmg | knock | stun | other |
|---|---|---|---|---|---|---|
| 100 beam-panel shot | 16 (DAT_008140d8) | {24, 8, 0} (DAT_00853b8c, DAT_008140d4) | 100 | 10 (DAT_00813f4c) | 1.0 | hitType 1 (dash-dodgeable, heat-scaled); `+0x6c` homing factor = 0 |
| 0x6e trail | 0 | none | 0 | — | — | `active(+0x70) = 0` |
Update 100: if `+0x6c > 0 && age < 50`: `FUN_005f0e70(&+0x60, angleTo(opp), (50 − age)·(+0x6c)·0.5)` (DAT_008140ec, DAT_00853b1c), `+0x64 = +0x60`. Move straight by `+0x68`. If `age > 10` and outside arena+30 (DAT_008140e8) → removed silently. Else spawn beam **0x6e** at its pos, angle `+0x60` (every frame).
Update 0x6e: `age > 3` → `alpha(+0x5c) −= 0.2` (DAT_00853b14); removed at alpha ≤ 0.
onHit 100: effect 1000 (angle +π), particle FUN_0068b8a0(1000 ×10), spark FUN_007bd5e0(1000), SE 0x13.
Draw: 100 draws nothing; 0x6e draws 2–3 quads from a shared team-coloured atlas via sprite objects
DAT_0087fdf4/0087fdf0 (src rects from colour index `DAT_008859f8 + team*0x10`, size 60×? / 40×8 /
48×16 divided by zoom, alpha 16·alpha / 0xff, colour tint by `DAT_00885a00+team*0x10`) — cosmetic.

### 5.2 Missile family: spawn FUN_0065e420, update FUN_00661c50, draw FUN_0065ecf0, onHit FUN_00661450, onKilled FUN_00661870
Template FUN_00685bb0: hitType 2 (not dash-dodged, not heat-scaled), `+0x68 = +0x6c = angle`,
`growScale(+0x5c) = 1`, active 1, stun 1.0, hp 60. Per id:

| id | Move | speed | `+0x6c` | hp | dmg | body r / proj r | knock | stun | other |
|---|---|---|---|---|---|---|---|---|---|
| 100 SMG bullet | 0x3c/0x3d, claymore | 15 (DAT_00853b88); **ver>0x58a 17** (`_DAT_00813f5c`) | `rand_f(−π, π)` (sprite spin) | 60; **ver>0x5a0 120** | 30 | 8 / 8 (DAT_008140d4) | 5 (DAT_00813f40) | **0.4** (DAT_00813f30) | |
| 200 beam panel | 0x3e, 0x40 | 0 | angle | 180; **ver>0x5a0 9999** | 0 | 0 / 10; **ver>0x5a0 0** | 10 | 1 | `growScale = 0`, active 0 |
| 300 Missile Shower | 0x3f, 0x40 | 8 (DAT_008140d4) | angle | 120 | 200 | 10 / 10 | 10 | 1 | `+0x74` = 0.0025 (DAT_008145e0) homing factor |

Update 100: `+0x6c += 0.25` (DAT_00813f24), wrap > π by −2π; move along `+0x68` at speed; if outside
arena+10 → call onHit, return 0.

Update 200 (beam panel; RNG-free in live version):
- age < 100: `growScale += 0.1` while < 1; when > 1: clamp 1, `active = 1`. At **ages 15, 20, 25, 30, 35**: fire **beam 100** at its pos, angle `+0x6c + off`, generic effect 0x548, SE 0x10:
  - no SL panel: off = −0.125, −0.06, 0, 0.06, 0.125 (DAT_00824a84, DAT_00824a80, —, DAT_00813ef8, DAT_00813f1c);
  - SL panel deployed: off = −0.2, −0.15, 0, 0.15, 0.2 (DAT_00817c04, DAT_00815dc4, —, DAT_00813f20, DAT_00853b14), and the beam's `+0x6c` homing = **0.001** (DAT_00814bd8).
  - ver ≤ 0x5a0: off = `rand_f(−0.25, 0.25)` (no panel) / `rand_f(−0.5, 0.5)` (panel), only for ages < 35 (4 shots), no homing set.
- age ≥ 100: `active = 0`; `growScale −= 0.1`; removed when ≤ 0 (≈ age 110).
- Never moves (speed 0); can't hit players (dmg 0, body r 0).

Update 300 (Missile Shower):
- age < 50: `speed −= 0.25` while speed > 0 (8 → 0 at age 32).
- age ≥ 50: `active = 1`; `speed += 0.1` while < 10; if age < 200: `FUN_005f0e70(&+0x68, angleTo(opp), speed·(+0x74))`, `+0x6c = +0x68`.
- move along `+0x68`; smoke effect 0x802 (behind by 20, DAT_008140fc) on even ages or when speed > 12.
- if outside arena+10: if byte `+0x59 == 1` or age ≥ 300 → onHit, return 0. Else if inside and age > 100 → `+0x59 = 1`.

onHit (hit player / expire): 100 → effect 1000 + spark FUN_007bd5e0, SE 0x13; 200/300 → effects
0x262, 0x514, SE 0x14 (`bom06`). onKilled (shot down): 100 → effect 0x516 (no SE); 200/300 →
0x262, 0x514, SE 0x1d (`gun12`). Both return 0 → missile removed.
Ids 400, 500 (old claymore/panel implementations) are dead code (§0.10).

Draw (cosmetic): 100 → sprite 0x51 frame 0 rotated by `+0x6c`; 200 → 0x53 frames 0 (alpha 1, rgb =
growScale), 1, 2 (additive, pulse alphas `sin((age%50)/50·2π)·0.25+0.75` and `·0.35+0.65`),
scale (g, 4 − 3g); 300 → exhaust 0x4f frames 1 and 0 (additive) 42 px behind (DAT_00824488),
y-scale 0.3 on every other 2-frame block, then 0x52 frames `((age/3)%3)*3 + 0..2` with the same
pulse layers.

### 5.3 Hitbox family: spawn FUN_005e0180, onHit FUN_005e0740
All hitType **3** (hit dashing players), stun 1.0, **all shapes are circles** `{0, 0, r}`
(`+0x34`). One frame each (objects.md §2.4).

| id | Spawned by | dmg | r | knock | onHit |
|---|---|---|---|---|---|
| 100 | Searchlight Shield (effect 300, §5.5) | 30 (set to 0 on most frames, 100 with combo>9) | 80 (DAT_00814164) | 0 (15 with combo>9) | dmg < 100: SE 0x13 (lock 4); if dmg > 0: 6 streaks 0x456 + 0x546 (RNG). dmg ≥ 100: if victim `state == 1 && speed > 2` → **dealsDamage = 0** (no damage); else streaks + SE 0x15 |
| 200 | SLP Spark (panel phase 30) | 90 (0 on most frames; final 150) | 140 (DAT_00817004) | 0 (final 30) | dmg > 0: effect 400 (RNG), SE 0x13; dmg ≥ 100: SE 0x15; dmg > 149: shake FUN_004d7ac0(10, 0.75, 1) |
| 300 | Shock Rod 0x41 | 0 | 60 (DAT_00813a58) | 0 | **owner `+0x158 = 1`** (owner = FUN_00780450(hb team)), SE 0x15 lock 2 |
| 0x136 | Shock Rod hold ×5 | 80 | 60 | 0 | SE 0x15 lock 2 |
| 0x140 | Shock Rod final | 80 | 60 | 25 (DAT_008140f0) | effect 0xa8d, shake (10, 0.75, 1), SE 0x19 |
| 0x15e | Punisher lunge | 0 | 80 | 0 | **owner `+0x158 = 1`**, SE 0x15 lock 2 |
| 0x168 | Punisher grab ×10 | 60 | 90 (DAT_00814170) | 0 | SE 0x15 lock 2 |
| 0x172 | Punisher final | 80 | 90 | 25 | as 0x140 |
| 400 | Shock Ball | 45 | 60 | 0 | if victim `state != 1 || (speed ≤ 2 && t > 4)`: 8 streaks + 0x515, SE 0x15 lock 4; **else dealsDamage = 0** |
| 0x19a | — (never spawned) | 0 | 60 | 0 | — |
| 500 | Desperation | 1400; **ver>0x5a0 1500** | 120 (DAT_00814174) | 0 | sets hitbox angle = atan2(victim − hb) (knockback direction outward) |
The 0-damage hitboxes still go through `FUN_00780ab0(p, 0, 3, 1.0)` → stun +1.0 → hitstun with 0
knockback (objects.md §4.4), which is what lets the grab states follow.

### 5.4 Enemy family (summons): spawn FUN_0051a570, update FUN_0051b1d0, draw FUN_0051d000, onKilled FUN_0051cbb0
Template FUN_0055ee70 (objects.md §2.6): memset 0, unique id, owner, team, p18, drawLayer 1, solid 0,
invulnerable 0, scale (1,1,1), mirrorChildren 1, parent −1. Spawn per id (offsets from FUN_0051a570's
locals; `+0x7c` = id copy, used by the update's switch):

| id | Object | hp/maxHp | active | shape | scale (x,y,z) | `+0x84` phase | Spawned by |
|---|---|---|---|---|---|---|---|
| 100 | Searchlight Panel | 1000; **ver>0x5a0 2000** | 0 | {0,0,30} (DAT_008140e8) | (0, 3, 0) (DAT_00813a50) | 0 | 0x49 |
| 200 | Claymore | 400 | 0 | {0,0,30} | (0, 3, 0) | 0 | 0x4c ×2 |
| 300 | Shock Ball | 999 | 0 | none | (0, 0, 0) | 0 | 0x48 ×10 |
| 400 | Desperation visual | 1000 | 0 | none | (1, 1, 0) | 0 | 0x4e |
`+0x78` age and `+0x80` timer are incremented by the manager after each update (objects.md §1.4).
`+0x58` = facing/aim angle, `+0x5c` = orbit/move direction, `+0x70` = orbit radius (panel) / speed.

#### Enemy 100 — Searchlight Panel (the ★ condition)
**Existence = deployed** (§1.1). Lifetime: unlimited; ends only when (a) killed by damage (HP < 1 →
onKilled: effects 600, 5× 0x834, 5× 500, SE 0x1a; returns 0 → erased), or (b) phase 900 (Alte KO,
§3 0x34) fade-out finishes. Becomes collidable (`active = 1`) at **age == 100** (or immediately when
SLP Spark sets it). Until then projectiles pass through it.

Per-frame update (enemy pass, before projectiles/effects):
1. Phase logic (`+0x84`):
   - **0 (unfold)**: scaleX += 0.1, scaleY −= 0.2 (DAT_00853b14), scaleZ += 0.1; when scaleX ≥ 1: all = 1, phase **10**, Alte effect 100 (aura, layer 1).
   - **10 (idle)**: nothing.
   - **20 (shield)**: every 5th age: effect 200; every 6th: effect 0xfa (cosmetic).
   - **30 (SLP Spark)**: at timer == 10: `FUN_004e9280(100, 0.5)`, `FUN_004d7ac0(50, 0.5, 2)`, effect 0x15e, SE 0x1e. For **10 ≤ timer ≤ 50**: hitbox **200** at panel pos, angle `+0x5c`; if timer == 50 set its dmg 150 (0x96) and knock 30 (DAT_008140e8); then **if age % 5 != 0 → dmg = 0** (this also zeroes the final hit unless age%5==0). Effect 0x104 every 6th timer in 11..59. timer > 100 → phase 10.
   - **other (900 = dismiss)**: `active = 0`; scaleX −= 0.1, scaleY += 0.1; return 0 (erase) when scaleX ≤ 0.
2. `owner = FUN_00780450(team)`, `opp = FUN_00780480(team)`. `+0x58` = in phase 20/30: angle(owner → opp); else angle(panel → opp).
3. `if (ver < 0x58b || owner+0xf8 < 1) invulnerable(+0x21) = 0`.
4. `if (ver > 0x58a && owner.S(+0x40) < 2 && owner.state == 0) +0xdc = 0` (lockout cleared by releasing S in neutral).
5. **shield = owner.S ≥ 2 && owner.state == 0 && !(ver > 0x58a && frame(DAT_008858f8) − +0xdc < 101)**.
   - shield: `FUN_005f0e70(&+0x5c, +0x58, 0.2)`; `if r > 50 (DAT_008140ec): r −= 10`; if phase == 10 → phase **20** + Alte effect **300** (the shield, owner = owner player's +4) and effect 300 is what does the work (§5.5).
   - not shield: if phase 30: `FUN_005f0e70(&+0x5c, +0x58, 0.2)`, `r = min(r + 10, 100)` (exact: `if r >= 100: r = 100 else r += 10`); else `if r < 100: r += 5` (DAT_00813f40); phase 20 → 10.
6. **Position: `pos = owner.pos + (cos,sin)(+0x5c) · r`** (`r = +0x70`). `+0x5c` starts at **0** (memset; neither the spawner nor 0x49 writes it) → the panel first sits 100 px to the **+x side** of Alte and only rotates while shielding / sparking; it keeps the last angle afterwards.
7. `if age == 100: active = 1`.

So: **SLP Spark (H) requires the panel to exist** (dispatch row A5); **Searchlight Shield requires the
panel to exist + S held + Alte in state 0 + not locked out**, and it costs **0.03 ammo A per frame**
(entry 0x4a) — when ammo A runs out the shield drops and `+0xdc` = current frame locks it until S
is released in neutral or 101 frames pass.

Panel invulnerability (`+0x21`, hits register but no HP loss vs beams/hitboxes/missiles; **lasers
ignore it**, objects.md §4.5): set by FUN_006b1b40 (any hyper or Alte's shield, ver > 0x58a); cleared
by the main loop when `0 < owner+0xf8 ≤ 20` and by step 3 when `owner+0xf8 < 1`; set every frame by
the active shield effect except on effect-age%15 ∈ {0,1,2} (§5.5) — the panel is vulnerable on those
frames (bug-compatible).

#### Enemy 200 — Claymore
- Phase 0: unfold like the panel; when scaleX ≥ 1: phase **10**, speed `+0x70 = 10` (DAT_00813f4c), age = 0, timer = 0, Alte effect 0x96 (follows it).
- Phase 10 / 11:
  - age > 30: `speed *= 0.975` (DAT_00814128) while > 2; sparks (0xa28+n, angle `+0x58 + rand_f(−0.2, 0.2)`) every 3rd age while speed > 2; facing `+0x58` turns toward the opponent by **0.012 rad/frame** (DAT_00813ed0) (ver < 0x58b: `+0x58 += +0x74`); wrap to (−π, π]; move `pos += (cos,sin)(+0x5c)·speed`.
  - `active = 1` when age > 29 (ver ≤ 0x58b: only at age == 30).
  - **Wall bounce** (outside arena radius 540, margin 0): `a = atan2(pos)`; `+0x5c = (a − +0x5c) + (a + π) + rand_f(−0.1, 0.1)`, FUN_005f0ce0 is called but its x87 result is popped (`fstp st0` at 0x51bd78) → the direction is **not** wrapped (only `+0x58` is wrapped each frame); `pos = (cos a, sin a)·540`; **age = 1000**; SE 0x25; effects 0x547/0x546.
  - **Fire check** when `age > 30 && age % 3 == 0 && phase == 10`: triangle `(pos, pos + 300·dir(+0x58 − 0.15), pos + 300·dir(+0x58 + 0.15))` (DAT_008145f0, DAT_00813f20) contains the opponent's position (FUN_005f1820) → effect 500 at pos+20·dir, **missile 100** (SMG bullet) at pos, angle `+0x58 + rand_f(−0.1, 0.1)`, ownerId = claymore id (§0.9), SE 0x29 lock 6; (ver > 0x58a) `+0xd4++`, `+0xd8 = age`; if `+0xd4 > 14` → (ver ≥ 0x58c) `timer = 1001` (expires after **15 shots**; ver 0x58b: phase 11 reload pause). This block **returns 1** whether or not it fired.
  - (Counter decay `+0xd4--` when `phase == 10 && age%3 == 0 && age − +0xd8 > 3` — unreachable in practice because the fire block returns first; transcribe anyway.)
  - phase 11 && age > 179 → phase 10, age 0, counters 0 (old versions only).
  - **timer > 1000** → `active = 0`, phase 900, scaleZ 0. (Lifetime ≈ 1001 frames after unfolding, or until a new Claymore is placed, or 15 shots.)
- Phase 900: scaleX −= 0.1, scaleY += 0.2; erased when scaleX < 0 (strictly).
- onKilled (HP 400 depleted): effects 600 / 0x834 / 500, SE 0x1a.

#### Enemy 300 — Shock Ball
- Phase 0: `+0x58 = rand_f(−π, π)` every frame (RNG); scaleX, scaleZ += 0.1; when scaleZ > 1: clamp, phase **100**, timer 0.
- Phase 100: when timer > 20: speed `+0x70 = 0`, phase **200**, timer 0, `+0x5c = angle(ball → opp) + rand_f(−0.25, 0.25)` (DAT_0081411c, DAT_00813f24).
- Phase 200: speed += 0.1 up to 7.5 (DAT_00814150); `pos += (cos,sin)(+0x5c)·speed`; **if the Searchlight Panel exists**: turn `+0x5c` toward the opponent by `speed · 0.0015` (DAT_008153dc). If timer > 50 and outside arena+20 → phase 900 + effect 0x515.
- Phase 900: scaleX −= 0.05; erased when ≤ 0.
- Every frame with phase != 900 and `age % 15 < 3`: Alte effect 400 (RNG angle).
- Phases 100/200, `timer % 4 == 0`: **hitbox 400** at pos, angle `+0x5c` (45 dmg, r 60).
- Not collidable (inactive, no shape) → cannot be shot.

#### Enemy 400 — Desperation visual
`pos.x = owner.x`; timer < 40: `pos.y = owner.y − sin(timer/80·π)·300 + 100`; else `owner.y −
sin(timer/80·π)·200` (DAT_00814164 = 80, DAT_008145f0, DAT_008147cc); scaleZ = 1; scaleX = timer/80.
timer > 78 → effects 700, 10× 0x834, 10× 0x1f5, SE 0x1b, erased. No collision.

Enemy draw (cosmetic): 100 → sprite 0x54, base frame 0 (idle) or `((age/4)%3 + 1)·3` in phases
20/30, three layers (base; additive pulse ×2) scaled (scaleX, scaleY), rotated by `+0x58`; 200 →
0x56 frames `((age/5)%3)·3 + 0..2`; 300 → 0x59 frames 0–2 (third layer ×2 scale); 400 → 0x4d.
Colour-call argument order is (a, r, g, b) (PORTING.md "Decompiler gotchas", CModel +0xdc).

### 5.5 Alte effects (type 4): spawn FUN_004eab40, update FUN_004edf10, draw FUN_004eb540
Effect struct (from spawn locals): `+0 type, +4 id, +8 colour/layer, +0xc ownerId, +0x10 team,
+0x14 byte, +0x18 pos, +0x24, +0x28 age, +0x2c scale, +0x30 alpha, +0x34/+0x38 angles, +0x3c, +0x40`.
Effect update pre-increments age (objects.md §1.4) → first update sees age 1.

**Effect 300 = Searchlight Shield (gameplay).** Spawn: scale 0, alpha 1. Each update:
- panel = FUN_006a0e50(team), owner = FUN_00780450(team). If no panel/owner: alpha −0.1, scale −0.1, erased at alpha ≤ 0.
- else: follow panel pos; `+0x34 = panel+0x58`; `+0x38 = rand_f(−π, π)` (RNG every frame).
  - if `panel.phase == 20 && FUN_007807f0(owner, 0x4a)` (ammo A −0.03): scale += 0.2 while < 1 (clamp 1; reaches 1.0 on the 5th update); **when scale ≥ 1: hitbox 100** at panel pos, angle `+0x34`, `dmg = (age % 5 == 0) ? 30 : 0`; **if owner `+0x128` (combo count) > 9: dmg = 100, knock = 15** (DAT_00853b88) on every frame. Then if `age % 15 < 3`: Alte effect 400 and **return** (panel invulnerability not set this frame); else `panel.invulnerable = 1`.
  - else: (ver > 0x58a && panel.phase == 20, i.e. out of ammo) `panel+0xdc = DAT_008858f8` (lockout); alpha −0.1, scale −0.1; erased at alpha ≤ 0.
  Because the cost is paid inside the effect, the shield drains ammo A even on frames when the hitbox deals 0.
  A new effect 300 is spawned at every 10→20 transition; an old one still fading will re-grow if phase becomes 20 again before it dies (two shield effects can coexist → double drain/hitboxes; follows directly from the code — not observed in-game).

Other ids (cosmetic; most follow the panel and consume RNG): 100 panel aura (alpha flag 0 in phases
20/30), 0x96 claymore aura (follows enemy by id via FUN_0055f0c0), 200, 0xfa, 0x104 panel sparks,
0x15e spark flash, 0x168, 400 particles, 500 muzzle, 600 rod/shock glow, 0x172, 0x262/0x263 debris,
700 explosion. Generic ids used: 0x456, 0x514, 0x515, 0x516, 0x546–0x548, 0x802, 0x834, 0x1f5, 0xa28–0xa2f, 0xa32–0xa39, 0xa8d, 0xaf0, 0xfa, 1000.

---

## 6. Sound ids used (SE table `0x9b99a0 + 4*id`, all.c:464010)
| id | file | where |
|---|---|---|
| 6 | up01.wav | hyper start |
| 7 | high_a.wav | Desperation start |
| 0xb | eco03.wav | shield |
| 0x10 | laser03.wav | beam-panel shots, Shock Ball spawn |
| 0x13 | don16.wav | beam / SMG / shield / spark hits |
| 0x14 | bom06.wav | missile expire/hit |
| 0x15 | crash17.wav | rod / punisher / shock-ball hits |
| 0x19 | hit_f.wav | rod & punisher finishers |
| 0x1a | tm2_bom002.wav | panel / claymore destroyed |
| 0x1b | explosion1.wav | Desperation explosion |
| 0x1d | gun12.wav | missile shot down |
| 0x1e | tm2_bom003.wav | grab start, punisher chain, SLP Spark |
| 0x1f | swing.wav | rod / punisher lunge |
| 0x20 | swing_h.wav | Shock Ball start, Desperation dash |
| 0x22 | burst00.wav | Missile Shower / Salvo missiles |
| 0x25 | reflect.wav | claymore wall bounce |
| 0x29 | sen_ge_gun04.wav | SMG / claymore shots |
| 0x2a | A5_01029.wav | SMG start |
| 0x2f | power35.wav | beam panels / Searchlight Panel / Claymore deploy |

---

## 7. Other per-character table entries (engine index 4)
| Table (real base) | Alte entry | Role |
|---|---|---|
| 0x881310 | FUN_006a7440 | input dispatch, standard (§2) |
| 0x881360 | FUN_006a7900 | input dispatch, alternate (§2) |
| 0x8813b0 | FUN_0069d5c0 | sprite loader (§4.1) |
| 0x881400 + 0x40 + 4*level | FUN_006a86a0 (level 0), FUN_006a7dd0 (levels 1, 2) | CPU AI (not reversed) |
| 0x881878/0x8818c8/0x881918/0x881968 | 4aa870 / 4ab7b0 / 4ab0e0 / 4ab6c0 | beam (§5.1) |
| 0x8819b8/0x881a08/0x881a58/0x881aa8/0x881af8 | 65e420 / 661c50 / 65ecf0 / 661450 / 661870 | missile (§5.2); onAbsorb 0x881b48 = 0 |
| 0x881b98/0x881be8 | 5e0180 / 5e0740 | hitbox (§5.3) |
| 0x881558/0x8815a8/0x8815f8/0x881648 | 51a570 / 51d000 / 51b1d0 / 51cbb0 | enemy spawn/draw/update/onKilled (§5.4) |
| 0x881698/0x8816e8/0x881738 | 4eab40 / 4edf10 / 4eb540 | effects (§5.5) |
| 0x881c38… (laser), 0x881d78… (whip) | none | — |
Registry lines: registry.c `DAT_00881320 = FUN_006a7440` etc. (grep `DAT_008818(88|d8)`, …).
`FUN_00617110` case 6 forces value 10 for char 4 (non-gameplay selection, all.c:303893) — ignore.

---

## 8. Stats record (index 4, base 0x882af0), written by FUN_0049ae40 (registry.c:1377-1410)
| Off | Value | Meaning |
|---|---|---|
| +0x00 | 5.0 (DAT_00813f40) | walk speed per axis |
| +0x04 | 13.0 (DAT_00813f58) | dash speed |
| +0x08 | 10.0 (DAT_00813f4c) | UNVERIFIED (Suguri 12) |
| +0x0c..+0x20 | {5,5,0}, {4,6,0} | **per-palette colour slots** (palette 0 / 1), copied to `0x8859f4 + team*0x10 + 4k` by FUN_005f5780 (all.c:284485) — colour index for effects / sparks / beam trails |
| +0x3c/+0x40/+0x44 | 0x39, 0x3a, 0x3b | sprite ids (rng / unloaded / system) |
| +0x48 / +0x60 | text ids 5 / 6 | colour-variant folder names (only 2 variants) |
| +0xa8 / +0x1a8 | `mdl\win_font_AT.lfd/.dat` | win font |
| +0x2a8 | `at_res_bgp` | |
| +0x2c0/+0x2c4/+0x2c8 | 0, 50, 0 | UNVERIFIED |
| +0x2cc/+0x2d0/+0x2d4 | 250, 140, 210 | colour RGB |
| +0x2d8 | 0 | |
FUN_004a7270 changes only Alte's cost entries (§1.2), no stats.

---

## 9. UNVERIFIED / open items
- Claymore spawn `+0x48` (z) is an uninitialised stack local (`local_18`/`local_c` in FUN_006a39d0);
  the port stores the player's z. Nothing in the 2D gameplay reads enemy z.
- Two simultaneous shield effects (fading + new): follows from the code; not observed in-game.
- Stats +0x08 (10.0) and +0x2c0..+0x2c8: meaning not traced here (core.md owns the stats record).
- AI (FUN_006a86a0 / FUN_006a7dd0): ported separately in ai_alte.c by another agent, not re-checked here.
- (resolved) Effects 100 / 0x96 light cones: 3-vertex gradient fan through DAT_00893e28
  (+0x18 begin, +0x2c tri, +0x1c end), blend 1, WORLD coordinates (no FUN_00619d90):
  apex at the effect, colour `colTab[variant]` (DAT_0087e138) with alpha byte
  `128·scale·pulse·aux2` (100) / `128·scale·pulse` (0x96), pulse = sin((age%50)/50·2π)·0.35+0.65;
  far corners same RGB, alpha 0, at distance 300 (DAT_008145f0, id 100) / 250 (DAT_008149ac, 0x96)
  along `rotation(+0x38) ± 0.2·scale` (100) / `± 0.15·scale` (0x96). aux2 = 0 in panel phases 20/30,
  so the panel's cone is invisible while shielding / sparking. Ported with aos_draw_tri; checked
  with drawdump (panel idle, shielding, two claymores).

Resolved in the verification pass: colour-call order (a,r,g,b); sprite ids 0x50/0x55/0x58/0x5a
(effects) and 0x57 (unused); stats +0x0c..+0x20 (colour slots); effect-300 hitbox timing (asserted
by tests/test_alte.c: shield damage is a multiple of 30); AIM transform 2D equivalence (§4.2).

## 10. Port status (src/sim/chars/alte.c, tests/test_alte.c)
tests/test_alte.c asserts: both dispatch schemes incl. the hyper cost fall-through and the ★ panel
switch H → 0x49 / 0x4b; SMG shot frames 6/9/12/15, bullet template, 0.025 ammo cost and hold loop;
beam-panel deployment frame/radius/template and their beams; Missile Shower frame/template;
panel spawn frame/HP/position/activation; ★ Searchlight Shield (phase 20, effect 300, net −0.02/f
ammo A, invulnerability, radius 50, out-of-ammo lockout and its release); barrier hook (shield →
panel invulnerable, cleared under barrierTime 21); SLP Spark phase/activation and damage pattern;
hitbox templates 100/200/500; KO hook; Claymore frame/cost/HP/replacement; Shock Rod chain damage
480, Punisher chain damage 680, Desperation grab damage 1500; 10 Shock Balls; end frame of every
state; determinism of random play.
