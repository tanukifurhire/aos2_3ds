/* Sumika (engine id 17) and Sumika's second form (id 18, story final boss)
 * CPU AI — tables 0x881510 / 0x881520:
 *   Sumika  diff 0 FUN_0077bd50, diff 1/2 FUN_0077b420
 *   SumikaB diff 0 FUN_00772ae0, diff 1/2 FUN_007721f0
 * Each function fills the same 16-bit button mask a human produces
 * (core.md §2.1).
 *
 * The second-form functions are the first form's with these differences
 * (form2 flag below):
 *  - state ids shifted to the second form's (0x21e/0x21f/0x21c -> 0x242/0x243/0x240);
 *  - whole body skipped when DAT_0087faac == 3 (see UNVERIFIED);
 *  - a final "statusOn[6] (+0xa9) set -> mask 0";
 *  - diff 1/2: no "difficulty > 1 and 3 hyper -> 0x160" block.
 *
 * Port notes:
 *  - Each original function keeps its mask in its own static short
 *    (Sumika DAT_009b68d4 / DAT_009b68d0, SumikaB DAT_009b68cc / DAT_009b68c8),
 *    shared by both players and persisting across rounds:
 *    w->cpuGlobal[id][0] (diff 0) / [1] (diff 1/2). Only bits 0..8 are ever
 *    set, so int32 storage is equivalent.
 *  - FUN_00459f80 runs on the cosmetic stream while input is sampled
 *    (core.md §1, sim.h cpuRng) -> aos_cpu_rand_int, same calls, same order.
 *    FUN_006b04e0 = aos_cpu_steer.
 *  - player +0x160 (movement mode: 0 -> 2 on first call; 1 = orbit the arena
 *    centre at radius 280 with the DAT_00853e00 sign flip, period 300;
 *    2 = radius-300 orbit minus the opponent's position (sic), period 100)
 *    lives in p->scratch[4..7]. The unconditional "circle the opponent"
 *    target computed first is never steered to (mode is never 0 by then)
 *    and is omitted (pure cos/sin).
 *  - +0xab of the opponent = statusOn[8]; +0xa9 = statusOn[6].
 *  - All arithmetic is SSE single precision in the original (checked in the
 *    disassembly), so plain float C reproduces it.
 *
 * UNVERIFIED: DAT_0087faac == 3 (a mode set by FUN_00645530 / menu
 * FUN_00643150, sumika.md §0.4) makes the second-form AI return without
 * writing the mask. The engine has no field for DAT_0087faac, so the port
 * always runs the body (ENGINE REQUEST: expose it in aos_world). */
#include "ai_orbit.h"

static void ai_clear_dirs(int32_t *m)
{
	if (*m & 1) *m -= 1;
	if (*m & 2) *m -= 2;
	if (*m & 4) *m -= 4;
	if (*m & 8) *m -= 8;
}

typedef struct {
	int id;
	bool form2;
	int32_t stShot, stShot2, stNoHyper; /* 0x21e/0x21f/0x21c or 0x242/0x243/0x240 */
} sumika_ai;

static const sumika_ai FORM1 = {AOS_CH_SUMIKA, false, 0x21e, 0x21f, 0x21c};
static const sumika_ai FORM2 = {AOS_CH_SUMIKA2, true, 0x242, 0x243, 0x240};

/* Movement and the 400-frame mode switch (identical in all four functions). */
static void ai_move(aos_world *w, aos_player *p, aos_player *o, int32_t *m)
{
	int32_t f = w->frame;
	float t[2];
	if (ai_mode(p) == 0)
		ai_set_mode(p, 2);
	if (ai_mode(p) == 1) {
		float a = ((float)(f % 300) / 300.0f) * AOS_2PI;
		t[0] = -aos_cos(a) * 280.0f;
		t[1] = -aos_sin(a) * 280.0f;
		ai_clear_dirs(m);
		aos_cpu_steer(m, p, t);
	}
	if (ai_mode(p) == 2) {
		float a = ((float)(f % 100) / 100.0f) * AOS_2PI;
		t[0] = aos_cos(a) * 300.0f - o->x;
		t[1] = aos_sin(a) * 300.0f - o->y;
		if ((f + 0x14d) % 200 < 0x96) {
			ai_clear_dirs(m);
			aos_cpu_steer(m, p, t);
		}
	}
	if (f % 400 == 399 && (aos_cpu_rand_int(w, 0, 100) < 0x28 || ai_mode(p) == 2))
		ai_set_mode(p, ai_mode(p) == 1 ? 2 : 1);
}

/* Gauge handling and the shot-chain follow-ups (identical in all four). */
static void ai_gauges(aos_world *w, aos_player *p, const sumika_ai *c, float d, int32_t *m)
{
	int32_t f = w->frame;
	if ((*m & 0x40) && 500.0f < d)
		*m -= 0x40;
	if (p->gaugeB < 0.5f && (*m & 0x20))
		*m -= 0x20;
	if (500.0f < d && ai_mode(p) == 1 && (f + 0x14d) % 400 == 399 && 1.0f <= p->gaugeB)
		*m = 0x60;
	if (p->gaugeB < 1.0f && (*m & 0x20) && (*m & 0x40))
		*m -= 0x60;
	if (p->state == c->stShot && p->timer == 0x15 && (f / 100) % 3 == 0)
		*m |= 0x10;
	if (p->state == c->stShot2 && p->timer == 0x12 && (f / 100) % 3 == 0)
		*m |= 0x10;
}

