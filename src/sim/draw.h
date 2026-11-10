/* Draw command list. T1 draw handlers and object draw functions translate
 * the original CModel calls (docs/re/render.md) into aos_draw() commands in
 * WORLD units (+y up); the platform layer applies the camera
 * (objects.md §6.2) and renders them in order. Not part of the snapshot. */
#ifndef AOS_DRAW_H
#define AOS_DRAW_H

#include <stdint.h>

/* Blend modes, FUN_004637d0. */
enum {
	AOS_BLEND_OPAQUE = 0, AOS_BLEND_ALPHA = 1, AOS_BLEND_ADD_ALPHA = 2, AOS_BLEND_KEEP = 3,
	AOS_BLEND_MUL = 4, AOS_BLEND_ADD = 5, AOS_BLEND_SCREEN = 6, AOS_BLEND_INVERT = 7,
};

/* Texture banks: which folder a name resolves in. Character art resolves in
 * the folder of the given player's character + palette (charaXxxA/B/C). */
enum { AOS_BANK_COMMON = -1, AOS_BANK_P0 = 0, AOS_BANK_P1 = 1 };

typedef struct {
	int16_t tex;      /* interned texture name (aos_tex) */
	int8_t bank;      /* AOS_BANK_* */
	uint8_t blend;
	float x, y, z;    /* world position of the quad centre */
	float sx, sy;     /* scale (1 = original texture pixels in world units) */
	uint16_t rotZ;    /* binary angle, CCW in world space */
	uint8_t flipX;    /* Y-rotation by π (facing left) */
	uint8_t hasSrc;
	uint32_t rgba;    /* 0xRRGGBBAA modulate */
	float halfW, halfH; /* if > 0: explicit half-size in world units (effects) */
	float src[4];     /* if hasSrc: source rect x0,y0,x1,y1 in original texture pixels */
	float tri[6];      /* tex == "$tri": untextured triangle, world-space corners x0,y0,x1,y1,x2,y2 */
	uint32_t triRgba[3]; /* per-corner colour (gradient) */
} aos_dcmd;

#define AOS_MAX_DCMD 4096 /* heavy hyper scenes reach 2048 (tests/perf_proxy: Iru, Nanako, Sham) */
typedef struct {
	int n;
	int dropped;
	aos_dcmd cmd[AOS_MAX_DCMD];
} aos_drawlist;

extern aos_drawlist aos_dl;

/* Intern a texture name ("sg_std00", "beamemit", ...). Stable for the process. */
int aos_tex(const char *name);
const char *aos_tex_name(int id);
int aos_tex_count(void);

/* Convenience: interned per-frame name from printf pattern, e.g. aos_texf("sg_std%02d", f). */
int aos_texf(const char *fmt, int frame);

/* Untextured gradient triangle in world units (Alte light cones, ribbon strips). */
void aos_draw_tri(const float xy[6], const uint32_t rgba[3], int blend);
void aos_draw_begin(void);
aos_dcmd *aos_draw(int tex, int bank, float x, float y, float z);

static inline uint32_t aos_rgba(float r, float g, float b, float a)
{
	int R = (int)(r * 255.0f + 0.5f), G = (int)(g * 255.0f + 0.5f), B = (int)(b * 255.0f + 0.5f), A = (int)(a * 255.0f + 0.5f);
	R = R < 0 ? 0 : R > 255 ? 255 : R;
	G = G < 0 ? 0 : G > 255 ? 255 : G;
	B = B < 0 ? 0 : B > 255 ? 255 : B;
	A = A < 0 ? 0 : A > 255 ? 255 : A;
	return (uint32_t)R << 24 | (uint32_t)G << 16 | (uint32_t)B << 8 | (uint32_t)A;
}

#endif
