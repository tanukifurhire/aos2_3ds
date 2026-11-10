# Rendering in AoS2 (what draw handlers do) — spec for porting T1 handlers

Sprites are drawn as textured quads through a C++ `CModel` class (vtable 0x849734, 74 methods;
D3D9). Each animation is a `std::vector` of models, one per frame; `FUN_004a9ab0(vec, i)` returns
`&vec[i]` (so `*FUN_004a9ab0(i)` = the model for frame i). The loader (e.g. Suguri `FUN_00751e40`)
creates the vectors from texture names like `"sg_std0%d"` and associates them with a sprite id
(e.g. 0x128). A draw handler's `this`/implicit vector is the current animation's frame vector.

## CModel methods used by draw handlers

| vtable offset | Address | Meaning | Port as |
|---|---|---|---|
| +0x38 | 0046c4f0 | Reset transform: identity rotation, position (0,0,0), scale (1,1,1) | start a new sprite draw |
| +0x50 | 0046ce70 | Rotate about X by binary angle (uses `FUN_00493940`) | rot_x (rare; tilt) |
| +0x54 | 0046d0b0 | Rotate about Y by binary angle (`FUN_004939e0`). **Used for facing: angle = player+0x84 (0 or π) → mirror** | flip_x when angle ≈ π |
| +0x58 | 0046d2f0 | Rotate about Z by binary angle (`FUN_00493a80`) — in-plane sprite rotation | rot_z |
| +0x5c | 0046d530 | Set position (x, y, z) → model+0x90/0x94/0x98 | pos |
| +0x60 | 0046d560 | Set scale (sx, sy, sz) → model+0x9c/0xa0/0xa4 | scale |
| +0x40 | 0046c580 | Render the model with the current transform/colour/blend | submit draw |
| +0x9c | 0046c290 | Per-model reset of texture-stage/animation state before drawing | (no-op in port) |
| +0xb0 | 0046e550 | Set per-mesh material value (args: a, b, meshIdx, subIdx); used by loaders | loader-only |
| +0xc8 | 0046ee20 | Set mesh+0x18c (per-mesh value) for meshIdx; used by loaders | loader-only |
| +0xd4 | 0046f4f0 | Recreate mesh render object; loader-only | loader-only |
| +0xdc | 0046ef70 | Set colour — argument order is **(a, r, g, b)** floats 0..1, then meshIdx (−1 = all) (corrected by kae.md/kyoko.md) | color |

Rotation matrices are composed in call order (each Rotate call multiplies into the model's
3×3 at +0x6c..+0x8c). For 2D porting: Y-rotation by π = horizontal mirror; Z = 2D rotation;
X-rotation only foreshortens vertically (cos factor) — approximate as y-scale by cos(angle).

## Helpers

| Function | Meaning |
|---|---|
| `FUN_0045a0f0(int deg)` | degrees → binary angle: `deg*0x10000/360` (integer division) |
| `FUN_0045a0c0(float rad)` | radians → binary angle: `(int)(rad*65536.0f/6.2831855f) & 0xffff` (`DAT_00853ba8`=65536, `DAT_00853b78`=2π) |
| `FUN_00492390(u32 bits)` | `fabsf` (masks sign bit with `DAT_00853df0`=0x7fffffff) |
| `FUN_004e8f50(model*)` | colour = global tint `DAT_00893da0..dac` (r,g,b,a) via +0xdc — normally white/opaque; used for screen-wide fades |
| `FUN_004637d0(mode)` | set blend mode (below) |
| `FUN_0045a680(float)` | float math wrapper (calls CRT `FUN_007fe880`, likely `floor`/`sqrt` — check call site) |
| `FUN_004d7e00(model, flag, player, rgb[3], ...)` | records the drawn frame for **afterimage trails** (uses player pos +0x6c/+0x70/+0x74, facing +0x84, and player+0xe0 vs 1.0/`DAT_00813a4c` to pick trail colour). Skipped when `DAT_00894330==1` or `DAT_009b11c0!=0`. |
| `DAT_00ddc0f0[65536]` | sine table (BSS) indexed by binary angle; cos = `[a+0x4000 & 0xffff]` |

## Blend modes (`FUN_004637d0`; D3DRS_SRCBLEND=0x13 / DESTBLEND=0x14)

| mode | src | dst | meaning |
|---|---|---|---|
| 0 | ONE | ZERO | opaque |
| 1 | SRCALPHA | INVSRCALPHA | normal alpha (default) |
| 2 | SRCALPHA | ONE | additive with alpha |
| 3 | — | — | (no case: leaves state unchanged) |
| 4 | ZERO | SRCCOLOR | multiply |
| 5 | ONE | ONE | pure additive |
| 6 | DESTCOLOR | ONE | screen-ish (dst*src + dst) |
| 7 | INVDESTCOLOR | ZERO | invert |

## Common draw handler shape (example: Suguri stand, T1 for state 0x128 = `FUN_0075bc90`)

```c
frame = (timer / 5) % 4; if (frame == 3) frame = 1;      // ping-pong 0,1,2,1
m = anim[frame];
color_from_global_tint(m);                              // FUN_004e8f50
m->Reset();                                             // +0x38
if (p->facing_flag_8c) m->RotY(rad2bam(PI));            // +0x54 (mirror)
m->SetPos(p->x, p->y, 0);                               // +0x5c
m->Render(0);                                           // +0x40
m->ResetTex();                                          // +0x9c
afterimage_record(m, 1, p, {0,0,0}, 0);                 // FUN_004d7e00
```

Port API (src/sim, to be implemented by the lead): `aos_draw(spriteName/frame, x, y, z,
rotY_bam, rotZ_bam, sx, sy, r, g, b, a, blend)` emitted into a draw list; the platform layer
renders it. Character agents translate each T1 handler into calls of that API.
