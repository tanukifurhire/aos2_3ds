/* Config > Controls logic (controls.h). Portable. */
#include "controls.h"

#include <stdio.h>
#include <string.h>

#include "../input/aos_input.h"
#include "../input/hyper_touch.h"
#include "../modes/save.h"
#include "audio.h"
#include "menu.h"
#include "modes_flow.h"
#include "modes_scenes.h"
#include "scene.h"
#include "text.h"

controls_state g_ctl;
int (*controls_hit)(int x, int y);

static void se(int id) { audio_se(id, AUDIO_SE_CD_DEFAULT); }

#define CH_FIRST 3 /* Sora */
#define CH_LAST 17 /* Sumika (her second form shares the layout) */

void controls_apply_save(void)
{
	aos_save *s = modes_save();
	aos_binds b;
	memcpy(b.btn, s->binds, sizeof b.btn);
	aos_binds_set_current(&b); /* invalid -> defaults */
	ht_layout_reset_all();
	for (int c = 0; c < AOS_SAVE_CHARS && c < AOS_NUM_CHARS; c++)
		if (s->hyperLayout[c][0] != 0xff)
			ht_layout_set(c, s->hyperLayout[c]); /* invalid -> default */
}

static void save_binds(void)
{
	aos_save *s = modes_save();
	memcpy(s->binds, aos_binds_current()->btn, sizeof s->binds);
	s->dirty = 1;
}

static void save_layout(int c)
{
	aos_save *s = modes_save();
	if (c < 0 || c >= AOS_SAVE_CHARS)
		return;
	if (ht_layout_is_default(c))
		memset(s->hyperLayout[c], 0xff, sizeof s->hyperLayout[c]); /* follows the default rule */
	else
		ht_layout_get(c, s->hyperLayout[c]);
	s->dirty = 1;
}

const char *controls_act_label(int act)
{
	static char buf[2][48];
	switch (act) {
	case AOS_ACT_DASH: return tr("MENU_CONFIG_KEY_DASH");
	case AOS_ACT_SHIELD: return tr("MENU_CONFIG_KEY_GUARD");
	case AOS_ACT_WEAPON_A: return tr("MENU_CONFIG_KEY_PRIMARYWEAPON");
	case AOS_ACT_WEAPON_B: return tr("MENU_CONFIG_KEY_SUBWEAPON");
	case AOS_ACT_SPECIAL_A:
	case AOS_ACT_SPECIAL_B: { /* the PC has one Special button (S); the port has S+A and S+B */
		char *b = buf[act == AOS_ACT_SPECIAL_B];
		snprintf(b, sizeof buf[0], "%s %c", tr("MENU_CONFIG_KEY_SPECIALWEAPON"), act == AOS_ACT_SPECIAL_A ? 'A' : 'B');
		return b;
	}
	default: return "";
	}
}

const char *controls_help(void)
{
	if (g_ctl.page == CT_WAIT)
		return "Press the new button for this action.  START: cancel";
	if (g_ctl.page == CT_LAYOUT)
		return "L/R: character  +Pad: choose  A: pick up / swap  Y: default  B: back";
	switch (g_ctl.cur) {
	case CTR_LAYOUT: return "Arrange each character's touch hyper buttons.";
	case CTR_TEST: return "Try the controls in the input lab.";
	case CTR_RESET: return "Put every action back on its default button.";
	case CTR_BACK: return "Back to the config menu.";
	default: return "A: choose a new button for this action. Dash + Guard together = Guard, Dash twice = re-dash.";
	}
}

static void help(void) { announce_show_text(controls_help()); }

void controls_open(void)
{
	int keep_chara = g_ctl.chara;
	memset(&g_ctl, 0, sizeof g_ctl);
	g_ctl.page = CT_LIST;
	g_ctl.flash_act = g_ctl.flash_other = -1;
	g_ctl.held = g_ctl.drag = -1;
	g_ctl.swapped[0] = g_ctl.swapped[1] = -1;
	g_ctl.pos = HT_LEFT;
	g_ctl.chara = keep_chara >= CH_FIRST && keep_chara <= CH_LAST         ? keep_chara
	            : g_setup.chara[0] >= CH_FIRST && g_setup.chara[0] <= CH_LAST ? g_setup.chara[0]
	                                                                         : AOS_CH_SORA;
	help();
}

static int wrap(int v, int n) { return (v % n + n) % n; }

/* the physical button pressed this frame (menu input slots), -1 none */
static int pressed_button(void)
{
	static const int slot[AOS_BTN_COUNT] = {MI_OK, MI_CANCEL, MI_X, MI_Y, MI_L, MI_R, MI_SELECT};
	for (int b = 0; b < AOS_BTN_COUNT; b++)
		if (min_pressed_nr(slot[b]))
			return b;
	return -1;
}

static void list_confirm(void)
{
	if (g_ctl.cur < AOS_ACT_COUNT) {
		se(0);
		g_ctl.page = CT_WAIT;
		g_ctl.wait = CT_WAIT_FRAMES;
		help();
		return;
	}
	switch (g_ctl.cur) {
	case CTR_LAYOUT:
		se(0);
		g_ctl.page = CT_LAYOUT;
		g_ctl.held = g_ctl.drag = -1;
		g_ctl.gen++;
		announce_hide(); /* the panel needs the whole bottom screen; the keys are shown on the top screen */
		break;
	case CTR_TEST:
		se(0);
		menu_resume_next();
		scene_goto(SCENE_LAB);
		break;
	case CTR_RESET: {
		aos_binds d;
		aos_binds_default(&d);
		aos_binds_set_current(&d);
		save_binds();
		g_ctl.flash_act = -2; /* all */
		g_ctl.flash_other = -1;
		g_ctl.flash = 40;
		se(4);
		break;
	}
	default: break;
	}
}

