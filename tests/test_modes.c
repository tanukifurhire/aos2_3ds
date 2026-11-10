/* Host test for src/modes: ladders (vs docs/re/modes_ladders.tsv, every character),
 * stepping with continues/retries, round records + match score from a real simulated
 * match, ranking, unlocks, save encode/decode/file. */
#include "../src/modes/ladder.h"
#include "../src/modes/save.h"
#include "../src/modes/score.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c, ...)                                                   \
	do {                                                                \
		checks++;                                                       \
		if (!(c)) {                                                     \
			failures++;                                                 \
			printf("FAIL %s:%d: ", __FILE__, __LINE__);                 \
			printf(__VA_ARGS__);                                        \
			printf("\n");                                               \
		}                                                               \
	} while (0)

/* ---------------- TSV ---------------- */

typedef struct {
	int mode, player, step, type, opp, stage, bgm, c1, c2, script, backdrop;
	int pool[12], npool;
} row;

static row rows[600];
static int nrows;

static int lead_int(const char *s) { return (int)strtol(s, NULL, 10); }

static void split(char *line, char **f, int maxf, int *nf)
{
	*nf = 0;
	f[(*nf)++] = line;
	for (char *p = line; *p && *nf < maxf; p++)
		if (*p == '\t') {
			*p = 0;
			f[(*nf)++] = p + 1;
		}
}

static void load_tsv(const char *path)
{
	FILE *fp = fopen(path, "rb");
	if (!fp) {
		printf("cannot open %s\n", path);
		exit(1);
	}
	char line[1024];
	while (fgets(line, sizeof line, fp)) {
		line[strcspn(line, "\r\n")] = 0;
		if (line[0] == '#' || !strncmp(line, "mode\t", 5) || !line[0])
			continue;
		char *f[16];
		int nf;
		split(line, f, 16, &nf);
		while (nf < 11)
			f[nf++] = "";
		row *r = &rows[nrows++];
		memset(r, 0, sizeof *r);
		r->mode = !strcmp(f[0], "story") ? LADDER_STORY : LADDER_ARCADE;
		r->player = lead_int(f[1]);
		r->step = lead_int(f[2]);
		r->type = !strcmp(f[3], "fight") ? LADDER_FIGHT : !strcmp(f[3], "conversation") ? LADDER_CONV
		          : !strcmp(f[3], "result") ? LADDER_RESULT : !strcmp(f[3], "ending") ? LADDER_ENDING : -1;
		r->opp = !strcmp(f[4], "RANDOM") ? LADDER_RANDOM : lead_int(f[4]);
		r->stage = lead_int(f[5]);
		r->bgm = lead_int(f[6]);
		if (f[7][0])
			sscanf(f[7], "%d/%d", &r->c1, &r->c2);
		const char *cv = strstr(f[8], "TEXT_CONVERSATION"); /* + %03d script id */
		if (cv) {
			r->script = lead_int(cv + 17);
			const char *b = strstr(cv, "backdrop ");
			r->backdrop = b ? lead_int(b + 9) : -1;
		}
		char *p = f[9];
		while (*p) {
			char *end;
			long v = strtol(p, &end, 10);
			if (end == p)
				break;
			r->pool[r->npool++] = (int)v;
			p = end;
		}
	}
	fclose(fp);
}

