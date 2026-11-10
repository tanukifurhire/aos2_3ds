/*
 * Drawing of the Part D menu sub-screens (modes_scenes.h): ranking view FUN_0065c7c0,
 * name entry FUN_00658970, unlock popup FUN_006471d0 (docs/re/modes.md §D.2.3, §D.5).
 *
 * 3DS layout: the PC formulas evaluated at s = 0.5 instead of the menu's 0.4 (the 10
 * ranking rows then fill the 240 px top screen: rows 21 px apart, eye strips 100x16 =
 * their packed size, so they are drawn 1:1). Rows use the menulabel font (PC: the Impact
 * "memory" font, not packed) with the columns spread over the full width (RX..EX); the score is drawn at the row size (the PC builds it
 * at scale 1.0 = 1.8x taller than the names at 800x600, an oversight that would overflow
 * the 16 px band here). Bottom screen: page / key hints under the help line.
 */
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "menu.h"
#include "modes_flow.h"
#include "modes_scenes.h"
#include "scene.h"
#include "text.h"
#include "ui.h"

#define W 400.0f
#define H 240.0f
#define BW 320.0f
#define SR 0.5f
#define PI_F 3.14159265f

static ui_tex tx_face, tx_rank; /* romfs:/gfx/m3_face_0.t3x (ttl_face x0.25), m3_ranking_0.t3x (ttl_ranking (200,200)+ x0.5) */

static void gfx_load(void)
{
	if (!tx_face.ok)
		ui_tex_load(&tx_face, "romfs:/gfx/m3_face_0.t3x", true);
	if (!tx_rank.ok)
		ui_tex_load(&tx_rank, "romfs:/gfx/m3_ranking_0.t3x", true);
}

void ranking_gfx_free(void)
{
	ui_tex_free(&tx_face);
	ui_tex_free(&tx_rank);
}

/* src in original texture pixels; texel = (src - origin) * scale */
static void tdraw(ui_tex *t, float ox, float oy, float sc, float sx0, float sy0, float sx1, float sy1, float x0, float y0,
                  float x1, float y1, unsigned argb)
{
	if (!t->ok || !(argb >> 24) || x1 <= x0 || y1 <= y0)
		return;
	const float TW = t->tex.width, TH = t->tex.height;
	Tex3DS_SubTexture sub = {
		.width = (u16)(x1 - x0 < 1 ? 1 : x1 - x0),
		.height = (u16)(y1 - y0 < 1 ? 1 : y1 - y0),
		.left = (sx0 - ox) * sc / TW,
		.right = (sx1 - ox) * sc / TW,
		.top = 1.0f - (sy0 - oy) * sc / TH,
		.bottom = 1.0f - (sy1 - oy) * sc / TH,
	};
	C2D_Image img = {&t->tex, &sub};
	C2D_ImageTint tint;
	C2D_PlainImageTint(&tint, ui_c2d(argb), 1.0f);
	C2D_DrawParams p = {.pos = {x0, y0, x1 - x0, y1 - y0}, .center = {0, 0}, .depth = UI_DEPTH, .angle = 0};
	C2D_SetTintMode(C2D_TintMult);
	C2D_DrawImage(img, &p, &tint);
	C2D_SetTintMode(C2D_TintSolid);
}

static unsigned wa(float a) { return ui_argb((int)(255.0f * a), 0xffffff); }

/* eye strip of character c: ttl_face (400(c>>4), 64(c&15)) 400x64, half (100s, 16s) */
static void eyes(int c, float cx, float cy, float a)
{
	if (c < 0 || c > 0x1f)
		return;
	float sx = 400.0f * (float)(c >> 4), sy = 64.0f * (float)(c & 15);
	tdraw(&tx_face, 0, 0, 0.25f, sx, sy, sx + 400, sy + 64, cx - 100 * SR, cy - 16 * SR, cx + 100 * SR, cy + 16 * SR, wa(a));
}

/* header strip + pill (same look as menu_draw.c's pill at the menu's text size) */
static void header(float yh, float a, unsigned strip, float py0, float py1, int label)
{
	ui_rect(0, yh - 6, W, yh + 6, ui_argb((int)(128 * a), strip));
	const char *t = tr_id(label);
	float x1 = text_width(FONT_MENULABEL, t, 1.0f) + 18;
	ui_mtex_draw(MT_PILLS, 0x2ef, py0, 0x2f0, py1, 0, yh - 8, x1, yh + 8, wa(a));
	ui_mtex_draw(MT_PILLS, 0x2f0, py0, 0x307, py1, x1, yh - 8, x1 + 12, yh + 8, wa(a));
	text_draw(FONT_MENULABEL, t, 8, yh, TEXT_LEFT, ui_c2d(wa(a)), 1.0f, 0);
}

