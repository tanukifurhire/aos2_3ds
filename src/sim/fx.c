/* Common visual effects: effect type 0 (spawn FUN_004ef2c0 / update FUN_004f3e70 /
 * draw FUN_004eff30), particles (FUN_0068b8a0 / FUN_0068c500 / FUN_0068c000),
 * sparks (FUN_007bd5e0 / FUN_007bd030 / FUN_007bd350), message popups
 * (FUN_0065dff0 / FUN_0065dee0 / FUN_0065e050), hyper screen effects (dark
 * FUN_004e9280, speedline FUN_007bec20, blur FUN_004d7ac0, cut-in FUN_004e8100),
 * afterimages (FUN_004d7e00) and the camera focus list (FUN_005f8fc0).
 *
 * Draw conventions: the original draws effects with a 2D sprite call taking a
 * screen-space dest RECT (half-sizes divided by zoomDiv), a source RECT, a
 * rotation bam(-angle) in y-down screen space and an ARGB colour. Here the
 * half-sizes are emitted undivided in world units (halfW/halfH), the rotation
 * as rotZ = bam(angle) (CCW, y-up), and the ARGB value is repacked to RGBA.
 * Full-screen fills use the pseudo texture "$white", the blur copy "$blur"
 * (platform requests, see report). */
#include "fx.h"

#include <stdio.h>
#include <string.h>

#include "draw.h"
#include "fxstate.h"
#include "perf.h"
#include "sim.h"

/* ------------------------------------------------------------------ */
/* state access                                                        */

#ifdef AOS_HAVE_FXSTATE
#define FXS(w) (&(w)->obj.fx)
#else

static aos_fxstate fx_fallback; /* until aos_objects embeds aos_fxstate (engine request) */
#define FXS(w) ((void)(w), &fx_fallback)
#endif

/* ------------------------------------------------------------------ */
/* helpers                                                             */

static inline uint32_t ftou(float f) { return (uint32_t)(int64_t)f; } /* MSVC (uint)float */
static inline uint32_t argb2rgba(uint32_t argb) { return argb << 8 | argb >> 24; }
static inline uint32_t grey(uint32_t a) { a &= 0xff; return a << 24 | a << 16 | a << 8 | a; }
static inline int imin(int a, int b) { return a < b ? a : b; }

/* Common effect textures, globals 0x87fc98 + 4*i (FUN_004e9820). */
static const char *const ctex[83] = {
	"bomb", "hyper", "gaze", "halo", "circle", "spread0", "spread1",
	"diffuse0", "diffuse1", "diffuse2", "diffuse3", "slash0", "slash1", "explosion0", "explosion1",
	"burst0", "burst1", "burst2", "burst3", "impact0", "impact1", "impactd0", "impactd1",
	"smoke_00", "smoke_01", "smoke_02", "smoke_03", "smoke_04", "smoke_05", "smoke_06", "smoke_07",
	"gray_smoke_00", "gray_smoke_01", "gray_smoke_02", "gray_smoke_03",
	"flamer_00", "flamer_01", "flamer_02", "flamer_03", "flamer_04", "flamer_05",
	"flameb_00", "flameb_01", "flameb_02", "flameb_03", "flameb_04", "flameb_05",
	"blade00", "blade01", "blade02", "blade03", "blade04", "blade05", "blade06", "blade07",
	"blade08", "blade09", "blade10", "blade11", "blade12", "blade13", "blade14", "blade15",
	"blade16", "blade17", "blade18", "blade19", "blade20", "blade21", "blade22", "blade23",
	"blade24", "blade25", "blade26", "blade27", "blade28", "blade29", "blade30", "blade31",
	"elect00", "elect01", "elect02", "elect03",
};
enum {
	T_BOMB = 0, T_HYPER = 1, T_GAZE = 2, T_HALO = 3, T_CIRCLE = 4, T_SPREAD = 5, T_DIFFUSE = 7,
	T_SLASH = 11, T_IMPACT = 19, T_SMOKE = 23, T_GRAYSMOKE = 31, T_FLAMER = 35, T_FLAMEB = 41,
	T_BLADE = 47, T_ELECT = 79, T_COUNT = 83,
};

static int ctex_id(int i) { return i >= 0 && i < T_COUNT ? aos_tex(ctex[i]) : -1; }

/* 2D colour table 0x87e138 (r, g, b), filled in FUN_0049ae40. */
static const uint8_t col2d[14][3] = {
	{0x80, 0x90, 0xff}, {0xff, 0x90, 0x80}, {0x80, 0xff, 0x80}, {0xff, 0xff, 0x90},
	{0xe1, 0x80, 0xff}, {0xff, 0x80, 0xe1}, {0x90, 0xe1, 0xff}, {0xff, 0xe1, 0x80},
	{0x80, 0xff, 0xe1}, {0xe1, 0xff, 0x80}, {0x80, 0x80, 0xff}, {0xff, 0xa4, 0xa4},
	{0x20, 0x20, 0x20}, {0xff, 0xff, 0xff},
};
static uint32_t col2d_rgb(int v)
{
	if (v < 0 || v > 13)
		return 0xffffff;
	return (uint32_t)col2d[v][0] << 16 | (uint32_t)col2d[v][1] << 8 | col2d[v][2];
}

/* One sprite quad of an effect: dest half-size, source rect, ARGB colour, blend. */
static aos_dcmd *quad(int tex, int bank, const float pos[3], float hw, float hh, int x0, int y0,
                      int x1, int y1, uint32_t argb, int blend, uint16_t rot)
{
	aos_dcmd *c = aos_draw(tex, bank, pos[0], pos[1], 0);
	c->halfW = hw < 0 ? -hw : hw;
	c->halfH = hh < 0 ? -hh : hh;
	c->hasSrc = 1;
	c->src[0] = (float)x0;
	c->src[1] = (float)y0;
	c->src[2] = (float)x1;
	c->src[3] = (float)y1;
	c->rgba = argb2rgba(argb);
	c->blend = (uint8_t)blend;
	c->rotZ = rot;
	return c;
}

/* ------------------------------------------------------------------ */
/* common effect type 0: spawn FUN_004ef2c0                            */

