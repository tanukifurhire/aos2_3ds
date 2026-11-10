/* Saki (engine id 11) CPU AI — table 0x8814b0: diff 0 FUN_00724010,
 * diff 1/2 FUN_00723720 (registry.c:195-197). Each function fills the same
 * 16-bit button mask a human produces (core.md §2.1).
 *
 * Port notes:
 *  - Each original function keeps its mask in its own static short
 *    (DAT_009b68a4 diff 0 / DAT_009b68a0 diff 1/2), shared by both players and
 *    persisting across rounds: w->cpuGlobal[ID][0] / [1]. Only bits 0..8 set.
 *  - FUN_00459f80 runs on the cosmetic stream -> aos_cpu_rand_int, same calls,
 *    same order (4 call sites in diff 0, 7 in diff 1/2). FUN_006b04e0 = aos_cpu_steer.
 *  - player +0x160 (movement mode: 1 = orbit the arena centre at radius 360,
 *    period 400, mirrored; 2 = circle the opponent at radius (dist + 100) / 4,
 *    period 300) lives in p->scratch[4..7] (ai_orbit.h). Diff 1/2 also forces
 *    mode 2 from states 0x167 / 0x168.
 *  - The `state == 0x15f` re-test inside the 0x15f branch is redundant (kept as
 *    a single test).
 *  - All arithmetic is SSE single precision in the original. */
#include "ai_orbit.h"

#define ID AOS_CH_SAKI

static void ai_clear_dirs(int32_t *m)
{
	if (*m & 1) *m -= 1;
	if (*m & 2) *m -= 2;
	if (*m & 4) *m -= 4;
	if (*m & 8) *m -= 8;
}

static void ai_move(aos_world *w, aos_player *p, aos_player *o, float d, int32_t *m, int lim)
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
		float a = ((float)(f % 300) / 300.0f) * AOS_2PI;
		t[0] = aos_cos(a) * (d + 100.0f) * 0.25f + o->x;
		t[1] = aos_sin(a) * (d + 100.0f) * 0.25f + o->y;
		if ((f + 0x14d) % 200 < lim) {
			ai_clear_dirs(m);
			aos_cpu_steer(m, p, t);
		}
	}
	if (f % 300 == 299 && aos_cpu_rand_int(w, 0, 100) < 0x28)
		ai_set_mode(p, ai_mode(p) == 1 ? 2 : 1);
}

static void ai_far_a(aos_world *w, aos_player *p, float d, int32_t *m)
{
	if (300.0f < d && ai_mode(p) == 1 && (w->frame + 0x14d) % 400 == 399 && 0.5f <= p->gaugeA)
		*m = 0x10;
}

/* Opponent in hitstun at mid range: B (0x20), or release it in 0x15f. */
static void ai_chase_b(aos_player *p, int32_t *m)
{
	if (p->state == 0x15f) {
		if (p->timer < 0xc)
			*m = 0;
	} else {
		*m = 0x20;
	}
}

static void ai_close(aos_world *w, aos_player *p, aos_player *o, float d, int32_t *m, int rmax)
{
	if (o->state != 0 && o->barrierTime == 0 && d < 200.0f && 0.75f <= p->gaugeA &&
	    (aos_cpu_rand_int(w, 0, rmax) < 10 || o->state == 1)) {
		if (p->hyper < 1.0f || w->frame % 3 != 0)
			*m = d <= 120.0f ? 0x60 : 0xd0;
		else
			*m = 0x140;
	}
}

static void ai_followup(aos_world *w, aos_player *p, int32_t *m)
{
	if (p->state == 0x161 && p->timer == 0xe && p->hitFlag == 1)
		*m = 0;
	if (p->state == 0x161 && p->timer == 0xf && p->hitFlag == 1) {
		if (p->hyper < 1.0f || w->frame % 3 != 0)
			*m = 0x10;
		else
			*m = 0x140;
	}
}

static void ai_diff0(aos_world *w, aos_player *p, uint16_t *out) /* FUN_00724010 */
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
	ai_move(w, p, o, d, m, 100);
	if (o->state == 1 && (f + 0x14d) % 0x32 == 0)
		*m |= 0x80;
	ai_far_a(w, p, d, m);
	if (o->state == 1 && o->barrierTime == 0 && f % 2 == 0 && d < 400.0f && 200.0f < d && 0.4f <= p->gaugeB)
		ai_chase_b(p, m);
	ai_close(w, p, o, d, m, 300);
	ai_followup(w, p, m);
	if (p->state == 1 && (*m & 0x20))
		*m -= 0x20;
	ai_hitstun_escape(w, p, o, m);
	*out = (uint16_t)*m;
}

static void ai_diff12(aos_world *w, aos_player *p, uint16_t *out) /* FUN_00723720 */
{
	int32_t *m = &w->cpuGlobal[ID][1];
	int32_t f = w->frame;
	if (f % 10 == 0)
		*m = aos_cpu_rand_int(w, 0, 0xff);
	aos_player *o = aos_opponent(w, p);
	float d = aos_dist(w, p);
	ai_move(w, p, o, d, m, 0x96);
	if (o->state == 1 && (f + 0x14d) % 0x32 == 0)
		*m |= 0x80;
	if (p->state != 0 && p->state != 2 && 0x13 < p->timer && p->timer % 4 == 0 &&
	    (p->heat < 1.0f || aos_cpu_rand_int(w, 0, 100) < 10 || o->state == 1))
		*m |= 0x80;
	if (o->state != 0 && o->barrierTime == 0 && f % 0x3c == 0 && 400.0f < d &&
	    aos_cpu_rand_int(w, 0, 200) < 0x14)
		*m = 0x120;
	if (p->state == 0x167 && p->timer < 0x50) {
		*m = 0x100;
		ai_set_mode(p, 2);
	}
	ai_far_a(w, p, d, m);
	if (o->state == 1 && o->barrierTime == 0 && d < 400.0f && 200.0f < d && 0.4f <= p->gaugeB)
		ai_chase_b(p, m);
	ai_close(w, p, o, d, m, 200);
	if (o->state == 1 && o->barrierTime == 0 && f % 4 == 0 && d < 360.0f && 100.0f < d)
		*m = 0xd0;
	ai_followup(w, p, m);
	if (p->state == 0x168 && p->timer == 0x2d) {
		*m = 0;
		ai_set_mode(p, 2);
	}
	if (p->state == 0x168 && p->timer == 0x2e)
		*m = 0x80;
	if (1 < w->difficulty && 3.0f <= p->hyper && o->state == 1 && d < 300.0f &&
	    (aos_cpu_rand_int(w, 0, 100) + f) % 0x32 < 10)
		*m = 0x160;
	if (p->state == 1 && (*m & 0x20) && (p->heat <= 1.25f || p->maxHp / 3 < p->hp))
		*m -= 0x20;
	ai_hitstun_escape(w, p, o, m);
	*out = (uint16_t)*m;
}

void aos_register_ai_saki(void)
{
	aos_tab.cpu[ID][0] = ai_diff0;  /* 0x8814b0 FUN_00724010 */
	aos_tab.cpu[ID][1] = ai_diff12; /* 0x8814b4 FUN_00723720 */
	aos_tab.cpu[ID][2] = ai_diff12; /* 0x8814b8 FUN_00723720 */
}
