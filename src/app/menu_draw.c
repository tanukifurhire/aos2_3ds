/* Logo / Menu / "coming soon" scenes: drawing and the 3DS side (stage 0x13
 * background, textures) — modes.md §A.3-A.7. Top screen = the original
 * formulas at s = 0.4 with larger text (A.7); bottom screen = item list,
 * sub-menu subtitles, the config panel and the help line. */
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../input/aos_input.h"
#include "../input/hyper_touch.h"
#include "../platform/panel.h"
#include "../platform/stage.h"
#include "audio.h"
#include "btnglyph.h"
#include "controls.h"
#include "conv_scene.h"
#include "menu.h"
#include "modes_scenes.h"
#include "music.h"
#include "scene.h"
#include "scene_core.h"
#include "select.h"
#include "text.h"
#include "ui.h"

#define W 400.0f
#define H 240.0f
#define BW 320.0f
#define S 0.4f /* s = min(W/800, H/600) */
#define WHITE 0xffffffffu
#define GREY 0xffa4a4a4u

static unsigned with_a(unsigned argb, float a) { return ui_argb((int)((float)(argb >> 24) * a), argb); }
static u32 c2d_a(unsigned argb, float a) { return ui_c2d(with_a(argb, a)); }

/* ================================================================ menu background (stage 0x13) */

#define MENU_STAGE 0x13 /* Menu Init FUN_004bfc10(0x13), modes.md §0.5 */

static void bg_begin(void)
{
	if (stage_current() != MENU_STAGE)
		stage3ds_begin(MENU_STAGE);
}

static void bg_tick(void) /* FUN_004bfd30 with the menu's game mode */
{
	stage_g.gameMode = 0;
	stage_g.zoom = 1.0f;
	stage_update();
}

/* ================================================================ header pill (A.2.3) */

typedef struct {
	float bx, by0, by1;          /* 1-px body column */
	float cx0, cy0, cx1, cy1;    /* end cap */
	unsigned strip;              /* strip RGB */
} pill_style;

static const pill_style PILL_BLUE = {0x2df, 8, 0x27, 0x2e0, 8, 0x2f7, 0x27, 0x97cbff};
static const pill_style PILL_YELLOW = {0x32d, 0x38, 0x57, 0x32e, 0x38, 0x345, 0x57, 0xffed97};
static const pill_style PILL_GREEN = {0x2ef, 0x68, 0x87, 0x2f0, 0x68, 0x307, 0x87, 0x9cffa5};
static const pill_style PILL_ORANGE = {0x2ef, 0x98, 0xb7, 0x2f0, 0x98, 0x307, 0xb7, 0xffd09c};

static void pill(const pill_style *p, float yh, float a, const char *label, float sw)
{
	if (a <= 0)
		return;
	ui_rect(0, yh - 6, sw, yh + 6, ui_argb((int)(128 * a), p->strip));
	float tw = text_width(FONT_MENULABEL, label, 1.0f);
	float x1 = tw + 18; /* textW + (int)(44 s) */
	ui_mtex_draw(MT_PILLS, p->bx, p->by0, p->bx + 1, p->by1, 0, yh - 8, x1, yh + 8, with_a(WHITE, a));
	ui_mtex_draw(MT_PILLS, p->cx0, p->cy0, p->cx1, p->cy1, x1, yh - 8, x1 + 12, yh + 8, with_a(WHITE, a));
	text_draw(FONT_MENULABEL, label, 8, yh, TEXT_LEFT, c2d_a(WHITE, a), 1.0f, 0);
}

/* row band (A.2.3): white (selected) / black at 144a, 1-px black edges */
static void row_band(float y, float half, bool sel, float a, float sw)
{
	ui_rect(0, y - half, sw, y + half, ui_argb((int)(144 * a), sel ? 0xffffff : 0));
	ui_hline(0, sw, y - half, ui_argb((int)(255 * a), 0));
	ui_hline(0, sw, y + half - 1, ui_argb((int)(255 * a), 0));
}

/* ================================================================ logo scene */

static void logo_init(void)
{
	ui_mtex_load(MT_DAIDAI, 0);
	ui_mtex_load(MT_SHINDEN, 0);
	logo_logic_init();
}

static void logo_draw_top(void) /* FUN_00632340 */
{
	int st = g_logo.state;
	ui_mtex_id t = (st == 10 || st == 0xf) ? MT_DAIDAI : (st == 0x14 || st == 0x19) ? MT_SHINDEN : MT_COUNT;
	if (t == MT_COUNT)
		return;
	int half = (int)(300 * S); /* 120 */
	ui_mtex_draw(t, 0, 0, 0x7ff, 0x7ff, W / 2 - half, H / 2 - half, W / 2 + half, H / 2 + half, WHITE);
}

