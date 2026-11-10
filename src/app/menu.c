/* Logo + Menu scene logic (modes.md §A.3-A.6). Portable: drawing, stage and
 * textures are in menu_draw.c. Comments give the original state/variable. */
#include "menu.h"
#include "modes_scenes.h"

#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "music.h"
#include "scene.h"
#include "text.h"

menu_state g_menu;
logo_state g_logo;
static bool resume_next;

void menu_resume_next(void) { resume_next = true; }

static void se(int id) { audio_se(id, AUDIO_SE_CD_DEFAULT); }

/* ================================================================ logo (scene 1) */

void logo_logic_init(void) /* FUN_00632120 */
{
	g_logo.state = 0;
	g_logo.counter = 0;
}

void logo_logic_update(void) /* FUN_006321a0, input without repeat (FUN_0061bc70) */
{
	bool ok = min_pressed_nr(MI_OK) || min_pressed_nr(MI_ALT_OK);
	g_logo.counter++;
	switch (g_logo.state) {
	case 0:
		if (g_logo.counter > 50) {
			scene_fade(5);
			g_logo.state = 10;
			g_logo.counter = 0;
		}
		break;
	case 10:
	case 0x14:
		if (ok || g_logo.counter > 199) {
			scene_fade(1);
			g_logo.state += 5;
		}
		break;
	case 0xf:
		if (!scene_fade_busy()) {
			scene_fade(5);
			g_logo.state = 0x14;
			g_logo.counter = 0;
		}
		break;
	case 0x19:
		if (!scene_fade_busy())
			scene_goto(SCENE_MENU); /* Logo Exit FUN_00632160 */
		break;
	}
}

/* ================================================================ menu (scene 4) */

const int menu_cfg_rows[MENU_CFG_ROWS] = {
	CR_LANGUAGE, CR_REQUIREDWINS, CR_TIMELIMIT, CR_SFX, CR_BGM, CR_CONTROLS, CR_HITSTUN,
	CR_HICONTRAST, CR_P1COLOR, CR_P2COLOR, CR_TOURNAMENT, CR_PAUSEHOLD, CR_PAUSEMENU, CR_GREYBG,
	CR_ARMOR, CR_ARMORPCT, CR_ATTACKIND, CR_ATTACKMIRROR, CR_CITYLOD, CR_NETDELAY, CR_EXIT,
};

void menu_logic_init(void) /* FUN_00642190 */
{
	int prev = scene_prev();
	if (resume_next) { /* port-only return path */
		resume_next = false;
		int keep_cursor = g_menu.cursor, keep_diff = g_menu.diff, keep_cfg = g_menu.cfg_cur;
		bool to_cfg = g_menu.state == MS_CONFIG || prev == SCENE_LAB;
		memset(&g_menu, 0, sizeof g_menu);
		g_menu.cursor = keep_cursor;
		g_menu.diff = keep_diff;
		g_menu.cfg_cur = keep_cfg;
		g_menu.a_band = 1;
		if (to_cfg) {
			g_menu.state = MS_CONFIG;
			g_menu.a_cfg = 1;
			g_menu.cfg_lang = g_config.language;
		} else {
			g_menu.state = MS_MAIN;
			g_menu.a_logo = 1;
			announce_show(g_menu.cursor); /* ANNOUNCE_MENU_<item> */
		}
		if (music_current() != MUSIC_MENU)
			music_request(MUSIC_MENU, true, true);
	} else if ((prev == SCENE_RESULT || prev == SCENE_ENDROLL || prev == SCENE_BATTLE) && g_setup.mode == MODE_ARCADE) {
		/* arcade end / game over: 0x19a name entry + ranking (ranking_menu_init, Part D) */
		int keep_cursor = g_menu.cursor, keep_diff = g_menu.diff;
		memset(&g_menu, 0, sizeof g_menu);
		g_menu.cursor = keep_cursor;
		g_menu.diff = keep_diff;
		g_menu.state = MS_MAIN;
		g_menu.a_band = 1;
		music_request(MUSIC_MENU, true, true);
	} else {
		/* from Logo / select cancel / story end: title, everything zeroed, no BGM */
		memset(&g_menu, 0, sizeof g_menu);
		g_menu.state = MS_TITLE;
		announce_hide();
	}
	ranking_menu_init(prev); /* Part D: name entry, unlock popups, ranking page */
	g_menu.counter = 0;
	min_flush();
	scene_fade(5);
}

float menu_sub_alpha(void) { return g_menu.a_diff + g_menu.a_match + g_menu.a_train + g_menu.a_manual + g_menu.a_cfg; }

