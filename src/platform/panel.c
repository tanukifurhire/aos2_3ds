#include "panel.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "hud.h"

#define BW 320
#define BH 240

static C3D_RenderTarget *bottom;
static C2D_TextBuf tbuf;
static C2D_Text txt[PANEL_LINES];
static char line[PANEL_LINES][64];
static u32 lcol[PANEL_LINES];
static bool dirty;

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

void panel_draw(const aos_world *w)
{
	if (!bottom)
		return;
	if (dirty && tbuf) {
		dirty = false;
		C2D_TextBufClear(tbuf);
		for (int i = 0; i < PANEL_LINES; i++) {
			C2D_TextParse(&txt[i], tbuf, line[i]);
			C2D_TextOptimize(&txt[i]);
		}
	}
	C2D_TargetClear(bottom, C2D_Color32(10, 14, 34, 255));
	C2D_SceneBegin(bottom);
	C2D_DrawRectangle(0, 0, 0.1f, BW, 170, C2D_Color32(24, 34, 78, 255), C2D_Color32(24, 34, 78, 255),
	                  C2D_Color32(10, 14, 34, 255), C2D_Color32(10, 14, 34, 255));
	C2D_DrawRectSolid(BW / 2, 6, 0.2f, 1, 156, C2D_Color32(90, 110, 170, 120));
	C2D_DrawRectSolid(0, 170, 0.2f, BW, 1, C2D_Color32(90, 110, 170, 200));
	hud_c2d_panel(w, BW, BH);
	if (tbuf)
		for (int i = 0; i < PANEL_LINES; i++)
			if (line[i][0])
				C2D_DrawText(&txt[i], C2D_WithColor, 6, 174 + i * 11, 0.9f, 0.42f, 0.42f, lcol[i]);
}
