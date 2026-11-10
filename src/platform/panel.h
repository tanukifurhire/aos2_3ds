/* 3DS bottom-screen match panel: heat + combo counters of both players (HUD
 * art, hud_panel_draw) and a few status lines in the system font. Replaces the
 * text console on the bottom screen while a match runs. Presentation only. */
#ifndef AOS_PANEL_H
#define AOS_PANEL_H

#include <3ds.h>
#include <citro2d.h>

#include "../sim/sim.h"

#define PANEL_LINES 6

/* Take the bottom screen from the console (con keeps printf harmless while the
 * panel is up: it is redirected to an off-screen copy). */
void panel_begin(PrintConsole *con);
/* Give the bottom screen back to the console (re-initialised, cleared). */
void panel_end(PrintConsole *con);
/* The bottom-screen render target (created by the first panel_begin); the
 * menus draw into it too (src/app/scene.c). */
C3D_RenderTarget *panel_target(void);
/* Status line i (0..PANEL_LINES-1); colour is RGBA (C2D_Color32). Re-parsed only when it changes. */
void panel_line(int i, u32 color, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void panel_clear_lines(void);
/* Draw the panel; call inside C3D_FrameBegin/End. */
void panel_draw(const aos_world *w);

#endif
