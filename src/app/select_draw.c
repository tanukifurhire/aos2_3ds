/* ArcSelect (scene 8) and VS Select (scene 6): drawing, 3DS side and scene
 * registration (logic in select.c). Top screen = the original layout at s = 0.4
 * (docs/re/modes.md §B.1/§B.2, modes_select_sprites.tsv); the stats panel, the
 * per-player option rows and the stage/BGM lists move to the bottom screen with
 * legible fonts (§B.6). Also the pause-menu overlay of the Battle scene. */
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../input/hyper_touch.h"
#include "../platform/panel.h"
#include "../platform/stage.h"
#include "audio.h"
#include "modes_flow.h"
#include "movelist.h"
#include "music.h"
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
#define GREY 0xffa4a4a4u
#define PI 3.14159265f

static unsigned a8(float a) { return (unsigned)(int)(255.0f * (a < 0 ? 0 : a > 1 ? 1 : a)) << 24; }
static u32 c2d(unsigned argb) { return ui_c2d(argb); }

/* ================================================================ shared pieces */

static void draw_bgp(void) /* sl_bgp (0x70,0xd4,0x38f,0x32c) -> (0,0,W,H) */
{
	bgfx_draw(bg_get(BG_SLBGP), 0x70, 0xd4, 0x38f, 0x32c, 0, 0, W, H, WHITE, 1, false);
}

static void portrait_rect(int c, float *sx, float *sy, bg_id *tex)
{
	int i = (c + 4) % 12;
	*tex = (c + 4) / 12 ? BG_CHARA01 : BG_CHARA00;
	*sx = (float)((i % 6) * 320);
	*sy = (float)((i / 6) * 1024);
}

static void draw_noise(int frame, float cx, float cy, unsigned col)
{
	int n = (frame / 4) % 8;
	float sx = (float)((n < 4 ? n : n - 4) * 320), sy = n < 4 ? 0 : 1024;
	bgfx_draw_c(bg_get(BG_CHARA02), sx, sy, sx + 319, sy + 1023, cx, cy, 0x50 * S, 0x100 * S, 0, col, 1, false);
}

static void draw_icon(int id, float cx, float cy, float half, unsigned col)
{
	int c = id % 16;
	float sx = (float)((c % 4) * 256), sy = (float)((c / 4) * 256);
	bgfx_draw_c(bg_get(id / 16 ? BG_ICON01 : BG_ICON00), sx, sy, sx + 255, sy + 255, cx, cy, half, half, 0, col, 1, false);
}

static void draw_name_plate(int id, float cx, float cy, float hw, float hh, unsigned col)
{
	float sx = 0, sy = 0;
	if (id != SEL_RANDOM) {
		sx = (float)((id - 2) % 2 * 512);
		sy = (float)((id - 2) / 2 * 128);
	}
	bgfx_draw_c(bg_get(BG_NAME0), sx, sy, sx + 511, sy + 127, cx, cy, hw, hh, 0, col, 1, false);
}

static void draw_ring(bool p2, float cx, float cy, float cd0, float cc0, int ring)
{
	float half = (float)(int)((1 - cc0) * 48 * S + 96 * S);
	float ang = (float)(ring % 360) * PI / 180.0f;
	float pulse = sinf((float)(ring % 100) / 100.0f * 2 * PI) * 0.25f + 0.75f;
	bgfx_draw_c(bg_get(p2 ? BG_LOCKRB : BG_LOCKBB), 0, 0, 0x1ff, 0x1ff, cx, cy, half, half, ang,
	            a8(cd0 * cc0 * pulse) | 0xffffff, 1, false);
	bgfx_draw_c(bg_get(p2 ? BG_LOCKR : BG_LOCKB), 0, 0, 0x1ff, 0x1ff, cx, cy, half, half, ang, a8(cd0 * cc0) | 0xffffff,
	            2, false);
	ui_blend(1);
}

static void letterbox(void)
{
	ui_rect(0, 0, W, H / 2 - I(250), WHITE);
	ui_rect(0, H / 2 + I(250), W, H, WHITE);
}

static void title(void) /* FUN_007aa5b0: sel_system (0,0x200,0x3ff,0x27f) at (W/2, H/2-275s) */
{
	bgfx_draw_c(bg_get(BG_SELSYS), 0, 0x200, 0x3ff, 0x27f, W / 2, H / 2 - I(275), 0x100 * S, 0x20 * S, 0, WHITE, 1, false);
}

static void bottom_bg(void)
{
	ui_rect_grad(0, 0, BW, 240, 0xff1a2c5a, 0xff1a2c5a, 0xff05070f, 0xff05070f);
}

/* header pill like the menus (A.2.3): green strip + text */
static void pill_label(float y, unsigned strip, const char *label, float a, float w)
{
	ui_rect(0, y - 6, w, y + 6, ui_argb((int)(128 * a), strip));
	float tw = text_width(FONT_MENULABEL, label, 1.0f) + 18;
	ui_rect(0, y - 8, tw, y + 8, a8(a) | 0xffffff);
	text_draw(FONT_MENULABEL, label, 8, y, TEXT_LEFT, c2d(a8(a) | 0x000000), 1.0f, 0);
}

static void stats_panel(int id, float x, float y, float a) /* FUN_007a49d0, bottom-screen layout */
{
	if (a <= 0 || id < 3)
		return;
	sel_stats st = sel_char_stats(id);
	char buf[64];
	snprintf(buf, sizeof buf, "%s %s", tr_id(0x1ea), tr_id(0x1eb + st.diff));
	text_draw(FONT_SYSTEM, buf, x, y, TEXT_LEFT, c2d(a8(a) | 0xffffff), 0.8f, 108);
	static const bg_id fill[5] = {BG_COUNT, BG_BAR25, BG_BAR50, BG_BAR75, BG_BARFULL};
	static const bg_id rate[5] = {BG_RATED, BG_RATEC, BG_RATEB, BG_RATEA, BG_RATES};
	for (int k = 0; k < 4; k++) {
		float yk = y + 18 + 30 * (float)k;
		text_draw(FONT_SYSTEM, tr_id(0x1ee + k), x, yk, TEXT_LEFT, c2d(a8(a) | 0xffffff), 0.8f, 108);
		float by = yk + 9;
		bgfx_draw(bg_get(BG_BAREMPTY), 0, 0, 0xbc, 0x28, x, by, x + 86, by + 18, a8(a) | 0xffffff, 1, false);
		int r = st.rank[k] < 0 ? 0 : st.rank[k] > 4 ? 4 : st.rank[k];
		if (fill[r] != BG_COUNT)
			bgfx_draw(bg_get(fill[r]), 0xe, 0, 0xbc, 0x28, x + 6, by + 1, x + 86, by + 17, a8(a) | 0xffffff, 1, false);
		bgfx_draw_c(bg_get(rate[r]), 0, 0, 0x80, 0x80, x + 98, by + 9, 9, 9, 0, a8(a) | 0xffffff, 1, false);
	}
}

