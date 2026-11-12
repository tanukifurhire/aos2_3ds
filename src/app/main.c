/*
 * AoS2 3DS - entry point: scene registration (scene.h), the Battle scene
 * (match loop) and the input lab; the local-wireless select/lobby is netsel.c.
 *
 * Flow (docs/re/modes.md §0.1): Logo -> Menu (title, main menu, sub-menus,
 * config) -> select -> Battle -> ...
 *
 * Controls (user mapping, aos_input.h): L Dash, R Shield (L+R too), A/B Weapon A/B,
 * X/Y Special A/B (= S+A / S+B), L,L re-dash, Circle Pad / D-Pad move; hypers on the
 * touch screen (bottom panel, src/input/hyper_touch.h). They are converted into the
 * original game's per-frame button mask (core.md §2.1) so the simulation sees exactly
 * what the PC game would.
 */
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "../input/aos_input.h"
#include "../input/hyper_touch.h"
#include "../platform/bgm.h"
#include "../platform/panel.h"
#include "../platform/render.h"
#include "../platform/spritebank.h"
#include "../platform/stage.h"
#include "../sim/draw.h"
#include "../sim/se_table.h"
#include "../sim/sim.h"
#include "../net/uds_link.h"
#include "audio.h"
#include "controls.h"
#include "conv_scene.h"
#include "menu.h"
#include "modes_flow.h"
#include "modes_scenes.h"
#include "music.h"
#include "netplay.h"
#include "prof.h"
#include "scene.h"
#include "scene_core.h"
#include "select.h"
#include "text.h"
#include "ui.h"

int lab_run(C3D_RenderTarget *top, const char *aerr);

static aos_world world; /* ~1 MB: static, not on the stack */
static const char *aerr;

/* Logical actions (the player's bindings already applied by aos_input_from_3ds, aos_input.h) -> original input
 * mask (also used by netplay.c). Circle Pad / D-pad = movement, not rebindable. */
uint16_t to_mask(const aos_input_out *o, u32 kh, const circlePosition *cp)
{
	uint16_t m = 0;
	int dx = cp->dx, dy = cp->dy;
	if ((kh & KEY_DLEFT) || dx < -40) m |= AOS_IN_LEFT;
	if ((kh & KEY_DRIGHT) || dx > 40) m |= AOS_IN_RIGHT;
	if ((kh & KEY_DUP) || dy > 40) m |= AOS_IN_UP;
	if ((kh & KEY_DDOWN) || dy < -40) m |= AOS_IN_DOWN;
	if (o->held & (AOS_HELD_WEAPON_A | AOS_HELD_SPECIAL_A)) m |= AOS_IN_A;
	if (o->held & (AOS_HELD_WEAPON_B | AOS_HELD_SPECIAL_B)) m |= AOS_IN_B;
	if (o->held & (AOS_HELD_SPECIAL_A | AOS_HELD_SPECIAL_B)) m |= AOS_IN_S;
	if (o->held & AOS_HELD_DASH) m |= AOS_IN_D; /* L,L = two Dash presses = the original's re-dash */
	/* H: the touch-screen hyper buttons (hyper_touch.h, OR'ed in by the caller) */
	if (o->held & AOS_HELD_SHIELD) m |= AOS_IN_SHIELD;
	return m;
}

/* Stage sound effects (thunder, explosions: FUN_007bc9c0 from the stage update). */
static void stage_se_cb(int id) { audio_se(id, AUDIO_SE_CD_DEFAULT); }

/* ================================================================ Battle (scene 2) */

static aos_input battle_in;
static ht_state battle_ht; /* touch hyper buttons -> P1's mask */
static bool battle_texfail;

static void battle_bgm(void)
{
	/* VS Select Exit picks the track (FUN_0079bfc0), Game Init starts it (FUN_005f4650) */
	music_battle_start(g_setup.bgm);
	printf("BGM: %s (#%d)\n", music_name(g_setup.bgm), g_setup.bgm);
}

static void battle_frame_done(void) { prof_frame_done(&world); }

#ifdef AOS_PROFILE
/* Profile builds: sdmc:/aos2_hyperpin.txt "level frame" pins P1's hyper gauge at `level` from match frame
 * `frame` on (screenshots of the touch hyper panel with a full gauge; like the bench's pinned gauges). */
static float pin_level = -1;
static int pin_from;
static void hyperpin_load(void)
{
	pin_level = -1;
	FILE *f = fopen("sdmc:/aos2_hyperpin.txt", "r");
	if (f) {
		if (fscanf(f, "%f %d", &pin_level, &pin_from) < 1)
			pin_level = -1;
		fclose(f);
	}
}
static void hyperpin_apply(void)
{
	if (pin_level >= 0 && world.frame >= pin_from)
		world.p[0].hyper = pin_level;
}
#else
#define hyperpin_load() ((void)0)
#define hyperpin_apply() ((void)0)
#endif

