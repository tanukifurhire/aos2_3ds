/* Star Breaker (engine id 9) CPU AI — table 0x881490: diff 0 FUN_0074cb90,
 * diff 1/2 FUN_0074c280 (registry.c:189-191). Each function fills the same
 * 16-bit button mask a human produces (core.md §2.1).
 *
 * Port notes:
 *  - Each original function keeps its mask in its own static short
 *    (DAT_009b68bc diff 0 / DAT_009b68b8 diff 1/2), shared by both players and
 *    persisting across rounds: w->cpuGlobal[ID][0] / [1]. Only bits 0..8 set.
 *  - FUN_00459f80 runs on the cosmetic stream -> aos_cpu_rand_int, same calls,
 *    same order (5 call sites in diff 0, 6 in diff 1/2). FUN_006b04e0 = aos_cpu_steer.
 *  - player +0x160 (movement mode: 1 = orbit the arena centre at radius 380,
 *    period 400; 2 = circle the opponent at radius (dist + 100) / 4, period 300)
 *    lives in p->scratch[4..7] (ai_orbit.h).
 *  - Both functions first compute a (dist + 50) / 4 circle target that every
 *    reachable path overwrites before use (the mode is always 1 or 2 there):
 *    dead, omitted (cos/sin are pure).
 *  - FUN_005f1050 (signed angle difference) is reimplemented as ai_angle_diff.
 *  - All arithmetic is SSE single precision in the original. */
#include "ai_orbit.h"

#define ID AOS_CH_STARBREAKER

static void ai_clear_dirs(int32_t *m)
{
	if (*m & 1) *m -= 1;
	if (*m & 2) *m -= 2;
	if (*m & 4) *m -= 4;
	if (*m & 8) *m -= 8;
}

/* FUN_005f1050(a, b): b - a after normalising both into (−π, π], wrapped. */
static float ai_angle_diff(float a, float b)
{
	aos_angle_normalize(&a);
	aos_angle_normalize(&b);
	float d = b - a;
	if (AOS_PI < d)
		d = -(AOS_2PI - d);
	else if (d < -3.1415927f) /* DAT_00814100 */
		d = AOS_2PI + d;
	return d;
}

static void ai_move(aos_world *w, aos_player *p, aos_player *o, float d, int32_t *m, int lim)
{
	int32_t f = w->frame;
	float t[2];
	if (ai_mode(p) == 0)
		ai_set_mode(p, 1);
	if (ai_mode(p) == 1) {
		float a = ((float)(f % 400) / 400.0f) * AOS_2PI;
		t[0] = aos_cos(a) * 380.0f;
		t[1] = aos_sin(a) * 380.0f;
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

static void ai_fire(aos_world *w, aos_player *o, float d, float r0, int32_t *m, int per)
{
	if ((*m & 0x10) && 500.0f < d)
		*m -= 0x10;
	if (o->state != 0 && o->state != 2 && o->barrierTime == 0 && w->frame % per == 0 && r0 < 300.0f &&
	    300.0f < d && aos_cpu_rand_int(w, 0, 200) < 0x14)
		*m = 0x180;
}

/* Combo follow-ups after a connecting 0x113/0x116 resp. 0x114/0x117 (hitFlag). */
static bool ai_is_a(const aos_player *p) { return (p->state == 0x113 || p->state == 0x116) && p->hitFlag == 1; }
static bool ai_is_b(const aos_player *p) { return (p->state == 0x114 || p->state == 0x117) && p->hitFlag == 1; }

static void ai_diff0(aos_world *w, aos_player *p, uint16_t *out) /* FUN_0074cb90 */
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
	ai_move(w, p, o, d, m, 100);
	ai_fire(w, o, d, r0, m, 0x78);
	if (o->state != 0 && o->barrierTime == 0 && d < 200.0f && 0.75f <= p->gaugeA &&
	    (aos_cpu_rand_int(w, 0, 300) < 10 || o->state == 1)) {
		if (1.5f < o->heat || 1.5f < o->hyper)
			*m = 0x50;
		else
			*m = 0x10;
	}
	if ((p->state == 0x113 || p->state == 0x116) && p->timer == 0xe && p->hitFlag == 1)
		*m = 0;
	if (ai_is_a(p) && p->timer == 0xf)
		*m = 0x10;
	if (ai_is_b(p) && p->timer == 0xd)
		*m = 0;
	if (ai_is_b(p) && p->timer == 0xe) {
		float a = aos_atan2(o->y - p->y, o->x - p->x);
		float b = aos_atan2(-p->y, -p->x);
		if (1.0f <= p->hyper && f % 2 == 0 && aos_fabs(ai_angle_diff(a, b)) < 1.25f)
			*m = 0x100;
		else if (1.0f <= p->hyper && f % 2 == 1)
			*m = 0x140;
		else
			*m = 0x10;
	}
	if (p->state == 1 && (*m & 0x20))
		*m -= 0x20;
	ai_hitstun_escape(w, p, o, m);
	*out = (uint16_t)*m;
}

static void ai_diff12(aos_world *w, aos_player *p, uint16_t *out) /* FUN_0074c280 */
{
	int32_t *m = &w->cpuGlobal[ID][1];
	int32_t f = w->frame;
	if (f % 10 == 0)
		*m = aos_cpu_rand_int(w, 0, 0xff);
	aos_player *o = aos_opponent(w, p);
	float d = aos_dist(w, p);
	float r0 = aos_sqrt(p->x * p->x + p->y * p->y);
	ai_move(w, p, o, d, m, 0x96);
	ai_fire(w, o, d, r0, m, 0x3c);
	if (o->state == 1 && (f + 0x14d) % 0x32 == 0)
		*m |= 0x80;
	if (o->state != 0 && o->barrierTime == 0 && d < 200.0f && 0.75f <= p->gaugeA &&
	    (aos_cpu_rand_int(w, 0, 200) < 10 || o->state == 1)) {
		if (1.5f < o->heat || 1.5f < o->hyper)
			*m = 0x50;
		else
			*m = 0x10;
	}
	if ((p->state == 0x113 || p->state == 0x116) && p->timer == 0xe && p->hitFlag == 1)
		*m = 0;
	if (ai_is_a(p) && p->timer == 0xf)
		*m = 0x10;
	if (ai_is_b(p) && p->timer == 0xd)
		*m = 0;
	if (ai_is_b(p) && p->timer == 0xe) {
		float a = aos_atan2(o->y - p->y, o->x - p->x);
		float b = aos_atan2(-p->y, -p->x);
		if (1.0f <= p->hyper && aos_fabs(ai_angle_diff(a, b)) < 1.25f)
			*m = 0x100;
		else if (p->hyper < 1.0f)
			*m = 0x10;
		else
			*m = 0x140;
	}
	if (1 < w->difficulty && 3.0f <= p->hyper && o->state == 1 && (aos_cpu_rand_int(w, 0, 100) + f) % 0x32 < 5)
		*m = 0x160;
	if (p->state == 1 && (*m & 0x20) && (p->heat <= 1.25f || p->maxHp / 3 < p->hp))
		*m -= 0x20;
	ai_hitstun_escape(w, p, o, m);
	*out = (uint16_t)*m;
}

void aos_register_ai_starbreaker(void)
{
	aos_tab.cpu[ID][0] = ai_diff0;  /* 0x881490 FUN_0074cb90 */
	aos_tab.cpu[ID][1] = ai_diff12; /* 0x881494 FUN_0074c280 */
	aos_tab.cpu[ID][2] = ai_diff12; /* 0x881498 FUN_0074c280 */
}
