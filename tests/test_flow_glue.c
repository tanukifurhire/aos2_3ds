/* Host test of the arcade/story flow glue (src/app/modes_flow.c) on the portable scene
 * core: ladder steps -> g_setup + scene ids, Result/continue/game over, Retry/Quit,
 * conversations, endroll, ranking + unlocks + save file, versus paths, pass-through of
 * unregistered Result/Conversation scenes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/app/modes_flow.h"
#include "../src/app/scene_core.h"

static int fails;
#define CHECK(c)                                                          \
	do {                                                                  \
		if (!(c)) {                                                       \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);           \
			fails++;                                                      \
		}                                                                 \
	} while (0)

static aos_world w; /* large: static */

/* decided match: P1 (win = 1) or P2 wins with one round record */
static void fake_match(int p1wins, int score)
{
	memset(&w, 0, sizeof w);
	w.p[0].hp = p1wins ? 5000 : 0;
	w.p[1].hp = p1wins ? 0 : 5000;
	w.nRoundRecs = 1;
	int win = p1wins ? 0 : 1;
	w.roundRec[0][win].hp = 5000;
	w.roundRec[0][win].comboDmg = score - 5000;
	w.roundRec[0][win].score = score;
	w.roundRec[0][win].seconds = 30;
}

static const scene_def dummy = {.name = "dummy"};

/* the framework applies scene_goto at frame end */
static int settle(void)
{
	scene_core_end_frame();
	return scene_current();
}

static void reg_all(int with_result_conv)
{
	for (int id = 1; id < SCENE_MAX; id++)
		scene_register(id, NULL);
	scene_register(SCENE_MENU, &dummy);
	scene_register(SCENE_BATTLE, &dummy);
	scene_register(SCENE_ARC_SELECT, &dummy);
	scene_register(SCENE_VS_SELECT, &dummy);
	scene_register(SCENE_ENDROLL, &dummy);
	if (with_result_conv) {
		scene_register(SCENE_RESULT, &dummy);
		scene_register(SCENE_CONVERSATION, &dummy);
	}
	scene_core_start(SCENE_MENU);
}

static void test_arcade_full(void)
{
	reg_all(1);
	modes_flow_init(1234, NULL);
	g_setup.mode = MODE_ARCADE;
	g_setup.difficulty = 1;
	modes_flow_start(10, 1); /* Suguri, Advanced */
	CHECK(settle() == SCENE_BATTLE);
	CHECK(modes_flow_active());
	CHECK(g_setup.chara[0] == 10 && g_setup.command[0] == 1);
	CHECK(g_setup.chara[1] >= 4 && g_setup.chara[1] <= 15 && g_setup.chara[1] != 10);
	CHECK(g_setup.stage == 15 && g_setup.bgm == 0x10 && g_setup.rounds == 1 && g_setup.timer == 180);
	CHECK(g_setup.input[1] == INPUT_CPU && g_setup.handicap[0] == 2 && g_setup.new_match);
	aos_match_params m;
	modes_flow_match_params(&m);
	CHECK(m.ids[0] == 10 && m.ids[1] == g_setup.chara[1] && m.schemes[0] == 1 && m.inputSrc[1] == 10);
	CHECK(m.roundsToWin == 1 && m.roundSeconds == 180 && m.difficulty == 1);

	/* fight 1 lost -> Result in continue mode -> continue -> same opponent */
	int opp = g_setup.chara[1];
	fake_match(0, 9000);
	modes_flow_game_exit(MODES_EXIT_DECIDED, &w);
	CHECK(settle() == SCENE_RESULT);
	CHECK(modes_flow_lost() && modes_flow_result()->winner == 1);
	modes_flow_result_exit(0, 1);
	CHECK(settle() == SCENE_BATTLE && g_setup.chara[1] == opp);
	CHECK(modes_flow_run()->continues == 1);

	/* pause Retry counts as a continue, Quit goes back to ArcSelect */
	modes_flow_game_exit(MODES_EXIT_RETRY, NULL);
	CHECK(settle() == SCENE_BATTLE && g_setup.chara[1] == opp && modes_flow_run()->continues == 2);

	/* win all 7 fights */
	int total = 0;
	for (int f = 0; f < 7; f++) {
		CHECK(scene_current() == SCENE_BATTLE);
		if (f == 6) /* boss: Hime for Suguri, Guardian's Chamber (continue used -> no secret stage) */
			CHECK(g_setup.chara[1] == 16 && g_setup.stage == 0x11 && g_setup.bgm == 0xe);
		fake_match(1, 10000 + f);
		total += 10000 + f;
		modes_flow_game_exit(MODES_EXIT_DECIDED, &w);
		CHECK(settle() == SCENE_RESULT && !modes_flow_lost());
		CHECK(modes_flow_result()->score == 10000 + f);
		modes_flow_result_exit(0, 0);
		settle();
	}
	CHECK(scene_current() == SCENE_ENDROLL);
	CHECK(modes_flow_run()->score == total);
	CHECK(modes_save()->wins == 7);
	modes_flow_endroll_exit();
	CHECK(settle() == SCENE_MENU);
	CHECK(!modes_flow_active());
	CHECK(modes_flow_run()->rank == 0); /* 70021 > 10000 default top */
	CHECK(modes_save()->rank[1][0].score == total && modes_save()->rank[1][0].character == 10);
	CHECK(!modes_arcade_cleared(modes_save(), 1, 10)); /* continues used: no clear record */
	CHECK(modes_save()->dirty);
}