static void test_tsv(void)
{
	int compared = 0;
	for (int mode = 0; mode < 2; mode++)
		for (int c = 3; c <= 18; c++) {
			ladder_run R;
			ladder_build(&R, mode, c, 1);
			int n = 0;
			for (int i = 0; i < nrows; i++) {
				const row *t = &rows[i];
				if (t->mode != mode || t->player != c)
					continue;
				n++;
				CHECK(t->step < R.nsteps, "%d/%d step %d missing", mode, c, t->step);
				if (t->step >= R.nsteps)
					continue;
				const ladder_entry *e = &R.step[t->step];
				compared++;
				CHECK(e->type == t->type, "%d/%d step %d type %d want %d", mode, c, t->step, e->type, t->type);
				if (t->type == LADDER_FIGHT) {
					CHECK(e->a == c && e->b == t->opp && e->stage == t->stage && e->bgm == t->bgm && e->colP1 == t->c1 &&
					          e->colP2 == t->c2,
					      "%d/%d step %d fight {%d %d st %d bgm %d} want {%d st %d bgm %d}", mode, c, t->step, e->a, e->b,
					      e->stage, e->bgm, t->opp, t->stage, t->bgm);
				} else if (t->type == LADDER_CONV) {
					CHECK(e->a == t->script && e->b == t->backdrop, "%d/%d step %d conv %d/%d want %d/%d", mode, c,
					      t->step, e->a, e->b, t->script, t->backdrop);
				}
				if (t->npool) {
					int same = t->npool == R.npool;
					for (int k = 0; same && k < t->npool; k++)
						same = R.pool[k] == t->pool[k];
					CHECK(same, "%d/%d pool differs", mode, c);
				}
			}
			CHECK(n == R.nsteps, "%s char %d: %d steps, TSV has %d", mode ? "story" : "arcade", c, R.nsteps, n);
		}
	CHECK(compared == nrows, "compared %d of %d TSV rows", compared, nrows);
	printf("ladder vs TSV: %d rows compared\n", compared);
}

/* ---------------- full runs ---------------- */

static aos_world W; /* only roundRec / hp are read by ladder_game_exit */

static void fake_match(int winner, int32_t score)
{
	memset(&W, 0, sizeof W);
	W.p[0].hp = winner == 0 ? 100 : 0;
	W.p[1].hp = winner == 0 ? 0 : 100;
	W.nRoundRecs = 1;
	W.roundRec[0][winner].score = score;
	W.roundRec[0][winner].hp = 100;
}

/* plays a run; lose_at = fight number (0-based) lost once then continued (-1 none);
 * retry_at = fight number retried once from the pause menu (-1 none) */
