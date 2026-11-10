/* Part B scene logic (select.h; docs/re/modes.md §B). Portable: no libctru. */
#include "select.h"

#include <stdio.h>
#include <string.h>

#include "../sim/rng.h"
#include "audio.h"
#include "modes_flow.h"
#include "music.h"
#include "scene.h"

#define SE(id) audio_se((id), AUDIO_SE_CD_DEFAULT)

static void approach(float *v, bool up, float rate)
{
	if (up) {
		*v += rate;
		if (*v > 1.0f)
			*v = 1.0f;
	} else {
		*v -= rate;
		if (*v < 0.0f)
			*v = 0.0f;
	}
}

static void to_zero(float *v, float rate)
{
	if (*v > 0.0f) {
		*v -= rate;
		if (*v < 0.0f)
			*v = 0.0f;
	} else if (*v < 0.0f) {
		*v += rate;
		if (*v > 0.0f)
			*v = 0.0f;
	}
}

/* ================================================================ data */

/* modes_select_pools.tsv order (ArcSelect Init FUN_0079df00, VS Init FUN_0079acc0 0x79b0..) */
static const int pool_order[14] = {9, 8, 7, 6, 5, 4, 3, 10, 11, 12, 13, 14, 15, 16};

static bool char_open(const aos_save *sv, int id) { return !sv || (id >= 0 && id < AOS_SAVE_CHARS && sv->charOpen[id]); }

int sel_pool_arcade(int mode, const aos_save *sv, int out[SEL_POOL_MAX])
{
	int n = 0;
	for (int i = 0; i < 14; i++)
		if (char_open(sv, pool_order[i]))
			out[n++] = pool_order[i];
	if (mode == 0 && char_open(sv, 17)) /* Sumika only in arcade (DAT_0087faa8 == 0 && DAT_0087b73d) */
		out[n++] = 17;
	return n;
}

int sel_pool_vs(const aos_save *sv, int out[SEL_POOL_MAX])
{
	int n = 0;
	for (int i = 0; i < 14; i++)
		if (char_open(sv, pool_order[i]))
			out[n++] = pool_order[i];
	if (char_open(sv, 17))
		out[n++] = 17;
	out[n++] = SEL_RANDOM; /* 0x79b431 */
	return n;
}

int sel_stage_list(const aos_save *sv, int out[16])
{
	/* Select Init 0x79b4..: flags DAT_0087b73f + id (b744 = 5, b74c = 13, ...) */
	static const int order[15] = {5, 13, 12, 11, 9, 10, 8, 7, 6, 1, 15, 16, 17, 18, 19};
	int n = 0;
	for (int i = 0; i < 15; i++) {
		int id = order[i];
		if (id == 1 || !sv || (id < AOS_SAVE_STAGES && sv->stageOpen[id]))
			out[n++] = id;
	}
	return n;
}

int sel_index_of(const int *list, int n, int id)
{
	for (int i = 0; i < n; i++)
		if (list[i] == id)
			return i;
	return -1;
}

sel_stats sel_char_stats(int id)
{
	/* modes_select_charstats.tsv (FUN_007a49d0); id 18 / random: default branch, all S */
	static const signed char t[15][5] = {
		{0, 4, 1, 3, 1}, {0, 2, 4, 1, 1}, {1, 3, 2, 2, 2}, {2, 1, 2, 1, 4}, {2, 0, 2, 1, 0},
		{1, 1, 4, 3, 1}, {0, 3, 3, 1, 3}, {1, 3, 3, 3, 1}, {2, 4, 1, 2, 2}, {0, 1, 4, 2, 3},
		{2, 2, 3, 1, 3}, {1, 3, 3, 4, 1}, {1, 1, 3, 0, 4}, {1, 2, 4, 2, 2}, {2, 2, 3, 2, 2},
	};
	sel_stats s = {0, {4, 4, 4, 4}};
	if (id >= 3 && id <= 17) {
		s.diff = t[id - 3][0];
		for (int k = 0; k < 4; k++)
			s.rank[k] = t[id - 3][k + 1];
	}
	return s;
}

bool sel_has_sp(int id)
{
	switch (id) { /* FUN_007a2880 / FUN_007a2a20 (DAT_009b8e10..2c) */
	case 3: case 9: case 10: case 11: case 13: case 14: case 16: case 17: return true;
	default: return false;
	}
}

