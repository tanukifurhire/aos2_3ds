# Objects, projectiles, collision (objects.md)

Scope: every gameplay object that is not a player — how player states spawn them, the object lists,
their struct layouts, per-frame update/draw dispatch, the shared movement helpers, the collision
shapes and the hit test, every collision pass (who-hits-whom and what happens), the damage /
hitstun entry points, graze, shields/dash invulnerability, sound/visual effect APIs, arena bounds
and camera.

All addresses are VAs in `aos2.exe`. Constants were read with `xref.exe rd`. "v" = the game
version global `DAT_0089426c` (see 0.1).

---------------------------------------------------------------------------------------------------

## 0. Conventions used by the whole object system

### 0.1 Version global `DAT_0089426c`

Many object parameters branch on `DAT_0089426c` (e.g. `if (DAT_0089426c < 0x58a) dmg=0xb4 else
dmg=0xd8`). It is the *replay/netplay data version*: `FUN_00794210` writes the match header at
`0x894268` with `[0]=0x44c, [1]=0x5a2`, so for a live (non-replay) match `DAT_0089426c = 0x5a2`.
**Port the 0x5a2 branches** (all `>0x5a0`, `>=0x58a`, `>0x58b` tests are TRUE). Older branches only
matter for old-replay playback.

### 0.2 Coordinates, angles, units

* World units are float "pixels". Origin = arena centre. **+x right, +y up** (world→screen flips y:
  `FUN_00619d90`: `sx = W/2 + (x-camX)/zoom`, `sy = H/2 - (y-camY)/zoom`).
* Angles are radians, 0 = +x, counter-clockwise positive (`x += cos(a)*v; y += sin(a)*v`
  everywhere, e.g. beam move `FUN_004b67a0`). `FUN_00467690` = cos, `FUN_0045a680` = sin,
  `FUN_00474240(y,x)` = atan2 (float).
* Angle wrap `FUN_005f0c60(float*)`: while a <= -π add 2π; while a > π subtract 2π
  (`DAT_00814100 = -π`, `DAT_00853b58 = π`, `DAT_00853b78 = 2π`). Result in (-π, π].
  `FUN_005f0ce0(a)` same but returns the value and uses `a < -π` (so -π stays -π).
* Draw code passes `-angle` (sign-bit flip with `DAT_00853e00 = 0x80000000`) to the sprite
  rotation because screen y is down.
* Constants used everywhere: `PTR_00853af4 = 0.0`, `DAT_00853b34 = 1.0`, `DAT_00853b1c = 0.5`,
  `DAT_00853b10 = 0.1`, `DAT_00814104 = π/2`, `DAT_0081416c = -π/2`.

### 0.3 Teams / owners (fields every object carries)

| Player field | Meaning | Evidence |
|---|---|---|
| `+0x04` | unique object id of the player ("owner id"). Objects store it as *owner*. | `FUN_007805f0(id)` returns the player whose `+4 == id` |
| `+0x14` | team: **1 = player slot 0, 2 = player slot 1** | `FUN_00780450(t)`: t==1→player[0], t==2→player[1]; `FUN_00780480(t)` returns the *opponent* (t==1→player[1], t==2→player[0]) |
| `+0x18` | passed through to missiles `+0x1c`, enemies `+0x18`, lasers `+0x14`. = palette/costume index: effect art bank `DAT_00885a00[team*0x10]` is set from it in `FUN_005f5780` (§5.2). Never read by collision code. |

Collision always rejects pairs whose team fields are equal (object `team` vs player `+0x14`).
Per-team colour used for effects: `DAT_008859f4 + team*0x10` (and `+0x8859f8`, `+0x885a00`).

### 0.4 Per-character "type" index

Every object list dispatches through function-pointer tables indexed by a **type** stored in the
object (called "class" in suguri.md). For character-owned objects **type = the engine character
index stored at player `+0x10`** (= HANDBOOK table id + 3: Sora 3 … Suguri 10 … Sumika-2nd 18;
every per-character table has 3 leading slots). Type 0 of the Effect list is the common effect set.
Tables are filled by `FUN_0049ae40` (`dec/registry.c`). Entries that are 0/unregistered are never
called for that type.

**Naming alignment with suguri.md:** object `id` here = "variant" there; `team` = "side";
`stun` = "stagger"; Hitbox family = "sword" hitboxes; Enemy family = Suguri's "Accel units";
Missile `onHit` (0x881aa8) = "expire", `onKilled` (0x881af8) = "shot-down".
Rendering (CModel API, blend modes, 16-bit angle units in draw calls) is specified in render.md;
gameplay angles in object structs are float radians as described in 0.2.

---------------------------------------------------------------------------------------------------

## 1. How player states spawn objects

A player state handler (T2, see HANDBOOK) calls a **per-character spawn function through a table**
with an *object id* (a per-character move/bullet id like 100, 200, 0x96 …) and the spawn position
and angle. Example, Suguri state 0x133 `FUN_007571b0`:

```
(*DAT_008818a0)(100, p+0x6c, p->angle(+0x84), p->id(+4), 0, p->team(+0x14));   // Beam id 100
FUN_007bc9c0(0xd);                                                              // sound
(*DAT_00881698)(0x456, color, p+0x6c, angle±π/2, p+4, 0, p+0x14);               // visual effect
```

Resource gate before spawning: `FUN_007807f0(p, stateId)` returns 1 and subtracts the move's cost
from gauges `+0xe8`, `+0xec` (and adds heat-gain to `+0xe4`) from the table at
`0x87c4f0 + stateId*0xc` = `{costE8, costEC, gainE4}`; returns 0 (no spawn) if either gauge is
too low. (Gauge semantics → core.md.)

The spawn function **copies a template into a local struct and `push_back`s it into a global
`std::vector`** (or `std::list` for effects), returning the new index (`size-1`). Callers often use
the returned index to patch fields immediately (`FUN_004ef290(idx)+0x40 = …` in Suguri 0x143
`FUN_007598a0`).

### 1.1 Object families (one global container each)

| Family (debug name) | Container | Elem size | Spawn table `base+4*type` | Update table | Draw table | Other tables | Init/Exit |
|---|---|---|---|---|---|---|---|
| **Beam** (bullets) | `std::vector` @ `0x89516c` | 0x98 | `0x881878` | `0x8818c8` → bool | `0x881918` | `0x881968` *onHit/onDestroy(beam)* | `FUN_004aa400` "Beam Init" |
| **Missile** (destructible projectiles) | vector @ `0x895178` | 0xb8 | `0x8819b8` | `0x881a08` → bool | `0x881a58` | `0x881aa8` *onHit(m)→bool keep*, `0x881af8` *onKilled(m)→bool keep*, `0x881b48` *onAbsorb(m, beam)* | `FUN_0065e290` "Missile Init" |
| **Hitbox** ("Flash"; one-frame attack boxes) | vector @ `0x895184` | 0x4c | `0x881b98` | — | — (debug only) | `0x881be8` *onHit(hb, player)* | `FUN_005e00f0` "Flash Init" (debug name "Flash") |
| **Laser** (segment chains) | vector @ `0x895190` | 0x50 | `0x881c38` | `0x881c88` → bool | `0x881cd8` | `0x881d28` *onHit(laser, segIdx, player)* | `FUN_0061cb90` "Laser Init" |
| **Enemy** (summons/parts with HP; hierarchical) | vector @ `0x895150` | 0xec | `0x881558` | `0x8815f8` → bool | `0x8815a8` (by layer) | `0x881648` *onKilled(e)→bool keep* | `FUN_0051a500` "Enemy Init" |
| **Whip** (visual chains) | vector @ `0x895724` | 0x244 | `0x881d78` | `0x881dc8` → bool | `0x881e18` | — | `FUN_007c4da0` "Whip Init" |
| Effect (visual; also dash graze zones) | `std::list` @ `0x89515c` | 0x44 | `0x881698` (type 0 = common `FUN_004ef2c0`) | `0x8816e8` | `0x881738` | — | `FUN_004ea9f0` "Effect Init" |
| Smoke / Particle / Spark / Blur | lists @ `0x895164` / `0x8956e8` / `0x8956f0` / `0x8956f8` | — | see §5 | | | | |
| Players | vector @ `0x8859d0` | 0x178 | — | `0x881310`/`0x881360`+4*`p+0x10` (input dispatch) | | | "Player Init" `FUN_0069cc40` |

Accessors (all `__thiscall` on the container): Beam `FUN_004aa6b0(i)` / count `FUN_004aa840`;
Missile `FUN_0052b950` / `FUN_0056b370`; Hitbox `FUN_004b2eb0` / `FUN_005e1660`;
Laser `FUN_0060d640` / `FUN_0060d660`; Enemy `FUN_004ef290` / `FUN_0051b1a0`;
Whip `FUN_00771b50` / `FUN_00771ce0`; Player `FUN_004e8a10` / `FUN_0054e140`.

**Capacity:** all are `std::vector::push_back` (`FUN_004aaad0` → realloc when full) — no fixed
pool limit exists in the spawn path. The port needs a fixed pool; size it generously
(**UNVERIFIED** maximum simultaneous counts — measure in-game). **Ordering matters** (see 1.3), so
a pool must preserve insertion order and erase-with-shift semantics.

### 1.2 Spawn signatures (per family)

