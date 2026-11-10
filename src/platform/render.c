#include "render.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../app/prof.h"
#include "../sim/draw.h"
#include "hud.h"
#include "stage.h"
#include "spritebank.h"
#include "sprbatch.h"

#define SCR_W 400
#define SCR_H 240
/* objects.md §6.3: screenScale = min(W/800, H/600) */
#define SCREEN_SCALE 0.4f

static const char *const folder[19] = {
	[3] = "charaSora", [4] = "charaAlte", [5] = "charaTsih", [6] = "charaMira", [7] = "charaSham",
	[8] = "charaNath", [9] = "charaStar", [10] = "charaSuguri", [11] = "charaSaki", [12] = "charaIru",
	[13] = "charaNanako", [14] = "charaKae", [15] = "charaKyoko", [16] = "charaHime",
	[17] = "charaSumika", [18] = "charaSumika",
};
static const char *const names[19] = {
	[3] = "Sora", [4] = "Alte", [5] = "Tsih", [6] = "Mira", [7] = "Sham", [8] = "Nath",
	[9] = "Star Breaker", [10] = "Suguri", [11] = "Saki", [12] = "Iru", [13] = "Nanako",
	[14] = "Kae", [15] = "Kyoko", [16] = "Hime", [17] = "Sumika", [18] = "Sumika (2nd)",
};
static const char *const common_folders[] = {
	"effectMdl", "beamMdl", "particleMdl", "msgMdl", "charaCmnA", "systemMdl",
};

const char *render_char_folder(int id) { return id >= 0 && id < 19 && folder[id] ? folder[id] : ""; }
const char *render_char_name(int id) { return id >= 0 && id < 19 && names[id] ? names[id] : "?"; }

static char loaded[2][32];

/* Palette folders that exist (B/C variants are missing for some characters). */
static void char_folder(char *out, size_t n, int id, int color)
{
	if (id == 18) { /* Sumika second form: sab_* art lives in charaAdd (no palette folders) */
		snprintf(out, n, "charaAdd");
		return;
	}
	static const char suffix[3] = {'A', 'B', 'C'};
	for (int c = color; c >= 0; c--) {
		snprintf(out, n, "%s%c", render_char_folder(id), suffix[c < 0 ? 0 : c > 2 ? 2 : c]);
		char probe[128];
		snprintf(probe, sizeof probe, "romfs:/gfx/%s.idx", out);
		FILE *f = fopen(probe, "r");
		if (f) {
			fclose(f);
			return;
		}
	}
	snprintf(out, n, "%sA", render_char_folder(id));
}

/* Name lookup cache: interned tex id -> frame, per bank. */
#define CACHE 4096
static const aos_frame *cache[3][CACHE];
static bool cached[3][CACHE];

bool render_load_match(const aos_world *w)
{
	render_unload_match();
	spr_init(); /* the match's 2D batcher (sprbatch.c); idempotent */
	for (size_t i = 0; i < sizeof common_folders / sizeof common_folders[0]; i++)
		spritebank_load_resident(common_folders[i]); /* never unloaded: VRAM while it fits */
	bool ok = true;
	for (int i = 0; i < 2; i++) {
		char_folder(loaded[i], sizeof loaded[i], w->p[i].id, w->p[i].color);
		ok &= spritebank_load(loaded[i]);
	}
	memset(cached, 0, sizeof cached);
	hud_c2d_load(w);
	return ok;
}

const char *render_player_folder(int i) { return i >= 0 && i < 2 ? loaded[i] : ""; }

void render_unload_match(void)
{
	hud_c2d_unload();
	/* both players may share one folder (Sumika 2nd form mirror: charaAdd); that
	 * case used to unload neither and leaked 4 MB of linear memory per match */
	if (loaded[0][0])
		spritebank_unload(loaded[0]);
	if (loaded[1][0] && strcmp(loaded[1], loaded[0]) != 0)
		spritebank_unload(loaded[1]);
	memset(loaded, 0, sizeof loaded);
	memset(cached, 0, sizeof cached);
}