static void load_select_textures(void)
{
	static const bg_id need[] = {BG_SLBGP, BG_CHARA00, BG_CHARA01, BG_CHARA02, BG_ICON00, BG_ICON01, BG_NAME0,
	                             BG_SELSYS, BG_LOCKB, BG_LOCKBB, BG_LOCKR, BG_LOCKRB, BG_BAREMPTY, BG_BAR25,
	                             BG_BAR50, BG_BAR75, BG_BARFULL, BG_RATED, BG_RATEC, BG_RATEB, BG_RATEA, BG_RATES,
	                             BG_STAR, BG_LOADING};
	for (unsigned i = 0; i < sizeof need / sizeof need[0]; i++)
		bg_load(need[i]);
}

static void select_free(void)
{
	bg_free_all();
	stage3ds_end(); /* the match loads its own stage after the character atlases */
}

/* ================================================================ ArcSelect */

static bool cleared(int id)
{
	const aos_save *sv = modes_save();
	return g_setup.mode == MODE_STORY ? modes_story_cleared(sv, id) : modes_arcade_cleared(sv, g_setup.difficulty, id);
}

static void arc_init(void)
{
	load_select_textures();
	arcsel_init();
}

static void arc_touch(void)
{
	arcsel_state *a = &g_arc;
	if (!min_pressed_nr(MI_TOUCH))
		return;
	int tx = g_min.touch_x, ty = g_min.touch_y;
	if (a->sub == ARC_COMMAND) {
		int k = ty >= 26 && ty < 110 ? 0 : ty >= 110 && ty < 194 ? 1 : -1;
		if (k < 0)
			return;
		if (k == a->cmd)
			g_min.st[MI_OK] = MIN_PRESSED;
		else {
			a->cmd = k;
			audio_se(2, AUDIO_SE_CD_DEFAULT);
		}
	} else if (a->sub == ARC_CHAR) {
		int col = (tx - 4) / 40, row = (ty - 26) / 40;
		if (tx < 4 || ty < 26 || col > 4 || row > 2)
			return;
		int i = row * 5 + col;
		if (i >= a->n)
			return;
		if (i == a->cursor)
			g_min.st[MI_OK] = MIN_PRESSED;
		else {
			a->d74 = i > a->cursor ? -1.0f : 1.0f;
			a->cc0 = 0;
			a->cursor = i;
			audio_se(2, AUDIO_SE_CD_DEFAULT);
		}
	}
}

static void arc_update(void)
{
	stage3ds_menu_tick(); /* select background = stage 4 (FUN_004bfd30) */
	arc_touch();
	arcsel_update();
}

static void arc_draw_top(void) /* FUN_0079ed60 */
{
	arcsel_state *a = &g_arc;
	if (a->leave) {
		sel_draw_loading();
		return;
	}
	draw_bgp();
	stage3ds_draw();
	ui_blend_reset();
	int c = arcsel_char();
	/* name band */
	float yb = H / 2 + I(50), hb = I(24);
	ui_rect(0, yb - hb, W, yb + hb, (unsigned)(int)(128 * a->ca8) << 24);
	ui_hline(0, W, yb - hb, a8(a->ca8) | 0xffffff);
	ui_hline(0, W, yb + hb - 1, a8(a->ca8) | 0xffffff);
	/* portrait column */
	draw_noise(a->frame, I(80), H / 2, a8(a->ca8) | 0x909090);
	float sx, sy;
	bg_id t;
	portrait_rect(c, &sx, &sy, &t);
	unsigned pc = a8(a->ca8 * a->cc0) | 0xffffff;
	bgfx_draw_c(bg_get(t), sx, sy, sx + 319, sy + 1023, I(80), H / 2, 0x50 * S, 0x100 * S, 0, pc, 1, false);
	draw_name_plate(c, W / 2, yb, 0x80 * S, 0x20 * S, pc);
	/* icon carousel FUN_0079fb30 */
	float iy = H / 2 + I(150);
	for (int k = -5; k <= 5; k++) {
		int al = (int)((float)(255 - 55 * (k < 0 ? -k : k)) * a->cd0);
		if (al < 0 || !a->n)
			continue;
		int id = a->pool[((a->cursor + k) % a->n + a->n) % a->n];
		float x = (float)(int)(W / 2 - (float)k * 90 * S + 90 * S * a->d74);
		draw_icon(id, x, iy, 0x40 * S, (unsigned)al << 24 | (k ? 0x808080 : 0xffffff));
		if (cleared(id))
			bgfx_draw_c(bg_get(BG_STAR), 0, 0, 0x3f, 0x3f, x + I(20), iy - I(26), 9 * S, 9 * S, 0,
			            (unsigned)al << 24 | 0xffffff, 1, false);
	}
	draw_ring(false, W / 2 + 90 * S * a->d74, iy, a->cd0, a->cc0, a->ring);
	/* command-mode panel FUN_0079f410 (bands taller than (int)(30s) for the legible font) */
	float cb = a->cb8;
	if (cb > 0) {
		pill_label(H / 2 - I(120), 0x9cffa5, tr_id(0x250), cb, W);
		for (int k = 0; k < 2; k++) {
			float y = H / 2 - I(60) + I(72 * k) + 4;
			bool sel = k == a->cmd;
			ui_rect(0, y - 14, W, y + 14, (unsigned)(int)(144 * cb) << 24 | (sel ? 0xffffff : 0));
			ui_hline(0, W, y - 14, a8(cb));
			ui_hline(0, W, y + 13, a8(cb));
			text_draw(FONT_MENUBIG, tr_id(0x10b + 2 * k), W / 2, y - 1, TEXT_CENTER, c2d(a8(cb) | (sel ? 0xffffff : 0xa4a4a4)),
			          1.0f, 0);
		}
	}
	letterbox();
	title();
}

static void arc_draw_bottom(void)
{
	arcsel_state *a = &g_arc;
	bottom_bg();
	if (a->leave)
		return;
	if (a->sub == ARC_COMMAND || (a->sub == ARC_CANCELLED && a->cb8 > 0)) {
		pill_label(12, 0x9cffa5, tr_id(0x250), 1, BW);
		for (int k = 0; k < 2; k++) {
			float y = 28 + 84 * (float)k;
			bool sel = k == a->cmd;
			if (sel)
				ui_rect(0, y, BW, y + 80, 0x46ffffff);
			text_draw(FONT_MENUBIG, tr_id(0x10b + 2 * k), 10, y + 14, TEXT_LEFT, c2d(sel ? WHITE : GREY), 1.0f, BW - 20);
			static char buf[400];
			text_wrap(FONT_MENUSUBTITLE, tr_id(0x10c + 2 * k), 1.0f, BW - 24, buf, sizeof buf);
			text_draw(FONT_MENUSUBTITLE, buf, 14, y + 36, TEXT_LEFT, c2d(sel ? WHITE : 0x99ffffff), 1.0f, 0);
		}
		return;
	}
	/* character grid (pool order, 5 columns) + stats panel */
	pill_label(12, 0x97cbff, g_setup.mode == MODE_STORY ? tr_id(0x8c) : tr_id(0x8b), 1, BW);
	for (int i = 0; i < a->n; i++) {
		float x = 4 + 40 * (float)(i % 5) + 20, y = 26 + 40 * (float)(i / 5) + 20;
		bool sel = i == a->cursor;
		if (sel)
			ui_rect(x - 20, y - 20, x + 20, y + 20, 0x80ffffff);
		draw_icon(a->pool[i], x, y, 18, sel ? WHITE : 0xffa0a0a0);
		if (cleared(a->pool[i]))
			bgfx_draw_c(bg_get(BG_STAR), 0, 0, 0x3f, 0x3f, x + 12, y - 13, 6, 6, 0, WHITE, 1, false);
	}
	int c = arcsel_char();
	stats_panel(c, 208, 30, 1.0f);
	const char *nk = text_id_key(0x1f8 + c - 3);
	text_draw(FONT_MENUBIG, nk ? tr(nk) : "", 104, 160, TEXT_CENTER, c2d(WHITE), 1.0f, 200);
	if (cleared(c))
		text_draw(FONT_SYSTEM, g_setup.mode == MODE_STORY ? "STORY CLEARED" : "CLEARED (no continue)", 104, 180,
		          TEXT_CENTER, c2d(0xffffe080), 0.8f, 200);
}