| Family | Signature (as called from player states) | Template init |
|---|---|---|
| Beam | `int spawn(int id, float pos[3]*, float angle, int ownerId, int age0, int team)` — e.g. `(*DAT_008818a0)(100, p+0x6c, p+0x84, p+4, 0, p+0x14)` | `memset 0x98`, `+0 = type`, then `FUN_004ba490` (below), then per-id fields |
| Missile | `int spawn(id, pos*, angle, ownerId, age0, team, p18)` — `(*DAT_008819e0)(100, p+0x6c, a, p+4, 0, p+0x14, p+0x18)` (`0x423498`) | `memset 0xb8`, `+0=type`, `FUN_00685bb0` |
| Hitbox | `int spawn(id, pos*, angle, ownerId, zero, team)` — `(*DAT_00881bc0)(200, pos, a, owner, 0, team)` | inline in each spawn fn (e.g. `FUN_005eca70`) |
| Laser | `int spawn(id, pos*, angle, ownerId, team, p18)` — `(*DAT_00881c60)(100, p+0x6c, p+0x84, p+4, p+0x14, p+0x18)` (`0x423438`) | `FUN_006307d0` |
| Enemy | `int spawn(id, ownerId, team, p18)` — `(*DAT_00881580)(100, p+4, p+0x14, p+0x18)`; caller then writes pos/angle into the returned element | `FUN_0055ee70` |
| Whip | `int spawn(id, ownerId, p3)` (`FUN_007c5d50`) | inline |
| Effect | `spawn(effId, color, pos*, angle, ownerId, age0, team)` | see §5 |

### 1.3 Frame order (from `FUN_006129b0`, the gameplay tick; normal play calls it from
`FUN_006124b0` with `param_1 = 0`)

1. `FUN_006f1720` — all players' per-frame update (state T2 handlers run here → spawns).
   At its start each player's hurtbox is reset (see §4.2).
2. `FUN_005231d0` — Enemy update.
3. Players' x, y, z (+0x6c,+0x70,+0x74) are **truncated toward zero to integers**
   (`(float)(int)v`) — every frame, after enemies, before all other objects.
4. `FUN_004aa460` Beam update → `FUN_0065e2f0` Missile → `FUN_0061cbf0` Laser → `FUN_004fde70`
   Effect → `FUN_007bb2b0` Smoke → `FUN_0068c500` Particle → `FUN_007bd030` Spark →
   `FUN_004d7b10`, `FUN_004d78b0` (Blur) → `FUN_007c4fb0` Whip.
5. Misc: `FUN_004e8150`, `FUN_004e9030`, `FUN_007be6f0`, `FUN_0065dee0`, `FUN_005f8b90`
   (**camera**, §6.3), `FUN_00617a60`, `FUN_00608a00`.
6. If `DAT_00885964 != 2` (2 = round decided), the collision passes, **in this exact order**:

| # | Fn | Pass |
|---|---|---|
| 1 | `FUN_0060bbd0` | Beam → Missile |
| 2 | `FUN_0060b900` | Beam → Enemy |
| 3 | `FUN_0060b610` | Beam → Player |
| 4 | `FUN_0060bf30` | Beam → graze zone (dash trail effects) |
| 5 | `FUN_0060fc30` | Hitbox → Missile |
| 6 | `FUN_0060fa30` | Hitbox → Enemy |
| 7 | `FUN_0060f5a0` | Hitbox → Player |
| 8 | `FUN_00611060` | Missile ↔ Missile |
| 9 | `FUN_00610de0` | Missile → Enemy |
| 10 | `FUN_00610b80` | Missile → Player |
| 11 | `FUN_006113a0` | Missile → graze zone |
| 12 | `FUN_00610360` | Laser truncation by Player body |
| 13 | `FUN_006107e0` | empty (`ret`) |
| 14 | `FUN_006107f0` | Laser → Missile |
| 15 | `FUN_006105d0` | Laser → Enemy |
| 16 | `FUN_006100b0` | Laser → Player |
| 17 | `FUN_0060d690` | Solid Enemy pushes Player out |

7. `FUN_00523370` — remove Enemy objects whose parent (`+0x90`) no longer exists (calls
   `0x881648[type]` then erases).
8. Frame counter `DAT_008858f8++`.
9. Re-hit cooldowns (v > 0x5a0, i.e. always): for every Missile with `+0xb0 > 0`: `--`, at 0 set
   `+0x7c = 1`; every Beam `+0x90 > 0`: `--`, at 0 set `+0x70 = 1`; every Enemy `+0xe4 > 0`:
   `--`, at 0 set `+0x2c = 1`.
10. Later (outside the tick, in `FUN_006124b0` after drawing via `FUN_005f91d0`):
    `FUN_005e0150` **clears the Hitbox vector** (`FUN_004a9f10` if not empty). Hitboxes therefore
    exist for exactly one frame: spawned by a state during step 1, tested in passes 5–7, drawn
    (debug), discarded. (`FUN_006129b0(1)` — used by rollback re-simulation — clears them at the
    end of the tick instead.)

**Iteration semantics (must be reproduced):** each list is walked by index `i = 0; i < size();
i++`, re-reading `size()` every iteration. Objects appended during the walk (spawned by an
update/callback) are **processed in the same walk, same frame**. Removal = `vector::erase(i)`
(order-preserving shift) followed by `i--`.

### 1.4 Per-family update loops

* **Beam** `FUN_004aa460`: `keep = update[type](b)`; if `!keep` erase (no callback); else
  `b->age(+8)++`. (Age is incremented *after* the update, so the first update sees the spawn
  value, normally 0.)
* **Missile** `FUN_0065e2f0`: identical pattern, age `+8`.
* **Laser** `FUN_0061cbf0`: `l->age(+0x20)++` **first**; if `age >= 0` call update, erase on false.
  A negative initial age = delayed start (update skipped while negative).
* **Enemy** `FUN_005231d0`: `keep = update[type](e)`; if false erase; else
  - if `e->parent(+0x90) == -1` (root object): for every enemy `c` with `c->parent == e->id(+0)`:
    `FUN_00528250(c, e, flip)` where `flip = (e->+0x6c byte == 1 && |e->+0x58| > π/2)` (§3.5);
  - `prevPos(+0x4c..+0x54) = pos(+0x40..+0x48)`; `+0x78++`; `+0x80++`.
* **Whip** `FUN_007c4fb0`: `+0xc++` first, then `update[type]`, erase on false.
* **Effect** `FUN_004fde70` (list): `+0x28++` first; if `< 0` skip; else `update[type]`, erase on
  false.

Draw order (gameplay layer, `FUN_005f91d0`): stage bg `FUN_00619ef0`, Smoke(0) `FUN_007bb390(0)`,
Effect(0) `FUN_004f4de0(0)` … then Whip `FUN_007c5080`, Enemy layer 0 and 1 (`FUN_0051e830(n)`
draws enemies whose `+0x1c == n`), Blur `FUN_004d7bc0`, players `FUN_006b1ea0`/`FUN_006b2200`
(**UNVERIFIED** that these two are the player draws), Enemy layer 2, Smoke(1), Missile draw
`FUN_0065e3c0`, Particle `FUN_0068c000`, Laser draw `FUN_0061ccd0`, Beam draw `FUN_004aa530`,
Effect(1) `FUN_004f4de0(1)`, Spark `FUN_007bd350`, `FUN_004d7900`, `FUN_004d7a70`, `FUN_0065e050`,
then debug hitbox overlay `FUN_0060c390` when `DAT_0087c4d9 == 1`.

---------------------------------------------------------------------------------------------------

## 2. Struct layouts

Field names are mine; Ghidra offsets in the first column. "init" = value written by the generic
template initialiser (per-id code may override).

### 2.1 Shape (embedded in every collidable object)

`struct Shape { float halfW, halfH, radius; }` (+ an angle stored elsewhere in the owner object).
`halfW > 0` ⇒ the object has an oriented rectangle of half-extents (halfW along local x, halfH along
local y); `radius > 0` ⇒ it has a circle. Both may be present (the test is a union). See §4.1.

### 2.2 Beam (0x98) — init `FUN_004ba490(b, id, pos*, angle, ownerId, age0, team)`

| Off | Type | Name | init / notes |
|---|---|---|---|
| 0x00 | int | type | char index (= owner `p+0x10`; set by spawn fn before init) |
| 0x04 | int | id | per-character bullet id |
| 0x08 | int | age | `age0` arg (usually 0); `++` after each successful update |
| 0x0c | int | ownerId | player `+4` |
| 0x10 | int | hitType | 1 (passed to damage fn; 1 = dash-avoidable class, §4.4) |
| 0x14 | int | team | |
| 0x18 | int | — | 0 |
| 0x1c | int | serial | `DAT_00885988++` (`FUN_004ba570`) |
| 0x20 | int | link | -1 (**UNVERIFIED** use) |
| 0x24 | vec3 | pos | from arg |
| 0x54 | byte | grazed | 0; set to 1 by graze pass (each beam can be grazed once) |
| 0x55 | byte | destroyOnHit | 1; if 0 the beam pierces (stays after hitting a player/enemy) |
| 0x58 | float | scale? | 1.0 (**UNVERIFIED**) |
| 0x5c | float | alpha | 1.0 (draw alpha; some types fade it and die at ≤0) |
| 0x60 | float | moveAngle | angle arg; direction of travel; also knockback direction on hit |
| 0x64 | float | angle | angle arg; rotation of the hit rectangle and sprite |
| 0x68 | float | speed | per id |
| 0x70 | byte | active | 1; 0 = cannot hit anything |
| 0x74 | Shape | shape | per id |
| 0x80 | float | — | debug draw copies `+0x60` here (`FUN_0060c390`) |
| 0x84 | int | damage | per id |
| 0x88 | float | knockback | speed given to victim in hitstun (§4.6) |
| 0x8c | float | stun | 1.0; added to victim stun accumulator (§4.6) |
| 0x90 | int | rehitTimer | 0; counts down at end of tick; at 0 sets `active=1` |
| 0x94 | byte | rehitFlag | 0; if 1, hitting a Missile/Enemy disables that target for 20 frames (§4.5) |

