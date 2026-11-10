# AoS2 core player systems (character-independent)

Source: `aos2.exe` (image base 0x400000). All addresses are VAs. `rd X` = `xref.exe rd X 1`.
Game logic version used for everything below: **0x5a2** (see §0). Values marked **UNVERIFIED**
say what would settle them.

---------------------------------------------------------------------------------------------------
## 0. Critical corrections to HANDBOOK.md

1. **Character id at +0x10 is NOT 0..15.** The value stored in the player struct (and used to
   index `0x881f80 + id*0x2dc`, the `0x8813b0` loader table, etc.) is **handbook id + 3**:
   Sora=3, Alte=4, Tsih=5, Mira=6, Sham=7, Nath=8, Star Breaker=9, Suguri=10, Saki=11, Iru=12,
   Nanako=13, Kae=14, Kyoko=15, Hime=16, Sumika=17, Sumika 2nd=18. Proof: the per-char tables are
   indexed as `(&DAT_00881310)[id]` (input A), `(&DAT_00881360)[id]` (input B), `(&DAT_008813b0)[id]`
   (loader) in FUN_006f1720 / FUN_005f4650, and `0x881310+4*3 = 0x88131c` = Sora; common state 0
   (FUN_006af390) switches `case 3: T2[8]` (Sora neutral) ... `case 0x12: T2[0x235]`; Nath (8) starts
   in state 0xd2, Sumika 2nd (0x12) in 0x241 (FUN_0069cc40). Records 0..2 of the stats array are unused.
2. **`0x88131c+4c` / `0x88136c+4c` are both "input fns"** for control scheme A and B (+0x0c), not
   input fn + "role TBD" (see §2.4). `0x881430+0x10c` = CPU input generators per difficulty.
3. **Game version global `DAT_0089426c`** (BSS) = field [1] of the replay/match header struct at
   `DAT_00894268`. Writers (all via that struct pointer, so no direct `mov [0x89426c]` exists):
   live match — Game Init FUN_005f4650 calls `FUN_00794210(&DAT_00894268, &DAT_0087faa8)` which
   does `header[0]=0x44c; header[1]=0x5a2`; replay playback — FUN_00793e30 (line ~313448) /
   FUN_00793870 (line ~448926) load the header from the replay file, so old replays carry their
   recording version. Config file `game.cfg` has its own version (`DAT_00881e68 = 0x5a2`). Many rules are gated on it (`0x58a`, `0x58b`, `0x59b`, `0x5a0`, `0x5a1`).
   For the port **always use 0x5a2**; the version-only branches are listed where they matter.
4. **Version patch FUN_004a7270(ver)** (called from Game Init FUN_005f4650 @0x5f4650 with
   DAT_0089426c) overwrites some registry values at match start; e.g. Star Breaker dash speed
   (`DAT_00883940` = rec 9 +4) = 13.0 (`DAT_00813f58`) for ver ≥ 0x58b (registry value 12.0).
   Applied in the tables of §7 and §6.9.

---------------------------------------------------------------------------------------------------
## 1. Per-frame loop of a match

### 1.1 Call tree (one gameplay frame)

`FUN_006124b0` @0x6124b0 (scene update for the battle scene), relevant parts in order:

1. (netplay/replay housekeeping)
2. **`FUN_006116c0` @0x6116c0 — input sampling** for both players (§2).
3. **`FUN_006129b0(0)` @0x6129b0 — the gameplay tick** (below). Skipped if `DAT_009b11c0`.
   Also called with param 1 from FUN_005fadf0/FUN_005fafc0 (rollback re-simulation; game RNG
   state is part of the snapshot, FUN_0045a030/FUN_0045a070, 5000 bytes).
4. FUN_005f9a30 (net stats), FUN_0068ce30 (pause), then rendering FUN_005f91d0 → FUN_006b1ea0
   @0x6b1ea0, which calls the draw table `T1[p->state](p)` (`0x880030+4*state`) for each player.

### 1.2 `FUN_006129b0` — gameplay tick, exact order

| # | What | Address |
|---|---|---|
| 1 | Save P0/P1 hyper (+0xe4) for the level-up sound check at the end | 0x6129b0 |
| 2 | `FUN_0045a0b0(1)` — select **game RNG** stream (§8) | |
| 3 | **Player update `FUN_006f1720`** — for i = 0, 1 in order: input fn → state update → physics → gauges (§1.3). P0 is fully processed before P1. | 0x6f1720 |
| 4 | `FUN_005231d0` — "Enemy"/unit objects update (list FUN_004ef290) | |
| 5 | **Truncate positions**: for both players `x=(float)(int)x; y=(float)(int)y; z=(float)(int)z` (+0x6c,+0x70,+0x74; C cast = truncation toward 0) | in 0x6129b0 |
| 6 | Object updates: FUN_004aa460 Beam, FUN_0065e2f0 Missile, FUN_0061cbf0 Laser, FUN_004fde70 (list FUN_004c6860), FUN_007bb2b0 Smoke, FUN_0068c500 Particle, FUN_007bd030 Spark, FUN_004d7b10/FUN_004d78b0 Blur, FUN_007c4fb0 Whip | |
| 7 | If `DAT_00885a34==0` (not resimulating): cosmetic RNG, FUN_004bfd30 Background, game RNG | |
| 8 | FUN_004e8150 CutIn, FUN_004e9030 Dark, FUN_007be6f0 Speedline, FUN_0065dee0 Message, FUN_005f8b90 (popups), FUN_00617a60 (round-start banner), FUN_00608a00 (round-end banner / next round) | |
| 9 | **Collisions, only if round not over (`DAT_00885964 != 2`)**, in this order: 0060bbd0, 0060b900, **0060b610 (beam→player)**, 0060bf30, 0060fc30, 0060fa30, **0060f5a0 (list 4b2eb0→player)**, 00611060, 00610de0, **00610b80 (missile→player)**, 006113a0, 00610360, 006107e0, 006107f0, 006105d0, **006100b0 (laser→player)**, 0060d690 (units push players) | |
| 10 | FUN_00523370 (unit cleanup) | |
| 11 | `DAT_008858f8++` (match frame counter; reset to 0 at round init) | |
| 12 | (ver > 0x5a0) object lifetime counters: missile +0xb0, beam +0x90, unit +0xe4 decremented; at 0 set the object's dead flag | |
| 13 | Training input-history display; combo/frame-advantage UI counters (no gameplay effect) | |
| 14 | **Round timer** (§6.10) | |
| 15 | `FUN_0045a0b0(0)` — back to cosmetic RNG; (param_1) FUN_005e0150 | |
| 16 | Hyper level-up sounds: if a player's hyper crossed 1.0 / 2.0 / 3.0 this frame play SE 0x3a / 0x3b / 0x3c | |

### 1.3 `FUN_006f1720` — player update, for each player p (index order 0,1)

```
opp = FUN_00780480(p->side)              // side 1 -> player[1], side 2 -> player[0]
p->shape = {0, 0, 16.0 (DAT_008140d8), 0}   // +0x5c..+0x68 hurt shape, reset every frame
p->hittable(+0xa2) = 1
p->friction(+0x9c) = 0.5 (DAT_00853b1c)
p->clampArena(+0xa0) = 1
if (p->scheme(+0x0c) == 0)  ok = inputA[p->id](p)     // (&DAT_00881310)[id], e.g. Suguri FUN_0075a030
else if (p->scheme == 1)    ok = inputB[p->id](p)     // (&DAT_00881360)[id], e.g. Suguri FUN_0075a4b0
if (that call returned false) { erase p from player vector; continue }   // never expected; see UNVERIFIED
--- physics ---
spd = p->speed(+0x98); if (p->status[4](+0xa7)) spd *= 0.25 (DAT_00813f24)
p->x += cos(p->moveAngle(+0x94)) * spd;  p->y += sin(p->moveAngle) * spd
   (Alte-only exception, see "notes for other specs")
if (p->speed <= 0.0) p->speed = 0; else p->speed -= p->friction;   // may go negative for 1 frame
--- status effects ---  for k=0..8: if (p->status[k](+0xa3+k)==1 && fx[k]) fx[k](p)   (§6.8)
--- heat decay ---  if (state!=6 && state!=1) heat -= (state in {0,1,2,3}) ? 0.01 (DAT_00813ec8) : 0.005 (DAT_00813eac)
   if (heat < 0) heat = 0;  if (heat > 3.0) heat = 3.0 (DAT_00813a50)
   (training locks, §6.11)
--- passive hyper ---
   if (difficulty DAT_0087fab0==2 && p->inputSrc(+0x08)==10(CPU) && frame%5==0) hyper += 0.001 (DAT_00814bd8)
   else if (infinite-hyper flags) hyper += 0.02 (DAT_00813ed8)
   if (hyper > 3.0) hyper = 3.0
--- weapon gauges ---  if (gA<0) gA=0; if (gB<0) gB=0
   if (state in {0,1,2}) { gA += 0.01; gB += 0.01; clamp each to <= 1.0 }   // +0xe8, +0xec
--- combo ---  if (state != 1) { if (comboTimer>0) comboTimer--; if (comboTimer<1) {comboDmg=0; comboHits=0; stun=0} }
--- HP bars ---  if (hp < redHp) redHp -= 10;  if (redHp < hp) redHp = hp
   third = maxHp/3 (int);  cap = (hp/third + 1)*third;  if (recov > cap) recov = cap
   (ver < 0x5a1 only: if hp <= third, hyperMul(+0xf0)=1.5 — NOT active in 0x5a2)
   (training infinite HP: hp += 50 (0x32) up to max, raise recov/redHp)
   if (hp < 1) recov = 0
FUN_00780650(p, opp)                       // attacker-side combo display timers (UI only)
if (state < 7) p->hitFlag(+0x158) = 0
if (p->clampArena && x*x+y*y > 291600.0 (double DAT_008257a0 = 540^2)) {
    a = atan2(y, x); x = cos(a)*540 (DAT_0081412c); y = sin(a)*540 }
p->+0x78/+0x7c/+0x80 = x, y, z
if (opp) { if (p->x < opp->x) facingRight(+0x8c)=1; if (p->x > opp->x) facingRight=0 }   // equal: unchanged
--- barrier (shield bubble), §6.6 ---  if (p->barrierTime(+0xf8) > 0) {...; barrierTime-- }
if (DAT_0088596c == 1) p->hittable = 0   // match finished (replay end)
p->timer(+0x154)++                        // AFTER the state handler ran
(stats only: track max heat in DAT_00894068 for P0)
```

