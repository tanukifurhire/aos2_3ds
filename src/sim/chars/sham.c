/* Sham (engine id 7, states 0xa7..0xbe) — port of docs/re/sham.md.
 * Every function names its original address. Version 0x5a2 branches only.
 * Object families (all type 7): enemy 0x53d340/0x53e810/0x543860/0x5434a0,
 * beam 0x4b2ed0/0x4b3a20/0x4b3200/0x4b3830, missile 0x676e50/0x677870/
 * 0x676f70/0x677730/0x6777d0, hitbox 0x5e92d0/0x5e96e0, laser 0x625190/
 * 0x625660/0x625290/0x625640, effect 0x504b80/0x506640/0x504f50,
 * whip 0x7c71a0/0x7c7ce0/0x7c72c0. */
#include <stddef.h>
#include <string.h>

#include "../draw.h"
#include "../fx.h"
#include "../motion.h"
#include "../sim.h"

#define ID AOS_CH_SHAM
#define PI 3.1415927f       /* DAT_00853b58 */
#define TWO_PI 6.2831855f   /* DAT_00853b78 */
#define HALF_PI 1.5707964f  /* DAT_00814104 */

void aos_se_cd(aos_world *w, int id, int cd);                              /* FUN_007bcaf0 */

/* Character global shared by both players (DAT_0087e7bc, sprite-spin counter). */
typedef struct { int32_t spin; } sham_global;
static sham_global *G(aos_world *w) { return (sham_global *)w->charGlobal[ID]; }

/* ------------------------------------------------------------------ */
/* helpers                                                             */

/* Team colour words DAT_008859f4/f8 + team*0x10 -> aos_team_colour(w, team, 0/1). */

static int eff0(aos_world *w, int32_t id, int32_t var, const float pos[3], float ang, int32_t owner, int32_t age0, int32_t team)
{
	return aos_spawn_effect(w, 0, id, var, pos, ang, owner, age0, team);
}

/* Hyper start after FUN_006b1b40: SE 6, FUN_004e9280(100,.75), FUN_007bec20(100,pos,slA,slS),
 * FUN_004d7ac0(20,.5,2). Sham's hypers spawn no effect 0xfa (sham.md §1.2). */
static void flourish(aos_world *w, aos_player *p, float slA, float slS)
{
	aos_hyper_flourish_ex(w, p, 6, 100, 0.75f /* DAT_00813f3c */, 100, slA, slS, 0x14, 0.5f /* DAT_00853b1c */, 2, 0);
}

static aos_player *player_of_team(aos_world *w, int32_t team) /* FUN_00780450 */
{
	return team == 1 ? &w->p[0] : &w->p[1];
}

static aos_player *opp_of_team(aos_world *w, int32_t team) /* FUN_00780480 */
{
	return team == 1 ? &w->p[1] : &w->p[0];
}

static void face(aos_world *w, aos_player *p) /* "face" block at t==0 of most states */
{
	aos_player *o = aos_opponent(w, p);
	if (o->x < p->x || o->x == p->x)
		p->faceAngle = PI;
	else
		p->faceAngle = 0;
	p->dashAngle = p->faceAngle;
}

static void aim(aos_world *w, aos_player *p) { p->faceAngle = aos_aim(w, p); }

/* end-of-state block: dash cancel from C, return to neutral after E */
static bool tail(aos_world *w, aos_player *p, int32_t C, int32_t E)
{
	if (p->timer < C || p->D != 3) {
		if (p->timer > E)
			p->state = AOS_ST_NEUTRAL;
		return true; /* "not cancelled" */
	}
	aos_dash(w, p, 1);
	return false;
}

static aos_shape player_shape(const aos_player *p)
{
	aos_shape s = {p->hurtShape[0], p->hurtShape[1], p->hurtShape[2]};
	return s;
}

static void wrap_pi(float *a) /* the inline +/-2π wrap used by the sprite updates */
{
	if (PI < *a)
		*a -= TWO_PI;
	if (*a <= -PI && -PI != *a)
		*a += TWO_PI;
}

/* ---- enemy construction (FUN_0053d340 building blocks) ---- */

static int32_t enemy_group(aos_world *w) { return aos_enemy_group_next(w); } /* FUN_0055efc0 (DAT_00885984) */

/* FUN_0055ee70 + push; the group id (+0x04) comes from aos_enemy_group_next,
 * called once per summon spawn as in FUN_0053d340. */
static aos_enemy *enew(aos_world *w, int32_t owner, int32_t team, int32_t p18, int32_t grp, int *idx)
{
	int i = aos_enemy_new(w, ID, owner, team, p18);
	if (i < 0)
		return NULL;
	aos_enemy *e = &w->obj.enemy[i];
	e->serial = grp;
	if (idx)
		*idx = i;
	return e;
}

static aos_enemy *find_group_root(aos_world *w, int32_t grp) /* FUN_0055eff0 */
{
	for (int i = 0; i < w->obj.nenemy; i++)
		if (w->obj.enemy[i].parentId == -1 && w->obj.enemy[i].serial == grp)
			return &w->obj.enemy[i];
	return NULL;
}

static int enemy_index_by_id(aos_world *w, int32_t id) /* FUN_0055f0c0 */
{
	for (int i = 0; i < w->obj.nenemy; i++)
		if (w->obj.enemy[i].id == id)
			return i;
	return -1;
}

static int find_dwarf(aos_world *w, int32_t owner) /* FUN_0072a8a0 */
{
	for (int i = 0; i < w->obj.nenemy; i++) {
		aos_enemy *e = &w->obj.enemy[i];
		if (e->ownerId == owner && e->type == 7 && e->f10 == 200 && e->f7c == 200)
			return i;
	}
	return -1;
}

static aos_enemy *spawn_enemy(aos_world *w, aos_player *p, int32_t id)
{
	int i = aos_spawn_enemy(w, ID, id, p->objId, p->side, p->color);
	return i >= 0 ? &w->obj.enemy[i] : NULL;
}

static void set_pos(aos_enemy *e, const aos_player *p)
{
	e->pos[0] = p->x;
	e->pos[1] = p->y;
	e->pos[2] = p->z;
}

/* ------------------------------------------------------------------ */
/* input dispatch                                                      */

static bool go(aos_world *w, aos_player *p, int32_t s)
{
	aos_set_state(w, p, s);
	return true;
}

static bool attacks(aos_world *w, aos_player *p) /* shared tail of FUN_0072e300 / FUN_0072e890 */
{
	if (p->state == AOS_ST_NEUTRAL || p->state == AOS_ST_DASH) {
		if (p->D == 3) {
			aos_dash(w, p, 0);
		} else if (p->Shield == 3 && aos_spend_hyper(p, 0.5f) && aos_may_shield(w, p)) {
			aos_shield(w, p);
		} else {
			if (p->S > 1 && p->B == 3) {
				p->state = 0xb6;
				p->timer = 0;
				p->hitFlag = 0;
				if (find_dwarf(w, p->objId) != -1)
					p->state = 0xb7;
				aos_tab.T2[p->state](w, p);
				return true;
			}
			if (p->S > 1 && p->A == 3)
				return go(w, p, 0xba);
			if (p->B == 3)
				return go(w, p, 0xbc);
			if (p->D > 1 && p->A == 3) {
				bool found = false;
				for (int i = 0; i < w->obj.nenemy; i++) {
					aos_enemy *e = &w->obj.enemy[i];
					if (e->ownerId == p->objId && (e->f10 == 100 || e->f10 == 0x6e) && e->f84 == 10) {
						found = true;
						break;
					}
				}
				return go(w, p, found ? 0xb2 : 0xb9);
			}
			if (p->A == 3)
				return go(w, p, 0xb1);
		}
	}
	return aos_tab.T2[p->state](w, p);
}

static bool input_a(aos_world *w, aos_player *p) /* 0x88132c FUN_0072e300 */
{
	if (aos_may_hyper(w, p)) {
		if (p->S > 1 && p->B > 1 && p->H == 3 && aos_spend_hyper(p, 3.0f)) {
			p->state = 0xbe; /* literal: hitFlag not cleared */
			p->timer = 0;
			aos_tab.T2[p->state](w, p);
			return true;
		}
		if (p->S > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0xb8);
		if (p->B > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0xbd);
		if (p->D > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0xb5);
		if (p->H == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0xb4);
	}
	return attacks(w, p);
}

static bool input_b(aos_world *w, aos_player *p) /* 0x88137c FUN_0072e890 */
{
	if (aos_may_hyper(w, p)) {
		if (p->H > 1 && p->Shield == 3 && aos_spend_hyper(p, 3.0f)) {
			p->state = 0xbe;
			p->timer = 0;
			aos_tab.T2[p->state](w, p);
			return true;
		}
		if (p->H > 1 && p->S == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0xb8);
		if (p->H > 1 && p->B == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0xbd);
		if (p->H > 1 && p->D == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0xb5);
		if (p->H > 1 && p->A == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0xb4);
	}
	return attacks(w, p);
}

/* ------------------------------------------------------------------ */
/* states                                                              */

static bool st_neutral(aos_world *w, aos_player *p) /* 0xa7 FUN_0072b3b0 */
{
	(void)w;
	float s = aos_tab.stats[p->id].walk;
	if (p->up > 1) p->y += s;
	if (p->down > 1) p->y -= s;
	if (p->left > 1) p->x -= s;
	if (p->right > 1) p->x += s;
	p->faceAngle = p->facingRight == 1 ? 0.0f : PI;
	return true;
}

static bool st_stub(aos_world *w, aos_player *p) /* 0xa8/0xaa/0xab FUN_0072b4a0/b0/c0 */
{
	(void)w, (void)p;
	return true;
}

static bool st_ko_hook(aos_world *w, aos_player *p) /* 0xa9 FUN_0072b4d0 (from common KO) */
{
	if (p->timer != 0)
		return true;
	for (int i = 0; i < w->obj.nenemy; i++) {
		aos_enemy *e = &w->obj.enemy[i];
		if (e->ownerId != p->objId)
			continue;
		if (e->f7c == 100 || e->f7c == 0x6e || e->f7c == 200 || e->f7c == 300 || e->f7c == 0x12d ||
		    e->f7c == 0x15e || e->f7c == 0x15f) {
			e->timer2 = 0;
			e->active = 0;
			e->f84 = 0x5a;
		}
		if (e->f7c == 400) {
			e->timer2 = 0;
			e->active = 0;
			e->f84 = 900;
		}
	}
	return true;
}

static void sprite_call_spawn(aos_world *w, aos_player *p, int32_t kind) /* body of 0xb1 / 0xb3 at t==9 */
{
	aos_enemy *e = spawn_enemy(w, p, kind);
	G(w)->spin++;
	if (!e)
		return; /* UNVERIFIED: pool full (original never fails) */
	set_pos(e, p);
	if (kind == 100) {
		float dx = 0, dy = 0;
		if (p->up > 1) dy = 70.0f;     /* DAT_008145ec */
		if (p->down > 1) dy = -70.0f;  /* DAT_008256c8 */
		if (p->right > 1) dx = 70.0f;
		if (p->left > 1) dx = -70.0f;
		e->pos[0] = p->x + dx;
		e->pos[1] = p->y + dy;
		e->angle = p->faceAngle;
		e->angle2 = aos_rand_real(w, -PI, PI);
		e->f70 = 5.0f;          /* DAT_00813f40 */
		e->scratch[0] = 0.01f;  /* +0x74 turn rate DAT_00813ec8 */
	} else { /* 0x6e (state 0xb3) */
		e->pos[0] += aos_rand_real(w, -100.0f, 100.0f);
		e->pos[1] += aos_rand_real(w, -100.0f, 100.0f);
		e->angle = aos_rand_real(w, -PI, PI);
		e->angle2 = aos_rand_real(w, -PI, PI);
		e->f70 = 7.5f;           /* DAT_00814150 */
		e->scratch[0] = 0.05f;   /* DAT_00813ef4 */
	}
	if (G(w)->spin % 2 == 0)
		e->scratch[0] *= -1.0f;
	aos_se_cd(w, 0x2f, 4);
}

static bool sprite_call(aos_world *w, aos_player *p, int32_t kind)
{
	if (p->timer == 0) {
		face(w, p);
		G(w)->spin = 0;
	}
	if (p->timer == 9 && aos_use_gauge(p, 0xb1))
		sprite_call_spawn(w, p, kind);
	if (p->timer == 13 && p->A > 1)
		p->timer = 7;
	else
		tail(w, p, 0x14, 0x1e);
	return true;
}

static bool st_sprite_call(aos_world *w, aos_player *p) { return sprite_call(w, p, 100); }    /* 0xb1 FUN_0072b740 */
static bool st_sprite_call_6e(aos_world *w, aos_player *p) { return sprite_call(w, p, 0x6e); } /* 0xb3 FUN_0072bb80 (unreachable) */

static bool st_sprite_release(aos_world *w, aos_player *p) /* 0xb2 FUN_0072bf10 */
{
	if (p->timer == 0) {
		face(w, p);
		G(w)->spin = 0;
	}
	if (p->timer == 10) {
		int c = 0;
		for (int i = 0; i < w->obj.nenemy; i++) {
			aos_enemy *e = &w->obj.enemy[i];
			if (e->ownerId == p->objId && (e->f10 == 100 || e->f10 == 0x6e) && e->f84 == 10) {
				e->f10 = 0xbe;
				e->f7c = 100;
				e->timer2 = c * 10;
				e->f84 = 0x14;
				e->scratch[0] = 0.025f; /* DAT_00813edc */
				e->f70 = 20.0f;         /* DAT_008140fc */
				c++;
			}
		}
	}
	if (tail(w, p, 0x14, 0x1e))
		p->friction *= 0.5f;
	return true;
}

