/* Nath (engine id 8) CPU AI — table 0x881480: diff 0 FUN_007172e0,
 * diff 1/2 FUN_00716a00. Each function fills the same 16-bit button mask a
 * human produces (core.md §2.1).
 *
 * Port notes:
 *  - Each original function keeps its mask in its own static short
 *    (DAT_009b689c diff 0, DAT_009b6898 diff 1/2) shared by both players and
 *    persisting across rounds: w->cpuGlobal[ID][0] / [1]. Only bits 0..8 are
 *    ever set, so int32 storage is equivalent.
 *  - FUN_00459f80 runs on the cosmetic stream while input is sampled
 *    (core.md §1, sim.h cpuRng) -> aos_cpu_rand_int, same calls, same order.
 *    FUN_006b04e0 = aos_cpu_steer.
 *  - player +0x160 (movement mode: 0 -> 2 on first call; 1 = orbit the arena
 *    centre at radius 400 with the DAT_00853e00 sign flip, period 400;
 *    2 = radius-300 orbit minus the opponent's position (sic), period 100)
 *    lives in p->scratch[4..7]. The unconditional "circle the opponent"
 *    target computed first is never steered to and is omitted (pure cos/sin).
 *  - player +0x164 = Nath's weapon phase (nath.md §1.5: 0 none, 1/2/3),
 *    kept by nath.c in the byte p->scratch[8].
 *  - `(o->state != 0 || o->state != 2)` in the original is always true (sic)
 *    and is dropped.
 *  - All arithmetic is SSE single precision in the original, so plain float
 *    C reproduces it. */
#include "ai_orbit.h"

#define ID AOS_CH_NATH

/* +0x164; nath.c stores it as the byte p->scratch[8] (values 0..3), so read it the same way. */
static int32_t weapon_phase(const aos_player *p) { return p->scratch[0x164 - 0x15c]; }

static void ai_clear_dirs(int32_t *m)
{
	if (*m & 1) *m -= 1;
	if (*m & 2) *m -= 2;
	if (*m & 4) *m -= 4;
	if (*m & 8) *m -= 8;
}

/* Movement and the 400-frame mode switch; `lim` = (frame+333)%200 window for
 * mode 2 (100 diff 0, 150 diff 1/2). */
