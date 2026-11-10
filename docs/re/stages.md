# Stages (3D backgrounds) — reverse-engineering spec + 3DS port notes

Everything here comes from `aos2.exe` (Steam build); addresses are virtual addresses. Port:
`src/stage/` (portable, literal ports), `src/platform/stage.c` + `stage.v.pica` (citro3d),
`tools/aos2mesh` (assets), `tests/stagepreview.c` (host preview).

## 1. Stage list

Stage id = `DAT_0087fadc`. Names from `selMdl/stagename.png` (64-px rows: ids 0–15 in the left
column, row = id; ids ≥ 16 in the right column, row = id & 15 — the select screen draws row
`id & 15`, column `id >> 4`, FUN_007aa670). Two-letter codes are the texture prefixes and the
`selMdl/stage<xx>` thumbnails.

| id | code | name | loader (Game Init) | init / reinit / exit / update / draw | unload |
|---:|---|---|---|---|---|
| 1 | rd | Random (VS list entry only) | — | — | — |
| 2 | — | Net (thumbnail only) | — | — | — |
| 4 | sl | (menu / title background) | FUN_004d2f10 | 4d3700 / 4d3790 / 4d37c0 / 4d3810 / 4d38b0 | 4d3370 |
| 5 | tr | Before the War | FUN_004d6500 | 4d6ab0 / 4d6b40 / 4d6b70 / 4d6bc0 / 4d6d80 | 4d6790 |
| 6 | bf | War 10,000 Years Ago | FUN_004bffe0 | 4c08e0 / 4c0980 / 4c09c0 / 4c0a10 / 4c0f90 | 4c04b0 |
| 7 | gr | Canyon of Wind | FUN_004c90e0 | 4c9900 / 4c9a60 / 4c9b60 / 4c9bb0 / 4c9ec0 | 4c9510 |
| 8 | dr | Dust Storm | FUN_004c7110 | 4c7ca0 / 4c7d70 / 4c7de0 / 4c7e30 / 4c8080 | 4c7890 |
| 9 | ri | Rain and Sunset | FUN_004d1110 | 4d1ab0 / 4d1b50 / 4d1b80 / 4d1bd0 / 4d1cd0 | 4d1630 |
| 10 | se | Equator doldrums | FUN_004d2090 | 4d29f0 / 4d2a90 / 4d2ac0 / 4d2b10 / 4d2be0 | 4d2590 |
| 11 | br | Big Bridge | FUN_004c1ff0 | 4c2db0 / 4c2ed0 / 4c2e80 / 4c2f30 / 4c2fb0 | 4c2710 |
| 12 | ct | Capital in Flames | FUN_004c3dc0 | 4c4b70 / 4c4f90 / 4c5350 / 4c53c0 / 4c5630 | 4c42a0 |
| 13 | iw | Whirlpool of Malice | FUN_004cd0e0 | 4cd9e0 / 4cdb30 / 4cdc10 / 4cdc60 / 4cdde0 | 4cd430 |
| 14 | ct | End of Winter (same code as 12) | FUN_004c3dc0 | same as 12 | 4c42a0 |
| 15 | ft | Nature 10,000 Years After | FUN_004c86f0 | 4c8cf0 / 4c8d90 / 4c8dd0 / 4c8e20 / 4c8f40 | 4c88c0 |
| 16 | gz | Crashed Spaceship | FUN_004ca360 | 4cafc0 / 4cb040 / 4cb060 / 4cb0b0 / 4cb0f0 | 4ca870 |
| 17 | hl | Guardian's Chamber | FUN_004cbfa0 | 4cc490 / 4cc540 / 4cc590 / 4cc5e0 / 4cca90 | 4cc1a0 |
| 18 | ml | Moonlight Dance Hall | FUN_004cef60 | 4cfb10 / 4cfcf0 / 4cfe70 / 4cfec0 / 4cff60 | 4cf5b0 |
| 19 | sh | Sumika's Hideout | FUN_004d3d60 | 4d5310 / 4d54c0 / 4d5610 / 4d5670 / 4d5890 | 4d4a40 |

Assets per stage: §4. Stages carry **no BGM**: in VS the BGM is chosen independently
(`DAT_0087fae0`, BGM list `0x9b8dd4`, see the music spec); in arcade/story every battle record
has its own BGM field (§2.3).

## 2. Framework and selection

### 2.1 Dispatch (FUN_004bf480, called once at start-up)

Six function tables of 20 entries, indexed by stage id:
`0x87a878` init, `0x87a8c8` re-init, `0x87a918` exit, `0x87a968` update, `0x87a9b8` draw(layer),
`0x87aa08` (all zero). Active stages live in a `std::vector<BG_STAGE_MODEL>` (RTTI
`.?AV?$vector@W4BG_STAGE_MODEL@@…`, base `0x87a868`); normally it holds one id.

* `FUN_004bfc10(id)`: if `id` is already in the vector → re-init `0x87a8c8[id]` (log
  `0x81440c`), else init `0x87a878[id]` and push it.
* `FUN_004bfca0(id)`: if present → exit `0x87a918[id]` and erase. `FUN_004bfbc0`: exit all, clear.
* `FUN_004bfd30()`: update every active stage — called from the gameplay tick `FUN_006129b0`
  step 7 only when not resimulating (`DAT_00885a34 == 0`) **while the cosmetic RNG
  `0x875b68` is selected** (core.md §1.2), and from the menu/select scenes.
* `FUN_004bfd80(layer)`: draw every active stage. Gameplay scene `FUN_005f91d0` calls
  `FUN_004bfd80(1)` right after clearing to `0xff0055aa` (+ Z clear 1.0) and before the 2D
  gameplay layer (`FUN_0061adc0` states, optional dim, arena ring `FUN_00619ef0`, objects…).
  Layer 0 is only drawn by other scenes (`FUN_007975a0` calls 1 then 0); every stage draw
  function only does work for layer 1.

Game Init `FUN_005f4650` (each round): when a new match starts (`DAT_00885879`) it calls every
stage unloader (list in §1) and then the loader for `DAT_0087fadc` (`switch(id - 5)`), then
**every round** `FUN_004bfc10(DAT_0087fadc)` → round 1 = init, later rounds = re-init
(the re-init entries reset the stage timers/particle lists). The menu stage 4 is removed with
`FUN_004bfca0(4)` when leaving the menus (e.g. `FUN_0079c110` on select exit).

### 2.2 VS stage choice (character select)

* List `0x9b8dc8` built by Select Init `FUN_0079acc0`, in this order, each entry gated by an unlock
  flag (`DAT_0087b744..752`; all set in a full save): `5, 13, 12, 11, 9, 10, 8, 7, 6, 1(random),
  15, 16, 17, 18, 19`. Id 14 and the menu stage 4 are never offered. Cursor `DAT_009b6ad8`
  (reset to 0 = first entry when entering from the main menu), left/right wraps (`FUN_0079c110`).
* On confirm: entry 1 → `FUN_007aaf90()`: copy of the list without the 1, `idx =
  FUN_00459f80(0, n*10) % n` where `n*10` uses the full list size (cosmetic RNG), else the id.
  The select background is stage 4 until confirm; then `FUN_004bfca0(4); FUN_004bfc10(id)`.
* Netplay: the host's stage/BGM bytes are sent in the match setup packet (`FUN_007b6ac0`,
  read by `FUN_007b91d0` / `FUN_005cf1f0`); replays store them at `DAT_00894298/9c`.

### 2.3 Arcade and story

Both modes run a list of 0x1c-byte records (`DAT_008940ac` = cursor): `+0` type (0 battle,
1 scene, 2 intermission, 3 end), battle: `+4` player char, `+8` opponent (1 = random from a pool,
no repeats), `+0xc/+0x10` colours, **`+0x14` stage**, `+0x18` BGM (0 → opponent's default
`DAT_00882258 + id*0x2dc`); scene: `+4` scene id, `+8` background stage (drawn behind the
dialogue via `FUN_004bfc10`).

* **Arcade** (`FUN_004a7cc0(player)`, run by `FUN_004a7910`): pool = chars 4–8, 11–15 minus the
  player. Fights: (stage 15, BGM 0x10), (6, 0xc), (7, 0xd), (8, 0xb), (9, 0x11), (11, 0xf), then the
  boss with BGM 0xe: player 3–9 → opponent Star Breaker (9) (Sora (3) if the player is 9) on
  stage 12; players 10–17 → Hime (16) (Suguri (10) if the player is 16/17) on stage 17.
  `FUN_004a7910` swaps the boss stage when no continue was used (`DAT_008940c8 == 0`, the
  continue counter): 12 → 13, 17 → 18.
* **Story** (`FUN_007befa0(char)`, run by `FUN_007bec90`): per character a fixed script; decoded
  battles use stage 15 for the first five fights (BGM 0x10, 0xc, 0xd, 0xb, 0x11 — the same BGM
  order as arcade), then stage 16 (BGM 0x12) vs a story rival and stage 19 (BGM 0x13) vs Sumika
  2nd form (18); scenes use backgrounds 15, 16, 19. (UNVERIFIED that the first five battles
  really share stage 15 — the decompiler shows the same constant for all of them.)

The 3DS app (`src/app/main.c`) only has versus: the select menu has a third row "Stage" that
cycles `stage_vs_list` (same order, default first entry as on PC, "Random" resolved with the
cosmetic RNG exactly like `FUN_007aaf90`).

## 3. Engine pieces used by the stages (`src/stage/sg.c`)

### 3.1 Primitives (all create one CMesh with one material; light type 0x800, fog 0x10000000)

| function | port | geometry |
|---|---|---|
| FUN_00468d80(rx,ry,rz,stacks,slices) | sg_sphere | row i: p0=(0,1,0)·RotX(i·π/(stacks−1)); column j ≤ slices: p=p0·RotY(2π−j·2π/slices); pos=p·(rx,ry,rz), normal=normalize(pos), uv=(1−j/slices, (1−p.y)/2); quads (v,v+1,v+s+1),(v+1,v+s+2,v+s+1). D3DX math via dispatch table `0x86f640` (`0x86f6b8` = MatrixRotationX, `+4` RotationY). |
| FUN_004678c0(w,h) | sg_plane | (−w/2,h/2) (w/2,h/2) (−w/2,−h/2) (w/2,−h/2), z=0, normal (0,0,−1), uv (0,0)(1,0)(0,1)(1,1), tris (0,1,2)(2,1,3). Constants `0x853c00..0x853d70`. |
| FUN_004681f0(w,d,nx,nz,s) | sg_grid_xz | x=i·w/(nx−1)−w/2, y=0, z=r·d/(nz−1)−d/2, normal (0,0,−1), u=(i/(nx−1)−.5)s+.5, v=((1−r/(nz−1))−.5)s+.5; tris (n,c+1,c)(n+1,c+1,n). |
| FUN_00467d00(w,h,nx,ny,s) | sg_grid_xy | same in the XY plane (z = 0). |
| FUN_004686e0(w,h,d) | sg_box | 24 vertices, faces +y, −y, −z, +z, +x, −x, outward normals, plane uv layout, tris (0,1,2)(3,2,1). |
| FUN_004742b0(name) + FUN_00469450(surf,split,sx,sz,h,rep) | sg_heightfield | W×H map (129²), x=i·sx/(W−1)−sx/2, y=red·h/255 (byte 2 of the A8R8G8B8 surface), z=j·sz/(H−1)−sz/2, uv=(i/(W−1)·rep, j/(H−1)·rep); angle-weighted normals (CMesh +0x28 = FUN_00471b70); split into split² chunk CMeshes ("HightField_%02u", "%s_%d_%d") that are frustum-culled (FUN_00463d20 in FUN_00472cc0). |
| FUN_00469e50("mdl\\x.x") | sg_xfile | D3DX `.x` load (frames under "MASTER_FRAME"); the stage files are single meshes with N materials. Material textures come from `mdl/3dtex.dat` ("LAG": `LAG\0`, u32 0, then {char name[16]; u32 w, h, fmt=2, size; R16G16B16A16}), looked up by the .x texture file name with byte-wise `tolower` (which also changes Shift-JIS trail bytes — that is why the names in 3dtex.dat look garbled). |
| FUN_00475660(max,kind) | sg_particles_* | CPerticle (vtable 0x849cc0): +0x1c texture, +0x28 begin, +0x20 add(x,y,z,w,h,RECT,argb), +0x2c draw. Quad corners ±w/2, ±h/2 oriented by a LookAtLH basis from the camera eye (FUN_004764c0), uv = texel rect / texture size, world = identity, lighting/fog off, cull none (FUN_00475b10). |

### 3.2 CModel (vtable 0x849734) as used by stages

Transform state: 3×3 rotation rows at +0x84/+0x78/+0x6c (identity after +0x38 reset), position
+0x90, scale +0x9c. `+0x50/+0x54/+0x58` (FUN_0046ce70/0046d0b0/0046d2f0) build Rx/Ry/Rz from the
sine table `0xddc0f0` (binary angle) and set `R = Rnew · R` (later calls rotate first, in model
space). Render (+0x40, FUN_0046c580): world = Scale · [R | pos]. `+0x9c` (FUN_0046c290) unbinds
texture stages after drawing; `+0xa0` (FUN_0046c3a0) does the same and sets `DAT_00d9296c = 1`
(following meshes multiply by `*(DAT_00d92968 + 0x50)`, which FUN_0046a0a0 sets to a mirror about y = 0 plus four user clip planes; only the Big Bridge reflection uses it — `st_br.c` folds the mirror into the model transform, the clip planes are omitted).
Per-mesh/material setters (−1 = all): `+0xb0(stage, tex, mesh, mat)` texture, `+0xc0/+0xc4` uv
offset u/v (CMesh +0x180/+0x184), `+0xc8(type)` vertex-colour mode (CMesh +0x18c), `+0xd0(type)`
fog mode (+0x194), `+0xdc(a,r,g,b)` material diffuse; `+0xd4/+0xd8` rebuild the mesh's shader
after a mode change.

