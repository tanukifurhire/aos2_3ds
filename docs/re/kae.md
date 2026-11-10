# Kae — complete moveset spec

Source: `aos2.exe` (Ghidra corpus `tools/re/dec/all.c`, constants read with `xref.exe rd`).
Every number cites its address. `t` = player state timer `p+0x154`. Template and shared helper names:
suguri.md (§1 shared mechanics, §5 object conventions), core.md, objects.md. Engine index **14**
(handbook id 11 + 3). States 0x1aa–0x1c7, sprite prefix `ke_`. Version global `DAT_0089426c` = 0x5a2
in live play (objects.md §0.1) — the newest branch is given; older branches are noted briefly.

---

## 0. Notes for the lead (affect other specs)

1. **render.md correction — CModel `+0xdc` argument order is `(a, r, g, b, meshIdx)`, not (r,g,b,a).**
   `FUN_0046ef70` stores `param_2` into the 4th float of the D3D colour value (`local_40`) and
   `param_3..5` into the first three (r,g,b). Corroborated by `FUN_004e8f50` (global tint: `_DAT_00893da0`
   is never cleared by the fade code, all.c:356756-356789, while da4/da8/dac are → da0 = alpha) and by Kae's
   missile draw, which pulses only the first argument (`FUN_006688c0`). UNVERIFIED only in that the
   D3D struct type (material vs. vertex colour) was not traced.
2. **Status 7 (`p+0xaa` / timer `p+0xc8`, tick `FUN_00751750`) *is* Kae's "Blazing" mode.** It is a generic
   status (core.md §6.8): heat forced ≥ 2.01 each frame, heat := 1.0 when it ends. Kae's code reads the
   flag to select ★ variants. It can be applied to the **opponent** by Kae's fire field (effect 500, §5.5)
   — in a Kae mirror the victim Kae then gets her ★ moves too. Suggest core.md name status 7 "Blazing".
3. `FUN_006b1d70` (proximity knock), on success, also clears the **camera focus list** (`mov ecx,0x895144;
   call FUN_004a9f10` @0x6b1e7d) — objects.md §4.6 omits this.
4. On-hit callbacks can write the **victim's heat** directly: Kae hitboxes 400, 0x136, 0x137 add +0.1
   (DAT_00853b10) to `victim+0xe0` (FUN_005e39d0); WHAAAAM★ adds +0.015/f during the hold (FUN_006df9a0).
   core.md §6.7 heat-source list should include these.
5. Ghidra idiom: `u = x & 0x80000000; if ((int)u < 0) u = 0; if (u == 0)` is the compiled form of
   `x % 1 == 0` — i.e. **always true** for x ≥ 0 (it is not an even-frame test; the even test uses
   `& 0x80000001`). Appears in Kae 0x1b5, 0x1c3, beam update. Other agents should not misread it.
6. Grab mechanic `FUN_00780a50(opp)` ("can be grabbed"): true iff `opp+0xf8 == 0` (no barrier), state ≠ 6,
   `+0xa2 != 0`, `+0xa9 != 1`, and `DAT_00885964 != 2`. Kae's grab hypers (0x1c4/0x1c6) then pin the
   opponent by rewriting its state/timer/position every frame (§3). No collision object is involved.

### Player fields used by Kae's code (beyond suguri.md's table)

| Off | Name | Notes / source |
|---|---|---|
| +0xaa | Blazing flag (status 7) | read by input fn and most states; set via `FUN_006b1a40(p,7,n)` |
| +0xc8 | Blazing frames left | decremented by `FUN_00751750` |
| +0xe0 | heat | status 7 floors it at 2.01 (DAT_00825fcc) → damage taken ×2.01 for hitType 1/3 (core.md §6.2) |
| +0x88 | secondary aim | several hypers keep the true aim here while +0x84 holds the horizontal facing |

---

## 1. Shared mechanics used by Kae's states

Everything in suguri.md §1 applies unchanged (frame model, `state=0` end without timer reset,
dash-cancel `FUN_006b17d0(p,1)`, cost table `FUN_007807f0`, `FUN_006b1b40` barriers, `FUN_00780950`
hyper permission, `FUN_00780910` hyper spend, helper functions, dash sparks). Kae-specific:

### 1.1 Blazing mode (status 7)
| Source | How entered | Duration | Address |
|---|---|---|---|
| **Blazing!** (S+H, 1 hyper level) | at t=20 of state 0x1b9: `FUN_006b1a40(p,7,1000)` | **1000 f** | FUN_006dccb0 |
| **Dash while hot** | every frame of common dash (state 2), the dash T2 calls Kae's hook `T2[0x1ab]` = FUN_006dbd20: `if heat >= 2.0 (DAT_00813a4c) && !blazing → FUN_006b1a40(p,7,500)`, SE 0x27, effect 0xa8c (DAT_00881698) at pos | **500 f** | FUN_006af5e0 `case 0xe` → FUN_006dbd20 |
| Fire field (Kae effect 500) | applies status 7 to the **opponent** within 100 px, 500 f (§5.5) | 500 f | FUN_004fb350 |

`FUN_006b1a40` (core.md §6.8): refused if target state 6 or `+0xa2==0`; if already on, duration =
max(old, new) (a fresh Blazing! during Blazing extends to ≥1000); plays SE 0x26 (`jya05.wav`) for k=7.
Per frame while on (`FUN_00751750`, status table 0x881530+7*4): 2 RNG draws + effect 3000 (cosmetic),
spark `FUN_007bd5e0(0x4b0,…)` on even global frames, **heat := max(heat, 2.01)**; timer−−; when timer < 1
or state 6: flag off, **heat := 1.0** (DAT_00853b34).

**Effects of Blazing on Kae's moves** (all from the flag tests listed in §3):
| Move | Normal | Blazing (★) |
|---|---|---|
| A / D+A beams | damage forced to 100 (ver>0x58b) | 120 (spawn default) |
| B (hold) | 0x1b7 "Pow!" missiles | 0x1bb **"Fooom!"** fire field |
| S+A combo hitboxes | 100 / 0x6e / 0x78 / 0x82 | 0x65 / 0x6f / 0x79 / 0x83 (more damage + particles) |
| S+B "Baaang!" | one clone at t=15 | clones at t=10 **and** t=15 |
| H "ZOOOOOM!" | hitboxes 300 / 0x136 | 0x12d / 0x137 |
| B+H "BA-BA-BAANG!" | 9 clones | + 4 fire fields at t=10 |
| S+H | 0x1b9 **Blazing!** | 0x1c4 **"Whaaam!"** (grab) |
| S+B+H "WHAAAAM!" | finisher hitbox 500 | 0x1fe + opponent heat +0.015/f while held |
| Dash hook | can enter Blazing | (no effect; already on) |
Trade-off: while Blazing Kae's own heat is ≥ 2.01, so she takes ≥ ×2.01 damage from hitType 1/3 objects.