### 2.3 Missile (0xb8) — init `FUN_00685bb0(m, id, pos*, angle, ownerId, age0, team, p18)`

| Off | Type | Name | init / notes |
|---|---|---|---|
| 0x00 | int | type | char index (= owner `p+0x10`) |
| 0x04 | int | id | |
| 0x08 | int | age | age0; `++` after update |
| 0x0c | int | ownerId | |
| 0x10 | int | hitType | **2** |
| 0x14 | int | team | |
| 0x1c | int | p18 | player `+0x18` |
| 0x20 | int | serial | `DAT_0088598c++` (`FUN_00685cb0`) |
| 0x24 | int | link | -1 |
| 0x28 | vec3 | pos | |
| 0x58 | byte | grazed | set by graze pass |
| 0x59 | byte | — | 0 |
| 0x5c | float | growScale | 1.0 (Suguri sets 0 and grows +0.2/frame to 1; draw scale) |
| 0x60 | float | — | 1.0 |
| 0x64 | int | — | 0 |
| 0x68 | float | moveAngle | angle |
| 0x6c | float | angle | angle (shape/sprite rotation) |
| 0x70 | float | speed | per id |
| 0x78 | int | hp | **60** default; reduced by projectiles; see §4.5 for death thresholds |
| 0x7c | byte | active | 1 |
| 0x80 | Shape | shapeBody | used vs Player, Enemy, graze zones |
| 0x90 | Shape | shapeProj | used vs Beam, Hitbox, Missile, Laser |
| 0xa0 | int | damage | dealt to players/enemies **and** to other projectiles' HP |
| 0xa4 | float | knockback | |
| 0xa8 | float | stun | 1.0 |
| 0xac | byte | absorbs | 0; if 1, beams hitting it call `0x881b48[type](m, beam)` (reflect/absorb, §4.5) |
| 0xb0 | int | rehitTimer | 0 |
| 0xb4 | byte | rehitFlag | 0 |

### 2.4 Hitbox (0x4c) — built inline (example `FUN_005eca70`, Suguri)

| Off | Type | Name | notes |
|---|---|---|---|
| 0x00 | byte | dealsDamage | 1 (if 0 the box only fires the onHit callback) |
| 0x01 | byte | oncePerFrame | 0; if 1, boxes with the same (id, ownerId) hit a player at most once per frame (registry §4.4) |
| 0x04 | int | type | char index (= owner `p+0x10`) |
| 0x08 | int | id | |
| 0x0c | int | — | 5th spawn arg (0) |
| 0x10 | int | ownerId | 4th spawn arg |
| 0x14 | int | hitType | 1 or 3 |
| 0x18 | int | team | |
| 0x1c | vec3 | pos | |
| 0x28 | float | angle | |
| 0x2c | Shape | shape | |
| 0x3c | int | damage | |
| 0x40 | float | knockback | |
| 0x44 | float | stun | 1.0 |
| 0x48 | byte | rehitFlag | 0 |

### 2.5 Laser (0x50) — init `FUN_006307d0(l, id, pos*, angle, ownerId, team, p18)`

| Off | Type | Name | init / notes |
|---|---|---|---|
| 0x00 | int | type | |
| 0x04 | int | id | |
| 0x08 | int | blockMode | 1; **if != 1 the laser is cut off at the first segment touching an enemy player** (pass 12) |
| 0x0c | int | hitType | 3 (1 would make it dash-avoidable) |
| 0x10 | int | team | |
| 0x14 | int | p18 | |
| 0x18 | int | ownerId | |
| 0x20 | int | age | 0; pre-incremented; update skipped while < 0 |
| 0x24 | vec3 | origin | |
| 0x30 | float | angle | segment shape rotation and knockback direction |
| 0x34 | vector<Seg> | segments | cleared and rebuilt by the type's update every frame |
| 0x40 | int | damage | applied **every frame of contact**; types set it to 0 on non-tick frames |
| 0x44 | float | knockback | |
| 0x48 | int | ? | per id (Suguri: 200 / 1000) **UNVERIFIED** |
| 0x4c | byte | stun | 1; converted to float and passed as stun |
| 0x4d, 0x4e | byte | — | 0 |

`Seg` (0x1c): `+0x00 Shape shape; +0x0c float pad; +0x10 vec3 pos` (`FUN_004a9a90` stride 0x1c;
pushed with `FUN_004aa150`).

### 2.6 Enemy (0xec) — init `FUN_0055ee70(e, ownerId, team, p18)` + per-type spawn

| Off | Type | Name | init / notes |
|---|---|---|---|
| 0x00 | int | id | `DAT_00885980++` (`FUN_0055ef90`) — unique; children reference it |
| 0x04 | int | serial | `DAT_00885984++` |
| 0x08 | int | ownerId | |
| 0x0c | int | type | char index (= owner `p+0x10`) |
| 0x10 | int | ? | spawn-specific |
| 0x14 | int | team | |
| 0x18 | int | p18 | |
| 0x1c | int | drawLayer | 1 (0,1,2 — §1.4) |
| 0x20 | byte | solid | 0; 1 = pushes players out (pass 17) **and is ignored by every projectile pass** |
| 0x21 | byte | invulnerable | 0; hits register but HP unchanged (lasers ignore this flag) |
| 0x24 | int | hp | |
| 0x28 | int | maxHp | |
| 0x2c | byte | active | collidable flag (0 = projectiles pass through) |
| 0x30 | Shape | shape | |
| 0x3c | float | — | debug copy of `+0x58` |
| 0x40 | vec3 | pos | |
| 0x4c | vec3 | prevPos | copied from pos after each update |
| 0x58 | float | angle | |
| 0x5c | float | angle2 | (written alongside `+0x58` by spawners) |
| 0x60, 0x64, 0x68 | float | scale x, y, z | 1.0 |
| 0x6c | byte | mirrorChildren | 1 |
| 0x70 | float | ? | |
| 0x78 | int | age | `++` after update |
| 0x80 | int | timer2 | `++` after update |
| 0x88 | int | motionId | 0 = none, 2 = "detached" (attachment skipped) |
| 0x8c | int | motionFrame | |
| 0x90 | int | parentId | -1 = root |
| 0x94..0xbb | int[10] | childSlots | -1 |
| 0xbc, 0xc0 | float | pivot x, y | rotated by the child's own angle, × child scale (`FUN_00528250`) |
| 0xc8, 0xcc | float | offset x, y | rotated by the parent's angle, × parent scale |
| 0xd4..0xdc, 0xe0, 0xe8..0xea | | | 0 |
| 0xe4 | int | rehitTimer | |

### 2.7 Whip (0x244) — `FUN_007c5d50` (Mira, type 6)

