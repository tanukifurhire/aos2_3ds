#include "render.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../app/prof.h"
#include "../sim/draw.h"
#include "hud.h"
#include "stage.h"
#include "spritebank.h"

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

static int cur_blend = -1;

static void set_blend(int mode)
{
	if (mode == cur_blend || mode == AOS_BLEND_KEEP)
		return;
	C2D_Flush();
	cur_blend = mode;
	GPU_BLENDFACTOR s, d;
	switch (mode) {
	case AOS_BLEND_OPAQUE: s = GPU_ONE; d = GPU_ZERO; break;
	case AOS_BLEND_ADD_ALPHA: s = GPU_SRC_ALPHA; d = GPU_ONE; break;
	case AOS_BLEND_MUL: s = GPU_ZERO; d = GPU_SRC_COLOR; break;
	case AOS_BLEND_ADD: s = GPU_ONE; d = GPU_ONE; break;
	case AOS_BLEND_SCREEN: s = GPU_DST_COLOR; d = GPU_ONE; break;
	case AOS_BLEND_INVERT: s = GPU_ONE_MINUS_DST_COLOR; d = GPU_ZERO; break;
	default: s = GPU_SRC_ALPHA; d = GPU_ONE_MINUS_SRC_ALPHA; break;
	}
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, s, d, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
}