static void logo_exit(void)
{
	ui_mtex_free(MT_DAIDAI);
	ui_mtex_free(MT_SHINDEN);
}

static const scene_def logo_scene = {
	.name = "logo", .init = logo_init, .update = logo_logic_update, .draw_top = logo_draw_top, .exit = logo_exit,
	.clear_top = 0xffffffff, .clear_bottom = 0xffffffff, /* clear white (C2D_Color32 order: all 0xff) */
};

/* ================================================================ menu scene */

static int ctl_hit(int x, int y) { return panel_hit_pos(x, y); }

static void menu_init(void)
{
	controls_hit = ctl_hit;
	bg_begin();
	ui_mtex_load(MT_LOGO, 0);
	ui_mtex_load(MT_PILLS, 0);
	ui_mtex_load(MT_ARROWS, 0);
	menu_logic_init();
}

static void menu_update(void)
{
	/* bottom-screen touch on the main-menu list (port addition) */
	if (g_menu.state == MS_MAIN && min_pressed_nr(MI_TOUCH)) {
		int i = (g_min.touch_y - 30) / 18;
		if (g_min.touch_y >= 30 && i >= 0 && i < ITEM_COUNT) {
			if (i == g_menu.cursor) {
				g_min.st[MI_OK] = MIN_PRESSED; /* tap the selected item = confirm */
			} else {
				g_menu.kick = i > g_menu.cursor ? 1.0f : -1.0f;
				g_menu.cursor = i;
				audio_se(2, AUDIO_SE_CD_DEFAULT);
				announce_show(i);
			}
		}
	}
	if (g_menu.state == MS_MAIN && min_pressed_nr(MI_Y)) { /* port debug: story conversation test (conv_scene.c) */
		audio_se(0, AUDIO_SE_CD_DEFAULT);
		scene_goto(SCENE_CONV_TEST);
		return;
	}
	menu_logic_update();
	bg_tick();
}

static void menu_exit(void)
{
	panel_preview_end(); /* Controls > Hyper layout preview */
	stage3ds_end(); /* Menu Exit FUN_004bfca0(0x13) */
	ui_mtex_free_all();
	ranking_gfx_free();
}

static float band_y(void)
{
	/* yB = H/2 + (int)(240 s) − (int)(Σ 490·s·a_sub); port: closed 214, open 30 (text is taller) */
	return 214.0f - (float)(int)(184.0f * menu_sub_alpha());
}

static void draw_band(void) /* FUN_00653810 */
{
	float a = g_menu.a_band * (1 - g_menu.a_ctl); /* port: the Controls screen uses the whole top screen */
	if (a <= 0)
		return;
	float yB = band_y();
	pill(&PILL_BLUE, yB - 21, a, tr_id(0x24a), W);
	int v = (int)(255 * a);
	ui_rect(0, yB - 11, W, yB + 11, (unsigned)((int)(144 * a)) << 24 | (unsigned)v * 0x010101u);
	ui_hline(0, W, yB - 11, ui_argb(v, 0));
	ui_hline(0, W, yB + 10, ui_argb(v, 0));
	const float pitch = 120; /* PC (int)(200 s) = 80; 120 with the legible font (modes.md A.7) */
	for (int k = -3; k <= 3; k++) {
		int idx = ((g_menu.cursor + k) % ITEM_COUNT + ITEM_COUNT) % ITEM_COUNT;
		float x = W / 2 + pitch * (float)k + (float)(int)(pitch * g_menu.kick);
		bool sel = k == 0 && g_menu.state == MS_MAIN;
		unsigned col = sel ? WHITE : GREY;
		if (g_config.tournament && idx == ITEM_MATCH)
			col = sel ? 0xffff8c00u : 0xffa45a00u;
		text_draw(FONT_MENU, tr_id(0x8b + idx), x, yB - 1, TEXT_CENTER, c2d_a(col, a), k == 0 ? 1.0f : 0.85f, 0);
	}
}

static void draw_rows(const pill_style *ps, int label_id, float hy0, float a, const int *ids, int n, int sel)
{
	if (a <= 0)
		return;
	float slide = (float)(int)(480 * S * (1 - a));
	float base = H / 2 - hy0 + slide;
	pill(ps, base - 50 * S, a, tr_id(label_id), W);
	for (int k = 0; k < n; k++) {
		float y = base + (float)(int)(80 * S) * (float)k;
		row_band(y, 12, k == sel, a, W);
		text_draw(FONT_MENUBIG, tr_id(ids[k]), W / 2, y, TEXT_CENTER, c2d_a(k == sel ? WHITE : GREY, a), 1.0f, W - 16);
	}
}