`+0 id, +4 type, +8 ownerId, +0xc age, +0x10 ?, +0x14 p3`, then 20 nodes of 0x1c bytes from
`+0x24`. Whips never appear in a collision pass — they are visual; damage is done by hitboxes the
owner spawns (**UNVERIFIED** — confirm in Mira's moveset).

---------------------------------------------------------------------------------------------------

## 3. Movement — shared machinery

There is **no generic movement code in the managers**: every behaviour lives in the per-type
update function (switch on `id`). The managers only provide age counting, removal, and (for
Enemy) parent attachment. The reusable building blocks are:

| Helper | Signature | Behaviour |
|---|---|---|
| straight move | inline | `pos.x += cos(moveAngle)*speed; pos.y += sin(moveAngle)*speed` |
| `FUN_005f0e70` | `turnToward(float* a, float target, float maxStep)` | wraps both; `d = target - a`, then if `d > π`: `d = -(2π - d)`; else if `d < -π`: `d += 2π`. If `|d| < maxStep` → `a = target`; else `a += maxStep * sign(d)` (if `d == 0` and `|d| >= maxStep` nothing). (Homing) |
| `FUN_007804b0` | `float angleToOpponent(player*)` | `atan2(opp.y - p.y, opp.x - p.x)` (special case: opponent char 5 with `+0x164==1`) |
| `FUN_005f1210` | `rotate(vec2*, angle)` | standard CCW rotation (same as `FUN_005f1170`) |
| `FUN_005f1c30` | distance between two vec2 (returns via x87) | |
| `FUN_0061ac10` | `bool inArena(vec2*, margin)` | `x²+y² < (540+margin)²` (§6.1) |
| `FUN_00459fd0` | `float rand(lo, hi)` | from game RNG `0x875b68` (alt RNG `0x876ef0` when `DAT_00a50ef0`); `lo + (u32/2^32)*(hi-lo)` from MT19937 (per suguri.md §5.7). Object updates consume RNG, so update order is sync-critical. |
| `FUN_00459f80` | `int randInt(lo, hi)` | same RNGs |
| `FUN_00528250` | attach child Enemy to parent (§3.5) | |

### 3.1 Example — straight bullet (Suguri Beam id 100; spawn `FUN_004b52a0`, update `FUN_004b67a0`)

Template (v ≥ 0x58a): speed `+0x68 = 21.0` (`DAT_00814154`; old: 16 / `DAT_008140d8`),
damage `+0x84 = 0xd8 = 216` (old 0xb4), shape `{halfW 24 (DAT_00853b8c), halfH 8 (DAT_008140d4),
r 0}`, knockback `+0x88 = 10` (`DAT_00813f4c`), stun 1.0, destroyOnHit 1.
Update: move straight; `if (!inArena(pos, 10)) return 0` (silent removal); spawns trail effects
(`DAT_008816c0` effect 300 every frame; 0x547/0x548 at age 1; particle 1000 while age < 10).
onHit `FUN_004b6570` (id 100): effect 1000, particle 1000 ×10, spark 1000, sound `0x13`.

### 3.2 Example — accelerating / decelerating (Suguri Beam id 300; trail beam)

`speed *= 0.95` (`DAT_00814124`) once age > 10; `alpha(+0x5c) -= 0.05` (`DAT_00813ef4`) once
age > 20; dies when alpha ≤ 0. Template sets `active(+0x70) = 0` and `destroyOnHit = 0` → purely
visual.

### 3.3 Example — homing missile (Suguri Missile id 100; spawn `FUN_0067c2b0`, update `FUN_0067d2a0`)

Template: `speed 14 (DAT_00813a54)`, `hp 60`, `damage 180`, `shapeBody.r = 8`,
`shapeProj.r = 8`, `knockback 10`, `growScale 0`.
Per frame: `growScale = min(1, growScale+0.2)` (`DAT_00853b14`);
age < 50: if `speed > 2` → `speed -= 0.5`; `turnToward(&moveAngle, angleTo(opponent), 0.02 rad)`;
50 ≤ age < 100: if `speed < 10` → `speed += 0.5`; turn rate `0.015 rad`;
age ≥ 100: no steering. `angle = moveAngle`; move straight. If `age > 50 && !inArena(pos,10)`:
call `onHit[type](m)` itself and return 0. (`DAT_00813ed8 = 0.02`, `DAT_00813ed4 = 0.015`,
`DAT_00813a4c = 2`, `DAT_00813f4c = 10`.)

### 3.4 Example — laser (Suguri Laser id 100; spawn `FUN_006289c0`, update `FUN_0062a540`)

Every frame: clear segments; `if age == 16: sound 0x12`; `if age > 99: return 0`;
`radius = (16 <= age <= 60) ? 50 : 0`; push 29 segments `i = 1..29` at
`origin + (cos a, sin a) * i * 60` each `Shape{0,0,radius}` (a chain of circles, 1740 units long).
`damage = (age % 4 == 0) ? (owner in state 0x136 ? 60 : 30) : 0` — i.e. a damage tick every 4th
frame; on the other frames contact still applies hitstun (§4.6) but 0 damage.
Template: `blockMode(+8) = 2` (cut by bodies), hitType 3, knockback 5 (`DAT_00813f40`), `+0x48 = 200`.

### 3.5 Attached objects (Enemy hierarchy) — `FUN_00528250(child, parent, flip)`

Skipped if `child.motionId == 2`. Otherwise:
`(mx,my,ma) = motionId ? FUN_00527ee0(motionId, motionFrame) : (0,0,0)` — keyframed path,
linear interpolation between keys `{t(0..1), angle, x, y}` with total length
`DAT_008855ec + id*0x14` frames and loop flag `DAT_008855f0 + id*0x14`. 32 motion slots are filled
by **hard-coded keyframes in `FUN_00526670`** ("EnemyMotion Init", 89 keyframes — transcribe from
there when a character needs them). With `flip`: `childAngle = parent.angle - ma`,
`mx *= parent.scaleX * -1` (`DAT_00853bb4 = -1.0`), offset x and y both negated; without:
`childAngle = parent.angle + ma`, offset x negated only. Then
`pos = parent.pos + rotate((offset*parentScale) + m, parent.angle) + rotate(pivot*childScale, childAngle)`;
`angle = angle2 = wrap(childAngle)`; `motionFrame++` (wraps by length if looping); recurse into the
child's own children. Field indices as decompiled: offset = `param_1[0x32], [0x33]` (`+0xc8`,
`+0xcc`), pivot = `param_1[0x2f], [0x30]` (`+0xbc`, `+0xc0`) — **UNVERIFIED** which of the two is
"offset" vs "pivot" semantically (the formula above is exact as decompiled: the `+0xc8/+0xcc` pair
is rotated by the parent angle, the `+0xbc/+0xc0` pair by the child angle).

Lasers can be attached by reading an Enemy each frame (Suguri laser 0xd2:
`angle = FUN_0055f060(ownerId)->angle(+0x58)`; `FUN_0055f060` finds an Enemy by `+0` id).
Effects follow players by looking up `FUN_007805f0(ownerId)` each frame (common effect 0xfa).

---------------------------------------------------------------------------------------------------

## 4. Collision

### 4.1 The shape test `FUN_005f1c90(posA, posB, shapeA, shapeB, angleA, angleB) → bool`

Only x,y are used. Steps, in order (first success returns true):

1. **Circle–circle**: if `rA > 0 && rB > 0 && |dx| <= rA+rB && |dy| <= rA+rB &&
   sqrt(dx²+dy²) < rA+rB`.
2. Build rotated rectangle corners (only if `halfW > 0`), via `FUN_005f1170` (CCW rotation):
   A: `(-w,-h), (w,-h), (w,h), (-w,h)`; B: `(-w,-h), (-w,h), (w,h), (w,-h)`; each `+ pos`.
3. **Rect–rect** (both `halfW > 0`): broad phase `|dx|,|dy| <= 1.5*max(wA,hA) + 1.5*max(wB,hB)`
   (`DAT_00814110 = 1.5`). Then for every edge pair, `FUN_005f12e0(p1,p2,q1,q2) =
   cross(p1→p2, p1→q1) * cross(p1→p2, p1→q2)` computed as written there; intersect if it is
   `< 0` for (A-edge vs B-edge) **and** `< 0` for (B-edge vs A-edge). Then containment: B's
   *centre* inside A (triangles 0-1-2 and 2-3-0 of A via `FUN_005f1820`), A's centre inside B.
   (Only centres are tested for containment, not corners.)
4. **Rect A – circle B** (`wA > 0 && rB > 0`): any corner with `dist < rB`; or any edge whose
   line distance `|n·c + k| / |n| <= rB` while the projection of the circle centre lies strictly
   inside the edge (both dot products `> 0`); or circle centre inside A (triangles).
5. **Rect B – circle A** symmetric.

`FUN_005f1820(a,b,c,p)`: rejects degenerate triangles; point-in-triangle by comparing each edge's
side of `p` with the centroid's side (`FUN_005f3440`), all `>= 0`.

Shape semantics: `halfW == 0` ⇒ no rectangle even if `halfH > 0`; `radius == 0` ⇒ no circle.
`{0,0,0}` never collides.

### 4.2 Player hurtbox

