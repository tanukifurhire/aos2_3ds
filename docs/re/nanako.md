# Nanako — complete moveset spec

Source: `aos2.exe` (Ghidra corpus `tools/re/dec/all.c`, constants read with `xref.exe rd`).
Same conventions as suguri.md (the template — read its §1 first): `t` = player state timer `p+0x154`,
`ver` = `DAT_0089426c` = **0x5a2** in live play (objects.md §0.1) — only the ver ≥ 0x5a1 behaviour is
described as primary; older branches are listed only where they exist. "UNVERIFIED" items say what
would settle them. Generic collision / damage semantics: objects.md §4; frame order: objects.md §1.3.

Nanako = handbook id 10, **engine index 13** (`p+0x10 == 0xd`), player states 0x18d–0x1a3,
sprite prefix `nk_`, texture folders `charaNanakoA/B/C`.

**The core fact about Nanako:** her player states almost never spawn attacks themselves. Every
attack is a *command* broadcast to her **7 Bits** — Enemy-family objects (type 13, ids 100..106)
created once at round start by state 0x197. Each Bit runs its own state machine (§5.1) and spawns the
actual beams / missiles / lasers / hitboxes. Porting Nanako = porting the Bit object.

---

## 0. Notes for the lead (affect all characters / other specs)

1. **`CModel` vtable +0xdc (`FUN_0046ef70`) argument order is (a, r, g, b, meshIdx)**, not (r,g,b,a)
   as render.md says: the function builds the material colour `{r=param_3, g=param_4, b=param_5,
   a=param_2}` (locals `local_4c/48/44/40` = D3DCOLORVALUE r,g,b,a). Evidence in use: Nanako's missile
   glow passes `(pulse, 1, 1, 1)` with additive blend (pulse is clearly alpha), Bits pass
   `(1.0, z, z, z)` to fade from black. Check every draw handler already transcribed.
2. Stats record base is `0x881f80 + e*0x2dc`; for e = 13 that is **0x8844ac** (13*0x2dc = 0x252c),
   not 0x884478.
3. **Bit-spawned missiles and lasers carry `ownerId` = the Bit's Enemy id (`enemy+0x00`), not the
   player's `p+4`** (`(*DAT_008819ec)(…, *param_1, …)`, `(*DAT_00881c6c)(…, *param_1, …)` in
   FUN_00528e20). Beams and hitboxes spawned by Bits use the player id (`enemy+0x08`). Any code that
   resolves a projectile's owner with `FUN_007805f0(ownerId)` (owner-player lookup) gets NULL for these
   — check the damage / hit-confirm / training-display paths in core.md/objects.md against that.
4. Enemy update is called every frame even while the object's command timer `+0x80` is negative —
   Nanako uses **negative `+0x80` as a per-Bit start delay** (her command fn writes e.g. `-10-5k`).
5. Initial state 0x197 (core.md already lists it) zeroes the player hurt shape `+0x5c..+0x68` every
   frame (FUN_007063a0), so Nanako has **no hurtbox during the 100-frame intro**.
6. Common state 6 (KO) T2 `FUN_006b0110` calls the character's T2[sprite-state] only for engine
   indices 4, 5, 6, 7, 8, 0xd, 0xf (Nanako: 0x18f → recall/destroy Bits). Other characters' KO T2 stubs
   are never called.

### Player fields used by Nanako's code
Same as suguri.md table; additionally:

| Off | Use |
|---|---|
| +0x40 (S button) | **read directly by every Bit each frame** (`owner+0x40 < 2` / `== 3`) — "Deploy Bits" (§2.1) |
| +0x150 | read by Bits: `== 2` (dashing) → Bits move ×2.5 faster (DAT_008153e0) |
| +0x5c..+0x68 | hurt shape (core.md) — zeroed every frame in 0x197 |

---

## 1. Shared mechanics

All helpers are as in suguri.md §1 (aim `FUN_007804b0`, distance `FUN_00780570`, input angle
`FUN_0061b230`, `FUN_005f0e70` rotate-toward, `FUN_005f1210` rotate vector, cos/sin, RNG
`FUN_00459fd0`/`FUN_00459f80`, SE `FUN_007bc9c0`/`FUN_007bcaf0`, dash-cancel `FUN_006b17d0(p,1)`,
hyper start `FUN_006b1b40`, hyper spend `FUN_00780910`, hyper permission `FUN_00780950`, cost table
`FUN_007807f0`, training frame-data recorders `FUN_00612830`/`FUN_00612900` — no gameplay effect).

- **Nanako's states never move her** (no state writes `+0x94/+0x98` except dash), and none checks the
  hit-confirm flag `+0x158`. There are no hit-confirm cancels.
- "Dash cancel at N" = `t >= N && Dash == 3 → FUN_006b17d0(p,1)`; FUN_00612900 is called at `t == N`.
- Bit-owned helper: `FUN_0055f110(bit)` = angle from the Bit to the opponent, with the same Nath
  special case as FUN_007804b0 (opponent `+0x10==5 && +0x164==1` → owner's input angle, or π/0 by
  x side).

### 1.1 Cost table rows (`0x87c4f0 + id*0xc`, written by FUN_0049ae40, registry.c:2005-2025)

Note: rows 0x198–0x19c are **charged per Bit per shot** (inside the Bit update), so one button press
can cost up to 7× (or 49×) the row. A Bit simply doesn't fire if the check fails.

| Row | Charged by | costA (+0xe8) | costB (+0xec) | hyperGain |
|---|---|---|---|---|
| 0x198 | Bit mode 0x96 (Tentacle Tone), per Bit | 0.075 (DAT_00813efc) | 0 | 0.01 (DAT_00813ec8) |
| 0x199 | Bit mode 100 (Tentacle Needle), per Bit | 0.1 (DAT_00853b10) | 0 | 0.02 (DAT_00813ed8) |
| 0x19a | Bit mode 0xa0 (Tentacle Fruit), per shot | 0.03 (DAT_00813ee0) | 0 | 0.005 (DAT_00813eac) |
| 0x19b | Bit mode 200 (Ether Leaf), per Bit | 0 | 0.075 (DAT_00813efc) | 0.01 |
| 0x19c | Bit mode 0xfa (Ether Bug), per shot | 0 | 0.04 (DAT_00813eec) | 0 |
| 0x19d | player 0x19d (Shell Shot), once, only if ≥1 Bit accepted | 0 | 0 | 0.05 (DAT_00813ef4) |
| 0x19e | player 0x19e (Shell Shot follow-up) | 0 | 0 | 0.05 |

No other Nanako state id has a row (all zero). The version patch `FUN_004a7270` touches no Nanako
entry (grep of 0x87d810–0x87d860 / 0x8844xx in it: none).

---

## 2. Input dispatch `FUN_00707380` (standard scheme, table 0x881344) — exact decode

**A. Hypers** — only if `FUN_00780950(p)`; first match wins; cost via `FUN_00780910`; if the cost
check fails the next row is tried.