static const scene_def arc_scene = {
	.name = "arcselect", .init = arc_init, .update = arc_update, .draw_top = arc_draw_top,
	.draw_bottom = arc_draw_bottom, .exit = select_free,
};

/* ================================================================ VS Select */

static bgfx std_tex[2][3], card_tex[7], nz_tex[8];
static int std_key[2] = {-1, -1};

static void std_load(int p, int chara, int colopt)
{
	int key = chara * 4 + colopt;
	if (std_key[p] == key)
		return;
	std_key[p] = key;
	char folder[32], name[64];
	if (chara == SEL_RANDOM)
		snprintf(folder, sizeof folder, "charaRndm");
	else if (chara < 3)
		snprintf(folder, sizeof folder, "charaHide");
	else /* FUN_007a0240: palette of the option (Auto = palette A) */
		sel_palette_folder(folder, sizeof folder, chara, sel_color_resolve(chara, colopt));
	for (int f = 0; f < 3; f++) {
		snprintf(name, sizeof name, "std_%s_%02d", folder, f);
		bgfx_load(&std_tex[p][f], name, 0, 0, 0.5f);
	}
}

static const char *stage_code(int id)
{
	static const char *const code[20] = {[1] = "rd", [5] = "tr", [6] = "bf", [7] = "gr", [8] = "dr", [9] = "ri",
	                                     [10] = "se", [11] = "br", [12] = "ct", [13] = "iw", [14] = "ct", [15] = "ft",
	                                     [16] = "gz", [17] = "hl", [18] = "ml", [19] = "sh"};
	return id >= 0 && id < 20 && code[id] ? code[id] : "";
}

static void vs_init3ds(void)
{
	load_select_textures();
	bg_load(BG_STAGENAME);
	std_key[0] = std_key[1] = -1;
	for (int i = 0; i < 8; i++) {
		char n[16];
		snprintf(n, sizeof n, "stagenz%d", i);
		bgfx_load(&nz_tex[i], n, 0, 0, 0.5f);
	}
	vssel_init();
}

static void vs_update3ds(void)
{
	vssel_state *v = &g_vs;
	if (!v->stage_swapped) {
		stage3ds_menu_tick();
	} else {
		if (stage_current() != v->stage && v->stage > 0) { /* FUN_004bfca0(4) + FUN_004bfc10(stage) */
			stage3ds_end();
			stage3ds_begin(v->stage);
		}
		stage_g.gameMode = 0;
		stage_g.zoom = 1.0f;
		stage_update();
	}
	/* touch on the stage list (bottom) */
	if (v->sub == VS_STAGE && min_pressed_nr(MI_TOUCH)) {
		int ty = g_min.touch_y;
		if (ty >= 28 && ty < 28 + 5 * 26) {
			int k = (ty - 28) / 26 - 2;
			if (k == 0)
				g_min.st[MI_OK] = MIN_PRESSED;
			else {
				v->stage_cur = ((v->stage_cur + k) % v->nstages + v->nstages) % v->nstages;
				v->d7c = k > 0 ? 1.0f : -1.0f;
				audio_se(2, AUDIO_SE_CD_DEFAULT);
			}
		}
	}
	vssel_update();
	for (int p = 0; p < 2; p++) /* standing sprites follow the cursor / colour */
		if (v->slot[p] != SLOT_CPU || v->cc8[p] > 0)
			std_load(p, v->chara[p], v->color[p]);
}

/* ---------------- pieces shared with the local-wireless select (netsel.c; selgfx.h "selx_") */
static void stage_cards(void);
static void stage_name_band(void);
static void bgm_band(void);
static void vs_portrait(int p);
static void vs_wheel(int p);
static void vs_standing(int p);

void selx_textures(void) /* the VS Select's textures (vs_init3ds without the scene state) */
{
	load_select_textures();
	bg_load(BG_STAGENAME);
	std_key[0] = std_key[1] = -1;
	for (int i = 0; i < 8; i++) {
		char n[16];
		snprintf(n, sizeof n, "stagenz%d", i);
		bgfx_load(&nz_tex[i], n, 0, 0, 0.5f);
	}
}
void selx_free(void) { select_free(); }
void selx_backdrop(void)
{
	draw_bgp();
	stage3ds_draw();
	ui_blend_reset();
}
void selx_frame(void)
{
	letterbox();
	title();
}
void selx_stage_select(void)
{
	stage_cards();
	stage_name_band();
	bgm_band();
}
void selx_portrait(int p) { vs_portrait(p); }
void selx_wheel(int p) { vs_wheel(p); }
void selx_standing(int p) { vs_standing(p); }
void selx_std(int p, int chara, int colopt) { std_load(p, chara, colopt); }
void selx_bottom_bg(void) { bottom_bg(); }
void selx_pill(float y, unsigned strip, const char *label, float a, float w) { pill_label(y, strip, label, a, w); }
void selx_stats(int id, float x, float y, float a) { stats_panel(id, x, y, a); }
void selx_icon(int id, float cx, float cy, float half, unsigned argb) { draw_icon(id, cx, cy, half, argb); }
void selx_noise(int frame, float cx, float cy, unsigned argb) { draw_noise(frame, cx, cy, argb); }
void selx_name_plate(int id, float cx, float cy, float hw, float hh, unsigned argb)
{
	draw_name_plate(id, cx, cy, hw, hh, argb);
}
void selx_ring(bool p2, float cx, float cy, float a, int ring) { draw_ring(p2, cx, cy, a, 1.0f, ring); }
void selx_stage_name(int id, float cx, float cy, float k, unsigned argb) /* stage_name_band's art, any place / size */
{
	float sx0, sy0, sx1, sy1, hw;
	if (id == 1)
		sx0 = 0, sy0 = 0x40, sx1 = 0x1df, sy1 = 0x7f, hw = 0x78;
	else if (id < 16)
		sx0 = 0, sy0 = (float)(64 * id), sx1 = 0x25f, sy1 = sy0 + 63, hw = 0x98;
	else
		sx0 = 480, sy0 = (float)(64 * (id & 15)), sx1 = 979, sy1 = sy0 + 63, hw = 0x7d;
	bgfx_draw_c(bg_get(BG_STAGENAME), sx0, sy0, sx1, sy1, cx, cy, hw * S * k, 0x10 * S * k, 0, argb, 1, false);
}
void selx_arrows(float xl, float xr, float y, unsigned argb) /* FUN_007a6f90 option arrows */
{
	bgfx_draw_c(bg_get(BG_SELSYS), 0x280, 0x118, 0x2bb, 0x153, xl, y, 7, 7, 0, argb, 1, false);
	bgfx_draw_c(bg_get(BG_SELSYS), 0x334, 0x118, 0x36f, 0x153, xr, y, 7, 7, 0, argb, 1, false);
}

