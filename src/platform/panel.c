#include "panel.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../app/prof.h"
#include "../app/selgfx.h"
#include "../app/text.h"
#include "../app/ui.h"
#include "../input/hyper_touch.h"
#include "sprbatch.h"

#define BW 320
#define BH 240

static bool cache_init(void);
static void cache_free(void);

static C3D_RenderTarget *bottom;
static C2D_TextBuf tbuf;
static C2D_Text txt[PANEL_LINES];
static char line[PANEL_LINES][64];
static u32 lcol[PANEL_LINES];
static bool dirty;  /* a debug line changed */
static bool pvalid; /* the cached panel image is current (panel_draw) */
/* What the last panel_draw showed (cache key + gauge fill). */
static uint32_t drawn_hash;
static bool drawn_ok;

/* While the panel owns the bottom framebuffer, stray printf output goes to a
 * console copy that writes into this buffer instead. */
static PrintConsole quiet;
static u16 quietFb[BW * BH];

void panel_begin(PrintConsole *con)
{
	quiet = *con;
	quiet.frameBuffer = quietFb;
	consoleSelect(&quiet);
	gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES);
	gfxSetDoubleBuffering(GFX_BOTTOM, true);
	if (!bottom)
		bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	if (!tbuf) {
		fontEnsureMapped();
		tbuf = C2D_TextBufNew(1024);
	}
	panel_clear_lines();
}

C3D_RenderTarget *panel_target(void) { return bottom; }

void panel_end(PrintConsole *con)
{
	consoleInit(GFX_BOTTOM, con); /* RGB565, single buffered, cleared, selected */
}

void panel_clear_lines(void)
{
	memset(line, 0, sizeof line);
	dirty = true;
	pvalid = false;
	drawn_ok = false;
}

void panel_line(int i, u32 color, const char *fmt, ...)
{
	if (i < 0 || i >= PANEL_LINES)
		return;
	char buf[64];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	if (strcmp(buf, line[i]) != 0 || lcol[i] != color) {
		strcpy(line[i], buf);
		lcol[i] = color;
		dirty = true;
	}
}

/* debug lines: profile builds, SELECT held */
static bool lines_shown(void)
{
#ifdef AOS_PROFILE
	return ((hidKeysHeld() | prof_script_keys()) & KEY_SELECT) != 0;
#else
	return false;
#endif
}

/* ================================================================ layout and art */

/* The button area (y 0..AREA_Y1) is cut by an X into four triangles meeting at (CX, CY): S+H top, H left,
 * B+H right, D+H bottom; the Accel Hyper is a hexagon on the crossing (user request 2026-11-10). */
#define CX 160.0f
#define CY 110.0f
#define AREA_Y1 220.0f
#define GAP 2.5f   /* each triangle is pushed this far outwards: thin dark seams along the diagonals */
#define HEX_R 70.0f /* flat-top hexagon: half width 70, half height 60.6 (was 48; ~1.46x, user request 2026-11-13) */
#define HS (HEX_R / 48.0f) /* hexagon contents scale with it */

static const unsigned slot_col[HT_NSLOT] = {
	[HT_UP] = 0x9a4cf0,     /* violet */
	[HT_LEFT] = 0x2b74f0,   /* blue */
	[HT_RIGHT] = 0x1fa84a,  /* green */
	[HT_DOWN] = 0xf08a1c,   /* orange */
	[HT_CENTER] = 0xe0243c, /* red, gold rim */
};

/* name area and cost-gem anchor per slot (inside the triangle, clear of the hexagon) */
static const struct {
	float x0, y0, x1, y1, gx, gy;
} area[HT_NSLOT] = {
	/* top/bottom: the strip between the screen edge and the hexagon (y 0..49 / 171..220), gems next to the
	 * hexagon; left/right: outside the hexagon's side corners (x 90 / 230) */
	[HT_UP] = {60, 3, 260, 36, 160, 42},
	[HT_LEFT] = {4, 66, 86, 146, 45, 156},
	[HT_RIGHT] = {234, 66, 316, 146, 275, 156},
	[HT_DOWN] = {60, 185, 260, 218, 160, 178},
	[HT_CENTER] = {CX - 38 * HS, CY - 17 * HS, CX + 38 * HS, CY + 19 * HS, CX, CY + 30 * HS},
};

#define GAUGE_X0 66.0f
#define GAUGE_X1 314.0f
#define GAUGE_Y0 226.0f
#define GAUGE_Y1 236.0f