static const int diff_ids[3] = {0xf6, 0xf8, 0xfa}, match_ids[2] = {0x99, 0x9b}, train_ids[2] = {0x9d, 0x9f};

static void draw_manual_top(void)
{
	float a = g_menu.a_manual;
	if (a <= 0) {
		ui_mtex_free(MT_MANUAL); /* 512 KB page: only while How to Play is shown */
		return;
	}
	ui_mtex_load(MT_MANUAL, g_menu.page + 1);
	float slide = (float)(int)(480 * S * (1 - a));
	/* page fitted to the 400 px width, scrolled vertically (port; PC draws 1024² at s) */
	float y0 = 0 - (float)g_menu.manual_scroll + slide;
	ui_mtex_draw(MT_MANUAL, 0, 0, 1024, 1024, 0, y0, W, y0 + W, with_a(WHITE, a));
}

/* ================================================================ Controls (MS_CONTROLS, controls.h) */

/* category colour per action (move-list band colours; Dash gold, Guard violet) */
static unsigned act_col(int act)
{
	static const unsigned c[AOS_ACT_COUNT] = {0xffd84a, 0xb07cff, 0x3a8cff, 0x3a8cff, 0x2fbf71, 0x2fbf71};
	return act >= 0 && act < AOS_ACT_COUNT ? c[act] : 0xa0a8c0;
}

static bool act_flash(int act)
{
	return g_ctl.flash > 0 && (g_ctl.flash_act == -2 || g_ctl.flash_act == act || g_ctl.flash_other == act) &&
	       (g_ctl.flash / 4) % 2 == 0;
}

/* gradient card with a colour tick (both screens) */
static void card(float x0, float y0, float x1, float y1, unsigned col, bool lit, float a)
{
	panel_chamfer(x0, y0 + 1.5f, x1, y1 + 1.5f, 5, with_a(0x90000000, a), with_a(0x90000000, a));
	panel_chamfer(x0, y0, x1, y1, 5, with_a(lit ? 0xf0405a9a : 0xd81c2a52, a), with_a(lit ? 0xf01c2a60 : 0xd80c1430, a));
	ui_rect(x0 + 5, y0 + 1, x1 - 5, y0 + 2, ui_argb((int)(40 * a), 0xffffff));
	ui_rect(x0, y0 + 4, x0 + 3, y1 - 4, ui_argb((int)(255 * a), col));
}

static const char *ctl_row_label(int r)
{
	switch (r) {
	case CTR_LAYOUT: return "Hyper button layout";
	case CTR_TEST: return "Input test";
	case CTR_RESET: return "Reset to default";
	case CTR_BACK: return tr("MENU_CONFIG_EXIT");
	default: return controls_act_label(r);
	}
}

static void ctl_bottom_list(float a)
{
	float slide = (float)(int)(240 * (1 - a));
	pill(&PILL_ORANGE, 12 + slide, a, tr_id(0xcb), BW);
	const float pulse = 0.5f + 0.5f * sinf(g_ctl.t * 6.2831853f * 1.5f);
	for (int r = 0; r < CTR_COUNT; r++) {
		float y0 = CT_ROW_Y0 + CT_ROW_H * (float)r + slide, cy = y0 + CT_ROW_H / 2.0f;
		bool sel = r == g_ctl.cur, act = r < AOS_ACT_COUNT, waiting = sel && g_ctl.page == CT_WAIT;
		unsigned col = act ? act_col(r) : r == CTR_LAYOUT ? panel_slot_color(HT_CENTER) : 0x97cbff;
		card(6, y0 + 1, BW - 6, y0 + CT_ROW_H - 1, col, sel, a * (sel ? 1.0f : 0.8f));
		text_draw(FONT_MENU, ctl_row_label(r), 16, cy, TEXT_LEFT, c2d_a(sel ? WHITE : GREY, a), 0.68f, 170);
		float gx = 236;
		if (waiting) {
			/* blinking prompt + the time left */
			text_draw(FONT_MENU, "PRESS A BUTTON", BW - 14, cy - 1, TEXT_RIGHT, c2d_a(0xffffe27a, a * (0.45f + 0.55f * pulse)),
			          0.62f, 0);
			float f = (float)g_ctl.wait / CT_WAIT_FRAMES;
			ui_rect(BW - 120, y0 + CT_ROW_H - 4, BW - 14, y0 + CT_ROW_H - 3, ui_argb((int)(90 * a), 0x000000));
			ui_rect(BW - 120, y0 + CT_ROW_H - 4, BW - 120 + 106 * f, y0 + CT_ROW_H - 3, ui_argb((int)(230 * a), 0xffe27a));
		} else if (act) {
			int b = aos_binds_current()->btn[r];
			float w = glyph_btn_width(b, 1);
			if (act_flash(r))
				panel_chamfer(gx - 4, y0 + 1, gx + 60, y0 + CT_ROW_H - 1, 4, with_a(0xc0ffe27a, a), with_a(0x60ffb020, a));
			glyph_btn(gx + 28 - w / 2, cy, b, a, 1);
		} else if (r == CTR_LAYOUT) {
			glyph_touch(gx + 15, cy, g_ctl.chara, HT_LEFT, a, 0.85f);
		}
		if (sel && !waiting) { /* cursor arrow on the card's right */
			u32 c = c2d_a(WHITE, a);
			C2D_DrawTriangle(BW - 10, cy, c, BW - 15, cy - 4, c, BW - 15, cy + 4, c, UI_DEPTH);
		}
	}
}

