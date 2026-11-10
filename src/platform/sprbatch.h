/* 2D batcher for the match screens (sprites, arena ring, HUD, bottom panel): textured and solid quads/triangles
 * in one vertex stream with ONE blend state for the PC's two common blend modes (docs/PERF.md "Render pipeline").
 *
 * Premultiplied alpha. GPU blend = (ONE, ONE_MINUS_SRC_ALPHA); the vertex colour carries the tint with its RGB
 * multiplied by the tint alpha a, and alpha = a for PC blend 1 (alpha) or 0 for PC blend 2 (additive with alpha):
 *   blend 1: out = tex.rgb·tint·(tex.a·a) + dst·(1 − tex.a·a)    (= SRC_ALPHA, ONE_MINUS_SRC_ALPHA)
 *   blend 2: out = tex.rgb·tint·(tex.a·a) + dst                  (= SRC_ALPHA, ONE)
 * The fragment is texture × vertex colour (texenv 0); straight-alpha textures (all match atlases since aos2pack
 * -edge, PERF.md "Atlas bleed") get rgb ×= tex.a in texenv 1, which also zeroes whatever colour ETC1 left in
 * transparent texels; pages packed premultiplied (aos2pack -pma) are used as is. A texture or texenv change
 * flushes; blend 1 ↔ 2 does not. The other PC modes (0 opaque, 4 mul, 5 add, 6 screen, 7 invert) keep their own
 * GPU blend state and get the raw tint (exact on straight-alpha textures and solid draws).
 *
 * Solid (untextured) primitives use a small white texture (a texture switch when they interleave with sprites;
 * a white clamp border on every atlas would avoid that, but Azahar samples parts of some VRAM atlases as
 * border, which turned invisible texels into white, see PERF.md).
 *
 * Vertex z comes from spr_depth() (the citro2d depths of the replaced draws). Between spr_begin and spr_end
 * nothing may draw through citro2d; spr_end hands the GPU state back (C2D_Prepare). */
#ifndef AOS_SPRBATCH_H
#define AOS_SPRBATCH_H

#include <citro3d.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
	float x, y, u, v;
	u32 c; /* R | G<<8 | B<<16 | A<<24 */
} spr_vtx;

#define SPR_SOLID_UV 0.5f /* texcoord of solid primitives (inside the white texture) */

bool spr_init(void);
void spr_exit(void);
/* Once per frame, before the frame's first pass (render_world). */
void spr_frame_begin(void);
/* Start a batch pass on the current citro2d scene target (call after C2D_SceneBegin): W = 400 (top) or 320. */
void spr_begin(int W, float depth);
void spr_depth(float z);
void spr_end(void);
/* Flush the pending vertices (for code that changes GPU state itself in between). */
void spr_flush(void);

/* Per-frame statistics (profile builds read them). */
typedef struct {
	unsigned draws, texsw, statesw, quads, dropped;
} spr_stats_t;
extern spr_stats_t spr_stats;

/* ---- state: texture (NULL = the white texture, for solid draws), premultiplied flag, PC blend mode ---- */
extern C3D_Tex *spr_tex_cur;
extern int spr_cls_cur;
extern bool spr_premul_cur;
void spr_state_change(C3D_Tex *t, bool premul, int cls);

/* PC blend mode (AOS_BLEND_*, hud.md) -> state class: 1 and 2 share class 1 (premultiplied); 3 (keep) must be
 * resolved by the caller. */
static inline int spr_class(int pcblend) { return pcblend == 1 || pcblend == 2 ? 1 : 16 + pcblend; }

extern C3D_Tex spr_white;
static inline void spr_state(C3D_Tex *t, bool premul, int pcblend)
{
	int cls = spr_class(pcblend);
	if (!t) {
		t = &spr_white; /* opaque: texenv 0 and 1 give the same; 1 is the atlases' mode, so no texenv switch */
		premul = false;
	}
	if (t != spr_tex_cur || cls != spr_cls_cur || premul != spr_premul_cur)
		spr_state_change(t, premul, cls);
}

/* Vertex colour for a PC colour 0xRRGGBBAA and blend mode (see the top comment). */
static inline u32 spr_color(uint32_t rgba, int pcblend)
{
	u32 r = rgba >> 24, g = (rgba >> 16) & 0xff, b = (rgba >> 8) & 0xff, a = rgba & 0xff;
	if (pcblend == 1 || pcblend == 2) {
		r = (r * a * 257 + 32896) >> 16; /* round(r·a/255) for 0..255 */
		g = (g * a * 257 + 32896) >> 16;
		b = (b * a * 257 + 32896) >> 16;
		if (pcblend == 2)
			a = 0;
	}
	return r | g << 8 | b << 16 | a << 24;
}

/* Room for one quad (4 vertices; the index pattern is TL TR BL BR -> (0,2,1) (1,2,3)); NULL when full. */
extern spr_vtx *spr_vp, *spr_vend;
spr_vtx *spr_quad_slow(void);
static inline spr_vtx *spr_quad(void)
{
	if (spr_vp < spr_vend) {
		spr_vtx *v = spr_vp;
		spr_vp += 4;
		return v;
	}
	return spr_quad_slow();
}

static inline void spr_set(spr_vtx *v, float x, float y, float u, float w, u32 c)
{
	v->x = x;
	v->y = y;
	v->u = u;
	v->v = w;
	v->c = c;
}

/* Solid triangle / quad helpers (PC colours 0xRRGGBBAA, PC blend). */
void spr_tri_solid(const float x[3], const float y[3], const uint32_t rgba[3], int pcblend);
void spr_rect_solid(float l, float t, float r, float b, uint32_t rgba, int pcblend);

#endif