static void ai_move(aos_world *w, aos_player *p, aos_player *o, int32_t *m, int lim)
{
	int32_t f = w->frame;
	float t[2];
	if (ai_mode(p) == 0)
		ai_set_mode(p, 2);
	if (ai_mode(p) == 1) {
		float a = ((float)(f % 400) / 400.0f) * AOS_2PI;
		t[0] = -aos_cos(a) * 400.0f;
		t[1] = -aos_sin(a) * 400.0f;
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
	if (f % 400 == 399 && (aos_cpu_rand_int(w, 0, 100) < 0x28 || ai_mode(p) == 2))
		ai_set_mode(p, ai_mode(p) == 1 ? 2 : 1);
}

static void ai_gauges(aos_player *p, float d, int32_t *m)
{
	if ((*m & 0x40) && 500.0f < d)
		*m -= 0x40;
	if (p->gaugeA < 0.4f && (*m & 0x10))
		*m -= 0x10;
	if (p->gaugeB < 0.4f && (*m & 0x20))
		*m -= 0x20;
}

/* Periodic special/hyper pick (0x140 summon weapon / 0x180 / 0x120). `hp` and
 * `maxHp` are read at function entry in the original. */
static void ai_special(aos_world *w, aos_player *p, aos_player *o, float d, int32_t *m, int per, int at,
                       int32_t hp, int32_t maxHp)
{
	if (o->state != 1 && 1.0f <= p->hyper && w->frame % per == at && aos_cpu_rand_int(w, 0, 200) < 0x32) {
		int32_t ph = weapon_phase(p);
		if ((ai_mode(p) == 1 && 1.5f <= p->hyper && ph < 3) || ph < 3 - hp / (maxHp / 3))
			*m = 0x140;
		else if (d <= 400.0f)
			*m = 0x180;
		else
			*m = 0x120;
	}
}

/* Weapon phase 1/2 close-range attack. */
static void ai_phase12(aos_world *w, aos_player *p, aos_player *o, float d, int32_t *m, int rmax)
{
	int32_t f = w->frame;
	int32_t ph = weapon_phase(p);
	if ((ph == 1 || ph == 2) && o->barrierTime == 0 && d < 300.0f && f % 5 == 1 &&
	    (aos_cpu_rand_int(w, 0, rmax) < 0x14 || o->state == 1)) {
		if (ph != 1 || 0x1d < f % 300 || p->hyper < 1.0f || o->state == 1)
			*m = 0x50;
		else
			*m = 0x100;
	}
}

static void ai_diff0(aos_world *w, aos_player *p, uint16_t *out) /* FUN_007172e0 */
{
	int32_t *m = &w->cpuGlobal[ID][0];
	int32_t f = w->frame;
	if (f % 0xf == 0)
		*m = aos_cpu_rand_int(w, 0, 0xff);
	aos_player *o = aos_opponent(w, p);
	float d = aos_dist(w, p);
	int32_t maxHp = p->maxHp, hp = p->hp;
	ai_move(w, p, o, m, 100);
	ai_gauges(p, d, m);
	ai_special(w, p, o, d, m, 0x78, 1, hp, maxHp);
	ai_phase12(w, p, o, d, m, 300);
	if (weapon_phase(p) == 3 && o->barrierTime == 0 && f % 5 == 3 &&
	    (aos_cpu_rand_int(w, 0, 300) < 0x14 || o->state == 1)) {
		if (240.0f <= d || o->state != 1) {
			if (f % 300 < 0x1e && f % 2 == 0 && 1.0f <= p->hyper && aos_cpu_rand_int(w, 0, 200) < 0x1e &&
			    o->state != 1) {
				*m = 0x100;
			} else if (aos_cpu_rand_int(w, 0, 300) < 0x1e) {
				*m = 0x50;
			}
		} else {
			*m = 0x140;
		}
	}
	if (p->state == 1 && (*m & 0x20))
		*m -= 0x20;
	ai_hitstun_escape(w, p, o, m);
	*out = (uint16_t)*m;
}

static void ai_diff12(aos_world *w, aos_player *p, uint16_t *out) /* FUN_00716a00 */
{
	int32_t *m = &w->cpuGlobal[ID][1];
	int32_t f = w->frame;
	if (f % 10 == 0)
		*m = aos_cpu_rand_int(w, 0, 0xff);
	aos_player *o = aos_opponent(w, p);
	float d = aos_dist(w, p);
	int32_t maxHp = p->maxHp, hp = p->hp;
	ai_move(w, p, o, m, 0x96);
	ai_gauges(p, d, m);
	ai_special(w, p, o, d, m, 0x3c, 0, hp, maxHp);
	ai_phase12(w, p, o, d, m, 200);
	if (weapon_phase(p) == 3 && o->barrierTime == 0 && f % 5 == 3 &&
	    (aos_cpu_rand_int(w, 0, 200) < 0x14 || o->state == 1)) {
		if (240.0f <= d || o->state != 1) {
			if (f % 300 < 0x1e && 1.0f <= p->hyper && aos_cpu_rand_int(w, 0, 200) < 0x1e && o->state != 1)
				*m = 0x100;
			else if (aos_cpu_rand_int(w, 0, 200) < 0x1e)
				*m = 0x50;
		} else {
			*m = 0x140;
		}
	}
	if (1 < w->difficulty && (weapon_phase(p) == 1 || o->hp < o->maxHp / 5) && 3.0f <= p->hyper &&
	    (aos_cpu_rand_int(w, 0, 100) + f) % 0x32 < 10)
		*m = 0x160;
	if (p->state == 1 && (*m & 0x20) && (p->heat <= 1.25f || p->maxHp / 3 < p->hp))
		*m -= 0x20;
	ai_hitstun_escape(w, p, o, m);
	*out = (uint16_t)*m;
}

void aos_register_ai_nath(void)
{
	aos_tab.cpu[ID][0] = ai_diff0;  /* 0x881480 FUN_007172e0 */
	aos_tab.cpu[ID][1] = ai_diff12; /* 0x881484 FUN_00716a00 */
	aos_tab.cpu[ID][2] = ai_diff12; /* 0x881488 FUN_00716a00 */
}
