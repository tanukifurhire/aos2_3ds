/* Result + continue screen (scene 3): drawing and the 3DS side (logic in select.c).
 * Top = the original at s = 0.4 (FUN_007975a0 / FUN_00799ce0 / FUN_00797ea0 / FUN_0079a240,
 * docs/re/modes_result_sprites.tsv); bottom = the score table and quote with legible fonts,
 * and the continue countdown (§B.6). */
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../platform/stage.h"
#include "modes_flow.h"
#include "scene.h"
#include "select.h"
#include "selgfx.h"
#include "text.h"
#include "ui.h"

#define W 400.0f
#define H 240.0f
#define BW 320.0f
#define S 0.4f
#define I(v) ((float)(int)((v) * S))
#define WHITE 0xffffffffu

static bgfx t_bgp, t_chr, t_sys;
static char quote[600];
static int quote_letters;

static unsigned a8(float a) { return (unsigned)(int)(255.0f * (a < 0 ? 0 : a > 1 ? 1 : a)) << 24; }

/* per-character UI offsets of the result art (stats record +0x2c0/+0x2c4/+0x2c8, core.md §7) */
static void art_offsets(int c, float *dx, float *dy, float *rot)
{
	static const float t[19][3] = {
		[4] = {0, 50, 0}, [5] = {30, -30, 0.5f}, [6] = {0, 50, 0}, [7] = {50, 0, 0}, [8] = {50, 30, 0},
		[9] = {0, 70, 0}, [10] = {30, -70, 0}, [11] = {50, 50, 0}, [13] = {100, 0, 0}, [14] = {-100, 80, 0.75f},
		[15] = {50, 80, 0},
	};
	*dx = *dy = *rot = 0;
	if (c >= 0 && c < 19) {
		*dx = t[c][0];
		*dy = t[c][1];
		*rot = t[c][2];
	}
}

static int utf8_count(const char *s)
{
	int n = 0;
	for (; *s; s++)
		n += ((unsigned char)*s & 0xc0) != 0x80 && *s != '\n';
	return n;
}

static void res_init(void)
{
	result_state *r = &g_res;
	int w = r->winner, c = r->chara[w];
	char folder[32], name[64];
	sel_palette_folder(folder, sizeof folder, c, r->color[w]);
	snprintf(name, sizeof name, "reschr_%s", folder);
	bgfx_load(&t_chr, name, 0, 0, 0.25f);
	snprintf(name, sizeof name, "ressys_%s", folder);
	bgfx_load(&t_sys, name, 0, 0, 0.5f);
	snprintf(name, sizeof name, "resbgp_%s", sel_char_prefix(c));
	bgfx_load(&t_bgp, name, 0, 0x96, 0.25f);
	bg_load(BG_RESSYSTEM);
	bg_load(BG_RESSYSTEM2);
	bg_load(BG_RESTAG);
	bg_load(BG_RESCONT);
	bg_load(BG_LOADING);
	/* win quote, wrapped like Result Init (W - 250s when W/H >= 1.5) */
	char key[40];
	result_quote_key(c, r->quote, key, sizeof key);
	quote[0] = 0;
	if (key[0])
		text_wrap(FONT_WINQUOTE, tr(key), 1.0f, W - I(250), quote, sizeof quote);
	quote_letters = utf8_count(quote);
	stage3ds_end(); /* Result Exit unloads the stages (FUN_004bfbc0); the port draws no stage here */
	result_init();
}

static void res_update(void)
{
	if (min_pressed_nr(MI_TOUCH)) /* port: tap = confirm */
		g_min.nr[MI_OK] = MIN_PRESSED;
	result_update();
}

/* rotated res_* cell from local rect (x0..x1, y0..y1) about (px, py) */
static void cell_local(const bgfx *g, float sx0, float sy0, float sx1, float sy1, float px, float py, float x0, float y0,
                       float x1, float y1, float ang, unsigned col)
{
	float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, c = cosf(ang), s = sinf(ang);
	bgfx_draw_c(g, sx0, sy0, sx1, sy1, px + cx * c - cy * s, py + cx * s + cy * c, (x1 - x0) / 2, (y1 - y0) / 2, ang, col,
	            1, false);
}

