/* Host test of the Part B scene logic (src/app/select.c, modes.md §B) on the portable scene
 * core + flow glue: pools/stage list/stats, ArcSelect timings + exit into the ladder, VS select
 * (CPU mode slot hand-over, options, stage/BGM, white fade, VS phase, exit -> g_setup), result
 * rank (FUN_00617740) and win quotes (FUN_00617110), result/continue state machine, pause menu. */
#include <stdio.h>
#include <string.h>

#include "../src/app/modes_flow.h"
#include "../src/app/music.h"
#include "../src/app/scene_core.h"
#include "../src/app/select.h"
#include "../src/app/text.h"

static int fails;
#define CHECK(c)                                                          \
	do {                                                                  \
		if (!(c)) {                                                       \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);           \
			fails++;                                                      \
		}                                                                 \
	} while (0)

/* ---- stubs of the 3DS side ---- */
static int last_se = -1, n_se, se_count[8], bgm_req = -1, bgm_force = -1, msel;
void audio_se(int id, int cd)
{
	(void)cd;
	last_se = id;
	n_se++;
	if (id >= 0 && id < 8)
		se_count[id]++;
}
void music_request(int id, bool restart, bool loop) { (void)restart, (void)loop; bgm_req = id; }
void music_force(int id, bool loop) { (void)loop; bgm_force = id; }
void music_sel_step(int dir) { msel += dir; audio_se(2, 0xff); }
void music_sel_set(int i) { msel = i; }
int music_resolve(void) { return msel == 0 ? 0x0b : 0x0c; }

static const scene_def dummy = {.name = "dummy"};
static const scene_def arc = {.name = "arc", .init = arcsel_init, .update = arcsel_update};
static const scene_def vs = {.name = "vs", .init = vssel_init, .update = vssel_update};
static const scene_def res = {.name = "result", .init = result_init, .update = result_update};

static int frame(uint16_t bits)
{
	minput_feed(&g_min, bits);
	scene_core_begin_frame();
	scene_core_update();
	announce_update();
	scene_fade_update();
	scene_core_end_frame();
	return scene_current();
}
static void press(uint16_t b) { frame(b); frame(0); }
static int idle(int n)
{
	for (int i = 0; i < n; i++)
		frame(0);
	return scene_current();
}
static int run_until_change(int max)
{
	int s = scene_current();
	for (int i = 0; i < max; i++)
		if (frame(0) != s)
			return i + 1;
	return -1;
}

static void reg_all(void)
{
	for (int id = 1; id < SCENE_MAX; id++)
		scene_register(id, NULL);
	scene_register(SCENE_MENU, &dummy);
	scene_register(SCENE_BATTLE, &dummy);
	scene_register(SCENE_CONVERSATION, &dummy);
	scene_register(SCENE_ENDROLL, &dummy);
	scene_register(SCENE_ARC_SELECT, &arc);
	scene_register(SCENE_VS_SELECT, &vs);
	scene_register(SCENE_RESULT, &res);
}

static void enter(int id) /* from the menu */
{
	scene_core_start(SCENE_MENU);
	scene_core_begin_frame();
	scene_goto(id);
	scene_core_end_frame();
	scene_core_begin_frame(); /* runs Init with prev = menu */
	scene_core_end_frame();
}