static void band(float y, float half, float a, int alpha_max)
{
	ui_rect(0, y - half, W, y + half, ui_argb((int)((float)alpha_max * a), 0xffffff));
	ui_hline(0, W, y - half, ui_argb((int)(255 * a), 0));
	ui_hline(0, W, y + half - 1, ui_argb((int)(255 * a), 0));
}

#define ROW_SCALE 1.0f
#define ROW_FONT FONT_MENULABEL
/* columns relative to the row centre x (PC at s = 0.5: rank -120, name -100, score right +20, eyes +70;
 * spread over the full 400 px so the port font fits) */
#define RX (-180.0f)
#define NX (-140.0f)
#define SX (90.0f)
#define EX (140.0f)

static void draw_view(void) /* FUN_0065c7c0 */
{
	float a = g_rank.a;
	if (a <= 0)
		return;
	int t = g_menu.counter, page = g_rank.page;
	float slide = (float)(int)(480 * SR * (1 - a));
	float yh = H / 2 - 180 * SR + slide - 20 * SR;
	header(yh, a, 0x9cffa5, 0x68, 0x87, 0x255); /* green pill, MENULABEL_RANKING */
	text_draw(FONT_MENU, tr_id(ranking_title_id(page)), W / 2 - 100 * SR + 50, yh, TEXT_CENTER, ui_c2d(wa(a)), 0.9f, 0); /* +50: clear of the pill */
	for (int k = 0; k < 3; k++) { /* arrows with a 3-frame trail */
		int o = (int)(sinf(2 * PI_F * (float)(t % 100) / 100.0f) * (float)(3 * k + 5) + (float)(5 * k + 5));
		float lx = W / 2 - 180 * SR + 50 - (float)o * SR, rx = W / 2 - 20 * SR + 50 + (float)o * SR, h = 15 * SR;
		tdraw(&tx_rank, 200, 200, 0.5f, 200 + 60 * k, 360, 260 + 60 * k, 420, lx - h, yh - h, lx + h, yh + h, wa(a));
		tdraw(&tx_rank, 200, 200, 0.5f, 380 + 60 * k, 360, 440 + 60 * k, 420, rx - h, yh - h, rx + h, yh + h, wa(a));
	}
	int n = ranking_rows(page);
	for (int k = 0; k < n; k++) {
		float f = 1 - fabsf(g_rank.v[k]);
		if (f <= 0)
			continue;
		float af = a * f;
		float x = W / 2 + (float)(int)(50 * SR * g_rank.v[k]);
		float y = H / 2 - 160 * SR + (float)(int)(42 * SR) * (float)k + slide;
		band(y, 16 * SR, af, 128);
		char rk[16], name[24], sc[16];
		u32 col = ui_c2d(wa(af));
		if (page < 3) {
			snprintf(rk, sizeof rk, "#%d", k + 1);
			char nm[8];
			ranking_name(page, k, false, nm);
			snprintf(name, sizeof name, "%s", nm);
			snprintf(sc, sizeof sc, "%ld", (long)ranking_score(page, k));
			text_draw(ROW_FONT, sc, x + SX, y, TEXT_RIGHT, col, ROW_SCALE, 0);
		} else {
			snprintf(rk, sizeof rk, "%s", ranking_tour_rank(page, k));
			snprintf(name, sizeof name, "%s", ranking_tour_name(page, k));
		}
		text_draw(ROW_FONT, rk, x + RX, y, TEXT_LEFT, col, ROW_SCALE, 0);
		text_draw(ROW_FONT, name, x + NX, y, TEXT_LEFT, col, ROW_SCALE, page < 3 ? 0 : 205);
		eyes(ranking_char(page, k), x + EX, y, af);
		if (page == g_rank.hl_diff && k == g_rank.hl_rank) { /* the run's new entry, pulsing */
			ui_blend(2);
			float p = sinf(2 * PI_F * (float)(t % 50) / 50.0f) * 64 + 64;
			ui_rect(0, y - 16 * SR, W, y + 16 * SR, ui_argb((int)(p * af), 0xc0e1ff));
			ui_blend(1);
		}
	}
}

