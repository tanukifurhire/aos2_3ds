# Kyoko — complete moveset spec

Source: `aos2.exe` (Ghidra corpus `tools/re/dec/all.c`, constants read with `xref.exe rd`).
Every number cites its address. `t` = player state timer `p+0x154`. Structure follows suguri.md;
shared helpers (FUN_007807f0 cost table, FUN_006b1b40 hyper start, FUN_00780910 hyper spend,
FUN_00780950 hyper permission, dash-cancel `FUN_006b17d0(p,1)`, FUN_00612830/00612900 training-only
frame-data recorders, sparks, RNG) are as documented in suguri.md §1 and objects.md and are not
re-derived. `ver` = DAT_0089426c = **0x5a2 in live play** (objects.md §0.1); the newest branch is
given, older branches are mentioned in brackets.

Kyoko = handbook id 12, **engine index 15** (`p+0x10 == 15`, object `type == 15`), states 0x1d1–0x1e7,
sprite prefix `ky_`, texture folders `charaKyokoA` / `charaKyokoB`.

---

## Notes for the lead (affect other specs)

1. **render.md colour call order is wrong.** CModel `+0xdc` = `FUN_0046ef70(a, r, g, b, meshIdx)`:
   param_2 is written to the material's 4th float (alpha), param_3..5 to r, g, b (FUN_0046ef70:
   `local_40 = param_2; local_4c = param_3; local_48 = param_4; local_44 = param_5`, D3DCOLORVALUE
   order r,g,b,a). Kyoko's draws confirm it: e.g. glow passes call `(pulse, s, s, s)` with additive
   blend and the team-tint passes call `(1.0, s·k, s, s)`.
2. **objects.md §4.5 mislabels type 15.** "Sumika type 15 `FUN_0066d190`" — type 15 is **Kyoko**
   (engine index 15 = handbook id 12). FUN_0066d190 is Kyoko's mirror-reflect callback (§5.2).
3. **The `p18` field** carried by Missile `+0x1c`, Laser `+0x14`, Enemy `+0x18` is the owner's
   **player slot** (`p+0x18`): Kyoko's missile / laser / enemy draws use it as the sprite bank index
   `0x885b80 + p18*0x3888` (e.g. missile draw @0x66a0e1 `imul eax,[0x9b3578],0x3888` where
   `DAT_009b3578 = m+0x1c`), and to pick the P1/P2 tint (`p18 == 0` vs else).
4. MSVC idiom `and eax,0x80000000; jns; dec; or eax,-1; inc` = **`t % 1`** (always 0) — Ghidra prints
   it as `t & 0x80000000`. Kyoko 0x1db uses it (@0x6e9d7a) → fires every frame.
5. Draw handlers communicate through static globals (`DAT_009b3570..358c` missile draw,
   `DAT_008958b0..f8` effect draw). Kyoko missile 500's first pass uses `DAT_009b3580` **without
   setting it** (stale value from the previous missile drawn) — cosmetic, but a faithful port must
   keep a shared "last glow alpha" variable or accept the difference.
6. Common states → Kyoko T1 mapping follows the same pattern as Suguri (§4.3) — the common T1s
   switch on `p+0x10` with `case 0xf`.

---

### Player fields used by Kyoko's code
Same as suguri.md table; additionally:

| Off | Name | Notes / source |
|---|---|---|
| +0x88 | copy of aim | written together with +0x84 in states that face the opponent by x side (0x1df, 0x1e0, 0x1e2, 0x1e5, 0x1e7) |
| +0x128 | shown combo-hit count (engine `comboShowHits`) | FUN_00780ab0: on each hit the attacker's +0x128 = 1 if its display timer +0x130 < 1 else +1 (cap 99); +0x130 = 120, zeroed by FUN_00780650 once the victim leaves hitstun. The Crystal field (§5.5) spawns its hitbox only while owner `+0x128 < 21` (ver ≥ 0x5a1) — i.e. at most ~20 field hits per combo |

---

## 1. Shared mechanics used by Kyoko states

Identical to suguri.md §1 (frame model, dash-cancel, cost table, hyper start/permission). Specifics:

- **"Face by x"** (FUN_006eac00 etc.): `opp = FUN_00780480(p+0x14)`; if `opp.x <= p.x` → `+0x84 = +0x88 = π`
  (DAT_00853b58) else `0`. Used by 0x1df, 0x1e0, 0x1e2, 0x1e5, 0x1e7. All other states use
  aim = `FUN_007804b0(p)`.
- **Hyper flourish** at t=0 of 0x1de, 0x1e0, 0x1e2, 0x1e4, 0x1e6: `FUN_006b1b40(p,10)`, SE 6,
  `FUN_004e9280(100, 0.75 DAT_00813f3c)`, `FUN_007bec20(100, pos, A, B)`, `FUN_004d7ac0(20, 0.5 DAT_00853b1c, 2)`,
  and (except 0x1e6) effect 0xfa via `DAT_00881698`. `(A,B)` = `(0,0)` for 0x1de/0x1e0/0x1e2;
  `(aim, π/4 DAT_0081fa90)` for 0x1e4/0x1e6. Cosmetic (suguri.md §1.4).
- Kyoko has **no movement or friction changes** in any attack state (no `+0x94/+0x98/+0x9c` writes);
  inherited dash speed simply decays.
- Several states draw RNG values **that are then overwritten** in ver ≥ 0x5a1 (kept only to stay
  RNG-compatible). They must still be drawn, in order.

### 1.2 Cost table entries (`0x87c4f0 + id*0xc` = {costA, costB, hyperGain}; registry.c:2136-2159)

| State | Move | costA | costB | hyperGain |
|---|---|---|---|---|
| 0x1db | Drizzling Rain (per shot) | 0.04 (DAT_00813eec) | 0 | 0.005 (DAT_00813eac) |
| 0x1dc | Clean Stream (per pair) and Hard Rain 1st icicle | 0.15 (DAT_00813f20) | 0 | 0.075 (DAT_00813efc); **ver ≥ 0x58b: 0.06** (DAT_00813ef8, FUN_004a7270 → DAT_0087db48) |
| 0x1dd | Hard Rain 2nd/3rd icicle | 0.15 | 0 | 0.05 (DAT_00813ef4) |
| 0x1df | Icy Crystal (per crystal) | 0 | 0.1 (DAT_00853b10) | 0.025 (DAT_00813edc) |
| 0x1e0 | Ice Over | 0 | 0 | 0 (never called) |
| 0x1e1 | Ripple | 0 | 0 | 0.01 (DAT_00813ec8) |
| 0x1e2 | Water Mirror | 0 | 0 | 0 (never called) |
| 0x1e5 | Crystal | 0 | 0.4 (DAT_00813f30) | 0.1 (DAT_00853b10) |