static bool st_poly_sprite(aos_world *w, aos_player *p) /* 0xb4 FUN_0072c170 */
{
	if (p->timer == 0) {
		aim(w, p);
		aos_barrier(w, p, 10);
		flourish(w, p, 0.0f, 0.0f);
	}
	if (3 < p->timer && p->timer < 0x2d && p->timer % 4 == 0) {
		aos_enemy *e = spawn_enemy(w, p, 0x78);
		G(w)->spin++;
		if (e) {
			set_pos(e, p);
			e->pos[0] += aos_rand_real(w, -150.0f, 150.0f);
			e->pos[1] += aos_rand_real(w, -150.0f, 150.0f);
			e->angle = aos_rand_real(w, -PI, PI);
			e->angle2 = aos_rand_real(w, -PI, PI);
			e->f70 = 7.5f;
			e->scratch[0] = 0.05f;
			if (G(w)->spin % 2 == 0)
				e->scratch[0] *= -1.0f;
		}
		aos_se_cd(w, 0x2f, 4);
	}
	tail(w, p, 0x2d, 0x50);
	return true;
}

static bool st_delta_sprite(aos_world *w, aos_player *p) /* 0xb5 FUN_0072c4b0 */
{
	if (p->timer == 0) {
		aim(w, p);
		aos_barrier(w, p, 10);
		flourish(w, p, 0.0f, 0.0f);
	}
	if (p->timer == 10) {
		static const float off[3] = {PI, 1.0471976f, -1.0471976f}; /* DAT_00853b58, DAT_008203bc */
		for (int k = 0; k < 3; k++) {
			int i = aos_spawn_enemy(w, ID, 0x96, p->objId, p->side, p->color);
			if (i < 0)
				continue;
			aos_enemy *e = &w->obj.enemy[i];
			set_pos(e, p);
			e->angle2 = p->faceAngle + off[k];
			e->f70 = 10.0f;
			aos_spawn_effect(w, ID, 400, 0, e->pos, 0, e->id, 0, e->team);
		}
		aos_se(w, 0x2f);
	}
	tail(w, p, 0x23, 0x32);
	return true;
}

static bool st_dwarf_call(aos_world *w, aos_player *p) /* 0xb6 FUN_0072c8c0 */
{
	if (p->timer == 0)
		face(w, p);
	if (p->timer == 0xf || p->timer == 0x14) {
		aos_enemy *e = spawn_enemy(w, p, 200);
		if (e) {
			set_pos(e, p);
			e->pos[0] += p->timer == 0xf ? 50.0f : -50.0f; /* DAT_008140ec */
			e->angle = p->faceAngle;
		}
		aos_se_cd(w, 0x2f, 4);
	}
	tail(w, p, 0x19, 0x23);
	return true;
}

static bool st_dwarf_tackle(aos_world *w, aos_player *p) /* 0xb7 FUN_0072cb30 */
{
	if (p->timer == 0)
		aim(w, p);
	if (p->timer == 5) {
		int n = 0;
		for (int i = 0; i < w->obj.nenemy; i++)
			if (w->obj.enemy[i].f10 == 200 && w->obj.enemy[i].ownerId == p->objId)
				n++;
		if (n < 2) {
			aos_enemy *e = spawn_enemy(w, p, 200);
			if (e) {
				set_pos(e, p);
				e->angle = p->faceAngle;
			}
			aos_se_cd(w, 0x2f, 4);
		}
		int k = 0;
		for (int c = 0; c < w->obj.nenemy; c++) {
			aos_enemy *d = &w->obj.enemy[c];
			if (d->f10 != 200 || d->f7c != 200 || (d->f84 != 10 && d->f84 != 99) || d->ownerId != p->objId)
				continue;
			if (!aos_use_gauge(p, 0xb7))
				continue;
			d->f84 = 0x14;
			d->timer2 = k * -0x14 + 0x5a;
			k++;
			for (int j = 0; j < w->obj.nenemy; j++) {
				aos_enemy *q = &w->obj.enemy[j];
				d = &w->obj.enemy[c];
				if (q->serial == d->serial && q->f10 == 0xd2 && q->ownerId == p->objId) {
					q->motionId = 5;
					q->motionFrame = 0;
				}
				if (q->serial == d->serial && q->f10 == 0xdc && q->ownerId == p->objId) {
					q->f7c = 0xdc;
					q->timer2 = 0;
					q->motionId = 6;
					q->motionFrame = 0;
				}
			}
		}
		if (k > 0)
			aos_se(w, 0x2a);
	}
	tail(w, p, 0x19, 0x23);
	return true;
}

static bool st_dwarf_rush(aos_world *w, aos_player *p) /* 0xb8 FUN_0072cf80 */
{
	if (p->timer == 0) {
		aim(w, p);
		aos_barrier(w, p, 10);
		flourish(w, p, p->faceAngle, HALF_PI * 0.5f /* DAT_0081fa90 */);
	}
	for (int k = 0; k < 10; k++) {
		if (p->timer != k * 3 + 10)
			continue;
		aos_enemy *e = spawn_enemy(w, p, 200);
		if (!e)
			continue;
		e->f7c = 0xc9;
		set_pos(e, p);
		e->pos[0] += aos_rand_real(w, -150.0f, 150.0f);
		e->pos[1] += aos_rand_real(w, -150.0f, 150.0f);
		e->angle = p->faceAngle;
		e->angle2 = p->faceAngle;
	}
	tail(w, p, 0x32, 0x50);
	return true;
}

static bool st_beam_ribbon(aos_world *w, aos_player *p) /* 0xb9 FUN_0072db50 */
{
	if (p->timer == 0)
		face(w, p);
	if ((p->timer == 0xf || p->timer == 0x11 || p->timer == 0x13 || p->timer == 0x15) && aos_use_gauge(p, 0xb9)) {
		int32_t col = aos_team_colour(w, p->side, 0);
		float a = aos_rand_real(w, -PI, PI);
		aos_spawn_effect(w, ID, 500, col, &p->x, a, p->objId, 0, p->side);
		aos_se_cd(w, 0x10, 3);
	}
	if (tail(w, p, 0x19, 0x23))
		p->friction *= 0.5f;
	return true;
}

static bool st_sphere(aos_world *w, aos_player *p) /* 0xba FUN_0072dd40 */
{
	if (p->timer == 0)
		face(w, p);
	if (p->timer == 10 && aos_use_gauge(p, 0xba)) {
		aos_enemy *e = spawn_enemy(w, p, 400);
		if (e) {
			set_pos(e, p);
			e->hp = (int32_t)(p->gaugeA * 100.0f) + 0x32; /* ver>0x5a0: DAT_00853b90 */
		}
		p->gaugeA = 0;
		if (e) {
			float pos[3] = {e->pos[0], e->pos[1], e->pos[2]};
			int32_t eid = e->id, team = e->team, p18 = e->p18;
			aos_spawn_laser(w, ID, 100, pos, 0.5235988f, eid, team, p18);  /* DAT_00817ddc */
			aos_spawn_laser(w, ID, 100, pos, -0.5235988f, eid, team, p18); /* DAT_00825d48 */
		}
		eff0(w, 0xaf0, aos_team_colour(w, p->side, 0), &p->x, 0, p->objId, 0, p->side);
		aos_se(w, 0xb);
	}
	if (tail(w, p, 0x19, 0x23))
		p->friction *= 0.5f;
	return true;
}

static void cube_ring(aos_world *w, aos_player *p, int32_t id, int lo, int hi, float div, float base)
{
	for (int k = lo; k < hi; k++) {
		aos_enemy *e = spawn_enemy(w, p, id);
		if (!e)
			continue;
		e->angle = 0;
		e->angle2 = ((float)k / div) * PI + base;
		e->pos[0] = aos_cos(e->angle2) * 100.0f + p->x;
		e->pos[1] = aos_sin(e->angle2) * 100.0f + p->y;
	}
}

static bool st_cube_tap(aos_world *w, aos_player *p) /* 0xbb FUN_0072d210 */
{
	if (p->timer == 0)
		face(w, p);
	if (p->timer == 0xc && aos_use_gauge(p, 0xbb)) {
		cube_ring(w, p, 300, -3, 4, 8.0f, p->faceAngle); /* DAT_008140d4; (k/8)·π + aim */
		aos_se_cd(w, 0x2f, 4);
	}
	tail(w, p, 0x14, 0x1e);
	return true;
}

static bool st_cube_hold(aos_world *w, aos_player *p) /* 0xbc FUN_0072d490 */
{
	if (p->timer == 0)
		face(w, p);
	if (p->timer < 0xc && p->B < 1) {
		p->state = 0xbb;
		aim(w, p);
		aos_tab.T2[p->state](w, p);
		return true;
	}
	if (p->timer == 0xf && aos_use_gauge(p, 0xbc)) {
		cube_ring(w, p, 300, -6, 6, 6.0f, 0.0f); /* DAT_00813f44 */
		aos_se_cd(w, 0x2f, 4);
	}
	tail(w, p, 0x19, 0x28);
	return true;
}

static bool st_bounding_cube(aos_world *w, aos_player *p) /* 0xbd FUN_0072d770 */
{
	if (p->timer == 0) {
		face(w, p);
		aos_barrier(w, p, 10);
		flourish(w, p, 0.0f, 0.0f);
	}
	if (p->timer == 10) {
		for (int k = -5; k < 5; k++) {
			aos_enemy *e = spawn_enemy(w, p, 0x12d);
			if (e) {
				e->angle = 0;
				e->angle2 = ((float)k / 5.0f) * PI;
				e->pos[0] = aos_cos(e->angle2) * 100.0f + p->x;
				e->pos[1] = aos_sin(e->angle2) * 100.0f + p->y;
			}
			e = spawn_enemy(w, p, 0x15f);
			if (e) {
				e->angle = 0;
				e->angle2 = ((float)k / 5.0f) * PI + 0.31415927f; /* _DAT_00825d44 */
				e->pos[0] = aos_cos(e->angle2) * 100.0f + p->x;
				e->pos[1] = aos_sin(e->angle2) * 100.0f + p->y;
			}
		}
	}
	tail(w, p, 0x1e, 0x3c);
	return true;
}

static bool st_arachne(aos_world *w, aos_player *p) /* 0xbe FUN_0072e070 */
{
	if (p->timer == 0) {
		aim(w, p);
		aos_barrier(w, p, 0x14);
		aos_hyper_flourish_ex(w, p, 6, 0x96, 0.85f /* DAT_00814120 */, 100, 0.0f, 0.0f, 100, 0.6f /* DAT_00813f34 */, 2, 0);
	}
	if (p->timer == 10) {
		aos_enemy *e = spawn_enemy(w, p, 500);
		if (e) {
			set_pos(e, p);
			e->angle = p->faceAngle;
			e->angle2 = e->angle;
			e->pos[0] -= aos_cos(e->angle) * 100.0f;
			e->pos[1] -= aos_sin(e->angle) * 100.0f;
		}
		aos_se(w, 0x33);
	}
	tail(w, p, 0x28, 100);
	return true;
}

/* ------------------------------------------------------------------ */
/* enemy family (type 7)                                               */

