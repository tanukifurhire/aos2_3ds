/* Object system — containers, generic managers, shape test and all collision
 * passes, ported from the decompiled functions named on each routine
 * (docs/re/objects.md). Quirks of the original loops are kept on purpose and
 * marked "literal". */
#include <string.h>

#include "sim.h"
#include "fx.h"
#include "motion.h"
#include "perf.h"

aos_obj_tables aos_otab;

/* ------------------------------------------------------------------ */
/* containers                                                          */

#define ERASE(arr, n, i)                                                        \
	do {                                                                        \
		memmove(&(arr)[(i)], &(arr)[(i) + 1], sizeof((arr)[0]) * (size_t)((n) - (i) - 1)); \
		AOS_PERF_ADD(erase_bytes, sizeof((arr)[0]) * (size_t)((n) - (i) - 1)); \
		AOS_PERF_ADD(erases, 1);                                                \
		AOS_CLEAR_DEAD(&(arr)[(n) - 1], sizeof((arr)[0])); /* objects.h */      \
		(n)--;                                                                  \
	} while (0)

static void beam_erase(aos_world *w, int i) { ERASE(w->obj.beam, w->obj.nbeam, i); }
static void missile_erase(aos_world *w, int i) { ERASE(w->obj.missile, w->obj.nmissile, i); }
static void laser_erase(aos_world *w, int i) { ERASE(w->obj.laser, w->obj.nlaser, i); }
static void enemy_erase(aos_world *w, int i) { ERASE(w->obj.enemy, w->obj.nenemy, i); }
static void whip_erase(aos_world *w, int i) { ERASE(w->obj.whip, w->obj.nwhip, i); }

static void copy3(float d[3], const float s[3]) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }

static int beam_holes; /* see aos_beams_update */
#ifndef AOS_BEAM_COMPACT_AT
#define AOS_BEAM_COMPACT_AT AOS_MAX_BEAM
#endif
_Static_assert(AOS_BEAM_COMPACT_AT <= AOS_MAX_BEAM, "see aos_effects_update");

int aos_beam_new(aos_world *w, int32_t type, int32_t id, const float pos[3], float angle,
                 int32_t ownerId, int32_t age0, int32_t team)
{
	if (w->obj.nbeam - beam_holes >= AOS_MAX_BEAM) { /* logical count (aos_beams_update) */
		w->obj.overflow++;
		return -1;
	}
	int i = w->obj.nbeam++;
	aos_beam *b = &w->obj.beam[i];
	memset(b, 0, sizeof *b);
	/* FUN_004ba490 template */
	b->type = type;
	b->id = id;
	b->age = age0;
	b->ownerId = ownerId;
	b->hitType = 1;
	b->team = team;
	b->serial = w->obj.beamSerial++;
	b->link = -1;
	copy3(b->pos, pos);
	b->destroyOnHit = 1;
	b->scale = 1.0f;
	b->alpha = 1.0f;
	b->moveAngle = angle;
	b->angle = angle;
	b->active = 1;
	b->stun = 1.0f;
	return i;
}

int aos_missile_new(aos_world *w, int32_t type, int32_t id, const float pos[3], float angle,
                    int32_t ownerId, int32_t age0, int32_t team, int32_t p18)
{
	if (w->obj.nmissile >= AOS_MAX_MISSILE) {
		w->obj.overflow++;
		return -1;
	}
	int i = w->obj.nmissile++;
	aos_missile *m = &w->obj.missile[i];
	memset(m, 0, sizeof *m);
	/* FUN_00685bb0 template */
	m->type = type;
	m->id = id;
	m->age = age0;
	m->ownerId = ownerId;
	m->hitType = 2;
	m->team = team;
	m->p18 = p18;
	m->serial = w->obj.missileSerial++;
	m->link = -1;
	copy3(m->pos, pos);
	m->growScale = 1.0f;
	m->f60 = 1.0f;
	m->moveAngle = angle;
	m->angle = angle;
	m->hp = 60;
	m->active = 1;
	m->stun = 1.0f;
	return i;
}

int aos_hitbox_new(aos_world *w, int32_t type, int32_t id, const float pos[3], float angle,
                   int32_t ownerId, int32_t zero, int32_t team)
{
	if (w->obj.nhitbox >= AOS_MAX_HITBOX) {
		w->obj.overflow++;
		return -1;
	}
	int i = w->obj.nhitbox++;
	aos_hitbox *h = &w->obj.hitbox[i];
	memset(h, 0, sizeof *h);
	h->dealsDamage = 1;
	h->type = type;
	h->id = id;
	h->f0c = zero;
	h->ownerId = ownerId;
	h->hitType = 1;
	h->team = team;
	copy3(h->pos, pos);
	h->angle = angle;
	h->stun = 1.0f;
	return i;
}

int aos_laser_new(aos_world *w, int32_t type, int32_t id, const float pos[3], float angle,
                  int32_t ownerId, int32_t team, int32_t p18)
{
	if (w->obj.nlaser >= AOS_MAX_LASER) {
		w->obj.overflow++;
		return -1;
	}
	int i = w->obj.nlaser++;
	aos_laser *l = &w->obj.laser[i];
	memset(l, 0, sizeof *l);
	/* FUN_006307d0 template */
	l->type = type;
	l->id = id;
	l->blockMode = 1;
	l->hitType = 3;
	l->team = team;
	l->p18 = p18;
	l->ownerId = ownerId;
	copy3(l->origin, pos);
	l->angle = angle;
	l->stun = 1;
	return i;
}

int aos_enemy_new(aos_world *w, int32_t type, int32_t ownerId, int32_t team, int32_t p18)
{
	if (w->obj.nenemy >= AOS_MAX_ENEMY) {
		w->obj.overflow++;
		return -1;
	}
	int i = w->obj.nenemy++;
	aos_enemy *e = &w->obj.enemy[i];
	memset(e, 0, sizeof *e);
	/* FUN_0055ee70 template */
	e->id = w->obj.enemyId++;
	e->serial = 0; /* group id: set by the spawner via aos_enemy_group_next (FUN_0055efc0) */
	e->ownerId = ownerId;
	e->type = type;
	e->team = team;
	e->p18 = p18;
	e->drawLayer = 1;
	e->scale[0] = e->scale[1] = e->scale[2] = 1.0f;
	e->mirrorChildren = 1;
	e->parentId = -1;
	for (int k = 0; k < 10; k++)
		e->childSlots[k] = -1;
	return i;
}

static int effect_holes; /* see aos_effects_update */

int aos_effect_new(aos_world *w, int32_t type, int32_t id, int32_t variant, const float pos[3],
                   float angle, int32_t ownerId, int32_t age0, int32_t team)
{
	if (w->obj.neffect - effect_holes >= AOS_MAX_EFFECT) { /* logical count (aos_effects_update) */
		w->obj.overflow++;
		return -1;
	}
	int i = w->obj.neffect++;
	aos_effect *e = &w->obj.effect[i];
	memset(e, 0, sizeof *e);
	e->type = type;
	e->id = id;
	e->variant = variant;
	e->ownerId = ownerId;
	e->team = team;
	e->drawLayer = 1;
	if (pos)
		copy3(e->pos, pos);
	e->f24 = 1;
	e->age = age0;
	e->moveDir = angle;  /* +0x34 */
	e->rotation = angle; /* +0x38 */
	return i;
}

