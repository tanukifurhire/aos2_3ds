/* Part B textures and draw helpers (selgfx.h). */
#include "selgfx.h"

#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Must match tools/aos2pack/seltex.go selStatic: name, crop origin, scale. */
static const struct {
	const char *name;
	float ox, oy, sc;
} sdesc[BG_COUNT] = {
	[BG_SLBGP] = {"slbgp", 0x70, 0xd4, 0.5f},
	[BG_CHARA00] = {"chara00", 0, 0, 0.25f},
	[BG_CHARA01] = {"chara01", 0, 0, 0.25f},
	[BG_CHARA02] = {"chara02", 0, 0, 0.25f},
	[BG_ICON00] = {"icon00", 0, 0, 0.25f},
	[BG_ICON01] = {"icon01", 0, 0, 0.25f},
	[BG_NAME0] = {"name0", 0, 0, 0.25f},
	[BG_SELSYS] = {"selsys", 0, 0xf0, 0.5f},
	[BG_LOCKB] = {"lockb", 0, 0, 0.25f},
	[BG_LOCKBB] = {"lockbb", 0, 0, 0.25f},
	[BG_LOCKR] = {"lockr", 0, 0, 0.25f},
	[BG_LOCKRB] = {"lockrb", 0, 0, 0.25f},
	[BG_STATBASE] = {"statbase", 0, 0, 0.5f},
	[BG_BAREMPTY] = {"barempty", 0, 0, 0.5f},
	[BG_BAR25] = {"bar25", 0, 0, 0.5f},
	[BG_BAR50] = {"bar50", 0, 0, 0.5f},
	[BG_BAR75] = {"bar75", 0, 0, 0.5f},
	[BG_BARFULL] = {"barfull", 0, 0, 0.5f},
	[BG_RATED] = {"rateD", 0, 0, 0.25f},
	[BG_RATEC] = {"rateC", 0, 0, 0.25f},
	[BG_RATEB] = {"rateB", 0, 0, 0.25f},
	[BG_RATEA] = {"rateA", 0, 0, 0.25f},
	[BG_RATES] = {"rateS", 0, 0, 0.25f},
	[BG_STAR] = {"star", 0, 0, 0.25f},
	[BG_STAGENAME] = {"stagename", 0, 0, 0.5f},
	[BG_LOADING] = {"loading", 0, 0, 0.5f},
	[BG_RESSYSTEM] = {"ressystem", 0, 0, 0.5f},
	[BG_RESSYSTEM2] = {"ressystem2", 0, 0, 0.5f},
	[BG_RESTAG] = {"restag", 0, 0, 0.5f},
	[BG_RESCONT] = {"rescont", 0, 0, 0.5f},
};
static bgfx stat[BG_COUNT];

#define MAX_TRACK 24
static bgfx *track[MAX_TRACK];
static int ntrack;

bool bgfx_exists(const char *name)
{
	char path[96];
	snprintf(path, sizeof path, "romfs:/gfx/m3_b_%s_0.t3x", name);
	FILE *f = fopen(path, "rb");
	if (f)
		fclose(f);
	return f != NULL;
}

bool bgfx_load(bgfx *g, const char *name, float ox, float oy, float sc)
{
	if (g->t.ok && !strcmp(g->key, name))
		return true;
	bgfx_free(g);
	char path[96];
	snprintf(path, sizeof path, "romfs:/gfx/m3_b_%s_0.t3x", name);
	g->ox = ox;
	g->oy = oy;
	g->sc = sc;
	snprintf(g->key, sizeof g->key, "%s", name);
	bool ok = ui_tex_load(&g->t, path, true);
	if (!ok)
		g->key[0] = 0;
	if (g < stat || g >= stat + BG_COUNT) {
		bool known = false;
		for (int i = 0; i < ntrack; i++)
			known |= track[i] == g;
		if (!known && ntrack < MAX_TRACK)
			track[ntrack++] = g;
	}
	return ok;
}

void bgfx_free(bgfx *g)
{
	ui_tex_free(&g->t);
	g->key[0] = 0;
}

bool bg_load(bg_id id) { return bgfx_load(&stat[id], sdesc[id].name, sdesc[id].ox, sdesc[id].oy, sdesc[id].sc); }
void bg_free(bg_id id) { bgfx_free(&stat[id]); }
bgfx *bg_get(bg_id id) { return &stat[id]; }

void bg_free_all(void)
{
	for (int i = 0; i < BG_COUNT; i++)
		bgfx_free(&stat[i]);
	for (int i = 0; i < ntrack; i++)
		bgfx_free(track[i]);
}

size_t bg_bytes(void)
{
	size_t n = 0;
	for (int i = 0; i < BG_COUNT; i++)
		n += stat[i].t.bytes;
	for (int i = 0; i < ntrack; i++)
		n += track[i]->t.bytes;
	return n;
}

static void blend_mode(int blend) { ui_blend(blend == 2 ? 2 : 1); }