/* template helper for single-part kinds: FUN_0055ee70 + fields */
static int enemy_spawn(aos_world *w, int32_t id, int32_t owner, int32_t team, int32_t p18) /* FUN_0053d340 */
{
	int idx = -1;
	if (id == 100 || id == 0x6e || id == 0x78) {
		int32_t g = enemy_group(w);
		aos_enemy *e = enew(w, owner, team, p18, g, &idx);
		if (!e)
			return -1;
		e->f10 = id == 0x78 ? 100 : id;
		e->f7c = id;
		e->f84 = 0;
		e->maxHp = e->hp = id == 0x6e ? 100 : 200;
		e->active = 0;
		e->shape = (aos_shape){0, 0, 10.0f};
		e->scale[0] = 0;
		e->scale[1] = 3.0f;
		e->scale[2] = 0;
		e->mirrorChildren = 0;
		e->motionId = 0;
		e->rehitTimer = 0;
		return idx;
	}
	if (id == 0x96) {
		int32_t g = enemy_group(w);
		int i1, i2, i3, i4;
		aos_enemy *c = enew(w, owner, team, p18, g, &i1);
		if (!c)
			return -1;
		aos_enemy *k[3] = {enew(w, owner, team, p18, g, &i2), enew(w, owner, team, p18, g, &i3), enew(w, owner, team, p18, g, &i4)};
		c = &w->obj.enemy[i1];
		c->f10 = 0x96;
		c->f7c = 0x96;
		c->f84 = 0;
		c->maxHp = c->hp = 600; /* ver>0x5a0 */
		c->active = 0;
		c->shape = (aos_shape){0, 0, 0};
		c->scale[0] = c->scale[1] = c->scale[2] = 0;
		c->mirrorChildren = 0;
		c->motionId = 0;
		c->parentId = -1;
		for (int j = 0; j < 3; j++) {
			if (!k[j])
				continue;
			k[j]->f10 = 0xa0 + j;
			k[j]->f7c = 0xa0;
			k[j]->f84 = 0;
			k[j]->maxHp = k[j]->hp = 600;
			k[j]->active = 0;
			k[j]->shape = (aos_shape){0, 0, 10.0f};
			k[j]->scale[0] = 0;
			k[j]->scale[1] = 3.0f;
			k[j]->scale[2] = 0;
			k[j]->mirrorChildren = 0;
			k[j]->motionId = 2;
			k[j]->parentId = c->id;
			c->childSlots[j] = k[j]->id;
		}
		return i1;
	}
	if (id == 200) {
		int32_t g = enemy_group(w);
		int i1, i2, i3;
		aos_enemy *b = enew(w, owner, team, p18, g, &i1);
		aos_enemy *gp = enew(w, owner, team, p18, g, &i2);
		aos_enemy *gn = enew(w, owner, team, p18, g, &i3);
		if (!b)
			return -1;
		b->f10 = 200;
		b->f7c = 200;
		b->f84 = 0;
		b->maxHp = b->hp = 800;
		b->active = 1;
		b->shape = (aos_shape){0, 0, 40.0f}; /* DAT_008140e0 */
		b->scale[0] = 0;
		b->scale[1] = 3.0f;
		b->scale[2] = 0;
		b->mirrorChildren = 1;
		b->motionId = 0;
		b->parentId = -1;
		if (gp) {
			gp->f10 = gp->f7c = 0xd2;
			gp->f84 = 0;
			gp->maxHp = gp->hp = 999;
			gp->active = 0;
			gp->shape = (aos_shape){0, 0, 0};
			gp->scale[0] = 0;
			gp->scale[1] = 3.0f;
			gp->scale[2] = 0;
			gp->motionId = 4;
			gp->parentId = b->id;
			b->childSlots[0] = gp->id;
		}
		if (gn) {
			gn->f10 = gn->f7c = 0xdc;
			gn->f84 = 0;
			gn->maxHp = gn->hp = 999;
			gn->active = 0;
			gn->shape = (aos_shape){0, 0, 0};
			gn->scale[0] = 0;
			gn->scale[1] = 3.0f;
			gn->scale[2] = 0;
			gn->motionId = 3;
			if (gp) {
				gn->parentId = gp->id;
				gp->childSlots[0] = gn->id;
			}
			gn->offset[0] = -20.0f; /* DAT_00814108 */
			gn->offset[1] = 0;
			gn->pivot[0] = 5.0f;    /* DAT_00813f40 */
			gn->pivot[1] = 0;
		}
		return i1;
	}
	if (id == 300 || id == 0x12d || id == 0x15e || id == 0x15f) {
		int32_t g = enemy_group(w);
		aos_enemy *e = enew(w, owner, team, p18, g, &idx);
		if (!e)
			return -1;
		e->f10 = id;
		e->f7c = id;
		if (id == 300 || id == 0x12d)
			e->solid = 1;
		else
			e->invulnerable = 1;
		e->f84 = 0;
		e->maxHp = e->hp = 100;
		e->active = 0;
		e->shape = (aos_shape){30.0f, 30.0f, 0}; /* DAT_008140e8 */
		e->scale[0] = 0;
		e->scale[1] = 3.0f;
		e->scale[2] = 0;
		e->mirrorChildren = 0;
		e->motionId = 0;
		return idx;
	}
	if (id == 400) {
		int32_t g = enemy_group(w);
		aos_enemy *e = enew(w, owner, team, p18, g, &idx);
		if (!e)
			return -1;
		e->f10 = 400;
		e->f7c = 400;
		e->solid = 1;
		e->f84 = 0;
		e->maxHp = e->hp = 200;
		e->active = 1;
		e->shape = (aos_shape){0, 0, 0};
		e->scale[0] = e->scale[1] = e->scale[2] = 0;
		e->mirrorChildren = 0;
		e->motionId = 0;
		return idx;
	}
	if (id == 500) {
		int32_t g = enemy_group(w);
		int i1, i2, i3, i4;
		aos_enemy *b = enew(w, owner, team, p18, g, &i1);
		aos_enemy *l1 = enew(w, owner, team, p18, g, &i2);
		aos_enemy *l2 = enew(w, owner, team, p18, g, &i3);
		aos_enemy *tl = enew(w, owner, team, p18, g, &i4);
		if (!b)
			return -1;
		b->f10 = b->f7c = 500;
		b->f84 = 0;
		b->invulnerable = 1;
		b->solid = 1;
		b->maxHp = b->hp = 999;
		b->active = 1;
		b->shape = (aos_shape){30.0f, 120.0f, 60.0f}; /* DAT_008140e8, DAT_00814174, DAT_00813a58 */
		b->scale[0] = b->scale[1] = 1.0f;
		b->scale[2] = 0;
		b->mirrorChildren = 1;
		b->motionId = 0;
		b->parentId = -1;
		aos_enemy *legs[3] = {l1, l2, tl};
		static const int32_t kind[3] = {0x1fe, 0x208, 0x212};
		static const int32_t mot[3] = {8, 0xb, 3};
		for (int j = 0; j < 3; j++) {
			aos_enemy *q = legs[j];
			if (!q)
				continue;
			q->f10 = q->f7c = kind[j];
			q->f84 = 0;
			q->invulnerable = 1;
			q->solid = j < 2;
			q->maxHp = q->hp = 999;
			q->active = 0;
			q->shape = j < 2 ? (aos_shape){100.0f, 30.0f, 0} : (aos_shape){0, 0, 0};
			q->scale[0] = q->scale[1] = 1.0f;
			q->scale[2] = 0;
			q->mirrorChildren = 1;
			q->motionId = mot[j];
			q->parentId = b->id;
			b->childSlots[j] = q->id;
		}
		if (l1) {
			l1->pivot[0] = 136.0f; /* DAT_00817dd8 */
			l1->pivot[1] = 0;
			l1->offset[0] = 0;
			l1->offset[1] = 82.0f; /* DAT_00817db4 */
		}
		if (l2) {
			l2->pivot[0] = 136.0f;
			l2->pivot[1] = 0;
			l2->offset[0] = 0;
			l2->offset[1] = -82.0f; /* DAT_00817dc4 */
		}
		if (tl) {
			tl->offset[0] = 120.0f; /* DAT_00814174 */
			tl->offset[1] = 0;
		}
		return i1;
	}
	return -1;
}

static int whip_spawn(aos_world *w, int32_t id, int32_t p2, int32_t enemyId); /* FUN_007c71a0 */

static void grow(aos_enemy *e) /* scaleX += 0.1, scaleY -= 0.2 */
{
	e->scale[0] += 0.1f;
	e->scale[1] -= 0.2f;
}

static void bright_up(aos_enemy *e, float step)
{
	e->scale[2] += step;
	if (1.0f < e->scale[2])
		e->scale[2] = 1.0f;
}

/* phase-0x14 homing tackle shared by kinds 100 and 0x78 */
static bool sprite_homing(aos_world *w, aos_enemy *e)
{
	if (5.0f < e->f70)
		e->f70 *= 0.975f; /* _DAT_00814128 */
	aos_player *o = opp_of_team(w, e->team);
	if (o) {
		e->angle2 += e->scratch[0];
		wrap_pi(&e->angle2);
		float tx = aos_cos(e->angle2) * e->f70 * 30.0f + o->x;
		float ty = aos_sin(e->angle2) * e->f70 * 30.0f + o->y;
		float a = aos_atan2(ty - e->pos[1], tx - e->pos[0]);
		aos_angle_toward(&e->angle, a, aos_fabs(e->scratch[0]) * 5.0f);
		e->pos[0] = aos_cos(e->angle) * e->f70 + e->pos[0];
		e->pos[1] = aos_sin(e->angle) * e->f70 + e->pos[1];
		aos_shape hs = player_shape(o);
		if (aos_shape_hit(&o->x, e->pos, &hs, &e->shape, o->faceAngle, e->angle))
			e->timer2 = 0xfa;
	}
	if (0xf9 < e->timer2) {
		aos_spawn_hitbox(w, ID, 0x96, e->pos, e->angle2, e->ownerId, 0, e->team);
		aos_spawn_effect(w, ID, 200, aos_team_colour(w, e->team, 0), e->pos, e->angle2, e->ownerId, 0, e->team);
		aos_se_cd(w, 0x1d, 4);
		return false;
	}
	return true;
}

static void orbit_owner(aos_world *w, aos_enemy *e, float turnMul)
{
	aos_player *o = aos_player_by_objid(w, e->ownerId);
	if (!o)
		return;
	e->angle2 += e->scratch[0];
	wrap_pi(&e->angle2);
	float tx = aos_cos(e->angle2) * 100.0f + o->x;
	float ty = aos_sin(e->angle2) * 100.0f + o->y;
	float a = aos_atan2(ty - e->pos[1], tx - e->pos[0]);
	aos_angle_toward(&e->angle, a, aos_fabs(e->scratch[0]) * turnMul);
	e->pos[0] = aos_cos(e->angle) * e->f70 + e->pos[0];
	e->pos[1] = aos_sin(e->angle) * e->f70 + e->pos[1];
}

static void dwarf_parts(aos_world *w, aos_enemy *b, int phase99, bool resetGunTimer)
{
	for (int j = 0; j < w->obj.nenemy; j++) {
		aos_enemy *q = &w->obj.enemy[j];
		if (q->f10 == 0xd2 && q->serial == b->serial) {
			q->motionId = 5;
			q->motionFrame = 0;
		}
		if (q->f10 == 0xdc && q->serial == b->serial) {
			q->f7c = phase99 ? 0xdd : 0xdc;
			if (resetGunTimer)
				q->timer2 = 0;
			q->motionId = 6;
			q->motionFrame = 0;
		}
	}
}

static float bob(const aos_enemy *e) { return aos_cos(((float)(e->timer2 % 100) / 100.0f) * PI); }