/* triangle k: two outer corners + the crossing, pushed outwards by GAP */
static void tri_of(int k, float x[3], float y[3])
{
	static const float ox[4][2] = {{0, BW}, {0, 0}, {BW, BW}, {0, BW}}, oy[4][2] = {{0, 0}, {0, AREA_Y1}, {0, AREA_Y1}, {AREA_Y1, AREA_Y1}};
	static const float dx[4] = {0, -1, 1, 0}, dy[4] = {-1, 0, 0, 1};
	x[0] = ox[k][0] + dx[k] * GAP;
	y[0] = oy[k][0] + dy[k] * GAP;
	x[1] = ox[k][1] + dx[k] * GAP;
	y[1] = oy[k][1] + dy[k] * GAP;
	x[2] = CX + dx[k] * GAP * 1.6f;
	y[2] = CY + dy[k] * GAP * 1.6f;
}

void panel_slot_rect(int slot, float *x0, float *y0, float *x1, float *y1)
{
	if (slot == HT_CENTER) {
		*x0 = CX - HEX_R, *x1 = CX + HEX_R, *y0 = CY - HEX_R * 0.866f, *y1 = CY + HEX_R * 0.866f;
		return;
	}
	float x[3], y[3];
	tri_of(slot < 0 || slot >= HT_NSLOT ? HT_UP : slot, x, y);
	*x0 = fminf(x[0], fminf(x[1], x[2]));
	*x1 = fmaxf(x[0], fmaxf(x[1], x[2]));
	*y0 = fminf(y[0], fminf(y[1], y[2]));
	*y1 = fmaxf(y[0], fmaxf(y[1], y[2]));
}

unsigned panel_slot_color(int slot) { return slot_col[slot < 0 || slot >= HT_NSLOT ? HT_CENTER : slot]; }

static int pl_local, pl_chara = -1;
static bool pl_on;
static bgfx pl_art;
static kv_table *pl_names, *pl_cats;

/* label layout per slot, fitted once per match */
typedef struct {
	char text[160], alt[64];
	font_id font;
	float scale, lh;
	int lines;
} slot_label;
static slot_label lab[HT_NSLOT];

static unsigned mixc(unsigned a, unsigned b, float t)
{
	unsigned r = 0;
	for (int s = 0; s < 24; s += 8) {
		float x = (float)((a >> s) & 0xff) * (1 - t) + (float)((b >> s) & 0xff) * t;
		r |= (unsigned)(x + 0.5f) << s;
	}
	return r;
}
static unsigned argb(float a, unsigned rgb) { return ui_argb((int)(255 * a + 0.5f), rgb); }

static const char *move_name(const char *key)
{
	static char buf[2][96]; /* first line only: a file's last value is followed by "[EOF]" */
	static int n;
	const char *v = key && pl_names ? kv_get(pl_names, key) : NULL;
	char *b = buf[n++ & 1];
	snprintf(b, sizeof buf[0], "%s", v ? v : "");
	b[strcspn(b, "\n")] = 0;
	return b;
}

/* Wrap s into the box with the biggest of a few font sizes that fits (lines and width). */
static void fit_label(slot_label *l, const char *s, float w, float h)
{
	static const struct {
		font_id f;
		float sc;
	} tries[] = {{FONT_MENUBIG, 1.0f}, {FONT_MENUBIG, 0.9f}, {FONT_MENU, 0.9f}, {FONT_MENU, 0.8f},
	             {FONT_MENU, 0.72f}, {FONT_MENU, 0.64f}, {FONT_MENU, 0.58f}};
	for (unsigned i = 0; i < sizeof tries / sizeof tries[0]; i++) {
		l->font = tries[i].f;
		l->scale = tries[i].sc;
		l->lines = text_wrap(l->font, s, l->scale, w, l->text, sizeof l->text);
		l->lh = text_line_height(l->font, l->scale) * 0.88f;
		if (l->lh * (float)l->lines <= h && text_width(l->font, l->text, l->scale) <= w)
			return;
	}
}

/* the area a slot's name may use (the alt line takes the bottom of it) */
static void label_area(int k, float *x0, float *y0, float *x1, float *y1)
{
	*x0 = area[k].x0;
	*x1 = area[k].x1;
	*y0 = area[k].y0;
	*y1 = area[k].y1;
	if (lab[k].alt[0])
		*y1 -= 11;
}

/* the labels sit at screen positions: fitted per match / after a layout edit */
static void fit_labels(void)
{
	memset(lab, 0, sizeof lab);
	for (int k = 0; k < HT_NSLOT; k++) {
		const ht_move *m = ht_slot(pl_chara, ht_pos_slot(pl_chara, k));
		if (!m->key)
			continue;
		if (m->alt)
			snprintf(lab[k].alt, sizeof lab[k].alt, "/ %s", move_name(m->alt));
		float x0, y0, x1, y1;
		label_area(k, &x0, &y0, &x1, &y1);
		fit_label(&lab[k], move_name(m->key), x1 - x0, y1 - y0);
	}
}

