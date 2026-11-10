/* Kae (engine id 14) CPU AI — table 0x8814e0: diff 0 FUN_006e1970,
 * diff 1/2 FUN_006e0ef0. Each function fills the same 16-bit button mask a
 * human produces (core.md §2.1).
 *
 * Port notes:
 *  - Each original function keeps its mask in its own static short (Kae DAT_009b687c / DAT_009b6878)
 *    (shared by both players, persisting across rounds): w->cpuGlobal[ID][0]
 *    (diff 0) / [1] (diff 1/2). Only bits 0..8 are ever set.
 *  - FUN_00459f80 runs on the cosmetic stream while input is sampled
 *    (core.md §1, sim.h cpuRng) -> aos_cpu_rand_int, same calls, same order.
 *    FUN_006b04e0 = aos_cpu_steer.
 *  - player +0x160 (movement mode: 1 = circle the opponent at distance+100,
 *    2 = circle a point mirrored from the opponent; 0 -> 2 on first call)
 *    lives in p->scratch[4..7].
 *  - +0xaa = statusOn[7] (overheat).
 *  - DAT_0087d988 = cost table 0x87c4f0 entry 0x1b7, field +4 (gauge B cost of
 *    state 0x1b7), read from aos_tab.cost (runtime-filled, version-patched).
 *  - All arithmetic is SSE single precision in the original. */
#include <string.h>

#include "../sim.h"

#define ID AOS_CH_KAE

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

/* FUN_006b0570: false if the target lies on the side p is not facing. */
static bool ai_facing_target(const aos_player *p, const float t[2])
{
	float c = aos_cos(p->faceAngle);
	float s = aos_sin(p->faceAngle);
	if (!(t[0] < p->x || t[0] == p->x || 0.0f <= c))
		return false;
	if (!(p->x < t[0] || p->x == t[0] || c <= 0.0f))
		return false;
	if (!(t[1] < p->y || t[1] == p->y || 0.0f <= s))
		return false;
	if (!(p->y < t[1] || p->y == t[1] || s <= 0.0f))
		return false;
	return true;
}

static void ai_clear_dirs(int32_t *m)
{
	if (*m & 1) *m -= 1;
	if (*m & 2) *m -= 2;
	if (*m & 4) *m -= 4;
	if (*m & 8) *m -= 8;
}

/* Movement block shared by both difficulties. `lim` = (frame+333)%200 window
 * (100 diff 0, 150 diff 1/2). Leaves the last target in t (read by
 * FUN_006b0570 in diff 1/2). */
static void ai_move(aos_world *w, aos_player *p, aos_player *o, float d, int32_t *m, int lim, float t[2])
{
	int32_t f = w->frame;
	float a = ((float)(f % 300) / 300.0f) * AOS_2PI;
	t[0] = aos_cos(a) * (d + 50.0f) * 0.25f + o->x;
	t[1] = aos_sin(a) * (d + 50.0f) * 0.25f + o->y;
	if (ai_mode(p) == 0)
		ai_set_mode(p, 2);
	if (ai_mode(p) == 1) {
		a = ((float)(f % 200) / 200.0f) * AOS_2PI;
		t[0] = aos_cos(a) * (d + 100.0f) * 0.25f + o->x;
		t[1] = aos_sin(a) * (d + 100.0f) * 0.25f + o->y;
		if ((f + 0x14d) % 200 < lim) {
			ai_clear_dirs(m);
			aos_cpu_steer(m, p, t);
		}
	}
	if (ai_mode(p) == 2) {
		a = ((float)(f % 100) / 100.0f) * AOS_2PI;
		t[0] = aos_cos(a) * 300.0f - o->x;
		t[1] = aos_sin(a) * 300.0f - o->y;
		if ((f + 0x14d) % 200 < lim) {
			ai_clear_dirs(m);
			aos_cpu_steer(m, p, t);
		}
	}
}

/* Shared: approach / sub weapon / Kae combo-state handling. */
static void ai_gauge_clears(aos_world *w, aos_player *p, int32_t *m)
{
	int32_t f = w->frame;
	if ((p->gaugeA < 0.3f || f % 0xb != 1) && (*m & 0x10))
		*m -= 0x10;
	if ((p->gaugeB < 0.4f || f % 0xb != 5) && (*m & 0x20))
		*m -= 0x20;
}

static void ai_far_b(aos_world *w, aos_player *p, float d, int32_t *m)
{
	if (w->frame % 500 < 0x1e && 500.0f < d && aos_tab.cost[0x1b7].b * 5.0f < p->gaugeB)
		*m = 0x20;
}

static void ai_special(aos_world *w, aos_player *p, aos_player *o, float d, int32_t *m, int period)
{
	if (o->state != 1 && w->frame % period == 0x27) {
		int r = aos_cpu_rand_int(w, 0, 200);
		if (r < 0x32) {
			if (ai_mode(p) == 1 && p->statusOn[7] == 0)
				*m = 0x140;
			else if (300.0f <= d)
				*m = 0x180;
			else
				*m = 0x120;
		}
	}
}