0x1de, 0x1e3, 0x1e4, 0x1e6, 0x1e7 never call FUN_007807f0 (Icicle is free; hypers give no gauge).
FUN_004a7270 touches no other Kyoko entry.

---

## 2. Input dispatch `FUN_006ec830` (standard scheme)

**A. Hypers** — only if `FUN_00780950(p)`. First match wins; a failed cost check falls through:

| # | Condition | Cost | New state | Move |
|---|---|---|---|---|
| 1 | S held (`+0x40>1`) && B held (`+0x3c>1`) && H pressed (`+0x44==3`) | 3.0 (DAT_00813a50) | 0x1e7 | **Avalanche** (Accel) |
| 2 | S held && H pressed | 1.0 (DAT_00853b34) | dist ≥ **360** (DAT_00817894) → **0x1e2**, else **0x1e4** | **Water Mirror** (long) / **Tree of Ice** (close) |
| 3 | B held && H pressed | 1.0 | 0x1e0 | **Ice Over** |
| 4 | Dash held (`+0x34>1`) && H pressed | 1.0 | 0x1de | **Freezing** (D+H) |
| 5 | H pressed | 1.0 | 0x1e6 | **Early Summer Rain** |

dist = `FUN_00780570(p)`.

**B. Normal moves** — only if `state == 0 || state == 2`:

| # | Condition | Result | Move |
|---|---|---|---|
| 1 | Dash pressed | `FUN_006b17d0(p,0)` | Dash |
| 2 | Shield pressed && `FUN_00780910(p,0.5)` && `FUN_007809e0(p)` | `FUN_006b1930(p,0)` | Shield |
| 3 | S held && A pressed | dist ≥ **200** (DAT_008147cc) → **0x1e1**, else **0x1e3** | **Ripple** (long) / **Icicle** (close) |
| 4 | Dash held (`+0x34>1`) && A pressed | 0x1dc | **Clean Stream** (D+A) |
| 5 | A pressed | 0x1dd (→ 0x1db on early release) | **Hard Rain** (hold) / **Drizzling Rain** (tap) |
| 6 | S held && B pressed | 0x1e5 | **Crystal** (S+B) |
| 7 | B pressed | 0x1df | **Icy Crystal** (B, hold to place more) |

Note: D+A tests `+0x34 > 1` (held), unlike Suguri's `> 0`. Finally returns `T2[state](p) != 0`.

**Alternate scheme `FUN_006ecdc0`** (`p+0x0c == 1`): identical normal-move block; hyper rows are "H held
(`+0x44>1`) and X pressed": Shield→0x1e7 (3.0); S→0x1e2/0x1e4 (360 split); B→0x1e0; Dash→0x1de; A→0x1e6 (1.0 each).

---

## 3. States

### 0x1d1 — neutral movement (T2 FUN_006e9bf0)
As Suguri 0x128 with `speed = stats+0 = 4.0` (DAT_00853b68 → 0x884a64) per axis, not normalised;
`+0x84 = 0` if facing==1 else π. Returns 1.

### 0x1d2, 0x1d3, 0x1d4, 0x1d5 — stubs (T2 FUN_006e9ce0 / 006e9d10 / 006e9cf0 / 006e9d00 return 1)
Only their T1s are used by common states (§4.3). 0x1d6–0x1da have no handlers.

