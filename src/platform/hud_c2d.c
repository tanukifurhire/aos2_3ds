/* 3DS backend of the HUD primitives (hud_gfx.h) on citro2d, plus the HUD's
 * texture set. Textures come from the atlas sprite bank (tools/aos2pack,
 * scaled 0.5); texel coordinates of the PC textures are mapped onto the atlas
 * sub-rectangles. citro2d cannot texture arbitrary quads/triangles, so:
 *  - hg_quad (HP bar parallelograms, top/bottom edges horizontal) is drawn as
 *    one textured strip per screen row, which reproduces the affine mapping;
 *  - hg_tri_tex (hyper arcs) uses vertex colours from the arc gradient
 *    textures (hud_grad.h; they are constant along u). */
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "hud.h"
#include "hud_gfx.h"
#include "hud_grad.h"
#include "render.h"
#include "spritebank.h"

#define DEPTH 0.9f

typedef struct {
	C2D_Image img;
	float ow, oh; /* original PC texture size */
	bool ok;
} hud_tex;

static hud_tex tex[HT_COUNT];

/* systemAddMdl holds the big menu atlases too; only the pages with
 * system/system2 are loaded (unless the whole folder is already loaded). */
#define ADD_PAGES 8
static C2D_SpriteSheet addSheet[ADD_PAGES];

static bool from_frame(hud_tex *t, const aos_frame *f)
{
	if (!f)
		return false;
	t->img = f->img;
	t->ow = f->ow;
	t->oh = f->oh;
	t->ok = true;
	return true;
}

static bool from_bank(hud_tex *t, const char *folder, const char *name)
{
	return from_frame(t, spritebank_get_in(folder, name));
}

static void load_add(hud_tex *t, const char *name)
{
	if (from_bank(t, "systemAddMdl", name))
		return;
	FILE *f = fopen("romfs:/gfx/systemAddMdl.idx", "r");
	if (!f)
		return;
	char n[32];
	int page, idx, w, h, ow, oh;
	while (fscanf(f, "%31s %d %d %d %d %d %d", n, &page, &idx, &w, &h, &ow, &oh) == 7) {
		if (strcmp(n, name) != 0 || page < 0 || page >= ADD_PAGES)
			continue;
		if (!addSheet[page]) {
			char path[64];
			snprintf(path, sizeof path, "romfs:/gfx/systemAddMdl_%d.t3x", page);
			addSheet[page] = C2D_SpriteSheetLoad(path);
		}
		if (addSheet[page] && (size_t)idx < C2D_SpriteSheetCount(addSheet[page])) {
			t->img = C2D_SpriteSheetGetImage(addSheet[page], (size_t)idx);
			t->ow = (float)ow;
			t->oh = (float)oh;
			t->ok = true;
		}
		break;
	}
	fclose(f);
}

/* <xx>_system texture per engine character id (stats +0x44 sprite, palette folder). */
static const char *const portrait[19] = {
	[3] = "sr_system", [4] = "at_system", [5] = "ts_system", [6] = "mr_system", [7] = "sa_system",
	[8] = "nt_system", [9] = "sb_system", [10] = "sg_system", [11] = "sk_system", [12] = "ir_system",
	[13] = "nk_system", [14] = "ke_system", [15] = "ky_system", [16] = "hm_system", [17] = "sm_system",
	[18] = "sab_system",
};

