/* Score, ranking, unlocks (docs/re/modes.md §D.2, §D.4.4, §D.5). */
#include "score.h"

#include <string.h>

void modes_match_result(const aos_world *w, modes_result *out) /* FUN_005f5780 0x5f5885..0x5f5b68 */
{
	memset(out, 0, sizeof *out);
	out->winner = aos_hp_winner(w);
	for (int i = 0; i < w->nRoundRecs; i++) { /* vector 0x885a74 + winner*0xc */
		const aos_round_rec *r = &w->roundRec[i][out->winner];
		out->seconds += r->seconds;
		if (out->hits < r->comboHits)
			out->hits = r->comboHits;
		out->hp += r->hp;
		out->damage += r->comboDmg;
		out->bonus += r->timeBonus;
		out->score += r->score;
	}
}

int modes_rank_insert(aos_save *s, int difficulty, int32_t score, int32_t character) /* FUN_00792f40 */
{
	if (difficulty < 0 || difficulty > 2) /* port guard: the PC indexes out of the table */
		return 10;
	aos_rank_entry *t = s->rank[difficulty];
	int i = 0;
	for (;; i++) {
		if (9 < i)
			return 10;
		if (t[i].score < score)
			break;
	}
	for (int k = 9; i <= k; k--) /* k = i reads t[i-1]; at i = 0 the PC reads the entry before the table */
		if (k > 0)
			t[k] = t[k - 1];
	t[i].score = score;
	t[i].character = character;
	for (int k = 0; k < AOS_RANK_NAME; k++)
		t[i].name[k] = 0x2d;
	s->dirty = 1; /* game.rkg is written by the name entry / menu (FUN_00792bf0) */
	return i;
}

void modes_rank_set_name(aos_save *s, int difficulty, int pos, const int32_t name[AOS_RANK_NAME])
{
	if (difficulty < 0 || difficulty > 2 || pos < 0 || pos >= AOS_RANK_ENTRIES)
		return;
	memcpy(s->rank[difficulty][pos].name, name, sizeof s->rank[difficulty][pos].name);
	s->dirty = 1;
}

static void push(modes_msgs *m, int32_t id) /* FUN_004aa120 */
{
	if (m && m->n < MODES_MAX_MSGS)
		m->id[m->n++] = id;
}

/* chars lo..hi-1 with any arcade-clear flag (DAT_0087b780 / b793 / b7a6) */
static int arcade_count(const aos_save *s, int lo, int hi)
{
	int n = 0;
	for (int c = lo; c < hi; c++)
		if (s->arcadeClear[0][c] == 1 || s->arcadeClear[1][c] == 1 || s->arcadeClear[2][c] == 1)
			n++;
	return n;
}

static int story_count(const aos_save *s)
{
	int n = 0;
	for (int c = 3; c < 0x11; c++)
		if (s->storyClear[c] == 1)
			n++;
	return n;
}

static void all_stage_bgm(aos_save *s) /* tail of FUN_004a8390 / FUN_007c3e80 */
{
	if (s->stageOpen[0x11] && s->stageOpen[0xc] && s->stageOpen[0x12] && s->stageOpen[0xd] && s->stageOpen[0x10] &&
	    s->stageOpen[0x13] && s->bgmOpen[0xe] && s->bgmOpen[0x14] && s->bgmOpen[0x13] && s->bgmOpen[0x15])
		aos_save_achieve(s, 0x10); /* ACH_UNLOCK_STAGEBGM */
}

