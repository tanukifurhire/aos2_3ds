/*
 * AoS2 3DS - entry point: menu, character select, match loop.
 *
 * Controls (user mapping, aos_input.h): L Dash, R Hyper, A/B Weapon A/B,
 * X/Y Special A/B (= S+A / S+B), L+R Shield, Circle Pad / D-Pad move.
 * They are converted into the original game's per-frame button mask
 * (core.md §2.1) so the simulation sees exactly what the PC game would.
 */
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string.h>

#include "../input/aos_input.h"
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
#include "music.h"
#include "netplay.h"
#include "prof.h"

int lab_run(C3D_RenderTarget *top, const char *aerr);

static aos_world world; /* ~0.5 MB: static, not on the stack */

static const int roster[] = {10, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17, 18};
#define NROSTER ((int)(sizeof roster / sizeof roster[0]))

/* User button layout -> original input mask (also used by netplay.c). */
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
	if (o->held & AOS_HELD_HYPER) m |= AOS_IN_H;
	if (o->held & AOS_HELD_SHIELD) m |= AOS_IN_SHIELD;
	return m;
}

/* Sound effects of this frame, then the per-frame SE/BGM updates in the
 * original's main-loop order (scene, FUN_007bcc00, FUN_004beeb0). */
static void play_frame_sounds(const aos_world *w)
{
	for (int i = 0; i < w->se_count; i++)
		audio_se(w->se[i], w->se_cd[i]);
	music_watch(w);
	audio_se_frame();
	music_update();
}

static void battle_bgm(void)
{
	/* VS Select Exit picks the track (FUN_0079bfc0), Game Init starts it (FUN_005f4650) */
	int bgm = music_resolve();
	music_battle_start(bgm);
	printf("BGM: %s (#%d)\n", music_name(bgm), bgm);
}

/* Stage sound effects (thunder, explosions: FUN_007bc9c0 from the stage update). */
static void stage_se_cb(int id) { audio_se(id, AUDIO_SE_CD_DEFAULT); }