static void draw_digits(int v, int ndig, float x, float y, float half, unsigned col, bool small)
{
	const bgfx *g = bg_get(BG_RESSYSTEM);
	int n = 1;
	for (int t = v; t >= 10; t /= 10)
		n++;
	for (int i = 0; i < ndig && i < n; i++) {
		int d = v % 10;
		v /= 10;
		float cx = x + (float)(ndig - i) * (small ? 20 : 30) * S;
		if (small)
			bgfx_draw_c(g, (float)(d * 64), 300, (float)(d * 64 + 63), 0x16b, cx, y, half, half, 0, col, 1, false);
		else
			bgfx_draw_c(g, (float)(d * 0x60), 200, (float)(d * 0x60 + 0x5f), 0x127, cx, y, half, half, 0, col, 1, false);
	}
}

static void score_rows(void) /* FUN_00797ea0 */
{
	result_state *r = &g_res;
	const bgfx *tag = bg_get(BG_RESTAG), *sys = bg_get(BG_RESSYSTEM);
	static const float ry[4] = {-90, -10, 30, 110};
	int vals[4] = {r->r.bonus, r->r.hp, r->r.damage, r->r.score};
	for (int k = 0; k < 4; k++) {
		float a = r->row[k];
		if (a <= 0)
			continue;
		float x = W / 2 - 300 * S + (1 - a) * 50 * S, y = H / 2 + I(ry[k]);
		float hh = (float)(int)(12 * S * a);
		ui_rect_grad(W / 2 - 400 * S, y - hh + 4, W / 2 + 100 * S - hh, y + hh + 4, 0xe1ffffff, 0x00ffffff, 0x00ffffff,
		             0xe1ffffff);
		unsigned col = a8(a) | 0xffffff;
		bgfx_draw_c(tag, 0, (float)(k * 0x40), 0x1ff, (float)(k * 0x40 + 0x3f), x, y, 0x80 * S, 0x10 * S, 0, col, 1, false);
		float half = (float)(int)(24 * S + 24 * S * (1 - a));
		int nd = k == 3 && vals[k] > 99999 ? 6 : 5;
		draw_digits(vals[k], nd, x + 120 * S, y, half, col, false);
		bgfx_draw_c(sys, 0x284, 4, 0x2db, 0x5b, x + (nd == 6 ? 330 : 300) * S, y, 0x16 * S, 0x16 * S, 0, col, 1, false);
		float sh = (float)(int)(16 * S + 16 * S * (1 - a));
		if (k == 0) { /* ( Fight Time: MM:SS ) */
			float yy = y + I(40);
			int s = r->r.seconds;
			bgfx_draw_c(tag, 0x200, 0, 0x3ff, 0x3f, x, yy, 0x80 * S, 0x10 * S, 0, col, 1, false);
			int d[5] = {s / 600, (s % 600) / 60, -1, (s % 60) / 10, s % 10};
			for (int i = 0; i < 5; i++) {
				float dx = x + 160 * S + (float)i * 20 * S;
				if (d[i] < 0)
					bgfx_draw_c(sys, 0x2e0, 0, 799, 0x3f, dx, yy, sh, sh, 0, col, 1, false);
				else
					bgfx_draw_c(sys, (float)(d[i] * 64), 300, (float)(d[i] * 64 + 63), 0x16b, dx, yy, sh, sh, 0, col, 1, false);
			}
			bgfx_draw_c(tag, 0x200, 0x80, 0x3ff, 0xbf, x + 270 * S, yy, 0x80 * S, 0x10 * S, 0, col, 1, false);
		}
		if (k == 2) { /* ( MaxHit: NN Hit ) */
			float yy = y + I(40);
			bgfx_draw_c(tag, 0x200, 0x40, 0x3ff, 0x7f, x, yy, 0x80 * S, 0x10 * S, 0, col, 1, false);
			int h = r->r.hits % 100;
			for (int i = 0; i < 2; i++) {
				int d = i ? h / 10 : h % 10;
				bgfx_draw_c(sys, (float)(d * 64), 300, (float)(d * 64 + 63), 0x16b, x + 140 * S + (float)(2 - i) * 20 * S,
				            yy, sh, sh, 0, col, 1, false);
			}
			bgfx_draw_c(sys, 0x324, 4, 0x37b, 0x3b, x + 215 * S, yy, 0x16 * S, 0xe * S, 0, col, 1, false);
			bgfx_draw_c(tag, 0x200, 0x80, 0x3ff, 0xbf, x + 270 * S, yy, 0x80 * S, 0x10 * S, 0, col, 1, false);
		}
	}
	/* rank emblem */
	float ra = r->rank_a;
	if (ra <= 0)
		return;
	float th2 = -0.15f * r->band;
	float cx = W / 2 + 200 * S + cosf(th2) * 100 * S * ra, cy = H / 2 + 80 * S + sinf(th2) * 100 * S * ra;
	const bgfx *s2 = bg_get(BG_RESSYSTEM2);
	float hw = (float)(int)(88 * S) + (float)(int)((1 - ra) * 44 * S), hh = (float)(int)(48 * S) + (float)(int)((1 - ra) * 24 * S);
	bgfx_draw_c(s2, 4, 4, 0x163, 0xc3, cx, cy, hw, hh, th2 - 0.5f, a8(ra) | 0xffffff, 1, false);
	bgfx_draw_c(s2, 0x16c, 4, 0x2cf, 0xc3, cx, cy, hw, hh, th2 - 0.5f, a8(ra) | 0xffffff, 2, false);
	ui_blend(1);
	int k = r->rank;
	float sx0 = (float)((k % 2) * 0x200 + 4), sy0 = (float)((k / 2) * 0x80 + 0x204);
	float lw = (float)(int)(126 * S + (1 - ra) * 32 * S);
	bgfx_draw_c(sys, sx0, sy0, sx0 + 0x1f7, sy0 + 0x77, cx, cy, lw, lw / 4, th2, a8(ra) | 0xffffff, 1, false);
	float lw2 = 126 * S + (1 - ra) * 64 * S;
	int al = (int)(255 * ra);
	bgfx_draw_c(sys, sx0, sy0, sx0 + 0x1f7, sy0 + 0x77, cx, cy, lw2, lw2 / 4, th2,
	            (unsigned)al << 24 | (unsigned)(255 - al) * 0x010101u, 2, false);
	ui_blend(1);
}

