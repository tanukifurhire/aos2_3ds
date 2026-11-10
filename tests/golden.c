/* Golden-trace regression guard for performance work: runs a fixed set of
 * matches (random inputs, CPU vs CPU, CPU vs CPU with hyper pinned at 3.0)
 * and prints one hash line per scenario every 600 frames:
 *   full = whole aos_world bytes, live = aos_world with every pool slot >= its
 *   count left out (dead slots are not simulation state).
 * Usage: golden [frames per scenario]   (tests/run_golden.sh records/compares)
 * Any optimisation must leave this output byte-identical. */
#include "../src/sim/draw.h"
#include "../src/sim/sim.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
	const uint8_t *b = p;
	for (size_t i = 0; i < n; i++)
		h = (h ^ b[i]) * 0x01000193u;
	return h;
}

#define POOL(arr, cnt) {offsetof(aos_world, obj) + offsetof(aos_objects, arr), sizeof(((aos_objects *)0)->arr[0]), \
                        offsetof(aos_world, obj) + offsetof(aos_objects, cnt), sizeof(((aos_objects *)0)->arr) / sizeof(((aos_objects *)0)->arr[0])}
static const struct { size_t off, esz, coff, cap; } pools[] = {
	POOL(beam, nbeam), POOL(missile, nmissile), POOL(hitbox, nhitbox), POOL(laser, nlaser), POOL(enemy, nenemy),
	POOL(effect, neffect), POOL(whip, nwhip), POOL(particle, nparticle), POOL(spark, nspark), POOL(popup, npopup),
	POOL(hitreg, nhitreg),
};
#define NPOOLS (sizeof pools / sizeof pools[0])

static uint32_t live_hash(const aos_world *w)
{
	const uint8_t *b = (const uint8_t *)w;
	uint32_t h = 0x811c9dc5u;
	size_t at = 0;
	/* pools are laid out in declaration order inside aos_objects */
	for (size_t k = 0; k < NPOOLS; k++) {
		int32_t n;
		memcpy(&n, b + pools[k].coff, 4);
		if (n < 0)
			n = 0;
		if ((size_t)n > pools[k].cap)
			n = (int32_t)pools[k].cap;
		h = fnv(h, b + at, pools[k].off + pools[k].esz * (size_t)n - at);
		at = pools[k].off + pools[k].esz * pools[k].cap;
	}
	return fnv(h, b + at, sizeof *w - at);
}

/* Dead-slot invariant (objects.h): every slot >= count is all zero. hitreg is
 * exempt (its count is reset without clearing; snapshots keep it whole). */
static void check_dead(const aos_world *w, int a, int b, int f)
{
	const uint8_t *p = (const uint8_t *)w;
	for (size_t k = 0; k + 1 < NPOOLS; k++) {
		int32_t n;
		memcpy(&n, p + pools[k].coff, 4);
		for (size_t i = pools[k].off + pools[k].esz * (size_t)n; i < pools[k].off + pools[k].esz * pools[k].cap; i++)
			if (p[i]) {
				printf("DEAD SLOT DIRTY: pool %u slot %u (count %d) in %d vs %d at frame %d\n", (unsigned)k,
				       (unsigned)((i - pools[k].off) / pools[k].esz), n, a, b, f);
				exit(1);
			}
	}
}

static uint32_t lcg(uint32_t *s) { return *s = *s * 1664525u + 1013904223u; }
static uint16_t next_mask(uint32_t *s, uint16_t prev)
{
	uint32_t r = lcg(s) >> 8;
	uint16_t m = prev;
	if ((r & 7) == 0)
		m ^= (uint16_t)(1u << ((r >> 3) % 10));
	if ((r & 0x3f0) == 0)
		m = 0;
	return m;
}

static aos_world W;
static int check;

/* mode 0 random inputs, 1 CPU vs CPU (difficulty 2), 2 CPU vs CPU with hyper pinned at 3 */
static void scenario(int a, int b, int mode, int frames)
{
	int32_t ids[2] = {a, b}, sch[2] = {0, 0}, col[2] = {0, a == b ? 1 : 0};
	aos_match_init(&W, ids, sch, col, 180, 2);
	if (mode) {
		W.difficulty = 2;
		W.p[0].inputSrc = W.p[1].inputSrc = 10;
	}
	uint16_t m[2] = {0, 0};
	uint32_t s = 777u + (uint32_t)(a * 31 + b);
	uint32_t hf = 0, hl = 0;
	int maxfx = 0, maxb = 0;
	for (int f = 1; f <= frames; f++) {
		m[0] = next_mask(&s, m[0]);
		m[1] = next_mask(&s, m[1]);
		if (mode == 2)
			W.p[0].hyper = W.p[1].hyper = 3.0f;
		aos_tick(&W, m);
		aos_draw_begin();
		aos_draw_world(&W);
		if (W.flow.matchEnd)
			aos_match_restart(&W);
		if (check && f % 7 == 0)
			check_dead(&W, a, b, f);
		if (W.obj.neffect > maxfx) maxfx = W.obj.neffect;
		if (W.obj.nbeam > maxb) maxb = W.obj.nbeam;
		if (f % 600 == 0 || f == frames) {
			hf = fnv(hf, &W, sizeof W);
			hl = live_hash(&W) ^ (hl * 0x9e3779b1u);
		}
	}
	printf("%2d %2d m%d live %08x full %08x (hp %d/%d, peak fx %d beams %d)\n", a, b, mode, hl, hf, W.p[0].hp, W.p[1].hp,
	       maxfx, maxb);
	fflush(stdout);
}

int main(int argc, char **argv)
{
	aos_register_all();
	int frames = argc > 1 ? atoi(argv[1]) : 3000;
	check = argc > 2 && !strcmp(argv[2], "check");
	for (int a = 3; a <= 18; a++) {
		int b = a == 18 ? 3 : a + 1;
		scenario(a, b, 0, frames);
		scenario(a, b, 1, frames);
		scenario(a, a, 2, frames);
	}
	return 0;
}