/* top screen of the list page: what every physical button does, as glyph cards */
static void ctl_top_list(float a)
{
	float slide = (float)(int)(400 * (1 - a));
	pill(&PILL_ORANGE, 10, a, tr_id(0xcb), W);
	static const int order[AOS_BTN_COUNT + 1] = {AOS_BTN_L, AOS_BTN_R, AOS_BTN_X, AOS_BTN_A, AOS_BTN_Y, AOS_BTN_B,
	                                             AOS_BTN_SELECT, -1 /* START */};
	const aos_binds *bd = aos_binds_current();
	int cur_btn = g_ctl.cur < AOS_ACT_COUNT ? bd->btn[g_ctl.cur] : -9;
	const float pulse = 0.5f + 0.5f * sinf(g_ctl.t * 6.2831853f * 1.5f);
	for (int i = 0; i < AOS_BTN_COUNT + 1; i++) {
		int b = order[i], act = b >= 0 ? aos_binds_action(bd, b) : -1;
		float x0 = 12 + (float)(i % 2) * 192 + slide, y0 = 26 + (float)(i / 2) * 36, x1 = x0 + 184, y1 = y0 + 31;
		bool lit = b == cur_btn || (g_ctl.page == CT_WAIT && pulse > 0.5f && b >= 0);
		unsigned col = b < 0 ? 0x97cbff : act_col(act);
		card(x0, y0, x1, y1, col, lit, a);
		float cy = (y0 + y1) / 2;
		if (b >= 0) {
			float w = glyph_btn_width(b, 1.6f);
			glyph_btn(x0 + 32 - w / 2, cy, b, a, 1.6f);
		} else { /* START: fixed (pause) */
			float w = text_width(FONT_SYSTEM, "START", 0.55f * 1.6f) + 12;
			panel_chamfer(x0 + 32 - w / 2, cy - 9, x0 + 32 + w / 2, cy + 9, 4, with_a(0xffe8eaf0, a), with_a(0xff8a90a0, a));
			panel_chamfer(x0 + 34 - w / 2, cy - 7, x0 + 30 + w / 2, cy + 7, 3, with_a(0xff50586a, a), with_a(0xff2a3040, a));
			text_draw(FONT_SYSTEM, "START", x0 + 32, cy, TEXT_CENTER, c2d_a(WHITE, a), 0.55f * 1.6f, 0);
		}
		const char *lbl = b < 0 ? tr("MENU_CONFIG_KEY_PAUSE") : act >= 0 ? controls_act_label(act) : "-";
		bool fl = act >= 0 && act_flash(act);
		text_draw_shadowed(FONT_MENU, lbl, x0 + 66, cy, TEXT_LEFT, c2d_a(fl ? 0xffffe27a : act >= 0 || b < 0 ? WHITE : GREY, a),
		                   0.8f, x1 - x0 - 72);
	}
	/* the two combinations, with the bound glyphs */
	float y = 190, x = 18 + slide;
	ui_rect(0, y - 14, W, y + 30, ui_argb((int)(120 * a), 0));
	text_draw(FONT_MENU, "Combinations", 10, y - 6, TEXT_LEFT, c2d_a(0xffffd09c, a), 0.55f, 0);
	y += 12;
	x += glyph_act(x, y, AOS_ACT_DASH, a, 1.2f) + 3;
	text_draw(FONT_MENU, "+", x + 4, y, TEXT_CENTER, c2d_a(WHITE, a), 0.8f, 0);
	x += 10;
	x += glyph_act(x, y, AOS_ACT_SHIELD, a, 1.2f) + 6;
	text_draw(FONT_MENU, controls_act_label(AOS_ACT_SHIELD), x, y, TEXT_LEFT, c2d_a(GREY, a), 0.7f, 0);
	x = 210 + slide;
	x += glyph_act(x, y, AOS_ACT_DASH, a, 1.2f) + 3;
	text_draw(FONT_MENU, ",", x + 2, y + 2, TEXT_CENTER, c2d_a(WHITE, a), 0.8f, 0);
	x += 7;
	x += glyph_act(x, y, AOS_ACT_DASH, a, 1.2f) + 6;
	text_draw(FONT_MENU, "Re-dash", x, y, TEXT_LEFT, c2d_a(GREY, a), 0.7f, 0);
}

