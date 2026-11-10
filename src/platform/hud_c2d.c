/* 3DS backend of the HUD primitives (hud_gfx.h) on the 2D batcher (sprbatch.h), plus the HUD's texture
 * set. Textures come from the atlas sprite bank (tools/aos2pack); texel coordinates of the PC textures are
 * mapped onto the atlas sub-rectangles.
 *  - hg_quad (HP bar parallelograms) is one textured quad split like the PC's triangle fan TL-TR-BR,
 *    TL-BR-BL (affine UVs). On citro2d, which cannot texture arbitrary quads, it was one strip per row.
 *  - hg_tri_tex (hyper arcs) uses vertex colours from the arc gradient textures (hud_grad.h; they are
 *    constant along u).
 * Blend 1 and 2 share one GPU state (premultiplied colours), so the HUD costs a few draw calls. */
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "hud.h"
#include "hud_gfx.h"
#include "hud_grad.h"
#include "render.h"
#include "sprbatch.h"
#include "spritebank.h"

#define DEPTH 0.9f

typedef struct {
	C2D_Image img;
	float ow, oh; /* original PC texture size */
	bool ok;
	bool premul; /* atlas RGB premultiplied by alpha (aos2pack -pma) */
} hud_tex;

static hud_tex tex[HT_COUNT];

/* systemAddMdl holds the big menu atlases too; only the pages with
 * system/system2 are loaded (unless the whole folder is already loaded). */
#define ADD_PAGES 8
static C2D_SpriteSheet addSheet[ADD_PAGES];

static bool from_frame(hud_tex *t, const aos_frame *f)
{
	if (!f)
		return false;
	t->img = f->img;
	t->ow = f->ow;
	t->oh = f->oh;
	t->ok = true;
	t->premul = f->premul;
	return true;
}

static bool from_bank(hud_tex *t, const char *folder, const char *name)
{
	return from_frame(t, spritebank_get_in(folder, name));
}

static void load_add(hud_tex *t, const char *name)
{
	if (from_bank(t, "systemAddMdl", name))
		return;
	FILE *f = fopen("romfs:/gfx/systemAddMdl.idx", "r");
	if (!f)
		return;
	char n[32];
	int page, idx, w, h, ow, oh;
	while (fscanf(f, "%31s %d %d %d %d %d %d", n, &page, &idx, &w, &h, &ow, &oh) == 7) {
		if (strcmp(n, name) != 0 || page < 0 || page >= ADD_PAGES)
			continue;
		if (!addSheet[page]) {
			char path[64];
			snprintf(path, sizeof path, "romfs:/gfx/systemAddMdl_%d.t3x", page);
			addSheet[page] = C2D_SpriteSheetLoad(path);
		}
		if (addSheet[page] && (size_t)idx < C2D_SpriteSheetCount(addSheet[page])) {
			t->img = C2D_SpriteSheetGetImage(addSheet[page], (size_t)idx);
			t->ow = (float)ow;
			t->oh = (float)oh;
			t->ok = true;
			t->premul = false;
		}
		break;
	}
	fclose(f);
}

/* <xx>_system texture per engine character id (stats +0x44 sprite, palette folder). */
static const char *const portrait[19] = {
	[3] = "sr_system", [4] = "at_system", [5] = "ts_system", [6] = "mr_system", [7] = "sa_system",
	[8] = "nt_system", [9] = "sb_system", [10] = "sg_system", [11] = "sk_system", [12] = "ir_system",
	[13] = "nk_system", [14] = "ke_system", [15] = "ky_system", [16] = "hm_system", [17] = "sm_system",
	[18] = "sab_system",
};

void hud_c2d_load(const aos_world *w)
{
	hud_c2d_unload();
	static const char *const sysm[] = {"gauge", "damage", "bgm_name", "fade", "hyperb", "hyperg", "hyperr"};
	for (int i = 0; i < 7; i++)
		from_bank(&tex[HT_GAUGE + i], "systemMdl", sysm[i]);
	for (int i = 0; i < 8; i++) {
		char n[8];
		snprintf(n, sizeof n, "noise%d", i);
		from_bank(&tex[HT_NOISE0 + i], "systemMdl", n);
	}
	from_bank(&tex[HT_CROWN_W], "msgMdl", "msg_leadercrownw");
	from_bank(&tex[HT_CROWN_L], "msgMdl", "msg_leadercrownl");
	load_add(&tex[HT_SYSTEM], "system");
	load_add(&tex[HT_SYSTEM2], "system2");
	for (int i = 0; i < 2; i++) {
		int id = w->p[i].id;
		if (id >= 0 && id < 19 && portrait[id] && !from_bank(&tex[HT_PORTRAIT0 + i], render_player_folder(i), portrait[id]))
			from_frame(&tex[HT_PORTRAIT0 + i], spritebank_get(portrait[id]));
	}
}

