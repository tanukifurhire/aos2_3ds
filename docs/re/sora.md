# Sora — complete moveset spec

Source: `aos2.exe` (Ghidra corpus `tools/re/dec/all.c`, constants read with `xref.exe rd`, layer ids
from `xref.exe dis`). Every number cites its address. `t` = player state timer `p+0x154`. Structure
mirrors suguri.md; shared helpers (cost table `FUN_007807f0`, hyper start `FUN_006b1b40`, hyper spend
`FUN_00780910`, hit-confirm `+0x158`, ammo `+0xe8/+0xec`, dash-cancel `FUN_006b17d0(p,1)`, training-only
`FUN_00612830/FUN_00612900`, dash sparks) are used with the meanings given there and in objects.md.
Live play runs `DAT_0089426c = 0x5a2` (objects.md §0.1): **port the ver ≥ 0x5a1 / > 0x5a0 columns.**

Engine character index (`p+0x10`) = **3** (handbook id 0 + 3). State ids 0x08–0x21.

---

## 0. Notes for the lead (affect all characters)

1. **Object families are NOT shared code.** Every family table (`0x881878` beam, `0x8819b8` missile,
   `0x881b98` hitbox, `0x881c38` laser, `0x881558` enemy, `0x881698` effect, …) has a *separate*
   spawn/update/draw/onHit function per character type, even though they are linked far from the
   character's code block. Sora (type 3) uses beam `FUN_004b3fe0`, not Suguri's `FUN_004b52a0`; its
   object ids overlap Suguri's (100, 200, 300 …) with different parameters. Every character spec must
   reverse its own type-index entries (§7 lists Sora's). The struct layouts/managers in objects.md are
   shared; the per-id behaviour is not.
2. **Effects can be gameplay objects.** Sora's effect **0x15e** (type 3 effect list, update
   `FUN_00508d60`) moves and spawns damaging hitboxes every frame (§5.6). So the Effect list (updated
   in tick step 4 after Beam→Missile→Laser, with `age` *pre*-incremented, objects.md §1.4) must be part
   of the deterministic simulation, not a cosmetic layer, at least for such ids.
3. **Common effect id 100 (the dash graze zone, objects.md §4.7) is spawned by non-dash code**: Sora's
   laser 100 at laser age 1 (`FUN_00626040`) and Boisterous Dance finisher 0x1d at t=6,9,12,15,18
   (`FUN_0073aaa0`). These are real graze zones owned by Sora (owner/team args are Sora's). Port them as
   the generic effect 100.
4. **`p+0x108` = barrier-break threshold** (name mine). `FUN_006b1b40` sets it to 50 (all.c:356702);
   a shield (common state 3 `FUN_006afac0` t=5, and `FUN_006af880`) can shatter the opponent's
   hyper barrier (`+0xf8 := 10`) only while `opp+0xf8 > opp+0x108`. Sora's Boisterous Dance whiff
   writes `+0xf8 = 50`, `+0x108 = 25` directly (§3 0x1c). For core.md.
5. Sora's missile *expiry* (left the arena) calls the **onKilled** table entry (`0x881af8`,
   `FUN_00679580`) itself, not onHit (Suguri's expiry calls onHit). Behaviour is per type.
6. Dispatch differences vs Suguri: Rapid-fire row tests Dash `+0x34 > 1` (Suguri `> 0`); there is a
   5th hyper row "Dash held + H" (state 0x17, not in the manual).

### Player fields used by Sora's code
Same as suguri.md's table (`+0x04` owner, `+0x0c` scheme, `+0x10` =3, `+0x14` side/team, `+0x18`
slot, `+0x1c..+0x28` dirs, `+0x34..+0x48` Dash/A/B/S/H/Shield, `+0x6c/+0x70` pos, `+0x84` aim, `+0x88`,
`+0x8c` facing, `+0x94` move dir, `+0x98` speed, `+0x9c` friction, `+0xe0` heat, `+0xe4` hyper,
`+0xe8/+0xec` ammo A/B, `+0x150/+0x154/+0x158`). Additionally written by Sora:

| Off | Name | Where |
|---|---|---|
| +0xf8 | hyper barrier frames left (shield timer, objects.md §4.2) | 0x1c whiff: := 50 (FUN_0073a360) |
| +0x108 | barrier-break threshold (Note 4) | 0x1c whiff (ver>0x58a): := 25 |

---

## 1. Shared mechanics

Frame model, state entry, "state = 0 without resetting t", dash cancel, sparks, `FUN_007804b0` aim,
`FUN_00780570` distance, `FUN_0061b230` input angle: **identical to suguri.md §1.1/§1.3.**
Dash sparks in Sora's states: `if speed(+0x98) >= 3.0 (DAT_00813a50)`: `r = rand_f(lo, hi)`,
`n = rand_i(0,100) % 8`, common effect `0xa28+n` at pos, angle `aim + r` (lo/hi per state).

### 1.1 Ammo / hyper cost table (`FUN_007807f0`, entries written by FUN_0049ae40, registry.c:1347-1376)

| State | Move | costA | costB | hyperGain |
|---|---|---|---|---|
| 0x12 | Beam Rifle | 0.25 (DAT_00813f24) | 0 | 0.04 (DAT_00813eec) |
| 0x13 | Rifle Spread (per shot) | 0.1 (DAT_00853b10) | 0 | 0.01 (DAT_00813ec8) |
| 0x14 | Charged Rifle | 0 | 0 | 0.1 (DAT_00853b10) |
| 0x15 | Slash Through → rifle | 0.4 (DAT_00813f30) | 0 | 0.1 |
| 0x18 | Sword 1 | 0 | 0 | 0.05 (DAT_00813ef4) |
| 0x19 | Sword 2 | 0 | 0 | 0.025 (DAT_00813edc) |
| 0x1a | Sword 3 | 0 | 0 | 0.025 |
| 0x1b | Slash Through | 0 | 0 | 0.05 |
| 0x1e | Launcher Homing (per missile) | 0 | 0.075 (DAT_00813efc) | 0.01 |
| 0x1f | Launcher Staggered (per missile) | 0 | 0.15 (DAT_00813f20) | 0.02 (DAT_00813ed8) |

Hypers 0x16, 0x17, 0x1c, 0x1d, 0x20, 0x21 never call it (no gauge gain). The version patch
`FUN_004a7270` touches no Sora entry (no reference to 0x87c5c8..0x87c66c or the 0x882814 record).

### 1.2 Hyper start / flourish
`FUN_006b1b40(p,10)` (normal hyper: `+0xf4=3, +0xf8=100, +0xfc=160, +0x108=50`) or `(p,0x14)`
(Accel). Flourish at t=0 of 0x16, 0x17, 0x1c, 0x20 is byte-identical to Suguri's: SE 6,
`FUN_004e9280(100, 0.75)`, `FUN_007bec20(100, pos, aim, π/4 (DAT_0081fa90))`, `FUN_004d7ac0(20, 0.5, 2)`,
common effect 0xfa. Permission `FUN_00780950` as suguri.md §1.4.

---

