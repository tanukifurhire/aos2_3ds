/*
 * Menu sub-screens of Part D (modes_scenes.h; docs/re/modes.md §D.2.3, §D.5, §A.4.2):
 * ranking view (state 400), arcade name entry (0x19a), unlock popups (0x1a4).
 * Portable logic (host-tested by tests/test_menu.c); drawing in ranking_draw.c.
 */
#include <string.h>

#include "audio.h"
#include "menu.h"
#include "modes_flow.h"
#include "modes_scenes.h"
#include "music.h"
#include "scene.h"
#include "text.h"

rank_state g_rank = {.hl_rank = 10};

static void se(int id) { audio_se(id, AUDIO_SE_CD_DEFAULT); }

/* ---------------------------------------------------------------- data */

char ranking_letter(int32_t v) /* FUN_00659480 */
{
	static const char sym[] = "./!?()\"-=";
	if (v >= 0 && v < 26)
		return (char)('A' + v);
	if (v >= 26 && v < 36)
		return (char)('0' + v - 26); /* index + 0x16 */
	if (v >= 36 && v < 45)
		return sym[v - 36];
	return ' '; /* 45 = blank (new entries) */
}

void ranking_name(int page, int k, bool trim, char out[8])
{
	memset(out, 0, 8);
	if (page < 0 || page > 2 || k < 0 || k >= AOS_RANK_ENTRIES)
		return;
	const aos_rank_entry *e = &modes_save()->rank[page][k];
	for (int i = 0; i < AOS_RANK_NAME; i++)
		out[i] = ranking_letter(e->name[i]);
	if (trim) /* FUN_00658410: find_last_not_of(' ') */
		for (int i = AOS_RANK_NAME - 1; i >= 0 && out[i] == ' '; i--)
			out[i] = 0;
}

/* Pages 3..8: hard-coded tournament results (FUN_006597b0 strings, characters pushed by the draw). */
static const struct {
	const char *rank, *name;
	int chara;
} tour[6][8] = {
	{{"#1", "HispanicAtTheDisco", 3}, {"#2", "FS Alte", 3}, {"#3", "FS Squash", 8}, {"#4", "Deadzero", 14},
	 {"#5", "S_ Hello", 11}, {"#5", "Syrorin", 11}, {"#7", "Duse", 9}, {"#7", "Zack", 14}},
	{{"#1", "IWSP", 12}, {"#2", "Golfdinger", 9}, {"#3", "nope", 10}, {"#4", "Jiraymin", 3},
	 {"#5", "iLLy", 3}, {"#5", "Celestine?Tetra", 16}, {"#7", "Bonfire", 16}, {"#7", "Soqolov", 5}},
	{{"#1", "OOTORI", 12}, {"#2", "Nix", 17}, {"#3", "Golfdinger", 9}, {"#4", "Fush", 14},
	 {"#5", "Crosster", 15}, {"#5", "DOC", 16}, {"#7", "iLLy___", 3}, {"#7", "SirAnthonyIII_", 3}},
	{{"#1", "trans rights!!!", 14}, {"#2", "Lacerarsi", 16}, {"#3", "Golfdinger", 9}, {"#4", "DOC", 16},
	 {"#5", "Glitch", 17}, {"#5", "Tripel", 3}, {"#7", "Sanic", 11}, {"#7", "Alte", 8}},
	{{"#1", "Glitch", 17}, {"#2", "trans rights!!!", 6}, {"#3", "Sanya", 11}, {"#4", "Lacera", 16},
	 {"#5", "Keko", 17}, {"#5", "Jam", 11}, {"#5", "Golfdinger", 9}, {"#5", "Sorafan", 3}},
	{{"#1", "trans rights!!!", 6}, {"#2", "Lacerarsi", 16}, {"#3", "Tripel", 3}, {"#4", "Keko", 17},
	 {"#5", "Ecial", 9}, {"#5", "Hispanic", 16}, {"#7", "Remedy", 16}, {"#7", "SirAnthonyIII_", 3}},
};

