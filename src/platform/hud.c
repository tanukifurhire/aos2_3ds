/* In-match HUD, round banners and screen transitions: a line-by-line port of
 * the PC battle scene's overlay draws (docs/re/hud.md). All layout math is the
 * original's, including its integer truncations, evaluated for the back
 * buffer size in hud_screen (the 3DS top screen = the PC game at 400x240).
 *
 * Not drawn (no font renderer yet, see hud.md §7): the BGM title text of the
 * round-start banner, Steam names, training/replay/netplay overlays. */
#include "hud.h"

#include <math.h>
#include <string.h>

#include "hud_gfx.h"

#define TWO_PI 6.2831855f /* DAT_00853b78 */

static const hud_screen *S_;

/* (int)(DAT_x * _DAT_009b1000): float constant times the HUD scale, truncated. */
static int SC(float c) { return (int)(c * S_->s); }
static int SC4(float c) { return (int)(c * S_->s4); }

static float fsin(float a) { return (float)sin((double)a); } /* FUN_0045a680 */
static float fcos(float a) { return (float)cos((double)a); } /* FUN_00467690 */

hud_screen hud_screen_for(int W, int H)
{
	hud_screen s;
	s.W = W;
	s.H = H;
	s.s4 = (float)W / 800.0f;
	s.s8 = (float)H / 600.0f;
	s.s = s.s8 <= s.s4 ? s.s8 : s.s4;
	return s;
}

/* FUN_005957d0: rect of half size (hw, hh) (HUD units) around (cx, cy). */
static void crect(int r[4], int cx, int cy, int hw, int hh)
{
	r[0] = cx - (int)((float)hw * S_->s);
	r[1] = cy - (int)((float)hh * S_->s);
	r[2] = (int)((float)hw * S_->s) + cx;
	r[3] = (int)((float)hh * S_->s) + cy;
}

static void setr(int r[4], int l, int t, int rr, int b)
{
	r[0] = l;
	r[1] = t;
	r[2] = rr;
	r[3] = b;
}

/* FUN_0045bf30: dest rect, inclusive source rect (u1 = right + 1). */
static void blit(int tex, const int d[4], int sl, int st, int sr, int sb, uint32_t argb, int blend)
{
	hg_image(tex, (float)d[0], (float)d[1], (float)d[2], (float)d[3], (float)sl, (float)st, (float)(sr + 1),
	         (float)(sb + 1), 0.0f, argb, blend);
}

/* CTexture +0x70 (FUN_0045d2e0): corners TL,TR,BR,BL; u/v = src left..right, top..bottom. */
static void quad(int tex, const int p[8], int sl, int st, int sr, int sb, uint32_t argb, int blend)
{
	float x[4] = {(float)p[0], (float)p[2], (float)p[4], (float)p[6]};
	float y[4] = {(float)p[1], (float)p[3], (float)p[5], (float)p[7]};
	hg_quad(tex, x, y, (float)sl, (float)st, (float)sr, (float)sb, argb, blend);
}

static int digit(int v, int n) /* FUN_005f1b50 */
{
	int d = 10;
	if (n != 1) {
		for (int i = 1; i < n - 1; i++)
			d *= 10;
		v /= d;
	}
	int r = v % 10;
	return r > 9 ? 0 : r;
}

/* Cosmetic RNG for the banner glitch slices (the PC draws them with the
 * cosmetic MT stream, FUN_00459f80; values only affect the look). */
static uint32_t hud_rng = 0x2545f491u;
static int rand_int(int lo, int hi)
{
	hud_rng = hud_rng * 1664525u + 1013904223u;
	return lo + (int)((hud_rng >> 8) % (uint32_t)(hi - lo + 1));
}

/* ------------------------------------------------------------------ */
/* FUN_006b29c0: weapon gauges A/B as half rings around each player (world space) */

typedef struct {
	const hud_camera *cam;
	float W2, H2;
} world_xf;

/* cos/sin of the 73 ring angles j / 72 * 2pi, computed once with the same
 * expressions (the rings used to call double sin/cos ~7000 times a frame,
 * several ms on an Old 3DS). Angles are passed as indices into these. */
static float ring_c[73], ring_s[73];
static bool ring_ready;

static void ring_tables(void)
{
	if (ring_ready)
		return;
	for (int j = 0; j <= 72; j++) {
		float a = (float)j / 72.0f * TWO_PI; /* DAT_008178ac */
		ring_c[j] = fcos(a);
		ring_s[j] = fsin(a);
	}
	ring_ready = true;
}

static void ring_tri(const world_xf *xf, float px, float py, int a0, float r0, uint32_t c0, int a1, float r1,
                     uint32_t c1, int a2, float r2, uint32_t c2)
{
	float wx[3] = {ring_c[a0] * r0 + px, ring_c[a1] * r1 + px, ring_c[a2] * r2 + px};
	float wy[3] = {ring_s[a0] * r0 + py, ring_s[a1] * r1 + py, ring_s[a2] * r2 + py};
	float x[3], y[3];
	for (int k = 0; k < 3; k++) {
		x[k] = xf->W2 + (wx[k] - xf->cam->camX) / xf->cam->zoomDiv;
		y[k] = xf->H2 - (wy[k] - xf->cam->camY) / xf->cam->zoomDiv;
	}
	uint32_t c[3] = {c0, c1, c2};
	hg_tri(x, y, c, 1);
}