void hud_c2d_load(const aos_world *w)
{
	hud_c2d_unload();
	static const char *const sysm[] = {"gauge", "damage", "bgm_name", "fade", "hyperb", "hyperg", "hyperr"};
	for (int i = 0; i < 7; i++)
		from_bank(&tex[HT_GAUGE + i], "systemMdl", sysm[i]);
	for (int i = 0; i < 8; i++) {
		char n[8];
		snprintf(n, sizeof n, "noise%d", i);
		from_bank(&tex[HT_NOISE0 + i], "systemMdl", n);
	}
	from_bank(&tex[HT_CROWN_W], "msgMdl", "msg_leadercrownw");
	from_bank(&tex[HT_CROWN_L], "msgMdl", "msg_leadercrownl");
	load_add(&tex[HT_SYSTEM], "system");
	load_add(&tex[HT_SYSTEM2], "system2");
	for (int i = 0; i < 2; i++) {
		int id = w->p[i].id;
		if (id >= 0 && id < 19 && portrait[id] && !from_bank(&tex[HT_PORTRAIT0 + i], render_player_folder(i), portrait[id]))
			from_frame(&tex[HT_PORTRAIT0 + i], spritebank_get(portrait[id]));
	}
}

void hud_c2d_unload(void)
{
	for (int p = 0; p < ADD_PAGES; p++)
		if (addSheet[p]) {
			C2D_SpriteSheetFree(addSheet[p]);
			addSheet[p] = NULL;
		}
	memset(tex, 0, sizeof tex);
}

/* ---- primitives ---- */

static int cur_blend = -1;

static void blend(int mode)
{
	if (mode == cur_blend)
		return;
	C2D_Flush();
	cur_blend = mode;
	GPU_BLENDFACTOR s, d;
	switch (mode) { /* FUN_004637d0 */
	case 0: s = GPU_ONE; d = GPU_ZERO; break;
	case 2: s = GPU_SRC_ALPHA; d = GPU_ONE; break;
	case 4: s = GPU_ZERO; d = GPU_SRC_COLOR; break;
	case 5: s = GPU_ONE; d = GPU_ONE; break;
	case 6: s = GPU_DST_COLOR; d = GPU_ONE; break;
	case 7: s = GPU_ONE_MINUS_DST_COLOR; d = GPU_ZERO; break;
	default: s = GPU_SRC_ALPHA; d = GPU_ONE_MINUS_SRC_ALPHA; break;
	}
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, s, d, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
}