int sel_color_resolve(int id, int opt)
{
	if (opt == 1)
		return 0;
	if (opt == 2)
		return 1;
	if (opt == 3 && sel_has_sp(id))
		return 2;
	return 0; /* Auto -> 0 (the opponent-aware code at 0x7a290e is unreachable) */
}

const char *sel_char_key(int id)
{
	static const char *const k[19] = {
		[3] = "SORA", [4] = "ALTE", [5] = "TSIH", [6] = "MIRA", [7] = "SHAM", [8] = "NATH",
		[9] = "STARBREAKER", [10] = "SUGURI", [11] = "SAKI", [12] = "IRU", [13] = "NANAKO", [14] = "KAE",
		[15] = "KYOKO", [16] = "HIME", [17] = "SUMIKA", [18] = "SUMIKA",
	};
	return id >= 0 && id < 19 && k[id] ? k[id] : "";
}

int sel_sp_text(int id)
{
	switch (id) { /* FUN_007a6f90: 0x1e2 SORA, 0x1e3 SUGURI, 0x1e4 HIME, 0x1e5 SUMIKA, 0x1e6 KAE, 0x1e7 NANAKO, 0x1e8 SAKI, 0x1e9 STAR */
	case 3: return 0x1e2;
	case 10: return 0x1e3;
	case 16: return 0x1e4;
	case 17: return 0x1e5;
	case 14: return 0x1e6;
	case 13: return 0x1e7;
	case 11: return 0x1e8;
	case 9: return 0x1e9;
	default: return 0x1e1;
	}
}

/* ================================================================ tile wipes */

void sel_wipe_start(sel_wipe *w, int type)
{
	w->type = type;
	w->done = false;
	for (int r = 0; r < 15; r++)
		for (int c = 0; c < 20; c++) {
			w->a[r][c] = type >= 0x13 ? 1.0f : 0.0f;
			w->delay[r][c] = c + r;
		}
}

void sel_wipe_update(sel_wipe *w) /* FUN_00561c70, tile part */
{
	if (!w->type || w->done)
		return;
	bool done = true;
	for (int r = 0; r < 15; r++)
		for (int c = 0; c < 20; c++) {
			if (w->delay[r][c] >= 0) {
				w->delay[r][c]--;
				done = false;
				continue;
			}
			float *a = &w->a[r][c];
			if (w->type == 0x11) {
				if (*a < 1.0f) {
					*a += 0.02f; /* DAT_00813ed8 */
					if (*a >= 1.0f)
						*a = 1.0f;
					done = false;
				}
			} else if (*a > 0.0f) {
				*a -= 0.025f; /* DAT_00813edc */
				if (*a <= 0.0f)
					*a = 0;
				done = false;
			}
		}
	w->done = done;
}

/* ================================================================ ArcSelect (scene 8) */

arcsel_state g_arc;

void arcsel_init(void) /* FUN_0079df00 */
{
	arcsel_state *a = &g_arc;
	int keep = a->cursor;
	a->n = sel_pool_arcade(g_setup.mode == MODE_STORY ? 1 : 0, modes_save(), a->pool);
	a->sub = ARC_COMMAND;
	a->t_sub = a->frame = 0;
	a->cb8 = a->cd0 = a->cc0 = a->ca8 = a->d74 = 0;
	a->leave = 0;
	if (scene_prev() == SCENE_MENU) {
		int i = sel_index_of(a->pool, a->n, 10); /* Suguri */
		a->cursor = i < 0 ? 0 : i;
	} else {
		a->cursor = keep < a->n ? keep : 0;
	}
	announce_show(0x34); /* ANNOUNCE_SELECT_COMMAND */
	scene_fade(5);
	music_force(MUSIC_SELECT, true); /* FUN_004bee60(0x16,1) */
}

static void arcsel_exit(void) /* FUN_0079e4b0 */
{
	music_request(0, true, true);
	if (g_arc.sub == ARC_CONFIRMED)
		modes_flow_start(arcsel_char(), g_setup.command[0]); /* builder + stepper(0) -> scene */
	else
		scene_goto(SCENE_MENU);
}

