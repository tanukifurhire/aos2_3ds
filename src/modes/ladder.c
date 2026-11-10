/* Arcade / story ladders (docs/re/modes.md §0.3, §D.1). Table: docs/re/modes_ladders.tsv. */
#include "ladder.h"

#include <string.h>

static void push(ladder_run *r, int32_t type, int32_t a, int32_t b, int32_t stage, int32_t bgm) /* FUN_004aa150 */
{
	if (r->nsteps >= LADDER_MAX_STEPS)
		return;
	ladder_entry *e = &r->step[r->nsteps++];
	memset(e, 0, sizeof *e); /* _memset(&local_24, 0, 0x1c): colours 0 */
	e->type = type;
	e->a = a;
	e->b = b;
	e->stage = stage;
	e->bgm = bgm;
}

static void pool_push(ladder_run *r, int32_t c) /* FUN_004aa170 */
{
	if (r->npool < LADDER_MAX_POOL)
		r->pool[r->npool++] = c;
}

static void build_arcade(ladder_run *r, int32_t c) /* FUN_004a7cc0 */
{
	if (c < 3 || c > 17)
		return;
	static const int32_t pool[] = {4, 5, 6, 7, 8, 0xb, 0xc, 0xd, 0xe, 0xf};
	for (size_t i = 0; i < sizeof pool / sizeof pool[0]; i++)
		if (c != pool[i])
			pool_push(r, pool[i]);
	static const int32_t slot[6][2] = {{0xf, 0x10}, {6, 0xc}, {7, 0xd}, {8, 0xb}, {9, 0x11}, {0xb, 0xf}}; /* stage, BGM */
	for (int i = 0; i < 6; i++) {
		push(r, LADDER_FIGHT, c, LADDER_RANDOM, slot[i][0], slot[i][1]);
		push(r, LADDER_RESULT, 0, 0, 0, 0);
	}
	int32_t boss, stage;
	if (c <= 9) { /* Sora..Star Breaker: Star Breaker on Capital in Flames (Sora for Star Breaker) */
		boss = c == 9 ? 3 : 9;
		stage = 0xc;
	} else { /* Suguri..Sumika: Hime on Guardian's Chamber (Suguri for Hime / Sumika) */
		boss = (c == 16 || c == 17) ? 10 : 16;
		stage = 0x11;
	}
	push(r, LADDER_FIGHT, c, boss, stage, 0xe);
	push(r, LADDER_RESULT, 0, 0, 0, 0);
	push(r, LADDER_ENDING, 0, 0, 0, 0);
}

/* FUN_007befa0 per character: random pool (push order), fixed first opponent (0 = random), rival. */
static const struct {
	int8_t pool[6];
	int8_t first, rival;
} story_tab[14] = {
	/* 3 Sora   */ {{4, 5, 6, 8, 9}, 7, 16},
	/* 4 Alte   */ {{12, 11, 13, 14, 16}, 0, 15},
	/* 5 Tsih   */ {{12, 11, 13, 15, 16}, 0, 14},
	/* 6 Mira   */ {{12, 11, 14, 15, 16}, 0, 13},
	/* 7 Sham   */ {{12, 13, 14, 15, 16}, 3, 11},
	/* 8 Nath   */ {{11, 13, 14, 15, 16}, 0, 12},
	/* 9 SB     */ {{12, 11, 13, 14, 15, 16}, 0, 10},
	/* 10 Suguri*/ {{4, 5, 7, 8}, 6, 9},
	/* 11 Saki  */ {{4, 6, 8, 9}, 5, 7},
	/* 12 Iru   */ {{5, 6, 7, 9}, 4, 8},
	/* 13 Nanako*/ {{4, 5, 7, 8, 9}, 14, 6},
	/* 14 Kae   */ {{4, 6, 7, 8, 9}, 13, 5},
	/* 15 Kyoko */ {{5, 6, 7, 9}, 8, 4},
	/* 16 Hime  */ {{4, 5, 6, 7, 8}, 9, 3},
};

