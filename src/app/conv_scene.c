/*
 * Story conversation scene (scene 5) on the 3DS: docs/re/modes.md §C.
 *
 * Logic, text layout and the per-frame draw description are src/modes/conv.c
 * (portable, host-tested); this file loads the data, feeds the input, runs
 * the events and draws conv_view:
 *
 *   Init FUN_004e35e0   script + story text (current language), portraits
 *                       (FUN_004e4630), dialogue font, backdrop stage
 *                       (FUN_004bfc10(entry +8)), fade from black (type 5)
 *   frame FUN_004e3a20  conv_frame; stage update FUN_004bfd30 every frame
 *   draw FUN_004e38e0   top screen = the original composition at s = 0.4:
 *                       clear 0xff0055aa, stage, 黒地 fills, letterbox, per
 *                       slot noise band + portrait (mirrored slot 0, dimmed
 *                       when "＄"); bottom screen = the two text slots at a
 *                       legible size (§C.5/§C.6: 16 px dialogue font, boxes
 *                       304x96 upper / 272x96 lower indented 40, noise band
 *                       behind a slot whose band is on)
 *   Exit FUN_004e3730   free everything, stepper(0) = modes_flow_conv_exit()
 *
 * Keys: A = OK / attack (confirm: complete the text, then next act), hold X or
 * Y (the user's Special A/B, both carry S) = fast-forward every 5 frames, B =
 * cancel (only blocks the attack path, as the PC). START = skip the rest of
 * the script (port addition; same exit path: fade to black, then Exit).
 */
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../modes/conv.h"
#include "../modes/conv_bm.h"
#include "../platform/spritebank.h"
#include "../platform/stage.h"
#include "../sim/se_table.h"
#include "audio.h"
#include "bmfont.h"
#include "conv_scene.h"
#include "menu.h"
#include "modes_flow.h"
#include "music.h"
#include "scene.h"
#include "text.h"
#include "ui.h"

#ifdef AOS_PROFILE /* capture runner at the end of the file */
#include <sys/stat.h>

#include "scene_core.h"
static bool cap_on(void);
static void cap_hook(void);
static void cap_input(conv_input *in);
#endif

#define STORY_ROOT "romfs:/story"
#define TOP_W 400
#define TOP_H 240
#define DEPTH UI_DEPTH

/* ================================================================ dialogue font */

/* Per-language dialogue font (tools/aos2pack fonts.go / fonts_dialogue.go), loaded
 * for the scene only: en/ptbr/sp share "dialogue" (122 glyphs, 32 KB), the others
 * have their own (jp/chs/cht/ko 512 KB, ru 64 KB). */
static const char *font_name(int lang)
{
	switch (lang) {
	case LANG_JP: return "dialogue_jp";
	case LANG_CHS: return "dialogue_chs";
	case LANG_CHT: return "dialogue_cht";
	case LANG_KO: return "dialogue_ko";
	case LANG_RU: return "dialogue_ru";
	default: return "dialogue";
	}
}

static struct {
	bm_font fnt;
	ui_tex page;
	bool ok;
} df;

static bool file_exists(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (f)
		fclose(f);
	return f != NULL;
}

bool conv_lang_has_font(int lang)
{
	char p[64];
	snprintf(p, sizeof p, "romfs:/font/%s.fnt", font_name(lang));
	return lang >= 0 && lang < LANG_COUNT && file_exists(p);
}

static void dfont_free(void)
{
	if (df.ok) {
		ui_tex_free(&df.page);
		bm_free(&df.fnt);
	}
	df.ok = false;
}

static bool dfont_load(int lang)
{
	dfont_free();
	char path[80];
	snprintf(path, sizeof path, "romfs:/font/%s.fnt", font_name(lang));
	FILE *fp = fopen(path, "rb");
	if (!fp)
		return false;
	fseek(fp, 0, SEEK_END);
	long n = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	uint8_t *b = n > 0 ? malloc((size_t)n) : NULL;
	bool ok = b && fread(b, 1, (size_t)n, fp) == (size_t)n && bm_parse(&df.fnt, b, (size_t)n);
	free(b);
	fclose(fp);
	if (!ok)
		return false;
	snprintf(path, sizeof path, "romfs:/gfx/%s", df.fnt.page_name[0]);
	if (!ui_tex_load(&df.page, path, true)) {
		bm_free(&df.fnt);
		return false;
	}
	df.ok = true;
	return true;
}