Consequences for frame counting:
- `p->timer` seen by a handler is 0 on the frame the state is entered **if the handler is invoked
  immediately** (the standard transition pattern calls `T2[new](p)` right away), else it first runs
  next frame with timer already 1 (e.g. `FUN_006b17d0` dash-cancel from inside a state handler;
  states set by collision code, e.g. hitstun, first run next frame with timer 0 since the player
  update for this frame is already done).
- Some transitions set only `state` (not timer), e.g. dash→neutral in state 2; timer continues.

---------------------------------------------------------------------------------------------------
## 2. Input

### 2.1 Raw sampling → 16-bit mask (`FUN_006116c0` @0x6116c0)
Per player a `uint16` mask is produced and stored in the replay/net input buffer, then decoded:
- `inputSrc (+0x08) < 9`: local device, `FUN_0061b2e0(p->device(+0x16c), &mask, 0, scheme)`.
- `== 9`: no input (mask 0).
- `== 10`: CPU, `(&DAT_00881400)[difficulty + id*4](p, &mask)` = table `0x881430 + id*0x10 + diff*4`
  (Suguri: `0x8814a0` diff0 FUN_0075b420, `0x8814a4`/`0x8814a8` diff1/2 FUN_0075a930). Mask forced 0
  while the round-end banner is up (`DAT_0087fb58`) and in training (`DAT_0087faa8==3`).
- `== 11`: netplay remote.
- Replay playback (`DAT_0087faac==4`) reads masks from the replay.

Mask bits (FUN_0061b2e0 keyboard-config slot → bit; default VK from FUN_004dc6f0):

| bit | slot (cfg `DAT_00881ec4+4k`) | default key | player field | meaning |
|---|---|---|---|---|
| 0x0001 | 0 | VK_UP | +0x24 | Up (world +y) |
| 0x0002 | 1 | VK_DOWN | +0x28 | Down (−y) |
| 0x0004 | 2 | VK_LEFT | +0x20 | Left (−x) |
| 0x0008 | 3 | VK_RIGHT | +0x1c | Right (+x) |
| 0x0010 | 6 | Z | +0x38 | **A** (Attack / regular weapon) |
| 0x0020 | 7 | X | +0x3c | **B** (Sub weapon) |
| 0x0040 | 8 | A | +0x40 | **S** (Special) |
| 0x0080 | 9 | V | +0x34 | **D** (Dash) |
| 0x0100 | 10 | C | +0x44 | **H** (Hyper) |
| 0x0200 | 11 | S | +0x48 | **Shield** ("GUARD" in exoption.txt) |
| 0x0400 | 12 | T | +0x4c | Pause |
| 0x0800 | 4 | Z | +0x2c | menu OK |
| 0x1000 | 5 | X | +0x30 | menu cancel |
| 0x2000 | 13 | — | — | (cfg slot 13, set into mask only) |
| 0x4000 / 0x8000 | — | — | +0x54 / +0x58 | training/replay only (decoded only if `DAT_0087faa8==3` or `DAT_0087faac==4`) |

Button letters per `manual.txt` (H=Hyper, D=Dash, S=Special, B=SubWeapon, A=Attack) match the
input fns (e.g. Suguri "HyperRifle A+H" = `+0x38 held && +0x44 == 3` → state 0x136).

### 2.2 Per-button state (`FUN_0061bc70` @0x61bc70)
For each field `v`, given bit `b` of this frame's mask:

| b | previous v | new v | meaning |
|---|---|---|---|
| 1 | 0 or 1 | **3** | pressed this frame |
| 1 | 2 or 3 | **2** | held |
| 0 | 2 or 3 | **1** | released this frame |
| 0 | 0 or 1 | **0** | up |

Engine idioms: `v == 3` = just pressed; `v > 1` (`1 < v`) = currently down; `v < 2` = currently up.

### 2.3 Round-start input lock (end of FUN_006116c0)
If `frame (DAT_008858f8) < 200` or match finished (`DAT_0088596c==1`): A(+0x38), B(+0x3c),
H(+0x44), Shield(+0x48) forced to 0. If `frame < 100` (or finished): also Right, Left, Up, Down
(+0x1c..+0x28) and Dash (+0x34) forced to 0. S (+0x40) and Pause are **not** cleared.

### 2.4 Control schemes (+0x0c, from `DAT_0087fabc[player]`)
- Scheme 0 ("type A", table `0x881310+4*id`, Suguri FUN_0075a030): Hyper/Accel = **hold X, press H**
  (`X > 1 && H == 3`).
- Scheme 1 ("type B", table `0x881360+4*id`, Suguri FUN_0075a4b0): **hold H, press X**
  (`H > 1 && X == 3`); Accel Hyper = H held + Shield pressed. In scheme 1 you cannot Shield while
  H is held (FUN_007809e0, ver ≥ 0x5a1).

### 2.5 Directional angle (`FUN_0061b230` @0x61b230, argument = &p->+0x1c)
Returns: Up+Right π/4, Up+Left 3π/4, Up π/2, Down+Right −π/4, Down+Left −3π/4, Down −π/2,
Left π, Right 0, none **4π = 12.566371 (`DAT_0082025c`) = "no direction" sentinel**; callers test
`angle < 2π (DAT_00853b78)`. Up takes priority over Down, then Left/Right combine. No analog.

### 2.6 Hold / charge detection
There is no generic hold counter. Charge moves are detected inside the move's state handler by
testing the button field against the state timer, e.g. Suguri state 0x133 (FUN at line 423179 of
dec/all.c): "if timer in 15..24 and A < 2 (released) → short version; if timer in 30..39 and A < 2 →
charged version". See suguri.md for the exact windows of each move.

### 2.7 Standard input-fn structure (all 16 chars, e.g. FUN_0075a030, FUN_0073b470)
```
if (FUN_00780950(p)) {                           // "may use hyper" (§6.5)
   [scheme A] S>1 && B>1 && H==3 && spend(3.0) → AccelHyper state     (Suguri 0x143, Sora 0x21)
              S>1 && H==3 && spend(1.0)         → hyper (S)            (Suguri 0x141)
              B>1 && H==3 && spend(1.0)         → hyper (B)            (Suguri 0x13f)
              (char-specific extra, e.g. Sora D>1 && H==3 → 0x17)
              H==3 && spend(1.0)                → hyper (A)            (Suguri 0x136)
   → each: state=new, timer=0, flag158=0, T2[new](p), return true
}
if (state == 0 || state == 2) {
   if (D == 3)                        FUN_006b17d0(p,0)   // dash (§4.3)
   else if (Shield==3 && spend(0.5) && FUN_007809e0(p)) FUN_006b1930(p,0)   // shield (§4.4)
   else  character attack table (A/B/S combos) → state=..., T2, return true
}
return T2[p->state](p) != 0;          // state update (also runs the dash/shield state just entered)
```
`spend(x)` = FUN_00780910 @0x780910: `if (hyper >= x) { hyper -= x; return 1 } return 0`.
**Faithful bug**: in the neutral shield test the 0.5 hyper is spent *before* FUN_007809e0 is
checked, so if FUN_007809e0 fails (opponent Accel barrier, round over, scheme-B with H held) the
hyper is lost and no shield happens. Attacks are only startable from states 0 and 2; hypers from
any state allowed by FUN_00780950 (i.e. they cancel attacks).

