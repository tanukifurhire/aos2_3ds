/* CPU-vs-CPU test for the Tsih / Mira / Iru AI (src/sim/chars/ai_{tsih,mira,iru}.c).
 * For each pairing and difficulty 0..2, both players are CPU (inputSrc 10) and
 * the match is run three times: twice from scratch (determinism) and once
 * restored from mid-match snapshots (rollback safety). Run under UBSan.
 * Usage: sim_ai3 [frames]   (build: tests/run_ai3.sh) */
#include "../src/sim/draw.h"
#include "../src/sim/sim.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	int hits[2];     /* frames entering hitstun */
	int states[2];   /* frames in a character-specific state (>6) */
	int rounds;
} stats;

static void next_round(aos_world *w, int *cd)
{
	if (w->roundOver && ++*cd > 300) {
		*cd = 0;
		if (w->p[0].roundsWon >= 2 || w->p[1].roundsWon >= 2)
			w->p[0].roundsWon = w->p[1].roundsWon = 0;
		aos_round_init(w);
	}
}

static bool sane(const aos_world *w)
{
	for (int i = 0; i < 2; i++)
		if (!isfinite(w->p[i].x) || !isfinite(w->p[i].y) || !isfinite(w->p[i].hyper) ||
		    !isfinite(w->p[i].heat))
			return false;
	return true;
}

/* Runs frames [from, to) from the given world. cd = round-change countdown (kept
 * outside the world here, so it is passed alongside snapshots). */
static int step(aos_world *w, int *cd, int from, int to, stats *st, int snapEvery, aos_world *snaps, int *snapCd)
{
	const uint16_t none[2] = {0, 0};
	for (int f = from; f < to; f++) {
		if (snaps && snapEvery && f % snapEvery == 0) {
			memcpy(&snaps[f / snapEvery], w, sizeof *w);
			snapCd[f / snapEvery] = *cd;
		}
		int prev[2] = {w->p[0].state, w->p[1].state};
		aos_tick(w, none);
		if (st)
			for (int q = 0; q < 2; q++) {
				if (w->p[q].state == AOS_ST_HITSTUN && prev[q] != AOS_ST_HITSTUN)
					st->hits[q]++;
				if (w->p[q].state > AOS_ST_KO)
					st->states[q]++;
			}
		aos_draw_begin();
		aos_draw_world(w);
		if (!sane(w)) {
			printf("FAIL: non-finite player state at frame %d\n", f);
			return 1;
		}
		int won = w->p[0].roundsWon + w->p[1].roundsWon;
		next_round(w, cd);
		if (st && w->p[0].roundsWon + w->p[1].roundsWon < won)
			st->rounds++;
	}
	return 0;
}

static void start(aos_world *w, int a, int b, int diff)
{
	int32_t ids[2] = {a, b}, sch[2] = {0, 0}, col[2] = {0, 1};
	aos_match_init(w, ids, sch, col, 180, 2);
	w->difficulty = diff;
	w->p[0].inputSrc = w->p[1].inputSrc = 10;
}

#define NSNAP 8
static aos_world A, B, R, snaps[NSNAP + 1];
static int snapCd[NSNAP + 1];

int main(int argc, char **argv)
{
	aos_register_all();
	int frames = argc > 1 ? atoi(argv[1]) : 12000;
	static const int mine[3] = {AOS_CH_TSIH, AOS_CH_MIRA, AOS_CH_IRU};
	static const int opp[] = {AOS_CH_TSIH, AOS_CH_MIRA, AOS_CH_IRU, AOS_CH_SUGURI, AOS_CH_SORA};
	int fail = 0;
	static aos_mt fresh;
	aos_mt_seed(&fresh, 5489);
	for (int i = 0; i < 3; i++)
		for (int d = 0; d < 3; d++)
			if (!aos_tab.cpu[mine[i]][d]) {
				printf("FAIL: no AI registered for id %d diff %d\n", mine[i], d);
				fail = 1;
			}
	for (int i = 0; i < 3; i++)
		for (size_t j = 0; j < sizeof opp / sizeof opp[0]; j++)
			for (int d = 0; d < 3; d++) {
				int a = mine[i], b = opp[j], f = 0, cdA = 0, cdB = 0;
				stats st = {0};
				start(&A, a, b, d);
				f |= step(&A, &cdA, 0, frames, &st, frames / NSNAP, snaps, snapCd);
				start(&B, a, b, d);
				f |= step(&B, &cdB, 0, frames, NULL, 0, NULL, NULL);
				if (memcmp(&A, &B, sizeof A)) {
					printf("FAIL %d vs %d diff %d: two identical runs diverged\n", a, b, d);
					f = 1;
				}
				for (int k = 1; k < NSNAP; k++) {
					int from = k * (frames / NSNAP), cd = snapCd[k];
					memcpy(&R, &snaps[k], sizeof R);
					f |= step(&R, &cd, from, frames, NULL, 0, NULL, NULL);
					if (memcmp(&R, &A, sizeof A)) {
						printf("FAIL %d vs %d diff %d: rollback from frame %d diverged\n", a, b, d, from);
						f = 1;
						break;
					}
				}
				printf("%2d vs %2d diff %d: hits taken %4d/%4d, special-state frames %5d/%5d, match resets %d, AI drew RNG %s: %s\n",
				       a, b, d, st.hits[0], st.hits[1], st.states[0], st.states[1], st.rounds,
				       memcmp(&A.cpuRng, &fresh, sizeof fresh) ? "yes" : "NO", f ? "FAIL" : "ok");
				if (st.states[0] == 0 && b != AOS_CH_SUGURI && b != AOS_CH_SORA)
					printf("  WARN: P1 CPU never left the common states\n");
				fail |= f;
			}
	printf("%s\n", fail ? "FAIL" : "all ok");
	return fail;
}
