/* 3DS bottom-screen match panel (port addition): five touch hyper buttons for the local player (UP S+H, LEFT H,
 * RIGHT B+H, DOWN D+H, CENTER Accel Hyper; src/input/hyper_touch.h) with the move names from the PC's command
 * list, their costs and the hyper gauge, over the menu background and the player's full-body result art.
 * The button layer is cached in an off-screen texture and redrawn only when a button changes state; per frame
 * the panel is one textured quad + the gauge fill, and the frame skips the bottom screen while nothing changed
 * (panel_same). Presentation + touch reading only. */
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

/* Match start / end: loads the local player's art and move names (local = 0 or 1, the player whose buttons
 * are shown and who the touches belong to), frees them again. */
void panel_match_begin(const aos_world *w, int local);
void panel_match_end(void);
/* Once per match frame, before the input mask is built: the hyper button held on the touch screen (HT_*,
 * latched at the touch-down if that hyper is available then; -1 none). */
int panel_touch(const aos_world *w);
/* Forget the touch (pause, round flow): the next press must start a new touch. */
void panel_touch_reset(void);

/* Debug status line i (0..PANEL_LINES-1), colour RGBA (C2D_Color32). Shown only in profile builds while SELECT
 * is held (small, over the gauge). */
void panel_line(int i, u32 color, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void panel_clear_lines(void);
/* Draw the panel; call inside C3D_FrameBegin/End. */
void panel_draw(const aos_world *w);
/* True when panel_draw would draw exactly what the screen shows from its last call: the frame may skip the
 * bottom screen (scene.c bottom_same). */
bool panel_same(const aos_world *w);

/* For the pause command list (select_draw.c): the panel's background (menu gradient + the player's art) at
 * opacity a over the current citro2d target, the button layout and colours, and the chamfered panel shape. */
void panel_backdrop(float a);
void panel_slot_rect(int slot, float *x0, float *y0, float *x1, float *y1);
unsigned panel_slot_color(int slot); /* 0xRRGGBB */
/* Convex chamfered rectangle (corners cut by c) with a vertical gradient top -> bottom (PC ARGB), citro2d. */
void panel_chamfer(float x0, float y0, float x1, float y1, float c, unsigned top, unsigned bottom);

/* Screen position (HT_*) under bottom-screen point x,y (-1 = the gauge strip). */
int panel_hit_pos(int x, int y);

/* Layout editor (Config > Controls > Hyper layout): the panel of character chara (art colour `color`) without a
 * match, its labels at the current ht layout. refresh after the layout changed; draw into the bound target:
 * cursor = outlined position, held = the picked-up position (-1 none), t = seconds (pulse). end frees it (also
 * done by panel_match_begin). */
void panel_preview_begin(int chara, int color);
int panel_preview_chara(void); /* -1 when no preview is loaded */
void panel_preview_refresh(void);
void panel_preview_draw(int cursor, int held, float t);
void panel_preview_end(void);
/* Move name (command list) of a ht_move key from the loaded preview / match ("" if none). */
const char *panel_move_name(const char *key);

#endif