static int fx_spawn0(aos_world *w, int32_t id, int32_t variant, const float pos[3], float angle,
                     int32_t owner, int32_t age0, int32_t team)
{
	/* init values: +0x2c scale, +0x30 alpha, +0x34/+0x38 dirs, +0x3c aux, +0x40 aux2, layer */
	float scale = 0, alpha = 0, dir = angle, rot = angle, aux = 0, aux2 = 0;
	int32_t layer = 1;
	switch (id) {
	case 100:
		alpha = 1, scale = 0.5f, aux = 0;
		break;
	case 200: case 0xd2:
		alpha = 1, scale = 0;
		aux = aos_rand_real(w, 0, 200.0f) + 100.0f;
		break;
	case 0xfa:
		alpha = 1, scale = 0, aux = 1;
		break;
	case 500: case 0x1f5:
		alpha = 1, scale = 1, layer = 0, aux = 1;
		break;
	case 600: case 0x262: case 0x26c: case 0x28a:
		alpha = 1, scale = 1;
		break;
	case 900:
		alpha = 0, scale = 1, aux = 0.1f;
		break;
	case 0x385:
		alpha = 1, scale = 0, aux = 0.1f;
		break;
	case 0x38e:
		alpha = 1, scale = 0;
		aux = aos_rand_real(w, 7.5f, 15.0f);
		break;
	case 0x38f:
		alpha = 1, scale = 0;
		aux = aos_rand_real(w, 3.5f, 7.5f);
		break;
	case 1000: case 0x3f2:
		alpha = 1, scale = 0.75f, aux = 1;
		break;
	case 0x44c: case 0x456:
		alpha = 1, scale = 1;
		aux = id == 0x44c ? 8.5f : 5.0f;
		break;
	case 0x514: case 0x515: case 0x516: case 0x546: case 0x547: case 0x548:
		alpha = 1, scale = 0.5f, aux = 1;
		break;
	case 2000: case 0x802:
		alpha = 1, scale = 1, layer = 0, aux = 1;
		break;
	case 0x834:
		alpha = 1, scale = 1, layer = 0;
		dir = aos_rand_real(w, -AOS_PI, AOS_PI);
		rot = aos_rand_real(w, -AOS_PI, AOS_PI);
		aux = 1;
		break;
	case 0x9c4: case 0x9c5: case 0x9c6:
		alpha = 1, scale = 0;
		break;
	case 0xa28: case 0xa29: case 0xa2a: case 0xa2b: case 0xa2c: case 0xa2d: case 0xa2e: case 0xa2f:
	case 0xa32: case 0xa33: case 0xa34: case 0xa35: case 0xa36: case 0xa37: case 0xa38: case 0xa39:
	case 0xa64: case 0xa65: case 0xa66: case 0xa67: case 0xa68: case 0xa69: case 0xa6a: case 0xa6b:
		alpha = 1, scale = 0.5f;
		break;
	case 0xa82: case 0xa83: case 0xa84: case 0xa85: case 0xa86: case 0xa87: case 0xa88: case 0xa89:
		alpha = 1, scale = 0.5f, aux2 = 1;
		break;
	case 0xa8c: case 0xa8d:
		alpha = 1, scale = 0, aux = 1;
		break;
	case 0xaf0:
		alpha = 1, scale = 0;
		dir = rot = aos_rand_real(w, -AOS_PI, AOS_PI);
		aux = 0.5f;
		break;
	case 3000: case 0xbc2:
		alpha = 1, scale = 0.25f, aux = 1;
		break;
	case 0xc1c:
		alpha = 1, scale = 1;
		break;
	default:
		return -1; /* original returns 0: nothing pushed */
	}
	int i = aos_effect_new(w, 0, id, variant, pos, angle, owner, age0, team);
	if (i < 0)
		return -1;
	aos_effect *e = &w->obj.effect[i];
	e->drawLayer = layer;
	e->scale = scale;
	e->alpha = alpha;
	e->moveDir = dir;
	e->rotation = rot;
	e->aux = aux;
	e->aux2 = aux2;
	return i;
}

/* ------------------------------------------------------------------ */
/* update FUN_004f3e70 (age already pre-incremented by the manager)    */

static void move_by(aos_effect *e, float ang, float spd)
{
	e->pos[0] = aos_cos(ang) * spd + e->pos[0];
	e->pos[1] = aos_sin(ang) * spd + e->pos[1];
}

static void follow_owner(aos_world *w, aos_effect *e)
{
	aos_player *o = aos_player_by_objid(w, e->ownerId);
	if (o) {
		e->pos[0] = o->x;
		e->pos[1] = o->y;
		e->pos[2] = o->z;
	}
}

/* blade groups: id++ on even ages, wrapping within its 8 ids */
static bool blade_step(aos_effect *e, float grow, int32_t first)
{
	e->scale += grow;
	e->alpha -= 0.05f;
	if (e->age % 2 == 0)
		e->id++;
	if (e->id > first + 7)
		e->id = first;
	return !(e->alpha <= 0.0f);
}

static bool fx_update0(aos_world *w, aos_effect *e)
{
	switch (e->id) {
	case 100: case 101: case 102: case 103: /* dash trail = graze zone (objects.md §4.7) */
		e->alpha -= 0.025f;
		e->scale += 0.08f;
		e->id = 100 + (aos_rand_int(w, 0, 400) & 3);
		return 0.0f < e->alpha;
	case 200: case 0xd2:
		e->scale += 0.03f;
		e->alpha -= 0.03f;
		e->pos[0] = aos_cos(e->rotation) * e->aux / 20.0f + e->pos[0];
		e->pos[1] = aos_sin(e->rotation) * e->aux / 20.0f + e->pos[1];
		return 0.0f < e->alpha;
	case 0xfa:
		follow_owner(w, e);
		e->scale += e->aux;
		e->aux *= 0.85f;
		if (e->age > 9)
			e->alpha -= 0.01f;
		return 0.0f < e->alpha;
	case 500: case 0x1f5:
		e->scale += 0.01f;
		e->alpha -= 0.02f;
		return e->age < 0x20;
	case 600: case 0x262: case 0x26c: case 0x28a:
		e->scale -= 0.025f;
		e->alpha -= 0.025f;
		return 0.0f < e->scale;
	case 900:
		e->scale -= 0.01f;
		e->alpha += 0.01f;
		return 0.0f < e->scale;
	case 0x385:
		e->scale += 0.015f;
		e->alpha -= 0.025f;
		return 0.0f < e->alpha;
	case 0x38e: case 0x38f:
		e->scale = e->aux / 750.0f + e->scale;
		e->alpha -= e->id == 0x38e ? 0.025f : 0.0125f;
		move_by(e, e->moveDir, e->aux);
		return 0.0f < e->alpha;
	case 1000: case 0x3f2:
		e->scale += 0.1f;
		e->alpha -= 0.04f;
		return e->age < 0x18;
	case 0x44c: case 0x456:
		move_by(e, e->moveDir, e->aux);
		e->scale += 0.075f;
		e->alpha -= 0.05f;
		return 0.0f < e->alpha;
	case 0x514: case 0x515: case 0x516: case 0x546: case 0x547: case 0x548:
		e->scale = 0.2f * e->aux + e->scale;
		e->alpha = e->alpha - 0.075f * e->aux;
		return 0.0f < e->alpha;
	case 2000: case 0x802:
		e->alpha -= 0.05f;
		return 0.0f < e->alpha;
	case 0x834:
		move_by(e, e->moveDir, e->aux);
		e->scale += 0.05f;
		e->alpha -= 0.02f;
		if (e->age % 3 == 0)
			e->age = aos_rand_int(w, 0, 100);
		return 0.0f < e->alpha;
	case 0x9c4: case 0x9c5: case 0x9c6:
		e->scale += 0.05f;
		return !(1.0f <= e->scale);
	case 0xa28: case 0xa29: case 0xa2a: case 0xa2b: case 0xa2c: case 0xa2d: case 0xa2e: case 0xa2f:
		return blade_step(e, 0.075f, 0xa28);
	case 0xa32: case 0xa33: case 0xa34: case 0xa35: case 0xa36: case 0xa37: case 0xa38: case 0xa39:
		return blade_step(e, 0.05f, 0xa32);
	case 0xa64: case 0xa65: case 0xa66: case 0xa67: case 0xa68: case 0xa69: case 0xa6a: case 0xa6b:
		return blade_step(e, 0.05f, 0xa64);
	case 0xa82: case 0xa83: case 0xa84: case 0xa85: case 0xa86: case 0xa87: case 0xa88: case 0xa89:
		return blade_step(e, 0.05f, 0xa82);
	case 0xa8c: case 0xa8d:
		if (e->id == 0xa8c)
			follow_owner(w, e);
		e->scale += e->aux;
		e->aux *= 0.9f;
		if (e->age > 4)
			e->alpha -= 0.05f;
		return !(e->alpha <= 0.0f);
	case 0xaf0:
		e->scale += e->aux;
		e->aux *= 0.9f;
		if (e->age > 4)
			e->alpha -= 0.05f;
		return 0.0f < e->alpha;
	case 3000: case 0xbc2:
		e->scale += 0.025f;
		e->alpha -= 0.05f;
		move_by(e, e->moveDir, 2.0f);
		return 0.0f < e->alpha;
	case 0xc1c: { /* FUN_00780450(team): team 1 -> P0, 2 -> P1 */
		aos_player *p = e->team == 1 ? &w->p[0] : e->team == 2 ? &w->p[1] : NULL;
		e->rotation = aos_rand_real(w, -AOS_PI, AOS_PI);
		e->alpha -= 0.05f;
		e->scale += 0.1f;
		if (p) { /* original dereferences NULL for other teams */
			e->pos[0] = -aos_cos(e->rotation) * 50.0f + p->x;
			e->pos[1] = -aos_sin(e->rotation) * 50.0f + p->y;
		}
		return 0.0f < e->alpha;
	}
	default:
		return true; /* unknown ids are kept (never spawned) */
	}
}

