/* Nanako (engine id 13) CPU AI — table 0x8814d0: diff 0 FUN_007083a0,
 * diff 1/2 FUN_00707c60. Each function fills the same 16-bit button mask a
 * human produces (core.md §2.1).
 *
 * Port notes:
 *  - Each original function keeps its mask in its own static short
 *    (DAT_009b6894 diff 0, DAT_009b6890 diff 1/2) shared by both players and
 *    persisting across rounds: w->cpuGlobal[ID][0] / [1]. Only bits 0..8 are
 *    ever set, so int32 storage is equivalent.
 *  - FUN_00459f80 runs on the cosmetic stream while input is sampled
 *    (core.md §1, sim.h cpuRng) -> aos_cpu_rand_int, same calls, same order.
 *    FUN_006b04e0 = aos_cpu_steer.
 *  - player +0x160 (movement mode 1 = orbit the arena, 2 = circle around a
 *    point mirrored from the opponent) lives in p->scratch[4..7].
 *  - All arithmetic is SSE single precision in the original (checked in the
 *    disassembly), so plain float C reproduces it. */
#include <string.h>

#include "../sim.h"

#define ID AOS_CH_NANAKO

static int32_t ai_mode(const aos_player *p) /* +0x160 */
{
	int32_t v;
	memcpy(&v, &p->scratch[0x160 - 0x15c], sizeof v);
	return v;
}

static void ai_set_mode(aos_player *p, int32_t v)
{
	memcpy(&p->scratch[0x160 - 0x15c], &v, sizeof v);
}

static void ai_clear_dirs(int32_t *m)
{
	if (*m & 1) *m -= 1;
	if (*m & 2) *m -= 2;
	if (*m & 4) *m -= 4;
	if (*m & 8) *m -= 8;
}

/* Shared tail "state 1 (hitstun), timer 15: maybe burst" with the RNG draw. */
static void ai_burst_roll(aos_world *w, aos_player *p, aos_player *o, int32_t *m)
{
	if (p->state == 1 && p->timer == 0xe)
		*m = 0;
	if (p->state == 1 && p->timer == 0xf) {
		int r = aos_cpu_rand_int(w, 0, 100);
		if ((0x5dc < o->comboShowDmg && 0x32 < r) || (1000 < o->comboShowDmg && 0x46 < r) || 0x5a < r)
			*m |= 0x80;
	}
}

/* Movement block shared by both difficulties (orbit / circle). `lim` is the
 * (frame+333)%200 window for mode 2 (100 diff 0, 150 diff 1/2). */
static void ai_move(aos_world *w, aos_player *p, aos_player *o, int32_t *m, int lim)
{
	int32_t f = w->frame;
	float t[2];
	if (ai_mode(p) == 0)
		ai_set_mode(p, 1);
	if (ai_mode(p) == 1) {
		float a = ((float)(f % 400) / 400.0f) * AOS_2PI;
		t[0] = -aos_cos(a) * 360.0f;
		t[1] = -aos_sin(a) * 360.0f;
		ai_clear_dirs(m);
		aos_cpu_steer(m, p, t);
	}
	if (ai_mode(p) == 2) {
		float a = ((float)(f % 100) / 100.0f) * AOS_2PI;
		t[0] = aos_cos(a) * 300.0f - o->x;
		t[1] = aos_sin(a) * 300.0f - o->y;
		if ((f + 0x14d) % 200 < lim) {
			ai_clear_dirs(m);
			aos_cpu_steer(m, p, t);
		}
	}
	if (f % 400 == 399) {
		int r = aos_cpu_rand_int(w, 0, 100);
		if (r < 0x28)
			ai_set_mode(p, ai_mode(p) == 1 ? 2 : 1);
	}
}