typedef struct {
	u32 col;
	bool snap;
} emit_ctx;

static void emit(void *vctx, const bm_glyph *g, float x, float y, float w, float h)
{
	const emit_ctx *c = vctx;
	float TW = df.page.tex.width, TH = df.page.tex.height;
	if (c->snap) { /* 1:1 glyphs on whole pixels: the bilinear filter hits texel centres */
		x = floorf(x + 0.5f);
		y = floorf(y + 0.5f);
	}
	Tex3DS_SubTexture sub = {
		.width = g->w, .height = g->h,
		.left = g->x / TW, .right = (g->x + g->w) / TW,
		.top = 1.0f - g->y / TH, .bottom = 1.0f - (g->y + g->h) / TH,
	};
	C2D_Image img = {&df.page.tex, &sub};
	C2D_ImageTint tint;
	C2D_PlainImageTint(&tint, c->col, 1.0f);
	C2D_DrawParams p = {.pos = {x, y, w, h}, .center = {0, 0}, .depth = DEPTH, .angle = 0};
	C2D_DrawImage(img, &p, &tint);
}

/* FUN_005931d0 (align 0): y = vertical centre of the first line. */
static void dtext(const char *s, float x, float y, uint32_t argb, float sx, float sy)
{
	if (!df.ok || !s || !*s || !(argb >> 24))
		return;
	float top = y - (float)(int)((float)df.fnt.line_height * sy / 2.0f + 0.5f);
	emit_ctx c = {ui_c2d(argb), sx == 1.0f && sy == 1.0f};
	C2D_SetTintMode(C2D_TintMult);
	bm_layout(&df.fnt, s, x, top, TEXT_LEFT, sx, sy, emit, &c);
	C2D_SetTintMode(C2D_TintSolid);
}

/* ================================================================ textures */

/* spritebank_clamp_sub for any atlas orientation: tex3ds may store an image
 * rotated (top < bottom; left/right are then v and top/bottom u coordinates),
 * which spritebank_clamp_sub collapses to one texel (systemMdl noise2/3/6 are
 * rotated on their page). Interpolating the fields stays valid either way. */
static float clamp_range(float x, float a, float b, float h)
{
	float lo = (a < b ? a : b) + h, hi = (a < b ? b : a) - h;
	if (hi < lo)
		lo = hi = (a + b) * 0.5f;
	return spritebank_clampf(x, lo, hi);
}

static void clamp_sub_any(Tex3DS_SubTexture *s, const Tex3DS_SubTexture *full, const C3D_Tex *tex)
{
	bool rot = Tex3DS_SubTextureRotated(full);
	float hu = 0.5f / (float)tex->width, hv = 0.5f / (float)tex->height;
	float hlr = rot ? hv : hu, htb = rot ? hu : hv;
	s->left = clamp_range(s->left, full->left, full->right, hlr);
	s->right = clamp_range(s->right, full->left, full->right, hlr);
	s->top = clamp_range(s->top, full->top, full->bottom, htb);
	s->bottom = clamp_range(s->bottom, full->top, full->bottom, htb);
}

/* An image addressed in texels of its ORIGINAL texture (ow x oh): u/v texel
 * coordinates are converted to the (possibly scaled, atlased) sub-texture. */
typedef struct {
	C2D_Image img;
	float ow, oh;
	bool ok;
} cimg;