static void load_char(int chara, int color)
{
	bgfx_free(&pl_art);
	kv_free(pl_names);
	kv_free(pl_cats);
	pl_chara = chara;
	char folder[32], name[64], path[96];
	sel_palette_folder(folder, sizeof folder, pl_chara, color);
	snprintf(name, sizeof name, "reschr_%s", folder);
	bgfx_load(&pl_art, name, 0, 0, 0.25f); /* 2048² full-body art at 1/4 (seltex.go), 256 KB */
	snprintf(path, sizeof path, "romfs:/script/%s/commandlist_names.txt", lang_dir(strings_language()));
	pl_names = kv_load(path);
	snprintf(path, sizeof path, "romfs:/script/%s/commandlist_categories.txt", lang_dir(strings_language()));
	pl_cats = kv_load(path);
}

void panel_match_begin(const aos_world *w, int local)
{
	pl_local = local & 1;
	load_char(w->p[pl_local].id, w->p[pl_local].color);
	text_width(FONT_SYSTEM, "0", 1.0f); /* load the fonts now, not in the first match frame */
	fit_labels();
	cache_free();
	cache_init();
	pl_on = true;
	panel_touch_reset();
	pvalid = false;
	drawn_ok = false;
}

void panel_match_end(void)
{
	pl_on = false;
	pl_chara = -1;
	bgfx_free(&pl_art);
	kv_free(pl_names);
	kv_free(pl_cats);
	pl_names = pl_cats = NULL;
	cache_free();
	pvalid = false;
	drawn_ok = false;
}

/* ================================================================ touch */

static int8_t touch_slot = -1;
static bool touch_down;

void panel_touch_reset(void)
{
	touch_slot = -1;
	touch_down = true; /* a finger already on the screen must lift first */
}

/* the hexagon wins inside its shape, else the diagonals of the X decide (scaled so they run at 45 degrees) */
static int hit(int x, int y)
{
	float dx = (float)x - CX, dy = (float)y - CY;
	if (fabsf(dy) <= HEX_R * 0.866f && fabsf(dx) <= HEX_R - fabsf(dy) * 0.57735f) /* follows HEX_R */
		return HT_CENTER;
	if ((float)y >= AREA_Y1)
		return -1; /* the gauge strip */
	float sy = dy * (CX / CY);
	if (fabsf(sy) >= fabsf(dx))
		return dy < 0 ? HT_UP : HT_DOWN;
	return dx < 0 ? HT_LEFT : HT_RIGHT;
}

int panel_hit_pos(int x, int y) { return hit(x, y); }

int panel_touch(const aos_world *w)
{
	int x = 0, y = 0;
	bool down = false;
	if (hidKeysHeld() & KEY_TOUCH) {
		touchPosition tp;
		hidTouchRead(&tp);
		x = tp.px;
		y = tp.py;
		down = true;
	}
	if (prof_script_touch(&x, &y))
		down = true;
	if (!down) {
		touch_down = false;
		touch_slot = -1;
		return -1;
	}
	if (!touch_down) { /* touch-down frame: latch the button if its hyper can start */
		touch_down = true;
		int k = pl_on ? hit(x, y) : -1;
		touch_slot = (int8_t)(k >= 0 && ht_ready(w, pl_local, ht_pos_slot(pl_chara, k)) ? k : -1);
	}
	return touch_slot < 0 ? -1 : ht_pos_slot(pl_chara, touch_slot); /* touch_slot: screen position */
}

/* ================================================================ drawing helpers */

void panel_chamfer(float x0, float y0, float x1, float y1, float c, unsigned top, unsigned bottom)
{
	const float px[8] = {x0 + c, x1 - c, x1, x1, x1 - c, x0 + c, x0, x0};
	const float py[8] = {y0, y0, y0 + c, y1 - c, y1, y1, y1 - c, y0 + c};
	u32 col[8];
	for (int i = 0; i < 8; i++) {
		float t = (py[i] - y0) / (y1 - y0);
		unsigned a = (unsigned)((float)(top >> 24) * (1 - t) + (float)(bottom >> 24) * t + 0.5f);
		col[i] = ui_c2d(a << 24 | mixc(top, bottom, t));
	}
	float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
	unsigned ca = ((top >> 24) + (bottom >> 24)) / 2;
	u32 cc = ui_c2d(ca << 24 | mixc(top, bottom, 0.5f));
	for (int i = 0; i < 8; i++) {
		int j = (i + 1) & 7;
		C2D_DrawTriangle(cx, cy, cc, px[i], py[i], col[i], px[j], py[j], col[j], UI_DEPTH);
	}
}