static void run(int mode, int c, uint32_t seed, int lose_at, int retry_at, int verbose)
{
	ladder_run R;
	ladder_out o;
	aos_save S;
	aos_mt rng;
	aos_mt_seed(&rng, seed);
	aos_save_defaults(&S);
	ladder_start(&R, mode, c, 2, &rng, &S, &o);
	int fights = 0, seen[32] = {0}, guard = 0, lost_once = 0, retried = 0, conts = 0, ended = 0, boss_cont = -1;
	int expect_score = 0;
	while (!o.finished && guard++ < 200) {
		switch (o.scene) {
		case LADDER_SCENE_BATTLE: {
			aos_match_params m;
			ladder_match_params(&R, 0, &m);
			CHECK(m.ids[0] == c && m.inputSrc[1] == 10 && m.roundsToWin == 1 && m.handicap[0] == 2 && m.handicap[1] == 2 &&
			          m.difficulty == 2,
			      "match params");
			CHECK(m.ids[1] >= 3 && m.ids[1] <= 18 && m.ids[1] != c, "opponent %d", m.ids[1]);
			int last_fight = (mode == LADDER_ARCADE && fights == 6) || (mode == LADDER_STORY && fights == 6);
			if (mode == LADDER_STORY && last_fight)
				CHECK(m.ids[1] == AOS_CH_SUMIKA2 && m.roundSeconds == 999 && R.setup.stage == 0x13, "story final fight");
			else
				CHECK(m.roundSeconds == 180, "timer 180");
			if (mode == LADDER_ARCADE && last_fight) {
				/* the remap happens when the boss fight first starts and is written back into the entry */
				if (boss_cont < 0)
					boss_cont = R.continues > 0;
				int st = R.setup.stage, cont = boss_cont;
				if (c <= 9)
					CHECK(st == (cont ? 0xc : 0xd), "arcade boss stage %d (continues %d)", st, R.continues);
				else
					CHECK(st == (cont ? 0x11 : 0x12), "arcade boss stage %d (continues %d)", st, R.continues);
			}
			if (fights == retry_at && !retried) {
				retried = 1;
				int opp = m.ids[1], st = R.setup.stage;
				ladder_game_exit(&R, LADDER_EXIT_RETRY, NULL, &rng, &S, &o, NULL);
				CHECK(o.scene == LADDER_SCENE_BATTLE && R.setup.p2 == opp && R.setup.stage == st && R.continues == conts + 1 &&
				          R.score == 0,
				      "retry refights the same opponent");
				conts++;
				expect_score = 0;
				continue;
			}
			if (fights == lose_at && !lost_once) {
				lost_once = 1;
				int opp = m.ids[1];
				fake_match(1, 5000);
				ladder_game_exit(&R, LADDER_EXIT_DECIDED, &W, &rng, &S, &o, NULL);
				CHECK(o.scene == LADDER_SCENE_RESULT && o.lost, "loss -> result in continue mode");
				ladder_result_exit(&R, 0, 1, &rng, &S, &o);
				conts++;
				expect_score = 0;
				CHECK(o.scene == LADDER_SCENE_BATTLE && R.setup.p2 == opp && R.continues == conts && R.score == 0,
				      "continue refights the same opponent");
				continue;
			}
			if (m.ids[1] != AOS_CH_SUMIKA2) {
				CHECK(!seen[m.ids[1]], "opponent %d twice", m.ids[1]);
				seen[m.ids[1]] = 1;
			}
			fake_match(0, 2000 + fights);
			if (mode == LADDER_ARCADE)
				expect_score += 2000 + fights;
			modes_result res;
			ladder_game_exit(&R, LADDER_EXIT_DECIDED, &W, &rng, &S, &o, &res);
			CHECK(res.winner == 0 && res.score == 2000 + fights, "result sums");
			CHECK(R.score == expect_score, "run score %d want %d", R.score, expect_score);
			CHECK(o.scene == LADDER_SCENE_RESULT && !o.lost, "won -> result entry");
			fights++;
			break;
		}
		case LADDER_SCENE_RESULT: {
			int w0 = S.wins;
			ladder_result_exit(&R, 0, 0, &rng, &S, &o);
			CHECK(S.wins == w0 + 1, "wins++ on a won result");
			break;
		}
		case LADDER_SCENE_CONVERSATION:
			CHECK(mode == LADDER_STORY && o.scriptId / 10 == c - 2, "conversation %d", o.scriptId);
			ladder_step(&R, 0, &rng, &S, &o);
			break;
		case LADDER_SCENE_ENDROLL:
			ended = 1;
			ladder_endroll_exit(&R, &rng, &S, &o);
			break;
		default:
			CHECK(0, "unexpected scene %d", o.scene);
			guard = 1000;
		}
	}
	CHECK(o.finished && o.scene == LADDER_SCENE_MENU && ended, "%d/%d run finished", mode, c);
	CHECK(fights == 7, "%d/%d fights %d", mode, c, fights);
	int nocont = lose_at < 0 && retry_at < 0;
	if (mode == LADDER_ARCADE) {
		CHECK(S.arcadeClear[2][c] == nocont && S.arcadeClears[2] == nocont, "arcade clear recorded iff no continue");
		CHECK(R.rank <= 10, "rank");
		if (nocont && expect_score > 9000)
			CHECK(R.rank < 10 && S.rank[2][R.rank].score == expect_score && S.rank[2][R.rank].character == c,
			      "ranked %d", R.rank);
	} else {
		CHECK(S.storyClear[c] == nocont && S.storyClears == nocont && R.rank == 11, "story clear recorded iff no continue");
		if (c >= 3 && c <= 16)
			CHECK((S.achievements >> (c <= 9 ? c + 5 : c - 9)) & 1, "story achievement");
	}
	CHECK(((S.achievements >> 0x11) & 1) == nocont, "ACH_NOCONTINUE");
	if (verbose)
		printf("  %s %2d seed %u: rank %d score %d msgs %d\n", mode ? "story " : "arcade", c, seed, R.rank, R.score,
		       R.msgs.n);
}