---------------------------------------------------------------------------------------------------
## 3. Player struct (size 0x178; `FUN_004e8a10(i)` = `base + i*0x178`, count = FUN_0054e140)

Initialised in Player Init FUN_0069cc40 @0x69cc40 (memset 0 then fields below) and Game Init
FUN_005f4650 @0x5f4650.

| Off | Type | Name | Meaning / writers |
|---|---|---|---|
| 0x00 | int | — | 0 |
| 0x04 | int | objId | unique id from FUN_00780420 (global counter `DAT_0088597c`); owner id passed to spawned objects |
| 0x08 | int | inputSrc | `DAT_0087fab4[i]`: <9 local device, 9 none, 10 CPU, 11 net (§2.1) |
| 0x0c | int | scheme | control scheme 0/1 (`DAT_0087fabc[i]`), §2.4 |
| 0x10 | int | id | character id **3..18** (§0) |
| 0x14 | int | side | P1=1, P2=2 (set in FUN_005f4650). Opponent = FUN_00780480(side) (1→player[1], 2→player[0]); own = FUN_00780450(side). Objects carry the owner's side to skip self-hits |
| 0x18 | int | color | palette 0..2 (`DAT_0087facc[i]`, 3 → 0; P2 bumped to 1 on mirror) |
| 0x1c,0x20,0x24,0x28 | int | Right, Left, Up, Down | button states §2.2 |
| 0x2c,0x30 | int | menuOk, menuCancel | |
| 0x34 | int | D | Dash |
| 0x38 | int | A | |
| 0x3c | int | B | |
| 0x40 | int | S | Special |
| 0x44 | int | H | Hyper |
| 0x48 | int | Shield | |
| 0x4c | int | Pause | |
| 0x50 | int | — | (slot 13 not decoded) |
| 0x54,0x58 | int | train1, train2 | mask bits 0x4000/0x8000 (training/replay) |
| 0x5c..0x68 | float[4] | hurtShape | {halfW, halfH, radius, ?} for FUN_005f1c90; reset each frame to {0,0,16,0}; while barrier active {0,0,barrierR*0.8,0} (§6.6) |
| 0x6c,0x70,0x74 | float | x, y, z | position; truncated to integer each tick (§1.2 #5). Start: P1 (−150,0), P2 (150,0) (`DAT_008149ec`, `DAT_0081414c`) |
| 0x78,0x7c,0x80 | float | — | copy of x,y,z after arena clamp (FUN_006f1720) |
| 0x84 | float | faceAngle | body/aim angle; neutral sets 0 (facing right) or π |
| 0x88 | float | dashAngle | desired dash direction (turned toward input at 0.08 rad/f) |
| 0x8c | byte | facingRight | 1 if x < opp.x (updated each frame, §1.3) |
| 0x90 | int | — | 0 at init (UNVERIFIED use) |
| 0x94 | float | moveAngle | velocity direction |
| 0x98 | float | speed | velocity magnitude (px/frame) |
| 0x9c | float | friction | subtracted from speed each frame; reset to 0.5 every frame before the state update |
| 0xa0 | byte | clampArena | 1 every frame; arena circle clamp if 1 |
| 0xa1 | byte | — | 1 at init (UNVERIFIED) |
| 0xa2 | byte | hittable | 1 every frame; 0 in KO state / after match end |
| 0xa3..0xab | byte[9] | statusOn[k] | status effect flags (§6.8). Notables: 0xa6 = no-dash, 0xa7 = slow, 0xa9 = hitstun-immune |
| 0xac..0xcc | int[9] | statusTime[k] | status durations (frames) |
| 0xd0 | int | hp | |
| 0xd4 | int | maxHp | handicap table, 9000 by default (§6.1) |
| 0xd8 | int | redHp | damage-trail bar (falls 10/frame toward hp) |
| 0xdc | int | recovHp | recoverable-HP ceiling (heals cannot exceed it) |
| 0xe0 | float | heat | 0..3.0 (=0..300%) |
| 0xe4 | float | hyper | 0..3.0 (3 stocks) |
| 0xe8 | float | gaugeA | weapon gauge 0..1, init 1.0 (name UNVERIFIED, §6.9) |
| 0xec | float | gaugeB | weapon gauge 0..1, init 1.0 (name UNVERIFIED, §6.9) |
| 0xf0 | float | hyperMul | multiplier on hyper gains; 1.0 (only changes for ver < 0x5a1) |
| 0xf4 | int | barrierType | 1 shield, 2 burst-shield, 3 hyper, 4 accel hyper, 5 (type 2 caller unknown) |
| 0xf8 | int | barrierTime | frames left; >0 ⇒ no damage (§6.6) |
| 0xfc | float | barrierR | full barrier radius |
| 0x100 | float | barrierScale | 0..1 grow/shrink |
| 0x104 | float | barrierRot | random visual rotation (consumes game RNG every frame) |
| 0x108 | int | barrierBreakT | 50 (0x32); a barrier with barrierTime > this can be cut to 10 by a shield |
| 0x10c,0x110 | — | — | UNVERIFIED (not seen in player code) |
| 0x114 | int | — | −1 at init (char-specific handle, UNVERIFIED) |
| 0x118..0x13c | — | combo UI | 0x11c comboDmg shown, 0x124 best dmg, 0x128 hits shown, 0x12c best hits, 0x130 display timer (120), 0x134/0x13c fade (FUN_00780650, FUN_00780ab0) — UI only |
| 0x140 | int | comboTimer | set 30 on every damage taken; counts down when not in hitstun |
| 0x144 | float | stun | hitstun accumulator; hit causes hitstun iff stun ≥ 1.0 (§6.3) |
| 0x148 | int | comboDamage | taken |
| 0x14c | int | comboHits | taken |
| 0x150 | int | state | |
| 0x154 | int | timer | frames in state (incremented after update) |
| 0x158 | byte | hitFlag | set to 1 by owned objects' hit callbacks (e.g. FUN_005e0740) — "my attack connected", used for cancels; cleared on state change and every frame while state < 7 |
| 0x15c..0x168 | | charScratch | character-specific (Tsih: byte 0x164 = stealth; FUN_007804b0) |
| 0x16c | int | device | input device handle (`DAT_0087fae8[i]`) |
| 0x170 | int | roundsWon | preserved across rounds |
| 0x174 | int | — | 0 |

Initial state (+0x150): 0, except Nath 0xd2, Nanako 0x197, Sumika 0x21d, Sumika 2nd 0x241
(FUN_0069cc40). Initial +0x84: P1 0, P2 π.

---------------------------------------------------------------------------------------------------
## 4. Common states (T2 `0x8809a0+4*s`, T1 `0x880030+4*s`, registry FUN_0049ae40)

| s | Name | T2 update | T1 draw |
|---|---|---|---|
| 0 | Neutral | FUN_006af390 | FUN_006b07a0 |
| 1 | Hitstun | FUN_006af880 | FUN_006b0c40 |
| 2 | Dash | FUN_006af5e0 | FUN_006b09f0 |
| 3 | Shield | FUN_006afac0 | FUN_006b0e90 |
| 4 | Burst shield (shield out of hitstun) | FUN_006afd90 | FUN_006b10e0 |
| 5 | Shield-pushed (blown away by burst) | FUN_006b00a0 | FUN_006b1330 |
| 6 | KO | FUN_006b0110 | FUN_006b1580 |
| 7 | (unused — `0x8809bc` never assigned) | — | — |

There are no intro / win / timeout states: rounds start in state 0 with input locks (§2.3);
winners stay in whatever state they are in.

### 4.0 Neutral (FUN_006af390)
Dispatches to the character's own neutral state: `T2[firstState(id)]` (id 3→0x08, 4→0x32,
5→0x5b, 6→0x7c, 7→0xa7, 8→0xc8, 9→0x101, 10→0x128, 11→0x153, 12→0x16e, 13→0x18d, 14→0x1aa,
15→0x1d1, 16→0x1f0, 17→0x211, 18→0x235), returns 1. Typical char neutral (Sora FUN_00737800):
```
w = stats[id].walk
if (Up>1) y += w; if (Down>1) y -= w; if (Left>1) x -= w; if (Right>1) x += w;   // no diagonal normalisation
faceAngle = facingRight ? 0 : π
```
(velocity +0x98 keeps decaying by friction in the physics step, so residual dash momentum carries.)