static bool enemy_update(aos_world *w, aos_enemy *e) /* FUN_0053e810 */
{
	switch (e->f7c) {
	case 400: { /* Resistant Sphere */
		if (e->f84 == 0) {
			e->scale[2] += 0.1f;
			e->scale[0] += 0.1f;
			e->shape.radius = e->scale[0] * 185.0f; /* _DAT_00817df0 */
			if (1.1f <= e->scale[0]) {              /* DAT_00853b38 */
				e->scale[2] = 1.0f;
				e->scale[0] = 1.1f;
				e->f84 = 100;
				e->timer2 = 0;
				aos_spawn_hitbox(w, ID, 500, e->pos, 0, e->ownerId, 0, e->team);
			}
		} else if (e->f84 == 100) {
			if (e->hp < e->timer2) {
				e->f84 = 900;
				e->timer2 = 0;
				e->active = 0;
			}
		} else {
			e->scale[2] -= 0.02f; /* DAT_00813ed8 */
			if (e->scale[2] <= 0.0f)
				return false;
		}
		aos_player *o = player_of_team(w, e->team);
		e->pos[0] = o->x;
		e->pos[1] = o->y;
		e->pos[2] = o->z;
		e->angle = aos_rand_real(w, -PI, PI);
		return true;
	}
	case 100:
		if (e->f84 == 0) {
			grow(e);
			if (9 < e->timer2) {
				e->scale[0] = e->scale[1] = 1.0f;
				e->f84 = 10;
				e->timer2 = 0;
				e->active = 1;
				whip_spawn(w, 2, e->p18, e->id);
			}
		} else if (e->f84 == 0x14) {
			return sprite_homing(w, e);
		} else if (e->f84 == 0x5a) {
			e->scale[0] -= 0.1f;
			e->scale[1] += 0.2f;
			if (e->scale[0] <= 0.0f)
				return false;
		} else {
			bright_up(e, 0.1f);
			if (e->timer2 % 200 < 0x1e) {
				if (e->timer2 % 200 == 1) {
					aos_player *o = opp_of_team(w, e->team);
					if (o)
						e->angle = aos_atan2(o->y - e->pos[1], o->x - e->pos[0]);
					int32_t col = aos_team_colour(w, e->team, 0);
					aos_spawn_beam(w, ID, 100, e->pos, e->angle - 0.5235988f, e->ownerId, 0, e->team);
					aos_spawn_beam(w, ID, 100, e->pos, e->angle, e->ownerId, 0, e->team);
					aos_spawn_beam(w, ID, 100, e->pos, e->angle + 0.5235988f, e->ownerId, 0, e->team);
					eff0(w, 0x514, col, e->pos, 0, e->ownerId, 0, e->team);
					aos_se_cd(w, 0xf, 4);
				}
			} else {
				orbit_owner(w, e, 5.0f);
			}
			if (0x256 < e->timer2) {
				e->f84 = 0x5a;
				e->timer2 = 0;
				e->active = 0;
			}
		}
		return true;
	case 0x6e: /* unused sprite variant (state 0xb3) */
		if (e->f84 == 0) {
			grow(e);
			if (9 < e->timer2) {
				e->scale[0] = e->scale[1] = 1.0f;
				e->f84 = 10;
				e->timer2 = 0;
				e->active = 1;
				aos_spawn_effect(w, ID, 100, 0, e->pos, 0, e->id, 0, e->team);
				whip_spawn(w, 2, e->p18, e->id);
			}
		} else if (e->f84 == 0x14) {
			if (4.0f < e->f70)
				e->f70 *= 0.975f;
			aos_player *o = opp_of_team(w, e->team);
			if (o) {
				e->angle2 += e->scratch[0];
				wrap_pi(&e->angle2);
				float tx = aos_cos(e->angle2) * 100.0f + o->x, ty = aos_sin(e->angle2) * 100.0f + o->y;
				aos_angle_toward(&e->angle, aos_atan2(ty - e->pos[1], tx - e->pos[0]), aos_fabs(e->scratch[0]) * 5.0f);
				e->pos[0] = aos_cos(e->angle) * e->f70 + e->pos[0];
				e->pos[1] = aos_sin(e->angle) * e->f70 + e->pos[1];
			}
		} else if (e->f84 == 0x5a) {
			e->scale[0] -= 0.1f;
			e->scale[1] += 0.2f;
			if (e->scale[0] <= 0.0f)
				return false;
		} else {
			bright_up(e, 0.1f);
			aos_player *o = aos_player_by_objid(w, e->ownerId);
			if (o) {
				e->angle2 += e->scratch[0];
				wrap_pi(&e->angle2);
				float tx = aos_cos(e->angle2) * 100.0f + o->x, ty = aos_sin(e->angle2) * 100.0f + o->y;
				aos_angle_toward(&e->angle, aos_atan2(ty - e->pos[1], tx - e->pos[0]), aos_fabs(e->scratch[0]) * 2.5f);
				e->pos[0] = aos_cos(e->angle) * e->f70 + e->pos[0];
				e->pos[1] = aos_sin(e->angle) * e->f70 + e->pos[1];
				if (e->timer2 % 2 == 0)
					aos_spawn_hitbox(w, ID, 100, e->pos, e->angle2, e->ownerId, 0, e->team);
			}
			if (0x256 < e->timer2) {
				e->f84 = 0x5a;
				e->timer2 = 0;
				e->active = 0;
			}
		}
		return true;
	case 0x78: /* Poly Sprite */
		if (e->f84 == 0) {
			grow(e);
			if (9 < e->timer2) {
				e->scale[0] = e->scale[1] = e->scale[2] = 1.0f;
				e->scratch[0] = 0.025f;
				e->f70 = 20.0f;
				e->f84 = 0x14;
				e->timer2 = 0x96;
				e->active = 1;
				whip_spawn(w, 2, e->p18, e->id);
			}
		} else if (e->f84 == 0x14) {
			return sprite_homing(w, e);
		}
		return true;
	case 0x96: { /* Delta Sprite centre */
		aos_enemy *k[3];
		if (e->f84 == 0) {
			e->f70 *= 0.95f; /* _DAT_00814124 */
			e->pos[0] = aos_cos(e->angle2) * e->f70 + e->pos[0];
			e->pos[1] = aos_sin(e->angle2) * e->f70 + e->pos[1];
			if (99 < e->timer2) {
				e->f84 = 100;
				e->timer2 = 0;
				e->f70 = 0;
				aos_player *o = opp_of_team(w, e->team);
				e->angle2 = aos_atan2(o->y - e->pos[1], o->x - e->pos[0]);
			}
		} else if (e->f84 == 100) {
			e->f70 += 0.025f;
			if (2.0f < e->f70)
				e->f70 = 2.0f;
			e->pos[0] = aos_cos(e->angle2) * e->f70 + e->pos[0];
			e->pos[1] = aos_sin(e->angle2) * e->f70 + e->pos[1];
			if (!aos_in_arena(e->pos, 0)) {
				e->f84 = 200;
				e->timer2 = 0;
			}
		} else if (99 < e->timer2) {
			for (int j = 0; j < 3; j++) {
				aos_enemy *c = aos_enemy_by_id(w, e->childSlots[j]);
				if (c) {
					c->parentId = -1;
					c->f10 = 0xbe;
					c->f7c = 100;
					c->timer2 = 0;
					c->f84 = 0x5a;
					c->scale[2] = 1.0f;
				}
			}
			return false;
		}
		int n = 0;
		for (int j = 0; j < 3; j++) {
			k[j] = aos_enemy_by_id(w, e->childSlots[j]);
			if (k[j])
				n++;
		}
		if (n != 3) {
			for (int j = 0; j < 3; j++) {
				aos_enemy *c = k[j];
				if (!c)
					continue;
				c->parentId = -1;
				c->f10 = 0xbe;
				c->f7c = 100;
				c->timer2 = 0;
				c->f84 = 0x14;
				c->scratch[0] = 0.025f;
				c->f70 = 20.0f;
				c->scale[2] = 1.0f;
				c->angle2 = aos_atan2(c->pos[1] - e->pos[1], c->pos[0] - e->pos[0]);
			}
			return false;
		}
		aos_player *o = opp_of_team(w, e->team);
		if (aos_point_in_tri(k[0]->pos, k[1]->pos, k[2]->pos, &o->x)) {
			aos_status(w, o, AOS_STATUS_NODASH, 200);
			if (o->state == AOS_ST_DASH)
				o->state = AOS_ST_NEUTRAL;
		}
		e->angle += 0.025f;
		if (PI < e->angle)
			e->angle -= TWO_PI;
		return true;
	}
	case 0xa0: { /* Delta Sprite corner */
		if (e->f84 == 0) {
			grow(e);
			if (9 < e->timer2) {
				e->scale[0] = e->scale[1] = 1.0f;
				e->f84 = 10;
				e->timer2 = 0;
				e->active = 1;
				whip_spawn(w, 2, e->p18, e->id);
			}
		} else {
			bright_up(e, 0.1f);
		}
		aos_enemy *pa = aos_enemy_by_id(w, e->parentId);
		if (pa) {
			float a = pa->angle;
			if (e->f10 == 0xa1)
				a += 2.0943952f; /* DAT_00817898 */
			if (e->f10 == 0xa2)
				a -= 2.0943952f;
			e->pos[0] = aos_cos(a) * 100.0f + pa->pos[0];
			e->pos[1] = aos_sin(a) * 100.0f + pa->pos[1];
		}
		return true;
	}
	case 200: { /* Dwarf */
		if (e->f84 == 0) {
			grow(e);
			if (9 < e->timer2) {
				e->scale[0] = e->scale[1] = 1.0f;
				e->f84 = 99;
				e->timer2 = 0;
				dwarf_parts(w, e, 1, false);
			}
		} else if (e->f84 == 10 || e->f84 == 0xb) {
			aos_player *ow = aos_player_by_objid(w, e->ownerId);
			aos_player *o = opp_of_team(w, e->team);
			if (ow && o)
				e->angle = aos_atan2(o->y - ow->y, o->x - ow->x);
			if (ow) {
				float a = (uint32_t)e->serial % 2 == 0 ? e->angle + HALF_PI : e->angle - HALF_PI;
				float tx = aos_cos(a) * 50.0f + ow->x, ty = aos_sin(a) * 50.0f + ow->y;
				if (tx < e->pos[0])
					e->pos[0] -= 4.0f;
				if (e->pos[0] <= tx && tx != e->pos[0])
					e->pos[0] += 4.0f;
				if (ty < e->pos[1])
					e->pos[1] -= 4.0f;
				if (e->pos[1] <= ty && ty != e->pos[1])
					e->pos[1] += 4.0f;
			}
			if (100 < e->timer2) {
				e->f84 = 99;
				e->timer2 = 0;
				dwarf_parts(w, e, 1, true);
			}
		} else if (e->f84 == 0x14) {
			if (e->timer2 == 100) {
				aos_player *o = opp_of_team(w, e->team);
				if (o) {
					e->angle = aos_atan2(o->y - e->pos[1], o->x - e->pos[0]);
					e->angle2 = e->angle;
				}
				e->f70 = 20.0f;
				aos_se(w, 0x20);
			}
			if (99 < e->timer2) {
				e->pos[0] = aos_cos(e->angle2) * e->f70 + e->pos[0];
				e->pos[1] = aos_sin(e->angle2) * e->f70 + e->pos[1];
				e->f70 *= 0.95f;
				if (e->timer2 == 0x65) {
					for (int k = 0; k < 5; k++) {
						float r = aos_rand_real(w, -0.15f, 0.15f);
						int32_t n = aos_rand_int(w, 0, 100);
						eff0(w, 0xa32 + n % 4, 0xd, e->pos, r + e->angle2, e->ownerId, k * -3, e->team);
					}
				}
				if (5.0f < e->f70) {
					float r = aos_rand_real(w, -HALF_PI, HALF_PI);
					float v[2] = {aos_cos(r) * 30.0f - 10.0f, aos_sin(r) * 50.0f};
					aos_vec_rotate(v, e->angle);
					float sp[3] = {v[0] + e->pos[0], v[1] + e->pos[1], 0};
					aos_spawn_spark(w, 0x7da, aos_team_colour(w, e->team, 0), 6, 1, e->angle + PI, sp, e->ownerId);
				}
				if (3.0f < e->f70 && e->timer2 % 4 == 0)
					aos_spawn_hitbox(w, ID, 200, e->pos, e->angle2, e->ownerId, 0, e->team);
			}
			if (200 < e->timer2) {
				e->f84 = 0xb;
				e->timer2 = 0;
			}
		} else {
			if (e->f84 == 0x5a) {
				aos_enemy_killed_fn f = aos_otab.enemyOnKilled[e->type];
				if (f)
					f(w, e);
				return false;
			}
			aos_player *ow = aos_player_by_objid(w, e->ownerId);
			aos_player *o = opp_of_team(w, e->team);
			if (ow && o)
				e->angle = aos_atan2(o->y - e->pos[1], o->x - e->pos[0]);
			bright_up(e, 0.1f);
			e->pos[1] = bob(e) + e->pos[1];
			if (200 < e->timer2) {
				e->f84 = 10;
				e->timer2 = 0;
				dwarf_parts(w, e, 0, true);
			}
		}
		return true;
	}
	case 0xc9: { /* Dwarf Rush dwarf */
		if (e->f84 == 0) {
			grow(e);
			if (9 < e->timer2) {
				e->scale[0] = e->scale[1] = 1.0f;
				e->f70 = 0;
				e->f84 = 0x14;
				e->timer2 = 0;
				for (int j = 0; j < w->obj.nenemy; j++) {
					aos_enemy *q = &w->obj.enemy[j];
					if (q->f10 == 0xd2 && q->serial == e->serial) {
						q->motionId = 5;
						q->motionFrame = 0;
					}
					if (q->f10 == 0xdc && q->serial == e->serial) {
						q->motionId = 6;
						q->motionFrame = 0;
					}
				}
			}
		}
		if (e->f84 == 1) {
			e->scale[0] -= 0.1f;
			e->scale[1] += 0.2f;
			if (9 < e->timer2)
				return false;
		} else if (e->f84 == 0x14) {
			if (e->timer2 == 0) {
				e->f70 = 10.0f;
				aos_se_cd(w, 0x20, 10);
			}
			if (0 <= e->timer2 && e->timer2 < 0x14 && e->timer2 % 2 == 0) {
				float r = aos_rand_real(w, -0.25f, 0.25f);
				int32_t n = aos_rand_int(w, 0, 100);
				eff0(w, 0xa32 + n % 4, 0xd, e->pos, r + e->angle2, e->ownerId, 0, e->team);
			}
			e->pos[0] = aos_cos(e->angle2) * e->f70 + e->pos[0];
			e->pos[1] = aos_sin(e->angle2) * e->f70 + e->pos[1];
			bright_up(e, 0.1f);
			if (0.0f < e->f70 && e->timer2 % 5 == 0)
				aos_spawn_hitbox(w, ID, 0xd2, e->pos, e->angle2, e->ownerId, 0, e->team);
			if (0x32 < e->timer2 && !aos_in_arena(e->pos, 100.0f)) {
				e->scale[0] = e->scale[1] = 1.0f;
				e->scale[2] = 0;
				e->f70 = 0;
				e->f84 = 1;
				e->timer2 = 0;
				for (int j = 0; j < w->obj.nenemy; j++) {
					aos_enemy *q = &w->obj.enemy[j];
					if (q->f10 == 0xd2 && q->serial == e->serial) {
						q->motionId = 4;
						q->f84 = 1;
						q->scale[2] = 0;
						q->motionFrame = 0;
					}
					if (q->f10 == 0xdc && q->serial == e->serial) {
						q->motionId = 0;
						q->f84 = 1;
						q->scale[2] = 0;
						q->motionFrame = 0;
					}
				}
				return true;
			}
		}
		return true;
	}
	case 0xd2:
	case 0xdc:
	case 0xdd: /* dwarf parts */
		if (e->f84 == 0) {
			grow(e);
			if (9 < e->timer2) {
				e->scale[0] = e->scale[1] = 1.0f;
				e->f84 = 10;
				e->timer2 = 0;
			}
		} else if (e->f84 == 1) {
			if (0.0f < e->scale[0])
				e->scale[0] -= 0.1f;
			if (e->scale[0] <= 3.0f && 3.0f != e->scale[0])
				e->scale[1] += 0.2f;
		} else {
			bright_up(e, 0.1f);
			e->pos[1] = bob(e) + e->pos[1];
			if (e->f7c == 0xdd && 0xa3 < e->timer2 % 200 && e->timer2 % 200 < 0xc2 && (e->timer2 - 0xa4) % 3 == 0) {
				int32_t col = aos_team_colour(w, e->team, 1);
				float a = aos_rand_real(w, -0.25f, 0.25f) + e->angle;
				aos_spawn_beam(w, ID, 200, e->pos, a, e->ownerId, 0, e->team);
				aos_spawn_missile(w, ID, 100, e->pos, a, e->id, 0, e->team, e->p18);
				eff0(w, 0x547, col, e->pos, a, e->ownerId, 0, e->team);
				aos_se_cd(w, 0x10, 5);
			}
		}
		return true;
	case 300:
	case 0x12d:
	case 0x15e:
	case 0x15f: { /* cubes */
		bool bounding = e->f7c == 0x15e || e->f7c == 0x15f;
		if (e->f84 == 0) {
			e->scale[2] = 1.0f;
			e->scale[0] += 0.05f; /* DAT_00813ef4 */
			e->scale[1] -= 0.1f;
			if (0xd <= e->timer2) {
				e->scale[0] = e->scale[1] = 1.0f;
				e->f84 = (e->f10 == 0x12d || e->f10 == 0x15f) ? 0x14 : 10;
				e->timer2 = 0;
				e->f70 = 0.1f;
				e->active = 1;
				e->angle = e->angle2;
			}
		} else if (e->f84 == 10 || e->f84 == 0x14) {
			if (e->f70 <= 5.0f && 5.0f != e->f70)
				e->f70 *= e->f84 == 10 ? 1.05f : 1.025f; /* DAT_00817de4 / _DAT_00817de0 */
			e->pos[0] = aos_cos(e->angle2) * e->f70 + e->pos[0];
			e->pos[1] = aos_sin(e->angle2) * e->f70 + e->pos[1];
			bool boom = false;
			if (bounding && e->f84 == 0x14) {
				aos_player *o = opp_of_team(w, e->team);
				aos_shape hs = player_shape(o);
				if (aos_shape_hit(&o->x, e->pos, &hs, &e->shape, o->faceAngle, e->angle))
					boom = true;
			}
			if (!boom && !aos_in_arena(e->pos, -10.0f)) /* DAT_00817c10 */
				boom = true;
			if (boom) {
				aos_spawn_hitbox(w, ID, bounding ? 0x96 : 300, e->pos, e->angle2, e->ownerId, 0, e->team);
				int32_t col = aos_team_colour(w, e->team, 1);
				aos_spawn_effect(w, ID, 300, col, e->pos, e->angle2, e->ownerId, 0, e->team);
				eff0(w, 0x514, col, e->pos, e->angle2, e->ownerId, 0, e->team);
				return false;
			}
		} else {
			e->scale[0] -= 0.1f;
			e->scale[1] += 0.2f;
			if (e->scale[0] <= 0.0f)
				return false;
		}
		return true;
	}
	case 500: { /* Arachne body */
		e->scale[2] += 0.05f;
		if (1.0f < e->scale[2])
			e->scale[2] = 1.0f;
		if (e->timer2 == 10)
			e->f70 = 0.15f; /* DAT_00813f20 */
		if (e->f70 <= 7.5f && 7.5f != e->f70)
			e->f70 *= 1.575f; /* DAT_00817de8 */
		if (7.5f < e->f70)
			e->f70 = 7.5f;
		e->pos[0] = aos_cos(e->angle2) * e->f70 + e->pos[0];
		e->pos[1] = aos_sin(e->angle2) * e->f70 + e->pos[1];
		for (int k = 0; k < 2; k++) {
			float r = aos_rand_real(w, -HALF_PI, HALF_PI);
			float v[2] = {aos_cos(r) * 80.0f - 20.0f, aos_sin(r) * 300.0f}; /* DAT_00814164, DAT_008140fc, DAT_008145f0 */
			aos_vec_rotate(v, e->angle);
			float sp[3] = {v[0] + e->pos[0], v[1] + e->pos[1], 0};
			aos_spawn_spark(w, 0x7da, aos_team_colour(w, e->team, 1), 10, 1, e->angle + PI, sp, e->ownerId);
		}
		if (0xe <= e->timer2 && e->timer2 % 2 == 0) {
			float oy = aos_rand_real(w, -280.0f, 280.0f);
			float ox = aos_rand_real(w, -240.0f, 240.0f);
			float v[2] = {ox, oy};
			aos_vec_rotate(v, e->angle2);
			float pt[3] = {aos_cos(e->angle2) * 360.0f + e->pos[0] + v[0], aos_sin(e->angle2) * 360.0f + e->pos[1] + v[1], 0};
			aos_spawn_effect(w, ID, 600, aos_team_colour(w, e->team, 1), pt, e->angle2, e->ownerId, 0, e->team);
			aos_se_cd(w, 0x14, 0x10);
		}
		if (e->timer2 == 100) {
			for (int j = 0; j < w->obj.nenemy; j++) {
				aos_enemy *q = &w->obj.enemy[j];
				if (q->f10 == 0x1fe && q->ownerId == e->ownerId) {
					q->motionId = 9;
					q->motionFrame = 0;
				}
				if (q->f10 == 0x208 && q->ownerId == e->ownerId) {
					q->motionId = 0xc;
					q->motionFrame = 0;
				}
			}
		}
		if (2 < e->timer2 && e->timer2 < 0x31 && e->timer2 % 3 == 0) {
			float pt[3] = {aos_cos(e->angle2) * -100.0f + e->pos[0], aos_sin(e->angle2) * -100.0f + e->pos[1], 0};
			float r = aos_rand_real(w, -0.15f, 0.15f);
			int32_t n = aos_rand_int(w, 0, 100);
			eff0(w, 0xa82 + n % 8, 0xd, pt, r + e->angle2, e->ownerId, 0, e->team);
			if (e->timer2 == 4)
				aos_se(w, 0x20);
		}
		if (100 <= e->timer2 && !aos_in_arena(e->pos, 50.0f)) {
			aos_spawn_effect(w, ID, 0x262, 0, e->pos, e->angle2, e->ownerId, 0, e->team);
			aos_screen_dark(w, 100, 0.75f);           /* DAT_00813f3c */
			aos_screen_blur(w, 0x32, 0.4f, 2);        /* DAT_00813f30 */
			aos_speedline(w, 100, e->pos, e->angle, 0.0f);
			for (int k = 0; k < 5; k++) {
				float r = aos_rand_real(w, -0.15f, 0.15f);
				int32_t n = aos_rand_int(w, 0, 100);
				eff0(w, 0xa82 + n % 8, 0xc, e->pos, r + e->angle2, e->ownerId, k * -5, e->team);
			}
			aos_se(w, 0x1a);
			return false;
		}
		return true;
	}
	case 0x1fe:
	case 0x208:
	case 0x212:
		e->scale[2] += 0.05f;
		if (1.0f < e->scale[2])
			e->scale[2] = 1.0f;
		return true;
	default:
		return true;
	}
}