static void draw_cmd(const aos_dcmd *c, float camX, float camY, float zoomDiv)
{
	static int tex_tri = -2, tex_white;
	if (tex_tri == -2) { /* pseudo texture ids (interned once; ids never change) */
		tex_tri = aos_tex("$tri");
		tex_white = aos_tex("$white");
	}
	const char *tn = aos_tex_name(c->tex);
	if (tn[0] == '$') { /* pseudo textures from fx.c */
		if (c->tex == tex_tri) {
			float px[3], py[3];
			u32 col[3];
			for (int k = 0; k < 3; k++) {
				px[k] = SCR_W / 2 + (c->tri[2 * k] - camX) / zoomDiv;
				py[k] = SCR_H / 2 - (c->tri[2 * k + 1] - camY) / zoomDiv;
				u32 v = c->triRgba[k];
				col[k] = C2D_Color32((v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
			}
			set_blend(c->blend);
			C2D_DrawTriangle(px[0], py[0], col[0], px[1], py[1], col[1], px[2], py[2], col[2], 0.5f);
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
		u32 col = C2D_Color32((c->rgba >> 24) & 0xff, (c->rgba >> 16) & 0xff, (c->rgba >> 8) & 0xff, c->rgba & 0xff);
		set_blend(c->blend);
		C2D_DrawTriangle(px[0], py[0], col, px[1], py[1], col, px[2], py[2], col, 0.5f);
		C2D_DrawTriangle(px[2], py[2], col, px[3], py[3], col, px[0], py[0], col, 0.5f);
		return;
	}
	const aos_frame *fr = lookup(c->tex, c->bank);
	if (!fr)
		return;
	C2D_Image img = fr->img;
	Tex3DS_SubTexture sub;
	if (c->hasSrc && fr->ow && fr->oh) {
		const Tex3DS_SubTexture *s = img.subtex;
		float u0 = c->src[0] / fr->ow, u1 = c->src[2] / fr->ow;
		float v0 = c->src[1] / fr->oh, v1 = c->src[3] / fr->oh;
		sub = *s;
		sub.left = s->left + (s->right - s->left) * u0;
		sub.right = s->left + (s->right - s->left) * u1;
		sub.top = s->top + (s->bottom - s->top) * v0;
		sub.bottom = s->top + (s->bottom - s->top) * v1;
		sub.width = (u16)fmaxf(1.0f, s->width * (u1 - u0));
		sub.height = (u16)fmaxf(1.0f, s->height * (v1 - v0));
		img.subtex = &sub;
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
	if (fabsf(pw) < 0.5f || fabsf(ph) < 0.5f)
		return;
	/* off-screen cull; a rotated quad reaches up to |pw|/2 + |ph|/2 from its centre on either axis
	 * (the axis-aligned test dropped long rotated beams whose centre is off-screen, e.g. Accel Burst) */
	float ex = fabsf(pw), ey = fabsf(ph);
	if (c->rotZ)
		ex = ey = ex + ey;
	if (sx + ex < -64 || sx - ex > SCR_W + 64 || sy + ey < -64 || sy - ey > SCR_H + 64)
		return;
	set_blend(c->blend);
	C2D_ImageTint tint;
	u32 rgba = c->rgba;
	u32 abgr = C2D_Color32((rgba >> 24) & 0xff, (rgba >> 16) & 0xff, (rgba >> 8) & 0xff, rgba & 0xff);
	C2D_PlainImageTint(&tint, abgr, 1.0f);
	/* Mirroring (flipX, negative sx/sy): citro2d lays the quad out from
	 * -center with |w|, so negative sizes/centres shift the sprite off its
	 * position by a full width (and make rotated sprites orbit). Keep the quad
	 * centred with positive extents and mirror the texture coordinates. */
	float qw = c->flipX ? -pw : pw, qh = ph;
	if (img.subtex != &sub) {
		sub = *img.subtex;
		img.subtex = &sub;
	}
	spritebank_clamp_sub(&sub, fr->img.subtex, fr->img.tex);
	if (qw < 0) {
		float t = sub.left;
		sub.left = sub.right;
		sub.right = t;
		qw = -qw;
	}
	if (qh < 0) {
		float t = sub.top;
		sub.top = sub.bottom;
		sub.bottom = t;
		qh = -qh;
	}
	C2D_DrawParams p = {
		.pos = {sx, sy, qw, qh},
		.center = {qw * 0.5f, qh * 0.5f},
		.depth = 0.5f,
		.angle = -(float)c->rotZ * (6.2831855f / 65536.0f), /* world CCW -> screen (y down) */
	};
	C2D_DrawImage(img, &p, &tint);
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
	u32 edge = C2D_Color32(0xff, 0x80, 0x40, (u8)((int)(t * 32.0f) + 0x80)), clear = C2D_Color32(0xff, 0x80, 0x40, 0);
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
	for (int i = 0; i < 72; i++) {
		float c0 = rc[i] / zoomDiv, s0 = -rs[i] / zoomDiv, c1 = rc[i + 1] / zoomDiv, s1 = -rs[i + 1] / zoomDiv;
		float rr[2] = {R + out, R - in};
		for (int k = 0; k < 2; k++) {
			float r = rr[k];
			C2D_DrawTriangle(ox + c0 * r, oy + s0 * r, clear, ox + c1 * r, oy + s1 * r, clear, ox + c1 * R, oy + s1 * R,
			                 edge, 0);
			C2D_DrawTriangle(ox + c0 * r, oy + s0 * r, clear, ox + c1 * R, oy + s1 * R, edge, ox + c0 * R, oy + s0 * R,
			                 edge, 0);
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
	aos_draw_world(w); /* shared with the host smoke test (src/sim/drawworld.c) */
	PROF_END(PROF_DRAWLIST);

	/* gameplay layer clear colour 0xff0055aa (FUN_005f91d0), then the stage
	 * background (src/stage, platform/stage.c; docs/re/stages.md) */
	C2D_TargetClear(top, C2D_Color32(0, 0x55, 0xaa, 255));
	C2D_SceneBegin(top);
	PROF_BEGIN(PROF_STAGE_DRAW);
	stage3ds_draw();
	PROF_END(PROF_STAGE_DRAW);
	PROF_BEGIN(PROF_2D);
	C2D_SetTintMode(C2D_TintMult);
	cur_blend = -1;
	set_blend(AOS_BLEND_ALPHA);
	arena_ring(w, zoomDiv);
	for (int i = 0; i < aos_dl.n; i++)
		draw_cmd(&aos_dl.cmd[i], w->obj.camX, w->obj.camY, zoomDiv);
	set_blend(AOS_BLEND_ALPHA);
	PROF_END(PROF_2D);
	/* HUD, round banners, transitions (hud.c, docs/re/hud.md) */
	PROF_BEGIN(PROF_HUD);
	hud_c2d_draw(w, SCR_W, SCR_H, w->obj.camX, w->obj.camY, zoomDiv);
	PROF_END(PROF_HUD);
	cur_blend = -1;
}