static void test_runs(void)
{
	for (int mode = 0; mode < 2; mode++)
		for (int c = 3; c <= (mode ? 16 : 17); c++)
			for (uint32_t seed = 1; seed <= 20; seed++) {
				run(mode, c, seed, -1, -1, 0);
				run(mode, c, seed, (int)(seed % 7), -1, 0);
				run(mode, c, seed, -1, (int)((seed + 3) % 7), 0);
			}
	/* empty ladders (Sumika has no story; Sumika 2nd / bad ids nothing) end at once */
	ladder_run R;
	ladder_out o;
	aos_mt rng;
	aos_mt_seed(&rng, 1);
	ladder_start(&R, LADDER_STORY, 17, 1, &rng, NULL, &o);
	CHECK(o.finished && o.scene == LADDER_SCENE_MENU, "story Sumika: empty");
	/* the opponent pick is FUN_00459f80(0, n*10) % n on the given stream */
	aos_mt a, b;
	aos_mt_seed(&a, 42);
	aos_mt_seed(&b, 42);
	ladder_start(&R, LADDER_ARCADE, 3, 0, &a, NULL, &o);
	int32_t pool[] = {4, 5, 6, 7, 8, 11, 12, 13, 14, 15};
	uint32_t v = (uint32_t)aos_rand_i(&b, 0, 100);
	CHECK(R.setup.p2 == pool[v % 10] && R.npool == 9, "first pick = pool[rand(0,100) %% 10] (%d)", R.setup.p2);
	/* quit -> ArcSelect */
	ladder_game_exit(&R, LADDER_EXIT_QUIT, NULL, &a, NULL, &o, NULL);
	CHECK(o.scene == LADDER_SCENE_ARCSELECT, "quit");
	/* game over: ranking insert + arcade unlock check */
	aos_save S;
	aos_save_defaults(&S);
	ladder_start(&R, LADDER_ARCADE, 10, 0, &a, &S, &o);
	fake_match(0, 20000);
	ladder_game_exit(&R, LADDER_EXIT_DECIDED, &W, &a, &S, &o, NULL);
	ladder_result_exit(&R, 0, 0, &a, &S, &o);
	fake_match(1, 0);
	ladder_game_exit(&R, LADDER_EXIT_DECIDED, &W, &a, &S, &o, NULL);
	CHECK(o.scene == LADDER_SCENE_RESULT && o.lost, "lost");
	ladder_result_exit(&R, 1, 0, &a, &S, &o);
	CHECK(o.finished && o.scene == LADDER_SCENE_MENU && R.rank == 0 && S.rank[0][0].score == 20000 &&
	          S.rank[0][0].character == 10 && S.rank[0][0].name[0] == 0x2d && S.rank[0][1].score == 10000,
	      "game over ranks the run (rank %d)", R.rank);
	CHECK(S.arcadeClears[0] == 0 && S.wins == 1, "no clear on game over");
}

/* ---------------- score / ranking / unlocks / save ---------------- */

static void test_rank(void)
{
	aos_save S;
	aos_save_defaults(&S);
	CHECK(S.rank[1][0].score == 10000 && S.rank[1][9].score == 1000 && S.rank[1][3].name[6] == 3 &&
	          S.rank[1][0].character == 10 && S.rank[1][1].character == 3,
	      "rank defaults");
	CHECK(modes_rank_insert(&S, 1, 9500, 5) == 1, "9500 -> 2nd");
	CHECK(modes_rank_insert(&S, 1, 9000, 6) == 3, "tie goes below (9000 after 9500, 9000)");
	CHECK(S.rank[1][3].character == 6 && S.rank[1][4].score == 8000 && S.rank[1][9].score == 3000, "shifted");
	CHECK(modes_rank_insert(&S, 1, 0, 6) == 10, "not ranked");
	CHECK(modes_rank_insert(&S, 0, 99999, 4) == 0 && S.rank[0][1].score == 10000 && S.rank[0][9].score == 2000, "top");
	CHECK(modes_rank_insert(&S, 3, 99999, 4) == 10, "bad difficulty");
}