### 1.2 Grab pin (used by 0x1c5 and 0x1c7)
Each frame of the pin window, if `FUN_00780a50(opp)`: `opp.state = 1; opp.timer = 0;
opp.pos = p.pos + (cos,sin)(p+0x84)*40` (DAT_008140e0); `opp.speed(+0x98) = 0`;
`opp.aim(+0x84) = wrap(p+0x84 + π)` (FUN_005f0c60). Opponent's own T2 for state 1 runs later in the
frame (player order) or next frame — the pin is re-applied every frame so it cannot dash-cancel
(hitstun cancel needs t ≥ 15).

---

## 2. Input dispatch `FUN_006e0490` (standard scheme) — exact decode

**A. Hypers** — only if `FUN_00780950(p)`. First match wins; on failed spend the next row is tried.

| # | Condition | Cost | New state | Official move |
|---|---|---|---|---|
| 1 | S held (`+0x40>1`) && B held (`+0x3c>1`) && H pressed (`+0x44==3`) | 3.0 (DAT_00813a50) | 0x1c6 | **WHAAAAM!** (Accel, S+B+H) |
| 2 | S held && H pressed | 1.0 (DAT_00853b34) | blazing ? **0x1c4** : **0x1b9** | **Whaaam!★** / **Blazing!** (S+H) |
| 3 | B held && H pressed | 1.0 | 0x1c1 | **BA-BA-BAANG!** (B+H) |
| 4 | Dash held (`+0x34>1`) && H pressed | 1.0 | 0x1b8 | **POW!** (D+H) |
| 5 | H pressed | 1.0 | 0x1c3 | **ZOOOOOM!** (H) |

**B. Normal moves** — only if `state == 0 || state == 2`:

| # | Condition | Result | Move |
|---|---|---|---|
| 1 | Dash pressed | `FUN_006b17d0(p,0)` | Dash |
| 2 | else Shield pressed && `FUN_00780910(p,0.5)` && `FUN_007809e0(p)` | `FUN_006b1930(p,0)` | Shield |
| 3 | S held && B pressed | 0x1c0 | **Baaang!** (S+B) |
| 4 | S held && A pressed | 0x1bc | **Zooooom!** (S+A, 4-hit chain) |
| 5 | B pressed | blazing ? 0x1bb : 0x1b7 (both → 0x1b6 if B released before t=11) | **Fooom!★** / **Pow!** (B hold); B tap = 0x1b6 |
| 6 | Dash held (`+0x34>1`) && A pressed | 0x1b5 | **Sla-slaaaash!** (D+A) |
| 7 | A pressed | 0x1b4 | **Slash!** (A) |

Differences from Suguri: D+A and D+H test Dash `> 1` (held ≥ 2), not `> 0`. Row 6 (D+A) contains a compiled-out
branch (`xor eax,eax; jz` @0x6e08f1) that would have entered **0x1ba** instead of 0x1b5 (`if (0) 0x1ba else 0x1b5`, @0x6e08f1–0x6e0907) — 0x1ba is
unreachable (§3). Returns `T2[state](p) != 0`.

**Alternate scheme `FUN_006e09c0`** (`p+0x0c==1`): identical normal-move block; hyper rows are
"H held (`+0x44>1`) and X pressed": Shield→0x1c6 (3.0), S→0x1b9/0x1c4, B→0x1c1, **Dash pressed**→0x1b8,
A→0x1c3 (1.0 each).

Not in the manual: S+A hit-confirm chain 0x1bc→0x1bd→0x1be→0x1bf (A press after a confirmed hit, t ≥ 12);
B+H always ends with a lunge slash (0x1c2); dash with heat ≥ 2.0 enters Blazing for 500 f.

---

## 3. States

Cost table entries (`0x87c4f0 + id*0xc` = {costA, costB, hyperGain}, registry.c:2069-2101; ver patch
`FUN_004a7270` overrides 0x1b4 and 0x1c0 gains):