static void test_data(void)
{
	aos_save sv;
	aos_save_defaults(&sv);
	int p[SEL_POOL_MAX];
	int n = sel_pool_arcade(0, &sv, p);
	CHECK(n == 12); /* 3..8, 10..15 open by default; SB, Hime, Sumika locked */
	CHECK(p[0] == 8 && p[5] == 3 && p[6] == 10 && p[n - 1] == 15);
	memset(sv.charOpen, 1, sizeof sv.charOpen);
	CHECK(sel_pool_arcade(0, &sv, p) == 15 && p[0] == 9 && p[13] == 16 && p[14] == 17);
	CHECK(sel_pool_arcade(1, &sv, p) == 14);
	CHECK(sel_pool_vs(&sv, p) == 16 && p[15] == SEL_RANDOM && p[14] == 17);
	aos_save_defaults(&sv);
	int st[16];
	n = sel_stage_list(&sv, st);
	CHECK(n == 8 && st[0] == 5 && st[1] == 11 && st[6] == 1 && st[7] == 15); /* 5,11,9,8,7,6,1,15 */
	sel_stats s = sel_char_stats(13);
	CHECK(s.diff == 2 && s.rank[0] == 2 && s.rank[3] == 3);
	CHECK(sel_color_resolve(3, 3) == 2 && sel_color_resolve(4, 3) == 0 && sel_color_resolve(4, 2) == 1);
	CHECK(sel_color_resolve(10, 0) == 0);
	/* rank FUN_00617740 */
	modes_result r = {0};
	r.seconds = 40, r.hp = 9000;
	CHECK(result_rank(&r, 1) == 0); /* 0.8 + 1.0 = 1.8 */
	r.seconds = 75, r.hp = 9000;
	CHECK(result_rank(&r, 1) == 2); /* 0.6 + 0.8 = 1.4 */
	r.seconds = 90, r.hp = 4000;
	CHECK(result_rank(&r, 1) == 3); /* 0.4 + 0.4 */
	r.seconds = 200, r.hp = 100;
	CHECK(result_rank(&r, 1) == 4);
	r.seconds = 100, r.hp = 18000;
	CHECK(result_rank(&r, 2) == 1); /* 1.0 (100 < 2*50) + 0.8 (0.9) */
	/* win quotes FUN_00617110 */
	CHECK(result_quote_pick(3, 10, 4) == 10 && result_quote_pick(3, 16, 4) == 11 && result_quote_pick(3, 7, 4) == 12);
	CHECK(result_quote_pick(3, 4, 4) == 1 && result_quote_pick(17, 3, 99) == 0 && result_quote_pick(1, 3, 5) == -1);
	char k[32];
	CHECK(!strcmp(result_quote_key(9, 11, k, 32), "WIN_STARBREAKER_X02"));
	CHECK(!strcmp(result_quote_key(18, 2, k, 32), "WIN_SUMIKA_03"));
	CHECK(strcmp(tr("WIN_SORA_01"), "WIN_SORA_01") != 0);
}

static void test_arcselect(void)
{
	reg_all();
	modes_flow_init(77, NULL);
	g_setup.mode = MODE_ARCADE;
	g_setup.difficulty = 1;
	enter(SCENE_ARC_SELECT);
	CHECK(scene_current() == SCENE_ARC_SELECT && g_arc.sub == ARC_COMMAND);
	CHECK(arcsel_char() == 10 && bgm_force == MUSIC_SELECT && !strcmp(announce_text(), announce_by_id(0x34)));
	idle(25);
	CHECK(g_arc.cb8 == 1.0f);
	press(MB_DOWN);
	CHECK(g_arc.cmd == 1 && last_se == 2);
	press(MB_A);
	CHECK(g_arc.sub == ARC_CHAR && g_setup.command[0] == 1 && last_se == 0);
	press(MB_LEFT); /* Left = cursor + 1: Saki */
	CHECK(arcsel_char() == 11 && g_arc.d74 < 0 && g_setup.chara[0] == 11);
	press(MB_RIGHT);
	press(MB_RIGHT); /* -> Sora */
	CHECK(arcsel_char() == 3);
	press(MB_A);
	CHECK(g_arc.sub == ARC_CONFIRMED && scene_fade_busy());
	int f = run_until_change(300);
	CHECK(f >= 100 && f <= 104); /* fade 2 = 100 frames + off + one loading frame */
	CHECK(scene_current() == SCENE_BATTLE && g_setup.chara[0] == 3 && g_setup.command[0] == 1);
	CHECK(modes_flow_active() && g_setup.stage == 15 && bgm_req == 0);
	/* cancel path */
	enter(SCENE_ARC_SELECT);
	press(MB_B);
	CHECK(g_arc.sub == ARC_CANCELLED && last_se == 1);
	CHECK(run_until_change(100) > 0 && scene_current() == SCENE_MENU);
}

