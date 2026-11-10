/* aos_effects_update / aos_beams_update compact their pool in one pass
 * (objects.c) instead of erasing record by record like the original. This test
 * runs the new manager and a copy of the original loop side by side on two
 * worlds with a synthetic object type whose handler spawns 0..4 children (also
 * near and at the pool cap) and dies at random, and requires identical pools,
 * overflow counts and RNG state every frame. */
#include "../src/sim/sim.h"

#include <stdio.h>
#include <string.h>

#define T 1 /* unused owner type slot */

static bool upd(aos_world *w, aos_effect *e)
{
	int n = aos_rand_int(w, 0, 100);
	int kids = n < 40 ? 0 : n < 70 ? 1 : n < 85 ? 2 : n < 95 ? 3 : 4;
	if (e->variant > 6) /* bound the chains */
		kids = 0;
	float pos[3] = {e->pos[0] + 1.0f, e->pos[1], 0};
	for (int k = 0; k < kids; k++) {
		int i = aos_effect_new(w, T, e->id + 1, e->variant + 1, pos, e->rotation, e->ownerId, k - 1, e->team);
		if (i >= 0) /* the handlers' pattern: touch the new record by its index right away */
			w->obj.effect[i].aux = (float)(e->age * 10 + k);
	}
	e->pos[0] += 0.5f;
	return aos_rand_int(w, 0, 100) >= w->charGlobal[0][0]; /* death rate in % */
}

/* The original loop (FUN_004fde70 as ported before the compaction). */
static void ref_update(aos_world *w)
{
	for (int i = 0; i < w->obj.neffect; i++) {
		aos_effect *e = &w->obj.effect[i];
		e->age++;
		if (e->age < 0)
			continue;
		aos_effect_update_fn f = aos_otab.effectUpdate[e->type];
		if (!f || !f(w, e)) {
			memmove(&w->obj.effect[i], &w->obj.effect[i + 1], sizeof e[0] * (size_t)(w->obj.neffect - i - 1));
			memset(&w->obj.effect[w->obj.neffect - 1], 0, sizeof e[0]);
			w->obj.neffect--;
			i--;
		}
	}
}

static bool bupd(aos_world *w, aos_beam *b)
{
	int n = aos_rand_int(w, 0, 100);
	int kids = n < 40 ? 0 : n < 70 ? 1 : n < 85 ? 2 : n < 95 ? 3 : 4;
	if (b->f18 > 6)
		kids = 0;
	for (int k = 0; k < kids; k++) {
		int i = aos_beam_new(w, T, b->id + 1, b->pos, b->angle, b->ownerId, k - 1, b->team);
		if (i >= 0) {
			w->obj.beam[i].f18 = b->f18 + 1;
			w->obj.beam[i].damage = b->age * 10 + k;
		}
	}
	b->pos[0] += 0.5f;
	return aos_rand_int(w, 0, 100) >= w->charGlobal[0][0];
}

static void ref_beams(aos_world *w)
{
	for (int i = 0; i < w->obj.nbeam; i++) {
		aos_beam_update_fn f = aos_otab.beamUpdate[w->obj.beam[i].type];
		if (!f || !f(w, &w->obj.beam[i])) {
			memmove(&w->obj.beam[i], &w->obj.beam[i + 1], sizeof w->obj.beam[0] * (size_t)(w->obj.nbeam - i - 1));
			memset(&w->obj.beam[w->obj.nbeam - 1], 0, sizeof w->obj.beam[0]);
			w->obj.nbeam--;
			i--;
		} else {
			w->obj.beam[i].age++;
		}
	}
}

static aos_world A, B;

static int beams(void)
{
	int fails = 0, capped = 0;
	for (int seed = 0; seed < 12; seed++) {
		memset(&A, 0, sizeof A);
		aos_mt_seed(&A.rng, (uint32_t)seed + 100);
		A.charGlobal[0][0] = 20 + seed * 3;
		int fill = seed < 6 ? 120 * seed : 760;
		for (int k = 0; k < fill; k++) {
			float p[3] = {(float)k, 0, 0};
			aos_beam_new(&A, T, 7, p, 0.0f, 1, -(k % 4), 1);
		}
		B = A;
		for (int f = 0; f < 400; f++) {
			if (f == 200)
				A.charGlobal[0][0] = B.charGlobal[0][0] = 5;
			aos_beams_update(&A);
			ref_beams(&B);
			if (A.obj.nbeam != B.obj.nbeam || A.obj.overflow != B.obj.overflow ||
			    memcmp(A.obj.beam, B.obj.beam, sizeof A.obj.beam) || memcmp(&A.rng, &B.rng, sizeof A.rng)) {
				printf("FAIL beams seed %d frame %d: n %d vs %d\n", seed, f, A.obj.nbeam, B.obj.nbeam);
				fails++;
				break;
			}
		}
		capped += A.obj.overflow > 0;
	}
	printf("beam pool compaction: %s (%d/12 runs hit the cap)\n", fails ? "FAIL" : "ok", capped);
	return fails != 0 || capped == 0;
}

int main(void)
{
	aos_register_all();
	aos_otab.effectUpdate[T] = upd;
	aos_otab.beamUpdate[T] = bupd;
	if (beams())
		return 1;
	int fails = 0, maxn = 0, overflowed = 0;
	for (int seed = 0; seed < 12; seed++) {
		memset(&A, 0, sizeof A);
		aos_mt_seed(&A.rng, (uint32_t)seed);
		A.charGlobal[0][0] = 20 + seed * 3; /* death rate */
		int fill = seed < 6 ? 300 * seed : 2040;
		for (int k = 0; k < fill; k++) {
			float p[3] = {(float)k, 0, 0};
			aos_effect_new(&A, k % 5 == 0 ? 0 : T, 7, k % 3, p, 0.0f, 1, -(k % 4), 1);
		}
		B = A;
		for (int f = 0; f < 400; f++) {
			if (f == 200)
				A.charGlobal[0][0] = B.charGlobal[0][0] = 5; /* let it grow into the cap */
			aos_effects_update(&A);
			ref_update(&B);
			if (A.obj.neffect > maxn)
				maxn = A.obj.neffect;
			if (A.obj.neffect != B.obj.neffect || A.obj.overflow != B.obj.overflow ||
			    memcmp(A.obj.effect, B.obj.effect, sizeof A.obj.effect) || memcmp(&A.rng, &B.rng, sizeof A.rng)) {
				printf("FAIL seed %d frame %d: n %d vs %d, overflow %d vs %d\n", seed, f, A.obj.neffect, B.obj.neffect,
				       A.obj.overflow, B.obj.overflow);
				fails++;
				break;
			}
		}
		overflowed += A.obj.overflow > 0;
	}
	printf("effect pool compaction: %s (peak %d records, %d/12 runs hit the cap)\n", fails ? "FAIL" : "ok", maxn,
	       overflowed);
	return fails != 0 || overflowed == 0;
}
