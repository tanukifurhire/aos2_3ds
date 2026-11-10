#include "../src/sim/rng.h"

#include <stdio.h>

static int failures, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
	aos_mt m;
	/* Reference values: std::mt19937 default seed 5489. The 10000th output
	 * is required by the C++ standard to be 4123659995. */
	aos_mt_seed(&m, 5489);
	uint32_t first = aos_mt_next(&m);
	CHECK(first == 3499211612u, "first output %u", first);
	aos_mt_seed(&m, 5489);
	uint32_t v = 0;
	for (int i = 0; i < 10000; i++)
		v = aos_mt_next(&m);
	CHECK(v == 4123659995u, "10000th output %u", v);
	aos_mt_seed(&m, 0); /* core.md §8: the game reseeds with 0 every round */
	uint32_t z0 = aos_mt_next(&m);
	CHECK(z0 == 2357136044u, "seed 0 first output %u", z0);

	aos_mt_seed(&m, 1);
	for (int i = 0; i < 100000; i++) {
		float f = aos_rand_f(&m, -0.3f, 0.3f);
		if (!(f >= -0.3f && f <= 0.3f)) { CHECK(0, "float out of range %f", f); break; }
		int32_t r = aos_rand_i(&m, 0, 100);
		if (r < 0 || r > 100) { CHECK(0, "int out of range %d", r); break; }
		int32_t z = aos_rand_i(&m, 7, 7);
		if (z != 7) { CHECK(0, "degenerate range %d", z); break; }
	}
	/* uniform_int over a non-power-of-two range should hit every value */
	int seen[3] = {0};
	for (int i = 0; i < 1000; i++) seen[aos_rand_i(&m, -1, 1) + 1]++;
	CHECK(seen[0] && seen[1] && seen[2], "range coverage %d %d %d", seen[0], seen[1], seen[2]);
	/* the full-range path returns raw draws */
	aos_mt a, b;
	aos_mt_seed(&a, 42); aos_mt_seed(&b, 42);
	CHECK((uint32_t)aos_rand_i(&a, (int32_t)0x80000000, 0x7fffffff) == aos_mt_next(&b) + 0x80000000u, "full range");
	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
