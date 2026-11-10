/* Simulation smoke test: random-input matches for a character pairing.
 * Checks: no crash (run under UBSan), determinism (two identical runs give
 * byte-identical worlds), rollback safety (snapshot / restore / replay gives
 * the same world), pool overflow and NaN positions. Also runs the draw
 * handlers every frame so draw code is exercised.
 * Usage: sim_smoke <char> [opponent] [frames] [cpu difficulty 0..2: both players CPU] */
#include "../src/sim/draw.h"
#include "../src/sim/sim.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct { const char *name; int id; } chars[] = {
	{"sora", 3}, {"alte", 4}, {"tsih", 5}, {"mira", 6}, {"sham", 7}, {"nath", 8},
	{"starbreaker", 9}, {"suguri", 10}, {"saki", 11}, {"iru", 12}, {"nanako", 13},
	{"kae", 14}, {"kyoko", 15}, {"hime", 16}, {"sumika", 17}, {"sumika2", 18},
};

static int char_id(const char *n)
{
	for (size_t i = 0; i < sizeof chars / sizeof chars[0]; i++)
		if (!strcmp(chars[i].name, n))
			return chars[i].id;
	return atoi(n);
}

static uint32_t lcg(uint32_t *s) { return *s = *s * 1664525u + 1013904223u; }

/* Random but "plausible" input: buttons held for a few frames at a time. */
static uint16_t next_mask(uint32_t *s, uint16_t prev)
{
	uint32_t r = lcg(s) >> 8;
	uint16_t m = prev;
	if ((r & 7) == 0)
		m ^= (uint16_t)(1u << ((r >> 3) % 10)); /* bits 0..9: dirs, A, B, S, D, H, Shield */
	if ((r & 0x3f0) == 0)
		m = 0;
	return m;
}

static bool sane(const aos_world *w)
{
	for (int i = 0; i < 2; i++)
		if (!isfinite(w->p[i].x) || !isfinite(w->p[i].y) || !isfinite(w->p[i].speed) ||
		    !isfinite(w->p[i].heat) || !isfinite(w->p[i].hyper))
			return false;
	return true;
}

static int g_minhp[2] = {1 << 30, 1 << 30}, g_kos[2];
static int g_cpu = -1; /* >= 0: CPU vs CPU at this difficulty (inputSrc 10) */

static int run(aos_world *w, int a, int b, int frames, uint32_t seed, int snap_at, aos_world *snap)
{
	int32_t ids[2] = {a, b}, sch[2] = {0, 0}, col[2] = {0, 1};
	aos_match_init(w, ids, sch, col, 180, 2);
	if (g_cpu >= 0) {
		w->difficulty = g_cpu;
		w->p[0].inputSrc = w->p[1].inputSrc = 10;
	}
	uint16_t m[2] = {0, 0};
	uint32_t s = seed;
	for (int f = 0; f < frames; f++) {
		if (f == snap_at && snap)
			memcpy(snap, w, sizeof *w);
		m[0] = next_mask(&s, m[0]);
		m[1] = next_mask(&s, m[1]);
		aos_tick(w, m);
		for (int q = 0; q < 2; q++) {
			if (w->p[q].hp < g_minhp[q]) g_minhp[q] = w->p[q].hp;
			if (w->p[q].state == AOS_ST_KO && w->p[q].timer == 1) g_kos[q]++;
		}
		aos_draw_begin();
		aos_draw_world(w);
		if (!sane(w)) {
			printf("FAIL: non-finite player state at frame %d\n", f);
			return 1;
		}
		if (w->roundOver && ++w->charGlobal[0][0] > 300 - 255 && ++w->charGlobal[0][1] > 255) { w->charGlobal[0][0] = w->charGlobal[0][1] = 0; /* next round after the KO/timeout */
			if (w->p[0].roundsWon >= 2 || w->p[1].roundsWon >= 2) {
				w->p[0].roundsWon = w->p[1].roundsWon = 0;
			}
			aos_round_init(w);
		}
	}
	return 0;
}

static aos_world A, B, S;

int main(int argc, char **argv)
{
	aos_register_all();
	int a = argc > 1 ? char_id(argv[1]) : 10;
	int b = argc > 2 ? char_id(argv[2]) : a;
	int frames = argc > 3 ? atoi(argv[3]) : 20000;
	g_cpu = argc > 4 ? atoi(argv[4]) : -1;
	int fail = 0;
	if (!aos_tab.inputA[a])
		printf("note: character %d has no input function registered (not ported yet)\n", a);
	fail |= run(&A, a, b, frames, 12345, frames / 2, &S);
	fail |= run(&B, a, b, frames, 12345, -1, NULL);
	if (memcmp(&A, &B, sizeof A)) {
		printf("FAIL: two identical runs diverged (hidden state outside aos_world?)\n");
		fail = 1;
	}
	/* rollback: restore the mid-match snapshot and replay the rest */
	{
		uint32_t s = 12345;
		uint16_t m[2] = {0, 0};
		for (int f = 0; f < frames / 2; f++) {
			m[0] = next_mask(&s, m[0]);
			m[1] = next_mask(&s, m[1]);
		}
		aos_world *w = &S;
		for (int f = frames / 2; f < frames; f++) {
			m[0] = next_mask(&s, m[0]);
			m[1] = next_mask(&s, m[1]);
			aos_tick(w, m);
			aos_draw_begin();
			aos_draw_world(w);
			if (w->roundOver && ++w->charGlobal[0][0] > 300 - 255 && ++w->charGlobal[0][1] > 255) { w->charGlobal[0][0] = w->charGlobal[0][1] = 0;
				if (w->p[0].roundsWon >= 2 || w->p[1].roundsWon >= 2)
					w->p[0].roundsWon = w->p[1].roundsWon = 0;
				aos_round_init(w);
			}
		}
		if (memcmp(&S, &A, sizeof A)) {
			printf("FAIL: snapshot/restore replay diverged (state outside aos_world?)\n");
			fail = 1;
		}
	}
	printf("min hp P1 %d P2 %d, KOs P1 %d P2 %d (both runs)\n", g_minhp[0], g_minhp[1], g_kos[0], g_kos[1]);
	if (A.obj.overflow)
		printf("WARN: %d spawns dropped (pool full)\n", A.obj.overflow);
	printf("%s vs %s: %d frames, P1 %d hp / P2 %d hp, rounds %d-%d, objects b%d m%d l%d e%d fx%d, draws %d: %s\n",
	       argc > 1 ? argv[1] : "suguri", argc > 2 ? argv[2] : (argc > 1 ? argv[1] : "suguri"), frames,
	       A.p[0].hp, A.p[1].hp, A.p[0].roundsWon, A.p[1].roundsWon, A.obj.nbeam, A.obj.nmissile,
	       A.obj.nlaser, A.obj.nenemy, A.obj.neffect, aos_dl.n, fail ? "FAIL" : "ok");
	return fail;
}