### 3.3 Vertex shader (runtime-assembled, FUN_004928b0 / FUN_00492a50)

The shaders that matter are assembled at run time from text fragments in `.rdata`
(`vs.1.1`; the `shader.pak` blobs are other, older shaders — they are XOR'd with the 9-byte key
`5h4d3rqb0` and are vs_2_0/ps_1_4 bytecode). Constants: c0 = (0.5, ?, 1, ?), c1 = (uvOff.u,
uvOff.v, −1/(end−start), end/(end−start)), c2 = (eye.xyz, ambient), c3..c6 = light directions
(object space, negated: FUN_00472480 uses the inverse world matrix), c7..c10 light colours,
c11 material diffuse, c12.y = normal extrusion (0), c13..c15 fog matrix, c20 = WVP.

Colour fragment by CMesh +0x18c:
* `0x100` "ライト無し": `oD0 = 1`
* `0x200` "頂点カラー": `oD0 = v6`
* `0x400` "ディフューズ": `oD0 = c11` (material)
* `0x800` "ライト": `r3 = N·c[3..6] + c2.w; oD0 = min((Σ r3.i · c[7+i].rgb, 1) · c11, 1)` —
  no `max(0, N·L)`; only light 0 is ever set (lights 1–3 have colour 0). Hardware clamps ≥ 0.
* `0x1000` directional × hemisphere, `0x2000` depth-as-colour (not used by stages).

Fog fragment by +0x194: `0x10000000` none; `0x20000000`: `oFog = dp4(pos, c15)·c1.z + c1.w`
with c13..c15 = transpose(world·view) → linear fog on view-space z: f = (end − z)/(end − start);
`0x40000000`: `oFog = |p_world|²·c1.z + c1.w` (literally the squared distance — not used by any
stage). Fog colour = D3DRS_FOGCOLOR from FUN_00463750(start, end, rgb); FUN_00473c70 also
switches D3DRS_FOGENABLE per mesh. Texture stage 0: MODULATE(texture, diffuse) for colour and
alpha; stage 1 ADD (only with a second texture, unused). `oT0 = uv + c1.xy`.

Blend modes FUN_004637d0 (render.md table). Render states the stages set: 7 ZENABLE, 0xe
ZWRITEENABLE, 0x16 CULLMODE (1 none, 3 CCW), 0x1c FOGENABLE, 0x1b/0x13 alpha blend. Light:
FUN_004676b0(i,on) enable (type 3 = directional), FUN_004676e0 ambient (scalar, c2.w),
FUN_00467700(i,r,g,b) colour, FUN_00467750(i,dir) normalized direction.

### 3.4 Camera (CCamera, vtable 0x848d44, FUN_00465c30)

`+0xc` FUN_00465e50(fov, near, far, aspect): fovy = fov·2π/360 (`0x853b04` = 1/360),
D3DXMatrixPerspectiveFovLH (`ys = cot(fovy/2)`, `xs = ys/aspect`, `Q = far/(far−near)`).
`+0x20` reset axes (right (1,0,0), up (0,1,0), forward (0,0,1)), `+0x44` FUN_00466ca0(eye, at,
up=(0,1,0)) LookAtLH basis, `+0x28/+0x2c/+0x30` rotate the basis about its own Y/X/Z
(FUN_004667b0/00466560/00466a00), `+0x24` FUN_00466180 build the view matrix, `+0x64`
FUN_00467070 SetTransform(VIEW/PROJ) and remember the eye (`0xa5206c`).

**Relation to the gameplay camera**: no stage reads `camX/camY` (`DAT_008859e8/ec`). In a match
(`DAT_00885870 == 2`) the FOV follows the gameplay zoom `Z = _DAT_008859e4` (objects.md §6.3):
`fov = (k·Z + c)·0.64` (`0x8145e4`), e.g. bf `k = 10, c = 30`; outside a match a fixed value
(bf: 70). The aspect argument is `DAT_009b1010` (window W/H; 4:3 on PC).

## 4. Per-stage specification

### 6 bf — War 10,000 Years Ago (src/stage/st_bf.c)
Loader FUN_004bffe0: sky = sphere(500,400,500,20,20) `bf_sky`; cloud = sphere(400,400,400,20,20)
`bf_cloud`; cloud2 = sphere(495,400,495,20,20) `bf_cloud2`; exp0/exp1 = plane(50,50) `bf_exp0/1`,
light type 0x400. Camera FUN_004c0760: fov = (in match 10·Z+30 (`0x813f4c`,`0x8140e8`), else 70
`0x8145ec`)·0.64, near 10, far 2000 (`0x8145fc`), eye (0,0,−200) (`0x814600`), target
(0, sin(a5c)·20 − 50 + frame·0.01, 0). States FUN_004c0670: alpha blend, Z off, fog off.
Light FUN_004c06c0: dir (0,−1,0), colour (aa9c, aaa0, aaa4), ambient aaa8. Update FUN_004c0a10:
a5c, a60 += 0.0025 (`0x8145e0`), a64 += 0.00175, wrapped at π; explosions (age, life −0.025,
scale −0.01 after age 4, angle −0.00185, pos = (cos·r, y0, sin·r)); every 5 frames after
frame 100: p = sin((frame%300)/300·π)·100, spawn if p ≤ rand(0,120) with r = rand(100,300),
angle = rand(−1,1)+π/2, y0 = rand(−200,0) (cosmetic RNG, this order); on frame%100==0 also SE
0x34 (in match) and the flash FUN_004c0630 (light g = 0.8, b = 0.6, ambient 1, recovering
+0.002/+0.005 per frame, ambient −0.005 to 0.6). Draw: sky rotY 180°, cloud2 rotY(a64) (blend 1),
explosions additive (blend 2): exp0 colour (life·0.5, 1, 0.5−(1−life), same), exp1
(life·0.4, 1,1,1), scale = particle scale; cloud rotY(a60) (blend 1); FUN_004bfdd0.

### Stage 5 "tr" — Before the War (wireframe room) — src/stage/st_tr.c

#### Functions
| role | address |
|---|---|
| loader | FUN_004d6500 |
| unload | FUN_004d6790 (frees 2 textures + 4 models) |
| init | FUN_004d6ab0 |
| reinit | FUN_004d6b40 |
| exit | FUN_004d6b70 (deletes camera) |
| update | FUN_004d6bc0 |
| draw | FUN_004d6d80 (only the `param == 1` branch exists) |
| camera | FUN_004d6970 |
| render states | FUN_004d6880 |
| light | FUN_004d68d0 |

#### Globals
| addr | meaning |
|---|---|
| _DAT_0087b5b0 | cleared by init/reinit, never read |
| DAT_0087b5b4 | room yaw (rad), +0.00125/tick (DAT_00814bdc), if > pi (DAT_00853b58) -= 2pi (DAT_00853b78) |
| DAT_0087b5b8 | camera bob phase, +0.0025/tick (DAT_008145e0), same wrap |
| DAT_0087b5e8 | glow pulse phase, +0.05/tick (DAT_00813ef4), same wrap. **Never reset** by init/reinit (static, 0 at boot) |
| DAT_0087b5d4/5d8/5dc | light colour r,g,b = 1 (DAT_00853b34, init only) |
| DAT_0087b5e0 | ambient = 1 (init only) |
| DAT_0087b5e4 | CCamera (FUN_00465c30 in init) |
| DAT_0087b5bc / 5c8 | textures tr_wire / tr_wireb (bgMdl, folder 2) |
| DAT_0087b5c0 / 5c4 | wall grid (xy) / floor grid (xz), texture tr_wire |
| DAT_0087b5cc / 5d0 | wall grid (xy) / floor grid (xz), texture tr_wireb |

#### Assets
- `tr_wire`, `tr_wireb` (bgMdl). (`tr_wire05` exists in the folder but is not loaded.)
- 2x FUN_00467d00(600, 600, 10, 20, 20.0, 0) = sg_grid_xy (DAT_008149b8 = 600, DAT_008140fc = 20.0; verified in disassembly at 0x4d6590) — 342 triangles each.
- 2x FUN_004681f0(600, 600, 10, 20, 20.0, 0) = sg_grid_xz.
- No light/fog type changes: all meshes keep SG_LIGHT_DIR / SG_FOG_NONE, colour (1,1,1,1).

#### Camera (FUN_004d6970)
- fov = 30 (DAT_008140e8) * 0.64 (DAT_008145e4); **no zoom dependence, no gameMode check**.
- near 10 (DAT_00813f4c), far 2000 (DAT_008145fc), aspect DAT_009b1010.
- eye (0,0,0); target (0, sin(b8)*5, 10) (DAT_00813f40 = 5, DAT_00813f4c = 10). A local = 100 (DAT_00853b90) is stored but never used.

#### Render states / light
- FUN_004d6880: ALPHABLEND 1, SRCBLEND 5, ZENABLE 1, ZWRITE 1, FOG 1 (no FUN_00463750 call; meshes have no fog type), light 0 on.
- FUN_004d68d0: light dir (0,-1,0) (DAT_00853bb4), ambient DAT_0087b5e0 (1), colour (1,1,1).
- No clear (the room covers the whole view).

#### Draw (param 1)
camera, states, light; blend 1; room(wire models); blend 2; room(wireb models); FUN_004bfdd0.

room(wall, floor):
- for a in {b4, b4 + pi/2 (DAT_00814104), b4 - pi/2, b4 + pi (DAT_00853b58)}: reset, rot_y(rad2bam(a)), pos(sin a * 300, 0, cos a * 300) (DAT_008145f0; argument order verified in disassembly at 0x4d6de5), render(0), render_end.
- floor: reset, rot_y(rad2bam(b4)), pos(0, 300, 0), render, render_end; again with pos(0, -300 (DAT_00815f44), 0).

#### Update (FUN_004d6bc0)
- e8 += 0.05, wrap; wireb wall and wireb floor colour = (a = sin(e8)*0.5 + 0.5 (DAT_00853b1c), r = g = b = 1), all meshes (-1).
- b4 += 0.00125, wrap; b8 += 0.0025, wrap.
- No RNG, no SE, no gameMode checks.

#### Port notes
- Literal.
- Preview: at tick 0 only the front wall is in view (19.2 deg fov at 300 units); the yaw then rotates corners into view; the wireb glow pulses with period ~126 ticks.
- Cost: <= 6 draw commands, ~2050 triangles. The 1-texel grid lines alias at 400x240 (UNVERIFIED on hardware; mipmaps for tr_wire would help).

### Stage 7 "gr" — Canyon of Wind — src/stage/st_gr.c

#### Functions
| role | address |
|---|---|
| loader | FUN_004c90e0 |
| unload | FUN_004c9510 |
| init | FUN_004c9900 |
| reinit | FUN_004c9a60 |
| exit | FUN_004c9b60 (deletes camera) |
| update | FUN_004c9bb0 |
| draw | FUN_004c9ec0 (only param 1) |
| camera (+ wind alpha) | FUN_004c9740 |
| render states + fog | FUN_004c9620 |
| light | FUN_004c96a0 |

#### Globals
| addr | meaning | init / reinit value |
|---|---|---|
| DAT_0087ac58 | frame counter | 0 |
| DAT_0087ac5c | = 5 (DAT_00813f40) every update, never read | — |
| _DAT_0087ac60 | never read | 1 |
| DAT_0087ac64 | gust extra scroll speed (0..0.005) | 0 |
| DAT_0087ac68 | canyon z scroll | 800 (DAT_008149c8) |
| DAT_0087ac6c / ac70 | wind / wind2 v offset | 0 |
| DAT_0087ac74 | wind alpha (written by the camera function) | 0 |
| DAT_0087ac78 | wind brightness | 0 |
| DAT_0087ac7c (char) / ac80 | gust active / gust timer | 0 / 0 |
| DAT_0087ac84/88/8c, ac90 | light rgb, ambient | 1 (init only) |
| DAT_0087ac94 | CCamera (deleted + recreated in init AND reinit; set to 0 by the loader) | |
| DAT_0087ac98/ac9c/aca0 | eye | (0, 600 (DAT_008149b8), 0) — never changes |
| DAT_0087aca4/aca8/acac | look vector (target = eye + look) | (0, 1, 300 (DAT_008145f0)) |
| DAT_0087acb0 / acc0 / acc8 | textures gr_skin / gr_wind / gr_wind2 | |
| DAT_0087acb8 | height-map surface gr_hightrock (129x129) | |
| DAT_0087acb4 / acbc / acc4 | rock height field / wind sphere / wind2 sphere | |

#### Assets
- `gr_skin`, `gr_wind`, `gr_wind2` (bgMdl); height map `gr_hightrock` (bgMdl).
- Rock: FUN_00469450(map, 16, 3000, 3000, 1000, 16, 0) (DAT_008147d8 = 3000, DAT_00814408 = 1000; verified in disassembly at 0x4c91e1) -> 256 chunk meshes of 8x8 cells (128 tris); colour (1,1,1,1), tex gr_skin, fog 0x20000000 (view).
- Wind: FUN_00468d80(350, 2000, 350, 50, 50, 0) (DAT_00814be0, DAT_008145fc), tex gr_wind, fog view.
- Wind2: FUN_00468d80(280, 2000, 280, 50, 50, 0) (DAT_00814f14), tex gr_wind2, fog view.
- Light type stays 0x800 (directional) for all.

#### Camera (FUN_004c9740)
- fov = 70 (DAT_008145ec); if gameMode == 2: 10 (DAT_00813f4c) * zoom + 40 (DAT_008140e0); times 0.64 (DAT_008145e4).
- near 10, far 2000 (DAT_008145fc), aspect DAT_009b1010.
- eye = (0, 600, 0), target = eye + (aca4, aca8, acac).
- After the camera is applied: ac74 = sin(atan2(acac, aca4)) * 0.25 (DAT_00813f24) + 0.5 (DAT_00853b1c). FUN_00474240(y, x) = (float)CRT atan2(y, x) (via FUN_007fcb30 = _CIatan2).