### 4.1 Hitstun (state 1, FUN_006af880). Entered by FUN_006b19a0 (§6.3) or FUN_006b1d70.
Per frame (t = timer):
1. **Burst shield**: if `Shield == 3` (ver ≥ 0x58b; older: A>1&&B==3 or B>1&&A==3 also)
   **and** FUN_007809e0(p) **and** spend(1.0) (`DAT_00853b34`): → `FUN_006b1b40(p,1)` (barrier
   type 2: 120 f, radius 200), state 4, timer 0, SE 0xb, blur `FUN_004d7ac0(10,0.75,1)`;
   `FUN_006b1d70(p, 150.0, 18.0)`: if opponent not KO, hittable, not hitstun-immune and
   distance < 150 → opponent forced to state 1 timer 0, `opp.faceAngle = atan2(p.y-o.y, p.x-o.x)`,
   `opp.moveAngle = that + π`, `opp.speed = 18` (DAT_00814144), SE 0x19; if that succeeded and
   `opp.barrierTime > opp.barrierBreakT (50)` → `opp.barrierTime = 10`, popup 4, SE 0x19.
   Then `T2[4](p)` immediately.
2. else if `t >= 15 && D == 3`: **dash cancel** `FUN_006b17d0(p,1)` (heat +0.25, §4.3).
3. else: `t == 15` → FUN_00612900 (training frame meter only); `t > 29` → state 0, timer 0.
   Char hooks: Tsih (5) `T2[0x5e]`, Sumika 2nd (0x12) `T2[0x238]`.

Knockback: hitstun motion is purely the physics step (moveAngle, speed, friction 0.5/f).
Timing from a hit landing at frame H (collision phase): handler runs t=0 at H+1, dash-cancel
possible at H+16 (t=15), state→0 at H+31 (t=30), neutral actions from H+32. So **hitstun = 30
handler frames, dash-cancellable from its 16th** (community "30f, cancel after 15" confirmed).
No hyper during hitstun (FUN_00780950 returns 0 in state 1).

### 4.2 Dash entry `FUN_006b17d0(p, cancel)` @0x6b17d0
```
if (p->status[3] /*+0xa6 no-dash*/) return;
heat += cancel ? 0.25 (DAT_00813f24) : 0.3 (DAT_00813f28);   cancel=1 also shows popup 0
SE 8
a = dirAngle(p); dashAngle = a;
if (a > 2π) dashAngle = faceAngle = (facingRight ? 0 : π);
state = 2; timer = 0;            // NOTE: no T2 call here; flag158 not touched
speed = stats[id].dashSpeed (+4); moveAngle = dashAngle
FUN_004d7ff0(objId)              // visual trail
```
Called with cancel=0 from neutral/dash (D==3 in states 0/2 — re-dashing during a dash restarts it),
cancel=1 from hitstun t≥15, shield t≥5, burst t≥20, pushed t≥30, and from character moves.

### 4.3 Dash (state 2, FUN_006af5e0)
```
hold = stats.dashHold(+8); walk = stats.walk(+0); if (slow) { hold*=0.25; walk*=0.25 }
if (D > 1 && speed < hold) { speed = hold; heat += 0.012 (DAT_00813ed0) }
a = dirAngle(p); if (a < 2π) turnToward(&dashAngle, a, 0.08 (DAT_00813f00))   // FUN_005f0e70
moveAngle = faceAngle = dashAngle
if (speed <= friction) { speed = 0; state = 0 }       // timer NOT reset
if (D < 2) { Up/Down/Left/Right add ±walk to y/x like neutral }   // steer while coasting
if (timer % 5 == 0) spawn effect 100 (afterimage) via DAT_00881698
char hooks: Tsih T2[0x5c], Kae T2[0x1ab], Sumika 2nd T2[0x236]
```
`turnToward` (FUN_005f0e70): normalise cur and target to (−π, π] (FUN_005f0c60: while ≤ −π add 2π,
while > π sub 2π); `d = tgt − cur`; if `d > π` d = −(2π − d) else if `d < −π` d = 2π + d;
if `|d| < rate` cur = tgt else cur += sign(d)·rate.
Result: a tapped dash starts at dashSpeed and decays 0.5/f; holding D keeps speed at dashHold
forever at +0.012 heat/frame (1.2%/f). Speeds per character in §7.

### 4.4 Shield (state 3, FUN_006afac0). Entry FUN_006b1930(p,0) @0x6b1930:
`state=3, timer=0, speed=0, FUN_006b1b40(p,0)` (barrier type 1: 60 f, R=120), SE 0xb. Cost 0.5
hyper (spent by the input fn). Handler:
- t==0: face the opponent: `faceAngle = dashAngle = (opp.x <= p.x) ? π : 0`.
- t==5: if `opp.barrierTime > opp.barrierBreakT(50)` and
  `dist(p,opp) <= p.barrierR*p.barrierScale + opp.barrierR*opp.barrierScale` → `opp.barrierTime = 10`
  (cuts a fresh hyper/accel/shield barrier), popup 4, SE 0xc.
- t even and 0 ≤ t < 13: particle: `r = rand_real(-0.1, 0.1)` then `k = rand_int(0,100) % 8`
  (signed), spawn effect `0xa82+k` at (x, y−48) with angle `r + π/2`, effect field +0x40 = 0.2.
  (**consumes game RNG**: 2 draws on t = 0,2,…,12)
- t ≥ 5 && D == 3 → dash cancel FUN_006b17d0(p,1).
- else t == 5 → FUN_00612900 (UI); t > 19 → state 0, timer 0.
Total 20 frames (t = 0..19), dash-cancellable from t=5. Protection comes from the barrier (§6.6).

### 4.5 Burst shield (state 4, FUN_006afd90) — entered from hitstun (§4.1), cost 1.0 hyper
- t==0: face opponent (as state 3).
- t==5: if opponent not KO, hittable, not hitstun-immune and `dist <= p.barrierR*p.barrierScale`
  → opponent: state 5, timer 0, `dashAngle = moveAngle = atan2(o.y-p.y, o.x-p.x)` (away from p),
  `speed = 20.0` (DAT_008140fc), SE 0xc.
- particles as state 3 (effect +0x40 = 0.25).
- t ≥ 20 && D == 3 → dash cancel (cancel=1). t == 20 → UI; t > 29 → state 0.
Barrier type 2: 120 frames, R=200 (outlasts the state).

### 4.6 Shield-pushed (state 5, FUN_006b00a0)
Only timing: t ≥ 30 && D == 3 → dash cancel; t == 30 → UI; t > 39 → state 0. Motion = physics
(speed 20 decaying 0.5/f = 40 frames). The victim can still be hit (no barrier).

### 4.7 KO (state 6, FUN_006b0110). Entered by FUN_00780ab0 when hp < 1 (no immediate call).
- Char hooks first: id 4 T2[0x34], 5 T2[0x5d], 6 T2[0x7e], 7 T2[0xa9], 8 T2[0xca], 0xd T2[0x18f], 0xf T2[0x1d3].
- t==0: speed = 0; **opponent.roundsWon++**; FUN_0060b310(0) (round-end banner, `DAT_0087fb58=1`);
  spawn effect 900.
- t==100: effect 0x385; 16× (two effects 0x38e/0x38f at angle `(i/16)·2π + rand_real(-0.2,0.2)`,
  rand drawn per effect = 32 game-RNG draws); SE 0xc.
- t==200: if match decided (FUN_00619c60) set replay-end flags.
- every frame: hittable = 0.
Round flow after that is FUN_00608a00 (banner counter `DAT_0087fb5c`; ≈300 frames later either
next round via FUN_00561fc0(2) → Game Init, or result screen).

---------------------------------------------------------------------------------------------------
## 5. Movement physics summary

| Item | Value | Source |
|---|---|---|
| Walk (neutral) | `stats.walk` added per held direction per frame, x and y independent (diagonal = walk·√2) | FUN_00737800 etc. |
| Velocity | `x += cos(moveAngle)*speed`, `y += sin(moveAngle)*speed`; speed ×0.25 when slowed (status 4) | FUN_006f1720 |
| Friction | `speed -= friction` after moving; friction reset to 0.5 each frame (moves may override after the reset); if speed ≤ 0 → 0 | FUN_006f1720 |
| Dash | initial `stats.dashSpeed`, decays 0.5/f; while D held floor = `stats.dashHold`; ends when speed ≤ friction | §4.3 |
| Dash steering | dashAngle turns ≤ 0.08 rad/frame toward input direction; coasting (D released) also adds walk movement | §4.3 |
| Arena | circle centred at origin, radius 540: if `x²+y² > 291600` (compare as double) project back to radius 540 along atan2(y,x) | FUN_006f1720, DAT_008257a0, DAT_0081412c |
| Position quantisation | after both players update: x,y,z truncated to integers (toward 0) | FUN_006129b0 |
| Facing | `facingRight = x < opp.x` (unchanged when equal); neutral sets faceAngle 0/π from it | FUN_006f1720 |
| Player–player collision | none found (players pass through each other) — UNVERIFIED negative | |
| Start positions | P1 (−150, 0) faceAngle 0; P2 (150, 0) faceAngle π | FUN_005f4650 |
| World axes | Up key → +y, Right → +x. Rendering is a 3D model scene; screen orientation UNVERIFIED (expected y-up) | FUN_00737800 |

