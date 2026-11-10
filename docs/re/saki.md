# Saki — complete moveset spec

Source: `aos2.exe` (Ghidra corpus `tools/re/dec/all.c`, constants read with `xref.exe rd`, raw
disassembly with `xref.exe dis` where the decompiler hid values). Every number cites its address.
`t` = player state timer `p+0x154`. Structure and helper names follow **suguri.md** (the template);
generic object/collision behaviour follows **objects.md**. "UNVERIFIED" items say what would settle
them. Version global `DAT_0089426c` = 0x5a2 in live play (objects.md §0.1) → **implement the
newest branch** wherever a version test appears.

---

## 0. Notes for the lead (affect other docs / all characters)

1. **Saki's engine index `p+0x10` is 11** (handbook id 8 + 3). All per-character tables use
   `base + 4*11 = base + 0x2c`; stats record = `0x881f80 + 11*0x2dc` = **0x883ef4** (registry.c:1844;
   carries `win_font_SK`, registry.c:1868). (0x8841d0 is index 12 = Iru — it carries `win_font_IR`,
   registry.c:1923.)
2. **objects.md §4.5 mislabels the absorb handler `FUN_00673790` as "Saki type 8".** Type 8 is
   handbook id 5 (Nath). Saki's absorb slot `0x881b48+0x2c = 0x881b74` is **0** (registry) — Saki has
   no reflecting/absorbing missiles.
3. **Zero-damage "hold" hitboxes.** Saki uses hitboxes with damage 0, hitType 3, knockback 0
   (ids 0x78, 0x82, 0xdc). `FUN_00780ab0` has no early-out for damage 0: it still adds the hitbox
   stun (1.0) to `+0x144`, sets `+0x140 = 30`, increments the hit counter `+0x14c` and returns
   `+0x144 >= 1.0` (all.c, FUN_00780ab0 body) → the victim gets `FUN_006b19a0(angle, 0)`, i.e.
   hitstun with **zero knockback** every frame — the tambourines / bell "catch and hold" the
   opponent. core.md's damage function must keep this behaviour.
4. **Beam updates run at negative ages** (objects.md §1.4: only lasers/effects skip age<0). Cyclone
   Cymbal spawns beams with `age0 = −4k` (FUN_00720890) to delay their age-based phases; the beams
   still move from frame 1.
5. **Critical hits that knock Saki herself back.** Hitbox on-hit `FUN_005e8430` (ids 300 and 0x19a)
   calls `FUN_006b1d70(victim, 100, 12)`, which acts on *the victim's opponent* — Saki. On a
   "critical" Maracas Hammer / Maracas Swing hit, Saki (if within 100 px of the victim) is put into
   **common hit state 1**, pushed away from the victim at speed 12 (§5.2).
6. Missile draw functions select the sprite bank from missile `+0x1c` (= player `+0x18`)
   (`DAT_009b35d4 = m+0x1c`, FUN_00675060) — confirms `p+0x18` = player/sprite slot
   (objects.md §0.3 lists it as UNVERIFIED).

### Player fields used by Saki's code
Same as suguri.md "Player fields" table; additionally:

| Off | Name | Notes / source |
|---|---|---|
| +0x128 | attacker combo counter | read by hitbox on-hit (ids 100/0x6e): `> 11` → knockback 10 (FUN_005e8430) — objects.md §4.6 |
| +0x14c | hit counter (victim) | incremented even by 0-damage hits (FUN_00780ab0) |
| +0xa2 / +0xa9 / +0xf8 | hittable / armour / shield timer | gate the Big Bang pull (`FUN_00780a50`) |

---

## 1. Shared mechanics

Everything in suguri.md §1 applies unchanged (frame model, `T2[id]` called on entry with `t=0`,
"state = 0 without resetting t", dash-cancel `FUN_006b17d0(p,1)` when `t >= N && Dash == 3`,
`FUN_00612900` = training display only, `FUN_00612830` = training display only, friction `+0x9c`
reset to 0.5 each frame, ammo regen, RNG). Helper names reused: `FUN_007807f0` (cost table),
`FUN_006b1b40` (hyper start), `FUN_00780910` (hyper spend), `FUN_00780950` (hyper allowed),
`FUN_007809e0` (shield allowed), `FUN_007804b0` (aim), `FUN_00780570` (distance), `FUN_005f0e70`
(rotate toward), `FUN_005f1210` (rotate vector), `FUN_00459fd0` (rand float), `FUN_00459f80`
(rand int inclusive), `FUN_007bc9c0` / `FUN_007bcaf0(id,n)` (SE).

**"Face" (used by most Saki states at t=0):** `opp = FUN_00780480(side)`; if `opp.x <= x` →
`+0x84 = +0x88 = π` (DAT_00853b58) else `+0x84 = +0x88 = 0`. Saki's specials mostly face
horizontally instead of aiming (aim is taken separately into `+0x88` where noted).

**Dash sparks (cosmetic, consume RNG)** — Saki variant: `r = rand_f(−0.15, 0.15)` (DAT_00815dc4,
DAT_00813f20), then `n = rand_i(0,100) % 8`, generic effect `0x881698(id+n, 0xd, pos', aim + π/2 + r,
owner, 0, side)` (disassembly @0x7216c1–0x721752). Base id 0xa28 at `pos` (Maracas Swing), 0xa32 or
0xa82 at `(x, y − 30)` (DAT_008140e8) (Sunshine Bell / Big Bang Bell; 0xa82 uses r ∈ [−0.25, 0.25],
DAT_0081411c/DAT_00813f24).

### 1.2 Ammo / hyper cost table rows (`0x87c4f0 + id*0xc = {costA, costB, hyperGain}`, registry.c:1886–1903)

| State | Move | costA (`+0xe8`) | costB (`+0xec`) | hyperGain |
|---|---|---|---|---|
| 0x15d | Tornado Cymbal | 0.4 (DAT_00813f30) | 0 | 0.05 (DAT_00813ef4) |
| 0x15e | Stardust Bell (per bell) | 0.1 (DAT_00853b10) | 0 | 0.01 (DAT_00813ec8) |
| 0x15f | Maracas Grenade (per maraca) | 0 | 0.05 (DAT_00813ef4) | 0.005 (DAT_00813eac) |
| 0x160 | Tambourine Boomerang | 0 | 0.4 (DAT_00813f30) | 0.075 (DAT_00813efc) |
| 0x162 | Maracas Hammer | 0 | 0 | 0.05 |
| 0x163 | (used by 0x164 Maracas Swing) | 0 | 0 | 0.075 |

0x161 (Tambourine Blade) never calls the cost function (free, no gauge). Hypers never call it.
`FUN_004a7270` (version patch) touches **no** Saki entry (its writes are all outside
0x87d4c4..0x87d5e3).

