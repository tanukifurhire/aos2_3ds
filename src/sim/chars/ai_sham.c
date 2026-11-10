/* Sham (engine id 7) CPU AI — table 0x881470: diff 0 FUN_0072f5a0,
 * diff 1/2 FUN_0072ee20 (registry.c:183-185). Each function fills the same
 * 16-bit button mask a human produces (core.md §2.1).
 *
 * Port notes:
 *  - Each original function keeps its mask in its own static short
 *    (DAT_009b68ac diff 0 / DAT_009b68a8 diff 1/2), shared by both players and
 *    persisting across rounds: w->cpuGlobal[ID][0] / [1]. Only bits 0..8 set.
 *  - FUN_00459f80 runs on the cosmetic stream -> aos_cpu_rand_int, same calls,
 *    same order (6 call sites in diff 0, 7 in diff 1/2). FUN_006b04e0 = aos_cpu_steer.
 *  - player +0x160 (movement mode: 1 = orbit the arena centre at radius 400,
 *    period 500, mirrored; 2 = radius-300 orbit minus the opponent position)
 *    lives in p->scratch[4..7] (ai_orbit.h).
 *  - DAT_0087cd7c = cost table 0x87c4f0 entry 0xb6 field +4 (gauge B);
 *    DAT_0087cd3c = entry 0xb1 field +0 (gauge A): aos_tab.cost.
 *  - `opp.state != 0 || opp.state != 2` (always true) is dropped.
 *  - FUN_0072a8a0 (static find_dwarf in sham.c) is reimplemented here.
 *  - All arithmetic is SSE single precision in the original. */
#include "ai_orbit.h"

#define ID AOS_CH_SHAM

static void ai_clear_dirs(int32_t *m)
{
	if (*m & 1) *m -= 1;
	if (*m & 2) *m -= 2;
	if (*m & 4) *m -= 4;
	if (*m & 8) *m -= 8;
}

/* FUN_0072a8a0(owner): index of the owner's dwarf (type 7, spawn id 200,
 * +0x7c == 200), or -1. */
static int ai_find_dwarf(aos_world *w, int32_t owner)
{
	for (int i = 0; i < w->obj.nenemy; i++) {
		const aos_enemy *e = &w->obj.enemy[i];
		if (e->ownerId == owner && e->type == 7 && e->f10 == 200 && e->f7c == 200)
			return i;
	}
	return -1;
}

static void ai_move(aos_world *w, aos_player *p, aos_player *o, int32_t *m, int lim)
{
	int32_t f = w->frame;
	float t[2];
	if (ai_mode(p) == 0)
		ai_set_mode(p, 1);
	if (ai_mode(p) == 1) {
		float a = ((float)(f % 500) / 500.0f) * AOS_2PI;
		t[0] = -aos_cos(a) * 400.0f;
		t[1] = -aos_sin(a) * 400.0f;
		ai_clear_dirs(m);
		aos_cpu_steer(m, p, t);
	}
	if (ai_mode(p) == 2) {
		float a = ((float)(f % 100) / 100.0f) * AOS_2PI;
		t[0] = aos_cos(a) * 300.0f - o->x; /* sic: minus the opponent position */
		t[1] = aos_sin(a) * 300.0f - o->y;
		if ((f + 0x14d) % 200 < lim) {
			ai_clear_dirs(m);
			aos_cpu_steer(m, p, t);
		}
	}
	if (f % 400 == 399 && aos_cpu_rand_int(w, 0, 100) < 0x28)
		ai_set_mode(p, ai_mode(p) == 1 ? 2 : 1);
}

/* Shared middle section; per = period of the dwarf-summon roll (0x78 / 0x3c),
 * rmax = range of the attack rolls (300 / 200). */
static void ai_mid(aos_world *w, aos_player *p, aos_player *o, float d, float r0, int32_t *m, int per, int rmax)
{
	int32_t f = w->frame;
	if (p->gaugeA < 0.4f && (*m & 0x10))
		*m -= 0x10;
	if ((*m & 0x40) && 500.0f < d)
		*m -= 0x40;
	/* original: (opp.state != 0 || opp.state != 2) && ... — always true, dropped */
	if (aos_tab.cost[0xb6].b <= p->gaugeB && f % per == 0x3b && 500.0f < d &&
	    ai_find_dwarf(w, p->objId) == -1 && aos_cpu_rand_int(w, 0, 200) < 0x32)
		*m = 0x60;
	if (p->state == 0xb1 && aos_tab.cost[0xb1].a <= p->gaugeA && p->timer == 0x10 &&
	    (o->state == 0 || o->state == 2 || o->state == 1))
		*m = 0x10;
	if (o->state != 0 && o->state != 2 && o->timer < 0x15 && o->barrierTime == 0 && (f / 10) % 3 == 1 &&
	    aos_cpu_rand_int(w, 0, rmax) < 10) {
		if (ai_mode(p) != 2 || 400.0f <= d || d <= 200.0f)
			*m = 300.0f <= r0 ? 0x100 : 0x140;
		else
			*m = 0x140;
	}
	if (o->state != 0 && o->state != 2 && o->barrierTime == 0 && d < 200.0f &&
	    (aos_cpu_rand_int(w, 0, rmax) < 10 || o->state == 1)) {
		if (0x1d < f % 300 || p->hyper < 1.0f || o->state == 1)
			*m = 0x50;
		else
			*m = 0x180;
	}
}

static void ai_diff0(aos_world *w, aos_player *p, uint16_t *out) /* FUN_0072f5a0 */
{
	int32_t *m = &w->cpuGlobal[ID][0];
	if (w->frame % 0xf == 0)
		*m = aos_cpu_rand_int(w, 0, 0xff);
	aos_player *o = aos_opponent(w, p);
	float d = aos_dist(w, p);
	float r0 = aos_sqrt(p->x * p->x + p->y * p->y);
	ai_move(w, p, o, m, 100);
	ai_mid(w, p, o, d, r0, m, 0x78, 300);
	if (p->state == 1 && (*m & 0x20))
		*m -= 0x20;
	ai_hitstun_escape(w, p, o, m);
	*out = (uint16_t)*m;
}

static void ai_diff12(aos_world *w, aos_player *p, uint16_t *out) /* FUN_0072ee20 */
{
	int32_t *m = &w->cpuGlobal[ID][1];
	int32_t f = w->frame;
	if (f % 10 == 0)
		*m = aos_cpu_rand_int(w, 0, 0xff);
	aos_player *o = aos_opponent(w, p);
	float d = aos_dist(w, p);
	float r0 = aos_sqrt(p->x * p->x + p->y * p->y);
	ai_move(w, p, o, m, 0x96);
	ai_mid(w, p, o, d, r0, m, 0x3c, 200);
	if (1 < w->difficulty && 3.0f <= p->hyper && o->state == 1 && (aos_cpu_rand_int(w, 0, 100) + f) % 0x32 < 5)
		*m = 0x160;
	if (p->state == 1 && (*m & 0x20) && (p->heat <= 1.25f || p->maxHp / 3 < p->hp))
		*m -= 0x20;
	ai_hitstun_escape(w, p, o, m);
	*out = (uint16_t)*m;
}

void aos_register_ai_sham(void)
{
	aos_tab.cpu[ID][0] = ai_diff0;  /* 0x881470 FUN_0072f5a0 */
	aos_tab.cpu[ID][1] = ai_diff12; /* 0x881474 FUN_0072ee20 */
	aos_tab.cpu[ID][2] = ai_diff12; /* 0x881478 FUN_0072ee20 */
}