Trig: `cos` FUN_00467690 / `sin` FUN_0045a680 are CRT double cos/sin of the float, rounded to
float; `atan2` FUN_00474240 = CRT atan2(y,x). Float precision matters for 1:1 (SSE scalar float).

---------------------------------------------------------------------------------------------------
## 6. Gauges and rules

### 6.1 HP
- maxHp = hp = redHp = recovHp = handicap table value (FUN_0069cc40; **corrected 2026-11**, modes.md §0.5):
  `DAT_0087fad4[i]` (per-player handicap) 0..5 → **4500 / 6750 / 9000 / 13500 / 18000 / 27000** via the jump
  table at 0x69d038 (`jmp [4*ecx+0x69d038]` at 0x69ce04, cases 0x69ce0b..0x69ce47; `ja` → 9000 for anything
  else). Ghidra collapsed the switch into `fad4 < 6 ? 4500 : 9000`, which is why this said 4500 before. Every
  menu path (arcade, story, match, training; modes.md §0.2) sets fad4 = fad8 = **2 → 9000 HP**. Port: per-player
  match parameter `aos_match_params.handicap` (`AOS_HANDICAP_DEFAULT` = 2), `aos_handicap_hp()`; netplay START
  carries it (proto 4).
- Exception Sumika 2nd form (id 0x12, boss): maxHp = hp = recov = 12000 / 15000 / 18000 for
  difficulty `DAT_0087fab0` = 0 / 1 / 2 (other values: the table value); redHp (`local_a8`) keeps the
  table value (9000) and snaps to hp next frame.
- `recovHp` cap: after damage `recov -= (int)(rawDamage*0.8)` and each frame recov is lowered to
  `(hp/(max/3)+1)*(max/3)` (integer). Heals (FUN_007808a0: `hp += (int)(amount*0.1)` capped at recov,
  then redHp = hp) only come from absorb-type moves (collision FUN_0060bf30/FUN_006113a0).