## 2. Input dispatch `FUN_0073b470` (standard scheme, table 0x881310 + 4*3 = 0x88131c)

**A. Hypers** — only if `FUN_00780950(p)`. First match wins; cost via `FUN_00780910` (on failure
the next row is tried):

| # | Condition | Cost | State | Official move |
|---|---|---|---|---|
| 1 | S held (`+0x40>1`) && B held (`+0x3c>1`) && H pressed (`+0x44==3`) | 3.0 (DAT_00813a50) | 0x21 | **Unlimited Charge** (S+B+H, Accel) |
| 2 | S held && H pressed | 1.0 (DAT_00853b34) | 0x1c | **Boisterous Dance** (S+H) |
| 3 | B held && H pressed | 1.0 | 0x20 | **Hyper Launcher** (B+H) |
| 4 | Dash held (`+0x34>1`) && H pressed | 1.0 | 0x17 | Hyper Spread, random-fan variant (D+H — not in the manual) |
| 5 | H pressed | 1.0 | 0x16 | **Hyper Spread** (A+H; A not checked) |

**B. Normal moves** — only if `state == 0 || state == 2`:

| # | Condition | Result | Move |
|---|---|---|---|
| 1 | Dash pressed | `FUN_006b17d0(p,0)` | Dash |
| 2 | Shield pressed && `FUN_00780910(p,0.5)` && `FUN_007809e0(p)` | `FUN_006b1930(p,0)` | Shield |
| 3 | S held && B pressed | 0x1b | **Slash Through** (S+B) |
| 4 | S held && A pressed | 0x18 (no distance test) | **Sword** (S+A) |
| 5 | B pressed | 0x1f (→0x1e on early release) | **Launcher** Staggered / Homing |
| 6 | Dash held (`+0x34 > 1`) && A pressed | 0x13 | **Rifle Spread** (D+A) |
| 7 | A pressed | 0x14 (→0x12 on early release) | **Beam Rifle** / **Charged Rifle** |

Then `return T2[state](p) != 0`.

**Alternate scheme `FUN_0073b930`** (`p+0x0c==1`, 0x88136c): hyper rows become "H held (`+0x44>1`)
and X pressed": Shield→0x21 (3.0), S→0x1c, B→0x20, **Dash pressed**→0x17, A→0x16 (1.0 each). Rows B
identical.

Hidden chains (not in manual): Sword 1 →(B)→ Slash Through, Sword 1→2→3 on S+A, Slash Through →(A)→
rifle 0x15; Boisterous Dance → finisher 0x1d when the opponent is in hitstun at t=65.

---

## 3. States

Common notation: "aim" = `+0x84 = FUN_007804b0(p)`; `dir(a)` = `(cos a, sin a)`. Spawns:
beam `(*DAT_00881884)(id, &pos, angle, p+4, 0, side)`; missile `(*DAT_008819c4)(id, &pos, angle, p+4, 0,
side, slot)`; hitbox `(*DAT_00881ba4)(id, &pos, angle, p+4, 0, side)`; laser `(*DAT_00881c44)(id, &pos,
angle, p+4, side, slot)`; Sora effect `(*DAT_008816a4)(id, color, &pos, angle, p+4, 0, side)`; common
effect `(*DAT_00881698)(…)`. Parameters of every id: §5.

### 0x08 — neutral movement (T2 FUN_00737800, called from common state 0 T2 FUN_006af390 `case 3`)
Identical to Suguri 0x128: per held direction pos ±= stats+0 = **5.0** (DAT_00813f40 → 0x882814),
diagonals not normalised; `+0x84 = facing ? 0 : π`. Returns 1.

### 0x09, 0x0a, 0x0b, 0x0c — stubs (T2 FUN_007378f0 / 00737920 / 00737900 / 00737910 return 1)
Their T1s serve common states (§4.3). 0x0d–0x11 have no handlers.

### 0x14 — Beam Rifle press / Charged Rifle (T2 FUN_00737a50) — A press
| t | Action |
|---|---|
| 0 | aim |
| 0–10 | if A not held (`+0x38 < 1`) → `state = 0x12` (timer kept), run 0x12 now (the rest of this frame is skipped) |
| 13–39 | if Dash not held (`+0x34 < 2`): free walk 5.0/axis as 0x08; re-aim every frame (re-aim even if Dash held) |
| 15–28 | if A released (`+0x38 < 2`): if `FUN_007807f0(p,0x12)` → beam **100** at pos, aim, SE 0xd; **t := 100** (even without ammo) |
| 29 | `FUN_0065dff0(2, pos, owner)` (charge-ready flash) + SE 0x2a |
| 30–39 | if A released: `FUN_007807f0(p,0x14)` (free, +0.1 hyper; result ignored) → beam **300** at pos, aim; common effects 0x456 at aim±π/2 (DAT_00814104), 0x515 at aim (colour `DAT_008859f4+side*0x10`); SE 0xe; **t := 100** |
| 40 | t := 30 (charge held indefinitely) |
| 25–39, t%3==0 | common effect 0x514 at pos, angle 0 |
| ≥115 | dash-cancel (612900 at t==115) |
| >150 | state 0 |
Order in a frame: early-release check; walk/re-aim (13–39); release 15–28; t==29; release 30–39;
t==40 loop; particles; cancel/end. No B-chain from this state (unlike Suguri).

### 0x12 — Beam Rifle quick shot (T2 FUN_00737930) — via 0x14 early release
| t | Action |
|---|---|
| 0 | aim (only if entered fresh) |
| 12 | if `FUN_007807f0(p,0x12)` (A −0.25): re-aim; beam **100** at pos, aim; SE 0xd |
| ≥20 | dash-cancel |
| >30 | state 0 |

### 0x13 — Rifle Spread (T2 FUN_00737f30) — Dash held + A
| t | Action |
|---|---|
| 0 | aim |
| 10,12,14,16,18,20 (9<t<21, t even → **6 shots**) | if `FUN_007807f0(p,0x13)` (A −0.1): re-aim; `r = rand_f(−0.4, 0.4)` (DAT_008147f0, DAT_00813f30); beam **200** at pos, angle aim+r; SE 0xd |
| every frame (except the cancel frame) | friction `+0x9c *= 0.5` (inherited dash speed decays 0.25/frame) |
| ≥20 | dash-cancel |
| >30 | state 0 |
No steering of the move direction (unlike Suguri 0x135).

### 0x15 — Slash Through rifle finisher (T2 FUN_007380d0) — A after Slash Through hit
| t | Action |
|---|---|
| 0 | aim |
| 5 | if `FUN_007807f0(p,0x15)` (A −0.4): re-aim; beam **300** (charged shot) at pos, aim; SE 0xe. Old versions only: ver==0x58a heat +1.2 (DAT_00817d78), 0x58b≤ver≤0x5a0 heat +0.3 (DAT_00813f28), cap 3.0 |
| ≥30 | dash-cancel |
| >40 | state 0 |
(ver==0x58b only: friction ×0.75 DAT_00813f3c.)