static void ai_close(aos_world *w, aos_player *p, aos_player *o, float d, int32_t *m, int rmax, int period)
{
	if (o->state != 0 && o->barrierTime == 0 && d < 200.0f) {
		int r = aos_cpu_rand_int(w, 0, rmax);
		if (r < 10 || o->state == 1) {
			if (0x1d < w->frame % period || p->hyper < 1.0f || o->state == 1)
				*m = 0x50;
			else if (p->statusOn[7] == 0)
				*m = 0x100;
			else
				*m = 0x140;
		}
	}
}

static void ai_combo_states(aos_player *p, aos_player *o, int32_t *m)
{
	if (p->state == 0x1bc && p->timer == 0xe && o->state == 1)
		*m = 0;
	if (p->state == 0x1bc && p->timer == 0xf && o->state == 1)
		*m = 0x10;
	if (p->state == 0x1bd && p->timer == 0xe && o->state == 1)
		*m = 0;
	if (p->state == 0x1bd && p->timer == 0xf && o->state == 1) {
		if (p->statusOn[7] != 1 || p->hyper < 1.0f)
			*m = 0x10;
		else
			*m = 0x140;
	}
	if (p->state == 0x1be && p->timer == 0xe && o->state == 1)
		*m = 0;
	if (p->state == 0x1be && p->timer == 0xf && o->state == 1)
		*m = 0x10;
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

static void ai_diff0(aos_world *w, aos_player *p, uint16_t *out) /* FUN_006e1970 */
{
	int32_t *m = &w->cpuGlobal[ID][0];
	int32_t f = w->frame;
	float t[2];
	if (f % 0xf == 0)
		*m = aos_cpu_rand_int(w, 0, 0xff);
	if (0x46 < f % 0xaa) {
		if (*m & 0x10) *m -= 0x10;
		if (*m & 0x20) *m -= 0x20;
	}
	aos_player *o = aos_opponent(w, p);
	float d = aos_dist(w, p);
	ai_move(w, p, o, d, m, 100, t);
	if (f % 300 == 299 && aos_cpu_rand_int(w, 0, 100) < 0x28)
		ai_set_mode(p, ai_mode(p) == 1 ? 2 : 1);
	ai_gauge_clears(w, p, m);
	if ((*m & 0x40) && 400.0f < d)
		*m -= 0x40;
	ai_far_b(w, p, d, m);
	ai_special(w, p, o, d, m, 0xa0);
	if (o->state == 1 && f % 2 == 0 && d < 300.0f)
		*m = 0x50;
	if (o->state != 2 && o->state != 0 && 0x14 < o->timer && d < 200.0f && o->barrierTime == 0 &&
	    f % 0x5a == 1)
		*m = 0x50;
	ai_close(w, p, o, d, m, 300, 400);
	ai_combo_states(p, o, m);
	if (p->state == 1 && (*m & 0x20))
		*m -= 0x20;
	ai_burst_roll(w, p, o, m);
	*out = (uint16_t)*m;
}

static void ai_diff12(aos_world *w, aos_player *p, uint16_t *out) /* FUN_006e0ef0 */
{
	int32_t *m = &w->cpuGlobal[ID][1];
	int32_t f = w->frame;
	float t[2];
	if (f % 10 == 0)
		*m = aos_cpu_rand_int(w, 0, 0xff);
	aos_player *o = aos_opponent(w, p);
	float d = aos_dist(w, p);
	ai_move(w, p, o, d, m, 0x96, t);
	if (f % 300 == 299 && (aos_cpu_rand_int(w, 0, 100) < 0x28 || ai_mode(p) == 2))
		ai_set_mode(p, ai_mode(p) == 1 ? 2 : 1);
	ai_gauge_clears(w, p, m);
	if ((f + 0x14d) % 0x82 < 100 && ai_mode(p) == 1)
		*m |= 0x80;
	if (((f % 0x14 == 0 && 0x50 < aos_cpu_rand_int(w, 0, 100)) || !ai_facing_target(p, t)) && (*m & 0x80))
		*m -= 0x80;
	if ((*m & 0x40) && 400.0f < d)
		*m -= 0x40;
	ai_far_b(w, p, d, m);
	ai_special(w, p, o, d, m, 0x50);
	if (o->state == 1 && d < 300.0f)
		*m = 0x50;
	if (o->state != 2 && o->state != 0 && 0x14 < o->timer && d < 200.0f && o->barrierTime == 0 &&
	    f % 0x32 == 1)
		*m = 0x50;
	ai_close(w, p, o, d, m, 200, 300);
	ai_combo_states(p, o, m);
	if (1 < w->difficulty && 3.0f <= p->hyper && o->state == 1 && d < 300.0f &&
	    (aos_cpu_rand_int(w, 0, 100) + f) % 0x32 < 10)
		*m = 0x160;
	if (p->state == 1 && (*m & 0x20) && (p->heat <= 1.25f || p->maxHp / 3 < p->hp))
		*m -= 0x20;
	ai_burst_roll(w, p, o, m);
	*out = (uint16_t)*m;
}

void aos_register_ai_kae(void)
{
	aos_tab.cpu[ID][0] = ai_diff0;  /* 0x8814e0 FUN_006e1970 */
	aos_tab.cpu[ID][1] = ai_diff12; /* 0x8814e4 FUN_006e0ef0 */
	aos_tab.cpu[ID][2] = ai_diff12; /* 0x8814e8 FUN_006e0ef0 */
}