static void gem(float cx, float cy, float r, bool lit)
{
	u32 rim = ui_c2d(lit ? 0xfffff2b0 : 0xff2a2a3a), in = ui_c2d(lit ? 0xffffc21a : 0xff4a4a5e),
	    hi = ui_c2d(lit ? 0xffffffff : 0xff6a6a80);
	C2D_DrawTriangle(cx, cy - r - 1.5f, rim, cx + r + 1.5f, cy, rim, cx, cy + r + 1.5f, rim, UI_DEPTH);
	C2D_DrawTriangle(cx, cy - r - 1.5f, rim, cx - r - 1.5f, cy, rim, cx, cy + r + 1.5f, rim, UI_DEPTH);
	C2D_DrawTriangle(cx, cy - r, hi, cx + r, cy, in, cx, cy + r, in, UI_DEPTH);
	C2D_DrawTriangle(cx, cy - r, hi, cx - r, cy, in, cx, cy + r, in, UI_DEPTH);
}

enum { S_EMPTY, S_READY, S_POOR, S_LOCKED };

static int slot_state(const aos_world *w, int k)
{
	const aos_player *p = &w->p[pl_local];
	const int s = ht_pos_slot(p->id, k); /* k: screen position */
	const ht_move *m = ht_slot(p->id, s);
	if (!m->key)
		return S_EMPTY;
	if (p->hyper < m->cost)
		return S_POOR;
	return ht_ready(w, pl_local, s) ? S_READY : S_LOCKED;
}

static void text_shadowed(font_id f, const char *s, float x, float y, int align, unsigned rgb, float a, float sc,
                          float maxw)
{
	text_draw(f, s, x + 1, y + 1.5f, align, ui_c2d(argb(a * 0.7f, 0x000000)), sc, maxw);
	text_draw(f, s, x, y, align, ui_c2d(argb(a, rgb)), sc, maxw);
}

static void hexagon(float cx, float cy, float r, unsigned top, unsigned bottom)
{
	float px[6], py[6];
	u32 col[6];
	for (int i = 0; i < 6; i++) {
		float a = (float)i * 1.0471976f;
		px[i] = cx + r * cosf(a);
		py[i] = cy + r * sinf(a);
		float t = (py[i] - (cy - r)) / (2 * r);
		unsigned al = (unsigned)((float)(top >> 24) * (1 - t) + (float)(bottom >> 24) * t + 0.5f);
		col[i] = ui_c2d(al << 24 | mixc(top, bottom, t));
	}
	unsigned ca = ((top >> 24) + (bottom >> 24)) / 2;
	u32 cc = ui_c2d(ca << 24 | mixc(top, bottom, 0.5f));
	for (int i = 0; i < 6; i++) {
		int j = (i + 1) % 6;
		C2D_DrawTriangle(cx, cy, cc, px[i], py[i], col[i], px[j], py[j], col[j], UI_DEPTH);
	}
}

static void seam(float x0, float y0, float x1, float y1, float w, u32 c) /* line as a thin quad */
{
	float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
	float nx = -dy / l * w / 2, ny = dx / l * w / 2;
	C2D_DrawTriangle(x0 + nx, y0 + ny, c, x1 + nx, y1 + ny, c, x0 - nx, y0 - ny, c, UI_DEPTH);
	C2D_DrawTriangle(x1 + nx, y1 + ny, c, x1 - nx, y1 - ny, c, x0 - nx, y0 - ny, c, UI_DEPTH);
}

static void draw_label(int k, int state, float dy)
{
	bool live = state == S_READY;
	const slot_label *l = &lab[k];
	float lx0, ly0, lx1, ly1;
	label_area(k, &lx0, &ly0, &lx1, &ly1);
	ly0 += dy;
	ly1 += dy;
	float th = l->lh * (float)l->lines;
	float ty = (ly0 + ly1) / 2 - th / 2 + l->lh / 2;
	text_shadowed(l->font, l->text, (lx0 + lx1) / 2, ty, TEXT_CENTER, live ? 0xffffff : 0xc4c8d4, live ? 1.0f : 0.85f,
	              l->scale, lx1 - lx0);
	if (l->alt[0])
		text_draw(FONT_SYSTEM, l->alt, (lx0 + lx1) / 2, ly1 + 6, TEXT_CENTER,
		          ui_c2d(argb(live ? 0.9f : 0.6f, 0xffffff)), 0.68f, lx1 - lx0);
	/* cost gems: lit while the gauge pays for it */
	int n = (int)(ht_slot(pl_chara, ht_pos_slot(pl_chara, k))->cost + 0.5f);
	float gx = area[k].gx - (float)(n - 1) * 7, gy = area[k].gy + dy;
	for (int i = 0; i < n; i++)
		gem(gx + 14 * (float)i, gy, 4.5f, state != S_POOR);
}