### 0x18 — Sword 1 (T2 FUN_00739670) — S+A (any distance)
| t | Action |
|---|---|
| 0 | aim; lunge: `+0x88 = +0x94 = aim`, `+0x98 = min(dist/14, 14)` (DAT_00813a54) |
| every frame | sparks if speed ≥ 3.0, `r = rand_f(−0.3, 0.3)` (DAT_0081fa88, DAT_00813f28) |
| 5 | SE 0x1f |
| 10 | if `FUN_007807f0(p,0x18)`: hitbox **200** at `pos + dir(aim)*60` (DAT_00813a58), angle aim. (0x58b<ver<0x5a1: heat +0.15) |
| ≥10 && flag158 | B pressed → **0x1b**; else S held && A pressed → **0x19** (each with FUN_00612830, t=0, flag=0) |
| ≥30 | dash-cancel |
| >40 | state 0 |

### 0x19 — Sword 2 (T2 FUN_00739a70)
| t | Action |
|---|---|
| 0 | aim; lunge `min(dist/15, 10)` (DAT_00853b88, DAT_00813f4c), dir aim |
| 5 | SE 0x1f |
| 10 | if cost ok: hitbox **0xd2** at `pos + dir(aim)*40` (DAT_008140e0), angle aim. (old ver heat +0.1) |
| ≥10 && flag158 && S held && A pressed | → **0x1a** |
| ≥30 | dash-cancel; >40 state 0 |
No sparks.

### 0x1a — Sword 3 (T2 FUN_00739d60)
| t | Action |
|---|---|
| 0 | aim; `+0x94 = aim`, speed 0 |
| 8 | re-aim; `+0x88 = +0x94 = aim`; speed **18** (DAT_00814144); SE 0x20 |
| 9 | if cost ok: hitbox **0xdc** at **pos** (no offset), angle aim (old ver heat +0.1) |
| every frame | sparks (speed ≥ 3, r ∈ [−0.3,0.3]); friction ×0.5 |
| ≥30 | dash-cancel; >40 state 0 |

### 0x1b — Slash Through (T2 FUN_0073a010) — S+B, or B after Sword 1 hit
| t | Action |
|---|---|
| 0 | aim; `+0x94 = aim`, speed 0 |
| 8 | re-aim; `+0x88 = +0x94 = aim`; speed **16** (DAT_008140d8); SE 0x21 (`swing_d.wav`) |
| 9 | if cost ok: hitbox **0xe6** at **pos**, angle aim (cos/sin are computed and discarded) |
| every frame | sparks (speed ≥ 3, r ∈ [−0.3,0.3]) |
| ≥8 && flag158 && A pressed | → **0x15** (rifle finisher) |
| otherwise | friction ×0.5; ≥30 dash-cancel; >40 state 0 |

### 0x1e — Launcher (Homing) (T2 FUN_007389c0) — via 0x1f early release
| t | Action |
|---|---|
| 12,15,…,30 (11<t<31, t%3==0 → **7 missiles**) | if `FUN_007807f0(p,0x1e)` (B −0.075): re-aim; `a = rand_f(−π/2, π/2)` (DAT_0081416c, DAT_00814104); `d = rand_f(40, 80)` (DAT_008140e0, DAT_00814164); `pt = pos + dir(aim)*(−100) (DAT_00853bb8) + rotate((d,0), a+aim)`; missile **100** at pt, angle `aim + a/3` (DAT_00813a50); SE 0x22 |
| ≥35 | dash-cancel |
| >45 | state 0 |
No aim at t=0 in this handler (set by 0x1f's t=0). No friction change.

### 0x1f — Launcher (Staggered Launch) press (T2 FUN_00738c00) — B press
| t | Action |
|---|---|
| 0 | aim |
| 0–10 | if B `+0x3c < 2` (released) → `state = 0x1e` (timer kept), run it |
| ver ≥ 0x5a1: 21,24,27,30 (20<t<31, t%3==0 → **4 rockets**) | if `FUN_007807f0(p,0x1f)` (B −0.15): `r = rand_f(π/3, 3π/4)` (DAT_008203bc, DAT_0081fa94) then (ver>0x5a0) **r := π/3 for t=21,24; π/2 for t=27,30**; sign = `(t/3)%2==0 ? + : −` (t=21 −, 24 +, 27 −, 30 +); `ang = aim ± r`; `d = rand_f(40,80)` then **d := 40 (t=21,24) / 50 (t=27,30)** (DAT_008140e0 / DAT_008140ec); `pt = pos + dir(aim)*(−50) (DAT_008149e4) + rotate((d,0), ang)`; missile **0x96** at pt, moveAngle ang; then the missile's `+0x6c` (draw/collision angle) := aim; SE 0x22 |
| ≥31 | dash-cancel (612900 at 31) |
| >56 | state 0 |
ver < 0x5a1: fire at 15,18,21,24 with the random r/d; dash-cancel ≥25; end >50.
Both rand draws happen even in the new version (RNG order).

### 0x16 — Hyper Spread (T2 FUN_007382e0) — H (or A+H), 1 level
| t | Action |
|---|---|
| 0 | aim; `FUN_006b1b40(p,10)`; flourish (§1.2) |
| 9 | re-aim |
| 9,12,…,36 (8<t<37, t%3==0 → **10 lasers**) | `a = rand_f(0, 0.35)` (DAT_00813f2c), `d = rand_f(50, 90)` (DAT_008140ec, DAT_00814170) (both drawn always); ver>0x5a0: **a** = t12: 0.04 (DAT_00813eec); t15,18: 0.08 (DAT_00813f00); t21,24: 0.12 (DAT_00824a8c); t27,30: 0.16 (DAT_0081cc64); others (9,33,36): 0; **d = (t−9) + 50**. If `(t−9)%9==0` (t=9,18,27,36): a = −a. `off = (cos a · d, sin a · d · 3)` rotated by aim; laser **100** at pos+off, angle **aim**; SE `FUN_007bcaf0(0x12, 6)` |
| ≥40 | dash-cancel |
| >50 | state 0 |
New-version offsets: t9 (−0,50), 12 (+.04,53), 15 (+.08,56), 18 (−.08,59), 21 (+.12,62), 24 (+.12,65),
27 (−.16,68), 30 (+.16,71), 33 (0,74), 36 (−0,77). Parallel lasers, fanned in position only.

### 0x17 — Hyper Spread, random fan (T2 FUN_007386c0) — Dash held + H, 1 level
t=0 as 0x16 (no re-aim at t=9). Same 10 laser frames: `a = rand_f(0,0.35)`, `d = rand_f(50, 100)`
(DAT_00853b90), `a = −a` when `(t−9)%9==0`; `off = (cos a·d, 3·sin a·d)` rotated by aim; laser **100**
at pos+off, angle **aim + a** (diverging fan). **No version override** — always random. SE (0x12,6).
Dash-cancel ≥40; >50 state 0.

### 0x1c — Boisterous Dance (T2 FUN_0073a360) — S+H, 1 level
| t | Action |
|---|---|
| 0 | aim; hyper start (10) + flourish |
| 5 | re-aim; lunge `+0x88 = +0x94 = aim`, `+0x98 = min(dist/15, 12)` (DAT_00853b88, DAT_00813f54) |
| every frame | sparks if speed ≥ 3, `r = rand_f(−0.5, 0.5)` (DAT_00853bb0, DAT_00853b1c) |
| 8 | SE 0x1f |
| 10 | hitbox **0xfa** at `pos + dir(aim)*60`, angle aim (no cost) |
| 11–59 | **if flag158 == 0 (opener whiffed): t := 66; `+0xf8 := 50`; ver>0x58a: `+0x108 := 25`** |
| 20, 40, 60 (11≤t≤60, (t−10)%10==0, not %20) | hitbox **0xfc** at `pos + dir(aim)*60`, angle `aim + 0.5` if `|aim| ≤ π/2` else `aim − 0.5`; SE 0x1f |
| 30, 50 ((t−10)%20==0) | hitbox **0xfb** same point, angle `aim − 0.5` if `|aim| ≤ π/2` else `aim + 0.5`; SE 0x1f |
| 65 | if opponent `state == 1` (hitstun) → **0x1d** (t=0, flag=0) |
| otherwise | friction ×0.5; ≥70 dash-cancel; >80 state 0 |
Whiff path jumps to t=66, skipping the hits and the t=65 finisher check (state ends at t=81).
flag158 is only set by 0xfa's callback (0xfb/0xfc don't), and is never cleared inside the state.
Hits 0xfb/0xfc also slow Sora: her `+0x98 *= 0.75` on each hit (callback, §5.4).