void hud_c2d_unload(void)
{
	for (int p = 0; p < ADD_PAGES; p++)
		if (addSheet[p]) {
			C2D_SpriteSheetFree(addSheet[p]);
			addSheet[p] = NULL;
		}
	memset(tex, 0, sizeof tex);
}

/* ---- primitives ---- */

/* Recording mode (hud_c2d_panel_hash): the primitives hash their arguments instead of drawing. */
static bool rec;
static uint32_t rec_h;
static void rec_mix(const void *p, size_t n)
{
	const uint8_t *b = p;
	for (size_t i = 0; i < n; i++)
		rec_h = (rec_h ^ b[i]) * 16777619u; /* FNV-1a */
}
#define REC(...)                                                                                                 	do {                                                                                                         		if (rec) {                                                                                               			const float rf_[] = {__VA_ARGS__};                                                                   			rec_mix(rf_, sizeof rf_);                                                                            			return;                                                                                              		}                                                                                                        	} while (0)


/* PC colour 0xAARRGGBB -> 0xRRGGBBAA (spr_color) */
static inline uint32_t pc(uint32_t argb) { return argb << 8 | argb >> 24; }

/* A PC texel rect (u0,v0)-(u1,v1) of a HUD texture as an atlas sub-rect (clamped to the frame's texel
 * centres); tex3ds may store an image rotated (top < bottom: left/right run along the texture's v). */
typedef struct {
	float l, r, t, b;
	bool rot;
} hud_uv;

static void hud_map(const hud_tex *h, float u0, float v0, float u1, float v1, hud_uv *m)
{
	const Tex3DS_SubTexture *s = h->img.subtex;
	Tex3DS_SubTexture sub = *s;
	float fu0 = u0 / h->ow, fu1 = u1 / h->ow, fv0 = v0 / h->oh, fv1 = v1 / h->oh;
	sub.left = s->left + (s->right - s->left) * fu0;
	sub.right = s->left + (s->right - s->left) * fu1;
	sub.top = s->top + (s->bottom - s->top) * fv0;
	sub.bottom = s->top + (s->bottom - s->top) * fv1;
	spritebank_clamp_sub(&sub, s, h->img.tex);
	m->l = sub.left;
	m->r = sub.right;
	m->t = sub.top;
	m->b = sub.bottom;
	m->rot = s->top < s->bottom;
}

/* corner texcoords (Tex3DS_SubTexture{TopLeft,...}): a = 0/1 left/right, b = 0/1 top/bottom */
static inline void hud_tc(const hud_uv *m, int a, int b, float *u, float *v)
{
	float x = a ? m->r : m->l, y = b ? m->b : m->t;
	if (m->rot) {
		*u = y;
		*v = x;
	} else {
		*u = x;
		*v = y;
	}
}

void hg_image(int t, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, float angle,
              uint32_t argb, int bl)
{
	if (t < 0 || t >= HT_COUNT || !tex[t].ok || (argb >> 24) == 0)
		return;
	REC(1, (float)t, x0, y0, x1, y1, u0, v0, u1, v1, angle, (float)(argb >> 16), (float)(argb & 0xffff), (float)bl);
	const hud_tex *h = &tex[t];
	/* Mirrored rects (P2) and v running upwards (noise strips): positive extents, mirrored texture
	 * coordinates. (The citro2d version flipped v with a negative quad height, which citro2d lays out
	 * below the rect: the banner noise strip was drawn one strip height too low.) */
	if (x1 < x0) {
		float tmp = x0;
		x0 = x1;
		x1 = tmp;
		tmp = u0;
		u0 = u1;
		u1 = tmp;
	}
	if (y1 < y0) {
		float tmp = y0;
		y0 = y1;
		y1 = tmp;
		tmp = v0;
		v0 = v1;
		v1 = tmp;
	}
	bool vflip = v1 < v0;
	hud_uv m;
	hud_map(h, u0, vflip ? v1 : v0, u1, vflip ? v0 : v1, &m);
	if (vflip) {
		float tmp = m.t;
		m.t = m.b;
		m.b = tmp;
	}
	spr_state(h->img.tex, h->premul, bl);
	spr_vtx *v = spr_quad();
	if (!v)
		return;
	u32 c = spr_color(pc(argb), bl);
	/* as C2Di_CalcQuad: rotation about the rect centre */
	float w = x1 - x0, hh = y1 - y0, px = x0 + w * 0.5f, py = y0 + hh * 0.5f;
	float cx[4] = {-(w * 0.5f), -(w * 0.5f) + w, -(w * 0.5f), -(w * 0.5f) + w};
	float cy[4] = {-(hh * 0.5f), -(hh * 0.5f), -(hh * 0.5f) + hh, -(hh * 0.5f) + hh};
	if (angle != 0.0f) {
		float rs = sinf(angle), rc = cosf(angle);
		for (int k = 0; k < 4; k++) {
			float x = cx[k] * rc - cy[k] * rs, y = cy[k] * rc + cx[k] * rs;
			cx[k] = x;
			cy[k] = y;
		}
	}
	for (int k = 0; k < 4; k++) { /* TL TR BL BR */
		float tu, tv;
		hud_tc(&m, k & 1, k >> 1, &tu, &tv);
		spr_set(&v[k], cx[k] + px, cy[k] + py, tu, tv, c);
	}
}

