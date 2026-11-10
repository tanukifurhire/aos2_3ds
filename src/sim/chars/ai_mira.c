/* Mira (engine id 6) CPU AI — 1:1 port of aos2.exe FUN_006fce20 (difficulty 0)
 * and FUN_006fc5c0 (difficulties 1 and 2), table 0x881460/64/68.
 *
 * The AI produces the same 16-bit button mask a human would (core.md §2.1).
 * Its persistent intention is a static in the original (DAT_009b688c / DAT_009b6888), shared by
 * both players -> w->cpuGlobal[ID][0] (diff 0) / [1] (diff 1-2). The movement
 * mode lives in player +0x160 (scratch[4..7]). FUN_00459f80 calls run on the
 * CPU (cosmetic) stream, aos_cpu_rand_int, in the original order: the PC game
 * samples input outside the tick, before the game RNG is selected.
 * Float chains are evaluated in double and rounded once, like the x87 code. */
#include "../sim.h"
#include "ai_orbit.h"

#define ID AOS_CH_MIRA

/* player +0x160 (ai_mode): CPU movement mode 1 = circle the arena, 2 = orbit the opponent. */

static void clear_dirs(int32_t *m) { *m &= ~0xf; }

/* FUN_006b0570: false if p faces away from target on either axis. */
static bool faces_target(const aos_player *p, const float t[2])
{
	float c = aos_cos(p->faceAngle);
	float s = aos_sin(p->faceAngle);
	if (!(t[0] <= p->x || 0.0f <= c))
		return false;
	if (!(p->x <= t[0] || c <= 0.0f))
		return false;
	if (!(t[1] <= p->y || 0.0f <= s))
		return false;
	if (!(p->y <= t[1] || s <= 0.0f))
		return false;
	return true;
}

/* FUN_006f5c00(team): index of the first deployed chakram (Enemy type 6, id 100), or -1. */
static int chakram_find(aos_world *w, int32_t team)
{
	for (int i = 0; i < w->obj.nenemy; i++) {
		const aos_enemy *e = &w->obj.enemy[i];
		if (e->team == team && e->type == 6 && e->f10 == 100)
			return i;
	}
	return -1;
}

static void move_modes(aos_world *w, aos_player *p, const aos_player *o, float d, int32_t *m,
                       float t[2], int window)
{
	int32_t f = w->frame;
	if (ai_mode(p) == 0)
		ai_set_mode(p, 1);
	if (ai_mode(p) == 1) {
		float a = ai_phase(f % 300, 300.0);
		t[0] = (float)((double)-aos_cos(a) * 360.0);
		t[1] = (float)((double)-aos_sin(a) * 360.0);
		clear_dirs(m);
		aos_cpu_steer(m, p, t);
	}
	if (ai_mode(p) == 2) {
		float a = ai_phase(f % 300, 300.0);
		t[0] = (float)((double)aos_cos(a) * ((double)d + 100.0) * 0.25 + (double)o->x);
		t[1] = (float)((double)aos_sin(a) * ((double)d + 100.0) * 0.25 + (double)o->y);
		if ((f + 333) % 200 < window) {
			clear_dirs(m);
			aos_cpu_steer(m, p, t);
		}
	}
	if (f % 400 == 399) {
		if (aos_cpu_rand_int(w, 0, 100) < 40 || ai_mode(p) == 1)
			ai_set_mode(p, ai_mode(p) == 1 ? 2 : 1);
	}
}