static void battle_init(void) /* Game Init FUN_005f4650 (app side) */
{
#ifdef AOS_PROFILE
	if (bench_active()) { /* sdmc:/aos2_bench.txt: run the scenarios, then quit (prof.h) */
		int b1, b2, bs;
		if (!bench_next(&b1, &b2, &bs)) {
			scene_goto(SCENE_QUIT);
			return;
		}
		g_setup.chara[0] = b1;
		g_setup.chara[1] = b2;
		g_setup.stage = bs;
		g_setup.color[0] = g_setup.color[1] = 3;
		g_setup.bgm = music_resolve();
		/* the menus never ran: give the match the old dev-select values (a zeroed g_setup had
		 * timer 0 -> the rounds never started and the benchmark only measured idle frames) */
		g_setup.mode = MODE_MATCH;
		g_setup.sub = SUB_CPU;
		g_setup.timer = 180;
		g_setup.rounds = 2;
		g_setup.handicap[0] = g_setup.handicap[1] = 2;
		g_setup.command[0] = g_setup.command[1] = 0;
		g_setup.input[0] = g_setup.input[1] = INPUT_CPU; /* bench_setup() also forces both to CPU */
	}
#endif
	/* menus' fonts/textures are not needed during the match (docs/PERF.md memory budget) */
	text_fonts_unload();
	ui_mtex_free_all();
	announce_hide();
	match_setup *s = &g_setup;
	aos_match_params m;
	aos_match_params_default(&m);
	for (int i = 0; i < 2; i++) {
		m.ids[i] = s->chara[i];
		m.schemes[i] = s->command[i];
		m.handicap[i] = s->handicap[i];
		m.inputSrc[i] = s->input[i];
	}
	/* colour 3 = auto: the second of two equal characters takes colour 1 */
	m.colors[0] = s->color[0] == 3 ? 0 : s->color[0];
	m.colors[1] = s->color[1] == 3 ? (s->chara[0] == s->chara[1] && m.colors[0] == 0 ? 1 : 0) : s->color[1];
	m.difficulty = s->difficulty;
	m.roundSeconds = s->timer;
	m.roundsToWin = s->rounds > 0 ? s->rounds : 2;
	if (modes_flow_active())
		modes_flow_match_params(&m); /* arcade/story: ladder_match_params (modes.md §D.3.1) */
	aos_match_init_ex(&world, &m);
	bench_setup(&world); /* profile builds: CPU vs CPU benchmark (prof.h) */
	int p1 = s->chara[0], p2 = s->chara[1];
	battle_texfail = !render_load_match(&world);
	battle_bgm();
	stage3ds_begin(s->stage); /* Game Init: loader + FUN_004bfc10 (random resolved like FUN_007aaf90) */
	audio_se_prepare(p1, p2, stage_current()); /* SE residency for this match (audio.h) */
	/* bottom screen: P1's touch hyper buttons (panel.h); debug lines only in profile builds (SELECT) */
	panel_clear_lines();
	panel_match_begin(&world, 0);
	if (battle_texfail)
		panel_line(0, C2D_Color32(255, 90, 90, 255), "texture load failed (%s/%s, linear free %luK)", render_player_folder(0),
		           render_player_folder(1), (unsigned long)(linearSpaceFree() / 1024));
	aos_input_init(&battle_in, NULL);
	ht_init(&battle_ht);
	hyperpin_load();
	pause_reset();
	prof_match_begin(&world, s->stage);
	scene_post_frame_hook = battle_frame_done;
}

#ifdef AOS_PROFILE
/* Profile builds: sdmc:/aos2_flow.txt "mode char difficulty frames" starts an arcade (0) / story (1)
 * run at boot and decides every fight after `frames` frames as a P1 win with a fake round record
 * (score 20000 + 1000 * fight) — exercises the whole flow (results, conversations, staff roll,
 * name entry, unlocks) without playing. Never in plain builds. */