static void continue_overlay(void) /* FUN_0079a240 */
{
	result_state *r = &g_res;
	float c = r->cont_a;
	if (c <= 0)
		return;
	ui_rect(0, 0, W, H, (unsigned)(int)(c * 164) << 24);
	float y = H / 2 - I(110), h = I(32);
	ui_rect(0, y - h, W, y + h, (unsigned)(int)(c * 164) << 24 | 0xffffff);
	ui_hline(0, W, y - h, a8(c));
	ui_hline(0, W, y + h - 1, a8(c));
	const bgfx *t = bg_get(BG_RESCONT);
	unsigned col = a8(c) | 0xffffff;
	bgfx_draw_c(t, 0, 0, 799, 99, W / 2, y, 200 * S, 0x19 * S, 0, col, 1, false);
	bgfx_draw_c(t, 4, 0x218, 0x18b, 0x24b, W / 2, H / 2 - I(50), 0x62 * S, 0xd * S, 0, col, 1, false);
	int d = r->count < 0 ? 0 : r->count;
	float sx = (float)((d % 4) * 256), sy = (float)((d / 4) * 0x90 + 100);
	bgfx_draw_c(t, sx, sy, sx + 0xff, sy + 0xf3 - 100, W / 2, H / 2 + I(30), 0x40 * S, 0x24 * S, 0, col, 1, false);
}