/* ------------------------------------------------------------------ */
/* draw FUN_004eff30                                                   */

/* rng sprite prefix per engine id (stats +0x3c sprite "<pfx>_rng0%d"). */
static const char *const rngpfx[AOS_NUM_CHARS] = {
	[3] = "sr", [4] = "at", [5] = "ts", [6] = "mr", [7] = "sm", [8] = "nt", [9] = "sb",
	[10] = "sg", [11] = "sk", [12] = "ir", [13] = "nk", [14] = "ke", [15] = "ky", [16] = "hm",
	[17] = "sa", [18] = "sab", /* UNVERIFIED: sa/sab assignment to Sumika / Sumika 2nd */
};

static void draw_trail(aos_world *w, aos_effect *e)
{
	/* literal: always player 0's sprite (stats[P0.id].+0x3c) and bank, frames 1 then 0 */
	int id = w->p[0].id;
	if (id < 0 || id >= AOS_NUM_CHARS || !rngpfx[id])
		return;
	int f = e->id & 3;
	if (e->alpha >= 0.2f) {
		if (e->alpha >= 0.35f) {
			if (e->alpha < 0.5f)
				f += 4;
		} else
			f += 8;
	} else
		f += 12;
	float hw = 10.0f * e->scale, hh = 60.0f * e->scale;
	uint32_t col = ftou(255.0f * e->alpha) << 24 | 0xffffff;
	static int rng_tex[AOS_NUM_CHARS][2]; /* interned "<pfx>_rng0<k>" + 1 (0 = not yet; perf: no snprintf per draw) */
	for (int k = 1; k >= 0; k--) {
		if (!rng_tex[id][k]) {
			char name[24];
			snprintf(name, sizeof name, "%s_rng0%d", rngpfx[id], k);
			rng_tex[id][k] = aos_tex(name) + 1;
		}
		quad(rng_tex[id][k] - 1, AOS_BANK_P0, e->pos, hw, hh, (f % 8) << 6, (f / 8) * 250,
		     (f % 8) * 64 + 63, (f / 8) * 250 + 249, col, 2, aos_rad2bam(e->rotation));
	}
}