static void test_unlocks(void)
{
	aos_save S;
	modes_msgs m;
	aos_save_defaults(&S);
	CHECK(S.charOpen[3] && S.charOpen[8] && !S.charOpen[9] && S.charOpen[10] && S.charOpen[15] && !S.charOpen[16] &&
	          !S.charOpen[17] && S.stageOpen[5] && S.stageOpen[15] && !S.stageOpen[10] && !S.stageOpen[12] &&
	          S.bgmOpen[11] && !S.bgmOpen[14] && S.bgmOpen[18],
	      "defaults");
	m.n = 5;
	modes_unlocks_arcade(&S, &m);
	CHECK(m.n == 0, "nothing unlocked, msgs cleared");
	S.arcadeClear[1][11] = 1; /* Saki at normal */
	modes_unlocks_arcade(&S, &m);
	CHECK(m.n == 1 && m.id[0] == 0x3f8 && S.charOpen[16], "Hime after a 10..15 clear");
	S.arcadeClear[0][16] = 1;
	modes_unlocks_arcade(&S, &m);
	CHECK(m.n == 1 && m.id[0] == 0x7e1 && S.stageOpen[17], "stage 17 with 2 of 10..16 (%d msgs)", m.n);
	S.wins = 60;
	modes_unlocks_arcade(&S, &m);
	CHECK(m.n == 4 && m.id[0] == 0x3f1 && m.id[1] == 0x7dc && m.id[2] == 0x7e2 && m.id[3] == 0x7dd && S.stageOpen[12] &&
	          S.stageOpen[13] && S.stageOpen[18] && !S.bgmOpen[14],
	      "wins > 59: SB, stages 12, 18, 13 in check order (%d)", m.n);
	/* story */
	aos_save_defaults(&S);
	m.n = 0;
	for (int c = 3; c <= 7; c++)
		S.storyClear[c] = 1;
	modes_unlocks_story(&S, &m, 0, 0, 3);
	CHECK(m.n == 2 && m.id[0] == 0x7e0 && m.id[1] == 0x7e3 && !S.charOpen[17] && !(S.achievements & (1u << 8)),
	      "5 stories: stages 16, 19; no achievement on easy");
	for (int c = 3; c <= 16; c++)
		S.storyClear[c] = 1;
	modes_unlocks_story(&S, &m, 0, 2, 16);
	CHECK(m.n == 5 && m.id[2] == 0x3f9 && S.charOpen[17] && (S.achievements & (1u << 7)) && (S.achievements & (1u << 15)),
	      "all stories: Sumika, Hime story achievement (%d)", m.n);
	aos_save_defaults(&S);
	S.storyClear[4] = 1;
	S.wins = 256;
	modes_unlocks_story(&S, &m, 1, 2, 4);
	CHECK(S.charOpen[17] && !(S.achievements & (1u << 9)), "Sumika via wins > 255 and one story; game over: no ach");
	/* clears */
	aos_save_defaults(&S);
	modes_record_clear(&S, 0, 1, 12, 1);
	CHECK(!S.arcadeClear[1][12] && !S.arcadeClears[1] && !S.achievements, "continue: nothing recorded");
	modes_record_clear(&S, 0, 1, 12, 0);
	CHECK(S.arcadeClear[1][12] && S.arcadeClears[1] == 1 && (S.achievements >> 0x11 & 1), "arcade clear");
	CHECK(modes_arcade_cleared(&S, 1, 12) && !modes_arcade_cleared(&S, 0, 12), "cleared mark");
}