### 1.4 Hyper start / flourish
- Normal hypers (0x165, 0x166, 0x167, 0x168), at t=0 after "face": `FUN_006b1b40(p,10)`; SE 6;
  `FUN_004e9280(100, 0.75)` (DAT_00813f3c); `FUN_007bec20(100, pos, 0, 0)`;
  `FUN_004d7ac0(20, 0.5, 2)`; generic effect `0x881698(0xfa, 0, pos, 0, owner, 0, side)`.
  (Suguri passes aim/π/4 to 7bec20; Saki passes 0, 0.)
- Accel (0x169): `FUN_006b1b40(p,0x14)`; SE 7; `FUN_004e9280(100, 0.85)` (DAT_00814120);
  `FUN_007bec20(100, pos, 0, 0)`; `FUN_004d7ac0(100, 0.6, 2)` (DAT_00813f34); effect 0xfa; SE 0x30.

---

## 2. Input dispatch `FUN_00722ce0` (standard scheme) — exact decode

**A. Hypers** — only if `FUN_00780950(p)`; first match wins; if the cost check fails the next row
is tried.

| # | Condition | Cost | State | Official move |
|---|---|---|---|---|
| 1 | S held (`+0x40>1`) && B held (`+0x3c>1`) && H pressed (`+0x44==3`) | 3.0 (DAT_00813a50) | 0x169 | **Big Bang Bell** (S+B+H, Accel) |
| 2 | S held && H pressed | 1.0 (DAT_00853b34) | 0x168 | **Rolling Tambourine** (S+H) |
| 3 | B held && H pressed | 1.0 | 0x167 | **Sunshine Bell** (B+H; "Hold" = keep **H** held, §3 0x167) |
| 4 | Dash held (`+0x34>1`) && H pressed | 1.0 | 0x166 | **Galaxy Bell** (D+H) |
| 5 | H pressed | 1.0 | 0x165 | **Cyclone Cymbal** (H) |

**B. Normal moves** — only if `state == 0 || state == 2`:

| # | Condition | Result | Move |
|---|---|---|---|
| 1 | Dash pressed (`+0x34==3`) | `FUN_006b17d0(p,0)` | Dash |
| 2 | else Shield pressed && `FUN_00780910(p,0.5)` && `FUN_007809e0(p)` | `FUN_006b1930(p,0)` | Shield |
| 3 | S held && B pressed | 0x161 | **Tambourine Blade** (S+B) |
| 4 | S held && Dash held (`+0x34>1`) && A pressed | 0x164 | **Maracas Swing** (D+S+A) |
| 5 | S held && A pressed | 0x162 | **Maracas Hammer** (S+A) |
| 6 | B pressed | 0x15f (→ 0x160 if B released before t=13) | **Maracas Grenade** (B hold) / **Tambourine Boomerang** (B tap) |
| 7 | Dash held (`+0x34>1`) && A pressed | 0x15e | **Stardust Bell** (D+A) |
| 8 | A pressed | 0x15d | **Tornado Cymbal** (A) |