static void res_draw_top(void) /* FUN_007975a0 */
{
	result_state *r = &g_res;
	if (r->leave) {
		sel_draw_loading();
		return;
	}
	ui_rect(0, 0, W, H, 0xff555555);
	int h = (int)(H * 2.829f / 2);
	bgfx_draw(&t_bgp, 0, 0x96, 0x7ff, 0x369, W / 2 - (float)h, 0, W / 2 + (float)h, H, WHITE, 1, false);
	float band = r->band;
	float th1 = 0.15f * band + 1.0471976f;
	/* blue band + "winner's report" strip (rotated 60 deg) */
	float bx = W / 2 + I(260), by = H / 2 - (float)(int)(80 * S * band);
	bgfx_draw_c(&t_sys, 2, 0x82, 0x3d, 0xbd, bx, by, (float)(int)(600 * S), (float)(int)(15 * S * band), th1, WHITE, 1,
	            false);
	int n = (int)(H / 160) + 2;
	if (n & 1)
		n++;
	for (int i = -n / 2; i < n / 2; i++) {
		float d = (float)i * 320 * S - 320 * S * r->scroll;
		cell_local(bg_get(BG_RESSYSTEM), 2, 0x17e, 0x27d, 0x1b9, W / 2 + 260 * S, H / 2 - 80 * S * band, d - 160 * S,
		           -16 * S * band, d + 160 * S, 16 * S * band, th1, WHITE);
	}
	/* WINNER + tag / name */
	bgfx_draw_c(bg_get(BG_RESSYSTEM), 0, 0, 0x27f, 0x9f, W / 2 - 400 * S + 150 * S, I(40), 0xa0 * S, 0x28 * S, 0, WHITE, 1,
	            false);
	float tx = W / 2 - 400 * S + 550 * S, ty = I(40);
	if (g_setup.mode == MODE_MATCH || g_setup.mode == MODE_TRAINING) {
		float sy = r->input[r->winner] == INPUT_CPU ? 0x300 : r->winner ? 0x280 : 0x200;
		bgfx_draw_c(bg_get(BG_RESTAG), 0, sy, 0x3ff, sy + 0x7f, tx, ty + I(4), 0xff * S, 0x20 * S, 0, WHITE, 1, false);
	} else {
		bgfx_draw_c(&t_sys, 0, 0, 0x3ff, 0x7f, tx, ty, 0xff * S, 0x20 * S, 0, WHITE, 1, false);
	}
	/* character art: slides in from the left over 50 frames */
	float dx, dy, rot;
	art_offsets(r->chara[r->winner], &dx, &dy, &rot);
	float sl = (float)r->slide;
	bgfx_draw_c(&t_chr, 0, 0, 0x7ff, 0x7ff, W / 2 + 200 * S - sl * sl / 2 + dx * S, H / 2 + 120 * S + dy * S, 0x200 * S,
	            0x200 * S, (-sl / 50.0f) * rot, WHITE, 1, false);
	/* FUN_00799ce0: dark band, blue strip, small strips, win quote (rotated th2) */
	float th2 = -0.15f * band, qx = W / 2, qy = H / 2 + I(230);
	sel_quad(qx, qy, (float)(int)(600 * S), 80 * S * band, th2, (unsigned)(255 - (int)(128 * band)) << 24);
	cell_local(&t_sys, 2, 0x82, 0x3d, 0xbd, qx, qy, -600 * S, -80 * S * band - 10 * S * band, 600 * S,
	           -80 * S * band + 12 * S * band, th2, WHITE);
	int m = (int)(W / 100) + 10;
	if (m & 1)
		m++;
	for (int i = -m / 2; i < m / 2; i++) {
		float d = (float)i * 200 * S - 200 * S * r->scroll;
		cell_local(bg_get(BG_RESSYSTEM), 2, 0x1ce, 0x18d, 0x1f1, qx, qy, d - 100 * S, -80 * S * band - 10 * S * band,
		           d + 100 * S, -80 * S * band + 10 * S * band, th2, WHITE);
	}
	if (quote[0] && band > 0) {
		/* letters fade in 2 frames apart (FUN_0079a7d0 alphas): draw the visible prefix */
		int shown = 0;
		while (shown < quote_letters && result_letter_alpha(shown) > 0)
			shown++;
		if (shown) {
			char buf[600];
			int k = 0;
			size_t i = 0;
			for (; quote[i] && i < sizeof buf - 1; i++) {
				unsigned char ch = (unsigned char)quote[i];
				if ((ch & 0xc0) != 0x80 && ch != '\n') {
					if (k == shown)
						break;
					k++;
				}
				buf[i] = quote[i];
			}
			buf[i] = 0;
			float lx = W / 2 - 350 * S, ly = H / 2 + 230 * S - 10 * S;
			text_draw_rot(FONT_WINQUOTE, buf, lx, ly + 8, TEXT_LEFT, ui_c2d(WHITE), 1.0f, th2, lx, ly);
		}
	}
	if (r->sub == RES_WON || r->sub == RES_LEAVING)
		score_rows();
	continue_overlay();
	sel_wipe_draw(&r->wipe);
}

