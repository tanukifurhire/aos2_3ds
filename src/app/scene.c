/* 3DS main loop of the scene framework (scene.h): input, the per-frame
 * post-scene calls in the original order, both screens, transitions and the
 * bottom-screen help line. */
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "../platform/panel.h"
#include "../platform/stage.h"
#include "audio.h"
#include "modes_flow.h"
#include "music.h"
#include "prof.h"
#include "scene.h"
#include "scene_core.h"
#include "text.h"
#include "ui.h"

static C3D_RenderTarget *top;
static PrintConsole *con;
static bool console_mode = true;


C3D_RenderTarget *scene_top(void) { return top; }
C3D_RenderTarget *scene_bottom(void) { return panel_target(); }
PrintConsole *scene_console(void) { return con; }

void scene_console_begin(void)
{
	if (!console_mode) {
		panel_end(con);
		console_mode = true;
	}
}

void scene_console_end(void)
{
	if (console_mode) {
		panel_begin(con);
		console_mode = false;
	}
}

void scene_setup(C3D_RenderTarget *t, PrintConsole *c)
{
	top = t;
	con = c;
}

static uint16_t read_buttons(void)
{
	u32 kh = hidKeysHeld();
	circlePosition cp;
	hidCircleRead(&cp);
	uint16_t b = 0;
	if ((kh & KEY_DUP) || cp.dy > 40) b |= MB_UP;
	if ((kh & KEY_DDOWN) || cp.dy < -40) b |= MB_DOWN;
	if ((kh & KEY_DLEFT) || cp.dx < -40) b |= MB_LEFT;
	if ((kh & KEY_DRIGHT) || cp.dx > 40) b |= MB_RIGHT;
	if (kh & KEY_A) b |= MB_A;
	if (kh & KEY_B) b |= MB_B;
	if (kh & KEY_X) b |= MB_X;
	if (kh & KEY_Y) b |= MB_Y;
	if (kh & KEY_L) b |= MB_L;
	if (kh & KEY_R) b |= MB_R;
	if (kh & KEY_START) b |= MB_START;
	if (kh & KEY_SELECT) b |= MB_SELECT;
	if (kh & KEY_TOUCH) {
		touchPosition tp;
		hidTouchRead(&tp);
		g_min.touch_x = tp.px;
		g_min.touch_y = tp.py;
		b |= MB_TOUCH;
	}
	return b;
}

/* FUN_00495170 on the bottom screen: gradient band + typed text. */
void announce_draw_bottom(void)
{
	float a = announce_alpha();
	if (a <= 0)
		return;
	const float yc = 218, W = 320;
	int hh = (int)(22.0f * a);
	ui_blend(1);
	ui_rect_grad(0, yc - (float)hh, W, yc + (float)hh, 0x80000000, 0xe1000000, 0xe1000000, 0xff000000);
	if (a < 1.0f)
		return;
	static char full[600], wrapped[640], typed[640];
	snprintf(full, sizeof full, "%s", announce_text());
	int lines = text_wrap(FONT_SYSTEM, full, 1.0f, W - 20, wrapped, sizeof wrapped);
	/* typed prefix in code points of the full text */
	announce_typed(typed, sizeof typed);
	int ncp = 0;
	for (const char *p = typed; *p; p++)
		ncp += ((unsigned char)*p & 0xc0) != 0x80;
	size_t i = 0;
	int k = 0;
	for (; wrapped[i]; i++) {
		if (((unsigned char)wrapped[i] & 0xc0) != 0x80) {
			if (k == ncp)
				break;
			k++;
		}
	}
	char shown[640];
	memcpy(shown, wrapped, i);
	shown[i] = 0;
	float adv = text_line_height(FONT_SYSTEM, 1.0f) - 2;
	if (lines > 3)
		lines = 3;
	text_draw_shadowed(FONT_SYSTEM, shown, W / 2, yc - adv * (float)(lines - 1) / 2.0f, TEXT_CENTER, 0xffffffff, 1.0f,
	                   0);
}

static void fade_overlay(float W)
{
	float a = scene_fade_alpha();
	if (a <= 0)
		return;
	ui_blend(1);
	ui_rect(0, 0, W, 240, ui_argb((int)(255.0f * a), scene_fade_white() ? 0xffffff : 0));
}

/* Hold SELECT outside a match: frame cost + memory readout on the bottom screen
 * (averages over 60 frames; emulator µs are an instruction-count proxy, PERF.md). */