static bool enemy_killed(aos_world *w, aos_enemy *e) /* FUN_005434a0 */
{
	switch (e->f10) {
	case 100:
	case 0x6e:
	case 0xbe:
		if (e->f84 == 0x14) {
			aos_spawn_hitbox(w, ID, 0x96, e->pos, e->angle2, e->ownerId, 0, e->team);
			aos_spawn_effect(w, ID, 200, aos_team_colour(w, e->team, 0), e->pos, e->angle2, e->ownerId, 0, e->team);
		} else {
			eff0(w, 600, 0, e->pos, 0, e->ownerId, 0, e->team);
		}
		break;
	case 200:
		eff0(w, 600, 0, e->pos, 0, e->ownerId, 0, e->team);
		for (int k = 0; k < 5; k++) {
			float pt[3] = {e->pos[0], e->pos[1], e->pos[2]};
			pt[0] += aos_rand_real(w, -50.0f, 50.0f);
			pt[1] += aos_rand_real(w, -50.0f, 50.0f);
			eff0(w, 0x834, 0, pt, 0, e->ownerId, 0, e->team);
		}
		for (int k = 0; k < 5; k++) {
			float pt[3] = {e->pos[0], e->pos[1], e->pos[2]};
			pt[0] += aos_rand_real(w, -30.0f, 30.0f);
			pt[1] += aos_rand_real(w, -30.0f, 30.0f);
			eff0(w, 500, 0, pt, 0, e->ownerId, k * -3 - 10, e->team);
		}
		aos_se(w, 0x1a);
		break;
	default:
		break;
	}
	return false;
}

/* ---- enemy draw FUN_00543860 ---- */

static int bank_of_owner(aos_world *w, int32_t owner)
{
	aos_player *o = aos_player_by_objid(w, owner);
	return o ? aos_pidx(w, o) : AOS_BANK_P0;
}

static int bank_of_enemy_id(aos_world *w, int32_t id) /* objects owned by a summon */
{
	aos_enemy *e = aos_enemy_by_id(w, id);
	return e ? bank_of_owner(w, e->ownerId) : AOS_BANK_P0;
}

static int enemy_bank(aos_world *w, const aos_enemy *e) /* sprite bank of the owner (original: e+0x18 slot) */
{
	return bank_of_owner(w, e->ownerId);
}

static aos_dcmd *esp(aos_world *w, aos_enemy *e, const char *fmt, int f, float size, int blend)
{
	aos_dcmd *c = aos_draw(aos_texf(fmt, f), enemy_bank(w, e), e->pos[0], e->pos[1], 0);
	c->halfW = c->halfH = size * 0.5f;
	c->sx = e->scale[0];
	c->sy = e->scale[1];
	c->blend = (uint8_t)blend;
	c->rgba = aos_rgba(e->scale[2], e->scale[2], e->scale[2], 1.0f);
	return c;
}

static void aim_x(aos_dcmd *c, float a, float flipTestAngle) /* AIM transform about +0x58 */
{
	c->rotZ = aos_rad2bam(a);
	c->flipX = 1;
	if (HALF_PI <= aos_fabs(flipTestAngle))
		c->sy = -c->sy; /* RotX(180°) */
}

static void glow3(aos_world *w, aos_enemy *e, const char *fmt, int f0, int f1, int f2, float size, bool aimx, float flipA)
{
	int fr[3] = {f0, f1, f2};
	for (int k = 0; k < 3; k++) {
		aos_dcmd *c = esp(w, e, fmt, fr[k], size, k == 0 ? AOS_BLEND_ALPHA : AOS_BLEND_ADD_ALPHA);
		if (aimx)
			aim_x(c, e->angle, flipA);
	}
}