void arcsel_update(void) /* FUN_0079e590 */
{
	arcsel_state *a = &g_arc;
	bool leaving = false;
	switch (a->sub) {
	case ARC_COMMAND:
		if (min_pressed(MI_UP)) {
			if (--a->cmd < 0)
				a->cmd = 1;
			SE(2);
		}
		if (min_pressed(MI_DOWN)) {
			if (++a->cmd > 1)
				a->cmd = 0;
			SE(2);
		}
		if (min_confirm()) {
			a->sub = ARC_CHAR;
			a->t_sub = 0;
			g_setup.command[0] = a->cmd != 0; /* DAT_0087fabc */
			SE(0);
			announce_show(0x2b); /* ANNOUNCE_CHARACTERSELECT_CHARACTER */
		} else if (min_cancel()) {
			a->sub = ARC_CANCELLED;
			a->t_sub = 0;
			scene_fade(1);
			SE(1);
			announce_hide();
		}
		break;
	case ARC_CHAR:
		if (min_pressed(MI_RIGHT)) { /* +0x00 Right: cursor -1 */
			if (--a->cursor < 0)
				a->cursor = a->n - 1;
			SE(2);
			a->d74 = 1.0f;
			a->cc0 = 0;
		}
		if (min_pressed(MI_LEFT)) {
			if (++a->cursor >= a->n)
				a->cursor = 0;
			SE(2);
			a->d74 = -1.0f;
			a->cc0 = 0;
		}
		g_setup.chara[0] = arcsel_char(); /* DAT_0087fac4 */
		if (min_confirm()) {
			a->sub = ARC_CONFIRMED;
			a->t_sub = 0;
			announce_hide();
			scene_fade(2);
			SE(0);
		} else if (min_cancel()) {
			a->sub = ARC_COMMAND;
			a->t_sub = 0;
			SE(1);
			announce_show(0x34);
		}
		break;
	default: /* 900 / 0x3b6: leave once the transition is done */
		if (!scene_fade_busy())
			leaving = true;
		break;
	}
	bool frozen = a->sub == ARC_CONFIRMED || a->sub == ARC_CANCELLED;
	if (a->sub == ARC_COMMAND)
		approach(&a->cb8, true, 0.05f);
	else if (!frozen || scene_fade_busy())
		approach(&a->cb8, false, 0.05f);
	if (a->sub == ARC_CHAR) {
		approach(&a->cd0, true, 0.05f);
		approach(&a->cc0, true, 0.1f);
		approach(&a->ca8, true, 0.1f);
	} else if (!frozen) {
		approach(&a->cd0, false, 0.05f);
		approach(&a->cc0, false, 0.05f);
		approach(&a->ca8, false, 0.05f);
	}
	to_zero(&a->d74, 0.1f);
	a->ring = (a->ring + 2) % 3600;
	a->frame++;
	a->t_sub++;
	if (leaving) {
		/* port: one "NOW LOADING" frame (Game Init FUN_005f95c0) before the next scene's Init */
		if (a->sub == ARC_CONFIRMED && a->leave++ == 0)
			return;
		arcsel_exit();
	}
}

/* ================================================================ VS Select (scene 6) */

vssel_state g_vs;
static bool vs_ever;

void vssel_init(void) /* FUN_0079acc0 */
{
	vssel_state *v = &g_vs;
	v->sub = VS_CHARS;
	v->t_sub = v->frame = 0;
	v->leave = 0;
	v->stage_swapped = false;
	bool from_menu = scene_prev() == SCENE_MENU || !vs_ever;
	vs_ever = true;
	if (from_menu) {
		for (int p = 0; p < 2; p++) {
			v->handicap[p] = 2;
			v->color[p] = 0;
			v->command[p] = 0;
			v->row[p] = 0;
		}
	}
	const aos_save *sv = modes_save();
	for (int p = 0; p < 2; p++) {
		v->en_hcp[p] = true;
		v->en_col[p] = true;
		v->en_cmd[p] = g_setup.input[p] != INPUT_CPU;
		if (g_setup.mode != MODE_TRAINING && g_setup.sub != SUB_ONLINE && g_config.tournament)
			v->en_hcp[p] = false;
	}
	v->ca0 = v->ca4 = v->ca8 = v->d7c = v->d80 = v->cbc = 0;
	for (int p = 0; p < 2; p++) {
		v->cc0[p] = v->cc8[p] = v->cd0[p] = v->cd8[p] = v->ce0[p] = v->d74[p] = 0;
		if (g_setup.input[p] < INPUT_CPU) {
			v->ctrl[p] = g_setup.input[p];
			v->slot[p] = SLOT_CHOOSE;
		} else {
			v->ctrl[p] = -1;
			v->slot[p] = SLOT_CPU;
		}
	}
	v->n = sel_pool_vs(sv, v->pool);
	v->nstages = sel_stage_list(sv, v->stages);
	if (from_menu) {
		int i = sel_index_of(v->pool, v->n, 10), j = sel_index_of(v->pool, v->n, 3);
		v->cursor[0] = i < 0 ? 0 : i;
		v->cursor[1] = j < 0 ? 0 : j;
		int k = sel_index_of(v->stages, v->nstages, 15); /* 0x79beb2 */
		v->stage_cur = k < 0 ? 0 : k;
		music_sel_set(0); /* index of 10 ("????", always first) */
	}
	for (int p = 0; p < 2; p++)
		if (v->cursor[p] >= v->n)
			v->cursor[p] = 0;
	if (v->stage_cur >= v->nstages)
		v->stage_cur = 0;
	for (int p = 0; p < 2; p++)
		v->chara[p] = g_setup.chara[p] = v->pool[v->cursor[p]];
	announce_show(0x2b);
	scene_fade(5);
	music_force(MUSIC_SELECT, true);
}