static void test_arcade_no_continue_secret_stage(void)
{
	reg_all(1);
	modes_flow_init(99, NULL);
	g_setup.mode = MODE_ARCADE;
	g_setup.difficulty = 2;
	modes_flow_start(3, 0); /* Sora: boss Star Breaker */
	settle();
	for (int f = 0; f < 7; f++) {
		if (f == 6)
			CHECK(g_setup.chara[1] == 9 && g_setup.stage == 0xd); /* Whirlpool of Malice */
		fake_match(1, 12000);
		modes_flow_game_exit(MODES_EXIT_DECIDED, &w);
		settle();
		modes_flow_result_exit(0, 0);
		settle();
	}
	CHECK(scene_current() == SCENE_ENDROLL);
	modes_flow_endroll_exit();
	CHECK(settle() == SCENE_MENU);
	CHECK(modes_arcade_cleared(modes_save(), 2, 3));
	CHECK(modes_save()->arcadeClears[2] == 1);
	/* Sora (3..8) arcade clear unlocks Star Breaker */
	int found = 0;
	for (int i = 0; i < modes_flow_msg_count(); i++)
		found |= modes_flow_msg(i) == 1000 + 9;
	CHECK(found && modes_save()->charOpen[9]);
	modes_flow_msgs_clear();
	CHECK(modes_flow_msg_count() == 0);
}

static void test_arcade_game_over_and_quit(void)
{
	reg_all(1);
	modes_flow_init(5, NULL);
	g_setup.mode = MODE_ARCADE;
	g_setup.difficulty = 0;
	modes_flow_start(4, 0);
	settle();
	fake_match(1, 15000);
	modes_flow_game_exit(MODES_EXIT_DECIDED, &w);
	settle();
	modes_flow_result_exit(0, 0);
	settle();
	fake_match(0, 9000);
	modes_flow_game_exit(MODES_EXIT_DECIDED, &w);
	CHECK(settle() == SCENE_RESULT && modes_flow_lost());
	modes_flow_result_exit(1, 0); /* countdown ran out */
	CHECK(settle() == SCENE_MENU && !modes_flow_active());
	CHECK(modes_flow_run()->rank == 0 && modes_save()->rank[0][0].score == 15000);

	modes_flow_start(4, 0);
	settle();
	modes_flow_game_exit(MODES_EXIT_QUIT, NULL);
	CHECK(settle() == SCENE_ARC_SELECT && !modes_flow_active());
}

