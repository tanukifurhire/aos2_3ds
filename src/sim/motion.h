/* EnemyMotion system (objects.md §3.5): 32 keyframed motion slots built by
 * "EnemyMotion Init" FUN_00526670, sampled by FUN_00527ee0 and consumed by the
 * Enemy hierarchy attachment FUN_00528250 (objects.c attach()). Static data. */
#ifndef AOS_MOTION_H
#define AOS_MOTION_H

#include <stdbool.h>
#include <stdint.h>

#define AOS_MOTION_SLOTS 32

typedef struct { float x, y, a; } motion_sample;

/* FUN_00527ee0(slot, frame, &xy, &angle): sample slot at an integer frame. */
motion_sample aos_motion_get(int32_t slot, int32_t frame);
int32_t aos_motion_len(int32_t slot);   /* DAT_008855ec + slot*0x14 */
bool aos_motion_loop(int32_t slot);     /* DAT_008855f0 + slot*0x14 */

/* Hook used by attach(): sample at *frame, then advance the frame exactly like
 * FUN_00528250 does (frame++; if loop && frame > len: frame -= len). */
motion_sample aos_motion_step(int32_t slot, int32_t *frame);

/* objects.c hook (set by aos_register_sham / aos_register_nath). */
extern motion_sample (*aos_enemy_motion)(int32_t motionId, int32_t *frame);

#endif
