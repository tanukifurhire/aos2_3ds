/* Kyoko (engine id 15) CPU AI — table 0x8814f0: diff 0 FUN_006edbe0,
 * diff 1/2 FUN_006ed350. Each function fills the same 16-bit button mask a
 * human produces (core.md §2.1).
 *
 * Port notes:
 *  - Each original function keeps its mask in its own static short (Kyoko DAT_009b6884 / DAT_009b6880)
 *    (shared by both players, persisting across rounds): w->cpuGlobal[ID][0]
 *    (diff 0) / [1] (diff 1/2). Only bits 0..8 are ever set.
 *  - FUN_00459f80 runs on the cosmetic stream while input is sampled
 *    (core.md §1, sim.h cpuRng) -> aos_cpu_rand_int, same calls, same order.
 *    FUN_006b04e0 = aos_cpu_steer.
 *  - player +0x160 (movement mode: 1 = orbit the arena at radius 480,
 *    2 = circle a point mirrored from the opponent; 0 -> 1 on first call)
 *    lives in p->scratch[4..7]. The "+0x160 == 0" branches are therefore
 *    dead code in the original too (kept literally, incl. their RNG draw).
 *  - DAT_0087db68 / DAT_0087dbb0 = cost table 0x87c4f0 entries 0x1df / 0x1e5,
 *    field +4 (gauge B cost), read from aos_tab.cost.
 *  - `opp.state != 0 || opp.state != 2` (always true) is dropped.
 *  - Diff 1/2 contains a no-op block (state 0x1dd, timer 10, gaugeA >= 0.75,
 *    (frame/10)%4 == 3 -> mask = mask) that is omitted.
 *  - All arithmetic is SSE single precision in the original. */
#include <string.h>

#include "../sim.h"

#define ID AOS_CH_KYOKO

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

/* FUN_006e9000(side): number of Kyoko enemies (type 15, spawn id 100) of `side`. */
static int ai_count_units(aos_world *w, int32_t side)
{
	int n = 0;
	for (int i = 0; i < w->obj.nenemy; i++) {
		const aos_enemy *e = &w->obj.enemy[i];
		if (e->team == side && e->type == 0xf && e->f10 == 100)
			n++;
	}
	return n;
}

static void ai_move(aos_world *w, aos_player *p, aos_player *o, int32_t *m, int lim)
{
	int32_t f = w->frame;
	float t[2];
	if (ai_mode(p) == 0)
		ai_set_mode(p, 1);
	if (ai_mode(p) == 1) {
		float a = ((float)(f % 600) / 600.0f) * AOS_2PI;
		t[0] = aos_cos(a) * 480.0f;
		t[1] = aos_sin(a) * 480.0f;
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
	if (f % 400 == 399 && aos_cpu_rand_int(w, 0, 100) < 0x28)
		ai_set_mode(p, ai_mode(p) == 1 ? 2 : 1);
}

static void ai_common_mid(aos_world *w, aos_player *p, float d, int32_t *m)
{
	if (p->gaugeA < 0.4f && (*m & 0x10))
		*m -= 0x10;
	if ((*m & 0x40) && 400.0f < d)
		*m -= 0x40;
	if ((p->state == 0x1d1 || p->state == 0x1d2) && d < 600.0f && ai_count_units(w, p->side) == 0 &&
	    (*m & 0x80) && (*m & 0x10))
		*m -= 0x80;
}

static void ai_specials(aos_world *w, aos_player *p, aos_player *o, float r0, int32_t *m, int per1, int per0)
{
	int32_t f = w->frame;
	if (o->state != 1 && ai_mode(p) == 1 && f % per1 == 0x27 && aos_cpu_rand_int(w, 0, 200) < 0x32) {
		if (300.0f <= r0)
			*m = 0x120;
		else
			*m = 0x180;
	}
	if (o->state != 1 && ai_mode(p) == 0 && f % per0 == 0x27 && aos_cpu_rand_int(w, 0, 200) < 0x32)
		*m = 0x100;
}

static void ai_b_moves(aos_world *w, aos_player *p, aos_player *o, float d, int32_t *m)
{
	if (p->state == 0x1df && p->timer < 0x3c && (o->state == 0 || o->state == 2 || o->state == 1) &&
	    aos_tab.cost[0x1df].b < p->gaugeB)
		*m = 0x20;
	/* original: (opp.state != 0 || opp.state != 2) && ... — always true, dropped */
	if (ai_mode(p) == 2 && aos_tab.cost[0x1e5].b <= p->gaugeB &&
	    w->frame % 0x3c == 0 && 500.0f < d && aos_cpu_rand_int(w, 0, 200) < 0x32)
		*m = 0x60;
}

static void ai_close(aos_world *w, aos_player *p, aos_player *o, float d, int32_t *m, int rmax, int period)
{
	if (o->state != 0 && o->barrierTime == 0 && d < 200.0f &&
	    (aos_cpu_rand_int(w, 0, rmax) < 10 || o->state == 1)) {
		if (0x1d < w->frame % period || p->hyper < 1.0f || o->state == 1)
			*m = 0x50;
		else
			*m = 0x140;
	}
}

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

static void ai_diff0(aos_world *w, aos_player *p, uint16_t *out) /* FUN_006edbe0 */
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
	float r0 = aos_sqrt(p->x * p->x + p->y * p->y);
	ai_move(w, p, o, m, 100);
	ai_common_mid(w, p, d, m);
	ai_specials(w, p, o, r0, m, 0x96, 0xb4);
	ai_b_moves(w, p, o, d, m);
	ai_close(w, p, o, d, m, 300, 400);
	if (p->state == 1 && (*m & 0x20))
		*m -= 0x20;
	ai_burst_roll(w, p, o, m);
	*out = (uint16_t)*m;
}

static void ai_diff12(aos_world *w, aos_player *p, uint16_t *out) /* FUN_006ed350 */
{
	int32_t *m = &w->cpuGlobal[ID][1];
	int32_t f = w->frame;
	if (f % 10 == 0)
		*m = aos_cpu_rand_int(w, 0, 0xff);
	aos_player *o = aos_opponent(w, p);
	float d = aos_dist(w, p);
	float r0 = aos_sqrt(p->x * p->x + p->y * p->y);
	ai_move(w, p, o, m, 0x96);
	ai_common_mid(w, p, d, m);
	ai_specials(w, p, o, r0, m, 0x50, 0x5a);
	if (p->state != 0 && p->state != 2 && 0x13 < p->timer && p->timer % 4 == 0 &&
	    (p->heat < 1.0f || aos_cpu_rand_int(w, 0, 100) < 10 || o->state == 1))
		*m |= 0x80;
	ai_b_moves(w, p, o, d, m);
	ai_close(w, p, o, d, m, 200, 300);
	if (1 < w->difficulty && 3.0f <= p->hyper && o->state == 1 && (aos_cpu_rand_int(w, 0, 100) + f) % 0x32 < 5)
		*m = 0x160;
	if (p->state == 1 && (*m & 0x20) && (p->heat <= 1.25f || p->maxHp / 3 < p->hp))
		*m -= 0x20;
	ai_burst_roll(w, p, o, m);
	*out = (uint16_t)*m;
}

void aos_register_ai_kyoko(void)
{
	aos_tab.cpu[ID][0] = ai_diff0;  /* 0x8814f0 FUN_006edbe0 */
	aos_tab.cpu[ID][1] = ai_diff12; /* 0x8814f4 FUN_006ed350 */
	aos_tab.cpu[ID][2] = ai_diff12; /* 0x8814f8 FUN_006ed350 */
}