static void leave(void)
{
	g_menu.state = MS_LEAVING;
	scene_fade(1);
}

static int wrap(int v, int n) { return (v % n + n) % n; }

static bool up(void) { return min_pressed(MI_UP); }
static bool down(void) { return min_pressed(MI_DOWN); }
static bool left(void) { return min_pressed(MI_LEFT); }
static bool right(void) { return min_pressed(MI_RIGHT); }

static void main_confirm(void)
{
	se(0);
	switch (g_menu.cursor) {
	case ITEM_ARCADE:
	case ITEM_STORY:
		g_setup.sub = SUB_CPU;
		g_menu.diff_from = MS_MAIN;
		g_menu.state = MS_DIFFICULTY;
		announce_show(9); /* ANNOUNCE_SMENU_DIF */
		break;
	case ITEM_ONLINE:
		g_setup.sub = SUB_ONLINE;
		leave();
		break;
	case ITEM_MATCH:
		g_setup.mode = MODE_MATCH;
		g_menu.state = MS_MATCH;
		announce_show(10);
		break;
	case ITEM_TRAINING:
		g_menu.state = MS_TRAINING;
		announce_show(11);
		break;
	case ITEM_RANKING: /* 400 (Part D, ranking.c) */
		ranking_open();
		break;
	case ITEM_MEMORY: /* 500 replay browser: not ported (no replays) */
		se(3);
		announce_show_text("Coming soon on 3DS.");
		break;
	case ITEM_CONFIG:
		g_menu.state = MS_CONFIG;
		g_menu.cfg_cur = 0;
		g_menu.cfg_lang = g_config.language;
		announce_show(menu_cfg_announce(menu_cfg_rows[0]));
		break;
	case ITEM_EXIT:
		leave();
		break;
	}
}

/* ---------------------------------------------------------------- config */

int menu_cfg_count(int row)
{
	switch (row) {
	case CR_LANGUAGE: return 3; /* en, ptbr, sp (packed glyph set) */
	case CR_REQUIREDWINS: return 3;
	case CR_TIMELIMIT: return 4;
	case CR_SFX:
	case CR_BGM: return 11;
	case CR_P1COLOR:
	case CR_P2COLOR: return 6;
	case CR_CITYLOD: return 4;
	case CR_NETDELAY: return 5;
	case CR_CONTROLS:
	case CR_EXIT: return 0;
	default: return 2; /* bools: Enabled / Disabled */
	}
}

static const int lang_opts[3] = {LANG_EN, LANG_PTBR, LANG_SP};

static bool *cfg_bool(int row)
{
	switch (row) {
	case CR_HITSTUN: return &g_config.hitstun_meter;
	case CR_HICONTRAST: return &g_config.high_contrast;
	case CR_TOURNAMENT: return &g_config.tournament;
	case CR_PAUSEHOLD: return &g_config.pause_hold;
	case CR_PAUSEMENU: return &g_config.pause_on_start;
	case CR_GREYBG: return &g_config.grey_bg;
	case CR_ARMOR: return &g_config.armor_points;
	case CR_ARMORPCT: return &g_config.armor_percent;
	case CR_ATTACKIND: return &g_config.attack_ind;
	case CR_ATTACKMIRROR: return &g_config.attack_ind_mirror;
	default: return NULL;
	}
}

int menu_cfg_value(int row)
{
	bool *b = cfg_bool(row);
	if (b)
		return *b ? 0 : 1; /* option 0 = ENABLED (drawn left) */
	switch (row) {
	case CR_LANGUAGE:
		for (int i = 0; i < 3; i++)
			if (lang_opts[i] == g_menu.cfg_lang)
				return i;
		return 0;
	case CR_REQUIREDWINS: return g_config.required_wins;
	case CR_TIMELIMIT: return g_config.time_limit;
	case CR_SFX: return g_config.se_volume;
	case CR_BGM: return g_config.bgm_volume;
	case CR_P1COLOR: return g_config.p1_color;
	case CR_P2COLOR: return g_config.p2_color;
	case CR_CITYLOD: return g_config.city_lod;
	case CR_NETDELAY: return g_config.net_delay;
	default: return 0;
	}
}

const char *menu_cfg_label(int row)
{
	switch (row) {
	case CR_CITYLOD: return "City sprites (3DS)";
	case CR_NETDELAY: return "Wireless input delay";
	case CR_CONTROLS: return tr_id(0xcb);
	default: break;
	}
	/* label text ids follow the row order of modes_menu_config.tsv */
	static const int ids[27] = {0xad, 0xb6, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xc0, 0xc4, 0xc5, 0xc6, 0xcb, 0xcc,
	                            0xcd, 0xce, 0xcf, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf};
	return row >= 0 && row < 27 ? tr_id(ids[row]) : "";
}

