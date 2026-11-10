# Mira — complete moveset spec

Source: `aos2.exe` (Ghidra corpus `tools/re/dec/all.c`, constants read with `xref.exe rd`, disassembly
with `xref.exe dis`). Every number cites its address. `t` = player state timer `p+0x154`. Structure and
naming follow suguri.md (shared helpers FUN_007807f0 cost table, FUN_006b1b40 hyper start, FUN_00780910
hyper spend, FUN_006b17d0 dash, `+0x158` hit-confirm, ammo `+0xe8/+0xec` are described there and not
re-derived). Object struct fields follow objects.md (type = engine index **6**). Version: objects.md §0.1
— live play has `DAT_0089426c = 0x5a2`; where code branches on it, **the v>0x5a0 / v>0x58a value is
given and is the one to implement** (older values in parentheses).

Engine index 6 (handbook id 3). States 0x7c–0x9d. Sprite prefix `mr_`.

---

## 0. Notes for the lead (cross-character findings)

1. **CModel vtable +0xdc colour argument order is (a, r, g, b), not (r, g, b, a)** as render.md says.
   `FUN_0046ef70` copies `param_3,param_4,param_5` into the first three floats of the D3D material
   colour (r,g,b) and `param_2` into the 4th (a). Evidence in use: Mira's silhouette draws call
   `+0xdc(1.0, 0, 0, 0)` (opaque black: 0x95 T1 FUN_00700df0, enemy 300 draw), chakram fade calls
   `+0xdc(a, a, a, a)`. The global tint `FUN_004e8f50` passes `DAT_00893da0..dac` in the same order, so
   `0x893da0` is alpha. Please fix render.md.
2. **Cost-table entry for state 0x88 (Gleaming Caltrops) is never written** (no `DAT_0087cb50..58`
   writer in registry.c or anywhere in all.c; the BSS stays 0), so `FUN_007807f0(p,0x88)` always
   succeeds, costs nothing and gives no hyper. Mira's D+A is free. (Probably true for other
   characters' unlisted states too — check each character's ids.)
3. **Global static `DAT_00880028`** is written by Mira's 0x95/0x96 at t=0 and read every frame of
   the state (lunge speed). It is shared by both players (mirror match on the same frame would
   clobber it; bug-compatible port = one global, simple port = per-player).
4. **Enemy objects see the owner's timer already incremented.** Player update (incl. `t++`,
   all.c:380847) is step 1 of the tick, Enemy update step 2 (objects.md §1.3). Mira's chakrams read
   `owner.state/owner.t` (Swift Chakrams), so they see `t_handler+1`. Any character whose summons
   read the owner timer has this off-by-one.
5. **Common KO state 6 T2 (`FUN_006b0110`) calls a per-character T2 hook** `switch(p+0x10)` → for
   engine index 6 it calls `T2[0x7e]` (Mira's KO hook fades her chakrams, §3). Cases exist for
   4,5,6,7,8,0xd,0xf. Common neutral T2 `FUN_006af390` likewise calls `T2[0x7c]` (case 6).
6. objects.md UNVERIFIED #6 settled: **whips are purely visual** (`FUN_007c6570` only records a
   ribbon behind a missile found by serial; no collision pass touches the Whip list).
7. **Enemy `+0x10`** = per-type sub-id (the spawn id, rewritable); Mira also uses `+0x7c` as the
   behaviour selector, `+0x84` as sub-phase and `+0x80` as phase timer. Mira's "chakrams deployed"
   test scans the Enemy list for `type==6 && +0x10==100 && team==side` (§1.2).
8. Player field **`+0x90`** is used by Mira 0x91 as a private self-propulsion speed (applied in the
   T2, independent of `+0x94/+0x98`). core.md should list it.
9. `FUN_006b1a40(p, k, frames)` = apply status flag `p+0xa3+k` with timer `p+0xac+4k = max(timer,
   frames)` (skipped when p is in state 6 or `+0xa2==0`; SE 0x26 for k=1,2,7, SE 0x15 for k=3 on
   first application). Mira's smoke uses k=4 (the `+0xa7` ×0.25 movement slow, suguri.md §7 table).
10. `FUN_00780a50(p)` = "can be forced into hitstun": `+0xf8==0 && state!=6 && +0xa2!=0 && +0xa9!=1
    && DAT_00885964!=2`. Mira uses it to freeze the opponent (`state=1, t=0, +0x98=0`) in combo
    follow-ups.
11. `FUN_0061c730(&p->dir, target)` = 1 iff a direction is held and `|wrap(target − inputAngle)| < π/2`
    (FUN_005f0f60 with step 0 returns the signed difference).
12. Dispatch D+A row tests Dash `p+0x34 > 1` (held ≥ 2), unlike Suguri's `> 0`.
13. FUN_00617110 (round-end quote picker) special-cases opponent index 6 and consumes RNG
    (`FUN_00459f80`) — not gameplay, but matters for RNG-exact replays.

### Player fields used by Mira's code (beyond suguri.md's table)
| Off | Use |
|---|---|
| +0x5c/+0x60/+0x64 | hurtbox Shape; 0x95 zeroes it for t<11 (fully intangible) |
| +0x90 | 0x91 private dash speed (×0.95/frame) |
| +0xa2 | hittable; 0x9b sets 0 every frame |
| +0xa7/+0xbc | slow status (set on the opponent by smoke clouds via FUN_006b1a40(opp,4,16)) |

---

## 1. Shared mechanics

Frame model, dash cancel (`t>=N && Dash==3 → FUN_006b17d0(p,1)`), `FUN_00612830/00612900`
(training display only), aim (`+0x84 = FUN_007804b0(p)`), sparks, hyper start/permission: exactly as
suguri.md §1. Dash speed for Mira = stats+4 = **12.0** (§8).