static void stage_cards(void) /* FUN_007a91f0, as a 2D projection (B.6) */
{
	vssel_state *v = &g_vs;
	float ca0 = v->ca0;
	if (ca0 <= 0)
		return;
	float R = (1 - ca0) * 200 + 400;
	static const int order[7] = {-3, 3, -2, 2, -1, 1, 0}; /* far ones first, the selected card on top */
	for (int j = 0; j < 7; j++) {
		int k = order[j];
		int id = v->stages[((v->stage_cur + k) % v->nstages + v->nstages) % v->nstages];
		float a = ((float)k + v->d7c) * 0.375f;
		float x = W / 2 + (-sinf(a) * R) * S, y = H / 2 - 52;
		float hw = 34 * fabsf(cosf(a)), hh = 34;
		char n[24];
		snprintf(n, sizeof n, "stage%s", stage_code(id));
		bgfx *t = &card_tex[k + 3];
		bool have = bgfx_exists(n) && bgfx_load(t, n, 0, 0, 0.5f);
		unsigned col = k == 0 ? a8(ca0) | 0xffffff : a8(ca0) | 0x808080;
		if (have)
			bgfx_draw_c(t, 0, 0, 0xff, 0xff, x, y, hw, hh, 0, col, 1, false);
		if (!have || k != 0)
			bgfx_draw_c(&nz_tex[(v->frame / 4) % 8], 0, 0, 0xff, 0xff, x, y, hw, hh, 0,
			            have ? a8(ca0 * 0.5f) | 0x808080 : a8(ca0) | 0xc0c0c0, 1, false);
		ui_hline(x - hw, x + hw, y - hh, a8(ca0) | (k ? 0x808080 : 0xffffff));
		ui_hline(x - hw, x + hw, y + hh - 1, a8(ca0) | (k ? 0x808080 : 0xffffff));
	}
}

static void stage_name_band(void) /* FUN_007aa670 */
{
	vssel_state *v = &g_vs;
	float ca0 = v->ca0;
	if (ca0 <= 0)
		return;
	float y = H / 2 - I(20), h = I(15);
	ui_rect(0, y - h, W, y + h, (unsigned)(int)(144 * ca0) << 24);
	ui_hline(0, W, y - h, a8(ca0));
	ui_hline(0, W, y + h - 1, a8(ca0));
	int id = v->stages[v->stage_cur];
	float sx0, sy0, sx1, sy1, hw;
	if (id == 1)
		sx0 = 0, sy0 = 0x40, sx1 = 0x1df, sy1 = 0x7f, hw = 0x78;
	else if (id < 16)
		sx0 = 0, sy0 = (float)(64 * id), sx1 = 0x25f, sy1 = sy0 + 63, hw = 0x98;
	else
		sx0 = 480, sy0 = (float)(64 * (id & 15)), sx1 = 979, sy1 = sy0 + 63, hw = 0x7d;
	bgfx_draw_c(bg_get(BG_STAGENAME), sx0, sy0, sx1, sy1, W / 2, y, hw * S * 1.5f, 0x10 * S * 1.5f, 0,
	            a8(ca0) | 0xffffff, 1, false);
}

static void bgm_band(void) /* FUN_007aabb0 */
{
	vssel_state *v = &g_vs;
	float ca0 = v->ca0;
	if (ca0 <= 0)
		return;
	float y = H / 2 + I(170), h = I(30);
	ui_rect(0, y - h, W, y + h, (unsigned)(int)(144 * ca0) << 24);
	bgfx_draw_c(bg_get(BG_SELSYS), 0, 0x154, 199, 0x17b, W / 2, y - I(12), 0x32 * S, 10 * S, 0, a8(ca0) | 0xc0c0c0, 1,
	            false);
	text_draw(FONT_SYSTEM, music_name(music_list_id(music_sel())), W / 2, y + I(12), TEXT_CENTER,
	          c2d(a8(ca0) | 0xffffff), 0.8f, W - 20);
}

static void vs_portrait(int p) /* FUN_007a6710 */
{
	vssel_state *v = &g_vs;
	float xp = (float)(int)(100 * S - 20 * S * v->d80);
	float x = p ? W - xp : xp;
	draw_noise(v->frame, x, H / 2, a8(v->ca8) | 0x909090);
	int c = v->chara[p] < 1 ? 1 : v->chara[p];
	float sx, sy;
	bg_id t;
	portrait_rect(c, &sx, &sy, &t);
	unsigned col;
	int al = (int)(255 * v->ca8);
	if (v->d80 > 0)
		col = (unsigned)al << 24 | 0xffffff;
	else if (v->slot[p] < SLOT_OPTIONS)
		col = (unsigned)(al / 2) << 24;
	else
		col = (unsigned)(al / 2) << 24 | 0xc0c0c0;
	bgfx_draw_c(bg_get(t), sx, sy, sx + 319, sy + 1023, x, H / 2, 80 * S, 256 * S, 0, col, 1, p == 0);
	if (v->ce0[p] > 0) {
		bgfx_draw_c(bg_get(BG_CHARA00), 0x500, 0, 0x63f, 0x3ff, x, H / 2, 80 * S, 256 * S, 0,
		            (unsigned)((int)(255 * v->ce0[p]) / 2) << 24 | 0xc8e1ff, 2, p == 0);
		ui_blend(1);
	}
}

static void vs_wheel(int p) /* FUN_007a3600 */
{
	vssel_state *v = &g_vs;
	float cd0 = v->cd0[p];
	if (cd0 <= 0 && v->cc0[p] <= 0)
		return;
	float R = ((1 - cd0 * cd0) * 180 + 180) * S;
	float x0 = p ? W / 2 + 400 * S : W / 2 - 400 * S;
	float sel_x = 0, sel_y = 0;
	for (int k = -4; k <= 4; k++) {
		float a = ((float)k / 6.0f) * PI + v->d74[p] * PI / 6.0f;
		float x = p ? x0 - cosf(a) * R : x0 + cosf(a) * R, y = H / 2 + sinf(a) * R;
		int id = v->pool[((v->cursor[p] + k) % v->n + v->n) % v->n];
		draw_icon(id, x, y, 0x40 * S, a8(cd0) | (k ? 0x808080 : 0xffffff));
		if (!k)
			sel_x = x, sel_y = y;
	}
	draw_ring(p == 1, sel_x, sel_y, cd0, v->cc0[p], v->ring[p]);
	/* name base + name */
	float nx = W / 2 + (p ? 1 : -1) * I(220), ny = H / 2 + I(200);
	float a = cd0 * v->cc0[p];
	bgfx_draw_c(bg_get(BG_SELSYS), 0x200, 0x154, 0x3ff, 0x193, nx, ny, 0x80 * S, 0x10 * S, 0,
	            (unsigned)((int)(a * 255) / 2) << 24 | 0xffffff, 1, false);
	bgfx_draw_c(bg_get(BG_SELSYS), 0x200, 0x194, 0x3ff, 0x1d3, nx, ny, 0x80 * S, 0x10 * S, 0, a8(a) | 0xffffff, 2, false);
	ui_blend(1);
	draw_name_plate(v->chara[p] < 1 ? 1 : v->chara[p], nx, ny, 0x50 * S, 0x14 * S, a8(a) | 0xffffff);
}