static void match_run(C3D_RenderTarget *top, PrintConsole *con, int p1, int p2, int stage)
{
	int32_t ids[2] = {p1, p2}, sch[2] = {0, 0}, col[2] = {0, p1 == p2 ? 1 : 0};
	aos_match_init(&world, ids, sch, col, 180, 2);
	world.p[1].inputSrc = 10; /* P2 = CPU (falls back to idle for characters without ported AI) */
	world.difficulty = 1;
	bench_setup(&world); /* profile builds: CPU vs CPU benchmark (prof.h) */
	consoleSelect(con);
	consoleClear();
	bool texfail = !render_load_match(&world);
	if (texfail)
		printf("\x1b[31mtexture load failed\x1b[0m\n");
	printf("\x1b[36m%s vs %s\x1b[0m   START: menu\n", render_char_name(p1), render_char_name(p2));
	if (!aos_tab.inputA[p1])
		printf("\x1b[33m%s is not ported yet\x1b[0m\n", render_char_name(p1));
	printf("L dash  R hyper  A/B weapons\nX/Y specials  L+R shield\n");
	battle_bgm();
	stage3ds_begin(stage); /* Game Init: loader + FUN_004bfc10 (random resolved like FUN_007aaf90) */
	printf("Stage: %s\n", stage_name(stage_current()));
	audio_se_prepare(p1, p2, stage_current()); /* SE residency for this match (audio.h) */
	/* bottom screen: match panel (heat/combo + status lines) instead of the console */
	panel_begin(con);
	panel_line(0, C2D_Color32(110, 220, 255, 255), "%s vs %s     START: menu", render_char_name(p1), render_char_name(p2));
	panel_line(1, C2D_Color32(220, 220, 220, 255), "L dash  R hyper  A/B weapons  X/Y specials  L+R shield");
	panel_line(2, C2D_Color32(220, 220, 220, 255), "Stage: %s", stage_name(stage_current()));
	if (texfail)
		panel_line(3, C2D_Color32(255, 90, 90, 255), "texture load failed");
	else if (!aos_tab.inputA[p1])
		panel_line(3, C2D_Color32(255, 220, 80, 255), "%s is not ported yet", render_char_name(p1));

	aos_input in;
	aos_input_init(&in, NULL);
	prof_match_begin(&world, stage);
	while (aptMainLoop()) {
		prof_frame_start();
		hidScanInput();
		u32 kd = hidKeysDown(), kh = hidKeysHeld();
		if ((kd & KEY_START) || bench_done())
			break;
		circlePosition cp;
		hidCircleRead(&cp);
		aos_input_out o = aos_input_update(&in, aos_input_from_3ds(kh));
		uint16_t masks[2] = {to_mask(&o, kh, &cp), 0};
		bench_pre_tick(&world);
		PROF_BEGIN(PROF_TICK);
		aos_tick(&world, masks);
		PROF_END(PROF_TICK);
		PROF_BEGIN(PROF_STAGE_UPD);
		stage3ds_tick(world.obj.zoomView, world.frame); /* FUN_004bfd30, tick step 7 */
		PROF_END(PROF_STAGE_UPD);
		PROF_BEGIN(PROF_AUDIO);
		play_frame_sounds(&world);
		PROF_END(PROF_AUDIO);

		/* Rounds advance inside aos_tick (FUN_00608a00, src/sim/flow.c). Once the
		 * match-end wipe covers the screen the PC shows the result screen; here
		 * the next match starts right away. */
		if (world.flow.matchEnd) {
			panel_line(4, C2D_Color32(120, 255, 120, 255), "%s wins!", aos_match_winner(&world) == 1 ? "P2" : "P1");
			aos_match_restart(&world);
			battle_bgm(); /* next match: result/select screens are skipped (music.md §5) */
		}

		PROF_BEGIN(PROF_WAIT);
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		PROF_END(PROF_WAIT);
		render_world(top, &world);
		PROF_BEGIN(PROF_PANEL);
		panel_draw(&world);
		PROF_END(PROF_PANEL);
		PROF_BEGIN(PROF_FRAMEEND);
		C3D_FrameEnd(0);
		PROF_END(PROF_FRAMEEND);
		prof_frame_done(&world);
		if (kh & KEY_SELECT) { /* hold SELECT: stage cost readout (stages.md §5) */
			static unsigned n, su, sd, sr;
			static float cpu;
			su += stage3ds_update_us;
			sd += stage3ds_last_us;
			sr += stage3ds_record_us;
			cpu += C3D_GetProcessingTime();
			if (++n == 60) {
				panel_line(5, C2D_Color32(180, 180, 255, 255), "stage upd %uus rec %uus draw %uus cpu %.1fms", su / n,
				           sr / n, sd / n, (double)(cpu / n));
				n = su = sd = sr = 0;
				cpu = 0;
			}
		}
	}
	prof_match_end();
	panel_end(con);
	stage3ds_end();
	render_unload_match();
	music_battle_exit();
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
	PrintConsole con;
	consoleInit(GFX_BOTTOM, &con);

	const char *aerr = NULL;
	if (audio_init(&aerr))
		bgm_init();
	aos_register_all();
	if (!stage3ds_init())
		printf("stage shader missing\n");
	stage_g.se = stage_se_cb;
	music_init();
	music_scene_select();

#ifdef AOS_PROFILE
	if (bench_active()) { /* sdmc:/aos2_bench.txt: run the scenarios, then quit (prof.h) */
		int b1, b2, bs;
		while (aptMainLoop() && bench_next(&b1, &b2, &bs))
			match_run(top, &con, b1, b2, bs);
		goto quit;
	}
#endif
	int sel[2] = {0, 0}, side = 0, net_delay = 2;
	int stage_sel = 0; /* index into stage_vs_list (select-screen order, FUN_0079acc0) */
	bool redraw = true;
	while (aptMainLoop()) {
		hidScanInput();
		u32 kd = hidKeysDown();
		if (kd & KEY_START)
			break;
		if (kd & (KEY_DLEFT | KEY_DRIGHT | KEY_DUP | KEY_DDOWN)) {
			if (side == 2) { /* stage row */
				if (kd & KEY_DRIGHT) stage_sel = (stage_sel + 1) % stage_vs_count;
				if (kd & KEY_DLEFT) stage_sel = (stage_sel + stage_vs_count - 1) % stage_vs_count;
			} else if (side == 3) { /* city sprite option (stages 12/14, presentation only) */
				if (kd & KEY_DRIGHT) stage_g.lod = (stage_g.lod + 1) % STAGE_LOD_COUNT;
				if (kd & KEY_DLEFT) stage_g.lod = (stage_g.lod + STAGE_LOD_COUNT - 1) % STAGE_LOD_COUNT;
			} else {
				if (kd & KEY_DRIGHT) sel[side] = (sel[side] + 1) % NROSTER;
				if (kd & KEY_DLEFT) sel[side] = (sel[side] + NROSTER - 1) % NROSTER;
			}
			if (kd & KEY_DDOWN) side = (side + 1) % 4;
			if (kd & KEY_DUP) side = (side + 3) % 4;
			redraw = true;
		}
		if (kd & (KEY_L | KEY_R)) { /* BGM select (the original's stage-select up/down) */
			music_sel_step((kd & KEY_L) ? -1 : 1);
			redraw = true;
		}
		if (kd & (KEY_CPAD_UP | KEY_CPAD_DOWN)) { /* config BGM volume 0..10 */
			music_set_level(music_level() + ((kd & KEY_CPAD_UP) ? 1 : -1));
			audio_se(2, AUDIO_SE_CD_DEFAULT);
			redraw = true;
		}
		if (kd & (KEY_CPAD_RIGHT | KEY_CPAD_LEFT)) { /* config SE volume 0..10 */
			audio_set_se_level(audio_se_level() + ((kd & KEY_CPAD_RIGHT) ? 1 : -1));
			audio_se(2, AUDIO_SE_CD_DEFAULT);
			redraw = true;
		}
		if (kd & KEY_A) {
			match_run(top, &con, roster[sel[0]], roster[sel[1]], stage_vs_list[stage_sel]);
			music_scene_select();
			redraw = true;
		}
		if (kd & (KEY_Y | KEY_B)) { /* local wireless versus: you play your P1 pick */
			netplay_run(top, &con, (kd & KEY_Y) != 0, roster[sel[0]], net_delay, stage_vs_list[stage_sel]);
			music_scene_select();
			redraw = true;
		}
		if (kd & KEY_SELECT) {
			net_delay = (net_delay + 1) % 5;
			redraw = true;
		}
		if (kd & KEY_X) {
			consoleClear();
			lab_run(top, aerr);
			consoleInit(GFX_BOTTOM, &con);
			music_scene_select();
			redraw = true;
		}
		if (redraw) {
			redraw = false;
			consoleSelect(&con);
			consoleClear();
			printf("\x1b[36mAcceleration of SUGURI 2 - 3DS (WIP)\x1b[0m\n\n");
			for (int s = 0; s < 2; s++) {
				int id = roster[sel[s]];
				printf("%s P%d: %-14s %s\n", s == side ? ">" : " ", s + 1, render_char_name(id),
				       aos_tab.inputA[id] ? "" : "(not ported)");
			}
			printf("%s Stage: %s\n", side == 2 ? ">" : " ", stage_name(stage_vs_list[stage_sel]));
			printf("%s City sprites: %s\n", side == 3 ? ">" : " ", stage_lod_names[stage_g.lod]);
			printf("\nLeft/Right: change  Up/Down: P1/P2/stage\n");
			printf("A: versus CPU (normal difficulty)\nX: input lab   START: quit\n");
			printf("\nLocal wireless (you = P1 pick):\nY: host match   B: join match\n");
			printf("SELECT: input delay %d f (host's is used)\n", net_delay);
			printf("\nL/R BGM: %.31s\n", music_name(music_list_id(music_sel())));
			printf("C-pad BGM vol %d (U/D)  SE vol %d (L/R)\n", music_level(), audio_se_level());
			if (aerr)
				printf("\n\x1b[33maudio: %s\x1b[0m\n", aerr);
		}
		audio_se_frame();
		music_update();
		stage3ds_menu_tick(); /* menu background = stage 4 (docs/re/stages.md) */
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		C2D_TargetClear(top, C2D_Color32(8, 10, 30, 255));
		C2D_SceneBegin(top);
		stage3ds_draw();
		C3D_FrameEnd(0);
	}
#ifdef AOS_PROFILE
quit:
#endif
	uds_link_exit();
	stage3ds_exit();
	bgm_exit();
	audio_exit();
	spritebank_exit();
	C2D_Fini();
	C3D_Fini();
	gfxExit();
	romfsExit();
	return 0;
}