### 1.1 Ammo / hyper cost table (`0x87c4f0 + id*0xc = {costA, costB, hyperGain}`), registry.c:1524-1554, ver patch FUN_004a7270 (all.c:130691-130693)
| State | Move | costA | costB | hyperGain |
|---|---|---|---|---|
| 0x86 | Ninja Stars (per pair) | 0.15 (DAT_00813f20) | 0 | 0.05 (DAT_00813ef4) |
| 0x88 | Caltrops (per star) | **0** (no entry, see §0.2) | 0 | 0 |
| 0x89 | Kunai 1 | 0 | **0.2** (v>0x5a0, DAT_00853b14); old 0.3 (DAT_00813f28) | 0.1 (DAT_00853b10) |
| 0x8a | Kunai 2 | 0 | **0.2**; old 0.3 | 0.05 |
| 0x8b | Kunai 3 | 0 | **0.2**; old 0.3 | 0.05 |
| 0x8c | Counterattack Kiri (per pair) | 0 | 0.15 (DAT_00813f20) | 0.05 |
| 0x8d | Binding Smoke | 0 | 0.3 (DAT_00813f28) | 0.075 (DAT_00813efc) |
| 0x90 | Soaring Chakram launch (also 0x93's launch) | 0 | 0 | 0.05 |
| 0x91 | Swift Chakrams (per chakram per frame) | 0.03 (DAT_00813ee0) | 0 | 0.005 (DAT_00813eac) |
| 0x97 | Quick Slash 1 | 0 | 0 | 0.05 |
| 0x98 | Quick Slash 2 | 0 | 0 | 0.025 (DAT_00813edc) |
| 0x99 | Quick Slash 3 | 0 | 0 | 0.035 (DAT_00813ee4) |
Hypers (0x8e, 0x8f, 0x92–0x94, 0x9a–0x9d) and 0x95/0x96 never call FUN_007807f0.

### 1.2 How "chakrams deployed" is tracked — `FUN_006f5c00(side)`
There is **no player flag**. `FUN_006f5c00` walks the Enemy vector (`FUN_0051b1a0` count,
`FUN_004ef290(i)` element) and returns the index of the **first** Enemy with `team(+0x14) == side`,
`type(+0xc) == 6` and `id(+0x10) == 100`, else −1. Enemy id 100 = a chakram spawned by Circling
Chakrams (0x8f) that has not been launched. It counts from the moment it is spawned (still growing
out, before it becomes shootable).
- **Set**: 0x8f spawns 4 Enemy-100 chakrams.
- **Cleared** (per chakram): launching rewrites `+0x10` to 0xb4 (0x90 Soaring, 0x93 Baiting) or
  0xbe (0x92 Sky-Soaring); being shot down (HP 500 ≤ 0 → onKilled FUN_005253c0 returns 0 →
  erased). Chakrams have no timeout while orbiting.
- KO hook 0x7e changes only `+0x7c` (behaviour) to fade them; `+0x10` stays 100 (irrelevant, Mira is KO).
- The dispatch computes it once per frame before reading buttons (FUN_006fbb60 first line).

### 1.3 Helpers specific to Mira
| Function | Behaviour |
|---|---|
| `FUN_006f5c00(side)` | first deployed chakram index or −1 (§1.2) |
| `FUN_0061c730(&p+0x1c, target)` | input held and within ±90° of target (§0.11) |
| `FUN_00780a50(p)` | may be forced into hitstun (§0.10) |
| `FUN_006b1a40(p,k,n)` | status flag k for ≥ n frames (§0.9) |
| `FUN_005f15a0(a,b,c,d,out)` | intersection of line a–b with line c–d (Cramer's rule, no parallel check) |
| `FUN_007e1bee(x)` | `abs(int)` |
| `FUN_00780450(team)` | player of that team (= owner; used to set the owner's hit-confirm) |

---

## 2. Input dispatch `FUN_006fbb60` (standard scheme) — exact decode

`ch = FUN_006f5c00(p+0x14)` first (−1 = no chakram).

**A. Hypers** — only if `FUN_00780950(p)`. First match wins; if the cost check fails the next row is tried.
| # | Condition | Cost | New state | Official move |
|---|---|---|---|---|
| 1 | S held (`+0x40>1`) && B held (`+0x3c>1`) && H pressed (`+0x44==3`) | 3.0 (DAT_00813a50) | 0x9d | **Twin Dragon Tornado** (Accel) |
| 2 | S held && H pressed | 1.0 (DAT_00853b34) | ch==−1 → **0x9a**; else **0x93** | **Divine Speed Slash** / ★**Baiting Chakrams** |
| 3 | B held && H pressed | 1.0 | ch==−1 → **0x8f**; else **0x92** | **Circling Chakrams** / ★**Sky-Soaring Chakrams** |
| 4 | Dash held (`+0x34>1`) && H pressed | 1.0 | 0x8e | **Purgatorial Smokescreen** (D+H) |
| 5 | H pressed | 1.0 | 0x94 | **Wrathful Tornado** |

**B. Normal moves** — only if state 0 or 2:
| # | Condition | Result | Move |
|---|---|---|---|
| 1 | Dash pressed | `FUN_006b17d0(p,0)` | dash |
| 2 | Shield pressed && `FUN_00780910(p,0.5)` && `FUN_007809e0(p)` | `FUN_006b1930(p,0)` | shield |
| 3 | S held && B pressed | ch==−1 → **0x8d**; else **0x90** | **Binding Smoke** / ★**Soaring Chakrams** |
| 4 | S held && A pressed | ch==−1 → **0x95**; else **0x91** | **Quick Slash** / ★**Swift Chakrams** |
| 5 | B pressed | **0x8c** (→0x89 if released before t=13) | **Counterattack Kiri** (hold) / **Glittering Kunai** (tap) |
| 6 | Dash held (`+0x34>1`) && A pressed | **0x88** | **Gleaming Caltrops** |
| 7 | A pressed | **0x86** | **Scorching Beam Ninja Stars** |
Finally returns `T2[state](p) != 0`.

**What plain A does (absent from the PDF):** A tap = state 0x86, which fires **one pair** of curving
ninja-star beams (ids 200/0xc9) at t=12. Holding A at t=20 loops the timer back to 10, firing another
pair every 10 frames for as long as A is held and ammo A lasts (0.15/pair). The PDF's "A (Hold)
Scorching Beam Ninja Stars" is the same state; the tap is just one cycle of it.

**Alternate scheme `FUN_006fc090`** (`p+0xc==1`, table 0x881378): hypers become "H held (`+0x44>1`) and X
pressed": Shield→0x9d (3.0); S→0x9a/0x93; B→0x8f/0x92; Dash→0x8e; A→0x94 (1.0 each), same chakram
branches. Normal-move block identical.

Not in the manual: Kunai chain (B, B, B with short dashes), Quick Slash chain (S+A, then A on hit ×2),
hold-B in Soaring Chakrams launches the next chakram, Baiting Chakrams launches every deployed
chakram one by one.

---

## 3. States

T2/T1 per state (registry: T2 at 0x8809a0+4·id, T1 at 0x880030+4·id):

| id | T2 | T1 | Role |
|---|---|---|---|
| 0x7c | 006f69a0 | 006fd4d0 | neutral movement |
| 0x7d | 006f6a90 (ret 1) | 006fd790 | dash draw (common 2) |
| 0x7e | 006f6ac0 | 006fe140 | KO hook (common 6) |
| 0x7f | 006f6aa0 (ret 1) | 006fdb90 | hit draw (common 1, 5) |
| 0x80 | 006f6ab0 (ret 1) | 006fde60 | shield draw (common 3, 4) |
| 0x86 | 006f6be0 | 006fe440 | Ninja Stars (A) |
| 0x88 | 006f7020 | 006fe700 | Caltrops (D+A) |
| 0x89/0x8a/0x8b | 006f73a0 / 006f7560 / 006f7890 | 006fe9c0 / 006fec80 / 006ff000 | Kunai 1/2/3 |
| 0x8c | 006f7b50 | 006ff380 | B press / Counterattack Kiri |
| 0x8d | 006f8ab0 | 007005b0 | Binding Smoke |
| 0x8e | 006f8c80 | 00700870 | Purgatorial Smokescreen |
| 0x8f | 006f7de0 | 006ff640 | Circling Chakrams |
| 0x90 | 006f8010 | 006ff920 | Soaring Chakrams |
| 0x91 | 006f8240 | 006ffc00 | Swift Chakrams |
| 0x92 | 006f83d0 | 006fffb0 | Sky-Soaring Chakrams |
| 0x93 | 006f85f0 | 007002b0 | Baiting Chakrams |
| 0x94 | 006f8f90 | 00700b10 | Wrathful Tornado |
| 0x95 | 006f9540 | 00700df0 | Quick Slash approach |
| 0x96 | 006f96a0 | 007011d0 (empty) | Quick Slash dash-through |
| 0x97/0x98/0x99 | 006f9850 / 006f9d20 / 006fa190 | 007011e0 / 007014a0 / 00701760 | Quick Slash hits 1/2/3 |
| 0x9a | 006fa520 | 00701a40 | Divine Speed Slash |
| 0x9b | 006faa10 | 00701d00 | Divine Speed Slash clones |
| 0x9c | 006facc0 | 007020f0 | Divine Speed Slash finisher |
| 0x9d | 006fb0e0 | 007025e0 | Twin Dragon Tornado |
0x81–0x85, 0x87 have no handlers.

Spawn tables (base + 4·6): Beam `DAT_00881890 = FUN_004adc50`, Missile `DAT_008819d0 = FUN_0066e4b0`,
Hitbox `DAT_00881bb0 = FUN_005e5480`, Laser `DAT_00881c50 = FUN_0061fbb0`, Enemy `DAT_00881570 =
FUN_00523620` (`spawn(id, owner, team, slot)`, caller writes pos/angle), Whip `DAT_00881d90 =
FUN_007c5d50` (`spawn(id, slot, missileSerial)`), Mira effect `DAT_008816b0 = FUN_004fdfa0`, generic
effect `DAT_00881698`. Parameters: §5.

### 0x7c — neutral (T2 FUN_006f69a0)
Identical to Suguri 0x128: each axis held → ±stats+0 (5.0, DAT_00813f40) per frame, diagonals not
normalised; `+0x84 = 0` if facing==1 else π. Returns 1.

### 0x7e — KO hook (T2 FUN_006f6ac0, called by common state 6 T2 every frame)
At t==0 only: for every Enemy with `ownerId(+8) == p+4` and `+0x7c ∈ {100, 0xb4, 0xb9, 0xbe}`:
`+0x7c = 0xb4, +0x84 = 900, +0x80 = 0, active(+0x2c) = 0` → all chakrams fade out (§5.5 phase 900).

### 0x86 — Scorching Beam Ninja Stars (T2 FUN_006f6be0) — A
| t | Action |
|---|---|
| 0 | aim; `+0x88 = aim` |
| 10–21 | free movement at stats+0 × 0.75 (DAT_00813f3c) = 3.75 per axis (no friction change) |
| 12 | if `FUN_007807f0(p,0x86)` (A −0.15): re-aim; `s = FUN_0061c730(input, aim) ? 1.0 (DAT_00853b34) : 1.5 (DAT_00814110)`; beam **200** at pos, angle aim − s; beam **0xc9** at pos, angle aim + s; SE 0xd. (A third branch `s = 2.0` (DAT_00813a4c) tests `FUN_0061c730(input, wrap(aim))` — identical to the first test, so it is **dead code**; disasm 0x6f6d70–0x6f6e4e.) |
| 20 | if A held (`+0x38>1`) → **t := 10** (repeat; next pair at t=12) |
| ≥20 | (when not looping) dash-cancel; 612900 at t==20 |
| >35 | state 0 |
So "forward held" makes the stars start ±1 rad from the aim; otherwise ±1.5 rad. They curve back inward (§5.1).

### 0x88 — Gleaming Caltrops (T2 FUN_006f7020) — Dash held + A
| t | Action |
|---|---|
| 0 | aim, `+0x88=aim` |
| 10–21 (old 8–20) | each frame: if `FUN_007807f0(p,0x88)` (free): re-aim; beam **300** at pos, angle aim + off(t); `FUN_007bcaf0(0xf,4)` |
| every frame | friction `+0x9c *= 0.5` (keeps dash momentum) |
| ≥25 | dash-cancel; >40 state 0 |
off(t) (v>0x5a0 switch): 10: −0.54 (DAT_00825914), 11: +0.54 (DAT_008258fc), 12: −0.33 (DAT_0082590c), 13: +0.33 (DAT_008258f4), 14: −0.12 (DAT_00825908), 15: +0.12 (DAT_00824a8c), 16: +0.09 (DAT_00813f0c), 17: −0.09 (DAT_00825904), 18: +0.3 (DAT_00813f28), 19: −0.3 (DAT_0081fa88), 20: +0.51 (DAT_008258f8), 21: −0.51 (DAT_00825910); (cases 8: −0.75, 9: +0.75, default +0.72 unreachable). **12 caltrops.** Old versions: random `rand_f(−0.75, 0.75)` instead.

### 0x8c — B press / Counterattack Kiri (T2 FUN_006f7b50) — B
| t | Action |
|---|---|
| 0 | aim |
| <13 and B not held (`+0x3c<1`) | re-aim; state = **0x89**; if t>10 → t := 10 (timer otherwise kept); run 0x89 now |
| 15, 17, 19, 21 (k=0..3) | if `FUN_007807f0(p,0x8c)` (B −0.15): missile **200** at pos, angle aim + 1.4 + 0.2k (DAT_00817dd0, DAT_00853b14) + whip 1 on it; missile **200** at aim − 1.4 − 0.2k + whip 1; `FUN_007bcaf0(0x23,4)` → **8 kiri** |
| ≥25 | dash-cancel; >35 state 0 |
No re-aim after t=0 (except on the release branch).

### 0x89 — Glittering Kunai 1 (T2 FUN_006f73a0) — via 0x8c early release
| t | Action |
|---|---|
| 12 | if `FUN_007807f0(p,0x89)` (B −0.2): for k=−1,0,1: missile **100** at pos, angle aim + 0.15k (DAT_00813f20) + whip 0 on it; SE 0x1f. (aim = the one set on release; no re-aim) |
| ≥12 and B pressed and `FUN_007807f0(p,0x8a)` | state **0x8a** (t=0, flag=0), run it (cost paid here) |
| ≥25 | dash-cancel; >40 state 0 |

### 0x8a — Kunai 2 (T2 FUN_006f7560)
| t | Action |
|---|---|
| 0 | if a direction is held: `+0x94` = input angle, `+0x98 = 16` (DAT_008140d8), heat `+0xe0 += 0.2` (v>0x5a0, DAT_00853b14; old 0.3), SE 8; then aim, `+0x88=aim` |
| every frame | dash sparks if speed ≥ 3.0 (DAT_00813a50), dir `+0x94` (suguri.md §1.3 spark recipe) |
| 12 | re-aim; 3 kunai (missile 100 at aim+0.15k, whip 0), SE 0x1f — **no cost check** |
| ≥12 and B pressed and `FUN_007807f0(p,0x8b)` | → **0x8b** |
| ≥30 | dash-cancel; >40 state 0 |

### 0x8b — Kunai 3 (T2 FUN_006f7890)
Same entry dash and sparks as 0x8a; t=12 re-aim + 3 kunai (no cost check), SE 0x1f; dash-cancel ≥20; >30 state 0.

### 0x8d — Binding Smoke (T2 FUN_006f8ab0) — S+B, no chakram
| t | Action |
|---|---|
| 0 | aim |
| 10 | if `FUN_007807f0(p,0x8d)` (B −0.3): Enemy **200** (smoke ball): pos = player pos, `+0x5c = aim`, `+0x70 = 10` (DAT_00813f4c); SE 0x1f |
| ≥20 (old 15) | dash-cancel; >35 (old >30) state 0 |

### 0x8e — Purgatorial Smokescreen (T2 FUN_006f8c80) — D+H, 1 level
| t | Action |
|---|---|
| 0 | aim; `FUN_006b1b40(p,10)`; SE 6; `FUN_004e9280(100,0.75)`; `FUN_007bec20(100,pos,0,0)`; `FUN_004d7ac0(20,0.5,2)`; effect 0xfa |
| 10+3k, k=0..7 (10,13,…,31) | Enemy **200** at pos; `r = rand_f(−0.75,0.75)` (DAT_0081fa78, DAT_00813f3c; **always drawn**); v>0x5a0: `+0x5c = (k%4)/4·2π` if t<20 else `(k%4)/4·2π + 1.0`; (old: `(k%4)/4·2π + r`); `+0x70 = 10` → **8 smoke balls** in two crosses |
| ≥40 | dash-cancel; >60 state 0 |

### 0x8f — Circling Chakrams (T2 FUN_006f7de0) — B+H, no chakram, 1 level
| t | Action |
|---|---|
| 0 | aim; hyper start + flourish exactly as 0x8e (incl. effect 0xfa) |
| 5 | 4 × Enemy **100** (chakram): pos = player pos, `+0x5c = k/4·2π` (k=0..3; DAT_00853b68 = 4), `+0x70 = 0`, `+0x74 = 0` |
| ≥40 | dash-cancel; >50 state 0 |

### 0x90 — Soaring Chakrams (T2 FUN_006f8010) — S+B with chakram
| t | Action |
|---|---|
| 0 | aim |
| 12 | `ch = FUN_006f5c00(side)`; if ch == −1 or `!FUN_007807f0(p,0x90)` → **t := 15**. Else launch chakram ch: `+0x10 = 0xb4, +0x7c = 0xb4, +0x80 = 0, +0x84 = 0`, `+0x5c = atan2(opp − chakram)`, `+0x70 = −16` (DAT_00825918); SE 0x20 |
| 14 | if B held (`+0x3c>1`) → **t := 6** (so the next deployed chakram launches at t=12 again) |
| ≥15 | dash-cancel; >30 state 0 |

### 0x91 — Swift Chakrams (T2 FUN_006f8240) — S+A with chakram
| t | Action |
|---|---|
| 0 | aim; `+0x90 = 0`; `+0x98 = 0` |
| 8 | `+0x90 = 20` (DAT_008140fc); heat += 0.5 (DAT_00853b1c); SE 0x20 |
| every frame | `pos += (cos,sin)(aim) · +0x90`; `+0x90 *= 0.95` (_DAT_00814124) — Mira flies at the opponent carrying the orbiting chakrams |
| ≥40 | dash-cancel; >50 state 0 |
The attack itself is done by each orbiting chakram while the owner is in 0x91 (§5.5 behaviour 100):
hitbox 100 every frame, faster spin, and beam bursts (3 × 0x6e) on frames where the chakram sees
`owner.t ∈ [12,18]` (handler t 11..17, see §0.4), each burst costing `FUN_007807f0(owner,0x91)`.

### 0x92 — Sky-Soaring Chakrams (T2 FUN_006f83d0) — B+H with chakram, 1 level
| t | Action |
|---|---|
| 0 | aim; `FUN_006b1b40(p,10)`; SE 6; `FUN_004e9280(100,0.75)`; `FUN_007bec20(100,pos,0,0)`; `FUN_004d7ac0(20,0.5,2)` (**no** effect 0xfa) |
| 1 | for every deployed chakram (re-searching after each): `+0x10 = +0x7c = 0xbe, +0x80 = +0x84 = 0`, `+0x5c = atan2(chakram − player)` (straight outward), `+0x70 = −16` |
| ≥40 | dash-cancel; >50 state 0 |

### 0x93 — Baiting Chakrams (T2 FUN_006f85f0) — S+H with chakram, 1 level
| t | Action |
|---|---|
| 0 | aim; hyper start; SE 6; `FUN_004e9280(100,0.75)`; `FUN_007bec20(100,pos,aim,π/4)` (DAT_0081fa90); `FUN_004d7ac0(20,0.5,2)` (no 0xfa) |
| 10,15,…,35 (9<t<36, t%5==0) | new Enemy **100** (bait chakram): draws `R = rand_f(100,150)` (DAT_00853b90, DAT_0081414c) and `φ = rand_f(−0.5,0.5)+π/2` (both **always drawn**); v>0x5a0: `R = 125` (DAT_0081cc70), `φ = π/2`; if t%10==0: `φ *= −1`. pos = player + (cos φ, sin φ)·R; `+0x10 = 0xb4` (not "deployed"), `+0x7c = 0xb9`, `+0x84 = 0` (fade-in), `+0x5c = aim`, `+0x70 = −16`, `+0x74 = 0.5` → 6 baits alternately below (t=10,20,30) and above (15,25,35) |
| 40 | if a deployed chakram exists and `FUN_007807f0(p,0x90)`: it becomes `+0x10=0xb4, +0x7c=0xb9, +0x80=0, +0x84=100` (already active homing phase), `+0x5c = atan2(opp − chakram)`, `+0x70=−16`, `+0x74=0.5`; SE 0x20; **t := 35** (→ one deployed chakram launched every 5 frames until none left; t=35 is not re-run because t is incremented after the handler) |
| ≥50 | dash-cancel; >80 state 0 |

### 0x94 — Wrathful Tornado (T2 FUN_006f8f90) — H, 1 level
| t | Action |
|---|---|
| 0 | aim; hyper start + flourish as 0x8e (with 0xfa) |
| 15 | `a` = input angle if a direction is held, else `atan2(opp − player)`. Line L1 through `(cos a, sin a)·1000` and `−(cos a, sin a)·1000` (DAT_00814408; i.e. through the arena centre along a); line L2 through `opp + (cos,sin)(a+π/2)·1000` and `opp + (cos,sin)(a−π/2)·1000` (through the opponent, perpendicular to a). origin = `FUN_005f15a0(L1, L2)` = the opponent projected onto L1. Laser **100** at origin, angle **a + π/2** (so the tornado line runs through the opponent, perpendicular to a) |
| ≥70 | dash-cancel; >100 state 0 |

### 0x95 — Quick Slash approach (T2 FUN_006f9540) — S+A, no chakram
| t | Action |
|---|---|
| 0 | aim; `DAT_00880028 = min(dist/12, 14)` (DAT_00813f54, DAT_00813a54); `+0x88 = +0x94 = aim`, `+0x98 = DAT_00880028` |
| 0–10 | `+0x94 = aim`, `+0x98 = DAT_00880028` (re-written every frame → constant speed); **hurtbox `+0x5c/+0x60/+0x64 = 0`** (intangible) |
| 11 | FUN_00612830; state **0x97** (t=0, flag=0), run it |
No dash-cancel window. Drawn as a black silhouette (§4).

### 0x97 — Quick Slash 1 (T2 FUN_006f9850)
| t | v>0x5a0 (old) |
|---|---|
| 0 | aim; `+0x94 = aim`, `+0x98 = 0` |
| 1 (5) | SE 0x1f |
| 2 (6) | if `FUN_007807f0(p,0x97)`: hitbox **300** at pos + (cos,sin)(aim)·20 (DAT_008140fc), angle aim |
| ≥8 (≥12) | flag158 && A pressed → opponent: if `FUN_00780a50(opp)` → `opp.state=1, opp.t=0, opp+0x98=0` (frozen in hitstun); Mira → **0x96** |
| ≥31 (≥35) | dash-cancel; >36 (>40) state 0 |

### 0x96 — Quick Slash dash-through (T2 FUN_006f96a0)
| t | Action |
|---|---|
| 0 | aim; `+0x94 = aim`, `+0x98 = 0`; `DAT_00880028 = dist/10` (DAT_00813f4c; no cap); Enemy **300** (afterimage) at pos with `+0x58 = +0x5c = aim`; SE 0x2c |
| 0–10 | `+0x94 = aim`, `+0x98 = 5 + DAT_00880028` (DAT_00813f40) |
| 11 | FUN_00612830; → **0x98**, run it |
Mira is **not drawn** in this state (T1 FUN_007011d0 is empty); she travels 11·(5 + d/10) px, i.e. past the opponent.

### 0x98 — Quick Slash 2 (T2 FUN_006f9d20)
| t | v>0x5a0 (old) |
|---|---|
| 0 | aim; lunge `+0x98 = min(dist/15, 10)` (DAT_00853b88, DAT_00813f4c), `+0x88 = +0x94 = aim` |
| 1 (5) | SE 0x1f |
| 4 (8) | if `FUN_007807f0(p,0x98)`: hitbox **0x136** at pos + aim·20, angle aim |
| ≥8 (≥12) | flag158 && A pressed → **0x99** |
| ≥26 (≥30) | dash-cancel; >36 (>40) state 0 |

### 0x99 — Quick Slash 3 (T2 FUN_006fa190)
| t | Action |
|---|---|
| 0 | aim; `+0x88 = +0x94 = aim`; `+0x98 = 0` |
| 10 | re-aim; `+0x94 = aim`; `+0x98 = 16` (DAT_008140d8); SE 0x20; then (v>0x5a0) if `FUN_007807f0(p,0x99)`: hitbox **0x140** at pos + aim·10 (DAT_00813f4c), angle aim. (old: at t=12) |
| ≥20 (old ≥40) | dash-cancel (returns immediately); 612900 at t==20 |
| >35 (old >55) | state 0 |
| every frame (not on the cancel frame) | friction ×0.5 |

### 0x9a — Divine Speed Slash (T2 FUN_006fa520) — S+H, no chakram, 1 level
| t | Action |
|---|---|
| 0 | aim; `FUN_006b1b40(p,10)`; SE 6; `FUN_004e9280(100,0.75)`; `FUN_004d7ac0(20,0.5,2)`; effect 0xfa (no 7bec20) |
| 8 | SE 0x1f |
| 10 | re-aim; `+0x98 = min(dist/12, 16)` (DAT_00813f54, DAT_008140d8); `+0x88 = +0x94 = aim` |
| 14 | hitbox **400** at pos (no offset), angle aim |
| 15–20 (v>0x5a0 only) | while flag158 == 0: hitbox **400** again each frame |
| every frame | sparks if speed ≥ 3.0: dir = aim + rand_f(−0.5, 0.5) (DAT_00853bb0, DAT_00853b1c) |
| 20 | if flag158: opp → frozen hitstun (if `FUN_00780a50`); camera focus `FUN_005f8fc0(80, opp+4, opp pos, 0.4)` (DAT_00813f30); `FUN_007bec20(100,pos,aim,0)`; state **0x9b** (t=0,flag=0); SE 0x2b; run it |
| ≥40 | dash-cancel; >60 state 0 |

### 0x9b — Divine Speed Slash clones (T2 FUN_006faa10)
| t | Action |
|---|---|
| every frame | `p+0xa2 = 0` (Mira not hittable) |
| 10,15,…,40 (9<t<41, t%5==0) | `r = rand_f(−π, π)` (DAT_00814100, DAT_00853b58); opp → frozen hitstun (if allowed); Enemy **400 + ((t−10)/5)%3** (400,401,402,400,…) at `opp.pos + (cos r, sin r)·80` (DAT_00814164); `+0x58 = π` if `opp.x <= e.x` else 0; `+0x5c = +0x58`; `FUN_007bcaf0(0x2f,4)` → **7 clones** |
| >60 | FUN_00612830; state **0x9c** (t=0); `FUN_006b1b40(p,10)` (hyper timer restarted); SE 0x2c; run it |
Not drawn after t≥20 (§4); no dash-cancel.

### 0x9c — Divine Speed Slash finisher (T2 FUN_006facc0)
| t | Action |
|---|---|
| 0 | `a = atan2(−opp.y, −opp.x)` (direction opp → arena centre); **teleport**: `pos = opp.pos − (cos a, sin a)·60` (DAT_00813a58) (60 px beyond the opponent, away from the centre); `+0x84 = π` if `opp.x <= x` else 0; `+0x88 = +0x84`; opp → frozen hitstun (if allowed) |
| 15 | aim; `+0x88 = +0x94 = aim`; `+0x98 = 16`; SE 0x20; hitbox **0x1a4** at pos, angle aim (v>0x5a0 at t=15; old t=17) |
| every frame | sparks if speed ≥ 8.0 (DAT_008140d4), dir aim + rand_f(−0.5,0.5) |
| ≥28 (old ≥40) | dash-cancel; >42 (old >60) state 0; friction ×0.5 each frame |

### 0x9d — Twin Dragon Tornado (T2 FUN_006fb0e0) — S+B+H, 3 levels (Accel)
| t | Action |
|---|---|
| 0 | `+0x84 = +0x88 = π` if `opp.x <= x` else 0; `FUN_006b1b40(p,0x14)`; SE 7; `FUN_004e9280(150,0.75)`; `FUN_007bec20(100,pos,aim,0)`; `FUN_004d7ac0(150,0.75,2)`; effect 0xfa |
| 15 | `a` as in 0x94. Laser **0x96** at the projection of opp onto the centre line along a, angle a+π/2; then `a' = a+π/2`: laser **0x96** at the projection of opp onto the centre line along a', angle a'+π/2 = a+π → two perpendicular tornado lines crossing at the opponent |
| ≥90 | dash-cancel; >100 state 0 |

---

## 4. Animation

### 4.1 Sprite registration (loader `FUN_006f2980`)
Arg = colour variant: 0 → folder text id 0x13, else 0x14 (`charaMiraA` / `charaMiraB`; only two
variants). Quad S×S via `FUN_00467d00(S,S,2,2,1.0,0)`, storage `0x885b80 + slot*0x3888 + id*0x18`.

| Sprite id | Pattern | Frames | S | Used by |
|---|---|---|---|---|
| 0x7c | `mr_std0%d` | 3 | 96 (DAT_00814118) | neutral |
| 0x81 | `mr_fwd0%d` | 3 | 96 | neutral moving toward |
| 0x82 | `mr_bck0%d` | 3 | 96 | neutral moving away; 0x95 silhouette |
| 0x7e | `mr_los0%d` | 6 | 96 | KO (0x7e), hit (0x7f) |
| 0x80 | `mr_sld0%d` | 5 | 96 | shield; most hyper/chakram poses; enemy 300/400 |
| 0x7d | `mr_dsh0%d` | 6 | 96 | dash, 0x91 |
| 0x83 | `mr_rng0%d` | 2 | 512 (DAT_008178b0) | stats +0x3c (HUD ring, UNVERIFIED) |
| 0x85 | `mr_system` | 1 | 512 | stats +0x44 |
| 0x89 | `mr_atcka0%d` | 5 | 96 | 0x86, 0x88, 0x89, 0x8c, 0x90 |
| 0x8a | `mr_atckb0%d` | 4 | 96 | 0x8a |
| 0x8b | `mr_atckc0%d` | 5 | 96 | 0x8b |
| 0x97 | `mr_infghta0%d` | 4 | 96 | 0x97, 0x9a; enemy 400 |
| 0x98 | `mr_infghtb0%d` | 5 | 96 | 0x98; enemy 401 |
| 0x99 | `mr_infghtc0%d` | 6 | 96 | 0x99, 0x9c; enemy 402 |
| 0x9e | `mr_blade0%d` | 2 | 72 (DAT_008178ac) | beam 200 (ninja star), effect 400 |
| 0x9f | `mr_caltrap0%d` | 10 | 64 (DAT_008140f8) | beam 300 |
| 0xa0 | `mr_knife0%d` | 3 | 80 (DAT_00814164) | missile 100 (kunai) |
| 0xa1 | `mr_gimlet0%d` | 3 | 80 | missile 200 (kiri) |
| 0xa2 | `mr_chakram0%d` | 5 | 128 (DAT_00814134) | enemies 100/0xb4/0xb9/0xbe, effect 100 |
| 0xa3 | `mr_tor0%d` | 4 | 128 | lasers 100/0x96 |
| 0xa5 | `mr_smkball0%d` | 6 | 48 (DAT_008140e4) | enemy 200, effect 500 |
| 0xa4 | `mr_smk0%d` | 4 | 128 | effect 200 (smoke puff) |
| 0xa6 | `mr_impact%d` | 3 | 128 | missile 0x65 (stuck kunai) |
| (whip) | `mr_whip0`, `mr_whip1` | 1 each | strip textures | stored at `0x87ff38/0x87fe48 + k*4 + slot*0x3c` (k=0,1) |
All 77 `mr_*.png` in `extracted/texture/charaMiraA/` are covered (plus `res_*`). Ids 0x84, 0x86–0x88,
0x8c–0x96, 0x9a–0x9d are not registered.

### 4.2 Draw transforms
Same as Suguri (suguri.md §4.2): neutral = rotY(180°) if facing; every other handler = **AIM
transform** (rotZ(aim) [+rotX(180°) if |aim| ≥ π/2] + rotY(180°)). Global tint first, normal alpha.
Afterimage `FUN_004d7e00` only in 0x7c, 0x7d, 0x8a, 0x8b, 0x91, 0x95, 0x9c.
`pp(n)` = `(t/n)&3`, 3→1. `cyc(n)` = `n<2 ? n : ((n−2)&3)+2` with 5→3 (0,1,2,3,4,3,2,3,4,3…).

### 4.3 Common states → Mira draw handlers (case 6 of the common T1s)
| Common | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|---|
| T1 | 0x7c | 0x7f | 0x7d | 0x80 | 0x80 | 0x7f | 0x7e |
(FUN_006b07a0 / 6b0c40 / 6b09f0 / 6b0e90 / 6b10e0 / 6b1330 / 6b1580.)

### 4.4 Per-state frames
| State | T1 | Sprite | Frame | Notes |
|---|---|---|---|---|
| 0x7c | 006fd4d0 | 0x7c; y+→0x81; y−→0x82; facing0&x−→0x81; facing1&x−→0x82; facing0&x+→0x82; facing1&x+→0x81 (later wins) | pp(5) | as Suguri (disasm 0x6fd535–0x6fd5c5) |
| 0x7d | 006fd790 | 0x7d | pp(3), +3 if facing1&&\|aim\|>π/2 or facing0&&\|aim\|<π/2 | |
| 0x7e | 006fe140 | 0x7e | pp(3) + 3 when global frame `DAT_008858f8` even | drawn only while t<100 |
| 0x7f | 006fdb90 | 0x7e | pp(3) | |
| 0x80 | 006fde60 | 0x80 | cyc(t/5) | |
| 0x86, 0x88, 0x89, 0x8c | 6fe440, 6fe700, 6fe9c0, 6ff380 | 0x89 | min(t/4, 4) | |
| 0x8a | 006fec80 | 0x8a | min(t/4, 3) | |
| 0x8b | 006ff000 | 0x8b | min(t/4, 4) | |
| 0x8d | 007005b0 | 0x80 | min(t/5, 4) | |
| 0x8e | 00700870 | 0x80 | 0 | |
| 0x8f | 006ff640 | 0x80 | t<5 ? 0 : min((t−5)/5, 4) | |
| 0x90 | 006ff920 | 0x89 | t<6 ? 0 : min((t−6)/4, 4) | |
| 0x91 | 006ffc00 | t<8: 0x80 frame 0; else 0x7d | pp(3) | |
| 0x92, 0x93 | 006fffb0, 007002b0 | 0x80 | t<10 ? 0 : cyc((t−10)/5) | |
| 0x94, 0x9d | 00700b10, 007025e0 | 0x80 | cyc(t/5) | |
| 0x95 | 00700df0 | 0x82 | pp(2) | colour (a=1, rgb=0): black silhouette |
| 0x96 | 007011d0 | — | — | nothing drawn |
| 0x97, 0x9a | 007011e0, 00701a40 | 0x97 | min(t/5, 3) | |
| 0x98 | 007014a0 | 0x98 | min(t/5, 3) | |
| 0x99 | 00701760 | 0x99 | t<10 ? 0 : min((t−10)/4 + 1, 5) | |
| 0x9b | 00701d00 | 0x80 | min(t/4, 4), only while t<20 | black silhouette, scale (1 − t/20, t/10) |
| 0x9c | 007020f0 | t<10: 0x80, frame min(t/4,4), black, scale (t/10, 2 − t/10); 10≤t<15: 0x99 frame 0; t≥15: 0x99 frame min((t−15)/4, 5) | | white from t≥10 |

---

## 5. Spawned objects

All Mira object functions live outside her code block (in the family code regions) and are Mira-only
(type 6 slot). Collision semantics: objects.md §4.

### 5.1 Beam family (type 6): spawn FUN_004adc50, update FUN_004aec70, draw FUN_004ae000, onHit FUN_004ae960
Template = FUN_004ba490 then per id (offsets objects.md §2.2). destroyOnHit 1, stun 1.0, hitType 1
(dash passes through) for all.

| id | Spawned by | speed +0x68 | scale +0x58 | shape {hx,hy,r} | dmg +0x84 | knock +0x88 | active +0x70 | behaviour |
|---|---|---|---|---|---|---|---|---|
| 100 "mine" | flying chakrams 0xb4/0xbe (sub 0, speed>0, even age) | **0** | 3.0 (DAT_00813a50) | {0,0,10} (DAT_00813f4c) | **40** (old 80) | 5 (DAT_00813f40) | 0 until age==5 → 1 | scale −0.2/frame (DAT_00853b14) down to 1.0; stationary; age>300 → becomes 0xbe (inactive, scale 3.0, effect 0x516) |
| 0x6e | chakram bursts (0x91, wall bursts) | 12 (DAT_00813f54) | 3.0 | {0,0,10} | **40** (old 80) | 5 | 1 | scale −0.2 to 1.0; move; speed ×0.95 (_DAT_00814124); age>80 or (age>5 and outside arena+10) → 0xbe |
| 0xbe | (state of the two above) | — | — | — | — | — | 0 | scale −0.5/frame (DAT_00853b1c); removed at ≤0 |
| 200 / 0xc9 ninja star | 0x86 | 16 (DAT_008140d8) | 1.0 | {0,0,16} (DAT_008140d8) | 200 | 5 | 1 | **0xc9 is stored as id 200** after init (`local_9c=200`); only the curve differs: `+0x6c` = +0.05 (DAT_00813ef4) for 200, −0.05 (DAT_00814114) for 0xc9 |
| 300 caltrop | 0x88 | 24 (DAT_00853b8c) | **0** | {0,0,10} | **1** until grown, then 50 | **0** | 1 | see below |

Updates:
- **200** (ninja star): age<10: speed ×0.75 (DAT_00813f3c); else speed +0.5 (DAT_00853b1c) while <20 (DAT_008140fc).
  `+0x5c` (alpha/curve weight) −0.01 (DAT_00813ec8) per frame to 0 (clamped at 0 when ≤0).
  `moveAngle(+0x60) += +0x6c · +0x5c` (curve that fades over 100 frames); move;
  `angle(+0x64) += +0x6c·10` (spin, wrapped); removed if age>50 and outside arena+10;
  every frame effect `0x8816b0(400)` at pos with angle +0x64 (trail, cosmetic).
  The pair starts at aim∓s and each curves toward the aim line (200 turns CCW, 0xc9 CW).
- **300** (caltrop): scale += 0.1 (DAT_00853b10); when scale > 1.0 → scale = 1.0 and **damage = 50**
  (0x32) (float: becomes > 1 on the 10th update, age 9); speed ×0.85 (DAT_00814120); `+0x64 += 0.05`;
  age>150 → effects 0x516 + 6 particles 0x456 (FUN_0068b8a0), removed (no onHit).
- onHit: 100/0x6e nothing; 200: 6 streaks 0x456 at `k/3·π + rand_f(−0.25,0.25)` (k=−3..2;
  DAT_0081411c, DAT_00813f24), effect 0x516, SE 0x13; 300: effect 0x516, 6 particles, SE 0x15.
Draw: 100/0x6e/0xbe from the shared bullet sheet `DAT_0087fde4` (colour index `DAT_008859fc+team*0x10`),
two quads half-size 30·scale and 15·scale; 200: sprite 0x9e frames 1 then 0 (additive), half-size
32·scale (DAT_008140dc), rotation −`+0x64`; 300: sprite 0x9f frame `((age/2)%5)*2+1` then that−1,
half-size 24·scale.

### 5.2 Missile family (type 6): spawn FUN_0066e4b0, update FUN_0066fd50, draw FUN_0066e660, onHit FUN_0066faa0, onKilled FUN_0066fc20 (onAbsorb 0x881b60 = 0)
Template FUN_00685bb0 + per id; growScale `+0x5c` starts 0; hitType 2 (not dash-avoidable).

| | Kunai (100) | Kiri (200) |
|---|---|---|
| speed | 16 (DAT_008140d8) | 30 (DAT_008140e8) |
| hp +0x78 | **80** (v>0x58a; old 60) | 60 |
| damage +0xa0 | **180** (v>0x58a; old 150) | 160 |
| shapeBody r (+0x88) / shapeProj r (+0x98) | 8 / 8 (DAT_008140d4) | 8 / 8 |
| knockback | 10 (DAT_00813f4c) | 10 |

Update (both: `growScale += 0.1` to 1.0):
- **Kunai 100**: age<20: speed ×0.85 (DAT_00814120); else speed +0.5 while <20. Move along `+0x68`.
  If outside the arena (margin 0): **id → 0x65** (stuck), age = 0, shapeBody r = 10, effect 0x516,
  `FUN_007bcaf0(0x16,3)`.
- **0x65** stuck kunai: no movement; still collides (body r=10); when age>100 → calls its own
  onKilled (`0x881af8[type]`) and is removed.
- **Kiri 200**: age<5000: if speed > 5 → speed ×0.85; age≥5000: speed += 0.2 (DAT_00853b14). Move.
  age<6 and even: spark effect (consumes 2 RNG draws). If outside arena+10: if age>5000 → removed
  silently; else (always outside arena+0 then) **snap to the wall**: `pos = 540·(cos,sin)(atan2(y,x))`,
  heading = angle = atan2(opp − pos), age += 5000, speed = 5 (DAT_00813f40), effects 0x547/0x546,
  `FUN_007bcaf0(0xb,4)` → the kiri drifts out sideways, and from the wall fires back at the
  opponent, accelerating until it leaves the far side.
- onHit (player contact): effects 0x516 + 0x9c4, SE 0x15; returns 0 (destroyed).
- onKilled (shot down / stuck timeout): effects 0x262 + 0x515, SE 0x1d; returns 0.
Draw: kunai sprite 0xa0 frame 0 (scale (3−2g, g), black while g<1), additive frames 1/2 pulsing
(`sin((age%50)/50·2π)`); 0x65 adds sprite 0xa6 glow; kiri sprite 0xa1 (scale (2−g, g)), additive
frames 1/2 once g == 1.

### 5.3 Whips (type 6): spawn FUN_007c5d50, update FUN_007c6570, draw FUN_007c5e70 — visual only
`spawn(kind, slot, missileSerial)`; kind 0 for kunai, 1 for kiri. Each update finds the missile by
serial (`FUN_00685ce0`); if gone → removed. Pushes a node pair `missile.pos ± (cos,sin)(angle+π/2)·w`
(w = 7.5 DAT_00814150 for kind 0, 5.0 for kind 1) into a 20-node ring (shifting when full). Draw:
textured strip from the slot's whip texture, alpha 0x80, v = node·12.8. No collision.

### 5.4 Hitbox family (type 6): spawn FUN_005e5480, onHit FUN_005e5af0
Layout objects.md §2.4; dealsDamage 1, oncePerFrame 0, stun 1.0, all **hitType 3** (hits dashing players).

| id | Spawned by | dmg | shape {hx,hy,r} | knock | rehitFlag | onHit |
|---|---|---|---|---|---|---|
| 100 | orbiting chakram while owner in 0x91 (every frame) | **40** (old 20) | {0,0,60} (DAT_00813a58) | 15 (DAT_00853b88) | 0 | 4 streaks 0x456 + 0x515, SE 0x16 lock 4 |
| 0xb4 | Soaring chakram (every frame in flight) | **140** (old 120) | {0,0,60} | 10 | 1 | if victim state 1 && victim speed > 2 (DAT_00813a4c): **dealsDamage := 0**; else streaks, SE 0x16 |
| 0xbe | Sky-Soaring chakram in flight | 50 | {0,0,60} | 5 | 0 | as 100 |
| 0xc3 | Sky-Soaring on the wall; Baiting chakrams | 120 | {0,0,60} | 15 | 1 | as 0xb4 (no damage on an already-flying victim) |
| 0xd2 | tornado lasers (per segment, even ages) | **0** | {0,0,**70**} (v>0x5a0 DAT_008145ec; old 60) | 0 | 0 | if victim state 1 && victim t<6: dealsDamage := 0 — otherwise the 0-damage hit still adds stun 1.0 and re-hitstuns with knock 0 (holds the victim) |
| 300 | Quick Slash 1 | 120 | {60, 80, 0} (DAT_00813a58, DAT_00814164) | 2.5 (DAT_008153e0) | 0 | effects; **sets owner hit-confirm**; SE 0x16 lock 3 |
| 0x136 | Quick Slash 2 | 80 | {60, 80, 0} | 2.5 | 0 | effects; **sets hit-confirm**; SE 0x16 |
| 0x140 | Quick Slash 3 | 160 | {80, 40, 0} (DAT_00814164, DAT_008140e0) | 0 | 0 | effects, SE 0x17, shake `FUN_004d7ac0(10,0.75,1)` |
| 400 | Divine Speed Slash | 120 | {100, 50, 0} (DAT_00853b90, DAT_008140ec) | 0 | 0 | **sets hit-confirm**; SE 0x16 |
| 0x19a | Divine clones (enemy 400–402) | 80 | {120, 60, 0} (DAT_00814174, DAT_00813a58) | 0 | 0 | effects, SE 0x16 |
| 0x1a4 | Divine finisher | 200 | **{132, 65.5, 0}** (v>0x5a0 DAT_00813a5c, DAT_0081fa74; old {120,60}) | 2.5 | 0 | effects, SE 0x17, shake |
| 200, 0x191 | (never spawned — dead ids: dmg 30 r60 / dmg 0 r100) | | | | | |
Knockback 0 + the hitstun state = the victim stops dead (combo freeze). Cosmetic RNG in onHit:
100/0xb4/0xbe/0xc3: 1 + 4 draws; 300/0x136/0x19a: 1 draw; 0x140: 1 draw; 0x1a4: none.

### 5.5 Enemy family (type 6): spawn FUN_00523620, update FUN_00523af0, draw FUN_005257b0, onKilled FUN_005253c0
Base init FUN_0055ee70 (objects.md §2.6). Per id template:

| id (+0x10) | +0x7c behaviour | hp/max | active +0x2c | shape | +0x60 | +0x64 | +0x68 | other |
|---|---|---|---|---|---|---|---|---|
| 100 chakram | 100 | 500/500 | 0 (1 once deployed) | {0,0,30} (DAT_008140e8) | 1.0 (spin multiplier) | 0 | 0 (deploy progress / alpha) | +0x84 = 0 |
| 200 smoke ball | 200 | 50/50 | **1** | {0,0,20} (DAT_008140fc) | 0 (scale) | 0 | 1.0 | |
| 300 afterimage | 300 | 999 | 0 | none | 1.0 (sx) | 1.0 (sy) | 1.0 | |
| 400/401/402 clone | 400 | 999 | 0 | none | 0 (sx) | 2.0 (sy) | 1.0 (alpha) | +0x84 = 0 |
Fields used: `+0x5c` heading, `+0x58` draw angle (spin), `+0x70` speed (or radius for orbiting chakrams,
or angular speed on the wall), `+0x74` orbit/wall angle, `+0x78` age, `+0x80` phase timer (both
incremented after the update), `+0x84` sub-phase. Chakrams are not solid and not invulnerable:
opponent beams/missiles/hitboxes/lasers damage them (and deployed chakrams absorb opponent beams like
shields, objects.md §4.5). Effect `0x8816b0(100)` = chakram glow trail.

**Behaviour 100 — orbiting chakram** (owner = `FUN_007805f0(ownerId)`):
- sub 0 (deploying): `r(+0x70) += 2` (DAT_00813a4c); `+0x68 = r/120` (DAT_00814174); if r > 120: r = 120,
  sub = 100, `+0x68 = 1.0`, **active = 1** (after 61 updates). `+0x74 += +0x68·0.05` (DAT_00813ef4);
  `+0x58 += +0x68·0.5` (wrap >π: −2π).
- sub 100: `+0x74 += +0x60·0.05`, and if owner state == 0x91: `+= +0x60·0.1` (DAT_00853b10) more;
  `+0x58 += +0x60·0.5` (+`+0x60·0.1` in 0x91); wraps as above.
  If owner state == 0x91 and `12 ≤ owner.t ≤ 18` (the `t % 1` test at 0x523edc is always true) and
  `FUN_007807f0(owner, 0x91)`: beams **0x6e** at pos with angles `+0x58`, `+0x58 + 2π/3`, `+0x58 − 2π/3`
  (DAT_00817898). If owner state == 0x91: effect 100, and hitbox **100** at pos with angle
  `wrap(owner.aim + π)` (knocks the victim back toward Mira).
- both subs: `pos = owner.pos + (cos,sin)(+0x5c + +0x74)·r` (follows the owner exactly).
No lifetime limit.

**Behaviour 0xb4 — Soaring (launched) chakram** (+0x10 = 0xb4):
- sub 0: if speed < 20: speed += 1 (DAT_00853b34), clamp 20 (v>0x58a). If v>0x58a and *pre-move* pos is
  outside arena−10 (r ≥ 530, DAT_00817c10 = −10): speed −= 0.5. Spin `+0x58 += +0x60·0.5`.
  `step = speed`, ×1.5 (DAT_00814110) if outside arena−10 and speed > 0. `pos += (cos,sin)(+0x5c)·step`.
  Hitbox **0xb4** at pos, angle `+0x5c`; effect 100. If speed > 0 and age even: beam **100** (mine)
  at pos, angle `+0x5c`. If speed ≥ 20 and *post-move* pos outside arena−10: **wall burst** = 18 beams
  **0x6e** at angles `+0x5c + π + 0.2k` (k = −9..8), effect 0xaf0, `FUN_007bcaf0(0x19,3)`,
  `FUN_004d7ac0(10,0.75,1)`, sub = 900, `+0x80 = 0`.
  (Edge case, bug-compatible: once speed has been reduced by the outside-zone −0.5 it can never be
  ≥ 20 at the burst test, so a chakram that enters the r≥530 zone below full speed flies on without
  bursting and is never removed. Launch speed −16 reaches 20 after 36 updates.)
- sub 900 (fade): `+0x68 −= 0.02` (DAT_00813ed8); `+0x58 += +0x68·+0x60·0.5`; removed at `+0x68 ≤ 0`
  (no onKilled).

**Behaviour 0xbe — Sky-Soaring chakram**:
- sub 0: if speed < 20: speed += 1 (no clamp; −16 reaches exactly 20). Spin. Move by speed along
  `+0x5c`. Hitbox **0xbe**; effect 100; mine beam 100 on even ages while speed > 0. When speed ≥ 20
  and outside arena+10 (r ≥ 550): sub = 10, `+0x80 = 0`, speed = 0, `+0x74 = atan2(y, x)`,
  `FUN_007bcaf0(0x19,3)`, shake.
- sub 10 (riding the wall): `+0x80 < 450`: if `+0x80 > 99`: speed += 0.00025 (_DAT_00815bc4), cap
  0.015 (DAT_00813ed4) [rad/frame]; `+0x80 ≥ 450`: speed ×0.95. `+0x74 += speed`;
  `pos = 540·(cos,sin)(+0x74)` (DAT_0081412c); spin. While `+0x80 < 450`: hitbox **0xc3** at pos,
  angle `atan2(−y, −x)` (toward centre). Effect 100 while 100 < `+0x80` < 450. Removed when
  `+0x80 == 500` (no onKilled). So: stuck still for 100 frames, then slides around the wall.

**Behaviour 0xb9 — Baiting chakram** (+0x10 = 0xb4):
- sub 0 (fade in, baits only): `+0x68 += 0.1` → at ≥ 1.0: `+0x68 = 1`, sub = 100, `+0x80 = 0` (10 updates).
- sub 100: if speed ≥ 0: speed += 1 while < 20; turn `+0x5c` toward the opponent by ≤ 0.01 rad
  (DAT_00813ec8, FUN_005f0e70). Else (speed < 0): speed += 1; if now ≥ 0: `+0x5c += rand_f(−0.5, 0.5)`
  (**gameplay RNG**). `step = speed`; if v>0x58a and outside arena−10: step = min(speed·1.5, 20) if
  speed ≥ 0 else speed − 0.5. Move. `+0x58 += +0x68·+0x60·0.5` (wrap when > 2π). Effect 100; hitbox
  **0xc3** at pos, angle `+0x5c`. Wall burst exactly as 0xb4 when speed ≥ 20 and outside arena−10.
- sub 900: fade as 0xb4.

**Behaviour 200 — smoke ball**:
- sub 0 (flying): scale `+0x60 += 0.2` to 1.0; after `+0x80 > 5`: speed −= 0.25 (DAT_00813f24). Move
  along `+0x5c`. Spin `+0x58 += +0x60·0.25`. If speed ≤ 0 or outside arena+10 → calls its onKilled
  (becomes a cloud). (From speed 10: stops after 46 updates.) Shootable (hp 50) — being shot down
  also turns it into a cloud.
- onKilled for 200: sub = 100, hp = 999, `+0x80 = 0`, active = 0; effects 0x515, 0x516; Mira effect
  500 (cloud visual, its age set to the ball's age); SE 0x1d; **returns 1** (kept).
- sub 100 (cloud): every 5 frames (`+0x80 % 5 == 0`): smoke effect 200 at pos + rand offset
  (3 RNG draws: angle rand_f(−π,π), dist rand_f(0,60), effect angle rand_f(−π,π)). If the opponent is
  within 100 (DAT_00853b90, FUN_005f1c30) of the cloud: `FUN_006b1a40(opp, 4, 16)` → **movement ×0.25
  for 16 frames** (refreshed while inside). Removed when `+0x80 > 300`.

**Behaviour 300 — afterimage**: `+0x60 −= 0.1`, `+0x64 += 0.2`; removed at `+0x60 ≤ 0` (10 frames). Visual.

**Behaviour 400 — Divine clone**:
- sub 0: `+0x60 += 0.1`, `+0x64 −= 0.1`; when `+0x60 ≥ 1`: both = 1, `+0x80 = 0`, sub = 100.
- sub 100: `+0x80 == 5`: speed = 10, `+0x5c = +0x58 = atan2(opp − e)`; `+0x80 == 10`: hitbox **0x19a** at
  pos, angle `+0x58`; `+0x80 > 20`: speed ×0.85; move; `+0x80 > 40`: alpha `+0x68 −= 0.1`; removed at ≤ 0.

**onKilled FUN_005253c0** for chakrams (100/0xb4/0xb9/0xbe): effect 600, 5 × effect 0x834 and 5 ×
effect 500 at random offsets (20 RNG draws), SE 0x1a; returns 0 (removed). Other ids: return 0.

Draw (FUN_005257b0): chakrams sprite 0xa2 frame `min(age/50, 2)` (for 0xb4 in sub 900: minus
`+0x80/20`, ≥0), colour (a=r=g=b=`+0x68`), scale `+0x60`, rotZ `+0x58`; smoke ball 0xa5 frame
`(age/3)%6` (not drawn as a cloud), scale `+0x60·(1 + 0.15·sin((age%10)/10·2π))`, colour (1, `+0x68`×3);
afterimage 0x80 frame `min(+0x80/2, 4)` black, scale (+0x60, +0x64), AIM-style rotation by `+0x58`;
clones: sub 0 → 0x80 frame `min(+0x80/2,4)`; sub 100 → 0x97/0x98/0x99 for 400/401/402, frame
`+0x80<5 ? 0 : (+0x80−5)/4`, cap 3/4/5; colour (a=`+0x68`, rgb = 1 if `+0x60 ≥ 1` else 0).

### 5.6 Laser family (type 6): spawn FUN_0061fbb0, update FUN_00620750, draw FUN_0061fd00, onHit FUN_00620470
Template FUN_006307d0 + `blockMode(+8) = 1` (not cut by bodies), hitType 3, knockback 0, stun 1.
Age pre-incremented (first update sees 1). Segments rebuilt every frame (`FUN_004a9f10`).

| | Wrathful Tornado (100) | Twin Dragon (0x96) |
|---|---|---|
| damage `+0x40` (per contact frame) | 35 (0x23) | 110 (0x6e) |
| lifetime | removed when age > 120 | same |
| segments | always, k = −20..19 | only when age > 14, k = −20..19 |
| segment radius | 60 (DAT_00813a58) if age%5==0 && 14<age<91, else 0 | 80 (DAT_00814164) under the same condition |
| segment pos | `O + D·(50k) + N·30·sin(((age+k)%20)/20·2π)` | `O + D·(50k + 20·sin(((age+k)%40)/40·2π)) + N·40·sin(((age+k)&15 as C %16)/16·2π)` |
`O` origin, `D = (cos,sin)(angle)`, `N = (cos,sin)(angle+π/2)`; `%` is C remainder (negative for
negative age+k). Constants: 50 DAT_008140ec, 30 DAT_008140e8, 20 DAT_008140fc, 40 DAT_008140e0, 16 DAT_008140d8.
Damage ticks therefore happen on ages 15, 20, …, 90 (16 ticks), any segment; one segment per laser per player per frame.
On even ages 16, 18, …, 88 (age%2==0 && 14<age<90): a hitbox **0xd2** (0 damage, r 70) at every segment
position (40 per frame) — keeps a touching victim in hitstun with no knockback (§5.4).
0x96 also spawns cosmetic sparks `0xa64 + rand_i%8` at segments with |k|>2, age<100, (age+k)%4==0
(2 RNG draws each — RNG-relevant).
onHit (segment contact): 100: effect 0x9c4 (1 RNG draw); victim `+0x94 = +0x84 = angle ± π/2` (+ when
age%10==0, − otherwise), `+0x98 = 5` (sideways shove, overriding the laser's knockback 0); SE 0x15 lock 5.
0x96: one discarded `rand_f(−π,π)`, same shove, 6 streaks 0x456 (6 draws), SE 0x15 lock 5.
Draw: sprite 0xa3 (`mr_tor`) per segment, frame `((age + 2k')/2)&3`, half-width 100, half-height
`(|k|·15+240)` (100) / `(|k|²·1.5+180)` (0x96), scaled in over age<10 and out after age>100, alpha
0x48/0x30 then 0x40 additive, rotated by −angle.

### 5.7 Mira effects `0x8816b0 = FUN_004fdfa0` (update FUN_004fed10, draw FUN_004fe220) — cosmetic, no RNG
100 chakram trail (sprite 0xa2, fades 0.1/frame), 200 smoke puff (0xa4), 300 (unused by the code
reviewed), 400 ninja-star trail (0x9e), 500 smoke cloud (0xa5, frame `(age/3)%6`). Generic effects:
0xfa hyper flash, 0x262/0x515/0x516 bursts, 0x456 streaks, 0x546/0x547 muzzle, 0x9c4/0x9c5 slashes,
0xaf0 wall burst, 0x834/600/500 chakram break, 0xa28–0xa2f dash sparks, 0xa64–0xa6b tornado sparks.

### 5.8 RNG consumers (gameplay-relevant marked ★)
0x8e smoke angle draw (always, discarded in v>0x5a0); 0x93 bait offset (2 draws, discarded);
★0x9b clone angle; ★0xb9 heading jitter; smoke cloud puffs; kiri sparks; dash sparks (0x8a/0x8b/0x9a/0x9c);
0x96 laser sparks; all onHit effect draws; chakram break effects. Order = object update order.

---

## 6. Sound ids used (SE table at all.c:464010, `0x9b99a0 + 4·id`)
| id | file | where |
|---|---|---|
| 6 | up01.wav | hyper start (0x8e,0x8f,0x92,0x93,0x94,0x9a) |
| 7 | high_a.wav | Twin Dragon start |
| 8 | air01.wav | Kunai 2/3 entry dash |
| 0xb | eco03.wav | kiri wall rebound (lock 4) |
| 0xd | _sen_mi_b_raifuru.wav | ninja stars |
| 0xf | laser02.wav | caltrops (lock 4) |
| 0x13 | don16.wav | ninja star hit |
| 0x15 | crash17.wav | caltrop hit, missile hits, laser shove |
| 0x16 | hit_s.wav | chakram/slash hitboxes, kunai sticking (lock 3) |
| 0x17 | hit_f.wav | Quick Slash 3, Divine finisher |
| 0x19 | hit_f.wav | chakram wall burst / wall stick (lock 3) |
| 0x1a | tm2_bom002.wav | chakram destroyed |
| 0x1d | gun12.wav | smoke ball bursts, missile shot down |
| 0x1f | swing.wav | kunai, smoke ball, slashes |
| 0x20 | swing_h.wav | chakram launches, Swift dash, Quick Slash 3, Divine finisher |
| 0x23 | swing_d.wav | kiri (lock 4) |
| 0x2b | tm2_sonic004.wav | Divine Speed Slash connects |
| 0x2c | tm2_sonic005.wav | Quick Slash dash-through, Divine → finisher |
| 0x2f | power35.wav | Divine clones (lock 4) |

---

## 7. Other per-character table entries (index 6)
| Table (real base) | Mira entry | Role |
|---|---|---|
| 0x881310 (+0x18 = 0x881328) | FUN_006fbb60 | input dispatch, standard |
| 0x881360 (0x881378) | FUN_006fc090 | input dispatch, alternate |
| 0x8813b0 (0x8813c8) | FUN_006f2980 | sprite loader |
| 0x881400 + 0x60 + 4·level | FUN_006fce20 (level 0), FUN_006fc5c0 (levels 1,2) | CPU AI (not reversed; uses shared RNG) |
| 0x881890/8e0/930/980 | 4adc50 / 4aec70 / 4ae000 / 4ae960 | beam family (§5.1) |
| 0x8819d0/a20/a70/ac0/b10 (b60 = 0) | 66e4b0 / 66fd50 / 66e660 / 66faa0 / 66fc20 | missile family (§5.2) |
| 0x881bb0/c00 | 5e5480 / 5e5af0 | hitbox family (§5.4) |
| 0x881c50/ca0/cf0/d40 | 61fbb0 / 620750 / 61fd00 / 620470 | laser family (§5.6) |
| 0x881570/610/5c0/660 | 523620 spawn / 523af0 update / 5257b0 draw / 5253c0 onKilled | enemy family (§5.5) |
| 0x881d90/de0/e30 | 7c5d50 / 7c6570 / 7c5e70 | whip family (§5.3) |
| 0x8816b0/700/750 | 4fdfa0 / 4fed10 / 4fe220 | Mira effects (§5.7) |
(registry.c:126,142,158,180-182,493-518.)

---

## 8. Stats record (index 6, base 0x8830a8), registry.c:1490-1523
| Off | Value | Meaning |
|---|---|---|
| +0x00 | 5.0 (DAT_00813f40) | walk speed per axis |
| +0x04 | **12.0** (DAT_00813f54) | dash speed (Suguri 15) |
| +0x08 | 10.0 (DAT_00813f4c) | UNVERIFIED (Suguri 12) |
| +0x0c..+0x20 | 6, 4, 0xb, 3, 2, 7 | UNVERIFIED ints (Suguri 0,5,0,2,1,0) |
| +0x3c/+0x40/+0x44 | 0x83, 0x84, 0x85 | sprite ids (ring / unregistered / system) |
| +0x48/+0x60 | text 0x13 / 0x14 strings | colour-variant folders |
| +0xa8 / +0x1a8 | `mdl\win_font_MR.lfd/.dat` | |
| +0x2a8 | `mr_res_bgp` | |
| +0x2c0/+0x2c4/+0x2c8 | 0, 50, 0 | UNVERIFIED (portrait offset?) |
| +0x2cc/+0x2d0/+0x2d4 | 220, 90, 130 | colour RGB |
| +0x2d8 | 0 | |
Ver patch FUN_004a7270 touches only Mira's kunai costs (§1.1).

---

## 9. Exact draw parameters (verified against the decompile; port: src/sim/chars/mira.c)

"blit" = 2-D texture sprite call (vtable +0x68) with source rect and destination half-size
(÷zoom, i.e. world units); rotation passed as `−angle` (world CCW = +angle) unless noted.
"quad" = CModel quad of the registered size S (half S/2). Colour calls are `(a, r, g, b)`.

| Object | Calls (in order) |
|---|---|
| beam 100/0x6e/0xbe (FUN_004ae000) | colour cell c = `DAT_008859fc[team]`; blit `beamsphere` src `((c%10)&1)·100+50+(c/10)·250, ((c%10)/2)·100`, 99×99, half 30·scale, alpha `(int)(96·a)`, blend 1; blit src `((c/10)·250, (c%10)·50)` 49×49, half 15·scale, alpha `(int)(255·a)`, blend 2 |
| beam 200 | blit `mr_blade01` src 0..127, half 32·scale (DAT_008140dc), rot +0x64, white, **blend 1**; then `mr_blade00`, same, **blend 2** |
| beam 300 | f = ((age/2)%5)·2+1: blit `mr_caltrap0f` half 24·scale (DAT_00853b8c) blend 1, then frame f−1 blend 2 |
| missile 100/0x65 (FUN_0066e660) | a1 = 0.85+0.15·sin((age%50)/50·2π), a2 = 0.75+0.25·sin(…). 0x65 only: quad `mr_impact0` (S 128) scale g·a1·0.5, colour (a1·0.5,1,1,1). Body quad `mr_knife00` (S 80) scale (3−2g, g), colour white if g ≥ 1 else (1,0,0,0); blend 2: `mr_knife01` colour a1, `mr_knife02` colour a2 (same scale); 0x65: `mr_impact1` colour (a2, .5,.5,.5); blend 1. All rotZ = +0x6c |
| missile 200 | quad `mr_gimlet00` scale (2−g, g) (no colour call → white); if g == 1: blend 2, `mr_gimlet01` (a1) and `02` (a2) with **no scale call** (1,1) |
| whip (FUN_007c5e70) | strip quads B[k]→B[k+1]→A[k+1]→A[k], u 0/256 (DAT_00814160), v = k·12.8 (DAT_00828324), colour 0x80ffffff; texture `mr_whip0` (kind 0) / `mr_whip1` (kind 1) of palette slot `whip+8`. Port draws each trapezoid as its best-fit rectangle |
| enemy (FUN_005257b0) | **switches on +0x10** (spawn sub-id), not +0x7c. Chakrams quad `mr_chakram0f` f = min(age/50,2) (−timer2/20 for +0x10==0xb4 in sub 900), scale +0x60, colour (+0x68 ×4); smoke ball (not when +0x84==100) quad `mr_smkball` frame (age/3)%6, scale +0x60·(1+0.15·sin((age%10)/10·2π)), colour (1, +0x68 ×3); 300 afterimage `mr_sld` min(timer2/2,4) black, scale (+0x60,+0x64), AIM transform by +0x58; 400/0x191/0x192 clones: sub 0 `mr_sld` min(timer2/2,4), else `mr_infghta/b/c` frame timer2<5 ? 0 : (timer2−5)/4 capped 3/4/5; colour (+0x68, rgb = +0x60 ≥ 1 ? 1 : 0) |
| laser (FUN_0061fd00) | per segment i (k = i−20): frame ((age+2i)/2)&3 of `mr_tor`, src 0..127, rot +0x30; half-width 100 (DAT_00853b90), half-height `|k|·15+240` (100) / `k²·1.5+180` (0x96), alpha 0x48 / 0x30; age<10: hh·age/10; age>100: hh·(120−age)/20 and alpha `(int)((120−age)/20·72)` (DAT_008178ac) / `·48`; blend 1. Second pass: hh·0.75, **hw + 0.75** (literal), alpha 0x40, blend 2 |
| effect 100 | blit `mr_chakram02` half 64·scale, alpha (int)(200·a), blend 2 |
| effect 200 | blit `mr_smk0f` f = min(age/12,3), half 32·scale, alpha 255a blend 1; again blend 2 with rgb = (int)(128·a) |
| effect 400 | blit `mr_blade00` half 32·scale, alpha 200a, blend 2 |
| effect 500 | blit `mr_smkball` frame (age/3)%6, half 24·scale, alpha 255a, blend 1 |
| effect 300 (no spawner) | common `explosion0/1[variant/8]`, cell row (variant%8)%4, col (variant%8)/4; half 180·scale alpha 128a blend 2, then half 200·scale (×0.85 on even ages) alpha 255a blend 2; rotation passed **without** the sign flip |

## 10. Verification status (tests/test_mira.c) and open items
Covered by tests: dispatch of every row in both schemes incl. chakram (★) branches and costs; ninja-star
spawn frame/pair/curve/cost and hold loop; dash-cancel/end frames of 0x86; 12 caltrops at t=10..21 with no
cost (§0.2) and damage 1→50; kunai tap (0x89 on release, 3 kunai at t=12, B −0.2, 180 damage on a placed
opponent); kiri hold (8 at t=15..21); smoke ball → cloud → opponent slow status; chakram deploy (t=5, 4,
radius 120, active, hp 500), Soaring launch (+0x10 → 0xb4), Sky-Soaring (all → 0xbe); Quick Slash chain
(0x95 intangible, →0x97 at t=11, 120 damage, hit-confirm, A → 0x96 with frozen opponent, → 0x98);
Wrathful Tornado (t=15, through the opponent, damage, removal at age 121, end at t>100); Divine Speed Slash
chain 0x9a→0x9b→0x9c with clones and Mira unhittable; every state ends; mirror random run deterministic.

Settled (previously UNVERIFIED):
- 0x88 cost entry: no writer of 0x87cb50..58 in all.c (static); caltrops are free.
- `+0xdc` colour order (a,r,g,b): confirmed project-wide (render.md fixed).
- Effect 300 of FUN_004fdfa0: no `(*DAT_008816b0)(300,…)` call anywhere (only 100/200/400/500).
- 0x97/0x98/0x99 hitbox z: uninitialised stack in the original, never read (shape test is 2-D).

Still open:
- Stats +0x08, +0x2c0..+0x2c8 meanings; `mr_rng` / `mr_system` HUD roles.
- CPU AI FUN_006fce20 / FUN_006fc5c0 not reversed.
- Whip strip drawn as best-fit rectangles (the draw API has no textured trapezoid).
- Engine pool limit: each tornado laser spawns 40 hitboxes (0xd2) per even frame; Twin Dragon (2 lasers)
  + a Wrathful Tornado exceed `AOS_MAX_HITBOX = 96` (mirror match: 28 boxes dropped per frame). The original
  vector is unbounded → needs ≥ 256.