static unsigned state_base(int k, int state)
{
	unsigned base = slot_col[ht_pos_slot(pl_chara, k)]; /* colour follows the hyper, not the position */
	if (state == S_POOR)
		return mixc(base, 0x50566a, 0.72f); /* greyed: the gauge does not pay for it */
	if (state == S_LOCKED)
		return mixc(base, 0x182038, 0.5f); /* darkened: paid for, but no hyper now (hit, an Accel running) */
	return base;
}

/* One quadrant: translucent tinted triangle (the art shows through), light toward the screen edge and darker
 * toward the crossing, gloss along the outer edge, bright seams along the diagonals. */
static void draw_quadrant(int k, int state, bool pressed)
{
	float x[3], y[3];
	tri_of(k, x, y);
	if (state == S_EMPTY) { /* no such hyper: a dim, quiet quadrant, the art stays visible */
		u32 e = ui_c2d(0x40080c1c), c = ui_c2d(0x70080c1c);
		C2D_DrawTriangle(x[0], y[0], e, x[1], y[1], e, x[2], y[2], c, UI_DEPTH);
		u32 s = ui_c2d(0x30ffffff);
		seam(x[0], y[0], x[2], y[2], 1, s);
		seam(x[1], y[1], x[2], y[2], 1, s);
		text_draw(FONT_MENUBIG, "-", (area[k].x0 + area[k].x1) / 2, (area[k].y0 + area[k].y1) / 2, TEXT_CENTER,
		          ui_c2d(0x50ffffff), 1.0f, 0);
		return;
	}
	bool live = state == S_READY || pressed; /* pressed: the hyper just started, show the touch, not the lock */
	unsigned base = pressed ? slot_col[ht_pos_slot(pl_chara, k)] : state_base(k, state);
	float a = pressed ? 0.92f : live ? 0.74f : 0.62f;
	unsigned outer = mixc(base, 0xffffff, pressed ? 0.45f : 0.22f), inner = mixc(base, 0x000000, pressed ? 0.25f : 0.5f);
	u32 co = ui_c2d(argb(a, outer)), ci = ui_c2d(argb(a + 0.12f > 1 ? 1 : a + 0.12f, inner));
	C2D_DrawTriangle(x[0], y[0], co, x[1], y[1], co, x[2], y[2], ci, UI_DEPTH);
	/* gloss band along the outer edge */
	float t = 0.30f;
	float x3 = x[1] + (x[2] - x[1]) * t, y3 = y[1] + (y[2] - y[1]) * t, x4 = x[0] + (x[2] - x[0]) * t,
	      y4 = y[0] + (y[2] - y[0]) * t;
	u32 g0 = ui_c2d(argb(pressed ? 0.06f : live ? 0.22f : 0.10f, 0xffffff)), g1 = ui_c2d(argb(0, 0xffffff));
	C2D_DrawTriangle(x[0], y[0], g0, x[1], y[1], g0, x3, y3, g1, UI_DEPTH);
	C2D_DrawTriangle(x[0], y[0], g0, x3, y3, g1, x4, y4, g1, UI_DEPTH);
	/* rims along the diagonals (the seams) */
	unsigned rim = pressed ? 0xfff4a0 : mixc(base, 0xffffff, live ? 0.65f : 0.35f);
	u32 rc = ui_c2d(argb(live || pressed ? 0.95f : 0.6f, rim));
	float w = pressed ? 3 : 2;
	seam(x[0], y[0], x[2], y[2], w, rc);
	seam(x[1], y[1], x[2], y[2], w, rc);
	draw_label(k, state, pressed ? 2 : 0);
}

