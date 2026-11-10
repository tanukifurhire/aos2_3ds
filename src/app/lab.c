/*
 * AoS2 3DS - "input lab" vertical slice.
 *
 * Purpose: validate the asset pipeline (decrypted AoS2 sprites + SE running on
 * 3DS) and let the controls be judged in an emulator/real hardware. Movement
 * speeds and timings here are PLACEHOLDERS, not the original game's values;
 * the real mechanics are still compiled inside aos2.exe (see docs/STATUS.md).
 *
 * Controls: Circle Pad / D-Pad move, L Dash, R Hyper, A/B Weapon A/B,
 * X/Y Special A/B, L+R Shield, L,L Hyper Dash.
 * SELECT + Up/Down: chord window, SELECT + Left/Right: double-tap window.
 * START: exit.
 */
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "../input/aos_input.h"
#include "audio.h"
#include "../platform/bgm.h"
#include "../platform/spritebank.h"

#define TOP_W 400
#define TOP_H 240
#define SPRITE_SCALE 0.4f /* in original PC pixels: 256px frame -> ~102px on the 400x240 screen */
#define LAB_BGM 29     /* bgm_suguri_00 (docs/re/bgm_table.tsv) */

/* Placeholder tuning (pixels/frame at 60 fps). */
#define SPEED_NORMAL 2.0f
#define SPEED_DASH 4.5f
#define SPEED_HYPER_DASH 7.0f
#define HYPER_DASH_FRAMES 20
#define HYPER_FLASH_FRAMES 30

typedef enum { ANIM_STAND, ANIM_FWD, ANIM_BCK, ANIM_DASH, ANIM_SHIELD } anim_id;

typedef struct {
	const char *fmt; /* texture name pattern, e.g. "sg_std%02d" */
	int count, frame_len;
	bool loop;
} anim_def;

static const anim_def anims[] = {
	[ANIM_STAND] = {"sg_std%02d", 3, 8, true},
	[ANIM_FWD] = {"sg_fwd%02d", 3, 6, true},
	[ANIM_BCK] = {"sg_bck%02d", 3, 6, true},
	[ANIM_DASH] = {"sg_dsh%02d", 9, 3, true},
	[ANIM_SHIELD] = {"sg_sld%02d", 6, 4, false},
};

typedef struct {
	float x, y;
	bool face_left;
	anim_id anim;
	int anim_t;
	int hyper_dash_t; /* frames of hyper-dash burst remaining */
	int hyper_flash_t;
	float dir_x, dir_y; /* last non-zero move direction (unit) */
} player;

static void set_anim(player *p, anim_id a)
{
	if (p->anim != a) {
		p->anim = a;
		p->anim_t = 0;
	}
}

static const aos_frame *anim_frame(const player *p)
{
	const anim_def *d = &anims[p->anim];
	int i = p->anim_t / d->frame_len;
	i = d->loop ? i % d->count : (i < d->count ? i : d->count - 1);
	char name[24];
	snprintf(name, sizeof name, d->fmt, i);
	return spritebank_get(name);
}

static void log_events(u32 frame, u32 ev)
{
	static const char *const names[] = {
		"DASH", "HYPER DASH", "HYPER", "SHIELD",
		"WEAPON A", "WEAPON B", "SPECIAL A", "SPECIAL B",
	};
	for (int i = 0; i < 8; i++)
		if (ev & (1u << i))
			printf("\x1b[2K%6lu  %s\n", (unsigned long)frame, names[i]);
}

static void print_header(const aos_input_cfg *c)
{
	printf("\x1b[1;1H\x1b[2K\x1b[36mAoS2 3DS input lab\x1b[0m  START=exit\n");
	printf("\x1b[2K chord window: %2u f (SEL+Up/Dn)\n", c->chord_window);
	printf("\x1b[2K double-tap:   %2u f (SEL+L/R)\n", c->double_tap_window);
	printf("\x1b[2K------------------------------------");
}