/* layout page: bottom = the live panel; top = the character, the moves by position and the keys */
static int ctl_gen_drawn = -1;

static void ctl_preview_sync(void)
{
	if (panel_preview_chara() != g_ctl.chara) {
		panel_preview_begin(g_ctl.chara, 0);
		ctl_gen_drawn = g_ctl.gen;
	} else if (ctl_gen_drawn != g_ctl.gen) {
		panel_preview_refresh();
		ctl_gen_drawn = g_ctl.gen;
	}
}

static void ctl_bottom_layout(void)
{
	ctl_preview_sync();
	int held = g_ctl.held >= 0 ? g_ctl.held : g_ctl.drag;
	panel_preview_draw(g_ctl.pos, held, g_ctl.t);
}

static float key_hint(float x, float y, int btn, const char *what, float a)
{
	x += glyph_btn(x, y, btn, a, 1.1f) + 4;
	text_draw(FONT_MENU, what, x, y, TEXT_LEFT, c2d_a(WHITE, a), 0.62f, 0);
	return x + text_width(FONT_MENU, what, 0.62f) + 12;
}

static void ctl_top_layout(float a)
{
	pill(&PILL_ORANGE, 10, a, "HYPER LAYOUT", W);
	const int c = g_ctl.chara;
	char key[48];
	snprintf(key, sizeof key, "CHARACTER_%s", sel_char_key(c));
	/* name between the L / R glyphs */
	ui_rect(0, 26, W, 54, ui_argb((int)(120 * a), 0));
	glyph_btn(12, 40, AOS_BTN_L, a, 1.4f);
	glyph_btn(W - 12 - glyph_btn_width(AOS_BTN_R, 1.4f), 40, AOS_BTN_R, a, 1.4f);
	text_draw_shadowed(FONT_MENUBIG, tr(key), W / 2, 40, TEXT_CENTER, c2d_a(WHITE, a), 1.1f, W - 100);
	bool def = ht_layout_is_default(c);
	const char *badge = def ? "DEFAULT LAYOUT" : "CUSTOM LAYOUT";
	float bw = text_width(FONT_MENULABEL, badge, 1.0f) + 16;
	panel_chamfer(W / 2 - bw / 2, 58, W / 2 + bw / 2, 72, 4, with_a(def ? 0xff607090 : 0xffffd84a, a),
	              with_a(def ? 0xff303848 : 0xffc08a10, a));
	text_draw(FONT_MENULABEL, badge, W / 2, 65, TEXT_CENTER, c2d_a(def ? WHITE : 0xff201404, a), 1.0f, 0);
	/* the four positions with what sits there now */
	static const int pos_order[4] = {HT_UP, HT_LEFT, HT_RIGHT, HT_DOWN};
	for (int i = 0; i < 4; i++) {
		int p = pos_order[i], slot = ht_pos_slot(c, p);
		const ht_move *m = ht_slot(c, slot);
		/* arranged like the panel: top, left / right, bottom */
		static const float px[4] = {108, 12, 204, 108}, py[4] = {78, 114, 114, 150};
		float x0 = px[i], y0 = py[i], x1 = x0 + 184, y1 = y0 + 32;
		bool lit = p == g_ctl.pos || p == g_ctl.held ||
		           ((p == g_ctl.swapped[0] || p == g_ctl.swapped[1]) && g_ctl.swap_flash > 0);
		card(x0, y0, x1, y1, m->key ? panel_slot_color(slot) : 0x50566a, lit, a);
		float cy = (y0 + y1) / 2;
		/* the map lights the quadrant this button is on now */
		glyph_touch(x0 + 10, cy, c, slot, a, 1.1f);
		const char *nm = m->key ? panel_move_name(m->key) : "-";
		text_draw(FONT_MENU, nm, x0 + 44, cy, TEXT_LEFT, c2d_a(m->key ? WHITE : GREY, a), 0.62f, x1 - x0 - 50);
	}
	/* key hints */
	float y = 200;
	ui_rect(0, y - 12, W, H, ui_argb((int)(120 * a), 0));
	float x = 14;
	x = key_hint(x, y, AOS_BTN_A, g_ctl.held >= 0 ? "Swap here" : "Pick up", a);
	x = key_hint(x, y, AOS_BTN_Y, "Default", a);
	key_hint(x, y, AOS_BTN_B, g_ctl.held >= 0 ? "Put back" : tr("MENU_CONFIG_EXIT"), a);
	text_draw(FONT_SYSTEM, "Touch: tap two buttons, or drag one onto another, to swap them.", 14, y + 19, TEXT_LEFT,
	          c2d_a(GREY, a), 0.75f, W - 28);
}

