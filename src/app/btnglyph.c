/* Button glyphs (btnglyph.h). */
#include "btnglyph.h"

#include <3ds.h>
#include <citro2d.h>

#include "../input/aos_input.h"
#include "../input/hyper_touch.h"
#include "../platform/panel.h"
#include "text.h"
#include "ui.h"

static unsigned a8(float a) { return (unsigned)(int)(255.0f * (a < 0 ? 0 : a > 1 ? 1 : a)) << 24; }
static u32 c2d(unsigned argb) { return ui_c2d(argb); }

/* face button: coloured disc (New 3DS colours), light rim, letter */
static float face(float x, float cy, int btn, float a, float sc)
{
	static const unsigned col[4] = {0xe0403c, 0xf0c020, 0x3a7cf0, 0x30b060}; /* A B X Y */
	float r = 7.5f * sc, cx = x + r;
	C2D_DrawCircleSolid(cx, cy + 1 * sc, UI_DEPTH, r + 0.5f, c2d(a8(a * 0.6f)));
	C2D_DrawCircleSolid(cx, cy, UI_DEPTH, r, c2d(a8(a) | 0xf4f4f8));
	C2D_DrawCircleSolid(cx, cy, UI_DEPTH, r - 1.5f * sc, c2d(a8(a) | col[btn & 3]));
	C2D_DrawCircleSolid(cx - 1.5f * sc, cy - 2 * sc, UI_DEPTH, r - 4.0f * sc, c2d(a8(a * 0.25f) | 0xffffff));
	const char *s = aos_btn_name(btn);
	text_draw(FONT_SYSTEM, s, cx + 0.5f, cy + 1, TEXT_CENTER, c2d(a8(a * 0.8f)), 0.8f * sc, 0);
	text_draw(FONT_SYSTEM, s, cx, cy, TEXT_CENTER, c2d(a8(a) | 0xffffff), 0.8f * sc, 0);
	return 2 * r;
}

/* shoulder button / SELECT: grey slanted pill with the name */
static float pill_w(int btn, float sc)
{
	return btn == AOS_BTN_SELECT ? text_width(FONT_SYSTEM, "SELECT", 0.55f * sc) + 9 * sc : 18 * sc;
}

static float pill(float x, float cy, int btn, float a, float sc)
{
	float w = pill_w(btn, sc), h = 12 * sc, c = 3 * sc;
	bool sel = btn == AOS_BTN_SELECT;
	panel_chamfer(x, cy - h / 2 + 1, x + w, cy + h / 2 + 1, c, a8(a * 0.6f), a8(a * 0.6f));
	panel_chamfer(x, cy - h / 2, x + w, cy + h / 2, c, a8(a) | 0xe8eaf0, a8(a) | 0x8a90a0);
	panel_chamfer(x + 1.5f * sc, cy - h / 2 + 1.5f * sc, x + w - 1.5f * sc, cy + h / 2 - 1.5f * sc, c - 1,
	              a8(a) | (sel ? 0x50586a : 0x6a7080), a8(a) | (sel ? 0x2a3040 : 0x3a4050));
	text_draw(FONT_SYSTEM, aos_btn_name(btn), x + w / 2, cy, TEXT_CENTER, c2d(a8(a) | 0xffffff), (sel ? 0.55f : 0.75f) * sc,
	          0);
	return w;
}

float glyph_btn_width(int btn, float sc) { return btn <= AOS_BTN_Y ? 15 * sc : pill_w(btn, sc); }

float glyph_btn(float x, float cy, int btn, float a, float sc)
{
	if (btn < 0 || btn >= AOS_BTN_COUNT)
		return 0;
	return btn <= AOS_BTN_Y ? face(x, cy, btn, a, sc) : pill(x, cy, btn, a, sc);
}

float glyph_act(float x, float cy, int act, float a, float sc)
{
	return act >= 0 && act < AOS_ACT_COUNT ? glyph_btn(x, cy, aos_binds_current()->btn[act], a, sc) : 0;
}

float glyph_act_width(int act, float sc)
{
	return act >= 0 && act < AOS_ACT_COUNT ? glyph_btn_width(aos_binds_current()->btn[act], sc) : 0;
}

/* a small map of the bottom panel (X-cut quadrants + the Accel hexagon) with the move's button lit at the
 * position the character's layout gives it */
float glyph_touch(float x, float cy, int chara, int slot, float a, float sc)
{
	const int pos = ht_slot_pos(chara, slot);
	const float w = 26 * sc, h = 18 * sc, y0 = cy - h / 2, mx = x + w / 2, my = y0 + h / 2;
	panel_chamfer(x - 1, y0 - 1, x + w + 1, y0 + h + 1, 3 * sc, a8(a) | 0xc8d0e0, a8(a) | 0x707890);
	ui_rect(x, y0, x + w, y0 + h, a8(a) | 0x0a1024);
	/* quadrant k: outer corners, inset 0.6 px from the diagonals */
	const float qx[4][2] = {{x + 1, x + w - 1}, {x, x}, {x + w, x + w}, {x + 1, x + w - 1}},
	            qy[4][2] = {{y0, y0}, {y0 + 1, y0 + h - 1}, {y0 + 1, y0 + h - 1}, {y0 + h, y0 + h}};
	const float ax[4] = {0, -1, 1, 0}, ay[4] = {-1, 0, 0, 1};
	for (int k = 0; k < 4; k++) {
		u32 c = c2d(a8(a) | (k == pos ? panel_slot_color(slot) : 0x3a4258));
		C2D_DrawTriangle(qx[k][0], qy[k][0], c, qx[k][1], qy[k][1], c, mx + ax[k], my + ay[k], c, UI_DEPTH);
	}
	/* the hexagon at the panel's proportions (HEX_R 70 of 320 px) */
	u32 cc = c2d(a8(a) | (slot == HT_CENTER ? panel_slot_color(HT_CENTER) : 0x4a5268)), rim = c2d(a8(a) | 0x0a1024);
	const float r = 5.7f * sc, hr = 4.9f * sc;
	C2D_DrawTriangle(mx - r - 1, my, rim, mx + r + 1, my, rim, mx, my - hr - 1, rim, UI_DEPTH);
	C2D_DrawTriangle(mx - r - 1, my, rim, mx + r + 1, my, rim, mx, my + hr + 1, rim, UI_DEPTH);
	C2D_DrawTriangle(mx - r, my, cc, mx + r, my, cc, mx, my - hr, cc, UI_DEPTH);
	C2D_DrawTriangle(mx - r, my, cc, mx + r, my, cc, mx, my + hr, cc, UI_DEPTH);
	return w;
}