#### Render states / light / fog
- FUN_004c9620: ALPHABLEND 1, SRCBLEND 5, Z 1, ZWRITE 1, FOG 1, light 0 on, fog(0, 2000, 0xff46505a).
- FUN_004c96a0: dir (0,-1,0), ambient ac90 (1), colour (1,1,1).
- Clear 0xff46505a.
- Rock pass: no FUN_004637d0 before it (blend mode = whatever the caller left; ported as is).
- Wind pass: Z 0, ZWRITE 0, CULL 1 (none), blend 1, fog(0, 1500 (DAT_008149d4), black), blend 2; wind; fog(0, 1800 (DAT_00814f18), black); wind2; blend 1; FUN_004bfdd0.

#### Draw (param 1)
1. rock: reset, colour (a 1, rgb .25 (DAT_00813f24)), pos(0, 0, 4500 (DAT_00814f20) - ac68), render; again with z = 1500 (DAT_008149d4) - ac68 (two copies 3000 apart scrolling toward -z).
2. wind / wind2: reset, colour (ac74, ac78, ac78, ac78), pos(eye), rot_x(deg2bam(90)), uv_v(-ac6c / -ac70) (XOR DAT_00853e00 = sign bit), render.

#### Update (FUN_004c9bb0)
- ac68 += 5; if > 3800 (DAT_00814f1c): -= 3000.
- ac6c += (5 * 0.0002 (DAT_00814f10) + ac64) * 0.75 (DAT_00813f3c); if > 1: -= 1. ac70 the same with factor 1.
- ac78 = (sin((f % 1500)/1500 * 2pi) * 0.25 + 0.75)^2.
- aca4 = sin((f % 4000)/4000 (DAT_008147e0) * 2pi) * 200 (DAT_008147cc); aca8 = sin((f % 3000)/3000 * 2pi) * 50 + 50 (DAT_008140ec); acac = 300.
- Gust: if f > 0 && f % 1000 == 200: active = 1, timer = 500. If active: if ac64 < 0.005 (DAT_00813eac): ac64 += 2.5e-5 (DAT_00814f0c); if timer > 399 && timer % 200 == 0 && gameMode == 2: SE 0x35 (only fires at timer 400); if timer < 1: active = 0 else timer--. Else if ac64 > 0 (PTR_00853af4 = 0.0f): ac64 -= 2.5e-5 (float drift kept literally).
- f++. No RNG.

#### Port notes
- Literal. At tick 0 (no update yet) ac78 = 0, so the wind tubes are invisible on the first frame.
- Cost: 40-62 commands, 14.7k-17.4k triangles (two 5000-triangle wind spheres + ~45 visible rock chunks). The two wind tubes are full-screen additive layers around the camera (fill-rate cost).

### Stage 8 "dr" — Dust Storm — src/stage/st_dr.c

#### Functions
| role | address |
|---|---|
| loader | FUN_004c7110 |
| unload | FUN_004c7890 |
| init | FUN_004c7ca0 |
| reinit | FUN_004c7d70 |
| exit | FUN_004c7de0 (deletes camera) |
| update | FUN_004c7e30 |
| draw | FUN_004c8080 (only param 1) |
| camera | FUN_004c7b80 |
| render states + fog | FUN_004c7a60 |
| light | FUN_004c7ae0 |

#### Globals
| addr | meaning | init / reinit value |
|---|---|---|
| DAT_0087abb0 | frame | 0 |
| DAT_0087abb4 | sway phase, +0.00125 (DAT_00814bdc), if > pi -= 2pi | 0 |
| DAT_0087abb8 | orbit angle, +0.002 (DAT_008145dc), same wrap | 0 |
| _DAT_0087abbc | orbit radius | 1900 (DAT_00814be8) |
| DAT_0087abc0 | orbit height | 800 (DAT_008149c8) |
| DAT_0087abc4 / abc8 | wind / tornado v scroll (+0.00075 DAT_00814bd4 / +0.001 DAT_00814bd8, if > 1 -= 1) | 0 |
| DAT_0087abcc | tornado alpha 0..1 | 0 |
| DAT_0087abd0/abd4/abd8 | orbit point = (cos b8 * r, h, sin b8 * r), used as camera eye | **not reset** |
| DAT_0087ac0c/10/14, ac18 | light rgb, ambient = 1 | init only |
| DAT_0087ac1c | CCamera | init |
| DAT_0087abdc / abe8 / abfc / abf4 / ac04 | textures dr_sand / dr_rock / dr_sky / dr_wind / dr_tornade (bgMdl) | |
| DAT_0087abe4 / abf0 | height maps dr_hightsand / dr_hightrock (129x129) | |
| DAT_0087abe0 / abec / ac00 / abf8 / ac08 | sand / rock / sky sphere / wind sphere / tornado sphere | |

#### Assets
- Sand: FUN_00469450(dr_hightsand, 16, 2000, 2000, 250 (DAT_008149ac), 10, 0); colour (1,1,1,1); tex dr_sand; fog 0x20000000. 256 chunks x 128 tris = 32768 tris.
- Rock: FUN_00469450(dr_hightrock, 8, 2000, 2000, 350 (DAT_00814be0), 20, 0); colour 1; tex dr_rock; fog view. 64 chunks x 512 tris = 32768 tris.
- Sky: FUN_00468d80(1500, 1200, 1500, 100, 100, 0) (DAT_008149d4, DAT_008149d0); fog 0x10000000; tex dr_sky; light 0x400 (material colour only). ~19.8k tris.
- Wind: FUN_00468d80(300, 2000, 300, 50, 50, 0) (DAT_008145f0, DAT_008145fc); fog 0x20000000, tex dr_wind, then fog 0x10000000 again (net: no fog).
- Tornado: same sphere parameters; fog none; tex dr_tornade; fog none.

#### Camera (FUN_004c7b80)
- fov 40 (DAT_008140e0) * 0.64; **no zoom / gameMode dependence**. near 50 (DAT_008140ec), far 3000 (DAT_008147d8), aspect DAT_009b1010.
- eye = (abd0, abd4, abd8); target (0,0,0). Unused local 100 (DAT_00853b90).
- update sets the point to radius 1200, height 1000; draw temporarily moves it to the sky/wind point and restores radius 1200 / height 1000 before returning, so the camera orbits the origin at r 1200, y 1000, angle b8 (one turn per ~3142 ticks).
- QUIRK: abd* are not reset by init/reinit; if draw ran before the first update of a stage the camera would use stale values (0,0,0 at boot = degenerate look-at; the preview at tick 0 shows only the clear colour). Kept literally.

#### Render states / light / fog
- FUN_004c7a60: ALPHABLEND 1, SRCBLEND 5, Z 1, ZWRITE 1, FOG 1, light 0 on, fog(50 (DAT_008140ec), 1500 (DAT_008149d4), 0xff646450).
- FUN_004c7ae0: dir (0,-1,0), ambient 1, colour 1.
- Clear 0xff646450; blend 1; CULL 3; sand and rock at (0,0,0).
- Then CULL 1, Z 0, ZWRITE 0, FOG 0; blend 2: sky, wind; blend 1: tornado; FUN_004bfdd0.

#### Draw (param 1)
- h = cos(b4) * 100 (DAT_00853b90) + 400 (DAT_008145f4); r = cos(b4) * 300 (DAT_008145f0) + 1600 (DAT_00814be4); point = (cos b8 * r, h, sin b8 * r).
- Sky: reset, colour (a = sin(b8) * 0.15 (DAT_00813f20) + 0.85 (DAT_00814120), 1, 1, 1) (verified in disassembly at 0x4c82c8), pos(point), render.
- Wind: reset, colour (1,1,1,1), pos(point), rot_y(rad2bam(-b8)), rot_x(deg2bam(90)), uv_v(+abc4), render.
- h = 1000 (DAT_00814408), r = 1200 (DAT_008149d0), point recomputed.
- blend 1; Tornado: reset, colour (abcc * 0.75 (DAT_00813f3c), 1, 1, 1), pos(point = camera eye), rot_y(-b8), rot_x(90 deg), uv_v(-abc8), render.

#### Update (FUN_004c7e30)
- b8 += 0.002 wrap; b4 += 0.00125 wrap; h = 1000, r = 1200, point recomputed.
- abc4 += 0.00075 wrap; abc8 += 0.001 wrap.
- if f % 2000 < 1601 (0x641): abcc -= 0.0025 (DAT_008145e0), clamp >= 0; else: if f % 2000 == 1610 (0x64a) && gameMode == 2: SE 0x37; abcc += 0.0025, clamp <= 1. (Dust storm darkening: ticks 1601..1999 of every 2000.)
- f++. No RNG. (Returns an int nobody uses.)

#### Port notes
- Literal.
- Cost: 134-171 commands, **~61.5k triangles per frame** (sand 32k + rock up to 32k, frustum culled per chunk; sky 19.8k; wind + tornado 10k). By far the heaviest of tr/gr/dr and probably too much for 60 FPS on o3DS. Candidates for a 3DS-only LOD (not done, would not be literal): fewer stacks/slices for the sky sphere (a featureless backdrop seen from inside), coarser height-field sampling.

### Stage 9 "ri": Rain and Sunset (src/stage/st_ri.c)

#### Functions
| role | address |
|---|---|
| loader | FUN_004d1110 |
| unloader | FUN_004d1630 (deletes textures/models 0x87b458..0x87b478) |
| init | FUN_004d1ab0 |
| reinit | FUN_004d1b50 |
| exit | FUN_004d1b80 (deletes camera 0x87b48c) |
| update | FUN_004d1bd0 |
| draw | FUN_004d1cd0 (only param 1 does anything) |
| camera | FUN_004d18b0 |
| render states | FUN_004d1790 |
| light | FUN_004d1810 |

#### Globals
| addr | meaning |
|---|---|
| DAT_0087b448 | int frame counter (stage-local) |
| DAT_0087b44c | cloud yaw (rad) |
| DAT_0087b450 | sea V scroll |
| DAT_0087b454 | rain V scroll |
| DAT_0087b458/464/46c/474 | textures ri_rain / ri_cloud / ri_horizon / ri_sea |
| DAT_0087b45c/460/468/470/478 | models rain (unused) / rain2 / cloud / horizon / sea |
| DAT_0087b47c/480/484 | light colour r,g,b |
| DAT_0087b488 | ambient |
| DAT_0087b48c | CCamera |

#### Assets (all /bgMdl/, folder 2)
| model | primitive | texture | light | fog |
|---|---|---|---|---|
| rain 0x87b45c | sg_sphere(240 [DAT_00815584], 3000 [DAT_008147d8], 240, 50, 50) | ri_rain | DIR (default) | VIEW |
| rain2 0x87b460 | sg_sphere(480 [DAT_00815884], 3000, 480, 50, 50) | ri_rain | DIR | VIEW |
| cloud 0x87b468 | sg_grid_xz(1000 [DAT_00814408], 1000, 20, 20, 1.0) | ri_cloud | DIR | VIEW |
| sea 0x87b478 | sg_grid_xz(3000, 2000 [DAT_008145fc], 50, 50, 12 [DAT_00813f54]) | ri_sea | DIR | VIEW |
| horizon 0x87b470 | sg_grid_xy(3000, 1500 [DAT_008149d4], 5, 5, 1.0) | ri_horizon | NONE (0x400) | NONE |

`rain` (0x87b45c) is created and textured but **never drawn**. The port keeps it to stay literal; it costs about 2550 vertices of memory.
Each fog/light type change is followed by vtable +0xd8/+0xd4 (shader rebuild, no-op in sg). ri_sky.png exists in the archive but this stage does not load it.

#### Init / reinit / exit
- init: frame=0, cloudAng=seaV=rainV=0; light colour (0.9 [DAT_00814178], 0.8 [DAT_008145e8], 0.7 [DAT_00813f38]); ambient 0.85 [DAT_00814120]; camera FUN_00465c30.
- reinit: frame and the three phases = 0 (light unchanged).
- exit: delete camera.

#### Update (FUN_004d1bd0), per tick
```
cloudAng -= 0.0005 [_DAT_0081587c]; if (cloudAng < -pi [DAT_00814100]) cloudAng += pi [DAT_00853b58]   // QUIRK: adds pi, not 2pi
seaV += 0.0125 [_DAT_00815880]; if (seaV > 1) seaV -= 1
rainV += 0.02 [DAT_00813ed8]; if (rainV > 1) rainV -= 0.5 [DAT_00853b1c]                            // QUIRK: subtracts 0.5
if (frame % 350 == 300 && gameMode == 2) SE 0x36                                                    // every 350 ticks, in match only
frame++
```
No RNG.

#### Camera (FUN_004d18b0)
- fov: gameMode 2: 10 [DAT_00813f4c] * zoom + 60 [DAT_00813a58]; otherwise 70 [DAT_008145ec]. Passed as fov*0.64 [DAT_008145e4].
- near 10 [DAT_00813f4c], far 2000 [DAT_008145fc], aspect DAT_009b1010.
- eye (0,0,0); up NULL (default).
- a = ((frame+500) % 3000) / 3000 [DAT_008147d8] * 2pi [DAT_00853b78]; b = (frame % 2000) / 2000 [DAT_008145fc] * 2pi
- target = (sin(b)*0.75 [DAT_00813f3c], sin(a)*1.5 [DAT_00814110] + 0.5 [DAT_00853b1c], cos(a)*0.5 + 1.5)
- The view sweeps between about 50 degrees up (clouds) and about 34 degrees down (sea) over 3000 ticks, and pans about 27 degrees left/right over 2000 ticks.
- Order: persp, reset, lookat, build, apply.

#### Render states (FUN_004d1790)
ALPHABLEND 1, SRCBLEND 5, ZENABLE 0, ZWRITE 0, FOG 1, CULL 1 (none), light 0 on, fog(0, 2000 [DAT_008145fc], 0xff000000 black).