/* Character banks resolve in that player's folder first (both players may
 * use the same character with different palettes). */
const aos_frame *spritebank_get_in(const char *folder, const char *name);

static const aos_frame *lookup(int tex, int bank)
{
	int b = bank < 0 ? 2 : bank & 1;
	if (tex < 0 || tex >= CACHE)
		return NULL;
	if (!cached[b][tex]) {
		cached[b][tex] = true;
		const char *n = aos_tex_name(tex);
		cache[b][tex] = b < 2 ? spritebank_get_in(loaded[b], n) : NULL;
		if (!cache[b][tex])
			cache[b][tex] = spritebank_get(n);
	}
	return cache[b][tex];
}

/* The PC's current blend mode: AOS_BLEND_KEEP draws use it (the GPU state is the batcher's, sprbatch.h). */
static int pc_blend = AOS_BLEND_ALPHA;

static int resolve_blend(int b)
{
	if (b != AOS_BLEND_KEEP)
		pc_blend = b;
	return pc_blend;
}

static void draw_cmd(const aos_dcmd *c, float camX, float camY, float zoomDiv)
{
	static int tex_tri = -2, tex_white;
	if (tex_tri == -2) { /* pseudo texture ids (interned once; ids never change) */
		tex_tri = aos_tex("$tri");
		tex_white = aos_tex("$white");
	}
	const char *tn = aos_tex_name(c->tex);
	if (tn[0] == '$') { /* pseudo textures from fx.c: solid primitives in the same batch (sprbatch.h) */
		if (c->tex == tex_tri) {
			float px[3], py[3];
			for (int k = 0; k < 3; k++) {
				px[k] = SCR_W / 2 + (c->tri[2 * k] - camX) / zoomDiv;
				py[k] = SCR_H / 2 - (c->tri[2 * k + 1] - camY) / zoomDiv;
			}
			spr_tri_solid(px, py, c->triRgba, resolve_blend(c->blend));
#ifdef AOS_PROFILE
			prof_cnt.tris++;
#endif
			return;
		}
		if (c->tex != tex_white)
			return; /* "$blur" (previous-frame feedback) not supported yet */
		float hw = (c->halfW > 0 ? c->halfW : 1) * c->sx / zoomDiv, hh = (c->halfH > 0 ? c->halfH : hw) * c->sy / zoomDiv;
		if (c->halfH > 0)
			hh = c->halfH * c->sy / zoomDiv;
		float sx = SCR_W / 2 + (c->x - camX) / zoomDiv, sy = SCR_H / 2 - (c->y - camY) / zoomDiv;
		float a = -(float)c->rotZ * (6.2831855f / 65536.0f), ca = cosf(a), sa = sinf(a);
		float px[4], py[4];
		const float ux[4] = {-1, 1, 1, -1}, uy[4] = {-1, -1, 1, 1};
		for (int k = 0; k < 4; k++) {
			px[k] = sx + ux[k] * hw * ca - uy[k] * hh * sa;
			py[k] = sy + ux[k] * hw * sa + uy[k] * hh * ca;
		}
		int bl = resolve_blend(c->blend);
		spr_state(NULL, false, bl);
		spr_vtx *v = spr_quad();
		if (!v)
			return;
		u32 col = spr_color(c->rgba, bl);
		/* corners 0..3 run around the rect; quad vertex order is TL TR BL BR */
		spr_set(&v[0], px[0], py[0], SPR_SOLID_UV, SPR_SOLID_UV, col);
		spr_set(&v[1], px[1], py[1], SPR_SOLID_UV, SPR_SOLID_UV, col);
		spr_set(&v[2], px[3], py[3], SPR_SOLID_UV, SPR_SOLID_UV, col);
		spr_set(&v[3], px[2], py[2], SPR_SOLID_UV, SPR_SOLID_UV, col);
		return;
	}
	const aos_frame *fr = lookup(c->tex, c->bank);
	if (!fr)
		return;
	const Tex3DS_SubTexture *fs = fr->img.subtex;
	Tex3DS_SubTexture sub = *fs;
	if (c->hasSrc && fr->ow && fr->oh) {
		float u0 = c->src[0] / fr->ow, u1 = c->src[2] / fr->ow;
		float v0 = c->src[1] / fr->oh, v1 = c->src[3] / fr->oh;
		sub.left = fs->left + (fs->right - fs->left) * u0;
		sub.right = fs->left + (fs->right - fs->left) * u1;
		sub.top = fs->top + (fs->bottom - fs->top) * v0;
		sub.bottom = fs->top + (fs->bottom - fs->top) * v1;
	}
	float wW, wH; /* world size of the quad */
	if (c->halfW > 0) {
		wW = 2.0f * c->halfW;
		wH = 2.0f * (c->halfH > 0 ? c->halfH : c->halfW);
	} else if (c->hasSrc) {
		wW = c->src[2] - c->src[0];
		wH = c->src[3] - c->src[1];
	} else {
		wW = fr->ow;
		wH = fr->oh;
	}
	wW *= c->sx;
	wH *= c->sy;
	float sx = SCR_W / 2 + (c->x - camX) / zoomDiv;
	float sy = SCR_H / 2 - (c->y - camY) / zoomDiv;
	float pw = wW / zoomDiv, ph = wH / zoomDiv;
#ifdef AOS_PROFILE
	if (bench_flags & BENCH_HALF_SPRITES) {
		pw *= 0.5f;
		ph *= 0.5f;
	}
#endif
	if (fabsf(pw) < 0.5f || fabsf(ph) < 0.5f)
		return;
	/* off-screen cull; a rotated quad reaches up to |pw|/2 + |ph|/2 from its centre on either axis
	 * (the axis-aligned test dropped long rotated beams whose centre is off-screen, e.g. Accel Burst) */
	float ex = fabsf(pw), ey = fabsf(ph);
	if (c->rotZ)
		ex = ey = ex + ey;
	if (sx + ex < -64 || sx - ex > SCR_W + 64 || sy + ey < -64 || sy - ey > SCR_H + 64)
		return;
	int bl = resolve_blend(c->blend);
	spr_state(fr->img.tex, fr->premul, bl);
	spr_vtx *v = spr_quad();
	if (!v)
		return;
	u32 col = spr_color(c->rgba, bl);
	/* Mirroring (flipX, negative sx/sy): the quad keeps positive extents around its centre and the texture
	 * coordinates are mirrored per corner. (Swapping the sub-rectangle's top/bottom instead, as the citro2d
	 * version did, made Tex3DS read a non-rotated frame as tex3ds-rotated (top < bottom): vertically mirrored
	 * sprites sampled a transposed atlas region - the "P2 sprite missing" / white or black square bug.) */
	float qw = pw, qh = ph;
	bool mx = c->flipX, my = false;
	if (qw < 0) {
		qw = -qw;
		mx = !mx;
	}
	if (qh < 0) {
		qh = -qh;
		my = true;
	}
	spritebank_clamp_sub(&sub, fs, fr->img.tex);
	/* corners as C2Di_CalcQuad (centre pivot, same float operations: identical vertices) */
	float hx = qw * 0.5f, hy = qh * 0.5f;
	float x0 = -hx, y0 = -hy, x1 = -hx + qw, y1 = -hy + qh;
	float cx[4] = {x0, x1, x0, x1}, cy[4] = {y0, y0, y1, y1}; /* TL TR BL BR */
	float ang = -(float)c->rotZ * (6.2831855f / 65536.0f); /* world CCW -> screen (y down) */
	if (ang != 0.0f) {
		float rs = sinf(ang), rc = cosf(ang);
		for (int k = 0; k < 4; k++) {
			float x = cx[k] * rc - cy[k] * rs, y = cy[k] * rc + cx[k] * rs;
			cx[k] = x;
			cy[k] = y;
		}
	}
	/* texcoords of the image corners (Tex3DS_SubTexture{TopLeft,...}; the frame's own orientation decides
	 * whether tex3ds stored it rotated), then mirrored */
	float ul = sub.left, ur = sub.right, vt = sub.top, vb = sub.bottom;
	if (mx) {
		float t = ul;
		ul = ur;
		ur = t;
	}
	if (my) {
		float t = vt;
		vt = vb;
		vb = t;
	}
	if (!(fs->top < fs->bottom)) {
		spr_set(&v[0], cx[0] + sx, cy[0] + sy, ul, vt, col);
		spr_set(&v[1], cx[1] + sx, cy[1] + sy, ur, vt, col);
		spr_set(&v[2], cx[2] + sx, cy[2] + sy, ul, vb, col);
		spr_set(&v[3], cx[3] + sx, cy[3] + sy, ur, vb, col);
	} else {
		spr_set(&v[0], cx[0] + sx, cy[0] + sy, vt, ul, col);
		spr_set(&v[1], cx[1] + sx, cy[1] + sy, vt, ur, col);
		spr_set(&v[2], cx[2] + sx, cy[2] + sy, vb, ul, col);
		spr_set(&v[3], cx[3] + sx, cy[3] + sy, vb, ur, col);
	}
#ifdef AOS_PROFILE
	{
		prof_cnt.quads++;
		/* covered screen pixels: the quad's bounding box clipped to the screen, scaled by the
		 * quad/bbox area ratio for rotated quads */
		float ca = fabsf(cosf(ang)), sa = fabsf(sinf(ang));
		float bw = qw * ca + qh * sa, bh = qw * sa + qh * ca;
		float bx0 = fmaxf(0, sx - bw / 2), bx1 = fminf(SCR_W, sx + bw / 2);
		float by0 = fmaxf(0, sy - bh / 2), by1 = fminf(SCR_H, sy + bh / 2);
		if (bx1 > bx0 && by1 > by0 && bw > 0 && bh > 0) {
			float a = (bx1 - bx0) * (by1 - by0) * (qw * qh) / (bw * bh);
			int k = bl == AOS_BLEND_ADD || bl == AOS_BLEND_ADD_ALPHA || bl == AOS_BLEND_SCREEN ? 1
			        : bl == AOS_BLEND_MUL || bl == AOS_BLEND_INVERT || bl == AOS_BLEND_OPAQUE ? 2
			                                                                                : 0;
			prof_cnt.px[k] += a;
		}
	}
#endif
}