void hg_quad(int t, const float x[4], const float y[4], float u0, float v0, float u1, float v1, uint32_t argb, int bl)
{
	if (t < 0 || t >= HT_COUNT || !tex[t].ok || (argb >> 24) == 0 || y[2] <= y[0])
		return;
	REC(2, (float)t, x[0], x[1], x[2], x[3], y[0], y[1], y[2], y[3], u0, v0, u1, v1, (float)(argb >> 16),
	    (float)(argb & 0xffff), (float)bl);
	const hud_tex *h = &tex[t];
	bool uf = u1 < u0, vf = v1 < v0;
	hud_uv m;
	hud_map(h, uf ? u1 : u0, vf ? v1 : v0, uf ? u0 : u1, vf ? v0 : v1, &m);
	if (uf) {
		float tmp = m.l;
		m.l = m.r;
		m.r = tmp;
	}
	if (vf) {
		float tmp = m.t;
		m.t = m.b;
		m.b = tmp;
	}
	spr_state(h->img.tex, h->premul, bl);
	spr_vtx *v = spr_quad();
	if (!v)
		return;
	u32 c = spr_color(pc(argb), bl);
	/* corners TL,TR,BR,BL = (u0,v0) (u1,v0) (u1,v1) (u0,v1), written TR TL BR BL: the quad's triangles
	 * (TR BR TL) (TL BR BL) are the PC fan TL-TR-BR, TL-BR-BL */
	static const int ord[4] = {1, 0, 2, 3}, ca[4] = {0, 1, 1, 0}, cb[4] = {0, 0, 1, 1};
	for (int k = 0; k < 4; k++) {
		int i = ord[k];
		float tu, tv;
		hud_tc(&m, ca[i], cb[i], &tu, &tv);
		spr_set(&v[k], x[i], y[i], tu, tv, c);
	}
}

void hg_tri(const float x[3], const float y[3], const uint32_t argb[3], int bl)
{
	if (!(argb[0] >> 24) && !(argb[1] >> 24) && !(argb[2] >> 24))
		return;
	REC(3, x[0], x[1], x[2], y[0], y[1], y[2], (float)(argb[0] >> 16), (float)(argb[0] & 0xffff),
	    (float)(argb[1] >> 16), (float)(argb[1] & 0xffff), (float)(argb[2] >> 16), (float)(argb[2] & 0xffff), (float)bl);
	uint32_t c[3] = {pc(argb[0]), pc(argb[1]), pc(argb[2])};
	spr_tri_solid(x, y, c, bl);
}

void hg_tri2(const float x[4], const float y[4], const uint32_t argb[4], int bl)
{
	if (!(argb[0] >> 24) && !(argb[1] >> 24) && !(argb[2] >> 24) && !(argb[3] >> 24))
		return;
	REC(6, x[0], x[1], x[2], x[3], y[0], y[1], y[2], y[3], (float)(argb[0] >> 16), (float)(argb[0] & 0xffff),
	    (float)(argb[1] >> 16), (float)(argb[1] & 0xffff), (float)(argb[2] >> 16), (float)(argb[2] & 0xffff),
	    (float)(argb[3] >> 16), (float)(argb[3] & 0xffff), (float)bl);
	spr_state(NULL, false, bl);
	spr_vtx *v = spr_quad();
	if (!v)
		return;
	/* (P0 P1 P2) (P0 P2 P3): diagonal P0-P2 -> quad order P1 P0 P2 P3 */
	static const int ord[4] = {1, 0, 2, 3};
	u32 c0 = spr_color(pc(argb[0]), bl);
	for (int k = 0; k < 4; k++) {
		int i = ord[k];
		u32 c = argb[i] == argb[0] ? c0 : spr_color(pc(argb[i]), bl);
		spr_set(&v[k], x[i], y[i], SPR_SOLID_UV, SPR_SOLID_UV, c);
	}
}

