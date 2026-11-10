/*
 * Endroll (scene 11): ending + staff roll (docs/re/modes.md §D.4; 3DS layout §D.6).
 *   Init   FUN_00519ce0: 5 pieces {texture, src rect, x, y}, fade-in 5, BGM 0x15 (Sham's
 *          story) or 4 (ending theme)
 *   Update FUN_0051a060: state 0 (51 f) -> 100 (scroll 0.5 px/f until counter 5640; A /
 *          START or counter > 10000 -> 900 + fade 2) -> 900 (fade done) -> Exit
 *   Draw   FUN_0051a1d0: clear 0xff222222, logo (alpha 255 - (counter - 200)), pieces,
 *          additive
 *   Exit   FUN_00519f60: BGM fade out, clear records + save + step(0) (modes_flow).
 *
 * 3DS: the roll is laid out in the PC's 800x600 virtual space at s = 1 and the top screen
 * shows the 400x240 window centred on the virtual centre (screen = virtual - (200, 180)),
 * so the pieces keep the PC's pixel size (352 px wide) and all timings are unchanged.
 * Dst rects are the PC's, including its non-uniform piece scale: the 504 x 737/1021 px
 * source pages are drawn 352 x 352/448 (x 0.70, y 0.48/0.44) while the logo from the same
 * texture is drawn uniformly (x 0.5, y 0.5) — constants 176/224/24 at 0x817ab8/0x817abc/
 * 0x853b8c (verified with xref rd); i.e. the PC squeezes the credit pages vertically and
 * so does the port. Textures: romfs:/gfx/m3_staff<N>_0.t3x (tools/aos2pack/endroll.go),
 * the left 512x1024 of resMdl/ed_staff<N>.png scaled to 352x512, L8, 1 MB for all four,
 * loaded only while this scene runs. Bottom screen: plain 0xff222222.
 */
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string.h>

#include "../platform/stage.h"
#include "modes_flow.h"
#include "music.h"
#include "scene.h"
#include "text.h"
#include "ui.h"

#define ER_PIECES 5

typedef struct {
	int tex;            /* +0 index into ed_staff1..4 */
	int l, t, r, b;     /* +4 src RECT (inclusive right/bottom: FUN_0045bf30 adds 1.0) */
	float x, y;         /* +0x14 / +0x18 centre */
} er_piece;

static er_piece piece[ER_PIECES];
static int er_state, er_counter; /* DAT_009b0920, DAT_009b0924 */
static bool er_started;          /* port: first update ran (scroll/counter of the previous frame) */
static ui_tex er_tex[4];

/* virtual 800x600 (s = 1) -> top screen */
#define VW 800
#define VH 600
#define OX 200
#define OY 180

/* packed page: src px (0..512, 0..1024) -> texels (0..352, 0..512) of a 512x512 texture */
static void er_draw(int ti, float sl, float st, float sr, float sb, int dl, int dt, int dr, int db, unsigned argb)
{
	ui_tex *t = &er_tex[ti];
	if (!t->ok || !(argb >> 24))
		return;
	float x0 = (float)(dl - OX), y0 = (float)(dt - OY), x1 = (float)(dr - OX), y1 = (float)(db - OY);
	if (x1 <= 0 || y1 <= 0 || x0 >= 400 || y0 >= 240 || x1 <= x0 || y1 <= y0)
		return;
	const float W = t->tex.width, H = t->tex.height, sx = 352.0f / 512.0f, sy = 512.0f / 1024.0f;
	Tex3DS_SubTexture sub = {
		.width = (u16)(x1 - x0),
		.height = (u16)(y1 - y0),
		.left = sl * sx / W,
		.right = sr * sx / W,
		.top = 1.0f - st * sy / H,
		.bottom = 1.0f - sb * sy / H,
	};
	C2D_Image img = {&t->tex, &sub};
	C2D_ImageTint tint;
	C2D_PlainImageTint(&tint, ui_c2d(argb), 1.0f);
	C2D_DrawParams p = {.pos = {x0, y0, x1 - x0, y1 - y0}, .center = {0, 0}, .depth = UI_DEPTH, .angle = 0};
	C2D_SetTintMode(C2D_TintMult); /* L8: rgb = luminance x white, additive */
	C2D_DrawImage(img, &p, &tint);
	C2D_SetTintMode(C2D_TintSolid);
}