const char *menu_cfg_option(int row, int i, char *buf, int bufsz)
{
	static const char *const lod[4] = {"Exact", "Smoke cap", "Half", "Cap + half"};
	if (cfg_bool(row))
		return tr_id(i == 0 ? 0xab : 0xac);
	switch (row) {
	case CR_LANGUAGE: { /* 0xae..0xb5 are drawn from the languagenames font on PC (native names) */
		static const char *const names[3] = {"English", "Português", "Español"};
		return names[i];
	}
	case CR_REQUIREDWINS: snprintf(buf, (size_t)bufsz, "%d", i + 1); return buf;
	case CR_TIMELIMIT:
		if (i == 3)
			return tr_id(0xbf);
		snprintf(buf, (size_t)bufsz, "%d", config_timer_seconds(i));
		return buf;
	case CR_SFX:
	case CR_BGM:
	case CR_NETDELAY: snprintf(buf, (size_t)bufsz, "%d", i); return buf;
	case CR_P1COLOR:
	case CR_P2COLOR: return tr_id(0xd0 + i);
	case CR_CITYLOD: return lod[i & 3];
	default: return "";
	}
}

int menu_cfg_announce(int row) { return row < 27 ? row + 13 : -1; } /* FUN_00647a40: ANNOUNCE 0xd + row */

const char *menu_cfg_help(int row)
{
	switch (row) {
	case CR_CITYLOD: return "Capital in Flames / Whirlpool city sprites: presentation option for speed.";
	case CR_NETDELAY: return "Local wireless input delay in frames (the host's value is used).";
	case CR_CONTROLS: return "Show the 3DS button mapping and test inputs (input lab).";
	default: return announce_by_id(menu_cfg_announce(row));
	}
}

static void tournament_off(void) { g_config.tournament = false; }

static void cfg_change(int row, int dir)
{
	bool *b = cfg_bool(row);
	if (b) {
		bool v = dir < 0; /* Left = ENABLED, Right = DISABLED */
		if (*b == v)
			return;
		*b = v;
		if (row == CR_TOURNAMENT) {
			if (v) { /* preset (modes_menu_config.tsv row 17) */
				g_config.required_wins = 0;
				g_config.time_limit = 1;
				g_config.hitstun_meter = true;
				g_config.high_contrast = false;
				g_config.p1_color = 2;
				g_config.p2_color = 0;
				g_config.pause_hold = true;
				g_config.grey_bg = false;
				g_config.armor_points = false;
				g_config.attack_ind = false;
				g_config.attack_ind_mirror = true;
			}
		} else if (row == CR_HICONTRAST) {
			if (v)
				g_config.grey_bg = true;
			tournament_off();
		} else if (row == CR_GREYBG) {
			if (!v)
				g_config.high_contrast = false;
			else
				tournament_off();
		} else if ((row == CR_HITSTUN) || (row == CR_ARMOR && v) || (row == CR_ATTACKIND && v) ||
		           (row == CR_PAUSEHOLD && !v) || (row == CR_ATTACKMIRROR && !v)) {
			tournament_off();
		}
		se(4);
		return;
	}
	int n = menu_cfg_count(row);
	if (!n)
		return;
	int v = menu_cfg_value(row), nv;
	bool clamp = row == CR_SFX || row == CR_BGM || row == CR_NETDELAY;
	nv = clamp ? (v + dir < 0 ? 0 : v + dir >= n ? n - 1 : v + dir) : wrap(v + dir, n);
	if (nv == v)
		return;
	switch (row) {
	case CR_LANGUAGE: g_menu.cfg_lang = lang_opts[nv]; break;
	case CR_REQUIREDWINS: g_config.required_wins = nv; tournament_off(); break;
	case CR_TIMELIMIT: g_config.time_limit = nv; tournament_off(); break;
	case CR_SFX: g_config.se_volume = nv; audio_set_se_level(nv); break; /* FUN_007bc800 */
	case CR_BGM: g_config.bgm_volume = nv; music_set_level(nv); break;   /* FUN_004bf2e0 */
	case CR_P1COLOR: g_config.p1_color = nv; tournament_off(); break;
	case CR_P2COLOR: g_config.p2_color = nv; tournament_off(); break;
	case CR_CITYLOD: g_config.city_lod = nv; menu_apply_city_lod(nv); break;
	case CR_NETDELAY: g_config.net_delay = nv; break;
	}
	se(4);
}

