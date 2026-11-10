/* 3DS menu drawing helpers (citro2d): single textures packed by
 * tools/aos2pack (fonts.go, "menu textures"), rectangles, gradients, blend
 * modes. Colours are PC ARGB (0xAARRGGBB) like the original's draw calls. */
#ifndef AOS_APP_UI_H
#define AOS_APP_UI_H

#include <citro2d.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
	C3D_Tex tex;
	Tex3DS_Texture t3x;
	bool ok;
	size_t bytes;
} ui_tex;

bool ui_tex_load(ui_tex *t, const char *path, bool linear);
void ui_tex_free(ui_tex *t);

/* Menu textures (romfs:/gfx/m3_<name>_0.t3x). Source rects passed to
 * ui_mtex_draw are in ORIGINAL texture pixels (the PC's src rects). */
typedef enum { MT_LOGO, MT_DAIDAI, MT_SHINDEN, MT_PILLS, MT_ARROWS, MT_MANUAL, MT_COUNT } ui_mtex_id;
bool ui_mtex_load(ui_mtex_id id, int page); /* page: manual 1..9, else 0; idempotent */
void ui_mtex_free(ui_mtex_id id);
void ui_mtex_free_all(void);
size_t ui_mtex_bytes(void);
/* Draw src (sx0,sy0)-(sx1,sy1) of the original texture into the screen rect. */
void ui_mtex_draw(ui_mtex_id id, float sx0, float sy0, float sx1, float sy1, float x0, float y0, float x1, float y1,
                  unsigned argb);

void ui_blend(int mode);
void ui_blend_reset(void); /* forget the cached mode (others change it) and set 1 */ /* FUN_004637d0 modes: 1 alpha, 2 additive */
void ui_rect(float l, float t, float r, float b, unsigned argb);
/* corners TL, TR, BR, BL (FUN_00457a00 fan order) */
void ui_rect_grad(float l, float t, float r, float b, unsigned tl, unsigned tr, unsigned br, unsigned bl);
void ui_hline(float x0, float x1, float y, unsigned argb);
static inline unsigned ui_argb(int a, unsigned rgb) { return (unsigned)(a < 0 ? 0 : a > 255 ? 255 : a) << 24 | (rgb & 0xffffff); }
/* PC ARGB -> C2D_Color32 order */
static inline u32 ui_c2d(unsigned argb)
{
	return C2D_Color32((argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff, argb >> 24);
}

/* text with a PC ARGB colour (text.h takes C2D colours) */
#define UI_DEPTH 0.5f

#endif