void bgfx_draw_c(const bgfx *g, float sx0, float sy0, float sx1, float sy1, float cx, float cy, float hw, float hh,
                 float angle, unsigned argb, int blend, bool mirror)
{
	if (!g->t.ok || !(argb >> 24) || hw <= 0 || hh <= 0)
		return;
	float W = g->t.tex.width, H = g->t.tex.height;
	float u0 = (sx0 - g->ox) * g->sc / W, u1 = (sx1 - g->ox) * g->sc / W;
	float v0 = (sy0 - g->oy) * g->sc / H, v1 = (sy1 - g->oy) * g->sc / H;
	if (mirror) {
		float t = u0;
		u0 = u1;
		u1 = t;
	}
	Tex3DS_SubTexture sub = {
		.width = (u16)(2 * hw < 1 ? 1 : 2 * hw),
		.height = (u16)(2 * hh < 1 ? 1 : 2 * hh),
		.left = u0, .right = u1, .top = 1.0f - v0, .bottom = 1.0f - v1,
	};
	C2D_Image img = {(C3D_Tex *)&g->t.tex, &sub};
	C2D_ImageTint tint;
	C2D_PlainImageTint(&tint, ui_c2d(argb), 1.0f);
	C2D_DrawParams p = {.pos = {cx, cy, 2 * hw, 2 * hh}, .center = {hw, hh}, .depth = UI_DEPTH, .angle = angle};
	blend_mode(blend);
	C2D_SetTintMode(C2D_TintMult);
	C2D_DrawImage(img, &p, &tint);
	C2D_SetTintMode(C2D_TintSolid);
}

void bgfx_draw(const bgfx *g, float sx0, float sy0, float sx1, float sy1, float x0, float y0, float x1, float y1,
               unsigned argb, int blend, bool mirror)
{
	if (x1 <= x0 || y1 <= y0)
		return;
	float hw = (x1 - x0) / 2, hh = (y1 - y0) / 2;
	bgfx_draw_c(g, sx0, sy0, sx1, sy1, x0 + hw, y0 + hh, hw, hh, 0, argb, blend, mirror);
}

void sel_quad_local(float px, float py, float x0, float y0, float x1, float y1, float angle, unsigned argb)
{
	if (!(argb >> 24))
		return;
	float c = cosf(angle), s = sinf(angle);
	float xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1}, X[4], Y[4];
	for (int i = 0; i < 4; i++) {
		X[i] = px + xs[i] * c - ys[i] * s;
		Y[i] = py + xs[i] * s + ys[i] * c;
	}
	u32 col = ui_c2d(argb);
	C2D_DrawTriangle(X[0], Y[0], col, X[1], Y[1], col, X[2], Y[2], col, UI_DEPTH);
	C2D_DrawTriangle(X[0], Y[0], col, X[2], Y[2], col, X[3], Y[3], col, UI_DEPTH);
}

void sel_quad(float cx, float cy, float hw, float hh, float angle, unsigned argb)
{
	if (angle == 0) {
		ui_rect(cx - hw, cy - hh, cx + hw, cy + hh, argb);
		return;
	}
	sel_quad_local(cx, cy, -hw, -hh, hw, hh, angle, argb);
}

/* ---------------------------------------------------------------- characters */

static const char *const fold[19] = {
	[3] = "charaSora", [4] = "charaAlte", [5] = "charaTsih", [6] = "charaMira", [7] = "charaSham",
	[8] = "charaNath", [9] = "charaStar", [10] = "charaSuguri", [11] = "charaSaki", [12] = "charaIru",
	[13] = "charaNanako", [14] = "charaKae", [15] = "charaKyoko", [16] = "charaHime", [17] = "charaSumika",
	[18] = "charaSumika",
};
static const char *const prefix[19] = {
	[3] = "sr", [4] = "at", [5] = "ts", [6] = "mr", [7] = "sm", [8] = "nt", [9] = "sb", [10] = "sg",
	[11] = "sk", [12] = "ir", [13] = "nk", [14] = "ke", [15] = "ky", [16] = "hm", [17] = "sa", [18] = "sa",
};

const char *sel_char_prefix(int c) { return c >= 0 && c < 19 && prefix[c] ? prefix[c] : ""; }

void sel_palette_folder(char *out, size_t n, int chara, int color)
{
	if (chara == 18) { /* Sumika 2nd form: charaAdd (no palettes) */
		snprintf(out, n, "charaAdd");
		return;
	}
	const char *f = chara >= 0 && chara < 19 && fold[chara] ? fold[chara] : "charaSuguri";
	static const char suf[3] = {'A', 'B', 'C'};
	for (int c = color < 0 ? 0 : color > 2 ? 2 : color; c > 0; c--) {
		char probe[64];
		snprintf(out, n, "%s%c", f, suf[c]);
		snprintf(probe, sizeof probe, "reschr_%s", out);
		if (bgfx_exists(probe))
			return;
	}
	snprintf(out, n, "%sA", f);
}

/* ---------------------------------------------------------------- loading */

void sel_draw_loading(void)
{
	/* FUN_005f95c0: clear black, loading.png (0,0,0x1ff,0x3f) centre (W-(int)(120s), H-(int)(40s)) half (0x80,0x10) */
	ui_rect(0, 0, 400, 240, 0xff000000);
	bg_load(BG_LOADING);
	const float s = 0.4f;
	bgfx_draw_c(bg_get(BG_LOADING), 0, 0, 0x200, 0x40, 400 - (int)(120 * s), 240 - (int)(40 * s), 128 * s, 16 * s, 0,
	            0xffffffff, 1, false);
}

/* ---------------------------------------------------------------- tile wipes */

void sel_wipe_draw(const sel_wipe *w)
{
	if (!w->type || (w->type == 0x13 && w->done))
		return;
	const float s4 = 0.5f, s8 = 0.4f; /* W/800, H/600 */
	ui_blend(1);
	for (int r = 0; r < 15; r++)
		for (int c = 0; c < 20; c++) {
			float a = w->a[r][c];
			if (a <= 0)
				continue;
			int hx = (int)(a * 20.0f * s4), hy = (int)(a * 20.0f * s8);
			int x = (int)(40.0f * s4) * c + (int)(20.0f * s4), y = (int)(40.0f * s8) * r + (int)(20.0f * s8);
			sel_quad((float)x, (float)y, (float)hx, (float)hy, 1.0f - a, (unsigned)(int)(255.0f * a) << 24);
		}
}
