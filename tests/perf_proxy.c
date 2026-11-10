/* Host-side, machine-independent cost proxy for the 3DS (built with -DAOS_PERF).
 * Runs CPU vs CPU matches (difficulty 2; "hyper" scenarios pin both hyper
 * gauges at 3.0 so the AIs fire hypers as often as they can) and reports per
 * tick: live objects, collision pairs tested (aos_shape_hit calls / full
 * evaluations), libm trig calls, bytes moved by pool erases, texture-name
 * string compares, draw commands and the bytes a compact rollback snapshot
 * copies. avg = mean over the run, max = worst single tick.
 * Usage: perf_proxy [frames] [a b mode]...   (mode 1 = CPU, 2 = CPU + hyper pinned) */
#include "../src/sim/draw.h"
#include "../src/sim/perf.h"
#include "../src/sim/sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

aos_perf_counters aos_perf;

static double now_us(void)
{
	struct timespec ts;
	timespec_get(&ts, TIME_UTC);
	return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1e3;
}

static size_t live_bytes(const aos_world *w)
{
	const aos_objects *o = &w->obj;
	return sizeof *w - sizeof w->obj.beam - sizeof w->obj.missile - sizeof w->obj.hitbox - sizeof w->obj.laser -
	       sizeof w->obj.enemy - sizeof w->obj.effect - sizeof w->obj.whip - sizeof w->obj.particle - sizeof w->obj.spark -
	       sizeof w->obj.popup + (size_t)o->nbeam * sizeof o->beam[0] + (size_t)o->nmissile * sizeof o->missile[0] +
	       (size_t)o->nhitbox * sizeof o->hitbox[0] + (size_t)o->nlaser * sizeof o->laser[0] +
	       (size_t)o->nenemy * sizeof o->enemy[0] + (size_t)o->neffect * sizeof o->effect[0] +
	       (size_t)o->nwhip * sizeof o->whip[0] + (size_t)o->nparticle * sizeof o->particle[0] +
	       (size_t)o->nspark * sizeof o->spark[0] + (size_t)o->npopup * sizeof o->popup[0];
}

enum { M_OBJ, M_PAIRS, M_FULL, M_TRIG, M_TMISS, M_ERASE, M_TEXCMP, M_DCMD, M_SNAP, M_TICKUS, M_DRAWUS, NM };
static const char *const mname[NM] = {"objects", "pairs", "pairsFull", "trig", "trigMiss", "eraseB", "texCmp", "dcmds", "snapB", "tickUs", "drawUs"};

static aos_world W;
static double gmax[NM];

static void run(int a, int b, int mode, int frames)
{
	int32_t ids[2] = {a, b}, sch[2] = {0, 0}, col[2] = {0, a == b ? 1 : 0};
	aos_match_init(&W, ids, sch, col, 180, 2);
	W.difficulty = 2;
	W.p[0].inputSrc = W.p[1].inputSrc = 10;
	double sum[NM] = {0}, mx[NM] = {0};
	uint16_t m[2] = {0, 0};
	for (int f = 0; f < frames; f++) {
		if (mode == 2)
			W.p[0].hyper = W.p[1].hyper = 3.0f;
		aos_perf_counters c0 = aos_perf;
		double t0 = now_us();
		aos_tick(&W, m);
		double t1 = now_us();
		aos_perf_counters c1 = aos_perf;
		aos_draw_begin();
		aos_draw_world(&W);
		double t2 = now_us();
		if (W.flow.matchEnd)
			aos_match_restart(&W);
		const aos_objects *o = &W.obj;
		double v[NM];
		v[M_OBJ] = o->nbeam + o->nmissile + o->nlaser + o->nenemy + o->neffect + o->nwhip + o->nparticle + o->nspark;
		v[M_PAIRS] = (double)(c1.shape_calls - c0.shape_calls);
		v[M_FULL] = (double)(c1.shape_full - c0.shape_full);
		v[M_TRIG] = (double)(aos_perf.trig - c0.trig);
		v[M_TMISS] = (double)(aos_perf.trig_miss - c0.trig_miss);
		v[M_ERASE] = (double)(c1.erase_bytes - c0.erase_bytes);
		v[M_TEXCMP] = (double)(aos_perf.tex_cmp - c1.tex_cmp);
		v[M_DCMD] = aos_dl.n;
		v[M_SNAP] = (double)live_bytes(&W);
		v[M_TICKUS] = t1 - t0;
		v[M_DRAWUS] = t2 - t1;
		for (int k = 0; k < NM; k++) {
			sum[k] += v[k];
			if (v[k] > mx[k])
				mx[k] = v[k];
			if (v[k] > gmax[k])
				gmax[k] = v[k];
		}
	}
	printf("%2d %2d %s", a, b, mode == 2 ? "hyp" : "cpu");
	for (int k = 0; k < NM; k++)
		printf(" %9.0f/%-9.0f", sum[k] / frames, mx[k]);
	printf("\n");
	fflush(stdout);
}

int main(int argc, char **argv)
{
	aos_register_all();
	int frames = argc > 1 ? atoi(argv[1]) : 3600;
	printf("a  b  mode");
	for (int k = 0; k < NM; k++)
		printf(" %19s", mname[k]);
	printf("\n          (each column: avg/max per tick)\n");
	if (argc > 4) {
		for (int i = 2; i + 2 < argc; i += 3)
			run(atoi(argv[i]), atoi(argv[i + 1]), atoi(argv[i + 2]), frames);
	} else {
		for (int a = 3; a <= 18; a++)
			run(a, a, 2, frames);
		for (int a = 3; a <= 18; a++)
			run(a, a == 18 ? 3 : a + 1, 1, frames);
	}
	printf("worst tick over all runs:");
	for (int k = 0; k < NM; k++)
		printf(" %s %.0f", mname[k], gmax[k]);
	printf("\n");
	return 0;
}