static void draw_name(void) /* FUN_00658970 */
{
	float b = g_rank.b;
	if (b <= 0)
		return;
	int t = g_menu.counter;
	bool ranked = g_rank.hl_rank < 10;
	float yh = H / 2 + (float)(int)(480 * SR * (1 - b)) - 30 * SR;
	header(yh, b, 0xffd09c, 0x98, 0xb7, 0x256); /* orange pill, MENULABEL_SCORE */
	if (ranked && (t / 20) % 2 == 0)
		text_draw(FONT_MENU, tr_id(0x23e), W / 2 - 180 * SR + 40, yh, TEXT_CENTER, ui_c2d(wa(b)), 1.0f, 0); /* RANKED!
		                                    * (PC x = W/2 - 180s; +40 px keeps it clear of the larger pill) */
	float y = H / 2;
	band(y, 16 * SR, b, 192);
	u32 col = ui_c2d(wa(b));
	char rk[16], sc[16], name[8];
	if (ranked) {
		snprintf(rk, sizeof rk, "#%d", g_rank.hl_rank + 1);
		ranking_name(g_rank.page, g_rank.hl_rank, true, name);
		text_draw(ROW_FONT, rk, W / 2 + RX, y, TEXT_LEFT, col, ROW_SCALE, 0);
		text_draw(ROW_FONT, name, W / 2 + NX, y, TEXT_LEFT, col, ROW_SCALE, 0);
		if (g_rank.pos < 7) { /* expanding ring at the end of the (trimmed) name — PC quirk kept */
			float p = g_rank.ring;
			float cx = W / 2 + NX + text_width(ROW_FONT, name, ROW_SCALE) - 2 * SR;
			int pos = g_rank.pos > 6 ? 6 : g_rank.pos;
			if (modes_save()->rank[g_rank.page][g_rank.hl_rank].name[pos] == RANK_BLANK)
				cx += 10 * SR;
			float h = (float)(int)((float)(int)(50 * p) * SR);
			tdraw(&tx_rank, 200, 200, 0.5f, 200, 200, 360, 360, cx - h, y - h, cx + h, y + h,
			      ui_argb((int)((1 - p) * 128 * b), 0xffffff));
		}
	}
	snprintf(sc, sizeof sc, "%ld", (long)g_rank.score);
	text_draw(ROW_FONT, sc, W / 2 + SX, y, TEXT_RIGHT, col, ROW_SCALE, 0);
	eyes(g_rank.chara, W / 2 + EX, y, b);
}

static void draw_unlock(void) /* FUN_006471d0 */
{
	float A = g_rank.pa;
	if (A <= 0 || g_rank.nmsg <= 0)
		return;
	float y0 = H / 2 - 32 * SR, y1 = H / 2 + 32 * SR;
	ui_rect(0, y0, W, y1, ui_argb((int)(192 * A), 0));
	ui_hline(0, W, y0, ui_argb((int)(255 * A), 0xffffff));
	ui_hline(0, W, y1 - 1, ui_argb((int)(255 * A), 0xffffff));
	int i = g_rank.msg < g_rank.nmsg ? g_rank.msg : g_rank.nmsg - 1;
	int32_t id = modes_flow_msg_count() > i ? modes_flow_msg(i) : 1000;
	text_draw(FONT_MENU, tr_id(modes_msg_text(id)), W / 2, H / 2, TEXT_CENTER,
	          ui_c2d(ui_argb((int)(255 * A * g_rank.pb), 0xffffff)), 1.0f, W - 16);
}

void ranking_draw_top(void)
{
	if (g_rank.a <= 0 && g_rank.b <= 0 && g_rank.pa <= 0) {
		ranking_gfx_free(); /* textures only while a sub-screen is visible */
		return;
	}
	gfx_load();
	ui_blend(1);
	draw_view();
	draw_name();
	draw_unlock();
}

void ranking_draw_bottom(void)
{
	const char *hint = NULL;
	char line[64];
	switch (g_menu.state) {
	case MS_RANKING:
		/* the main band is hidden here, so cover menu_draw's dimmed bottom logo */
		ui_rect_grad(0, 0, BW, 240, 0xff1a2c5a, 0xff1a2c5a, 0xff05070f, 0xff05070f);
		snprintf(line, sizeof line, "%s  %d / %d", tr_id(ranking_title_id(g_rank.page)), g_rank.page + 1, RANK_PAGES);
		text_draw(FONT_MENUBIG, line, BW / 2, 70, TEXT_CENTER, ui_c2d(wa(g_rank.a)), 1.0f, BW - 10);
		hint = "Left/Right: page    A/B: back";
		break;
	case MS_NAME_ENTRY:
		if (g_rank.hl_rank < 10)
			text_draw(FONT_SYSTEM, "Up/Down: letter   Left/Right: move", BW / 2, 100, TEXT_CENTER, ui_c2d(0xffa4a4a4), 1.0f,
			          BW - 10);
		hint = g_rank.hl_rank < 10 ? "A: next letter (7th = done)   B: delete" : "A/B: ranking";
		break;
	case MS_UNLOCK: hint = "A: next"; break;
	default: return;
	}
	text_draw(FONT_SYSTEM, hint, BW / 2, 120, TEXT_CENTER, ui_c2d(0xffa4a4a4), 1.0f, BW - 10);
}