static void enemy_draw(aos_world *w, aos_enemy *e) /* FUN_00543860 */
{
	switch (e->f10) {
	case 400: {
		float pulse = aos_sin(((float)(e->age % 0x32) / 50.0f) * TWO_PI) * 0.15f + 0.6f;
		float b = e->scale[2];
		aos_dcmd *c = esp(w, e, "sm_impact%d", 3, 96.0f, AOS_BLEND_ALPHA);
		c->sx = c->sy = e->scale[0] * 5.0f;
		c->rotZ = aos_rad2bam(e->angle);
		c->rgba = aos_rgba(b, b, b, pulse * b);
		c = esp(w, e, "sm_impact%d", 4, 96.0f, AOS_BLEND_ADD_ALPHA);
		c->sx = c->sy = e->scale[0] * 5.0f;
		c->rotZ = aos_rad2bam(e->angle);
		c->rgba = aos_rgba(b, b / 2.0f, b, 0.5f);
		break;
	}
	case 100:
	case 0x6e:
	case 0xa0:
	case 0xa1:
	case 0xa2:
		glow3(w, e, "sm_wisp%02d", (e->age / 4) % 4, 4, 5, 64.0f, false, 0);
		break;
	case 0xbe:
		glow3(w, e, "sm_wisp%02d", 6 + (e->age / 4) % 4, 10, 11, 64.0f, true, e->angle);
		break;
	case 200: {
		int b = 0;
		if (e->f84 == 10)
			b = 3;
		else if (e->f84 == 0x14)
			b = (5.0f < e->f70 || 5.0f == e->f70) ? 6 : 9;
		glow3(w, e, "sm_dwarf%02d", b, b + 1, b + 2, b < 12 ? 144.0f : 72.0f, true, e->angle);
		break;
	}
	case 0xd2:
	case 0xdc: {
		aos_enemy *r = find_group_root(w, e->serial);
		if (r && (r->f84 == 10 || (r->f84 == 0x14 && !(5.0f < r->f70 || 5.0f == r->f70))))
			break;
		if (e->f10 == 0xd2)
			aim_x(esp(w, e, "sm_dwarf%02d", 12, 72.0f, AOS_BLEND_ALPHA), e->angle, e->angle);
		else
			glow3(w, e, "sm_dwarf%02d", 13, 14, 15, 72.0f, true, e->angle);
		break;
	}
	case 300:
	case 0x12d:
	case 0x15e:
	case 0x15f: {
		const char *fmt = (e->f10 == 300 || e->f10 == 0x12d) ? "sm_cube%02d" : "sm_cubeb%02d";
		float pulse = aos_sin(((float)(e->age % 0x32) / 50.0f) * TWO_PI) * 0.35f + 0.65f;
		aos_dcmd *c = esp(w, e, fmt, (e->age / 5) % 3 + 3, 72.0f, AOS_BLEND_ALPHA);
		c->sx *= 1.15f;
		c->sy *= 1.15f;
		c->rotZ = aos_rad2bam(e->angle);
		c->rgba = aos_rgba(e->scale[2], e->scale[2], e->scale[2], pulse * e->scale[2]);
		c = esp(w, e, fmt, (e->age / 5) % 3, 72.0f, AOS_BLEND_ADD_ALPHA);
		c->rotZ = aos_rad2bam(e->angle);
		break;
	}
	case 500:
		glow3(w, e, "sm_arach%02d", 0, 1, 2, 384.0f, true, e->angle);
		break;
	case 0x1fe:
	case 0x208: {
		aos_enemy *r = find_group_root(w, e->serial);
		bool flip = HALF_PI <= aos_fabs(r ? r->angle : e->angle); /* test uses the root's angle */
		for (int k = 0; k < 3; k++) {
			aos_dcmd *c = esp(w, e, "sm_arach%02d", 3 + k, 384.0f, k == 0 ? AOS_BLEND_ALPHA : AOS_BLEND_ADD_ALPHA);
			c->rotZ = aos_rad2bam(e->angle);
			c->flipX = 1;
			/* 0x1fe: RotX(180) when flipped; 0x208: RotX(180) when NOT flipped (FUN_00543860) */
			if ((e->f10 == 0x1fe) == flip)
				c->sy = -c->sy;
		}
		break;
	}
	case 0x212:
		glow3(w, e, "sm_arach%02d", 6, 7, 8, 384.0f, true, e->angle);
		break;
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* beam family (type 7)                                                */

static int beam_spawn(aos_world *w, int32_t id, const float pos[3], float ang, int32_t owner, int32_t age0, int32_t team) /* FUN_004b2ed0 */
{
	if (id != 100 && id != 200 && id != 300 && id != 0x15e)
		return -1;
	int i = aos_beam_new(w, ID, id, pos, ang, owner, age0, team);
	if (i < 0)
		return -1;
	aos_beam *b = &w->obj.beam[i];
	if (id == 100) {
		b->speed = 18.0f; /* DAT_00814144 */
		b->scale = 3.0f;
		b->damage = 0x78;
		b->shape = (aos_shape){0, 0, 10.0f};
		b->knockback = 5.0f;
	} else if (id == 200) {
		b->speed = 12.0f;
		b->scale = 2.0f;
		b->damage = 0x50; /* ver>0x5a0 */
		b->shape = (aos_shape){0, 0, 10.0f};
		b->knockback = 5.0f;
	} else {
		b->speed = 10.0f;
		b->damage = 0x78;
		b->shape = (aos_shape){15.0f, 5.0f, 0};
		b->knockback = 10.0f;
	}
	return i;
}

static bool beam_update(aos_world *w, aos_beam *b) /* FUN_004b3a20 */
{
	switch (b->id) {
	case 100:
		if (6.0f < b->speed)
			b->speed *= 0.95f;
		b->pos[0] = aos_cos(b->moveAngle) * b->speed + b->pos[0];
		b->pos[1] = aos_sin(b->moveAngle) * b->speed + b->pos[1];
		if (b->scale <= 1.0f)
			b->scale = 1.0f;
		else
			b->scale -= 0.2f;
		if (0x1e < b->age && !aos_in_arena(b->pos, 20.0f)) {
			b->id = 0x6e;
			b->active = 0;
			b->scale = 3.0f;
			eff0(w, 0x516, aos_team_colour(w, b->team, 0), b->pos, 0, b->ownerId, 0, b->team);
		}
		return true;
	case 0x6e:
		b->scale -= 0.5f;
		return !(b->scale <= 0.0f);
	case 200:
		if (5.0f < b->speed)
			b->speed *= 0.975f;
		b->pos[0] = aos_cos(b->moveAngle) * b->speed + b->pos[0];
		b->pos[1] = aos_sin(b->moveAngle) * b->speed + b->pos[1];
		if (b->scale <= 1.0f)
			b->scale = 1.0f;
		else
			b->scale -= 0.2f;
		return aos_in_arena(b->pos, 10.0f);
	case 300:
	case 0x15e: {
		if (5.0f < b->speed)
			b->speed *= 0.985f; /* _DAT_00814148 */
		float c = b->speed * 0.0075f * ((float)(0x96 - b->age) / 150.0f);
		if (c <= 0.0f)
			c = 0.0f;
		if (b->id == 300)
			b->moveAngle += c;
		else
			b->moveAngle -= c;
		b->angle = b->moveAngle;
		b->pos[0] = aos_cos(b->moveAngle) * b->speed + b->pos[0];
		b->pos[1] = aos_sin(b->moveAngle) * b->speed + b->pos[1];
		if (b->age % 2 == 0)
			aos_spawn_effect(w, ID, 0x226, aos_team_colour(w, b->team, 0), b->pos, b->moveAngle, b->ownerId, 0, b->team);
		if (0x1e < b->age && !aos_in_arena(b->pos, 20.0f))
			return false;
		return true;
	}
	default:
		return true;
	}
}

static void beam_onhit(aos_world *w, aos_beam *b) /* FUN_004b3830 */
{
	int32_t col = aos_team_colour(w, b->team, 0);
	if (b->id == 100) {
		eff0(w, 1000, col, b->pos, b->angle + PI, b->ownerId, 0, b->team);
		aos_spawn_particle(w, 1000, col, b->pos, b->angle, b->ownerId, 0, 3);
		aos_spawn_spark(w, 1000, col, 10, 5, b->angle, b->pos, b->ownerId);
		aos_se_cd(w, 0x13, 4);
	} else if (b->id == 300 || b->id == 0x15e) {
		eff0(w, 1000, col, b->pos, b->angle + PI, b->ownerId, 0, b->team);
		aos_spawn_particle(w, 1000, col, b->pos, b->angle, b->ownerId, 0, 10);
		aos_spawn_spark(w, 1000, col, 10, 10, b->angle, b->pos, b->ownerId);
		aos_se(w, 0x13);
	}
}

/* Common bullet atlas DAT_0087fde4 = "beamsphere" (loaded all.c:132423);
 * cells of colour index c (DAT_008859f4 + team*0x10): glow 100x100, core 50x50. */
static void cell_glow(int32_t c, float r[4])
{
	r[0] = (float)((c % 10 & 1) * 100 + 0x32 + (c / 10) * 0xfa);
	r[1] = (float)((c % 10 / 2) * 100);
	r[2] = (float)((c % 10 & 1) * 100 + 0x95 + (c / 10) * 0xfa);
	r[3] = r[1] + 99.0f;
}
static void cell_core(int32_t c, float r[4])
{
	r[0] = (float)((c / 10) * 0xfa);
	r[1] = (float)((c % 10) * 0x32);
	r[2] = r[0] + 49.0f;
	r[3] = r[1] + 49.0f;
}

static uint32_t argb(uint32_t v) { return v << 8 | v >> 24; } /* 0xAARRGGBB -> rgba */

/* 2D sprite call (vtable +0x68(dst, 1.0, src, rot, argb, blend)). Half-sizes in world units;
 * rotWorld = world CCW angle (the original passes -angle in screen space for +angle). */
static aos_dcmd *quad2d(int tex, int bank, const float pos[3], float hw, float hh, const float src[4],
                        float rotWorld, uint32_t argbv, int blend)
{
	aos_dcmd *c = aos_draw(tex, bank, pos[0], pos[1], 0);
	c->halfW = hw;
	c->halfH = hh;
	c->hasSrc = 1;
	for (int k = 0; k < 4; k++)
		c->src[k] = src[k];
	c->rotZ = aos_rad2bam(rotWorld);
	c->blend = (uint8_t)blend;
	c->rgba = argb(argbv);
	return c;
}

static uint32_t aw(float alpha255) { return (uint32_t)(int64_t)alpha255 << 24 | 0xffffff; } /* (uint)a<<24|0xffffff */

static void bullet(int32_t col, const float pos[3], float rg, float ag, float rc, float ac, uint32_t coreRgb)
{
	float r[4];
	int tex = aos_tex("beamsphere");
	cell_glow(col, r);
	quad2d(tex, AOS_BANK_COMMON, pos, rg, rg, r, 0, aw(ag), AOS_BLEND_ALPHA);
	cell_core(col, r);
	quad2d(tex, AOS_BANK_COMMON, pos, rc, rc, r, 0, (uint32_t)(int64_t)ac << 24 | coreRgb, AOS_BLEND_ADD_ALPHA);
}

static int team_bank(int32_t team) { return team == 2 ? AOS_BANK_P1 : AOS_BANK_P0; } /* DAT_00885a00 + team*0x10 */

static void beam_draw(aos_world *w, aos_beam *b) /* FUN_004b3200 */
{
	if (b->id == 100 || b->id == 0x6e) {
		bullet(aos_team_colour(w, b->team, 0), b->pos, 24.0f * b->scale, 96.0f * b->alpha, 10.0f * b->scale,
		       255.0f * b->alpha, 0xffffff); /* DAT_00853b8c, DAT_00814118 / DAT_00813f4c, DAT_00853b94 */
	} else if (b->id == 200) {
		static const float src[4] = {0, 0, 63, 63};
		int bank = team_bank(b->team);
		quad2d(aos_texf("sm_rcore%02d", 4), bank, b->pos, 24.0f * b->scale, 24.0f * b->scale, src, b->angle,
		       aw(128.0f * b->alpha), AOS_BLEND_ALPHA); /* DAT_00814134 */
		quad2d(aos_texf("sm_rcore%02d", 3), bank, b->pos, 24.0f * b->scale, 24.0f * b->scale, src, b->angle,
		       aw(255.0f * b->alpha), AOS_BLEND_ADD_ALPHA);
	}
}

/* ------------------------------------------------------------------ */
/* missile family (type 7) — dwarf gun shell                           */

static int missile_spawn(aos_world *w, int32_t id, const float pos[3], float ang, int32_t owner, int32_t age0, int32_t team, int32_t p18) /* FUN_00676e50 */
{
	if (id != 100)
		return -1;
	int i = aos_missile_new(w, ID, id, pos, ang, owner, age0, team, p18);
	if (i < 0)
		return -1;
	aos_missile *m = &w->obj.missile[i];
	m->growScale = 1.0f;
	m->speed = 12.0f;
	m->hp = 0x3c;
	m->damage = 100; /* ver>0x5a0 */
	m->shapeBody.radius = 8.0f;
	m->shapeProj.radius = 8.0f;
	m->knockback = 10.0f;
	return i;
}

static bool missile_update(aos_world *w, aos_missile *m) /* FUN_00677870 */
{
	if (m->id != 100)
		return true;
	if (5.0f < m->speed)
		m->speed *= 0.975f;
	m->pos[0] = aos_cos(m->moveAngle) * m->speed + m->pos[0];
	m->pos[1] = aos_sin(m->moveAngle) * m->speed + m->pos[1];
	if (0x14 < m->age && !aos_in_arena(m->pos, 10.0f)) {
		aos_missile_cb_fn f = aos_otab.missileOnHit[m->type];
		if (f)
			f(w, m);
		return false;
	}
	return true;
}

static bool missile_boom(aos_world *w, aos_missile *m) /* FUN_00677730 / FUN_006777d0 */
{
	if (m->id == 100) {
		eff0(w, 0x262, 0, m->pos, 0, m->ownerId, 0, m->team);
		eff0(w, 0x514, 1, m->pos, 0, m->ownerId, 0, m->team);
		aos_se(w, 0x14);
	}
	return false;
}

static void missile_draw(aos_world *w, aos_missile *m) /* FUN_00676f70 */
{
	if (m->id != 100)
		return;
	float s1 = aos_sin(((float)(m->age % 0x32) / 50.0f) * TWO_PI);
	float a1 = s1 * 0.15f + 0.85f, a2 = s1 * 0.25f + 0.75f;
	float lit = (1.0f < m->growScale || 1.0f == m->growScale) ? 1.0f : 0.0f;
	int bank = bank_of_enemy_id(w, m->ownerId); /* owner = dwarf gun part id */
	for (int k = 0; k < 3; k++) {
		aos_dcmd *c = aos_draw(aos_texf("sm_rcore%02d", k), bank, m->pos[0], m->pos[1], 0);
		c->halfW = c->halfH = 24.0f;
		c->sx = c->sy = m->growScale;
		c->rotZ = aos_rad2bam(m->angle);
		c->blend = (uint8_t)(k == 0 ? AOS_BLEND_ALPHA : AOS_BLEND_ADD_ALPHA);
		c->rgba = k == 0 ? aos_rgba(lit, lit, lit, 1) : aos_rgba(1, 1, 1, k == 1 ? a1 : a2);
	}
}

/* ------------------------------------------------------------------ */
/* hitbox family (type 7)                                              */

static int hitbox_spawn(aos_world *w, int32_t id, const float pos[3], float ang, int32_t owner, int32_t zero, int32_t team) /* FUN_005e92d0 */
{
	int32_t dmg;
	float r, kb;
	switch (id) {
	case 300: dmg = 0xfa; r = 60.0f; kb = 10.0f; break;
	case 100: dmg = 0x1e; r = 30.0f; kb = 10.0f; break;
	case 0x96: dmg = 200; r = 60.0f; kb = 10.0f; break; /* ver>0x5a0 */
	case 200: dmg = 0x3c; r = 60.0f; kb = 10.0f; break;
	case 0xd2: dmg = 0x28; r = 60.0f; kb = 5.0f; break;
	case 400: dmg = 0x82; r = 80.0f; kb = 5.0f; break;
	case 0x19a: dmg = 0x104; r = 280.0f; kb = 5.0f; break;
	case 500: dmg = 100; r = 187.0f; kb = 20.0f; break; /* DAT_0081fa80 */
	default: return -1;
	}
	int i = aos_hitbox_new(w, ID, id, pos, ang, owner, zero, team);
	if (i < 0)
		return -1;
	aos_hitbox *h = &w->obj.hitbox[i];
	h->hitType = 3;
	h->damage = dmg;
	h->shape = (aos_shape){0, 0, r};
	h->knockback = kb;
	return i;
}

static void hitbox_onhit(aos_world *w, aos_hitbox *h, aos_player *v) /* FUN_005e96e0 */
{
	switch (h->id) {
	case 300:
		eff0(w, 0xaf0, aos_team_colour(w, h->team, 1), &v->x, 0, h->ownerId, 0, h->team);
		aos_se_cd(w, 0x1d, 4);
		break;
	case 0x96: {
		int32_t col = aos_team_colour(w, h->team, 0);
		for (int k = -2; k < 3; k++) {
			float a = ((float)k / 5.0f) * TWO_PI;
			a += aos_rand_real(w, -0.25f, 0.25f);
			eff0(w, 0x456, col, &v->x, a, h->ownerId, 0, h->team);
		}
		aos_se_cd(w, 0x13, 4);
		break;
	}
	case 200:
	case 0xd2: {
		float r0 = aos_rand_real(w, -PI, PI);
		int32_t col = aos_team_colour(w, h->team, 0);
		for (int k = -3; k < 3; k++) {
			float a = ((float)k / 3.0f) * PI;
			a = a + aos_rand_real(w, -0.25f, 0.25f) + r0;
			eff0(w, 0x456, col, &v->x, a, h->ownerId, 0, h->team);
		}
		aos_spawn_spark(w, 1000, col, 5, 6, h->angle, &v->x, h->ownerId);
		aos_se_cd(w, 0x18, 4);
		break;
	}
	case 400:
	case 0x19a: {
		h->angle = aos_atan2(v->y - h->pos[1], v->x - h->pos[0]);
		float lo = h->id == 400 ? -30.0f : -50.0f, hi = h->id == 400 ? 30.0f : 50.0f; /* DAT_00817c08/008140e8, DAT_008149e4/008140ec */
		if (h->id == 0x19a) {
			for (int k = 0; k < 3; k++) {
				float pt[3] = {v->x, v->y, v->z};
				pt[0] += aos_rand_real(w, lo, hi);
				pt[1] += aos_rand_real(w, lo, hi);
				eff0(w, 0x834, 0, pt, 0, h->ownerId, 0, h->team);
			}
		}
		int n = h->id == 400 ? 3 : 4;
		for (int k = 0; k < n; k++) {
			float pt[3] = {v->x, v->y, v->z};
			pt[0] += aos_rand_real(w, lo, hi);
			pt[1] += aos_rand_real(w, lo, hi);
			eff0(w, 500, 0, pt, 0, h->ownerId, k * -3, h->team);
		}
		aos_se_cd(w, 0x1d, h->id == 400 ? 4 : 5);
		break;
	}
	case 500:
		h->angle = aos_atan2(v->y - h->pos[1], v->x - h->pos[0]);
		eff0(w, 0xaf0, aos_team_colour(w, h->team, 0), &v->x, 0, h->ownerId, 0, h->team);
		aos_se_cd(w, 0x19, 3);
		aos_screen_blur(w, 10, 0.75f, 1); /* DAT_00813f3c */
		break;
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* laser family (type 7) — Resistant Sphere orbit rings (no collision) */

static int laser_spawn(aos_world *w, int32_t id, const float pos[3], float ang, int32_t owner, int32_t team, int32_t p18) /* FUN_00625190 */
{
	if (id != 100)
		return -1;
	int i = aos_laser_new(w, ID, id, pos, ang, owner, team, p18);
	if (i < 0)
		return -1;
	aos_laser *l = &w->obj.laser[i];
	l->damage = 0;
	l->f48 = 0;
	l->hitType = 0;
	l->blockMode = 0;
	l->knockback = 0;
	l->stun = 0;
	return i;
}

static bool laser_update(aos_world *w, aos_laser *l) /* FUN_00625660 */
{
	if (l->id != 100)
		return true;
	l->nseg = 0;
	aos_enemy *s = aos_enemy_by_id(w, l->ownerId);
	if (l->age < 1000 && !s)
		l->age = 1000;
	else if (l->age < 1000 && s->f84 == 900)
		l->age = 1000;
	if (0x419 < l->age)
		return false;
	if (s) {
		l->origin[0] = s->pos[0];
		l->origin[1] = s->pos[1];
		l->origin[2] = s->pos[2];
	}
	float sc = 1.0f;
	if (l->age < 10)
		sc = (float)l->age / 10.0f;
	if (1000 < l->age)
		sc = (float)(0x41a - l->age) / 50.0f;
	aos_shape shp = {0, sc, 0};
	for (int k = -6; k < 6; k++) {
		float th = ((float)k / 6.0f) * PI + (((float)(w->frame % 10) / 10.0f) * PI) / 6.0f;
		float v[2] = {aos_cos(th) * sc * 176.0f, aos_sin(th) * sc * 44.0f}; /* DAT_00817ab8, DAT_0081c69c */
		aos_vec_rotate(v, l->angle);
		float pt[3] = {v[0] + l->origin[0], v[1] + l->origin[1], 0};
		aos_laser_push_seg(l, &shp, pt);
	}
	return true;
}

static void laser_onhit(aos_world *w, aos_laser *l, int32_t seg, aos_player *p) /* FUN_00625640: empty */
{
	(void)w, (void)l, (void)seg, (void)p;
}

static void laser_draw(aos_world *w, aos_laser *l) /* FUN_00625290 */
{
	int32_t col = aos_team_colour(w, l->team, 0);
	for (int k = 0; k < l->nseg; k++) {
		float s = l->seg[k].shape.halfH;
		bullet(col, l->seg[k].pos, 24.0f * s, 64.0f * s, 10.0f * s, 144.0f * s, 0xff80ff); /* DAT_008140f8, DAT_00814138 */
	}
}

/* ------------------------------------------------------------------ */
/* effect family (type 7) — gameplay-relevant (Beam Ribbon, Arachne)   */

static int effect_spawn(aos_world *w, int32_t id, int32_t var, const float pos[3], float ang, int32_t owner, int32_t age0, int32_t team) /* FUN_00504b80 */
{
	if (id != 500 && id != 300 && id != 100 && id != 200 && id != 400 && id != 0x226 && id != 600 && id != 0x262)
		return -1;
	int i = aos_effect_new(w, ID, id, var, pos, ang, owner, age0, team);
	if (i < 0)
		return -1;
	aos_effect *e = &w->obj.effect[i];
	e->moveDir = ang;
	e->rotation = ang;
	e->aux = 0;
	switch (id) {
	case 500:
		e->alpha = 0.5f;
		e->scale = 0;
		e->rotation = aos_rand_real(w, -0.25f, 0.25f);
		e->aux = aos_rand_real(w, 10.0f, 15.0f);
		break;
	case 300:
	case 200:
	case 600:
		e->alpha = 0;
		e->scale = 2.0f;
		break;
	case 100:
		e->alpha = 0;
		e->scale = 0;
		break;
	case 400:
		e->alpha = 0;
		e->scale = 1.0f;
		break;
	case 0x226:
		e->alpha = 1.0f;
		e->scale = 1.0f;
		break;
	case 0x262:
		e->alpha = 1.0f;
		e->scale = 0;
		e->drawLayer = 0;
		e->aux = 0.175f; /* DAT_00817888 */
		break;
	}
	return i;
}

static bool effect_update(aos_world *w, aos_effect *e) /* FUN_00506640 */
{
	switch (e->id) {
	case 500:
		if (e->age < 0x55) {
			e->alpha += 0.005f; /* DAT_00813eac */
			e->scale += 0.01f;
			e->aux *= 0.95f;
			e->rotation *= 0.95f;
			e->moveDir += e->rotation;
			e->pos[0] = aos_cos(e->moveDir) * e->aux + e->pos[0];
			e->pos[1] = aos_sin(e->moveDir) * e->aux + e->pos[1];
			if (e->age == 0x54) {
				aos_player *o = opp_of_team(w, e->team);
				if (o)
					e->rotation = aos_atan2(o->y - e->pos[1], o->x - e->pos[0]);
				for (int k = -3; k < 3; k++) {
					float a = ((float)k / 3.0f) * PI + e->rotation;
					aos_spawn_beam(w, ID, k % 2 == 0 ? 300 : 0x15e, e->pos, a, e->ownerId, 0, e->team);
				}
				eff0(w, 0x515, e->variant, e->pos, 0, e->ownerId, 0, e->team);
				aos_se_cd(w, 0xf, 3);
			}
			return true;
		}
		e->scale -= 0.05f;
		return !(e->scale <= 0.0f);
	case 100: {
		if (e->scale <= 1.0f && 1.0f != e->scale)
			e->scale += 0.05f;
		if (1.0f < e->scale)
			e->scale = 1.0f;
		if (e->alpha <= 1.0f && 1.0f != e->alpha)
			e->alpha += 0.05f;
		if (1.0f < e->alpha)
			e->alpha = 1.0f;
		e->rotation = aos_rand_real(w, -PI, PI);
		int i = enemy_index_by_id(w, e->ownerId);
		if (i < 0)
			return false;
		e->pos[0] = w->obj.enemy[i].pos[0];
		e->pos[1] = w->obj.enemy[i].pos[1];
		e->pos[2] = w->obj.enemy[i].pos[2];
		return w->obj.enemy[i].f84 == 10;
	}
	case 300:
	case 200:
	case 600:
		if (e->age < 100) {
			e->alpha += 0.1f;
			e->scale -= 0.1f;
			if (e->scale <= 1.0f) {
				e->alpha = 1.0f;
				e->scale = 1.0f;
				e->age = 100;
				e->drawLayer = 0;
				if (e->id == 600)
					aos_spawn_hitbox(w, ID, 400, e->pos, e->rotation, e->ownerId, 0, e->team);
			}
		} else if (0x77 < e->age) {
			e->alpha -= 0.05f;
			e->scale -= 0.05f;
			if (e->scale <= 0.0f)
				return false;
		}
		return true;
	case 400:
		if (!aos_enemy_by_id(w, e->ownerId))
			return false;
		e->alpha += 0.05f;
		if (1.0f < e->alpha)
			e->alpha = 1.0f;
		return true;
	case 0x226:
		e->alpha -= 0.05f;
		return !(e->alpha <= 0.0f);
	case 0x262:
		if (e->age < 0x32) {
			e->scale += e->aux;
			e->aux *= 0.8f; /* DAT_008145e8 */
		} else {
			e->alpha -= 0.01f;
			if (e->alpha <= 0.0f)
				return false;
		}
		if (e->age < 0x15 && e->age % 5 == 0)
			aos_spawn_hitbox(w, ID, 0x19a, e->pos, e->rotation, e->ownerId, 0, e->team);
		if (e->age < 0xb) {
			for (int k = 0; k < 2; k++) {
				float a = aos_rand_real(w, -PI, PI);
				float d = aos_rand_real(w, 0, 100.0f);
				float pt[3] = {aos_cos(a) * d + e->pos[0], aos_sin(a) * d + e->pos[1], 0};
				aos_spawn_particle(w, 1000, e->variant, pt, a, e->ownerId, 0, 1);
			}
		} else if (e->age < 0x32) {
			float a = aos_rand_real(w, -PI, PI);
			eff0(w, e->age % 2 == 0 ? 200 : 0xd2, 0, e->pos, a, e->ownerId, 0, e->team);
			return true;
		}
		return true;
	default:
		return true;
	}
}

static void effect_draw(aos_world *w, aos_effect *e) /* FUN_00504f50 */
{
	static const float full[4] = {0, 0, 255, 255};
	float r[4];
	switch (e->id) {
	case 500: /* Beam Ribbon orb: bullet atlas, colour = variant (+8) */
		bullet(e->variant, e->pos, 90.0f * e->scale, 128.0f * e->alpha, 30.0f * e->scale, 255.0f * e->alpha,
		       0xffffff); /* DAT_00814170, DAT_00814134 / DAT_008140e8, DAT_00853b94 */
		break;
	case 0x226: { /* ribbon trail: stretched cells */
		int tex = aos_tex("beamsphere");
		uint32_t a = (uint32_t)(int64_t)(64.0f * e->alpha); /* DAT_008140f8 */
		cell_glow(e->variant, r);
		quad2d(tex, AOS_BANK_COMMON, e->pos, 80.0f * e->scale, 12.0f * e->scale, r, e->rotation,
		       a << 24 | 0xff0000 | (a & 0x7f) << 9 | 0xff, AOS_BLEND_ALPHA); /* DAT_00814164, DAT_00813f54 */
		cell_core(e->variant, r);
		quad2d(tex, AOS_BANK_COMMON, e->pos, 30.0f * e->scale, 6.0f * e->scale, r, e->rotation,
		       aw(255.0f * e->alpha), AOS_BLEND_ADD_ALPHA); /* DAT_008140e8, DAT_00813f44 */
		break;
	}
	case 100: /* sprite glow: sm_impact 0/1, additive */
		for (int k = 0; k < 2; k++)
			quad2d(aos_texf("sm_impact%d", k), team_bank(e->team), e->pos, 48.0f * e->scale, 48.0f * e->scale, full,
			       e->rotation, aw(255.0f * e->alpha), AOS_BLEND_ADD_ALPHA); /* DAT_008140e4 */
		break;
	case 200:
	case 300:
	case 600: { /* explosion atlas DAT_0087fccc[c/8] (explosion0/1); cell by colour c = variant */
		int32_t c = e->variant;
		int tex = aos_tex(c / 8 ? "explosion1" : "explosion0");
		int col = (c % 8) / 4, row = (c % 8) & 3;
		r[0] = (float)(col << 8);
		r[1] = (float)(row << 7);
		r[2] = r[0] + 127.0f;
		r[3] = r[1] + 127.0f;
		quad2d(tex, AOS_BANK_COMMON, e->pos, 180.0f * e->scale, 180.0f * e->scale, r, -e->rotation,
		       aw(128.0f * e->alpha), AOS_BLEND_ADD_ALPHA); /* DAT_00815258 */
		float h = 200.0f * e->scale; /* DAT_008147cc */
		if (e->age % 2 == 0)
			h *= 0.85f; /* DAT_00814120 */
		r[0] = (float)(col * 0x100 + 0x80);
		r[2] = (float)(col * 0x100 + 0xff);
		quad2d(tex, AOS_BANK_COMMON, e->pos, h, h, r, -e->rotation, aw(255.0f * e->alpha), AOS_BLEND_ADD_ALPHA);
		break;
	}
	case 400: { /* Delta triangle: sm_wisp_fld mapped through the 3 corners, 3 passes */
		aos_enemy *c0 = aos_enemy_by_id(w, e->ownerId);
		if (!c0)
			break;
		aos_enemy *k[3];
		int n = 0;
		for (int j = 0; j < 3; j++)
			n += (k[j] = aos_enemy_by_id(w, c0->childSlots[j])) != NULL;
		if (n != 3)
			break;
		float pulse = aos_sin(((float)(w->frame % 100) / 100.0f) * TWO_PI) * 0.25f + 0.75f;
		float a = (float)(uint32_t)(int64_t)(pulse * 164.0f * e->alpha) / 255.0f; /* DAT_00813f24, DAT_00813f3c, DAT_008147c8 */
		int tex = aos_tex("sm_wisp_fld");
		for (int j = 0; j < 3; j++) {
			/* vtable +0x70 maps the 256x256 texture onto (c[j], c[j+1], c[j+2], c[j]): the bilinear
			 * map degenerates to u = fraction from c[j] to the opposite edge, v = position along it.
			 * Reproduced exactly up to discretisation as NB bands of constant u, each a quad
			 * sampling the texture column [u0,u1] (draw.h has no 4-corner primitive). */
			const float *v0 = k[j]->pos, *p1 = k[(j + 1) % 3]->pos, *p2 = k[(j + 2) % 3]->pos;
			float ex = p2[0] - p1[0], ey = p2[1] - p1[1], L = aos_sqrt(ex * ex + ey * ey);
			if (L <= 0.0f)
				continue;
			float mx = (p1[0] + p2[0]) * 0.5f - v0[0], my = (p1[1] + p2[1]) * 0.5f - v0[1];
			float nx = -ey / L, ny = ex / L; /* edge normal, oriented away from c[j] */
			float hgt = mx * nx + my * ny;
			if (hgt < 0.0f) {
				nx = -nx, ny = -ny;
				hgt = -hgt;
			}
			enum { NB = 16 };
			for (int b = 0; b < NB; b++) {
				float u = ((float)b + 0.5f) / (float)NB;
				aos_dcmd *d = aos_draw(tex, team_bank(e->team), v0[0] + mx * u, v0[1] + my * u, 0);
				d->halfW = hgt / (float)NB * 0.5f;
				d->halfH = L * u * 0.5f;
				d->hasSrc = 1;
				d->src[0] = (float)b * 256.0f / (float)NB;
				d->src[2] = (float)(b + 1) * 256.0f / (float)NB;
				d->src[1] = 0;
				d->src[3] = 255.0f;
				d->rotZ = aos_rad2bam(aos_atan2(ny, nx));
				d->blend = AOS_BLEND_ADD_ALPHA;
				d->rgba = aos_rgba(1, 1, 1, a);
			}
		}
		break;
	}
	case 0x262: { /* Arachne exit blast: sm_exp 0/1 */
		int bank = team_bank(e->team);
		quad2d(aos_texf("sm_exp%02d", 0), bank, e->pos, 720.0f * e->scale, 720.0f * e->scale, full, -e->rotation,
		       aw(128.0f * e->alpha), AOS_BLEND_ADD_ALPHA); /* DAT_008178c4 */
		float h = 960.0f * e->scale, av; /* DAT_008178c8 */
		if (e->age % 2 == 0) {
			h *= 0.85f;
			av = 225.0f * e->alpha; /* DAT_00817890 */
		} else {
			av = 192.0f * e->alpha; /* DAT_0081415c */
		}
		uint32_t a = (uint32_t)(int64_t)av, b = (0xffu - (uint32_t)((int)a / 2)) & 0xff;
		uint32_t cv = bank == AOS_BANK_P0 ? (a << 24 | (a & 0xff) << 16 | 0xff00 | b)
		                                  : (a << 24 | (a & 0xff) << 16 | b << 8 | 0xff);
		quad2d(aos_texf("sm_exp%02d", 1), bank, e->pos, h, h, full, -e->rotation, cv, AOS_BLEND_ADD_ALPHA);
		break;
	}
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* whip family (type 7) — sprite ribbons (visual only)                 */

static int whip_spawn(aos_world *w, int32_t id, int32_t p2, int32_t enemyId) /* FUN_007c71a0 */
{
	if (id != 2 && id != 3)
		return -1;
	int i = aos_whip_new(w, ID, id, p2, 0); /* visual only: a full pool does not affect the sim */
	if (i >= 0)
		w->obj.whip[i].f10 = enemyId; /* +0x10 = param_3 */
	return i;
}

static int whip_spawn_tab(aos_world *w, int32_t id, int32_t ownerId, int32_t p3)
{
	return whip_spawn(w, id, ownerId, p3);
}

static bool whip_update(aos_world *w, aos_whip *h) /* FUN_007c7ce0 (type 2 only; 3 is never spawned) */
{
	if (h->id != 2)
		return true;
	aos_enemy *e = aos_enemy_by_id(w, h->f10);
	if (!e)
		return false;
	float a = e->angle + HALF_PI;
	float L[2] = {-aos_cos(a) * 5.0f + e->pos[0], -aos_sin(a) * 5.0f + e->pos[1]};
	float R[2] = {aos_cos(a) * 5.0f + e->pos[0], aos_sin(a) * 5.0f + e->pos[1]};
	int k;
	for (k = AOS_WHIP_NODES - 1; k >= 0; k--)
		if (h->node[k][6] == 0.0f)
			break;
	if (k >= 0) {
		h->node[k][3] = L[0];
		h->node[k][4] = L[1];
		h->node[k][0] = R[0];
		h->node[k][1] = R[1];
		h->node[k][6] = 1.0f;
	} else {
		for (k = AOS_WHIP_NODES - 1; k > 0; k--)
			memcpy(h->node[k], h->node[k - 1], sizeof h->node[k]);
		h->node[0][3] = L[0];
		h->node[0][4] = L[1];
		h->node[0][0] = R[0];
		h->node[0][1] = R[1];
	}
	return true;
}

static void whip_draw(aos_world *w, aos_whip *h) /* FUN_007c72c0 type 2: textured strip sm_whip0, 0x80ffffff */
{
	int bank = bank_of_enemy_id(w, h->f10);
	int tex = aos_tex("sm_whip0"); /* whip texture slot 2 (DAT_0087fe50) */
	for (int k = 0; k < AOS_WHIP_NODES - 1; k++) {
		if (h->node[k][6] == 0.0f || h->node[k + 1][6] == 0.0f)
			continue;
		/* quad (L_k, L_k+1, R_k+1, R_k), u: L 0 / R 256, v: k*12.8 (_DAT_00828324); drawn as a
		 * rectangle between the edge midpoints (exact when consecutive edges are parallel). */
		float m0[2] = {(h->node[k][0] + h->node[k][3]) * 0.5f, (h->node[k][1] + h->node[k][4]) * 0.5f};
		float m1[2] = {(h->node[k + 1][0] + h->node[k + 1][3]) * 0.5f, (h->node[k + 1][1] + h->node[k + 1][4]) * 0.5f};
		float dx = m1[0] - m0[0], dy = m1[1] - m0[1];
		float wx = h->node[k][0] - h->node[k][3], wy = h->node[k][1] - h->node[k][4];
		if (dx * dx + dy * dy <= 0.0f || wx * wx + wy * wy <= 0.0f)
			continue; /* degenerate quad: nothing is drawn (halfW/H 0 would mean "texture size") */
		aos_dcmd *c = aos_draw(tex, bank, (m0[0] + m1[0]) * 0.5f, (m0[1] + m1[1]) * 0.5f, 0);
		c->halfW = aos_sqrt(wx * wx + wy * wy) * 0.5f;
		c->halfH = aos_sqrt(dx * dx + dy * dy) * 0.5f;
		c->hasSrc = 1;
		c->src[0] = 0;
		c->src[2] = 256.0f;
		c->src[1] = (float)k * 12.8f;
		c->src[3] = (float)(k + 1) * 12.8f;
		c->rotZ = aos_rad2bam(aos_atan2(dy, dx) - HALF_PI);
		c->blend = AOS_BLEND_ALPHA;
		c->rgba = argb(0x80ffffffu);
	}
}

/* ------------------------------------------------------------------ */
/* player draw (T1)                                                    */

static aos_dcmd *spr(aos_world *w, aos_player *p, const char *fmt, int frame, float size)
{
	aos_dcmd *c = aos_draw(aos_texf(fmt, frame), aos_pidx(w, p), p->x, p->y, 0);
	c->halfW = c->halfH = size * 0.5f;
	return c;
}

static aos_dcmd *spr_aim(aos_world *w, aos_player *p, const char *fmt, int frame, float size)
{
	aos_dcmd *c = spr(w, p, fmt, frame, size);
	c->rotZ = aos_rad2bam(p->faceAngle);
	c->flipX = 1;
	if (HALF_PI <= aos_fabs(p->faceAngle))
		c->sy = -1.0f;
	return c;
}

static int pp(int32_t t, int n)
{
	int f = (t / n) & 3;
	return f == 3 ? 1 : f;
}

static int loop3(int32_t t) /* FUN_00730620 frame formula */
{
	int f = t / 3;
	if (1 < f)
		f = ((f - 2) & 3) + 2;
	if (f == 5)
		f = 3;
	return f;
}

static void dr_neutral(aos_world *w, aos_player *p) /* 0xa7 FUN_0072fca0 */
{
	const char *s = "sm_std%02d";
	if (p->up > 1) s = "sm_fwd%02d";
	if (p->down > 1) s = "sm_bck%02d";
	if (p->facingRight == 0 && p->left > 1) s = "sm_fwd%02d";
	if (p->facingRight == 1 && p->left > 1) s = "sm_bck%02d";
	if (p->facingRight == 0 && p->right > 1) s = "sm_bck%02d";
	if (p->facingRight == 1 && p->right > 1) s = "sm_fwd%02d";
	aos_dcmd *c = spr(w, p, s, pp(p->timer, 5), 96.0f);
	c->flipX = p->facingRight != 0;
	int32_t rot[3] = {0, p->facingRight ? aos_deg2bam(0xb4) : 0, 0};
	aos_afterimage(w, c, 1, p, rot); /* FUN_004d7e00 */
}

static void dr_dash(aos_world *w, aos_player *p) /* 0xa8 FUN_0072ff50 */
{
	int f = pp(p->timer, 3);
	float a = aos_fabs(p->faceAngle);
	if ((p->facingRight == 1 && a > HALF_PI) || (p->facingRight == 0 && a < HALF_PI))
		f += 3;
	aos_dcmd *c = spr_aim(w, p, "sm_dsh%02d", f, 96.0f);
	int32_t rot[3] = {aos_fabs(p->faceAngle) >= HALF_PI ? aos_deg2bam(0xb4) : 0, aos_deg2bam(0xb4),
	                  aos_rad2bam(p->faceAngle)};
	aos_afterimage(w, c, 1, p, rot); /* FUN_004d7e00 */
}

static void dr_ko(aos_world *w, aos_player *p) /* 0xa9 FUN_00730900 */
{
	int f = pp(p->timer, 3);
	if (w->frame % 2 == 0)
		f += 3;
	if (p->timer < 100)
		spr_aim(w, p, "sm_los%02d", f, 96.0f);
}

static void dr_hit(aos_world *w, aos_player *p) { spr_aim(w, p, "sm_los%02d", pp(p->timer, 3), 96.0f); }  /* 0xaa FUN_00730350 */
static void dr_sld(aos_world *w, aos_player *p) { spr_aim(w, p, "sm_sld%02d", loop3(p->timer), 96.0f); } /* 0xab, 0xbb..0xbd */
static void dr_atkb(aos_world *w, aos_player *p) { spr_aim(w, p, "sm_atkb%02d", loop3(p->timer), 96.0f); } /* 0xb1..0xb5, 0xb9 */
static void dr_atka(aos_world *w, aos_player *p) /* 0xb6..0xb8, 0xba, 0xbe */
{
	int f = p->timer / 3;
	spr_aim(w, p, "sm_atka%02d", f > 3 ? 3 : f, 96.0f);
}

/* ------------------------------------------------------------------ */

void aos_register_sham(void)
{
	aos_enemy_motion = aos_motion_step;

	aos_tab.inputA[ID] = input_a;
	aos_tab.inputB[ID] = input_b;
	/* CPU AI FUN_0072f5a0 / FUN_0072ee20 not ported */

	aos_tab.T2[0xa7] = st_neutral;
	aos_tab.T2[0xa8] = st_stub;
	aos_tab.T2[0xa9] = st_ko_hook;
	aos_tab.T2[0xaa] = st_stub;
	aos_tab.T2[0xab] = st_stub;
	aos_tab.T2[0xb1] = st_sprite_call;
	aos_tab.T2[0xb2] = st_sprite_release;
	aos_tab.T2[0xb3] = st_sprite_call_6e;
	aos_tab.T2[0xb4] = st_poly_sprite;
	aos_tab.T2[0xb5] = st_delta_sprite;
	aos_tab.T2[0xb6] = st_dwarf_call;
	aos_tab.T2[0xb7] = st_dwarf_tackle;
	aos_tab.T2[0xb8] = st_dwarf_rush;
	aos_tab.T2[0xb9] = st_beam_ribbon;
	aos_tab.T2[0xba] = st_sphere;
	aos_tab.T2[0xbb] = st_cube_tap;
	aos_tab.T2[0xbc] = st_cube_hold;
	aos_tab.T2[0xbd] = st_bounding_cube;
	aos_tab.T2[0xbe] = st_arachne;

	aos_tab.T1[0xa7] = dr_neutral;
	aos_tab.T1[0xa8] = dr_dash;
	aos_tab.T1[0xa9] = dr_ko;
	aos_tab.T1[0xaa] = dr_hit;
	aos_tab.T1[0xab] = dr_sld;
	for (int s = 0xb1; s <= 0xb5; s++)
		aos_tab.T1[s] = dr_atkb;
	aos_tab.T1[0xb9] = dr_atkb;
	aos_tab.T1[0xb6] = dr_atka;
	aos_tab.T1[0xb7] = dr_atka;
	aos_tab.T1[0xb8] = dr_atka;
	aos_tab.T1[0xba] = dr_atka;
	aos_tab.T1[0xbe] = dr_atka;
	aos_tab.T1[0xbb] = dr_sld;
	aos_tab.T1[0xbc] = dr_sld;
	aos_tab.T1[0xbd] = dr_sld;

	aos_otab.enemySpawn[ID] = enemy_spawn;
	aos_otab.enemyUpdate[ID] = enemy_update;
	aos_otab.enemyDraw[ID] = enemy_draw;
	aos_otab.enemyOnKilled[ID] = enemy_killed;
	aos_otab.beamSpawn[ID] = beam_spawn;
	aos_otab.beamUpdate[ID] = beam_update;
	aos_otab.beamDraw[ID] = beam_draw;
	aos_otab.beamOnHit[ID] = beam_onhit;
	aos_otab.missileSpawn[ID] = missile_spawn;
	aos_otab.missileUpdate[ID] = missile_update;
	aos_otab.missileDraw[ID] = missile_draw;
	aos_otab.missileOnHit[ID] = missile_boom;
	aos_otab.missileOnKilled[ID] = missile_boom;
	aos_otab.missileOnAbsorb[ID] = NULL;
	aos_otab.hitboxSpawn[ID] = hitbox_spawn;
	aos_otab.hitboxOnHit[ID] = hitbox_onhit;
	aos_otab.laserSpawn[ID] = laser_spawn;
	aos_otab.laserUpdate[ID] = laser_update;
	aos_otab.laserDraw[ID] = laser_draw;
	aos_otab.laserOnHit[ID] = laser_onhit;
	aos_otab.effectSpawn[ID] = effect_spawn;
	aos_otab.effectUpdate[ID] = effect_update;
	aos_otab.effectDraw[ID] = effect_draw;
	aos_otab.whipSpawn[ID] = whip_spawn_tab;
	aos_otab.whipUpdate[ID] = whip_update;
	aos_otab.whipDraw[ID] = whip_draw;
}