static void draw_cimg(const cimg *t, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                      uint32_t argb)
{
	if (!t->ok || !(argb >> 24))
		return;
	if (x1 < x0) { /* mirrored dst rect (slot 0 portrait): positive extent, mirrored u */
		float k = x0;
		x0 = x1;
		x1 = k;
		k = u0;
		u0 = u1;
		u1 = k;
	}
	/* v running upwards (the noise band, FUN_004e3fc0): crop with v0 < v1 and flip the quad
	 * instead — a crop with top/bottom swapped would read as a tex3ds-rotated image */
	bool flip = v1 < v0;
	if (flip) {
		float k = v0;
		v0 = v1;
		v1 = k;
	}
	const Tex3DS_SubTexture *s = t->img.subtex;
	Tex3DS_SubTexture sub = *s;
	sub.left = s->left + (s->right - s->left) * (u0 / t->ow);
	sub.right = s->left + (s->right - s->left) * (u1 / t->ow);
	sub.top = s->top + (s->bottom - s->top) * (v0 / t->oh);
	sub.bottom = s->top + (s->bottom - s->top) * (v1 / t->oh);
	sub.width = (u16)fmaxf(1.0f, x1 - x0);
	sub.height = (u16)fmaxf(1.0f, fabsf(y1 - y0));
	clamp_sub_any(&sub, s, t->img.tex);
	C2D_Image img = {t->img.tex, &sub};
	C2D_ImageTint tint;
	C2D_PlainImageTint(&tint, ui_c2d(argb), 1.0f);
	/* citro2d: a negative height flips the texture vertically inside the same rect */
	C2D_DrawParams p = {.pos = {x0, y0, x1 - x0, flip ? y0 - y1 : y1 - y0}, .center = {0, 0}, .depth = DEPTH, .angle = 0};
	C2D_SetTintMode(C2D_TintMult);
	C2D_DrawImage(img, &p, &tint);
	C2D_SetTintMode(C2D_TintSolid);
}

/* Portraits: romfs:/story/portrait/<name>.t3x, the PC's src rect pre-cropped and
 * scaled to the 232x102 on-screen size (FORMATS.md "Story conversations"). */
static C2D_SpriteSheet port_sheet[CONV_NAME_COUNT];
static cimg port[CONV_NAME_COUNT];

static void portraits_free(void)
{
	for (int i = 0; i < CONV_NAME_COUNT; i++) {
		if (port_sheet[i])
			C2D_SpriteSheetFree(port_sheet[i]);
		port_sheet[i] = NULL;
		port[i].ok = false;
	}
}

static void portraits_load(const conv_script *s) /* FUN_004e4630 */
{
	portraits_free();
	int ids[CONV_NAME_COUNT];
	int n = conv_script_portraits(s, ids, CONV_NAME_COUNT);
	for (int k = 0; k < n; k++) {
		int id = ids[k];
		if (id <= 0 || id >= CONV_NAME_COUNT) /* 0 = "none": fully transparent, not shipped */
			continue;
		char path[80];
		snprintf(path, sizeof path, STORY_ROOT "/portrait/%s.t3x", conv_portrait_name(id));
		port_sheet[id] = C2D_SpriteSheetLoad(path);
		if (!port_sheet[id] || C2D_SpriteSheetCount(port_sheet[id]) < 1)
			continue;
		port[id].img = C2D_SpriteSheetGetImage(port_sheet[id], 0);
		port[id].ow = port[id].img.subtex->width;
		port[id].oh = port[id].img.subtex->height;
		port[id].ok = true;
	}
}

/* Noise band textures systemMdl/noise0..7 (FUN_004e2910). The match keeps the
 * systemMdl atlas resident once loaded (render.c); before the first match only
 * the atlas page that holds the noise images is loaded, for the scene. */
static cimg noise[8];
static C2D_SpriteSheet noise_page;

static void noise_load(void)
{
	bool all = true;
	for (int i = 0; i < 8; i++) {
		char n[8];
		snprintf(n, sizeof n, "noise%d", i);
		const aos_frame *f = spritebank_get_in("systemMdl", n);
		noise[i].ok = f != NULL;
		if (f) {
			noise[i].img = f->img;
			noise[i].ow = f->ow;
			noise[i].oh = f->oh;
		}
		all &= noise[i].ok;
	}
	if (all)
		return;
	FILE *f = fopen("romfs:/gfx/systemMdl.idx", "r");
	if (!f)
		return;
	char n[32];
	int page, idx, w, h, ow, oh;
	while (fscanf(f, "%31s %d %d %d %d %d %d", n, &page, &idx, &w, &h, &ow, &oh) == 7) {
		int k;
		if (sscanf(n, "noise%d", &k) != 1 || k < 0 || k > 7)
			continue;
		if (!noise_page) {
			char path[64];
			snprintf(path, sizeof path, "romfs:/gfx/systemMdl_%d.t3x", page);
			noise_page = C2D_SpriteSheetLoad(path);
			if (!noise_page)
				break;
		}
		if ((size_t)idx < C2D_SpriteSheetCount(noise_page)) { /* all noise images share one page */
			noise[k].img = C2D_SpriteSheetGetImage(noise_page, (size_t)idx);
			noise[k].ow = (float)ow;
			noise[k].oh = (float)oh;
			noise[k].ok = true;
		}
	}
	fclose(f);
}

