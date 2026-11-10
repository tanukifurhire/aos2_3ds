/* Tsih (engine id 5) CPU AI — 1:1 port of aos2.exe FUN_00787fe0 (difficulty 0)
 * and FUN_00787850 (difficulties 1 and 2), table 0x881450/54/58.
 *
 * The AI produces the same 16-bit button mask a human would (core.md §2.1).
 * Its persistent intention is a static in the original (DAT_009b68dc / DAT_009b68d8), shared by
 * both players -> w->cpuGlobal[ID][0] (diff 0) / [1] (diff 1-2). The movement
 * mode lives in player +0x160 (scratch[4..7]). FUN_00459f80 calls run on the
 * CPU (cosmetic) stream, aos_cpu_rand_int, in the original order: the PC game
 * samples input outside the tick, before the game RNG is selected.
 * Float chains are evaluated in double and rounded once, like the x87 code. */
#include "../sim.h"
#include "ai_orbit.h"

#define ID AOS_CH_TSIH

/* player +0x160 (ai_mode): CPU movement mode 1 = orbit the arena centre (r 400), 2 = orbit around minus the opponent position (sic). */

static void clear_dirs(int32_t *m) { *m &= ~0xf; } /* the four "if (m & k) m -= k" */

/* FUN_00782a70(team): number of Tsih Enemy objects (type 5, spawn id 100) of that team. */
static int count_units(aos_world *w, int32_t team)
{
	int n = 0;
	for (int i = 0; i < w->obj.nenemy; i++) {
		const aos_enemy *e = &w->obj.enemy[i];
		if (e->team == team && e->type == 5 && e->f10 == 100)
			n++;
	}
	return n;
}

static bool stealth(const aos_player *p) { return p->scratch[0x164 - 0x15c] == 1; }

static void cpu_easy(aos_world *w, aos_player *p, uint16_t *mask) /* FUN_00787fe0 */
{
	int32_t *g = &w->cpuGlobal[ID][0];
	int32_t m = *g;
	int32_t f = w->frame;
	float t[2];
	if (f % 15 == 0)
		m = aos_cpu_rand_int(w, 0, 0xff);
	if (f % 170 > 70)
		m &= ~0x30;
	aos_player *o = aos_opponent(w, p);
	float d = aos_dist(w, p);
	if (ai_mode(p) == 0)
		ai_set_mode(p, 2);
	if (ai_mode(p) == 1) {
		float a = ai_phase(f % 400, 400.0);
		t[0] = (float)((double)aos_cos(a) * 400.0);
		t[1] = (float)((double)aos_sin(a) * 400.0);
		clear_dirs(&m);
		aos_cpu_steer(&m, p, t);
	}
	if (ai_mode(p) == 2) {
		float a = ai_phase(f % 100, 100.0);
		t[0] = (float)((double)-aos_cos(a) * 300.0 - (double)o->x); /* sic: minus the opponent position */
		t[1] = (float)((double)-aos_sin(a) * 300.0 - (double)o->y);
		if ((f + 333) % 200 < 100) {
			clear_dirs(&m);
			aos_cpu_steer(&m, p, t);
		}
	}
	if (f % 400 == 399) {
		if (aos_cpu_rand_int(w, 0, 100) < 40)
			ai_set_mode(p, ai_mode(p) == 1 ? 2 : 1);
	}
	if (p->gaugeA < 0.4f && (m & 0x10))
		m -= 0x10;
	if (p->gaugeB < 0.4f && (m & 0x20))
		m -= 0x20;
	if ((m & 0x40) && 500.0f < d)
		m -= 0x40;
	if (p->state == 0x6d && p->timer == 0x12) {
		int n = count_units(w, p->side);
		if (aos_cpu_rand_int(w, 0, 100) < n * 10)
			m = 0x80;
	}
	if (o->state == 1 && o->barrierTime == 0 && f % 6 == 0 && d < 360.0f && 100.0f < d)
		m = 0x60;
	/* original: (o->state != 0 || o->state != 2) — always true */
	if (o->barrierTime == 0 && f % 120 == 1 && 500.0f < d)
		m = 0x60;
	if ((p->state == 0x73 || p->state == 0x74 || p->state == 0x75) && p->timer < 0x18 &&
	    p->timer == 0x15 && o->state == 2)
		(void)aos_cpu_rand_int(w, 0, 100); /* result unused */
	if (o->state != 1 && f % 80 == 0 && 400.0f < d) {
		if (aos_cpu_rand_int(w, 0, 200) < 50) {
			if (ai_mode(p) == 2)
				m = 0x120;
			else if (stealth(p) || (f / 10) % 3 == 2)
				m = 0x100;
			else
				m = 0x140;
		}
	}
	if (p->state == 1 && (m & 0x20))
		m -= 0x20;
	ai_hitstun_escape(w, p, o, &m);
	*g = m;
	*mask = (uint16_t)m;
}