static void fx_draw0(aos_world *w, aos_effect *e)
{
	if (e->age < 0) /* FUN_004f4de0 skips dormant records */
		return;
	const float s = e->scale, a = e->alpha;
	const uint16_t rot = aos_rad2bam(e->rotation);
	const int v = e->variant;
	switch (e->id) {
	case 100: case 101: case 102: case 103:
		draw_trail(w, e);
		return;
	case 200: case 0xd2: {
		uint32_t a8 = ftou(255.0f * a);
		int y0 = e->id == 200 ? 0 : 0x1e;
		quad(ctex_id(T_HYPER), AOS_BANK_COMMON, e->pos, e->aux * s, 12.0f, 0, y0, 0xff, y0 + 0x1d,
		     grey(a8), e->id == 200 ? 2 : 1, rot);
		return;
	}
	case 0xfa:
		quad(ctex_id(T_HALO), AOS_BANK_COMMON, e->pos, 36.0f * s, 36.0f * s, 0, 0, 0x1ff, 0x1ff,
		     ftou(255.0f * a) << 24 | 0xffffff, 2, 0);
		return;
	case 500: case 0x1f5: {
		int f = e->age / 4;
		float h1 = ((s - 1.0f) * 2.0f + s) * (e->id == 0x1f5 ? 128.0f : 64.0f);
		quad(ctex_id(T_BOMB), AOS_BANK_COMMON, e->pos, h1, h1, (f % 4) << 7, (f / 4) * 0x80 + 0x40,
		     (f % 4) * 0x80 + 0x7f, (f / 4) * 0x80 + 0xc0, ftou(100.0f * a) << 24 | 0xffffff, 1, rot);
		float h2 = (e->id == 0x1f5 ? 64.0f : 32.0f) * s;
		quad(ctex_id(T_BOMB), AOS_BANK_COMMON, e->pos, h2, h2, f << 6, 0, f * 0x40 + 0x3f, 0x3f,
		     ftou(255.0f * a) << 24 | 0xffc0c0, 2, rot);
		return;
	}
	case 600: case 0x262: case 0x26c: case 0x28a: {
		float m = e->id == 0x262 ? 0.5f : e->id == 0x26c ? 0.3f : e->id == 0x28a ? 1.5f : 1.0f;
		float h = 120.0f * s + 120.0f * s;
		if (e->id != 600)
			h *= m;
		int32_t ac = 0xff - (int32_t)((1.0f - a) * 512.0f);
		if (ac < 0)
			ac = 0;
		uint32_t a8 = ftou(255.0f * a);
		quad(ctex_id(T_BOMB), AOS_BANK_COMMON, e->pos, h, h, 0, 0x180, 0x7f, 0x1ff,
		     a8 << 24 | 0xff0000 | ((uint32_t)ac & 0xff) << 8 | ((uint32_t)ac & 0xff), 2, 0);
		h = 240.0f * s;
		if (e->id != 600)
			h *= m;
		uint32_t a2 = (uint32_t)((int32_t)(200.0f * a) / 2 + 0x9b);
		quad(ctex_id(T_BOMB), AOS_BANK_COMMON, e->pos, h, h, 0x80, 0x180, 0xff, 0x1ff,
		     a2 << 24 | 0xffffff, 2, 0);
		return;
	}
	case 900:
		quad(ctex_id(T_GAZE), AOS_BANK_COMMON, e->pos, 1024.0f * s, 1024.0f * s, 0, 0, 0xff, 0xff,
		     grey(ftou(255.0f * a)), 2, 0);
		return;
	case 0x385:
		quad(ctex_id(T_HYPER), AOS_BANK_COMMON, e->pos, 1024.0f * s, 1024.0f * s, 0, 0x100, 0xff,
		     0x1ff, grey(ftou(255.0f * a)), 2, 0);
		return;
	case 0x38e: case 0x38f: /* screen angle bam(π − rot) -> world rot − π */
		quad(ctex_id(T_HYPER), AOS_BANK_COMMON, e->pos, 512.0f * s, 32.0f * s, 0, 0, 0xff, 0x1d,
		     grey(ftou(255.0f * a)), 2, aos_rad2bam(e->rotation - AOS_PI));
		return;
	case 1000: case 0x3f2: {
		int bc = v % 8, tex = v / 8;
		int c0 = e->age < 10 ? 0 : (e->age - 10) / 5 + 1;
		float h = (e->id == 1000 ? 32.0f : 24.0f) * s;
		uint32_t col = ftou(255.0f * a) << 24 | 0xffffff;
		int x0 = (bc / 4) * 0x100 + c0 * 0x40;
		int t = ctex_id(T_SPREAD + tex);
		quad(t, AOS_BANK_COMMON, e->pos, h, h, x0, (bc % 4) << 7, x0 + 0x3f, (bc % 4) * 0x80 + 0x3f,
		     col, 2, rot);
		quad(t, AOS_BANK_COMMON, e->pos, h, h, x0, (bc % 4) * 0x80 + 0x40, x0 + 0x3f,
		     (bc % 4) * 0x80 + 0x7f, col, 2, rot);
		return;
	}
	case 0x44c: case 0x456: {
		int tex = v / 4, bc = v % 4, c0 = imin(e->age / 3, 2);
		float hw = (e->id == 0x44c ? 48.0f : 32.0f) * s, hh = (e->id == 0x44c ? 12.0f : 8.0f) * s;
		aos_dcmd *c = quad(ctex_id(T_DIFFUSE + tex), AOS_BANK_COMMON, e->pos, hw, hh, c0 << 7,
		                   bc << 7, c0 * 0x80 + 0x7f, bc * 0x80 + 0x7f,
		                   ftou(255.0f * a) << 24 | 0xffffff, 2, rot);
		c->flipX = 1; /* dest rect is x-mirrored */
		return;
	}
	case 0x514: case 0x515: case 0x516: case 0x546: case 0x547: case 0x548: {
		int tex = v / 8, vv = v % 8, bc = vv / 4, c0 = vv % 4;
		int k = (e->id - 0x514) % 0x32; /* 0, 1, 2 */
		float h = (k == 0 ? 48.0f : k == 1 ? 32.0f : 16.0f) * s;
		float hw = e->id >= 0x546 ? h / 3.0f : h;
		int t = ctex_id(T_IMPACT + tex);
		quad(t, AOS_BANK_COMMON, e->pos, hw, h, bc << 8, c0 << 7, bc * 0x100 + 0x7f, c0 * 0x80 + 0x7f,
		     ftou(128.0f * a) << 24 | 0xffffff, 1, rot);
		quad(t, AOS_BANK_COMMON, e->pos, hw, h, bc * 0x100 + 0x80, c0 << 7, bc * 0x100 + 0xff,
		     c0 * 0x80 + 0x7f, ftou(255.0f * a) << 24 | 0xffffff, 2, rot);
		return;
	}
	case 2000: case 0x802: {
		int f = imin(e->age / 8, 7);
		float hw = (e->id == 2000 ? 24.0f : 32.0f) * s, hh = (e->id == 2000 ? 24.0f : 12.0f) * s;
		uint32_t a8 = ftou(255.0f * a);
		quad(ctex_id(T_SMOKE + f), AOS_BANK_COMMON, e->pos, hw, hh, 0, 0, 0x3f, 0x3f,
		     a8 << 24 | (a8 & 0xff) << 16 | (a8 & 0xff) << 8 | (a8 & 0xff), 1, rot);
		return;
	}
	case 0x834: {
		int f = (e->age / 2) % 4;
		uint32_t a8 = ftou(255.0f * a), ac = (uint32_t)((int32_t)a8 / 2);
		quad(ctex_id(T_GRAYSMOKE + f), AOS_BANK_COMMON, e->pos, 32.0f * s, 32.0f * s, 0, 0, 0x7f,
		     0x7f, a8 << 24 | (ac & 0xff) << 16 | (ac & 0xff) << 8 | (ac & 0xff), 1, rot);
		return;
	}
	case 0x9c4: case 0x9c5: case 0x9c6: {
		int tex = v / 8, c0 = v % 8;
		float ha = 300.0f * s + 80.0f, hb = (1.0f - s) * 32.0f;
		if (e->id == 0x9c5)
			ha *= 1.5f, hb *= 1.5f;
		if (e->id == 0x9c6)
			ha *= 3.0f, hb *= 3.0f;
		int t = ctex_id(T_SLASH + tex);
		quad(t, AOS_BANK_COMMON, e->pos, ha, hb, 0xfa, c0 * 0x3c, 499, c0 * 0x3c + 0x3b,
		     ftou(128.0f * a) << 24 | 0xffffff, 1, rot);
		ha *= 0.75f, hb *= 0.75f;
		quad(t, AOS_BANK_COMMON, e->pos, ha, hb, 0, c0 * 0x3c, 0xf9, c0 * 0x3c + 0x3b,
		     grey(ftou(255.0f * a)), 2, rot);
		return;
	}
	case 0xa28: case 0xa29: case 0xa2a: case 0xa2b: case 0xa2c: case 0xa2d: case 0xa2e: case 0xa2f:
	case 0xa32: case 0xa33: case 0xa34: case 0xa35: case 0xa36: case 0xa37: case 0xa38: case 0xa39:
	case 0xa64: case 0xa65: case 0xa66: case 0xa67: case 0xa68: case 0xa69: case 0xa6a: case 0xa6b:
	case 0xa82: case 0xa83: case 0xa84: case 0xa85: case 0xa86: case 0xa87: case 0xa88: case 0xa89: {
		int f = imin(e->age / 4, 3) + (e->id % 10) * 4;
		float hw, hh;
		if (e->id <= 0xa2f)
			hw = 28.0f * s, hh = 54.0f * s;
		else if (e->id <= 0xa39)
			hw = 72.0f * s, hh = 128.0f * s;
		else if (e->id <= 0xa6b)
			hw = 32.0f * s, hh = 128.0f * s;
		else
			hw = 160.0f * s * e->aux2, hh = 640.0f * s * e->aux2;
		quad(ctex_id(T_BLADE + f), AOS_BANK_COMMON, e->pos, hw, hh, 0, 0, 0xff, 0xff,
		     ftou(255.0f * a) << 24 | col2d_rgb(v), 1, rot);
		return;
	}
	case 0xa8c: case 0xa8d:
		quad(ctex_id(T_GAZE), AOS_BANK_COMMON, e->pos, 48.0f * s, 48.0f * s, 0, 0, 0xff, 0xff,
		     ftou(255.0f * a) << 24 | 0xffffff, 2, 0);
		return;
	case 0xaf0: {
		int f = imin(e->age / 4, 2);
		float h = 40.0f * s; /* version >= 0x58a (36 before) */
		quad(ctex_id(T_CIRCLE), AOS_BANK_COMMON, e->pos, h, h, (f % 2) << 8, (f / 2) << 8,
		     (f % 2) * 0x100 + 0xff, (f / 2) * 0x100 + 0xff,
		     ftou(255.0f * a) << 24 | col2d_rgb(v), 2, rot);
		return;
	}
	case 3000: case 0xbc2: {
		int f = imin((e->age / 2) * 2, 4) + e->age % 2;
		int t = ctex_id((e->id == 3000 ? T_FLAMER : T_FLAMEB) + f);
		float h = 60.0f * s;
		uint32_t a8 = ftou(32.0f * a);
		quad(t, AOS_BANK_COMMON, e->pos, h, h, 0, 0, 0x7f, 0x7f,
		     a8 << 24 | 0xff0000 | (a8 & 0xff) << 8 | (a8 & 0xff), 1, rot);
		quad(t, AOS_BANK_COMMON, e->pos, h, h, 0, 0, 0x7f, 0x7f,
		     ftou(64.0f * a) << 24 | 0xffffff, 2, rot);
		return;
	}
	case 0xc1c: {
		/* literal: for a in [.25, .75) the alpha byte wraps (only the low byte is kept) */
		uint32_t a8 = a < 0.75f ? ftou((a / 0.25f) * 255.0f) : 0xff;
		quad(ctex_id(T_ELECT + e->age % 4), AOS_BANK_COMMON, e->pos, 24.0f * s, 24.0f * s, 0, 0,
		     0x7f, 0x7f, a8 << 24 | 0xffffff, 2, rot);
		return;
	}
	default:
		return;
	}
}

