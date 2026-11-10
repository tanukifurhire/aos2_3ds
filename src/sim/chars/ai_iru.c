/* Iru (engine id 12) CPU AI — 1:1 port of aos2.exe FUN_006d4b60 (difficulty 0)
 * and FUN_006d43b0 (difficulties 1 and 2), table 0x8814c0/c4/c8.
 *
 * The AI produces the same 16-bit button mask a human would (core.md §2.1).
 * Its persistent intention is a static in the original (DAT_009b6874 / DAT_009b6870), shared by
 * both players -> w->cpuGlobal[ID][0] (diff 0) / [1] (diff 1-2). The movement
 * mode lives in player +0x160 (scratch[4..7]). FUN_00459f80 calls run on the
 * CPU (cosmetic) stream, aos_cpu_rand_int, in the original order: the PC game
 * samples input outside the tick, before the game RNG is selected.
 * Float chains are evaluated in double and rounded once, like the x87 code. */
#include "../sim.h"
#include "ai_orbit.h"

#define ID AOS_CH_IRU

/* player +0x160 (ai_mode): CPU movement mode 1 = circle the arena, 2 = circle around -opponent. */

static void clear_dirs(int32_t *m) { *m &= ~0xf; }

/* FUN_006d05c0(team): number of Iru Enemy objects (type 12, spawn id 100) of that team. */
static int count_units(aos_world *w, int32_t team)
{
	int n = 0;
	for (int i = 0; i < w->obj.nenemy; i++) {
		const aos_enemy *e = &w->obj.enemy[i];
		if (e->team == team && e->type == 0xc && e->f10 == 100)
			n++;
	}
	return n;
}

static void move_modes(aos_world *w, aos_player *p, const aos_player *o, int32_t *m)
{
	int32_t f = w->frame;
	float t[2];
	if (ai_mode(p) == 0)
		ai_set_mode(p, 1);
	if (ai_mode(p) == 1) {
		float a = ai_phase(f % 600, 600.0);
		t[0] = (float)((double)aos_cos(a) * 400.0);
		t[1] = (float)((double)aos_sin(a) * 400.0);
		clear_dirs(m);
		aos_cpu_steer(m, p, t);
	}
	if (ai_mode(p) == 2) {
		ai_target_far(w, o, t); /* r 300 orbit minus the opponent position (sic) */
		if ((f + 333) % 200 < 150) {
			clear_dirs(m);
			aos_cpu_steer(m, p, t);
		}
	}
	if (f % 400 == 399) {
		if (aos_cpu_rand_int(w, 0, 100) < 40)
			ai_set_mode(p, ai_mode(p) == 1 ? 2 : 1);
	}
}

/* state 0x182 (frames 20..70): chase the opponent, or stop when it is in hitstun */
static void chase(aos_player *p, const aos_player *o, int32_t *m)
{
	if (p->state == 0x182 && p->timer > 0x13 && p->timer < 0x47) {
		if (o->state == 1) {
			*m = 0;
		} else {
			float t[2] = {o->x, o->y}; /* the original also copies o->z (unused) */
			clear_dirs(m);
			aos_cpu_steer(m, p, t);
		}
	}
}

static void cpu_easy(aos_world *w, aos_player *p, uint16_t *mask) /* FUN_006d4b60 */
{
	int32_t *g = &w->cpuGlobal[ID][0];
	int32_t m = *g;
	int32_t f = w->frame;
	if (f % 15 == 0)
		m = aos_cpu_rand_int(w, 0, 0xff);
	if (f % 170 > 70)
		m &= ~0x30;
	aos_player *o = aos_opponent(w, p);
	float d = aos_dist(w, p);
	move_modes(w, p, o, &m);
	if (p->gaugeB < 0.4f && (m & 0x20))
		m -= 0x20;
	if (o->state != 1 && ai_mode(p) == 2 && f % 120 == 0 && 400.0f < d) {
		if (aos_cpu_rand_int(w, 0, 200) < 50)
			m = count_units(w, p->side) < 4 ? 0x140 : 0x180;
	}
	if (o->state != 2 && o->barrierTime == 0 && f % 260 > 249 && d < 500.0f)
		m = 0x10;
	if (p->state == 0x179 && p->timer < 0xb && o->state != 2 && o->timer < 0x14 && 300.0f < d)
		m = 0;
	if (o->state != 2 && o->state != 0 && o->barrierTime == 0 && 1.0f <= p->hyper &&
	    ai_mode(p) == 1 && f % 600 == 599 && 500.0f < d)
		m = 0x100;
	chase(p, o, &m);
	if (p->state == 1 && (m & 0x20))
		m -= 0x20;
	ai_hitstun_escape(w, p, o, &m);
	*g = m;
	*mask = (uint16_t)m;
}

static void cpu_normal(aos_world *w, aos_player *p, uint16_t *mask) /* FUN_006d43b0 */
{
	int32_t *g = &w->cpuGlobal[ID][1];
	int32_t m = *g;
	int32_t f = w->frame;
	if (f % 10 == 0)
		m = aos_cpu_rand_int(w, 0, 0xff);
	aos_player *o = aos_opponent(w, p);
	float d = aos_dist(w, p);
	move_modes(w, p, o, &m);
	if (p->gaugeB < 0.4f && (m & 0x20))
		m -= 0x20;
	if (p->state != 0 && p->state != 2 && p->timer > 0x13 && p->timer % 4 == 0) {
		bool skip = false;
		if (p->heat >= 1.0f) {
			if (aos_cpu_rand_int(w, 0, 100) > 9 && o->state != 1)
				skip = true;
		}
		if (!skip)
			m |= 0x80;
	}
	if (o->state != 1 && ai_mode(p) == 2 && f % 60 == 0 && 400.0f < d) {
		if (aos_cpu_rand_int(w, 0, 200) < 50)
			m = count_units(w, p->side) < 4 ? 0x140 : 0x180;
	}
	if (o->state != 2 && o->barrierTime == 0 && f % 160 > 139 && d < 500.0f)
		m = 0x10;
	if (p->state == 0x179 && p->timer < 0xb && o->state != 2 && o->timer < 0x14 && 300.0f < d)
		m = 0;
	if (o->state != 2 && o->state != 0 && o->barrierTime == 0 && 1.0f <= p->hyper &&
	    ai_mode(p) == 1 && f % 400 == 399 && 500.0f < d)
		m = 0x100;
	chase(p, o, &m);
	if (w->difficulty > 1 && 3.0f <= p->hyper && o->state == 1 && 400.0f < d) {
		int r = aos_cpu_rand_int(w, 0, 100);
		if ((r + f) % 50 < 5)
			m = 0x160;
	}
	if (p->state == 1 && (m & 0x20) && (p->heat <= 1.25f || p->maxHp / 3 < p->hp))
		m -= 0x20;
	ai_hitstun_escape(w, p, o, &m);
	*g = m;
	*mask = (uint16_t)m;
}

void aos_register_ai_iru(void)
{
	aos_tab.cpu[ID][0] = cpu_easy;   /* 0x8814c0 */
	aos_tab.cpu[ID][1] = cpu_normal; /* 0x8814c4 */
	aos_tab.cpu[ID][2] = cpu_normal; /* 0x8814c8 */
}