static struct {
	unsigned n, top_us, bot_us, upd_us, stage_us;
	float cpu, gpu;
	char line[2][96];
} dbg;

static void debug_overlay(unsigned upd, unsigned topus, unsigned botus)
{
	dbg.upd_us += upd;
	dbg.top_us += topus;
	dbg.bot_us += botus;
	dbg.stage_us += stage3ds_last_us;
	dbg.cpu += C3D_GetProcessingTime();
	dbg.gpu += C3D_GetDrawingTime();
	if (++dbg.n == 60) {
		snprintf(dbg.line[0], sizeof dbg.line[0], "upd %uus top %uus (stage %uus) bot %uus", dbg.upd_us / 60, dbg.top_us / 60,
		         dbg.stage_us / 60, dbg.bot_us / 60);
		snprintf(dbg.line[1], sizeof dbg.line[1], "cpu %.1fms gpu %.1fms lin %luK fonts %uK tex %uK",
		         (double)(dbg.cpu / 60), (double)(dbg.gpu / 60), (unsigned long)(linearSpaceFree() / 1024),
		         (unsigned)(text_fonts_bytes() / 1024), (unsigned)(ui_mtex_bytes() / 1024));
		memset(&dbg, 0, offsetof(__typeof__(dbg), line));
	}
	ui_rect(0, 0, 320, 34, 0xc0000000);
	text_draw(FONT_SYSTEM, dbg.line[0], 4, 8, TEXT_LEFT, 0xffffffff, 1.0f, 312);
	text_draw(FONT_SYSTEM, dbg.line[1], 4, 25, TEXT_LEFT, 0xffffffff, 1.0f, 312);
}

static unsigned us_since(u64 t0) { return (unsigned)((svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000000)); }

static u32 clear_col(uint32_t c) { return c ? c : C2D_Color32(0, 0, 0, 255); }

void scene_main(int first)
{
	scene_core_start(first);
	scene_console_end();
	while (aptMainLoop()) {
		prof_frame_start();
		hidScanInput();
		minput_feed(&g_min, read_buttons());
		const scene_def *d = scene_core_begin_frame();
		if (d && d->run) {
			scene_console_begin();
			d->run();
			scene_console_end();
			if (!scene_switch_pending())
				scene_goto(scene_prev());
			if (!scene_core_end_frame())
				break;
			continue;
		}
		u64 t_upd = svcGetSystemTick();
		scene_core_update();
		unsigned upd_us = us_since(t_upd);
		/* post-scene calls in the original main-loop order */
		announce_update();      /* FUN_00495040 */
		PROF_BEGIN(PROF_AUDIO);
		audio_se_frame();       /* FUN_007bcc00 */
		music_update();         /* FUN_004beeb0 */
		PROF_END(PROF_AUDIO);

		PROF_BEGIN(PROF_WAIT);
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		PROF_END(PROF_WAIT);
		C2D_TargetClear(top, clear_col(d ? d->clear_top : 0));
		C2D_SceneBegin(top);
		ui_blend_reset();
		u64 t_top = svcGetSystemTick();
		if (d && d->draw_top)
			d->draw_top();
		unsigned top_us = us_since(t_top);
		C2D_SceneBegin(top);
		fade_overlay(400);
		C3D_RenderTarget *bot = panel_target();
		if (bot) {
			C2D_TargetClear(bot, clear_col(d ? d->clear_bottom : 0));
			C2D_SceneBegin(bot);
			ui_blend_reset();
			u64 t_bot = svcGetSystemTick();
			if (d && d->draw_bottom)
				d->draw_bottom();
			C2D_SceneBegin(bot);
			ui_blend_reset();
			announce_draw_bottom();
			fade_overlay(320);
			if ((hidKeysHeld() & KEY_SELECT) && scene_current() != SCENE_BATTLE)
				debug_overlay(upd_us, top_us, us_since(t_bot));
		}
		PROF_BEGIN(PROF_FRAMEEND);
		C3D_FrameEnd(0);
		PROF_END(PROF_FRAMEEND);
		if (scene_post_frame_hook)
			scene_post_frame_hook();
		scene_fade_update(); /* FUN_00561c70 */
		if (scene_current() != SCENE_BATTLE)
			modes_save_poll(); /* progress.sav when dirty, never during a match frame (modes_flow.h) */
		if (!scene_core_end_frame())
			break;
	}
	scene_console_begin();
}