static u32 c2d(uint32_t argb)
{
	return C2D_Color32((argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff, argb >> 24);
}

void hg_image(int t, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, float angle,
              uint32_t argb, int bl)
{
	if (t < 0 || t >= HT_COUNT || !tex[t].ok || (argb >> 24) == 0)
		return;
	const hud_tex *h = &tex[t];
	const Tex3DS_SubTexture *s = h->img.subtex;
	/* Mirrored rects (P2): positive extents, mirrored texture coordinates. */
	if (x1 < x0) {
		float tmp = x0;
		x0 = x1;
		x1 = tmp;
		tmp = u0;
		u0 = u1;
		u1 = tmp;
	}
	if (y1 < y0) {
		float tmp = y0;
		y0 = y1;
		y1 = tmp;
		tmp = v0;
		v0 = v1;
		v1 = tmp;
	}
	Tex3DS_SubTexture sub = *s;
	float fu0 = u0 / h->ow, fu1 = u1 / h->ow, fv0 = v0 / h->oh, fv1 = v1 / h->oh;
	sub.left = s->left + (s->right - s->left) * fu0;
	sub.right = s->left + (s->right - s->left) * fu1;
	sub.top = s->top + (s->bottom - s->top) * fv0;
	sub.bottom = s->top + (s->bottom - s->top) * fv1;
	sub.width = (u16)fmaxf(1.0f, fabsf(x1 - x0));
	sub.height = (u16)fmaxf(1.0f, fabsf(y1 - y0));
	spritebank_clamp_sub(&sub, s, h->img.tex);
	C2D_Image img = {h->img.tex, &sub};
	blend(bl);
	C2D_ImageTint tint;
	C2D_PlainImageTint(&tint, c2d(argb), 1.0f);
	float w = x1 - x0, hh = y1 - y0;
	C2D_DrawParams p = {
		.pos = {x0 + w * 0.5f, y0 + hh * 0.5f, w, hh},
		.center = {w * 0.5f, hh * 0.5f},
		.depth = DEPTH,
		.angle = angle,
	};
	C2D_DrawImage(img, &p, &tint);
}

void hg_quad(int t, const float x[4], const float y[4], float u0, float v0, float u1, float v1, uint32_t argb, int bl)
{
	float yt = y[0], yb = y[2];
	if (yb <= yt)
		return;
	for (int row = (int)floorf(yt); row < (int)ceilf(yb); row++) {
		float a = fmaxf(yt, (float)row), b = fminf(yb, (float)(row + 1));
		if (b <= a)
			continue;
		float tm = ((a + b) * 0.5f - yt) / (yb - yt);
		float xl = x[0] + (x[3] - x[0]) * tm, xr = x[1] + (x[2] - x[1]) * tm;
		float va = v0 + (v1 - v0) * (a - yt) / (yb - yt), vb = v0 + (v1 - v0) * (b - yt) / (yb - yt);
		if (fabsf(xr - xl) < 0.01f)
			continue;
		hg_image(t, xl, a, xr, b, u0, va, u1, vb, 0, argb, bl);
	}
}

void hg_tri(const float x[3], const float y[3], const uint32_t argb[3], int bl)
{
	if (!(argb[0] >> 24) && !(argb[1] >> 24) && !(argb[2] >> 24))
		return;
	blend(bl);
	C2D_DrawTriangle(x[0], y[0], c2d(argb[0]), x[1], y[1], c2d(argb[1]), x[2], y[2], c2d(argb[2]), DEPTH);
}

void hg_tri_tex(int t, const float x[3], const float y[3], const float u[3], const float v[3], uint32_t argb, int bl)
{
	(void)u;
	uint32_t c[3];
	for (int k = 0; k < 3; k++) {
		uint32_t g = 0xffffffff;
		if (t >= HT_HYPER_B && t <= HT_HYPER_R) {
			int row = (int)v[k];
			g = hud_hyper_grad[t - HT_HYPER_B][row < 0 ? 0 : row > 255 ? 255 : row];
		}
		uint32_t r = ((g >> 16 & 0xff) * (argb >> 16 & 0xff)) / 255, gg = ((g >> 8 & 0xff) * (argb >> 8 & 0xff)) / 255;
		uint32_t b = ((g & 0xff) * (argb & 0xff)) / 255, a = ((g >> 24) * (argb >> 24)) / 255;
		c[k] = a << 24 | r << 16 | gg << 8 | b;
	}
	hg_tri(x, y, c, bl);
}

void hg_rect(float l, float t, float r, float b, uint32_t argb, int bl)
{
	if (!(argb >> 24) || r <= l || b <= t)
		return;
	blend(bl);
	C2D_DrawRectSolid(l, t, DEPTH, r - l, b - t, c2d(argb));
}

void hg_line(float x0, float y0, float x1, float y1, uint32_t argb, int bl)
{
	if (!(argb >> 24))
		return;
	blend(bl);
	C2D_DrawLine(x0, y0, c2d(argb), x1, y1, c2d(argb), 1.0f, DEPTH);
}

void hud_c2d_draw(const aos_world *w, int W, int H, float camX, float camY, float zoomDiv)
{
	hud_screen sc = hud_screen_for(W, H);
	hud_camera cam = {camX, camY, zoomDiv};
	cur_blend = -1;
	C2D_SetTintMode(C2D_TintMult);
	hud_draw(w, &sc, &cam);
	blend(1);
	C2D_SetTintMode(C2D_TintSolid); /* citro2d default (text rendering relies on it) */
}

void hud_c2d_panel(const aos_world *w, int W, int H)
{
	cur_blend = -1;
	C2D_SetTintMode(C2D_TintMult);
	hud_panel_draw(w, W, H);
	blend(1);
	C2D_SetTintMode(C2D_TintSolid);
}