/* Hyper when the opponent's statusOn[8] (+0xab) is set; `per` 300 diff 0, 100 diff 1/2. */
static void ai_status_hyper(aos_world *w, aos_player *p, aos_player *o, const sumika_ai *c, int32_t *m, int per)
{
	int32_t f = w->frame;
	if (p->state != c->stNoHyper && 1.0f <= p->hyper && o->statusOn[8] == 1) {
		if (f % per == 99)
			*m = 0x120;
		else if (f % per == 0x62)
			*m = 0;
	}
}

static void ai_diff0(aos_world *w, aos_player *p, uint16_t *out, const sumika_ai *c)
{
	/* form2: `if (DAT_0087faac == 3) return;` � not modelled (see UNVERIFIED) */
	int32_t *m = &w->cpuGlobal[c->id][0];
	int32_t f = w->frame;
	if (f % 10 == 0)
		*m = aos_cpu_rand_int(w, 0, 0xff);
	aos_player *o = aos_opponent(w, p);
	float d = aos_dist(w, p);
	ai_move(w, p, o, m);
	ai_gauges(w, p, c, d, m);
	if (o->state != 1 && ai_mode(p) == 1 && f % 100 == 0 && 400.0f < d && aos_cpu_rand_int(w, 0, 200) < 0x1e)
		*m = 0x120;
	if (o->state != 2 && o->state != 0 && o->barrierTime == 0 && 1.0f <= p->hyper && ai_mode(p) == 2 &&
	    f % 400 == 299 && 400.0f < d)
		*m = 0x100;
	if (o->state == 1 && o->barrierTime == 0 && 1.0f <= p->hyper && ai_mode(p) == 1 && f % 500 == 299 &&
	    d < 300.0f)
		*m = 0x140;
	ai_status_hyper(w, p, o, c, m, 300);
	if (p->state == 1 && (*m & 0x20))
		*m -= 0x20;
	ai_hitstun_escape(w, p, o, m);
	if (c->form2 && p->statusOn[6] == 1)
		*m = 0;
	*out = (uint16_t)*m;
}

static void ai_diff12(aos_world *w, aos_player *p, uint16_t *out, const sumika_ai *c)
{
	/* form2: `if (DAT_0087faac == 3) return;` � not modelled (see UNVERIFIED) */
	int32_t *m = &w->cpuGlobal[c->id][1];
	int32_t f = w->frame;
	if (f % 10 == 0)
		*m = aos_cpu_rand_int(w, 0, 0xff);
	aos_player *o = aos_opponent(w, p);
	float d = aos_dist(w, p);
	ai_move(w, p, o, m);
	ai_gauges(w, p, c, d, m);
	/* dash-cancel long states every 4 frames (when overheated: 10% or vs hitstun) */
	if (p->state != 0 && p->state != 2 && 0x13 < p->timer && p->timer % 4 == 0 &&
	    (p->heat < 1.0f || aos_cpu_rand_int(w, 0, 100) < 10 || o->state == 1))
		*m |= 0x80;
	if (o->state != 1 && ai_mode(p) == 1 && f % 0x3c == 0 && 400.0f < d && aos_cpu_rand_int(w, 0, 200) < 0x32)
		*m = (f / 0xf0) % 3 == 0 ? 0x180 : 0x120;
	if (o->state != 2 && o->state != 0 && o->barrierTime == 0 && 1.0f <= p->hyper && ai_mode(p) == 2 &&
	    f % 300 == 299 && 400.0f < d)
		*m = 0x100;
	if (o->state == 1 && o->barrierTime == 0 && 1.0f <= p->hyper && ai_mode(p) == 1 && f % 400 == 299 &&
	    d < 300.0f)
		*m = 0x140;
	ai_status_hyper(w, p, o, c, m, 100);
	if (!c->form2 && 1 < w->difficulty && 3.0f <= p->hyper && o->state == 1 && d < 300.0f &&
	    (aos_cpu_rand_int(w, 0, 100) + f) % 0x32 < 10)
		*m = 0x160;
	if (p->state == 1 && (*m & 0x20) && (p->heat <= 1.25f || p->maxHp / 3 < p->hp))
		*m -= 0x20;
	ai_hitstun_escape(w, p, o, m);
	if (c->form2 && p->statusOn[6] == 1)
		*m = 0;
	*out = (uint16_t)*m;
}

static void ai_s1_diff0(aos_world *w, aos_player *p, uint16_t *out) { ai_diff0(w, p, out, &FORM1); }   /* FUN_0077bd50 */
static void ai_s1_diff12(aos_world *w, aos_player *p, uint16_t *out) { ai_diff12(w, p, out, &FORM1); } /* FUN_0077b420 */
static void ai_s2_diff0(aos_world *w, aos_player *p, uint16_t *out) { ai_diff0(w, p, out, &FORM2); }   /* FUN_00772ae0 */
static void ai_s2_diff12(aos_world *w, aos_player *p, uint16_t *out) { ai_diff12(w, p, out, &FORM2); } /* FUN_007721f0 */

void aos_register_ai_sumika(void)
{
	aos_tab.cpu[AOS_CH_SUMIKA][0] = ai_s1_diff0;   /* 0x881510 */
	aos_tab.cpu[AOS_CH_SUMIKA][1] = ai_s1_diff12;  /* 0x881514 */
	aos_tab.cpu[AOS_CH_SUMIKA][2] = ai_s1_diff12;  /* 0x881518 */
	aos_tab.cpu[AOS_CH_SUMIKA2][0] = ai_s2_diff0;  /* 0x881520 */
	aos_tab.cpu[AOS_CH_SUMIKA2][1] = ai_s2_diff12; /* 0x881524 */
	aos_tab.cpu[AOS_CH_SUMIKA2][2] = ai_s2_diff12; /* 0x881528 */
}