static void vs_standing(int p) /* FUN_007a9910 */
{
	vssel_state *v = &g_vs;
	float cc8 = v->cc8[p];
	if (cc8 <= 0)
		return;
	int f = (v->frame / 8) % 4;
	if (f == 3)
		f = 1;
	float x = W / 2 + (p ? 1 : -1) * (20 * v->d80 + 160) * S;
	float y = H / 2 + (100 - (1 - cc8) * 64) * S;
	float hw = (float)(int)(128 * cc8 * S), hh = (float)(int)((2 - cc8) * 128 * S);
	bgfx_draw_c(bg_get(BG_SELSYS), 0x28a, 0xf0, 0x33d, 0x117, x, y + 64 * S, (float)(int)(40 * cc8 * S),
	            (float)(int)(10 * cc8 * S), 0, a8(cc8 * 0.8f) | 0xffffff, 1, false);
	int v8 = (int)(255 * cc8);
	bgfx_draw_c(&std_tex[p][f], 0, 0, 0xff, 0xff, x, y, hw, hh, 0, 0xff000000u | (unsigned)v8 * 0x010101u, 1, p == 0);
}

static void vs_draw_top(void) /* FUN_0079d420 */
{
	vssel_state *v = &g_vs;
	if (v->leave) {
		sel_draw_loading();
		return;
	}
	draw_bgp();
	stage3ds_draw();
	ui_blend_reset();
	stage_cards();
	if (v->ca4 > 0)
		ui_rect(0, 0, W, H, a8(v->ca4) | 0xffffff);
	stage_name_band();
	bgm_band();
	for (int p = 0; p < 2; p++)
		vs_portrait(p);
	for (int p = 0; p < 2; p++)
		vs_wheel(p);
	for (int p = 0; p < 2; p++)
		vs_standing(p);
	letterbox();
	/* PLAYER1 / PLAYER2 / CPU labels FUN_007a9c90 (H/2-235s, just under the PC's letterbox). Port: the
	 * wheels hug the screen edges at 400x240, so the labels go into the top letterbox bar beside the
	 * title (dark tints on the white bar) instead of over the icons. */
	for (int p = 0; p < 2; p++) {
		int id = g_setup.input[p] == INPUT_CPU ? 0x1d0 : 0x1ce + p;
		text_draw(FONT_SYSTEM, tr_id(id), p ? W - 6 : 6, (H / 2 - I(250)) / 2, p ? TEXT_RIGHT : TEXT_LEFT,
		          c2d(a8(v->ca8) | (p ? 0xc02828 : 0x2850c0)), 0.8f, 88);
	}
	title();
}

static const char *opt_value(int p, int row, char *buf, int n)
{
	vssel_state *v = &g_vs;
	static const char *const hcp[6] = {"50%", "75%", "100%", "150%", "200%", "300%"}; /* sel_system labels */
	switch (row) {
	case ROW_HANDICAP: return hcp[v->handicap[p] % 6];
	case ROW_COLOR:
		if (v->color[p] == 3)
			return tr_id(sel_sp_text(v->chara[p]));
		return tr_id(0x1df + v->color[p]);
	case ROW_INPUT: return tr_id(0x1f3 + v->command[p]);
	default: snprintf(buf, (size_t)n, "%s", tr_id(0x1f5)); return buf;
	}
}

static void vs_draw_bottom(void)
{
	vssel_state *v = &g_vs;
	bottom_bg();
	if (v->leave)
		return;
	if (v->sub == VS_CHARS) {
		for (int p = 0; p < 2; p++) {
			float x0 = 160 * (float)p;
			bool active = v->ctrl[0] == p;
			ui_rect(x0, 0, x0 + 160, 22, p ? 0xc0802020 : 0xc0204080);
			int lid = g_setup.input[p] == INPUT_CPU ? 0x1d0 : 0x1ce + p;
			text_draw(FONT_SYSTEM, tr_id(lid), x0 + 6, 11, TEXT_LEFT, c2d(WHITE), 0.8f, 0);
			if (v->slot[p] == SLOT_READY)
				text_draw(FONT_SYSTEM, "OK", x0 + 154, 11, TEXT_RIGHT, c2d(0xff80ff80), 0.8f, 0);
			if (v->slot[p] == SLOT_CPU && !active) {
				text_draw(FONT_SYSTEM, "-", x0 + 80, 60, TEXT_CENTER, c2d(GREY), 1.0f, 0);
				continue;
			}
			int c = v->chara[p];
			const char *nk = c == SEL_RANDOM ? NULL : text_id_key(0x1f8 + c - 3);
			text_draw(FONT_MENU, nk ? tr(nk) : "????", x0 + 80, 36, TEXT_CENTER,
			          c2d(v->slot[p] == SLOT_CHOOSE ? WHITE : 0xffd0d0d0), 1.0f, 150);
			if (c >= 3) {
				sel_stats st = sel_char_stats(c);
				char b[48];
				snprintf(b, sizeof b, "%s %s", tr_id(0x1ea), tr_id(0x1eb + st.diff));
				text_draw(FONT_SYSTEM, b, x0 + 80, 54, TEXT_CENTER, c2d(GREY), 0.8f, 150);
			}
			bool opts = v->slot[p] == SLOT_OPTIONS;
			for (int row = 1; row <= 4; row++) {
				float y = 74 + 30 * (float)(row - 1);
				bool sel = opts && v->row[p] == row;
				if (sel)
					ui_rect(x0 + 2, y - 2, x0 + 158, y + 26, 0x55ffffff);
				unsigned tc = sel || v->slot[p] == SLOT_READY ? WHITE : 0xff808080;
				if (row < 4) {
					static const int lab[4] = {0, 0x1d7, 0x1de, 0x1f2};
					text_draw(FONT_SYSTEM, tr_id(lab[row]), x0 + 8, y + 5, TEXT_LEFT, c2d(tc), 0.8f, 0);
				}
				char buf[32];
				bool dis = (row == 1 && !v->en_hcp[p]) || (row == 2 && !v->en_col[p]) || (row == 3 && !v->en_cmd[p]);
				unsigned vc = dis ? 0xffff4040 : tc;
				text_draw(FONT_MENU, opt_value(p, row, buf, sizeof buf), x0 + 80, y + (row < 4 ? 17 : 11), TEXT_CENTER,
				          c2d(vc), row < 4 ? 0.8f : 1.0f, 130);
				if (sel && row < 4) {
					/* FUN_007a6f90 arrows: sel_system (0x280,0x118,0x2bb,0x153) / (0x334,0x118,0x36f,0x153) */
					bgfx_draw_c(bg_get(BG_SELSYS), 0x280, 0x118, 0x2bb, 0x153, x0 + 12, y + 17, 7, 7, 0, WHITE, 1, false);
					bgfx_draw_c(bg_get(BG_SELSYS), 0x334, 0x118, 0x36f, 0x153, x0 + 148, y + 17, 7, 7, 0, WHITE, 1, false);
				}
			}
		}
		return;
	}
	/* stage + BGM */
	pill_label(12, 0x97cbff, "STAGE", 1, BW);
	for (int k = -2; k <= 2; k++) {
		int id = v->stages[((v->stage_cur + k) % v->nstages + v->nstages) % v->nstages];
		float y = 28 + 26 * (float)(k + 2) + 13;
		if (!k)
			ui_rect(0, y - 12, BW, y + 12, 0x60ffffff);
		text_draw(FONT_MENU, stage_name(id), BW / 2, y, TEXT_CENTER, c2d(k ? 0xff909090 : WHITE), k ? 0.85f : 1.0f, BW - 40);
	}
	pill_label(170, 0x9cffa5, "BGM", 1, BW);
	text_draw(FONT_MENU, music_name(music_list_id(music_sel())), BW / 2, 190, TEXT_CENTER, c2d(WHITE), 0.9f, BW - 20);
}

