/* Hime (engine id 16) CPU AI — table 0x881500: diff 0 FUN_006c98f0,
 * diff 1/2 FUN_006c8fd0. Each function fills the same 16-bit button mask a
 * human produces (core.md §2.1).
 *
 * Port notes:
 *  - Each original function keeps its mask in its own static short
 *    (DAT_009b686c diff 0, DAT_009b6868 diff 1/2) shared by both players and
 *    persisting across rounds: w->cpuGlobal[ID][0] / [1]. Only bits 0..8 are
 *    ever set, so int32 storage is equivalent.
 *  - FUN_00459f80 runs on the cosmetic stream while input is sampled
 *    (core.md §1, sim.h cpuRng) -> aos_cpu_rand_int, same calls, same order.
 *    FUN_006b04e0 = aos_cpu_steer.
 *  - player +0x160 (movement mode: 0 -> 1 on first call; 1 = orbit the arena
 *    centre at radius 420, period 600; 2 = circle the opponent at
 *    (dist+100)/4, period 300, both with the DAT_00853e00 sign flip) lives in
 *    p->scratch[4..7].
 *  - DAT_0087dcb4 = cost table 0x87c4f0 entry 0x1fb field +0 (gauge A cost):
 *    aos_tab.cost[0x1fb].a.
 *  - `(frame * 0xff) % 600` (imul, sic — the neighbouring tests use
 *    frame + 0xff) is computed with 32-bit wraparound.
 *  - The "hyper special" choice tests the *opponent's* heat (+0xe0 of the
 *    FUN_00780480 result), as in the original.
 *  - All arithmetic is SSE single precision in the original (checked in the
 *    disassembly), so plain float C reproduces it. */
#include "ai_orbit.h"

#define ID AOS_CH_HIME

static void ai_clear_dirs(int32_t *m)
{
	if (*m & 1) *m -= 1;
	if (*m & 2) *m -= 2;
	if (*m & 4) *m -= 4;
	if (*m & 8) *m -= 8;
}

/* Movement block shared by both difficulties; `lim` = (frame+333)%200 window
 * for mode 2 (100 diff 0, 150 diff 1/2). */
static void ai_move(aos_world *w, aos_player *p, aos_player *o, float d, int32_t *m, int lim)
{
	int32_t f = w->frame;
	float t[2];
	if (ai_mode(p) == 0)
		ai_set_mode(p, 1);
	if (ai_mode(p) == 1) {
		float a = ((float)(f % 600) / 600.0f) * AOS_2PI;
		t[0] = -aos_cos(a) * 420.0f;
		t[1] = -aos_sin(a) * 420.0f;
		ai_clear_dirs(m);
		aos_cpu_steer(m, p, t);
	}
	if (ai_mode(p) == 2) {
		float a = ((float)(f % 300) / 300.0f) * AOS_2PI;
		t[0] = -aos_cos(a) * (d + 100.0f) * 0.25f + o->x;
		t[1] = -aos_sin(a) * (d + 100.0f) * 0.25f + o->y;
		if ((f + 0x14d) % 200 < lim) {
			ai_clear_dirs(m);
			aos_cpu_steer(m, p, t);
		}
	}
	if (f % 400 == 399 && aos_cpu_rand_int(w, 0, 100) < 0x28)
		ai_set_mode(p, ai_mode(p) == 1 ? 2 : 1);
}

/* Periodic main shot from range: (frame+255)%600 >= 550 every `per` frames. */
static void ai_far_shot(aos_world *w, float d, int32_t *m, int per)
{
	int32_t f = w->frame;
	int32_t fm = (int32_t)((uint32_t)f * 0xffu); /* sic: imul */
	if ((f + 0xff) % 600 < 0x226 || (f + 0xff) % per != 0 || d <= 400.0f) {
		if (0x225 < fm % 600 && 400.0f < d && (*m & 0x10))
			*m -= 0x10;
	} else {
		*m = 0x10;
	}
}

/* Hyper from range: mode 1 at long range (0x100), mode 2 up close (0x180). */
static void ai_far_hyper(aos_world *w, aos_player *p, aos_player *o, float d, int32_t *m, int per)
{
	int32_t f = w->frame;
	if (o->state != 2 && o->state != 0 && o->barrierTime == 0 && 1.0f <= p->hyper && ai_mode(p) == 1 &&
	    f % per == per - 1 && 500.0f < d)
		*m = 0x100;
	if (o->state != 2 && o->state != 0 && o->barrierTime == 0 && 1.0f <= p->hyper && ai_mode(p) == 2 &&
	    f % per == 299 && d < 300.0f)
		*m = 0x180;
}