static void ctl_draw_top(void)
{
	float a = g_menu.a_ctl;
	if (a <= 0)
		return;
	if (g_ctl.page == CT_LAYOUT && g_menu.state == MS_CONTROLS)
		ctl_top_layout(a);
	else
		ctl_top_list(a);
}

static bool ctl_draw_bottom(void) /* true: drew the whole screen (layout page) */
{
	float a = g_menu.a_ctl;
	if (g_menu.state == MS_CONTROLS && g_ctl.page == CT_LAYOUT) {
		ctl_bottom_layout();
		return true;
	}
	if (panel_preview_chara() >= 0)
		panel_preview_end(); /* art + names only while the layout page is open */
	if (a > 0)
		ctl_bottom_list(a);
	return false;
}

static void menu_draw_top(void) /* FUN_00645080 */
{
	stage3ds_draw();
	ui_blend_reset();
	float al = g_menu.a_logo;
	if (al > 0) {
		int half = (int)(256 * S);
		ui_mtex_draw(MT_LOGO, 0, 0, 0x3ff, 0x3ff, W / 2 - half, H / 2 - half, W / 2 + half, H / 2 + half,
		             with_a(WHITE, al));
		char ver[64];
		snprintf(ver, sizeof ver, "%s3DS", tr_id(0x94));
		text_draw(FONT_MENU, ver, W / 2 + (int)(380 * S), H / 2 + (int)(180 * S), TEXT_RIGHT, c2d_a(WHITE, al), 0.7f, 0);
	}
	draw_band();
	draw_rows(&PILL_YELLOW, 0x24f, 80 * S, g_menu.a_diff, diff_ids, 3, g_menu.diff);
	draw_rows(&PILL_GREEN, 0x24c, 40 * S, g_menu.a_match, match_ids, 2, g_menu.match_cur);
	draw_rows(&PILL_GREEN, 0x24d, 40 * S, g_menu.a_train, train_ids, 2, g_menu.train_cur);
	draw_manual_top();
	ctl_draw_top();
	if (g_menu.a_manual > 0)
		pill(&PILL_YELLOW, 10, g_menu.a_manual, tr_id(0x24e), W);
	ranking_draw_top(); /* Part D: ranking 400 / name entry 0x19a / unlock popup 0x1a4 */
	if (g_menu.flash > 0) {
		ui_blend(2);
		ui_rect(0, 0, W, H, ui_argb((int)(255 * g_menu.flash), 0xffffff));
		ui_blend(1);
	}
}

/* ---------------------------------------------------------------- bottom screen */

static void bottom_rows(const pill_style *ps, int label_id, float a, const int *ids, int n, int sel)
{
	if (a <= 0)
		return;
	pill(ps, 12, a, tr_id(label_id), BW);
	float pitch = n == 3 ? 52 : 70;
	for (int k = 0; k < n; k++) {
		float y = 34 + pitch * (float)k;
		bool s = k == sel;
		if (s)
			ui_rect(0, y - 4, BW, y + pitch - 10, ui_argb((int)(70 * a), 0xffffff));
		text_draw(FONT_MENU, tr_id(ids[k]), 10, y + 6, TEXT_LEFT, c2d_a(s ? WHITE : GREY, a), 0.9f, BW - 20);
		static char buf[400];
		text_wrap(FONT_MENUSUBTITLE, tr_id(ids[k] + 1), 1.0f, BW - 24, buf, sizeof buf);
		/* menusubtitle glyphs are black with a white edge: draw on the light highlight or dimmed */
		text_draw(FONT_MENUSUBTITLE, buf, 14, y + 24, TEXT_LEFT, c2d_a(WHITE, s ? a : a * 0.6f), 1.0f, 0);
	}
}