void aos_register_fx(void)
{
	aos_otab.effectSpawn[0] = fx_spawn0;
	aos_otab.effectUpdate[0] = fx_update0;
	aos_otab.effectDraw[0] = fx_draw0;
}

/* ------------------------------------------------------------------ */
/* particles                                                           */

static bool particle_push(aos_world *w, const aos_particle *pr)
{
	if (w->obj.nparticle >= AOS_MAX_PARTICLE) {
		w->obj.overflow++;
		return false;
	}
	w->obj.particle[w->obj.nparticle++] = *pr;
	return true;
}

int aos_spawn_particle(aos_world *w, int32_t kind, int32_t colour, const float pos[3], float angle,
                       int32_t owner, int32_t age0, int32_t count) /* FUN_0068b8a0 */
{
	aos_particle r;
	memset(&r, 0, sizeof r);
	int n = 0;
	switch (kind) {
	case 1000: case 0x3e9: case 0x3ea:
		r.kind = 1000;
		r.alpha = 1.0f;
		break;
	case 0x44c: case 0x456: case 0x4b0: case 0x4ba:
		r.kind = kind;
		r.alpha = 0.5f;
		break;
	default:
		return 0;
	}
	r.colour = colour;
	r.pos[0] = pos[0], r.pos[1] = pos[1], r.pos[2] = pos[2];
	r.owner = owner;
	r.age = age0;
	r.scale = 1.0f;
	for (int i = 0; i < count; i++) {
		if (kind == 0x3e9)
			r.dir = angle;
		else if (kind == 0x3ea)
			r.dir = aos_rand_real(w, -AOS_PI, AOS_PI);
		else if (kind == 1000)
			r.dir = aos_rand_real(w, -1.2f, 1.2f) + angle;
		else
			r.dir = aos_rand_real(w, -0.5f, 0.5f) + angle;
		r.spin[1] = aos_rand_real(w, -AOS_PI, AOS_PI); /* +0x18 */
		r.spin[0] = aos_rand_real(w, -AOS_PI, AOS_PI); /* +0x14 */
		r.growth = aos_rand_real(w, -1.0f, 1.0f) * 0.02f;
		r.speed = r.kind == 1000 ? aos_rand_real(w, 2.5f, 6.5f) : aos_rand_real(w, 0.5f, 3.5f);
		n += particle_push(w, &r);
	}
	return n;
}

/* One particle's frame; false = erase it. */
static bool particle_step(aos_particle *r)
{
	r->age++;
	int fadeAge;
	float fade;
	switch (r->kind) {
	case 1000: fadeAge = 0x19, fade = 0.04f; break;
	case 0x44c: case 0x456: fadeAge = 10, fade = 0.05f; break;
	case 0x4b0: case 0x4ba: fadeAge = 4, fade = 0.1f; break;
	default: return true;
	}
	if (r->age > fadeAge)
		r->alpha -= fade;
	r->spin[1] += 0.2f;
	r->spin[0] += 0.1f;
	r->pos[0] = aos_cos(r->dir) * r->speed + r->pos[0];
	r->pos[1] = aos_sin(r->dir) * r->speed + r->pos[1];
	r->scale += r->growth;
	return !(r->alpha <= 0.0f);
}

/* The original erases dead records one by one (vector::erase in the walk).
 * Nothing in this walk looks at other records or spawns new ones, so one
 * order-preserving compaction pass gives the identical list without moving
 * the tail once per erased record. */
static void particles_update(aos_world *w) /* FUN_0068c500 */
{
	int n = w->obj.nparticle, keep = 0;
	for (int i = 0; i < n; i++) {
		aos_particle *r = &w->obj.particle[i];
		if (!particle_step(r))
			continue;
		if (keep != i) {
			memcpy(&w->obj.particle[keep], r, sizeof *r);
			AOS_PERF_ADD(erase_bytes, sizeof *r);
		}
		keep++;
	}
	AOS_CLEAR_DEAD(&w->obj.particle[keep], sizeof(aos_particle) * (size_t)(n - keep));
	w->obj.nparticle = keep;
}

static const char *const sphname[12] = {
	"blue", "red", "green", "yellow", "violet", "magenta",
	"aqua", "orange", "emerald", "ygreen", "cobalt", "carmine",
};

void aos_particles_draw(aos_world *w) /* FUN_0068c000: sph<c>b 24x24 (B1), sph<c> 16x16 (B2) */
{
	/* interned "sph<c>b" / "sph<c>" + 1 per colour (0 = not yet), looked up on first use as before: the
	 * per-particle snprintf was the most expensive part of this loop on the 3DS (perf only) */
	static int sph_tex[12][2];
	for (int i = 0; i < w->obj.nparticle; i++) {
		const aos_particle *r = &w->obj.particle[i];
		if (r->colour < 0 || r->colour >= 12)
			continue; /* UNVERIFIED: loader reserves 14 colours, only 12 textures exist */
		float a = r->alpha;
		int *t = sph_tex[r->colour];
		if (!t[0]) {
			char name[24];
			snprintf(name, sizeof name, "sph%sb", sphname[r->colour]);
			t[0] = aos_tex(name) + 1;
		}
		aos_dcmd *c = aos_draw(t[0] - 1, AOS_BANK_COMMON, r->pos[0], r->pos[1], 0);
		c->halfW = c->halfH = 12.0f;
		c->sx = c->sy = r->scale;
		c->blend = AOS_BLEND_ALPHA;
		c->rgba = aos_rgba(a, a, a, a * 0.25f);
		if (!t[1]) {
			char name[24];
			snprintf(name, sizeof name, "sph%s", sphname[r->colour]);
			t[1] = aos_tex(name) + 1;
		}
		c = aos_draw(t[1] - 1, AOS_BANK_COMMON, r->pos[0], r->pos[1], 0);
		c->halfW = c->halfH = 8.0f;
		c->sx = c->sy = r->scale;
		c->blend = AOS_BLEND_ADD_ALPHA;
		c->rgba = aos_rgba(1, 1, 1, a);
	}
}

/* ------------------------------------------------------------------ */
/* sparks                                                              */

int aos_spawn_spark(aos_world *w, int32_t kind, int32_t colour, int32_t maxLen, int32_t count,
                    float angle, const float pos[3], int32_t owner) /* FUN_007bd5e0 */
{
	float turn, lo, hi, base;
	bool targetIsAngle;
	switch (kind) {
	case 1000: turn = 0.02f, lo = -2, hi = 5, base = 10, targetIsAngle = true; break;
	case 0x44c: turn = 0.01f, lo = -2, hi = 4, base = 6, targetIsAngle = true; break;
	case 0x4b0: turn = 0.01f, lo = -2, hi = 2, base = 3, targetIsAngle = true; break;
	case 2000: case 0x7da: case 3000: case 0xbc2:
		turn = 0.02f, lo = -2, hi = 5, base = 10, targetIsAngle = false;
		break;
	default:
		return 0;
	}
	int n = 0;
	for (int i = 0; i < count; i++) {
		aos_spark s;
		memset(&s, 0, sizeof s);
		s.kind = kind;
		s.maxLen = maxLen;
		s.age = 0;
		s.npts = 1;
		s.pts[0][0] = pos[0];
		s.pts[0][1] = pos[1];
		s.owner = owner;
		s.colour = colour;
		s.alpha = 1.0f;
		if (kind == 0x7da || kind == 0xbc2)
			s.heading = angle;
		else
			s.heading = aos_rand_real(w, -1.5707964f, 1.5707964f) + angle;
		aos_angle_normalize(&s.heading);
		s.target = targetIsAngle ? angle : s.heading;
		s.turn = turn;
		s.speed = aos_rand_real(w, lo, hi) + base;
		if (w->obj.nspark >= AOS_MAX_SPARK) {
			w->obj.overflow++;
			continue;
		}
		w->obj.spark[w->obj.nspark++] = s;
		n++;
	}
	return n;
}

