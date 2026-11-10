/* Alte (engine id 4) CPU AI: table 0x881400 + 4*0x10 + diff*4 = 0x881440..
 * diff 0 FUN_006a86a0, diff 1/2 FUN_006a7dd0 (registry.c:174-176).
 * Same template as Suguri's AI (see ai_orbit.h), except mode 1 heads for a
 * fixed radius-320 circle around the arena centre (and always steers), and the
 * hyper choice depends on whether her Searchlight Panel is out. */
#include "ai_orbit.h"

#define G_EASY 0 /* DAT_009b6814 */
#define G_STD 1  /* DAT_009b6810 */

/* FUN_006a0e50(team): index of the team's Searchlight Panel (Enemy type 4, id 100) or -1. */
static int find_panel(aos_world *w, int32_t team)
{
	for (int i = 0; i < w->obj.nenemy; i++) {
		aos_enemy *e = &w->obj.enemy[i];
		if (e->team == team && e->type == 4 && e->f10 == 100)
			return i;
	}
	return -1;
}

/* Mode 1 target: -(cos, sin)(phase) * 320 (DAT_00853e00 sign flip, DAT_008178c0). */
static void ai_target_ring(aos_world *w, float t[2])
{
	float a = ai_phase(w->frame % 300, 300.0);
	t[0] = -aos_cos(a) * 320.0f;
	t[1] = -aos_sin(a) * 320.0f;
}

static void ai_easy(aos_world *w, aos_player *p, uint16_t *mask) /* FUN_006a86a0 */
{
	int32_t *g = &w->cpuGlobal[AOS_CH_ALTE][G_EASY];
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
		ai_target_ring(w, t);
		*g &= ~0xf;
		aos_cpu_steer(g, p, t);
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
	if (p->gaugeB < 0.4f && (*g & AOS_IN_B))
		*g -= AOS_IN_B;
	if (500.0f < dist && ai_mode(p) == 1 && (f + 0x14d) % 800 == 399 && 1.0f <= p->gaugeB)
		*g = 0x60;
	if (o->state != 1 && f % 0x78 == 0 && 400.0f < dist) {
		if (aos_cpu_rand_int(w, 0, 200) < 0x32)
			*g = find_panel(w, p->side) == -1 ? 0x100 : 0x120;
	}
	if ((p->state == 0x3c || p->state == 0x3d) && p->timer < 0x16 && aos_tab.cost[0x3d].a < p->gaugeA &&
	    f % 100 < 0x3c && o->state == 1)
		*g = 0x10;
	if ((p->state == 0x3c || p->state == 0x3d) && p->timer < 0x16 && 0.3f < p->gaugeA && f % 100 < 0x3c &&
	    500.0f < dist)
		*g = 0x10;
	if (p->state == 0x3f && p->timer == 9 && (f / 10) % 2 == 0)
		*g = 0;
	if (o->state != 0 && o->barrierTime == 0 && dist < 300.0f) {
		int32_t r = aos_cpu_rand_int(w, 0, 300);
		if (r < 5 || o->state == 1) {
			if (f % 300 < 0x1e && f % 2 == 0 && 1.0f <= p->hyper && o->state != 1)
				*g = 0x140;
			else
				*g = 0x50;
		}
	}
	/* (o->state != 0 || o->state != 2) is always true in the original */
	if (o->barrierTime == 0 && dist < 200.0f) {
		int32_t r = aos_cpu_rand_int(w, 0, 300);
		if ((r < 10 || o->state == 1) && find_panel(w, p->side) != -1)
			*g = 0x100;
	}
	if (p->state == 1 && (*g & AOS_IN_B))
		*g -= AOS_IN_B;
	ai_hitstun_escape(w, p, o, g);
	*mask = (uint16_t)*g;
}

static void ai_std(aos_world *w, aos_player *p, uint16_t *mask) /* FUN_006a7dd0 (diff 1 and 2) */
{
	int32_t *g = &w->cpuGlobal[AOS_CH_ALTE][G_STD];
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
		ai_target_ring(w, t);
		*g &= ~0xf;
		aos_cpu_steer(g, p, t);
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
	if (p->gaugeB < 0.4f && (*g & AOS_IN_B))
		*g -= AOS_IN_B;
	if (500.0f < dist && ai_mode(p) == 1 && (f + 0x14d) % 400 == 399 && 1.0f <= p->gaugeB)
		*g = 0x60;
	if (o->state != 1 && f % 0x3c == 0 && 400.0f < dist) {
		if (aos_cpu_rand_int(w, 0, 200) < 0x32)
			*g = find_panel(w, p->side) == -1 ? 0x100 : 0x120;
	}
	if ((p->state == 0x3c || p->state == 0x3d) && p->timer < 0x16 && aos_tab.cost[0x3d].a < p->gaugeA &&
	    o->state == 1)
		*g = 0x10;
	if ((p->state == 0x3c || p->state == 0x3d) && p->timer < 0x16 && 0.3f < p->gaugeA && 500.0f < dist)
		*g = 0x10;
	if (p->state == 0x3f && p->timer == 9 && (f / 10) % 2 == 0)
		*g = 0;
	if (o->state != 0 && o->barrierTime == 0 && dist < 300.0f) {
		int32_t r = aos_cpu_rand_int(w, 0, 200);
		if (r < 5 || o->state == 1) {
			if (f % 300 > 0x1d || p->hyper < 1.0f || o->state == 1)
				*g = 0x50;
			else
				*g = 0x140;
		}
	}
	/* (o->state != 0 || o->state != 2) is always true in the original */
	if (o->barrierTime == 0 && dist < 200.0f) {
		int32_t r = aos_cpu_rand_int(w, 0, 200);
		if ((r < 10 || o->state == 1) && find_panel(w, p->side) != -1)
			*g = 0x100;
	}
	if ((p->state == 0x44 || p->state == 0x45) && p->timer == 0x1d)
		*g = 0;
	if ((p->state == 0x44 || p->state == 0x45) && p->timer == 0x1e && o->state != 1)
		*g = 0x20;
	if (w->difficulty > 1 && 3.0f <= p->hyper && o->state == 1 && dist < 200.0f) {
		int32_t r = aos_cpu_rand_int(w, 0, 100);
		if ((r + f) % 0x32 < 10)
			*g = 0x160;
	}
	if (p->state == 1 && (*g & AOS_IN_B) && (p->heat <= 1.25f || p->maxHp / 3 < p->hp))
		*g -= AOS_IN_B;
	ai_hitstun_escape(w, p, o, g);
	*mask = (uint16_t)*g;
}

void aos_register_ai_alte(void)
{
	aos_tab.cpu[AOS_CH_ALTE][0] = ai_easy;
	aos_tab.cpu[AOS_CH_ALTE][1] = ai_std;
	aos_tab.cpu[AOS_CH_ALTE][2] = ai_std;
}