static void noise_free(void)
{
	if (noise_page)
		C2D_SpriteSheetFree(noise_page);
	noise_page = NULL;
	memset(noise, 0, sizeof noise);
}

/* FUN_004e3fc0: tiles of tile_w px from x 0, texel-addressed (u 0..tile_w,
 * v_top at y0 .. v_bottom at y1), vertex colour 0xff404040, alpha blend. */
static void band(int nz, int tiles, float tile_w, float y0, float y1, float v_top, float v_bottom, uint32_t argb)
{
	const cimg *t = &noise[nz & 7];
	for (int k = 0; k < tiles; k++)
		draw_cimg(t, (float)k * tile_w, y0, (float)(k + 1) * tile_w, y1, 0, v_top, tile_w, v_bottom, argb);
}

/* ================================================================ scene 5 */

static struct {
	bool debug;          /* opened from the test picker */
	int dbg_script, dbg_backdrop, dbg_lang;
	int lang;            /* language actually used */
	bool loaded;
	conv_script script;
	conv_font cf;
	conv_scene c;
	unsigned frames;
} cv;

/* Bottom-screen text slots (modes.md §C.5): slot 1 (top-right portrait) upper,
 * slot 0 (bottom-left portrait) lower and indented; the noise band behind the
 * text spans the screen width. Box sizes = conv_layout box_w/box_h. */
enum { BOT_W = 320, BOX_H = 96 };
static const struct {
	float x, band_y0, band_y1;
	int box_w;
} bot_slot[CONV_SLOTS] = {
	{40, 126, 230, 272}, /* slot 0 */
	{8, 10, 114, 304},   /* slot 1 */
};

static int se_id_of(const char *wav) /* the wav named by the act -> SE id with that file */
{
	for (int i = 0; i < AOS_SE_COUNT; i++)
		if (aos_se_table[i].file && !strcasecmp(aos_se_table[i].file, wav))
			return i;
	return -1;
}

/* Conversation Exit FUN_004e3730 -> story stepper FUN_007bec90(0) (arcade
 * FUN_004a7910(0) — no arcade ladder has conversations). The only call into the
 * sequencer; the test picker returns to itself instead. */
static void conv_exit_next(void)
{
	if (cv.debug) {
		scene_goto(SCENE_CONV_TEST);
		return;
	}
	modes_flow_conv_exit();
}

static void handle(unsigned ev)
{
	if (ev & CONV_EV_FADE_IN)
		scene_fade(5); /* FUN_00561fc0(5) */
	if (ev & CONV_EV_FADE_OUT)
		scene_fade(1); /* FUN_00561fc0(1) */
	if (ev & CONV_EV_BGM_FADE)
		music_request(0, true, true); /* FUN_004bee00(0,1,1): fade the BGM out */
	if (ev & CONV_EV_SE) {
		/* PC: FUN_007bccb0(0x38, wav, se.dat) + FUN_007bc9c0(0x38). The three wavs are
		 * also regular SE ids (swing_h 0x20, jya05 0x26, chain 0x28): play that id. */
		int id = cv.c.se ? se_id_of(cv.c.se) : -1;
		if (id >= 0)
			audio_se(id, AUDIO_SE_CD_DEFAULT);
	}
	if (ev & CONV_EV_EXIT)
		conv_exit_next();
}

static void cv_free(void)
{
	conv_free(&cv.c);
	conv_script_free(&cv.script);
	portraits_free();
	noise_free();
	dfont_free();
	cv.loaded = false;
}