int ranking_rows(int page) { return page < 3 ? 10 : 8; }
const char *ranking_tour_rank(int page, int k) { return page >= 3 && page < 9 && k < 8 ? tour[page - 3][k].rank : ""; }
const char *ranking_tour_name(int page, int k) { return page >= 3 && page < 9 && k < 8 ? tour[page - 3][k].name : ""; }
int ranking_char(int page, int k)
{
	if (page >= 0 && page < 3 && k >= 0 && k < 10)
		return modes_save()->rank[page][k].character;
	return page >= 3 && page < 9 && k >= 0 && k < 8 ? tour[page - 3][k].chara : 0;
}
int32_t ranking_score(int page, int k)
{
	return page >= 0 && page < 3 && k >= 0 && k < 10 ? modes_save()->rank[page][k].score : 0;
}
int ranking_title_id(int page)
{
	static const int ids[3] = {0xf6, 0xf8, 0xfa}; /* copies 0xfc..0xfe (menu font) of Easy/Normal/Hard */
	return page < 3 ? ids[page < 0 ? 0 : page] : 0xff + page - 3; /* 0x105.. = copies of 0xff..0x104 */
}

/* ---------------------------------------------------------------- transitions */

static void rows_in(float sign) /* v[k] = (10-k)*0.25 + 1 (entering / Right) or its negative (Left) */
{
	for (int k = 0; k < 10; k++)
		g_rank.v[k] = sign * ((float)(10 - k) * 0.25f + 1.0f);
}

static void to_view(void) /* FUN_006593b0 (Steam upload: skipped) + FUN_006597b0 + state 400 */
{
	g_menu.state = MS_RANKING;
	announce_show(0x35); /* ANNOUNCE_RANKING: left/right = difficulty / tournament */
	rows_in(1);
}

static bool from_run(int prev)
{
	/* Result (3) / Endroll (11); port: Battle (2) when no Result scene is registered (modes_flow pass-through) */
	return (prev == SCENE_RESULT || prev == SCENE_ENDROLL || (prev == SCENE_BATTLE && !scene_get(SCENE_RESULT))) &&
	       (g_setup.mode == MODE_ARCADE || g_setup.mode == MODE_STORY) && !modes_flow_active();
}

void ranking_menu_init(int prev) /* FUN_00642190, Part D branches */
{
	g_rank.a = g_rank.b = g_rank.pa = g_rank.pb = 0;
	g_rank.msg = 0;
	g_rank.nmsg = 0;
	if (!from_run(prev)) {
		if (g_menu.state == MS_TITLE) /* normal path: page 0, nothing highlighted */
			g_rank.page = 0;
		g_rank.hl_rank = 10;
		return;
	}
	const ladder_run *r = modes_flow_run();
	if (g_setup.mode == MODE_ARCADE) { /* -> 0x19a: ranking of the run's difficulty, name entry if ranked */
		g_rank.page = r->difficulty;
		g_rank.hl_rank = r->rank;
		g_rank.hl_diff = r->difficulty;
		g_rank.score = r->score;
		g_rank.chara = r->player;
		g_rank.b = 1;
		g_rank.pos = 0;
		g_menu.state = MS_NAME_ENTRY;
		g_menu.a_band = 1;
		if (g_rank.hl_rank < 10)
			announce_show(0x36); /* ANNOUNCE_RANKING_ENTERNAME */
		else
			announce_hide();
	} else {
		g_rank.hl_rank = 10; /* story: title (Menu Init already set it) */
	}
	g_rank.nmsg = modes_flow_msg_count();
	if (g_rank.nmsg > 0) { /* unlock popups first */
		g_menu.state = MS_UNLOCK;
		g_menu.a_band = 0;
		g_rank.b = 0;
		announce_hide();
		se(5);
	}
}

void ranking_open(void) /* main menu item 6 */
{
	to_view();
}

/* ---------------------------------------------------------------- input */

static void view_update(void) /* FUN_0065c670 */
{
	if (min_pressed(MI_RIGHT)) {
		g_rank.page = (g_rank.page + 1) % RANK_PAGES;
		se(2);
		rows_in(1);
	}
	if (min_pressed(MI_LEFT)) {
		g_rank.page = (g_rank.page + RANK_PAGES - 1) % RANK_PAGES;
		se(2);
		rows_in(-1);
	}
	if (min_confirm() || min_cancel()) {
		g_menu.state = MS_MAIN;
		g_rank.hl_rank = 10;
		se(1);
		announce_show(g_menu.cursor); /* FUN_006536f0 */
	}
}