static void er_push(int i, int tex, int l, int t, int r, int b, float y)
{
	piece[i] = (er_piece){tex, l, t, r, b, (float)VW / 2.0f, y}; /* x = W / 2.0 (0x813a4c) */
}

static void endroll_init(void) /* FUN_00519ce0 */
{
	text_fonts_unload(); /* no text in this scene */
	ui_mtex_free_all();
	stage3ds_end();      /* no stage behind the roll (FUN_004bfbc0 before scene 11) */
	announce_hide();
	er_state = 0;
	er_counter = 0;
	er_started = false;
	float y = (float)VH + 400.0f;   /* H + 400s (0x8145f4) */
	er_push(0, 0, 4, 0x11c, 0x1fb, 0x3fc, y);
	y += 430.0f;                    /* 0x817ac0 */
	er_push(1, 1, 4, 4, 0x1fb, 0x400, y);
	y += 460.0f;                    /* 0x815dc0 */
	er_push(2, 2, 4, 4, 0x1fb, 0x400, y);
	y += 460.0f;
	er_push(3, 3, 4, 4, 0x1fb, 0x400, y);
	y += 784.0f;                    /* 0x817ac4 */
	er_push(4, 0, 4, 0x94, 0x1fb, 0xf3, y);
	for (int i = 0; i < 4; i++) {
		char path[48];
		snprintf(path, sizeof path, "romfs:/gfx/m3_staff%d_0.t3x", i + 1);
		ui_tex_load(&er_tex[i], path, true);
	}
	scene_fade(5);
	if (g_setup.mode == MODE_STORY && g_setup.chara[0] == 7)
		music_request(0x15, true, true); /* Sham's story: bgm_add_03 */
	else
		music_request(4, true, true);    /* bgm_03, ending theme */
}

static void endroll_update(void) /* FUN_0051a060 */
{
	if (er_started) { /* tail of the previous frame (after its draw): scroll + counter */
		if ((er_state == 100 || er_state == 900) && er_counter < 0x1608)
			for (int i = 0; i < ER_PIECES; i++)
				piece[i].y -= 0.5f; /* DAT_00853b1c * s */
		er_counter++;
	}
	er_started = true;
	if (er_state == 0) {
		if (er_counter > 0x32) {
			er_state = 100;
			er_counter = 0;
		}
	} else if (er_state == 100) {
		if (er_counter > 10000 || min_confirm()) {
			er_state = 900;
			scene_fade(2);
		}
	} else if (er_state == 900 && !scene_fade_busy()) {
		modes_flow_endroll_exit(); /* Exit: records, save, step(0) -> menu */
	}
}

static void endroll_draw_top(void) /* FUN_0051a1d0 (clear 0xff222222 by the framework) */
{
	ui_blend(2);
	int a = 0xff;
	if (er_counter > 200)
		a = 0xff - (er_counter - 200);
	if (a < 0)
		a = 0;
	/* logo: FUN_005957d0(W/2, H/2, 0x80, 0x18), src (0,0,0x1ff,0x5f) */
	er_draw(0, 0, 0, 0x1ff + 1, 0x5f + 1, VW / 2 - 128, VH / 2 - 24, VW / 2 + 128, VH / 2 + 24, (unsigned)a << 24 | 0xffffff);
	for (int i = 0; i < ER_PIECES; i++) {
		const er_piece *p = &piece[i];
		int hw = 176, hh = i == 0 ? 176 : i < 4 ? 224 : 24; /* 0x817ab8 / 0x817abc / 0x853b8c */
		int x = (int)p->x, y = (int)p->y;
		er_draw(p->tex, (float)p->l, (float)p->t, (float)p->r + 1, (float)p->b + 1, x - hw, y - hh, x + hw, y + hh,
		        0xffffffff);
	}
	ui_blend(1);
}

static void endroll_exit(void)
{
	music_request(MUSIC_NONE, true, true); /* FUN_004bee00(0,1,1) */
	for (int i = 0; i < 4; i++)
		ui_tex_free(&er_tex[i]);
}

static const scene_def endroll_scene = {
	.name = "endroll", .init = endroll_init, .update = endroll_update, .draw_top = endroll_draw_top,
	.exit = endroll_exit, .clear_top = 0xff222222, .clear_bottom = 0xff222222,
};

void endroll_register(void) { scene_register(SCENE_ENDROLL, &endroll_scene); }