static void ai_diff0(aos_world *w, aos_player *p, uint16_t *out) /* FUN_007083a0 */
{
	int32_t *m = &w->cpuGlobal[ID][0];
	int32_t f = w->frame;
	if (f % 0xf == 0)
		*m = aos_cpu_rand_int(w, 0, 0xff);
	if (0x46 < f % 0xaa) {
		if (*m & 0x10) *m -= 0x10;
		if (*m & 0x20) *m -= 0x20;
	}
	aos_player *o = aos_opponent(w, p);
	float d = aos_dist(w, p);
	/* FUN_00458320(x*x + y*y): result unused (pure) */
	ai_move(w, p, o, m, 100);
	if ((*m & 0x40) && ((*m & 0x10) || (*m & 0x20)) && 400.0f < d)
		*m -= 0x40;
	if (p->gaugeA < 0.4f && (*m & 0x10))
		*m -= 0x10;
	if (p->gaugeB < 0.4f && (*m & 0x20))
		*m -= 0x20;
	if (p->state == 0x19c && p->timer == 10 && (f / 10) % 4 != 1)
		*m = 0;
	if (p->state == 0x199 && p->timer == 0xc && (f / 10) % 3 != 1)
		*m = 0;
	if (o->state != 1 && f % 0xa0 == 0x27) {
		int r = aos_cpu_rand_int(w, 0, 200);
		if (r < 0x32)
			*m = ai_mode(p) == 1 ? 0x140 : 0x100;
	}
	if (o->state != 0 && o->barrierTime == 0 && d < 200.0f) {
		int r = aos_cpu_rand_int(w, 0, 300);
		if (r < 10 || o->state == 1) {
			if (0x1d < f % 400 || p->hyper < 1.0f || o->state == 1)
				*m = 0x60;
			else
				*m = 0x120;
		}
	}
	if (o->state == 1 && o->barrierTime == 0 && 400.0f < d && 1.0f < p->hyper && f % 0x5a == 1)
		*m = 0x180;
	if (p->state == 1 && (*m & 0x20))
		*m -= 0x20;
	ai_burst_roll(w, p, o, m);
	*out = (uint16_t)*m;
}

static void ai_diff12(aos_world *w, aos_player *p, uint16_t *out) /* FUN_00707c60 */
{
	int32_t *m = &w->cpuGlobal[ID][1];
	int32_t f = w->frame;
	if (f % 10 == 0)
		*m = aos_cpu_rand_int(w, 0, 0xff);
	aos_player *o = aos_opponent(w, p);
	float d = aos_dist(w, p);
	ai_move(w, p, o, m, 0x96);
	if ((*m & 0x40) && ((*m & 0x10) || (*m & 0x20)) && 400.0f < d)
		*m -= 0x40;
	if (p->gaugeA < 0.4f && (*m & 0x10))
		*m -= 0x10;
	if (p->gaugeB < 0.4f && (*m & 0x20))
		*m -= 0x20;
	if (p->state == 0x19c && p->timer == 10 && (f / 10) % 4 != 1)
		*m = 0;
	if (p->state == 0x199 && p->timer == 0xc && (f / 10) % 3 != 1)
		*m = 0;
	if (o->state != 1 && f % 0x50 == 0x27) {
		int r = aos_cpu_rand_int(w, 0, 200);
		if (r < 0x32)
			*m = ai_mode(p) == 1 ? 0x140 : 0x100;
	}
	if (o->state != 0 && o->barrierTime == 0 && d < 200.0f) {
		int r = aos_cpu_rand_int(w, 0, 200);
		if (r < 10 || o->state == 1) {
			if (0x1d < f % 300 || p->hyper < 1.0f || o->state == 1)
				*m = 0x60;
			else
				*m = 0x120;
		}
	}
	if (o->state == 1 && o->barrierTime == 0 && 400.0f < d && 1.0f < p->hyper && f % 0x3c == 1)
		*m = 0x180;
	if (1 < w->difficulty && 3.0f <= p->hyper && o->state == 1 && d < 300.0f) {
		int r = aos_cpu_rand_int(w, 0, 100);
		if ((r + f) % 0x32 < 10)
			*m = 0x160;
	}
	if (p->state == 1 && (*m & 0x20) && (p->heat <= 1.25f || p->maxHp / 3 < p->hp))
		*m -= 0x20;
	ai_burst_roll(w, p, o, m);
	*out = (uint16_t)*m;
}

void aos_register_ai_nanako(void)
{
	aos_tab.cpu[ID][0] = ai_diff0;  /* 0x8814d0 FUN_007083a0 */
	aos_tab.cpu[ID][1] = ai_diff12; /* 0x8814d4 FUN_00707c60 */
	aos_tab.cpu[ID][2] = ai_diff12; /* 0x8814d8 FUN_00707c60 */
}
