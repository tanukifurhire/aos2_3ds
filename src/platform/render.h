/* Renders the simulation's draw list (src/sim/draw.h) on the 3DS top screen. */
#ifndef AOS_RENDER_H
#define AOS_RENDER_H

#include <citro2d.h>
#include <stdbool.h>

#include "../sim/sim.h"

/* Load common art + both players' character folders (charaXxxA/B/C by palette). */
bool render_load_match(const aos_world *w);
void render_unload_match(void);

/* Draw the world (stage backdrop, draw list, HUD) to the given target. */
void render_world(C3D_RenderTarget *top, const aos_world *w);
/* Target passed to the last render_world (profile builds' frame capture). */
C3D_RenderTarget *render_last_target(void);

/* Character folder base names by engine id (e.g. 10 -> "charaSuguri"). */
const char *render_char_folder(int id);
const char *render_char_name(int id);
/* Palette folder loaded for player i (e.g. "charaSuguriA"), "" if none. */
const char *render_player_folder(int i);

#endif