| State | Move | costA | costB | gain (ver ≥ 0x58c) |
|---|---|---|---|---|
| 0x1b4 | Slash! (per shot) | 0.1 (DAT_00853b10) | 0 | **0.011** (DAT_00813ecc; <0x58c: 0.01 DAT_00813ec8) |
| 0x1b5 | Sla-slaaaash! (per shot) | 0.05 (DAT_00813ef4) | 0 | 0.01 (DAT_00813ec8) |
| 0x1b6 | B tap (per missile) | 0 | 0.1 | 0.01 |
| 0x1b7 | Pow! (per pair) | 0 | 0.1 | 0.01 |
| 0x1ba | (unused) | 0.05 | 0 | 0.015 (DAT_00813ed4) |
| 0x1bb | Fooom! | 0 | 0.5 (DAT_00853b1c) | 0.025 (DAT_00813edc) |
| 0x1bc | Zooooom! hit 1 (also 0x1c2's slash) | 0 | 0 | 0.05 (DAT_00813ef4) |
| 0x1bd / 0x1be / 0x1bf | chain 2 / 3 / 4 | 0 | 0 | 0.03 (DAT_00813ee0) / 0.04 (DAT_00813eec) / 0.025 |
| 0x1c0 | Baaang! (per clone) | 0 | 0.25 (DAT_00813f24) | **0.045** (DAT_00813ef0; <0x58c: 0.05) |
Hypers (0x1b8, 0x1b9, 0x1c1–0x1c7) never call FUN_007807f0.

Common patterns: "aim" = `+0x84 = FUN_007804b0(p)`; "face" = `+0x84 = +0x88 = (opp.x <= x) ? π : 0`;
"flourish" = SE 6 (`up01.wav`) + `FUN_004e9280(100, 0.75)` + `FUN_007bec20(100, pos, 0, 0)` +
`FUN_004d7ac0(20, 0.5, 2)` [+ effect 0xfa where noted] (cosmetic, suguri.md §1.4).
"steer(n)" = if a direction is held (`FUN_0061b230 ≤ 2π`): rotate `+0x84` toward input angle by ≤ n rad
(`FUN_005f0e70`). "lunge(d, cap)" = aim, `+0x88 = +0x94 = aim`, `+0x98 = min(dist/d, cap)` (dist = FUN_00780570).
"sparks" = suguri.md §1.3 dash sparks (if speed ≥ 3.0 DAT_00813a50, dir = +0x84). Spawns:
beam `DAT_008818b0 = FUN_004ac290`, missile `DAT_008819f0 = FUN_00668720`, hitbox `DAT_00881bd0 = FUN_005e3290`,
enemy ("clone") `DAT_00881590 = FUN_00520990`, Kae effect `DAT_008816d0 = FUN_004f9cb0`, generic effect
`DAT_00881698`. `FUN_00612830/00612900` = training frame meter only.

### 0x1aa — neutral movement (T2 FUN_006dbc30)
Identical to Suguri 0x128 with `speed = stats+0 = 4.0` (DAT_00853b68 → 0x884788) per axis; `+0x84 = 0` if
facing else π. Returns 1.

### 0x1ab — dash hook (T2 FUN_006dbd20), called every frame of common state 2
See §1.1. Returns 1.

### 0x1ac, 0x1ad, 0x1ae — stubs (T2 FUN_006dbdd0 / 006dbdb0 / 006dbdc0 return 1)
Only their T1s are used (§4.3). 0x1af–0x1b3 have no handlers.

### 0x1b4 — Slash! (A) (T2 FUN_006dbde0)
| t | Action |
|---|---|
| 0 | aim |
| 0–19 | steer(0.1) (DAT_00853b10) |
| 12,14,16,18,20 (11<t<21, even) | if `FUN_007807f0(p,0x1b4)`: beam **100** at pos, angle +0x84; if ver>0x58b && !blazing → beam damage := 100 (`FUN_004aa6b0(idx)+0x84`); SE `FUN_007bcaf0(0x10,4)` (`laser03.wav`) |
| ≥28 | dash-cancel (612900 at 28) |
| >35 | state 0 |
Up to 5 shots, 0.1 A each.

### 0x1b5 — Sla-slaaaash! (D+A) (T2 FUN_006dbfb0)
| t | Action |
|---|---|
| 0 | aim |
| 10..18 (every frame) | if `FUN_007807f0(p,0x1b5)`: re-aim; angle = aim − (t−14)·0.08 if `|aim| ≤ π/2` else aim + (t−14)·0.08 (DAT_00813f00, DAT_00814104) → beam **200**; !blazing (ver>0x58b) → damage 100; SE `FUN_007bcaf0(0x10,3)` |
| every frame | friction `+0x9c *= 0.5` |
| ver ≥ 0x5a1: ≥33 dash-cancel (612900 at 33); >40 state 0 | (ver ≤ 0x5a0: ≥28 / >35) |
9 beams sweeping a 0.64-rad fan, 0.05 A each: aim+0.32 → aim−0.32 when `|aim| ≤ π/2`, aim−0.32 → aim+0.32 otherwise (the sweep sign flips with side, so it is mirror-symmetric).

### 0x1b7 — Pow! (B hold) (T2 FUN_006dc3f0)
| t | Action |
|---|---|
| 0 | aim |
| 0–10 | if B not held (`+0x3c < 1`) → state = 0x1b6 (timer kept), run it now |
| 18,20,…,28 (17<t<29, even) | if `FUN_007807f0(p,0x1b7)` (B −0.1): two missiles **100** at pos, angles `aim + ((t−21)/4)·0.7 − 0.3` and `aim − ((t−21)/4)·0.7 − 0.3` (DAT_00853b68, DAT_00813f38, DAT_00813f28; **both** subtract 0.3 — verified in disassembly @0x6dc4ef/0x6dc569); SE `FUN_007bcaf0(0x1f,4)` (`swing.wav`) |
| ≥40 | dash-cancel; >50 state 0 |
6 pairs (12 missiles), opening fan; aim fixed at t=0.

### 0x1b6 — B tap (T2 FUN_006dc270) — via 0x1b7/0x1bb early release
| t | Action |
|---|---|
| 11–23 | steer(0.1) |
| 12,14,…,22 (11<t<23, even) | if `FUN_007807f0(p,0x1b6)` (B −0.1): missile **100** at pos, angle +0x84; SE (0x1f,4) |
| ≥20 | dash-cancel; >30 state 0 |
Up to 6 missiles. (Aim comes from the parent state's t=0.)

### 0x1bb — Fooom!★ (B hold, Blazing) (T2 FUN_006dcb00)
| t | Action |
|---|---|
| 0 | face (horizontal) |
| 0–10 | B not held → re-aim (`+0x84 = FUN_007804b0`), state 0x1b6, run it |
| 15 | if `FUN_007807f0(p,0x1bb)` (B −0.5): **Kae effect 500** (fire field, §5.5) at pos, angle 0; SE 0x26 |
| ≥25 | dash-cancel; >40 state 0 |

### 0x1bc — Zooooom! hit 1 (S+A) (T2 FUN_006dce90)
| t | Action |
|---|---|
| 0 | aim; lunge(10 (DAT_00813f4c), 20 (DAT_008140fc)); if speed > 10: SE 0x24 (`swing_h.wav`) |
| every frame | sparks |
| 5 | SE 0x1f |
| 10 | if `FUN_007807f0(p,0x1bc)`: hitbox **blazing ? 0x65 : 100** at pos + dir(aim)·60 (DAT_00813a58), angle aim |
| <20 | if speed ≥ 1 && dist < 50 (DAT_008140ec): speed := 0 (stop in front) |
| ≥12 | hit-confirm (`+0x158`) && A pressed → 612830, state **0x1bd** (t=0, flag 0), run it |
| ≥35 | dash-cancel; >40 state 0 |

### 0x1bd / 0x1be — chain 2 / 3 (T2 FUN_006dd270 / FUN_006dd550)
| t | Action |
|---|---|
| 0 | lunge(15 (DAT_00853b88), 10) (no SE) |
| 5 | SE 0x1f |
| 8 | if cost ok: hitbox **0x6e/0x6f★** (0x1bd) or **0x78/0x79★** (0x1be) at pos + dir(aim)·40 (DAT_008140e0), angle aim |
| ≥12 | hit-confirm && A pressed → **0x1be** / **0x1bf** |
| ≥30 | dash-cancel; >40 state 0 |
(No sparks, no stop-in-front.)

### 0x1bf — chain 4 / finisher (T2 FUN_006dd830)
| t | Action |
|---|---|
| 0 | lunge(15, 10) |
| 5 | SE 0x20 (`swing_h.wav`) |
| 8 | if cost ok: `a = input angle`; if none (`> π`) `a = +0x84`; hitbox **0x82/0x83★** at pos + dir(**aim**)·40, **angle a** (knockback direction chosen by the held direction); then `+0x84 = a` |
| >34 (ver>0x5a0; else >40) | state 0. **No dash-cancel.** |

### 0x1c0 — Baaang! (S+B) (T2 FUN_006ddab0)
| t | Action |
|---|---|
| 0 | face |
| 15, and also 10 if blazing | if `FUN_007807f0(p,0x1c0)` (B −0.25): clone **100** (`FUN_00520990`); ver ≥ 0x5a1: clone pos = Kae pos + (dx, dy), dx = +70 if x+ held / −70 if x− held (DAT_008145ec / DAT_008256c8; x− wins), dy likewise for y+/y− (y− wins), else 0; clone angle (+0x58) = +0x84. (ver ≤ 0x5a0: random offset at angle rand_f(−π,π), radius rand_f(70,140) DAT_00817004.) SE `FUN_007bcaf0(0x2f,4)` (`power35.wav`); effect 0xaf0 (color = `DAT_008859f4+team*0x10`) at clone pos |
| ver ≥ 0x5a1: ≥25 dash-cancel; >40 state 0 | (≤0x5a0: ≥20 / >35) |
Blazing → two clones (both at the same offset), each costing 0.25 B.

### 0x1b8 — POW! (D+H, 1 level) (T2 FUN_006dc620)
| t | Action |
|---|---|
| 0 | aim; `FUN_006b1b40(p,10)`; flourish (no 0xfa) |
| 16,18,…,30 (15<t<31, even) | two missiles **0x96**: angles `aim + ((t−21)/4)·0.5 − 2.2` and `aim − ((t−21)/4)·0.5 + 2.2` (DAT_00853b1c, `_DAT_008256c4` = 2.2); SE (0x1f,4); no cost check |
| ≥40 | dash-cancel; >60 state 0 |
8 pairs = 16 missiles launched backward; they bounce off the arena wall straight at the opponent (§5.2).

### 0x1b9 — Blazing! (S+H, 1 level) (T2 FUN_006dccb0)
| t | Action |
|---|---|
| 0 | face; `FUN_006b1b40(p,10)`; flourish + effect 0xfa |
| 20 | `FUN_006b1a40(p,7,1000)` (**Blazing 1000 f**), SE 0x27 (`explosion1.wav`), effect 0xa8c at pos; `FUN_006b1d70(p, 160 (DAT_00814168), 18 (DAT_00814144))` — opponent within 160 px is knocked into hitstun at speed 18 away from Kae (SE 0x19) |
| >30 | state 0 (no dash-cancel) |

### 0x1c1 — BA-BA-BAANG! (B+H, 1 level) (T2 FUN_006ddee0)
| t | Action |
|---|---|
| 0 | face; `+0x88 = aim`; `FUN_006b1b40(p,10)`; flourish (no 0xfa) |
| 0,3,…,24 (t%3==0, t<25) | clone **0x96** at Kae pos + dir(a)·160 (DAT_00814168), clone angle = +0x84; `a = +0x88 + (t/6)·π/4 + π/8` if t%6==0 else `+0x88 − (t/6)·π/4 − π/8` (DAT_0081fa90, `_DAT_008256c0`); SE (0x2f,4); effect 0xaf0 at clone. **9 clones**, alternating sides in a widening ring |
| 10, blazing only | 4 **Kae effect 500** fire fields at Kae pos + dir((i/2)·π + π/4)·160 for i = −2..1 (angles −3π/4, −π/4, π/4, 3π/4); SE 0x26 |
| 50 | state **0x1c2** (t=0, flag 0), run it |
No dash-cancel.

### 0x1c2 — BA-BA-BAANG! follow-up slash (T2 FUN_006de3a0)
| t | Action |
|---|---|
| 0 | lunge(5 (DAT_00813f40), 24 (DAT_00853b8c)); SE 0x24 if speed > 10 |
| every frame | sparks |
| 5 | if `FUN_007807f0(p,0x1bc)` (free, +0.05): hitbox **0x104** at pos + dir(aim)·60, angle aim |
| <6 | if speed ≥ 1 && dist < 60: speed ×= 0.25 |
| ≥10 | dash-cancel; >30 state 0 |

### 0x1c3 — ZOOOOOM! (H, 1 level) (T2 FUN_006de6b0)
| t | Action |
|---|---|
| 0 | `+0x88 = aim`; `FUN_006b1b40(p,10)`; flourish + 0xfa |
| 10 | `+0x84 = aim` (re-aimed); speed 24 (DAT_00853b8c), dir +0x84; 5 sparks: `r = rand_f(−0.15, 0.15)` (DAT_00815dc4, DAT_00813f20), `n = rand_i(0,100)%4`, effect 0xa32+n, angle r + `+0x88`, age −3·i |
| 10–30 | if dist < 60: speed := 5 (DAT_00813f40) |
| 10–29 (every frame) | hitbox **300 / 0x12d★** at Kae pos, angle +0x84; if t%3 ≠ 0 its damage := 0 (`FUN_004b2eb0(idx)+0x3c`). Damage frames t = 12,15,18,21,24,27 (6 × 80 / 110★); the 0-damage boxes still add stun 1.0 and knock back (core.md §6.2), so the target is kept in hitstun every frame |
| 10–29, t%3==0 | **Kae effect 600** at pos, angle +0x84 (cone sprite) |
| 10–29, speed > 5 | trail particle `FUN_007bd5e0(0x7da, …)` at random offset (2 RNG draws: angle rand_f(−π/2, π/2); offset (cos·30−10, sin·50) rotated by +0x84) |
| 30 | hitbox **0x136 / 0x137★** at pos, angle +0x84 + π; then if speed < 10 → speed 10 |
| every frame (unless cancelling) | friction ×0.5 |
| ≥50 | dash-cancel; >80 state 0 |

### 0x1c4 — Whaaam!★ (S+H while Blazing, 1 level) (T2 FUN_006decf0)
| t | Action |
|---|---|
| 0 | aim; `FUN_006b1b40(p,10)`; SE 6; `FUN_004e9280(100,0.75)`; `FUN_004d7ac0(20,0.5,2)` |
| 10 | re-aim; speed 18 (DAT_00814144), dir aim; SE 0x20 |
| 10–20 | if dist < 70 (DAT_008145ec) && `FUN_00780a50(opp)`: 612830; pin opponent once (§1.2); state **0x1c5** (t=0, flag 0); speed ×= 0.25; run T2[0x1c5]; camera focus `FUN_005f8fc0(80, ownerId, pos, 0.4)` (DAT_00813f30); flourish + 0xfa; return |
| every frame | sparks |
| ≥40 | dash-cancel; >60 state 0 (whiff) |

### 0x1c5 — Whaaam! hold (T2 FUN_006df110)
| t | Action |
|---|---|
| 0–80 | pin opponent (§1.2) |
| 28,36,44,52,60,68,76 ((t−20)%8==0, 27<t<84) | `r = rand_f(−0.1, 0.6)` (DAT_00814130, DAT_00813f34); if (t−20)%16==0 (t=36,52,68): `r = −r`, id **0x6e**, else id **100**; angle = +0x84 + r; hitbox id at pos + dir(angle)·50 (DAT_008140ec), angle. (Always the non-★ ids.) |
| 84 | hitbox **0xbe** at pos + dir(+0x84)·50, angle +0x84; effect 0xa8c there |
| ≥100 | dash-cancel; >120 state 0 |
Total if all land: 4×100 + 3×60 + 150 = 730 base (before heat multiplier).

### 0x1c6 — WHAAAAM! (S+B+H, Accel, 3 levels) (T2 FUN_006df4f0)
| t | Action |
|---|---|
| 0 | aim; `FUN_006b1b40(p,0x14)` (Accel barrier); SE 7 (`high_a.wav`); `FUN_004e9280(50, 0.85)` (DAT_00814120); `FUN_007bec20(50,pos,0,0)`; `FUN_004d7ac0(50, 0.75, 2)` |
| 10 | re-aim; speed 15 (DAT_00853b88); SE 0x20 |
| 10–20 | if dist < 70 && grabbable: 612830(p,1,0,0); pin once; state **0x1c7**; speed ×0.25; `+0x84 = +0x88 = (opp.x <= x) ? π : 0` (after the pin); run T2[0x1c7]; camera focus(80, …, 0.4); `FUN_004e9280(100,0.85)`; `FUN_004d7ac0(100,0.6,2)`; `FUN_007bec20`; effect 0xfa; if blazing SE 0x26 |
| every frame | sparks |
| ≥100 | dash-cancel; >120 state 0 (whiff) |

### 0x1c7 — WHAAAAM! hold + blast (T2 FUN_006df9a0)
| t | Action |
|---|---|
| 0–70 | pin opponent (§1.2) (horizontal, since +0x84 is 0/π); if blazing: `opp.heat += 0.015` (DAT_00813ed4) per pinned frame (≈ +1.07 total) |
| 75 | hitbox **500** (blazing: **0x1fe**) at pos + dir(+0x84)·50, angle π/2 (DAT_00814104); effects 0xa8c, 600 (generic) there; 5 × effect 0x834 and 6 × generic effect 500 (ages −5…−30) at the opponent ± rand (offsets rand_f(−50,50) / rand_f(−60,60), 2 draws each); SE 0x27 |
| 9..75, t%3==0 | cosmetic: spark 0xa82+rand%8 under Kae (alpha 0.5), particle `FUN_0068b8a0(1000,…)` (3+ RNG draws each) |
| 10..70 even | particle `FUN_0068b8a0(0x44c,…)` |
| blazing, t<61, t%5==0 | Kae effect **0x208** at (x, y−50) |
| blazing, t<61 even | effect 3000 at random offset |
| ≥90 | dash-cancel; >100 state 0 |
(Cosmetic RNG order within a frame is exactly the source order above — matters for replay parity.)

### 0x1ba — unused D+A variant (T2 FUN_006dc860) — unreachable (§2)
t=0 aim; t 10..18 every frame: if `FUN_007807f0(p,0x1ba)`: beam **300** at pos, angle `aim + (t−14)·(|aim| ≤ π/2 ? 0.1 : −0.1)` (DAT_00853b10 / DAT_00814130) (also computes an unused offset vector); dash-cancel ≥30; >50 state 0. Port only for completeness.

---

## 4. Animation

### 4.1 Sprite registration (loader `FUN_006d86a0`)
Arg = colour variant 0/1/2 → folder text ids 0xe/0xf/0x10 (charaKaeA/B/C). Same mechanics as suguri.md §4.1
(S×S quad, `FUN_00467d00(S,S,2,2,1.0,0)`, bank `0x885b80 + slot*0x3888 + id*0x18`).

| Sprite id | Pattern | Frames | S | Used by |
|---|---|---|---|---|
| 0x1aa | `ke_std0%d` | 5 | 96 (DAT_00814118) | neutral; clone 100 waiting |
| 0x1af | `ke_fwd0%d` | 3 | 96 | neutral moving toward |
| 0x1b0 | `ke_bck0%d` | 3 | 96 | neutral moving away |
| 0x1ac | `ke_los0%d` | 6 | 96 | T1 0x1ac, 0x1ad |
| 0x1ae | `ke_sld0%d` | 4 | 96 | T1 0x1ae, 0x1b9–0x1bb, 0x1c0, 0x1c1; clone appear/leave |
| 0x1ab | `ke_dsh0%d` | 6 | 96 | dash |
| 0x1b1 | `ke_rng0%d` | 2 | 512 (DAT_008178b0) | stats +0x3c (UI) |
| 0x1b3 | `ke_system` | 1 | 512 | stats +0x44 (UI) |
| 0x1b4 | `ke_atka0%d` | 6 | 96 | 0x1b4, 0x1b5 |
| 0x1b6 | `ke_atkb0%d` | 6 | 96 | 0x1b6–0x1b8 |
| 0x1c3 | `ke_hyp0%d` | 8 | 96 | 0x1c3, parts of 0x1c5/0x1c7 |
| 0x1bc | `ke_infghta0%d` | 4 | 96 | 0x1bc, 0x1c2, 0x1c5 |
| 0x1bd | `ke_infghtb0%d` | 5 | 96 | 0x1bd, 0x1c4–0x1c7 |
| 0x1be | `ke_infghtc0%d` | 4 | 96 | 0x1be |
| 0x1bf | `ke_infghtd0%d` | 7 | 96 | 0x1bf, 0x1c5; clone rush |
| 0x1c8 | `ke_pile0%d` | 3 | 64 (DAT_008140f8) | missiles |
| 0x1c9 | `ke_moon0%d` | 2 | 64 | beams 100/200; Kae effects 200/0xfa |
| 0x1ca | `ke_blade0%d` | 4 | 64 | Kae effect 300 |
| 0x1cb | `ke_field` | 1 | 64 | Kae effects 0x1fe/0x208 |
| 0x1d0 | `ke_cone0%d` | 3 | 96 | Kae effect 600 |
All 83 `ke_*.png` in `extracted/texture/charaKaeA/` are covered (plus res_bgp/res_chr/res_sys). ids 0x1ad, 0x1b2, 0x1b5, 0x1b7–0x1bb, 0x1c0–0x1c2, 0x1c4–0x1c7, 0x1cc–0x1cf not registered (0x1b2 is referenced by stats +0x40 only).

### 4.2 Draw transforms
Identical to Suguri (suguri.md §4.2): all non-neutral states use the AIM transform (rotZ(+0x84); if
`|aim| ≥ π/2` rotX(180°); rotY(180°)); neutral (0x1aa) only rotY(180°) when facing. Global tint
`FUN_004e8f50` first; normal alpha blend; single body sprite per state (no blade/slash layers).
Afterimage `FUN_004d7e00` only in 0x1aa and 0x1ab.

### 4.3 Common states → Kae draw handlers (verified from FUN_006b07a0 … FUN_006b1580)
| Common state | Kae T1 |
|---|---|
| 0 neutral | 0x1aa FUN_006e2310 |
| 1 hitstun, 5 | 0x1ad FUN_006e29f0 |
| 2 dash | 0x1ab FUN_006e25f0 |
| 3 shield, 4 | 0x1ae FUN_006e2cc0 |
| 6 KO | 0x1ac FUN_006e2f80 |

### 4.4 Per-state frames (`pp(n)` = `(t/n)&3`, 3→1 → 0,1,2,1)
| State | T1 | Sprite | Frame |
|---|---|---|---|
| 0x1aa | FUN_006e2310 | 0x1aa; overridden like Suguri (y+→0x1af, y−→0x1b0, x−: facing0→0x1af / facing1→0x1b0, x+: facing0→0x1b0 / facing1→0x1af; later wins) | std: `f=(t/5)%8; if f>4: f=8−f` (0,1,2,3,4,3,2,1); fwd/bck: pp(5) |
| 0x1ab | FUN_006e25f0 | 0x1ab | pp(3), +3 if moving against facing (facing1 && \|aim\|>π/2, or facing0 && \|aim\|<π/2) |
| 0x1ac | FUN_006e2f80 | 0x1ac | pp(3) + 3 if global frame `DAT_008858f8` even |
| 0x1ad | FUN_006e29f0 | 0x1ac | pp(3) |
| 0x1ae, 0x1b9, 0x1bb, 0x1c0, 0x1c1 | 6e2cc0 / 6e4640 / 6e4380 / 6e5400 / 6e56c0 | 0x1ae | min(t/5, 3) |
| 0x1ba | FUN_006e40e0 | 0x1ae | 0 |
| 0x1b4, 0x1b5 | 6e3280 / 6e3560 | 0x1b4 | t<8 ? 0 : min((t−8)/4+1, 5) |
| 0x1b6, 0x1b7, 0x1b8 | 6e3840 / 6e3b20 / 6e3e00 | 0x1b6 | t<10 ? 0 : min((t−10)/4+1, 5) |
| 0x1bc, 0x1c2 | 6e4900 / 6e5980 | 0x1bc | min(t/5, 3) |
| 0x1bd | 6e4bc0 | 0x1bd | min(t/4, 4) |
| 0x1be | 6e4e80 | 0x1be | min(t/5, 3) |
| 0x1bf | 6e5140 | 0x1bf | min(t/3, 6) |
| 0x1c3 | 6e5c40 | 0x1c3 | t<15 ? min(t/3, 4) : pp-style `((t−15)&3, 3→1) + 5` (5,6,7,6) |
| 0x1c4, 0x1c6 | 6e5f40 / 6e65c0 | 0x1bd | min(t/5, 4) |
| 0x1c5 | 6e6200 | t<20: 0x1c3 min(t/5,3); 20≤t≤76: if (t−20)%16 < 8 → 0x1bc min(((t−20)%8)/3, 3) else 0x1bd min(((t−20)%8)/3, 4); t>76: 0x1bf min((t−76)/3, 6) | (sprite choice @0x6e6209–0x6e6326) |
| 0x1c7 | 6e6880 | t<70: 0x1bd, `f=t/5; if f≥2: f=((f−2)&3)+2; if f==5: f=3` (0,1,2,3,4,3,2,3,4,…); t≥70: 0x1c3 min((t−70)/4, 3) | |

---

## 5. Spawned objects (Kae = type 14)

Table addresses: beam spawn/update/draw/onHit 0x8818b0/0x881900/0x881950/0x8819a0 = FUN_004ac290 /
004acc90 / 004ac450 / 004aca30; missile 0x8819f0/0x881a40/0x881a90/0x881ae0/0x881b30 = 668720 / 6692e0 /
6688c0 / 669050 / 6691b0; hitbox 0x881bd0/0x881c20 = 5e3290 / 5e39d0; enemy spawn/draw/update/killed
0x881590/0x8815e0/0x881630/0x881680 = 520990 / 5217a0 / 520ae0 / 521780; effect spawn/update/draw
0x8816d0/0x881720/0x881770 = 4f9cb0 / 4fb350 / 4fa1f0. Kae has no laser or whip entries.
Field names per objects.md §2. Collision/damage rules per objects.md §4 (beams/hitType-1 hitboxes pass
through dashing targets; hitType-3 hitboxes and missiles do not).

### 5.1 Beams (FUN_004ac290 / FUN_004acc90)
Init `FUN_004ba490` (hitType 1, stun 1.0, scale/alpha 1.0). Per id:

| id | speed | shape {hx,hy,r} | dmg | knock | destroyOnHit / active | update |
|---|---|---|---|---|---|---|
| 100 (Slash!) | 12 (DAT_00813f54) | {0,0,40} (DAT_008140e0) | 120 (0x78); **100** when fired while not Blazing (ver>0x58b, set by 0x1b4) | 10 | 1 / 1 | straight; dies when outside arena+30 (`FUN_0061ac10(pos,30)` DAT_008140e8); Kae effect 200 every frame (trail) |
| 200 (Sla-slaaaash!) | 12 | {0,0,30} | 120 / 100 (not Blazing) | 10 | 1 / 1 | if speed > 10: speed −= 0.5 (→10 after 4 frames); **at age 10: damage := (dmg==120 ? 50 : 40)** (ver ≥ 0x58c; older: 50); dies outside arena+30; Kae effect 0xfa every frame |
| 300 (unused) | 6 (DAT_00813f44) | none | 0 | 0 | 0 / 0 | straight; at ages 12 and 20: hitbox **400** at pos, angle; dies at age > 40; effect 3000 at pos ± rand_f(−20,20) (DAT_00814108) every frame |

onHit `FUN_004aca30`: 100 → effect 0x9c4 (random angle) + Kae effect 300 (random angle), SE 0x13
(`don16.wav`); 200 → effect 0x3f2 20 px ahead (DAT_008140fc), particles `FUN_0068b8a0(1000,…,4)`,
sparks `FUN_007bd5e0(1000,…,6,4,…)`, SE 0x13.
Draw `FUN_004ac450`: sprite **0x1c9** (`ke_moon`) from the team's sprite bank (slot `DAT_00885a00+team*0x10`,
2-D sprite vector at +0xc), two passes: frame 1 with blend arg 1 then frame 0 with blend arg 2 (additive),
rect half-size (64·scale, 64·scale)/zoom for 100, (64·scale, 32·scale)/zoom for 200 (DAT_008140f8, DAT_008140dc),
rotation −angle(+0x64), colour ARGB(255·alpha, white).

### 5.2 Missiles (FUN_00668720 / FUN_006692e0)
Both ids: growScale(+0x5c) = 0, speed 12, hp 60, body radius 10, proj radius 10, damage **40**, knockback 10
(DAT_00813f4c), hitType 2 (missiles hit dashing players).

| | 100 (B tap / Pow!) | 0x96 (POW!) |
|---|---|---|
| grow | +0.1/frame to 1.0 | same |
| early | age < 10: speed −= 1.0 (12 → 2) | age < 20: speed −= 0.5 (12 → 2) |
| later | age > 20 and speed < 10: speed += 0.5 | age > 40 and speed < 15 (DAT_00853b88): += 0.5 |
| trail | age > 20: Kae effect 100 at pos, angle +0x6c | age > 20: Kae effect 0x96 (after the move) |
| move | `pos += dir(+0x68)·speed` | same |
| wall (age < 1000, outside arena: `FUN_0061ac10(pos,0)` false) | **reflect**: `w = atan2(y,x)`; heading = (w − heading) + w + π (**not wrapped** — the FUN_005f0ce0 result is discarded @0x669507); pos = dir(w)·540 (DAT_0081412c); draw angle = heading; age := 1000; SE (0x25,5) (`reflect.wav`); effect 0x516 | **redirect**: pos = dir(w)·540; heading = atan2(opp − pos) (straight at the opponent's current position); draw angle = heading; age := 1000; SE (0x25,5); effect 0x516 |
| after bounce (age ≥ 1000) | if outside arena+30: id := 0xbe, active(+0x7c) := 0 (fade) | same |
| 0xbe (fading) | growScale −= 0.1; removed at ≤ 0 | |
Because age jumps to 1000, the "age > 20" acceleration/trail rules stay active after a bounce.
onHit (vs player) `FUN_00669050`: effect 0x9c4, sparks, SE 0x15 (`crash17.wav`); returns 0 → destroyed.
onKilled (shot down) `FUN_006691b0`: effects 0x262 and 0x514, SE (0x1d,4) (`gun12.wav`); destroyed.
Draw `FUN_006688c0` (ids 100, 0x96, 0xbe): sprite 0x1c8 (`ke_pile`): frame 0 scaled (3 − 2s, s, 1)
(s = growScale), colour (a,r,g,b) = (1,1,1,1) when s == 1 else (1,0,0,0) (black silhouette while growing —
see Notes 1); rotZ(+0x6c); then, only when s == 1, blend 2 (additive) frames 1 and 2 with alpha
`sin((age%50)/50·2π)·0.15 + 0.85` and `·0.25 + 0.75` (DAT_00813f20, DAT_00814120, DAT_00813f24, DAT_00813f3c); blend back to 1.

### 5.3 Hitboxes (FUN_005e3290 / on-hit FUN_005e39d0)
All hitType **3** (hit dashing targets), stun 1.0, dealsDamage 1, oncePerFrame 0. Values for ver ≥ 0x58c
(older in parentheses):

| id | Spawned by | dmg | shape {hx,hy,r} | knock | on-hit |
|---|---|---|---|---|---|
| 100 / 0x65★ | 0x1bc hit 1; 0x1c5 (100) | 100 / 120 (120/140) | {100, 80, 0} (DAT_00853b90, DAT_00814164) | 2.5 (DAT_008153e0) | effects 0x9c4, 0xaf0, particles, sparks; **sets owner hit-confirm**; SE (0x18,3) (`hit.wav`); ★: 5 × (effect 3000 + spark 0x4b0) at victim ±30 |
| 0x6e / 0x6f★ | 0x1bd; 0x1c5 (0x6e) | 60 / 70 (80/90) | {100, 80, 0} | 2.5 | as above without 0xaf0; sets hit-confirm |
| 0x78 / 0x79★ | 0x1be | 40 / 60 (60/80) | {80, 80, 0} | 2.5 | as 0x6e; sets hit-confirm |
| 0x82 / 0x83★ | 0x1bf | 180 / 270 (160/210) | {80, 80, 0} | **25** (DAT_008140f0) (old 20) | effects 0x9c4, 0xaf0, particles, sparks; SE 0x19 (`hit_f.wav`); shake `FUN_004d7ac0(10,0.75,1)`; ★ particles. **No hit-confirm** |
| 0xbe | 0x1c5 finisher | 150 | {80, 80, 0} | 30 (DAT_008140e8) | as 0x82 (SE 0x19, shake) |
| 200 | clone 100 attack | 180 | {0, 0, 60} (DAT_00813a58) | 10 | effects, SE (0x18,3) |
| 0xfa | clone 0x96 attack | 110 | {0, 0, 60} | **0** | effects (0xaf0 at the hitbox pos), SE (0x18,3) |
| 0x104 | 0x1c2 | 160 | {0, 0, 100} | 30 | effects 0xa8c, 0xaf0; SE 0x19; shake |
| 300 / 0x12d★ | 0x1c3 (every frame, 0 dmg on t%3≠0) | 80 / 110 | {0, 0, 80} | 5 (DAT_00813f40) | only if damage ≠ 0: effects 0x9c4, 0xaf0, SE (0x18,3) |
| 0x136 / 0x137★ | 0x1c3 t=30 | 120 / 160 | {0, 0, 80} | 15 (DAT_00853b88) | effects 0xa8c, 0xaf0, SE 0x19, then the 400 handler: 4 × effect 0x456 fan (rand), **victim heat += 0.1**, SE (0x13,4) |
| 400 | beam 300 (unused) | 60 | {0, 0, 40} | 5 | 4 × 0x456, **victim heat += 0.1**, SE (0x13,4) |
| 500 / 0x1fe★ | 0x1c7 blast | **1500 / 1800** (ver>0x5a0; older 1000/1600) | {80, 80, 0} | 30 | effect 0x9c5 at victim, angle π/2 |
None of these modify `+0x40` after spawn except 0x1c3's damage zeroing. Callback RNG order is as listed.

### 5.4 Clones — Enemy family (FUN_00520990 / FUN_00520ae0 / FUN_005217a0; onKilled FUN_00521780 returns 0)
Spawn (ids 100 and 0x96): `FUN_0055ee70` init, type 14, `+0x10 = 100`, subtype `+0x7c = id`, phase
`+0x84 = 0`, hp/maxHp 9999, **active(+0x2c) = 0, shape 0 → not collidable** (cannot be shot down),
scale (0, 3.0, 0) (DAT_00813a50), `+0x6c = 0`, motion 0. The caller writes pos (+0x40/+0x44) and angle
(+0x58). `t2` = `+0x80` (incremented after each update; set to 0 on phase changes).

| Phase (+0x84) | id 100 (Baaang!) | id 0x96 (BA-BA-BAANG!) |
|---|---|---|
| 0 appear | scaleY −= 0.2 (DAT_00853b14), scaleX += 0.1; when scaleX ≥ 1: scale (1,1), phase 100, t2 = 0 (10 frames) | same |
| 100 wait | when t2 > **100**: phase 200, t2 = 0; angle(+0x58) = angle2(+0x5c) = atan2(opp − pos); speed(+0x70) = min(dist/12, **15**); 5 sparks 0xa32+rand%4 (angle rand_f(−0.15,0.15)+angle2); SE 0x1f | when t2 > **10**: same with speed cap **18** (DAT_00814144); SE 0x1f before the sparks |
| 200 rush | pos += dir(angle2)·speed; speed −= **0.45** (DAT_00817c0c; ver<0x58c 0.5), clamp ≥ 0; t2 == 10: hitbox **200** at pos + dir(angle)·40, angle; t2 > 40 → phase 900, t2 = 0 (ver 0x58a–0x58b only: a second rush, phase 300, speed min(dist/16, 10), decel 0.5, same hitbox) | speed −= 0.5; t2 == 10: hitbox **0xfa**; t2 > 40 → 900 |
| 900 vanish | scaleY += 0.2, scaleX −= 0.1; removed when scaleX ≤ 0 | same |
Total lifetime id 100: 10 + 101 + 41 + 10 ≈ 162 frames; the strike lands ~121 frames after spawn.
Draw `FUN_005217a0`: sprite by phase — 100: 0x1aa (`ke_std`) `f=(t2/10)%8, f>4 → 8−f`; 200: 0x1bf min(t2/3, 6);
else 0x1ae min(t2/5, 3). Drawn twice with colour (a=0.5, 1,1,1): pass 1 normal blend, pass 2 additive
(blend 2), scale (scaleX, scaleY, 1), pos, AIM transform with angle +0x58; blend restored to 1.

### 5.5 Kae effects (FUN_004f9cb0 / FUN_004fb350 / FUN_004fa1f0)
Effect record per objects.md (`+0x28` age, incremented before update; negative = delayed; `+0x2c` scale,
`+0x30` alpha (init 1.0), `+0x34/+0x38` angle). **Only id 500 has gameplay effect.**

| id | Update | Draw |
|---|---|---|
| **500 fire field** (Fooom!★, BA-BA-BAANG!★) | removed when age > 480 (0x1e0); each frame: `o = opponent(team)`; if `o+0xaa == 0` && `|o.pos − field.pos| < 100` (FUN_005f1c30, DAT_00853b90) → **`FUN_006b1a40(o, 7, 500)`** (opponent forced into Blazing/overheat: heat ≥ 2.01 for 500 f, then heat = 1.0); age%10==0: Kae effect 0x1fe at field pos, random angle (1 RNG); even age: generic effect 3000 at field pos + dir(rand_f(−π,π))·rand_f(10,80) (2 RNG) | not drawn itself |
| 0x1fe / 0x208 | alpha −= 0.02, scale += 0.1; removed at alpha ≤ 0 | sprite 0x1cb `ke_field`, half-size 48·s (0x1fe, two passes blend 1 then 2, alpha 128·a) / (96·s, 28·s) (0x208, alpha 96·a) |
| 100 / 0x96 (missile trails) | alpha −= 0.075 / 0.05 | common atlas `beamemitb` (`DAT_0087fdf4`, loaded all.c:132446): src cell x = (variant/8)·240, y = (variant%8)·64, 120×64; 100: half (32s, 20s), alpha 64·a, blend 2; 0x96: same cell half (48s, 32s) alpha 64·a + right half-cell (x+120) half (32s, 20s·a), grey = alpha 128·a, blend 2 |
| 200 / 0xfa (beam trails) | alpha −= 0.1 | 0x1c9 `ke_moon`, half-size 72·s (0xfa: 72×36), alpha 144·a |
| 300 (beam-100 hit flash) | scale += 0.075, alpha −= 0.05, angle(+0x38) += 0.125 | 0x1ca `ke_blade`, half-size 72·s |
| 600 (ZOOOOOM! cone) | scale += 0.1, alpha −= 0.1 | 0x1d0 `ke_cone` frame 0, half-size 96·s, additive, rotated by angle |
The generic effect ids used (0xa28–0xa2f, 0xa32–0xa35 sparks, 0xa82–0xa89, 0xa8c, 0xaf0, 0xfa, 0x9c4/0x9c5, 0x3f2,
0x456, 0x516, 0x262, 0x514, 0x834, 500, 600, 3000) are cosmetic but consume RNG where noted.

---

## 6. Sound ids used (SE table all.c:464010; id = (addr − 0x9b99a0)/4)
| id | file | where |
|---|---|---|
| 6 | up01.wav | hyper start (0x1b8, 0x1b9, 0x1c1, 0x1c3, 0x1c4) |
| 7 | high_a.wav | WHAAAAM! start |
| 0x10 | laser03.wav | Slash!, Sla-slaaaash! |
| 0x13 | don16.wav | beam hits; 0x136/0x137/400 hits |
| 0x15 | crash17.wav | missile hits player |
| 0x18 | hit.wav | combo / clone / ZOOOOOM! hits |
| 0x19 | hit_f.wav | heavy hits (0x82/0x83, 0xbe, 0x104, 0x136/0x137); proximity knock |
| 0x1d | gun12.wav | missile shot down |
| 0x1f | swing.wav | missile launch, combo swings, clone rush |
| 0x20 | swing_h.wav | combo finisher, Whaaam!/WHAAAAM! dash |
| 0x24 | swing_h.wav (2nd entry) | long lunge (speed > 10) |
| 0x25 | reflect.wav | missile wall bounce |
| 0x26 | jya05.wav | status 7 applied (inside FUN_006b1a40); Fooom!; fire fields |
| 0x27 | explosion1.wav | Blazing start; WHAAAAM! blast |
| 0x2f | power35.wav | clone summon |

---

## 7. Other per-character table entries (index 14)
| Table (real base) | Kae entry | Role |
|---|---|---|
| 0x881310 | FUN_006e0490 | input dispatch, standard (§2) |
| 0x881360 | FUN_006e09c0 | input dispatch, alternate (§2) |
| 0x8813b0 | FUN_006d86a0 | sprite loader (§4.1) |
| 0x881400 + 0x10·14 + 4·level | FUN_006e1970 (level 0), FUN_006e0ef0 (levels 1, 2) | CPU AI (reads +0xaa and +0x160; not reversed) |
| family tables | §5 | |

## 8. Stats record (index 14, base 0x884788), FUN_0049ae40 (registry.c:2027-2068)
| Off | Value | Meaning |
|---|---|---|
| +0x00 | 4.0 (DAT_00853b68) | walk speed per axis |
| +0x04 | 17.0 (`_DAT_00813f5c`) | dash speed |
| +0x08 | 11.0 (DAT_00813f50) | dash hold speed (core.md §4.3) |
| +0x0c..+0x2c | 11, 5, 0, 4, 10, 0, 11, 5, 0 | per-palette team colour slots {k0,k1,k2} (palette 0: 11,5,0; 1: 4,10,0; 2: 11,5,0) → `DAT_008859f4+team*0x10` (objects.md §6, FUN_005f5780) |
| +0x3c/+0x40/+0x44 | 0x1b1, 0x1b2, 0x1b3 | sprite ids (ring / unregistered / system) |
| +0x48/+0x60/+0x78 | folder strings (text 0xe/0xf/0x10) | |
| +0xa8 / +0x1a8 | `mdl\win_font_KE.lfd/.dat` | |
| +0x2a8 | `ke_res_bgp` | |
| +0x2c0/+0x2c4/+0x2c8 | −100, 80, 0.75 | UNVERIFIED (portrait offset/scale?) |
| +0x2cc/+0x2d0/+0x2d4 | 240, 100, 100 | colour (RGB) |
| +0x2d8 | 0 | |

## 9. UNVERIFIED / open items
Resolved in the verification pass (2026-11-09):
- **Facing**: character art faces −x (e.g. `sg_rfl03` gun points left). Original RotY(π) = mirror → engine `flipX = 1`; neutral mirrors when `+0x8c != 0`; models drawn without RotY (missile `ke_pile`) are unmirrored. Confirmed with `tests/run_drawdump.sh` renders (attacks point at the opponent).
- CModel `+0xdc` order (a, r, g, b) — adopted in render.md / PORTING.md.
- Effects 100/0x96 texture = `beamemitb` atlas (§5.5).
- Stats +0x0c..+0x2c = team colour slots (§8).
- Behaviour checked by `tests/test_kae.c` (81 checks: dispatch both schemes, spawn frames, damage, costs, end/cancel frames, chain, Blazing sources, grabs, clones).

Still open:
- Stats +0x2c0..+0x2c8 (UI offsets/scale; no gameplay effect).
- ver ≤ 0x5a0 branches (old-replay playback only; not ported).
- Hitstun-motion detail inside the grab pin: the pinned opponent is rewritten every frame, so player update order does not change the outcome.