static void build_story(ladder_run *r, int32_t c) /* FUN_007befa0 */
{
	if (c < 3 || c > 16)
		return;
	const int k = c - 3;
	for (int i = 0; i < 6 && story_tab[k].pool[i]; i++)
		pool_push(r, story_tab[k].pool[i]);
	const int32_t script = (c - 2) * 10; /* script 10k+n (k = c - 2), resource TEXT_CONVERSATION%03d */
	push(r, LADDER_CONV, script, 0xf, 0, 0);
	static const int32_t bgm[5] = {0x10, 0xc, 0xd, 0xb, 0x11};
	for (int i = 0; i < 5; i++) { /* all five on stage 15 (0x7bf122) */
		int32_t opp = i == 0 && story_tab[k].first ? story_tab[k].first : LADDER_RANDOM;
		push(r, LADDER_FIGHT, c, opp, 0xf, bgm[i]);
		push(r, LADDER_RESULT, 0, 0, 0, 0);
	}
	push(r, LADDER_CONV, script + 1, 0x10, 0, 0);
	push(r, LADDER_FIGHT, c, story_tab[k].rival, 0x10, 0x12);
	push(r, LADDER_RESULT, 0, 0, 0, 0);
	push(r, LADDER_CONV, script + 2, 0x13, 0, 0);
	push(r, LADDER_FIGHT, c, AOS_CH_SUMIKA2, 0x13, 0x13);
	push(r, LADDER_RESULT, 0, 0, 0, 0);
	push(r, LADDER_CONV, script + 3, 0x13, 0, 0);
	push(r, LADDER_ENDING, 0, 0, 0, 0);
}

void ladder_build(ladder_run *r, int mode, int32_t character, int32_t difficulty)
{
	memset(r, 0, sizeof *r);
	r->mode = mode;
	r->difficulty = difficulty;
	r->index = 0;         /* DAT_008940ac */
	r->player = character; /* DAT_008940a8 */
	r->continues = 0;     /* DAT_008940c8 */
	r->score = 0;         /* DAT_008940cc */
	r->rank = 10;         /* DAT_008940d0 */
	r->msgs.n = 0;        /* FUN_004a9eb0 */
	if (mode == LADDER_STORY)
		build_story(r, character);
	else
		build_arcade(r, character);
}

int32_t ladder_char_bgm(int32_t c)
{
	(void)c;
	return 0;
}

/* ladder finished: stepper else-branch (and Result Exit game over, gameOver = 1) */
static void run_end(ladder_run *r, int gameOver, aos_save *save, ladder_out *out)
{
	out->scene = SCENE_MENU;
	out->finished = 1;
	if (r->mode == LADDER_STORY) {
		r->rank = 0xb;
		if (save)
			modes_unlocks_story(save, &r->msgs, gameOver, r->difficulty, r->player); /* FUN_007c3e80 */
	} else {
		r->rank = save ? modes_rank_insert(save, r->difficulty, r->score, r->setup.p1) : 10; /* FUN_00792f40 */
		if (save)
			modes_unlocks_arcade(save, &r->msgs); /* FUN_004a8390 */
	}
}

void ladder_step(ladder_run *r, int param, aos_mt *cosmetic, aos_save *save, ladder_out *out)
{ /* FUN_004a7910 / FUN_007bec90 */
	memset(out, 0, sizeof *out);
	if (!(r->index < r->nsteps)) {
		run_end(r, 0, save, out);
		return;
	}
	if (param == 3) {
		r->continues++;
		r->score = 0;
		r->index--;
	}
	if (param == 1 && r->winner == 1) {
		out->scene = SCENE_RESULT; /* lost: Result in continue mode, cursor not advanced */
		out->lost = 1;
		return;
	}
	ladder_entry *e = &r->step[r->index];
	switch (e->type) {
	case LADDER_FIGHT: {
		ladder_setup *g = &r->setup;
		g->p1 = e->a;
		g->colP1 = e->colP1;
		g->p2 = e->b;
		g->colP2 = e->colP2;
		if (g->p2 == LADDER_RANDOM && r->npool > 0) { /* FUN_00459f80 on the cosmetic MT 0x875b68
		                                                * (port guard: an empty pool never happens) */
			int32_t n = r->npool;
			uint32_t v = (uint32_t)aos_rand_i(cosmetic, 0, n * 10);
			uint32_t i = v % (uint32_t)n;
			g->p2 = r->pool[i];
			memmove(&r->pool[i], &r->pool[i + 1], sizeof r->pool[0] * (size_t)(n - 1 - (int32_t)i)); /* erase */
			r->npool--;
			e->b = g->p2; /* written back: a retry/continue refights the same opponent */
		}
		g->bgm = e->bgm;
		if (g->bgm == 0)
			g->bgm = ladder_char_bgm(g->p2);
		g->stage = e->stage;
		if (r->mode == LADDER_ARCADE) { /* boss on the secret stage when no continue was used */
			if (g->stage == 0xc && r->continues == 0) {
				e->stage = 0xd;
				g->stage = e->stage;
			}
			if (g->stage == 0x11 && r->continues == 0) {
				e->stage = 0x12;
				g->stage = e->stage;
			}
		}
		out->scene = SCENE_BATTLE;
		out->newMatch = 1;
		break;
	}
	case LADDER_CONV:
		out->scriptId = e->a;  /* FUN_004e35e0 */
		out->backdrop = e->b;  /* FUN_004bfc10 */
		out->scene = SCENE_CONVERSATION;
		break;
	case LADDER_RESULT:
		out->scene = SCENE_RESULT;
		break;
	case LADDER_ENDING:
		out->scene = SCENE_ENDROLL;
		break;
	}
	r->index++;
}