static void res_draw_bottom(void)
{
	result_state *r = &g_res;
	ui_rect_grad(0, 0, BW, 240, 0xff1a2c5a, 0xff1a2c5a, 0xff05070f, 0xff05070f);
	if (r->leave)
		return;
	if (r->sub == RES_CONTINUE || (r->sub == RES_LEAVING && r->cont_a > 0)) {
		const bgfx *t = bg_get(BG_RESCONT);
		bgfx_draw_c(t, 4, 0x218, 0x18b, 0x24b, BW / 2, 40, 0x62 * 0.8f, 0xd * 0.8f, 0, WHITE, 1, false);
		int d = r->count < 0 ? 0 : r->count;
		float sx = (float)((d % 4) * 256), sy = (float)((d / 4) * 0x90 + 100);
		bgfx_draw_c(t, sx, sy, sx + 0xff, sy + 0xf3 - 100, BW / 2, 110, 0x40, 0x24, 0, WHITE, 1, false);
		text_draw(FONT_SYSTEM, "A: Continue     B: count down", BW / 2, 170, TEXT_CENTER, ui_c2d(WHITE), 1.0f, 0);
		return;
	}
	/* quote in full */
	int w = r->winner;
	const char *nk = text_id_key(0x1f8 + r->chara[w] - 3);
	text_draw(FONT_MENU, nk ? tr(nk) : "", 8, 12, TEXT_LEFT, ui_c2d(WHITE), 1.0f, 200);
	static char q[700];
	char key[40];
	result_quote_key(r->chara[w], r->quote, key, sizeof key);
	if (key[0]) {
		text_wrap(FONT_SYSTEM, tr(key), 1.0f, BW - 16, q, sizeof q);
		text_draw(FONT_SYSTEM, q, 8, 34, TEXT_LEFT, ui_c2d(0xffe0e0e0), 1.0f, 0);
	}
	if (r->sub != RES_WON && r->sub != RES_LEAVING)
		return;
	/* score table (the same numbers as the top screen) */
	const bgfx *tag = bg_get(BG_RESTAG);
	int vals[4] = {r->r.bonus, r->r.hp, r->r.damage, r->r.score};
	for (int k = 0; k < 4; k++) {
		float a = r->row[k];
		if (a <= 0)
			continue;
		float y = 104 + 22 * (float)k;
		bgfx_draw_c(tag, 0, (float)(k * 0x40), 0x1ff, (float)(k * 0x40 + 0x3f), 60, y, 56, 7, 0, a8(a) | 0xffffff, 1, false);
		char b[24];
		snprintf(b, sizeof b, "%d pt", (int)vals[k]);
		text_draw(FONT_MENU, b, 246, y, TEXT_RIGHT, ui_c2d(a8(a) | 0xffffff), 0.9f, 0);
	}
	if (r->row[3] > 0) {
		char b[64];
		snprintf(b, sizeof b, "Fight Time %d:%02d   Max Hit %d", (int)(r->r.seconds / 60), (int)(r->r.seconds % 60), (int)r->r.hits);
		text_draw(FONT_SYSTEM, b, BW / 2, 192, TEXT_CENTER, ui_c2d(a8(r->row[3]) | 0xc0c0c0), 1.0f, BW - 10);
	}
	if (r->rank_a > 0) {
		int k = r->rank;
		float sx0 = (float)((k % 2) * 0x200 + 4), sy0 = (float)((k / 2) * 0x80 + 0x204);
		bgfx_draw_c(bg_get(BG_RESSYSTEM), sx0, sy0, sx0 + 0x1f7, sy0 + 0x77, 286, 136, 32, 8, -0.15f,
		            a8(r->rank_a) | 0xffffff, 1, false);
	}
}

static void res_exit(void) { bg_free_all(); }

static const scene_def res_scene = {
	.name = "result", .init = res_init, .update = res_update, .draw_top = res_draw_top,
	.draw_bottom = res_draw_bottom, .exit = res_exit,
};

void result_scene_register(void) { scene_register(SCENE_RESULT, &res_scene); }