### 0x1d — Boisterous Dance finisher (T2 FUN_0073aaa0)
| t | Action |
|---|---|
| 0 | aim; speed 0 |
| 5 | re-aim; `+0x88 = +0x94 = aim`; speed **20** (DAT_008140fc); hitbox **0x104** at `pos + dir(aim)*60`, angle aim; common effect 0xa8c at that point; SE 0x20 |
| 6,9,12,15,18 (4<t<21, t%3==0) | **common effect 100 (graze zone)** at pos, angle aim (Note 3) |
| ≥40 | dash-cancel; >50 state 0 |
No friction change (speed decays 0.5/frame).

### 0x20 — Hyper Launcher (T2 FUN_007391c0) — B+H, 1 level
| t | Action |
|---|---|
| 0 | aim; hyper start (10) + flourish |
| 12,14,…,30 (11<t<31, t even → **10 rockets**) | `r = rand_f(π/3, 3π/4)`; ver>0x5a0: r = t12,14: π/3; 16,18: π/2; 20,22: π; 24,26: 5π/6 (DAT_00825e78 = 2.618); 28,30: 3π/4. sign `(t/2)%2==0 ? + : −` (12 +, 14 −, 16 +, …, 30 −); `ang = aim ± r`; `d = rand_f(20, 80)` (DAT_008140fc, DAT_00814164); ver>0x5a0: d = 12,14: 40; 16,18: 50; 20,22: 60; 24,26: 70 (DAT_008145ec); 28,30: 40. `pt = pos + dir(aim)*(−50) + rotate((d,0), ang)`; missile **0x96** at pt, moveAngle ang; then missile `+0x6c := aim`, **damage `+0xa0 := 200`**; SE `FUN_007bcaf0(0x22, 4)` (no cost) |
| ≥30 | dash-cancel; >60 state 0 |

### 0x21 — Unlimited Charge (T2 FUN_0073ad10) — S+B+H, 3 levels
| t | Action |
|---|---|
| 0 | aim; speed 0; `FUN_006b1b40(p,0x14)`; SE 7; `FUN_004e9280(150, 0.85)` (DAT_00814120); `FUN_007bec20(100, pos, aim, π/4)`; `FUN_004d7ac0(150, 0.65 (DAT_0081788c), 2)`; common effect 0xfa |
| t<51, t even | 4 cosmetic particles: for k=−2..1: `rand_i(0,100) < 50` ? common effect 200 : 0xd2, angle `k/2·π + rand_f(−0.75, 0.75)` (RNG: int then float per k) |
| 10–60 | `pt = pos + dir(aim)·t·4` (DAT_00853b68). Sora effect **300** at pt (visual ring), `+0x3c = (t−10)·20`, even t: `+0x40 = 0.85`. Hitbox: t%5==0 → **300**, else **0x12d**, at pt, angle aim; its radius `+0x34 = ((t−10)/50)·280` (DAT_008140ec, DAT_00814f14) |
| 60 | additionally Sora effect **0x15e** at pt, `+0x3c = 1000` → travelling wave that spawns hitboxes (§5.6) |
| t%3==0, t<55 | cosmetic wall sparks common `0xa82 + rand_i(0,100)%8` at `pos + dir(aim)*50`, angle `aim + rand_f(−0.25, 0.25)`, `+0x40 = t/60` |
| ≥80 | dash-cancel; >110 state 0 |
Hitboxes 300/0x12d **pull** the victim toward the circle centre (callback re-aims knockback, §5.4).
The growing circle (r 0 → 280 px, centre advancing 4 px/frame: 40 → 240 px ahead) hits every frame;
damage only on t = 10,15,…,60 (11 ticks × 110).

---

## 4. Animation

### 4.1 Sprite registration (loader `FUN_007333a0`)
Arg = colour variant 0/1/2 → folder text id 0x20/0x21/0x22 (charaSoraA/B/C). Variant 2 falls back to
0x21 when both players are Sora (`p+0x10 == 3` for both) and `FUN_007a2880` gives one side 2 and the
other 0 (mirror-match palette swap; 0x7334c0-0x733556). Quad = S×S via `FUN_00467d00(S,S,2,2,1.0,0)`.
Storage `0x885b80 + slot*0x3888 + id*0x18`. Sprite id = `local_1a8` assignment in the loader.