Notes:
- "Dash held" is `> 1` for Saki (Suguri's D+A tests `> 0`). Because row B1 (Dash pressed) wins on
  the press frame, D+A / D+S+A / D+H need Dash to be held from an earlier frame (typically while
  dashing, state 2).
- Row 4 is tested before row 5, so S+A while Dash is held always gives Maracas Swing.
- Returns `T2[state](p) != 0` at the end.

**Alternate scheme `FUN_00723200`** (`p+0x0c == 1`): normal moves identical; hypers are
"H held (`+0x44>1`) and X pressed (`==3`)": 1. Shield → 0x169 (3.0); 2. S → 0x168; 3. B → 0x167;
4. **Dash** → 0x166; 5. **A** → 0x165 (1.0 each). Plain H does nothing in the alt scheme. The
Sunshine Bell hold still tests H held (`+0x44 > 1`).

Not in the manual: Tambourine Blade → Maracas Hammer cancel on A after hit-confirm (§3 0x161);
critical hits of Maracas Hammer / Maracas Swing (§5.2); Maracas Grenade aim adjustable with up/down.

---

## 3. States

### 0x153 — neutral movement (T2 FUN_0071fd20; called by common state 0 T2 FUN_006af390 `case 0xb`)
Identical to Suguri 0x128 with `speed = stats+0 = 4.0` (DAT_00853b68 → 0x883ef4): y+ held → y+=4,
y− → y−=4, x− → x−=4, x+ → x+=4 (not normalised). `+0x84 = 0` if facing==1 else π. Returns 1.

### 0x154, 0x155, 0x156, 0x157 — stubs (T2 FUN_0071fe10 / 0071fe40 / 0071fe20 / 0071fe30 return 1)
Only their T1 draws are used by common states (§4.3). 0x158–0x15c have no handlers (ids 0x158/0x159
are reused as sprite ids).

### 0x15d — Tornado Cymbal (T2 FUN_0071fe50) — A
| t | Action |
|---|---|
| 0 | face (`+0x84 = +0x88 = 0/π`) |
| 14 | if `FUN_007807f0(p,0x15d)` (A −0.4): `+0x88 = aim`; for i = −18..17: beam (`i` even → **300**, odd → **0x136**) at pos, angle `aim + (i/18)·π` (DAT_00814144 = 18) — **36 cymbals, 10° apart, full circle**; SE 0x10 |
| every frame | friction ×0.5 (DAT_00853b1c) |
| ≥20 | dash-cancel (612900 at 20) |
| >30 | state 0 |

### 0x15e — Stardust Bell (T2 FUN_00720090) — D+A
| t | Action |
|---|---|
| 0 | aim (`+0x84`) |
| 10..13 (each frame) | if `FUN_007807f0(p,0x15e)` (A −0.1): beam (`t` even → **200**, odd → **0xd2**) at pos, angle `rand_f(−π, π)` (DAT_00814100, DAT_00853b58); SE `FUN_007bcaf0(0xf,2)` → **up to 4 bells** |
| every frame | friction ×0.5 |
| ≥16 | dash-cancel; >30 state 0 |

### 0x15f — B press / Maracas Grenade (T2 FUN_00720270)
| t | Action |
|---|---|
| 0 and 14 | face (`+0x84 = +0x88 = 0/π`); then if up held (`+0x24>1`): rotate `+0x88` toward π/2 by ≤ **0.4** (DAT_00814104, DAT_00813f30); if down held (`+0x28>1`): rotate `+0x88` toward −π/2 by ≤ **1.2** (DAT_0081416c, DAT_00817d78) (asymmetric — confirmed in disassembly @0x7202fc–0x720366; reproduce) |
| t < 13 && B not held (`+0x3c < 1`) | `+0x84 = aim`; state = **0x160** (timer kept); run its T2 now; return |
| 14..21 (each frame) | if `FUN_007807f0(p,0x15f)` (B −0.05): `r = rand_f(π/6, π/3)` (DAT_00817ddc, DAT_008203bc); angle = `+0x88 + r` if opponent is to the right (`opp.x > x`), else `+0x88 − r` (always tilted upward); missile **100** at pos, that angle; SE `FUN_007bcaf0(0x2f,3)` → **up to 8 maracas** |
| ≥25 | dash-cancel; >50 state 0 |
No friction change.

### 0x160 — Tambourine Boomerang (T2 FUN_00720590) — via 0x15f early release
| t | Action |
|---|---|
| 12 | if `FUN_007807f0(p,0x160)` (B −0.4): missile **200** at pos, angle `+0x84` (aim fixed at the release frame); SE 0x31 |
| ≥25 | dash-cancel; >40 state 0 |
(The switch happens on the first frame with `+0x3c < 1`: on the release frame B reads 1 ("released", FUN_0061bc70), so 0x15f → 0x160 happens one frame **after** release — releasing on 0x15f t=1 switches at t=2. Release with the switch at t ≤ 12 → fires at t=12. No friction change. Verified by tests/test_saki.c.)

### 0x161 — Tambourine Blade (T2 FUN_00720680) — S+B
| t | Action |
|---|---|
| 0 | aim |
| 4 | if `|aim| <= π/2`: missile **0x136** at pos, angle `aim − π/2`; else missile **300**, angle `aim + π/2`; SE 0x31. **No cost check.** (Both start below Saki and sweep through the front to above, §5.3.) |
| ≥10 | flag158 && A pressed → FUN_00612830, state **0x162** (t=0, flag=0) |
| every frame | friction ×0.5 |
| ≥25 | dash-cancel; >40 state 0 |

### 0x162 — Maracas Hammer (T2 FUN_00721a80) — S+A, or A after hit-confirm in 0x161
| t | Action |
|---|---|
| 0 | aim; lunge: `+0x88 = +0x94 = aim`, `+0x98 = min(dist/14, 10)` (DAT_00813a54, DAT_00813f4c) |
| 8 | SE 0x1f |
| 10 | if `FUN_007807f0(p,0x162)` (free, +0.05): hitbox **300** at `pos + (cos,sin)(aim)·30` (DAT_008140e8), angle aim |
| ≥35 | dash-cancel; >50 state 0 |
No friction change.

### 0x163 — unused lunge (T2 FUN_00721c90)
Same as 0x162 without the hitbox (t=0 lunge `min(dist/14,10)`, t=8 SE 0x1f), friction ×0.5,
dash-cancel ≥35, >50 state 0. **No code enters 0x163** (grep for `0x163` in all.c: only the sprite
id in the loader and the cost lookup in 0x164). Its cost row is used by 0x164.

### 0x164 — Maracas Swing (T2 FUN_00721dc0) — D+S+A
| t | Action |
|---|---|
| 0 | aim; `+0x94 = aim`; speed 0 |
| 10 | if `FUN_007807f0(p,0x163)` (free, +0.075): re-aim; `+0x88 = +0x94 = aim`; speed **12** (DAT_00813f54); SE 0x20 |
| every frame | if speed ≥ 5 (DAT_00813f40): dash sparks 0xa28+n at pos, angle `aim + π/2 + rand(−0.15,0.15)` |
| 12, 18, 24 | hitbox **400** at `pos + dir(aim)·30`, angle aim |
| 15, 21 | hitbox **400** at `pos − dir(aim)·30` (behind), angle aim |
| 27 | hitbox **0x19a** at `pos − dir(aim)·30`, angle `aim + π` |
| every frame | friction ×0.5 (→ 0.25/frame decel) |
| ≥40 | dash-cancel; >50 state 0 |
Hitboxes are spawned with no cost check. `aim` = `+0x84` (from t=10).

### 0x165 — Cyclone Cymbal (T2 FUN_00720890) — H, 1 level
| t | Action |
|---|---|
| 0 | face; hyper start (kind 10) + flourish (§1.4) |
| 16, 18, 20, 22, 24 (wave k = 0..4, t = 16+2k) | `+0x88 = aim`; for i = −18..17: beam (`i` even → **300**, odd → **0x136**) at pos, angle `aim + (i/18)·π − k·0.1` (DAT_00853b10), **age0 = −4k**; SE 0x10 on k = 0, 2, 4 → 5 waves × 36 = **180 cymbals** |
| ≥30 | dash-cancel; >40 state 0 |
No speed/friction change.

### 0x166 — Galaxy Bell (T2 FUN_00720ba0) — D+H, 1 level
| t | Action |
|---|---|
| 0 | face; hyper start + flourish |
| 8..40, t even | beam (`t%4==0` → **200**, else **0xd2**) at pos, angle `rand_f(−π, π)`; then that beam's `age(+8) = rand_i(0, 150)` and `damage(+0x84) = 40`; SE `FUN_007bcaf0(0xf,4)` → **17 bells** |
| ≥40 | dash-cancel; >60 state 0 |

### 0x167 — Sunshine Bell (T2 FUN_00720ea0) — B+H, hold H, 1 level
Bell position each frame: `b = (x, y + t·2.5)` (DAT_008153e0) — rises above Saki.
| t | Action |
|---|---|
| 0 | face; hyper start + flourish |
| 1 | SE 0x30 |
| 10..80, if H held (`+0x44>1`) or t < 30 | Saki effect `0x8816c4(200, 0, &b, t·0.1, owner, 0, side)`, effect `+0x2c` (scale) = `(t−10)/20` if t<30 else `t/30` (DAT_008140fc, DAT_008140e8); if `t%10==0`: hitbox **200** at b, angle `+0x84`, radius := 64·scale (DAT_008140f8 → hitbox `+0x34`); if `t%10==5`: hitbox **0xd2** likewise |
| 10..80, release | if `(t > 29 && H < 2)` or `(t > 29 && hyper(+0xe4) <= 0)` or `t == 80`: beam **100** at b, angle `+0x84`, then `+0x64 = t·0.1`, `+0x58 (scale) = t/30`, `+0x6c = t/30`, `+0x68 (speed) = 6 − (t−30)/10` (DAT_00813f44, DAT_00813f4c), `+0x60 = atan2(opp − b)`; **t := 80**; SE 0x20 |
| 10..80, t%4==0 (uses t after the release write) | sparkle: `rand_f(300,1000)`, `rand_f(−π,π)` (results unused — dead offset computation, RNG still consumed), Saki effect `0xfa` at **pos** with angle `rand_f(−π,π)` (spawn itself draws `rand_f(1,4)`), effect `+0x3c = rand_f(15,45)` (DAT_00853b88, DAT_0081ce48) |
| 10..80, else t even | same with effect `0x104`, `+0x3c = rand_f(10,30)` |
| 10..80, t even | dash sparks 0xa32+n at `(x, y−30)` |
| ≥85 | dash-cancel; >100 state 0 |
Charge range: released at t ∈ [30, 80] (minimum 30 even if H is tapped). Nothing drains the hyper
gauge during the hold; the `hyper <= 0` test only ends the hold early when the 1.0 payment left the
gauge at exactly 0 (no Saki code drains it — see §9 "Resolved").
Bigger charge = bigger ball (scale t/30 ∈ [1, 2.67]) and slower top speed (§5.1).

### 0x168 — Rolling Tambourine (T2 FUN_007217c0) — S+H, 1 level
| t | Action |
|---|---|
| 0 | face; hyper start + flourish; then `+0x84 = aim` |
| 10 | for i = −5..4: `i` even → missile **0x140** at pos, angle `aim + (i/5)·π`; odd → missile **0x14a**, angle `aim − (i/5)·π` (DAT_00813f40 = 5); SE 0x31 → **10 tambourines** orbiting Saki (5 each direction) |
| ≥40 | dash-cancel; >60 state 0 |

### 0x169 — Big Bang Bell (T2 FUN_00722270) — S+B+H, 3 levels (Accel)
Bell position: `b = (x, y + t·3.5)` (DAT_008178a0). Active for 0 ≤ t ≤ 60.
| t | Action |
|---|---|
| 0 | face; Accel start + flourish (§1.4) incl. SE 0x30 |
| 60 | 6×2 particles `FUN_0068b8a0(1000, team colour A/B, &b, (k/3)·π, owner, 0, 4)` k=0..5; `a = atan2(opp − b)`; Saki effect `0x8816c4(300, 0, &b, 0, …)`; generic effect 0xa8c at b; hitbox **0x122** at b, angle a; SE 0x27 |
| 0..60 | Saki effect 200 at b, angle t·0.1, scale `(t−10)/20` (negative for t<10); 4 streaks: `rand_i(0,100) < 50` → Saki effect 0x172 (colour A) else 0x173 (colour B), angle `rand_f(0, π/2) + (k/4)·2π` |
| 0..60 | **pull**: if `FUN_00780a50(opp)` (opp `+0xf8==0`, state≠6, `+0xa2`, `+0xa9≠1`, not KO): `opp.pos −= (cos,sin)(atan2(opp − b))·6` (DAT_00813f44) — 6 px/frame toward the bell |
| 0..40 | sparkles as 0x167 (t%4==0 → 0xfa, else t even → 0x104) |
| 0..60, t even | sparks: t<46 → 0xa32+n (r ±0.15); else 0xa82+n (r ±0.25) at `(x, y−30)` |
| ≥85 | dash-cancel; >100 state 0 |
Saki's speed is not zeroed by any hyper (momentum decays by normal friction).

---

## 4. Animation

### 4.1 Sprite registration (loader `FUN_0071cf70`)
Arg = colour variant: 0 → folder string id 0x1b, 1 → 0x1c, 2 → 0x1d (`charaSakiA/B/C`; extracted
folders exist). Variant 2 falls back to 0x1c when **both** players are index 11 and
`FUN_007a2880(0/1)` returns the (2,0)/(0,2) pattern (Saki mirror, UNVERIFIED meaning of 7a2880).
Each frame: `printf(pattern, n)`, quad `FUN_00467d00(S,S,2,2,1.0,0)` (same as Suguri).

| Sprite id | Pattern | Frames | S | Used by |
|---|---|---|---|---|
| 0x153 | `sk_std0%d` | 3 | 96 (DAT_00814118) | neutral |
| 0x158 | `sk_fwd0%d` | 3 | 96 | neutral, moving toward |
| 0x159 | `sk_bck0%d` | 3 | 96 | neutral, moving away |
| 0x155 | `sk_los0%d` | 6 | 96 | T1 0x155, 0x156 |
| 0x157 | `sk_sld0%d` | 4 | 96 | shield; 0x15d, 0x161, 0x165, 0x166, 0x168 |
| 0x154 | `sk_dsh0%d` | 6 | 96 | dash |
| 0x15a | `sk_rng0%d` | 2 | 512 (DAT_008178b0) | stats +0x3c (HUD ring, UNVERIFIED) |
| 0x15c | `sk_system` | 1 | 512 | stats +0x44 (UI, UNVERIFIED) |
| 0x15f | `sk_thrw0%d` | 6 | 96 | 0x15e, 0x15f, 0x160 |
| 0x167 | `sk_ball0%d` | 8 | 96 | 0x167, 0x169 |
| 0x162 | `sk_hammer0%d` | 4 | 96 | 0x162 |
| 0x163 | `sk_swing0%d` | 4 | 96 | 0x163, 0x164 |
| 0x16a | `sk_grnd0%d` / `sk_grnd%d` (n ≥ 10) | 18 | 72 (DAT_008178ac) | maraca (missile 100) |
| 0x16b | `sk_tmbrn0%d` | 3 | 128 (DAT_00814134) | tambourines (missiles 200/300/0x136/0x140/0x14a), effects 100/0x6e |
| 0x16c | `sk_ssbell0%d` | 3 | 256 (DAT_00814160) | Sunshine ball (beam 100), effect 200 |
| 0x16d | `sk_wisp0%d` | 2 | 64 (DAT_008140f8) | bells (beam 200/0xd2), effects 0xfa/0x104 |
All 76 `sk_*` frames in `extracted/texture/charaSakiA/` are covered (+ `sk_res_bgp`, res_chr, res_sys).
Sprite id 0x15b (stats +0x40) is not registered. Pause-menu command list: `FUN_0071f2f0`
(text ids 0x319–0x324; called from all.c:355471 `case 0xb`) — no gameplay.

### 4.2 Draw transforms
- **AIM transform** (every state except 0x153), identical to Suguri §4.2: pos; if `|+0x84| >= π/2`:
  rotZ(+0x84), rotX(180°), rotY(180°); else rotZ(+0x84), rotY(180°). Note most Saki states set
  `+0x84` to 0/π (face), so the body is drawn upright, mirrored by side.
- **Neutral** (0x153): rotY(180°) if facing != 0.
- Global tint `FUN_004e8f50` before every body draw; **no blend-mode call** in any Saki player draw
  (state left at default, normal alpha).
- Heat afterimage `FUN_004d7e00` queued only in 0x153, 0x154, 0x162, 0x163, 0x164.

### 4.3 Common states → Saki draw handlers (`case 0xb` in common T1s)
| Common state | Saki T1 |
|---|---|
| 0 neutral | 0x153 FUN_00724710 |
| 1 / 5 | 0x156 FUN_00724dd0 |
| 2 dash | 0x154 FUN_007249d0 |
| 3 / 4 | 0x157 FUN_007250a0 |
| 6 | 0x155 FUN_00725360 |

### 4.4 Per-state frames (`pp(n)` = `(t/n)&3`, 3→1; `ramp(s, c)` = `t<s ? 0 : min((t−s)/4 + 1, c)`)
Sprite ids from the immediate written at function start (disassembly, e.g. `mov [ebp-0x14],0x153` @0x724735).

| State | T1 | Sprite | Frame |
|---|---|---|---|
| 0x153 | FUN_00724710 | 0x153; overrides (later wins, @0x724769–0x72480c): y+→0x158; y−→0x159; facing0&x−→0x158; facing1&x−→0x159; facing0&x+→0x159; facing1&x+→0x158 | pp(5) |
| 0x154 | FUN_007249d0 | 0x154 | pp(3); +3 if moving against facing (facing1 && \|+0x84\|>π/2, or facing0 && \|+0x84\|<π/2) |
| 0x155 | FUN_00725360 | 0x155 | pp(3) + 3 if global frame `DAT_008858f8` even; **drawn only while t < 100** |
| 0x156 | FUN_00724dd0 | 0x155 | pp(3) |
| 0x157 | FUN_007250a0 | 0x157 | min(t/5, 3) |
| 0x15d | FUN_00725660 | 0x157 | min(t/4, 3) |
| 0x15e | FUN_00725920 | 0x15f | ramp(8, 5) |
| 0x15f | FUN_00725c00 | 0x15f | ramp(10, 5) |
| 0x160 | FUN_00725ee0 | 0x15f | ramp(8, 5) |
| 0x161 | FUN_007261c0 | 0x157 | ramp(8, 3) |
| 0x162 | FUN_00727040 | 0x162 | ramp(8, 3) |
| 0x163 | FUN_007273e0 | 0x163 | ramp(5, 3) |
| 0x164 | FUN_00727780 | 0x163 | t<10: 0; else n = (t−10)/3 + 1; if n>3: n −= 3; if n>3: n −= 3; cap 3 → 1,2,3 ×3 then 3 |
| 0x165 | FUN_007264a0 | 0x157 | ramp(5, 3) |
| 0x166 | FUN_00726780 | 0x157 | ramp(8, 3) |
| 0x167 | FUN_00726a60 | 0x167 | t<80: pp(3); else min((t−80)/3 + 3, 7) |
| 0x168 | FUN_00726d60 | 0x157 | ramp(5, 3) |
| 0x169 | FUN_00727b40 | 0x167 | as 0x167 |
(`(x + (x>>31 & 3)) >> 2` in the decompile = truncating `/4`.)

---

## 5. Spawned objects (parameters)

Spawn calls (all with `owner = p+4`, `side = p+0x14`): beam `0x8818a4 = FUN_004b14c0(id, &pos, angle,
owner, age0, side)`; missile `0x8819e4 = FUN_00674ca0(id, &pos, angle, owner, 0, side, p+0x18)`;
hitbox `0x881bc4 = FUN_005e7fd0(id, &pos, angle, owner, 0, side)`; Saki effect `0x8816c4 = FUN_00503530`;
generic effect `0x881698 = FUN_004ef2c0`. Struct layouts: objects.md §2.

### 5.1 Beam family (type 11): spawn FUN_004b14c0, update FUN_004b25b0, draw FUN_004b17d0, onHit FUN_004b2260
Template `FUN_004ba490` defaults: hitType 1, stun 1.0, active 1, destroyOnHit 1, scale/alpha 1.0,
`+0x60 = +0x64 = angle`. Per id (spawn locals map to `+0x55, +0x68, +0x6c, +0x70, +0x74..7c, +0x84, +0x88, +0x8c`):

| Id | Used by | speed +0x68 | +0x6c | active | shape {hx,hy,r} | dmg | knock | stun | destroyOnHit |
|---|---|---|---|---|---|---|---|---|---|
| 100 Sunshine ball | 0x167 | 0 (state overwrites) | 0 (state overwrites) | **0** | {0,0,0} | 0 | 10 (DAT_00813f4c) | 1.0 | 0 |
| 200 / 0xd2 bell | 0x15e, 0x166 | 5 (DAT_00813f40) | — | **0** | {0,0,15} (DAT_00853b88) | 100 (0x166: 40) | 10 | **0.4** (DAT_00813f30) | 1 |
| 300 / 0x136 cymbal | 0x15d, 0x165 | 5 | accel −0.1 (DAT_00814130) | **0** | {0,0,10} (DAT_00813f4c) | 120 | 10 | 1.0 | 1 |

**Update (age = `+8`, update sees the spawn age first):**
- **100 Sunshine ball:** spin `+0x64 += 0.1` (wrap at π by −2π). If age < 300: `cap = 8 − +0x6c·1.8`
  (DAT_008140d4, DAT_0081413c); if `speed < cap` → speed += 0.05 (DAT_00813ef4); turn `+0x60`
  toward the opponent by ≤ 0.03 rad (DAT_00813ee0). Move `pos += (cos,sin)(+0x60)·speed`. If age > 300
  and outside arena+500 (DAT_00814140) → removed. Every frame a hitbox at pos, angle `+0x64`, radius
  `64·scale(+0x58)`: `age%18==0` → **200**, `age%18==9` → **0xd2**, else **0xdc**. Particle
  `FUN_0068b8a0(1000, colour (A if age even else B), pos, +0x64 + π, owner, 0, 1)` every frame.
  The ball never collides itself (active 0); all its damage is via the hitboxes.
- **200 / 0xd2 bell:** age < 200: turn `+0x60` toward **Saki** (`FUN_00780450(side)` = own player) by
  ≤ 0.08 (DAT_00813f00), `+0x64 = +0x60` → orbits/follows Saki at speed 5 (inactive);
  200 ≤ age < 300: turn toward the opponent by ≤ 0.04 (DAT_00813eec), speed += 0.1 while < 16
  (DAT_008140d8), **active = 1**; age == 200: generic effect 0x515 (launch flash). Move; every
  `age%3==0` particle 0x456 (angle `+0x64+π`). Age > 300 and outside arena+10 → removed.
  (Galaxy Bell's random start age 0..150 shortens the orbit phase.)
- **300 / 0x136 cymbal:** age == 10 → active = 1; age == 50 → `+0x6c` (accel) = 0.2 (DAT_00853b14);
  50 ≤ age ≤ 300: `+0x60 += 0.01` (300) / `−= 0.01` (0x136) (DAT_00813ec8), `+0x64 = +0x60`;
  every frame `speed += +0x6c`, then move. So: 5 → decelerates 0.1/frame to ≈0 at age 50, then
  accelerates 0.2/frame while curling (300 CCW, 0x136 CW). Age > 50 and outside arena+10 → removed.

**onHit (FUN_004b2260; called when destroyed on a player hit):** ids 200/0xd2/300/0x136: generic effect
1000 (colour A for 200/300, B for 0xd2/0x136) at pos, angle `+0x64+π`; sparks
`FUN_007bd5e0(1000, colour, 5, 5, +0x64, pos, owner)`; SE 0x13. id 100: nothing.

**Draw (FUN_004b17d0)** (screen-space quads via sprite method +0x68(dst, 1.0, src, rot16, argb, blend);
half-sizes divided by zoom `DAT_008859e0`):
- 100: sprite 0x16c frame 0, src (0,0)-(255,255); pass 1 half 144·scale (DAT_00814138), alpha 128·a,
  blend 1; pass 2 half 128·scale, alpha 255·a, blend 2; rotation `+0x64`.
- 200/0xd2: sprite 0x16d frame (id==200 ? 0 : 1), src (0,0)-(63,63); pass 1 half (active ? 48
  : 24)·scale (DAT_008140e4/DAT_00853b8c), alpha 128·a, blend 1; pass 2 half ×0.75, alpha (active ?
  255 : 128)·a, blend 2.
- 300/0x136: from the shared bullet atlas `DAT_0087fde4`, cell chosen by the team colour value `c`
  (`DAT_008859f4/8859f8 + side*0x10`): glow src x = `(c%10%2)·100 + 50 + (c/10)·250`, y = `((c%10)/2)·100`,
  99×99, dst half (40, 20), alpha 96·a, blend 1; core src (`(c/10)·250`, `(c%10)·50`) 49×49, dst half
  (20, 10), alpha 255·a, blend 2; rotation `−(+0x64)`.

### 5.2 Hitbox family (type 11): spawn FUN_005e7fd0, onHit FUN_005e8430
Fields per objects.md §2.4; stun 1.0, dealsDamage 1, oncePerFrame 0 for all.

| Id | Spawned by | hitType | dmg | shape {hx,hy,r} | knock | on-hit |
|---|---|---|---|---|---|---|
| 100, 0x6e | boomerang (missile 200), Tambourine Blade (300/0x136) | 3 | 25 | {0,0,70} (DAT_008145ec) | 0 | A |
| 0x65, 0x6f | Rolling Tambourine (0x140/0x14a) | 3 | 25 | {0,0,70} | 0 | A (without the 0x514 / knock / SE 0x15 part) |
| 0x78 | boomerang (between ticks) | 3 | 0 | {0,0,r}, r = 80 / ver>0x58a 70 / **ver>0x598 69** (DAT_00814164, DAT_008145ec, DAT_0081fa7c) | 0 | B |
| 0x82 | tambourines 300/0x136/0x140/0x14a (between ticks) | 3 | 0 | {0,0,69} (as 0x78) | 0 | B + hit-confirm |
| 200, 0xd2 | Sunshine ball / charge (radius overwritten to 64·scale) | **1** | 25 | {0,0,120} (DAT_00814174) | 0 | C |
| 0xdc | Sunshine ball (other frames) | 3 | 0 | {0,0,120} → 64·scale | 0 | none |
| 300 | Maracas Hammer | 3 | **250** | {0,0,60} (DAT_00813a58) | 15 (DAT_00853b88) | D (crit 10/101 → 450) |
| 400 | Maracas Swing ×5 | 3 | 100 | {100, 20, 0} (DAT_00853b90, DAT_008140fc) | 5 (DAT_00813f40) | E |
| 0x19a | Maracas Swing final | 3 | 100 | {100, 20, 0} | 15 | F (crit 20/101 → 250) |
| 0x122 | Big Bang Bell blast | 3 | **1333** (0x535) | {0,0,300} (DAT_008145f0) | 30 (DAT_008140e8) | none |

On-hit callbacks (run before damage; `v` = victim player; "flying" = `v.state == 1 && v.speed(+0x98) > 2` (DAT_00813a4c)):
- **A** (100/0x65/0x6e/0x6f): if flying → `dealsDamage = 0` (no further hit). Else: `r0 = rand_f(−π,π)`;
  4× generic effect 0x456 at the hitbox pos, angle `(k/2)·π + rand_f(−0.25,0.25) + r0` (k=−2..1);
  effect 0x515; for 100/0x6e only: effect 0x514, **if attacker `+0x128 > 11` → knockback := 10**,
  SE `FUN_007bcaf0(0x15,4)`; then SE `FUN_007bcaf0(0x13,4)`.
- **B** (0x78/0x82): if flying → `dealsDamage = 0`; else for 0x82 only: attacker hit-confirm `+0x158 = 1`.
- **C** (200/0xd2): `r0 = rand_f(−π,π)`; 6× effect 0x456 at v.pos, angle `(k/3)·π + rand(−0.25,0.25) + r0`;
  effect 0x515; SE `FUN_007bcaf0(0x13,5)`.
- **D** (300): effect 0xaf0 at v.pos; 6 particle pairs `FUN_0068b8a0(1000/0x44c, colour A, v.pos, (k/3)·π, owner, 0, 1/2)`;
  8× effect 0x456, angle `(k/4)·π + rand(−0.25,0.25)`; SE 0x19; then `n = rand_i(0,100)`; if `n <= 9`
  (**critical**): effect 600; 5× effect 0x834 and 5× effect 500 (age0 −5−3k) at v.pos + (rand(−50,50),
  rand(−50,50)) (DAT_008149e4, DAT_008140ec); SE 0x1a; **damage := 450** (0x1c2);
  `FUN_006b1d70(v, 100, 12)` → **Saki** (v's opponent) is put into state 1, pushed away from v at
  speed 12, SE 0x19, if within 100 px (and hittable, not armoured, not state 6).
- **E** (400): 4 particle pairs at v.pos, angle (k/2)·π (k=−2..1); 6× effect 0x456 angle `(k/3)·π + rand(−0.5,0.5)`; SE 0x18.
- **F** (0x19a): effect 0xa8c; 6 particle pairs; SE 0x19; shake `FUN_004d7ac0(10, 0.75, 1)`;
  `n = rand_i(0,100)`; if `n <= 19` (**critical**): as D's critical block, **damage := 250**.

### 5.3 Missile family (type 11): spawn FUN_00674ca0, update FUN_00676290, draw FUN_00675060, onHit FUN_00675df0, onKilled FUN_00676050
Template `FUN_00685bb0` defaults: hitType 2, hp 60, active 1, stun 1.0, `+0x68 = +0x6c = angle`.
Every Saki missile draws `rand_f(−π, π)` at spawn into `+0x6c` (spin angle).

| Id | Used by | +0x70 | +0x74 | hp | active | body r (+0x88) | proj r (+0x98) | dmg (+0xa0) | knock |
|---|---|---|---|---|---|---|---|---|---|
| 100 maraca | 0x15f | vy = sin(angle)·10 | vx = cos(angle)·10 | 60 | 1 | 20 (DAT_008140fc) | 10 | 50 (+5/frame to 200) | 10 |
| 200 boomerang | 0x160 | speed 12 (DAT_00813f54) | 0.012 (unused) | 180 | **0** | 0 | 0 | 0 | 10 |
| 300 / 0x136 / 0x140 / 0x14a tambourine | 0x161, 0x168 | radius 32 (DAT_008140dc) | — | 180 | **0** | 0 | 0 | 0 | 10 |

Inactive missiles are skipped by every collision pass (Missile→Player requires `+0x7c`, FUN_00610b80
line 37; object passes likewise) — the tambourines/boomerang are pure hitbox carriers and cannot be
shot down.

**Update:**
- **100 Maracas Grenade** (ballistic): if age even or `|vy| > 11` (DAT_00813f50): smoke effect 2000 at
  pos, angle `rand_f(−π,π)`; `vy −= 0.25` (DAT_00813f24, gravity); `x += vx; y += vy`; spin `+0x6c += 0.1`;
  damage += 5 while < 200; outside arena+30 → onHit + removed. Hitting a player: damage = current
  `+0xa0` (50 at spawn → 200 after 30 frames), knockback along `+0x68` (launch angle — not updated),
  then onHit → destroyed.
- **200 Tambourine Boomerang:** if speed > −12 (DAT_00824a90): speed −= 0.2 (DAT_00853b14); spin −0.5;
  move along `+0x68` (fixed) — flies out (12 → 0 in 60 frames), comes back along the same line and
  continues; Saki effect 100 at pos (angle spin) on even global frames; removed when speed < 0 and
  outside arena+500 (ver ≥ 0x5a1; older: +10). Hitbox every frame at pos, angle `+0x68`:
  `age%16==0` → **100**, `age%16==8` → **0x6e**, else **0x78**.
- **300 / 0x136 Tambourine Blade:** radius += 4 while < 80 (DAT_00814164, DAT_00853b68); 300: spin −0.5,
  orbit angle `+0x68 −= 0.12` (DAT_00824a8c); 0x136: +0.5, +0.12. `pos = Saki.pos + (cos,sin)(+0x68)·radius`
  (follows Saki). Saki effect 0x6e every frame. **Removed at age 30.** Hitbox: age < 21 && age%4==0 →
  **100** (angle spin); age < 21 && age%4==2 → **0x6e**; else **0x82** (angle `+0x68`).
- **0x140 / 0x14a Rolling Tambourine:** radius += 6 while < 120 (DAT_00814174, DAT_00813f44); 0x140:
  spin −0.3, orbit −0.08 (DAT_00813f28, DAT_00813f00); 0x14a: +0.3, +0.08. Position around Saki as above;
  effect 0x6e; **removed at age 70**. Hitbox: age < 51 && age%8==0 → **0x65**; age < 51 && age%8==4 →
  **0x6f**; else **0x82**.

**onHit (FUN_00675df0)** — returns false (always destroyed). id 100 only: 3× effect 0x834 at pos +
(rand(−30,30), rand(−30,30)) (DAT_00817c08, DAT_008140e8), 4× effect 500 at pos + (rand(−20,20)…)
(DAT_00814108, DAT_008140fc; age0 −3k), effect 0x515, SE 0x14.
**onKilled (FUN_00676050)** — false. id 100: effect 600; 3× 0x834; 3× 500 (age0 −5−3k); SE 0x1a.
(DAT_00817c08 = −30, DAT_00814108 = −20; cosmetic, consumes RNG.)

**Draw (FUN_00675060)**, sprite bank = slot `+0x1c`, rotZ(`+0x6c`), pulse
`a1 = sin((age%50)/50·2π)·0.15 + 0.85`, `a2 = sin(…)·0.25 + 0.75` (DAT_008140ec, DAT_00813f20,
DAT_00814120, DAT_00813f24, DAT_00813f3c):
- 100: sprite 0x16a, `f = (age/5)%6`: frame f (normal), then blend 2: frame f+6 with colour
  `(a1,1,1,1)`, frame f+12 with `(a2,1,1,1)`; blend back to 1.
- others: sprite 0x16b frame 0 scaled 1.05 (DAT_00817de4, ver > 0x58a), then blend 2: frame 1 with a1,
  frame 2 with a2. (CModel +0xdc takes (a, r, g, b, mesh) — PORTING.md "decompiler gotchas" — so the pulse is the alpha of a white quad.)

### 5.4 Saki effect family `0x8816c4 = FUN_00503530`, update FUN_00504630, draw FUN_00503900 (cosmetic)
| Id | Spawned by | Behaviour |
|---|---|---|
| 100 / 0x6e | boomerang / tambourines | sprite 0x16b afterimage, half 64·scale; alpha −0.05 (100) / −0.1 (0x6e) per frame |
| 200 | Sunshine / Big Bang bell glow | sprite 0x16c, sizes 144/128·scale (`+0x2c` written by the state); lives 2 frames |
| 0xfa / 0x104 | sparkles | sprite 0x16d; spawn draws `rand_f(1,4)`; moves along `+0x34` at `+0x3c`, converges toward Saki pos+(0,50), alpha +0.025, `+0x2c −= 0.02` (DAT_00813ed8) → removed at ≤ 0 |
| 300 | Big Bang flash | scale +0.05/frame, alpha −0.025 (DAT_00813edc) |
| 0x172 / 0x173 | Big Bang streaks | speed 20 / 16, scale/alpha ±0.03 / ±0.05 |
Generic ids used: 0xfa hyper flash, 0x456 streaks, 0x514/0x515 bursts, 0x834/500/600 explosions,
0xa28/0xa32/0xa82 sparks, 0xa8c, 0xaf0, 1000 hit, 2000 smoke. No gameplay effect except RNG.

### 5.5 RNG (gameplay-relevant draws)
Stardust/Galaxy bell angles, Galaxy start ages, maraca launch angle and spin, every missile's spawn
spin, critical-hit rolls (Hammer, Swing). Cosmetic draws (sparkles incl. dead offset draws, sparks,
smoke, hit effects) share the engine — replicate in order for replay-exact play.

---

## 6. Sound ids used (SE table all.c:464010; id = (addr − 0x9b99a0)/4)
| id | file | where |
|---|---|---|
| 6 | up01.wav | hyper start |
| 7 | high_a.wav | Big Bang Bell |
| 0xf | laser02.wav | Stardust / Galaxy Bell |
| 0x10 | laser03.wav | Tornado / Cyclone Cymbal |
| 0x13 | don16.wav | beam / hitbox hits |
| 0x14 | bom06.wav | maraca explodes |
| 0x15 | crash17.wav | tambourine hits (100/0x6e) |
| 0x18 | hit.wav | Maracas Swing hits |
| 0x19 | hit_f.wav | Hammer / Swing final hit; FUN_006b1d70 |
| 0x1a | tm2_bom002.wav | critical hit, maraca shot down |
| 0x1f | swing.wav | Maracas Hammer (and 0x163) |
| 0x20 | swing_h.wav | Maracas Swing, Sunshine Bell release |
| 0x27 | explosion1.wav | Big Bang blast |
| 0x2f | power35.wav | Maracas Grenade |
| 0x30 | synth-sweep2.wav | Sunshine Bell t=1, Big Bang t=0 |
| 0x31 | whip.wav | Boomerang, Tambourine Blade, Rolling Tambourine |

---

## 7. Other per-character table entries (index 11)
| Table | Saki entry | Role |
|---|---|---|
| 0x881310 (0x88133c) | FUN_00722ce0 | input dispatch, standard (§2) |
| 0x881360 (0x88138c) | FUN_00723200 | input dispatch, alternate (§2) |
| 0x8813b0 (0x8813dc) | FUN_0071cf70 | sprite loader (§4.1) |
| 0x881400+0x10·11 (0x8814b0/b4/b8) | FUN_00724010 (level 0), FUN_00723720 (levels 1, 2) | CPU AI (not reversed; e.g. holds H in 0x167 while t<80, all.c:401457) |
| 0x881878/8c8/918/968 (+0x2c) | 4b14c0 / 4b25b0 / 4b17d0 / 4b2260 | beam family (§5.1) |
| 0x8819b8/a08/a58/aa8/af8 (+0x2c) | 674ca0 / 676290 / 675060 / 675df0 / 676050 | missile family (§5.3) |
| 0x881b48 (0x881b74) | 0 | no absorb |
| 0x881b98/be8 (+0x2c) | 5e7fd0 / 5e8430 | hitbox family (§5.2) |
| 0x881698/6e8/738 (+0x2c) | 503530 / 504630 / 503900 | Saki effects (§5.4) |
| enemy 0x881558.., laser 0x881c38.., whip 0x881d78.., 0x881788/7d8/828 | — | none for index 11 |
Saki's code block also holds Nath's draw handlers 0x8803e4..0x8803f4 (FUN_0071c1b0..0071ccb0) at its start.

---

## 8. Stats record (index 11, base 0x883ef4), written by FUN_0049ae40 (registry.c:1844–1885)
| Off | Value | Meaning |
|---|---|---|
| +0x00 | 4.0 (DAT_00853b68) | walk speed per axis per frame |
| +0x04 | 14.0 (DAT_00813a54) | dash speed |
| +0x08 | 12.0 (DAT_00813f54) | dash hold speed (common dash state keeps speed >= this while D held; core.c st_dash) |
| +0x0c..+0x2c | {3,6,0}, {1,5,0}, {3,6,0} | team colour slots per palette (colour-table indices A, B, C; objects.md §5) — Saki objects use A = 3 yellow / B = 6 aqua in palette 0 |
| +0x3c/+0x40/+0x44 | 0x15a, 0x15b, 0x15c | sprite ids (ring / unregistered / system) |
| +0x48/+0x60/+0x78 | text ids 0x1b/0x1c/0x1d | colour-variant folder names |
| +0xa8 / +0x1a8 | `mdl\win_font_SK.lfd/.dat` | win font |
| +0x2a8 | `sk_res_bgp` | |
| +0x2c0/+0x2c4/+0x2c8 | 50, 50, 0 | UNVERIFIED |
| +0x2cc/+0x2d0/+0x2d4 | 255, 190, 120 | colour (RGB) |
| +0x2d8 | 0 | |
No HP value here (as Suguri). No version-patch entries.

---

## 9. UNVERIFIED / open items
- Folder fallback condition `FUN_007a2880` in the loader (mirror match colour choice).
- Effect 300 (Big Bang flash) sprite: FUN_00503900 never sets the sprite id for 300 and draws texture
  frame 2 of whatever Saki sprite the previous Saki effect draw left in `DAT_00895a18` (bank =
  `DAT_00885a00[team]`, ECX = 0x885b8c + bank·0x3888 + id·0x18). At t = 60 the last layer-1 Saki effect
  drawn before it is normally a sparkle (sprite 0x16d, only 2 frames) → out-of-range read. The port
  draws `sk_ssbell02` (the otherwise unused 256 px glow); settle with a runtime look at the flash.
- Stats +0x2c0..+0x2c8 (50, 50, 0).
- AI functions FUN_00724010 / FUN_00723720 are ported separately (src/sim/chars/ai_saki.c, not by me).

Resolved in the port pass (2026-11):
- Sunshine Bell hold: no Saki code drains the gauge. Every `+0xe4 -= ` in all.c is FUN_00780910
  (spend), FUN_00751920 (status 2 "drain"), FUN_00553760 / FUN_0055add0 (enemy objects) and
  FUN_006a4f60 / FUN_006a56b0 (other character code). So the `hyper <= 0` exit fires only when the
  1.0 payment left exactly 0 or an opponent's drain empties the gauge.
- CModel +0xdc order = (a, r, g, b) → missile pulses are alpha.
- `FUN_0068b8a0` draws 5 RNG values per particle, `FUN_007bd5e0` kind 1000 draws 2 per spark at spawn
  (engine `aos_spawn_particle` / `aos_spawn_spark`).
- `FUN_004a9f10` in FUN_006b1d70 = clear of the camera focus vector 0x895144 (`aos_camera_focus_clear`).
- AIM transform composition: vertex = Rz(aim)·Rx(180°)·Ry(180°)·v (inferred, not read from the matrix code:
  FUN_0046d2f0 multiplies into +0x6c..+0x8c); this is the only order that makes the left-facing art point along the aim, and it
  renders as flipX + rotZ(aim) (+ y-mirror when |aim| ≥ π/2).
- Stats +0x08 = dash hold speed; +0x0c..+0x2c = team colour slots.
- The asymmetric Maracas Grenade aim steps (0.4 up / 1.2 down) are literal (disassembly
  @0x720305 / 0x72033b) and ported as such.