### 6.2 Damage — `FUN_00780ab0(target, dmg, type, stunAdd)` @0x780ab0, returns "enter hitstun"
```
d = dmg
if ((type == 1 || type == 3) && heat > 1.0) d = (int)((float)dmg * (heat - 1.0)) + dmg;   // heat multiplier
hp -= d
recov -= (int)((float)dmg * 0.8 (DAT_008145e8));  if (recov < redHp) redHp = recov
comboTimer = 30; comboDamage += d; comboHits += 1
stun += stunAdd;  ret = (stun >= 1.0)
hyper += (float)d * 0.0001 (DAT_0082642c) * hyperMul          // victim gains meter
if (hp < 1) { hp = 0; state = 6; timer = 0; DAT_00885964 = 2 /*round over*/; ret = 0 }   // (training: no KO if infinite HP)
attacker (opp of target): combo display (+0x11c += d, +0x128++, +0x130=120; caps 99999/99)
```
- **Heat multiplier**: damage taken ×(1 + max(0, heat−1)), i.e. ×1 up to 100% heat, ×2 at 200%,
  ×3 at 300%. Only for object damage types 1 and 3 (type is the object's +0x10 field; see objects.md).
- No other scaling (no combo proration) in this function.
- Callers (the "→player" collision passes in §1.2): if the object overlaps the target and
  `target.barrierTime == 0`: `if (FUN_00780ab0(...)) FUN_006b19a0(target, objAngle, objKnockSpeed)`.

### 6.3 Hitstun entry `FUN_006b19a0(p, angle, speed)` @0x6b19a0
If `status[6] (+0xa9, hitstun-immune)` is set: nothing. Else `state = 1, timer = 0,
faceAngle = norm(angle + π), dashAngle = angle, moveAngle = angle, speed = speed`.
Stun accumulation: a hit only causes hitstun when `stun ≥ 1.0`; `stun` is reset only when
`comboTimer` reaches 0, and comboTimer only counts down while **not** in hitstun (30 frames after
the last hit). Once ≥1.0, every further hit of the combo re-enters hitstun (timer restarts at 0).

### 6.4 Invulnerability / immunity
| Condition | Effect | Source |
|---|---|---|
| `barrierTime (+0xf8) > 0` | no damage from any player-damage pass; objects still collide with the barrier circle and may be consumed | FUN_0060b610 etc. |
| state 2 (dash) **or** (state 0 and speed > 0) and no barrier | **Beams, lasers and list-4b2eb0 objects pass through** (no collision at all). Missiles (list 52b950) still hit. | FUN_0060b610, FUN_006100b0, FUN_0060f5a0 vs FUN_00610b80 |
| `hittable (+0xa2) == 0` | skipped by all collision passes (KO, match end) | |
| `status[6] (+0xa9)` | damage still applies, but no hitstun / no shield push / no burst push | FUN_006b19a0, FUN_006b1d70 |
| round over (`DAT_00885964 == 2`) | collision passes not run | FUN_006129b0 |
Graze: **no graze mechanic exists** in the player code (no heat/hyper gain on near-miss found) —
UNVERIFIED negative (searched all writers of +0xe0/+0xe4).

### 6.5 Hyper gauge (+0xe4, 0..3.0)
Gains: (a) damage taken `d*0.0001` (§6.2); (b) **using a move**: FUN_007807f0 adds the move's
table gain (§6.9); (c) absorb moves: `objDamage * DAT_0082007c * hyperMul` (FUN_0060bf30);
(d) CPU on difficulty 2: +0.001 every 5 frames; (e) training infinite: +0.02/f.
Clamp to ≤ 3.0 each frame (FUN_006f1720). No decay.
Costs: Hyper attack 1.0, Accel Hyper 3.0, Shield 0.5, Burst shield (from hitstun) 1.0.

`FUN_00780950(p)` @0x780950 "may hyper": false if state 6 or 1; false if own barrier
`type==3 && time > 80` (first 20 frames of own hyper barrier); false if own `type==4 && time != 0`
(own Accel active); false if opponent `type==4 && time ≥ 1` (opponent Accel active); else true.
`FUN_007809e0(p)` @0x7809e0 "may shield": false if opponent Accel barrier active, false if round
over, false if scheme 1 and H held; else true.
Accel Hyper availability: hyper ≥ 3.0 + FUN_00780950 + the input (S>1 && B>1 && H==3 in scheme 0).

### 6.6 Barriers `FUN_006b1b40(p, kind)` @0x6b1b40
| kind | type +0xf4 | time +0xf8 | radius +0xfc | used by |
|---|---|---|---|---|
| 0 | 1 | 60 | 120 (DAT_00814174) | Shield (state 3) |
| 1 | 2 | 120 | 200 (DAT_008147cc) | Burst shield (state 4) |
| 2 | 5 | 60 | 160 (DAT_00814168) | (no caller found) |
| 10 | 3 | 100 | 160 | start of every Hyper attack (char code) |
| 0x14 | 4 | 100 | 160 | Accel Hyper (char code) |
Always: `barrierScale = 0, barrierRot = 0, barrierBreakT = 50`. (Alte, ver ≥ 0x58b: also sets a
flag on her unit object.)
Per frame while `time > 0` (FUN_006f1720, after physics):
```
hurtShape = {0,0,0,0}
if (time < 21) { scale = time / 20.0 (DAT_008140fc); hurtShape.radius = 0 }        // last 20 frames: no collision
else { scale = (scale >= 1.0) ? 1.0 : scale + 0.1; hurtShape.radius = scale * R * 0.8 }
barrierRot = rand_real(-π, π)        // game RNG, every frame
time--
```
So a barrier blocks all damage for its whole duration, and for all but the last 20 frames also
physically absorbs objects within `0.8·R·scale`.

### 6.7 Heat (+0xe0, 0..3.0 = 0..300%)
Gains: dash +0.30, dash-cancel +0.25, held dash +0.012/f, plus per-move additions in character
code (e.g. Suguri +0.3 in several moves) and status 1 (+0.007/f) / status 7 (floor 2.01).
Decay per frame (FUN_006f1720): none in hitstun/KO; −0.01 in states 0, 2, 3; −0.005 in every
other state (attacks, 4, 5). Clamped to [0, 3.0]. Effect: damage-taken multiplier (§6.2).

### 6.8 Status effects (`FUN_006b1a40(p, k, frames)` @0x6b1a40, callbacks `DAT_00881530[k]`)
Apply: if not KO and hittable: if already on, duration = max(old, frames); else on, duration = frames
(SE 0x26 for k = 1, 2, 7; SE 0x15 for k = 3 when newly applied). Callbacks run each frame (§1.3):

| k | flag / timer | callback | effect | ends |
|---|---|---|---|---|
| 0 | 0xa3 / 0xac | none | — | — |
| 1 | 0xa4 / 0xb0 | FUN_00751b20 | heat +0.007/f (DAT_00813eb4); particles (2 game-RNG draws/f) | time<1 or KO |
| 2 | 0xa5 / 0xb4 | FUN_00751920 | hyper −0.001/f (≥0); recovHp −1/f (≥ hp); particles (2 RNG/f) | time<1 or KO |
| 3 | 0xa6 / 0xb8 | FUN_00751ce0 | **cannot dash**; particle every frame%10==1 (1 RNG) | time<1 or KO |
| 4 | 0xa7 / 0xbc | FUN_00751dc0 | **slow**: velocity, walk and dash speeds ×0.25 | time<1 or KO |
| 5 | 0xa8 / 0xc0 | none (0x881544 unset) | — (flag never auto-cleared) | — |
| 6 | 0xa9 / 0xc4 | FUN_00751cd0 (no-op) | **hitstun immunity** (set 99999 by boss code) | never |
| 7 | 0xaa / 0xc8 | FUN_00751750 | heat forced ≥ 2.01 (DAT_00825fcc); on end heat = 1.0; particles (2 RNG/f) | time<1 or KO |
| 8 | 0xab / 0xcc | FUN_007516e0 | marker only | time<1 or state ≠ 1 |
Applied by character moves (callers at dec/all.c lines 169296, 182600, 190578, 195978, 197554,
280017, 281339, 281417, 372241, 372726, 433328).

### 6.9 Weapon gauges A/B (+0xe8 / +0xec) and the per-move table
- Range 0..1, start 1.0, regen +0.01/f (DAT_00813ec8) only in states 0, 1, 2; capped 1.0.
- `FUN_007807f0(p, s)` @0x7807f0: if `gaugeA >= T[s].a && gaugeB >= T[s].b` → subtract both,
  `hyper += T[s].hyper * hyperMul`, return 1; else return 0 (move not performed / weaker branch).
  `T = 0x87c4f0 + s*0xc` {float a, float b, float hyperGain}, indexed by **state id**.
- Table built in FUN_0049ae40 (registry; parsed from disassembly 0x4a1200–0x4a7261) and patched by
  FUN_004a7270 for ver 0x5a2 (patched cells marked *). Entries not listed are 0/0/0.
- Names: suguri.md calls these "ammo A/B"; HUD names UNVERIFIED (B-weapon moves mostly use
  gaugeB, A/S moves gaugeA).

| state | char | gaugeA cost | gaugeB cost | hyper gain |
|---|---|---|---|---|
| 0x012 | Sora | 0.25 | 0 | 0.04 |
| 0x013 | Sora | 0.1 | 0 | 0.01 |
| 0x014 | Sora | 0 | 0 | 0.1 |
| 0x015 | Sora | 0.4 | 0 | 0.1 |
| 0x018 | Sora | 0 | 0 | 0.05 |
| 0x019 | Sora | 0 | 0 | 0.025 |
| 0x01a | Sora | 0 | 0 | 0.025 |
| 0x01b | Sora | 0 | 0 | 0.05 |
| 0x01e | Sora | 0 | 0.075 | 0.01 |
| 0x01f | Sora | 0 | 0.15 | 0.02 |
| 0x03c | Alte | 0.025 | 0 | 0.005 |
| 0x03d | Alte | 0.03 | 0 | 0.005 |
| 0x03e | Alte | 0 | 0.4 | 0.075 |
| 0x03f | Alte | 0 | 0.3 | 0.05 |
| 0x041 | Alte | 0 | 0 | 0.15* |
| 0x04a | Alte | 0.03* | 0 | 0 |
| 0x04c | Alte | 0 | 0.7* | 0.1 |
| 0x067 | Tsih | 0.15 | 0 | 0.02 |
| 0x068 | Tsih | 0.125 | 0 | 0.015 |
| 0x06a | Tsih | 0.5 | 0 | 0.05 |
| 0x06b | Tsih | 0.35 | 0 | 0.05 |
| 0x06d | Tsih | 0 | 0.4 | 0.075 |
| 0x073 | Tsih | 0 | 0 | 0.05 |
| 0x074 | Tsih | 0 | 0 | 0.05 |
| 0x075 | Tsih | 0 | 0 | 0.05 |
| 0x086 | Mira | 0.15 | 0 | 0.05 |
| 0x089 | Mira | 0 | 0.2* | 0.1 |
| 0x08a | Mira | 0 | 0.2* | 0.05 |
| 0x08b | Mira | 0 | 0.2* | 0.05 |
| 0x08c | Mira | 0 | 0.15 | 0.05 |
| 0x08d | Mira | 0 | 0.3 | 0.075 |
| 0x090 | Mira | 0 | 0 | 0.05 |
| 0x091 | Mira | 0.03 | 0 | 0.005 |
| 0x097 | Mira | 0 | 0 | 0.05 |
| 0x098 | Mira | 0 | 0 | 0.025 |
| 0x099 | Mira | 0 | 0 | 0.035 |
| 0x0b1 | Sham | 0.2 | 0 | 0.02 |
| 0x0b3 | Sham | 0.2 | 0 | 0.025 |
| 0x0b7 | Sham | 0 | 0 | 0.05* |
| 0x0b9 | Sham | 0.15 | 0 | 0.015 |
| 0x0ba | Sham | 0.5 | 0 | 0.05* |
| 0x0bb | Sham | 0 | 0.3 | 0.05 |
| 0x0bc | Sham | 0 | 0.5 | 0.1 |
| 0x0d5 | Nath | 0.075 | 0 | 0.0075 |
| 0x0d6 | Nath | 0.05 | 0 | 0.005 |
| 0x0d7 | Nath | 0.03 | 0 | 0.003 |
| 0x0d8 | Nath | 0 | 0.085 | 0.0085 |
| 0x0da | Nath | 0 | 0.125* | 0.01 |
| 0x0db | Nath | 0 | 0.083* | 0.0075 |
| 0x0dd | Nath | 0.3 | 0 | 0.05 |
| 0x0df | Nath | 0 | 0.4 | 0.05 |
| 0x0e0 | Nath | 0 | 0.25 | 0.035 |
| 0x0e1 | Nath | 0 | 0.2 | 0.02 |
| 0x0e3 | Nath | 0 | 0 | 0.025 |
| 0x0e7 | Nath | 0 | 0 | 0.02 |
| 0x0e9 | Nath | 0 | 0 | 0.04 |
| 0x0ea | Nath | 0.1 | 0 | 0.02 |
| 0x10c | Star Breaker | 0.09* | 0 | 0.015 |
| 0x10d | Star Breaker | 0 | 0.4 | 0.03 |
| 0x10f | Star Breaker | 0 | 0.15 | 0.038* |
| 0x110 | Star Breaker | 0 | 0.35 | 0.05 |
| 0x113 | Star Breaker | 0.075 | 0 | 0.02 |
| 0x114 | Star Breaker | 0.075 | 0 | 0.02 |
| 0x115 | Star Breaker | 0.1 | 0 | 0.025 |
| 0x116 | Star Breaker | 0.075 | 0 | 0.02 |
| 0x117 | Star Breaker | 0.075 | 0 | 0.02 |
| 0x118 | Star Breaker | 0.1 | 0 | 0.025 |
| 0x132 | Suguri | 0.25 | 0 | 0.04 |
| 0x133 | Suguri | 0 | 0 | 0.1 |
| 0x134 | Suguri | 0.4 | 0 | 0.1 |
| 0x135 | Suguri | 0.15 | 0 | 0.02 |
| 0x137 | Suguri | 0 | 0.075 | 0.02 |
| 0x138 | Suguri | 0 | 0.1 | 0.015 |
| 0x139 | Suguri | 0 | 0 | 0.05 |
| 0x13a | Suguri | 0 | 0 | 0.025 |
| 0x13b | Suguri | 0 | 0 | 0.025 |
| 0x13c | Suguri | 0.25 | 0 | 0.025 |
| 0x13d | Suguri | 0.25 | 0 | 0.025 |
| 0x13e | Suguri | 0.25 | 0 | 0.025 |
| 0x140 | Suguri | 0 | 0 | 0.1* |
| 0x15d | Saki | 0.4 | 0 | 0.05 |
| 0x15e | Saki | 0.1 | 0 | 0.01 |
| 0x15f | Saki | 0 | 0.05 | 0.005 |
| 0x160 | Saki | 0 | 0.4 | 0.075 |
| 0x162 | Saki | 0 | 0 | 0.05 |
| 0x163 | Saki | 0 | 0 | 0.075 |
| 0x178 | Iru | 0.3 | 0 | 0.11* |
| 0x179 | Iru | 0.08 | 0 | 0.01* |
| 0x17a | Iru | 0 | 0.03 | 0.01* |
| 0x17b | Iru | 0 | 0.03 | 0.007* |
| 0x17c | Iru | 0.6* | 0 | 0.02* |
| 0x180 | Iru | 0 | 0.03* | 0.0072* |
| 0x181 | Iru | 0 | 0 | 0 |
| 0x182 | Iru | 0 | 0 | 0 |
| 0x198 | Nanako | 0.075 | 0 | 0.01 |
| 0x199 | Nanako | 0.1 | 0 | 0.02 |
| 0x19a | Nanako | 0.03 | 0 | 0.005 |
| 0x19b | Nanako | 0 | 0.075 | 0.01 |
| 0x19c | Nanako | 0 | 0.04 | 0 |
| 0x19d | Nanako | 0 | 0 | 0.05 |
| 0x19e | Nanako | 0 | 0 | 0.05 |
| 0x1b4 | Kae | 0.1 | 0 | 0.011* |
| 0x1b5 | Kae | 0.05 | 0 | 0.01 |
| 0x1b6 | Kae | 0 | 0.1 | 0.01 |
| 0x1b7 | Kae | 0 | 0.1 | 0.01 |
| 0x1ba | Kae | 0.05 | 0 | 0.015 |
| 0x1bb | Kae | 0 | 0.5 | 0.025 |
| 0x1bc | Kae | 0 | 0 | 0.05 |
| 0x1bd | Kae | 0 | 0 | 0.03 |
| 0x1be | Kae | 0 | 0 | 0.04 |
| 0x1bf | Kae | 0 | 0 | 0.025 |
| 0x1c0 | Kae | 0 | 0.25 | 0.045* |
| 0x1db | Kyoko | 0.04 | 0 | 0.005 |
| 0x1dc | Kyoko | 0.15 | 0 | 0.06* |
| 0x1dd | Kyoko | 0.15 | 0 | 0.05 |
| 0x1df | Kyoko | 0 | 0.1 | 0.025 |
| 0x1e0 | Kyoko | 0 | 0 | 0 |
| 0x1e1 | Kyoko | 0 | 0 | 0.01 |
| 0x1e2 | Kyoko | 0 | 0 | 0 |
| 0x1e5 | Kyoko | 0 | 0.4 | 0.1 |
| 0x1fa | Hime | 0.1 | 0 | 0.02 |
| 0x1fb | Hime | 0.7 | 0 | 0.1 |
| 0x1fc | Hime | 0.075 | 0 | 0.015 |
| 0x1fd | Hime | 0 | 0.5* | 0.102* |
| 0x1fe | Hime | 0 | 0.2 | 0.05 |
| 0x1ff | Hime | 0 | 0 | 0.05 |
| 0x200 | Hime | 0 | 0 | 0.02 |
| 0x201 | Hime | 0 | 0 | 0.02 |
| 0x202..0x206 | Hime | 0 | 0 | 0 |
| 0x21b | Sumika | 0.25* | 0 | 0.025 |
| 0x21e | Sumika | 0 | 0 | 0.015 |
| 0x21f | Sumika | 0 | 0 | 0.02 |
| 0x222 | Sumika | 0 | 0.03* | 0.01 |
| 0x224 | Sumika | 0 | 0.05 | 0.01 |
| 0x226 | Sumika | 0 | 1.0 | 0.15 |
| 0x23f | Sumika 2nd | 0.2 | 0 | 0.02 |
| 0x242 | Sumika 2nd | 0 | 0 | 0.015 |
| 0x243 | Sumika 2nd | 0 | 0 | 0.02 |
| 0x246 | Sumika 2nd | 0 | 0.03* | 0.01 |
| 0x248 | Sumika 2nd | 0 | 0.05 | 0.01 |
| 0x24a | Sumika 2nd | 0 | 1.0 | 0.15 |

Float sources (rd): 0.003 813ea0, 0.004 813ea4, 0.005 813eac, 0.0072 813eb8, 0.0075 813ebc,
0.0085 813ec4, 0.01 813ec8, 0.011 813ecc, 0.012 813ed0, 0.015 813ed4, 0.02 813ed8, 0.025 813edc,
0.03 813ee0, 0.035 813ee4, 0.038 813ee8, 0.04 813eec, 0.045 813ef0, 0.05 813ef4, 0.06 813ef8,
0.075 813efc, 0.08 813f00, 0.083 813f04, 0.085 813f08, 0.09 813f0c, 0.102 813f10, 0.11 813f18,
0.125 813f1c, 0.15 813f20, 0.25 813f24, 0.3 813f28, 0.35 813f2c, 0.4 813f30, 0.6 813f34,
0.7 813f38, 0.1 853b10, 0.2 853b14, 0.5 853b1c, 1.0 853b34. (The exact float bit patterns are
the constants at those addresses — copy them, e.g. 0.083 = 0x3da9fbe7.)

### 6.10 Rounds, timer, KO
- Round timer `DAT_00885974` (seconds) set in Game Init: config `DAT_00881eac` 0→90, 1→180
  (default), 2→270, 3→999 (infinite); arcade/story (`DAT_0087faa8` 0/1) 180; training 999;
  netplay `DAT_0087faf4`.
- Decrement (end of tick, after `frame++`): if `frame % 60 == 0 && timer != 999 && timer > 0 &&
  !roundStartBanner(DAT_0087faf8) && !roundEndBanner(DAT_0087fb58)` → timer−−.
- **Time over** (`timer == 0 && round not over`): if `P0.hp > P1.hp` P0.roundsWon++, **else
  P1.roundsWon++ (a tie goes to P2)**; round over (`DAT_00885964 = 2`), FUN_0060b310(1).
- **KO**: FUN_00780ab0 sets state 6 and round over; the KO handler gives the opponent the round.
- Rounds to win `DAT_0087faf0` (VS: config `DAT_00881f60 + 1`; arcade 1). Match over when a
  player's roundsWon ≥ it (FUN_00619c60). New rounds re-run Game Init (positions, gauges, HP reset;
  roundsWon kept).

