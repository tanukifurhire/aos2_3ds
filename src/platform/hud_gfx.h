/* Drawing primitives used by the HUD port (hud.c). They mirror the PC
 * renderer calls the original HUD code makes, in screen pixels with y down:
 *
 *   hg_image  CTexture blits FUN_0045bf30 (axis aligned) and FUN_0045c7d0
 *             (rotated about the rect centre); the corners may be mirrored
 *             (x1 < x0), as the PC does for player 2.
 *   hg_quad   CTexture vtable +0x70 (FUN_0045d2e0): 4 corners TL,TR,BR,BL,
 *             a triangle fan with affine UVs. Top and bottom edges are
 *             horizontal in every HUD use (HP bar parallelograms).
 *   hg_tri    untextured vertex-coloured triangle (CVertexBuffer FUN_004789e0
 *             +0x2c without a texture).
 *   hg_tri_tex textured triangle (the hyper gauge arcs).
 *   hg_rect   FUN_00457c60 solid rectangle; hg_line FUN_00457ae0.
 *
 * Texture coordinates are texels of the ORIGINAL PC texture (the backend
 * scales to its own atlas). Colours are D3DCOLOR ARGB; blend is the PC blend
 * mode (render.md: 1 alpha, 2 additive with alpha, ...).
 *
 * Backends: src/platform/hud_c2d.c (3DS, citro2d) and tests/hudpreview.c (host,
 * writes a TSV that tools/drawpng renders with the real textures). */
#ifndef AOS_HUD_GFX_H
#define AOS_HUD_GFX_H

#include <stdint.h>

enum {
	HT_GAUGE,      /* systemMdl/gauge     DAT_00893dc4 */
	HT_DAMAGE,     /* systemMdl/damage    DAT_00893dc8 */
	HT_BGMNAME,    /* systemMdl/bgm_name  DAT_00893dcc */
	HT_FADE,       /* systemMdl/fade      DAT_0087fa94 */
	HT_HYPER_B,    /* systemMdl/hyperb    DAT_00893dd0 (buffer DAT_00893ddc) */
	HT_HYPER_G,    /* systemMdl/hyperg    DAT_00893dd4 */
	HT_HYPER_R,    /* systemMdl/hyperr    DAT_00893dd8 */
	HT_NOISE0,     /* systemMdl/noise0..7 (buffers DAT_00893e08..e24) */
	HT_SYSTEM = HT_NOISE0 + 8, /* systemAddMdl/system  DAT_00893dbc */
	HT_SYSTEM2,    /* systemAddMdl/system2 DAT_00893dc0 */
	HT_CROWN_W,    /* msgMdl/msg_leadercrownw DAT_0087fe3c */
	HT_CROWN_L,    /* msgMdl/msg_leadercrownl DAT_0087fe40 */
	HT_PORTRAIT0,  /* <xx>_system of player 1's palette folder (sprite stats+0x44) */
	HT_PORTRAIT1,
	HT_COUNT
};

void hg_image(int tex, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
              float angle, uint32_t argb, int blend);
void hg_quad(int tex, const float x[4], const float y[4], float u0, float v0, float u1, float v1, uint32_t argb,
             int blend);
void hg_tri(const float x[3], const float y[3], const uint32_t argb[3], int blend);
/* Two hg_tri in one call (the same triangles, so the same pixels): (P0 P1 P2) and (P0 P2 P3). */
void hg_tri2(const float x[4], const float y[4], const uint32_t argb[4], int blend);
void hg_tri_tex(int tex, const float x[3], const float y[3], const float u[3], const float v[3], uint32_t argb,
                int blend);
/* Two hg_tri_tex in one call: (P0 P1 P2) and (P2 P1 P3). */
void hg_tri_tex2(int tex, const float x[4], const float y[4], const float u[4], const float v[4], uint32_t argb,
                 int blend);
void hg_rect(float l, float t, float r, float b, uint32_t argb, int blend);
void hg_line(float x0, float y0, float x1, float y1, uint32_t argb, int blend);

#endif