void ladder_start(ladder_run *r, int mode, int32_t character, int32_t difficulty, aos_mt *cosmetic, aos_save *save,
                  ladder_out *out)
{ /* FUN_0079e4b0, state 900 */
	ladder_build(r, mode, character, difficulty);
	ladder_step(r, 0, cosmetic, save, out);
}

void ladder_game_exit(ladder_run *r, int how, const aos_world *w, aos_mt *cosmetic, aos_save *save, ladder_out *out,
                      modes_result *result)
{ /* FUN_005f5780, DAT_0087faa8 0/1 */
	if (how == LADDER_EXIT_DECIDED) {
		modes_result res;
		modes_match_result(w, &res);
		r->winner = res.winner; /* DAT_009b6930 */
		if (r->mode == LADDER_ARCADE && r->winner == 0)
			r->score += res.score;
		if (result)
			*result = res;
		ladder_step(r, 1, cosmetic, save, out);
	} else if (how == LADDER_EXIT_RETRY) {
		ladder_step(r, 3, cosmetic, save, out);
	} else {
		memset(out, 0, sizeof *out);
		out->scene = SCENE_ARCSELECT; /* pause Quit: run abandoned */
	}
}

void ladder_result_exit(ladder_run *r, int gameOver, int cont, aos_mt *cosmetic, aos_save *save, ladder_out *out)
{ /* FUN_00796ca0 */
	if (gameOver) {
		memset(out, 0, sizeof *out);
		run_end(r, 1, save, out);
	} else if (cont) {
		r->continues++;
		r->score = 0;
		r->index--;
		ladder_step(r, 0, cosmetic, save, out);
	} else {
		if (save) {
			save->wins++; /* DAT_0087b76c */
			save->dirty = 1; /* FUN_007c47f0 */
		}
		ladder_step(r, 0, cosmetic, save, out);
	}
}

void ladder_endroll_exit(ladder_run *r, aos_mt *cosmetic, aos_save *save, ladder_out *out)
{ /* FUN_00519f60 */
	if (save)
		modes_record_clear(save, r->mode, r->difficulty, r->player, r->continues);
	ladder_step(r, 0, cosmetic, save, out);
}

int32_t ladder_round_seconds(int mode, int32_t stage) /* FUN_005f4650 l.390-398 */
{
	return (mode == LADDER_STORY && stage == 0x13) ? 999 : 180;
}

void ladder_match_params(const ladder_run *r, int32_t p1Scheme, aos_match_params *m)
{ /* Menu Exit FUN_00642880 case 0/1 + stepper globals + Game Init */
	aos_match_params_default(m);
	m->ids[0] = r->setup.p1;
	m->ids[1] = r->setup.p2;
	m->colors[0] = r->setup.colP1;
	m->colors[1] = r->setup.colP2;
	m->schemes[0] = p1Scheme;
	m->schemes[1] = 0;
	m->handicap[0] = m->handicap[1] = AOS_HANDICAP_DEFAULT; /* fad4 = fad8 = 2 */
	m->inputSrc[0] = 0;
	m->inputSrc[1] = 10; /* fab8 = 10: CPU */
	m->difficulty = r->difficulty;
	m->roundsToWin = 1; /* faf0 = 1 */
	m->roundSeconds = ladder_round_seconds(r->mode, r->setup.stage);
}