### 6.11 Training-only overrides (`DAT_0087faa8 == 3`, or replay of training)
Heat lock `DAT_00894358` 0..3 (floor 0/1/2/3), hyper cap `DAT_0089435c`, gauges full
`DAT_00894361/62`, infinite HP `DAT_00894353` (+50 hp/f, no KO). Not needed for versus.

---------------------------------------------------------------------------------------------------
## 7. Per-character stats record `DAT_00881f80 + id*0x2dc` (FUN_0049ae40 registry, + FUN_004a7270)

Gameplay fields (all others are UI/resource data):

| id | Char | +0x00 walk | +0x04 dashSpeed | +0x08 dashHold |
|---|---|---|---|---|
| 3 | Sora | 5 | 15 | 12 |
| 4 | Alte | 5 | 13 | 10 |
| 5 | Tsih | 6 | 12 | 11 |
| 6 | Mira | 5 | 12 | 10 |
| 7 | Sham | 5 | 12 | 10 |
| 8 | Nath | 4 | 15 | 9 |
| 9 | Star Breaker | 5 | **13** (12 before patch) | 10 |
| 10 | Suguri | 5 | 15 | 12 |
| 11 | Saki | 4 | 14 | 12 |
| 12 | Iru | 4 | 14 | 12 |
| 13 | Nanako | 4 | 14 | 12 |
| 14 | Kae | 4 | 17 | 11 |
| 15 | Kyoko | 4 | 12 | 10 |
| 16 | Hime | 5 | 14 | 12 |
| 17 | Sumika | 5 | 14 | 12 |
| 18 | Sumika 2nd | 5 | 14 | 12 |
Constants: 4 `DAT_00853b68`, 5 `DAT_00813f40`, 6 `DAT_00813f44`, 9 `DAT_00813f48`, 10 `DAT_00813f4c`,
11 `DAT_00813f50`, 12 `DAT_00813f54`, 13 `DAT_00813f58`, 14 `DAT_00813a54`, 15 `DAT_00853b88`,
17 `DAT_00813f5c`.

UI/resource fields (for completeness; not used by gameplay):