#### Light (FUN_004d1810)
dir (0, -1 [DAT_00853bb4], 0); ambient DAT_0087b488 (0.85); colour (0.9, 0.8, 0.7); light 0 on.

#### Draw (FUN_004d1cd0, layer 1)
1. camera, states, light; blend 1; clear colour 0xff000000.
2. clouds: for i = 10 .. 1: reset; pos(0, i*200 [DAT_008147cc], i*-50 [DAT_008149e4]); rotY(rad2bam(cloudAng) + deg2bam(i*36)); render.
3. sea: reset; pos(0, -50, 1000 [DAT_00814408]); uv_v(seaV); render.
4. ZENABLE 0, ZWRITE 0 (again).
5. horizon twice: pos(-1500 [DAT_00814a24], 100 [DAT_00853b90], 1000) and pos(1500, 100, 1000); blend still 1.
6. blend 1; fog(0, 2000, black) again.
7. rain2: reset; pos(0,0,0); uv_v(-rainV + 0.5); render. The negation is an xorps with the sign mask DAT_00853e00, confirmed in the disassembly at 0x4d201a.
8. FUN_004bfdd0 (restore 2D).

#### Cost
Per frame, before frustum culling: rain2 4900 tris + clouds 10 x 722 + sea 4802 + horizon 2 x 32, about 17.0k triangles, at most 15 commands (the preview shows 5 to 11). Fill: up to 10 overlapping alpha cloud layers plus the full-screen rain sphere when the camera looks up. This is the heaviest part on 3DS.

### Stage 10 "se": Equator doldrums (src/stage/st_se.c)

Sibling of "ri" (stage 9), with the same structure and constants. The differences are listed at the end.

#### Functions
| role | address |
|---|---|
| loader | FUN_004d2090 |
| unloader | FUN_004d2590 |
| init | FUN_004d29f0 |
| reinit | FUN_004d2a90 |
| exit | FUN_004d2ac0 |
| update | FUN_004d2b10 |
| draw | FUN_004d2be0 |
| camera | FUN_004d2810 |
| render states | FUN_004d26f0 (identical to ri FUN_004d1790) |
| light | FUN_004d2770 (identical to ri FUN_004d1810) |

#### Globals
| addr | meaning |
|---|---|
| _DAT_0087b490 | int frame counter: reset by init/reinit, **never incremented or read** |
| DAT_0087b494 | cloud yaw |
| DAT_0087b498 | sea UV scroll (used for both U and V) |
| DAT_0087b49c | rain V scroll |
| DAT_0087b4a0/4ac/4b4/4bc | textures se_rain / se_cloud / se_horizon / se_sea |
| DAT_0087b4a4/4a8/4b0/4b8/4c0 | models rain (unused) / rain2 / cloud / horizon / sea |
| DAT_0087b4c4/4c8/4cc, 4d0 | light colour, ambient |
| DAT_0087b4d4 | CCamera |

#### Assets (/bgMdl/)
| model | primitive | texture | light | fog |
|---|---|---|---|---|
| rain 0x87b4a4 | sg_sphere(300 [DAT_008145f0], 3000, 300, 50, 50) | se_rain | DIR | VIEW, never drawn |
| rain2 0x87b4a8 | sg_sphere(600 [DAT_008149b8], 3000, 600, 50, 50) | se_rain | DIR | VIEW |
| cloud 0x87b4b0 | sg_grid_xz(1000, 1000, 20, 20, 1.0) | se_cloud | DIR | VIEW |
| sea 0x87b4c0 | sg_grid_xz(3000, 2000, 50, 50, 12) | se_sea | DIR | VIEW |
| horizon 0x87b4b8 | sg_grid_xy(3000, 1500, 5, 5, 1.0) | se_horizon | DIR (default) | VIEW |

#### Init/reinit/exit, light, render states
All identical to ri: light colour (0.9, 0.8, 0.7), ambient 0.85, dir (0,-1,0). States: ALPHABLEND 1, SRCBLEND 5, Z off, ZWRITE off, FOG on, CULL none, fog(0, 2000, black).

#### Update (FUN_004d2b10)
The same three phase updates as ri: cloudAng -= 0.0005 and wraps by +pi when below -pi; seaUV += 0.0125 and wraps by -1; rainV += 0.02 and drops by 0.5 when above 1. **There is no frame increment and no sound effect.**

#### Camera (FUN_004d2810)
- fov: gameMode 2: 10*zoom + 60; otherwise **50** [DAT_008140ec]. Multiplied by 0.64; near 10, far 2000.
- eye (0,0,0). The target formula is the same as ri, but it is driven by the **match frame counter DAT_008858f8** (`stage_g.matchFrame`) and has no +500 offset: a = (matchFrame % 3000)/3000*2pi, b = (matchFrame % 2000)/2000*2pi; target = (sin b*0.75, sin a*1.5+0.5, cos a*0.5+1.5).

#### Draw (FUN_004d2be0)
1. camera, states, light; blend 1; clear 0xff000000.
2. clouds for i = 10..1, exactly as ri.
3. sea: pos(0,-50,1000); uv_v(seaUV) **and uv_u(seaUV)**; render.
4. **blend 2**; horizon drawn **once** at pos(0, 100, 1000); then blend 1. There is no explicit Z state re-set.
5. rain2: pos(0,0,0); uv_v(-rainV + 0.5); render (no extra fog() call).
6. FUN_004bfdd0.

#### ri vs se differences
| | ri | se |
|---|---|---|
| textures | ri_* | se_* |
| unused rain sphere radius | 240 | 300 |
| drawn rain sphere radius | 480 | 600 |
| horizon | light NONE (0x400), fog NONE, drawn twice at x=+-1500 with blend 1 | default light DIR, fog VIEW, drawn once at x=0 with **blend 2** |
| sea scroll | V only | U and V |
| non-match fov | 70 | 50 |
| camera time base | stage frame + 500 | match frame DAT_008858f8 (the camera is frozen outside matches if the match counter does not run) |
| frame counter | incremented | never incremented |
| sound | SE 0x36 when frame%350==300 in gameMode 2 | none |
| fog() re-issued before rain | yes | no (no effect, same values) |

Preview note: the single se horizon is only 3000 wide, spanning x from -1500 to 1500 at z=1000. Its left/right edge therefore becomes visible at the extremes of the pan (for example matchFrame 1700, `png/s10_t1700_z1.0.png`). This is literal behaviour and is presumably also visible on PC (UNVERIFIED against the PC game).

#### Cost
About 17.0k triangles (4900 + 7220 + 4802 + 32), at most 14 commands (the preview shows 4 to 9). Same fill concern as ri.

### Stage 11 "br" — Big Bridge (src/stage/st_br.c)

#### Functions
| role | address |
|---|---|
| loader (Game Init case 11) | FUN_004c1ff0 |
| unloader | FUN_004c2710 (deletes the 8 models / 6 textures) |
| init (0x87a8a4) | FUN_004c2db0 |
| reinit (0x87a8f4) | FUN_004c2ed0 |
| exit (0x87a944) | FUN_004c2e80 (deletes camera) |
| update (0x87a994) | FUN_004c2f30 |
| draw (0x87a9e4) | FUN_004c2fb0 (only param 1 branch exists) |
| camera | FUN_004c2ab0(far) |
| render states + fog | FUN_004c2990 |
| light | FUN_004c2a10 |
| private depth surface create / delete | FUN_004c2930 / FUN_004c2960 (FUN_004749f0(W,H) = D3D8 CreateDepthStencilSurface D16, global DAT_0087ab08) |
| mirror + clip planes | FUN_0046a0a0(p1,p2,p3,p4) |

#### Assets (loader order)
| global | asset | parameters | states |
|---|---|---|---|
| DAT_0087aab8 | mdl\bridge.x (7616 tris, 6 materials) | — | fog 0x20000000 |
| DAT_0087aabc | mdl\bridgeR.x (7616 tris, 6 materials) | — | fog 0x20000000 |
| DAT_0087aac0 / tex aac4 | sphere(3000,3000,3000,100,100) + bg_sky | DAT_008147d8 = 3000 | fog 0x10000000 |
| DAT_0087aac8 / tex aacc | grid_xz(4000,4000,50,50,10) + bg_fog | DAT_008147e0, DAT_00813f4c | fog 0x20000000 — **never drawn** |
| DAT_0087aad0 / tex aad4 | grid_xz(8000,8000,50,50,20) + bg_sea | DAT_008147ec, DAT_008140fc | fog 0x20000000 |
| DAT_0087aad8 / tex aadc | grid_xz(2000,2000,10,10,1) + bg_shadow | DAT_008145fc, DAT_00853b34 | fog 0x20000000 |
| DAT_0087aae0 / tex aae4 | plane(256,256) + bg_lamp | DAT_00814160 | fog 0x20000000 |
| DAT_0087aae8 / tex aaec | plane(164,164) + bg_lamp2 | DAT_008147c8 | fog 0x20000000 |
All textures from /bgMdl/ (folder 2). Light type stays default 0x800 everywhere. The .x materials keep their own textures/colours (no +0xb0 on the bridge models).

#### Variables
DAT_0087aab0 frame; DAT_0087aab4 sky yaw (read only, never written -> 0); eye DAT_0087aaf0/af4/af8 = (0, 1000 [DAT_00814408], 0); look offset DAT_0087aafc/ab00/ab04 = (0, -0.4 [DAT_008147f0], 1 [DAT_00853b34]); light colour DAT_0087ab0c/10/14 = (0.5, 0.5, 0.4 [DAT_00853b1c, DAT_00813f30]); ambient DAT_0087ab18 = 0.5; camera DAT_0087ab1c. Reinit resets frame, eye and offset only.

#### Update (FUN_004c2f30)
eyeZ -= 10 (DAT_00813f4c); if eyeZ > 1000 -> -= 2000 (DAT_008145fc); if eyeZ < -1000 (DAT_00814604) -> += 2000; frame++. No RNG, no SE, no gameMode check. The bridge is drawn at z = -1000/1000/3000, so the 2000-unit wrap is seamless.

#### Camera (FUN_004c2ab0(far))
- fov = 70 (DAT_008145ec); in match (gameMode 2) 10*zoom + 60 (DAT_00813f4c, DAT_00813a58). persp(fov*0.64 [DAT_008145e4], near 50 [DAT_008140ec], far, DAT_009b1010).
- eye = (eyeX, eyeY, eyeZ), at = eye + offset. In match both get x += sin(frame%5000/5000*2pi)*300 (DAT_008147e4, DAT_008145f0).
- reset, lookat(eye, at, NULL); rot_x(rad2bam(sin(frame%3300/3300*2pi)*0.55)) (DAT_008147dc, DAT_008147c4 — about +-31 deg, the camera periodically looks down at the sea); in match rot_y(rad2bam(sin(frame%1300/1300*2pi)*0.25)) (DAT_008147d0, DAT_00813f24); build; apply.
- Called with far 4000 (DAT_008147e0) at the start and again with far 6000 (DAT_008147e8) before the lamps.

#### Draw (FUN_004c2fb0, layer 1)
1. camera(4000); FUN_004c2990: ALPHABLEND 1, SRCBLEND 5, Z 1, ZWRITE 1, FOG 1, light 0 on, fog(1000, 2500 [DAT_008147d4], 0x96968c); FUN_004c2a10: dir (0,-1,0), ambient 0.5, colour (0.5,0.5,0.4).
2. clear colour 0xff96968c; blend 1; ZWRITE 0; CULL 1 (none).
3. sky: colour (1,1,1,1), pos = eye, rot_y(rad2bam(DAT_0087aab4 = 0)), +0x9c.
4. ZWRITE 1, Z 1. DAT_0087ab08->+0x24 (set private depth surface) and FUN_00463a00(1.0) clear.
5. FUN_0046a0a0 with corners (-4000,0,-4000) (4000,0,-4000) (4000,0,4000) (-4000,0,4000) [DAT_008147f4, DAT_008147e0]: copies the view-proj (DAT_00a52068), D3DXPlaneFromPoints (thunk 0x415994) -> D3DXMatrixReflect (thunk 0x414ea8) into *(DAT_00d92968+0x50) = mirror at y=0; projects the 4 corners to NDC and sets 4 user clip planes (device vtable +0xc0 = SetClipPlane) through each projected edge and the origin.
6. bridgeR three times: colour (a1, r0, g0, b0) (black), pos (0, 200 [DAT_008147cc], 1000 / 3000 / -1000), +0x40 then **+0xa0** (FUN_0046c3a0: sets DAT_00d9296c=1 and draws; FUN_00472cc0 then uses world * mirror). => black, view-fogged reflection of the bridge.
7. DAT_0087e7b4->+0x24 (back to the scene depth surface, created by FUN_004e8d20).
8. ZWRITE 1, CULL 3. sea: colour (0.85 [DAT_00814120],1,1,1), pos (0,0,2000), +0x9c.
9. Z 0. shadow x3: colour (0.5,1,1,1), pos (0,0,0/2000/4000), rot_y(deg2bam(90)).
10. Z 1, ZWRITE 1. bridge x3 at (0,200,1000/3000/-1000) (no colour call).
11. fog(1000, 6000 [DAT_008147e8], 0x000000); camera(6000); Z 1, ZWRITE 0, CULL 1, blend 2 (add).
12. a = sin(matchFrame%50/50*2pi)*0.15 + 0.85 (DAT_008858f8!, DAT_008140ec, DAT_00813f20, DAT_00814120); a second value sin*0.25+0.75 (DAT_00813f24, DAT_00813f3c) is computed and unused. For i=0..9, z = i*2000 - 6000: lamp colour(a,1,1,1) at (200, 1100 [DAT_00814180], z); lamp2 colour(a,...) same pos; lamp at (-200 [DAT_00814600], 1100, z); lamp2 same (colour kept).
13. blend 1; FUN_004bfdd0.