/* The Accel Hyper: a hexagon with a gold rim on the crossing. */
static void draw_center(int state, bool pressed)
{
	const int k = HT_CENTER;
	if (state == S_EMPTY) { /* no Accel Hyper (Sumika's second form): a dark plug, no label */
		hexagon(CX, CY, HEX_R, 0xc0303848, 0xc0181c28);
		hexagon(CX, CY, HEX_R - 3, 0xd0101420, 0xd0080a12);
		text_draw(FONT_MENUBIG, "-", CX, CY, TEXT_CENTER, ui_c2d(0x50ffffff), 1.0f, 0);
		return;
	}
	bool live = state == S_READY || pressed;
	unsigned base = pressed ? mixc(slot_col[k], 0xffffff, 0.15f) : state_base(k, state);
	float dy = pressed ? 2 : 0;
	hexagon(CX + 1, CY + 4, HEX_R + 1, 0x90000000, 0x60000000); /* shadow */
	unsigned rt = pressed ? 0xffffe0 : 0xf6dc6a, rb = pressed ? 0xfff070 : 0xa86a10;
	if (!live && !pressed)
		rt = mixc(rt, 0x606878, 0.5f), rb = mixc(rb, 0x303440, 0.5f);
	hexagon(CX, CY + dy, HEX_R, 0xff000000 | rt, 0xff000000 | rb);
	hexagon(CX, CY + dy, HEX_R - 3.5f, 0xff000000 | mixc(base, 0xffffff, pressed ? 0.05f : 0.28f),
	        0xff000000 | mixc(base, 0x000000, pressed ? 0.6f : 0.45f));
	/* gloss: upper half */
	{
		float r = HEX_R - 4.5f, h = r * 0.866f;
		u32 g0 = ui_c2d(argb(pressed ? 0.10f : live ? 0.28f : 0.14f, 0xffffff)), g1 = ui_c2d(argb(0.03f, 0xffffff));
		float cy = CY + dy;
		C2D_DrawTriangle(CX - r / 2, cy - h, g0, CX + r / 2, cy - h, g0, CX + r, cy, g1, UI_DEPTH);
		C2D_DrawTriangle(CX - r / 2, cy - h, g0, CX + r, cy, g1, CX - r, cy, g1, UI_DEPTH);
	}
	const char *cv = pl_cats ? kv_get(pl_cats, "COMMANDLIST_CATEGORY_ACCELHYPER") : NULL;
	char cat[48];
	snprintf(cat, sizeof cat, "%s", cv ? cv : "Accel Hyper");
	cat[strcspn(cat, "\n")] = 0; /* the file's last key: "[EOF]" follows */
	text_draw(FONT_MENU, cat, CX, CY - 29 * HS + dy, TEXT_CENTER, ui_c2d(argb(live ? 1.0f : 0.6f, 0xffe27a)), 0.62f, 62 * HS);
	draw_label(k, state, dy);
}

/* Background: the menus' bottom gradient + the player's full-body art. */
static void backdrop(float a, float art)
{
	ui_rect_grad(0, 0, BW, BH, argb(a, 0x1a2c5a), argb(a, 0x1a2c5a), argb(a, 0x05070f), argb(a, 0x05070f));
	if (pl_art.t.ok)
		bgfx_draw(&pl_art, 0, 0, 0x7ff, 0x7ff, 28, -14, 292, 250, argb(art, 0xffffff), 1, false);
	/* soft vignette at the bottom so the gauge reads */
	ui_rect_grad(0, 196, BW, BH, argb(0, 0x05070f), argb(0, 0x05070f), argb(0.85f * a, 0x05070f),
	             argb(0.85f * a, 0x05070f));
}

/* the move list draws it fainter: the cards must read over it */
void panel_backdrop(float a) { backdrop(a, 0.30f * a); }

/* ================================================================ cache + per-frame draw */

/* The panel's static layer (background, art, buttons, gauge frame) changes only when a button's state, the
 * pressed button, the gauge level or a shown debug line does: it is rendered into an off-screen texture then
 * and drawn as one quad every frame, with the gauge fill on top (a few solid quads). */
#define TW 512
#define TH 256
static C3D_Tex ptex;
static C3D_RenderTarget *ptgt;
static bool pfail;

/* The cache is a render target, and citro3d renders only into VRAM: 256 KB of VRAM, taken at match start after
 * the sprite atlases were placed (render_load_match) and given back at match end. Without it (VRAM full) the
 * panel is drawn in full every frame it changes. */
