/* 3DS backend of the stage backgrounds (src/stage, docs/re/stages.md). */
#ifndef PLATFORM_STAGE_H
#define PLATFORM_STAGE_H

#include <citro2d.h>
#include <stdbool.h>

#include "../stage/stage.h"

bool stage3ds_init(void);   /* shader + fog ramp; call once after C2D_Init */
void stage3ds_exit(void);
/* Load stage `id` (STAGE_RANDOM resolves with the cosmetic RNG). */
void stage3ds_begin(int id);
void stage3ds_end(void);
/* Menu frame: keep the menu background (stage 4) loaded and update it. */
void stage3ds_menu_tick(void);
/* Feed the original globals: zoom = aos_world.obj.zoomView, matchFrame = frame counter. */
void stage3ds_tick(float zoom, int matchFrame);
/* Draw the stage into the bound C2D scene (call between C2D_SceneBegin and the
 * 2D gameplay layer). Flushes C2D, draws with citro3d, restores C2D state and
 * resets the depth buffer for the 2D layer. */
void stage3ds_draw(void);
/* CPU time of the last stage3ds_draw (record + submit) in microseconds. */
extern unsigned stage3ds_last_us, stage3ds_record_us; /* total / sg recording only */
/* CPU time of the last stage3ds_tick (stage update) in microseconds. */
extern unsigned stage3ds_update_us;

#endif
