/* BMFont text renderer on citro2d (text.h). Fonts load lazily on first use
 * and are freed before matches (text_fonts_unload). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bmfont.h"
#include "text.h"
#include "ui.h"

static const char *const fnames[FONT_COUNT] = {"system", "menu", "menubig", "menulabel", "menusubtitle", "winquote",
                                               "dialogue"};
static struct {
	bm_font fnt;
	ui_tex page;
	bool tried, ok;
} fonts[FONT_COUNT];

static bool font_get(font_id f)
{
	if ((unsigned)f >= FONT_COUNT)
		return false;
	if (fonts[f].tried)
		return fonts[f].ok;
	fonts[f].tried = true;
	char path[64];
	snprintf(path, sizeof path, "romfs:/font/%s.fnt", fnames[f]);
	FILE *fp = fopen(path, "rb");
	if (!fp)
		return false;
	fseek(fp, 0, SEEK_END);
	long n = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	uint8_t *b = malloc((size_t)n);
	bool ok = b && fread(b, 1, (size_t)n, fp) == (size_t)n && bm_parse(&fonts[f].fnt, b, (size_t)n);
	free(b);
	fclose(fp);
	if (!ok)
		return false;
	snprintf(path, sizeof path, "romfs:/gfx/%s", fonts[f].fnt.page_name[0]);
	if (!ui_tex_load(&fonts[f].page, path, true)) {
		bm_free(&fonts[f].fnt);
		return false;
	}
	fonts[f].ok = true;
	return true;
}

bool text_fonts_load(void)
{
	bool ok = true;
	for (int i = 0; i < FONT_COUNT; i++)
		ok &= font_get((font_id)i);
	return ok;
}

void text_fonts_unload(void)
{
	for (int i = 0; i < FONT_COUNT; i++) {
		if (fonts[i].ok) {
			ui_tex_free(&fonts[i].page);
			bm_free(&fonts[i].fnt);
		}
		fonts[i].ok = fonts[i].tried = false;
	}
}

size_t text_fonts_bytes(void)
{
	size_t n = 0;
	for (int i = 0; i < FONT_COUNT; i++)
		n += fonts[i].page.bytes;
	return n;
}

float text_width(font_id f, const char *s, float scale)
{
	return font_get(f) ? bm_text_width(&fonts[f].fnt, s, scale, NULL) : 0;
}

float text_line_height(font_id f, float scale) { return font_get(f) ? (float)fonts[f].fnt.line_height * scale : 0; }

float text_height(font_id f, const char *s, float scale)
{
	if (!font_get(f))
		return 0;
	int lines;
	bm_text_width(&fonts[f].fnt, s, scale, &lines);
	return (float)fonts[f].fnt.line_height * scale + (float)(lines - 1) * floorf((float)fonts[f].fnt.base * scale + 0.5f);
}

typedef struct {
	ui_tex *page;
	u32 col;
	bool snap;
} emit_ctx;

static void emit(void *vctx, const bm_glyph *g, float x, float y, float w, float h)
{
	emit_ctx *c = vctx;
	float W = c->page->tex.width, H = c->page->tex.height;
	if (c->snap) { /* 1:1 glyphs: whole pixels, so the bilinear filter hits texel centres */
		x = floorf(x + 0.5f);
		y = floorf(y + 0.5f);
	}
	Tex3DS_SubTexture sub = {
		.width = g->w, .height = g->h,
		.left = g->x / W, .right = (g->x + g->w) / W,
		.top = 1.0f - g->y / H, .bottom = 1.0f - (g->y + g->h) / H,
	};
	C2D_Image img = {&c->page->tex, &sub};
	C2D_ImageTint tint;
	C2D_PlainImageTint(&tint, c->col, 1.0f);
	C2D_DrawParams p = {.pos = {x, y, w, h}, .center = {0, 0}, .depth = UI_DEPTH, .angle = 0};
	C2D_DrawImage(img, &p, &tint);
}

static void draw(font_id f, const char *s, float x, float y, int align, uint32_t rgba, float scale, float maxw)
{
	if (!s || !*s || !(rgba >> 24) || !font_get(f))
		return;
	bm_font *fn = &fonts[f].fnt;
	float sx = scale, sy = scale;
	if (maxw > 0) { /* FUN_00593430: horizontal squeeze only */
		float w = bm_text_width(fn, s, scale, NULL);
		if (w > maxw)
			sx = scale * maxw / w;
	}
	float top = y - (float)(int)((float)fn->line_height * sy / 2.0f + 0.5f); /* FUN_005931d0: y = centre */
	emit_ctx c = {&fonts[f].page, rgba, sx == 1.0f && sy == 1.0f};
	C2D_SetTintMode(C2D_TintMult);
	bm_layout(fn, s, x, top, align, sx, sy, emit, &c);
	C2D_SetTintMode(C2D_TintSolid);
}

void text_draw(font_id f, const char *s, float x, float y, int align, uint32_t rgba, float scale, float maxw)
{
	draw(f, s, x, y, align, rgba, scale, maxw);
}

void text_draw_shadowed(font_id f, const char *s, float x, float y, int align, uint32_t rgba, float scale, float maxw)
{
	uint32_t sh = (uint32_t)((rgba >> 24) * 0x20 / 255) << 24; /* ARGB 0x20000000 */
	static const int8_t o[4][2] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
	for (int i = 0; i < 4; i++)
		draw(f, s, x + o[i][0], y + o[i][1], align, sh, scale, maxw);
	draw(f, s, x, y, align, rgba, scale, maxw);
}

int text_wrap(font_id f, const char *s, float scale, float maxw, char *out, size_t outsz)
{
	if (!font_get(f)) {
		snprintf(out, outsz, "%s", s);
		return 1;
	}
	return bm_wrap(&fonts[f].fnt, s, scale, maxw, out, outsz);
}