static void name_update(void) /* FUN_00658550 */
{
	if (g_rank.hl_rank >= 10) { /* not ranked: any key -> ranking view (no SE, no save) */
		if (min_confirm() || min_cancel())
			to_view();
		return;
	}
	aos_save *s = modes_save();
	int32_t *name = s->rank[g_rank.page][g_rank.hl_rank].name;
	if (min_confirm()) {
		g_rank.pos++;
		if (g_rank.pos > 6) {
			to_view();
			se(0);
			s->dirty = 1; /* FUN_00792bf0: game.rkg written (modes_save_poll) */
			return;
		}
		se(0);
	}
	int p = g_rank.pos > 6 ? 6 : g_rank.pos;
	if (min_cancel() && g_rank.pos > 0) {
		name[p] = RANK_BLANK; /* clears the current slot, then moves back */
		g_rank.pos--;
		se(1);
	}
	if (min_pressed(MI_LEFT) && g_rank.pos > 0) {
		g_rank.pos--;
		se(2);
	}
	if (min_pressed(MI_RIGHT) && g_rank.pos < 6) {
		g_rank.pos++;
		se(2);
	}
	p = g_rank.pos;
	if (min_pressed(MI_UP)) {
		name[p] = name[p] - 1 < 0 ? name[p] - 1 + RANK_LETTERS : name[p] - 1;
		se(2);
	}
	if (min_pressed(MI_DOWN)) {
		name[p] = name[p] + 1 > RANK_BLANK ? name[p] + 1 - RANK_LETTERS : name[p] + 1;
		se(2);
	}
}

static void unlock_update(void) /* FUN_00646ee0 */
{
	if (g_rank.pa < 1.0f || !min_confirm())
		return;
	g_rank.msg++;
	if (g_rank.msg < g_rank.nmsg) {
		g_rank.pb = 0;
		se(5);
		return;
	}
	modes_flow_msgs_clear(); /* port: shown once (the PC clears the list at the next run start) */
	if (g_setup.mode == MODE_STORY) {
		memset(&g_menu, 0, sizeof g_menu);
		g_menu.state = MS_TITLE;
		g_rank.page = 0;
		g_rank.hl_rank = 10;
		for (int k = 0; k < 10; k++)
			g_rank.v[k] = 0;
	} else {
		g_menu.state = MS_NAME_ENTRY;
		g_rank.page = g_rank.hl_diff;
		g_menu.a_band = 1;
		g_rank.b = 1;
		g_rank.pos = 0;
		if (g_rank.hl_rank < 10)
			announce_show(0x36);
	}
}

void ranking_update(void)
{
	switch (g_menu.state) {
	case MS_RANKING: view_update(); break;
	case MS_NAME_ENTRY: name_update(); break;
	case MS_UNLOCK: unlock_update(); break;
	}
}

static void approach(float *a, bool up)
{
	*a += up ? 0.05f : -0.05f;
	if (*a > 1)
		*a = 1;
	if (*a < 0)
		*a = 0;
}

void ranking_tail(void)
{
	if (g_menu.state == MS_LEAVING)
		return;
	approach(&g_rank.a, g_menu.state == MS_RANKING);
	approach(&g_rank.b, g_menu.state == MS_NAME_ENTRY);
	bool pop = g_menu.state == MS_UNLOCK && !scene_fade_busy();
	approach(&g_rank.pa, pop);
	approach(&g_rank.pb, pop);
	if (g_menu.state == MS_RANKING)
		for (int k = 0; k < 10; k++) {
			if (g_rank.v[k] > 0) {
				g_rank.v[k] -= 0.05f;
				if (g_rank.v[k] < 0)
					g_rank.v[k] = 0;
			} else if (g_rank.v[k] < 0) {
				g_rank.v[k] += 0.05f;
				if (g_rank.v[k] > 0)
					g_rank.v[k] = 0;
			}
		}
	if (g_menu.state == MS_NAME_ENTRY) {
		g_rank.ring += 0.025f; /* advanced inside the PC draw FUN_00658970 */
		if (g_rank.ring >= 1.0f)
			g_rank.ring -= 1.0f;
	}
}
