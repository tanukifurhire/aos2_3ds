# Porting a character to the engine (read before writing code)

The engine (`src/sim/`) is a near-literal port of aos2.exe's match logic. A character is ported
by translating its decompiled functions **one-to-one** into `src/sim/chars/<name>.c`, using the
engine API below. Keep the original structure (same state ids, same frame numbers, same order of
operations, same RNG calls in the same order). Name each C function after what it is and put the
original address in a comment: `static bool st_beam_rifle(aos_world *w, aos_player *p) /* 0x133 T2 FUN_007571b0 */`.

Read: `src/sim/sim.h`, `src/sim/objects.h`, `src/sim/draw.h`, and skim `src/sim/core.c`,
`src/sim/objects.c` to see the shared helpers. Your spec is `docs/re/<name>.md`; core.md /
objects.md / render.md define the shared systems. The decompiled code (`tools/re/dec/all.c`,
`tools/re/fn.sh <addr>`) is the source of truth whenever the spec is ambiguous.

## Mapping table: original → engine

| Original | Engine |
|---|---|
| player `+0x10` id, `+0x14` side, `+0x18` palette, `+0x04` objId | `p->id`, `p->side`, `p->color`, `p->objId` |
| `+0x1c/+0x20/+0x24/+0x28` | `p->right / left / up / down` |
| `+0x34..+0x48` | `p->D, A, B, S, H, Shield` (3 pressed, 2 held, 1 released, 0 up) |
| `+0x6c/+0x70/+0x74`, `+0x84`, `+0x88`, `+0x8c` | `p->x, y, z`, `faceAngle`, `dashAngle`, `facingRight` |
| `+0x94`, `+0x98`, `+0x9c` | `moveAngle`, `speed`, `friction` |
| `+0xa3+k` / `+0xac+4k` | `statusOn[k]` / `statusTime[k]` |
| `+0xd0..+0xdc`, `+0xe0`, `+0xe4`, `+0xe8/+0xec`, `+0xf0` | `hp, maxHp, redHp, recovHp`, `heat`, `hyper`, `gaugeA/B`, `hyperMul` |
| `+0xf4..+0x108` | `barrierType, barrierTime, barrierR, barrierScale, barrierRot, barrierBreakT` |
| `+0x150/+0x154/+0x158` | `state, timer, hitFlag` |
| `+0x174` | `p->f174` |
| `+0x15c..+0x16b` char scratch | `p->scratch[off - 0x15c]` (Tsih +0x164 = scratch[8]) |
| char state that the original kept in globals | `p->ext.raw` (define a struct for your character and cast; 256 bytes; must be plain data — it is snapshotted for netplay) |
| a global shared by both players (one per character) | `w->charGlobal[ID]` (64 bytes, plain data, snapshotted) |
| `state=s; timer=0; flag158=0; T2[s](p)` | `aos_set_state(w, p, s)` |
| state change without timer reset | assign `p->state` and call `aos_tab.T2[s](w, p)` yourself if the original does |
| `FUN_00780910(p,c)` / `FUN_00780950` / `FUN_007809e0` / `FUN_007807f0(p,s)` | `aos_spend_hyper`, `aos_may_hyper`, `aos_may_shield`, `aos_use_gauge` |
| `FUN_007804b0` aim / `FUN_00780570` dist / `FUN_0061b230(p+0x1c)` | `aos_aim`, `aos_dist`, `aos_dir_angle` |
| `FUN_006b17d0(p,c)` dash / `FUN_006b1930` shield / `FUN_006b1b40` barrier | `aos_dash`, `aos_shield`, `aos_barrier` |
| `FUN_00780ab0` / `FUN_006b19a0` / `FUN_006b1a40` / `FUN_007808a0` | `aos_damage`, `aos_hitstun`, `aos_status`, `aos_heal` |
| `FUN_007bc9c0(id)` / `FUN_007bcaf0(id,cd)` | `aos_se(w,id)` / `aos_se_cd(w,id,cd)` |
| `FUN_00459fd0(a,b)` / `FUN_00459f80(lo,hi)` | `aos_rand_real(w,a,b)` / `aos_rand_int(w,lo,hi)` — **same calls, same order** |
| cos/sin/atan2/sqrt/fabs, `FUN_005f0c60`, `FUN_005f0e70`, `FUN_005f1210` | `aos_cos/sin/atan2/sqrt/fabs`, `aos_angle_normalize`, `aos_angle_toward`, `aos_vec_rotate` |
| `FUN_0061ac10(pos, m)` | `aos_in_arena(pos, m)` |
| spawn via `DAT_008818a0` (beam), `0x8819b8` missile, `0x881b98` hitbox, `0x881c38` laser, `0x881558` enemy, `0x881698` effect | `aos_spawn_beam/missile/hitbox/laser/enemy/effect(w, p->id, …)` (type = `p->id`; effect type 0 = common effects) |
| `FUN_004aa6b0(i)` etc. (object by index after spawn) | `&w->obj.beam[i]`, `&w->obj.missile[i]`, … (index returned by spawn; −1 if the pool is full — check it) |
| `FUN_0055f060(id)` enemy by id / `FUN_007805f0(objId)` player by owner | `aos_enemy_by_id`, `aos_player_by_objid` |
| `FUN_0065dff0(kind,pos,owner)` popup | `aos_popup(w, kind, pos, owner)` (declare `void aos_popup(aos_world*, int, const float[3], int32_t);`) |
| hyper flourish (SE 6, effect 0xfa, screen dim/speedline/blur) | `aos_hyper_flourish(w, p)` + the barrier call your state makes |
| `FUN_00612830` / `FUN_00612900` | training frame meter only — **omit** |
| version branches on `DAT_0089426c` | take the branch for **0x5a2** (`AOS_GAME_VERSION`) only |