static int flow_auto_frames;
static aos_world flow_fake;
static bool flow_auto_load(void)
{
	FILE *f = fopen("sdmc:/aos2_flow.txt", "r");
	if (!f)
		return false;
	int mode = 0, c = 10, diff = 1, skip = 0;
	bool ok = fscanf(f, "%d %d %d %d %d", &mode, &c, &diff, &flow_auto_frames, &skip) >= 4;
	fclose(f);
	if (!ok)
		return false;
	if (skip) { /* optional 5th field 1: pass through Result / Conversation (modes_flow.c) */
		scene_register(SCENE_RESULT, NULL);
		scene_register(SCENE_CONVERSATION, NULL);
	}
	g_setup.mode = mode == 1 ? MODE_STORY : MODE_ARCADE;
	g_setup.difficulty = diff;
	modes_flow_start(c, 0);
	return true;
}
static bool flow_auto_decide(void)
{
	int script = prof_script_decide(); /* sdmc:/aos2_keys.txt "decide [1|2]" (any mode) */
	if (!script && (flow_auto_frames <= 0 || !modes_flow_active() || (int)scene_frame() < flow_auto_frames))
		return false;
	memset(&flow_fake, 0, sizeof flow_fake);
	flow_fake.p[script == 2 ? 1 : 0].hp = 9000; /* the winner (aos_hp_winner) */
	flow_fake.nRoundRecs = 1;
	int idx = modes_flow_active() ? modes_flow_run()->index : 0;
	flow_fake.roundRec[0][0] = (aos_round_rec){10000, 30, 9000, 1000 * idx, 5, 0};
	flow_fake.roundRec[0][0].score = 20000 + 1000 * idx;
	for (int i = 0; i < 2; i++) {
		flow_fake.p[i].id = world.p[i].id;
		flow_fake.p[i].color = world.p[i].color;
	}
	result_capture(&flow_fake);
	modes_flow_game_exit(MODES_EXIT_DECIDED, &flow_fake);
	return true;
}
#else
#define flow_auto_decide() false
#endif

static void battle_update(void)
{
	u32 kh = hidKeysHeld() | prof_script_keys();
	if (bench_done()) {
		scene_goto(SCENE_BATTLE);
		return;
	}
	/* pause menu (FUN_0068ce30 / FUN_0068f4a0, Part B select.c): Retry / Quit leave through Game Exit */
	int pause = bench_active() ? PAUSE_OFF : pause_update(&world);
	if (pause == PAUSE_RETRY || pause == PAUSE_QUIT) {
		modes_flow_game_exit(pause == PAUSE_RETRY ? MODES_EXIT_RETRY : MODES_EXIT_QUIT, NULL);
		return;
	}
	if (pause == PAUSE_ON) {
		panel_touch_reset(); /* a touch must start after the pause */
		return; /* frozen: no tick, no stage update */
	}
	circlePosition cp;
	hidCircleRead(&cp);
	aos_input_out o = aos_input_update(&battle_in, aos_input_from_3ds(kh));
	hyperpin_apply();
	int slot = panel_touch(&world); /* hyper button held on the touch screen (latched at touch-down) */
	uint16_t masks[2] = {ht_apply(&battle_ht, world.p[0].id, world.p[0].scheme, slot, to_mask(&o, kh, &cp)), 0};
	bench_pre_tick(&world);
	PROF_BEGIN(PROF_TICK);
	aos_tick(&world, masks);
	PROF_END(PROF_TICK);
	PROF_BEGIN(PROF_STAGE_UPD);
	stage3ds_tick(world.obj.zoomView, world.frame); /* FUN_004bfd30, tick step 7 */
	PROF_END(PROF_STAGE_UPD);
	PROF_BEGIN(PROF_AUDIO);
	for (int i = 0; i < world.se_count; i++)
		audio_se(world.se[i], world.se_cd[i]);
	music_watch(&world);
	PROF_END(PROF_AUDIO);
	/* Rounds advance inside aos_tick (FUN_00608a00, src/sim/flow.c). Once the
	 * match-end wipe covers the screen the PC goes to the result screen (Part B);
	 * until that scene exists the next match starts right away. */
	if (flow_auto_decide())
		return;
	if (world.flow.matchEnd) {
		if (modes_flow_active() || (scene_get(SCENE_RESULT) && !bench_active())) {
			result_capture(&world); /* Game Exit: win quote (cosmetic RNG), sums, rank (Part B) */
			modes_flow_game_exit(MODES_EXIT_DECIDED, &world); /* Game Exit: ladder step / Result (modes_flow.h) */
		} else {
			aos_match_restart(&world);
			battle_bgm(); /* next match: result/select screens are skipped (music.md §5) */
		}
	}
#ifdef AOS_PROFILE
	if (kh & KEY_SELECT) { /* hold SELECT: stage cost readout (stages.md §5), profile builds */
		static unsigned n, su, sd, sr;
		static float cpu;
		su += stage3ds_update_us;
		sd += stage3ds_last_us;
		sr += stage3ds_record_us;
		cpu += C3D_GetProcessingTime();
		if (++n == 60) {
			panel_line(5, C2D_Color32(180, 180, 255, 255), "stage %uus/%uus/%uus cpu %.1fms lin %luK", su / n, sr / n, sd / n,
			           (double)(cpu / n), (unsigned long)(linearSpaceFree() / 1024));
			n = su = sd = sr = 0;
			cpu = 0;
		}
	}
#endif
}