static void test_save(const char *dir)
{
	aos_save S, T;
	aos_save_defaults(&S);
	S.wins = 1234;
	S.arcadeClear[2][9] = 1;
	S.storyClear[16] = 1;
	S.bgmOpen[24] = 1;
	S.stageOpen[19] = 1;
	S.charOpen[18] = 1;
	S.storyClears = 7;
	S.arcadeClears[1] = 3;
	S.achievements = 0x20001u;
	modes_rank_insert(&S, 2, 77777, 15);
	uint8_t buf[AOS_SAVE_FILE_BYTES];
	size_t n = aos_save_encode(&S, buf, sizeof buf);
	CHECK(n == AOS_SAVE_FILE_BYTES && n == 16 + 0xa4 + 0xca8 + 4, "size %u", (unsigned)n);
	/* the sys image keeps the PC byte layout */
	CHECK(buf[16 + 0x04 + 3] == 1 && buf[16 + 0x04 + 9] == 0 && buf[16 + 0x44] == (1234 & 0xff) &&
	          buf[16 + 0x58 + 2 * 0x13 + 9] == 1 && buf[16 + 0x91 + 16] == 1 && buf[16 + 0x2b + 24] == 1 &&
	          buf[16 + 0x17 + 19] == 1 && buf[16] == 0xa2 && buf[17] == 0x05,
	      "game.sys layout");
	CHECK(aos_save_decode(&T, buf, n) == 1 && !memcmp(T.charOpen, S.charOpen, sizeof S.charOpen) && T.wins == 1234 &&
	          T.arcadeClear[2][9] && T.storyClear[16] && T.bgmOpen[24] && T.stageOpen[19] && T.storyClears == 7 &&
	          T.arcadeClears[1] == 3 && T.achievements == 0x20001u && T.rank[2][0].score == 77777 &&
	          T.rank[2][0].character == 15 && T.rank[2][1].score == 10000 && !T.dirty,
	      "round trip");
	buf[100] ^= 1;
	CHECK(aos_save_decode(&T, buf, n) == 0 && T.wins == 0 && T.dirty, "crc error -> defaults");
	buf[100] ^= 1;
	CHECK(aos_save_decode(&T, buf, n - 1) == 0, "short");
	char d[400], p[460];
	snprintf(d, sizeof d, "%s/savetest", dir);
	snprintf(p, sizeof p, "%s/progress.sav", d);
	remove(p);
	CHECK(aos_save_load_file(&T, p) == 0 && T.wins == 0, "missing file -> defaults");
	CHECK(aos_save_write_file(&S, d, p) == 1, "write %s", p);
	CHECK(aos_save_write_file(&S, d, p) == 1, "overwrite");
	CHECK(aos_save_load_file(&T, p) == 1 && T.wins == 1234 && T.rank[2][0].score == 77777, "load");
}