## What to implement in your module

```c
void aos_register_<name>(void)   /* strong symbol; replaces the weak stub in chars/registry.c */
{
	aos_tab.inputA[ID] = input_a;      /* 0x881310 */
	aos_tab.inputB[ID] = input_b;      /* 0x881360 (control scheme B) */
	aos_tab.cpu[ID][0..2] = ...;       /* CPU AI per difficulty (may be NULL for now) */
	aos_tab.T2[state] = ...;  aos_tab.T1[state] = ...;   /* every state id you own */
	aos_otab.beamSpawn[ID] = ...; beamUpdate, beamDraw, beamOnHit; missile*; hitbox*; laser*;
	enemy*; effectSpawn/Update/Draw[ID]; whip*  — whatever families the character registers
}
```
Gauge costs are already in `costs.c` (generated from core.md); stats/firstState/initialState are
in `core.c`. Common states 0–6 call your character's states the way the original does (e.g. neutral
calls `T2[firstState]`; draw of common states → see "Drawing" below).

## Drawing

Translate each T1 handler / object draw into `aos_draw()` commands (src/sim/draw.h):
```c
aos_dcmd *c = aos_draw(aos_texf("sg_std%02d", frame), aos_pidx(w, p), p->x, p->y, 0);
c->halfW = c->halfH = 96 * 0.5f;       /* sprite registered as an S×S quad (loader) -> half = S/2 */
c->flipX = (RotY(π) applied?);       /* mirror exactly when the original applies RotY(π): neutral mirrors when +0x8c (facingRight) != 0, i.e. flipX = p->facingRight */
c->rotZ = aos_rad2bam(angle);         /* RotZ */
c->blend = AOS_BLEND_ALPHA;           /* FUN_004637d0 mode */
c->rgba = aos_rgba(r, g, b, a);       /* +0xdc colour: NOTE the original passes (a, r, g, b) */
```
Texture names are the original file names without extension (`sg_std00`, `beamemit`, `hyper`…).
Character art uses bank = player index (0/1) so palettes A/B/C resolve correctly; common art
(effectMdl, beamMdl, particleMdl, msgMdl) uses `AOS_BANK_COMMON`. Objects know their owner via
`aos_player_by_objid(w, ownerId)` → bank `aos_pidx`. Use `hasSrc`+`src[]` for sub-rectangles.
**X-rotation** (tilt) has no 2D equivalent: approximate as y-scale × cos(angle).
The loader functions are not ported as code: just use the sprite pattern/size table from your spec.
Common states' draw is dispatched by the engine (core.c `common_draw`, ported from the common T1
handlers) to your five stub states: neutral → `T1[first+0]`, dash → `T1[first+1]`, KO → `T1[first+2]`,
hitstun/pushed → `T1[first+3]`, shield/burst → `T1[first+4]` (`first` = your first state id).
Register all your T1 handlers under their own state ids (including the stubs' T1).

## Rules

- Never invent numbers. If the spec lacks something, read the decompiled function. If still
  unknown, implement your best reading and mark it `/* UNVERIFIED: … */`.
- Keep RNG call order identical (cosmetic effects included) — netplay determinism depends on it.
- No globals: per-match state goes in `aos_player` (`ext`), objects' scratch fields, or the world.
  Static const tables are fine.
- Only touch `src/sim/chars/<name>.c` (+ your own header if needed). If the engine is missing
  something you need, write it in your file as a `static` helper and list it under
  "Engine requests" in your final report — don't edit shared engine files.
- Must compile warning-free: `tools/zig/zig-x86_64-windows-0.14.1/zig.exe cc -std=gnu11 -Wall -Wextra -Werror -c src/sim/chars/<name>.c -o build/hostobj/<name>.o`
  and run the smoke test `./tests/run_sim.sh <name>` (exists once the lead adds it; it plays a scripted
  match with your character and checks for crashes / non-determinism).

## Decompiler gotchas (collected from the specs)
- `CModel+0xdc(a, r, g, b, mesh)` — alpha comes **first** (render.md table corrected).
- Ghidra prints `t % 1`-style tests as `t & 0x80000000`; that is "t is non-negative", not an even-frame test.
- `p18` passed to spawns = owner's player slot/palette (sprite bank and P1/P2 tint).
- Some draw code keeps values in static globals shared between objects (e.g. Kyoko missile 500
  glow alpha left from the previous missile drawn). Reproduce with a file-static in your module
  (draw-only state is not part of the snapshot, so a static is fine for draw code only).
- Object families are per character (Sora's beam fn ≠ Suguri's); reverse your own entries.
- Effects can carry gameplay (Sora 0x15e wave, Alte shield effect 300): treat your effect family
  as gameplay code, simulated in order.
- Gauge costs: `src/sim/costs.c` is generated from core.md §6.9 (registry + version-0x5a2 patch
  FUN_004a7270). Check your character's rows against your spec; report any difference as an
  engine request (don't edit costs.c).
- Hitboxes/objects may carry team 3 (hits both players) — the passes compare `team != side`.
- Projectiles spawned by summons may record the summon as owner (Nanako's Bits):
  `aos_player_by_objid` returns NULL for them — handle it like the original does.

## Engine API added during porting (use these instead of local copies)
- `src/sim/fx.h`: `aos_spawn_particle` (FUN_0068b8a0), `aos_spawn_spark` (FUN_007bd5e0),
  `aos_popup`, `aos_screen_dark` / `aos_speedline` / `aos_screen_blur` / `aos_cutin`,
  `aos_camera_focus` (FUN_005f8fc0) / `aos_camera_focus_count`, `aos_hyper_flourish` (standard:
  SE 6, dark, speedline, blur, effect 0xfa at angle 0) and `aos_hyper_flourish_ex` (parameterised,
  e.g. Accel SE 7), `aos_afterimage(w, cmd, flag, p, rot)` (FUN_004d7e00, call right after emitting
  the body draw command), `aos_afterimage_clear`.
- `sim.h`: `aos_team_colour(w, team, k)` (colour-table index, DAT_008859f4+team*0x10+4k),
  `aos_colour_rgba(idx, a)`, `aos_draw_line(...)` (FUN_00457ae0), `aos_draw_world`.
- `objects.h`: `aos_whip_new`, `aos_point_in_tri` (FUN_005f1820), `aos_enemy_group_next`
  (FUN_0055efc0 — **call once per summon spawn and store in `e->serial`**; `aos_enemy_new` no longer
  touches the counter), motion system in `motion.h` (keyframed enemy motions).
- Engine hooks: `aos_tab.barrierHook[id]` (barrier created / last 20 frames) — ask the lead for
  new hook points instead of editing core.c.
- Conventions settled by the first ports: unknown object ids → return −1 without allocating;
  keep the RNG draws the original makes even when a pool is full; the art bank for an object is
  the owner's **player index** (not the palette); `p18` in spawns = palette.
- `charGlobal[0]` is reserved for tests.
