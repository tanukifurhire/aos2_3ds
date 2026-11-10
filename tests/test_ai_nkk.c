/* CPU-vs-CPU check for the Nanako / Kae / Kyoko AIs (src/sim/chars/ai_nanako.c,
 * ai_kae.c, ai_kyoko.c). For every pairing of the three characters and each
 * difficulty 0..2: both players are CPU (inputSrc 10), the match runs with
 * zero external input; checks determinism (two runs byte-identical) and
 * rollback safety (snapshot at mid-match, restore, replay -> identical), plus
 * that the AI actually acts (buttons pressed, damage dealt).
 * Build: same object set as tests/run_sim.sh with this file instead of sim_smoke.c. */
#include "../src/sim/draw.h"
#include "../src/sim/sim.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	aos_world w;
	int gap; /* frames since round over (test-side round restart) */
} sim;

static long g_btn[2], g_kos[2];
static int g_minhp[2];

static void step(sim *s, bool stats)
{
	const uint16_t none[2] = {0, 0};
	aos_tick(&s->w, none);
	aos_draw_begin();
	aos_draw_world(&s->w);
	if (stats)
		for (int q = 0; q < 2; q++) {
			const aos_player *p = &s->w.p[q];
			if (p->A == 3 || p->B == 3 || p->S == 3 || p->D == 3 || p->H == 3)
				g_btn[q]++;
			if (p->hp < g_minhp[q])
				g_minhp[q] = p->hp;
			if (p->state == AOS_ST_KO && p->timer == 1)
				g_kos[q]++;
		}
	if (s->w.roundOver && ++s->gap > 300) {
		s->gap = 0;
		if (s->w.p[0].roundsWon >= 2 || s->w.p[1].roundsWon >= 2)
			s->w.p[0].roundsWon = s->w.p[1].roundsWon = 0;
		aos_round_init(&s->w);
	}
}

static void start(sim *s, int a, int b, int diff)
{
	int32_t ids[2] = {a, b}, sch[2] = {0, 0}, col[2] = {0, 1};
	memset(s, 0, sizeof *s);
	aos_match_init(&s->w, ids, sch, col, 99, 2);
	s->w.difficulty = diff;
	s->w.p[0].inputSrc = s->w.p[1].inputSrc = 10;
}

static sim A, B, S;

int main(int argc, char **argv)
{
	static const int ids[3] = {AOS_CH_NANAKO, AOS_CH_KAE, AOS_CH_KYOKO};
	static const char *nm[3] = {"nanako", "kae", "kyoko"};
	int frames = argc > 1 ? atoi(argv[1]) : 12000;
	int fail = 0;
	aos_register_all();
	for (int i = 0; i < 3; i++)
		for (int d = 0; d < 3; d++)
			if (!aos_tab.cpu[ids[i]][d]) {
				printf("FAIL: %s diff %d has no CPU function\n", nm[i], d);
				fail = 1;
			}
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			for (int d = 0; d < 3; d++) {
				g_btn[0] = g_btn[1] = g_kos[0] = g_kos[1] = 0;
				g_minhp[0] = g_minhp[1] = 1 << 30;
				start(&A, ids[i], ids[j], d);
				start(&B, ids[i], ids[j], d);
				for (int f = 0; f < frames; f++) {
					if (f == frames / 2)
						S = A;
					step(&A, true);
					step(&B, false);
				}
				for (int f = frames / 2; f < frames; f++)
					step(&S, false);
				bool det = !memcmp(&A, &B, sizeof A);
				bool rb = !memcmp(&A, &S, sizeof A);
				bool sane = isfinite(A.w.p[0].x) && isfinite(A.w.p[1].x);
				bool acts = g_btn[0] > 0 && g_btn[1] > 0 && (g_minhp[0] < 4500 || g_minhp[1] < 4500);
				printf("%-6s vs %-6s diff %d: presses %5ld/%5ld minhp %5d/%5d KOs %ld/%ld  det %s rollback %s%s\n",
				       nm[i], nm[j], d, g_btn[0], g_btn[1], g_minhp[0], g_minhp[1], g_kos[0], g_kos[1],
				       det ? "ok" : "FAIL", rb ? "ok" : "FAIL", acts ? "" : "  (AI idle?)");
				if (!det || !rb || !sane || !acts)
					fail = 1;
			}
	printf("%s\n", fail ? "FAIL" : "ok");
	return fail;
}