static void bottom_main(float a)
{
	pill(&PILL_BLUE, 12, a, tr_id(0x24a), BW);
	for (int i = 0; i < ITEM_COUNT; i++) {
		float y = 39 + 18 * (float)i;
		bool s = i == g_menu.cursor;
		if (s)
			row_band(y, 9, true, a, BW);
		unsigned col = s ? WHITE : GREY;
		if (g_config.tournament && i == ITEM_MATCH)
			col = s ? 0xffff8c00u : 0xffa45a00u;
		text_draw(FONT_MENU, tr_id(0x8b + i), BW / 2, y, TEXT_CENTER, c2d_a(col, a), 0.8f, 0);
	}
}

static void bottom_config(float a) /* FUN_00649210, moved to the bottom screen (A.7) */
{
	if (a <= 0)
		return;
	float slide = (float)(int)(240 * (1 - a));
	pill(&PILL_ORANGE, 12 + slide, a, tr_id(0x251), BW);
	ui_rect(0, 22 + slide, BW, 196 + slide, ui_argb((int)(0xa0 * a), 0));
	const int vis = 9;
	int top = g_menu.cfg_cur - 4;
	if (top > MENU_CFG_ROWS - vis)
		top = MENU_CFG_ROWS - vis;
	if (top < 0)
		top = 0;
	for (int k = 0; k < vis && top + k < MENU_CFG_ROWS; k++) {
		int i = top + k, row = menu_cfg_rows[i];
		float y = 33 + 19 * (float)k + slide;
		bool s = i == g_menu.cfg_cur;
		if (s)
			ui_rect(0, y - 9, BW, y + 9, ui_argb((int)(0x78 * a), 0xbebebe));
		text_draw(FONT_MENU, menu_cfg_label(row), 8, y, TEXT_LEFT, c2d_a(s ? WHITE : GREY, a), 0.72f, 150);
		int n = menu_cfg_count(row), v = menu_cfg_value(row);
		if (!n)
			continue;
		char b0[16];
		/* all options separated by "/" when they fit (PC), else the current one between arrows */
		const float cx = 238, maxw = 156, sc = 0.72f;
		const char *sep = tr_id(0xa9);
		float tw = 0;
		if (n <= 4) {
			for (int o = 0; o < n; o++)
				tw += text_width(FONT_MENU, menu_cfg_option(row, o, b0, sizeof b0), sc) + (o ? 8 + text_width(FONT_MENU, sep, sc) : 0);
		}
		if (n <= 4 && tw <= maxw) {
			float x = cx - tw / 2;
			for (int o = 0; o < n; o++) {
				if (o) {
					x += 4;
					text_draw(FONT_MENU, sep, x, y, TEXT_LEFT, c2d_a(GREY, a), sc, 0);
					x += text_width(FONT_MENU, sep, sc) + 4;
				}
				const char *t = menu_cfg_option(row, o, b0, sizeof b0);
				text_draw(FONT_MENU, t, x, y, TEXT_LEFT, c2d_a(o == v ? WHITE : GREY, a), sc, 0);
				x += text_width(FONT_MENU, t, sc);
			}
		} else {
			/* the menu font has no '<' '>': small triangles instead */
			const char *t = menu_cfg_option(row, v, b0, sizeof b0);
			float tw2 = fminf(text_width(FONT_MENU, t, sc), maxw - 24);
			u32 c = c2d_a(s ? WHITE : GREY, a);
			text_draw(FONT_MENU, t, cx, y, TEXT_CENTER, c, sc, maxw - 24);
			float xl = cx - tw2 / 2 - 8, xr = cx + tw2 / 2 + 8;
			C2D_DrawTriangle(xl - 4, y, c, xl + 2, y - 4, c, xl + 2, y + 4, c, UI_DEPTH);
			C2D_DrawTriangle(xr + 4, y, c, xr - 2, y - 4, c, xr - 2, y + 4, c, UI_DEPTH);
		}
	}
	/* scroll arrows (systemAddMdl/arrows frame 0) */
	float bob = (float)(int)(sinf((float)(scene_frame() % 100) / 100.0f * 6.2831853f) * 5 + 5) * S;
	if (top > 0)
		ui_mtex_draw(MT_ARROWS, 0, 0x5e, 0x3b, 0x99, BW - 18, 24 - bob + slide, BW - 6, 36 - bob + slide, with_a(WHITE, a));
	if (top + vis < MENU_CFG_ROWS)
		ui_mtex_draw(MT_ARROWS, 0x3c, 0x5e, 0x77, 0x99, BW - 18, 182 + bob + slide, BW - 6, 194 + bob + slide, with_a(WHITE, a));
}