static const scene_def vs_scene = {
	.name = "vsselect", .init = vs_init3ds, .update = vs_update3ds, .draw_top = vs_draw_top,
	.draw_bottom = vs_draw_bottom, .exit = select_free,
};

/* ================================================================ pause overlay (Battle scene) */


void pause_draw_top(void)
{
	pause_state *p = &g_pause;
	if (!p->on)
		return;
	ui_blend_reset();
	/* the HUD writes depth 0.9 (hud_c2d.c): draw the overlay above it, or the dim, bands and
	 * texts fail the depth test wherever the HUD / effects were drawn */
	ui_set_depth(0.95f);
	float a = p->a;
	if (a > 0) {
		ui_rect(0, 0, W, H, (unsigned)(int)(128 * a) << 24);
		float yh = H / 2 - I(110);
		pill_label(yh, 0x97cbff, tr_id(0x257), a, W);
		static const int ids[4] = {0x10f, 0x110, 0x111, 0};
		for (int k = 0; k < 4; k++) {
			float y = yh + 34 + 28 * (float)k;
			bool sel = k == p->cur;
			ui_rect(0, y - 11, W, y + 11, (unsigned)(int)(144 * p->a_row) << 24 | (sel ? 0xffffff : 0x404040));
			const char *s = ids[k] ? tr_id(ids[k]) : tr_id(0x1f7); /* Quit: "EXIT" */
			text_draw(FONT_MENUBIG, s, W / 2, y, TEXT_CENTER, c2d(a8(a) | (sel ? 0xffffff : 0xa4a4a4)), 1.0f, 0);
		}
	}
	if (p->st == 900 && !p->exiting && p->a == 0 && p->count > 0) {
		/* resume countdown 3, 2, 1 */
		char d[4];
		snprintf(d, sizeof d, "%d", p->count > 0x3c ? 3 : p->count > 0x1e ? 2 : 1);
		text_draw(FONT_MENUBIG, d, W / 2, H / 2, TEXT_CENTER, c2d(WHITE), 2.0f, 0);
	}
	ui_set_depth(UI_DEPTH_DEFAULT);
}

/* ---- command list (bottom screen; the PC draws it over the paused match, FUN_006aed80 + FUN_00591620).
 * Port layout: the touch hyper panel is replaced by the list while it is open: the panel backdrop (menu
 * gradient + the player's art), category bands, one card per move with the 3DS input drawn as button glyphs
 * (A B X Y L, the touch hyper buttons as a small map of the panel with the move's button lit) and the move
 * name in the menu font. Up/Down scroll (pause_state.scroll); the announce band (0x66) keeps the bottom rows. */

static kv_table *cmdlist, *cmdcats, *cmdinstr;

#define CL_TOP 30.0f
#define CL_BOTTOM 194.0f
#define CL_ROW 19.0f
#define CL_HEAD 16.0f
#define CL_NOTE 13.0f

static const unsigned cat_col[4] = {0x3a8cff, 0x2fbf71, 0xb05cff, 0xff4050};
static const char *const cat_key[4] = {"COMMANDLIST_CATEGORY_REGULARWEAPON", "COMMANDLIST_CATEGORY_SPECIALWEAPON",
                                       "COMMANDLIST_CATEGORY_HYPERATTACK", "COMMANDLIST_CATEGORY_ACCELHYPER"};

static const char *cl_get(kv_table *t, const char *key, const char *def)
{
	static char buf[4][128]; /* first line only: a file's last value is followed by "[EOF]" */
	static int n;
	const char *v = t ? kv_get(t, key) : NULL;
	char *b = buf[n++ & 3];
	snprintf(b, sizeof buf[0], "%s", v ? v : def);
	b[strcspn(b, "\n")] = 0;
	return b;
}

static void cl_load(void)
{
	static const char *const files[3] = {"commandlist_names", "commandlist_categories", "commandlist_instructions"};
	kv_table **dst[3] = {&cmdlist, &cmdcats, &cmdinstr};
	for (int i = 0; i < 3; i++)
		if (!*dst[i]) {
			char path[96];
			snprintf(path, sizeof path, "romfs:/script/%s/%s.txt", lang_dir(strings_language()), files[i]);
			*dst[i] = kv_load(path);
		}
}

static void cl_free(void)
{
	kv_free(cmdlist);
	kv_free(cmdcats);
	kv_free(cmdinstr);
	cmdlist = cmdcats = cmdinstr = NULL;
}

/* 3DS face button: coloured disc (New 3DS colours), light rim, letter */
static float glyph_face(float x, float cy, char letter, float a)
{
	unsigned col = letter == 'A' ? 0xe0403c : letter == 'B' ? 0xf0c020 : letter == 'X' ? 0x3a7cf0 : 0x30b060;
	float r = 7.5f, cx = x + r;
	C2D_DrawCircleSolid(cx, cy + 1, UI_DEPTH, r + 0.5f, c2d(a8(a * 0.6f)));
	C2D_DrawCircleSolid(cx, cy, UI_DEPTH, r, c2d(a8(a) | 0xf4f4f8));
	C2D_DrawCircleSolid(cx, cy, UI_DEPTH, r - 1.5f, c2d(a8(a) | col));
	C2D_DrawCircleSolid(cx - 1.5f, cy - 2, UI_DEPTH, r - 4.0f, c2d(a8(a * 0.25f) | 0xffffff));
	char s[2] = {letter, 0};
	text_draw(FONT_SYSTEM, s, cx + 0.5f, cy + 1, TEXT_CENTER, c2d(a8(a * 0.8f)), 0.8f, 0);
	text_draw(FONT_SYSTEM, s, cx, cy, TEXT_CENTER, c2d(a8(a) | 0xffffff), 0.8f, 0);
	return 2 * r;
}