#### Port notes
- The mirror (+0xa0 parent matrix) is folded into the model transform locally (negate rot column 1 and pos.y before sg_render; culling is off at that point). Literal result.
- The 4 user clip planes are NOT reproduced (sg has no clip planes). The mirrored geometry lies below y=0 and the sea (alpha 0.85) is drawn over it; the visible difference should be minor. UNVERIFIED.
- The two depth-surface switches are emitted as sg_clear_z(): the private surface is cleared explicitly; the scene surface has nothing written before it in the background pass (sky ZWRITE off), so restoring it is equivalent to a clear (assumes the scene clears Z before the background pass).

#### Performance (host stats, 8000 ticks, every 10th frame)
sg commands 23 avg / 38 max. Triangles submitted: ~56k avg / 70.8k max — sky sphere 19 800, sea 4 802, shadows 3x200, bridge 3x7 616 + bridgeR 3x7 616 (frustum culling drops some), lamps 80. This is the heaviest of my stages; on 3DS the 100x100 sky sphere and the 3 mirrored bridges are the obvious candidates if it misses 60 FPS.

### Stages 12 "ct" — Capital in Flames, and 14 — End of Winter (src/stage/st_ct.c)

#### 12 vs 14
FUN_004bf480 registers the same functions for both ids (0x87a8a8.. for 12, 0x87a8b0.. for 14), and the Game Init switch (FUN_005f4650) calls FUN_004c3dc0 for both. None of the stage functions reads DAT_0087fadc. **The background code is the same for both**, so whatever makes them different lives outside the stage module (name, BGM). A related quirk elsewhere: FUN_004a7910 (match setup) remaps a chosen stage 12 to 13 (and 17 to 18) when DAT_008940c8 == 0. st_ct.c exports stage_ct and stage_ct2 with the same functions.

#### Functions
| role | address |
|---|---|
| loader | FUN_004c3dc0 |
| unloader | FUN_004c42a0 |
| init | FUN_004c4b70 |
| reinit | FUN_004c4f90 |
| exit | FUN_004c5350 (deletes camera, clears flames, smoke, sources) |
| update | FUN_004c53c0 |
| draw | FUN_004c5630 (param 1 only) |
| states / fog | FUN_004c4430 |
| light(up) | FUN_004c44b0 |
| camera | FUN_004c4570 |
| smoke update | FUN_004c4840 |
| flame update | FUN_004c4a70 |
| std::list helpers (this = ECX, visible only in disassembly) | begin FUN_004c6d30, end FUN_004c6f20, != FUN_0049a210, deref FUN_004c6860, ++ FUN_004c68e0, erase FUN_004c6fa0 (smoke) / FUN_004c6f50 (flame), clear FUN_004c6e20 (smoke) / FUN_004c6dc0 (src) / FUN_004c6d60 (flame), push_back FUN_004c70b0 (src) / FUN_004c70d0 (smoke) / FUN_004c7090 (flame) |

#### Assets
| global | asset | states |
|---|---|---|
| DAT_0087ab24 | mdl\city.x (4544 tris, 8 materials) | fog 0x20000000 |
| DAT_0087ab28 | mdl\city_f.x (4544 tris, 8 materials) | fog 0x20000000 |
| DAT_0087ab2c | mdl\tower.x (3120 tris, 8 materials) | fog 0x20000000 |
| DAT_0087ab30[4] / tex DAT_0087ab40[4] | plane(32,32) [DAT_008140dc] + cy_smoke_00..03 (sprintf "cy_smoke_0%d") | light 0x400, fog 0x10000000 |
| DAT_0087ab50[6] / tex DAT_0087ab68[6] | plane(32,32) + cy_flame00..05 ("cy_flame0%d") | light 0x400, fog 0x10000000 |

The .x materials keep their own textures and colours. The brief mentioned +0xa0 / FUN_0046a0a0 for the city, but FUN_004c5630 uses neither: every model finishes with +0x9c. (They are used by stage 11.)

#### Lists
- 0x87ab9c fire sources (16 B): +0 x, +4 y (never initialised, never read), +8 z, +0xc bool on (=1). Init/reinit push 22 entries (x, z): (600,1200) (680,600) (-600,-650) (-720,-100) (-200,-1300) (-250,-500) (850,50) (-1500,-500) (-50,-600) (-750,300) (-950,500) (1700,200) (120,-900) (1500,-150) (750,-250) (560,700) (700,-550) (100,800) (-800,600) (550,-230) (-560,1000) (-60,700). Constants: DAT_008149b0..DAT_00814a24, DAT_00853bb8 (-100), DAT_00853b90 (100), DAT_00814140 (500), DAT_00814174 (120), DAT_008147cc (200), DAT_008145f0 (300), DAT_008140ec (50), DAT_00814408 (1000), DAT_00814600 (-200). The exact pair per push is in st_ct.c reset_lists().
- 0x87ab94 smoke (28 B): +0 x, +4 y, +8 z, +0xc life (alpha), +0x10 red, +0x14 scale, +0x18 anim (int).
- 0x87aba4 flames (24 B): +0 x, +4 y, +8 z, +0xc alpha, +0x10 scale, +0x14 age (int).

#### Init / reinit
- init: frame DAT_0087ab20 = 0. Light colour DAT_0087ab80/84/88 = (0.75 [DAT_00813f3c], 0.25 [DAT_00813f24], 0.25). Ambient DAT_0087ab8c = 0.5. Creates camera DAT_0087ab90. Then clears smoke, clears sources, pushes the 22 sources and clears flames.
- reinit: the same without the light values and the camera.

#### Update (FUN_004c53c0)
1. If frame%10 == 0, for each source with on set: push a smoke {x, 0, z, life 1, red 1, scale 0.5 [DAT_00853b1c], anim = randi(0,40)}.
2. If frame%9 == 0, for each source: x = rand(-25,25) + src.x, then z = rand(-25,25) + src.z (DAT_008149e0, DAT_008140f0), y = 0, alpha 1, scale = rand(0,1) + 1, age 0, and push a flame. RNG order per source is x, z, scale.
3. Smoke (FUN_004c4840):
   - anim = (anim+1)%4000; y += 1
   - x += rand(-1,1)*scale, then z += rand(-1,1)*scale
   - scale += 0.01 [DAT_00813ec8]
   - red -= 0.005 [DAT_00813eac], clamped to >= 0
   - life -= 0.0025 [DAT_008145e0]
   - erased when life <= 0, i.e. after 400 ticks
4. Flames (FUN_004c4a70): age++; scale += 0.01; alpha = 1 - age/120 [DAT_00814174]; erased when alpha <= 0, i.e. after 120 ticks.
5. frame++.

All RNG calls use the cosmetic stream. A live smoke costs 2 calls per tick, so the steady state uses about 1760 calls per tick. Update plays no SE and has no gameMode checks. Steady state is about 880 smoke and 293 flames.

#### Camera (FUN_004c4570)
- fov 70 (DAT_008145ec); in a match, 10*zoom + 50 (DAT_00813f4c, DAT_008140ec).
- a = (frame%8000)/8000*2pi (DAT_008147ec). eye = (cos a*1000, 1000, sin a*1000) (DAT_00814408); at = (0, 250 [DAT_008149ac], 0).
- persp(fov*0.64, 50, 4000 [DAT_008147e0], aspect); reset; lookat(eye, at, NULL).
- rot_x(rad2bam(sin(frame%3300/3300*2pi)*0.15)) (DAT_008147dc, DAT_00813f20).
- Match only: rot_z(rad2bam(sin(frame%2700/2700*2pi)*0.1)) (DAT_008149dc, DAT_00853b10), then rot_y(rad2bam(sin(frame%1300/1300*2pi)*0.15)) (DAT_008147d0).
- build; apply.

#### Draw (FUN_004c5630)
1. camera. FUN_004c4430: ALPHABLEND 1, SRCBLEND 5, Z 1, ZWRITE 1, FOG 1, light on, fog(1000, 2500 [DAT_008147d4], black). FUN_004c44b0(0): dir (0,-1,0), ambient 0.5, colour (0.75,0.25,0.25).
2. Clear 0xff000000; Z 0, ZWRITE 0, CULL 3. City: scale(1, 2 [DAT_00813a4c], 1), colour (1,1,1,1), pos 0. It is drawn with Z off.
3. glow = sin(frame%50/50*2pi)*0.15 + 0.85, using the stage frame DAT_0087ab20 (stage 11 uses matchFrame here). ZWRITE 0, Z 0, blend 2 (add). city_f: colour (a = glow*0.25, r 1, g 0.25, b 0), pos 0.
4. blend 2, Z 0, ZWRITE 0, CULL 1. Flames: k = min(age/20, 5) picks the flame model; colour (alpha/2 [DAT_00813a4c], 1, 1, 1); scale(scale, scale, scale); pos. Arguments checked in disassembly at 0x4c5964.
5. blend 1, Z 1, CULL 1 (ZWRITE stays 0). Smoke: model (anim/10)%4; colour (a = life, r = red, g 0, b 0); scale; pos. Arguments checked in disassembly at 0x4c5b7b.
6. ZWRITE 1, Z 1, CULL 3. FUN_004c44b0(1): light dir (0,+1,0), i.e. lit from below. Tower at 0.
7. FUN_004bfdd0.

The sprites are plain XY planes, not billboards. As the camera orbits they are seen from changing angles and sometimes edge-on; this is the original behaviour.

#### Port notes
- The port is literal.
- The lists are fixed arrays with caps 22 / 1024 / 512, which are never reached.
- The port has no unload function; sg_free_all frees everything.

#### Performance (host stats, 8000 ticks, 3DS aspect)
- Triangles: 13.1k–13.8k (city 4544 + city_f 4544 + tower 3120 + 2 per sprite).
- **sg commands: 453–636 on average, 817 at most (zoom 2.0)**, one per visible sprite after frustum culling. That is below SG_MAX_CMDS (1024) with only about a 20% margin. On 3DS the cost is mostly per-draw overhead, not triangles.

### Stage 13 "iw" — Whirlpool of Malice (src/stage/st_iw.c)

#### Functions
| role | address |
|---|---|
| loader | FUN_004cd0e0 |
| unloader | FUN_004cd430 |
| init | FUN_004cd9e0 |
| reinit | FUN_004cdb30 |
| exit | FUN_004cdc10 (deletes camera, clears vector) |
| update | FUN_004cdc60 |
| draw | FUN_004cdde0 (param 1 only) |
| states | FUN_004cd520 |
| light | FUN_004cd570 |
| camera(far) | FUN_004cd610 |
| rock spawn | FUN_004cd770 (push_back FUN_004ceef0) |
| rock move/erase | FUN_004cd910 (size FUN_004cef10, [] FUN_004ceba0) |

#### Assets
| global | asset | states |
|---|---|---|
| DAT_0087b37c / tex b380 | sphere(500, 2000, 500, 50, 50) [DAT_00814140, DAT_008145fc] + iw_wind | fog 0x20000000, light 0x400 |
| DAT_0087b384 / tex b388 | sphere(2000, 2000, 2000, 100, 100) + iw_sky | fog 0x10000000, light 0x400 |
| DAT_0087b38c / tex b390 | box(100, 100, 100) [DAT_00853b90] + iw_rock | defaults (light 0x800, fog none) |

All from /bgMdl/. iw_fog.png exists in that folder but is never loaded.

#### Variables
- DAT_0087b360: frame
- DAT_0087b364: spawn counter
- DAT_0087b368 / b36c: wind uv V / U
- DAT_0087b370, b374: advanced every tick, never used
- DAT_0087b378: angle, advanced every tick, never used
- eye DAT_0087b3a8/ac/b0 = 0
- look offset DAT_0087b3b4/b8/bc = (0, 10 [DAT_00813f4c], 0)
- light colour DAT_0087b394/98/9c = (0.75 [DAT_00813f3c], 0.25 [DAT_00813f24], 0.25)
- ambient DAT_0087b3a0 = 0.5 [DAT_00853b1c]
- camera DAT_0087b3a4

#### Rocks (std::vector, 32 B per element)
Layout: +0 x, +4 y, +8 z, +0xc type (spawn counter % 3), +0x10 always 0, +0x14/+0x18/+0x1c rx/ry/rz (radians, never change).

Spawn (FUN_004cd770):
1. x = rand(-1000,1000), then z = rand(-1000,1000) (DAT_00814604, DAT_00814408).
2. If x + z < 200 (DAT_008147cc): if |z| <= |x| set z = 300, else set x = 300 (DAT_008145f0). Quirk: the test is on the signed sum, not a distance.
3. y = eyeY - 100.
4. rx, ry, rz = rand(-pi, pi) each, in that order (DAT_00814100, DAT_00853b58).
5. type = counter % 3; counter++.

Move (FUN_004cd910): y += 2.5 (DAT_008153e0); a rock is erased when y > eyeY + 1000. A rock lives 440 ticks, so about 88 are alive at once.

#### Init / reinit
Both reset every variable, clear the vector and pre-simulate 1000 ticks: spawn when i%5 == 0, then move. The pre-simulation uses 800 cosmetic RNG values. init also sets the light values and creates the camera, after the pre-simulation.

#### Update (FUN_004cdc60)
1. b378 += 0.001 (DAT_00814bd8); if > pi, subtract 2pi.
2. b368 += 0.0025 (DAT_008145e0), b36c += 0.0015 (DAT_008153dc), b370 += 0.004 (DAT_00813ea4), b374 += 0.002 (DAT_008145dc); each wraps by subtracting 1 when > 1.
3. If frame%5 == 0, spawn.
4. move; frame++.

No SE, no gameMode checks.

#### Camera (FUN_004cd610)
- fov 120 (DAT_00814174); in a match, 20*zoom + 100 (DAT_008140fc, DAT_00853b90).
- persp(fov*0.64, near 10 [DAT_00813f4c], far 4000, aspect); reset.
- lookat(eye (0,0,0), at (0,10,0), up (0,1,0)): the camera looks straight up. Because the view direction equals up, LookAt falls into its degenerate case and uses up = (0,0,-1).
- build; apply. The camera is not animated.

