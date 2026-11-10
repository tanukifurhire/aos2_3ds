/* Sprite bank: loads the per-folder atlases produced by tools/aos2pack
 * (romfs:/gfx/<folder>.idx + <folder>_<page>.t3x) and looks frames up by
 * their original texture name (e.g. "sg_std00"). */
#ifndef AOS_SPRITEBANK_H
#define AOS_SPRITEBANK_H

#include <citro2d.h>
#include <stdbool.h>

typedef struct {
	C2D_Image img;
	u16 w, h;   /* atlas (scaled) size */
	u16 ow, oh; /* original PC texture size — draw at ow/w scale to get PC units */
	u8 premul;  /* .idx 8th field = 1: the page's RGB is premultiplied by its alpha (aos2pack -pma); the 2D
	             * batcher (sprbatch.h) draws straight-alpha pages premultiplied in the texture combiner.
	             * .idx 9th field = 1 (aos2pack -edge): the atlas image has a 1px border that repeats its edge
	             * texels; img.subtex is the rect inside it (spritebank.c) */
} aos_frame;

/* Loads a folder (idempotent). Returns false if its index can't be read. */
bool spritebank_load(const char *folder);
/* Same, for folders kept for the whole session: pages go to VRAM while it has room. */
bool spritebank_load_resident(const char *folder);
/* Frees a folder's textures. */
void spritebank_unload(const char *folder);
/* Finds a frame by name in any loaded folder; NULL if not found. */
const aos_frame *spritebank_get(const char *name);
/* Find a frame by name in one specific loaded folder. */
const aos_frame *spritebank_get_in(const char *folder, const char *name);
void spritebank_exit(void);

/* Atlas pages are sampled bilinearly and their images are packed edge to edge.
 * Clamp a (possibly cropped) sub-rectangle `s` of the frame `full` to the
 * frame's texel centres, so the filter never reaches a neighbouring image: the
 * same edge-clamp the PC gets from separate textures. The clamp alone leaves no
 * margin for the PICA's fixed-point texture coordinates / filter weights (thin
 * lines along sprite edges were reported on a real Old 3DS), so the match
 * atlases also give every frame a 1px border that repeats its edge texels
 * (aos2pack -edge, PERF.md "Atlas bleed"). */
static inline float spritebank_clampf(float v, float lo, float hi) /* plain compares: ARMv6 VFP has no min/max */
{
	return v < lo ? lo : v > hi ? hi : v;
}

static inline void spritebank_clamp_sub(Tex3DS_SubTexture *s, const Tex3DS_SubTexture *full, const C3D_Tex *tex)
{
	/* half a texel; sizes are powers of two 8..1024, so no division (this runs per HUD strip) */
	static const float half[8] = {1 / 16.0f, 1 / 32.0f, 1 / 64.0f, 1 / 128.0f, 1 / 256.0f, 1 / 512.0f, 1 / 1024.0f, 1 / 2048.0f};
	float hu = half[(__builtin_ctz(tex->width) - 3) & 7], hv = half[(__builtin_ctz(tex->height) - 3) & 7];
	/* tex3ds may store an image rotated (top < bottom; left/right then run along
	 * the texture's v axis and top/bottom along u), so take each field pair's
	 * range in either order and swap the half-texel axes for rotated frames. */
	if (full->top < full->bottom) {
		float k = hu;
		hu = hv;
		hv = k;
	}
	float l = (full->left < full->right ? full->left : full->right) + hu;
	float r = (full->left < full->right ? full->right : full->left) - hu;
	float b = (full->top < full->bottom ? full->top : full->bottom) + hv;
	float t = (full->top < full->bottom ? full->bottom : full->top) - hv;
	if (r < l)
		l = r = (full->left + full->right) * 0.5f;
	if (t < b)
		t = b = (full->top + full->bottom) * 0.5f;
	s->left = spritebank_clampf(s->left, l, r);
	s->right = spritebank_clampf(s->right, l, r);
	s->top = spritebank_clampf(s->top, b, t);
	s->bottom = spritebank_clampf(s->bottom, b, t);
}

/* Texture bytes held by the loaded folders in VRAM (true) or linear memory (false). */
size_t spritebank_bytes(bool vram);

#endif