static void cpu_easy(aos_world *w, aos_player *p, uint16_t *mask) /* FUN_006fce20 */
{
	int32_t *g = &w->cpuGlobal[ID][0];
	int32_t m = *g;
	int32_t f = w->frame;
	float t[2] = {0, 0};
	if (f % 15 == 0)
		m = aos_cpu_rand_int(w, 0, 0xff);
	if (f % 170 > 70)
		m &= ~0x30;
	int chak = chakram_find(w, p->side);
	aos_player *o = aos_opponent(w, p);
	float d = aos_dist(w, p);
	move_modes(w, p, o, d, &m, t, 100);
	if (p->gaugeB < 0.75f && (m & 0x20))
		m -= 0x20;
	if ((m & 0x40) && 500.0f < d)
		m -= 0x40;
	if (o->state != 0 && o->barrierTime == 0 && d < 200.0f) {
		int r = aos_cpu_rand_int(w, 0, 300);
		if (r < 10 || o->state == 1) {
			if (f % 500 > 29 || p->hyper < 1.0f || chak != -1 || o->state == 1)
				m = 0x50;
			else
				m = 0x140;
		}
	}
	if (p->state == 0x97 && p->timer == 0xe && p->hitFlag == 1)
		m = 0;
	if (p->state == 0x97 && p->timer == 0xf && p->hitFlag == 1)
		m = 0x50;
	if (p->state == 0x98 && p->timer == 0xe && p->hitFlag == 1)
		m = 0;
	if (p->state == 0x98 && p->timer == 0xf && p->hitFlag == 1) {
		if (1.0f <= p->hyper && chak == -1 && o->state == 1 && f % 2 == 0) {
			m = 0x140;
			ai_set_mode(p, 1);
		} else {
			m = 0x50;
		}
	}
	if (p->state == 1 && (m & 0x20))
		m -= 0x20;
	ai_hitstun_escape(w, p, o, &m);
	*g = m;
	*mask = (uint16_t)m;
}

static void cpu_normal(aos_world *w, aos_player *p, uint16_t *mask) /* FUN_006fc5c0 */
{
	int32_t *g = &w->cpuGlobal[ID][1];
	int32_t m = *g;
	int32_t f = w->frame;
	float t[2] = {0, 0};
	if (f % 10 == 0)
		m = aos_cpu_rand_int(w, 0, 0xff);
	int chak = chakram_find(w, p->side);
	aos_player *o = aos_opponent(w, p);
	float d = aos_dist(w, p);
	move_modes(w, p, o, d, &m, t, 150);
	if (p->gaugeB < 0.75f && (m & 0x20))
		m -= 0x20;
	if ((m & 0x40) && 500.0f < d)
		m -= 0x40;
	if (o->state != 1 && ai_mode(p) == 1 && f % 60 == 0 && 400.0f < d) {
		if (aos_cpu_rand_int(w, 0, 200) < 50)
			m = 0x120;
	}
	if (o->state == 1 && (f + 333) % 50 == 0)
		m |= 0x80;
	/* Ghidra goto tangle at 0x6fca4f/0x6fca66: in mode 2 every 20th frame a roll
	 * >= 81 drops Dash; otherwise Dash is dropped unless Mira faces the target
	 * (t = this frame's movement target, local_14). */
	{
		bool drop;
		if (ai_mode(p) == 2 && f % 20 == 0 && aos_cpu_rand_int(w, 0, 100) >= 81)
			drop = true;
		else
			drop = !faces_target(p, t);
		if (drop && (m & 0x80))
			m -= 0x80;
	}
	if (p->state != 0 && p->state != 2 && p->timer > 0x13 && p->timer % 4 == 0) {
		bool skip = false;
		if (p->heat >= 1.0f) {
			if (aos_cpu_rand_int(w, 0, 100) > 9 && o->state != 1)
				skip = true;
		}
		if (!skip)
			m |= 0x80;
	}
	if (o->state != 0 && o->barrierTime == 0 && d < 200.0f) {
		int r = aos_cpu_rand_int(w, 0, 200);
		if (r < 10 || o->state == 1) {
			if (f % 300 > 29 || p->hyper < 1.0f || chak != -1 || o->state == 1)
				m = 0x50;
			else
				m = 0x140;
		}
	}
	if (p->state == 0x97 && p->timer == 0xe && p->hitFlag == 1)
		m = 0;
	if (p->state == 0x97 && p->timer == 0xf && p->hitFlag == 1)
		m = 0x50;
	if (p->state == 0x98 && p->timer == 0xe && p->hitFlag == 1)
		m = 0;
	if (p->state == 0x98 && p->timer == 0xf && p->hitFlag == 1) {
		if (p->hyper < 1.0f || chak != -1 || o->state != 1) {
			m = 0x50;
		} else {
			m = 0x140;
			ai_set_mode(p, 1);
		}
	}
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

void aos_register_ai_mira(void)
{
	aos_tab.cpu[ID][0] = cpu_easy;   /* 0x881460 */
	aos_tab.cpu[ID][1] = cpu_normal; /* 0x881464 */
	aos_tab.cpu[ID][2] = cpu_normal; /* 0x881468 */
}