At the start of each player's update (`FUN_006f1720`): `shape(+0x5c) = {0, 0, 16.0
(DAT_008140d8)}`, `+0x68 = 0`, `hittable(+0xa2) = 1`, friction `+0x9c = 0.5`, arenaClamp
`+0xa0 = 1`. State handlers may change any of these for the frame. Hurtbox angle = player `+0x84`.
While the player's **shield timer `+0xf8 > 0`**: shape forced to `{0,0,0}` then (for `+0xf8 >= 21`)
`radius = +0x100 * +0xfc * 0.8` (`DAT_008145e8`), `+0x100` ramping +0.1/frame to 1 (≤ 20: radius 0
and `+0x100 = timer/20`); `+0xf8--` each frame. (Shield semantics → core.md.)
When `DAT_0088596c == 1` (round over flag) `hittable = 0`.

### 4.3 "Dash invulnerability" flag (used by several passes)

`dashing = (state == 2) || (state == 0 && speed(+0x98) > 0)`; but `dashing = false` if
`+0xf8 > 0` (shield up). State 2 = dash (`FUN_006af5e0`).

### 4.4 Passes vs players

All require `player.hittable(+0xa2) != 0`, `object.team != player.team`, and stop entirely when
`DAT_00885964 == 2` becomes true mid-pass (KO). "Damage" = `FUN_00780ab0` (§4.6), "Hitstun" =
`FUN_006b19a0`.

**Beam → Player `FUN_0060b610`.** For each player, for each beam with `active` and
`!(v>=0x58a && type==8 && id==200)` (Nath beam 200 never hits players):
if `FUN_005f1c90(beam.pos, p.pos, beam.shape, p.shape, beam.angle(+0x64), p+0x84)` and **not
dashing**: if `p+0xf8 == 0` → `if (Damage(p, beam.damage, beam.hitType, beam.stun))
Hitstun(p, beam.moveAngle(+0x60), beam.knockback)`; then `destroy = beam.destroyOnHit`.
If `destroy`: call `onHit[type](beam)`, erase, and **stop checking further beams against this
player this frame** (`break`). (So with a shield up the beam is consumed with no damage; a dashing
player is skipped entirely — the beam passes through.) Piercing beams (`destroyOnHit=0`) hit again
every frame unless their update clears `active`.

**Hitbox → Player `FUN_0060f5a0`.** First clears the per-frame hit registry `std::list` @
`0x895730` (`FUN_00606f00`). For each player with `hittable && +0xf8 < 1` (shielded players are
skipped entirely), each hitbox with `team != p.team` and `!(hb.hitType == 1 && dashing)`:
if `hb.oncePerFrame` and the registry already holds `(hb.id, hb.ownerId)` (`FUN_0060fe80`) → skip.
On overlap (`hb.pos, p.pos, hb.shape, p.shape, hb.angle, p+0x84`): call
`onHit[hb.type](hb, p)` (`0x881be8`); then if `hb.dealsDamage`: `if (Damage(p, hb.damage,
hb.hitType, hb.stun)) Hitstun(p, hb.angle(+0x28), hb.knockback)`; record "hp was > 0 before and
is 0 after" for an achievement check (`FUN_007ca890(0x17)` — Mira(3) KO'd by Sham(4) hitbox 500;
not gameplay); if `oncePerFrame` push `(id, ownerId)` into the registry (`FUN_00610090`).
Hitboxes are not removed on hit (they vanish at frame end anyway).

**Missile → Player `FUN_00610b80`.** No dash check (missiles hit dashing players). Uses
`m.shapeBody(+0x80)` and `m.angle(+0x6c)`. On overlap: if `p+0xf8 == 0` → `if (Damage(p, m.damage,
m.hitType(=2), m.stun)) Hitstun(p, m.moveAngle(+0x68), m.knockback)`; then always call
`onHit[type](m)` (`0x881aa8`); if it returns false erase and `break` (next player).

**Laser → Player.** Two passes, using every segment `s`:
* `FUN_00610360` (block): for laser with `blockMode != 1` and `!(hitType == 1 && dashing)`: find
  the first segment overlapping the player (`s.pos, p.pos, s.shape, p.shape, l.angle, p+0x84`);
  erase segments `[k, end)`. Only one laser is cut per player per frame (`break`).
* `FUN_006100b0` (damage): for laser with `!(hitType == 1 && dashing)` (no `blockMode` test): first
  overlapping segment `k`: if `p+0xf8 == 0` → **`Hitstun(p, l.angle, l.knockback)` first, then
  `Damage(p, l.damage, l.hitType, (float)l.stun)`** (hitstun unconditionally, return value
  ignored); then `onHit[type](l, k, p)` (`0x881d28`). One segment per laser per frame.

### 4.5 Passes between objects (projectile cancelling)

Generic rules: a projectile's `damage` is subtracted from the other object's `hp`. Beams and
hitboxes have no HP; Missiles and Enemies do.

| Pass | Shapes used | Effect on hit |
|---|---|---|
| Beam→Missile `FUN_0060bbd0` | beam `shape,+0x64` vs missile `shapeProj(+0x90),+0x6c`; requires `m.active(+0x7c)`, `beam.active`, teams differ | if `m.absorbs`: `0x881b48[m.type](m, beam)`; beam erased **without** its onHit. Else `m.hp -= beam.damage`; if `beam.rehitFlag`: `m.active = 0, m.rehitTimer = 20`; if `m.hp < 0` → missile dies: `onKilled[m.type](m)` (`0x881af8`), erase if false; the killing beam **survives** and the beam loop stops for this missile. Else beam is destroyed: `onHit[beam.type](beam)` + erase, continue with next beam. |
| Beam→Enemy `FUN_0060b900` | beam vs `e.shape(+0x30)`, `e.angle(+0x58)`, `e.pos(+0x40)`; requires `e.active(+0x2c)`, `!e.solid` | if `e.invulnerable`: guard effect `FUN_0065dff0(3, beam.pos, e.ownerId)`; else `e.hp -= beam.damage` (+ rehit rule: `e.active=0, e.rehitTimer=20`). If `e.hp < 1` → stop (enemy dies after loop: `onKilled[e.type](e)` (`0x881648`), erase if false). Else if `beam.destroyOnHit`: beam onHit + erase. |
| Hitbox→Missile `FUN_0060fc30` | hb shape vs `m.shapeProj` | `m.hp -= hb.damage` (+ rehit rule via `hb.rehitFlag(+0x48)`); if `m.hp < 0` → `onKilled`, erase if false. |
| Hitbox→Enemy `FUN_0060fa30` | | invulnerable: nothing; else `e.hp -= hb.damage` (+ rehit rule); if `hp < 1` → onKilled. |
| Missile↔Missile `FUN_00611060` | both `shapeProj(+0x90)`, angles `+0x6c`; pairs (i, j>i), teams differ, both active | **both** `hp -= other.damage`; a missile with `hp < 1` → `onHit[type]` (`0x881aa8`) and erase if false (for `i`: onHit if `j` also died else onKilled). |
| Missile→Enemy `FUN_00610de0` | `m.shapeBody(+0x80)` vs enemy | invulnerable: guard effect 3 at `m.pos`; else `e.hp -= m.damage`. Missile then `onHit` (erase if false). Enemy `hp < 1` → onKilled. |
| Laser→Missile `FUN_006107f0` | segment vs `m.shapeProj(+0x90)` | `m.hp -= l.damage`; `hp < 0` → onKilled. One segment per laser. |
| Laser→Enemy `FUN_006105d0` | segment vs enemy | `e.hp -= l.damage` (**ignores `invulnerable`**); `hp < 1` → onKilled. |

There is **no Beam↔Beam, Beam↔Laser, Hitbox↔Beam, or Laser↔Laser interaction.**
**Reflection** is implemented through `absorbs` missiles: e.g. Saki type 8 `FUN_00673790`: for
reflector ids 200/0xc9 → effect 0x516, spawns its own Beam 300 at the absorbed beam's position
with the reflector's angle/owner/team, sound `FUN_007bcaf0(0x25,3)`; Sumika type 15
`FUN_0066d190` likewise (ids 300/0x136).

### 4.6 Damage and hitstun entry points

`bool FUN_00780ab0(player* victim, int damage, int hitType, float stun)` — **the damage
application function** (formula details → core.md). What it touches: `+0xd0` HP, `+0xdc`,
`+0xd8`, `+0x140 = 30`, `+0x148 += dmg`, `+0x14c++`, `+0x144 += stun`; heat-related scaling for
`hitType` 1 or 3 using `+0xe0`; `+0xe4 += dmg*0.0001*+0xf0`; KO → state 6 and
`DAT_00885964 = 2`; combo counters on the attacker (`FUN_00780480(victim.team)` `+0x11c`, `+0x128`,
`+0x130 = 120`). **Returns `+0x144 >= 1.0`** (stun accumulator full) — callers apply hitstun only
when true (except lasers). `+0x144` resets to 0 when `+0x140` (30-frame window) runs out while
not in state 1 (`FUN_006f1720`).

`void FUN_006b19a0(player* p, float angle, float speed)` — **hitstun/knockback**: unless
`p+0xa9 == 1` (super armour, **UNVERIFIED** name): `state(+0x150) = 1; timer = 0;
+0x84 = wrap(angle + π)` (face the attacker), `+0x88 = angle`, `+0x98 = speed`, `+0x94 = angle`.
Movement then comes from the generic player motion in `FUN_006f1720`
(`pos += (cos,sin)(+0x94) * +0x98` and `+0x98 -= +0x9c` friction each frame) — core.md.

`FUN_006b1d70(src, float range, float speed)` — proximity knock: if the opponent is alive,
hittable, `+0xa9 == 0` and within `range` of `src`, put it in state 1 pushed away from `src`
(`+0x94 = atan2(src-opp) + π`), sound `0x19`. Callers: `FUN_006af880` (common state 1 T2),
`FUN_005e8430`, `FUN_006c5970`, `FUN_006c7b80`, `FUN_006dccb0`, `FUN_0074a5b0`, `FUN_0076fac0`,
`FUN_00779f00`, `FUN_007867c0`.

**No hitstop/freeze** exists in this path: the victim changes state immediately and every object
keeps updating. (`DAT_009b11c0`, which skips `FUN_006129b0`, is the netplay wait flag set by
`FUN_00607220/00607280`.)

### 4.7 Graze (dash trail zones)

* In dash state 2 (`FUN_006af5e0`), every frame with `timer % 5 == 0` the player spawns common
  effect **id 100** at its position with its angle (`(*DAT_00881698)(100, 0, p+0x6c, p+0x84, p+4,
  0, p+0x14)`). Its update (`FUN_004f3e70`, ids 100–103): `+0x30 -= 0.025` (`DAT_00813edc`),
  `+0x2c += 0.08` (`DAT_00813f00`), id re-rolled to `100 + (randInt(0,400) & 3)` (**consumes the
  game RNG every frame**), dies when `+0x30 <= 0` → in float32, 1.0−40·0.025 is still > 0 (≈+2.6e-7) and the 41st update makes it ≤ 0, so the zone survives 40 updates and is present in **40 frames** of collision passes (spawn frame included), removed in its 41st update (checked by simulating float32 subtraction).
 
* Pass 4 `FUN_0060bf30` / pass 11 `FUN_006113a0`: for each Effect with `type(+0)==0` and `id`
  in 100..103 (zone owner = effect `+0x0c`, team `+0x10`, pos `+0x18`, angle `+0x38`), the zone
  shape is the **fixed rectangle `{halfW 20 (DAT_008140fc), halfH 50 (DAT_008140ec), r 0}`** at the
  effect's angle. Each beam (`!grazed`, active, other team) / missile (`!grazed(+0x58)`, active,
  `shapeBody`) overlapping it: sound `FUN_007bcaf0(9,3)`, `grazed = 1`, effect
  `FUN_0065dff0(1, zone.pos, zone.owner)`, and the zone's owner player gets
  `+0xe4 += damage * k * +0xf0` (k = 0.00015 `DAT_0082007c` for beams, 0.0002 `DAT_00814f10` for
  missiles) and `FUN_007808a0(owner, damage, 1)`: `hp(+0xd0) = min(+0xdc, hp + (int)(damage*0.1))`,
  `+0xd8 = hp`. Only the first zone per object counts (`break`); a projectile can be grazed once.

### 4.8 Solid enemies push players `FUN_0060d690`

For each player (the loop **returns** for all players if the current one is in state 6, is not
hittable, or has `+0xa9 == 1` — bug-compatible): for each Enemy with `solid && active &&
e.ownerId != p.id`, with the player treated as a circle of radius 1 (`{0,0,1.0}`):
* if `e.shape.r > 0` and overlapping: move the player to `e.pos + (cos,sin)(atan2(p-e)) * r`
  (pushed onto the circle edge).
* if `e.shape.halfW > 0` and overlapping: compute the side using `FUN_0060e110(p,e)`, then project
  the player onto the rectangle's edge lines (`FUN_005f15a0` line intersection, `FUN_005f32a0`
  side test, constant 1000 `DAT_00814408` for the line extents and 100 `DAT_00853b90`) and place it
  at `edge + normal * (halfH or halfW + 4 (DAT_00853b68))`. **UNVERIFIED** exact geometry of
  `FUN_0060e110`/`FUN_005f15a0`/`FUN_005f32a0` — only needed for characters with solid summons.

### 4.9 Multi-hit summary

| Object | How repeated hits happen |
|---|---|
| Hitbox | Lives one frame. The attacking state decides on which frames to spawn boxes. Within one frame, `oncePerFrame` dedups by `(id, ownerId)`. |
| Beam | Destroyed on first player hit unless `destroyOnHit = 0`; piercing beams hit every overlapping frame unless the type update toggles `active`. `rehitFlag` + `rehitTimer` (20 frames) applies only to Missile/Enemy targets. |
| Missile | Per-type `onHit` decides whether it survives. |
| Laser | Contact every frame; damage ticks are made by the type writing `damage = 0` on off-frames. |
| Any vs player | Knockback only when the victim's stun accumulator `+0x144` reaches 1.0 (30-frame window). |

---------------------------------------------------------------------------------------------------

## 5. Sound and visual-effect APIs

Rendering primitives (CModel draw calls, blend modes, 16-bit angle units
`angle16 = rad·65536/2π` via `FUN_0045a0c0`) are specified in **render.md**; this section only
says what triggers what. "B1/B2" = blend mode 1 (SRCALPHA, INVSRCALPHA) / 2 (SRCALPHA, ONE).

### 5.1 Sound effects

**Loading** (`FUN_007bc080`, from SoundEffect Init `FUN_007bbf10`): 0x3d (61) slots,
`int seHandle[61]` @ `0x9b99a0`, all initially -1. `FUN_005157d0(archive)` selects
`.\se.dat` / `se2.dat` / `se3.dat`; `FUN_00515800(name, voices)` loads one sample.
Archive format (verified by parsing all three files): consecutive records
`u32 dataSize; char name[] (NUL-terminated, uppercase); u32 fmtSize (=18); WAVEFORMATEX; PCM data`.
Lookup uppercases the name (`CharUpperBuffA`); on a miss the loader skips `dataSize+0x12`.
Voices = `abs(n)` (max 10) static buffers per id, used round-robin.
Volume per id: `-(int)(50 - 50*t[lvl])*100` hundredths of dB (50 = `DAT_008140ec`;
`t` @ `0x86c890` = {0, .3, .5, .6, .65, .7, .75, .85, .9, .95, 1.0}; `lvl = DAT_00881eaa`, 0 = SE off).

| id | file | v | id | file | v | id | file | v |
|---|---|---|---|---|---|---|---|---|
| 0x00 | sh_decide.wav | 2 | 0x15 | crash17.wav | 3 | 0x2a | A5_01029.wav | 1 |
| 0x01 | sh_cancel.wav | 1 | 0x16 | hit_s.wav | 3 | 0x2b | tm2_sonic004.wav | 1 |
| 0x02 | cursor5.wav | 3 | 0x17 | hit_f.wav | 2 | 0x2c | tm2_sonic005.wav | 1 |
| 0x03 | error3.wav | 1 | 0x18 | hit.wav | 3 | 0x2d | rock.wav | 2 |
| 0x04 | pi38.wav | 2 | 0x19 | hit_f.wav | 2 | 0x2e | WindF@11.wav | 2 |
| 0x05 | decide11.wav | 1 | 0x1a | tm2_bom002.wav | 3 | 0x2f | power35.wav | 3 |
| 0x06 | up01.wav | 2 | 0x1b | explosion1.wav | 2 | 0x30 | synth-sweep2.wav | 1 |
| 0x07 | high_a.wav | 2 | 0x1c | bomb1.wav | 1 | 0x31 | whip.wav | 2 |
| 0x08 | air01.wav | 2 | 0x1d | gun12.wav | 3 | 0x32 | nori_ge_j_jet01.wav | 1 |
| 0x09 | power36.wav | 2 | 0x1e | tm2_bom003.wav | 2 | 0x33 | synth-sweep1.wav | 1 |
| 0x0a | pi.wav | 2 | 0x1f | swing.wav | 3 | 0x34 | bomb2.wav | 2 |
| 0x0b | eco03.wav | 2 | 0x20 | swing_h.wav | 2 | 0x35 | WindA@08.wav | 2 |
| 0x0c | don11.wav | 1 | 0x21 | swing_d.wav | 2 | 0x36 | rain1(fade).wav | 2 |
| 0x0d | _sen_mi_b_raifuru.wav | 3 | 0x22 | burst00.wav | 3 | 0x37 | tornade.wav | 1 |
| 0x0e | tm2_gun000.wav | 3 | 0x23 | swing_d.wav | 2 | 0x38 | chosen at runtime from se.dat (`FUN_004e5190` → `FUN_007bccb0`) — **UNVERIFIED** name | 1 |
| 0x0f | laser02.wav | 3 | 0x24 | swing_h.wav | 2 | 0x39 | smoke02.wav (se2.dat) | 3 |
| 0x10 | laser03.wav | 2 | 0x25 | reflect.wav | 3 | 0x3a | BuildingMeterLvl1.wav (se3.dat) | 3 |
| 0x11 | tm2_laser000.wav | 1 | 0x26 | jya05.wav | 2 | 0x3b | BuildingMeterLvl2.wav (se3.dat) | 3 |
| 0x12 | gun11_r.wav | 2 | 0x27 | explosion1.wav | 1 | 0x3c | BuildingMeterLvl3.wav (se3.dat) | 3 |
| 0x13 | don16.wav | 3 | 0x28 | chain.wav | 3 | | | |
| 0x14 | bom06.wav | 3 | 0x29 | sen_ge_gun04.wav | 2 | | | |

All are in se.dat unless noted. "v" = voices.

**Runtime API**

| Fn | Signature | Behaviour |
|---|---|---|
| `FUN_007bc9c0` | `se_play(id)` | Plays iff SE on, handle != -1, `(DAT_008858f1 \|\| DAT_00881f5c ∉ {2,3})`, `cooldown[id]` (@`0x9b9ad4`) < 1 and `notYetThisFrame[id]` (@`0x9b9a94`) != 0; then `FUN_00517420(h,-1)`, clears the frame byte, cooldown = 2. |
| `FUN_007bcaf0` | `se_play_cd(id, cd)` | same but needs cooldown == 0 and sets cooldown = cd (graze: `(9,3)` = power36.wav) |
| `FUN_007bcc00` | per-frame tick | frame bytes reset to 1, positive cooldowns decremented; once per frame in main loop `FUN_006325f0` |
| `FUN_007bcc50` | reset | all cooldowns = 0 |
| `FUN_007bcc90` | `se_stop(id)` | stop + rewind all voices |
| `FUN_007bccb0` | `se_reload(id, name, archive)` | replace a slot (1 voice) |
| `FUN_007bcd00` | `se_is_playing?(id)` | **UNVERIFIED** |

Playback (`FUN_00517420`): voice = round-robin index `0x899ae8[h]`; Stop, rewind, Play
(non-looping); advance mod voice count (all busy ⇒ the oldest voice restarts). **No pan, no pitch,
no priority, no global channel cap.** Dedupe: each id at most once per frame; `se_play` spacing ≥ 2
frames; `se_play_cd(id,n)` ≥ n frames. When `DAT_00885870 == 2` during re-simulation the play is
only counted (`0x885a8c[id]` vs `0x893eac[id]`) — rollback-only, not needed for the port.

### 5.2 Visual effects — entry points

| API | Container / record | Signature | Notes |
|---|---|---|---|
| Effect `(*(0x881698+4*type))` | `std::list` @ `0x89515c`, 0x44 | `void* spawn(id, variant/colour, pos*, angle, ownerId, age0, team)` | type 0 = common `FUN_004ef2c0` (update `FUN_004f3e70`, draw `FUN_004eff30`); types 3..18 per character; returns 0 for an unknown id |
| Particle `FUN_0068b8a0` | list @ `0x8956e8`, 0x38 | `(kind, colour, pos*, angle, owner, age0, count)` | 3D sphere models (`/particleMdl/`) |
| Spark `FUN_007bd5e0` | list @ `0x8956f0` | `(kind, colour, maxLen, count, angle, pos*, owner)` | polyline trails |
| Smoke `FUN_007bb450` | list @ `0x895164`, 0x38 | `(kind, pos*, dir, p4, p5, bank)`; tables spawn `0x881788`, update `0x8817d8`, draw `0x881828` | only type 9 (Star Breaker) registered |
| Message `FUN_0065dff0` | list @ `0x895718`, 0x18 | `(kind, pos*, owner)` | text popups: 0 msg_canl, 1 msg_pass (graze), 2 msg_chrg, 3 msg_gurd (guard) |

All are `push_back` with no cap and are processed in insertion order; the age is pre-incremented and
records with age < 0 stay dormant (Effect loop `FUN_004fde70`).

**Effect record (0x44):** `+00 type, +04 id (some ids rewrite it to animate), +08 variant/colour,
+0c ownerId, +10 team, +14 drawLayer (1 or 0), +18 pos[3], +24 =1 (unused), +28 age, +2c scale,
+30 alpha, +34 moveDir, +38 rotation, +3c speed/aux, +40 aux2`. `FUN_004f4de0(layer)` draws
records with `+0x14 == layer`.

**2D colour table** @ `0x87e138`, stride 0xc (r,g,b), filled in `FUN_0049ae40`:
0 (80,90,FF) · 1 (FF,90,80) · 2 (80,FF,80) · 3 (FF,FF,90) · 4 (E1,80,FF) · 5 (FF,80,E1) ·
6 (90,E1,FF) · 7 (FF,E1,80) · 8 (80,FF,E1) · 9 (E1,FF,80) · 10 (80,80,FF) · 11 (FF,A4,A4) ·
12 (20,20,20) · 13 (FF,FF,FF).
Per-team colour slots `0x8859f4 + 0x10*team + 4*k` (k 0..2) = `stats[idx].colour[palette][k]`
(@ `0x881f8c + idx*0x2dc + palette*0xc + 4k`), set in `FUN_005f5780`. Suguri's palettes are {0,5,0},
{2,1,0} and {0,5,0}. Character art bank = `0x885b80 + bank*0x3888 + spriteId*0x18` with
`bank = DAT_00885a00[team*0x10]` = player `+0x18`. **So player `+0x18` is the palette/costume
index** (resolves §0.3).

**Common textures** (`FUN_004e9820`, folder `effectMdl/`), globals `0x0087fxxx`: fc98 bomb, fc9c
hyper, fca0 gaze, fca4 halo, fca8 circle, fcac/fcb0 spread0/1, fcb4–fcc0 diffuse0–3, fcc4/fcc8
slash0/1, fccc/fcd0 explosion0/1, fcd4–fce0 burst0–3, fce4/fce8 impact0/1, fcec/fcf0 impactd0/1,
fcf4+4i smoke_0i (i<8), fd14+4i gray_smoke_0i (i<4), fd24+4i flamer_0i (i<6), fd3c+4i flameb_0i
(i<6), fd54+4i blade00–31, fdd4+4i elect00–03. Beam textures (`beamMdl/`, `FUN_004aa200`): fde4
beamsphere, fdf0 beamemit, fdf4 beamemitb.

**Common effects (type 0).** Columns: init (scale, alpha, +3c, layer); per-frame update and death;
draw (texture, half-size before ÷zoom, blend). The full source-rect arithmetic per id is in
`FUN_004eff30`; transcribe it from there when implementing a given id.

| id | init | update / death | draw |
|---|---|---|---|
| 100→101–103 (dash trail = graze zone, §4.7) | .5, 1, 0, 1 | alpha −.025, scale +.08, id = 100+(rand(0,400)&3); dies at alpha ≤ 0 | **player 0's** bank and sprite `stats[P0].+0x3c` (Suguri 0x12f), even when spawned by player 1 (literal code at `0x4f01a8`); frame f = (id&3) + {0 if a≥.5, 4 if .35–.5, 8 if .2–.35, 12 if <.2}; src ((f&7)·64, (f>>3)·250, 64×250); half 10s×60s; white a·255; B2 |
| 200 / 0xd2 | 0, 1, rand(100,300) | scale +.03, alpha −.03, pos += dir(+38)·(+3c)/20 | hyper src (0,0,255,29) / (0,30,255,59), half (+3c·s)×12, B2 / B1 |
| 0xfa | 0, 1, 1 | follows owner; scale += +3c, +3c ×= .85; after age 9: alpha −.01 | halo, half 36s, B2 |
| 500 / 0x1f5 | 1, 1, 1, layer 0 | scale +.01, alpha −.02, dies at age ≥ 32 | bomb, 2 passes (B1 then B2), f = age/4 |
| 600, 0x262, 0x26c, 0x28a | 1, 1, 0 | scale −.025, alpha −.025, dies at scale ≤ 0 | bomb src (0,384,127,511) / (128,384,255,511), half 240s×{1, .5, .3, 1.5}, B2 |
| 900 | 1, 0, .1 | scale −.01, alpha +.01 | gaze, half 1024s, B2 |
| 0x385 | 0, 1, .1 | scale +.015, alpha −.025 | hyper (0,256,255,511), half 1024s, B2 |
| 0x38e / 0x38f | 0, 1, rand(7.5,15) / rand(3.5,7.5) | scale += +3c/750; alpha −.025 / −.0125; pos += dir·(+3c) | hyper (0,0,255,29), half 512s×32s, B2 |
| 1000 / 0x3f2 | .75, 1, 1 | scale +.1, alpha −.04, dies at age ≥ 24 | spread[v>>3], half 32s / 24s, B2 |
| 0x44c / 0x456 | 1, 1, 8.5 / 5 | move; scale +.075; alpha −.05 | diffuse[v>>2], half 48s×12s / 32s×8s, x-mirrored, B2 |
| 0x514 / 0x515 / 0x516 | .5, 1, 1 | scale += .2·(+3c); alpha −= .075·(+3c) | impact[v>>3], 2 passes (a·128 B1, a·255 B2), half 48s / 32s / 16s |
| 0x546 / 0x547 / 0x548 | as 0x514 | as 0x514 | as 0x514 with half-width ÷3 |
| 2000 / 0x802 | 1, 1, —, layer 0 | alpha −.05 | smoke_0[min(age/8,7)], half 24s / 32s×12s, B1 |
| 0x834 | 1, 1, 1, layer 0, dirs rand(±π) | move; scale +.05; alpha −.02; when age%3==0, age = rand(0,100) | gray_smoke, half 32s, B1 |
| 0x9c4 / 0x9c5 / 0x9c6 | 0, 1 | scale +.05; dies at scale ≥ 1 | slash[v>>3], 2 passes |
| 0xa28–a2f, a32–a39, a64–a6b, a82–a89 | .5, 1 | scale +.075 (a28) or +.05 (others); alpha −.05; id++ on even ages (wraps within its 8) | blade[min(age/4,3)+(id%10)·4], colour a \| colourTable[v], B1 |
| 0xa8c / 0xa8d | 0, 1, 1 | (a8c follows owner) scale += +3c, +3c ×= .9; after age 4: alpha −.05 | gaze, half 48s, B2 |
| 0xaf0 | 0, 1, .5, dirs rand(±π) | as a8c | circle, half 40s (v ≥ 0x58a), B2 |
| 3000 / 0xbc2 | .25, 1, 1 | scale +.025; alpha −.05; pos += dir·2 | flamer / flameb, 2 passes, half 60s |
| 0xc1c | 1, 1 | +38 = rand(±π); alpha −.05; scale +.1; pos = player(team) − 50·dir | elect0[age&3], half 24s, B2; alpha byte wraps for a∈[.25,.75) (literal code behaviour) |

The random draws in this table consume the game RNG, so they must be reproduced in order.

**Per-character effect tables** (spawn / update / draw), type = character index:
3 `5070d0/508d60/507530` · 4 `4eab40/4edf10/4eb540` · 5 `5142a0/514b70/5143e0` ·
6 `4fdfa0/4fed10/4fe220` · 7 `504b80/506640/504f50` · 8 `500ae0/502f80/500fb0` ·
9 `5094a0/50bb10/509ac0` · 10 `50c820/50e9e0/50cd50` · 11 `503530/504630/503900` ·
12 `4f7a00/4f9540/4f7d20` · 13 `4fef30/5004c0/4ff3e0` · 14 `4f9cb0/4fb350/4fa1f0` ·
15 `4fb8d0/4fd940/4fbcf0` · 16 `4f4ea0/4f70c0/4f55f0` · 17 `511880/513c60/511e00` ·
18 `50ee70/511240/50f3e0`. They use the same record and signature, with art from the owner team's
bank. Worked example (Suguri, type 10): see suguri.md §5.6. For instance, id 300 (the visible rifle
bolt) draws `beamemitb`/`beamemit` quads, 200/0xd2 draw `sg_moon` (sprite 0x147), 100 draws
`sg_blade` (0x148) and 600 draws `sg_slash` (0x141).

**Particle** (`FUN_0068b8a0`). Record: `+00 kind (0x3e9/0x3ea are stored as 1000), +04 colour,
+08 age, +0c owner, +10 alpha, +14/+18 spin, +1c pos, +28 dir, +2c speed,
+30 growth = rand(−1,1)·.02, +34 scale = 1`.

| kind | alpha | dir | speed | fade |
|---|---|---|---|---|
| 1000 | 1 | angle + rand(±1.2) | rand(2.5, 6.5) | from age 25: alpha −.04 |
| 0x3e9 | 1 | angle | rand(2.5, 6.5) | as 1000 |
| 0x3ea | 1 | rand(±π) | rand(2.5, 6.5) | as 1000 |
| 0x44c / 0x456 | .5 | angle + rand(±.5) | rand(.5, 3.5) | from age 10: alpha −.05 |
| 0x4b0 / 0x4ba | .5 | angle + rand(±.5) | rand(.5, 3.5) | from age 4: alpha −.1 |

Update `FUN_0068c500`: pos += dir·speed, scale += growth, removed at alpha ≤ 0.
Draw `FUN_0068c000`: model `sph<colour>b` (B1, colour (a·.25, a, a, a)), then `sph<colour>` (B2).
Colours 0..11 = blue, red, green, yellow, violet, magenta, aqua, orange, emerald, ygreen, cobalt,
carmine.

**Spark** (`FUN_007bd5e0`). Record: `+00 kind, +04 colour, +08 maxLen, +0c age, +10 owner,
+14 alpha = 1, trail list, +20 heading, +24 target heading, +28 turn rate, +2c speed`.

| kind | heading | target | turn | speed |
|---|---|---|---|---|
| 1000 | angle + rand(±π/2) | angle | .02 | rand(−2,5)+10 |
| 0x44c | angle + rand(±π/2) | angle | .01 | rand(−2,4)+6 |
| 0x4b0 | angle + rand(±π/2) | angle | .01 | rand(−2,2)+3 |
| 2000, 3000 | angle + rand(±π/2) | = heading | .02 | rand(−2,5)+10 |
| 0x7da, 0xbc2 | angle | = heading | .02 | rand(−2,5)+10 |

Update `FUN_007bd030`: kinds 3000/0xbc2 jitter with 50% chance per frame (heading +=
rand(−10,10)/30, speed ×= rand(.75,1.15)); push a new head point at previous head + dir·speed; pop
the tail if longer than maxLen or age < 2; turn toward the target (`FUN_005f0e70`); alpha −.025;
age++. Draw `FUN_007bd350`: B2, 1-px line segments, segment i alpha 255·a·(1 − (i−1)/N), RGB table
@ `0xa4e2c8`.

**Smoke** (type 9, Star Breaker only): update `FUN_007bbb20`, draw `FUN_007bb5b0` (model
`sb_flamer%d`) — belongs to the Star Breaker spec.

**Message popups** `FUN_0065dff0(kind, pos, owner)`: textures `0x87fe20[kind]` (`/msgMdl/`).
Update: y += 1.5/frame, removed when timer > 40. Draw: half 50×50 for t < 21, then
(50+(t−20)·1.5) × (50−(t−20)·2.5); alpha 0xE1 on even t, 0x60 on odd t; B1. Kind 4 is spawned in
two places but not handled by update or draw (**UNVERIFIED** cleanup).

---------------------------------------------------------------------------------------------------

## 6. Stage, bounds, camera

### 6.1 Arena

* Circular arena, centre (0,0), **radius 540** (`DAT_0081412c = 540.0`).
* Players: at the end of each player update, if `+0xa0 == 1` and `x²+y² > 291600.0`
  (`_DAT_008257a0`, double = 540²): `a = atan2(y,x); x = cos(a)*540; y = sin(a)*540`
  (`FUN_006f1720`).
* Objects: no generic bound — each type's update calls `FUN_0061ac10(pos, margin)` (inside iff
  `x²+y² < (540+margin)²`) and returns 0 (silent removal; Beam/Laser) or calls its own `onHit`
  first (Suguri missile). Typical margins: 10 (`DAT_00813f4c`), 30 (`DAT_008140e8`).

### 6.2 World → screen

`FUN_00619d90(vec*)`: `sx = W/2 + (x - camX) / zoomDiv`, `sy = H/2 - (y - camY) / zoomDiv` with
`camX = DAT_008859e8`, `camY = DAT_008859ec`, `zoomDiv = _DAT_008859e0`,
`W,H = DAT_009b0fe4, DAT_009b0fe8` (window size). Sprite rectangle half-sizes are also divided by
`zoomDiv` (e.g. `FUN_004b56e0`).

### 6.3 Camera `FUN_005f8b90` (called every tick, step 5)

```
dist   = |P0.pos - P1.pos|                          (FUN_00619e20, cached per frame)
cx,cy  = (P0.pos + P1.pos) / 2
target = clamp(dist / 420, 0.65, 2.0)               (DAT_0081ce8c, DAT_0081788c, DAT_00813a4c)
if (target > Z)            Z = target               (zoom-out is instant)
else if (Z > target+0.0075) Z -= 0.0075             (DAT_00813ebc; zoom-in is rate-limited)
else                        Z = target
then blend through the focus list (vector @ 0x895144, entries 0x1c):
  for each entry e: (ex,ey) = e.playerId(+0) != -1 ? player(e.playerId).pos : e.pos(+4,+8)
                    cx += (ex-cx)*e.w(+0x18); cy += (ey-cy)*e.w; Z += (e.zoom(+0x10)-Z)*e.w