static void test_story(void)
{
	reg_all(1);
	modes_flow_init(7, NULL);
	g_setup.mode = MODE_STORY;
	g_setup.difficulty = 1;
	modes_flow_start(7, 0); /* Sham */
	CHECK(settle() == SCENE_CONVERSATION);
	CHECK(modes_flow_conv_script() == 50 && modes_flow_conv_backdrop() == 15);
	modes_flow_conv_exit();
	CHECK(settle() == SCENE_BATTLE && g_setup.chara[1] == 3); /* fixed first opponent Sora */
	int fights = 0, convs = 1;
	for (int guard = 0; guard < 60 && scene_current() != SCENE_ENDROLL; guard++) {
		switch (scene_current()) {
		case SCENE_BATTLE:
			fights++;
			if (g_setup.chara[1] == AOS_CH_SUMIKA2)
				CHECK(g_setup.stage == 0x13 && g_setup.timer == 999);
			fake_match(1, 10000);
			modes_flow_game_exit(MODES_EXIT_DECIDED, &w);
			break;
		case SCENE_RESULT: modes_flow_result_exit(0, 0); break;
		case SCENE_CONVERSATION:
			convs++;
			CHECK(modes_flow_conv_script() == 50 + convs - 1);
			modes_flow_conv_exit();
			break;
		}
		settle();
	}
	CHECK(fights == 7 && convs == 4);
	CHECK(scene_current() == SCENE_ENDROLL);
	CHECK(modes_flow_next_scene() == SCENE_ENDROLL);
	modes_flow_endroll_exit();
	CHECK(settle() == SCENE_MENU && modes_flow_run()->rank == 11);
	CHECK(modes_story_cleared(modes_save(), 7) && modes_save()->storyClears == 1);
}

static void test_passthrough(void)
{
	reg_all(0); /* no Result / Conversation scenes yet */
	modes_flow_init(3, NULL);
	g_setup.mode = MODE_STORY;
	g_setup.difficulty = 1;
	modes_flow_start(10, 0);
	CHECK(settle() == SCENE_BATTLE); /* conversation skipped */
	fake_match(1, 10000);
	modes_flow_game_exit(MODES_EXIT_DECIDED, &w);
	CHECK(settle() == SCENE_BATTLE); /* result skipped */
	fake_match(0, 10000);
	modes_flow_game_exit(MODES_EXIT_DECIDED, &w);
	CHECK(settle() == SCENE_MENU && !modes_flow_active()); /* lost without a Result scene: game over */
}

static void test_versus(void)
{
	reg_all(1);
	modes_flow_init(3, NULL);
	g_setup.mode = MODE_MATCH;
	fake_match(1, 10000);
	modes_flow_game_exit(MODES_EXIT_DECIDED, &w);
	CHECK(settle() == SCENE_RESULT && modes_flow_result()->winner == 0 && !modes_flow_lost());
	modes_flow_result_exit(0, 0);
	CHECK(settle() == SCENE_VS_SELECT);
	modes_flow_game_exit(MODES_EXIT_RETRY, NULL);
	CHECK(settle() == SCENE_BATTLE && g_setup.new_match);
	modes_flow_game_exit(MODES_EXIT_QUIT, NULL);
	CHECK(settle() == SCENE_VS_SELECT);
}

static void test_save_file(const char *dir)
{
	char path[512];
	snprintf(path, sizeof path, "%s/flow_glue.sav", dir);
	remove(path);
	modes_flow_init(1, path);
	CHECK(modes_save()->dirty); /* missing: defaults written at once */
	CHECK(modes_save_poll() == 1 && !modes_save()->dirty);
	CHECK(modes_save_poll() == 0);
	modes_save()->wins = 42;
	modes_save()->dirty = 1;
	CHECK(modes_save_poll() == 1);
	modes_flow_init(2, path);
	CHECK(modes_save()->wins == 42 && !modes_save()->dirty);
	remove(path);
}

int main(int argc, char **argv)
{
	test_arcade_full();
	test_arcade_no_continue_secret_stage();
	test_arcade_game_over_and_quit();
	test_story();
	test_passthrough();
	test_versus();
	test_save_file(argc > 1 ? argv[1] : ".");
	if (fails) {
		printf("test_flow_glue: %d FAILED\n", fails);
		return 1;
	}
	printf("test_flow_glue: ok\n");
	return 0;
}