static void battle_draw_top(void)
{
	render_world(scene_top(), &world);
	if (g_pause.on) {
		C2D_SceneBegin(scene_top());
		pause_draw_top();
	}
}

static void battle_draw_bottom(void)
{
	PROF_BEGIN(PROF_PANEL);
	panel_draw(&world);
	PROF_END(PROF_PANEL);
	if (g_pause.on) {
		C2D_SceneBegin(scene_bottom());
		pause_draw_bottom(world.p[0].id);
	}
}

static void battle_exit(void) /* Game Exit FUN_005f5780 (app side) */
{
	scene_post_frame_hook = NULL;
	prof_match_end();
	stage3ds_end();
	render_unload_match();
	music_battle_exit();
	panel_match_end();
	panel_clear_lines();
}

/* The match panel is the only thing on the bottom screen (outside pause): skip it while unchanged. */
static bool battle_bottom_same(void)
{
	static bool paused_last;
	bool same = !g_pause.on && !paused_last && panel_same(&world);
	paused_last = g_pause.on;
	return same;
}

static const scene_def battle_scene = {
	.name = "battle", .init = battle_init, .update = battle_update, .draw_top = battle_draw_top,
	.draw_bottom = battle_draw_bottom, .exit = battle_exit, .bottom_same = battle_bottom_same,
};

/* Local wireless (scene 7, select + lobby + match runner): netsel.c / netplay.c. */

/* ================================================================ input lab (port) */

static void lab_scene_run(void)
{
	text_fonts_unload();
	ui_mtex_free_all();
	stage3ds_end();
	consoleClear();
	lab_run(scene_top(), aerr);
	consoleInit(GFX_BOTTOM, scene_console());
	menu_resume_next();
	scene_goto(SCENE_MENU);
}

static const scene_def lab_scene = {.name = "input lab", .run = lab_scene_run};

/* ================================================================ main */

static void scenes_register_all(void)
{
	menu_scenes_register();           /* Part A: logo, menu, coming soon */
	scene_register(SCENE_BATTLE, &battle_scene);
	select_scenes_register(); /* Part B: ArcSelect (8), VS Select (6), Result (3) */
	netsel_register(); /* local wireless: select, lobby, match (netsel.c) */
	scene_register(SCENE_LAB, &lab_scene);
	endroll_register(); /* Part D: staff roll (endroll.c) */
	conv_scenes_register(); /* Part C: conversation (5) + port test picker (conv_scene.c) */
}

int main(void)
{
	romfsInit();
	osSetSpeedupEnable(true); /* New 3DS: 804 MHz + L2 cache (no-op on old models) */
	gfxInitDefault();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 4); /* stages issue hundreds of draws (city: ~540) */
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS * 4);
	C2D_Prepare();
	C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	static PrintConsole con;
	consoleInit(GFX_BOTTOM, &con);

	mkdir("sdmc:/3ds", 0777);
	mkdir("sdmc:/3ds/aos2_3ds", 0777);
	config_load(); /* config.ini (Part A); progress (unlocks/clears/ranking) is the separate progress.sav */
	modes_flow_init((uint32_t)svcGetSystemTick(), AOS_SAVE_PATH); /* cosmetic MT + save, before music_init */
	controls_apply_save(); /* button bindings + hyper layouts from the save (aos_input_from_3ds, hyper_touch) */
	if (audio_init(&aerr))
		bgm_init();
	aos_register_all();
	if (!stage3ds_init())
		printf("stage shader missing\n");
	stage_g.se = stage_se_cb;
	stage_g.lod = g_config.city_lod;
	music_init();
	music_set_level(g_config.bgm_volume);
	audio_set_se_level(g_config.se_volume);
	if (!strings_init("romfs:/script", g_config.language))
		printf("script text missing\n");

	scenes_register_all();
	scene_setup(top, &con);
	int first = bench_active() ? SCENE_BATTLE : SCENE_LOGO;
#ifdef AOS_PROFILE
	if (!bench_active() && flow_auto_load())
		first = modes_flow_next_scene();
#endif
	scene_main(first);

	uds_link_exit();
	stage3ds_exit();
	text_fonts_unload();
	ui_mtex_free_all();
	strings_exit();
	bgm_exit();
	audio_exit();
	spritebank_exit();
	C2D_Fini();
	C3D_Fini();
	gfxExit();
	romfsExit();
	return 0;
}