static void ai_close(aos_world *w, aos_player *p, aos_player *o, float d, int32_t *m, int rmax, int per)
{
	if (o->state != 0 && o->barrierTime == 0 && d < 200.0f &&
	    (aos_cpu_rand_int(w, 0, rmax) < 10 || o->state == 1)) {
		if (0x1d < w->frame % per || p->hyper < 1.0f || o->state == 1)
			*m = 0x50;
		else if (1.0f <= o->heat) /* sic: opponent's heat */
			*m = 0x140;
		else
			*m = 0x180;
	}
}

/* States 0x1ff / 0x200 (combo chain) holds and follow-ups up to the 0x200
 * hit branch, which differs per difficulty. */
static void ai_chain(aos_player *p, aos_player *o, int32_t *m)
{
	if (p->state == 0x1ff && p->timer == 0xc && o->state == 1)
		*m = 0;
	if (p->state == 0x1ff && p->timer == 0xd && p->hitFlag == 1)
		*m = 0x10;
	if (p->state == 0x200 && p->timer == 0x14 && o->state == 1)
		*m = 0;
}

static void ai_diff0(aos_world *w, aos_player *p, uint16_t *out) /* FUN_006c98f0 */
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
	if ((*m & 0x40) && 500.0f < d)
		*m -= 0x40;
	ai_far_shot(w, d, m, 0x14);
	if (o->state != 2 && o->barrierTime == 0 && 0xbd < f % 200 && 500.0f < d)
		*m = 0x20;
	if (p->gaugeB < 0.4f && (*m & 0x20))
		*m -= 0x20;
	if (p->state == 0x1fb && p->timer == 10 && p->gaugeA < aos_tab.cost[0x1fb].a)
		*m = 0;
	ai_far_hyper(w, p, o, d, m, 600);
	ai_close(w, p, o, d, m, 300, 400);
	ai_chain(p, o, m);
	if (p->state == 0x200 && p->timer == 0x15 && p->hitFlag == 1) {
		if (1.0f <= p->hyper && f % 2 == 0 && d < 200.0f)
			*m = 0x120;
		else if (d < 200.0f || p->gaugeB < 0.6f)
			*m = 0x60;
		else
			*m = 0x20;
	}
	if (p->state == 1 && (*m & 0x20))
		*m -= 0x20;
	ai_hitstun_escape(w, p, o, m);
	*out = (uint16_t)*m;
}

static void ai_diff12(aos_world *w, aos_player *p, uint16_t *out) /* FUN_006c8fd0 */
{
	int32_t *m = &w->cpuGlobal[ID][1];
	int32_t f = w->frame;
	if (f % 10 == 0)
		*m = aos_cpu_rand_int(w, 0, 0xff);
	aos_player *o = aos_opponent(w, p);
	float d = aos_dist(w, p);
	ai_move(w, p, o, d, m, 0x96);
	if ((*m & 0x40) && 500.0f < d)
		*m -= 0x40;
	ai_far_shot(w, d, m, 0xf);
	if (o->state != 2 && o->barrierTime == 0 && 0xb3 < f % 200 && 500.0f < d)
		*m = 0x20;
	if (p->gaugeB < 0.4f && (*m & 0x20))
		*m -= 0x20;
	if (p->state == 0x1fb && p->timer == 10 && p->gaugeA < aos_tab.cost[0x1fb].a)
		*m = 0;
	ai_far_hyper(w, p, o, d, m, 400);
	ai_close(w, p, o, d, m, 200, 300);
	ai_chain(p, o, m);
	if (p->state == 0x200 && p->timer == 0x15 && p->hitFlag == 1) {
		if (p->hyper < 1.0f || 200.0f <= d) {
			if (d < 200.0f || p->gaugeB < 0.6f)
				*m = 0x60;
			else
				*m = 0x20;
		} else {
			*m = 0x120;
		}
	}
	if (1 < w->difficulty && 3.0f <= p->hyper && o->state == 1 && d < 300.0f &&
	    (aos_cpu_rand_int(w, 0, 100) + f) % 0x32 < 10)
		*m = 0x160;
	if (p->state == 1 && (*m & 0x20) && (p->heat <= 1.25f || p->maxHp / 3 < p->hp))
		*m -= 0x20;
	ai_hitstun_escape(w, p, o, m);
	*out = (uint16_t)*m;
}

void aos_register_ai_hime(void)
{
	aos_tab.cpu[ID][0] = ai_diff0;  /* 0x881500 FUN_006c98f0 */
	aos_tab.cpu[ID][1] = ai_diff12; /* 0x881504 FUN_006c8fd0 */
	aos_tab.cpu[ID][2] = ai_diff12; /* 0x881508 FUN_006c8fd0 */
}