static inline uint32_t grad_colour(int t, float v, uint32_t argb)
{
	uint32_t g = 0xffffffff;
	if (t >= HT_HYPER_B && t <= HT_HYPER_R) {
		int row = (int)v;
		g = hud_hyper_grad[t - HT_HYPER_B][row < 0 ? 0 : row > 255 ? 255 : row];
	}
	if (argb == 0xffffffff)
		return g;
	uint32_t r = ((g >> 16 & 0xff) * (argb >> 16 & 0xff)) / 255, gg = ((g >> 8 & 0xff) * (argb >> 8 & 0xff)) / 255;
	uint32_t b = ((g & 0xff) * (argb & 0xff)) / 255, a = ((g >> 24) * (argb >> 24)) / 255;
	return a << 24 | r << 16 | gg << 8 | b;
}

/* (P0 P1 P2) (P2 P1 P3): diagonal P1-P2, already the quad's TL TR BL BR order */
void hg_tri_tex2(int t, const float x[4], const float y[4], const float u[4], const float v[4], uint32_t argb, int bl)
{
	(void)u;
	uint32_t c[4];
	for (int k = 0; k < 4; k++)
		c[k] = grad_colour(t, v[k], argb);
	REC(7, (float)t, x[0], x[1], x[2], x[3], y[0], y[1], y[2], y[3], v[0], v[1], v[2], v[3], (float)(argb >> 16),
	    (float)(argb & 0xffff), (float)bl);
	if (!(c[0] >> 24) && !(c[1] >> 24) && !(c[2] >> 24) && !(c[3] >> 24))
		return;
	spr_state(NULL, false, bl);
	spr_vtx *q = spr_quad();
	if (!q)
		return;
	for (int k = 0; k < 4; k++)
		spr_set(&q[k], x[k], y[k], SPR_SOLID_UV, SPR_SOLID_UV, spr_color(pc(c[k]), bl));
}

void hg_tri_tex(int t, const float x[3], const float y[3], const float u[3], const float v[3], uint32_t argb, int bl)
{
	(void)u;
	uint32_t c[3];
	for (int k = 0; k < 3; k++) {
		uint32_t g = 0xffffffff;
		if (t >= HT_HYPER_B && t <= HT_HYPER_R) {
			int row = (int)v[k];
			g = hud_hyper_grad[t - HT_HYPER_B][row < 0 ? 0 : row > 255 ? 255 : row];
		}
		uint32_t r = ((g >> 16 & 0xff) * (argb >> 16 & 0xff)) / 255, gg = ((g >> 8 & 0xff) * (argb >> 8 & 0xff)) / 255;
		uint32_t b = ((g & 0xff) * (argb & 0xff)) / 255, a = ((g >> 24) * (argb >> 24)) / 255;
		c[k] = a << 24 | r << 16 | gg << 8 | b;
	}
	hg_tri(x, y, c, bl);
}

void hg_rect(float l, float t, float r, float b, uint32_t argb, int bl)
{
	if (!(argb >> 24) || r <= l || b <= t)
		return;
	REC(4, l, t, r, b, (float)(argb >> 16), (float)(argb & 0xffff), (float)bl);
	spr_rect_solid(l, t, r, b, pc(argb), bl);
}

void hg_line(float x0, float y0, float x1, float y1, uint32_t argb, int bl)
{
	if (!(argb >> 24))
		return;
	REC(5, x0, y0, x1, y1, (float)(argb >> 16), (float)(argb & 0xffff), (float)bl);
	/* C2D_DrawLine with thickness 1: the quad p0 -/+ n, p1 -/+ n (n = half-pixel normal) */
	float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
	if (!(len > 0.0f))
		return;
	float ux = (-dy / len) * 0.5f, uy = (dx / len) * 0.5f;
	spr_state(NULL, false, bl);
	spr_vtx *v = spr_quad();
	if (!v)
		return;
	u32 c = spr_color(pc(argb), bl);
	spr_set(&v[0], x0 - ux, y0 - uy, SPR_SOLID_UV, SPR_SOLID_UV, c);
	spr_set(&v[1], x1 - ux, y1 - uy, SPR_SOLID_UV, SPR_SOLID_UV, c);
	spr_set(&v[2], x0 + ux, y0 + uy, SPR_SOLID_UV, SPR_SOLID_UV, c);
	spr_set(&v[3], x1 + ux, y1 + uy, SPR_SOLID_UV, SPR_SOLID_UV, c);
}

/* Inside the top screen's batch pass (render_world). */
void hud_c2d_draw(const aos_world *w, int W, int H, float camX, float camY, float zoomDiv)
{
	hud_screen sc = hud_screen_for(W, H);
	hud_camera cam = {camX, camY, zoomDiv};
	spr_depth(DEPTH);
	hud_draw(w, &sc, &cam);
}
