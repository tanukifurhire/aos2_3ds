/* Common visual effects API (fx.c): common effect type 0, particles, sparks,
 * message popups, screen effects used by hypers, afterimages, camera focus.
 * Argument orders mirror the original functions. */
#ifndef AOS_FX_H
#define AOS_FX_H

#include <stdint.h>

#include "draw.h"
#include "sim.h"

/* ---- simulation side (consume RNG / world state; call from T2/object code) ---- */

/* FUN_0068b8a0(kind, colour, pos, angle, owner, age0, count). kinds 1000, 0x3e9, 0x3ea,
 * 0x44c, 0x456, 0x4b0, 0x4ba; colour 0..11 (blue, red, green, yellow, violet, magenta,
 * aqua, orange, emerald, ygreen, cobalt, carmine). Returns the number spawned. */
int aos_spawn_particle(aos_world *w, int32_t kind, int32_t colour, const float pos[3], float angle,
                       int32_t owner, int32_t age0, int32_t count);

/* FUN_007bd5e0(kind, colour, maxLen, count, angle, pos, owner). kinds 1000, 0x44c, 0x4b0,
 * 2000, 0x7da, 3000, 0xbc2; colour 0..11 (spark RGB table 0xa4e2c8). */
int aos_spawn_spark(aos_world *w, int32_t kind, int32_t colour, int32_t maxLen, int32_t count,
                    float angle, const float pos[3], int32_t owner);

/* FUN_0065dff0(kind, pos, owner): 0 msg_canl, 1 msg_pass, 2 msg_chrg, 3 msg_gurd. */
void aos_popup(aos_world *w, int kind, const float pos[3], int32_t owner);

void aos_screen_dark(aos_world *w, int32_t frames, float level);                 /* FUN_004e9280 */
void aos_speedline(aos_world *w, int32_t frames, const float pos[3], float angle,
                   float spread);                                                /* FUN_007bec20 */
void aos_screen_blur(aos_world *w, int32_t frames, float amount, int32_t blend); /* FUN_004d7ac0 */
void aos_cutin(aos_world *w);                                                    /* FUN_004e8100 */
void aos_camera_focus(aos_world *w, int32_t timer, int32_t playerId, const float pos[3],
                      float zoom);                                               /* FUN_005f8fc0 */
/* Focus-list part of FUN_005f8b90: ages the list, then blends (cx, cy, z) through it.
 * objects.c aos_camera_update should call it after computing the base centre/zoom. */
void aos_camera_focus_clear(aos_world *w);                  /* FUN_004a9f10 (used by FUN_006b1d70 proximity knock) */
int aos_camera_focus_count(const aos_world *w);             /* focus list size (e.g. Star Breaker adds focus only when empty) */
void aos_camera_focus_apply(aos_world *w, float *cx, float *cy, float *z);

/* Standard hyper start: SE 6, dark(100, .75), speedline(100, pos, faceAngle, π/4),
 * blur(20, .5, 2), common effect 0xfa. Characters with other parameters call the
 * primitives above (or aos_hyper_flourish_ex) in the original order. */
void aos_hyper_flourish(aos_world *w, aos_player *p);
/* Same sequence with parameters; slFrames <= 0 omits the speedline, fa == 0 omits 0xfa. */
void aos_hyper_flourish_ex(aos_world *w, aos_player *p, int se, int32_t darkT, float darkL,
                           int32_t slFrames, float slAngle, float slSpread, int32_t blurT,
                           float blurA, int32_t blurBlend, int fa);

/* ---- draw side (draw-only state; call from T1 handlers / the renderer) ---- */

/* FUN_004d7e00(model, flag, player, rot{x,y,z} bam): records the command just emitted
 * by a T1 handler as an afterimage (drawn at the player's position, blend 2, alpha .25
 * fading by .0125/tick, tint from player heat). */
void aos_afterimage(aos_world *w, const aos_dcmd *model, int flag, const aos_player *p,
                    const int32_t rot[3]);
void aos_afterimage_clear(aos_world *w, int32_t objId); /* FUN_004d7ff0 */

void aos_particles_draw(aos_world *w);   /* FUN_0068c000 */
void aos_sparks_draw(aos_world *w);      /* FUN_007bd350 */
void aos_popups_draw(aos_world *w);      /* FUN_0065e050 */
void aos_afterimages_draw(aos_world *w); /* FUN_004d7bc0 */
void aos_dark_draw(aos_world *w);        /* FUN_004e9180 (full-screen fill, "$white") */
void aos_speedlines_draw(aos_world *w);  /* FUN_007be8b0 (approximation) */
void aos_blur_draw(aos_world *w);        /* FUN_004d7900 ("$blur" = previous frame) */
void aos_cutin_draw(aos_world *w);       /* FUN_004e8200 */

#endif