static void cv_init(void) /* Init FUN_004e35e0 (+ FUN_004bfc10(backdrop) from the stepper) */
{
	announce_hide();
	ui_mtex_free_all();
	text_fonts_unload(); /* menu fonts reload lazily; the scene draws with its own dialogue font */
	cv_free();
	cv.frames = 0;
	int id = cv.debug ? cv.dbg_script : modes_flow_conv_script();
	int backdrop = cv.debug ? cv.dbg_backdrop : modes_flow_conv_backdrop();
	int lang = cv.debug ? cv.dbg_lang : g_config.language;
	if (!conv_lang_has_font(lang))
		lang = LANG_EN;
	memset(&cv.script, 0, sizeof cv.script);
	if (!conv_script_load(&cv.script, STORY_ROOT, id, lang) && lang != LANG_EN) {
		conv_script_free(&cv.script);
		lang = LANG_EN;
		conv_script_load(&cv.script, STORY_ROOT, id, lang);
	}
	cv.lang = lang;
	if (!dfont_load(lang))
		dfont_load(LANG_EN);
	portraits_load(&cv.script);
	noise_load();
	stage3ds_begin(backdrop);
	cv.loaded = true;

	cv.cf = conv_font_from_bm(&df.fnt);
	conv_layout lay;
	conv_layout_original(&lay, df.ok ? &cv.cf : NULL, 1.0f, TOP_W, TOP_H);
	for (int i = 0; i < CONV_SLOTS; i++) { /* bottom-screen boxes instead of 540x88·s (§C.6) */
		lay.box_w[i] = bot_slot[i].box_w;
		lay.box_h[i] = BOX_H;
	}
	handle(conv_init(&cv.c, &cv.script, id, &lay));
#ifdef AOS_PROFILE
	if (cap_on())
		scene_post_frame_hook = cap_hook;
#endif
}

static void cv_update(void) /* FUN_004e3a20 */
{
	conv_input in = {
		.ok = g_min.nr[MI_OK],
		.cancel = g_min.nr[MI_CANCEL],
		.attack = g_min.nr[MI_OK], /* Weapon A = the A button too */
		.special = g_min.nr[MI_X] > g_min.nr[MI_Y] ? g_min.nr[MI_X] : g_min.nr[MI_Y],
	};
#ifdef AOS_PROFILE
	if (cap_on())
		cap_input(&in);
#endif
	if (cv.c.script->count == 0 && cv.c.state == CONV_ST_TALK) { /* missing script: leave at once */
		cv.c.state = CONV_ST_FADEOUT;
		scene_fade(1);
	}
	if (min_pressed_nr(MI_START) && cv.c.state == CONV_ST_TALK) { /* port: skip the rest */
		cv.c.state = CONV_ST_FADEOUT;
		handle(CONV_EV_FADE_OUT);
	}
	handle(conv_frame(&cv.c, &in, scene_fade_busy()));
	stage_g.gameMode = SCENE_CONVERSATION; /* FUN_004bfd30 with DAT_00885870 = 5 */
	stage_g.zoom = 1.0f;
	stage_update();
	cv.frames++;
}

static void cv_draw_top(void) /* FUN_004e38e0 */
{
	if (cv.c.state == CONV_ST_DONE) { /* Exit ran: nothing is drawn (the fade is off now) */
		ui_rect(0, 0, TOP_W, TOP_H, 0xff000000);
		return;
	}
	const conv_view *v = &cv.c.view;
	stage3ds_draw(); /* FUN_004bfd80(1) */
	ui_blend_reset();
	for (int i = 0; i < v->nfill; i++) /* FUN_004e5af0: 黒地 / 白地 */
		ui_rect(0, 0, TOP_W, TOP_H, v->fill_argb[i]);
	if (v->bar_bottom > v->bar_top) { /* letterbox */
		ui_rect(0, 0, TOP_W, (float)v->bar_top, 0xff000000);
		ui_rect(0, (float)v->bar_bottom, TOP_W, TOP_H, 0xff000000);
	}
	for (int i = 0; i < CONV_SLOTS; i++) { /* FUN_004e54b0(0), (1) */
		const conv_slot_view *o = &v->slot[i];
		if (o->band)
			band(o->noise, o->tiles, (float)o->tile_w, o->y0, o->y1, o->v_top, o->v_bottom, o->band_argb);
		for (int k = 0; k < o->nport; k++) {
			const conv_portrait_draw *p = &o->port[k];
			if (p->id > 0 && p->id < CONV_NAME_COUNT && port[p->id].ok)
				draw_cimg(&port[p->id], (float)p->l, (float)p->t, (float)p->r, (float)p->b, 0, 0, port[p->id].ow,
				          port[p->id].oh, p->argb);
		}
	}
}

