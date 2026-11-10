#include "ui.h"

#include <stdio.h>
#include <string.h>

bool ui_tex_load(ui_tex *t, const char *path, bool linear)
{
	memset(t, 0, sizeof *t);
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;
	t->t3x = Tex3DS_TextureImportStdio(f, &t->tex, NULL, false);
	fclose(f);
	if (!t->t3x)
		return false;
	C3D_TexSetFilter(&t->tex, linear ? GPU_LINEAR : GPU_NEAREST, linear ? GPU_LINEAR : GPU_NEAREST);
	C3D_TexSetWrap(&t->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	t->bytes = t->tex.size;
	t->ok = true;
	return true;
}

void ui_tex_free(ui_tex *t)
{
	if (!t->ok)
		return;
	Tex3DS_TextureFree(t->t3x);
	C3D_TexDelete(&t->tex);
	memset(t, 0, sizeof *t);
}

/* ---------------------------------------------------------------- menu textures */

/* Must match tools/aos2pack/fonts.go menuTexs: crop origin + scale. */
static const struct {
	const char *name;
	float ox, oy, scale;
} mdesc[MT_COUNT] = {
	[MT_LOGO] = {"logo", 0, 0, 0.25f},
	[MT_DAIDAI] = {"daidai", 0, 0, 0.125f},
	[MT_SHINDEN] = {"shinden", 0, 0, 0.125f},
	[MT_PILLS] = {"pills", 0x2d0, 0, 1.0f},
	[MT_ARROWS] = {"arrows", 0, 0x5e, 0.5f},
	[MT_MANUAL] = {"manual_en", 0, 0, 0.5f},
};
static ui_tex mtex[MT_COUNT];
static int mpage[MT_COUNT];

bool ui_mtex_load(ui_mtex_id id, int page)
{
	if (mtex[id].ok && mpage[id] == page)
		return true;
	ui_tex_free(&mtex[id]);
	char path[96];
	if (id == MT_MANUAL)
		snprintf(path, sizeof path, "romfs:/gfx/m3_%s_%02d_0.t3x", mdesc[id].name, page);
	else
		snprintf(path, sizeof path, "romfs:/gfx/m3_%s_0.t3x", mdesc[id].name);
	mpage[id] = page;
	return ui_tex_load(&mtex[id], path, true);
}

void ui_mtex_free(ui_mtex_id id) { ui_tex_free(&mtex[id]); }

void ui_mtex_free_all(void)
{
	for (int i = 0; i < MT_COUNT; i++)
		ui_tex_free(&mtex[i]);
}

size_t ui_mtex_bytes(void)
{
	size_t n = 0;
	for (int i = 0; i < MT_COUNT; i++)
		n += mtex[i].bytes;
	return n;
}

void ui_mtex_draw(ui_mtex_id id, float sx0, float sy0, float sx1, float sy1, float x0, float y0, float x1, float y1,
                  unsigned argb)
{
	ui_tex *t = &mtex[id];
	if (!t->ok || !(argb >> 24) || x1 <= x0 || y1 <= y0)
		return;
	float s = mdesc[id].scale, W = t->tex.width, H = t->tex.height;
	float u0 = (sx0 - mdesc[id].ox) * s / W, u1 = (sx1 - mdesc[id].ox) * s / W;
	float v0 = (sy0 - mdesc[id].oy) * s / H, v1 = (sy1 - mdesc[id].oy) * s / H;
	Tex3DS_SubTexture sub = {
		.width = (u16)(x1 - x0 < 1 ? 1 : x1 - x0),
		.height = (u16)(y1 - y0 < 1 ? 1 : y1 - y0),
		.left = u0, .right = u1, .top = 1.0f - v0, .bottom = 1.0f - v1,
	};
	C2D_Image img = {&t->tex, &sub};
	C2D_ImageTint tint;
	C2D_PlainImageTint(&tint, ui_c2d(argb), 1.0f);
	C2D_DrawParams p = {.pos = {x0, y0, x1 - x0, y1 - y0}, .center = {0, 0}, .depth = UI_DEPTH, .angle = 0};
	C2D_SetTintMode(C2D_TintMult);
	C2D_DrawImage(img, &p, &tint);
	C2D_SetTintMode(C2D_TintSolid);
}

/* ---------------------------------------------------------------- primitives */

static int cur_blend = -1;
void ui_blend_reset(void)
{
	cur_blend = -1;
	ui_blend(1);
}

void ui_blend(int mode)
{
	if (mode == cur_blend)
		return;
	C2D_Flush();
	cur_blend = mode;
	if (mode == 2)
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_SRC_ALPHA, GPU_ONE);
	else
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA,
		               GPU_ONE_MINUS_SRC_ALPHA);
}

void ui_rect(float l, float t, float r, float b, unsigned argb)
{
	if (!(argb >> 24) || r <= l || b <= t)
		return;
	C2D_DrawRectSolid(l, t, UI_DEPTH, r - l, b - t, ui_c2d(argb));
}

void ui_rect_grad(float l, float t, float r, float b, unsigned tl, unsigned tr, unsigned br, unsigned bl)
{
	if (r <= l || b <= t)
		return;
	C2D_DrawRectangle(l, t, UI_DEPTH, r - l, b - t, ui_c2d(tl), ui_c2d(tr), ui_c2d(bl), ui_c2d(br));
}

void ui_hline(float x0, float x1, float y, unsigned argb) { ui_rect(x0, y, x1, y + 1, argb); }