int lab_run(C3D_RenderTarget *top, const char *aerr)
{
	PrintConsole bottom;
	consoleInit(GFX_BOTTOM, &bottom);
	consoleSetWindow(&bottom, 0, 4, 40, 26);
	PrintConsole header;
	consoleInit(GFX_BOTTOM, &header);
	consoleSetWindow(&header, 0, 0, 40, 4);

	if (!spritebank_load("charaSuguriA"))
		svcBreak(USERBREAK_PANIC);

	consoleSelect(&bottom);
	if (aerr)
		printf("\x1b[33maudio: %s\x1b[0m\n", aerr);
	else if (!bgm_play(LAB_BGM))
		printf("\x1b[33mbgm %d failed\x1b[0m\n", LAB_BGM);

	aos_input_cfg cfg = {AOS_INPUT_DEFAULT_CHORD_WINDOW, AOS_INPUT_DEFAULT_DOUBLE_TAP_WINDOW};
	aos_input in;
	aos_input_init(&in, &cfg);
	consoleSelect(&header);
	print_header(&cfg);
	consoleSelect(&bottom);

	player p = {.x = TOP_W / 2, .y = TOP_H / 2, .dir_x = 1};
	u32 frame = 0;
	float grid_off_x = 0, grid_off_y = 0;

	while (aptMainLoop()) {
		hidScanInput();
		u32 kd = hidKeysDown(), kh = hidKeysHeld();
		if (kd & KEY_START)
			break;

		/* Live tuning of the input windows. */
		bool tuning = kh & KEY_SELECT;
		if (tuning && (kd & (KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT))) {
			if ((kd & KEY_DUP) && cfg.chord_window < 15) cfg.chord_window++;
			if ((kd & KEY_DDOWN) && cfg.chord_window > 0) cfg.chord_window--;
			if ((kd & KEY_DRIGHT) && cfg.double_tap_window < 60) cfg.double_tap_window++;
			if ((kd & KEY_DLEFT) && cfg.double_tap_window > 1) cfg.double_tap_window--;
			/* Windows only change between presses: re-init resets pending
			 * state cleanly instead of reinterpreting an in-flight press. */
			aos_input_init(&in, &cfg);
			consoleSelect(&header);
			print_header(&cfg);
			consoleSelect(&bottom);
		}

		aos_input_out o = aos_input_update(&in, aos_input_from_3ds(kh));
		log_events(frame, o.events);
		if (o.events & AOS_EV_WEAPON_A) audio_play(SFX_WEAPON_A);
		if (o.events & AOS_EV_WEAPON_B) audio_play(SFX_WEAPON_B);
		if (o.events & AOS_EV_SPECIAL_A) audio_play(SFX_SPECIAL_A);
		if (o.events & AOS_EV_SPECIAL_B) audio_play(SFX_SPECIAL_B);
		if (o.events & AOS_EV_DASH) audio_play(SFX_DASH);
		if (o.events & AOS_EV_HYPER_DASH) {
			audio_play(SFX_HYPER_DASH);
			p.hyper_dash_t = HYPER_DASH_FRAMES;
		}
		if (o.events & AOS_EV_HYPER) {
			audio_play(SFX_HYPER);
			p.hyper_flash_t = HYPER_FLASH_FRAMES;
		}
		if (o.events & AOS_EV_SHIELD) audio_play(SFX_SHIELD);

		/* Movement direction from circle pad, D-pad as fallback. */
		circlePosition cp;
		hidCircleRead(&cp);
		float mx = cp.dx / 156.0f, my = -cp.dy / 156.0f;
		if (fabsf(mx) < 0.15f) mx = 0;
		if (fabsf(my) < 0.15f) my = 0;
		if (!tuning) {
			if (kh & KEY_DLEFT) mx = -1;
			if (kh & KEY_DRIGHT) mx = 1;
			if (kh & KEY_DUP) my = -1;
			if (kh & KEY_DDOWN) my = 1;
		}
		float len = sqrtf(mx * mx + my * my);
		if (len > 1) {
			mx /= len;
			my /= len;
			len = 1;
		}
		if (len > 0) {
			p.dir_x = mx / len;
			p.dir_y = my / len;
		}

		bool shielding = o.held & AOS_HELD_SHIELD;
		bool dashing = (o.held & AOS_HELD_DASH) || p.hyper_dash_t > 0;
		float vx = 0, vy = 0;
		if (shielding) {
			set_anim(&p, ANIM_SHIELD);
		} else if (dashing) {
			/* Dash keeps going in the last direction even with the stick idle. */
			float s = p.hyper_dash_t > 0 ? SPEED_HYPER_DASH : SPEED_DASH;
			vx = p.dir_x * s;
			vy = p.dir_y * s;
			set_anim(&p, ANIM_DASH);
		} else if (len > 0) {
			vx = mx * SPEED_NORMAL;
			vy = my * SPEED_NORMAL;
			bool toward_facing = p.face_left ? vx < 0 : vx > 0;
			set_anim(&p, (vx == 0 || toward_facing) ? ANIM_FWD : ANIM_BCK);
		} else {
			set_anim(&p, ANIM_STAND);
		}
		if (vx < -0.01f) p.face_left = true;
		if (vx > 0.01f) p.face_left = false;
		if (p.hyper_dash_t > 0) p.hyper_dash_t--;
		if (p.hyper_flash_t > 0) p.hyper_flash_t--;
		p.anim_t++;

		/* The camera follows the player; the grid scroll shows motion. */
		grid_off_x = fmodf(grid_off_x - vx, 40.0f);
		grid_off_y = fmodf(grid_off_y - vy, 40.0f);
		p.x = TOP_W / 2;
		p.y = TOP_H / 2;

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		C2D_TargetClear(top, C2D_Color32(0x08, 0x0c, 0x24, 0xff));
		C2D_SceneBegin(top);
		C2D_DrawRectangle(0, 0, 0, TOP_W, TOP_H,
		                  C2D_Color32(0x10, 0x18, 0x48, 0xff), C2D_Color32(0x10, 0x18, 0x48, 0xff),
		                  C2D_Color32(0x04, 0x06, 0x14, 0xff), C2D_Color32(0x04, 0x06, 0x14, 0xff));
		u32 gc = C2D_Color32(0x30, 0x50, 0xa0, 0x60);
		for (float gx = grid_off_x - 40; gx < TOP_W + 40; gx += 40)
			C2D_DrawRectSolid(gx, 0, 0, 1, TOP_H, gc);
		for (float gy = grid_off_y - 40; gy < TOP_H + 40; gy += 40)
			C2D_DrawRectSolid(0, gy, 0, TOP_W, 1, gc);

		const aos_frame *fr = anim_frame(&p);
		if (fr) {
			C2D_Sprite spr;
			C2D_SpriteFromImage(&spr, fr->img);
			C2D_SpriteSetCenter(&spr, 0.5f, 0.5f);
			C2D_SpriteSetPos(&spr, p.x, p.y);
			float s = SPRITE_SCALE * (float)fr->ow / (float)fr->w;
			C2D_SpriteSetScale(&spr, p.face_left ? -s : s, s);
			if (fr->premul) { /* atlas stored premultiplied (aos2pack -pma, spritebank.h) */
				C2D_Flush();
				C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE,
				               GPU_ONE_MINUS_SRC_ALPHA);
			}
			C2D_DrawSprite(&spr);
			if (fr->premul) {
				C2D_Flush();
				C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA,
				               GPU_ONE_MINUS_SRC_ALPHA);
			}
		}

		if (shielding)
			C2D_DrawCircleSolid(p.x, p.y, 0, 46, C2D_Color32(0x60, 0xc0, 0xff, 0x50));
		if (p.hyper_flash_t > 0) {
			u8 a = (u8)(0x90 * p.hyper_flash_t / HYPER_FLASH_FRAMES);
			C2D_DrawRectSolid(0, 0, 0, TOP_W, TOP_H, C2D_Color32(0xff, 0xff, 0xff, a));
		}
		C3D_FrameEnd(0);
		frame++;
	}

	bgm_stop();
	spritebank_unload("charaSuguriA");
	return 0;
}