/* One spark's frame; false = erase it. */
static bool spark_step(aos_world *w, aos_spark *s)
{
	{
		if (s->kind / 1000 == 3 && aos_rand_int(w, 0, 100) > 0x32) {
			s->heading = aos_rand_real(w, -10.0f, 10.0f) / 30.0f + s->heading;
			s->speed = aos_rand_real(w, 0.75f, 1.15f) * s->speed;
		}
		/* push_front(front + dir·speed) */
		float nx = aos_cos(s->heading) * s->speed + s->pts[0][0];
		float ny = aos_sin(s->heading) * s->speed + s->pts[0][1];
		int keep = s->npts < AOS_SPARK_PTS ? s->npts : AOS_SPARK_PTS - 1;
		memmove(&s->pts[1], &s->pts[0], sizeof s->pts[0] * (size_t)keep);
		s->pts[0][0] = nx;
		s->pts[0][1] = ny;
		s->npts = keep + 1;
		if (s->maxLen < s->npts || s->age < 2)
			s->npts--; /* pop_back */
		aos_angle_toward(&s->heading, s->target, s->turn);
		s->alpha -= 0.025f;
		s->age++;
	}
	return 0.0f < s->alpha;
}

/* Same single-pass compaction as particles_update: the walk only touches the
 * record itself and the match RNG (in record order), never other sparks. */
static void sparks_update(aos_world *w) /* FUN_007bd030 */
{
	int n = w->obj.nspark, keep = 0;
	for (int i = 0; i < n; i++) {
		aos_spark *s = &w->obj.spark[i];
		if (!spark_step(w, s))
			continue;
		if (keep != i) {
			memcpy(&w->obj.spark[keep], s, sizeof *s);
			AOS_PERF_ADD(erase_bytes, sizeof *s);
		}
		keep++;
	}
	AOS_CLEAR_DEAD(&w->obj.spark[keep], sizeof(aos_spark) * (size_t)(n - keep));
	w->obj.nspark = keep;
}

/* spark RGB table 0xa4e2c8 (FUN_007bcd30) */
static const uint8_t sparkrgb[12][3] = {
	{0x00, 0x80, 0xff}, {0xff, 0x40, 0x00}, {0x20, 0xe1, 0x40}, {0xff, 0xe1, 0x00},
	{0xc0, 0x00, 0xff}, {0xff, 0x00, 0xc0}, {0x00, 0xe1, 0xff}, {0xe1, 0xc0, 0x00},
	{0x80, 0xff, 0xe1}, {0x80, 0xff, 0x40}, {0x80, 0x80, 0xe1}, {0xff, 0x00, 0x00},
};

/* 1-px line as a "$white" quad (1 screen px ≈ zoom world units at 800x600). */
static void line(aos_world *w, float x0, float y0, float x1, float y1, uint32_t argb, int blend)
{
	float dx = x1 - x0, dy = y1 - y0;
	aos_dcmd *c = aos_draw(aos_tex("$white"), AOS_BANK_COMMON, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 0);
	c->halfW = aos_sqrt(dx * dx + dy * dy) * 0.5f;
	c->halfH = 0.5f * (w->obj.zoom > 0 ? w->obj.zoom : 1.0f);
	c->rotZ = aos_rad2bam(aos_atan2(dy, dx));
	c->rgba = argb2rgba(argb);
	c->blend = (uint8_t)blend;
}

void aos_sparks_draw(aos_world *w) /* FUN_007bd350 */
{
	for (int i = 0; i < w->obj.nspark; i++) {
		const aos_spark *s = &w->obj.spark[i];
		if (s->npts < 2)
			continue;
		uint32_t rgb = 0;
		if (s->colour >= 0 && s->colour < 12)
			rgb = (uint32_t)sparkrgb[s->colour][0] << 16 | (uint32_t)sparkrgb[s->colour][1] << 8 |
			      sparkrgb[s->colour][2];
		float a = 255.0f * s->alpha;
		for (int k = 1; k < s->npts; k++) {
			int32_t al = (int32_t)(a * (1.0f - (float)(k - 1) / (float)s->npts));
			line(w, s->pts[k - 1][0], s->pts[k - 1][1], s->pts[k][0], s->pts[k][1],
			     (uint32_t)al << 24 | rgb, 2);
		}
	}
}

/* ------------------------------------------------------------------ */
/* message popups                                                      */

void aos_popup(aos_world *w, int kind, const float pos[3], int32_t owner) /* FUN_0065dff0 */
{
	/* Kind 4 records are pushed by the original but only ever get timer++ (never drawn or
	 * removed); they carry no behaviour, so the port does not store them (pool safety). */
	if (kind < 0 || kind > 3)
		return;
	if (w->obj.npopup >= AOS_MAX_POPUP) {
		w->obj.overflow++;
		return;
	}
	aos_popuprec *q = &w->obj.popup[w->obj.npopup++];
	q->kind = kind;
	q->pos[0] = pos[0];
	q->pos[1] = pos[1];
	q->pos[2] = pos[2];
	q->owner = owner;
	q->timer = 0;
}

static void popups_update(aos_world *w) /* FUN_0065dee0 */
{
	for (int i = 0; i < w->obj.npopup; i++) {
		aos_popuprec *q = &w->obj.popup[i];
		if (q->kind >= 0 && q->kind <= 3) {
			q->pos[1] += 1.5f;
			if (q->timer >= 0x29) {
				memmove(q, q + 1, sizeof *q * (size_t)(w->obj.npopup - i - 1));
				AOS_CLEAR_DEAD(&w->obj.popup[w->obj.npopup - 1], sizeof *q);
				w->obj.npopup--;
				i--;
				continue;
			}
		}
		q->timer++;
	}
}

void aos_popups_draw(aos_world *w) /* FUN_0065e050 */
{
	static const char *const msg[4] = {"msg_canl", "msg_pass", "msg_chrg", "msg_gurd"};
	for (int i = 0; i < w->obj.npopup; i++) {
		const aos_popuprec *q = &w->obj.popup[i];
		if (q->kind < 0 || q->kind > 3)
			continue;
		float hw = 50.0f, hh = 50.0f;
		if (q->timer >= 0x15) {
			hw = (float)(q->timer - 0x14) * 1.5f + 50.0f;
			hh = 50.0f - (float)(q->timer - 0x14) * 2.5f;
		}
		uint32_t a = q->timer % 2 == 0 ? 0xe1 : 0x60;
		quad(aos_tex(msg[q->kind]), AOS_BANK_COMMON, q->pos, hw, hh, 0, 0, 0x7f, 0x7f,
		     a << 24 | 0xffffff, 1, 0);
	}
}

/* ------------------------------------------------------------------ */
/* screen effects                                                      */

void aos_screen_dark(aos_world *w, int32_t frames, float level) /* FUN_004e9280 */
{
	aos_fxstate *f = FXS(w);
	if (f->ndark >= AOS_MAX_DARK)
		return;
	f->dark[f->ndark++] = (aos_darkrec){frames, 0.0f, level};
}

