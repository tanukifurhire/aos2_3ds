/* Shared pieces of the CPU AI functions (FUN_0075b420 family). Most characters'
 * AIs are the same template: a persistent random mask, a movement mode in
 * p+0x160 (scratch[4]) choosing between two target points, and the same
 * hitstun tech-dash tail. Header-only (static inline) so any ai_<name>.c can use it. */
#ifndef AOS_AI_ORBIT_H
#define AOS_AI_ORBIT_H

#include "../sim.h"

#include <string.h>

static inline int32_t ai_mode(const aos_player *p)
{
	int32_t v;
	memcpy(&v, &p->scratch[0x160 - 0x15c], sizeof v);
	return v;
}

static inline void ai_set_mode(aos_player *p, int32_t v) { memcpy(&p->scratch[0x160 - 0x15c], &v, sizeof v); }

/* ((float)(frame % period) / div) * 2π, rounded to float as it is passed to cos/sin. */
static inline float ai_phase(int32_t n, double div) { return (float)((double)(float)n / div * (double)AOS_2PI); }

/* Circle around the opponent: radius (dist + 50) * 0.25, period 300 frames. */
static inline void ai_target_near(aos_world *w, const aos_player *o, float dist, float t[2])
{
	float a = ai_phase(w->frame % 300, 300.0);
	t[0] = (float)((double)aos_cos(a) * ((double)dist + 50.0) * 0.25 + (double)o->x);
	t[1] = (float)((double)aos_sin(a) * ((double)dist + 50.0) * 0.25 + (double)o->y);
}

/* Radius-300 orbit (period 100) minus the opponent's position (sic). */
static inline void ai_target_far(aos_world *w, const aos_player *o, float t[2])
{
	float a = ai_phase(w->frame % 100, 100.0);
	t[0] = (float)((double)aos_cos(a) * 300.0 - (double)o->x);
	t[1] = (float)((double)aos_sin(a) * 300.0 - (double)o->y);
}

/* Shared tail of every AI: in hitstun, maybe tech-dash out on timer 15. */
static inline void ai_hitstun_escape(aos_world *w, aos_player *p, aos_player *o, int32_t *g)
{
	if (p->state == 1 && p->timer == 0xe)
		*g = 0;
	if (p->state == 1 && p->timer == 0xf) {
		int32_t r = aos_cpu_rand_int(w, 0, 100);
		if ((o->comboShowDmg > 1500 && r > 50) || (o->comboShowDmg > 1000 && r > 70) || r > 90)
			*g |= AOS_IN_D;
	}
}

#endif
