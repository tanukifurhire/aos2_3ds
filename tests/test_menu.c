/* Host test of the Logo/Menu state machines (src/app/menu.c, modes.md
 * §A.3-A.5) on the portable scene core: timings, SE ids, BGM requests, help
 * line ids, Menu Exit -> match setup + next scene, config rules. */
#include <stdio.h>
#include <string.h>

#include "../src/app/menu.h"
#include "../src/app/music.h"
#include "../src/app/scene.h"
#include "../src/app/scene_core.h"
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
static int last_se = -1, n_se, bgm_req = -1, se_lvl = 5, bgm_lvl = 5, lang_applied = -1;
void audio_se(int id, int cd) { (void)cd; last_se = id; n_se++; }
void audio_set_se_level(int l) { se_lvl = l; }
void music_request(int id, bool restart, bool loop) { (void)restart, (void)loop; bgm_req = id; }
int music_current(void) { return bgm_req; }
void music_set_level(int l) { bgm_lvl = l; }
void menu_apply_language(int lang) { lang_applied = lang; }
void menu_apply_city_lod(int lod) { (void)lod; }

static int menu_frames;
static void m_init(void) { menu_logic_init(); }
static void m_update(void) { menu_logic_update(); menu_frames++; }
static void l_init(void) { logo_logic_init(); }
static const scene_def logo = {.init = l_init, .update = logo_logic_update};
static const scene_def menu = {.init = m_init, .update = m_update};
static const scene_def dummy = {.name = "dummy"};

/* one frame: input bits, scene update, post-scene calls, transition update */
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

static void press(uint16_t b)
{
	frame(b);
	frame(0);
}

static int run_until_scene_change(int max)
{
	int s = scene_current();
	for (int i = 0; i < max; i++)
		if (frame(0) != s)
			return i + 1;
	return -1;
}

static void test_logo(void)
{
	scene_core_start(SCENE_LOGO);
	/* 51 + 200 + 20 + 1 + 200 + 20 + 1 (+1 switch) frames without input */
	int n = run_until_scene_change(2000);
	printf("  logo without input: %d frames\n", n);
	CHECK(n >= 490 && n <= 496);
	CHECK(scene_current() == SCENE_MENU);
	CHECK(scene_prev() == SCENE_LOGO);
	/* skipping both logos with A */
	scene_core_start(SCENE_LOGO);
	for (int i = 0; i < 60; i++)
		frame(0);
	press(MB_A);
	for (int i = 0; i < 30; i++)
		frame(0);
	press(MB_A);
	n = run_until_scene_change(100);
	CHECK(n > 0 && n < 30);
}

static void test_title_and_main(void)
{
	scene_core_start(SCENE_LOGO);
	run_until_scene_change(2000);
	frame(0); /* Menu Init */
	CHECK(g_menu.state == MS_TITLE);
	CHECK(bgm_req == -1); /* title is silent */
	/* title auto-advances after 300 frames */
	int i;
	for (i = 0; i < 400 && g_menu.state == MS_TITLE; i++)
		frame(0);
	CHECK(i >= 299 && i <= 302);
	CHECK(g_menu.state == MS_MAIN && bgm_req == MUSIC_MENU && last_se == 0);
	CHECK(g_menu.flash > 0.9f);
	CHECK(!strcmp(announce_text(), announce_by_id(0)));
	/* carousel wraps both ways, SE 2, help line follows */
	press(MB_LEFT);
	CHECK(g_menu.cursor == ITEM_EXIT && last_se == 2);
	CHECK(!strcmp(announce_text(), announce_by_id(8)));
	press(MB_RIGHT);
	press(MB_RIGHT);
	CHECK(g_menu.cursor == ITEM_STORY);
	/* glide: kick 1 -> 0 in 10 frames */
	CHECK(g_menu.kick < 1.0f && g_menu.kick > 0.7f);
	for (i = 0; i < 12; i++)
		frame(0);
	CHECK(g_menu.kick == 0);
	/* cancel jumps to Exit */
	press(MB_B);
	CHECK(g_menu.cursor == ITEM_EXIT);
	press(MB_RIGHT); /* -> Arcade */
	CHECK(g_menu.cursor == ITEM_ARCADE);
	/* logo alpha rises only in state 100; band in every state but the title */
	CHECK(g_menu.a_logo == 1.0f && g_menu.a_band == 1.0f);
}

static void test_arcade_exit(void)
{
	press(MB_A);
	CHECK(g_menu.state == MS_DIFFICULTY && last_se == 0);
	CHECK(announce_id_of("ANNOUNCE_SMENU_DIF") == 9 && !strcmp(announce_text(), announce_by_id(9)));
	press(MB_UP);
	CHECK(g_menu.diff == 2);
	press(MB_B);
	CHECK(g_menu.state == MS_MAIN && last_se == 1);
	press(MB_A);
	press(MB_DOWN); /* Hard -> wraps to Easy -> Normal */
	CHECK(g_menu.diff == 0 || g_menu.diff == 1);
	int want = g_menu.diff;
	press(MB_A);
	CHECK(g_menu.state == MS_LEAVING && scene_fade_busy());
	int n = run_until_scene_change(100);
	CHECK(n >= 19 && n <= 23); /* 20-frame fade to black */
	CHECK(scene_requested() == SCENE_ARC_SELECT);
	CHECK(g_setup.mode == MODE_ARCADE && g_setup.sub == SUB_CPU && g_setup.difficulty == want);
	CHECK(g_setup.input[1] == INPUT_CPU && g_setup.rounds == 1 && g_setup.timer == 180);
	CHECK(g_setup.handicap[0] == 2 && g_setup.handicap[1] == 2);
}