void aos_speedline(aos_world *w, int32_t frames, const float pos[3], float angle, float spread)
{                                                         /* FUN_007bec20 (pos is not stored) */
	(void)pos;
	aos_fxstate *f = FXS(w);
	if (f->nspeed >= AOS_MAX_SPEED) /* gated on option DAT_00881ebc == 1 (assumed on) */
		return;
	f->speed[f->nspeed++] = (aos_speedrec){frames, angle, spread, 0.0f, 0.0f};
}

void aos_screen_blur(aos_world *w, int32_t frames, float amount, int32_t blend) /* FUN_004d7ac0 */
{
	aos_fxstate *f = FXS(w); /* gated on option DAT_00881ebc == 1 (assumed on) */
	f->blurTimer = frames;
	f->blurOn = 1;
	f->blurAmount = amount;
	f->blurBlend = blend;
	f->blurCaptured++; /* sim side: counts blur starts (deterministic); the draw side tracks capture */
	f->blurLevel = 1.0f;
}

void aos_cutin(aos_world *w) /* FUN_004e8100 (no caller in this build) */
{
	aos_fxstate *f = FXS(w);
	if (f->ncutin >= AOS_MAX_CUTIN)
		return;
	f->cutin[f->ncutin++] = (aos_cutinrec){0, 1, 0};
}

void aos_camera_focus(aos_world *w, int32_t timer, int32_t playerId, const float pos[3], float zoom)
{                                                         /* FUN_005f8fc0 */
	aos_fxstate *f = FXS(w);
	if (f->nfocus >= AOS_MAX_FOCUS)
		return;
	f->focus[f->nfocus++] = (aos_focusrec){playerId, {pos[0], pos[1], pos[2]}, zoom, timer, 0.0f};
}

void aos_camera_focus_apply(aos_world *w, float *cx, float *cy, float *z) /* FUN_005f8b90 part */
{
	aos_fxstate *f = FXS(w);
	for (int i = 0; i < f->nfocus; i++) {
		aos_focusrec *e = &f->focus[i];
		e->timer--;
		if (e->timer < 1)
			e->w -= 0.05f;
		else
			e->w += 0.05f;
		if (1.0f < e->w)
			e->w = 1.0f;
		if (e->timer < 1 && e->w <= 0.0f) {
			memmove(e, e + 1, sizeof *e * (size_t)(f->nfocus - i - 1));
			f->nfocus--;
			i--;
		}
	}
	for (int i = 0; i < f->nfocus; i++) {
		const aos_focusrec *e = &f->focus[i];
		float ex = e->pos[0], ey = e->pos[1];
		if (e->playerId != -1) {
			aos_player *p = aos_player_by_objid(w, e->playerId);
			if (p)
				ex = p->x, ey = p->y;
		}
		*cx = (ex - *cx) * e->w + *cx;
		*cy = (ey - *cy) * e->w + *cy;
		*z = (e->zoom - *z) * e->w + *z;
	}
}

static void screen_update(aos_world *w)
{
	aos_fxstate *f = FXS(w);
	/* blur FUN_004d78b0 */
	if (f->blurTimer < 1) {
		f->blurLevel -= 0.025f;
		if (f->blurLevel <= 0.0f)
			f->blurOn = 0;
	} else
		f->blurTimer--;
	/* cut-in FUN_004e8150 */
	for (int i = 0; i < f->ncutin; i++)
		if (++f->cutin[i].age > 0x31) {
			memmove(&f->cutin[i], &f->cutin[i + 1], sizeof f->cutin[0] * (size_t)(f->ncutin - i - 1));
			f->ncutin--;
			i--;
		}
	/* dark FUN_004e9030 */
	for (int i = 0; i < f->ndark; i++) {
		aos_darkrec *d = &f->dark[i];
		d->timer--;
		if (d->timer < 1)
			d->level -= 0.01f;
		else
			d->level += 0.1f;
		if (1.0f < d->level)
			d->level = 1.0f;
		if (d->level <= 0.0f) {
			memmove(d, d + 1, sizeof *d * (size_t)(f->ndark - i - 1));
			f->ndark--;
			i--;
		}
	}
	/* speedline FUN_007be6f0 */
	for (int i = 0; i < f->nspeed; i++) {
		aos_speedrec *s = &f->speed[i];
		s->timer--;
		if (s->timer < 1)
			s->level -= 0.01f;
		else
			s->level += 0.1f;
		if (1.0f < s->level)
			s->level = 1.0f;
		s->phase += 0.025f;
		if (1.0f < s->phase)
			s->phase -= 1.0f;
		if (s->level <= 0.0f) {
			memmove(s, s + 1, sizeof *s * (size_t)(f->nspeed - i - 1));
			f->nspeed--;
			i--;
		}
	}
}

/* world-space rectangle covering the 800x600 reference screen around the camera */
static aos_dcmd *screen_quad(aos_world *w, const char *tex, float extra)
{
	float z = w->obj.zoom > 0 ? w->obj.zoom : 1.0f;
	aos_dcmd *c = aos_draw(aos_tex(tex), AOS_BANK_COMMON, w->obj.camX, w->obj.camY, 0);
	c->halfW = 400.0f * z * extra;
	c->halfH = 300.0f * z * extra;
	return c;
}

void aos_dark_draw(aos_world *w) /* FUN_004e9180 */
{
	aos_fxstate *f = FXS(w);
	if (f->ndark == 0)
		return;
	float d = 0.0f;
	for (int i = 0; i < f->ndark; i++) {
		float v = f->dark[i].target * f->dark[i].level;
		if (d < v)
			d = v;
	}
	aos_dcmd *c = screen_quad(w, "$white", 1.5f); /* full window */
	c->rgba = argb2rgba((uint32_t)(int32_t)(255.0f * d) << 24);
	c->blend = AOS_BLEND_ALPHA;
}

void aos_speedlines_draw(aos_world *w) /* FUN_007be8b0 */
{
	/* The original renders "speedline" on a radius-800 sphere (FUN_00468d80) seen from
	 * inside: RotY(angle) RotZ(spread), UV scrolled by -phase, colour (level,1,1,1), B2.
	 * From the centre its streaks run along meridians and converge on a pole lying in the
	 * aim direction. 2D equivalent (approximation of the 3D projection): streaks radiating
	 * from a vanishing point off-screen along the aim, fanned by the spread, scrolling
	 * outward with the phase. Deterministic, no RNG. */
	aos_fxstate *f = FXS(w);
	float z = w->obj.zoomView > 0 ? w->obj.zoomView : (w->obj.zoom > 0 ? w->obj.zoom : 1.0f);
	for (int i = 0; i < f->nspeed; i++) {
		const aos_speedrec *s = &f->speed[i];
		if (s->level <= 0.0f)
			continue;
		float ca = aos_cos(s->angle), sa = aos_sin(s->angle);
		float vx = w->obj.camX + ca * 900.0f * z, vy = w->obj.camY + sa * 900.0f * z;
		for (int k = 0; k < 28; k++) {
			uint32_t hsh = (uint32_t)k * 2654435761u;
			float off = ((float)(hsh >> 8 & 0xffff) / 65535.0f - 0.5f) * (1.2f + s->spread);
			float a = s->angle + AOS_PI + off;
			float ph = s->phase * 3.0f + (float)(hsh >> 4 & 0xff) / 255.0f;
			ph -= (float)(int)ph;
			float r0 = (300.0f + ph * 900.0f) * z, r1 = r0 + (120.0f + (float)(hsh & 0x7f)) * z;
			float x0 = vx + aos_cos(a) * r0, y0 = vy + aos_sin(a) * r0;
			float x1 = vx + aos_cos(a) * r1, y1 = vy + aos_sin(a) * r1;
			aos_draw_line(x0, y0, x1, y1, 2.0f * z, aos_rgba(1, 1, 1, s->level * 0.35f), AOS_BLEND_ADD_ALPHA);
		}
	}
}