#### Draw (FUN_004cdde0)
1. camera(4000). FUN_004cd520: ALPHABLEND 1, SRCBLEND 5, Z 0, ZWRITE 0, FOG 0, light on. FUN_004cd570: dir (0,-1,0), ambient 0.5, colour (0.75,0.25,0.25).
2. CULL 1. Sky: colour 1, pos eye.
3. blend 2 (add); FOG 1; fog(1000, 3000 [DAT_008147d8], black).
   - Wind pass 1: pos eye, colour (0.25,1,1,1), scale (5, 1.5, 5) [DAT_00813f40, DAT_00814110], uv_v(b368), uv_u(b36c).
   - Wind pass 2: colour (0.5,1,1,1), scale (2, 1.5, 2) [DAT_00813a4c], same offsets. b370/b374 are not used here either.
4. blend 1; ZWRITE 1; Z 1; fog(100, 1000, black). This fog has no effect because the rocks have fog type none.
5. Each rock:
   - alpha a = 1, or (eyeY + 1000 - y)/400 when y > eyeY + 600 (DAT_008149b8, DAT_008145f4)
   - colour (a,1,1,1); pos (x + eyeX, y, z + eyeZ)
   - rot_x / rot_y / rot_z(rad2bam(r))
   - scale by type: type 0 (1, 0.08, 0.08) [DAT_00813f00]; type 1 (0.6, 0.3, 0.05) [DAT_00813f34, DAT_00813f28, DAT_00813ef4]; type 2 (0.25, 0.25, 0.25)
6. FUN_004bfdd0.

#### Port notes
The port is literal. The vector is a fixed array with a cap of 256, which is never reached.

#### Performance (host stats, 8000 ticks)
- sg commands: 28–41 on average, 51 at most.
- Triangles: about 30k (sky 19 800 + 2 × wind 4 900 + 12 per rock). The 100×100 sky sphere is the main cost.

### Stage 15 "ft": Nature 10,000 Years After (src/stage/st_ft.c)

#### Functions
| role | address |
|---|---|
| loader | FUN_004c86f0 |
| unloader | FUN_004c88c0 |
| init | FUN_004c8cf0 |
| reinit | FUN_004c8d90 |
| exit | FUN_004c8dd0 |
| update | FUN_004c8e20 |
| draw | FUN_004c8f40 |
| camera | FUN_004c8a50 |
| render states | FUN_004c8960 |
| light | FUN_004c89b0 |

#### Globals
| addr | meaning |
|---|---|
| DAT_0087ac20 | int frame counter (incremented, never read) |
| DAT_0087ac24 | camera target bob phase |
| DAT_0087ac28 | camera roll phase (0..2pi) |
| DAT_0087ac2c | sky sphere yaw |
| DAT_0087ac30 | cloud yaw |
| DAT_0087ac34 / ac3c | textures ft_forest / ft_cloud2 |
| DAT_0087ac38 / ac40 | models forest sphere / cloud grid |
| DAT_0087ac44/48/4c, ac50 | light colour (1,1,1), ambient 1 |
| DAT_0087ac54 | CCamera |

#### Assets (/bgMdl/)
| model | primitive | texture | light | fog |
|---|---|---|---|---|
| forest 0x87ac38 | sg_sphere(600 [DAT_008149b8], 400 [DAT_008145f4], 600, 40, 40) | ft_forest | DIR | NONE |
| cloud 0x87ac40 | sg_grid_xz(900 [DAT_00814d74], 900, 20, 20, 1.0) | ft_cloud2 | DIR | NONE |

ft_sky.png and ft_cloud.png exist in the archive but this stage does not load them. ml also uses ft_cloud2.

#### Init/reinit/exit
- init: all phases and frame = 0, light colour and ambient 1.0 [DAT_00853b34], camera.
- reinit: phases and frame = 0.
- exit: delete camera.

#### Update (FUN_004c8e20)
```
camY    += 0.0025  [DAT_008145e0];  > pi  -> -= 2pi
roll    += 0.00075 [_DAT_00814bd4]; > 2pi -> -= 2pi   (wraps at 2pi)
skyAng  += 0.002   [_DAT_008145dc]; > pi  -> -= 2pi
cloudAng+= 0.0035  [DAT_00814d6c];  > pi  -> -= 2pi
frame++
```
No RNG, no SE.

#### Camera (FUN_004c8a50)
- fov: default 70 [DAT_008145ec]; gameMode 2: 15 [DAT_00853b88]*zoom + 40 [DAT_008140e0]; gameMode 4: 90 [DAT_00814170]. Multiplied by 0.64; near 10, far 2000.
- eye (0, 0, -200 [DAT_00814600]); target (0, sin(camY)*40 + 50 [DAT_008140ec], 0).
- After lookat, a roll is applied (vtable +0x30, sg_cam_rot_z):
  - roll < pi/2 [DAT_00814104]: rad2bam((sin(roll) - 1) * 0.35 [DAT_00813f2c])
  - roll > 3pi/2 [DAT_00814d70 = 4.712389]: rad2bam((sin(roll) + 1) * -0.35 [DAT_00814d78])
  - otherwise: rad2bam((|sin(roll)| - 1) * -0.35)
  - The result is a continuous rocking between -0.35 and +0.35 rad (about +-20 degrees) with a period of 2pi/0.00075, about 8378 ticks. It starts at -20 degrees after init.
- Then build, apply.

#### Render states (FUN_004c8960)
ALPHABLEND 1, SRCBLEND 5, **ZENABLE 1, ZWRITE 1**, FOG 1, light 0 on. No fog range or colour is set; this does not matter because all meshes are fog NONE. Cull is not set, so it stays as the previous FUN_004bfdd0 left it (none).

#### Light (FUN_004c89b0)
dir (0,-1,0), ambient 1, colour (1,1,1).

#### Draw (FUN_004c8f40)
1. camera, states, light; blend 1 (no colour clear).
2. forest: reset; pos 0; rotY(rad2bam(skyAng)); render.
3. ZENABLE 0.
4. cloud: reset; pos(0, 100 [DAT_00853b90], 0); rotY(rad2bam(cloudAng)); render.
5. FUN_004bfdd0.

#### Cost
3120 + 722, about 3.8k triangles, 2 commands. Cheap.

### Stage 16 "gz" — Crashed Spaceship (src/stage/st_gz.c)

#### Functions
| role | address |
|---|---|
| loader | FUN_004ca360 |
| unload | FUN_004ca870 (5 textures, 5 models, CPerticle) |
| init | FUN_004cafc0 (frame 0, list clear FUN_004cbed0, light colour/ambient = 1, camera FUN_00465c30) |
| reinit | FUN_004cb040 (frame 0, list clear) |
| exit | FUN_004cb060 (delete camera, list clear) |
| update | FUN_004cb0b0 |
| draw | FUN_004cb0f0 |
| camera | FUN_004cab60 |
| states | FUN_004caa20 |
| light | FUN_004caaa0(up) |
| particle spawn | FUN_004cad10 (push_back FUN_004cbf80 -> FUN_004cbb80, inserts before the head node) |
| particle update | FUN_004cae90 (erase FUN_004cbf30) |

Globals: frame DAT_0087acd0; light colour DAT_0087ad00/04/08, ambient DAT_0087ad0c; camera DAT_0087ad10;
std::list of particles at DAT_0087ad14.

#### Assets (loader FUN_004ca360, all textures folder 3 = /bgMdlAdd/)
| global | asset | primitive | settings |
|---|---|---|---|
| acd4/acd8 | gz_sky (512²) | grid_xy(2000, 1000, 20, 20, 1.0) (`0x8145fc`, `0x814408`) | defaults |
| acec/acf0 | gz_particle (64²) | grid_xz(1000, 1000, 4, 4, 1.0) | defaults ("orbs") |
| acf8/acf4 | gz_particle | CPerticle FUN_00475660(1000, 2) | +0x1c texture |
| acdc/ace0 | gz_forest (512², alpha) | grid_xz(1200, 1200, 20, 20, 1.0) (`0x8149d0`) | fog 0x20000000 |
| ace4/ace8 | gz_base (512²) | grid_xz(1200, 1200, 20, 20, 1.0) | fog 0x10000000 |
| acfc | mdl\ship.x (no texture; 1 subset, 3792 tris) | — | fog 0x20000000 |

#### Camera FUN_004cab60
fov = 70 (`0x8145ec`); in match (gameMode 2) 10·zoom + 30 (`0x813f4c`, `0x8140e8`).
persp(fov·0.64 (`0x8145e4`), 10, 2500 (`0x8147d4`), aspect); reset; lookat eye (0, 750, −750)
(`0x8149c4`, `0x814a10`), at (0,0,0), up NULL; rot_x(rad2bam(sin((frame%3300)/3300·2π)·0.15 − 0.25))
(`0x8147dc`, `0x813f20`, `0x81411c`); build; apply. The pitch swings between −0.4 and −0.1 rad (camera tilts
up; at the extreme the ship sinks to the bottom edge — literal).

#### States / light
FUN_004caa20: 0x1b=1, 0x13=5, Z off, ZWRITE off, FOG on, light0 on, fog(1000, 1500, 0xff000000)
(`0x814408`, `0x8149d4`). FUN_004caaa0(up): dir (0, up==1 ? 1 : −1 (`0x853bb4`), 0), ambient ad0c,
colour (ad00, ad04, ad08), light0 on.

#### Draw FUN_004cb0f0 (param 1 only; param 0 draws nothing)
1. camera, states, light(0); clear 0xff000000; blend 1.
2. sky: reset, pos (0, −200, 1000) (`0x814600`, `0x814408`), render.
3. light colour (0.54901963, 0.74509805, 1) (`0x8150d0`, `0x8150d4`); ambient = sin((frame%500)/500·2π)·0.25 + 0.35
   (`0x814140`, `0x813f24`, `0x813f2c`); light(1) (lit from below). Z on, ZWRITE on, cull 3.
   ship: reset, colour (1,1,1,1), scale 5 (`0x813f40`), rot_y(FUN_0045a0f0(frame%3600)/10) (int BAM, not masked
   to 16 bits — computed locally since sg_deg2bam returns uint16; 0.1°/frame),
   pos (0, sin((frame%400)/400·2π)·5 + 50, 0) (`0x8145f4`, `0x8140ec`).
4. light colour/ambient back to 1, light(0). Z off, ZWRITE off, cull 3, blend 2 (additive).
   4 orbs i = 0..3: a = −((i·900 + frame)%3600)/3600·2π (`0x853e00` sign flip, `0x8150d8`),
   pos (cos a·120, 60, sin a·120) (`0x814174`, `0x813a58`), scale 0.03 (`0x813ee0`) → 30×30,
   colour a=1 r=0.5 g=1 b=0.75 (`0x853b1c`, `0x813f3c`).
5. Z on. base (still blend 2): colour alpha sin((frame%500)/500·2π)·0.25 + 0.75, rgb 1; pos (0, −100, 0) (`0x853bb8`).
6. blend 1, Z on, ZWRITE off, cull 3. forest layers i = 10..0: colour alpha i·0.04 + 0.125 (`0x813eec`, `0x813f1c`),
   scale 1 − i·0.065 (`0x8150cc`), pos (0, −50 − i·5, 0) (`0x8149e4`, `0x813f40`), rot_y(deg2bam((i·36)%360)).
7. particles: +0x28 begin; SetRect(0,0,63,63); for each list element (at most 1000):
   x = cos(ang)·radius + px, y = py, z = sin(ang)·radius + pz, size 10×10 (`0x813f4c`),
   colour ((int)(255·alpha) << 24) | 0xffffff (`0x853b94`); +0x2c end.
   FUN_00475b10 (the CPerticle draw) itself calls FUN_004637d0(kind) with kind = 2 → additive. sg ignores the
   kind, so the port calls sg_blend(2) right before sg_particles_end (same global effect).
8. FUN_004bfdd0.

#### Update FUN_004cb0b0
particle update FUN_004cae90; `if (frame % 5 == 0)` spawn FUN_004cad10; frame++.
Particle element (0x24 bytes): +0 age (int), +4 x, +8 y, +0xc z, +0x10 ang, +0x14 spin (±0.05, never read),
+0x18 radius, +0x1c dr, +0x20 alpha.
* update: age++, radius += dr, y += 1, if age > 30 alpha −= 0.01 (`0x813ec8`); erase if !(alpha > 0).
* spawn (cosmetic RNG, this order): r = rand(50, 200) (`0x8140ec`, `0x8147cc`); a = rand(−π, π)
  (`0x814100`, `0x853b58`); pos (cos a·r, −50, sin a·r); ang = rand(−π, π); spin = 0.05 (`0x813ef4`), negated if
  randi(0, 100) < 50; radius 0; dr = rand(1, 10)·0.1 (`0x853b10`); alpha 1.
  Lifetime ≈ 131 frames → ≈ 27 particles alive.
No sound effects; the only gameMode check is the camera fov.

#### Performance (per frame)
≈ 16–20 commands; ≈ 14.4k triangles (sky 800, ship 3792, orbs 4×32, base 800, forest 11×800 = 8800 in
alpha-blended, mostly overlapping layers — the fill-rate hot spot), ≈ 27 particle quads.

### Stage 17 "hl" — Guardian's Chamber (src/stage/st_hl.c)

#### Functions
| role | address |
|---|---|
| loader | FUN_004cbfa0 |
| unload | FUN_004cc1a0 (2 textures, 2 models) |
| init | FUN_004cc490 |
| reinit | FUN_004cc540 |
| exit | FUN_004cc590 (delete camera) |
| update | FUN_004cc5e0 |
| draw | FUN_004cca90 |
| camera | FUN_004cc330 |
| states | FUN_004cc240 |
| light | FUN_004cc290 |
| 2D rotate | FUN_005f1210(v, a): (x, y) ← (x·cos a − y·sin a, x·sin a + y·cos a) |
| CModel +0x64 | FUN_0046d590(pos*, at*, up*) — orientation, see below |