void aos_draw_players(aos_world *w);

/* FUN_00619ef0: arena boundary glow, first item of the 2D gameplay layer.
 * 72 segments; orange 0xff8040 with alpha (int)(sin(t)*32)+0x80 at r = 540,
 * fading to 0 at 540 + (80 - 20 sin t) outside and 540 - (20 - 5 sin t) inside,
 * t = (frame % 100) / 100 * 2pi. CPrimitive3D triangles in world space. */
static void arena_ring(const aos_world *w, float zoomDiv)
{
	float t = sinf(((float)(w->frame % 100) / 100.0f) * 6.2831855f);
	float in = -(t * 5.0f) + 20.0f, out = -(t * 20.0f) + 80.0f, R = 540.0f;
	u32 ea = (u32)(u8)((int)(t * 32.0f) + 0x80);
	u32 edge = spr_color(0xff804000u | ea, AOS_BLEND_ALPHA), clear = spr_color(0xff804000u, AOS_BLEND_ALPHA);
	float ox = SCR_W / 2 - w->obj.camX / zoomDiv, oy = SCR_H / 2 + w->obj.camY / zoomDiv;
	static float rc[73], rs[73]; /* cosf/sinf of the fixed segment angles, computed once */
	static bool rready;
	if (!rready) {
		for (int i = 0; i <= 72; i++) {
			float a = (float)i / 72.0f * 6.2831855f;
			rc[i] = cosf(a);
			rs[i] = sinf(a);
		}
		rready = true;
	}
	spr_state(NULL, false, AOS_BLEND_ALPHA);
	for (int i = 0; i < 72; i++) {
		float c0 = rc[i] / zoomDiv, s0 = -rs[i] / zoomDiv, c1 = rc[i + 1] / zoomDiv, s1 = -rs[i + 1] / zoomDiv;
		float rr[2] = {R + out, R - in};
		for (int k = 0; k < 2; k++) {
			float r = rr[k];
			/* the PC's two triangles (o0 o1 e1) (o0 e1 e0) as one quad: TL=o1 TR=o0 BL=e1 BR=e0 gives
			 * (TL BL TR) (TR BL BR) = the same two triangles */
			spr_vtx *v = spr_quad();
			if (!v)
				return;
			spr_set(&v[0], ox + c1 * r, oy + s1 * r, SPR_SOLID_UV, SPR_SOLID_UV, clear); /* o1 */
			spr_set(&v[1], ox + c0 * r, oy + s0 * r, SPR_SOLID_UV, SPR_SOLID_UV, clear); /* o0 */
			spr_set(&v[2], ox + c1 * R, oy + s1 * R, SPR_SOLID_UV, SPR_SOLID_UV, edge);  /* e1 */
			spr_set(&v[3], ox + c0 * R, oy + s0 * R, SPR_SOLID_UV, SPR_SOLID_UV, edge);  /* e0 */
		}
	}
}