static void weapon_rings(const aos_world *w, const hud_camera *cam)
{
	world_xf xf = {cam, (float)(S_->W / 2), (float)(S_->H / 2)};
	ring_tables();
	int fr = w->frame;
	float pulse = fsin((float)(fr % 100) / 100.0f * TWO_PI); /* DAT_00853b90, DAT_00853b78 */
	float f9 = -(pulse * 5.0f) + 10.0f;                       /* DAT_00813f40, DAT_00813f4c */
	uint32_t ia = (uint32_t)((int)(pulse * 32.0f) + 0x5c);   /* DAT_008140dc */
	const float R1 = 77.0f, R2 = 73.0f;                       /* DAT_0081ee98, DAT_00825374 */
	int odd = ((fr / 2) % 2) != 0;
	for (int i = 0; i < 2; i++) {
		const aos_player *p = &w->p[i];
		if (w->roundOver)
			continue;
		uint32_t eA, eB, fA, fB;
		if (i != 1) { /* version 0x5a2 >= 0x58b: P2 gets its own colours */
			eA = 0x4080ff;
			eB = 0xff8040;
			fA = odd ? 0x9080e1ff : 0x8080c0ff;
			fB = odd ? 0x90ffe180 : 0x80ffc080;
		} else {
			eA = 0xff4080;
			eB = 0x40ff80;
			fA = odd ? 0x90ffa0c0 : 0x80ff80c0;
			fB = odd ? 0x90a0ffc0 : 0x8080ffc0;
		}
		float px = p->x, py = p->y;
		int nA = (int)(37.0f * p->gaugeA), nB = (int)(37.0f * p->gaugeB); /* DAT_0081c6f8 */
		for (int half = 0; half < 2; half++) {
			uint32_t edge = half ? eB : eA, in = ia << 24 | (half ? eB : eA), fill = half ? fB : fA;
			int n = half ? nB : nA;
			for (int j = half * 36; j < half * 36 + 36; j++) {
				int a0 = j, a1 = j + 1; /* angles (float)j / 72 * 2pi: ring_c/ring_s */
				ring_tri(&xf, px, py, a0, R1 + f9, edge, a1, R1 + f9, edge, a1, R1, in);
				ring_tri(&xf, px, py, a0, R1 + f9, edge, a1, R1, in, a0, R1, in);
				ring_tri(&xf, px, py, a0, R2 - f9, edge, a1, R2 - f9, edge, a1, R2, in);
				ring_tri(&xf, px, py, a0, R2 - f9, edge, a1, R2, in, a0, R2, in);
				ring_tri(&xf, px, py, a0, R1, 0x80000000, a1, R1, 0x80000000, a1, R2, 0x80000000);
				ring_tri(&xf, px, py, a0, R1, 0x80000000, a1, R2, 0x80000000, a0, R2, 0x80000000);
				if (j - half * 36 < n) {
					ring_tri(&xf, px, py, a0, R1, fill, a1, R1, fill, a1, R2, fill);
					ring_tri(&xf, px, py, a0, R1, fill, a1, R2, fill, a0, R2, fill);
				}
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/* FUN_006b6830: hyper stocks as gradient arcs around the portrait */

/* cos/sin of the arc angles per side (index j = 0..50), same expressions as before. */
static float arc_c[2][51], arc_s[2][51];
static bool arc_ready;

static void arc_tables(void)
{
	if (arc_ready)
		return;
	for (int j = 0; j <= 50; j++) {
		float a1 = (float)j / 50.0f * 2.1f + -1.8f;          /* side 1: _DAT_00825340, _DAT_00825384 */
		float a2 = -1.3415928f - (float)j / 50.0f * 2.1f;    /* side 2: DAT_00825380 */
		arc_c[0][j] = fcos(a1);
		arc_s[0][j] = fsin(a1);
		arc_c[1][j] = fcos(a2);
		arc_s[1][j] = fsin(a2);
	}
	arc_ready = true;
}

static void hyper_arcs(const aos_player *p, float cx3, float cy3)
{
	arc_tables();
	int sd = p->side == 1 ? 0 : 1;
	float rin = 60.0f * S_->s, rout = 100.0f * S_->s; /* DAT_00813a58, DAT_00853b90 */
	float W2 = (float)(S_->W / 2), H2 = (float)(S_->H / 2);
	for (int k = 0; k < 3; k++) {
		int n = (int)((p->hyper - (float)k) * 50.0f); /* DAT_008140ec */
		if (n > 50)
			n = 50;
		for (int j = 0; j < n; j++) {
			const float c0 = arc_c[sd][j], s0 = arc_s[sd][j], c1 = arc_c[sd][j + 1], s1 = arc_s[sd][j + 1];
			float t0 = (float)j / 50.0f * 256.0f, t1 = (float)(j + 1) / 50.0f * 256.0f;
			float ix0 = c0 * rin + cx3, iy0 = s0 * rin + cy3;
			float ox0 = c0 * rout + cx3, oy0 = s0 * rout + cy3;
			float ix1 = c1 * rin + cx3, iy1 = s1 * rin + cy3;
			float ox1 = c1 * rout + cx3, oy1 = s1 * rout + cy3;
			float xa[3] = {W2 + ix0, W2 + ox0, W2 + ix1}, ya[3] = {H2 - iy0, H2 - oy0, H2 - iy1};
			float ua[3] = {0, 256, 256}, va[3] = {t0, t0, t1};
			hg_tri_tex(HT_HYPER_B + k, xa, ya, ua, va, 0xffffffff, 1);
			float xb[3] = {W2 + ix1, W2 + ox0, W2 + ox1}, yb[3] = {H2 - iy1, H2 - oy0, H2 - oy1};
			float ub[3] = {0, 256, 256}, vb[3] = {t1, t0, t1};
			hg_tri_tex(HT_HYPER_B + k, xb, yb, ub, vb, 0xffffffff, 1);
		}
	}
}

/* ------------------------------------------------------------------ */
/* FUN_006b5b50: combo damage / hit counter */

/* Body of FUN_006b5b50 for anchor (X, Y) and scale s (the bottom-screen panel
 * reuses it at its own anchor/scale; the top HUD passes the original values). */
static void combo_at(const aos_player *p, float X, float Y, float s)
{
	float fB = p->comboFadeB, fA = p->comboFadeA;
#define SC(c) ((int)((float)(c) * s))
	uint32_t a = (uint32_t)(int)(255.0f * fB) & 0xff;
	uint32_t col = a << 24 | a << 16 | a << 8 | a;
	int r[4], cx, cy, hw, hh;
	/* "HIT" */
	cx = (int)X - SC(35);
	cy = (int)Y + SC(45);
	hw = (int)((1.0f - fB) * 30.0f * s + 30.0f * s);
	hh = (int)(20.0f * s * fB);
	setr(r, cx - hw, cy - hh, cx + hw, cy + hh);
	blit(HT_DAMAGE, r, 0x348, 0xa0, 0x3bf, 0xef, col, 1);
	blit(HT_DAMAGE, r, 0x2d0, 0xa0, 0x347, 0xef, col, 1);
	/* "DAMAGE" */
	cx = (int)X + SC(40);
	cy = (int)Y + SC(40);
	hw = (int)((1.0f - fB) * 60.0f * s + 60.0f * s);
	hh = (int)(20.0f * s * fB);
	setr(r, cx - hw, cy - hh, cx + hw, cy + hh);
	blit(HT_DAMAGE, r, 0x2d0, 0x50, 0x3bf, 0x9f, col, 1);
	blit(HT_DAMAGE, r, 0x2d0, 0, 0x3bf, 0x4f, col, 1);
	float e = 1.0f - fA;
	for (int pass = 0; pass < 2; pass++) /* 5 damage digits: shadow set, then white set */
		for (int i = 0; i < 5; i++) {
			cx = ((int)X - SC(36) * i) + SC(72); /* DAT_008140f4, DAT_008178ac */
			cy = (int)Y;
			hw = (int)((e * 24.0f + 36.0f * fB) * s);
			hh = (int)((e * 24.0f + 36.0f + (1.0f - fB) * 36.0f) * s);
			int d = digit(p->comboShowDmg, i + 1);
			setr(r, cx - hw, cy - hh, cx + hw, cy + hh);
			int sy = (d / 5) * 0x90 + (pass ? 0 : 0x120);
			blit(HT_DAMAGE, r, (d % 5) * 0x90, sy, (d % 5) * 0x90 + 0x8f, sy + 0x8f, col, 1);
		}
	for (int pass = 0; pass < 2; pass++) /* 2 hit digits */
		for (int i = 0; i < 2; i++) {
			cx = ((int)X - SC(20) * i) - SC(60);
			cy = (int)Y + SC(40);
			hw = (int)((e * 10.0f + 25.0f * fB) * s);
			hh = (int)((e * 10.0f + 25.0f + (1.0f - fB) * 25.0f) * s);
			int d = digit(p->comboShowHits, i + 1);
			setr(r, cx - hw, cy - hh, cx + hw, cy + hh);
			int sy = pass ? 0x240 : 0x2a4;
			blit(HT_DAMAGE, r, d * 100, sy, d * 100 + 99, sy + 0x63, col, 1);
		}
#undef SC
	/* the tail (comboShowHits = 1 once faded out) is simulated: aos_flow_frame_end */
}

static void combo(const aos_player *p)
{
	float s = S_->s, fB = p->comboFadeB;
	float X;
	if (p->side == 1)
		X = 80.0f * s + 40.0f * s * fB; /* DAT_00814164, DAT_008140e0 */
	else
		X = ((float)S_->W - 80.0f * s) - 40.0f * s * fB;
	combo_at(p, X, 120.0f * s, s); /* DAT_00814174 */
}

/* ------------------------------------------------------------------ */
/* FUN_006bafd0: the in-match HUD */

static void timer_block(const aos_world *w, int cx, int cy)
{
	int r[4];
	crect(r, cx, cy, 0x39, 0x30);
	blit(HT_GAUGE, r, 0x212, 0x262, 0x33d, 0x38d, 0x80000000, 1);
	/* two sparkles orbiting the timer */
	float a = (float)(w->frame % 160) / 160.0f * TWO_PI; /* DAT_00814168 */
	int ox = (int)(fcos(a) * (16.0f * S_->s)), oy = (int)(fsin(a) * (32.0f * S_->s));
	crect(r, ox + cx, oy + cy, 10, 10);
	blit(HT_GAUGE, r, 0x2d0, 200, 0x317, 0x10f, 0x80ffffff, 2);
	a = (float)(-(w->frame % 120)) / 120.0f * TWO_PI; /* DAT_00814174 */
	ox = (int)(fcos(a) * (32.0f * S_->s));
	oy = (int)(fsin(a) * (16.0f * S_->s));
	crect(r, ox + cx, oy + cy, 10, 10);
	blit(HT_GAUGE, r, 0x2d0, 200, 0x317, 0x10f, 0x80ffffff, 2);
	/* Orange variants (0xffff8c00 / 0xff804500) need config DAT_00881f70 == 1 in
	 * versus; it is 0 by default and has no option here. */
	if (w->roundTimer == 999) {
		crect(r, cx, cy, 0x24, 0x12);
		blit(HT_GAUGE, r, 800, 0xa0, 0x3af, 0xe7, 0xffffffff, 1);
		return;
	}
	for (int i = 0; i < 3; i++) {
		int x = (SC(24.0f) + cx) - (int)((float)(i * 0x18) * S_->s); /* DAT_00853b8c */
		int d = digit(w->roundTimer, i + 1);
		crect(r, x, cy, 0x12, 0x12);
		uint32_t col = (d == 0 && w->roundTimer < 100 && i == 2) ? 0xff808080 : 0xffffffff;
		blit(HT_GAUGE, r, (d % 5) * 0x48 + 600, (d / 5) * 0x48, (d % 5) * 0x48 + 0x29f, (d / 5) * 0x48 + 0x47, col,
		     1);
	}
}

static void lead_crowns(const aos_world *w, int cx, int cy)
{
	float aspect = (float)S_->W / (float)S_->H; /* DAT_009b0fdc / fe0 */
	int dx, dy;
	if (1.6f <= aspect) { /* DAT_0082533c */
		dx = SC(64.0f);
		dy = SC(34.0f);
	} else {
		dx = SC(150.0f);
		dy = SC(64.0f);
	}
	int left[4], right[4];
	crect(left, cx - dx, cy + dy, 10, 10);
	crect(right, cx + dx, cy + dy, 10, 10);
	int h0 = w->p[0].hp, h1 = w->p[1].hp;
	blit(h0 > h1 ? HT_CROWN_W : HT_CROWN_L, left, 0, 0, 0x40, 0x40, 0xffffffff, 1);
	blit(h1 > h0 ? HT_CROWN_W : HT_CROWN_L, right, 0, 0, 0x40, 0x40, 0xffffffff, 1);
}

static void round_markers(const aos_world *w, int i)
{
	int rtw = w->roundsToWin, won = w->p[i].roundsWon;
	if (rtw <= 1)
		return;
	int step = SC(20.0f), y0 = S_->H / 2 - SC(285.0f); /* DAT_008140fc, DAT_0081eeb4 */
	int x0 = i == 0 ? (S_->W / 2 - SC(32.0f)) - step * rtw : S_->W / 2 + SC(32.0f) + step * rtw;
	int r[4];
	for (int k = 0; k < rtw; k++) {
		int x = i == 0 ? step * k + x0 : x0 - step * k;
		if (k < rtw - 1) {
			setr(r, x - SC(10.0f), y0 - SC(10.0f), x + SC(10.0f), SC(10.0f) + y0);
			if (k < won)
				blit(HT_GAUGE, r, 0x397, 0xff, 0x3be, 0x126, 0xffffffff, 1);
			else
				blit(HT_GAUGE, r, 0x3c3, 0xff, 0x3ea, 0x126, 0xffffffff, 1);
		} else {
			int c = i == 0 ? x + SC(3.0f) : x - SC(3.0f);
			setr(r, c - SC(14.0f), y0 - SC(14.0f), c + SC(14.0f), SC(14.0f) + y0);
			if (k < won)
				blit(HT_GAUGE, r, 0x325, 0xf1, 0x35c, 0x128, 0xffffffff, 1);
			else
				blit(HT_GAUGE, r, 0x35d, 0xf1, 0x394, 0x128, 0xffffffff, 1);
		}
	}
}

static void hp_bar(const aos_world *w, const aos_player *p, int i, int x0, int y0, int L)
{
	int d = i == 0 ? 1 : -1; /* P2 mirrors every x offset */
	int P[8];
	int s8 = SC(8.0f), s10 = SC(10.0f);
	/* frame: parallelogram slanted by 14 px (in W/800 units) */
	P[0] = x0 + d * (-SC4(2.0f) - SC4(30.0f));
	P[1] = (y0 - s8) - s8;
	P[2] = x0 + d * (-SC4(2.0f) + L + SC4(30.0f));
	P[3] = P[1];
	P[4] = x0 + d * (L + SC4(12.0f) + SC4(30.0f));
	P[5] = s8 + y0 + s8;
	P[6] = x0 + d * (SC4(12.0f) - SC4(30.0f));
	P[7] = P[5];
	quad(HT_GAUGE, P, 0, 200, 0x2cf, 0x117, 0xffffffff, 1);
	int seg = p->maxHp / 3;
	if (seg == 0)
		return;
	double wave = sin((double)(w->frame % 0x32) / 50.0 * 6.2831854820251465); /* FUN_007d8790 */
	uint32_t pulse = (uint32_t)((int)(wave * 50.0) + 0xcd);
	float gl = fsin((float)(w->frame % 0xb4) / 180.0f * TWO_PI); /* DAT_00815258 */
	uint32_t ga = (uint32_t)(int)((gl * 0.15f + 0.15f) * 255.0f);
	for (int k = 0; k < 3; k++) {
		float fr = (float)(p->redHp - seg * k) / (float)seg;
		int wr = (int)(fr * (float)L);
		if (wr < 0)
			continue;
		if (wr < L) { /* recoverable part (red) */
			float fc = (float)(p->recovHp - seg * k) / (float)seg;
			int wc = (int)(fc * (float)L);
			if (L <= wc)
				wc = L;
			int sw = (int)(fc * 600.0f); /* DAT_008149b8 */
			if (599 < sw)
				sw = 600;
			P[0] = x0;
			P[1] = y0 - s8;
			P[2] = x0 + d * wc;
			P[3] = P[1];
			P[4] = x0 + d * (wc + s10);
			P[5] = s8 + y0;
			P[6] = x0 + d * s10;
			P[7] = P[5];
			quad(HT_GAUGE, P, 0, 0xa0, sw, 199, 0xffffffff, 1);
		} else {
			wr = L;
		}
		int sw = (int)(fr * 600.0f);
		if (599 < sw)
			sw = 600;
		P[0] = x0;
		P[1] = y0 - s8;
		P[2] = x0 + d * wr;
		P[3] = P[1];
		P[4] = x0 + d * (wr + s10);
		P[5] = s8 + y0;
		P[6] = x0 + d * s10;
		P[7] = P[5];
		uint32_t col;
		if (L < (int)(fr * (float)L))
			col = 0xffe1e1e1; /* the segment above is not empty */
		else if (k == 0 && wr < L)
			col = (pulse & 0xff) << 8 | 0xffff0000 | (pulse & 0xff); /* last segment: red pulse */
		else
			col = 0xffffffff;
		quad(HT_GAUGE, P, 0, k * 0x28 + 0x28, sw, k * 0x28 + 0x4f, col, 1);
		if ((wr < L || k == 2) && k != 0)
			quad(HT_GAUGE, P, 0, 0, sw, 0x27, ga << 24 | 0xffffff, 2);
	}
}

static void hyper_orbs(const aos_world *w, const aos_player *p, int i, int x0, int y0)
{
	float hy = p->hyper;
	int lv = (int)(hy / 1.0f);
	if (lv == 0 && 0.5f <= hy)
		lv = -1;
	bool half2 = lv == 1 && 1.5f <= hy, half3 = lv == 2 && 2.5f <= hy;
	uint32_t g = (uint32_t)(int)((fsin((float)(w->frame % 200) / 200.0f * TWO_PI) * 0.15f + 0.85f) * 255.0f) & 0xff;
	uint32_t base = 0xff000000 | g << 16 | g << 8 | g;
	uint32_t ga = (uint32_t)(int)((fsin((float)(w->frame % 50) / 50.0f * TWO_PI) * 0.25f + 0.75f) * 255.0f);
	uint32_t glow = ga << 24 | 0xffffff;
	int r[4];
	for (int j = 1; j < 4; j++) {
		int x = i == 0 ? ((SC(90.0f) + x0 + (int)((float)(j << 5) * S_->s)) - SC(30.0f))
		               : ((x0 - SC(90.0f)) - SC(32.0f) * j) + SC(30.0f);
		crect(r, x, SC(30.0f) + y0, 0x18, 0x18);
		int bx = -1, by = 0;
		if (lv < j) {
			if (j == 1 && lv == -1) {
				bx = 0x180;
				by = 0x118;
			} else if (j == 2 && half2) {
				bx = 0x340;
				by = 0x288;
			} else if (j == 3 && half3) {
				bx = 0x3a0;
				by = 0x288;
			} else {
				blit(HT_GAUGE, r, 0, 0x118, 0x5f, 0x177, 0xffffffff, 1);
				continue;
			}
			/* half-filled orb: P2 uses the mirrored source rect */
			int l = i == 0 ? bx : bx + 0x5f, rr = i == 0 ? bx + 0x5f : bx;
			blit(HT_GAUGE, r, l, by, rr, by + 0x5f, base, 1);
			blit(HT_GAUGE, r, l, by + 0x60, rr, by + 0xbf, glow, 2);
		} else {
			blit(HT_GAUGE, r, lv * 0x60, 0x118, lv * 0x60 + 0x5f, 0x177, base, 1);
			blit(HT_GAUGE, r, lv * 0x60, 0x178, lv * 0x60 + 0x5f, 0x1d7, glow, 2);
		}
	}
}

static uint32_t heat_colour(int v) /* white up to 100 %, then towards red */
{
	uint32_t B = 0xff, G = 0xff;
	if (v >= 0x65) {
		int b = 0xff - (v * 2 - 200);
		B = (uint32_t)(b < 0 ? 0 : b);
		G = (uint32_t)(0xff - (v - 100));
	}
	return (G & 0xff) << 8 | 0xffff0000 | (B & 0xff);
}

static void heat(const aos_player *p, int i, int x0, int y0)
{
	int v = (int)(p->heat * 100.0f);
	uint32_t col = heat_colour(v);
	int r[4];
	crect(r, i == 0 ? SC(240.0f) + x0 : x0 - SC(230.0f), SC(30.0f) + y0, 0x40, 0xc); /* DAT_00815584, DAT_00824fa8 */
	blit(HT_GAUGE, r, 0x208, 0x398, 0x307, 0x3c7, col, 1);                         /* "HEAT  %" */
	for (int k = 0; k < 3; k++) {
		int x = i == 0 ? (int)((float)((0xe6 - k * 0xf) + 0x1e) * S_->s) + x0
		               : ((x0 - SC(240.0f)) - (int)((float)(k * 0xf) * S_->s)) + SC(30.0f);
		int d = digit(v, k + 1);
		crect(r, x, SC(30.0f) + y0, 8, 10);
		blit(HT_GAUGE, r, d * 0x20 + 0x208, 0x3c8, d * 0x20 + 0x227, 0x3ef, col, 1);
	}
}

static void player_block(const aos_world *w, int i)
{
	const aos_player *p = &w->p[i];
	int W = S_->W, H = S_->H;
	int L = W / 2 - SC(100.0f); /* local_940: HP bar length */
	int r[4];
	round_markers(w, i);
	/* translucent gradient strip under the bar */
	int xa = i == 0 ? SC(15.0f) + SC(90.0f) : (W - SC(15.0f)) - SC(90.0f), ya = SC(20.0f) + SC(45.0f);
	setr(r, xa, ya - SC(8.0f), i == 0 ? SC(240.0f) + xa : xa - SC(240.0f), SC(8.0f) + ya);
	blit(HT_GAUGE, r, 0x200, 0x200, 0x3df, 0x227, 0x80ffffff, 1);
	/* portrait disc, hyper arcs, portrait */
	int pcx = i == 0 ? SC(15.0f) : W - SC(15.0f), pcy = SC(20.0f), ph = SC(128.0f);
	if (i == 0)
		crect(r, pcx, pcy, 0x80, 0x80);
	else
		setr(r, ph + pcx, pcy - ph, pcx - ph, ph + pcy); /* mirrored */
	blit(HT_GAUGE, r, 0, 0x200, 0x1ff, 0x3ff, 0x80ffffff, 1);
	if (i == 0)
		hyper_arcs(p, (float)(SC(15.0f) - W / 2), (float)(H / 2 - SC(20.0f)));
	else
		hyper_arcs(p, (float)(W / 2 - SC(15.0f)), (float)(H / 2 - SC(20.0f)));
	blit(HT_PORTRAIT0 + i, r, 0, 0, 0x1ff, 0x1ff, 0xffffffff, 1);
	/* (Steam name text, FUN_005931d0 on DAT_00893e38: empty offline, not drawn) */
	int x0 = i == 0 ? SC(60.0f) : W - SC(60.0f), y0 = SC(36.0f);
	hp_bar(w, p, i, x0, y0, L);
	hyper_orbs(w, p, i, x0, y0);
	heat(p, i, x0, y0);
}

static void hud_main(const aos_world *w)
{
	int cx = S_->W / 2, cy = S_->H / 2 - SC(275.0f); /* DAT_0082537c */
	timer_block(w, cx, cy);
	lead_crowns(w, cx, cy);
	for (int i = 0; i < 2; i++)
		player_block(w, i);
	for (int i = 0; i < 2; i++)
		combo(&w->p[i]);
}

/* ------------------------------------------------------------------ */
/* Banner pieces shared by FUN_00617c90 and FUN_00608ee0 */

static void banner_ring(int t, float alpha, int frame)
{
	float b4, c8;
	if (t < 100) {
		b4 = (float)(100 - t) * (float)(100 - t) * 0.1f + 280.0f; /* DAT_00853b10, DAT_00814f14 */
		c8 = (float)(100 - t) * 2.0f + 5.0f;
	} else if (t < 0xc9) {
		c8 = 5.0f;
		b4 = 280.0f;
	} else {
		c8 = 5.0f;
		b4 = (float)(t - 200) * (float)(t - 200) * 2.0f + 280.0f;
	}
	float off = (float)(frame % 0x168) / 360.0f * TWO_PI; /* DAT_00817894 */
	uint32_t col = (uint32_t)(int)(128.0f * alpha) << 24 | 0xe1e1ff;
	float W2 = (float)(S_->W / 2), H2 = (float)(S_->H / 2), s = S_->s;
	uint32_t c[3] = {col, col, col};
	for (int k = 0; k < 0x78; k++) {
		float a0 = (float)k / 120.0f * TWO_PI + off, a1 = (float)(k + 1) / 120.0f * TWO_PI + off;
		float ro = b4 + c8, ri = b4;
		float x0o = (fcos(a0) * ro + 0.0f) * s, y0o = (fsin(a0) * ro + 0.0f) * s;
		float x1o = (fcos(a1) * ro + 0.0f) * s, y1o = (fsin(a1) * ro + 0.0f) * s;
		float x1i = (fcos(a1) * ri + 0.0f) * s, y1i = (fsin(a1) * ri + 0.0f) * s;
		float x0i = (fcos(a0) * ri + 0.0f) * s, y0i = (fsin(a0) * ri + 0.0f) * s;
		float xa[3] = {W2 + x0o, W2 + x1o, W2 + x1i}, ya[3] = {H2 - y0o, H2 - y1o, H2 - y1i};
		hg_tri(xa, ya, c, 2);
		float xb[3] = {W2 + x0o, W2 + x1i, W2 + x0i}, yb[3] = {H2 - y0o, H2 - y1i, H2 - y0i};
		hg_tri(xb, yb, c, 2);
	}
}

static void banner_frame(float alpha, uint32_t noiseAlpha, uint32_t lineAlpha, float f10, int frame)
{
	int W = S_->W, H = S_->H;
	uint32_t bar = (uint32_t)(int)(192.0f * alpha) << 24; /* DAT_0081415c */
	hg_rect(0, 0, (float)W, (float)(H / 2 - SC(220.0f)), bar, 1);
	hg_rect(0, (float)(H / 2 + SC(220.0f)), (float)W, (float)H, bar, 1);
	/* noise strip (noise0..7 by frame/4), texel-mapped (hud.md §4.3) */
	int tile = SC(256.0f), n = W / tile, tex = HT_NOISE0 + (frame / 4) % 8;
	float vc = 128.0f * S_->s;
	for (int i = 0; i < n + 1; i++) {
		float x = -(float)(W / 2) + (float)(i * tile);
		hg_image(tex, (float)(W / 2) + x, (float)(H / 2) - f10, (float)(W / 2) + x + (float)tile,
		         (float)(H / 2) + f10, 0, vc + f10, (float)tile, vc - f10, 0, noiseAlpha << 24 | 0x404040, 1);
	}
	hg_line(0, (float)H / 2.0f - f10, (float)W, (float)(H / 2) - f10, lineAlpha << 24 | 0xffffff, 1);
	hg_line(0, (float)H / 2.0f + f10, (float)W, (float)(H / 2) + f10, lineAlpha << 24 | 0xffffff, 1);
}

/* One animated title letter with its glitch slices once fully shown.
 * amp: letter half size in HUD units (32 for the 128 px cells of system.png,
 * 25 for the 100 px cells of system2.png); yc: centre line. pre: the start
 * banner truncates (1-f)*32 etc. to int before scaling, the end banner not. */
static void banner_letter(int tex, int x, int yc, int k, float f, int srcx, int srcy, int sw, int sh, int amp,
                          bool pre, bool glitch64)
{
	float s = S_->s;
	int off, hw, hh;
	if (pre) {
		off = (int)((float)(int)((1.0f - f) * (float)amp) * s);
		hw = (int)((float)(int)((float)amp * f) * s);
		hh = (int)((float)(int)((1.0f - f) * (float)(2 * amp) + (float)amp) * s);
	} else {
		off = (int)((1.0f - f) * (float)amp * s);
		hw = (int)((float)amp * f * s);
		hh = (int)(((1.0f - f) * (float)(2 * amp) + (float)amp) * s);
	}
	int y = (k % 2 == 0) ? yc + off : yc - off;
	int r[4];
	setr(r, x - hw, y - hh, x + hw, y + hh);
	blit(tex, r, srcx, srcy, srcx + sw, srcy + sh, 0xffffffff, 1);
	if (f < 1.0f)
		return;
	for (int g = 0; g < 4; g++) {
		int r1 = rand_int(-30, 30), r2 = rand_int(-16, 16);
		int gx, gy, gh;
		if (glitch64) { /* start banner: 64 px cells at y 0x140 of system.png (as the PC reads them) */
			gx = (k % 8) * 0x40 + 0x20 + r1 * 2;
			gy = (k / 8) * 0x40 + 0x140;
			gh = 0x3f;
		} else { /* end banner: always the "IGNITION" row, cell k */
			gx = (k % 8) * 0x80 + 0x40 + r1 * 2;
			gy = (k / 8) * 0x80 + 0x280;
			gh = 0x7f;
		}
		int hy = amp == 25 ? 0x19 : 0x20;
		setr(r, (int)((float)r1 * s) + x, y - (int)((float)(r2 + hy) * s), (int)((float)(r1 + 1) * s) + x,
		     (int)((float)(r2 + hy) * s) + y);
		blit(HT_SYSTEM, r, gx, gy, gx + 1, gy + gh, g % 2 ? 0x7fffffff : 0x7f000000, 1);
	}
}

static void start_banner(const aos_world *w) /* FUN_00617c90 */
{
	const aos_flow *f = &w->flow;
	if (!w->roundStartBanner)
		return;
	float s = S_->s;
	float f10 = ((1.0f - f->stAlpha) * 50.0f + 50.0f) * s;
	banner_ring(f->stT, f->stAlpha, w->frame);
	banner_frame(f->stAlpha, (uint32_t)(int)(255.0f * f->stAlpha * f->stNoise),
	             (uint32_t)(int)(255.0f * f->stAlpha), f10, w->frame);
	int ta = (int)(f->stText * 255.0f), W2 = S_->W / 2, H2 = S_->H / 2;
	uint32_t tc = (uint32_t)ta << 24 | 0xffffff;
	int r[4], hw, hh = (int)((float)(int)(f->stText * 32.0f) * s);
	if (f->stRound == 0) { /* "STAND BY" */
		hw = (int)((float)((int)((1.0f - f->stText) * 256.0f) + 0x100) * s);
		setr(r, W2 - hw, H2 - hh, W2 + hw, H2 + hh);
		blit(HT_SYSTEM, r, 0, 0x200, 0x3ff, 0x27f, tc, 1);
	} else if (f->stRound == 99) { /* "FINAL" "ROUND" */
		hw = (int)((float)((int)((1.0f - f->stText) * 122.0f) + 0x7a) * s);
		int c = W2 - SC(130.0f);
		setr(r, c - hw, H2 - hh, c + hw, H2 + hh);
		blit(HT_SYSTEM, r, 0, 3, 0x1e7, 0x82, tc, 1);
		hw = (int)((float)((int)((1.0f - f->stText) * 126.0f) + 0x7e) * s);
		c = SC(126.0f) + W2;
		setr(r, c - hw, H2 - hh, c + hw, H2 + hh);
		blit(HT_SYSTEM, r, 0, 0x7c, 0x1f7, 0xfb, tc, 1);
	} else { /* "ROUND" n */
		hw = (int)((float)((int)((1.0f - f->stText) * 126.0f) + 0x7e) * s);
		int c = W2 - SC(34.0f);
		setr(r, c - hw, H2 - hh, c + hw, H2 + hh);
		blit(HT_SYSTEM, r, 0, 0x7c, 0x1f7, 0xfb, tc, 1);
		hw = (int)((float)((int)((1.0f - f->stText) * 28.0f) + 0x1c) * s);
		c = SC(128.0f) + W2;
		setr(r, c - hw, H2 - hh, c + hw, H2 + hh);
		int n = f->stRound;
		blit(HT_SYSTEM, r, n * 0x70, 0x104, n * 0x70 + 0x6f, 0x183, tc, 1);
	}
	if (ta == 0xff)
		for (int k = 0; k < 5; k++) {
			int r1 = rand_int(-30, 30), r2 = rand_int(-64, 64);
			setr(r, W2 - (int)((float)(r2 + 0x100) * s), (int)((float)r1 * s) + H2, (int)((float)(r2 + 0x100) * s) + W2,
			     (int)((float)(r1 + 1) * s) + H2);
			blit(HT_SYSTEM, r, 0, r1 * 2 + 0x240, 0x3ff, r1 * 2 + 0x242, k % 2 ? 0x7fffffff : 0x7f000000, 1);
		}
	/* "IGNITION": 8 letters */
	for (int k = 0; k < 8; k++) {
		float a = f->stLetter[k];
		if (a == 0.0f)
			continue;
		int x = (W2 + SC(64.0f) * -3 + SC(64.0f) * k) - SC(32.0f);
		banner_letter(HT_SYSTEM, x, H2, k, a, (k % 8) * 0x80, (k / 8) * 0x80 + 0x280, 0x7f, 0x7f, 32, true, true);
	}
	/* BGM title (font text, right-aligned before the icon) not drawn; the icon is */
	uint32_t ic = (uint32_t)(int)(255.0f * f->stAlpha) << 24 | 0xffffff;
	crect(r, SC(112.0f) + W2 + SC(240.0f), H2 + SC(200.0f), 0x10, 0x10); /* DAT_0081eea0, DAT_008147cc */
	blit(HT_BGMNAME, r, 0, 0, 0x3f, 0x3f, ic, 1);
}

static void end_banner(const aos_world *w) /* FUN_00608ee0 */
{
	const aos_flow *f = &w->flow;
	if (!w->roundEndBanner)
		return;
	float s = S_->s;
	float f10 = ((1.0f - f->enAlpha) * 50.0f + 50.0f) * s;
	banner_ring(f->enT, f->enAlpha, w->frame);
	banner_frame(f->enAlpha, (uint32_t)(int)(128.0f * f->enAlpha), (uint32_t)(int)(192.0f * f->enAlpha), f10,
	             w->frame);
	int W2 = S_->W / 2, H2 = S_->H / 2;
	for (int set = 0; set < AOS_END_SETS; set++) {
		static const int count[AOS_END_SETS] = {7, 7, 11, 12};
		for (int k = 0; k < count[set]; k++) {
			float a = f->enLetter[set][k];
			if (a == 0.0f)
				continue;
			if (set < 2) { /* "FINISH!" / "TIMEOUT" */
				int c = k + (set == 0 ? 9 : 16);
				int x = (W2 - SC(192.0f)) + (int)((float)(k << 6) * s);
				banner_letter(HT_SYSTEM, x, H2, k, a, (c % 8) * 0x80, (c / 8) * 0x80 + 0x280, 0x7f, 0x7f, 32, false,
				              false);
			} else { /* "SURRENDERED" / "DISCONNECTED" (netplay) */
				int x = set == 2 ? (W2 - SC(250.0f)) + (int)((float)(k * 0x32) * s)
				                 : (W2 - SC(300.0f)) + SC(25.0f) + (int)((float)(k * 0x32) * s);
				int sy = (k / 10) * 100 + (set == 2 ? 0xe : 0xd2);
				int yc = set == 2 ? H2 + SC(2.0f) : H2; /* DAT_00813a4c */
				banner_letter(HT_SYSTEM2, x, yc, k, a, (k % 10) * 100, sy, 99, 0x63, 25, false, false);
			}
		}
	}
}

/* FUN_005622a0: screen transitions over everything */
static void transition(const aos_world *w)
{
	const aos_flow *f = &w->flow;
	if (!f->fadeOn)
		return;
	int t = f->fadeType;
	float W = (float)S_->W, H = (float)S_->H;
	if (t >= 1 && t <= 16) {
		int a = (int)(255.0f * f->fadeA);
		uint32_t col;
		if (t <= 4)
			col = (uint32_t)a << 24;
		else if (t <= 8)
			col = (uint32_t)(0xff - a) << 24;
		else if (t <= 12)
			col = (uint32_t)a << 24 | 0xffffff;
		else
			col = (uint32_t)(0xff - a) << 24 | 0xffffff;
		hg_rect(0, 0, W, H, col, 1);
		return;
	}
	if (t < 0x11 || t > 0x14)
		return;
	float s4 = S_->s4, s8 = S_->s8;
	for (int c = 0; c < 20; c++)
		for (int r = 0; r < 15; r++) {
			float a = f->tileA[r][c];
			uint32_t col = (uint32_t)(int)(255.0f * a) << 24;
			int hx = (int)(a * 20.0f * s4), hy = (int)(a * 20.0f * s8);
			int x = (int)(40.0f * s4) * c + (int)(20.0f * s4), y = (int)(40.0f * s8) * r + (int)(20.0f * s8);
			/* FUN_0045c7d0: rotated by (1 - a) rad about the rect centre */
			hg_image(HT_FADE, (float)(x - hx), (float)(y - hy), (float)(x + hx), (float)(y + hy), 0, 0, 0x21, 0x21,
			         1.0f - a, col, 1);
		}
}

void hud_draw(const aos_world *w, const hud_screen *sc, const hud_camera *cam)
{
	S_ = sc;
	weapon_rings(w, cam);
	if (aos_hud_visible(w))
		hud_main(w);
	start_banner(w);
	end_banner(w);
	transition(w);
}

/* ------------------------------------------------------------------ */
/* 3DS bottom-screen match panel (port addition, not in the PC game): the heat
 * readout and combo counter of both players, drawn larger with the same art
 * and the same show/fade rules. Presentation only. */

void hud_panel_draw(const aos_world *w, int W, int H)
{
	(void)H;
	int r[4];
	for (int i = 0; i < 2; i++) {
		const aos_player *p = &w->p[i];
		int colx = i * (W / 2), cw = W / 2;
		/* portrait (P2 mirrored as on the top screen) */
		if (i == 0)
			setr(r, colx + 4, 4, colx + 60, 60);
		else
			setr(r, colx + cw - 4, 4, colx + cw - 60, 60);
		blit(HT_GAUGE, r, 0, 0x200, 0x1ff, 0x3ff, 0x80ffffff, 1);
		blit(HT_PORTRAIT0 + i, r, 0, 0, 0x1ff, 0x1ff, 0xffffffff, 1);
		/* heat: the top HUD's label + digits at 0.75 of the PC size */
		int v = (int)(p->heat * 100.0f);
		uint32_t col = heat_colour(v);
		const float k = 0.75f;
		int hx = i == 0 ? colx + 108 : colx + cw - 108, hy = 34;
		setr(r, hx - (int)(64 * k), hy - (int)(12 * k), hx + (int)(64 * k), hy + (int)(12 * k));
		blit(HT_GAUGE, r, 0x208, 0x398, 0x307, 0x3c7, col, 1);
		for (int d = 0; d < 3; d++) {
			int x = hx + (int)((float)(20 - 15 * d) * k);
			int g = digit(v, d + 1);
			setr(r, x - (int)(8 * k), hy - (int)(10 * k), x + (int)(8 * k), hy + (int)(10 * k));
			blit(HT_GAUGE, r, g * 0x20 + 0x208, 0x3c8, g * 0x20 + 0x227, 0x3ef, col, 1);
		}
		/* combo counter at 0.62 (fits a 160 px column) */
		if (p->comboFadeB > 0.0f)
			combo_at(p, (float)(colx + cw / 2 + 4), 108.0f, 0.62f);
	}
}