static bool cache_init(void)
{
	if (ptgt)
		return true;
	if (pfail || !spr_init())
		return false;
	pfail = true;
	if (!C3D_TexInitVRAM(&ptex, TW, TH, GPU_RGB565))
		return false;
	C3D_TexSetFilter(&ptex, GPU_NEAREST, GPU_NEAREST);
	C3D_TexSetWrap(&ptex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	/* no depth buffer (citro3d then turns depth reads/writes off for this target): the static layer is drawn
	 * back to front anyway */
	ptgt = C3D_RenderTargetCreateFromTex(&ptex, GPU_TEXFACE_2D, 0, -1);
	if (!ptgt) {
		C3D_TexDelete(&ptex);
		return false;
	}
	pfail = false;
	return true;
}

static void cache_free(void)
{
	if (ptgt) {
		C3D_FrameSync(); /* the GPU may still read the texture */
		C3D_RenderTargetDelete(ptgt);
		C3D_TexDelete(&ptex);
		ptgt = NULL;
	}
	pfail = false;
}

typedef struct {
	int8_t state[HT_NSLOT];
	int8_t pressed, level, chara, lines;
} panel_key;

static void key_of(const aos_world *w, panel_key *k)
{
	memset(k, 0, sizeof *k);
	k->chara = (int8_t)pl_chara;
	for (int i = 0; i < HT_NSLOT; i++)
		k->state[i] = (int8_t)(pl_on ? slot_state(w, i) : S_EMPTY);
	k->pressed = touch_slot;
	float h = pl_on ? w->p[pl_local].hyper : 0;
	k->level = (int8_t)(h < 0 ? 0 : h > 3 ? 3 : (int)h);
	k->lines = (int8_t)lines_shown();
}

static int gauge_fill_px(const aos_world *w)
{
	float h = pl_on ? w->p[pl_local].hyper : 0;
	h = h < 0 ? 0 : h > 3 ? 3 : h;
	return (int)(h * 256.0f); /* sub-cell resolution well below a pixel */
}

static uint32_t content_hash(const aos_world *w)
{
	panel_key k;
	key_of(w, &k);
	uint32_t h = 2166136261u;
	const uint8_t *b = (const uint8_t *)&k;
	for (size_t i = 0; i < sizeof k; i++)
		h = (h ^ b[i]) * 16777619u;
	if (k.lines) {
		b = (const uint8_t *)line;
		for (size_t i = 0; i < sizeof line; i++)
			h = (h ^ b[i]) * 16777619u;
	}
	int f = gauge_fill_px(w) >> 3; /* ~1/3 px steps of the 80 px cells */
	return (h ^ (uint32_t)f) * 16777619u;
}

static void draw_gauge_frame(int level)
{
	panel_chamfer(4, GAUGE_Y0 - 5, BW - 4, GAUGE_Y1 + 3, 5, 0xc0101830, 0xe0060a18);
	char lbl[48];
	snprintf(lbl, sizeof lbl, "%s", tr("MENU_CONFIG_KEY_HYPER"));
	text_draw_shadowed(FONT_SYSTEM, lbl, 9, (GAUGE_Y0 + GAUGE_Y1) / 2, TEXT_LEFT, ui_c2d(0xffd8e4ff), 0.78f, 40);
	char d[12];
	snprintf(d, sizeof d, "%d", level);
	text_draw(FONT_MENU, d, GAUGE_X0 - 5, (GAUGE_Y0 + GAUGE_Y1) / 2 - 1, TEXT_RIGHT,
	          ui_c2d(level >= 3 ? 0xffffd040 : level ? 0xff8ff0ff : 0xff8090a8), 0.75f, 0);
	float cw = (GAUGE_X1 - GAUGE_X0 - 8) / 3;
	for (int i = 0; i < 3; i++) {
		float cx0 = GAUGE_X0 + (cw + 4) * (float)i;
		ui_rect(cx0 - 1, GAUGE_Y0 - 1, cx0 + cw + 1, GAUGE_Y1 + 1, 0xff4a5a80);
		ui_rect(cx0, GAUGE_Y0, cx0 + cw, GAUGE_Y1, 0xff0a1024);
	}
}

static void draw_gauge_fill(const aos_world *w) /* citro2d, after the batch pass */
{
	float h = (float)gauge_fill_px(w) / 256.0f;
	float cw = (GAUGE_X1 - GAUGE_X0 - 8) / 3;
	bool full = h >= 3.0f;
	for (int i = 0; i < 3; i++) {
		float f = h - (float)i;
		if (f <= 0)
			break;
		bool whole = f >= 1;
		if (f > 1)
			f = 1;
		float cx0 = GAUGE_X0 + (cw + 4) * (float)i, cx1 = cx0 + cw * f;
		unsigned top = full ? 0xfffff07a : whole ? 0xff7ff2ff : 0xff3a8ab0, bot = full ? 0xffd08a10 : whole ? 0xff1a90d0
		                                                                                                     : 0xff1a5070;
		ui_rect_grad(cx0, GAUGE_Y0, cx1, GAUGE_Y1, top, top, bot, bot);
		ui_rect(cx0, GAUGE_Y0, cx1, GAUGE_Y0 + 1, 0xb0ffffff);
	}
}

static void draw_static(const aos_world *w)
{
	panel_key k;
	key_of(w, &k);
	ui_blend_reset();
	backdrop(1.0f, 0.85f); /* the character stands clearly behind the translucent quadrants */
	if (pl_on) {
		for (int i = 0; i < HT_NSLOT; i++)
			if (i != HT_CENTER)
				draw_quadrant(i, k.state[i], k.pressed == i);
		draw_center(k.state[HT_CENTER], k.pressed == HT_CENTER);
	}
	draw_gauge_frame(k.level);
	if (k.lines && tbuf) {
		ui_rect(0, 150, BW, BH, 0xc0000000);
		for (int i = 0; i < PANEL_LINES; i++)
			if (line[i][0])
				C2D_DrawText(&txt[i], C2D_WithColor, 4, 152 + i * 14, 0.9f, 0.42f, 0.42f, lcol[i]);
	}
}

/* panel_same's hash for this frame's panel_draw (the scene asks first, then draws) */
static uint32_t asked_hash;
static bool asked;
static uint32_t cached_key; /* key hash of the cached static layer */

bool panel_same(const aos_world *w)
{
	if (!bottom || !drawn_ok || !pvalid || (dirty && lines_shown()))
		return false;
	asked_hash = content_hash(w);
	asked = true;
	return asked_hash == drawn_hash;
}

void panel_draw(const aos_world *w)
{
	if (!bottom)
		return;
	drawn_hash = asked ? asked_hash : content_hash(w);
	asked = false;
	drawn_ok = true;
	bool text = false;
	if (dirty && tbuf && lines_shown()) {
		dirty = false;
		text = true;
		C2D_TextBufClear(tbuf);
		for (int i = 0; i < PANEL_LINES; i++) {
			C2D_TextParse(&txt[i], tbuf, line[i]);
			C2D_TextOptimize(&txt[i]);
		}
	}
	panel_key k;
	key_of(w, &k);
	uint32_t kh = 2166136261u;
	const uint8_t *b = (const uint8_t *)&k;
	for (size_t i = 0; i < sizeof k; i++)
		kh = (kh ^ b[i]) * 16777619u;
	const u32 bg = C2D_Color32(10, 14, 34, 255);
	bool cached = ptgt != NULL; /* made at match start (panel_match_begin) */
	if (cached && (!pvalid || text || kh != cached_key)) {
		pvalid = true;
		cached_key = kh;
		C2D_TargetClear(ptgt, bg);
		C2D_SceneBegin(ptgt);
		draw_static(w);
	}
	C2D_TargetClear(bottom, bg);
	C2D_SceneBegin(bottom);
	if (!cached) /* no cache texture: everything every frame */
		draw_static(w);
	spr_begin(BW, 0.1f);
	if (cached) { /* the cache's 320x240 corner, 1:1 (drawn y-down from the texture's top row = v 1) */
		spr_state(&ptex, true, 1);
		spr_vtx *v = spr_quad();
		if (v) {
			const float u1 = (float)BW / TW, v1 = 1.0f - (float)BH / TH;
			spr_set(&v[0], 0, 0, 0, 1, 0xffffffff);
			spr_set(&v[1], BW, 0, u1, 1, 0xffffffff);
			spr_set(&v[2], 0, BH, 0, v1, 0xffffffff);
			spr_set(&v[3], BW, BH, u1, v1, 0xffffffff);
		}
	}
	spr_end();
	draw_gauge_fill(w);
}

/* ================================================================ layout editor preview */

void panel_preview_begin(int chara, int color)
{
	panel_preview_end();
	load_char(chara, color);
	text_width(FONT_SYSTEM, "0", 1.0f);
	fit_labels();
	pl_on = true;
	pvalid = false;
	drawn_ok = false;
}

int panel_preview_chara(void) { return pl_on ? pl_chara : -1; }

const char *panel_move_name(const char *key) { return move_name(key); }

void panel_preview_refresh(void) { fit_labels(); }

void panel_preview_end(void)
{
	if (pl_on || pl_art.t.ok || pl_names)
		panel_match_end();
}

/* outline of quadrant k (cursor / picked-up marker) */
static void quad_outline(int k, float w, u32 c)
{
	float x[3], y[3];
	tri_of(k, x, y);
	/* pulled in a little so the screen edge does not clip it */
	float cx = (x[0] + x[1] + x[2]) / 3, cy = (y[0] + y[1] + y[2]) / 3;
	for (int i = 0; i < 3; i++) {
		x[i] += (cx - x[i]) * 0.03f + (x[i] <= 0 ? 2 : x[i] >= BW ? -2 : 0);
		y[i] += (cy - y[i]) * 0.03f + (y[i] <= 0 ? 2 : y[i] >= AREA_Y1 ? -2 : 0);
	}
	for (int i = 0; i < 3; i++)
		seam(x[i], y[i], x[(i + 1) % 3], y[(i + 1) % 3], w, c);
}

void panel_preview_draw(int cursor, int held, float t)
{
	ui_blend_reset();
	backdrop(1.0f, 0.85f);
	if (!pl_on)
		return;
	for (int k = 0; k < HT_NSLOT; k++)
		if (k != HT_CENTER) {
			bool has = ht_slot(pl_chara, ht_pos_slot(pl_chara, k))->key != NULL;
			draw_quadrant(k, has ? S_READY : S_EMPTY, k == held);
		}
	draw_center(ht_slot(pl_chara, HT_CENTER)->key ? S_LOCKED : S_EMPTY, false); /* fixed: drawn dimmed */
	float pulse = 0.5f + 0.5f * sinf(t * 6.2831853f);
	if (held >= 0 && held != HT_CENTER)
		quad_outline(held, 3, ui_c2d(argb(0.95f, 0xfff4a0)));
	if (cursor >= 0 && cursor != HT_CENTER && cursor != held)
		quad_outline(cursor, 2.5f, ui_c2d(argb(0.55f + 0.45f * pulse, 0xffffff)));
}
