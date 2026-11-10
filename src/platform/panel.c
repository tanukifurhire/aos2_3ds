#include "panel.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "hud.h"
#include "sprbatch.h"

#define BW 320
#define BH 240

static C3D_RenderTarget *bottom;
static C2D_TextBuf tbuf;
static C2D_Text txt[PANEL_LINES];
static char line[PANEL_LINES][64];
static u32 lcol[PANEL_LINES];
static bool dirty;
static bool pvalid; /* the cached panel image is current (panel_draw) */
/* What the last panel_draw showed: status lines + the counters' primitive hash. */
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

/* The panel's static layer (background, separators, status lines in the system font) changes only when a status
 * line does: it is rendered into an off-screen texture then and drawn as one quad every frame; the heat/combo
 * counters (hud_c2d_panel, a few dozen batched quads) are drawn on top every frame. The full citro2d draw cost
 * ~0.8 ms CPU per frame, mostly the text. */
#define TW 512
#define TH 256
static C3D_Tex ptex;
static C3D_RenderTarget *ptgt;
static bool pfail;

static bool cache_init(void)
{
	if (ptgt || pfail)
		return ptgt != NULL;
	pfail = true;
	if (!spr_init())
		return false;
	if (!C3D_TexInit(&ptex, TW, TH, GPU_RGB565)) /* linear: VRAM is kept for the sprite atlases */
		return false;
	C3D_TexSetFilter(&ptex, GPU_NEAREST, GPU_NEAREST);
	C3D_TexSetWrap(&ptex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	/* no depth buffer (citro3d then turns depth reads/writes off for this target): the static layer is drawn
	 * back to front anyway, and a depth buffer would take 256 KB of VRAM */
	ptgt = C3D_RenderTargetCreateFromTex(&ptex, GPU_TEXFACE_2D, 0, -1);
	if (!ptgt) {
		C3D_TexDelete(&ptex);
		return false;
	}
	pfail = false;
	return true;
}

static void draw_static(void)
{
	C2D_DrawRectangle(0, 0, 0.1f, BW, 170, C2D_Color32(24, 34, 78, 255), C2D_Color32(24, 34, 78, 255),
	                  C2D_Color32(10, 14, 34, 255), C2D_Color32(10, 14, 34, 255));
	C2D_DrawRectSolid(BW / 2, 6, 0.2f, 1, 156, C2D_Color32(90, 110, 170, 120));
	C2D_DrawRectSolid(0, 170, 0.2f, BW, 1, C2D_Color32(90, 110, 170, 200));
	if (tbuf)
		for (int i = 0; i < PANEL_LINES; i++)
			if (line[i][0])
				C2D_DrawText(&txt[i], C2D_WithColor, 6, 174 + i * 11, 0.9f, 0.42f, 0.42f, lcol[i]);
}


static uint32_t content_hash(const aos_world *w)
{
	uint32_t h = hud_c2d_panel_hash(w, BW, BH);
	const uint8_t *b = (const uint8_t *)line;
	for (size_t i = 0; i < sizeof line; i++)
		h = (h ^ b[i]) * 16777619u;
	b = (const uint8_t *)lcol;
	for (size_t i = 0; i < sizeof lcol; i++)
		h = (h ^ b[i]) * 16777619u;
	return h;
}

/* panel_same's hash for this frame's panel_draw (the scene asks first, then draws) */
static uint32_t asked_hash;
static bool asked;

bool panel_same(const aos_world *w)
{
	if (!bottom || !drawn_ok || dirty || !pvalid)
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
	bool text = dirty;
	if (dirty && tbuf) {
		dirty = false;
		C2D_TextBufClear(tbuf);
		for (int i = 0; i < PANEL_LINES; i++) {
			C2D_TextParse(&txt[i], tbuf, line[i]);
			C2D_TextOptimize(&txt[i]);
		}
	}
	const u32 bg = C2D_Color32(10, 14, 34, 255);
	bool cached = cache_init();
	if (cached && (!pvalid || text)) {
		pvalid = true;
		C2D_TargetClear(ptgt, bg);
		C2D_SceneBegin(ptgt);
		draw_static();
	}
	C2D_TargetClear(bottom, bg);
	C2D_SceneBegin(bottom);
	if (!cached) /* no cache texture: everything every frame */
		draw_static();
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
	hud_c2d_panel(w, BW, BH); /* depth 0.9 */
	spr_end();
}