static void test_vsselect(void)
{
	reg_all();
	modes_flow_init(5, NULL);
	memset(modes_save()->stageOpen, 1, sizeof modes_save()->stageOpen);
	g_setup.mode = MODE_MATCH;
	g_setup.sub = SUB_CPU;
	g_setup.input[0] = 0;
	g_setup.input[1] = INPUT_CPU;
	enter(SCENE_VS_SELECT);
	vssel_state *v = &g_vs;
	CHECK(v->slot[0] == SLOT_CHOOSE && v->slot[1] == SLOT_CPU && v->ctrl[0] == 0 && v->ctrl[1] == -1);
	CHECK(v->pool[v->cursor[0]] == 10 && v->pool[v->cursor[1]] == 3 && v->stages[v->stage_cur] == 15);
	CHECK(v->handicap[0] == 2 && v->en_cmd[0] && !v->en_cmd[1]);
	press(MB_DOWN); /* Suguri -> Saki */
	CHECK(g_setup.chara[0] == 11 && v->d74[0] > 0);
	press(MB_A);
	CHECK(v->slot[0] == SLOT_OPTIONS && v->row[0] == ROW_ACCEPT);
	press(MB_UP); /* row 3 input */
	press(MB_RIGHT);
	CHECK(v->command[0] == 1 && last_se == 4);
	press(MB_UP); /* row 2 colour */
	press(MB_LEFT); /* Auto -> SP (Saki has one) */
	CHECK(v->color[0] == 3);
	press(MB_A);
	CHECK(v->slot[0] == SLOT_READY && v->ctrl[0] == 1 && v->slot[1] == SLOT_CHOOSE);
	press(MB_B); /* cancel on the CPU slot: back to P1's options */
	CHECK(v->slot[1] == SLOT_CPU && v->ctrl[0] == 0 && v->slot[0] == SLOT_OPTIONS);
	press(MB_A);
	CHECK(v->slot[0] == SLOT_READY && v->ctrl[0] == 1);
	for (int i = 0; i < 20 && g_setup.chara[1] != SEL_RANDOM; i++) /* down to Random (last) */
		press(MB_DOWN);
	CHECK(g_setup.chara[1] == SEL_RANDOM);
	press(MB_A);
	press(MB_UP); /* row 3: input disabled for the CPU */
	press(MB_RIGHT);
	CHECK(v->command[1] == 0 && last_se == 3);
	press(MB_A);
	CHECK(v->sub == VS_STAGE && v->slot[1] == SLOT_READY && !strcmp(announce_text(), announce_by_id(0x33)));
	press(MB_RIGHT);
	CHECK(v->stages[v->stage_cur] == 16);
	press(MB_DOWN);
	CHECK(msel == 1);
	press(MB_A);
	CHECK(v->sub == VS_WHITE && v->stage == 16 && bgm_req == 0);
	int wf = 0;
	while (v->sub == VS_WHITE && wf < 200)
		frame(0), wf++;
	CHECK(wf >= 98 && wf <= 101); /* ca4 += 0.01/frame */
	CHECK(v->sub == VS_VS && v->stage_swapped && bgm_req == MUSIC_VERSUS);
	CHECK(g_setup.chara[1] != SEL_RANDOM && g_setup.chara[1] >= 3 && g_setup.chara[1] <= 17);
	idle(102);
	CHECK(v->sub == VS_CONFIRMED);
	int f = run_until_change(300);
	CHECK(f > 99 && scene_current() == SCENE_BATTLE);
	CHECK(g_setup.chara[0] == 11 && g_setup.color[0] == 2 && g_setup.command[0] == 1 && g_setup.handicap[0] == 2);
	CHECK(g_setup.stage == 16 && g_setup.bgm == 0x0c && g_setup.new_match);
	/* rematch from the result: everything kept */
	scene_core_begin_frame();
	scene_goto(SCENE_RESULT);
	scene_core_end_frame();
	scene_core_begin_frame();
	scene_goto(SCENE_VS_SELECT);
	scene_core_end_frame();
	scene_core_begin_frame();
	scene_core_end_frame();
	CHECK(v->color[0] == 3 && v->command[0] == 1 && v->stages[v->stage_cur] == 16 && msel == 1);
	CHECK(v->pool[v->cursor[0]] == 11);
}

static aos_world w;
static void fake_world(int p1wins)
{
	memset(&w, 0, sizeof w);
	w.p[0].id = 10;
	w.p[1].id = 3;
	w.p[0].hp = p1wins ? 6000 : 0;
	w.p[1].hp = p1wins ? 0 : 6000;
	w.nRoundRecs = 1;
	w.roundRec[0][!p1wins].hp = 6000;
	w.roundRec[0][!p1wins].seconds = 45;
	w.roundRec[0][!p1wins].score = 12000;
}

