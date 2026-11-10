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

/* Atlas pages are sampled bilinearly and their images are packed edge to edge
 * (no padding: a 1px border would cost ~0.5 MB per character atlas and break
 * the power-of-two packing). Clamp a (possibly cropped) sub-rectangle `s` of
 * the frame `full` to the frame's texel centres, so the filter never reaches
 * a neighbouring image: the same edge-clamp the PC gets from separate textures. */
static inline float spritebank_clampf(float v, float lo, float hi) /* plain compares: ARMv6 VFP has no min/max */
{
	return v < lo ? lo : v > hi ? hi : v;
}

static inline void spritebank_clamp_sub(Tex3DS_SubTexture *s, const Tex3DS_SubTexture *full, const C3D_Tex *tex)
{
	/* half a texel; sizes are powers of two 8..1024, so no division (this runs per HUD strip) */
	static const float half[8] = {1 / 16.0f, 1 / 32.0f, 1 / 64.0f, 1 / 128.0f, 1 / 256.0f, 1 / 512.0f, 1 / 1024.0f, 1 / 2048.0f};
	float hu = half[(__builtin_ctz(tex->width) - 3) & 7], hv = half[(__builtin_ctz(tex->height) - 3) & 7];
	float l = full->left + hu, r = full->right - hu;
	float t = full->top - hv, b = full->bottom + hv; /* t3x V runs bottom-up: top > bottom */
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