static void cfg_exit(void) /* FUN_00647d20: apply language, save, back to 100 */
{
	if (g_menu.cfg_lang != g_config.language) {
		g_config.language = g_menu.cfg_lang;
		menu_apply_language(g_config.language);
	}
	config_save();
	g_menu.state = MS_MAIN;
	announce_show(g_menu.cursor);
	se(0);
}

static void config_update(void) /* FUN_00647fd0 */
{
	int row = menu_cfg_rows[g_menu.cfg_cur];
	if (up() || down()) {
		g_menu.cfg_cur = wrap(g_menu.cfg_cur + (down() ? 1 : -1), MENU_CFG_ROWS);
		row = menu_cfg_rows[g_menu.cfg_cur];
		int a = menu_cfg_announce(row);
		if (a >= 0)
			announce_show(a);
		else
			announce_show_text(menu_cfg_help(row));
		se(2);
	} else if (left() || right()) {
		cfg_change(row, left() ? -1 : 1);
	} else if (min_confirm()) {
		if (row == CR_EXIT)
			cfg_exit();
		else if (row == CR_CONTROLS) {
			se(0);
			config_save();
			menu_resume_next();
			scene_goto(SCENE_LAB); /* port: input lab instead of key config 0x280 */
		}
	} else if (min_cancel()) {
		if (row == CR_EXIT)
			cfg_exit();
		else {
			g_menu.cfg_cur = MENU_CFG_ROWS - 1;
			announce_show(menu_cfg_announce(CR_EXIT));
			se(1);
		}
	}
}

/* ---------------------------------------------------------------- update */

static void approach(float *a, bool on)
{
	*a += on ? 0.05f : -0.05f; /* 0x813ef4 */
	if (*a > 1)
		*a = 1;
	if (*a < 0)
		*a = 0;
}