void aos_blur_draw(aos_world *w) /* FUN_004d7900 */
{
	/* Draws the captured previous frame (FUN_004d7a70) full-screen with source = the
	 * central 97.5% (radial zoom), alpha 255·amount·level, blend = mode argument. */
	aos_fxstate *f = FXS(w);
	static uint8_t seen_start = 0xff, captured; /* draw-only state: never written into the world */
	if (seen_start != f->blurCaptured) {
		seen_start = f->blurCaptured;
		captured = 0;
	}
	if (f->blurOn && captured) {
		aos_dcmd *c = screen_quad(w, "$blur", 1.0f);
		c->sx = c->sy = 1.0f / 0.975f;
		c->rgba = argb2rgba(ftou(255.0f * f->blurAmount * f->blurLevel) << 24 | 0xffffff);
		c->blend = (uint8_t)f->blurBlend;
	}
	if (f->blurOn) /* FUN_004d7a70: capture after the frame (runs at the end of drawing) */
		captured = 1;
}

void aos_cutin_draw(aos_world *w) /* FUN_004e8200 */
{
	aos_fxstate *f = FXS(w);
	float z = w->obj.zoom > 0 ? w->obj.zoom : 1.0f;
	for (int i = 0; i < f->ncutin; i++) {
		aos_dcmd *c = aos_draw(aos_tex("$white"), AOS_BANK_COMMON, w->obj.camX, w->obj.camY, 0);
		c->halfW = 400.0f * z;
		c->halfH = 64.0f * z;
		c->rgba = argb2rgba(0x80000000u);
		c->blend = AOS_BLEND_ALPHA;
		/* UNVERIFIED: then a 512x128 portrait of player 0 (sprite vector lost in the
		 * decompile) twice, 0xff808080 B1 and 0xff909090 B2, half 256x64 — omitted. */
	}
}

/* ------------------------------------------------------------------ */
/* afterimages (draw-only state, like the original which skips recording
 * while re-simulating)                                                */

#define MAX_AFTER 128
typedef struct {
	int32_t objId;
	float pos[3];
	aos_dcmd cmd;
	int32_t rot[3];
	int32_t blend;
	float a, f34, f38, f3c; /* +0x30 alpha, tint fields */
} afterimage;
static afterimage after[MAX_AFTER];
static int nafter;

void aos_afterimage(aos_world *w, const aos_dcmd *model, int flag, const aos_player *p,
                    const int32_t rot[3]) /* FUN_004d7e00 */
{
	(void)w, (void)flag;
	if (!model || nafter >= MAX_AFTER)
		return;
	afterimage *r = &after[nafter++];
	r->objId = p->objId;
	r->pos[0] = p->x, r->pos[1] = p->y, r->pos[2] = p->z;
	r->cmd = *model;
	r->rot[0] = rot ? rot[0] : 0, r->rot[1] = rot ? rot[1] : 0, r->rot[2] = rot ? rot[2] : 0;
	r->blend = 2;
	r->a = 0.25f;
	float h = p->heat;
	if (h >= 1.0f) {
		if (h >= 2.0f) {
			r->f34 = 1.0f;
			r->f3c = 0.85f - (h - 2.0f) * 0.5f;
			r->f38 = 0.0f;
		} else {
			r->f34 = (h - 1.0f) * 0.75f + 0.25f;
			r->f3c = 1.0f - (h - 1.0f) * 0.15f;
			r->f38 = 0.75f - (h - 1.0f) * 0.75f;
		}
	} else {
		r->f34 = 0.25f, r->f3c = 1.0f, r->f38 = 0.75f;
	}
}

void aos_afterimage_clear(aos_world *w, int32_t objId) /* FUN_004d7ff0 */
{
	(void)w;
	for (int i = 0; i < nafter; i++)
		if (after[i].objId == objId) {
			memmove(&after[i], &after[i + 1], sizeof after[0] * (size_t)(nafter - i - 1));
			nafter--;
			i--;
		}
}

static void afterimages_update(void) /* FUN_004d7b10 */
{
	for (int i = 0; i < nafter; i++) {
		after[i].a -= 0.0125f;
		if (!(0.0f < after[i].a)) {
			memmove(&after[i], &after[i + 1], sizeof after[0] * (size_t)(nafter - i - 1));
			nafter--;
			i--;
		}
	}
}

void aos_afterimages_draw(aos_world *w) /* FUN_004d7bc0 */
{
	(void)w;
	for (int i = 0; i < nafter; i++) {
		const afterimage *r = &after[i];
		aos_dcmd *c = aos_draw(r->cmd.tex, r->cmd.bank, r->pos[0], r->pos[1], 0);
		int16_t tex = c->tex;
		int8_t bank = c->bank;
		*c = r->cmd;
		c->tex = tex, c->bank = bank;
		c->x = r->pos[0], c->y = r->pos[1], c->z = 0;
		c->rotZ = (uint16_t)r->rot[2];
		c->flipX = ((r->rot[1] & 0xffff) == 0x8000);
		if (r->rot[0]) /* X-rotation: foreshortening approximation (PORTING.md) */
			c->sy *= aos_cos((float)(r->rot[0] & 0xffff) * (AOS_2PI / 65536.0f));
		c->blend = (uint8_t)r->blend;
		/* CModel colour (a, +0x34, +0x3c, +0x38) */
		c->rgba = aos_rgba(r->f34, r->f3c, r->f38, r->a);
	}
}

/* ------------------------------------------------------------------ */
/* per-tick (core.c, after aos_effects_update)                        */

void aos_fx_tick(aos_world *w)
{
	/* original order (FUN_006129b0): particles, sparks, afterimages, blur, (whips),
	 * cut-in, dark, speedline, message; none of the later lists touch the RNG or the
	 * whips, so running them here is equivalent */
	particles_update(w);
	sparks_update(w);
	afterimages_update();
	screen_update(w);
	popups_update(w);
}

/* ------------------------------------------------------------------ */
/* hyper flourish                                                      */

void aos_hyper_flourish_ex(aos_world *w, aos_player *p, int se, int32_t darkT, float darkL,
                           int32_t slFrames, float slAngle, float slSpread, int32_t blurT,
                           float blurA, int32_t blurBlend, int fa)
{
	aos_se(w, se);
	aos_screen_dark(w, darkT, darkL);
	if (slFrames > 0)
		aos_speedline(w, slFrames, &p->x, slAngle, slSpread);
	aos_screen_blur(w, blurT, blurA, blurBlend);
	if (fa)
		aos_spawn_effect(w, 0, 0xfa, 0, &p->x, 0.0f, p->objId, 0, p->side);
}

void aos_hyper_flourish(aos_world *w, aos_player *p) /* suguri.md §1.4, e.g. FUN_00757a50 t=0 */
{
	aos_hyper_flourish_ex(w, p, 6, 100, 0.75f, 100, p->faceAngle, 0.7853982f /* DAT_0081fa90 */,
	                      0x14, 0.5f, 2, 1);
}

int aos_camera_focus_count(const aos_world *w) /* entries in the focus list (0x895144) */
{
	return FXS((aos_world *)w)->nfocus;
}

void aos_camera_focus_clear(aos_world *w) /* FUN_004a9f10 on the focus vector 0x895144 */
{
	FXS(w)->nfocus = 0;
}