static void cpu_normal(aos_world *w, aos_player *p, uint16_t *mask) /* FUN_00787850 */
{
	int32_t *g = &w->cpuGlobal[ID][1];
	int32_t m = *g;
	int32_t f = w->frame;
	float t[2];
	if (f % 10 == 0)
		m = aos_cpu_rand_int(w, 0, 0xff);
	aos_player *o = aos_opponent(w, p);
	float d = aos_dist(w, p);
	if (ai_mode(p) == 0)
		ai_set_mode(p, 2);
	if (ai_mode(p) == 1) {
		float a = ai_phase(f % 400, 400.0);
		t[0] = (float)((double)aos_cos(a) * 400.0);
		t[1] = (float)((double)aos_sin(a) * 400.0);
		clear_dirs(&m);
		aos_cpu_steer(&m, p, t);
	}
	if (ai_mode(p) == 2) {
		float a = ai_phase(f % 100, 100.0);
		t[0] = (float)((double)-aos_cos(a) * 300.0 - (double)o->x); /* sic */
		t[1] = (float)((double)-aos_sin(a) * 300.0 - (double)o->y);
		if ((f + 333) % 200 < 150) {
			clear_dirs(&m);
			aos_cpu_steer(&m, p, t);
		}
	}
	if (f % 400 == 399) {
		if (aos_cpu_rand_int(w, 0, 100) < 40)
			ai_set_mode(p, ai_mode(p) == 1 ? 2 : 1);
	}
	if (p->gaugeA < 0.4f && (m & 0x10))
		m -= 0x10;
	if (p->gaugeB < 0.4f && (m & 0x20))
		m -= 0x20;
	/* mash dash (burst out / cancel) every 4th frame of a long non-neutral state */
	if (p->state != 0 && p->state != 2 && p->timer > 0x13 && p->timer % 4 == 0) {
		bool skip = false;
		if (p->heat >= 1.0f) {
			if (aos_cpu_rand_int(w, 0, 100) > 9 && o->state != 1)
				skip = true;
		}
		if (!skip)
			m |= 0x80;
	}
	if ((m & 0x40) && 400.0f < d)
		m -= 0x40;
	if (p->state == 0x6d && p->timer == 0x12) {
		int n = count_units(w, p->side);
		if (aos_cpu_rand_int(w, 0, 100) < n * 10)
			m = 0x80;
	}
	if (o->state == 1 && o->barrierTime == 0 && f % 4 == 0 && d < 360.0f && 100.0f < d)
		m = 0x60;
	/* original: (o->state != 0 || o->state != 2) — always true */
	if (o->barrierTime == 0 && f % 70 == 1 && 500.0f < d)
		m = 0x60;
	if ((p->state == 0x73 || p->state == 0x74 || p->state == 0x75) && p->timer < 0x18 &&
	    p->timer == 0x15 && o->state == 2)
		(void)aos_cpu_rand_int(w, 0, 100); /* result unused */
	if (o->state != 1 && f % 60 == 0 && 400.0f < d) {
		if (aos_cpu_rand_int(w, 0, 200) < 50) {
			if (ai_mode(p) == 2)
				m = 0x120;
			else if (stealth(p) || (f / 10) % 3 == 2)
				m = 0x100;
			else
				m = 0x140;
		}
	}
	if (w->difficulty > 1 && 3.0f <= p->hyper && o->state == 1 && d < 300.0f) {
		int r = aos_cpu_rand_int(w, 0, 100);
		if ((r + f) % 50 < 10)
			m = 0x160;
	}
	if (p->state == 1 && (m & 0x20) && (p->heat <= 1.25f || p->maxHp / 3 < p->hp))
		m -= 0x20;
	ai_hitstun_escape(w, p, o, &m);
	*g = m;
	*mask = (uint16_t)m;
}

void aos_register_ai_tsih(void)
{
	aos_tab.cpu[ID][0] = cpu_easy;   /* 0x881450 */
	aos_tab.cpu[ID][1] = cpu_normal; /* 0x881454 */
	aos_tab.cpu[ID][2] = cpu_normal; /* 0x881458 */
}