void menu_logic_update(void) /* FUN_00643150 */
{
	menu_state *m = &g_menu;
	m->counter++;
	switch (m->state) {
	case MS_TITLE:
		if (min_confirm() || m->counter > 299) {
			m->state = MS_MAIN;
			m->flash = 1.0f;
			se(0);
			music_request(MUSIC_MENU, true, true);
			announce_show(0);
		}
		break;
	case MS_MAIN:
		/* PC: left/right only. Port: up/down too (the bottom-screen item list is vertical) */
		if (right() || left() || up() || down()) {
			int d = right() || down() ? 1 : -1;
			m->cursor = wrap(m->cursor + d, ITEM_COUNT);
			m->kick = (float)d;
			se(2);
			announce_show(m->cursor); /* FUN_006536f0 */
		} else if (min_cancel()) {
			if (m->cursor != ITEM_EXIT) {
				m->cursor = ITEM_EXIT;
				se(2);
				announce_show(ITEM_EXIT);
			}
		} else if (min_confirm()) {
			main_confirm();
		}
		break;
	case MS_DIFFICULTY:
		if (up() || down()) {
			m->diff = wrap(m->diff + (down() ? 1 : -1), 3);
			se(2);
		} else if (min_cancel()) {
			m->state = m->diff_from == MS_MATCH ? MS_MATCH : MS_MAIN;
			announce_show(m->state == MS_MATCH ? 10 : m->cursor);
			se(1);
		} else if (min_confirm()) {
			se(0);
			leave();
		}
		break;
	case MS_MATCH:
		if (up() || down()) {
			m->match_cur = wrap(m->match_cur + 1, 2);
			se(2);
		} else if (min_cancel()) {
			m->state = MS_MAIN;
			announce_show(m->cursor);
			se(1);
		} else if (min_confirm()) {
			se(0);
			if (m->match_cur == 0) { /* 2P Game: on 3DS = local wireless (two consoles) */
				g_setup.sub = SUB_2P;
				leave();
			} else {
				g_setup.sub = SUB_CPU;
				m->diff_from = MS_MATCH;
				m->state = MS_DIFFICULTY;
				announce_show(9);
			}
		}
		break;
	case MS_TRAINING:
		if (up() || down()) {
			m->train_cur = wrap(m->train_cur + 1, 2);
			se(2);
		} else if (min_cancel()) {
			m->state = MS_MAIN;
			announce_show(m->cursor);
			se(1);
		} else if (min_confirm()) {
			se(0);
			if (m->train_cur == 0) {
				m->state = MS_MANUAL;
				m->page = 0;
				m->manual_scroll = 0;
				announce_show(12);
			} else {
				g_setup.mode = MODE_TRAINING;
				g_setup.sub = SUB_CPU;
				leave();
			}
		}
		break;
	case MS_MANUAL:
		if (left() || min_pressed(MI_L)) {
			if (m->page > 0) {
				m->page--;
				m->manual_scroll = 0;
				se(2);
			} else
				se(3);
		} else if (right() || min_pressed(MI_R)) {
			if (m->page < 8) {
				m->page++;
				m->manual_scroll = 0;
				se(2);
			} else
				se(3);
		} else if (min_confirm()) {
			se(0);
			if (m->page < 8) {
				m->page++;
				m->manual_scroll = 0;
			} else {
				m->state = MS_TRAINING;
				announce_show(11);
			}
		} else if (min_cancel()) {
			m->state = MS_TRAINING;
			announce_show(11);
			se(1);
		}
		/* port: scroll the page (fit to the 400 px width, 400 px tall) */
		if (min_state(MI_UP) >= MIN_HELD)
			m->manual_scroll -= 4;
		if (min_state(MI_DOWN) >= MIN_HELD)
			m->manual_scroll += 4;
		if (m->manual_scroll < 0)
			m->manual_scroll = 0;
		if (m->manual_scroll > 160)
			m->manual_scroll = 160;
		break;
	case MS_CONFIG:
		config_update();
		break;
	case MS_RANKING:
	case MS_NAME_ENTRY:
	case MS_UNLOCK:
		ranking_update(); /* Part D (ranking.c) */
		break;
	case MS_LEAVING:
		if (!scene_fade_busy())
			menu_logic_exit();
		break;
	}
	/* tail: panel alphas (frozen while leaving), flash, carousel glide */
	if (m->state != MS_LEAVING) {
		approach(&m->a_logo, m->state == MS_MAIN);
		approach(&m->a_band, m->state != MS_TITLE && m->state != MS_UNLOCK && m->state != MS_RANKING); /* port: ranking rows reach the band */
		approach(&m->a_diff, m->state == MS_DIFFICULTY);
		approach(&m->a_match, m->state == MS_MATCH);
		approach(&m->a_train, m->state == MS_TRAINING);
		approach(&m->a_manual, m->state == MS_MANUAL);
		approach(&m->a_cfg, m->state == MS_CONFIG);
	}
	ranking_tail();
	if (m->flash > 0) {
		m->flash -= 0.025f; /* 0x813edc */
		if (m->flash < 0)
			m->flash = 0;
	}
	if (m->kick > 0) {
		m->kick -= 0.1f;
		if (m->kick < 0)
			m->kick = 0;
	} else if (m->kick < 0) {
		m->kick += 0.1f;
		if (m->kick > 0)
			m->kick = 0;
	}
}

void menu_logic_exit(void) /* FUN_00642880 */
{
	match_setup *s = &g_setup;
	s->menu_item = g_menu.cursor;
	s->handicap[0] = s->handicap[1] = 2;
	s->input[0] = 0;
	s->input[1] = INPUT_CPU;
	s->color[0] = s->color[1] = 3;
	s->timer = config_timer_seconds(g_config.time_limit);
	s->new_match = true;
	switch (g_menu.cursor) {
	case ITEM_ARCADE:
	case ITEM_STORY:
		s->mode = g_menu.cursor == ITEM_ARCADE ? MODE_ARCADE : MODE_STORY;
		s->sub = SUB_CPU;
		s->difficulty = g_menu.diff;
		s->rounds = 1;
		s->timer = 180;
		scene_goto(SCENE_ARC_SELECT);
		break;
	case ITEM_ONLINE:
		s->sub = SUB_ONLINE;
		menu_resume_next();
		scene_goto(SCENE_NET_SELECT);
		break;
	case ITEM_MATCH:
		s->mode = MODE_MATCH;
		s->rounds = g_config.required_wins + 1;
		if (s->sub == SUB_2P) { /* 3DS: the second player is on another console */
			menu_resume_next();
			scene_goto(SCENE_NET_SELECT);
		} else {
			s->difficulty = g_menu.diff;
			scene_goto(SCENE_VS_SELECT);
		}
		break;
	case ITEM_TRAINING:
		s->mode = MODE_TRAINING;
		s->sub = SUB_CPU;
		s->difficulty = 1;
		s->rounds = 1;
		s->timer = 999;
		scene_goto(SCENE_VS_SELECT);
		break;
	case ITEM_EXIT:
		scene_goto(SCENE_QUIT);
		break;
	default:
		scene_goto(SCENE_MENU);
		break;
	}
}