static void vs_exit(void) /* FUN_0079bfc0 */
{
	vssel_state *v = &g_vs;
	if (v->sub == VS_CONFIRMED) {
		for (int p = 0; p < 2; p++) {
			g_setup.command[p] = v->command[p];
			g_setup.color[p] = sel_color_resolve(v->chara[p], v->color[p]);
			g_setup.handicap[p] = v->handicap[p];
			g_setup.chara[p] = v->chara[p];
		}
		g_setup.stage = v->stage;
		g_setup.bgm = music_resolve(); /* FUN_007aaed0 */
		g_setup.new_match = true;
		scene_goto(SCENE_BATTLE);
	} else {
		scene_goto(SCENE_MENU);
	}
	music_request(0, true, true);
}

static int pick_random(const int *list, int n) /* FUN_007aaf90 / FUN_007ab0c0: list without 1, cosmetic RNG */
{
	int tmp[SEL_POOL_MAX], m = 0;
	for (int i = 0; i < n && m < SEL_POOL_MAX; i++)
		if (list[i] != 1)
			tmp[m++] = list[i];
	if (!m)
		return 0;
	uint32_t r = (uint32_t)aos_rand_i(modes_cosmetic(), 0, m * 10);
	return tmp[r % (uint32_t)m];
}

static void vs_options(int slot) /* FUN_007a2a20 (offline: rows 1..4) */
{
	vssel_state *v = &g_vs;
	int ch = v->pool[v->cursor[slot]];
	bool sp = sel_has_sp(ch);
	if (!sp && v->color[slot] == 3)
		v->color[slot] = 0;
	if (min_pressed(MI_UP)) {
		if (--v->row[slot] < 1)
			v->row[slot] = 4;
		announce_show(0x2d + v->row[slot]); /* FUN_007ab2a0 */
		SE(2);
	}
	if (min_pressed(MI_DOWN)) {
		if (++v->row[slot] > 4)
			v->row[slot] = 1;
		announce_show(0x2d + v->row[slot]);
		SE(2);
	}
	int d = min_pressed(MI_LEFT) ? -1 : min_pressed(MI_RIGHT) ? 1 : 0;
	if (!d)
		return;
	switch (v->row[slot]) {
	case ROW_HANDICAP:
		if (!v->en_hcp[slot]) {
			SE(3);
			break;
		}
		v->handicap[slot] = (v->handicap[slot] + d + 6) % 6;
		SE(4);
		break;
	case ROW_COLOR:
		if (!v->en_col[slot]) {
			SE(3);
			break;
		}
		{
			int nc = sp ? 4 : 3;
			v->color[slot] = (v->color[slot] + d + nc) % nc;
		}
		SE(4);
		break;
	case ROW_INPUT:
		if (!v->en_cmd[slot]) {
			SE(3);
			break;
		}
		v->command[slot] ^= 1;
		SE(4);
		break;
	default:
		break;
	}
}