bool aos_laser_push_seg(aos_laser *l, const aos_shape *s, const float pos[3])
{
	if (l->nseg >= AOS_LASER_SEGS)
		return false;
	aos_seg *g = &l->seg[l->nseg++];
	g->shape = *s;
	g->pad = 0;
	copy3(g->pos, pos);
	return true;
}

aos_enemy *aos_enemy_by_id(aos_world *w, int32_t id)
{
	for (int i = 0; i < w->obj.nenemy; i++)
		if (w->obj.enemy[i].id == id)
			return &w->obj.enemy[i];
	return NULL;
}

aos_player *aos_player_by_objid(aos_world *w, int32_t objId)
{
	for (int i = 0; i < 2; i++)
		if (w->p[i].objId == objId)
			return &w->p[i];
	return NULL;
}

/* Table-dispatched spawns (types without a registered function spawn nothing). */
int aos_spawn_beam(aos_world *w, int32_t type, int32_t id, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team)
{
	return aos_otab.beamSpawn[type] ? aos_otab.beamSpawn[type](w, id, pos, angle, owner, age0, team) : -1;
}
int aos_spawn_missile(aos_world *w, int32_t type, int32_t id, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team, int32_t p18)
{
	return aos_otab.missileSpawn[type] ? aos_otab.missileSpawn[type](w, id, pos, angle, owner, age0, team, p18) : -1;
}
int aos_spawn_hitbox(aos_world *w, int32_t type, int32_t id, const float pos[3], float angle, int32_t owner, int32_t zero, int32_t team)
{
	return aos_otab.hitboxSpawn[type] ? aos_otab.hitboxSpawn[type](w, id, pos, angle, owner, zero, team) : -1;
}
int aos_spawn_laser(aos_world *w, int32_t type, int32_t id, const float pos[3], float angle, int32_t owner, int32_t team, int32_t p18)
{
	return aos_otab.laserSpawn[type] ? aos_otab.laserSpawn[type](w, id, pos, angle, owner, team, p18) : -1;
}
int aos_spawn_enemy(aos_world *w, int32_t type, int32_t id, int32_t owner, int32_t team, int32_t p18)
{
	return aos_otab.enemySpawn[type] ? aos_otab.enemySpawn[type](w, id, owner, team, p18) : -1;
}
int aos_spawn_effect(aos_world *w, int32_t type, int32_t id, int32_t variant, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team)
{
	return aos_otab.effectSpawn[type] ? aos_otab.effectSpawn[type](w, id, variant, pos, angle, owner, age0, team) : -1;
}

/* ------------------------------------------------------------------ */
/* geometry: FUN_005f1c90 and helpers                                  */

/* FUN_005f1170 with the cosine/sine already evaluated (aos_cos/aos_sin are pure,
 * so hoisting them out of the 4-corner loop gives bit-identical corners). */
static void rot2cs(float v[2], float c, float s)
{
	float x = v[0];
	v[0] = v[0] * c - v[1] * s;
	v[1] = x * s + v[1] * c;
}

/* Upper bound of how far from its centre a shape can register a hit: circle
 * radius, or the rectangle's corner distance sqrt(w^2 + h^2) <= 1.415 * max(w, |h|)
 * (1.5 gives slack for float rounding). 0 = the shape takes part in no test.
 * NaN propagates so the caller never skips on it. */
static float shape_reach(const aos_shape *s)
{
	float r = s->radius > 0.0f ? s->radius : 0.0f;
	if (s->halfW > 0.0f) {
		float h = aos_fabs(s->halfH);
		float m = (s->halfW >= h ? s->halfW : h) * 1.5f;
		if (!(m <= r))
			r = m;
	}
	return r;
}

static float side2(const float *a, const float *b, const float *c, const float *d) /* FUN_005f12e0 / 5f3440 */
{
	return ((a[0] - b[0]) * (c[1] - a[1]) + (a[1] - b[1]) * (a[0] - c[0])) *
	       ((a[0] - b[0]) * (d[1] - a[1]) + (a[1] - b[1]) * (a[0] - d[0]));
}

bool aos_point_in_tri(const float *a, const float *b, const float *c, const float *p) /* FUN_005f1820 */
{
	if ((a[0] - c[0]) * (a[1] - b[1]) == (a[0] - b[0]) * (a[1] - c[1]))
		return false;
	float g[2] = {(a[0] + b[0] + c[0]) / 3.0f, (a[1] + b[1] + c[1]) / 3.0f};
	return side2(a, b, p, g) >= 0.0f && side2(a, c, p, g) >= 0.0f && side2(b, c, p, g) >= 0.0f;
}

static bool rect_circle(const float c[4][2], const float *cp, float r)
{
	for (int k = 0; k < 4; k++) {
		float dx = c[k][0] - cp[0], dy = c[k][1] - cp[1];
		if (aos_sqrt(dx * dx + dy * dy) < r)
			return true;
	}
	float px = cp[0], py = cp[1];
	for (int k = 0; k < 4; k++) {
		float x3 = c[k][0], y3 = c[k][1], x5 = c[(k + 1) & 3][0], y5 = c[(k + 1) & 3][1];
		float a = y5 - y3, b = x3 - x5;
		float num = aos_fabs(a * px + b * py + (y3 * x5 - x3 * y5));
		float den = aos_sqrt(a * a + b * b);
		if (num / den <= r && (x5 - x3) * (px - x3) + (y5 - y3) * (py - y3) > 0.0f &&
		    (x3 - x5) * (px - x5) + (y3 - y5) * (py - y5) > 0.0f)
			return true;
	}
	return aos_point_in_tri(c[0], c[1], c[2], cp) || aos_point_in_tri(c[2], c[3], c[0], cp);
}