void modes_unlocks_arcade(aos_save *s, modes_msgs *msgs) /* FUN_004a8390 */
{
	if (msgs)
		msgs->n = 0; /* FUN_004a9eb0 */
	if (s->charOpen[16] == 0) { /* Hime: arcade clear with any of 10..15 at any difficulty, or wins > 29 */
		int ok = 0;
		for (int d = 0; d < 3; d++)
			for (int c = 10; c <= 15; c++)
				if (s->arcadeClear[d][c] == 1)
					ok = 1;
		if (0x1d < s->wins)
			ok = 1;
		if (ok) {
			s->charOpen[16] = 1;
			push(msgs, 0x3f8);
		}
	}
	if (s->charOpen[9] == 0) { /* Star Breaker: any of 3..8, or wins > 59 */
		int ok = 0;
		for (int d = 0; d < 3; d++)
			for (int c = 3; c <= 8; c++)
				if (s->arcadeClear[d][c] == 1)
					ok = 1;
		if (0x3b < s->wins)
			ok = 1;
		if (ok) {
			s->charOpen[9] = 1;
			push(msgs, 0x3f1);
		}
	}
	static const struct { int8_t stage, bgm, lo, hi, need, wins; int16_t msg; } rule[] = {
		{0x11, -1, 10, 0x11, 2, 0x0e, 0x7e1}, /* stage 17 Guardian's Chamber */
		{0x0c, -1, 3, 10, 2, 0x1d, 0x7dc},    /* stage 12 Capital in Flames */
		{0x12, -1, 10, 0x11, 4, 0x2c, 0x7e2}, /* stage 18 Moonlight Dance Hall */
		{0x0d, -1, 3, 10, 4, 0x3b, 0x7dd},    /* stage 13 Whirlpool of Malice */
		{-1, 0x0e, 3, 0x11, 5, 0x59, 0xbc6},  /* BGM 14 */
		{-1, 0x14, 3, 0x11, 10, 0x77, 0xbcc}, /* BGM 20 */
	};
	for (size_t k = 0; k < sizeof rule / sizeof rule[0]; k++) {
		uint8_t *flag = rule[k].stage >= 0 ? &s->stageOpen[rule[k].stage] : &s->bgmOpen[rule[k].bgm];
		if (*flag)
			continue;
		int n = arcade_count(s, rule[k].lo, rule[k].hi);
		if (rule[k].wins < s->wins)
			n = 100;
		if (rule[k].need <= n) {
			*flag = 1;
			push(msgs, rule[k].msg);
		}
	}
	all_stage_bgm(s);
	s->dirty = 1; /* FUN_007c47f0 */
}

void modes_unlocks_story(aos_save *s, modes_msgs *msgs, int gameOver, int difficulty, int32_t character)
{ /* FUN_007c3e80 */
	if (!gameOver && 0 < difficulty) {
		/* ACH_STORY_*: chars 3..9 -> 8..14, 10..16 -> 1..7 */
		if (character >= 3 && character <= 9)
			aos_save_achieve(s, character + 5);
		else if (character >= 10 && character <= 16)
			aos_save_achieve(s, character - 9);
	}
	int n = story_count(s);
	if (s->charOpen[17] == 0 && ((0xff < s->wins && 0 < n) || 0xd < n)) {
		s->charOpen[17] = 1;
		push(msgs, 0x3f9);
		aos_save_achieve(s, 0xf); /* ACH_UNLOCK_SUMIKA */
	}
	static const struct { int8_t stage, bgm, need; int16_t msg; } rule[] = {
		{0x10, -1, 3, 0x7e0},  /* stage 16 Crashed Spaceship */
		{0x13, -1, 5, 0x7e3},  /* stage 19 Sumika's Hideout */
		{-1, 0x13, 7, 0xbcb},  /* BGM 19 */
		{-1, 0x15, 10, 0xbcd}, /* BGM 21 */
	};
	for (size_t k = 0; k < sizeof rule / sizeof rule[0]; k++) {
		uint8_t *flag = rule[k].stage >= 0 ? &s->stageOpen[rule[k].stage] : &s->bgmOpen[rule[k].bgm];
		if (*flag == 0 && rule[k].need <= n) {
			*flag = 1;
			push(msgs, rule[k].msg);
		}
	}
	all_stage_bgm(s);
	s->dirty = 1; /* FUN_007c47f0 */
}

void modes_record_clear(aos_save *s, int mode, int difficulty, int32_t character, int32_t continues)
{ /* FUN_00519f60 */
	if (continues == 0) {
		if (mode == 0 && difficulty >= 0 && difficulty < 3 && character >= 0 && character < AOS_SAVE_CHARS) {
			s->arcadeClears[difficulty]++;
			s->arcadeClear[difficulty][character] = 1;
		}
		if (mode == 1 && character >= 0 && character < AOS_SAVE_CHARS) {
			s->storyClears++;
			s->storyClear[character] = 1;
		}
		aos_save_achieve(s, 0x11); /* ACH_NOCONTINUE */
	}
	s->dirty = 1; /* FUN_007c47f0 */
}