static void test_result(void)
{
	reg_all();
	modes_flow_init(9, NULL);
	/* versus: win -> rows/rank timeline -> confirm -> 0x11 wipe -> VS select */
	g_setup.mode = MODE_MATCH;
	g_setup.input[1] = INPUT_CPU;
	fake_world(1);
	result_capture(&w);
	CHECK(g_res.winner == 0 && g_res.quote == 11 && g_res.rank == 1 && g_res.r.score == 12000); /* Suguri vs Sora: X02 */
	enter(SCENE_RESULT);
	CHECK(g_res.sub == RES_WON && bgm_force == MUSIC_RESULT);
	memset(se_count, 0, sizeof se_count);
	press(MB_A); /* t <= 199: skip to 200 */
	CHECK(g_res.t == 202);
	idle(100);
	CHECK(se_count[5] == 1 && g_res.row[0] == 1.0f && g_res.row[3] == 1.0f);
	press(MB_A);
	CHECK(g_res.sub == RES_LEAVING && bgm_req == 0);
	CHECK(run_until_change(200) > 80 && scene_current() == SCENE_VS_SELECT);
	/* arcade loss: continue countdown 9..0 then game over */
	g_setup.mode = MODE_ARCADE;
	g_setup.difficulty = 0;
	modes_flow_start(10, 0);
	scene_core_end_frame();
	fake_world(0);
	result_capture(&w);
	modes_flow_game_exit(MODES_EXIT_DECIDED, &w);
	scene_core_end_frame();
	scene_core_begin_frame();
	CHECK(scene_current() == SCENE_RESULT && g_res.sub == RES_LOST);
	idle(201);
	memset(se_count, 0, sizeof se_count);
	press(MB_A);
	CHECK(g_res.sub == RES_CONTINUE && g_res.count == 9);
	idle(97); /* the first tick comes 98 frames after entering (e4 % 100 == 99) */
	CHECK(g_res.count == 9);
	idle(1);
	CHECK(g_res.count == 8 && se_count[2] == 1);
	press(MB_B); /* cancel speeds up */
	CHECK(g_res.count == 7);
	idle(800);
	CHECK(g_res.sub == RES_LEAVING && g_res.game_over && !g_res.cont);
	CHECK(run_until_change(200) > 0 && scene_current() == SCENE_MENU && !modes_flow_active());
	/* arcade loss -> continue -> same fight */
	modes_flow_start(10, 0);
	scene_core_end_frame();
	int opp = g_setup.chara[1];
	fake_world(0);
	w.p[1].id = opp;
	result_capture(&w);
	modes_flow_game_exit(MODES_EXIT_DECIDED, &w);
	scene_core_end_frame();
	scene_core_begin_frame();
	idle(210);
	press(MB_A);
	press(MB_A);
	CHECK(g_res.cont);
	CHECK(run_until_change(200) > 0 && scene_current() == SCENE_BATTLE && g_setup.chara[1] == opp);
	CHECK(modes_flow_run()->continues == 1);
}

static void test_pause(void)
{
	reg_all();
	scene_core_start(SCENE_BATTLE);
	scene_core_begin_frame();
	scene_core_end_frame();
	memset(&w, 0, sizeof w);
	pause_reset();
	w.frame = 150;
	minput_feed(&g_min, MB_START);
	CHECK(pause_update(&w) == PAUSE_OFF && !g_pause.on); /* first 200 frames: no pause */
	minput_feed(&g_min, 0);
	w.frame = 300;
	minput_feed(&g_min, MB_START);
	CHECK(pause_update(&w) == PAUSE_OFF && g_pause.on); /* opens; this frame still ticks */
	int n = 0;
	for (; n < 30 && g_pause.st != 100; n++) {
		minput_feed(&g_min, 0);
		CHECK(pause_update(&w) == PAUSE_ON);
	}
	CHECK(n == 15); /* 0.075/frame: 14 frames to 1.0, state 100 on the next */
	/* resume: countdown 91 frames with SE 4 x3 then SE 0 */
	minput_feed(&g_min, MB_A);
	pause_update(&w);
	memset(se_count, 0, sizeof se_count);
	int f = 0;
	while (g_pause.on && f < 300) {
		minput_feed(&g_min, 0);
		pause_update(&w);
		f++;
	}
	CHECK(se_count[4] == 3 && se_count[0] == 1 && f > 91 && f < 110);
	/* Retry */
	minput_feed(&g_min, MB_START);
	pause_update(&w);
	for (int i = 0; i < 16; i++) {
		minput_feed(&g_min, 0);
		pause_update(&w);
		scene_fade_update();
	}
	minput_feed(&g_min, MB_DOWN);
	pause_update(&w);
	minput_feed(&g_min, 0);
	pause_update(&w);
	minput_feed(&g_min, MB_DOWN);
	pause_update(&w);
	CHECK(g_pause.cur == 2);
	minput_feed(&g_min, MB_A);
	pause_update(&w);
	CHECK(g_pause.exiting == PAUSE_RETRY && bgm_req == 0 && scene_fade_busy());
	int r = PAUSE_ON;
	for (f = 0; f < 400 && r == PAUSE_ON; f++) {
		minput_feed(&g_min, 0);
		r = pause_update(&w);
		scene_fade_update();
	}
	CHECK(r == PAUSE_RETRY && f > 195 && f < 210); /* fade 3: 0.005/frame */
}

int main(void)
{
	if (!strings_init("extracted/script", 0))
		printf("note: extracted/script missing, text checks may fail\n");
	test_data();
	test_arcselect();
	test_vsselect();
	test_result();
	test_pause();
	if (fails) {
		printf("test_select: %d FAILED\n", fails);
		return 1;
	}
	printf("test_select: all passed\n");
	return 0;
}
