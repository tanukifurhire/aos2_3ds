#include "amath.h"

#include <math.h>
#include <string.h>

#include "perf.h"

/* cos/sin are pure, so remembering recent results is exact. Objects moving
 * along a fixed heading ask for the same angle every frame, and code often
 * wants cos(a) and sin(a) together; on the 3DS each miss is a double-precision
 * libm call on a VFPv2 without hardware transcendental support. */
#define TRIG_MEMO 1024 /* power of two */
static struct {
	uint32_t key;
	float c, s;
	uint8_t hc, hs;
} trig_memo[TRIG_MEMO];

static inline uint32_t trig_key(float a, uint32_t *slot)
{
	uint32_t k;
	memcpy(&k, &a, sizeof k);
	*slot = (k * 0x9e3779b1u) >> 22; /* 10 bits */
	return k;
}

float aos_cos(float a)
{
	uint32_t h, k = trig_key(a, &h);
	AOS_PERF_ADD(trig, 1);
	if (trig_memo[h].key == k && trig_memo[h].hc)
		return trig_memo[h].c;
	if (trig_memo[h].key != k)
		trig_memo[h].key = k, trig_memo[h].hs = 0;
	AOS_PERF_ADD(trig_miss, 1);
	trig_memo[h].hc = 1;
	return trig_memo[h].c = (float)cos((double)a);
}

float aos_sin(float a)
{
	uint32_t h, k = trig_key(a, &h);
	AOS_PERF_ADD(trig, 1);
	if (trig_memo[h].key == k && trig_memo[h].hs)
		return trig_memo[h].s;
	if (trig_memo[h].key != k)
		trig_memo[h].key = k, trig_memo[h].hc = 0;
	AOS_PERF_ADD(trig_miss, 1);
	trig_memo[h].hs = 1;
	return trig_memo[h].s = (float)sin((double)a);
}
float aos_atan2(float y, float x) { AOS_PERF_ADD(trig, 1); return (float)atan2((double)y, (double)x); }
float aos_sqrt(float v) { return (float)sqrt((double)v); }
float aos_fabs(float v) { return fabsf(v); }

void aos_angle_normalize(float *a)
{
	const float lo = -3.1415927f; /* DAT_00814100 */
	while ((*a <= lo && *a != lo) || *a > AOS_PI) {
		if (*a <= lo && *a != lo)
			*a += AOS_2PI;
		if (*a > AOS_PI)
			*a -= AOS_2PI;
	}
}

void aos_angle_toward(float *a, float target, float step)
{
	aos_angle_normalize(a);
	aos_angle_normalize(&target);
	float d = target - *a;
	if (d > AOS_PI)
		d = -(AOS_2PI - d);
	else if (d < -3.1415927f)
		d = AOS_2PI + d;
	if (step <= aos_fabs(d)) {
		if (d > 0.0f)
			*a += step;
		else if (d < 0.0f)
			*a -= step;
	} else {
		*a = target;
	}
}

void aos_vec_rotate(float v[2], float ang)
{
	float c = aos_cos(ang), s = aos_sin(ang), x = v[0];
	v[0] = v[0] * c - v[1] * s;
	v[1] = x * s + v[1] * c;
}

float aos_input_angle(const aos_dirs *d)
{
	if (d->yp < 2) {
		if (d->yn < 2) {
			if (d->xn < 2)
				return d->xp < 2 ? AOS_NO_INPUT : 0.0f;
			return AOS_PI;
		}
		if (d->xp < 2)
			return d->xn < 2 ? -1.5707964f : -2.3561945f;
		return -0.7853982f;
	}
	if (d->xp < 2)
		return d->xn < 2 ? 1.5707964f : 2.3561945f;
	return 0.7853982f;
}

uint16_t aos_rad2bam(float rad) { return (uint16_t)((int32_t)((rad * 65536.0f) / AOS_2PI) & 0xffff); }
uint16_t aos_deg2bam(int32_t deg) { return (uint16_t)((deg * 0x10000) / 0x168); }