/* round records + match score from the real sim (FUN_00619cb0 / FUN_0061ac80 / Game Exit) */
static void test_records(void)
{
	aos_register_all();
	int matches = 0;
	for (int a = 3; a <= 17; a += 2) {
		aos_match_params m;
		aos_match_params_default(&m);
		m.ids[0] = a;
		m.ids[1] = a == 17 ? AOS_CH_SUMIKA2 : a + 1;
		m.inputSrc[0] = m.inputSrc[1] = 10;
		m.difficulty = 2;
		m.roundsToWin = a % 4 == 1 ? 1 : 2;
		m.roundSeconds = 60;
		aos_match_init_ex(&W, &m);
		CHECK(W.p[0].maxHp == 9000 && W.p[0].hp == 9000 && W.p[0].redHp == 9000 && W.p[0].recovHp == 9000,
		      "default HP 9000");
		if (m.ids[1] == AOS_CH_SUMIKA2)
			CHECK(W.p[1].maxHp == 18000 && W.p[1].hp == 18000 && W.p[1].recovHp == 18000 && W.p[1].redHp == 9000,
			      "Sumika 2nd HP 18000 (redHp keeps the table 9000)");
		uint16_t none[2] = {0, 0};
		int rounds = 0;
		for (int f = 0; f < 60 * 60 * 6 && !W.flow.matchEnd; f++) {
			int n0 = W.nRoundRecs;
			aos_tick(&W, none);
			if (W.nRoundRecs != n0) { /* built from the state at the end of the tick that ends the banner */
				rounds++;
				const aos_round_rec *r = W.roundRec[n0];
				int win = aos_hp_winner(&W);
				int bonus = (int)((float)(180 - W.frame / 60) / 120.0f * 10000.0f);
				bonus = bonus > 10000 ? 10000 : bonus < 0 ? 0 : bonus;
				for (int i = 0; i < 2; i++) {
					CHECK(r[i].hp == W.p[i].hp && r[i].comboDmg == W.p[i].comboBestDmg &&
					          r[i].comboHits == W.p[i].comboBestHits && r[i].seconds == W.frame / 60,
					      "record fields");
					CHECK(r[i].timeBonus == (i == win ? bonus : 0), "time bonus %d want %d", r[i].timeBonus,
					      i == win ? bonus : 0);
					CHECK(r[i].score == r[i].hp + r[i].comboDmg + r[i].timeBonus, "round score");
				}
			}
		}
		CHECK(W.flow.matchEnd, "%d vs %d: match ended", m.ids[0], m.ids[1]);
		CHECK(rounds == W.nRoundRecs && rounds >= m.roundsToWin && rounds <= 2 * m.roundsToWin - 1 + 1,
		      "rounds %d records %d", rounds, W.nRoundRecs);
		modes_result res;
		modes_match_result(&W, &res);
		int32_t sum = 0;
		for (int i = 0; i < W.nRoundRecs; i++)
			sum += W.roundRec[i][res.winner].score;
		CHECK(res.winner == aos_hp_winner(&W) && res.score == sum, "match score");
		printf("  %2d vs %2d: %d round(s), winner P%d, score %d (hp %d dmg %d bonus %d, %d s, %d hits)\n", m.ids[0],
		       m.ids[1], rounds, res.winner + 1, res.score, res.hp, res.damage, res.bonus, res.seconds, res.hits);
		aos_match_restart(&W);
		CHECK(W.nRoundRecs == 0, "records cleared by a new match");
		matches++;
	}
	/* handicap table */
	static const int32_t hp[8] = {4500, 6750, 9000, 13500, 18000, 27000, 9000, 9000};
	for (int h = 0; h < 8; h++)
		CHECK(aos_handicap_hp(h == 7 ? -1 : h) == hp[h], "handicap %d", h);
	aos_match_params m;
	aos_match_params_default(&m);
	m.ids[0] = 3;
	m.ids[1] = 4;
	m.handicap[0] = 0;
	m.handicap[1] = 5;
	aos_match_init_ex(&W, &m);
	CHECK(W.p[0].maxHp == 4500 && W.p[1].maxHp == 27000 && W.p[1].redHp == 27000, "per-player handicap");
	aos_match_restart(&W);
	CHECK(W.p[0].maxHp == 4500 && W.p[1].maxHp == 27000, "handicap survives a new match");
	printf("round records: %d matches\n", matches);
}

int main(int argc, char **argv)
{
	load_tsv(argc > 1 ? argv[1] : "docs/re/modes_ladders.tsv");
	test_tsv();
	test_runs();
	test_rank();
	test_unlocks();
	test_save(argc > 2 ? argv[2] : "build/tests");
	test_records();
	printf("test_modes: %s (%d/%d checks failed)\n", failures ? "FAIL" : "ok", failures, checks);
	return failures != 0;
}