static void cv_draw_bottom(void)
{
	if (cv.c.state == CONV_ST_DONE)
		return;
	const conv_view *v = &cv.c.view;
	static char shown[2048];
	for (int i = 0; i < CONV_SLOTS; i++) {
		const conv_slot_view *o = &v->slot[i];
		if (o->band) { /* the slot's noise band, texel-addressed 1:1 like the original at s = 1 */
			float y0 = bot_slot[i].band_y0, y1 = bot_slot[i].band_y1, half = (y1 - y0) / 2;
			band(o->noise, BOT_W / 256 + 1, 256, y0, y1, 128 + half, 128 - half, o->band_argb);
		}
		if (o->text_len <= 0 || !o->text)
			continue;
		int n = o->text_len < (int)sizeof shown - 1 ? o->text_len : (int)sizeof shown - 1;
		memcpy(shown, o->text, (size_t)n);
		shown[n] = 0;
		/* first line centred 12 px below the band top: the 96 px box sits 4 px inside it */
		float y = bot_slot[i].band_y0 + 4 + (float)df.fnt.line_height * o->sy / 2.0f;
		dtext(shown, bot_slot[i].x, y, o->text_argb, o->sx, o->sy);
	}
}

static void cv_exit(void)
{
#ifdef AOS_PROFILE
	if (cap_on())
		scene_post_frame_hook = NULL;
#endif
	cv_free();
	stage3ds_end(); /* PC: stages stay for the fight (Game Init reloads); freed here for the memory peak */
	cv.debug = false;
}

static const scene_def conv_scene_def = {
	.name = "conversation", .init = cv_init, .update = cv_update, .draw_top = cv_draw_top,
	.draw_bottom = cv_draw_bottom, .exit = cv_exit,
	.clear_top = 0xffaa5500, /* PC 0xff0055aa as C2D_Color32 (ABGR) */
};

/* ================================================================ test picker (port) */

static const char *const lang_names[LANG_COUNT] = {"English", "Japanese", "Chinese (S)", "Chinese (T)",
                                                    "Portuguese (BR)", "Spanish", "Korean", "Russian"};
static const char *const char_names[15] = {"", "Sora", "Alte", "Tsih", "Mira", "Sham", "Nath", "Star Breaker",
                                           "Suguri", "Saki", "Iru", "Nanako", "Kae", "Kyoko", "Hime"};
static struct {
	int index, lang, row;
	bool init;
} pk;

static int backdrop_of(int id) /* story ladder (modes.md §0.4): ch.1 stage 15, ch.2 16, ch.3/4 19 */
{
	int n = id % 10;
	return n == 0 ? 15 : n == 1 ? 16 : 19;
}

static void pk_init(void)
{
	if (!pk.init) {
		pk.init = true;
		pk.lang = g_config.language;
	}
	announce_show_text("Up/Down: row  Left/Right: change (L/R: 4 scripts)  A: play  B: back");
}

static void pk_update(void)
{
	if (min_pressed(MI_UP) || min_pressed(MI_DOWN)) {
		pk.row ^= 1;
		audio_se(2, AUDIO_SE_CD_DEFAULT);
	}
	int d = min_pressed(MI_RIGHT) ? 1 : min_pressed(MI_LEFT) ? -1 : 0;
	if (min_pressed(MI_R) || min_pressed(MI_L)) { /* jump one character (4 chapters) */
		pk.index = ((pk.index / 4 + (min_pressed(MI_R) ? 1 : -1) + 14) % 14) * 4;
		audio_se(2, AUDIO_SE_CD_DEFAULT);
	}
	if (d) {
		if (pk.row == 0)
			pk.index = (pk.index + d + CONV_SCRIPT_COUNT) % CONV_SCRIPT_COUNT;
		else
			pk.lang = (pk.lang + d + LANG_COUNT) % LANG_COUNT;
		audio_se(2, AUDIO_SE_CD_DEFAULT);
	}
	if (min_confirm()) {
		audio_se(0, AUDIO_SE_CD_DEFAULT);
		cv.debug = true;
		cv.dbg_script = conv_script_id(pk.index);
		cv.dbg_backdrop = backdrop_of(cv.dbg_script);
		cv.dbg_lang = pk.lang;
		announce_hide();
		scene_goto(SCENE_CONVERSATION);
	} else if (min_cancel()) {
		audio_se(1, AUDIO_SE_CD_DEFAULT);
		announce_hide();
		menu_resume_next();
		scene_goto(SCENE_MENU);
	}
}