bool aos_shape_hit(const float pA[3], const float pB[3], const aos_shape *a, const aos_shape *b,
                   float angA, float angB)
{
	AOS_PERF_ADD(shape_calls, 1);
	/* Exact broad phase: every test below (circle/circle, rect/rect edge crossings
	 * and point-in-triangle, rect/circle corner/edge/inside) needs the centres
	 * within reach(A) + reach(B). Skip only when clearly farther (+1 unit of
	 * slack) and only for sane coordinates, so float rounding can't matter;
	 * NaN/inf inputs fall through to the full test. */
	{
		float dx = aos_fabs(pA[0] - pB[0]), dy = aos_fabs(pA[1] - pB[1]);
		float reach = shape_reach(a) + shape_reach(b) + 1.0f;
		if ((dx > reach || dy > reach) && aos_fabs(pA[0]) < 1.0e5f && aos_fabs(pA[1]) < 1.0e5f &&
		    aos_fabs(pB[0]) < 1.0e5f && aos_fabs(pB[1]) < 1.0e5f)
			return false;
	}
	AOS_PERF_ADD(shape_full, 1);
	float rr = a->radius + b->radius;
	if (a->radius > 0.0f && b->radius > 0.0f && aos_fabs(pA[0] - pB[0]) <= rr &&
	    aos_fabs(pA[1] - pB[1]) <= rr &&
	    aos_sqrt((pA[0] - pB[0]) * (pA[0] - pB[0]) + (pA[1] - pB[1]) * (pA[1] - pB[1])) < rr)
		return true;
	float ca[4][2] = {{0}}, cb[4][2] = {{0}};
	if (a->halfW > 0.0f) { /* A corners: (-w,-h) (w,-h) (w,h) (-w,h) */
		const float s[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
		float c = aos_cos(angA), sn = aos_sin(angA);
		for (int k = 0; k < 4; k++) {
			ca[k][0] = s[k][0] * a->halfW;
			ca[k][1] = s[k][1] * a->halfH;
			rot2cs(ca[k], c, sn);
			ca[k][0] += pA[0];
			ca[k][1] += pA[1];
		}
	}
	if (b->halfW > 0.0f) { /* B corners: (-w,-h) (-w,h) (w,h) (w,-h) */
		const float s[4][2] = {{-1, -1}, {-1, 1}, {1, 1}, {1, -1}};
		float c = aos_cos(angB), sn = aos_sin(angB);
		for (int k = 0; k < 4; k++) {
			cb[k][0] = s[k][0] * b->halfW;
			cb[k][1] = s[k][1] * b->halfH;
			rot2cs(cb[k], c, sn);
			cb[k][0] += pB[0];
			cb[k][1] += pB[1];
		}
	}
	if (a->halfW > 0.0f && b->halfW > 0.0f) {
		float ea = (a->halfW <= a->halfH ? a->halfH : a->halfW) * 1.5f;
		float eb = (b->halfW <= b->halfH ? b->halfH : b->halfW) * 1.5f;
		if (aos_fabs(pA[0] - pB[0]) <= ea + eb && aos_fabs(pA[1] - pB[1]) <= ea + eb) {
			for (int i = 0; i < 4; i++)
				for (int j = 0; j < 4; j++)
					if (side2(ca[i], ca[(i + 1) & 3], cb[j], cb[(j + 1) & 3]) < 0.0f &&
					    side2(cb[j], cb[(j + 1) & 3], ca[i], ca[(i + 1) & 3]) < 0.0f)
						return true;
			if (aos_point_in_tri(ca[0], ca[1], ca[2], pB) || aos_point_in_tri(ca[2], ca[3], ca[0], pB) ||
			    aos_point_in_tri(cb[0], cb[1], cb[2], pA) || aos_point_in_tri(cb[2], cb[3], cb[0], pA))
				return true;
		}
	}
	if (a->halfW > 0.0f && b->radius > 0.0f && rect_circle((const float(*)[2])ca, pB, b->radius))
		return true;
	if (b->halfW > 0.0f && a->radius > 0.0f && rect_circle((const float(*)[2])cb, pA, a->radius))
		return true;
	return false;
}

bool aos_in_arena(const float pos[3], float margin)
{
	float r = 540.0f + margin;
	return pos[0] * pos[0] + pos[1] * pos[1] < r * r;
}

/* ------------------------------------------------------------------ */
/* managers                                                            */

/* FUN_00528250 — attach child enemy to parent. Keyframes: motion.c
 * (FUN_00526670 / FUN_00527ee0), reached through aos_enemy_motion which also
 * performs the frame advance + loop wrap of the original tail. */
motion_sample (*aos_enemy_motion)(int32_t motionId, int32_t *frame) = 0;

static void attach(aos_world *w, aos_enemy *c, aos_enemy *pa, bool flip)
{
	if (c->motionId == 2)
		return;
	float pang = pa->angle;          /* local_38 */
	float ca, mx, my;                /* local_34, local_14, local_10 */
	if (c->motionId == 0) {
		ca = 0.0f;                   /* literal: motionless children get angle 0 (0x52828a) */
		mx = 0.0f;
		my = 0.0f;
	} else {
		motion_sample m = {0, 0, 0};
		if (aos_enemy_motion)
			m = aos_motion_get(c->motionId, c->motionFrame);
		if (flip) {
			ca = pang - m.a;
			mx = pa->scale[0] * -1.0f * m.x;
			my = m.y * pa->scale[1];
		} else {
			ca = pang + m.a;
			mx = m.x * pa->scale[0];
			my = m.y * pa->scale[1];
		}
	}
	float v[2];
	v[0] = -c->offset[0] * pa->scale[0];
	v[1] = (flip ? -c->offset[1] : c->offset[1]) * pa->scale[1];
	v[1] = v[1] + my;
	v[0] = v[0] + mx;
	aos_vec_rotate(v, pang); /* FUN_005f1210 */
	v[0] = v[0] + pa->pos[0];
	v[1] = v[1] + pa->pos[1];
	float pv[2] = {c->pivot[0] * c->scale[0], c->pivot[1] * c->scale[1]};
	aos_vec_rotate(pv, ca);
	c->pos[0] = v[0] + pv[0];
	c->pos[1] = v[1] + pv[1];
	c->angle = ca;
	c->angle2 = c->angle;
	aos_angle_normalize(&c->angle);
	aos_angle_normalize(&c->angle2);
	/* frame advance: motionFrame++, wrap by length if the slot loops */
	if (c->motionId != 0 && aos_enemy_motion)
		(void)aos_enemy_motion(c->motionId, &c->motionFrame); /* aos_motion_step: advances */
	else
		c->motionFrame++; /* slot 0: length 0, no loop */
	/* children of c get the SAME flip flag (param_3 passed through) */
	int32_t cid = c->id;
	for (int j = 0; j < w->obj.nenemy; j++)
		if (w->obj.enemy[j].parentId == cid)
			attach(w, &w->obj.enemy[j], c, flip);
}

static void attach_children(aos_world *w, aos_enemy *parent, bool flip)
{
	int32_t pid = parent->id;
	for (int j = 0; j < w->obj.nenemy; j++)
		if (w->obj.enemy[j].parentId == pid)
			attach(w, &w->obj.enemy[j], parent, flip);
}

void aos_enemies_update(aos_world *w)
{
	for (int i = 0; i < w->obj.nenemy; i++) {
		aos_enemy *e = &w->obj.enemy[i];
		aos_enemy_update_fn f = aos_otab.enemyUpdate[e->type];
		if (!f || !f(w, e)) {
			enemy_erase(w, i--);
			continue;
		}
		e = &w->obj.enemy[i];
		if (e->parentId == -1) {
			bool flip = e->mirrorChildren == 1 && aos_fabs(e->angle) > 1.5707964f;
			attach_children(w, e, flip);
		}
		copy3(e->prevPos, e->pos);
		e->age++;
		e->timer2++;
	}
}

/* Same one-pass compaction as aos_effects_update (see there): beam update
 * handlers never scan the beam pool (the only scan, Kyoko's redirect_beams,
 * runs from missile/enemy updates), use spawn indices right away only, and
 * the pool-full check in aos_beam_new uses the logical count. */
void aos_beams_update(aos_world *w)
{
	aos_objects *o = &w->obj;
	int keep = 0;
	for (int i = 0; i < o->nbeam; i++) {
		if (o->nbeam > AOS_BEAM_COMPACT_AT && keep < i) {
			int gap = i - keep;
			memmove(&o->beam[keep], &o->beam[i], sizeof o->beam[0] * (size_t)(o->nbeam - i));
			AOS_PERF_ADD(erase_bytes, sizeof o->beam[0] * (size_t)(o->nbeam - i));
			AOS_CLEAR_DEAD(&o->beam[o->nbeam - gap], sizeof o->beam[0] * (size_t)gap);
			o->nbeam -= gap;
			i = keep;
		}
		beam_holes = i - keep;
		aos_beam_update_fn f = aos_otab.beamUpdate[o->beam[i].type];
		if (!f || !f(w, &o->beam[i]))
			continue;
		o->beam[i].age++;
		if (keep != i) {
			memcpy(&o->beam[keep], &o->beam[i], sizeof o->beam[0]);
			AOS_PERF_ADD(erase_bytes, sizeof o->beam[0]);
		}
		keep++;
	}
	AOS_CLEAR_DEAD(&o->beam[keep], sizeof o->beam[0] * (size_t)(o->nbeam - keep));
	o->nbeam = keep;
	beam_holes = 0;
}

void aos_missiles_update(aos_world *w)
{
	for (int i = 0; i < w->obj.nmissile; i++) {
		aos_missile_update_fn f = aos_otab.missileUpdate[w->obj.missile[i].type];
		if (!f || !f(w, &w->obj.missile[i]))
			missile_erase(w, i--);
		else
			w->obj.missile[i].age++;
	}
}

void aos_lasers_update(aos_world *w)
{
	for (int i = 0; i < w->obj.nlaser; i++) {
		aos_laser *l = &w->obj.laser[i];
		l->age++;
		if (l->age < 0)
			continue;
		aos_laser_update_fn f = aos_otab.laserUpdate[l->type];
		if (!f || !f(w, l))
			laser_erase(w, i--);
	}
}

/* Effects are the biggest pool and die in bunches, so erasing them one by one
 * (the original's vector::erase) can move megabytes per frame in a heavy hyper
 * scene. Here the walk compacts in one pass instead: records [keep, i) are
 * dead or already moved down. This gives the identical list because effect
 * update handlers never look at other effects or at neffect - their only
 * access to the pool is the index a spawn returns, used right away - and the
 * one observable difference, the pool-full check in aos_effect_new, uses the
 * logical count neffect - effect_holes. The array has AOS_EFFECT_SLOTS =
 * 2 * AOS_MAX_EFFECT physical slots and the walk compacts before a handler
 * runs whenever more than AOS_MAX_EFFECT are in use, so a handler (which can
 * add at most AOS_MAX_EFFECT - logical count) never runs out of slots. */

#ifndef AOS_EFFECT_COMPACT_AT /* tests lower it to exercise the mid-walk compaction */
#define AOS_EFFECT_COMPACT_AT AOS_MAX_EFFECT
#endif
_Static_assert(AOS_EFFECT_COMPACT_AT <= AOS_MAX_EFFECT, "see aos_effects_update");
void aos_effects_update(aos_world *w)
{
	aos_objects *o = &w->obj;
	int keep = 0;
	for (int i = 0; i < o->neffect; i++) {
		if (o->neffect > AOS_EFFECT_COMPACT_AT && keep < i) {
			int gap = i - keep;
			memmove(&o->effect[keep], &o->effect[i], sizeof o->effect[0] * (size_t)(o->neffect - i));
			AOS_PERF_ADD(erase_bytes, sizeof o->effect[0] * (size_t)(o->neffect - i));
			AOS_CLEAR_DEAD(&o->effect[o->neffect - gap], sizeof o->effect[0] * (size_t)gap);
			o->neffect -= gap;
			i = keep;
		}
		effect_holes = i - keep;
		aos_effect *e = &o->effect[i];
		e->age++;
		bool alive = true;
		if (e->age >= 0) {
			aos_effect_update_fn f = aos_otab.effectUpdate[e->type];
			alive = f && f(w, e);
		}
		if (!alive)
			continue;
		if (keep != i) {
			memcpy(&o->effect[keep], &o->effect[i], sizeof o->effect[0]);
			AOS_PERF_ADD(erase_bytes, sizeof o->effect[0]);
		}
		keep++;
	}
	AOS_CLEAR_DEAD(&o->effect[keep], sizeof o->effect[0] * (size_t)(o->neffect - keep));
	o->neffect = keep;
	effect_holes = 0;
}

void aos_whips_update(aos_world *w)
{
	for (int i = 0; i < w->obj.nwhip; i++) {
		aos_whip *h = &w->obj.whip[i];
		h->age++;
		aos_whip_update_fn f = aos_otab.whipUpdate[h->type];
		if (!f || !f(w, h))
			whip_erase(w, i--);
	}
}

void aos_enemies_cleanup(aos_world *w) /* FUN_00523370 */
{
	for (int i = 0; i < w->obj.nenemy; i++) {
		aos_enemy *e = &w->obj.enemy[i];
		if (e->parentId != -1 && !aos_enemy_by_id(w, e->parentId)) {
			aos_enemy_killed_fn f = aos_otab.enemyOnKilled[e->type];
			if (f)
				f(w, e);
			enemy_erase(w, i--);
		}
	}
}

void aos_rehit_timers(aos_world *w)
{
	for (int i = 0; i < w->obj.nmissile; i++) {
		aos_missile *m = &w->obj.missile[i];
		if (m->rehitTimer > 0 && --m->rehitTimer == 0)
			m->active = 1;
	}
	for (int i = 0; i < w->obj.nbeam; i++) {
		aos_beam *b = &w->obj.beam[i];
		if (b->rehitTimer > 0 && --b->rehitTimer == 0)
			b->active = 1;
	}
	for (int i = 0; i < w->obj.nenemy; i++) {
		aos_enemy *e = &w->obj.enemy[i];
		if (e->rehitTimer > 0 && --e->rehitTimer == 0)
			e->active = 1;
	}
}

/* ------------------------------------------------------------------ */
/* collision passes (FUN_006129b0 step 9)                              */

static bool dashing(const aos_player *p) /* objects.md §4.3 */
{
	bool d = p->state == AOS_ST_DASH || (p->state == AOS_ST_NEUTRAL && p->speed > 0.0f);
	return d && !(p->barrierTime > 0);
}

static bool missile_on_hit(aos_world *w, int i)
{
	aos_missile_cb_fn f = aos_otab.missileOnHit[w->obj.missile[i].type];
	return f ? f(w, &w->obj.missile[i]) : false;
}
static bool missile_on_killed(aos_world *w, int i)
{
	aos_missile_cb_fn f = aos_otab.missileOnKilled[w->obj.missile[i].type];
	return f ? f(w, &w->obj.missile[i]) : false;
}
static void beam_on_hit(aos_world *w, int i)
{
	aos_beam_hit_fn f = aos_otab.beamOnHit[w->obj.beam[i].type];
	if (f)
		f(w, &w->obj.beam[i]);
}
static bool enemy_on_killed(aos_world *w, int i)
{
	aos_enemy_killed_fn f = aos_otab.enemyOnKilled[w->obj.enemy[i].type];
	return f ? f(w, &w->obj.enemy[i]) : false;
}

void aos_popup(aos_world *w, int kind, const float pos[3], int32_t owner); /* fx.c, FUN_0065dff0 */
void aos_se_cd(aos_world *w, int id, int cd);                              /* FUN_007bcaf0 */

/* 1. Beam -> Missile, FUN_0060bbd0 */
static void pass_beam_missile(aos_world *w)
{
	for (int c = 0; c < w->obj.nmissile; c++) {
		if (!w->obj.missile[c].active)
			continue;
		bool killed = false;
		for (int b = 0; b < w->obj.nbeam; b++) {
			aos_beam *bm = &w->obj.beam[b];
			aos_missile *m = &w->obj.missile[c];
			if (!bm->active || bm->team == m->team)
				continue;
			if (!aos_shape_hit(bm->pos, m->pos, &bm->shape, &m->shapeProj, bm->angle, m->angle))
				continue;
			bool destroy = false, skipHit = false;
			if (m->absorbs == 1) {
				if (aos_otab.missileOnAbsorb[m->type])
					aos_otab.missileOnAbsorb[m->type](w, m, bm);
				destroy = skipHit = true;
			} else {
				m->hp -= bm->damage;
				if (bm->rehitFlag == 1) {
					m->active = 0;
					m->rehitTimer = 0x14;
				}
				if (m->hp < 0)
					killed = true;
				else
					destroy = true;
			}
			if (destroy) {
				if (!skipHit)
					beam_on_hit(w, b);
				beam_erase(w, b--);
			} else if (killed) {
				break;
			}
		}
		if (killed && !missile_on_killed(w, c))
			missile_erase(w, c--);
	}
}

/* 2. Beam -> Enemy, FUN_0060b900 */
static void pass_beam_enemy(aos_world *w)
{
	for (int k = 0; k < w->obj.nenemy; k++) {
		aos_enemy *e = &w->obj.enemy[k];
		bool dead = false;
		if (!e->active || e->solid == 1)
			continue;
		for (int b = 0; b < w->obj.nbeam; b++) {
			bool destroy = false;
			aos_beam *bm = &w->obj.beam[b];
			if (!bm->active || bm->team == e->team)
				continue;
			if (dead)
				break;
			if (aos_shape_hit(bm->pos, e->pos, &bm->shape, &e->shape, bm->angle, e->angle)) {
				if (e->invulnerable == 1) {
					aos_popup(w, 3, bm->pos, e->ownerId);
				} else {
					e->hp -= bm->damage;
					if (bm->rehitFlag == 1) {
						e->active = 0;
						e->rehitTimer = 0x14;
					}
				}
				if (e->hp < 1)
					dead = true;
				else
					destroy = bm->destroyOnHit != 0;
			}
			if (destroy) {
				beam_on_hit(w, b);
				beam_erase(w, b--);
			}
		}
		if (dead && !enemy_on_killed(w, k))
			enemy_erase(w, k--);
	}
}

/* 3. Beam -> Player, FUN_0060b610 */
static bool pass_beam_player(aos_world *w)
{
	for (int pi = 0; pi < 2; pi++) {
		aos_player *p = &w->p[pi];
		if (!p->hittable)
			continue;
		bool dash = dashing(p);
		for (int b = 0; b < w->obj.nbeam; b++) {
			aos_beam *bm = &w->obj.beam[b];
			bool destroy = false;
			if (!bm->active || bm->team == p->side || (bm->type == 8 && bm->id == 200))
				continue;
			if (aos_shape_hit(bm->pos, &p->x, &bm->shape, (const aos_shape *)p->hurtShape, bm->angle, p->faceAngle) && !dash) {
				if (p->barrierTime == 0 && aos_damage(w, p, bm->damage, bm->hitType, bm->stun))
					aos_hitstun(w, p, bm->moveAngle, bm->knockback);
				destroy = w->obj.beam[b].destroyOnHit != 0;
			}
			if (destroy) {
				beam_on_hit(w, b);
				beam_erase(w, b);
				break;
			}
			if (w->roundOver)
				return false;
		}
	}
	return true;
}

/* 4 / 11. projectile -> graze zone, FUN_0060bf30 / FUN_006113a0 */
static void pass_graze(aos_world *w, bool missiles)
{
	static const aos_shape zone = {20.0f, 50.0f, 0.0f};
	for (int z = 0; z < w->obj.neffect; z++) {
		aos_effect *e = &w->obj.effect[z];
		if (e->type != 0 || e->id < 100 || e->id > 103)
			continue;
		int n = missiles ? w->obj.nmissile : w->obj.nbeam;
		for (int i = 0; i < n; i++) {
			const float *pos;
			const aos_shape *sh;
			float ang;
			int32_t dmg;
			if (missiles) {
				aos_missile *m = &w->obj.missile[i];
				if (m->grazed == 1 || !m->active || m->team == e->team)
					continue;
				pos = m->pos, sh = &m->shapeBody, ang = m->angle, dmg = m->damage;
			} else {
				aos_beam *b = &w->obj.beam[i];
				if (b->grazed == 1 || !b->active || b->team == e->team)
					continue;
				pos = b->pos, sh = &b->shape, ang = b->angle, dmg = b->damage;
			}
			if (!aos_shape_hit(pos, e->pos, sh, &zone, ang, e->rotation))
				continue;
			aos_se_cd(w, 9, 3);
			if (missiles)
				w->obj.missile[i].grazed = 1;
			else
				w->obj.beam[i].grazed = 1;
			aos_popup(w, 1, e->pos, e->ownerId);
			aos_player *o = aos_player_by_objid(w, e->ownerId);
			if (o) {
				o->hyper = (float)dmg * (missiles ? 0.0002f : 0.00015f) * o->hyperMul + o->hyper;
				aos_heal(o, (float)dmg);
			}
			break;
		}
	}
}

/* 5. Hitbox -> Missile, FUN_0060fc30 */
static void pass_hitbox_missile(aos_world *w)
{
	for (int c = 0; c < w->obj.nmissile; c++) {
		if (!w->obj.missile[c].active)
			continue;
		bool killed = false;
		for (int h = 0; h < w->obj.nhitbox; h++) {
			aos_hitbox *hb = &w->obj.hitbox[h];
			aos_missile *m = &w->obj.missile[c];
			if (hb->team == m->team)
				continue;
			if (!aos_shape_hit(hb->pos, m->pos, &hb->shape, &m->shapeProj, hb->angle, m->angle))
				continue;
			m->hp -= hb->damage;
			if (hb->rehitFlag == 1) {
				m->active = 0;
				m->rehitTimer = 0x14;
			}
			if (m->hp < 0) {
				killed = true;
				break;
			}
		}
		if (killed && !missile_on_killed(w, c))
			missile_erase(w, c--);
	}
}

/* 6. Hitbox -> Enemy, FUN_0060fa30 */
static void pass_hitbox_enemy(aos_world *w)
{
	for (int k = 0; k < w->obj.nenemy; k++) {
		aos_enemy *e = &w->obj.enemy[k];
		bool dead = false;
		if (!e->active || e->solid == 1)
			continue;
		for (int h = 0; h < w->obj.nhitbox; h++) {
			aos_hitbox *hb = &w->obj.hitbox[h];
			if (hb->team == e->team)
				continue;
			if (aos_shape_hit(hb->pos, e->pos, &hb->shape, &e->shape, hb->angle, e->angle)) {
				if (e->invulnerable != 1) {
					e->hp -= hb->damage;
					if (hb->rehitFlag == 1) {
						e->active = 0;
						e->rehitTimer = 0x14;
					}
				}
				if (e->hp < 1)
					dead = true;
			}
			if (dead)
				break;
		}
		if (dead && !enemy_on_killed(w, k))
			enemy_erase(w, k--);
	}
}

static bool hitreg_has(aos_world *w, const aos_hitbox *hb) /* FUN_0060fe80 */
{
	for (int i = 0; i < w->obj.nhitreg; i++)
		if (w->obj.hitreg[i][0] == hb->id && w->obj.hitreg[i][1] == hb->ownerId)
			return true;
	return false;
}

/* 7. Hitbox -> Player, FUN_0060f5a0 */
static void pass_hitbox_player(aos_world *w)
{
	w->obj.nhitreg = 0;
	for (int pi = 0; pi < 2; pi++) {
		aos_player *p = &w->p[pi];
		if (!p->hittable || p->barrierTime >= 1)
			continue;
		bool dash = dashing(p);
		for (int h = 0; h < w->obj.nhitbox; h++) {
			aos_hitbox *hb = &w->obj.hitbox[h];
			if (hb->team == p->side || (hb->hitType == 1 && dash))
				continue;
			if (hb->oncePerFrame == 1 && hitreg_has(w, hb))
				continue;
			if (!aos_shape_hit(hb->pos, &p->x, &hb->shape, (const aos_shape *)p->hurtShape, hb->angle, p->faceAngle))
				continue;
			if (aos_otab.hitboxOnHit[hb->type])
				aos_otab.hitboxOnHit[hb->type](w, hb, p);
			if (hb->dealsDamage) {
				if (aos_damage(w, p, hb->damage, hb->hitType, hb->stun))
					aos_hitstun(w, p, hb->angle, hb->knockback);
				if (hb->oncePerFrame == 1 && w->obj.nhitreg < AOS_MAX_HITREG) {
					w->obj.hitreg[w->obj.nhitreg][0] = hb->id;
					w->obj.hitreg[w->obj.nhitreg][1] = hb->ownerId;
					w->obj.nhitreg++;
				}
			}
		}
	}
}

/* 8. Missile <-> Missile, FUN_00611060 (literal, including the re-test of a kept j) */
static void pass_missile_missile(aos_world *w)
{
	for (int c = 0; c < w->obj.nmissile; c++) {
		if (!w->obj.missile[c].active)
			continue;
		bool iDead = false, jDead = false;
		int j = c;
		for (;;) {
			j++;
			if (j >= w->obj.nmissile)
				break;
			aos_missile *mi = &w->obj.missile[c], *mj = &w->obj.missile[j];
			if (!mj->active || mj->team == mi->team)
				continue;
			jDead = false;
			if (!aos_shape_hit(mi->pos, mj->pos, &mi->shapeProj, &mj->shapeProj, mi->angle, mj->angle))
				continue;
			mi->hp -= mj->damage;
			mj->hp -= mi->damage;
			if (mi->hp < 1)
				iDead = true;
			if (mj->hp < 1)
				jDead = true;
			if (jDead) {
				if (!missile_on_hit(w, j))
					missile_erase(w, j);
				if (!iDead) {
					j--;
					continue;
				}
			}
			if (iDead)
				break;
		}
		if (iDead) {
			bool keep = jDead ? missile_on_hit(w, c) : missile_on_killed(w, c);
			if (!keep)
				missile_erase(w, c--);
		}
	}
}

/* 9. Missile -> Enemy, FUN_00610de0 */
static void pass_missile_enemy(aos_world *w)
{
	for (int k = 0; k < w->obj.nenemy; k++) {
		aos_enemy *e = &w->obj.enemy[k];
		bool dead = false;
		if (!e->active || e->solid == 1)
			continue;
		for (int c = 0; c < w->obj.nmissile; c++) {
			aos_missile *m = &w->obj.missile[c];
			bool hit = false;
			if (!m->active || m->team == e->team)
				continue;
			if (dead)
				break;
			if (aos_shape_hit(m->pos, e->pos, &m->shapeBody, &e->shape, m->angle, e->angle)) {
				if (e->invulnerable == 1)
					aos_popup(w, 3, m->pos, e->ownerId);
				else
					e->hp -= m->damage;
				hit = true;
				if (e->hp < 1)
					dead = true;
			}
			if (hit && !missile_on_hit(w, c)) {
				missile_erase(w, c);
				break;
			}
		}
		if (dead && !enemy_on_killed(w, k))
			enemy_erase(w, k--);
	}
}

/* 10. Missile -> Player, FUN_00610b80 */
static bool pass_missile_player(aos_world *w)
{
	for (int pi = 0; pi < 2; pi++) {
		aos_player *p = &w->p[pi];
		if (!p->hittable)
			continue;
		for (int c = 0; c < w->obj.nmissile; c++) {
			aos_missile *m = &w->obj.missile[c];
			if (!m->active || m->team == p->side)
				continue;
			if (aos_shape_hit(m->pos, &p->x, &m->shapeBody, (const aos_shape *)p->hurtShape, m->angle, p->faceAngle)) {
				if (p->barrierTime == 0 && aos_damage(w, p, m->damage, m->hitType, m->stun))
					aos_hitstun(w, p, m->moveAngle, m->knockback);
				if (!missile_on_hit(w, c)) {
					missile_erase(w, c);
					break;
				}
			}
			if (w->roundOver)
				return false;
		}
	}
	return true;
}

/* 12. Laser truncation by player bodies, FUN_00610360 */
static void pass_laser_block(aos_world *w)
{
	for (int pi = 0; pi < 2; pi++) {
		aos_player *p = &w->p[pi];
		if (!p->hittable)
			continue;
		bool dash = dashing(p);
		bool cut = false;
		for (int li = 0; li < w->obj.nlaser; li++) {
			aos_laser *l = &w->obj.laser[li];
			if (l->team == p->side || l->blockMode == 1 || (l->hitType == 1 && dash))
				continue;
			int k;
			for (k = 0; k < l->nseg; k++)
				if (aos_shape_hit(l->seg[k].pos, &p->x, &l->seg[k].shape, (const aos_shape *)p->hurtShape, l->angle, p->faceAngle)) {
					cut = true;
					break;
				}
			if (cut) {
				l->nseg = k + 1; /* erase (k, end): the iterator is post-incremented first (0x61056c), so the touching segment stays */
				break;
			}
		}
	}
}

/* Laser broad phase (perf only, exact). A laser is a chain of up to AOS_LASER_SEGS segment shapes and the
 * missile/enemy passes test every segment of every laser against every target (Tsih's worst tick: ~20k
 * segment tests). aos_shape_hit rejects a pair without any other effect when the centres are farther apart on
 * one axis than reach(A) + reach(B) + 1 (coordinates < 1e5). If a target lies beyond the bounding box of all
 * segment reaches by reach(target) + 2 on one axis, every segment test of that laser would take that exit, so
 * the laser can be skipped without changing any result; the extra unit covers float rounding of the box.
 * Boxes are rebuilt whenever a kill handler ran (it may spawn or change lasers). */
typedef struct {
	float x0, y0, x1, y1;
	bool ok; /* false: never skip (insane coordinates / NaN) */
} aos_lbox;
static aos_lbox lbox[AOS_MAX_LASER];

static void laser_boxes(const aos_world *w)
{
	for (int li = 0; li < w->obj.nlaser; li++) {
		const aos_laser *l = &w->obj.laser[li];
		aos_lbox *b = &lbox[li];
		b->x0 = b->y0 = 1.0e9f;
		b->x1 = b->y1 = -1.0e9f;
		b->ok = true;
		for (int k = 0; k < l->nseg && k < AOS_LASER_SEGS; k++) {
			const float *q = l->seg[k].pos;
			float r = shape_reach(&l->seg[k].shape);
			if (!(aos_fabs(q[0]) < 1.0e5f && aos_fabs(q[1]) < 1.0e5f && r < 1.0e5f)) {
				b->ok = false;
				break;
			}
			if (q[0] - r < b->x0)
				b->x0 = q[0] - r;
			if (q[0] + r > b->x1)
				b->x1 = q[0] + r;
			if (q[1] - r < b->y0)
				b->y0 = q[1] - r;
			if (q[1] + r > b->y1)
				b->y1 = q[1] + r;
		}
	}
}

static bool laser_far(int li, const float pos[3], const aos_shape *s)
{
	const aos_lbox *b = &lbox[li];
	float r = shape_reach(s) + 2.0f, x = pos[0], y = pos[1];
	if (!b->ok || !(aos_fabs(x) < 1.0e5f && aos_fabs(y) < 1.0e5f && r < 1.0e5f))
		return false;
	return x + r < b->x0 || x - r > b->x1 || y + r < b->y0 || y - r > b->y1;
}

/* 14. Laser -> Missile, FUN_006107f0 (literal: the hit flag is not reset per laser) */
static void pass_laser_missile(aos_world *w)
{
	if (!w->obj.nlaser)
		return;
	laser_boxes(w);
	for (int c = 0; c < w->obj.nmissile; c++) {
		if (!w->obj.missile[c].active)
			continue;
		bool killed = false, hit = false;
		for (int li = 0; li < w->obj.nlaser; li++) {
			aos_laser *l = &w->obj.laser[li];
			aos_missile *m = &w->obj.missile[c];
			if (l->team == m->team || laser_far(li, m->pos, &m->shapeProj))
				continue;
			for (int k = 0; k < l->nseg; k++) {
				if (aos_shape_hit(l->seg[k].pos, m->pos, &l->seg[k].shape, &m->shapeProj, l->angle, m->angle)) {
					m->hp -= l->damage;
					if (m->hp < 0)
						killed = true;
					hit = true;
				}
				if (hit)
					break;
			}
			if (killed)
				break;
		}
		if (killed) {
			if (!missile_on_killed(w, c))
				missile_erase(w, c--);
			laser_boxes(w);
		}
	}
}

/* 15. Laser -> Enemy, FUN_006105d0 (literal: flag not reset per laser; ignores invulnerable) */
static void pass_laser_enemy(aos_world *w)
{
	if (!w->obj.nlaser)
		return;
	laser_boxes(w);
	for (int k = 0; k < w->obj.nenemy; k++) {
		aos_enemy *e = &w->obj.enemy[k];
		if (!e->active || e->solid == 1)
			continue;
		bool hit = false, dead = false;
		for (int li = 0; li < w->obj.nlaser; li++) {
			aos_laser *l = &w->obj.laser[li];
			if (l->team == e->team || laser_far(li, e->pos, &e->shape))
				continue;
			for (int s = 0; s < l->nseg; s++) {
				if (aos_shape_hit(l->seg[s].pos, e->pos, &l->seg[s].shape, &e->shape, l->angle, e->angle)) {
					e->hp -= l->damage;
					if (e->hp < 1)
						dead = true;
					hit = true;
				}
				if (hit)
					break;
			}
		}
		if (dead) {
			if (!enemy_on_killed(w, k))
				enemy_erase(w, k--);
			laser_boxes(w);
		}
	}
}

/* 16. Laser -> Player, FUN_006100b0 (literal: once one laser hits, later lasers
 * only test their first segment) */
static bool pass_laser_player(aos_world *w)
{
	for (int pi = 0; pi < 2; pi++) {
		aos_player *p = &w->p[pi];
		if (!p->hittable)
			continue;
		bool dash = dashing(p), hit = false;
		for (int li = 0; li < w->obj.nlaser; li++) {
			aos_laser *l = &w->obj.laser[li];
			if (l->team == p->side || (l->hitType == 1 && dash))
				continue;
			for (int s = 0; s < l->nseg; s++) {
				if (aos_shape_hit(l->seg[s].pos, &p->x, &l->seg[s].shape, (const aos_shape *)p->hurtShape, l->angle, p->faceAngle)) {
					if (p->barrierTime == 0) {
						aos_hitstun(w, p, l->angle, l->knockback);
						aos_damage(w, p, l->damage, l->hitType, (float)l->stun);
					}
					if (aos_otab.laserOnHit[l->type])
						aos_otab.laserOnHit[l->type](w, l, s, p);
					hit = true;
				}
				if (w->roundOver)
					return false;
				if (hit)
					break;
			}
		}
	}
	return true;
}

/* 17. Solid enemies push players — exact port of FUN_0060d690 with its
 * rectangle helpers FUN_0060e110 (entry side), FUN_005f15a0 (line
 * intersection), FUN_005f32a0 (orientation), FUN_005f1390, FUN_005f3330. */
static double seg_side(const float *p1, const float *p2, const float *p3, const float *p4) /* FUN_005f1390 (x87 result) */
{
	return ((double)((p1[0] - p2[0]) * (p3[1] - p1[1]) + (p1[1] - p2[1]) * (p1[0] - p3[0]))) *
	       (double)((p1[0] - p2[0]) * (p4[1] - p1[1]) + (p1[1] - p2[1]) * (p1[0] - p4[0]));
}

static void line_isect(const float *p1, const float *p2, const float *p3, const float *p4, float *o) /* FUN_005f15a0 */
{
	float a1 = p1[1] - p2[1], b1 = p2[0] - p1[0];
	float c1 = (p2[0] - p1[0]) * p1[1] - (p2[1] - p1[1]) * p1[0];
	float a2 = p3[1] - p4[1], b2 = p4[0] - p3[0];
	float c2 = (p4[0] - p3[0]) * p3[1] - (p4[1] - p3[1]) * p3[0];
	o[0] = (b2 * c1 - b1 * c2) / (a1 * b2 - b1 * a2);
	o[1] = (a1 * c2 - a2 * c1) / (a1 * b2 - b1 * a2);
}

static int orient3(const float *p1, const float *p2, const float *p3) /* FUN_005f32a0 */
{
	float v = (p2[1] - p3[1]) * p1[0] + (p3[1] - p1[1]) * p2[0] + (p1[1] - p2[1]) * p3[0];
	if (v > 0.0f)
		return 1;
	return v < 0.0f ? -1 : 0;
}

static float cross2(const float *a, const float *b) { return a[0] * b[1] - b[0] * a[1]; } /* FUN_005f3330 */

/* FUN_0060e110: which pair of edges the player crossed (1 = the two edges at
 * local y = ±halfH, -1 = the two at local x = ±halfW). */
static int solid_side(const aos_player *p, const aos_enemy *e)
{
	float c = aos_cos(e->angle), s = aos_sin(e->angle);
	float cn = aos_cos(e->angle + 1.5707964f), sn = aos_sin(e->angle + 1.5707964f);
	float hx = e->shape.halfW, hy = e->shape.halfH, ex = e->pos[0], ey = e->pos[1];
	float P[4][2][2] = {
		{{c * hx + cn * hy + ex, s * hx + sn * hy + ey}, {-c * hx + cn * hy + ex, -s * hx + sn * hy + ey}},
		{{(c * hx - cn * hy) + ex, (s * hx - sn * hy) + ey}, {(-c * hx - cn * hy) + ex, (-s * hx - sn * hy) + ey}},
		{{cn * hy + c * hx + ex, sn * hy + s * hx + ey}, {-cn * hy + c * hx + ex, -sn * hy + s * hx + ey}},
		{{(cn * hy - c * hx) + ex, (sn * hy - s * hx) + ey}, {(-cn * hy - c * hx) + ex, (-sn * hy - s * hx) + ey}},
	};
	static const int ret[4] = {1, 1, -1, -1};
	bool moved = p->x != p->px || p->y != p->py;
	for (int k = 0; k < 4; k++)
		if (moved && seg_side(P[k][0], P[k][1], &p->px, &p->x) <= 0.0)
			return ret[k];
	/* not crossed (or did not move): nearest edge by |cross| / edge length */
	static const int order[4] = {0, 2, 1, 3};
	int best = 1;
	float bestd = 0;
	for (int i = 0; i < 4; i++) {
		int k = order[i];
		float d[2] = {P[k][1][0] - P[k][0][0], P[k][1][1] - P[k][0][1]};
		float q[2] = {p->x - P[k][0][0], p->y - P[k][0][1]};
		float v = aos_fabs(cross2(d, q)) / ((ret[k] > 0 ? hx : hy) * 2.0f);
		if (i == 0) {
			bestd = v;
			best = 1;
		} else if (v < bestd) {
			best = ret[k];
			if (i < 3)
				bestd = v; /* literal: the last comparison does not update the minimum */
		}
	}
	return best;
}

static void pass_solid_push(aos_world *w)
{
	const float four = 4.0f; /* DAT_00853b68 */
	for (int pi = 0; pi < 2; pi++) {
		aos_player *p = &w->p[pi];
		aos_shape pt = {0, 0, 1.0f}; /* local_60 */
		float pang = p->faceAngle;    /* local_54 */
		if (p->state == AOS_ST_KO || !p->hittable || p->statusOn[AOS_STATUS_STUNIMMUNE] == 1)
			return; /* literal: returns for all players */
		for (int k = 0; k < w->obj.nenemy; k++) {
			aos_enemy *e = &w->obj.enemy[k];
			if (!e->solid || !e->active || e->ownerId == p->objId)
				continue;
			if (e->shape.radius > 0.0f && aos_shape_hit(e->pos, &p->x, &e->shape, &pt, e->angle, pang)) {
				float a = aos_atan2(p->y - e->pos[1], p->x - e->pos[0]);
				p->x = aos_cos(a) * e->shape.radius + e->pos[0];
				p->y = aos_sin(a) * e->shape.radius + e->pos[1];
			}
			if (e->shape.halfW > 0.0f && aos_shape_hit(e->pos, &p->x, &e->shape, &pt, e->angle, pang)) {
				float ca = aos_cos(e->angle), sa = aos_sin(e->angle);
				if (solid_side(p, e) > 0) {
					float A0[2] = {-ca * 1000.0f + e->pos[0], -sa * 1000.0f + e->pos[1]};
					float A1[2] = {ca * 1000.0f + e->pos[0], sa * 1000.0f + e->pos[1]};
					float P0[2] = {p->x, p->y};
					float cn = aos_cos(e->angle + 1.5707964f), sn = aos_sin(e->angle + 1.5707964f);
					float P1[2] = {-cn * 100.0f + p->x, -sn * 100.0f + p->y};
					float f[2];
					line_isect(P0, P1, A0, A1, f);
					if (orient3(P0, A0, A1) < 1) {
						p->x = f[0] - aos_cos(e->angle + 1.5707964f) * (e->shape.halfH + four);
						p->y = f[1] - aos_sin(e->angle + 1.5707964f) * (e->shape.halfH + four);
					} else {
						p->x = aos_cos(e->angle + 1.5707964f) * (e->shape.halfH + four) + f[0];
						p->y = aos_sin(e->angle + 1.5707964f) * (e->shape.halfH + four) + f[1];
					}
				}
				/* literal: this second projection runs after a rect hit regardless of the side */
				float cn = aos_cos(e->angle + 1.5707964f), sn = aos_sin(e->angle + 1.5707964f);
				float B0[2] = {-cn * 1000.0f + e->pos[0], -sn * 1000.0f + e->pos[1]};
				float B1[2] = {cn * 1000.0f + e->pos[0], sn * 1000.0f + e->pos[1]};
				float Q0[2] = {p->x, p->y};
				float Q1[2] = {-ca * 100.0f + p->x, -sa * 100.0f + p->y};
				float g[2];
				line_isect(Q0, Q1, B0, B1, g);
				if (orient3(Q0, B0, B1) < 0) {
					p->x = aos_cos(e->angle) * (e->shape.halfW + four) + g[0];
					p->y = aos_sin(e->angle) * (e->shape.halfW + four) + g[1];
				} else {
					p->x = g[0] - aos_cos(e->angle) * (e->shape.halfW + four);
					p->y = g[1] - aos_sin(e->angle) * (e->shape.halfW + four);
				}
			}
		}
	}
}

void aos_collisions(aos_world *w)
{
	if (w->roundOver)
		return;
	pass_beam_missile(w);
	pass_beam_enemy(w);
	if (!pass_beam_player(w))
		return;
	pass_graze(w, false);
	pass_hitbox_missile(w);
	pass_hitbox_enemy(w);
	pass_hitbox_player(w);
	pass_missile_missile(w);
	pass_missile_enemy(w);
	if (!pass_missile_player(w))
		return;
	pass_graze(w, true);
	pass_laser_block(w);
	pass_laser_missile(w);
	pass_laser_enemy(w);
	if (!pass_laser_player(w))
		return;
	pass_solid_push(w);
}

/* ------------------------------------------------------------------ */
/* camera FUN_005f8b90 (focus list for hyper framing not yet ported)   */

void aos_camera_update(aos_world *w)
{
	float dx = w->p[0].x - w->p[1].x, dy = w->p[0].y - w->p[1].y;
	float dist = aos_sqrt(dx * dx + dy * dy);
	float t = dist / 420.0f;
	if (t < 0.65f)
		t = 0.65f;
	if (t > 2.0f)
		t = 2.0f;
	if (t > w->obj.zoom)
		w->obj.zoom = t;
	else if (w->obj.zoom > t + 0.0075f)
		w->obj.zoom -= 0.0075f;
	else
		w->obj.zoom = t;
	float cx = (w->p[0].x + w->p[1].x) * 0.5f, cy = (w->p[0].y + w->p[1].y) * 0.5f, zv = w->obj.zoom;
	aos_camera_focus_apply(w, &cx, &cy, &zv); /* focus list (fx.c) */
	w->obj.camX = cx;
	w->obj.camY = cy;
	w->obj.zoomView = zv;
}

int aos_whip_new(aos_world *w, int32_t type, int32_t id, int32_t ownerId, int32_t p3)
{
	if (w->obj.nwhip >= AOS_MAX_WHIP) {
		w->obj.overflow++;
		return -1;
	}
	int i = w->obj.nwhip++;
	aos_whip *h = &w->obj.whip[i];
	memset(h, 0, sizeof *h);
	h->id = id;
	h->type = type;
	h->ownerId = ownerId;
	h->p3 = p3;
	return i;
}

int32_t aos_enemy_group_next(aos_world *w) /* FUN_0055efc0: DAT_00885984++, once per summon spawn */
{
	return w->obj.enemySerial++;
}