static bool list_update(void)
{
	bool back = false;
	if (min_pressed(MI_UP) || min_pressed(MI_DOWN)) {
		g_ctl.cur = wrap(g_ctl.cur + (min_pressed(MI_DOWN) ? 1 : -1), CTR_COUNT);
		se(2);
		help();
	} else if (min_pressed_nr(MI_TOUCH)) {
		int r = (g_min.touch_y - CT_ROW_Y0) / CT_ROW_H;
		if (g_min.touch_y >= CT_ROW_Y0 && r >= 0 && r < CTR_COUNT) {
			if (r != g_ctl.cur) {
				g_ctl.cur = r;
				se(2);
				help();
			} else if (r == CTR_BACK)
				back = true;
			else
				list_confirm();
		}
	} else if (min_confirm()) {
		if (g_ctl.cur == CTR_BACK)
			back = true;
		else
			list_confirm();
	} else if (min_cancel()) {
		back = true;
	}
	if (back)
		se(1);
	return !back;
}

static void wait_update(void)
{
	int b = pressed_button();
	if (b >= 0) {
		aos_binds n = *aos_binds_current();
		int other = aos_binds_assign(&n, g_ctl.cur, b);
		aos_binds_set_current(&n);
		save_binds();
		g_ctl.flash_act = g_ctl.cur;
		g_ctl.flash_other = other;
		g_ctl.flash = 50;
		g_ctl.page = CT_LIST;
		se(other >= 0 ? 4 : 0);
		if (other >= 0) {
			static char msg[128];
			snprintf(msg, sizeof msg, "%s and %s swapped buttons.", controls_act_label(g_ctl.cur),
			         controls_act_label(other));
			announce_show_text(msg);
		} else
			help();
		return;
	}
	if (min_pressed_nr(MI_START) || min_pressed_nr(MI_TOUCH) || --g_ctl.wait <= 0) {
		g_ctl.page = CT_LIST;
		se(1);
		help();
	}
}

static void layout_swap(int a, int b)
{
	if (ht_layout_swap(g_ctl.chara, a, b)) {
		save_layout(g_ctl.chara);
		g_ctl.gen++;
		g_ctl.swapped[0] = a;
		g_ctl.swapped[1] = b;
		g_ctl.swap_flash = 24;
		se(4);
	}
}

/* A / tap on position p: pick up, drop, or swap with the picked-up one */
static void layout_pick(int p)
{
	if (p < 0 || p == HT_CENTER)
		return;
	g_ctl.pos = p;
	if (g_ctl.held < 0) {
		g_ctl.held = p;
		se(0);
	} else if (g_ctl.held == p) {
		g_ctl.held = -1;
		se(1);
	} else {
		layout_swap(g_ctl.held, p);
		g_ctl.held = -1;
	}
}

static void layout_update(void)
{
	if (min_pressed_nr(MI_L) || min_pressed_nr(MI_R)) {
		g_ctl.chara += min_pressed_nr(MI_R) ? 1 : -1;
		if (g_ctl.chara > CH_LAST)
			g_ctl.chara = CH_FIRST;
		if (g_ctl.chara < CH_FIRST)
			g_ctl.chara = CH_LAST;
		g_ctl.held = g_ctl.drag = -1;
		g_ctl.gen++;
		se(2);
		return;
	}
	static const int dir_slot[4] = {MI_UP, MI_LEFT, MI_RIGHT, MI_DOWN}; /* index = HT_UP..HT_DOWN */
	for (int k = 0; k < 4; k++)
		if (min_pressed(dir_slot[k])) {
			if (g_ctl.pos != k)
				se(2);
			g_ctl.pos = k;
			return;
		}
	/* touch: tap = pick / drop / swap like A, drag from one quadrant to another = swap */
	int tp = controls_hit ? controls_hit(g_min.touch_x, g_min.touch_y) : -1;
	if (min_pressed_nr(MI_TOUCH)) {
		g_ctl.drag = tp == HT_CENTER ? -1 : tp;
		if (g_ctl.drag >= 0)
			g_ctl.pos = g_ctl.drag;
		return;
	}
	if (g_min.nr[MI_TOUCH] == MIN_RELEASED) {
		int from = g_ctl.drag;
		g_ctl.drag = -1;
		if (from < 0)
			return;
		if (tp == from || tp < 0 || tp == HT_CENTER)
			layout_pick(from);
		else {
			layout_swap(from, tp);
			g_ctl.held = -1;
			g_ctl.pos = tp;
		}
		return;
	}
	if (min_confirm()) {
		layout_pick(g_ctl.pos);
	} else if (min_pressed_nr(MI_Y)) {
		if (!ht_layout_is_default(g_ctl.chara)) {
			ht_layout_set(g_ctl.chara, NULL);
			save_layout(g_ctl.chara);
			g_ctl.gen++;
			se(4);
		} else
			se(3);
		g_ctl.held = -1;
	} else if (min_cancel()) {
		se(1);
		if (g_ctl.held >= 0) {
			g_ctl.held = -1;
		} else {
			g_ctl.page = CT_LIST;
			help();
		}
	}
}

bool controls_update(void)
{
	g_ctl.t += 1.0f / 60.0f;
	if (g_ctl.flash > 0 && --g_ctl.flash == 0)
		g_ctl.flash_act = g_ctl.flash_other = -1;
	if (g_ctl.swap_flash > 0 && --g_ctl.swap_flash == 0)
		g_ctl.swapped[0] = g_ctl.swapped[1] = -1;
	switch (g_ctl.page) {
	case CT_WAIT: wait_update(); return true;
	case CT_LAYOUT: layout_update(); return true;
	default: return list_update();
	}
}