static C3D_RenderTarget *last_target;
C3D_RenderTarget *render_last_target(void) { return last_target; }

void render_world(C3D_RenderTarget *top, const aos_world *cw)
{
	last_target = top;
	aos_world *w = (aos_world *)cw; /* draw handlers take a non-const world but don't mutate sim state */
	float Z = w->obj.zoomView > 0 ? w->obj.zoomView : (w->obj.zoom > 0 ? w->obj.zoom : 1.0f);
	float zoomDiv = Z / SCREEN_SCALE;
	PROF_BEGIN(PROF_DRAWLIST);
	aos_draw_begin();
#ifdef AOS_PROFILE
	if (!(bench_flags & BENCH_NO_DRAWLIST))
#endif
		aos_draw_world(w); /* shared with the host smoke test (src/sim/drawworld.c) */
	PROF_END(PROF_DRAWLIST);

	/* gameplay layer clear colour 0xff0055aa (FUN_005f91d0), then the stage
	 * background (src/stage, platform/stage.c; docs/re/stages.md) */
	C2D_TargetClear(top, C2D_Color32(0, 0x55, 0xaa, 255));
	C2D_SceneBegin(top);
	PROF_BEGIN(PROF_STAGE_DRAW);
#ifdef AOS_PROFILE
	if (!(bench_flags & BENCH_NO_STAGE))
#endif
		stage3ds_draw();
	PROF_END(PROF_STAGE_DRAW);
	PROF_BEGIN(PROF_2D);
	/* arena ring, draw list and HUD: one batch pass (sprbatch.h), blend 1 and 2 without state changes */
	spr_frame_begin();
	spr_begin(SCR_W, 0.0f); /* the ring was drawn at citro2d depth 0, sprites at 0.5, the HUD at 0.9 */
	pc_blend = AOS_BLEND_ALPHA;
	arena_ring(w, zoomDiv);
	spr_depth(0.5f);
	int ndl = aos_dl.n;
#ifdef AOS_PROFILE
	if (bench_flags & BENCH_NO_SUBMIT)
		ndl = 0;
#endif
	for (int i = 0; i < ndl; i++)
		draw_cmd(&aos_dl.cmd[i], w->obj.camX, w->obj.camY, zoomDiv);
	PROF_END(PROF_2D);
	/* HUD, round banners, transitions (hud.c, docs/re/hud.md) */
	PROF_BEGIN(PROF_HUD);
#ifdef AOS_PROFILE
	if (!(bench_flags & BENCH_NO_HUD))
#endif
		hud_c2d_draw(w, SCR_W, SCR_H, w->obj.camX, w->obj.camY, zoomDiv);
	spr_end();
	PROF_END(PROF_HUD);
}