| # | Condition | Cost | State | Move |
|---|---|---|---|---|
| 1 | S held (`+0x40>1`) && B held (`+0x3c>1`) && H pressed (`+0x44==3`) | 3.0 (DAT_00813a50) | 0x1a3 | **Spring Storm** (S+B+H, Accel) |
| 2 | S held && H pressed | 1.0 (DAT_00853b34) | 0x1a1 | **Tentacle Sting** (S+H) |
| 3 | B held && H pressed | 1.0 | 0x1a2 | **Flower Crown** (B+H) |
| 4 | Dash held (`+0x34>1`) && H pressed | 1.0 | 0x1a0 | **Tentacle Breath (All at Once)** (D+H) |
| 5 | H pressed | 1.0 | 0x19f | **Tentacle Breath (One by One)** (H) |

**B. Normal moves** — only if `state == 0 || state == 2`:

| # | Condition | Result | Move |
|---|---|---|---|
| 1 | Dash pressed (`+0x34==3`) | `FUN_006b17d0(p,0)` | Dash |
| 2 | else Shield pressed && `FUN_00780910(p,0.5)` && `FUN_007809e0(p)` | `FUN_006b1930(p,0)` | Shield |
| 3 | S held && B pressed (`+0x3c==3`) | 0x19d | **Shell Shot** (S+B) |
| 4 | B pressed | 0x19c (→ 0x19b on early release) | **Ether Bug** (B hold) / **Ether Leaf** (B) |
| 5 | Dash held (`+0x34>1`, i.e. ≥2 — stricter than Suguri's `>0`) && A pressed | 0x19a | **Tentacle Fruit** (D+A) |
| 6 | A pressed | 0x199 (→ 0x198 on early release) | **Tentacle Needle** (A hold) / **Tentacle Tone** (A) |

Then `return T2[state](p) != 0`. S+A has no special meaning (falls to row 6).

**Alternate scheme `FUN_007077f0`** (table 0x881394, `p+0x0c==1`): identical except the hyper rows
are "H held (`+0x44>1`) and X pressed": 1. Shield pressed (`+0x48==3`) → 0x1a3; 2. S pressed → 0x1a1;
3. B pressed → 0x1a2; 4. Dash pressed → 0x1a0; 5. A pressed → 0x19f (diff of the two functions).

### 2.1 "Deploy Bits" — S (Hold)
There is **no Deploy-Bits state and no dispatch row**. The Bits read their owner's S button directly in
their idle mode (Bit mode 10, §5.1, FUN_00528e20 lines for mode 10):
- `owner+0x40 < 2` (S not held): each Bit homes on a slot **100 px** (DAT_00853b90) from Nanako;
- `owner+0x40 >= 2` (S held): slot radius **240 px** (DAT_00815584) — the Bits spread out;
- `owner+0x40 == 3` (S pressed this frame): Bit speed := 16 (DAT_008140d8), decel 0.5 (a burst toward the new slot).
This works in every player state (even mid-attack and during dash), but only affects Bits that are in
idle mode 10. S is consumed by nothing else in Nanako's kit except as a modifier (S+B, S+H, S+B+H).

---

## 3. Player states

Common patterns: "aim" = `+0x84 = FUN_007804b0(p)` (stored only; Nanako's states never use it except
for drawing and 0x1a0). "cmd(X)" = `FUN_00704e30(X, p+4, p+0x14)` = broadcast command X to her Bits
(§5.2). "flourish" = `FUN_006b1b40(p,10)`; SE 6 (`up01.wav`); `FUN_004e9280(100, 0.75 (DAT_00813f3c))`;
`FUN_007bec20(100, &pos, a, b)`; `FUN_004d7ac0(20, 0.5, 2)` (cosmetic screen effects, as Suguri; **no**
effect 0xfa for normal hypers). "face" = `+0x84 = +0x88 = (opp.x <= x) ? π : 0` (no atan2).

### 0x18d — neutral movement (T2 FUN_00706250; called from common state 0 T2)
As Suguri 0x128 with `speed = stats+0 = 4.0` (DAT_00853b68 → 0x8844ac) per axis per frame, no
diagonal normalisation; `+0x84 = facing==1 ? 0 : π`. Returns 1.

### 0x18e, 0x190, 0x191 — stubs (T2 FUN_00706340 / 00706350 / 00706360 return 1)
Only their T1s are used (common states 2 / 1,5 / 3,4 — §4.3). 0x192–0x196 have no handlers (sprite ids only).

### 0x18f — KO hook (T2 FUN_00706370; called each frame of common state 6 via FUN_006b0110 `case 0xd`)
`t == 0`: cmd(900) → every Bit self-destructs after `100 − 10k` frames (k = Bit index 0..6; §5.1 mode 900).

### 0x197 — round intro / Bit deployment (T2 FUN_007063a0) — initial state (FUN_0069cc40 `case 0xd`)
| t | Action |
|---|---|
| every frame | hurt shape `+0x5c = +0x60 = +0x64 = +0x68 = 0` (no hurtbox) |
| 50 | for k = 0..6: `idx = (*DAT_0088158c)(100+k, p+4, p+0x14, p+0x18)` (Bit spawn, §5.1); Bit `pos(+0x40..48) = p pos`; `+0x58 = p+0x84`; `+0x84 (mode) = 0`; `+0x80 = −10·|k−3|` (FUN_007e1bee = abs) — middle Bit first, outer Bits up to 30 frames later; `+0x74 = 10.0` (DAT_00813f4c); then whip trail `(*DAT_00881dac)(5, p+0x18, bit.id)` (§5.7) |
| >100 | state 0 |
No dash cancel; normal moves are blocked (state ≠ 0/2); hypers are possible in principle (gauge is 0 at round start).

### 0x199 — A press / Tentacle Needle (T2 FUN_00706600)
| t | Action |
|---|---|
| 0 | aim |
| 0–14 | if A not held (`+0x38<2`): **t := 10**, state := 0x198, run T2[0x198] now (flag kept) |
| 15 | cmd(100) → all 7 Bits fire beam 100 at t+5 (§5.1 mode 100) |
| ≥25 | dash-cancel; >40 state 0 |

### 0x198 — Tentacle Tone (T2 FUN_00706540) — via 0x199 early release
Entered with t = 10 (so the t==0 aim never runs here; aim was set by 0x199).
| t | Action |
|---|---|
| 11 | cmd(0x96) → Bits fire beam 200, staggered (§5.1) |
| ≥20 | dash-cancel; >40 state 0 |
Net: release on any frame 0–14 → command fires on the frame after release (t=11) — e.g. release at
t=12 sets t back to 10.

### 0x19a — Tentacle Fruit (T2 FUN_00706710) — Dash held + A
t0 aim; **t10 cmd(0xa0)**; dash-cancel ≥30; >50 state 0.

### 0x19c — B press / Ether Bug (T2 FUN_00706890)
| t | Action |
|---|---|
| 0 | aim |
| 0–11 | if B not held (`+0x3c<2`): state := 0x19b (**timer kept**), run it |
| 12 | cmd(0xfa) |
| ≥35 (ver ≤ 0x5a0: 45) | dash-cancel |
| >50 (ver ≤ 0x5a0: 60) | state 0 |

### 0x19b — Ether Leaf (T2 FUN_007067d0) — via 0x19c early release
t0 aim (fresh entry only); **t15 cmd(200)**; dash-cancel ≥20; >40 state 0.

### 0x19d — Shell Shot (T2 FUN_00706f40) — S held + B
Version-dependent frames: `L8` (follow-up check) = 23 (ver ≥ 0x5a1; 0x58c..0x5a0: 28; older 18),
`Lc` (dash-cancel) = 20 (0x58c..0x5a0: 30), `L10` (end) = 30 (0x58c..0x5a0: 40).
| t | Action |
|---|---|
| 0 | aim |
| 5 | `n = cmd(500)` (sends Bits 0x66 and 0x68 to tackle, §5.1 mode 500); if n > 0: `FUN_007807f0(p,0x19d)` (+0.05 hyper), SE 0x2a (`A5_01029.wav`) |
| 23 | if B held (`+0x3c>1`): state **0x19e** (t=0, flag=0), run it |
| ≥20 | dash-cancel (checked only when the 0x19e branch did not fire) |
| >30 | state 0 |

### 0x19e — Shell Shot follow-up (T2 FUN_007070e0) — 0x19d with B still held at t=23 (not in the manual)
t0 aim; **t3**: if `FUN_007807f0(p,0x19e)` (always succeeds, +0.05 hyper) → cmd(0x226) (sends Bits
0x65 and 0x69 to tackle). Dash-cancel ≥12; >20 state 0.

### 0x19f — Tentacle Breath (One by One) (T2 FUN_007069d0) — H, 1 level
t0: aim, flourish with `FUN_007bec20(100, pos, 0, 0)`. **t10 cmd(300)**. Dash-cancel ≥60; >70 state 0.

### 0x1a0 — Tentacle Breath (All at Once) (T2 FUN_00706b00) — D+H, 1 level
t0: aim, flourish with `FUN_007bec20(100, pos, aim, π/4 (DAT_0081fa90))`. **t10 cmd(0x136)**.
Dash-cancel ≥50; >70 state 0.

### 0x1a1 — Tentacle Sting (T2 FUN_00706c40) — S+H, 1 level
t0: face, flourish (`FUN_007bec20(100,pos,0,0)`). **t1 cmd(0x15e)**. Dash-cancel ≥70; >80 state 0.

### 0x1a2 — Flower Crown (T2 FUN_00706dc0) — B+H, 1 level
t0: face, flourish. **t10 cmd(400)**. Dash-cancel ≥20; >60 state 0.

### 0x1a3 — Spring Storm (T2 FUN_007071c0) — S+B+H, 3 levels
t0: face; `FUN_006b1b40(p,0x14)` (Accel); SE 7 (`high_a.wav`); `FUN_004e9280(100, 0.85 (DAT_00814120))`;
`FUN_007bec20(100,pos,0,0)`; `FUN_004d7ac0(100, 0.75, 2)`; effect `0x881698(0xfa, 0, pos, 0, p+4, 0, side)`.
**t1 cmd(600)**. Dash-cancel ≥80; >100 state 0.

---

## 4. Animation

### 4.1 Sprite registration (loader `FUN_007028c0`)
Arg selects the folder: 1 → string id 0x16, 2 → 0x17, else 0x15 (charaNanakoA/B/C presumably).
Quad size S as in Suguri (`FUN_00467d00(S,S,…)`); storage `0x885b80 + slot*0x3888 + id*0x18`.

| Sprite id | Pattern | Frames | S | Used by |
|---|---|---|---|---|
| 0x18d | `nk_std0%d` | 3 | 96 (DAT_00814118) | neutral |
| 0x192 | `nk_fwd0%d` | 3 | 96 | neutral, moving toward |
| 0x193 | `nk_bck0%d` | 3 | 96 | neutral, moving away |
| 0x18f | `nk_los0%d` | 6 | 96 | T1 0x18f (KO), 0x190 (hit) |
| 0x191 | `nk_sld0%d` | 5 | 96 | T1 0x191, 0x199, 0x19a, 0x19c, 0x19d, 0x19e, 0x1a0 |
| 0x18e | `nk_dsh0%d` | 6 | 96 | dash |
| 0x198 | `nk_atka0%d` | 5 | 96 | T1 0x198, 0x19b, 0x19f |
| 0x197 | `nk_atkb0%d` | 5 | 96 | T1 0x197, 0x1a1, 0x1a2, 0x1a3 |
| 0x194 | `nk_rng0%d` | 2 | 512 (DAT_008178b0) | stats +0x3c (HUD ring?, UNVERIFIED) |
| 0x196 | `nk_system` | 1 | 512 | stats +0x44 (UI?) |
| 0x1a4 | `nk_bit0%d`/`nk_bit%d` | 14 (00–13) | 54 (DAT_008178a8) | Bits (§5.1) |
| 0x1a5 | `nk_leaf0%d` | 3 | 54 | missile 200 |
| 0x1a6 | `nk_bug0%d` | 3 | 54 | missile 300 |
| 0x1a8 | `nk_petal0%d` | 2 | 64 (DAT_008140f8) | effects (400/0x19a?) |
| 0x1a7 | `nk_lsr` | 1 | 256 (DAT_00814160) | lasers (drawn from a 512×512 atlas via sprite batch `FUN_0047f7b0(0)`) |
| 0x19d | `nk_cone0%d` | 9 | 54 | effect (draw FUN_004ff3e0) |
| 0x1a9 | `nk_bitmelee0%d` | 2 | 62 (DAT_00825a00) | glow on Bits 0x66/0x68 |
| whip 5 | `nk_whip0` | 1 | — | Bit trail ribbon, stored at `0x87fe48 + 5*4 + arg*0x3c` (+0x87ff38 table) |
`nk_bit14.png` exists in the folder but is not loaded (loop `< 0xe`). ids 0x190, 0x195, 0x199–0x19c,
0x19e–0x1a3 are not registered.

### 4.2 Draw transforms
All of Nanako's T1s except neutral use Suguri's **AIM transform** (suguri.md §4.2: rotZ(aim);
if |aim| ≥ π/2 also rotX(180°); then rotY(180°)). Neutral 0x18d: rotY(180°) iff facing != 0.
Tint `FUN_004e8f50` before every draw; blend normal. Afterimage `FUN_004d7e00` only in 0x18d and 0x18e.

### 4.3 Common states → Nanako draw handlers
| Common state | Nanako T1 |
|---|---|
| 0 neutral | 0x18d FUN_00708a90 |
| 1 hit, 5 | 0x190 FUN_00709150 |
| 2 dash | 0x18e FUN_00708d50 |
| 3 shield, 4 | 0x191 FUN_00709420 |
| 6 KO | 0x18f FUN_00709700 |
(from the `case 0xd:` of FUN_006b07a0 / 6b0c40 / 6b09f0 / 6b0e90 / 6b10e0 / 6b1330 / 6b1580.)

### 4.4 Per-state frames
`pp(n)` = `(t/n)&3`, 3→1. `L5` = `n = t/5; if n > 1: n = ((n−2)&3) + 2; if n == 5: n = 3` → sequence
0,1,2,3,4,3,2,3,4,3,2,… (5 frames each). `C5` = `min(t/5, 4)`.

| State | T1 | Sprite | Frame |
|---|---|---|---|
| 0x18d | FUN_00708a90 | 0x18d, overridden by direction exactly as Suguri (y+→0x192, y−→0x193, facing0&x−→0x192, facing1&x−→0x193, facing0&x+→0x193, facing1&x+→0x192; later check wins) | pp(5) |
| 0x18e | FUN_00708d50 | 0x18e | pp(3); +3 when moving against facing (facing1 && \|aim\|>π/2, or facing0 && \|aim\|<π/2) |
| 0x18f | FUN_00709700 | 0x18f | pp(3) + (3 if `DAT_008858f8` even); **drawn only while t < 100** |
| 0x190 | FUN_00709150 | 0x18f | pp(3) |
| 0x191 | FUN_00709420 | 0x191 | L5 |
| 0x197 | FUN_00709a00 | 0x197 | L5 |
| 0x198 | FUN_00709ce0 | 0x198 | C5 |
| 0x199 | FUN_00709fa0 | 0x191 | L5 |
| 0x19a | FUN_0070a280 | 0x191 | L5 |
| 0x19b | FUN_0070a560 | 0x198 | C5 |
| 0x19c | FUN_0070a820 | 0x191 | L5 |
| 0x19d | FUN_0070b660 | 0x191 | L5 |
| 0x19e | FUN_0070b940 | 0x191 | L5 |
| 0x19f | FUN_0070ab00 | 0x198 | C5 |
| 0x1a0 | FUN_0070adc0 | 0x191 | L5 |
| 0x1a1 | FUN_0070b0a0 | 0x197 | L5 |
| 0x1a2 | FUN_0070b380 | 0x197 | L5 |
| 0x1a3 | FUN_0070bc20 | 0x197 | L5 |
(Sprite ids from the `imul …,0x18` operands: e.g. `mov [ebp-0x14],0x18d` @0x708ab5; one immediate per
handler found by disassembly. Note 0x198 is entered at t=10 → C5 starts at frame 2.)

---

## 5. Spawned objects

All Nanako objects are type 13 (engine index). Family tables (index 13):
| Family | Spawn / Update / Draw / extra |
|---|---|
| Enemy (Bits) | 0x88158c FUN_00528ca0 / 0x88162c FUN_00528e20 / 0x8815dc FUN_0052bbf0 / onKilled 0x88167c FUN_0052b980 |
| Beam | 0x8818ac FUN_004af380 / 0x8818fc FUN_004b0220 / 0x88194c FUN_004af560 / onHit 0x88199c FUN_004aff60 |
| Missile | 0x8819ec FUN_00670310 / 0x881a3c FUN_00671310 / 0x881a8c FUN_006704b0 / onHit 0x881adc FUN_006710b0 / onKilled 0x881b2c FUN_006711e0 / absorb 0x881b7c = 0 |
| Hitbox | 0x881bcc FUN_005e6730 / onHit 0x881c1c FUN_005e6a90 |
| Laser | 0x881c6c FUN_00620e90 / 0x881cbc FUN_00622af0 / 0x881d0c FUN_00620ff0 / onHit 0x881d5c FUN_00622ad0 (empty) |
| Whip | 0x881dac FUN_007c6a80 / 0x881dfc FUN_007c6f00 / 0x881e4c FUN_007c6b70 |
| Effect | 0x8816cc FUN_004fef30 / 0x88171c FUN_005004c0 / 0x88176c FUN_004ff3e0 |

### 5.1 Bits (Enemy family, ids 100..106) — spawn FUN_00528ca0, update FUN_00528e20, draw FUN_0052bbf0

**Spawn template** (FUN_00528ca0; struct offsets per objects.md §2.6): `+0x0c` type 13, `+0x10` idx
(100+k), `+0x24/+0x28` hp/maxHp 9999, `+0x2c` active **0** (projectiles pass through; Bits cannot be
hit or destroyed by the opponent), shape `{0, 0, 20}` (DAT_008140fc), scale `(3.0 (DAT_00813a50), 0, 0)`,
`+0x6c` 0, `+0x7c` 100, `+0x84` mode 0, `+0x88` 0, `+0xe4` 0, solid 0. Caller (0x197) sets pos, `+0x58`,
`+0x80`, `+0x74`.

Bit fields: `+0x40` pos, `+0x58` **angle** (facing/firing direction, also draw rotation), `+0x5c`
**heading** (movement direction), `+0x60/+0x64` scale x/y, `+0x68` brightness (scaleZ slot; 0→1 at
deploy), `+0x70` speed (in mode 0: orbit radius), `+0x74` decel (mode 0: radius velocity; mode 200
missiles: n/a), `+0x78` age, `+0x7c` sub-kind (always 100 → `switch(+0x7c−100)` case 0), `+0x80`
command timer (incremented after update by the Enemy loop, objects.md §1.4; **negative = delay**),
`+0x84` mode. `k = idx − 100`. "slot angle" `σ(k) = (idx−103)/7·2π` (DAT_0081789c = 7, DAT_00853b78 = 2π).

The update does nothing if the owner (`FUN_00780450(team)`) or opponent (`FUN_00780480(team)`)
is missing. Let `O` = owner (Nanako), `E` = opponent, `T` = `+0x80`. "→idle" = `T=0, mode=10,
speed=16 (DAT_008140d8), decel=0.5 (DAT_00853b1c)`. Modes are tested in the order below
(if/else chain).

**Mode 0 — deploy.** `T==0`: SE 0x2f (`power35.wav`). `0 ≤ T < 20`: scaleX −= 0.1, scaleY += 0.05
(DAT_00853b10, DAT_00813ef4); radius `+0x70 += +0x74; +0x74 *= 0.95` (_DAT_00814124); angle =
heading = `σ(k) + π/2`; pos = O.pos + (cos,sin)(heading)·radius (Bits fan out to ≈128 px).
`T > 19`: brightness += 0.05. `T > 40`: brightness = scaleX = scaleY = 1, T=0, **mode 10**, speed 20
(DAT_008140fc), decel 0.5. (ver ≤ 0x5a0 also adds rand_f(−0.2, 0.2) to heading here.)

**Mode 10 — idle / follow.**
- `s = speed` (×2.5 DAT_008153e0 if `O.state == 2`); pos += (cos,sin)(heading)·s.
- `φ = σ(k) + atan2(E.y−O.y, E.x−O.x)`; target `P = O.pos + R·(cos,sin)(φ)`, **R = 100 if `O+0x40 < 2`, else 240** (§2.1).
- heading rotates toward `atan2(P − pos)` by ≤ `speed·0.015` (DAT_00813ed4).
- angle = `FUN_0055f110(bit)` (faces the opponent).
- speed −= decel, floor **8** (DAT_008140d4) — Bits never stop; they circle/jitter around their slot.
- `T > 349` → T = 0 (only matters for the ver ≤ 0x5a0 random retarget at `T == k·50`).
- `O+0x40 == 3` → speed 16, decel 0.5.

**Mode 100 — Tentacle Needle.** T0: angle = FUN_0055f110. **T5**: if `FUN_007807f0(O,0x199)`:
beam **100** at pos, angle (owner id = player `+8`), effect `0x881698(0x548, teamColour, pos, angle,…)`,
SE 0xd lock 3. T>50 → idle. (No movement in this mode.)

**Mode 0x96 — Tentacle Tone.** As mode 100 but cost row 0x198, beam **200**, SE 0x10 (`laser03.wav`)
lock 5; T>30 → idle.

**Mode 0xa0 — Tentacle Fruit.** Every frame: angle ±= 0.1 (DAT_00853b10; **+ for even idx, − for odd**),
wrapped to (−π, π]. T ∈ {20,25,30,35,40,45,50} (`9<T && 19<T && T%5==0`): if
`FUN_007807f0(O,0x19a)`: beam **300** at pos, angle; effect 0x516 (teamColour) at pos; SE 0xf
(`laser02.wav`) lock 3; `return 1` (skip rest). T>50 → idle. → up to 7 shots × 7 Bits.

**Mode 200 — Ether Leaf.** T0: angle = FUN_0055f110 + `sgn·0.6` (DAT_00813f34), `sgn = (idx odd) ? +1 : −1`
(ver ≤ 0x5a0: `sgn·rand_f(0.2, 1.2)`). **T5**: if `FUN_007807f0(O,0x19b)` (ver ≤ 0x5a0 also requires the
Bit inside the arena): missile **200** at pos, angle, **ownerId = Bit id**; then that missile's turn
rate `+0x74 = −sgn·0.025` (DAT_00813edc) (curves back across the aim line); SE 0x10 lock 3; `return 1`.
T>30 → idle.

**Mode 0xfa — Ether Bug.** T0: angle += π. Every frame angle ±= 0.1 (as 0xa0), wrapped. T ∈
{20,25,…,50}: if `FUN_007807f0(O,0x19c)`: missile **300** at pos, angle (ownerId = Bit id); SE 0x31
(`whip.wav`) lock 5; `return 1`. T>50 → idle. (ver ≤ 0x5a0 also requires the Bit inside the arena.)

**Mode 300 — Tentacle Breath, one by one.** T0: angle = FUN_0055f110. **T10**: laser **100** at pos,
angle (ownerId = Bit id, team, p18); SE 0x12 (`gun11_r.wav`) lock 5. T>50 → idle.

**Mode 0x136 — Tentacle Breath, all at once.** T0: angle = **FUN_007804b0(O)** (Nanako's aim at the
opponent — all 7 beams are parallel). **T10**: laser **0x96**; SE 0x12 lock 5. T>50 → idle.

**Modes 0x15e / 0x160 — Tentacle Sting, approach.** Every frame (also while T < 0): effect
`0x8816cc(300 + age%3, 0, pos, angle, …)` (afterimage); `s = speed` (×2.5 if O dashing); pos +=
(cos,sin)(heading)·s; target `P = E.pos + r·(cos,sin)(β)` with 0x15e: `r=180` (DAT_00815258),
`β = k/7·2π`; 0x160: `r=240`, `β = ((idx−98)%7)/7·2π` (slot shifted by 2); heading → `atan2(P−pos)` by
≤ `speed·0.02` (DAT_00813ed8); angle = atan2(E − pos); speed −= decel, floor 8. `T > 25` → T=0,
speed 20, decel 1.0, heading = π (ver ≤ 0x5a0: rand_f(−π, π)), mode += 1.

**Modes 0x15f / 0x161 — Tentacle Sting, fire.** No movement. T0: angle = atan2(E − pos). **T5**: beam
**100** at pos, angle (**no cost check**), effect 0x548, SE 0xd lock 3. `T > 10` → T=0, speed 16,
decel 0.5, mode += 1; 0x162 → mode 10. ⇒ each Bit fires twice (14 beams total).

**Mode 400 — Flower Crown.** No movement. Every frame: angle ±= 0.1 (even/odd idx), wrapped;
heading = angle. T ∈ {10,15,20}: 4 × beam **300** at angle, angle+π, angle+π/2, angle−π/2 (no cost).
T == 14 (`T%7==0` in 10..20): 4 × missile **300** in the same 4 directions (ownerId = Bit id, no cost).
T == 1: 4 effects `0x8816cc(400, 0, pos, angle + j·π/2)` for j = −2..1. T>50 → idle.

**Mode 500 — Shell Shot tackle** (Bits 0x66, 0x68; and 0x65, 0x69 via 0x226). Set up by the command:
angle = heading = atan2(E − pos), speed 2.0 (DAT_00813a4c), T = −5 (0x66) / −10 (0x68) / 0 (0x65, 0x69,
ver ≥ 0x58d). With ver ≥ 0x58c: `A = 10`, `End = 30`.
- `T ≤ 8`: pos += heading·speed; heading → atan2(E−pos) by ≤ 0.02; angle = heading (creeps and tracks).
- `T = 9, 10`: as above plus afterimage effect `300+age%3`, speed += 0.25 (DAT_00813f24); at `T == 10`:
  speed := **20**, SE 0x1f (`swing.wav`).
- `T = 11..30`: pos += heading·speed (straight, no steering, constant 20 px/frame). While T < 25:
  afterimage effect and effect `0x8816cc(500)`; when `((T + A) & 3) == 0` (T ∈ {14, 18, 22} → **3 hitboxes**):
  hitbox **100** at pos, angle = heading (owner = player id). At T == 11: 5 sparks:
  `r = rand_f(−0.15, 0.15)` (DAT_00815dc4, DAT_00813f20), `n = rand_i(0,100)`, effect `0xa32 + n%4` at pos,
  angle heading + r (RNG order: float then int, per spark).
- `T > 30` → mode **0x1f5**, T=0, speed 20, decel 0.5.

**Mode 0x1f5 — return.** As mode 10's homing but always R = 100 and turn ≤ `speed·0.015`; `s` ×2.5 if O
dashing; angle = atan2(E − pos); speed −= decel, floor 8 (ver == 0x58b only: 60 frames, ×0.75, floor 6).
After 30 frames → mode 10, T=0. Command 500/0x226 skips Bits currently in 500 or 0x1f5.

**Mode 600 — Spring Storm, gather.** `φ = idx/7·2π + π/2` (note: idx, not k), wrapped to (−π, π];
`P = O.pos + rotate((140, 0), φ)` (DAT_00817004); heading → atan2(P − pos) by ≤ 0.4 (DAT_00813f30);
angle = heading; speed = 16 if T < 10 else 8; pos += heading·speed. `T > 20` → mode **0x262**, T=0,
pos := P, heading = angle = φ.

**Mode 0x262 — Spring Storm, fire.** T==1: 4 effects `0x8816cc(0x19a, 0, pos, angle + j·π/2)`, j=−2..1.
**T==10**: laser **200** at pos, angle φ (radially outward from Nanako; ownerId = Bit id), SE 0x12 lock 5.
Every frame: heading ±= 0.1 (even/odd idx), wrapped only from above (`> π → −2π`). T ∈ {10,15,20}:
4 × beam **300** at heading+π, heading+π (**duplicated — no beam at +0**), heading−π/2, heading+π/2;
SE 0xf lock 3. `T > 49` → mode 10, T=0 (speed/decel unchanged).

**Mode 900 — death.** No movement. `T > 99` → `FUN_0052b980(bit)` (explosion: effect 0x26c; 3 × effect
0x834 at pos + (rand_f(−50,50), rand_f(−50,50)); 3 × effect 500 at pos + (rand_f(−20,20), rand_f(−20,20)) with age0 −10−3j; SE 0x1d `gun12.wav`) and the Bit is removed (`return 0`). Set with T = 10k → Bit k dies after
100 − 10k frames.

Any other mode value: nothing (Bit frozen).

**Draw (FUN_0052bbf0)**, sprite 0x1a4 (54 px), rotZ(angle), colour (a=1, rgb=brightness `+0x68`), normal blend:
| Mode | Frame |
|---|---|
| 0 | `(|T|/5)%3`, scale (`+0x60`, `+0x64`, 1) |
| 10 | `(T/5)%3` |
| 100, 0x96, 0xa0, 200, 0xfa, 300, 0x136 | `3·m+3`, m = T<30 ? min(max(T,0)/2, 3) : 3 − min((T−30)/4, 3) (opening 3→6→9→12, closing) |
| 0x15f, 0x161 | `3·m+3`, m = T<6 ? min(T,3) : clamp(10−T, 0, 3) |
| 400 | T<0: `(|T|/5)%3`; T<46: `3·min(T,3)+3`; else `3·clamp(10−T,0,3)+3` |
| 0x262 | T<46: `3·min(T,3)+3`; else `3·clamp(10−T,0,3)+3` |
| others (500, 0x1f5, 0x15e, 0x160, 600, 900) | `(|T|/5)%3` |
Bits 0x66 and 0x68 (ver > 0x58a) additionally draw first, with additive blend (mode 2), sprite 0x1a9
frame 0 (colour a=0.5, rgb=0.25·brightness) and frame 1 (a=1, rgb=0.75·brightness), scale
(`+0x60`,`+0x64`,1), rotZ(angle). A pulse `0.75+0.25·sin((age%50)/50·2π)` is computed but appears unused
(UNVERIFIED). Draw layer `+0x1c` = 1 (default).

### 5.2 Command broadcast `FUN_00704e30(cmd, ownerId, team)` → returns #Bits accepted (500/0x226 only)
For every Enemy with `+0x08 == ownerId && +0x14 == team && 100 ≤ +0x10 < 0x6b`, sets `+0x84 = mode`
and `+0x80 = delay` (**overrides whatever the Bit is doing**, except 500/0x226):
| cmd | mode | `+0x80` (k = idx−100) | Extra |
|---|---|---|---|
| 100, 200, 0xfa, 600, other | same | 0 | |
| 0x96, 0xa0 | same | −10 − 5k | |
| 300 | 300 | −20 − 10k | |
| 0x136 | 0x136 | −10 | |
| 0x15e | 0x15e | −3k | speed 20 (DAT_008140fc), decel 0.2 (DAT_00853b14) |
| 400 | 400 | −2k | |
| 900 | 900 | +10k | |
| 500 | 500 | Bit 0x66: −5, 0x68: −10 | only Bits 0x66, 0x68 not already in 500/0x1f5; angle = heading = atan2(E−bit); speed 2.0; counted |
| 0x226 | **500** | 0 (ver < 0x58d: −10) | only Bits 0x65, 0x69 not in 500/0x1f5; same setup; counted |

### 5.3 Beams (FUN_004af380 / FUN_004b0220 / draw FUN_004af560 / onHit FUN_004aff60)
Common init FUN_004ba490 (objects.md §2.2): hitType 1 (dash passes through), destroyOnHit 1, stun 1.0.
| Id | Used by | speed | shape {hx,hy,r} | dmg | knock | behaviour |
|---|---|---|---|---|---|---|
| 100 Needle / Sting | modes 100, 0x15f/0x161 | 20 (DAT_008140fc) | {24, 8, 0} (DAT_00853b8c, DAT_008140d4) | 100 | 10 | straight; dies when age > 20 and outside arena+20 (`FUN_0061ac10(pos, 20)`, arena r 540); each frame effect `0x8816cc(200, teamColour, pos, angle)`; age 1: effect 0x548 |
| 200 Tone | mode 0x96 | 15 (DAT_00853b88) | {20, 10, 0} (DAT_008140fc, DAT_00813f4c) | 120 | 10 | as 100; each frame effect `0x8816cc(0xd2)` + particle `FUN_0068b8a0(0x4b0, colour, pos, angle+π, …, 1)` |
| 300 Fruit / Crown / Storm | modes 0xa0, 400, 0x262 | 14 (DAT_00813a54) | {0, 0, 10} (DAT_00813f4c) | 50 | 10 | scale `+0x58` starts 2.0 (DAT_00813a4c), −0.1/frame to 1.0; speed −0.5/frame while > 2; **never leaves via arena check**; at age 50 → id 0x136, active = 0, scale = 2 |
| 0x136 (300 fading) | — | 0 (no move code) | inactive (`+0x70=0`) | — | — | no movement; scale −0.25/frame, removed at ≤ 0 (8 frames); effect 0x516 spawned at the switch |
On hit (FUN_004aff60): effect 1000 at angle+π, particles `FUN_0068b8a0(1000,…,n)` (n = 6/5/3 for
100/200/300), sparks `FUN_007bd5e0(1000, colour, 6|5|5, 6|5|5, angle, pos, owner)`, SE 0x13 lock 3.
Draw: 100 draws nothing (visual = effect 200 trail). 200/300/0x136: two screen-space quads from the
shared bullet atlas (`DAT_0087fde4` vtable +0x68), cell `c = DAT_008859f4[team]`: glow src
`(100·(c%10&1)+50+250·(c/10), 100·((c%10)/2)) – (+0x63 wide …)`, core src `(250·(c/10), 50·(c%10))` 50×50;
200: glow half-size (50, 20)/zoom alpha 96·a blend 1, core (30,10)/zoom alpha 255·a blend 2, rotated by −angle;
300/0x136: glow half 24·scale, core 15·scale, unrotated. (exact rects: FUN_004af560.)

### 5.4 Missiles (FUN_00670310 / FUN_00671310 / draw FUN_006704b0 / onHit FUN_006710b0 / onKilled FUN_006711e0)
Init FUN_00685bb0: hitType 2 (not dodged by dash), hp **60**, stun 1.0. Owner id = the Bit.
| Id | speed | shapes (body / vs projectiles) | dmg | knock | behaviour |
|---|---|---|---|---|---|
| 200 Ether Leaf | 15 (DAT_00853b88) | {0,0,10} / {0,0,10} | **120** (ver ≤ 0x58a: 150) | 10 | age < 100: heading += `+0x74` (±0.025, set by the Bit), draw angle = heading; speed −0.2/frame while > 6 (DAT_00813f44); smoke effect 0x802 every even age |
| 300 Ether Bug | 14 (DAT_00813a54) | {0,0,10} / {0,0,10} | 150 | 10 | heading fixed; speed −0.5/frame while > 3; draw angle += 0.08/frame (DAT_00813f00) wrapped; effect `0x8816cc(100)` on even global frames (`DAT_008858f8`) |
Both: `pos += (cos,sin)(heading)·speed`; removed (effect 0x26c) when outside arena + **200** (DAT_008147cc;
ver ≤ 0x5a0: +20). onHit (hit player): effects 0x262, 0x514; SE 0x14 (`bom06.wav`); destroyed.
onKilled (shot down): effects 0x26c, 0x515; SE 0x1d (`gun12.wav`). No absorb handler.
Draw: sprite 0x1a5 (leaf) / 0x1a6 (bug), at pos, rotZ(draw angle): frame 0 normal blend; frames 1 and 2
additive (blend 2) with alpha `0.75+0.25·sin((age%50)/50·2π)` and `0.65+0.35·sin(…)` (DAT_00813f24,
DAT_00813f3c, DAT_00813f2c, DAT_0081788c).

### 5.5 Hitboxes (FUN_005e6730 / onHit FUN_005e6a90) — one-frame, objects.md §2.4
All hitType **3** (hit dashing targets), stun 1.0, knockback 10 (DAT_00813f4c).
| Id | Spawned by | dmg | shape {hx,hy,r} | on-hit |
|---|---|---|---|---|
| 100 | Bit mode 500 (Shell Shot) ×3 per Bit | 30 | {0, 0, 80} (DAT_00814164) | 6 streaks 0x456 (angles rand base + j/3·π + rand_f(−0.25,0.25)), effect 0xaf0, sparks FUN_007bd5e0, SE 0x18 (`hit.wav`) lock 4 |
| 200 | laser 100 ×3 | 50 | {800, 40, 0} (DAT_008149c8, DAT_008140e0) | 6 streaks 0x456, effect 0x516, SE 0x13 lock 4 |
| 0xd2 | laser 0x96 ×4 | 80 | {800, 60, 0} (DAT_00813a58) | as 200 |
| 300 | laser 200 ×7 | 120 | {800, 90, 0} (DAT_00814170) | 4 streaks (rand_f(−0.75,0.75)), 0x516, SE 0x13 lock 4 |
| 0x136 | laser 200 (non-damage frames) ×14 | **0** | {800, 90, 0} | no case in onHit (no effect). Still a damage-dealing box (`+0x00 = 1`) with 0 damage: still adds stun and causes hitstun (knock 10) — FUN_00780ab0 computes the stun result before its `dmg < 1` early-out |
Rect hitboxes are centred `D` px along the laser direction from the laser origin (D = 800 for 200/0xd2,
720 for 300/0x136) and rotated by the laser angle → they cover 0..1600 (resp. −80..1520) px along the beam.

### 5.6 Lasers (FUN_00620e90 / FUN_00622af0 / draw FUN_00620ff0; onHit FUN_00622ad0 is empty)
Init FUN_006307d0: blockMode `+0x08 = 1`, hitType 3, damage `+0x40 = 0`, knock 10, `+0x48 = 0`, stun 1.
**Segment list is cleared every update and never filled** (`FUN_004a9f10`) — Nanako's lasers do no
segment collision at all; all damage is via the periodic hitboxes. Origin and angle fixed at spawn.
Laser age is pre-incremented (first update sees 1).
| Id | From | Hitboxes (at origin + dir·D, angle) | Muzzle | Removed |
|---|---|---|---|---|
| 100 | Bit mode 300 | ages 1, 6, 11 → hitbox **200** (3 × 50) | effect 0x546 at origin+dir·30 on age%5==2, age<16 | age > 29 |
| 0x96 | Bit mode 0x136 | ages 11, 16, 21, 26 → hitbox **0xd2** (4 × 80) | same | age > 49 |
| 200 | Bit mode 0x262 | even ages 10..50: age%6==0 (12,18,…,48 → 7×) hitbox **300** (120), else hitbox **0x136** (0 dmg) at dir·720 (DAT_008178c4) | 0x546 on age%5==2, age<41 | age > 79 |
Each hitbox frame also calls FUN_00612830(player[team−1],0,0,1) (training display only).
Draw (sprite batch `FUN_0047f7b0(0)` = `nk_lsr` atlas, screen space, rotated by −angle, centre =
origin + dir·1000 (DAT_00814408), half-length 1000/zoom):
- 100: half-height h = (age/2 even ? 80 : 100)/zoom; age<5: ×age/5; age>15: ×(30−age)/15; alpha A = age<16 ? 255 : (30−age)/15·255. Quad 1 src (0,0x154)-(0x1ff,499) alpha A/5 blend 1; quad 2 src (0,0)-(0x1ff,0x9f) h×0.75 alpha A/3 blend 2; quad 3 src row `((age/2)%3)·60+160` (60 tall) half-height 20/zoom (same ramps) colour 0xa0ffffff blend 2; then muzzle glow from the bullet atlas at origin+dir·30, half 50 (ramped), alpha A/2 and A, blend 2.
- 0x96: h = (age/2 even ? 90 : 120)/zoom; age<10: ×0.1; age<14: ×(age−9)/5; age>30: ×(50−age)/30; A = age<31 ? 255 : (50−age)/30·255; inner quad: 30/zoom with the same <10/<14 ramps and age>20: ×(50−age)/30; muzzle half 50, age<5 ramp, age>20 ×(50−age)/30.
- 200: h = (age/2 even ? 180 : 240)/zoom; age<5: ×age/5; age>50: ×(80−age)/30; A = age<51 ? 255 : (80−age)/30·255; inner 30/zoom same ramps; muzzle half 100 (same ramps).
(`zoom` = `_DAT_008859e0`, camera, objects.md §6.)

### 5.7 Whip trail (FUN_007c6a80 / update FUN_007c6f00 / draw FUN_007c6b70) — cosmetic
One per Bit (whip id 5, `+0x08 = p+0x18`, `+0x10 = Bit id`). Update: find the Bit (`FUN_0055f060`);
if gone → removed. Push the pair `bit.pos ± 5·(cos,sin)(angle+π/2)` (DAT_00813f40) into a 20-entry
history (fill first free slot from the end, else shift). Draw: 19 textured quads between consecutive
history pairs, texture `nk_whip0` (slot `0x87fe5c + p18*0x3c`), vertex colour 0x80ffffff, u = i·12.8
(_DAT_00828324), v 0..256.

### 5.8 Nanako effects (type 13; spawn FUN_004fef30, update FUN_005004c0, draw FUN_004ff3e0) — cosmetic, no RNG
| Id | Spawned by | Update |
|---|---|---|
| 100 | missile 300 trail | alpha −0.075/frame |
| 200 | beam 100 body (every frame) | alpha −0.1/frame after age 2 |
| 0xd2 | beam 200 body | alpha −0.2/frame after age 2 |
| 300/301/302 | Bit afterimage (Sting, Shell Shot) | alpha −0.1/frame |
| 400 | Flower Crown ring (4 per Bit) | orbits its Bit: radius += 4 while < 32 (DAT_008140dc), angle +0.1, alpha +0.1 to 1, fade −0.05 after age 30 |
| 0x19a | Spring Storm ring | same with radius ×2, fade after age 50 |
| 500 | Shell Shot trail | alpha −0.15, scale +0.25 per frame |
Generic effects (`0x881698`, type 0): 0x548/0x546 muzzle, 0x516 burst, 0x456 streaks, 0xaf0, 1000,
0x26c, 0x262, 0x514, 0x515, 0x802 smoke, 0x834, 500, 0xa32–0xa35 sparks, 0xfa Accel flash.
RNG consumers: Shell-Shot sparks (T==11), Bit death, hitbox on-hit streaks, beam on-hit
particles/sparks (FUN_0068b8a0 / FUN_007bd5e0 — whether they draw RNG: see suguri.md §5.7/objects.md).

---

## 6. Sound ids (SE table `0x9b99a0 + 4*id`, all.c:464010)
| id | file | where |
|---|---|---|
| 6 | up01.wav | hyper start |
| 7 | high_a.wav | Spring Storm start |
| 0xd | _sen_mi_b_raifuru.wav | beam 100 (Needle, Sting) |
| 0xf | laser02.wav | Fruit shots, Spring Storm beams |
| 0x10 | laser03.wav | Tone, Ether Leaf |
| 0x12 | gun11_r.wav | lasers |
| 0x13 | don16.wav | beam hits, laser hitbox hits |
| 0x14 | bom06.wav | missile hits player |
| 0x18 | hit.wav | Shell Shot hit |
| 0x1d | gun12.wav | missile shot down, Bit death |
| 0x1f | swing.wav | Shell Shot dash start |
| 0x2a | A5_01029.wav | Shell Shot accepted |
| 0x2f | power35.wav | Bit deploy |
| 0x31 | whip.wav | Ether Bug shots |

---

## 7. Other per-character table entries (index 13)
| Table | Entry | Role |
|---|---|---|
| 0x881310 (+0x34) | FUN_00707380 | input dispatch, standard (§2) |
| 0x881360 (+0x34) | FUN_007077f0 | input dispatch, alternate (§2) |
| 0x8813b0 (+0x34) | FUN_007028c0 | sprite loader (§4.1) |
| 0x8814d0 / d4 / d8 | FUN_007083a0 (level 0), FUN_00707c60 (levels 1, 2) | CPU AI (not reversed) |
| 0x88158c… | §5 families | |
| T2 0x880fd4..0x88102c / T1 0x880664..0x8806bc | §3 / §4.4 | registry.c:921-956 |

## 8. Stats record (index 13, base **0x8844ac**), FUN_0049ae40 (registry.c:1962-2004)
| Off | Value | Meaning |
|---|---|---|
| +0x00 | 4.0 (DAT_00853b68) | walk speed per axis |
| +0x04 | 14.0 (DAT_00813a54) | dash speed |
| +0x08 | 12.0 (DAT_00813f54) | UNVERIFIED (same as Suguri) |
| +0x0c..+0x2c | 5, 0xb, 0, 1, 7, 0, 5, 0xb, 0 | UNVERIFIED |
| +0x3c/+0x40/+0x44 | 0x194, 0x195, 0x196 | sprite ids (ring / — / system) |
| +0x48/+0x60/+0x78 | text 0x15/0x16/0x17 | colour-variant folder names |
| +0xa8 / +0x1a8 | `mdl\win_font_NK.lfd/.dat` | |
| +0x2a8 | `nk_res_bgp` | |
| +0x2c0/+0x2c4/+0x2c8 | 100, 0, 0 | UNVERIFIED |
| +0x2cc/+0x2d0/+0x2d4 | 0xf0, 0x78, 0xfa | colour |
| +0x2d8 | 0 | |

## 9. UNVERIFIED / open items
- RESOLVED (iru.md §5.3): a 0-damage hitbox still causes hitstun/knockback — `FUN_00780ab0` adds stun and computes its "stun full" return value before the `dmg < 1` early-out (which only skips the attacker's combo counters).
- Consequences of projectile ownerId = Bit id (§0.3): damage attribution, hit-confirm, combo counters.
- RESOLVED: the Bit-draw pulse in FUN_0052bbf0 is dead code — stored to [ebp-0x100]/[ebp-0x104] (0x52bc76, 0x52bccc) and never loaded.
- RESOLVED: the loader is called with `p+0x18` as its argument (all.c ~284446: `(*(&DAT_008813b0)[p+0x10])(p+0x18)`), so loader arg == `p+0x18` == colour variant; whip texture registration and lookup agree.
- Beam 0x136 (Fruit fade) keeps no movement — confirmed from FUN_004b0220 (no move code in case 0x136); visual only.
- RESOLVED: effects 300..302 draw sprite 0x1a4 (`nk_bit`) frame id−300, half-size 27·scale (DAT_008178d4), src (0,0)-(127,127), alpha 128·a, blend 2, rotated by +angle (FUN_004ff3e0 case 0x12c..0x12e).
- Whip ribbon (§5.7): vertex layout = Iru's (FUN_007c56f0 vs FUN_007c6b70 differ only in id and texture slot): A_k (tex 0, 12.8k), A_k+1, B_k+1 (256, …), B_k — texture x across the ribbon, y along it. The port draws one rotated quad per segment (draw.h has no textured strip): exact while consecutive pairs are parallel; zero-length segments draw nothing, as in the original. Remaining approximation, by design.
- Blits (beams/lasers/effects) use `CTexture::Draw` (FUN_0045c470): (destRect screen px, z, srcRect texels, angle bam clockwise on screen — callers pass −angle, ARGB, blend), see iru.md §5.4.
- Verified by tests/test_nanako.c: 7 Bits at 0x197 t=50 with delays −10|k−3|, no hurtbox during the intro, idle orbit ≈100 / ≈240 px with S held, S press speed 16, both dispatch schemes, command frames, per-Bit ammo (rows 0x198–0x19c), Shell Shot → 0x19e, Spring Storm double +π beam (no beam along the heading), laser hitbox totals 150 / 320 / 840, KO → Bits self-destruct by 100−10k.
- Stats +0x08, int block +0x0c..+0x2c, +0x2c0..+0x2c8.
- Old-version (ver ≤ 0x5a0) RNG branches listed above are irrelevant for live play.