static void back_to_menu(void)
{
	menu_resume_next();
	scene_goto(SCENE_MENU);
	scene_core_end_frame();
	frame(0);
}

static void test_match_and_training(void)
{
	back_to_menu();
	CHECK(g_menu.state == MS_MAIN && g_menu.cursor == ITEM_ARCADE); /* resumed, cursor kept */
	press(MB_RIGHT);
	press(MB_RIGHT);
	press(MB_RIGHT);
	CHECK(g_menu.cursor == ITEM_MATCH);
	press(MB_A);
	CHECK(g_menu.state == MS_MATCH && g_setup.mode == MODE_MATCH);
	press(MB_DOWN);
	press(MB_A); /* CPU Game -> difficulty */
	CHECK(g_menu.state == MS_DIFFICULTY);
	press(MB_B); /* back to the match panel, not the main menu */
	CHECK(g_menu.state == MS_MATCH);
	press(MB_A);
	press(MB_A);
	run_until_scene_change(100);
	CHECK(scene_requested() == SCENE_VS_SELECT && g_setup.sub == SUB_CPU);
	CHECK(g_setup.rounds == g_config.required_wins + 1 && g_setup.timer == config_timer_seconds(g_config.time_limit));

	back_to_menu();
	press(MB_RIGHT);
	press(MB_A);
	CHECK(g_menu.state == MS_TRAINING);
	press(MB_A); /* How to Play */
	CHECK(g_menu.state == MS_MANUAL && g_menu.page == 0);
	press(MB_LEFT);
	CHECK(last_se == 3); /* blocked at the first page */
	for (int i = 0; i < 8; i++)
		press(MB_A);
	CHECK(g_menu.page == 8);
	press(MB_A); /* confirm on the last page returns */
	CHECK(g_menu.state == MS_TRAINING);
	press(MB_DOWN);
	press(MB_A); /* Practice */
	run_until_scene_change(100);
	CHECK(scene_requested() == SCENE_VS_SELECT && g_setup.mode == MODE_TRAINING && g_setup.timer == 999);
}

static void test_config(void)
{
	config_path = "build/tests/test_menu_config.ini";
	back_to_menu();
	while (g_menu.cursor != ITEM_CONFIG)
		press(MB_RIGHT);
	press(MB_A);
	CHECK(g_menu.state == MS_CONFIG && g_menu.cfg_cur == 0);
	CHECK(!strcmp(announce_text(), announce_by_id(13))); /* ANNOUNCE_CONFIG_LANGUAGE */
	press(MB_DOWN); /* Required wins */
	CHECK(menu_cfg_rows[g_menu.cfg_cur] == CR_REQUIREDWINS);
	g_config.tournament = true;
	press(MB_RIGHT);
	CHECK(g_config.required_wins == 2 && !g_config.tournament && last_se == 4);
	press(MB_RIGHT);
	CHECK(g_config.required_wins == 0); /* wraps */
	/* volumes clamp and apply */
	press(MB_DOWN);
	press(MB_DOWN);
	CHECK(menu_cfg_rows[g_menu.cfg_cur] == CR_SFX);
	for (int i = 0; i < 8; i++)
		press(MB_RIGHT);
	CHECK(g_config.se_volume == 10 && se_lvl == 10);
	/* tournament preset */
	while (menu_cfg_rows[g_menu.cfg_cur] != CR_TOURNAMENT)
		press(MB_DOWN);
	g_config.time_limit = 3;
	press(MB_LEFT);
	CHECK(g_config.tournament && g_config.time_limit == 1 && g_config.required_wins == 0);
	/* language copy applies on exit */
	while (g_menu.cfg_cur != 0)
		press(MB_UP);
	press(MB_RIGHT);
	CHECK(g_menu.cfg_lang == LANG_PTBR && g_config.language == LANG_EN);
	press(MB_B); /* cancel on a row -> Exit row */
	CHECK(menu_cfg_rows[g_menu.cfg_cur] == CR_EXIT && last_se == 1);
	press(MB_A);
	CHECK(g_menu.state == MS_MAIN && g_config.language == LANG_PTBR && lang_applied == LANG_PTBR);
	g_config.language = LANG_EN;
}

static void test_exit(void)
{
	while (g_menu.cursor != ITEM_EXIT)
		press(MB_RIGHT);
	press(MB_A);
	run_until_scene_change(100);
	CHECK(scene_requested() == SCENE_QUIT);
}

int main(void)
{
	if (!strings_init("extracted/script", LANG_EN)) {
		printf("test_menu: extracted/script missing\n");
		return 1;
	}
	config_defaults(&g_config);
	scene_register(SCENE_LOGO, &logo);
	scene_register(SCENE_MENU, &menu);
	scene_register(SCENE_COMING_SOON, &dummy);
	test_logo();
	test_title_and_main();
	test_arcade_exit();
	test_match_and_training();
	test_config();
	test_exit();
	if (fails) {
		printf("test_menu: %d failure(s)\n", fails);
		return 1;
	}
	printf("test_menu: ok\n");
	return 0;
}