### 0x1dd — A press: Hard Rain (hold) (T2 FUN_006ea480)
| t | Action |
|---|---|
| 0 | aim |
| t < 14 && A not held (`+0x38 < 2`) | `state = 0x1db` (timer kept), run 0x1db now, return (nothing else this frame) |
| 11–24 | if a direction is held (input angle ≤ 2π): rotate aim `+0x84` toward input angle by ≤ 0.1 rad (DAT_00853b10) via FUN_005f0e70 |
| 15 | if `FUN_007807f0(p,0x1dc)` (**uses Clean Stream's entry**: A −0.15): missile **400** at pos, angle aim; SE `FUN_007bcaf0(0x10,5)` (`laser03.wav`) |
| 20 | if A held (`+0x38>1`) && `FUN_007807f0(p,0x1dd)`: missile **400**, same; SE (0x10,5) |
| 25 | same as t=20 |
| ≥ 17 (ver ≤0x5a0: 20) | dash-cancel |
| > 37 (ver ≤0x5a0: 40) | state 0 |

### 0x1db — Drizzling Rain (T2 FUN_006e9d20) — via 0x1dd release before t=14
| t | Action |
|---|---|
| 0 | aim (only if entered at t=0, which never happens via 0x1dd — aim is the one set by 0x1dd at t=0, possibly steered during t=11–13) |
| 15–30, every frame (`t % 1 == 0`, @0x6e9d7a) | if `FUN_007807f0(p,0x1db)` (A −0.04): RNG in order `r1 = rand_f(−π, π)` (DAT_00814100, DAT_00853b58), `r2 = rand_f(−0.3, 0.3)` (DAT_0081fa88, DAT_00813f28), `d1 = rand_f(30, 150)` (DAT_008140e8, DAT_0081414c), `d2 = rand_f(30,150)`; spawn point = `pos + (cos r1 · d1, sin r1 · d2)`; beam **100** at that point, angle `aim + r2`; SE `FUN_007bcaf0(0x10,5)` |
| ≥ 22 (ver ≤0x5a0: 25) | dash-cancel |
| > 57 (ver ≤0x5a0: 60) | state 0 |
Up to 16 beams (0.64 ammo A total).

### 0x1dc — Clean Stream (T2 FUN_006ea9a0) — Dash held + A
| t | Action |
|---|---|
| 0 | aim |
| 15 | if `FUN_007807f0(p,0x1dc)` (A −0.15): re-aim; beam **200** at pos, angle aim − 0.8 (DAT_008145e8) and beam **200** at aim + 0.8; SE 0xe (`tm2_gun000.wav`) |
| 17 | if `FUN_007807f0(p,0x1dc)`: re-aim; beams **200** at aim ± 1.2 (DAT_00817d78); SE 0xe |
| ≥ 20 | dash-cancel |
| > 30 | state 0 |

### 0x1df — Icy Crystal (T2 FUN_006eac00) — B press (hold for more)
| t | Action |
|---|---|
| 0 | face by x (+0x84 = +0x88 = 0/π) |
| 5, 11, 17, 23, 29, 35, 41 (`4<t<42 && (t−5)%6==0`) | if `FUN_007807f0(p,0x1df)` (B −0.1): RNG `ra = rand_f(−π,π)`, `rd = rand_f(50,150)` (DAT_008140ec, DAT_0081414c) [ver ≤0x5a0: point = pos + (cos ra, sin ra)·rd]. **ver ≥0x5a1:** aim = `FUN_007804b0(p)` (stored); `(off, dist)` by t: 5:(+0.5,150) 11:(−0.5,129) 17:(+0.5,108) 23:(−0.5,87) 29:(+0.5,66) 35:(−0.5,45) 41 (default):(+0.5,24) (DAT_00853b1c=0.5, DAT_00853bb0=−0.5, DAT_0081414c=150, DAT_00825798=129, DAT_00824444=108, DAT_0081ee9c=87, DAT_00825794=66, DAT_0081ce48=45, DAT_00853b8c=24); `a = wrap(aim + off)` (FUN_005f0c60); point = pos + (cos a, sin a)·dist. Missile **100** at point, angle 0; effect 0x514 at point (colour `DAT_008859f4+side*0x10`); SE 0xb (`eco03.wav`) |
| 13–59 | if B not held (`+0x3c < 2`): **t := 60** |
| ≥ 65 | dash-cancel |
| > 80 | state 0 |
So holding B places up to 7 crystals in a zig-zag line toward the opponent; releasing jumps to recovery.

### 0x1e5 — Crystal (T2 FUN_006eb8d0) — S+B
| t | Action |
|---|---|
| 0 | face by x |
| 10 | if `FUN_007807f0(p,0x1e5)` (B −0.4): RNG `r = rand_f(−π,π)`; enemy **100** (`DAT_00881594(100, p+4, side, slot)`); [ver ≤0x5a0: enemy pos = pos + 50·(cos r, ? r) — decompiler shows cos for both axes, UNVERIFIED]. **ver ≥0x5a1:** enemy pos = `(x + dx, y + dy)` with `dy = +70` if y+ held (`+0x24>1`), then `−70` if y− held (y− wins) (DAT_008145ec, DAT_008256c8); `dx = +70` if x+ held, then `−70` if x− held (x− wins); 0 when not held. Enemy `+0x58` = aim. SE 0xb |
| ≥ 15 | dash-cancel |
| > 30 | state 0 |

### 0x1e1 — Ripple (T2 FUN_006eb3d0) — S+A, dist ≥ 200
| t | Action |
|---|---|
| 0 | aim |
| 10 | if `FUN_007807f0(p,0x1e1)` (free, +0.01 hyper): three missiles **300**, angle aim, at pos + rotate(o, aim) for o = (40, 50), (40, −50), (50, 0) (DAT_008140e0=40, DAT_008140ec=50, DAT_008149e4=−50); SE `FUN_007bcaf0(0xb,6)` |
| ≥ 20 | dash-cancel |
| > 30 | state 0 |
(The first FUN_005f1210 call shows a 3rd argument in the decompile — artefact; it rotates by aim like the others.)

### 0x1e3 — Icicle (T2 FUN_006ebbc0) — S+A, dist < 200 (no ammo cost)
| t | Action |
|---|---|
| 0 | aim |
| 8, 12, 16, 20, 24 (`7<t<25, t%4==0`) | RNG `r = rand_f(−0.5, 0.5)` (DAT_00853bb0, DAT_00853b1c), `a = aim + r` [ver ≤0x5a0 uses that]. **ver ≥0x5a1:** aim = `FUN_007804b0(p)` (stored); a = aim−0.5 (t8), aim+0.5 (t12), aim−0.25 (t16, DAT_00813f24), aim+0.25 (t20), aim (default). t=24: a = aim (all versions). Missile **500** at pos, angle a (visual); SE 0xb; hitbox **200** at pos + (cos a, sin a)·80 (DAT_00814164), angle a. At t=24 the hitbox's knockback `+0x40` := 15.0 (DAT_00853b88) via `FUN_004b2eb0(idx)` |
| ≥ 39 (ver 0x58b–0x5a0: 45; ≤0x58a: 50) | dash-cancel |
| > 49 (55; 60) | state 0 |

### 0x1e6 — Early Summer Rain (T2 FUN_006e9fc0) — H, 1 level
Timings (ver > 0x58a; [≤0x58a]): laser window 20–50 [30–60], dash-cancel 30 [40], end >50 [>60].
| t | Action |
|---|---|
| 0 | aim; hyper flourish (no 0xfa effect; FUN_007bec20(100,pos,aim,π/4)) |
| 4, 6, …, 24 (`3<t<25`, even → **11**) | RNG `ox = rand_f(30, 90)` (DAT_008140e8, DAT_00814170), `oy = rand_f(−120, 120)` (DAT_0081525c, DAT_00814174); missile **300** at pos + rotate((ox,oy), aim), angle aim; SE `FUN_007bcaf0(0xb,4)` |
| 20, 22, …, 50 (even → **16**) | RNG `r = rand_f(−0.4, 0.4)` (DAT_008147f0, DAT_00813f30); `la = aim + r`. ver ≥0x58c: `a = FUN_007804b0(p)` (**not** stored), `q = rand_f(−π/2, π/2)` (DAT_0081416c, DAT_00814104), `d = rand_f(60, 100)` (DAT_00813a58, DAT_00853b90); origin = pos + (cos a, sin a)·10 (DAT_00813f4c) + rotate((d,0), a+q). [ver <0x58c: origin = pos + (cos la, sin la)·50.] Laser **100** at origin, angle `la`; SE `FUN_007bcaf0(0x12,4)` (`gun11_r.wav`) |
| ≥ 30 | dash-cancel |
| > 50 | state 0 |

### 0x1de — Freezing (T2 FUN_006ea7c0) — Dash + H, 1 level
| t | Action |
|---|---|
| 0 | aim; flourish (FUN_007bec20(100,pos,0,0)) |
| 20 | six missiles **400** at pos, angles `aim + (k/3)·π` for k = −3..2 (DAT_00813a50=3) — every 60°; SE `FUN_007bcaf0(0x10,5)` (no cost check) |
| ≥ 30 | dash-cancel |
| > 50 | state 0 |

### 0x1e0 — Ice Over (T2 FUN_006eb0b0) — B + H, 1 level
| t | Action |
|---|---|
| 0 | face by x; flourish (0,0) |
| 12, 15, …, 48 (`11<t<51, t%3==0` → **13**) | RNG `ra = rand_f(−π, π)`, `rd = rand_f(80, 200)` (DAT_00814164, DAT_008147cc); missile **100** at pos + (cos ra, sin ra)·rd, angle 0; effect 0x514 there; SE `FUN_007bcaf0(0xb,6)` |
| ≥ 70 | dash-cancel |
| > 80 | state 0 |

### 0x1e2 — Water Mirror (T2 FUN_006eb6b0) — S + H, dist ≥ 360, 1 level
| t | Action |
|---|---|
| 0 | face by x; flourish (0,0) |
| 20 | twelve missiles **0x136** at pos, angles `(k/6)·π` for k = −6..5 (DAT_00813f44=6) — absolute, every 30°; SE 0xb |
| ≥ 30 | dash-cancel |
| > 40 | state 0 |

### 0x1e4 — Tree of Ice (T2 FUN_006ebf50) — S + H, dist < 360, 1 level
| t | Action |
|---|---|
| 0 | aim; flourish (FUN_007bec20(100,pos,aim,π/4)) |
| 8, 12, …, 40 (`7<t<41, t%4==0` → **9**) | `r = rand_f(−0.1, 0.35)` (DAT_00814130, DAT_00813f2c); a = aim + r if `t%8==0` else aim − r (t=8,16,24,32,40 → +; 12,20,28,36 → −). Missile **0x226** at pos + (cos a, sin a)·40 (DAT_008140e0), angle a (visual); SE 0xb; hitbox **0xfa** at pos + (cos a, sin a)·140 (DAT_00817004), angle a; at t=40 its knockback := 15.0 (DAT_00853b88) |
| ≥ 50 | dash-cancel |
| > 60 | state 0 |
Aim is fixed at t=0.

### 0x1e7 — Avalanche (T2 FUN_006ec2f0) — S + B + H, 3 levels
| t | Action |
|---|---|
| 0 | face by x; `FUN_007bec20(100, pos, −3π/4 (DAT_0081fa9c) if facing left (aim=π) else −π/4 (DAT_0081fa98), π/4)`; `FUN_006b1b40(p,0x14)`; SE 7 (`high_a.wav`); `FUN_004e9280(150, 0.85 DAT_00814120)`; `FUN_004d7ac0(150, 0.6 DAT_00813f34, 2)`; effect 0xfa |
| 0, 2, …, 70 (`0≤t<71`, even → **36**) | `opp = FUN_00780480(side)`; `s = −π/4` (DAT_0081fa98) if aim ≥ π/2 else `+π/4` (DAT_0081fa90); `base = opp.pos + rotate((600,0), π/2 + s)` (DAT_008149b8, DAT_00814104) — 600 px above-behind the opponent (upper-left when Kyoko faces right, upper-right when facing left); RNG `ox = rand_f(−300, 300)` (DAT_00815f44, DAT_008145f0), `oy = rand_f(−200, 100)` (DAT_00814600, DAT_00853b90); point = base + rotate((ox,oy), s); RNG `r = rand_f(−0.2, 0.2)` (DAT_00817c04, DAT_00853b14); angle = (aim ≥ π/2 ? −π/2 + s : −s) + r (i.e. −3π/4 or −π/4 ± 0.2, falling diagonally toward the opponent). Missile id by `(t/2)%3`: 0 → **0x96**, 1 → **0x97**, 2 → **0x98**. Effect 0x514 at point; SE `FUN_007bcaf0(0xb,6)` |
| ≥ 90 | dash-cancel |
| > 100 | state 0 |

---

## 4. Animation

### 4.1 Sprite registration (loader `FUN_006e6ba0`)
Arg = colour variant: `0` → folder text id 0x11, otherwise 0x12 (`charaKyokoA` / `charaKyokoB`) —
**only two variants** (stats +0x78 is never written). Frame n = `printf(pattern, n)`; each frame is an
S×S quad `FUN_00467d00(S,S,2,2,1.0,0)`. Storage `0x885b80 + slot*0x3888 + id*0x18`.

| Sprite id | Pattern | Frames | S | Used by |
|---|---|---|---|---|
| 0x1d1 | `ky_std0%d` | 3 | 96 (DAT_00814118) | neutral |
| 0x1d6 | `ky_fwd0%d` | 3 | 96 | neutral moving toward |
| 0x1d7 | `ky_bck0%d` | 3 | 96 | neutral moving away |
| 0x1d3 | `ky_los0%d` | 6 | 96 | T1 0x1d3, 0x1d4 |
| 0x1d5 | `ky_sld0%d` | 6 | 96 | shield; hyper poses |
| 0x1d2 | `ky_dsh0%d` | 6 | 96 | dash |
| 0x1d8 | `ky_rng0%d` | 2 | 512 (DAT_008178b0) | stats +0x3c (HUD ring?, UNVERIFIED) |
| 0x1da | `ky_system` | 1 | 512 | stats +0x44 (UI?, UNVERIFIED) |
| 0x1e1 | `ky_atka0%d` | 5 | 96 | A-type attack poses |
| 0x1df | `ky_atkb0%d` | 5 | 96 | B-type attack poses |
| 0x1e8 | `ky_ice0%d` | 4 | 96 | missile 100 / 0x96–0x98 |
| 0x1e9 | `ky_crys0%d` | 4 | 128 (DAT_00814134) | Crystal enemy, missile 200 |
| 0x1ea | `ky_mirr0%d` | 4 | 128 | mirrors (missile 300 / 0x136) |
| 0x1eb | `ky_iccl0%d` | 9 | frames 0–5: 128; 6–8: 320 (DAT_008178c0) | missile 500 (0–5), 0x226 (6–8), 0x1c2 (8), effect 100/0x104 |
| 0x1ec | `ky_smk0%d` | 4 | 128 | effects 200 / 0xc9 (smoke) |
| 0x1ee | `ky_impact%d` | 3 | 128 | Crystal enemy field |
| 0x1ef | `ky_lsr` | 1 | 256 (DAT_00814160) | laser 100 (texture blit) |
All 69 `ky_*.png` in `extracted/texture/charaKyokoA/` are covered. ids 0x1d4, 0x1d9, 0x1db–0x1de,
0x1e0, 0x1e2–0x1e7, 0x1ed are not registered (0x1d9 is referenced by stats +0x40 only).

### 4.2 Draw transforms
Same as suguri.md §4.2: **AIM transform** in every T1 except neutral = Reset, SetPos(x,y,0), then if
`|aim| ≥ π/2`: rotZ(aim), rotX(180°), rotY(180°), else rotZ(aim), rotY(180°) (FUN_0045a0f0(0xb4)).
Neutral: rotY(π) if facing != 0. Global tint `FUN_004e8f50` before each body draw; no blend change.
Afterimage `FUN_004d7e00(m,1,p,{0,0,0},0)` only in 0x1d1 and 0x1d2.

### 4.3 Common states → Kyoko draw handlers (case 0xf in FUN_006b07a0 … FUN_006b1580)
| Common state | Kyoko T1 |
|---|---|
| 0 neutral | 0x1d1 FUN_006ee3b0 |
| 1 (hit) | 0x1d4 FUN_006eea70 |
| 2 dash | 0x1d2 FUN_006ee670 |
| 3, 4 shield | 0x1d5 FUN_006eed40 |
| 5 | 0x1d4 |
| 6 | 0x1d3 FUN_006ef020 |
Common T2 state 0 → `DAT_008810e4` = 0x1d1 FUN_006e9bf0.

### 4.4 Per-state frames (`pp(n)` = `(t/n)&3` with 3→1; `t/4` truncates toward 0)
| State | T1 | Sprite | Frame |
|---|---|---|---|
| 0x1d1 | FUN_006ee3b0 | 0x1d1; overrides in order (later wins): y+→0x1d6; y−→0x1d7; facing0&x−→0x1d6; facing1&x−→0x1d7; facing0&x+→0x1d7; facing1&x+→0x1d6 (immediates @0x6ee41b..0x6ee4a5) | pp(5) |
| 0x1d2 | FUN_006ee670 | 0x1d2 | pp(3); +3 if moving against facing (facing1 && \|aim\|>π/2, or facing0 && \|aim\|<π/2) |
| 0x1d3 | FUN_006ef020 | 0x1d3 | pp(3) + (3 if `DAT_008858f8` even); **drawn only while t < 100** |
| 0x1d4 | FUN_006eea70 | 0x1d3 | pp(3) |
| 0x1d5 | FUN_006eed40 | 0x1d5 | t<10 ? 0 : min((t−10)/4, **6**) — frame 6 does not exist, but shield states end at t≤29 → max frame 4 (FUN_006afac0 ends >19, FUN_006afd90 >29) |
| 0x1db, 0x1dc, 0x1dd, 0x1e1, 0x1e3, 0x1e6 | FUN_006ef320 / 006efe40 / 006ef8a0 / 006f0680 / 006f0ee0 / 006ef5e0 | 0x1e1 | min(t/4, 4) |
| 0x1df, 0x1e0 | FUN_006f0100 / 006f03c0 | 0x1df | min(t/4, 4) (Icy Crystal's release jump t:=60 → frame 4) |
| 0x1de, 0x1e2 | FUN_006efb60 / 006f0940 | 0x1d5 | t<10 ? 0 : min((t−10)/4, 5) |
| 0x1e4, 0x1e7 | FUN_006f11a0 / 006f1460 | 0x1d5 | min(t/4, 5) |
| 0x1e5 | FUN_006f0c20 | 0x1d5 | min(t/3, 5) |
Sprite ids from `mov [ebp-0xc], imm` at the start of each handler (e.g. 0x6ef326 → 0x1e1, 0x6f0106 → 0x1df, 0x6efb66 → 0x1d5).

---

## 5. Spawned objects (type 15)

Family tables (index 15): beam 0x8818b4/0x881904/0x881954/0x8819a4; missile 0x8819f4/0x881a44/0x881a94/
0x881ae4/0x881b34/0x881b84; hitbox 0x881bd4/0x881c24; laser 0x881c74/0x881cc4/0x881d14/0x881d64;
enemy 0x881594/0x8815e4/0x881634/0x881684; effect 0x8816d4/0x881724/0x881774. Struct layouts: objects.md §2.

### 5.1 Beam family: spawn FUN_004ad0f0, update FUN_004ad5d0, draw FUN_004ad2f0 (empty), onHit FUN_004ad310
Generic init FUN_004ba490 (hitType 1 → **dashing targets pass through**, destroyOnHit 1, stun 1.0).

| Id | Spawned by | initial speed | shape {hx,hy,r} | dmg | knock | update |
|---|---|---|---|---|---|---|
| 100 Drizzling Rain | 0x1db | **−8** (DAT_0081410c) | {30, 8, 0} (DAT_008140e8, DAT_008140d4) | 120 | 10 (DAT_00813f4c) | if speed < 20 (DAT_008140fc): speed += 2 (DAT_00813a4c) (−8 → 20 by age 14; flies backwards first); move; **die** if age > 19 and outside arena+10 (FUN_0061ac10) |
| 200 Clean Stream | 0x1dc | 30 (DAT_008140e8) | {24, 10, 0} (DAT_00853b8c, DAT_00813f4c) | **180** [ver ≤0x5a0: 240] | 10 | age < 20: if speed > 2: speed −= 2 (30 → 2 at age 14). age ≥ 20: if speed < 25 (DAT_008140f0): += 1.5 (DAT_00814110); while age < 40: turn moveAngle toward opponent by ≤ (50 − age)·0.003 rad (DAT_008140ec, DAT_00813ea0; 0.09 → 0.033), `+0x64 = +0x60`. Move; die if age > 49 and outside arena+20 |
| 300 mirror reflection | missile 300/0x136 absorb (§5.2) | 16 (DAT_008140d8) | {20, 6, 0} (DAT_008140fc, DAT_00813f44) | 100 | 10 | age < 20: if speed > 2: speed −= 1 (16 → 2 at age 14). age ≥ 20: if speed < 12: += 2; while age < 50 home as 200. Die if age > 49 and outside arena+10 |
Beam 200 `+0x6c` (=0 at spawn) is a "redirected" flag set by the Crystal enemy (§5.5): it sets
moveAngle/angle straight at the opponent, `+0x6c = 1.0`, **age := 40** (ends homing), speed ≥ 12.

Per-frame visuals (cosmetic): 100 → Kyoko effect **0x136** every frame at pos/angle, generic 0x547 at age 1;
200 → effect **300** every frame + particle `FUN_0068b8a0(0x456, …, angle+π, …, 1)` + 0x547 at age 3;
300 → effect **0x140** every frame + 0x547 at age 1. Colour = `DAT_008859f4 + side*0x10`.
onHit (destroyed on player/enemy hit): 100 and 300 → effect 0x3f2 (angle+π), particles 1000×3, sparks
`FUN_007bd5e0(1000,c,4,4,…)`, SE 0x13; 200 → effect 1000, particles ×10, sparks (10,10), SE 0x13.

### 5.2 Missile family: spawn FUN_00669960, update FUN_0066d200, draw FUN_00669f40, onHit FUN_0066caf0, onKilled FUN_0066d070, onAbsorb FUN_0066d190
Init FUN_00685bb0 (hitType 2 — **not dodged by dash**, hp 60, active 1, scale 1, `+0x60` 1, stun 1.0,
`+0x64` 0). `+0x5c` = grow/alpha "s", `+0x60` = fade "f", `+0x64` = phase, `+0x70` = speed,
`+0x74` = aux. Missile→Player requires `active (+0x7c)`; damage then onHit; erase if onHit returns 0.

| Id | Spawn fields | Behaviour (update) | onHit (player hit / killed) |
|---|---|---|---|
| **100** crystal (Icy Crystal, Ice Over) | s 0, phase 100, hp 300, active 0, body r 20 / proj r 20 (DAT_008140fc), dmg 200, knock 5 (DAT_00813f40), speed 0 | phase 100: s += 0.1 (DAT_00853b10); when s > 1: s = 1, **active**. At age > 149: phase 200, speed 0, moveAngle = atan2(opp − pos). phase 200: speed += 0.5 (DAT_00853b1c) while < 12 (DAT_00813f54); move; once speed ≥ 12 and outside arena+20 → call onHit, die. phase other: s −= 0.05, die ≤ 0 | 6 streaks 0x456 at k·π/3 + rand_f(−0.25, 0.25) (k=−3..2; RNG ×6), effect 0x516, SE 0x13; **destroyed**. onKilled: effect 0x514, then destroyed (returns 0) |
| **300** mirror (Ripple, Early Summer Rain) | s 0, speed 0, **absorbs 1**, hp 300, active 0, body {20, 40, 0} / proj {20, 40, 0} (DAT_008140fc, DAT_008140e0), dmg 0, knock 0 | age < 100: s += 0.1, when > 1: s = 1, active. age ≥ 100: s −= 0.05 (DAT_00813ef4), active 0, die ≤ 0 (~age 119). Static | streaks ×6, effect 0x515, SE 0x13; destroyed. (Touching the opponent deals 0 damage with stun 1.0 — see UNVERIFIED) |
| **0x136** orbit mirror (Water Mirror) | as 300 | age < 100: s += 0.05; >1 → active. radius(`+0x70`) += 5 (DAT_00813f40) while < 100 (DAT_00853b90). moveAngle += 0.075 (DAT_00813efc), wrapped to ≤ π by −2π; angle = moveAngle; **pos = owner's current pos + (cos,sin)(moveAngle)·radius** (`FUN_007805f0(ownerId)`). age > 519: s −= 0.05, inactive, die ≤ 0 | as 300 |
| **400** icicle (Hard Rain, Freezing) | speed 14 (DAT_00813a54), hp 10, active 1, body r 30 / proj r 30, dmg 100, knock 10 | every even age: effect **200** at pos, angle rand_f(−π,π) (RNG). if speed > 6 (DAT_00813f44): −= 0.25 (DAT_00813f24). Move. If outside arena (margin 0): call onHit, keep | **turns into 0x1c2 in place**: effect 0x514, SE 0x15 (`crash17.wav`); id = 0x1c2, phase 0, speed 0, f = 1, s = 0, hp 200, dmg 150, active 0, body r 40 / proj r 40, knock 10; kept. onKilled → same conversion |
| **0x1c2** ice block (from 400) | (see left) | phase 0: s += 0.1, `+0x70` += 2.5 (DAT_008153e0); when s ≥ 1: s = 1, **active**, phase 100. phase 100: every 10 ages effect 200 (random angle); age > 800 → inactive, phase 900. phase 900: s −= 0.1, f −= 0.1, `+0x70` += 2.5, six particles 0x456 at angle `k·π/3 − angle` and radius `+0x70`; die when s ≤ 0. Every phase: if age%4==0: `+0x74 = rand_f(0.5, 0.75)` (RNG; draw tint); moveAngle = atan2(opp − pos). **Never moves** | effect 0x514, SE 0x15; hp 999, phase 900, inactive; kept (shatters). onKilled → same |
| **0x96/0x97/0x98** avalanche chunk | angle(`+0x6c`) = moveAngle + π/2, phase 100, s 0, f 1, hp 360, active 0, body r 60 / proj r 60 (DAT_00813a58), dmg 180, knock 5, speed 0 | phase 100: s += 0.1; >1 → s 1, active, phase 200. phase 200: speed += 0.5 while < 15 (DAT_00853b88); if age > 120 && speed ≥ 15 && outside arena+50: speed 0, inactive, phase 900; every 5 ages Kyoko effect **0xc9** (random angle, RNG). phase 900: speed += 0.5 while < 15, f −= 0.05, die f ≤ 0. Always `pos += (cos,sin)(moveAngle)·speed` | phase 900, speed 0, inactive; streaks ×6; SE 0x13; **kept** (fades). onKilled: returns 0 without effects → erased (only 100/200 have explicit killed branches; 0x96–0x98 fall to `return param_1 & 0xffffff00`) |
| **500** icicle visual (Icicle) | speed 10 (DAT_00813f4c), s 0, hp 999, active 0, no shapes | age < 10: s += 0.2 (cap 1); else particle 0x456 at pos/angle, s −= 0.2. age 3: effect 0x547. Move. Die if age > 10 && s ≤ 0 (~age 15) | — (cannot collide) |
| **0x226** tree visual (Tree of Ice) | speed 16 (DAT_008140d8), s 0, hp 999, active 0, no shapes | age < 10: s += 0.2 (cap 1); else particle 0x456, s −= 0.1, speed = 20. age 1: effect 0x546. Move. Die if age > 10 && s ≤ 0 (~age 20) | — |
| 200 (unused) | s 0, hp 300, active 0, proj r 30 | grow like 100; when active, redirects own beams 200 like the Crystal enemy (§5.5) | — |

No Kyoko state spawns missile 200 (grep of `DAT_008819f4` callers); it is dead code.

**onAbsorb FUN_0066d190** (beam hits an active mirror 300/0x136 — objects.md §4.5: beam erased without
its onHit, mirror takes no damage): spawn Kyoko beam **300** at the absorbed beam's pos, angle = the
mirror's angle `+0x6c`, owner/side of the mirror; SE `FUN_007bcaf0(0x25,3)` (`reflect.wav`).
(Ripple mirrors face aim; Water Mirror mirrors face radially outward; reflected beams then home.)

**Draw FUN_00669f40** (bank = `p18` §Notes 3; tint `slot0`/`slot1` = `p18 == 0` / else; colour args (a,r,g,b)).
`pulse1 = sin((age%20)/20·2π)·0.15 + 0.85` (DAT_00813f20, DAT_00814120), `pulse2 = sin((age%50)/50·2π)·0.25 + 0.75`
(DAT_00813f24, DAT_00813f3c). Two passes for 100/200/300/0x136/0x96–0x98: (1) additive (blend 2), frame 3,
colour (pulse2, s, s, s); (2) normal (blend 1), frame `(age/2)%3`, colour slot0 (1, s·pulse1, s, s) /
slot1 (1, s, s·pulse1, s). Scale `(s, 4 − 3s, s)` (DAT_00853b68, DAT_00813a50).
- 100 → sprite 0x1e8, no rotation. 200 → 0x1e9, no rotation.
- 300/0x136 → 0x1ea, rotZ(angle).
- 0x96 ×2, 0x97 ×3, 0x98 ×2.5 on x and y scale (DAT_00813a4c, DAT_00813a50, DAT_008153e0); 0x1e8; both
  pulses × f; pass-2 alpha = f; rotZ(angle).
- 0x1c2 → 0x1eb frame 8, six shards at angle `k·π/3 − angle`, radius `+0x70`, each: blend 1 colour slot0
  (1, v, 0.5v+0.5, 1) / slot1 (1, 1, v, 0.5v+0.5) with v = `+0x74`, then blend 2 alpha 0.5 same colour;
  scale (s·0.25, s·0.35, s) (DAT_00813f24, DAT_00813f2c), rotZ(shard angle).
- 500 → 0x1eb; scale (x,y) = age<10 ? (s, 1) : (4 − 3s, s); pass 1 additive frame `age%3+3`, alpha = stale
  `DAT_009b3580` (§Notes 5); pass 2 normal frame `age%3`, colour (1,s,s,s); rotZ(angle).
- 0x226 → 0x1eb frame `age%3+6`; scale age<10 ? (s,1) : (3 − 2s, s); additive only, colour slot0
  (1, s, 0.5s+0.5, 1) / slot1 (1, 1, s, 0.5s+0.5); rotZ(angle).

### 5.3 Hitbox family: spawn FUN_005e4d20, onHit FUN_005e5050
One-frame boxes (suguri.md §5.2). All: dealsDamage 1, oncePerFrame 0, stun 1.0, **hitType 3** (hits dashing targets).

| Id | Spawned by | dmg | shape {hx,hy,r} | knock | onHit (runs before damage) |
|---|---|---|---|---|---|
| 100 | Crystal field (§5.5), every frame | **1** [ver ≤0x5a0: 30] | {0, 0, 100} (DAT_00853b90) | 10 | **angle := atan2(target − box)** (knockback pushes radially out); SE `FUN_007bcaf0(0x15,4)` |
| 200 | Icicle ×5 | 80 | {100, 50, 0} (DAT_00853b90, DAT_008140ec) | **0** [ver ≤0x5a0: 5]; last hit 15 | effect 0x9c4 at target, random angle (RNG), effect 0x515, SE 0x16 (`hit_s.wav`) |
| 0xfa | Tree of Ice ×9 | **70** [ver ≤0x58a: 80] | {200, 100, 0} (DAT_008147cc, DAT_00853b90) | 0; last hit 15 | effect 0x9c4 angle `box.angle + π/2 + rand_f(−0.5, 0.5)` (RNG), 0x515, SE 0x16 |
| 300 | laser 100 (Early Summer Rain), ages 1 and 3 | 80 | {760, 20, 0} (DAT_0081fa70, DAT_008140fc) | 10 | 6 streaks 0x456 (RNG ×6), effect 0x516, SE `FUN_007bcaf0(0x13,4)` |
Geometry: Icicle box centre 80 px ahead → covers −20…180 px along the aim; Tree of Ice centre 140 → −60…340;
laser box centre 800 → 40…1560 px along the laser.

### 5.4 Laser family: spawn FUN_0061f650, update FUN_0061faa0, draw FUN_0061f750, onHit FUN_0061fa80 (empty)
Laser 100 (only id): blockMode 1, hitType 3, damage 0, knock 10, stun 1. Update clears its segment list
every frame (`FUN_004a9f10`) → **the laser itself never collides**; damage comes from hitbox **300**
spawned at ages 1 and 3 (first update sees age 1 — spawn frame, and 2 frames later) at
`origin + (cos,sin)(angle)·800` (DAT_008149c8), angle = laser angle. Removed when age > 19.
Draw (cosmetic): screen-space blit of sprite 0x1ef's texture (`bank+0xc` vector, @0x61f8d2) centred at
`origin + dir·1000` (DAT_00814408), half-length 1000/zoom, half-height `h = age ≤ 10 ? 40 : 40 − 3(age − 10)`
/zoom; pass 1 src (0,0)-(511,159), colour 0x80ffffff, mode 2; pass 2 half-height ×0.15 (DAT_00813f20),
src rows `160 + 60k … 219 + 60k`, k = `(age/2)%3`, colour 0x40ffffff; rotation −angle.

### 5.5 Enemy family (Crystal, S+B): spawn FUN_00521ee0, update FUN_00522050, draw FUN_00522630, onKilled FUN_00522580
Spawn id 100 only (`+0x10 = 100`, `+0x7c = 100`): hp/maxHp **450** (0x1c2) [0x58a–0x5a0: 400; older 500],
active 0, shape {0, 0, 30} (DAT_008140e8), scale (0, 3, 0), `+0x6c` (triggered) 0, phase `+0x84` 0,
not solid, not invulnerable. Caller sets pos `+0x40/44` and angle `+0x58` (§3 0x1e5).
- **phase 0**: sx += 0.1, sy −= 0.2, sz += 0.1; when sx ≥ 1 (10 frames): scale 1, **active**, phase 100, `+0x80 = 0`.
- **phase 100**: for every beam with owner == crystal owner, type 15, id 200, `+0x6c == 0` and
  circle-overlap of radius 30+30 (both shapes `{0,0,30}`) between crystal pos and beam pos:
  crystal `+0x6c = 1` (triggered); beam moveAngle = angle = atan2(opp − beam.pos), beam `+0x6c = 1.0`,
  beam age = 40, beam speed = max(speed, 12); effect 0x515 at beam; SE 0x25 (`reflect.wav`).
  If triggered: `+0x58 = rand_f(−π, π)` (RNG, every frame), `+0x74 = min(+0x74 + 0.1, 1)`, and spawn
  **hitbox 100** at the crystal pos — **only while the owner's `+0x128 < 21`** (ver ≥ 0x5a1;
  owner = `FUN_00780450(side)`). `+0x80 > 699` → phase 900.
- **phase 900**: sx −= 0.05, sy += 0.1, sz −= 0.05, `+0x74 −= 0.1` (clamped ≥ 0); removed when sx ≤ 0.
- onKilled (hp < 1 from enemy projectiles): effect 0x514, SE 0xb, phase 900, inactive, hp 999 → returns
  true (kept to fade out).
So the Crystal is a shootable 450-HP object; Clean Stream beams passing through it are redirected at
the opponent and turn it into a 700-frame pulsing barrier (1-damage, 10-knockback radial hitbox every frame).
Draw (cosmetic, age = `+0x78`): if phase 100: three passes of sprite 0x1ee (impact) at scale
`k = +0x74·2·(1 + 0.05·sin((age%75)/75·2π))` (DAT_00813a4c, DAT_00813ef4, DAT_00815254), rotZ(+0x58): frame 1
blend 1 alpha pulse2·0.75; frame 2 blend 2 alpha pulse2·0.5 scale 2k; frame 0 blend 2 alpha pulse1·0.75.
Always: sprite 0x1e9 frame 3 blend 2 colour (pulse2, sz, sz, sz) scale (sx, sy, 1); then frame `(age/2)%3`
blend 1 colour slot0 (1, sz·pulse1, sz, sz) / slot1 (1, sz, sz·pulse1, sz). No rotation on the crystal itself.

### 5.6 Kyoko effect family `0x8816d4 = FUN_004fb8d0`, update FUN_004fd940, draw FUN_004fbcf0 (cosmetic)
Effect record 0x44 (`+0x04` id, `+0x08` colour, `+0x18` pos, `+0x28` age, `+0x2c` scale, `+0x30` alpha, `+0x34/+0x38` angle).
| Id | Spawned by | Update | Draw |
|---|---|---|---|
| 0x136 | beam 100 every frame | alpha −0.1 after age 2 | 2D blit from global bullet sheets (`DAT_0087fdf4/0087fdf0`), src by colour index; additive flash first 2 ages |
| 300 | beam 200 every frame | alpha −0.1 after age 5 | as above, larger (60×60 body) |
| 0x140 | beam 300 every frame | alpha −0.1 after age 1 | as 0x136 |
| 200 | missile 400 (even ages), 0x1c2 | alpha −0.025, scale +0.1, die ≤ 0 | sprite 0x1ec (`ky_smk`) frame min(age/12, 3), half-size 32·scale |
| 0xc9 | avalanche chunks | as 200 | as 200, half alpha, single pass |
| 100–102, 0xfa, 0x104 | not spawned by any Kyoko code (0xfa would spawn effect 200 every 10 frames for 300 frames) | | |
Generic `FUN_004ef2c0` ids used: 0xfa hyper flash, 0x514/0x515/0x516, 0x456, 0x3f2, 1000, 0x547, 0x546, 0x9c4.
All effects/particles that draw random numbers consume the shared MT19937 (suguri.md §5.7).

---

## 6. Sound ids used (SE table all.c:464010; id = (addr − 0x9b99a0)/4)
| id | file | where |
|---|---|---|
| 6 | up01.wav | hyper start |
| 7 | high_a.wav | Avalanche |
| 0xb | eco03.wav | Icy Crystal, Ice Over, Crystal, Ripple, Icicle, Tree of Ice, Water Mirror, ESR mirrors, Avalanche, Crystal killed |
| 0xe | tm2_gun000.wav | Clean Stream |
| 0x10 | laser03.wav | Drizzling Rain, Hard Rain, Freezing |
| 0x12 | gun11_r.wav | Early Summer Rain lasers |
| 0x13 | don16.wav | beam / missile / laser-hitbox hits |
| 0x15 | crash17.wav | icicle → ice block, ice block shatter, Crystal field hit |
| 0x16 | hit_s.wav | Icicle / Tree of Ice hits |
| 0x25 | reflect.wav | mirror reflection, Crystal redirect |

---

## 7. Other per-character table entries (index 15)

| Table (real base) | Kyoko entry | Role |
|---|---|---|
| 0x881310 | FUN_006ec830 | input dispatch, standard (§2) |
| 0x881360 | FUN_006ecdc0 | input dispatch, alternate (§2) |
| 0x8813b0 | FUN_006e6ba0 | sprite loader (§4.1) |
| 0x881400 + 0x10*15 + 4*level | FUN_006edbe0 (level 0), FUN_006ed350 (levels 1, 2) | CPU AI (not reversed) |
| beam / missile / hitbox / laser / enemy / effect | §5 | |

---

## 8. Stats record (index 15, base 0x884a64), written by FUN_0049ae40 (registry.c:2103-2135)

| Off | Value | Meaning |
|---|---|---|
| +0x00 | 4.0 (DAT_00853b68) | walk speed per axis per frame |
| +0x04 | 12.0 (DAT_00813f54) | dash speed |
| +0x08 | 10.0 (DAT_00813f4c) | UNVERIFIED (Suguri: 12) |
| +0x0c..+0x20 | 0, 0, 0, 5, 0, 0 (0x884a70..84) | UNVERIFIED |
| +0x3c/+0x40/+0x44 | 0x1d8, 0x1d9, 0x1da | sprite ids (ring / unregistered / system) |
| +0x48 / +0x60 | text ids 0x11 / 0x12 | colour-variant folders (only two) |
| +0xa8 / +0x1a8 | `mdl\win_font_KY.lfd/.dat` | |
| +0x2a8 | `ky_res_bgp` | |
| +0x2c0/+0x2c4/+0x2c8 | 50, 80, 0 | UNVERIFIED (portrait offset?) |
| +0x2cc/+0x2d0/+0x2d4 | 150, 150, 255 | colour (RGB) |
| +0x2d8 | 0 (DAT_00884d3c) | |
HP is not in this record (core.md).

---

## 9. UNVERIFIED / open items
- 0x1e5 old-version (ver ≤ 0x5a0) crystal y offset: decompile shows `cos(r)·50` for both axes — check
  disassembly at 0x6eb9e0..0x6eba60 (irrelevant for 0x5a2).
- ~~Mirror contact~~ RESOLVED: `FUN_00780ab0(p, 0, 2, 1.0)` adds 1.0 stun → returns true on a fresh target, so the opponent enters hitstun with knockback 0 (no damage); the mirror is destroyed by its onHit.
- ~~`+0x128`~~ RESOLVED: attacker shown combo-hit count (see field table).
- Float accumulation thresholds (e.g. `s += 0.1` > 1.0 happening on the 10th vs 11th increment) — simulate
  in float32 when implementing (objects.md did this for graze zones).
- Exact visuals of FUN_004e9280 / FUN_007bec20 / FUN_004d7ac0, effect 0x1ec/0x1ee textures' frame layout,
  the global bullet sheets `DAT_0087fdf0/0087fdf4` used by effects 0x136/300/0x140. (Effect 0x136/300/0x140 blit sizes verified from FUN_004fbcf0: 300 body 60×60, core 40×16, flash 54×26, white core until age 6; 0x136 = 0x140: body 60×40, core 40×8, flash 48×16, white until age 5. Laser draw FUN_0061f750 verified.)
- ~~Missile 300 init~~ RESOLVED: `cmp id,0x12c; jz 0x669bab` and jump table 0x669e28[0] (id 0x136) = 0x669bab — one shared initialiser.
- Stats +0x08, +0x0c..+0x20, +0x2c0..+0x2c8; AI functions.
- Up/down meaning of `+0x24`/`+0x28` (assumed +0x24 = up, +y; objects.md §0.2 has +y up).

## 10. Port verification (src/sim/chars/kyoko.c)
- Behaviour test `tests/test_kyoko.c` (input→state for both schemes, incl. the 200 / 360 range splits at the exact boundary; Hard Rain t=15/20/25; Drizzling Rain 16 beams t=15..30, end t>57; Clean Stream 4×beam 200 dmg 180, 0.30 A, hyper 2×0.06, dash-cancel from t=20; Icicle 5×80 on a placed target, free; Crystal cost 0.4 B / +0.1 hyper, hp 450, phases, beam-200 redirect, 1-dmg field, fade after 700; mirror absorb → beam 300; icicle 400 → ice block 0x1c2 at the wall, static, shatters after age 800; ESR 16 lasers + 11 mirrors; Water Mirror 12 mirrors orbiting at r=100; Avalanche 36 chunks (first on the press frame), Accel barrier; Freezing 6 icicles at t=20; all states end; random hyper play deterministic).
- Facing: character art faces −x; the original RotY(π) = 2D flipX (neutral: when +0x8c != 0; AIM transform: always, plus vertical flip for |aim| ≥ π/2). Missile/object models without RotY are not mirrored.
- Visual pass with `tests/run_drawdump.sh` (Ripple, Water Mirror, Avalanche, Crystal + Clean Stream, ESR, Hard Rain → ice block).
