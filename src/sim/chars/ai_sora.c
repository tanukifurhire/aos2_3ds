/* Sora (engine id 3) CPU AI: table 0x881400 + 3*0x10 + diff*4 = 0x881430..
 * diff 0 FUN_0073c750, diff 1/2 FUN_0073bdf0 (registry.c:171-173).
 * Same template as Suguri's AI (see ai_orbit.h / ai_suguri.c). */
#include "ai_orbit.h"

#define G_EASY 0 /* DAT_009b68b4 */
#define G_STD 1  /* DAT_009b68b0 */

static void ai_easy(aos_world *w, aos_player *p, uint16_t *mask) /* FUN_0073c750 */
{
	int32_t *g = &w->cpuGlobal[AOS_CH_SORA][G_EASY];
	int32_t f = w->frame;
	if (f % 15 == 0)
		*g = aos_cpu_rand_int(w, 0, 0xff);
	if (f % 170 > 0x46)
		*g &= ~(AOS_IN_A | AOS_IN_B);
	aos_player *o = aos_opponent(w, p);
	float dist = aos_dist(w, p);
	float t[2];
	ai_target_near(w, o, dist, t);
	if (ai_mode(p) == 0)
		ai_set_mode(p, 2);
	if (ai_mode(p) == 1) {
		ai_target_near(w, o, dist, t);
		if ((f + 0x14d) % 200 < 100) {
			*g &= ~0xf;
			aos_cpu_steer(g, p, t);
		}
	}
	if (ai_mode(p) == 2) {
		ai_target_far(w, o, t);
		if ((f + 0x14d) % 200 < 100) {
			*g &= ~0xf;
			aos_cpu_steer(g, p, t);
		}
	}
	if (f % 400 == 399) {
		int32_t r = aos_cpu_rand_int(w, 0, 100);
		if (r < 0x28 || ai_mode(p) == 2)
			ai_set_mode(p, ai_mode(p) == 1 ? 2 : 1);
	}
	if ((*g & AOS_IN_S) && 500.0f < dist)
		*g -= AOS_IN_S;
	if (f % 1000 < 0x1e && 500.0f < dist && (double)aos_tab.cost[0x1f].b * 3.0 < (double)p->gaugeB)
		*g = 0x20;
	if (p->state == 0x1f && p->timer == 10)
		*g = 0;
	if (o->state == 1 && o->barrierTime == 0 && dist < 400.0f)
		*g = 0x60;
	if (o->state != 0 && o->barrierTime == 0 && dist < 200.0f) {
		int32_t r = aos_cpu_rand_int(w, 0, 300);
		if (r < 10 || o->state == 1) {
			if (f % 400 > 0x1d || p->hyper < 1.0f || o->state == 1)
				*g = 0x50;
			else
				*g = 0x140;
		}
	}
	if (p->state == 0x18 && p->timer == 0xe && o->state == 1)
		*g = 0;
	if (p->state == 0x18 && p->timer == 0xf && o->state == 1)
		*g = 0x50;
	if (p->state == 0x19 && p->timer == 0xe && o->state == 1)
		*g = 0;
	if (p->state == 0x19 && p->timer == 0xf && o->state == 1)
		*g = 0x50;
	if (p->state == 0x1a && p->timer == 0x14 && o->state == 1 && f % 2 == 0)
		*g = 0x100;
	if (p->state == 1 && (*g & AOS_IN_B))
		*g -= AOS_IN_B;
	ai_hitstun_escape(w, p, o, g);
	*mask = (uint16_t)*g;
}