/* shoulder button: grey slanted pill */
static float glyph_shoulder(float x, float cy, char letter, float a)
{
	float w = 18, h = 12;
	panel_chamfer(x, cy - h / 2 + 1, x + w, cy + h / 2 + 1, 3, a8(a * 0.6f), a8(a * 0.6f));
	panel_chamfer(x, cy - h / 2, x + w, cy + h / 2, 3, a8(a) | 0xe8eaf0, a8(a) | 0x8a90a0);
	panel_chamfer(x + 1.5f, cy - h / 2 + 1.5f, x + w - 1.5f, cy + h / 2 - 1.5f, 2, a8(a) | 0x6a7080, a8(a) | 0x3a4050);
	char s[2] = {letter, 0};
	text_draw(FONT_SYSTEM, s, x + w / 2, cy, TEXT_CENTER, c2d(a8(a) | 0xffffff), 0.75f, 0);
	return w;
}

/* touch hyper button: a small map of the bottom panel with the move's button lit */
static float glyph_touch(float x, float cy, int slot, float a)
{
	const float w = 26, h = 18, sx = w / 320.0f, sy = h / 240.0f, y0 = cy - h / 2;
	panel_chamfer(x - 1, y0 - 1, x + w + 1, y0 + h + 1, 3, a8(a) | 0xc8d0e0, a8(a) | 0x707890);
	ui_rect(x, y0, x + w, y0 + h, a8(a) | 0x0a1024);
	for (int k = 0; k < HT_NSLOT; k++) {
		float bx0, by0, bx1, by1;
		panel_slot_rect(k, &bx0, &by0, &bx1, &by1);
		unsigned c = k == slot ? panel_slot_color(k) : 0x3a4258;
		ui_rect(x + bx0 * sx + 0.5f, y0 + by0 * sy + 0.5f, x + bx1 * sx - 0.5f, y0 + by1 * sy - 0.5f, a8(a) | c);
	}
	return w;
}

static float glyph_text(float x, float cy, const char *s, unsigned rgb, float a)
{
	text_draw(FONT_SYSTEM, s, x, cy, TEXT_LEFT, c2d(a8(a) | rgb), 0.75f, 0);
	return text_width(FONT_SYSTEM, s, 0.75f);
}

static float glyph_hold(float x, float cy, float a)
{
	const char *s = cl_get(cmdinstr, "COMMANDLISTINSTRUCTION_HOLD", "HOLD");
	float w = text_width(FONT_SYSTEM, s, 0.62f) + 8;
	panel_chamfer(x, cy - 6, x + w, cy + 6, 3, a8(a) | 0xffd84a, a8(a) | 0xc08a10);
	panel_chamfer(x + 1, cy - 5, x + w - 1, cy + 5, 2, a8(a) | 0x3a2a08, a8(a) | 0x201404);
	text_draw(FONT_SYSTEM, s, x + w / 2, cy, TEXT_CENTER, c2d(a8(a) | 0xffe27a), 0.62f, 0);
	return w;
}

static int tok_slot(const ml_entry *e)
{
	bool s = false, b = false, d = false;
	for (int i = 0; i < e->ntok; i++) {
		s |= e->tok[i] == T_S;
		b |= e->tok[i] == T_B;
		d |= e->tok[i] == T_D;
	}
	return s && b ? HT_CENTER : s ? HT_UP : b ? HT_RIGHT : d ? HT_DOWN : HT_LEFT;
}

/* The 3DS input of a move (user mapping): S+A = X, S+B = Y, D = L held, hypers = touch buttons. Returns the end
 * x; *tag gets a range note (CLOSE / LONG) for the right side of the card. */
static float draw_input(const ml_entry *e, float x, float cy, float a, const char **tag)
{
	bool hA = false, hB = false, hS = false, hD = false, hold = false, dashing = false;
	*tag = NULL;
	for (int i = 0; i < e->ntok; i++)
		switch (e->tok[i]) {
		case T_A: hA = true; break;
		case T_B: hB = true; break;
		case T_S: hS = true; break;
		case T_D: hD = true; break;
		case T_HOLD: hold = true; break;
		case T_DASHING: dashing = true; break;
		case T_CLOSE: *tag = cl_get(cmdinstr, "COMMANDLISTINSTRUCTION_CLOSERANGE", "(CLOSE RANGE)"); break;
		case T_LONG: *tag = cl_get(cmdinstr, "COMMANDLISTINSTRUCTION_LONGRANGE", "(LONG RANGE)"); break;
		default: break;
		}
	if (dashing) { /* "also while dashing": the L is optional */
		x += glyph_text(x, cy, "(", 0xa0a8c0, a);
		x += glyph_shoulder(x + 1, cy, 'L', a * 0.75f) + 1;
		x += glyph_text(x + 1, cy, ")", 0xa0a8c0, a) + 3;
	}
	if (hD && e->cat < ML_HYPER) { /* the D+H touch button holds L itself */
		x += glyph_shoulder(x, cy, 'L', a) + 2;
		ui_rect(x, cy - 0.75f, x + 7, cy + 0.75f, a8(a) | 0xffffff); /* "+" */
		ui_rect(x + 2.75f, cy - 3.5f, x + 4.25f, cy + 3.5f, a8(a) | 0xffffff);
		x += 9;
	}
	if (e->cat >= ML_HYPER) {
		x += glyph_touch(x, cy, tok_slot(e), a);
	} else {
		char btn = hS && hA ? 'X' : hS && hB ? 'Y' : hS ? 'X' : hA ? 'A' : 'B';
		x += glyph_face(x, cy, btn, a);
	}
	if (hold)
		x += 3 + glyph_hold(x + 3, cy, a);
	return x;
}

/* width draw_input will take (same rules, no drawing): the name column starts after the widest input */
static float input_width(const ml_entry *e)
{
	bool hD = false, hold = false, dashing = false;
	for (int i = 0; i < e->ntok; i++) {
		hD |= e->tok[i] == T_D;
		hold |= e->tok[i] == T_HOLD;
		dashing |= e->tok[i] == T_DASHING;
	}
	float w = e->cat >= ML_HYPER ? 26 : 15;
	if (dashing)
		w += text_width(FONT_SYSTEM, "()", 0.75f) + 1 + 18 + 1 + 1 + 3;
	if (hD && e->cat < ML_HYPER)
		w += 18 + 2 + 9;
	if (hold)
		w += 3 + text_width(FONT_SYSTEM, cl_get(cmdinstr, "COMMANDLISTINSTRUCTION_HOLD", "HOLD"), 0.62f) + 8;
	return w;
}
static float cl_name_x; /* name column of the open list */

enum { CI_HEAD, CI_MOVE, CI_NOTE };
typedef struct {
	uint8_t kind, cat;
	const ml_entry *e;
	const char *note;
} cl_item;