static void pk_draw_top(void)
{
	ui_blend_reset();
	ui_rect(0, 8, 400, 34, 0xa0000000);
	text_draw(FONT_MENUBIG, "Story conversation test", 200, 21, TEXT_CENTER, 0xffffffff, 1.0f, 0);
	int id = conv_script_id(pk.index);
	char line[128];
	for (int r = 0; r < 2; r++) {
		float y = 80 + 34 * (float)r;
		ui_rect(0, y - 13, 400, y + 13, r == pk.row ? 0x90ffffff : 0x90000000);
		if (r == 0)
			snprintf(line, sizeof line, "TEXT_CONVERSATION%03d  %s, chapter %d", id, char_names[id / 10 % 15], id % 10 + 1);
		else
			snprintf(line, sizeof line, "Language  %s%s", lang_names[pk.lang],
			         conv_lang_has_font(pk.lang) ? "" : " (no font: English)");
		text_draw(FONT_MENU, line, 200, y, TEXT_CENTER, r == pk.row ? 0xffffffff : ui_c2d(0xffa4a4a4), 0.9f, 390);
	}
	snprintf(line, sizeof line, "Backdrop: stage %d  %s", backdrop_of(id), stage_name(backdrop_of(id)));
	text_draw(FONT_MENU, line, 200, 170, TEXT_CENTER, ui_c2d(0xffa4a4a4), 0.8f, 390);
	text_draw(FONT_SYSTEM, "In a conversation: A next, hold X/Y fast-forward, START skip", 200, 200, TEXT_CENTER,
	          ui_c2d(0xffa4a4a4), 1.0f, 390);
}

static void pk_draw_bottom(void)
{
	ui_rect_grad(0, 0, 320, 240, 0xff1a2c5a, 0xff1a2c5a, 0xff05070f, 0xff05070f);
	text_draw(FONT_MENU, "Port debug: story scripts", 160, 20, TEXT_CENTER, 0xffffffff, 0.8f, 300);
	text_draw(FONT_SYSTEM, "56 scripts: TEXT_CONVERSATION010..143", 160, 60, TEXT_CENTER, ui_c2d(0xffa4a4a4), 1.0f, 300);
	text_draw(FONT_SYSTEM, "opened from the main menu with Y", 160, 78, TEXT_CENTER, ui_c2d(0xffa4a4a4), 1.0f, 300);
}

static const scene_def conv_test_def = {
	.name = "conversation test", .init = pk_init, .update = pk_update, .draw_top = pk_draw_top,
	.draw_bottom = pk_draw_bottom, .clear_top = 0xff1e0a08,
};

/* ================================================================ capture runner (PROFILE builds) */
#ifdef AOS_PROFILE
/* sdmc:/aos2_convcap.txt: one conversation per line, "script lang step [cap frames...]"
 * (scene frames since Init). The app boots straight into the first one (no logo/menu),
 * presses A every `step` frames (0 = never) and writes both screens of the listed frames
 * to sdmc:/cap/c<script>_<lang>_<frame>_{top,bot}.raw (raw tiled RGBA8 targets, 240x400 /
 * 240x320) plus frame cost / linear-memory lines to sdmc:/cap/conv_log.txt. After the
 * last line the app quits. Negative step = hold X (fast-forward) instead. */
#define CAP_LINES 16
static struct {
	int n, cur;
	int script[CAP_LINES], lang[CAP_LINES], step[CAP_LINES];
	int ncap[CAP_LINES], cap[CAP_LINES][16];
	FILE *log;
	unsigned acc_n;
	float acc_cpu, acc_gpu;
	unsigned acc_upd;
} cc;

static bool cap_on(void) { return cc.n > 0; }

static void cap_dump(C3D_RenderTarget *t, const char *path)
{
	if (!t)
		return;
	size_t n = (size_t)t->frameBuf.width * t->frameBuf.height * 4;
	GSPGPU_InvalidateDataCache(t->frameBuf.colorBuf, n);
	FILE *f = fopen(path, "wb");
	if (f) {
		fwrite(t->frameBuf.colorBuf, 1, n, f);
		fclose(f);
	}
}

