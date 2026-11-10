/* In-match HUD, round banners and screen transitions, ported from the PC
 * battle scene draw FUN_005f91d0 (docs/re/hud.md). Platform independent: it
 * draws through hud_gfx.h. */
#ifndef AOS_HUD_H
#define AOS_HUD_H

#include "../sim/sim.h"

/* The PC lays the HUD out for the back buffer size: W,H = DAT_009b0fe4/fe8,
 * s = DAT_009b1000 = min(W/800, H/600), s4 = DAT_009b1004 = W/800,
 * s8 = DAT_009b1008 = H/600. The 3DS top screen is the PC game at 400x240. */
typedef struct {
	int W, H;
	float s, s4, s8;
} hud_screen;

hud_screen hud_screen_for(int W, int H);

/* Camera used for world-space HUD parts (weapon gauge rings): screen =
 * (W/2 + (x - camX) / zoomDiv, H/2 - (y - camY) / zoomDiv). */
typedef struct {
	float camX, camY, zoomDiv;
} hud_camera;

/* Everything FUN_005f91d0 draws after the gameplay layer, in its order:
 * weapon rings (FUN_006b29c0), HUD (FUN_006bafd0, only while visible),
 * round-start banner (FUN_00617c90), round-end banner (FUN_00608ee0),
 * screen transition (FUN_005622a0). Reads the world only. */
void hud_draw(const aos_world *w, const hud_screen *sc, const hud_camera *cam);

#ifdef __3DS__
/* citro2d backend (hud_c2d.c): HUD textures for a match (after the sprite
 * bank folders are loaded) and the draw for a W x H target. */
void hud_c2d_load(const aos_world *w);
void hud_c2d_unload(void);
void hud_c2d_draw(const aos_world *w, int W, int H, float camX, float camY, float zoomDiv);
#endif

#endif