static float item_h(const cl_item *it) { return it->kind == CI_HEAD ? CL_HEAD : it->kind == CI_MOVE ? CL_ROW : CL_NOTE; }

static int cl_items(int chara, cl_item *out, int max)
{
	const ml_entry *e;
	int n = movelist_get(chara, &e), k = 0, cat = -1;
	for (int i = 0; i < n && k < max - 1; i++) {
		if (e[i].cat != cat) {
			cat = e[i].cat;
			out[k++] = (cl_item){CI_HEAD, (uint8_t)cat, NULL, NULL};
		}
		out[k++] = (cl_item){CI_MOVE, e[i].cat, &e[i], NULL};
	}
	static const char *const notes[3] = {"NOTE", "NOTE1", "NOTE2"};
	for (int i = 0; i < 3 && k < max; i++) {
		char key[64];
		snprintf(key, sizeof key, "COMMANDLIST_%s_%s", sel_char_key(chara), notes[i]);
		const char *v = cmdlist ? kv_get(cmdlist, key) : NULL;
		if (v)
			out[k++] = (cl_item){CI_NOTE, 0, NULL, v};
	}
	return k;
}

static void draw_item(const cl_item *it, float y, float a)
{
	const float x0 = 6, x1 = BW - 12;
	if (it->kind == CI_HEAD) {
		unsigned c = cat_col[it->cat & 3];
		float cy = y + CL_HEAD / 2 + 1;
		ui_rect_grad(x0, y + 3, x1, y + CL_HEAD - 1, a8(a * 0.9f) | c, a8(0) | c, a8(0) | c, a8(a * 0.9f) | c);
		ui_rect(x0, y + 3, x0 + 3, y + CL_HEAD - 1, a8(a) | 0xffffff);
		text_draw_shadowed(FONT_MENU, cl_get(cmdcats, cat_key[it->cat & 3], ""), x0 + 9, cy, TEXT_LEFT,
		                   c2d(a8(a) | 0xffffff), 0.62f, 0);
		return;
	}
	if (it->kind == CI_NOTE) {
		char note[128];
		snprintf(note, sizeof note, "%s", it->note);
		note[strcspn(note, "\n")] = 0;
		text_draw(FONT_SYSTEM, note, x0 + 4, y + CL_NOTE / 2, TEXT_LEFT, c2d(a8(a) | 0xffe27a), 0.7f, x1 - x0 - 8);
		return;
	}
	const ml_entry *e = it->e;
	float cy = y + CL_ROW / 2;
	unsigned acc = e->cat >= ML_HYPER ? panel_slot_color(tok_slot(e)) : cat_col[e->cat & 3];
	panel_chamfer(x0, y + 1, x1, y + CL_ROW - 1, 5, a8(a * 0.82f) | 0x1c2a52, a8(a * 0.82f) | 0x0c1430);
	ui_rect(x0 + 4, y + 2, x1 - 4, y + 3, a8(a * 0.12f) | 0xffffff);
	ui_rect(x0, y + 4, x0 + 3, y + CL_ROW - 4, a8(a) | acc);
	const char *tag;
	draw_input(e, x0 + 10, cy, a, &tag);
	float nx = cl_name_x, nmax = x1 - nx - 4;
	if (tag) {
		float tw = text_width(FONT_SYSTEM, tag, 0.65f);
		text_draw(FONT_SYSTEM, tag, x1 - 5, cy, TEXT_RIGHT, c2d(a8(a * 0.85f) | 0xa8c8ff), 0.65f, 0);
		nmax -= tw + 6;
	}
	if (e->star)
		text_draw(FONT_MENU, e->star == 2 ? "**" : "*", nx - 2, cy, TEXT_RIGHT, c2d(a8(a) | 0xffe27a), 0.72f, 0);
	text_draw_shadowed(FONT_MENU, cl_get(cmdlist, e->key, ""), nx, cy, TEXT_LEFT, c2d(a8(a) | 0xffffff), 0.72f, nmax);
}

void pause_draw_bottom(int chara)
{
	pause_state *p = &g_pause;
	if (!p->on) {
		cl_free();
		return;
	}
	ui_blend_reset();
	/* paused: the hyper buttons are inactive */
	if (p->a > 0)
		ui_rect(0, 0, BW, 240, a8(p->a * 0.45f));
	if (p->a_cmd <= 0)
		return;
	float a = p->a_cmd;
	cl_load();
	panel_backdrop(a); /* the buttons go away under the list */
	/* header: MOVE LIST pill + the character's name */
	pill_label(12, 0x97cbff, tr_id(0x110), a, BW);
	char key[48];
	snprintf(key, sizeof key, "CHARACTER_%s", sel_char_key(chara));
	text_draw_shadowed(FONT_MENUBIG, tr(key), BW - 8, 13, TEXT_RIGHT, c2d(a8(a) | 0xffffff), 0.9f, 150);
	cl_item items[40];
	int n = cl_items(chara, items, 40);
	float iw = 0;
	for (int i = 0; i < n; i++)
		if (items[i].kind == CI_MOVE && input_width(items[i].e) > iw)
			iw = input_width(items[i].e);
	cl_name_x = 6 + 10 + iw + 12;
	/* scroll limit: the first item from which the rest fits the viewport */
	int smax = n;
	float acc = 0;
	while (smax > 0 && acc + item_h(&items[smax - 1]) <= CL_BOTTOM - CL_TOP)
		acc += item_h(&items[--smax]);
	p->scroll_max = smax;
	if (p->scroll > smax)
		p->scroll = smax;
	float y = CL_TOP;
	for (int i = p->scroll; i < n && y + item_h(&items[i]) <= CL_BOTTOM + 0.5f; i++) {
		draw_item(&items[i], y, a);
		y += item_h(&items[i]);
	}
	/* scroll bar + arrows */
	if (smax > 0) {
		float tx = BW - 7, track = CL_BOTTOM - CL_TOP - 16;
		ui_rect(tx, CL_TOP + 8, tx + 3, CL_BOTTOM - 8, a8(a * 0.5f) | 0x0a1024);
		float th = track * (float)(n - smax) / (float)n, ty = CL_TOP + 8 + (track - th) * (float)p->scroll / (float)smax;
		ui_rect(tx, ty, tx + 3, ty + th, a8(a) | 0x97cbff);
		u32 up = c2d(a8(p->scroll > 0 ? a : a * 0.25f) | 0xffffff), dn = c2d(a8(p->scroll < smax ? a : a * 0.25f) | 0xffffff);
		C2D_DrawTriangle(tx + 1.5f, CL_TOP, up, tx - 2, CL_TOP + 5, up, tx + 5, CL_TOP + 5, up, UI_DEPTH);
		C2D_DrawTriangle(tx + 1.5f, CL_BOTTOM, dn, tx - 2, CL_BOTTOM - 5, dn, tx + 5, CL_BOTTOM - 5, dn, UI_DEPTH);
	}
}

/* ================================================================ registration */

void result_scene_register(void);

void select_scenes_register(void)
{
	scene_register(SCENE_ARC_SELECT, &arc_scene);
	scene_register(SCENE_VS_SELECT, &vs_scene);
	result_scene_register();
}