static void vs_chars(void)
{
	vssel_state *v = &g_vs;
	for (int i = 0; i < 2; i++) { /* controllers (3DS: only 0 drives a slot) */
		if (v->ctrl[i] == -1)
			continue;
		int s = v->ctrl[i];
		if (v->slot[s] == SLOT_CHOOSE) {
			if (min_pressed(MI_UP)) {
				if (--v->cursor[s] < 0)
					v->cursor[s] = v->n - 1;
				v->d74[s] = -1.0f;
				v->cc0[s] = 0;
				SE(2);
			}
			if (min_pressed(MI_DOWN)) {
				if (++v->cursor[s] >= v->n)
					v->cursor[s] = 0;
				v->d74[s] = 1.0f;
				v->cc0[s] = 0;
				SE(2);
			}
			v->chara[s] = g_setup.chara[s] = v->pool[v->cursor[s]];
			if (min_confirm()) {
				v->slot[s] = SLOT_OPTIONS;
				v->row[s] = ROW_ACCEPT;
				SE(0);
				v->ce0[s] = 1.0f;
				break;
			}
			if (min_cancel()) {
				if (g_setup.sub == SUB_2P || g_setup.sub == SUB_ONLINE || i == s) {
					v->sub = VS_CANCELLED;
					v->t_sub = 0;
					announce_hide();
					scene_fade(1);
				} else { /* CPU mode while picking the CPU: back to P1's options */
					v->slot[s] = SLOT_CPU;
					v->ctrl[i] = v->ctrl[i] == 0 ? 1 : 0;
					v->slot[v->ctrl[i]] = SLOT_OPTIONS;
				}
				SE(1);
				break;
			}
		}
		if (v->slot[s] == SLOT_OPTIONS) {
			vs_options(s);
			if (min_confirm()) { /* SP colours of DLC characters: owned (port has the art) */
				v->slot[s] = SLOT_READY;
				v->ce0[s] = 1.0f;
				if (g_setup.sub != SUB_2P && g_setup.sub != SUB_ONLINE) {
					v->ctrl[i] = v->ctrl[i] == 0 ? 1 : 0;
					if (v->slot[v->ctrl[i]] != SLOT_READY)
						v->slot[v->ctrl[i]] = SLOT_CHOOSE;
				}
				SE(0);
				break;
			}
			if (min_cancel()) {
				v->slot[s] = SLOT_CHOOSE;
				SE(1);
				break;
			}
		}
		if (v->slot[s] == SLOT_READY && min_cancel()) {
			v->slot[s] = SLOT_OPTIONS;
			SE(1);
			break;
		}
	}
	if (v->slot[0] == SLOT_READY && v->slot[1] == SLOT_READY) {
		v->sub = VS_STAGE;
		v->t_sub = 0;
		v->ce0[0] = v->ce0[1] = 1.0f;
		announce_show(0x33); /* ANNOUNCE_CHARACTERSELECT_STAGE */
	}
}

static void vs_stage(void)
{
	vssel_state *v = &g_vs;
	if (min_pressed(MI_LEFT)) {
		if (--v->stage_cur < 0)
			v->stage_cur = v->nstages - 1;
		v->d7c = -1.0f;
		SE(2);
	}
	if (min_pressed(MI_RIGHT)) {
		if (++v->stage_cur >= v->nstages)
			v->stage_cur = 0;
		v->d7c = 1.0f;
		SE(2);
	}
	if (min_pressed(MI_UP))
		music_sel_step(-1); /* SE 2 inside */
	if (min_pressed(MI_DOWN))
		music_sel_step(1);
	if (min_confirm()) {
		v->sub = VS_WHITE;
		v->ca4 = 0;
		int id = v->stages[v->stage_cur];
		v->stage = id == 1 ? pick_random(v->stages, v->nstages) : id;
		SE(0);
		announce_hide();
		music_request(0, true, true);
	}
	if (min_cancel()) {
		v->sub = VS_CHARS;
		v->t_sub = 0;
		if (g_setup.sub == SUB_2P || g_setup.sub == SUB_ONLINE) {
			v->slot[0] = v->slot[1] = SLOT_OPTIONS;
		} else {
			v->ctrl[0] = 1;
			v->slot[v->ctrl[0]] = SLOT_OPTIONS;
		}
		SE(1);
		announce_show(0x2b);
	}
}