Globals: frame DAT_0087ad20, camAng ad24, wallAng ad28, pulse ad2c; ring tables (stride 0x10: x, y, z, pad):
ring1 DAT_0087ad30 (36), ring2 DAT_0087af70 (36), ring3 DAT_0087b1b0 (24); tilt1 DAT_0087b330, tilt2 b334;
light colour b348/b34c/b350, ambient b354, camera b358.

#### Assets (folder 2 = /bgMdl/)
* hl_wall (512²) on sphere(600, 600, 600, stacks 20, slices 100) (`0x8149b8`), light type 0x400 (material colour).
* hl_cube (256²) on box(16, 16, 16) (`0x8140d8`), default light type 0x800.

#### CModel +0x64 (FUN_0046d590) — not in sg
f = normalize(at − pos); up = (0,1,0) if NULL; if f = (0, ±1, 0): up = (0, 0, −f.y);
s = −normalize(f × up); y = normalize(f × s); rows +0x84 = s, +0x78 = y, +0x6c = f; position +0x90 = pos.
Implemented locally (model_orient in st_hl.c and st_sh.c, writes sg_model.rot / pos). If at == pos the original
divides by zero (NaN matrix); the port keeps the reset orientation. This only happens on a draw before the
first update, when the ring tables are still 0 (all boxes at the origin). Candidate for an sg core function.

#### Camera FUN_004cc330
fov 75 (`0x815254`), in match 10·zoom + 40 (`0x813f4c`, `0x8140e0`); persp(fov·0.64, 10, 2000 (`0x8145fc`), aspect);
eye (0, sin(camAng)·180 + 0 (`0x815258`, PTR_00853af4 = 0.0), −120 (`0x81525c`)), at (0,0,0); build; apply.

#### States / light
FUN_004cc240: 0x1b=1, 0x13=5, Z on, ZWRITE on, FOG on (no fog parameters are set; all models have fog type none).
FUN_004cc290: dir (0, 1, 0), ambient b354, colour (b348, b34c, b350) = init (0.5, 1, 0.85) (`0x853b1c`, `0x814120`).
Init ambient 1.0; every update sets ambient = pulse.

#### Update FUN_004cc5e0
camAng += 0.0025 (`0x8145e0`), wallAng += 0.001 (`0x814bd8`), both wrapped at π (−2π).
pulse = sin((frame%200)/200·2π)·0.2 + 0.8 (`0x8147cc`, `0x853b14`, `0x8145e8`); ambient = pulse.
ring1 i < 36: t = (frame%1000)/1000·2π; x = cos(i/36·2π + t)·250, z = sin(...)·250 (`0x8140f4`, `0x8149ac`),
y = 0, then rotate (x, y) by sin(tilt1)·0.75 (`0x813f3c`). If frame%300 > 249: tilt1 += 0.01 (`0x813ec8`), wrap at π.
ring2 i < 36: t = (frame%1300)/1300·2π (`0x8147d0`); angle i/36·2π − t; radius 280 (`0x814f14`); rotate by
sin(tilt2)·0.75. If frame%300 < 50: tilt2 −= 0.01, wrap below −π (`0x814100`).
ring3 i < 24: (cos(i/24·2π)·120, −200, sin(i/24·2π)·120) (`0x853b8c`, `0x814174`, `0x814600`) — static.
frame++. No RNG, no SE, gameMode only in the camera.