static void ai_std(aos_world *w, aos_player *p, uint16_t *mask) /* FUN_0073bdf0 (diff 1 and 2) */
{
	int32_t *g = &w->cpuGlobal[AOS_CH_SORA][G_STD];
	int32_t f = w->frame;
	if (f % 10 == 0)
		*g = aos_cpu_rand_int(w, 0, 0xff);
	aos_player *o = aos_opponent(w, p);
	float dist = aos_dist(w, p);
	float t[2];
	ai_target_near(w, o, dist, t);
	if (ai_mode(p) == 0)
		ai_set_mode(p, 2);
	if (ai_mode(p) == 1) {
		ai_target_near(w, o, dist, t);
		if ((f + 0x14d) % 200 < 0x96) {
			*g &= ~0xf;
			aos_cpu_steer(g, p, t);
		}
	}
	if (ai_mode(p) == 2) {
		ai_target_far(w, o, t);
		if ((f + 0x14d) % 200 < 0x96) {
			*g &= ~0xf;
			aos_cpu_steer(g, p, t);
		}
	}
	if (f % 400 == 399) {
		int32_t r = aos_cpu_rand_int(w, 0, 100);
		if (r < 0x28 || ai_mode(p) == 2)
			ai_set_mode(p, ai_mode(p) == 1 ? 2 : 1);
	}
	if ((*g & AOS_IN_S) && 500.0f < dist)
		*g -= AOS_IN_S;
	if (o->state == 1 && (f + 0x14d) % 0x32 == 0)
		*g |= AOS_IN_D;
	if (f % 500 < 0x1e && 500.0f < dist && (double)aos_tab.cost[0x1f].b * 3.0 < (double)p->gaugeB)
		*g = 0x20;
	if (p->state == 0x1f && p->timer == 10 && dist < 400.0f && f % 3 == 1)
		*g = 0;
	/* dash-cancel out of long states every 4 frames (unless overheated and unlucky) */
	if (p->state != 0 && p->state != 2 && p->timer > 0x13 && p->timer % 4 == 0) {
		bool skip = false;
		if (p->heat >= 1.0f) {
			int32_t r = aos_cpu_rand_int(w, 0, 100);
			if (r > 9 && o->state != 1)
				skip = true;
		}
		if (!skip)
			*g |= AOS_IN_D;
	}
	if (o->state == 1 && o->barrierTime == 0 && dist < 400.0f)
		*g = 0x60;
	if (o->state != 0 && o->barrierTime == 0 && dist < 200.0f) {
		int32_t r = aos_cpu_rand_int(w, 0, 200);
		if (r < 10 || o->state == 1) {
			if (f % 300 > 0x1d || p->hyper < 1.0f || o->state == 1)
				*g = 0x50;
			else
				*g = 0x140;
		}
	}
	if (p->state == 0x18 && p->timer == 0xe && o->state == 1)
		*g = 0;
	if (p->state == 0x18 && p->timer == 0xf && o->state == 1) {
		int32_t r = aos_cpu_rand_int(w, 0, 100);
		if (r < 0x50 || p->gaugeA < aos_tab.cost[0x15].a)
			*g = 0x50;
		else
			*g = 0x60;
	}
	if (p->state == 0x19 && p->timer == 0xe && o->state == 1)
		*g = 0;
	if (p->state == 0x19 && p->timer == 0xf && o->state == 1)
		*g = 0x50;
	if (p->state == 0x1a && p->timer == 0x14 && o->state == 1)
		*g = 0x100;
	if (p->state == 0x1b && p->timer == 0xf && o->state == 1)
		*g = 0;
	if (p->state == 0x1b && p->timer == 0x10 && o->state == 1)
		*g = 0x10;
	if (w->difficulty > 1 && 3.0f <= p->hyper && o->state == 1) {
		int32_t r = aos_cpu_rand_int(w, 0, 100);
		if ((r + f) % 0x32 < 10)
			*g = 0x160;
	}
	if (p->state == 1 && (*g & AOS_IN_B) && (p->heat <= 1.25f || p->maxHp / 3 < p->hp))
		*g -= AOS_IN_B;
	ai_hitstun_escape(w, p, o, g);
	*mask = (uint16_t)*g;
}

void aos_register_ai_sora(void)
{
	aos_tab.cpu[AOS_CH_SORA][0] = ai_easy;
	aos_tab.cpu[AOS_CH_SORA][1] = ai_std;
	aos_tab.cpu[AOS_CH_SORA][2] = ai_std;
}