void vssel_update(void) /* FUN_0079c110 */
{
	vssel_state *v = &g_vs;
	bool leaving = false;
	switch (v->sub) {
	case VS_VS:
		v->ca4 -= 0.025f;
		if (v->ca4 <= 0)
			v->ca4 = 0;
		if (v->t_sub > 100) {
			v->sub = VS_CONFIRMED;
			scene_fade(2);
		}
		break;
	case VS_CHARS: vs_chars(); break;
	case VS_STAGE: vs_stage(); break;
	case VS_WHITE:
		v->ca4 += 0.01f;
		if (v->ca4 >= 1.0f) {
			v->stage_swapped = true; /* FUN_004bfca0(4) + FUN_004bfc10(stage): the draw side swaps */
			v->sub = VS_VS;
			v->t_sub = 0;
			v->ca4 = 1.0f;
			music_request(MUSIC_VERSUS, true, false); /* jingle 2 */
			for (int p = 0; p < 2; p++)
				if (v->chara[p] == SEL_RANDOM) {
					v->chara[p] = g_setup.chara[p] = pick_random(v->pool, v->n);
					v->ce0[p] = 1.0f;
					v->cc8[p] = 0;
				}
		}
		break;
	default:
		if ((v->sub == VS_CONFIRMED || v->sub == VS_CANCELLED) && !scene_fade_busy())
			leaving = true;
		break;
	}
	bool freeze = v->sub == VS_WHITE || v->sub == VS_VS || v->sub == VS_CONFIRMED || v->sub == VS_CANCELLED;
	for (int p = 0; p < 2; p++) {
		if (v->slot[p] == SLOT_CHOOSE)
			approach(&v->cd0[p], true, 0.1f);
		else if (!freeze)
			approach(&v->cd0[p], false, 0.1f);
		to_zero(&v->d74[p], 0.1f);
		bool opt = v->slot[p] == SLOT_OPTIONS || v->slot[p] == SLOT_READY;
		if (opt) {
			approach(&v->cd8[p], true, 0.1f);
			approach(&v->cc8[p], true, 0.1f);
		} else if (!freeze) {
			approach(&v->cd8[p], false, 0.1f);
			approach(&v->cc8[p], false, 0.1f);
		}
		if (v->slot[p] == SLOT_CPU) {
			if (!freeze)
				approach(&v->cc0[p], false, 0.1f);
		} else {
			approach(&v->cc0[p], true, 0.1f);
		}
		approach(&v->ce0[p], false, 0.025f);
		v->ring[p] = (v->ring[p] + 2) % 3600;
	}
	if (v->sub < 400)
		approach(&v->ca8, true, 0.1f);
	else if (v->sub != VS_CONFIRMED && v->sub != VS_CANCELLED)
		approach(&v->ca8, false, 0.1f);
	approach(&v->ca0, v->sub == VS_STAGE, 0.1f);
	to_zero(&v->d7c, 0.1f);
	if (v->sub == VS_STAGE)
		approach(&v->d80, true, 0.1f);
	else if (!freeze)
		approach(&v->d80, false, 0.1f);
	if (v->sub < 400)
		approach(&v->cbc, true, 0.025f);
	else if (v->sub != VS_CONFIRMED && v->sub != VS_CANCELLED)
		approach(&v->cbc, false, 0.05f);
	v->frame++;
	v->t_sub++;
	if (leaving) {
		if (v->sub == VS_CONFIRMED && v->leave++ == 0)
			return; /* port: one "NOW LOADING" frame */
		vs_exit();
	}
}

/* ================================================================ Result (scene 3) */

result_state g_res;

int result_rank(const modes_result *r, int n) /* FUN_00617740 */
{
	if (n < 1)
		n = 1;
	float tf, hf;
	int sec = r->seconds;
	if (sec < n * 50)
		tf = 1.0f;
	else if (sec < n * 70)
		tf = 0.8f;
	else if (sec < n * 80)
		tf = 0.6f;
	else if (sec < n * 100)
		tf = 0.4f;
	else if (sec < n * 120)
		tf = 0.2f;
	else
		tf = 0.1f;
	float f = (float)r->hp / ((float)n * 10000.0f);
	if (f == 1.0f)
		hf = 1.0f;
	else if (f >= 0.8f)
		hf = 0.8f;
	else if (f >= 0.6f)
		hf = 0.6f;
	else if (f >= 0.4f)
		hf = 0.4f;
	else if (f >= 0.2f)
		hf = 0.2f;
	else
		hf = 0.1f;
	if (hf == 1.0f && tf >= 0.8f)
		return 0;
	volatile float sum = hf + tf; /* SSE single precision */
	if (sum >= 1.7f)
		return 0;
	if (sum >= 1.5f)
		return 1;
	if (sum >= 1.2f)
		return 2;
	if (sum >= 0.8f)
		return 3;
	return 4;
}

int result_quote_pick(int w, int l, int roll) /* FUN_00617110: X quote vs listed rivals overrides the roll */
{
	static const signed char x[19][3] = {
		[3] = {10, 16, 7}, [4] = {7, 15, 0}, [5] = {6, 14, 0}, [6] = {4, 13, 0}, [7] = {5, 11, 3},
		[8] = {9, 12, 0}, [9] = {8, 10, 0}, [10] = {16, 3, 9}, [11] = {12, 7, 0}, [12] = {13, 8, 0},
		[13] = {14, 6, 0}, [14] = {15, 5, 0}, [15] = {11, 4, 0}, [16] = {10, 3, 0},
	};
	if (w < 3 || w > 18)
		return -1;
	int q = roll % 3;
	for (int k = 0; k < 3; k++)
		if (x[w][k] && l == x[w][k])
			q = 10 + k;
	return q;
}