#### Draw FUN_004cca90 (param 1)
camera, states, light. Wall: reset, pos 0, colour (a 1, rgb pulse), rot_y(rad2bam(wallAng)), render
(blend / cull are not set: inherited from the previous frame's FUN_004bfdd0 → blend 1, cull none).
Z on, ZWRITE on, cull 3.
ring1: per box reset, pos, +0x64(pos, origin, up = ring1[(i+1)%36] − ring1[i]), render; then Z off + blend 2,
+0x64 again, render (additive glow pass); Z on, blend 1.
ring2: same orientation rule, single pass. ring3: +0x64(pos, (0, −200, 0), (0, 1, 0)), single pass. FUN_004bfdd0.
Cube top faces render black: light dir (0,1,0) means the light comes from below, and the 0x800 shader has no
max(0, N·L) (N·L + ambient < 0 → clamped 0) — consistent with stages.md §3.3.

#### Performance
Wall 4000 tris + up to 132 box draws (+36 glow passes) × 12 tris ≈ 6k tris. Up to 133 commands (draw before the
first update, all boxes at the origin); 17–27 commands in normal play after frustum culling. One command per box.

### Stage 18 "ml": Moonlight Dance Hall (src/stage/st_ml.c)

#### Functions
| role | address |
|---|---|
| loader | FUN_004cef60 |
| unloader | FUN_004cf5b0 |
| init | FUN_004cfb10 |
| reinit | FUN_004cfcf0 |
| exit | FUN_004cfe70 (deletes camera, clears star vector) |
| update | FUN_004cfec0 |
| draw | FUN_004cff60 |
| camera | FUN_004cf8a0 |
| render states | FUN_004cf780 |
| light | FUN_004cf800 |
| star vector | 12-byte elements (x,y,z floats); at FUN_004d0af0, size FUN_004d0db0, push_back FUN_004d0d90 -> FUN_004d0670, clear FUN_004a9f10 |

#### Globals
| addr | meaning |
|---|---|
| DAT_0087b3d0 | int frame counter |
| DAT_0087b3d4 | ml_cloud sphere yaw |
| DAT_0087b3d8 | phase animated in update, never read |
| DAT_0087b3dc/3e4/3ec/3f4/3fc/404 | textures ml_sky / ml_cloud / ml_cloud2 / ft_cloud2 / ml_moon / ml_star |
| DAT_0087b3e0/3e8/3f0/3f8/400/408 | models sky / cloud / cloud2 (unused) / ftCloud grid / moon / star |
| DAT_0087b40c/410/414, 418 | light colour 1,1,1; ambient 1 |
| DAT_0087b41c | CCamera |

#### Assets (/bgMdl/)
| model | primitive | texture | light | fog |
|---|---|---|---|---|
| sky 0x87b3e0 | sg_sphere(500 [DAT_00814140], 300 [DAT_008145f0], 500, 20, 20) | ml_sky | DIR | NONE |
| cloud 0x87b3e8 | sg_sphere(400 [DAT_008145f4], 400, 400, 20, 20) | ml_cloud | DIR | NONE |
| cloud2 0x87b3f0 | sg_sphere(495 [DAT_008145f8], 350 [DAT_00814be0], 495, 20, 20) | ml_cloud2 | n/a | n/a, **never drawn** |
| ftCloud 0x87b3f8 | sg_grid_xz(2400 [DAT_00815588], 2400, 20, 20, 1.0) | ft_cloud2 | DIR | NONE (set explicitly) |
| moon 0x87b400 | sg_plane(240 [DAT_00815584], 240) | ml_moon | WHITE (0x100) | NONE |
| star 0x87b408 | sg_plane(1.5 [DAT_00814110], 1.5) | ml_star | WHITE (0x100) | NONE |

#### Init (FUN_004cfb10) / reinit (FUN_004cfcf0)
Both set frame = 0 and 3d4 = 3d8 = 0, clear the star vector, then run 30 iterations that each push two stars. The RNG is FUN_00459fd0, called in the order x, y, z:
```
(  rand(100 [DAT_00853b90], 300 [DAT_008145f0]), rand(0, 200 [DAT_008147cc]), rand(200, 400 [DAT_008145f4]) )
( -rand(100, 300),                                rand(0, 200),                 rand(200, 500 [DAT_00814140]) )
```
The negation is an xor with the DAT_00853e00 sign mask. That gives 60 stars and 180 RNG draws per init/reinit.
init additionally sets light colour and ambient to 1.0 and creates the camera. exit deletes the camera and clears the stars.

#### Update (FUN_004cfec0)
```
frame++
3d4 += 0.0025  [DAT_008145e0]; > pi -> -= 2pi
3d8 += 0.00175 [_DAT_008145d4]; > pi -> -= 2pi   (unused)
```

#### Camera (FUN_004cf8a0)
- fov: default 70; gameMode 2: 10*zoom + 30 [DAT_008140e8]. Multiplied by 0.64; near 10, **far 2500 [DAT_008147d4]**.
- eye (0,0,-200), target (0,0,0), lookat, then:
  - rot_x (vtable +0x2c): rad2bam(sin((frame % 3300)/3300 [_DAT_008147dc] * 2pi) * 0.15 [DAT_00813f20])
  - gameMode 2 only: rot_z (+0x30): rad2bam(sin((frame % 2700)/2700 [_DAT_008149dc] * 2pi) * 0.1 [DAT_00853b10]), then rot_y (+0x28): rad2bam(sin((frame % 1300)/1300 [_DAT_008147d0] * 2pi) * 0.15)
- Then build, apply.

#### Render states (FUN_004cf780)
ALPHABLEND 1, SRCBLEND 5, Z off, ZWRITE off, **FOG 0**, light 0 on, fog(600 [DAT_008149b8], 800 [DAT_008149c8], 0xff5791ff). The fog range and colour are set but fog is disabled.

#### Light (FUN_004cf800)
dir (0,-1,0), ambient 1, colour (1,1,1).

#### Draw (FUN_004cff60)
1. camera, states, light; blend 1 (no clear).
2. sky: reset; pos 0; rotY(rad2bam((frame % 10000)/10000 [_DAT_00853ba0] * 2pi)); rotY(deg2bam(180)); render.
3. blend 2; moon: reset; pos(0, 100, 200); render.
4. stars, still with blend 2: for each star, reset; pos(x,y,z); render. The planes are not billboarded; they all face -z.
5. blend 1; for i = 10 .. 0 (11 layers): s = sin(((i*50 + frame) % 500)/500 [DAT_00814140] * 2pi);
   colour(a 1, r s*0.25 [DAT_00813f24] + 0.75 [DAT_00813f3c], g s*0.15 + 0.85 [DAT_00814120], b 1);
   pos(0, -50 - i*10, 0); rotY(rad2bam((frame % (8000-300i)) / (8000-300i) * 2pi) + deg2bam(36i)); render.
6. cloud sphere: reset; colour(a 0.5, 1,1,1); pos 0; rotY(rad2bam(3d4)); render.
7. FUN_004bfdd0.

#### Cost
sky 760 + moon 2 + 60 stars x 2 + 11 x 722 + cloud 760, about 9.6k triangles. Up to 75 commands (the preview shows 16 to 41 after culling). Fill: 11 overlapping full-width cloud layers plus the translucent cloud sphere. The 60 star draws are 60 tiny commands; the backend could batch them if needed (not changed here).

### Stage 19 "sh" — Sumika's Hideout (src/stage/st_sh.c)

#### Functions
| role | address |
|---|---|
| loader | FUN_004d3d60 |
| unload | FUN_004d4a40 |
| init | FUN_004d5310 |
| reinit | FUN_004d54c0 (init body without light/camera) |
| exit | FUN_004d5610 (delete camera, vector clear FUN_004a9f10, list clear FUN_004cbed0) |
| update | FUN_004d5670 |
| draw | FUN_004d5890 |
| states | FUN_004d4cf0 |
| light | FUN_004d4d40 |
| camera | FUN_004d4de0 |
| particle spawn | FUN_004d5060 (list DAT_0087b5a8) |
| particle update | FUN_004d51e0 |

Globals: frame DAT_0087b520, camYaw b524, camBob b528, roll b52c; light colour b588/b58c/b590, ambient b594;
camera b598; noise std::vector DAT_0087b59c (element 0x1c bytes; FUN_004a9a90 = &v[i], FUN_004aa1a0 = size,
FUN_004aa150 = push_back); particle std::list DAT_0087b5a8.

#### Assets
| global | texture (folder) | primitive | settings |
|---|---|---|---|
| b530/b534 | sh_wall (3, 1024²) | sphere(400, 400, 400, 20, 100) (`0x8145f4`) | light 0x100, fog none |
| b53c/b538 | sh_shpere (3, 512²) | sphere(80, 80, 80, 40, 40) (`0x814164`) | defaults |
| b544/b540 | sh_shpere2 (3) | sphere(80, 80, 80, 40, 40) | defaults |
| b54c/b548 | sh_shpereb (3) | sphere(80, 80, 80, 40, 40) | defaults |
| b554/b550 | sh_atmos (3) | sphere(80, 80, 80, 40, 40) | defaults |
| b578/b568 | sp_noise00 (2, 128²) | grid_xy(40, 40, 2, 2, 1.0) (`0x8140e0`) | light 0x400, fog none |
| b57c..b584 / b56c..b574 | sp_noise01..03 (2) | plane(40, 40) | light 0x400, fog none |
| b564/b560 | sl_roll (2) on mdl\stripe.x | — | |
| b55c/b558 | gz_particle (3, 64²) | CPerticle(1000, 2) | |

#### Init / reinit
frame, camYaw, camBob, roll = 0; vector clear; for i < 36 push {+0 yaw = i/36·2π (`0x8140f4`),
+4 pitch = rand(−0.15, 0.15) − 0.25 (`0x815dc4`, `0x813f20`, `0x81411c`), +8 yaw2 = yaw,
+0xc scale = rand(−0.25, 0.25) + 1.25 (`0x813f24`, `0x815dbc`), +0x10 bright 0, +0x14 timer i·180}
(+0x18 uninitialised). 2 cosmetic RNG draws per element, in this order. List clear.
Init only: light colour 1, ambient 0.75 (`0x813f3c`), camera.

#### Camera FUN_004d4de0
fov = in match 15·zoom + 50 (`0x853b88`, `0x8140ec`), else 80 (`0x814164`); persp(fov·0.64, 10, 2000 (`0x8145fc`), aspect).
eye (cos(camYaw)·300, sin(camBob)·80 + 180, sin(camYaw)·300) (`0x8145f0`, `0x814164`, `0x815258`), at (0,0,0).
In match only: rot_z(rad2bam(sin((frame%2700)/2700·2π)·0.1)) (`0x8149dc`, `0x853b10`), then
rot_y(rad2bam(sin((frame%1300)/1300·2π)·0.15)) (`0x8147d0`, `0x813f20`). build, apply.

#### States / light
FUN_004d4cf0: alpha blend, Z on, ZWRITE on, FOG off, light0 on. FUN_004d4d40: dir (0, −1, 0), ambient b594,
colour b588..b590.

#### Update FUN_004d5670
camYaw += 0.0025 (`0x8145e0`) wrap π; camBob += 0.01 (`0x813ec8`) wrap π; particle update; if frame%5 == 0 spawn;
roll += 0.0025, if > 1: −1; for each noise: timer++; if timer%500 < 350: bright −= 0.01 (clamped to 0) else
bright += 0.01 (clamped to 1); frame++.
Particles: identical to gz FUN_004cad10/FUN_004cae90 except spawn y = −350 (`0x815dc8`). No SE.

#### Draw FUN_004d5890 (param 1)
1. camera, states, light; blend 1; clear 0xff5ab0ff; Z off, ZWRITE off.
2. stripe: pos (0, −400, 0) (`0x815bc8`), colour (a 0.5, rgb 1), uv_v(−roll) (`0x853e00` sign flip), render.
3. wall: pos 0, render.
4. noise billboards i < 36: model = noise[(i + frame) % 4] (signed C modulo); p = (cos(yaw)·460, sin(pitch)·460,
   sin(yaw2)·460) (`0x815dc0`); reset; scale(s, s, s); colour (a 0.75, rgb bright·0.5 + 0.25); pos p;
   +0x64(p, origin, NULL) (billboard facing the centre, see hl.md); render. They sit outside the radius-400
   wall but Z is off, so they draw over it.
5. Z off, ZWRITE off, FOG off, FUN_004676b0(0, 0) (see note).
   Planet at (0, −100, 0) (`0x853bb8`): sh_shpereb colour (a 1, rgb 0) (blend 1); blend 2;
   k = sin((frame%800)/800·2π)·0.5 + 0.5 (`0x8149c8`); sh_shpere colour (a k, 0.75, 0.75, 1);
   sh_shpere2 colour (a 1 − k, 1, 0.75, 0.75); sh_atmos colour (a 0.5, 1, 1, 1).
6. particles as gz (RECT 0,0,63,63, size 10, kind 2 = additive). FUN_004bfdd0.

**Note (light disable)**: FUN_004676b0 only writes DAT_00e25bd0 (type 3) and DAT_00e25d70[i]; nothing reads
DAT_00e25d70. The 0x800 shader constants are uploaded by FUN_00472480 from the light colour (DAT_00e25bd4..)
unconditionally, so in the original the planet layers stay lit after the "disable". sg zeroes the light colour
of a disabled light (sg.c cmd_new: `if (st.lightOn[0])`), which made the planet black; the port therefore omits
this call. Proposed core fix: copy the light colour regardless of lightOn (the call could then be restored).

#### Performance
≈ 18–21 commands (36 billboards, mostly frustum-culled); ≈ 22k triangles: stripe 5112 + wall 4000 +
4 × 3200 planet spheres (three blended) + ≤ 36 × 2–8 + ≈ 27 particle quads.

### Stage 4 "sl" — menu / title background (src/stage/st_sl.c)

#### Functions
| role | address |
|---|---|
| loader | FUN_004d2f10 |
| unload | FUN_004d3370 (5 textures, 5 models) |
| init | FUN_004d3700 |
| reinit | FUN_004d3790 |
| exit | FUN_004d37c0 (delete camera) |
| update | FUN_004d3810 |
| draw | FUN_004d38b0 |
| states | FUN_004d34f0 |
| light | FUN_004d3540 |
| camera | FUN_004d35e0 |

Globals: frame DAT_0087b4d8, atmosU b4dc, roll b4e0, light colour b50c/b510/b514, ambient b518, camera b51c.
There are no gameMode checks in any of these functions (fixed camera).

#### Assets (folder 2 = /bgMdl/)
* sl_atmos on grid_xz(1800, 1800, 10, 10, 1.0) (`0x814f18`) — b4e8/b4e4, loaded but never drawn.
* sl_shpere on sphere(400, 400, 400, 80, 80) (`0x8145f4`) — b4f0/b4ec.
* sl_shpereb on sphere(400, 400, 400, 80, 80) — b4f8/b4f4.
* sl_atmos (second load) on sphere(400, 400, 400, 80, 80) — b500/b4fc.
* sl_roll on mdl\stripe.x — b508/b504.

#### Init / update
init: frame, atmosU, roll = 0; light colour and ambient 1; camera. reinit: frame, atmosU, roll = 0.
update FUN_004d3810: atmosU += 0.00025 (`0x815bc4`), roll += 0.0025 (`0x8145e0`), each: if > 1 then −1; frame++.

#### Camera FUN_004d35e0
persp(50 (`0x8140ec`)·0.64, 10, 3000 (`0x8147d8`), aspect); eye (0, 750, 1500) (`0x8149c4`, `0x8149d4`),
at (0, −200, 0) (`0x814600`); build; apply.

#### Draw FUN_004d38b0 (param 1)
camera; FUN_004d34f0 (alpha blend, Z on, ZWRITE on, FOG off, light0 off); FUN_004d3540 (dir (0, −1, 0),
ambient b518, colour b50c.., light0 on); blend 1.
stripe: pos (0, −400, 0) (`0x815bc8`), colour (a 0.5, rgb 1), uv_v(−roll), render.
sl_shpereb: pos 0, colour (a 1, rgb v) with v = sin((frame%400)/400·2π)·0.05 + 0.05 (`0x8145f4`, `0x813ef4`), render.
Z off, ZWRITE off, FOG off, FUN_004676b0(0, 0), blend 2:
sl_shpere colour (1, 1, 1, 1), pos 0, render; sl_atmos colour (a 0.5, rgb 1), pos 0, uv_u(atmosU), render.
FUN_004bfdd0.
The FUN_004676b0(0, 0) before the additive spheres is omitted in the port for the same reason as in sh.md
(the flag is never read by the shader path, but sg zeroes a disabled light's colour).
No clear: whatever is behind is the menu's own clear (the host preview shows its default clear colour).

#### Performance
4 commands but ≈ 43.5k triangles per frame: three 80×80 spheres (12 800 tris each; two additive, covering most of
the screen) + stripe 5112. The heaviest of these four stages for the 3DS vertex budget; if the menu drops frames,
lowering the sphere tessellation (e.g. 40×40 as in sh) would be an approximation worth considering.

## 5. 3DS implementation

* `src/stage/st_<code>.c` are literal ports of the functions in §1 (same constants, same RNG
  order on a private cosmetic MT stream seeded at start-up, same SE ids through
  `stage_g.se`). They call `sg.h`, which records draw commands (`sg_cmds`).
* `src/platform/stage.c` executes them with citro3d: one vertex shader (`stage.v.pica`)
  implements the colour formula of §3.3 branch-free (unlit = light (0,0,0,1)); texenv 0 =
  texture × colour; fog = texenv 1 `INTERPOLATE(fog colour, previous, ramp)` where texcoord1 =
  per-vertex fog factor sampled from a 64×8 L8 ramp (1 − f) — exactly D3D's vertex fog blend.
  The D3D WVP is converted to PICA clip space on the CPU (`x' = y, y' = −x` for the rotated top
  screen, `z' = z − w` so depth 1..0 = near..far, D3D LESSEQUAL → GPU_GEQUAL), culling maps
  D3DCULL_CCW → GPU_CULL_BACK_CCW. Clears are ordered commands (full-screen quads); after the
  stage a depth-only quad restores the cleared depth for the citro2d layer.
* Aspect: the stage cameras get 400/240 instead of 800/600, i.e. the same vertical FOV as the PC
  and a wider horizontal view — matching the 2D layer, which keeps the vertical scale
  (`screenScale = 0.4`) and also shows more horizontally.
* Textures: `tools/aos2mesh` converts every `bgMdl`/`bgMdlAdd` PNG and the 11 LAG textures to
  mipmapped `.t3x` (ETC1 if opaque, else ETC1A4; non-power-of-two images resampled), height maps
  to `stage/hf/*.bin` (red bytes), `.x` files to `stage/mdl/*.bin`.
* Each round the app detects the match-frame counter restarting and calls the stage re-init
  (Game Init's `FUN_004bfc10` on an active stage).
* The arena ring `FUN_00619ef0` (first item of the 2D layer) is drawn with citro2d triangles in
  `render.c` (72 segments, orange `0xff8040`, alpha `(int)(sin t·32)+0x80` at r = 540 fading to 0
  at 540 + (80 − 20 sin t) and 540 − (20 − 5 sin t), t = (frame % 100)/100·2π).
* Performance work (all output-identical apart from 8-bit vertex-colour quantisation of
  batched meshes):
  - recording (`sg.c`): the 65536-entry sine table of the original instead of `sinf`; the
    object-space light direction uses a closed-form 3×3 inverse and only for lit meshes;
    `view×proj` is computed once per `sg_cam_apply`; commands point to per-frame camera
    snapshots instead of copying matrices; small meshes are frustum-tested by their vertices.
  - backend (`platform/stage.c`): a GPU state cache (uniforms, texenv, blend, depth, cull,
    attribute layout are only re-sent when they change — height-field chunks share
    everything); consecutive small meshes (≤ 24 vertices: planes, boxes, billboards) with the
    same texture/blend/depth/cull/fog/camera are transformed to world space and coloured on the
    CPU with the §3.3 formula, then drawn with one call, in the original order.
  - sprite animations that a stage draws interleaved (city smoke `cy_smoke_00..03`, flames
    `cy_flame00..05`, sh noise `sp_noise00..03`) are additionally packed into atlases by
    aos2mesh (`stage/tex/atlas.txt`); a batched mesh samples the atlas cell when its uv stay in
    [0,1] without scrolling, otherwise its own texture.
  - `C3D_Init` uses a 4× command buffer, and the stage stops issuing draws at 60 % usage so the
    2D layer always fits (a full buffer hung Capital in Flames).
  - Hold SELECT in a match to show the stage cost (update / record / total µs, CPU ms) on the
    bottom screen.
* Netplay: the host picks the stage (its menu selection; "Random" resolved with
  `stage_random()` = FUN_007aaf90) and sends it in the START packet (byte 47,
  `RB_PROTO_VERSION` 3); both consoles load it, the stage is updated once per new displayed
  tick (it is cosmetic, outside the rollback state).
* Measured in Azahar in **Old 3DS mode** (`is_new_3ds=false`; target O3DS XL, 268 MHz), match vs
  CPU after ~12 s, hold SELECT. The game alone runs at ~52–54 emulated FPS here, so the stage µs
  are the stage-specific numbers:

  | id | stage | upd µs | record µs | stage total µs | emu FPS |
  |---:|---|---:|---:|---:|---:|
  | 5 | tr | 2 | 106 | 313 | 52 |
  | 6 | bf | 2 | 46 | 605 | 54 |
  | 7 | gr | 3 | 2751 | 3779 | 49 |
  | 8 | dr | 3 | 1785 | 4849 | 49 |
  | 9 | ri | 2 | 113 | 499 | 54 |
  | 10 | se | 1 | 106 | 446 | 53 |
  | 11 | br | 2 | 410 | 1198 | 51 |
  | 12 | ct, Smoke cap (default) | 1259 | 2347 | 3225 | 49–57 |
  | 13 | iw | 18 | 1186 | 1835 | 52 |
  | 15 | ft | 2 | 126 | 269 | 53 |
  | 16 | gz | 4 | 212 | 1038 | 53 |
  | 17 | hl | 287 | 985 | 2289 | 49 |
  | 18 | ml | 1 | 641 | 1144 | 53 |
  | 19 | sh | 11 | 378 | 912 | 53 |
  | 4 | sl (menu) | – | – | – | 60 |

  Capital in Flames per option (early → steady state, total stage µs / FPS): Exact 11173 / 29 →
  10922 / 30; Smoke cap 3421 / 49 → 3449 / 57; Half-rate 6805 / 36 → 6936 / 38; Cap + half-rate
  2352 / 51 → 2147 / 57. `osSetSpeedupEnable(true)` is called but assumed to do nothing (Old 3DS).
  Other stages above ~2 ms (proposals only, not implemented): Dust Storm (height fields 2×32k
  tris + 100×100 sky: sample the height maps every 2nd texel / 50×50 sky), Canyon of Wind (two
  50×50 additive wind spheres: 25×25 tessellation), Guardian's Chamber (one draw per cube plus a
  287 µs update: nothing obvious to approximate; the cubes already batch).
* City sprite option (`stage_g.lod`, menu row "City sprites"; stages 12/14 only, a 3DS
  presentation approximation — target is the Old 3DS at 268 MHz):
  - **Exact**: the original.
  - **Smoke cap** (default: the only single option that reaches the game baseline): above `CT_CAP` (300) live smoke + flame sprites only every k-th sprite by
    spawn number is drawn, k = ceil(n / 300) (≈ 4 at steady state). Selection by a per-sprite
    spawn serial, so a sprite never flickers in and out while it ages.
  - **Half-rate smoke**: the smoke and flame lists are stepped twice on even ticks and not at
    all on odd ticks (same number of cosmetic-RNG draws per tick pair; the order relative to
    the odd-tick flame spawns differs); odd ticks re-emit the previous tick's sprite commands
    under the current camera.
  - **Cap + half-rate**: both.
  Stages run on their own cosmetic RNG stream and are outside the simulation and the rollback
  state, so the option never affects gameplay or netplay sync (each console may use a
  different setting).
* Host preview: `tests/run_stagepreview.sh <id> <ticks> <zoom> out.png [-3ds]` renders the same
  command list with a software rasterizer implementing §3.3 (perspective-correct, bilinear,
  z-buffer, D3D cull/blend/fog).