| id | +0x0c..+0x2c (3 colors × 3 ints; read per `+0x18` color into `DAT_008859f4`) | +0x3c,+0x40,+0x44 (sprite ids, portrait/cut-in; UNVERIFIED role) | +0x2c0,+0x2c4 (int), +0x2c8 (float) UI offsets/scale | +0x2cc..+0x2d4 RGB |
|---|---|---|---|---|
| 3 | 9 8 0 / 6 4 0 / 9 8 0 | 0x0f 0x10 0x11 | 0, 0, 0 | 170,240,70 |
| 4 | 5 5 0 / 4 6 0 / – | 0x39 0x3a 0x3b | 0, 50, 0 | 250,140,210 |
| 5 | 10 8 6 / 3 7 11 / – | 0x62 0x63 0x64 | 30, −30, 0.5 | 130,130,220 |
| 6 | 6 4 11 / 3 2 7 / – | 0x83 0x84 0x85 | 0, 50, 0 | 220,90,130 |
| 7 | 5 8 6 / 3 7 3 / – | 0xae 0xaf 0xb0 | 50, 0, 0 | 100,230,90 |
| 8 | 6 8 10 / 3 7 11 / – | 0xcf 0xd0 0xd1 | 50, 30, 0 | 130,220,220 |
| 9 | 1 0 1 / 1 0 0 / 1 0 1 | 0x108 0x109 0x10a | 0, 70, 0 | 80,170,180 |
| 10 | 0 5 0 / 2 1 0 / 0 5 0 | 0x12f 0x130 0x131 | 30, −70, 0 | 160,180,255 |
| 11 | 3 6 0 / 1 5 0 / 3 6 0 | 0x15a 0x15b 0x15c | 50, 50, 0 | 255,190,120 |
| 12 | 2 6 0 / 0 0 0 / – | 0x175 0x176 0x177 | 0, 0, 0 | 140,230,160 |
| 13 | 5 11 0 / 1 7 0 / 5 11 0 | 0x194 0x195 0x196 | 100, 0, 0 | 240,120,250 |
| 14 | 11 5 0 / 4 10 0 / 11 5 0 | 0x1b1 0x1b2 0x1b3 | −100, 80, 0.75 | 240,100,100 |
| 15 | 0 0 0 / 5 0 0 / – | 0x1d8 0x1d9 0x1da | 50, 80, 0 | 150,150,255 |
| 16 | 0 11 5 / 2 3 7 / 0 11 5 | 0x1f7 0x1f8 0x1f9 | 0, 0, 0 | 250,200,100 |
| 17 | 0 11 5 / 2 3 7 / 0 11 5 | 0x218 0x219 0x21a | 0, 0, 0 | 250,200,100 |
| 18 | 0 11 5 / 2 3 7 / – | 0x23c 0x23d 0x23e | (not set) | (not set) |
("–" = third color not written by the registry ⇒ 0.) Other fields: +0x48 resource name strings
(`res_chr`/`res_sys` lookups), +0x2a8 `xx_res_bgp` string, +0x2d8 = 0 (BGM index read at
0x49... `DAT_00882258`), font file paths. Records 0–2 unused.

---------------------------------------------------------------------------------------------------
## 8. RNG

Two **MT19937** generators (MSVC `std::mt19937`, 32-bit, state 0x4e2 dwords incl. index, 5000 bytes):
- `DAT_00875b68` cosmetic, seeded from `std::random_device` at startup (FUN_00459e30).
- `DAT_00876ef0` **game**, selected while `DAT_00a50ef0 == 1` (FUN_0045a0b0). The tick switches to
  it at entry and back at exit (§1.2), so every RNG call made during the tick (including visual
  effects spawned by gameplay code, barrier rotation, status particles) consumes the game stream.
- Seeding: Game Init FUN_005f4650 (each round): `FUN_0045a0b0(1); FUN_00459f20(0); FUN_0045a0b0(0)`
  → game RNG = `init_genrand(0)` (FUN_0045a110: `s[0]=seed; s[i] = 1812433253*(s[i-1]^(s[i-1]>>30)) + i`,
  i < 624, index = 624). So **every round starts from seed 0**. Output tempering is standard MT
  (FUN_0045a500: `y ^= y>>11; y ^= (y<<7)&0x9d2c5680; y ^= (y<<15)&0xefc60000; y ^= y>>18`).
  Note this is a doubled-buffer implementation; outputs equal reference MT19937 (UNVERIFIED by test
  vector: first output for seed 0 should be 2357136044).
- `rand_int(lo, hi)` = FUN_00459f80(lo, hi) → FUN_0045a1b0, inclusive range, MSVC uniform_int:
  ```
  n = hi - lo;                      // unsigned
  if (n == 0xffffffff) return lo + mt();   (FUN_0045a340)
  if (n == 0) return lo;            // consumes NO random number
  for (;;) { r = mt(); if (r/(n+1) < 0xffffffff/(n+1) || 0xffffffff%(n+1) == n) return lo + r%(n+1); }
  ```
- `rand_real(a, b)` = FUN_00459fd0(a, b) → FUN_0045a270 (MSVC generate_canonical<float,24>):
  `k = ceil(24/log2(2^32)) = 1` → one draw: `u = (float)(double)mt() / 4294967296.0f` (u may round
  to 1.0), result `= (b − a)*u + a` in float.
- Netplay uses rollback: the game RNG state is saved/restored with the snapshot (FUN_0045a030 /
  FUN_0045a070), background (cosmetic RNG) is skipped during re-simulation.

For deterministic port netplay: keep one game RNG, reseed with 0 each round, and call it exactly
where the PC game does inside the tick (if bit-parity with PC replays is not required, purely visual
draws may be moved to a separate stream — but then all clients must do the same).

---------------------------------------------------------------------------------------------------
## 9. UNVERIFIED list
- `DAT_0087fad4[i]` (HP handicap, §6.1): no menu found that offers values other than 2 — check the
  character-select text ids 0x1d8..0x1dd.
- HUD names of gaugeA/gaugeB (+0xe8/+0xec).
- Whether any T2 handler ever returns 0 (would erase the player in FUN_006f1720).
- Status k=5 (+0xa8) has no callback; nobody found that sets it.
- Barrier kind 2 (type 5) has no caller found.
- Screen orientation of +y (Up key = +y in world; renderer is 3D).
- MT19937 equivalence (test vector), and float rounding of `(float)(double)u32` in rand_real.
- Stats +0x3c..+0x44 sprite-id role; +0x0c..+0x2c color triplets.

---------------------------------------------------------------------------------------------------
## 10. Notes for other specs

**objects.md**
- Collision order and categories are in §1.2 #9; lists: Beam `FUN_004aa6b0` (count FUN_004aa840),
  Missile `FUN_0052b950` (FUN_0056b370), Laser `FUN_0060d640`, units/"Enemy" `FUN_004ef290`
  (FUN_0051b1a0), plus lists `FUN_004b2eb0` and `FUN_004c6860`.
- Beam→player (FUN_0060b610): skip if beam owner side == player side; ver ≥ 0x58a skips beam
  `type 8, subtype 200`. Dash immunity (§6.4) applies to beams/lasers/4b2eb0 but **not** missiles.
  Damage args: beam +0x84 dmg, +0x10 type, +0x8c stunAdd; hitstun args +0x60 angle, +0x88 speed;
  then if beam +0x55 → beam hit callback `DAT_00881968[beam type]` and erase.
  Missile: +0xa0 dmg, +0x10 type, +0xa8 stun, +0x68 angle, +0xa4 speed, callback `DAT_00881aa8`.
- Shape test FUN_005f1c90(objShape, playerPos, objPos?, playerShape(+0x5c), objAngle, playerAngle):
  shape = {halfW, halfH, radius, ?}; circle part `dist < r1+r2`, box part uses rotated corners and
  a 1.5× (DAT_00814110) bounding pre-check.
- FUN_0060d690 (units push players): note it `return`s (not continue) when a player is KO /
  unhittable / hitstun-immune, skipping the other player.
- Object lifetime counters decremented in the tick (§1.2 #12).
- `FUN_00780ab0` damage type: heat multiplier only for types 1 and 3.
- Spawner `DAT_00881698` (FUN_004ef2c0) = effect spawn (id, color, pos*, angle, ownerId, 0, side).

**suguri.md**
- Suguri id is **10** in +0x10 (handbook 7). Input fns FUN_0075a030 (scheme A) / FUN_0075a4b0
  (scheme B), CPU FUN_0075b420 (diff 0) / FUN_0075a930 (diff 1,2). Stats: walk 5, dash 15, hold 12.
- Gauge costs/hyper gains for her states are in the §6.9 table; `FUN_007807f0(p, state)` gates
  the moves. Several of her handlers add heat +0.3 directly (dec/all.c 423658, 423727, 424000, 424013).
- Accel Hyper 0x143 (S>1 && B>1 && H==3, cost 3.0); hypers 0x141 (S), 0x13f (B), 0x136 (A), cost 1.0.
- Hyper/Accel states should call `FUN_006b1b40(p,10)` / `(p,0x14)` (barrier) — check her handlers.
- `FUN_007804b0` = aim angle at opponent (atan2), except vs stealthed Tsih (+0x164).

**Alte (for whoever does her)**: FUN_006f1720 has an Alte-only movement rule (ver ≥ 0x58b, id 4,
state 0x41 with timer 15..40, or state 0x44 with timer 15..34): after the normal velocity step, if
the step was < 8 px on both axes (DAT_008140d4) and speed > 0.1, apply the step again with the
larger-movement axis ×3.0 and the other ×1.0. Barrier creation also flags her unit object.