const char *result_quote_key(int chara, int quote, char *buf, int n)
{
	if (quote < 0 || !*sel_char_key(chara)) {
		buf[0] = 0;
		return buf;
	}
	if (quote >= 10)
		snprintf(buf, (size_t)n, "WIN_%s_X%02d", sel_char_key(chara), quote - 9);
	else
		snprintf(buf, (size_t)n, "WIN_%s_%02d", sel_char_key(chara), quote + 1);
	return buf;
}

void result_capture(const aos_world *w)
{
	result_state *r = &g_res;
	r->winner = aos_hp_winner(w);
	for (int i = 0; i < 2; i++) {
		r->chara[i] = w->p[i].id;
		r->color[i] = w->p[i].color;
		r->input[i] = g_setup.input[i];
	}
	/* FUN_00617110(winner, -1): roll on the cosmetic MT first (Game Exit order) */
	int wc = r->chara[r->winner], lc = r->chara[!r->winner];
	r->quote = wc >= 3 && wc <= 18 ? result_quote_pick(wc, lc, aos_rand_i(modes_cosmetic(), 0, 100)) : -1;
	modes_match_result(w, &r->r);
	r->rounds = w->flow.roundIndex + 1;
	r->rank = result_rank(&r->r, r->rounds);
	r->captured = true;
}

void result_init(void) /* FUN_00796030 */
{
	result_state *r = &g_res;
	bool ladder = g_setup.mode == MODE_ARCADE || g_setup.mode == MODE_STORY;
	r->sub = ladder && r->winner == 1 ? RES_LOST : RES_WON;
	r->t = r->e4 = 0;
	r->game_over = r->cont = false;
	r->slide = 50;
	r->row[0] = 0;
	r->row[1] = -0.5f;
	r->row[2] = -1.0f;
	r->row[3] = -1.5f;
	r->band = r->rank_a = r->cont_a = r->scroll = 0;
	r->count = 9;
	r->leave = 0;
	sel_wipe_start(&r->wipe, 0x13);
	announce_hide();
	music_force(MUSIC_RESULT, false); /* FUN_004bee60(1,0) */
}

float result_letter_alpha(int i)
{
	/* per-letter floats start at -0.2*i (0x796531), +0.1/frame once t > 60 */
	int k = g_res.t - 60;
	if (k <= 0)
		return 0;
	float a = -0.2f * (float)i + 0.1f * (float)k;
	return a < 0 ? 0 : a > 1 ? 1 : a;
}

void result_update(void) /* FUN_00796ef0 */
{
	result_state *r = &g_res;
	bool leaving = false;
	sel_wipe_update(&r->wipe);
	bool ok = min_pressed_nr(MI_OK) || min_pressed_nr(MI_START), cancel = min_pressed_nr(MI_CANCEL);
	if (r->sub == RES_CONTINUE) {
		if (r->e4 % 100 == 99) {
			r->count--;
			SE(2);
		}
		if (cancel) {
			r->count--;
			SE(2);
		}
		if (r->count < 0) {
			r->sub = RES_LEAVING;
			r->game_over = true;
			sel_wipe_start(&r->wipe, 0x11);
			announce_hide();
		}
		if (ok) {
			r->sub = RES_LEAVING;
			r->cont = true;
			sel_wipe_start(&r->wipe, 0x11);
			SE(0);
			announce_hide();
		}
	} else if (r->sub == RES_WON || r->sub == RES_LOST) {
		/* t == 350: ANNOUNCE 0x38 "press Hyper to save a replay" — no replays on 3DS */
		if (ok && r->t <= 199) {
			r->t = 200;
		} else if (ok) {
			announce_hide();
			music_request(0, true, true);
			if (r->sub == RES_LOST) {
				r->sub = RES_CONTINUE;
				r->e4 = 0;
				r->count = 9;
				announce_show(0x3d); /* ANNOUNCE_RESULT_CONTINUE */
			} else {
				r->sub = RES_LEAVING;
				sel_wipe_start(&r->wipe, 0x11);
			}
		}
	} else if (r->sub == RES_LEAVING && r->wipe.done) {
		leaving = true;
	}
	if (r->slide > 0)
		r->slide--;
	if (r->sub == RES_WON) {
		if (r->t > 200)
			for (int k = 0; k < 4; k++) {
				r->row[k] += 0.05f;
				if (r->row[k] >= 1.0f)
					r->row[k] = 1.0f;
			}
		if (r->t == 300)
			SE(5);
		if (r->t > 300)
			approach(&r->rank_a, true, 0.025f);
	}
	if (r->t > 50)
		approach(&r->band, true, 0.05f);
	r->scroll += 0.02f / 0.4f; /* DAT_00813ed8 / s */
	if (r->scroll > 0.5f)
		r->scroll -= 1.0f;
	if (r->sub == RES_CONTINUE)
		approach(&r->cont_a, true, 0.05f);
	r->t++;
	r->e4++;
	if (leaving) {
		if (r->leave++ == 0)
			return; /* port: one "NOW LOADING" frame before the next scene */
		modes_flow_result_exit(r->game_over, r->cont); /* FUN_00796ca0 */
	}
}