static void menu_draw_bottom(void)
{
	ui_rect_grad(0, 0, BW, 240, 0xff1a2c5a, 0xff1a2c5a, 0xff05070f, 0xff05070f);
	if (ctl_draw_bottom())
		return;
	float a = g_menu.a_band;
	if (g_menu.state == MS_TITLE || a < 1) { /* dimmed static logo (A.7) */
		float al = 0.5f * (1 - a);
		ui_mtex_draw(MT_LOGO, 0, 0, 0x3ff, 0x3ff, BW / 2 - 100, 120 - 100, BW / 2 + 100, 120 + 100, with_a(WHITE, al));
	}
	float sub = menu_sub_alpha();
	if (g_menu.state == MS_MAIN || (g_menu.state == MS_LEAVING && sub < 0.5f))
		bottom_main(a);
	bottom_rows(&PILL_YELLOW, 0x24f, g_menu.a_diff, diff_ids, 3, g_menu.diff);
	bottom_rows(&PILL_GREEN, 0x24c, g_menu.a_match, match_ids, 2, g_menu.match_cur);
	bottom_rows(&PILL_GREEN, 0x24d, g_menu.a_train, train_ids, 2, g_menu.train_cur);
	if (g_menu.a_manual > 0) {
		float am = g_menu.a_manual;
		pill(&PILL_YELLOW, 12, am, tr_id(0x24e), BW);
		char t[64];
		snprintf(t, sizeof t, "%d / 9", g_menu.page + 1);
		text_draw(FONT_MENUBIG, t, BW / 2, 80, TEXT_CENTER, c2d_a(WHITE, am), 1.0f, 0);
		text_draw(FONT_SYSTEM, "Left/Right or L/R: page    Up/Down: scroll", BW / 2, 120, TEXT_CENTER, c2d_a(GREY, am), 1.0f,
		          BW - 10);
		text_draw(FONT_SYSTEM, "A: next page    B: back", BW / 2, 140, TEXT_CENTER, c2d_a(GREY, am), 1.0f, 0);
	}
	bottom_config(g_menu.a_cfg);
	ranking_draw_bottom();
}

static const scene_def menu_scene = {
	.name = "menu", .init = menu_init, .update = menu_update, .draw_top = menu_draw_top,
	.draw_bottom = menu_draw_bottom, .exit = menu_exit,
	.clear_top = 0xffaa5500, /* PC clear 0xff0055aa (ARGB) in C2D_Color32 order */
};

/* ================================================================ coming soon */

static void soon_init(void) { announce_hide(); }

static void soon_update(void)
{
	if (scene_frame() > 10 && (min_confirm() || min_cancel())) {
		audio_se(1, AUDIO_SE_CD_DEFAULT);
		if (scene_prev() == SCENE_MENU)
			menu_resume_next();
		scene_goto(scene_prev());
	}
}

static const char *scene_title(int id)
{
	switch (id) {
	case SCENE_ARC_SELECT: return g_setup.mode == MODE_STORY ? "Story mode" : "Arcade mode";
	case SCENE_RESULT: return "Result screen";
	case SCENE_CONVERSATION: return "Story conversation";
	case SCENE_ENDROLL: return "Ending";
	case SCENE_VS_SELECT: return "Character select";
	default: return "This screen";
	}
}

static void soon_draw_top(void)
{
	int req = scene_requested();
	text_draw(FONT_MENUBIG, scene_title(req), W / 2, 100, TEXT_CENTER, 0xffffffff, 1.0f, W - 20);
	text_draw(FONT_MENU, "Coming soon", W / 2, 136, TEXT_CENTER, ui_c2d(GREY), 1.0f, 0);
}

static void soon_draw_bottom(void)
{
	text_draw(FONT_SYSTEM, "A / B: back", BW / 2, 120, TEXT_CENTER, ui_c2d(GREY), 1.0f, 0);
}

static const scene_def soon_scene = {
	.name = "coming soon", .init = soon_init, .update = soon_update, .draw_top = soon_draw_top,
	.draw_bottom = soon_draw_bottom, .clear_top = 0xff301008, .clear_bottom = 0xff180804,
};

/* ================================================================ platform hooks */

void menu_apply_language(int lang)
{
	strings_init("romfs:/script", lang); /* fonts are shared by en/ptbr/sp */
}

void menu_apply_city_lod(int lod) { stage_g.lod = lod; }

void menu_scenes_register(void)
{
	scene_register(SCENE_LOGO, &logo_scene);
	scene_register(SCENE_MENU, &menu_scene);
	scene_register(SCENE_COMING_SOON, &soon_scene);
}