static void cap_hook(void) /* after C3D_FrameEnd of scene frame cv.frames-1 */
{
	int fr = (int)cv.frames - 1, k = cc.cur;
	cc.acc_cpu += C3D_GetProcessingTime();
	cc.acc_gpu += C3D_GetDrawingTime();
	cc.acc_n++;
	if (cc.log && cc.acc_n == 60) {
		fprintf(cc.log, "script %03d lang %d frames %d..%d: cpu %.2f ms gpu %.2f ms stage draw %u us upd %u us lin %lu K\n",
		        cc.script[k], cc.lang[k], fr - 59, fr, (double)(cc.acc_cpu / 60), (double)(cc.acc_gpu / 60),
		        stage3ds_last_us, stage3ds_update_us, (unsigned long)(linearSpaceFree() / 1024));
		fflush(cc.log);
		cc.acc_n = 0;
		cc.acc_cpu = cc.acc_gpu = 0;
	}
	for (int i = 0; i < cc.ncap[k]; i++) {
		if (cc.cap[k][i] != fr)
			continue;
		C3D_FrameBegin(0); /* waits until the GPU finished this frame */
		char path[64];
		snprintf(path, sizeof path, "sdmc:/cap/c%03d_%d_%04d_top.raw", cc.script[k], cc.lang[k], fr);
		cap_dump(scene_top(), path);
		snprintf(path, sizeof path, "sdmc:/cap/c%03d_%d_%04d_bot.raw", cc.script[k], cc.lang[k], fr);
		cap_dump(scene_bottom(), path);
		C3D_FrameEnd(0);
	}
}

static void cap_start(int k)
{
	cc.cur = k;
	cv.debug = true;
	cv.dbg_script = cc.script[k];
	cv.dbg_backdrop = backdrop_of(cc.script[k]);
	cv.dbg_lang = cc.lang[k];
	cc.acc_n = 0;
	cc.acc_cpu = cc.acc_gpu = 0;
	scene_goto(SCENE_CONVERSATION);
}

static void cap_boot_update(void)
{
	if (cc.log)
		fprintf(cc.log, "boot: lin %lu K\n", (unsigned long)(linearSpaceFree() / 1024));
	cap_start(0);
}

/* the picker stands in between conversations: next line or quit */
static void cap_next_update(void)
{
	if (cc.log)
		fprintf(cc.log, "after %03d: lin %lu K\n", cc.script[cc.cur], (unsigned long)(linearSpaceFree() / 1024));
	if (cc.cur + 1 < cc.n)
		cap_start(cc.cur + 1);
	else
		scene_goto(SCENE_QUIT);
}

static const scene_def cap_boot_def = {.name = "conv capture", .update = cap_boot_update};
static const scene_def cap_next_def = {.name = "conv capture next", .update = cap_next_update};

static bool cap_setup(void)
{
	FILE *f = fopen("sdmc:/aos2_convcap.txt", "r");
	if (!f)
		return false;
	char line[256];
	while (cc.n < CAP_LINES && fgets(line, sizeof line, f)) {
		int k = cc.n, off = 0, used;
		if (sscanf(line, "%d %d %d%n", &cc.script[k], &cc.lang[k], &cc.step[k], &used) != 3)
			continue;
		off = used;
		while (cc.ncap[k] < 16 && sscanf(line + off, "%d%n", &cc.cap[k][cc.ncap[k]], &used) == 1) {
			cc.ncap[k]++;
			off += used;
		}
		cc.n++;
	}
	fclose(f);
	if (!cc.n)
		return false;
	mkdir("sdmc:/cap", 0777);
	cc.log = fopen("sdmc:/cap/conv_log.txt", "w");
	scene_register(SCENE_LOGO, &cap_boot_def);
	scene_register(SCENE_CONV_TEST, &cap_next_def);
	return true;
}

/* scripted input of the capture run */
static void cap_input(conv_input *in)
{
	int st = cc.step[cc.cur], fr = (int)cv.frames, k = cc.cur;
	if (cc.ncap[k] > 0 && fr > cc.cap[k][cc.ncap[k] - 1])
		st = -1; /* past the last capture: fast-forward to the end */
	if (st > 0) {
		in->ok = in->attack = fr > 0 && fr % st == 0 ? MIN_PRESSED : MIN_UP;
	} else if (st < 0) {
		in->special = MIN_HELD;
	}
}
#endif

void conv_scenes_register(void)
{
	scene_register(SCENE_CONVERSATION, &conv_scene_def);
	scene_register(SCENE_CONV_TEST, &conv_test_def);
#ifdef AOS_PROFILE
	cap_setup();
#endif
}