/* ================================================================ pause menu */

pause_state g_pause;

void pause_reset(void) { memset(&g_pause, 0, sizeof g_pause); }

int pause_update(const aos_world *w)
{
	pause_state *p = &g_pause;
	if (!p->on) {
		/* FUN_0068ce30: not after the match was decided, not in the first 200 frames of a round,
		 * not during the round-end banner; Pause = START. The tick of this frame still runs. */
		if (!w->matchFinished && w->frame > 199 && !w->roundEndBanner && min_pressed_nr(MI_START)) {
			p->on = true;
			p->st = 10;
			p->t = 0;
			p->count = 0x5b;
			p->a = p->a_row = p->a_cmd = 0;
			p->cur = 0;
			p->exiting = 0;
			p->fading = false;
		}
		return PAUSE_OFF;
	}
	/* FUN_0068f4a0, offline branch (items 0 Resume, 1 Command list, 2 Retry, 3 Quit) */
	if (p->st == 900) {
		if (p->exiting) {
			if (!p->fading) {
				p->fading = true;
			} else if (!scene_fade_busy()) {
				return p->exiting;
			}
		} else if (p->a == 0.0f) {
			if (w->frame < 0xc9) {
				p->on = false;
			} else {
				if (p->count == 0x5b || p->count == 0x3c || p->count == 0x1e)
					SE(4);
				if (--p->count == 0) {
					p->count = 0x5b;
					SE(0);
					p->on = false;
				}
			}
		}
	} else if (p->st == 200) {
		/* port: the bottom-screen list scrolls (select_draw.c pause_draw_bottom) */
		if (min_pressed(MI_UP) && p->scroll > 0) {
			p->scroll--;
			SE(2);
		}
		if (min_pressed(MI_DOWN) && p->scroll < p->scroll_max) {
			p->scroll++;
			SE(2);
		}
		if (min_confirm() || min_cancel()) {
			SE(1);
			announce_hide();
			p->st = 900;
		}
	} else if (p->st == 10) {
		if (p->a == 1.0f)
			p->st = 100;
	} else if (p->st == 100) {
		if (min_pressed(MI_UP)) {
			if (--p->cur < 0)
				p->cur = 3;
			SE(2);
		}
		if (min_pressed(MI_DOWN)) {
			if (++p->cur > 3)
				p->cur = 0;
			SE(2);
		}
		if (min_pressed_nr(MI_START) || min_cancel()) {
			SE(1);
			p->st = 900;
			announce_hide();
		} else if (min_confirm()) {
			bool keep = false;
			switch (p->cur) {
			case 0: SE(1); break;
			case 1:
				SE(0);
				p->st = 200;
				p->scroll = 0;
				keep = true;
				announce_show(0x66); /* ANNOUNCE_PAUSE_COMMANDLIST_VIEWING */
				break;
			default: /* 2 Retry (DAT_00885970 = 3), 3 Quit (= 2) */
				SE(0);
				p->exiting = p->cur == 2 ? PAUSE_RETRY : PAUSE_QUIT;
				music_request(0, true, true);
				scene_fade(3);
				break;
			}
			if (!keep) {
				p->st = 900;
				announce_hide();
			}
		}
	}
	p->t++;
	approach(&p->a_row, p->st == 10 || p->st == 100, 0.075f);
	if (p->st == 10 || p->st == 100 || p->st == 200)
		approach(&p->a, true, 0.075f);
	else
		approach(&p->a, false, 0.075f);
	approach(&p->a_cmd, p->st == 200, 0.075f);
	return PAUSE_ON;
}
