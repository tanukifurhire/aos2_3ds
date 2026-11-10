/* Math helpers ported 1:1 from aos2.exe. Angles are float radians unless a
 * name says "bam" (16-bit binary angle, 0x10000 = 360°). */
#ifndef AOS_AMATH_H
#define AOS_AMATH_H

#include <stdint.h>

#define AOS_PI 3.1415927f       /* DAT_00853b58 */
#define AOS_2PI 6.2831855f      /* DAT_00853b78 */
#define AOS_NO_INPUT 12.566371f /* _DAT_0082025c: "no direction held" (> 2π) */

/* Directional inputs as stored at player+0x1c (x+), +0x20 (x−), +0x24 (y+), +0x28 (y−). */
typedef struct {
	int32_t xp, xn, yp, yn;
} aos_dirs;

float aos_cos(float a);               /* FUN_00467690 (double cos, cast to float) */
float aos_sin(float a);               /* FUN_0045a680 */
float aos_atan2(float y, float x);    /* FUN_00474240 */
float aos_sqrt(float v);              /* FUN_00458320 */
float aos_fabs(float v);              /* FUN_00492390 */
void aos_angle_normalize(float *a);   /* FUN_005f0c60: into (−π, π] */
void aos_angle_toward(float *a, float target, float step); /* FUN_005f0e70 */
void aos_vec_rotate(float v[2], float ang);                /* FUN_005f1210 */
float aos_input_angle(const aos_dirs *d);                  /* FUN_0061b230 */
uint16_t aos_rad2bam(float rad);      /* FUN_0045a0c0 */
uint16_t aos_deg2bam(int32_t deg);    /* FUN_0045a0f0 */

#endif
