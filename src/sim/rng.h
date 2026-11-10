/* AoS2 RNG: std::mt19937 + MSVC distribution algorithms, as used by the
 * original (engines at 0x875b68 / 0x876ef0; FUN_0045a500 = mt19937(),
 * FUN_0045a270 = uniform_real<float>, FUN_0045a1b0 = uniform_int). */
#ifndef AOS_RNG_H
#define AOS_RNG_H

#include <stdint.h>

typedef struct {
	uint32_t mt[624];
	int idx;
} aos_mt;

void aos_mt_seed(aos_mt *m, uint32_t seed);
uint32_t aos_mt_next(aos_mt *m);
/* FUN_00459fd0: uniform float in [a, b) — one 32-bit draw. */
float aos_rand_f(aos_mt *m, float a, float b);
/* FUN_00459f80: uniform int in [lo, hi] inclusive (MSVC rejection loop). */
int32_t aos_rand_i(aos_mt *m, int32_t lo, int32_t hi);

#endif