camX,camY = cx,cy;  zoomDiv = Z / screenScale;  _DAT_008859e4 = Z
```
Focus entries are aged first: `timer(+0x14)--`; `w += 0.05` while `timer >= 1` else `w -= 0.05`
(clamped to ≤ 1); removed when `timer < 1 && w <= 0`. Added by `FUN_005f8fc0(timer, playerId,
pos*, zoom)` (used for hyper/cut-in framing). `Z` (`DAT_00895140`) resets to 2.0 in
`FUN_005f8b40`. `screenScale = _DAT_009b1000 = min(W/800, H/600)` (`FUN_004db370`), so at
800×600 one world unit = 1/Z pixels.

---------------------------------------------------------------------------------------------------

## 7. UNVERIFIED items (and how to settle them)

1. (resolved: player `+0x18` = palette/costume bank index, §5.2)
2. Beam `+0x20`, `+0x58`; Laser `+0x48`; Enemy `+0x10`, `+0x70` — grep the per-type update fns.
3. (resolved: graze zone = 40 collision frames, see §4.7)
4. `FUN_00528250` offset/pivot semantics — read `FUN_00528250` disassembly and a spawner that
   fills `+0xbc..+0xcc`.
5. Solid-enemy rectangle pushout geometry (`FUN_0060e110`, `FUN_005f15a0`, `FUN_005f32a0`).
6. Whips having no collision of their own — confirm in Mira's states (`0x7c..0x9d`).
7. Player draw functions `FUN_006b1ea0` / `FUN_006b2200` identification.
8. Max simultaneous object counts (for fixed pools) — instrument in game.
9. `+0xa9` = "cannot be hit-stunned" (super armour) naming — check which states set it.
10. SE id 0x38 file name (chosen at runtime by `FUN_004e5190`); `FUN_007bcd00` semantics.
11. Message popup kind 4 is never updated/drawn — find where that list is cleared on round reset.
12. Common effect 100 always draws with player 0's art (literal code at `0x4f01a8`); check in-game
    before treating it as a bug.

---------------------------------------------------------------------------------------------------

## 8. Notes for other specs

* **core.md** — `FUN_006129b0` is the gameplay tick (order in §1.3). Players' positions are
  truncated to integers after the Enemy update every frame. Player fields seen here: `+0x5c` hurtbox
  Shape (reset to r=16 each frame), `+0x84` facing/hurtbox angle, `+0x88`, `+0x94` move angle,
  `+0x98` speed, `+0x9c` friction (reset 0.5 each frame), `+0xa0` arena clamp flag, `+0xa2`
  hittable, `+0xa3..+0xab` 9 option flags dispatching `DAT_00881530[i]`, `+0xa7` (×0.25 speed),
  `+0xa9` no-hitstun, `+0xd0` HP, `+0xd4` max HP, `+0xd8` displayed HP (drains 10/frame),
  `+0xdc` recoverable cap, `+0xe0` heat (clamped 0..3; per frame −0.01 in states 0, 2, 3,
−0.005 in other states, no decay in states 1 and 6; dash state 2 adds +0.012 `DAT_00813ed0` per
accelerating frame; victim's heat > 1 scales hitType 1/3 damage in `FUN_00780ab0`),
  `+0xe4` hyper gauge (0..3; sounds 0x3a/0x3b/0x3c on crossing 1/2/3), `+0xe8`,`+0xec` gauges
  (+0.01/frame in states 0,1,2, max 1), `+0xf0` gauge-gain rate (set to 1.5 at low HP only for
  v ≤ 0x5a0), `+0xf8` shield timer, `+0x11c..+0x13c` combo display, `+0x140..+0x14c` stun window,
  `+0x170` rounds won. Move cost table `0x87c4f0 + state*0xc` (`FUN_007807f0`).
  RNG: `FUN_00459fd0`/`FUN_00459f80` (state `0x875b68`).
  `FUN_00612830` / `FUN_00612900` (called by almost every attack state) only feed the training
  frame-advantage display (`DAT_00885904…`), not gameplay.
* **suguri.md** — Suguri object ids: Beam (type 10) 100, 0x96, 200, 0xd2, 300, 0x15e
  (`FUN_004b52a0` spawn / `FUN_004b67a0` update / `FUN_004b56e0` draw / `FUN_004b6570` onHit);
  Missile 100, 200 (`FUN_0067c2b0`/`FUN_0067d2a0`); Hitbox 100, 0x6e, 0x78, 0x82, 0x96, 200, 300,
  0x136, 400, 0x1c2 (`FUN_005eca70`, onHit `FUN_005ecfd0`); Laser 100, 0x6e, 200, 0xd2
  (`FUN_006289c0`/`FUN_0062a540`/draw `FUN_00628c00`/onHit `FUN_0062a2c0`); Enemy 100–103
  (`FUN_00550660`, used by hyper 0x143 `FUN_007598a0`); Effects through `DAT_008816c0`.
  Beam 200 onHit spawns Beam 0xd2 (explosion) at its position.
