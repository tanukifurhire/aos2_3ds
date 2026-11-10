#include "rng.h"

void aos_mt_seed(aos_mt *m, uint32_t seed)
{
	m->mt[0] = seed;
	for (int i = 1; i < 624; i++)
		m->mt[i] = 1812433253u * (m->mt[i - 1] ^ (m->mt[i - 1] >> 30)) + (uint32_t)i;
	m->idx = 624;
}

uint32_t aos_mt_next(aos_mt *m)
{
	if (m->idx >= 624) {
		for (int i = 0; i < 624; i++) {
			uint32_t y = (m->mt[i] & 0x80000000u) | (m->mt[(i + 1) % 624] & 0x7fffffffu);
			m->mt[i] = m->mt[(i + 397) % 624] ^ (y >> 1) ^ ((y & 1u) ? 0x9908b0dfu : 0u);
		}
		m->idx = 0;
	}
	uint32_t y = m->mt[m->idx++];
	y ^= y >> 11;
	y ^= (y << 7) & 0x9d2c5680u;
	y ^= (y << 15) & 0xefc60000u;
	y ^= y >> 18;
	return y;
}

/* MSVC generate_canonical<float, 24>: k = max(1, ceil(24/32)) = 1 draw,
 * r = draw / 2^32 computed in double, converted to float (clamped below 1),
 * then a + r*(b-a) in float (FUN_0045a270 disassembly). */
float aos_rand_f(aos_mt *m, float a, float b)
{
	double d = (double)aos_mt_next(m) / 4294967296.0;
	float r = (float)d;
	if (r >= 1.0f)
		r = 0.99999994f; /* nextafter(1, 0) as MSVC does */
	return r * (b - a) + a;
}

/* FUN_0045a1b0, transcribed: range = hi - lo (unsigned); full range ->
 * single draw; otherwise reject draws in the incomplete last bucket. */
int32_t aos_rand_i(aos_mt *m, int32_t lo, int32_t hi)
{
	uint32_t range = (uint32_t)hi - (uint32_t)lo;
	uint32_t v;
	if (range == 0xffffffffu) {
		v = aos_mt_next(m);
	} else {
		uint32_t u, top;
		do {
			u = 0;
			top = 0;
			if (range != 0) {
				u = aos_mt_next(m);
				top = 0xffffffffu;
			}
			v = u % (range + 1);
		} while (top / (range + 1) <= u / (range + 1) && top % (range + 1) != range);
	}
	return (int32_t)(v + (uint32_t)lo);
}
