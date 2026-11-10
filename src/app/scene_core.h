/* Internal interface between the portable scene core (scene_core.c) and the
 * 3DS loop (scene.c) / host tests. Scenes use scene.h only. */
#ifndef AOS_APP_SCENE_CORE_H
#define AOS_APP_SCENE_CORE_H

#include <stddef.h>

#include "scene.h"

void scene_core_start(int first);
const scene_def *scene_core_begin_frame(void); /* runs a pending init; returns the current def (NULL = quit) */
void scene_core_update(void);
bool scene_core_end_frame(void);               /* applies scene_goto; false once SCENE_QUIT is reached */
bool scene_switch_pending(void);
int scene_requested(void);                     /* id last switched to (before the coming-soon fallback) */

void scene_fade_update(void);
bool scene_fade_white(void);

void announce_update(void);
float announce_alpha(void);
void announce_typed(char *out, size_t sz);

extern const char *config_path;

/* Called after C3D_FrameEnd every frame when set (battle: profiler). */
extern void (*scene_post_frame_hook)(void);
#ifdef __3DS__
void scene_setup(struct C3D_RenderTarget_tag *top, struct PrintConsole *con); /* before scene_main */
#endif

#endif