| Sprite id | Pattern | Frames | S | Used by |
|---|---|---|---|---|
| 0x08 | `sr_std0%d` | 3 | 96 (DAT_00814118) | neutral |
| 0x09 | `sr_dsh0%d` | 9 | 96 | dash |
| 0x0a | `sr_los0%d` | 6 | 96 | T1 0x0a, 0x0b |
| 0x0c | `sr_sld0%d` | 6 | 96 | T1 0x0c, 0x1e, 0x1f, 0x20 |
| 0x0d | `sr_fwd0%d` | 3 | 96 | neutral (toward) |
| 0x0e | `sr_bck0%d` | 3 | 96 | neutral (away) |
| 0x0f | `sr_rng0%d` | 2 | 512 (DAT_008178b0) | stats +0x3c (UNVERIFIED role) |
| 0x11 | `sr_system` | 1 | 512 | stats +0x44 (UNVERIFIED role) |
| 0x12 | `sr_rfl0%d` | 5 | 96 | T1 0x12–0x17, 0x21 |
| 0x18 | `sr_swda0%d` | 7 | 96 | Sword 1 / Boisterous body |
| 0x19 | `sr_swdb0%d` | 4 | 96 | Sword 2 / Boisterous body |
| 0x1a | `sr_swdc0%d` | 6 | 96 | Sword 3 / Slash Through / Boisterous body |
| 0x22 | `sr_msl0%d` | 5 | 64 (DAT_008140f8) | homing missile |
| 0x23 | `sr_rckt0%d` | 9 | 96 | rocket 0x96 |
| 0x24 | `sr_bfire`, `sr_bfireb` | 2 | 48 (DAT_008140e4) | missile exhaust |
| 0x25 | `sr_exp0%d` | 3 | 256 (DAT_00814160) | effect (UNVERIFIED which) |
| 0x26 | `sr_laser` | 1 | 256 | laser 100 atlas |
| 0x27 | `sr_rflc0%d` | 4 | 256 | effect (UNVERIFIED which) |
| 0x28 | `sr_swdas0%d` | 3 | 192 (DAT_0081415c) | slash layer (swda) |
| 0x29 | `sr_swdbs0%d` | 3 | 96 | slash layer (swdb) |
| 0x2a | `sr_swdcs0%d` | 2 | 192 | slash layer (swdc) |
| 0x2d | `sr_swdab0%d` | 3 | 192 | blade layer (swda) |
| 0x2e | `sr_swdbb0%d` | 2 | 96 | blade layer (swdb) |
| 0x2f | `sr_swdcb0%d` | 2 | 192 | blade layer (swdc) |
All 94 `sr_*.png` in `extracted/texture/charaSoraA/` are covered (+3 `res_*`). ids 0x0b, 0x10, 0x13–0x17,
0x1b–0x21, 0x2b, 0x2c not registered (0x10 = stats +0x40 id, unregistered like Suguri's 0x130).

### 4.2 Draw transforms
AIM transform (every Sora T1 except 0x08) — exact in 2D as flipX + sy = −1 (when |aim| ≥ π/2) + rotZ(aim), since RotX(180°)·RotY(180°) on a flat quad is a 180° turn applied before rotZ; identical to Suguri: pos; if `|aim| ≥ π/2`: rotZ(aim), rotX(180°), rotY(180°), else rotZ(aim), rotY(180°). Each
body/blade draw preceded by global tint `FUN_004e8f50`, except T1 0x21 (no tint call) and the slash
layers. Heat afterimage `FUN_004d7e00` only in T1 0x08, 0x09, 0x1a, 0x1b, 0x1c, 0x1d.

### 4.3 Common states → Sora draw handlers (common T1s `case 3`)
| Common state | T1 |
|---|---|
| 0 | 0x08 FUN_0073cf10 (FUN_006b07a0 → DAT_00880050) |
| 1, 5 | 0x0b FUN_0073d5d0 (DAT_0088005c) |
| 2 | 0x09 FUN_0073d1b0 (DAT_00880054) |
| 3, 4 | 0x0c FUN_0073d8a0 (DAT_00880060) |
| 6 | 0x0a FUN_0073db60 (DAT_00880058) |

### 4.4 Per-state frames (`pp(n)` = `(t/n)&3` with 3→1)
| State | T1 | Sprite | Frame |
|---|---|---|---|
| 0x08 | FUN_0073cf10 | 0x08; overrides as Suguri (y+→0x0d; y−→0x0e; facing0&x−→0x0d; facing1&x−→0x0e; facing0&x+→0x0e; facing1&x+→0x0d; later wins) (0x73cf35-0x73d005) | pp(5) |
| 0x09 | FUN_0073d1b0 | 0x09 | `(t/2)%6`; moving against facing: pp(3)+6 |
| 0x0a | FUN_0073db60 | 0x0a | pp(3) + (3 if `DAT_008858f8` even); **drawn only while t < 100** |
| 0x0b | FUN_0073d5d0 | 0x0a | **pp(3)** (Suguri's equivalent is frame 0) |
| 0x0c, 0x1e, 0x1f, 0x20 | FUN_0073d8a0 / 0073efa0 / 0073f260 / 0073f520 | 0x0c | min(t/4, 5) |
| 0x12 | FUN_0073de60 | 0x12 | t<12 ? 0 : min((t−12)/3+1, 4) |
| 0x13 | FUN_0073e700 | 0x12 | t<20 ? 0 : min((t−20)/3+1, 4) |
| 0x14 | FUN_0073e140 | 0x12 | t<100 ? 0 : min((t−100)/3+1, 4) |
| 0x15 | FUN_0073e420 | 0x12 | t<5 ? 0 : min((t−5)/3+1, 4) |
| 0x16, 0x17 | FUN_0073e9e0 / 0073ecc0 | 0x12 | t<36 ? 0 : min((t−36)/3+1, 4) |
| 0x21 | FUN_00743850 | 0x12 | `f = t<50 ? 0 : (t−50)/4+1`; if f>1: `f = ((f−2)&3)+2`; 5→3 (loop 2,3,4,3) |

Sword family (draw order left→right; blade = blade layer id, slash = slash layer id):
| State | T1 | Body f | Blade frame b | Order | Slash frame |
|---|---|---|---|---|---|
| 0x18 | FUN_0073f7e0 | body 0x18, f = min(t/3, 6) | 0x2d: f0→0, f1→1, else 2 | if b==0 blade(0); body; slash; if b≠0 blade(b) | 0x28: f1→0, f2→1, f3→2, else none (wrapped in `FUN_004637d0(1)`) |
| 0x19 | FUN_00740370 | body 0x19, f = min(t/3, 3) | 0x2e: f0→0, f1→none, f2,3→1 | body; slash; blade | 0x29: f0→0, f1→0, f2→1, f3→2; none if t>15 |
| 0x1a | FUN_00740be0 | body 0x1a, f = t<8 ? 0 : min((t−8)/3+1, 5) | 0x2f: f0→0 else 1 | body (+ghost); slash; blade | 0x2a: f1→0, f2→1, else none |
| 0x1b | FUN_00741520 | as 0x1a | as 0x1a | as 0x1a | as 0x1a (no blend calls) |
| 0x1d | FUN_00742c20 | body 0x18, f = min(t/3, 6) | 0x2d as 0x18 | as 0x18 (+ghost) | 0x28 as 0x18 |
| 0x1c | FUN_00741e50 | t<16: body 0x18, f=min(t/3,6), blade 0x2d (as 0x18). 16≤t<66: `k=(t−16)/10`, `f=min(((t−16)%10)/2, 3)`; k even → body 0x1a, blade 0x2f (f0→0 else 1); k odd → body 0x19, blade 0x2e (f0→0, f1→none, f2,3→1). t≥66: body 0x1a, f=min((t−66)/3+4, 5), blade 0x2f frame 1 | — | if (blade 0x2d && b==0) blade(0) first; body (+ghost); slash; blade unless it was drawn first or is "none" | slash id 0x28/0x29/0x2a by body; 0x18/0x1a: f1→0, f2→1, f3→2 (0x1a: f≥3 none); 0x19: f0→0, f1→0, f2→1, f3→2 |
(Layer ids from immediates: 0x73f7e9/0x73fdf9, 0x740379/0x7406af, 0x740c08/0x740fc4, 0x741548/0x7418fa,
0x741e84..0x74266d, 0x742c48/0x7432bb; blade id via `imul …, 0x2d/0x2e/0x2f`.)

---

## 5. Spawned objects (Sora = type 3; all functions are Sora-specific)

### 5.1 Beam: spawn FUN_004b3fe0, update FUN_004b47c0, draw FUN_004b4310, onHit FUN_004b4590
Template: memset 0x98, type 3, `FUN_004ba490` (objects.md §2.2: hitType 1, destroyOnHit 1, alpha 1,
active 1, stun 1.0). Collision rules = generic Beam passes (dashing targets pass through).

| Id | Used by | speed (+0x68) | shape {hx,hy,r} | dmg | knock | lifetime / behaviour |
|---|---|---|---|---|---|---|
| 100 Beam Rifle | 0x12, 0x14 | **17** (`_DAT_00813f5c`) | {24, 8, 0} (DAT_00853b8c, DAT_008140d4) | **180** | 10 (DAT_00813f4c) | straight; dies silently outside arena+10 |
| 200 Rifle Spread | 0x13 | 17 | {24, 8, 0} | **60** | 10 | straight; `age > 29` → die (checked first); at **age 20 `active := 0`** (hits only ages 0–19); dies outside arena+10 |
| 300 Charged shot | 0x14, 0x15 | **15** (DAT_00853b88) | {0, 0, 30} (DAT_008140e8) | **130** (ver≥0x58c; 0x58b: 90; older 120) | 10 | straight; dies outside arena+30 (ver>0x58a; else +10). **onHit → beam 0x136** at its pos/angle + SE 0x13 |
| 0x136 Charge burst | 300's onHit | 5 (DAT_00813f40) | {0,0,0} (never collides) | 0 | 0 | move full speed while inside arena+5, else ×0.1 (DAT_00853b10) (ver≥0x58b); dies outside arena+30; **ages 0,4,8,12,16: hitbox 100** at pos (5 hits); returns 0 when age > 20 |

Per-frame visuals (cosmetic, but consume no RNG except as noted): 100 → Sora effect 200 every frame,
common 0x547+0x548 at age 1, particle `FUN_0068b8a0(1000, …, 1)` while age<10; 200 → Sora effect 0xd2
every frame (its `+0x34 = 1 − (30−age)/10` when age>20), 0x548 at age 1, particle 0x44c while age<5;
300 → Sora effect 0xdc every frame, 0x546+0x548 when age%8==0, particle 0x456 every frame; 0x136 →
Sora effect 0xdc every frame, 0x547 when age%5==0, particle 0x456. Draw FUN_004b4310 (300/0x136
only): 2D sprite **0x27 (`sr_rflc`)** of the team's sprite slot `DAT_00885a00+side*0x10`
(`imul eax, edx, 0x27` @0x4b445c into the 2D vector at `0x885b8c`), frame 0 (blend 2, alpha 128·a
DAT_00814134) then frame 3 (blend 1, alpha 255·a DAT_00853b94), src (0,0)-(127,127), half (60, 40)/zoom
(DAT_00813a58, DAT_008140e0), bam(−angle) = +angle in world space. onHit visuals: 100 → common 1000 (angle+π), particles 1000 ×10, spark 1000 (10,10),
SE 0x13; 200 → common 0x3f2, particles ×5, spark (6,6), SE 0x13.

### 5.2 Missile: spawn FUN_00677970, update FUN_00679630, draw FUN_00677b20, onHit FUN_00679420, onKilled FUN_00679580
Template: memset 0xb8, type 3, `FUN_00685bb0` (hitType 2 → not dodged by dash; stun 1.0), growScale
`+0x5c = 0` (+0.2/frame to 1.0, DAT_00853b14).

| | Homing 100 (0x1e) | Rocket 0x96 (0x1f, 0x20) |
|---|---|---|
| initial speed | **5** (DAT_00813f40) | **6** (DAT_00813f44) |
| HP (+0x78) | 60 | **120** |
| damage (+0xa0) | **150** | **250** (Hyper Launcher overrides **200**) |
| shapeBody / shapeProj | {0,0,10} / {0,0,10} (DAT_00813f4c) | {30, 20, 0} / {30, 20, 0} (DAT_008140e8, DAT_008140fc) — rectangle at angle `+0x6c` |
| knockback | 10 | 10 |
| phase 1 | age<20: if speed > 2 (DAT_00813a4c): speed −= 0.25 (DAT_00813f24). Angle fixed | age<100: if speed > 0.5 (double `_DAT_00824a98`): speed −= 0.2; **moveAngle** turns toward opponent ≤ 0.005 rad/frame (DAT_00813eac); `+0x6c` stays = aim |
| phase 2 | 20≤age<100: moveAngle turns toward opponent ≤ 0.01 rad/frame (DAT_00813ec8); `+0x6c = moveAngle`; if speed < 12 (DAT_00813f54): speed += 0.8 (DAT_008145e8) | age==100: `+0x6c` turns toward opponent ≤ 0.1 (DAT_00853b10); then `+0x6c = moveAngle = +0x6c + rand_f(−0.2, 0.2)` (DAT_00817c04) |
| phase 3 | age≥100: straight | 100<age<150: if speed < 14 (DAT_00813a54): speed += 1.0; moveAngle turns ≤ 0.03 rad/frame (DAT_00813ee0); `+0x6c = moveAngle`. age≥150: straight |
| expiry | age > 30 && outside arena+10 → call **onKilled** itself, remove | age > 120 && outside arena+10 → onKilled, remove |
Move after steering: `pos += dir(moveAngle)·speed`. Cosmetic RNG users: homing — smoke common 2000
every even age at `pos − dir(+0x6c)·20` (random angle), sparks 0xa28+n every frame for ages 20–24;
rocket — smoke every frame for age>100, sparks for ages 100–110.
onHit (hit a player, `0x881aa8`) → returns 0 (removed): 100: common 0x262 + 0x514, SE 0x14; 0x96: Sora
effect 100 at angle+π, common 0x514, SE 0x14. onKilled (shot down / expiry) → removed: common 0x262 +
0x514, SE **0x1d** (`gun12.wav`).
Draw: 100 → sprite 0x22 body frame (age/3)%3 (colour (1,v,v,v), v = 1 if grown else 0), glow frames 3
and 4 additive with alpha `sin((age%50)/50·2π)·0.15+0.85` / `·0.25+0.75`; exhaust 0x24 frames 1,0 at 42 px
behind (DAT_00824488), y-scale 0.3 on even age/2. 0x96 → sprite 0x23 frames `((age/3)%3)·3 + {0,1,2}`,
exhaust 64 px behind (DAT_008140f8). Body scale (3−2s, s, 1).

### 5.3 Laser: spawn FUN_00625910, update FUN_00626040, draw FUN_00625a10, onHit FUN_00625ed0
Only id **100** (Hyper Spread 0x16/0x17). Template (`FUN_006307d0` + fields): `blockMode +0x08 = 2`
(**cut at the first segment touching the enemy player**, pass 12), hitType 3 (dash doesn't dodge),
**damage +0x40 = 10 on every frame of contact** (never zeroed), knockback +0x44 = 5.0, +0x48 = 200,
stun 1. Age pre-incremented (first update sees 1).
Update: clear segments; `age > 39` → removed; age==1 → **common effect 100 (graze zone)** at origin,
angle (Note 3); radius = 20 (DAT_008140fc) for 10 ≤ age ≤ 30 else 0; push 60 segments k=0..59 at
`origin + dir(angle)·k·30` (DAT_008140e8), shape {0,0,radius}. Origin/angle fixed.
Per frame of contact (pass 16): hitstun(angle, 5) then Damage(10, type 3, 1.0) → up to 21 frames × 10.
onHit: on even ages spark 1000 + 5 streaks 0x456 at the segment (angle + k·0.65 + rand_f(−0.45,0.45)),
SE `(0x13,4)` (cosmetic, RNG).
Draw: atlas sprite 0x26 (`sr_laser`), per segment: k=0 src (448,64)-(512,127) half 60/zoom (+5 when
(age/3) even), additive; k>0: src (320,64)-(383,127) half (80, 50[+10 when (age/3) even]) scaled by
age/10 (age<10) or (40−age)/10 (age>30), alpha 32; plus src (384,64)-(447,127) half (120, 20), alpha
32 — all /zoom, rotated −angle, mode 2.

### 5.4 Hitbox: spawn FUN_005e9f20, onHit FUN_005ea560
One-frame boxes (objects.md §2.4): dealsDamage 1, oncePerFrame 0, stun 1.0. Generic pass: hitType 1
misses dashing targets, 3 hits them; callback before damage.

| Id | Spawned by | hitType | dmg | shape {hx,hy,r} | knock | on-hit |
|---|---|---|---|---|---|---|
| 100 | charge burst 0x136 ×5 | **1** | **70** (ver>0x58a; else 80) | {0,0,40} (DAT_008140e0) | 5 | 6 streaks 0x456 + 0x516, SE 0x13 |
| 200 | Sword 1 | 3 | 180 | {100, 80, 0} (DAT_00853b90, DAT_00814164) | 5 | spark 0x9c4 (rand angle), 0xaf0; **hit-confirm**; SE (0x16,3) |
| 0xd2 | Sword 2 | 3 | 60 (0x58b: 80) | {0,0,100} | 5 | 0x9c4; **hit-confirm**; SE (0x16,3) |
| 0xdc | Sword 3 | 3 | **140** (ver>0x58a; else 120) | {120, 80, 0} (DAT_00814174) | 5 | 0x9c4, 0xaf0; SE 0x17 |
| 0xe6 | Slash Through | 3 | 240 | {160, 80, 0} (DAT_00814168) | 5 | 0x9c4, 0xaf0, 3× Sora effect 0x6e; **hit-confirm**; SE (0x16,3) |
| 0xfa | Boisterous opener | 3 | 100 | {160, 80, 0} | 5 | 0xaf0; **hit-confirm**; camera focus `FUN_005f8fc0(70, Sora id, Sora pos, 0.4)`; SE (0x16,3) |
| 0xfb | Boisterous ×2 | 3 | 50 | {0,0,100} | **2.5** (DAT_008153e0) | 0x9c4 at box angle; SE (0x16,3); **Sora's `+0x98 *= 0.75`** |
| 0xfc | Boisterous ×3 | 3 | 50 | {0,0,100} | 2.5 | as 0xfb + 0xaf0 |
| 0x104 | Boisterous finisher | 3 | 200 | {160, 80, 0} | 5 | 0x9c4, 0xaf0; SE 0x17; shake `FUN_004d7ac0(10, 0.75, 1)` |
| 300 | Unlimited Charge circle (damage tick) | 3 | 110 | {0,0,320} (DAT_008178c0), r overwritten by state | 5 | **box angle := atan2(box − victim)** (knockback pulls victim to the centre); 6 streaks; SE (0x13,4) |
| 0x12d | UC circle (other frames) | 3 | 0 | {0,0,320} → overwritten | 5 | same pull; SE (0x13,4) |
| 0x15e | UC wave (every 4th age) | 3 | 160 | {0,0,320} | 10 | 6 streaks; SE (0x13,4) |
| 0x15f | UC wave (other ages) | 3 | 0 | {0,0,320} | 10 | SE (0x13,4) |
"hit-confirm" = owner (`FUN_00780450(box team)`) `+0x158 = 1`. 0-damage boxes still add stun 1.0 →
hitstun every frame. Callback effect calls consume RNG in the order written.

### 5.5 Enemy family — none (Sora has no 0x881558/0x8815a8/0x8815f8/0x881648 entries).

### 5.6 Sora effects: spawn FUN_005070d0, update FUN_00508d60, draw FUN_00507530 (`DAT_008816a4`)
Effect record (0x44): +0 type 3, +4 id, +8 colour, +0xc owner, +0x10 team, +0x14 flag, +0x18 pos,
+0x24 =1, +0x28 age (pre-incremented by the list), +0x2c/+0x30/+0x34 per id, +0x38 angle, +0x3c, +0x40.
| Id | Gameplay? | Behaviour |
|---|---|---|
| **0x15e** | **YES** | Init +0x2c=+0x30=+0x34=1; state sets `+0x3c = 1000`. Each update: `pos += dir(angle)·24` (DAT_00853b8c); `+0x40 = 0.85` (even age) / 1.0; **hitbox `age%4==0 ? 0x15e : 0x15f`** at pos, angle, owner/team of the effect; then `age > 79` → removed; else 2 particles 0x7da (RNG: angle rand_f(−π/2, π/2), offset (cos·160−40, sin·300) rotated). First update age 1 → hitboxes at ages 1..80 (20 damaging ticks at ages 4,8,…,80; travels 1920 px). |
| 100 | no | flash (from missile 0x96 onHit, …): grows until age 100 then fades; spawns effect 0x6e every 5 ages ≤100 (RNG) |
| 0x6e | no | spark (random +0x34), fades after age 20 |
| 200 / 0xd2 / 0xdc | no | beam 100 / 200 / 300 bodies; fade 0.1/frame after age 5 / 2 |
| 300 | no | UC ring, alpha −0.35/frame (DAT_00813f2c); draw size `+0x3c·+0x40` |

Effect draw FUN_00507530 (all `+0x68` 2D blits: dst rect half/zoom around the screen point, src rect,
bam(−rotation), ARGB, blend), `v` = variant (colour index):
- 100: sheet `(&DAT_0087fccc)[v/8]` = `explosion0`/`explosion1`; cell x = ((v%8)/4)·256, y = ((v%8)%4)·128;
  left half (0x80 wide) half-size 210·scale (DAT_008178d8), alpha 128·a; right half 240·scale (DAT_00815584,
  ×0.85 on even age), alpha 255·a; both blend 2, angle passed **without** the sign flip.
- 0x6e: sprite 0x25 frame 2 (`sr_exp02`), team slot, half (300·s, 300·s·+0x34) (DAT_008145f0), src 0..255,
  ARGB = (64·a, g, g, g) with g = 255·+0x40, blend 1.
- 200 / 0xd2 / 0xdc: `beamemitb` (DAT_0087fdf4) cell x = (v/8)·0xf0, y = (v%8)·0x40, 0x78×0x40 wide:
  body half (60, H1) alpha 32·a(·+0x34) blend 1; then age<2: `beamemit` (DAT_0087fdf0) cell x = (v/8)·0x100,
  half (48, H3) white blend 2; else right cell (x+0x78) half (40, H2) alpha 255·a(·a), rgb white while
  age < F else 0x0080ff, blend 2. 200: H1 60, H2 10, H3 18, F 6; 0xd2: 50s, 8s, 16s, F 6; 0xdc: 60s, 40s,
  64s, F 4 (DAT_00813a58, DAT_00813f4c, DAT_00814144, DAT_008140ec, DAT_008140d4, DAT_008140d8,
  DAT_008140e0, DAT_008140f8).
- 300 / 0x15e: `+0x60` blit (no rotation) of frames 0 and 1 of the sprite id held in the draw global
  DAT_00895abc — only the 0x6e case writes it (0x25), so it is a stale value (assumed `sr_exp`, UNVERIFIED
  initial value); half `+0x3c·+0x40`, alpha 64·a then 255·a, blend 2.

### 5.7 RNG
Gameplay draws: 0x13 spread, 0x16/0x17 laser offsets (drawn even in new version), 0x1e homing
offsets/angles, 0x1f/0x20 rocket r/d (drawn even when overridden), rocket age-100 jitter. Cosmetic
draws share the engine (sparks in 0x18/0x1a/0x1b/0x1c, UC particles, missile smoke/sparks, hit
effects, effect 0x15e particles) — replicate in order for sync.

---

## 6. Sound ids (SE table base 0x9b99a0, all.c:464010+)
| id | file | where |
|---|---|---|
| 6 | up01.wav | hyper start |
| 7 | high_a.wav | Unlimited Charge |
| 0xd | _sen_mi_b_raifuru.wav | Beam Rifle, Rifle Spread |
| 0xe | tm2_gun000.wav | Charged shot (0x14, 0x15) |
| 0x12 | gun11_r.wav | Hyper Spread lasers (lock 6) |
| 0x13 | don16.wav | beam / burst / laser / UC hits |
| 0x14 | bom06.wav | missile hits player |
| 0x16 | hit_s.wav | sword / Boisterous hits |
| 0x17 | hit_f.wav | Sword 3, finisher hits |
| 0x1d | gun12.wav | missile shot down / expired |
| 0x1f | swing.wav | Sword 1/2, Boisterous |
| 0x20 | swing_h.wav | Sword 3, finisher |
| 0x21 | swing_d.wav | Slash Through |
| 0x22 | burst00.wav | missile launch (Hyper Launcher lock 4) |
| 0x2a | A5_01029.wav | charge complete |

---

## 7. Other per-character table entries (index 3)
| Table (real base + 4*3) | Entry | Role |
|---|---|---|
| 0x88131c | FUN_0073b470 | input dispatch std (§2) |
| 0x88136c | FUN_0073b930 | input dispatch alt (§2) |
| 0x8813bc | FUN_007333a0 | sprite loader (§4.1) |
| 0x881430 / 0x881434 / 0x881438 | FUN_0073c750 (level 0), FUN_0073bdf0 (levels 1, 2) | CPU AI (not reversed) |
| 0x881884/0x8818d4/0x881924/0x881974 | 4b3fe0 / 4b47c0 / 4b4310 / 4b4590 | beam (§5.1) |
| 0x8819c4/0x881a14/0x881a64/0x881ab4/0x881b04 | 677970 / 679630 / 677b20 / 679420 / 679580 | missile (§5.2); absorb 0x881b54 = 0 |
| 0x881ba4/0x881bf4 | 5e9f20 / 5ea560 | hitbox (§5.4) |
| 0x881c44/0x881c94/0x881ce4/0x881d34 | 625910 / 626040 / 625a10 / 625ed0 | laser (§5.3) |
| 0x8816a4/0x8816f4/0x881744 | 5070d0 / 508d60 / 507530 | Sora effects (§5.6) |
| enemy, whip tables | — | none |

---

## 8. Stats record (index 3, base 0x882814; registry.c:1304-1346)
| Off | Value | Meaning |
|---|---|---|
| +0x00 | 5.0 (DAT_00813f40) | walk speed per axis |
| +0x04 | 15.0 (DAT_00853b88) | dash speed |
| +0x08 | 12.0 (DAT_00813f54) | UNVERIFIED (same as Suguri) |
| +0x0c..+0x2c | 9, 8, 0, 6, 4, 0, 9, 8, 0 | per-palette effect colour slots, 3 per palette (A {9,8,0}, B {6,4,0}, C {9,8,0}) → `DAT_008859f4 + team*0x10 + 4k` (FUN_005f5780, found by the Alte port) |
| +0x3c/+0x40/+0x44 | 0x0f, 0x10, 0x11 | sprite ids (rng / unregistered / system) |
| +0x48/+0x60/+0x78 | text 0x20/0x21/0x22 | colour-variant folders |
| +0xa8 / +0x1a8 | `mdl\win_font_SR.lfd/.dat` | win font |
| +0x2a8 | `sr_res_bgp` | |
| +0x2c0..+0x2c8 | 0, 0, 0 | UNVERIFIED |
| +0x2cc..+0x2d4 | 170, 240, 70 | colour RGB |
| +0x2d8 | 0 (0x882aec) | |

---

## 9. UNVERIFIED / open items
- Shared with suguri.md: button encoding, hyper screen-effect visuals, stats +0x08 / int block,
  `+0x24/+0x28` up/down.
- `+0x108` naming (barrier-break threshold) — evidence FUN_006afac0 t=5 and FUN_006af880; core.md.
- Whether the graze zones spawned by laser age 1 / state 0x1d are intended (they are what the code
  does; port as-is).
- Sprite roles of 0x0f, 0x11 (HUD; not drawn by sim code). 0x25 = effects 0x6e/300/0x15e, 0x27 =
  charged-shot beam (resolved, §5.1/§5.6).
- Effects 300/0x15e use a stale draw-global sprite id (DAT_00895abc); before any 0x6e was drawn it is
  the BSS initial value 0 (sprite 0 of the slot — nothing registered), so the very first UC of a session
  may draw nothing in the original. Port assumes 0x25.
- CModel colour (vtable +0xdc, FUN_0046ef70) takes **(a, r, g, b, mesh)** — it stores param 2 as alpha
  (D3DCOLORVALUE {r=p3,g=p4,b=p5,a=p2}); render.md's "(r,g,b,a)" is wrong (missile draw: body (1,v,v,v) =
  opaque, black until grown; glow layers (pulse,1,1,1) = alpha pulse).
- Exact visuals of `FUN_005f8fc0(70, …, 0.4)` camera focus on Boisterous opener (objects.md §6.3
  describes the mechanism: zoom target 0.4 for 70 frames).
