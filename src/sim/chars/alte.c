/* Alte (engine id 4, states 0x32..0x4e, prefix at_). 1:1 port of the
 * decompiled functions named on each routine; spec docs/re/alte.md.
 * Only the version 0x5a2 branches are ported.
 *
 * Particles/sparks, screen effects, camera focus and afterimages use fx.h.
 * Not available in the engine yet:
 *  - gradient triangles (Alte effects 100 / 0x96 light cones): not drawable
 *    with aos_draw, omitted. */
#include <stdio.h>
#include <string.h>

#include "../draw.h"
#include "../fx.h"
#include "../objects.h"
#include "../sim.h"

#define ID AOS_CH_ALTE

void aos_se_cd(aos_world *w, int id, int cd);

#define HALF_PI 1.5707964f  /* DAT_00814104 */
#define PI_6 0.5235988f     /* DAT_00817ddc */
#define TWO_PI_3 2.0943952f /* DAT_00817898 */

/* ------------------------------------------------------------------ */
/* helpers                                                             */

static aos_player *player_of_team(aos_world *w, int32_t team) /* FUN_00780450 */
{
	return team == 1 ? &w->p[0] : team == 2 ? &w->p[1] : NULL;
}

static aos_player *opp_of_team(aos_world *w, int32_t team) /* FUN_00780480 */
{
	return team == 1 ? &w->p[1] : team == 2 ? &w->p[0] : NULL;
}

/* FUN_006a0e50: index of the team's Searchlight Panel (Enemy type 4, id 100) or -1. */
static int find_panel(aos_world *w, int32_t team)
{
	for (int i = 0; i < w->obj.nenemy; i++) {
		aos_enemy *e = &w->obj.enemy[i];
		if (e->team == team && e->type == 4 && e->f10 == 100)
			return i;
	}
	return -1;
}

/* Per-team colour slots 0x8859f4 + team*0x10 + 4k (FUN_005f5780). */
static int32_t team_col(aos_world *w, int32_t team, int k) { return aos_team_colour(w, team, k); }

static int fx0(aos_world *w, int32_t id, int32_t var, const float pos[3], float ang, int32_t owner,
               int32_t age0, int32_t team)
{
	return aos_spawn_effect(w, 0, id, var, pos, ang, owner, age0, team);
}

static int fx4(aos_world *w, int32_t id, int32_t var, const float pos[3], float ang, int32_t owner,
               int32_t age0, int32_t team)
{
	return aos_spawn_effect(w, ID, id, var, pos, ang, owner, age0, team);
}

static int hbox(aos_world *w, int32_t id, const float pos[3], float ang, int32_t owner, int32_t team)
{
	return aos_spawn_hitbox(w, ID, id, pos, ang, owner, 0, team);
}

static int msl(aos_world *w, aos_player *p, int32_t id, const float pos[3], float ang)
{
	return aos_spawn_missile(w, ID, id, pos, ang, p->objId, 0, p->side, p->color);
}

static void at_dir(float out[3], const aos_player *p, float ang, float d)
{
	out[0] = aos_cos(ang) * d + p->x;
	out[1] = aos_sin(ang) * d + p->y;
	out[2] = p->z;
}

static void dash_sparks(aos_world *w, aos_player *p) /* 0xa28+n pattern */
{
	float r = aos_rand_real(w, -0.3f, 0.3f); /* DAT_0081fa88, DAT_00813f28 */
	int n = aos_rand_int(w, 0, 100) % 8;
	fx0(w, 0xa28 + n, 0xd, &p->x, r + p->faceAngle, p->objId, 0, p->side);
}

/* FUN_00780a50: may the opponent be grabbed? */
static bool grabbable(aos_world *w, const aos_player *o)
{
	return o->barrierTime == 0 && o->state != AOS_ST_KO && o->hittable != 0 &&
	       o->statusOn[AOS_STATUS_STUNIMMUNE] != 1 && !w->roundOver;
}

/* §1.4 pin the opponent at distance d along p->faceAngle. */
static void pin(aos_world *w, aos_player *p, float d)
{
	aos_player *o = aos_opponent(w, p);
	if (!grabbable(w, o))
		return;
	o->state = AOS_ST_HITSTUN;
	o->timer = 0;
	o->x = aos_cos(p->faceAngle) * d + p->x;
	o->y = aos_sin(p->faceAngle) * d + p->y;
	o->speed = 0;
	o->faceAngle = p->faceAngle + AOS_PI;
	aos_angle_normalize(&o->faceAngle);
}

static void enter(aos_world *w, aos_player *p, int32_t s) /* normal-move entry: no hitFlag clear */
{
	p->state = s;
	p->timer = 0;
	aos_tab.T2[s](w, p);
}

static void walk(aos_player *p, float k)
{
	float s = aos_tab.stats[p->id].walk * k;
	if (p->up > 1) p->y += s;
	if (p->down > 1) p->y -= s;
	if (p->left > 1) p->x -= s;
	if (p->right > 1) p->x += s;
}

static void face_opp_x(aos_world *w, aos_player *p)
{
	aos_player *o = aos_opponent(w, p);
	p->faceAngle = (o->x < p->x || o->x == p->x) ? AOS_PI : 0.0f;
}

/* Enemy +0xd4..+0xdc hold ints in the original (shot counter, last-shot age,
 * shield lockout frame); the engine declares fd4 as float[3] -> bit copies. */
static int32_t eint(const aos_enemy *e, int k)
{
	int32_t v;
	memcpy(&v, &e->fd4[k], sizeof v);
	return v;
}
static void eset(aos_enemy *e, int k, int32_t v) { memcpy(&e->fd4[k], &v, sizeof v); }

/* ------------------------------------------------------------------ */
/* input dispatch                                                      */

static bool hyper_rows(aos_world *w, aos_player *p, bool schemeB)
{
	if (!aos_may_hyper(w, p))
		return false;
	int32_t s = -1;
	if (!schemeB) { /* FUN_006a7440 */
		if (p->S > 1 && p->B > 1 && p->H == 3 && aos_spend_hyper(p, 3.0f))
			s = 0x4d;
		else if (p->S > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f))
			s = 0x44;
		else if (p->B > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f))
			s = 0x40;
		else if (p->D > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f))
			s = 0x48;
		else if (p->H == 3 && aos_spend_hyper(p, 1.0f))
			s = find_panel(w, p->side) < 0 ? 0x49 : 0x4b;
	} else { /* FUN_006a7900 */
		if (p->H > 1 && p->Shield == 3 && aos_spend_hyper(p, 3.0f))
			s = 0x4d;
		else if (p->H > 1 && p->S == 3 && aos_spend_hyper(p, 1.0f))
			s = 0x44;
		else if (p->H > 1 && p->B == 3 && aos_spend_hyper(p, 1.0f))
			s = 0x40;
		else if (p->H > 1 && p->D == 3 && aos_spend_hyper(p, 1.0f))
			s = 0x48;
		else if (p->H > 1 && p->A == 3 && aos_spend_hyper(p, 1.0f))
			s = find_panel(w, p->side) < 0 ? 0x49 : 0x4b;
	}
	if (s < 0)
		return false;
	aos_set_state(w, p, s);
	return true;
}

static bool dispatch(aos_world *w, aos_player *p, bool schemeB)
{
	if (hyper_rows(w, p, schemeB))
		return true;
	if (p->state == 0 || p->state == 2) {
		if (p->D == 3) {
			aos_dash(w, p, 0);
		} else if (p->Shield == 3 && aos_spend_hyper(p, 0.5f) && aos_may_shield(w, p)) {
			aos_shield(w, p);
		} else {
			if (p->S > 1 && p->A == 3) { enter(w, p, 0x41); return true; }
			if (p->S > 1 && p->B == 3) { enter(w, p, 0x4c); return true; }
			if (p->B == 3) { enter(w, p, 0x3f); return true; }
			if (p->D > 1 && p->A == 3) { enter(w, p, 0x3d); return true; }
			if (p->A == 3) { enter(w, p, 0x3c); return true; }
		}
	}
	return aos_tab.T2[p->state] ? aos_tab.T2[p->state](w, p) : true;
}

static bool input_a(aos_world *w, aos_player *p) { return dispatch(w, p, false); } /* FUN_006a7440 */
static bool input_b(aos_world *w, aos_player *p) { return dispatch(w, p, true); }  /* FUN_006a7900 */

/* ------------------------------------------------------------------ */
/* states (T2)                                                         */

static bool st_neutral(aos_world *w, aos_player *p) /* 0x32 T2 FUN_006a1ab0 */
{
	walk(p, 1.0f);
	p->faceAngle = p->facingRight == 1 ? 0.0f : AOS_PI;
	if (find_panel(w, p->side) != -1 && p->S > 1)
		p->faceAngle = aos_aim(w, p);
	return true;
}

static bool st_stub(aos_world *w, aos_player *p) /* 0x33/0x35/0x36 FUN_006a1be0/1bf0/1c00 */
{
	(void)w;
	(void)p;
	return true;
}

static bool st_ko_hook(aos_world *w, aos_player *p) /* 0x34 T2 FUN_006a1c10 (from common state 6) */
{
	if (p->timer == 0) {
		for (int i = 0; i < w->obj.nenemy; i++) {
			aos_enemy *e = &w->obj.enemy[i];
			if (e->ownerId == p->objId && e->f10 == 100) {
				e->f84 = 900;
				e->timer2 = 0;
				e->active = 0;
			}
			if (e->ownerId == p->objId && e->f10 == 200) {
				e->f84 = 900;
				e->timer2 = 0;
				e->active = 0;
				e->scale[2] = 0;
			}
		}
	}
	return true;
}

static bool smg(aos_world *w, aos_player *p, int32_t cost, float mv) /* FUN_006a1d50 / 006a2570 */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer > 3 && p->timer < 16)
		walk(p, mv);
	if (p->timer == 4)
		aos_se(w, 0x2a);
	if (p->timer > 5 && p->timer < 16 && p->timer % 3 == 0 && aos_use_gauge(p, cost)) {
		p->faceAngle = aos_aim(w, p);
		float pt[3];
		at_dir(pt, p, p->faceAngle, 30.0f); /* DAT_008140e8 */
		float r = aos_rand_real(w, -0.1f, 0.1f); /* DAT_00814130, DAT_00853b10 */
		msl(w, p, 100, pt, r + p->faceAngle);
		fx4(w, 500, 0, pt, p->faceAngle, p->objId, 0, p->side);
		if (p->timer == 6)
			fx0(w, 0x547, team_col(w, p->side, 1), pt, p->faceAngle, p->objId, 0, p->side);
		aos_spawn_spark(w, 1000, team_col(w, p->side, 1), 8, 1, p->faceAngle + AOS_PI, pt, p->objId);
		aos_se_cd(w, 0x29, 6);
	}
	if (p->timer == 15 && p->A > 1)
		p->timer = 6;
	else if (p->timer < 19 || p->D != 3) {
		if (p->timer > 29)
			p->state = 0;
	} else
		aos_dash(w, p, 1);
	return true;
}

static bool st_smg(aos_world *w, aos_player *p) { return smg(w, p, 0x3c, 0.5f); }      /* 0x3c */
static bool st_smg_dash(aos_world *w, aos_player *p) { return smg(w, p, 0x3d, 1.0f); } /* 0x3d */

static void beam_panels(aos_world *w, aos_player *p, float off) /* six missile-200 panels */
{
	for (int k = -3; k < 3; k++) {
		float a = ((float)k / 3.0f) * AOS_PI + p->faceAngle + off;
		float pt[3];
		at_dir(pt, p, a, 100.0f); /* DAT_00853b90 */
		msl(w, p, 200, pt, a);
	}
}

static void missile_shower(aos_world *w, aos_player *p, float off, int panel)
{
	for (int k = -3; k < 3; k++) {
		int i = msl(w, p, 300, &p->x, ((float)k / 3.0f) * AOS_PI + p->faceAngle + off);
		if (panel != -1 && i >= 0)
			w->obj.missile[i].f74 = 0.0035f; /* DAT_00814d6c (ver >= 0x58c) */
	}
}

static bool st_beam_panels(aos_world *w, aos_player *p) /* 0x3e T2 FUN_006a2d90 */
{
	if (p->timer == 8 && aos_use_gauge(p, 0x3e)) {
		int panel = find_panel(w, p->side);
		beam_panels(w, p, panel == -1 ? 0.0f : PI_6);
		aos_se(w, 0x2f);
	}
	if (p->timer < 20 || p->D != 3) {
		if (p->timer > 40)
			p->state = 0;
	} else
		aos_dash(w, p, 1);
	return true;
}

static bool st_missile(aos_world *w, aos_player *p) /* 0x3f T2 FUN_006a33c0 */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer < 9 && p->B < 2) {
		p->state = 0x3e;
		aos_tab.T2[0x3e](w, p);
		return true;
	}
	if (p->timer == 16 && aos_use_gauge(p, 0x3f)) {
		missile_shower(w, p, 0.0f, find_panel(w, p->side));
		aos_se(w, 0x22);
	}
	if (p->timer < 25 || p->D != 3) {
		if (p->timer > 40)
			p->state = 0;
	} else
		aos_dash(w, p, 1);
	return true;
}

static bool st_salvo(aos_world *w, aos_player *p) /* 0x40 T2 FUN_006a2fa0, B+H */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		p->dashAngle = p->moveAngle = p->faceAngle;
		p->speed = 0;
		aos_barrier(w, p, 10);
		aos_hyper_flourish_ex(w, p, 6, 100, 0.75f, 100, p->faceAngle, 0.0f, 0x14, 0.5f, 2, 1); /* speedline spread 0 */
	}
	if (p->timer == 20 || p->timer == 40) {
		float off = p->timer == 40 ? PI_6 : 0.0f;
		missile_shower(w, p, off, find_panel(w, p->side));
		aos_se(w, 0x22);
	}
	if (p->timer == 10 || p->timer == 30)
		beam_panels(w, p, p->timer == 30 ? 0.0f : PI_6);
	if (p->timer < 50 || p->D != 3) {
		if (p->timer > 70)
			p->state = 0;
	} else
		aos_dash(w, p, 1);
	return true;
}

static void rod_glow(aos_world *w, aos_player *p, float dist, float evenScale, float mul, bool overwrite)
{
	float pt[3];
	at_dir(pt, p, p->faceAngle, dist);
	int i = fx4(w, 600, 0, pt, 0, p->objId, 0, p->side);
	if (i >= 0) {
		aos_effect *e = &w->obj.effect[i];
		if (p->timer % 2 == 0)
			e->scale = evenScale;
		e->scale = overwrite ? mul : e->scale * mul; /* 0x41 overwrites (literal) */
	}
}

static bool st_rod(aos_world *w, aos_player *p) /* 0x41 T2 FUN_006a4180, S+A */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		p->dashAngle = p->moveAngle = p->faceAngle;
		p->speed = 0;
	}
	if (p->timer == 12 && aos_use_gauge(p, 0x41)) {
		p->faceAngle = aos_aim(w, p);
		float v = aos_dist(w, p) / 10.0f;
		if (18.0f <= v) v = 18.0f; /* DAT_00814144 */
		if (v < 14.0f) v = 14.0f;  /* DAT_00813a54 (ver > 0x58a) */
		p->dashAngle = p->moveAngle = p->faceAngle;
		p->speed = v;
		aos_se(w, 0x1f);
	}
	if (15.0f <= p->speed) /* DAT_00853b88 */
		dash_sparks(w, p);
	if (p->timer >= 15 && p->timer <= 46 && p->hitFlag == 1) {
		p->speed *= 0.5f;
		aos_set_state(w, p, 0x42);
		fx0(w, 0xaf0, 0, &p->x, 0, p->objId, 0, p->side);
		aos_se(w, 0x1e);
		return true;
	}
	if (p->timer > 14 && p->timer < 47) {
		aos_player *o = aos_opponent(w, p);
		aos_angle_toward(&p->faceAngle, aos_atan2(o->y - p->y, o->x - p->x), p->speed * 0.0025f);
		p->dashAngle = p->moveAngle = p->faceAngle;
	}
	if (p->timer > 14 && p->timer < 47) {
		rod_glow(w, p, 60.0f, 1.1f, 0.75f, true);
		hbox(w, 300, &p->x, p->faceAngle, p->objId, p->side);
		if (p->timer % 3 == 0) {
			float pt[3];
			at_dir(pt, p, p->faceAngle, 90.0f); /* DAT_00814170 */
			float r = aos_rand_real(w, -AOS_PI, AOS_PI);
			fx4(w, 400, 0, pt, r, p->objId, 0, p->side);
		}
	}
	if (p->timer > 44 && p->D == 3) {
		aos_dash(w, p, 1);
		return true;
	}
	if (p->timer > 55)
		p->state = 0;
	p->friction *= 0.5f;
	return true;
}

static bool st_rod_hold(aos_world *w, aos_player *p) /* 0x42 T2 FUN_006a4ab0 */
{
	if (p->timer >= 0 && p->timer < 20) {
		rod_glow(w, p, 60.0f, 1.1f, 0.75f, false);
		if (p->timer % 3 == 0) {
			float pt[3];
			at_dir(pt, p, p->faceAngle, 60.0f);
			float r = aos_rand_real(w, -AOS_PI, AOS_PI);
			fx4(w, 400, 0, pt, r, p->objId, 0, p->side);
		}
		if (p->timer % 4 == 0)
			hbox(w, 0x136, &p->x, p->faceAngle, p->objId, p->side);
		pin(w, p, 60.0f); /* DAT_00813a58 */
	}
	if (p->timer == 20)
		hbox(w, 0x140, &p->x, p->faceAngle, p->objId, p->side);
	if (p->timer < 30 || p->D != 3) {
		if (p->timer > 60)
			p->state = 0;
	} else
		aos_dash(w, p, 1);
	return true;
}

/* 0x44 / 0x45 / 0x46 shared body (FUN_006a4f60 / 006a56b0 / 006a5cc0). */
static bool punisher(aos_world *w, aos_player *p, int stage)
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		if (stage == 0) {
			float v = aos_dist(w, p) / 14.0f; /* DAT_00813a54 */
			if (10.0f <= v) v = 10.0f;        /* DAT_00813f4c */
			p->dashAngle = p->moveAngle = p->faceAngle;
			p->speed = v;
			aos_barrier(w, p, 10);
			aos_hyper_flourish(w, p);
		}
	}
	if (p->timer == 12) {
		p->faceAngle = aos_aim(w, p);
		float v = aos_dist(w, p) / 10.0f;
		if (18.0f <= v) v = 18.0f;
		if (stage == 0 && v < 14.0f) v = 14.0f;
		p->dashAngle = p->moveAngle = p->faceAngle;
		p->speed = v;
		aos_se(w, 0x1f);
	}
	if (16.0f <= p->speed) /* DAT_008140d8 */
		dash_sparks(w, p);
	if (p->timer > 11 && p->timer < 41) {
		rod_glow(w, p, 80.0f, 1.2f, 1.5f, false);
		hbox(w, 0x15e, &p->x, p->faceAngle, p->objId, p->side);
		if (p->timer % 2 == 0) {
			float pt[3];
			at_dir(pt, p, p->faceAngle, 100.0f);
			float r = aos_rand_real(w, -AOS_PI, AOS_PI);
			fx4(w, 400, 0, pt, r, p->objId, 0, p->side);
		}
	}
	if (p->timer >= 12 && p->timer <= 40 && p->hitFlag == 1) {
		p->speed *= 0.5f;
		aos_set_state(w, p, 0x47);
		fx0(w, 0xaf0, 0, &p->x, 0, p->objId, 0, p->side);
		aos_se(w, 0x1e);
		return true;
	}
	if (stage < 2 && p->timer >= 25 && p->B == 3 && p->hyper > 0.5f) {
		p->hyper -= 0.5f;
		aos_set_state(w, p, stage == 0 ? 0x45 : 0x46);
		aos_se(w, 0x1e);
		fx0(w, 0xaf0, 0, &p->x, 0, p->objId, 0, p->side);
		return true;
	}
	if (p->timer < 50 || p->D != 3) {
		if (p->timer > 80)
			p->state = 0;
	} else
		aos_dash(w, p, 1);
	return true;
}

static bool st_punisher(aos_world *w, aos_player *p) { return punisher(w, p, 0); }  /* 0x44, S+H */
static bool st_punisher2(aos_world *w, aos_player *p) { return punisher(w, p, 1); } /* 0x45 */
static bool st_punisher3(aos_world *w, aos_player *p) { return punisher(w, p, 2); } /* 0x46 */

static bool st_punisher_grab(aos_world *w, aos_player *p) /* 0x47 T2 FUN_006a61d0 */
{
	if (p->timer >= 0 && p->timer < 40) {
		float pt[3];
		at_dir(pt, p, p->faceAngle, 80.0f); /* DAT_00814164 */
		int i = fx4(w, 600, 0, pt, 0, p->objId, 0, p->side);
		if (i >= 0) {
			if (p->timer % 2 == 0)
				w->obj.effect[i].scale = 1.25f; /* DAT_00815dbc */
			w->obj.effect[i].scale *= 1.5f;
		}
		if (p->timer % 2 == 0) {
			float r = aos_rand_real(w, -AOS_PI, AOS_PI);
			fx4(w, 400, 0, pt, r, p->objId, 0, p->side);
		}
		if (p->timer % 5 == 0)
			fx0(w, 0x514, 0, pt, 0, p->objId, 0, p->side);
		if (p->timer % 2 == 0) {
			for (int k = 0; k < 4; k++) {
				int n = aos_rand_int(w, 0, 100);
				float r = aos_rand_real(w, 0.0f, HALF_PI);
				fx4(w, n < 50 ? 0x262 : 0x263, team_col(w, p->side, 0), pt,
				    r + ((float)k / 4.0f) * AOS_2PI, p->objId, 0, p->side);
			}
		}
		if (p->timer % 4 == 0)
			hbox(w, 0x168, &p->x, p->faceAngle, p->objId, p->side);
		pin(w, p, 80.0f);
	}
	if (p->timer == 40)
		hbox(w, 0x172, &p->x, p->faceAngle, p->objId, p->side);
	if (p->timer < 50 || p->D != 3) {
		if (p->timer > 80)
			p->state = 0;
	} else
		aos_dash(w, p, 1);
	return true;
}

static bool st_shock_ball(aos_world *w, aos_player *p) /* 0x48 T2 FUN_006a67e0, D+H */
{
	if (p->timer == 0) {
		face_opp_x(w, p);
		p->dashAngle = p->faceAngle;
		p->speed = 0;
		aos_barrier(w, p, 10);
		aos_hyper_flourish_ex(w, p, 6, 100, 0.75f, 100, p->faceAngle, 0.0f, 0x14, 0.5f, 2, 1); /* speedline spread 0 */
		aos_se(w, 0x20);
	}
	for (int k = 0; k < 5; k++) {
		if (p->timer != k * 5 + 10)
			continue;
		for (int s = -1; s <= 1; s += 2) {
			float a = p->faceAngle + (float)s * HALF_PI + ((float)k / 5.0f) * AOS_PI;
			int i = aos_spawn_enemy(w, ID, 300, p->objId, p->side, p->color);
			if (i < 0)
				continue;
			aos_enemy *e = &w->obj.enemy[i];
			e->pos[0] = aos_cos(a) * 160.0f + p->x; /* DAT_00814168 */
			e->pos[1] = aos_sin(a) * 160.0f + p->y;
			e->angle2 = a;
			e->angle = p->faceAngle;
		}
		aos_se_cd(w, 0x10, 5);
	}
	if (p->timer < 40 && p->timer % 2 == 0) {
		float pt[3] = {p->x, p->y - 30.0f, p->z};
		float a = p->faceAngle + HALF_PI;
		float r = aos_rand_real(w, -0.15f, 0.15f); /* DAT_00815dc4, DAT_00813f20 */
		int n = aos_rand_int(w, 0, 100) % 8;
		fx0(w, 0xa32 + n, 0xd, pt, a + r, p->objId, 0, p->side);
	}
	if (p->timer < 40 || p->D != 3) {
		if (p->timer > 55)
			p->state = 0;
	} else
		aos_dash(w, p, 1);
	return true;
}

static bool st_panel(aos_world *w, aos_player *p) /* 0x49 T2 FUN_006a35e0, H (no panel) */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		aos_barrier(w, p, 10);
		aos_hyper_flourish_ex(w, p, 6, 100, 0.75f, 100, p->faceAngle, 0.0f, 0x14, 0.5f, 2, 1); /* speedline spread 0 */
	}
	if (p->timer == 10) {
		int i = aos_spawn_enemy(w, ID, 100, p->objId, p->side, p->color);
		if (i >= 0) {
			aos_enemy *e = &w->obj.enemy[i];
			e->pos[0] = p->x;
			e->pos[1] = p->y;
			e->pos[2] = p->z;
			e->angle = p->faceAngle;
			e->f70 = 100.0f; /* DAT_00853b90 */
		}
		aos_se(w, 0x2f);
	}
	if (p->timer < 30 || p->D != 3) {
		if (p->timer > 60)
			p->state = 0;
	} else
		aos_dash(w, p, 1);
	return true;
}

static bool st_spark(aos_world *w, aos_player *p) /* 0x4b T2 FUN_006a37d0, H (panel present) */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		aos_barrier(w, p, 10);
		aos_hyper_flourish(w, p);
	}
	if (p->timer == 5) {
		int i = find_panel(w, p->side);
		if (i != -1) {
			aos_enemy *e = &w->obj.enemy[i];
			e->f84 = 30;
			e->timer2 = 0;
			e->scale[0] = 1.0f;
			e->scale[1] = 1.0f;
			e->active = 1;
		}
	}
	if (p->timer < 60 || p->D != 3) {
		if (p->timer > 80)
			p->state = 0;
	} else
		aos_dash(w, p, 1);
	return true;
}

static bool st_claymore(aos_world *w, aos_player *p) /* 0x4c T2 FUN_006a39d0, S+B */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer == 10 && aos_use_gauge(p, 0x4c)) {
		for (int i = 0; i < w->obj.nenemy; i++)
			if (w->obj.enemy[i].f10 == 200 && w->obj.enemy[i].team == p->side)
				w->obj.enemy[i].timer2 = 0x3e9;
		for (int s = -1; s <= 1; s += 2) {
			float a = p->faceAngle + (float)s * 2.0f; /* DAT_00813a4c */
			int i = aos_spawn_enemy(w, ID, 200, p->objId, p->side, p->color);
			if (i < 0)
				continue;
			aos_enemy *e = &w->obj.enemy[i];
			e->pos[0] = aos_cos(a) * 80.0f + p->x; /* DAT_00814164 */
			e->pos[1] = aos_sin(a) * 80.0f + p->y;
			e->pos[2] = p->z; /* UNVERIFIED: original stores an uninitialised stack value */
			e->angle2 = p->faceAngle + (float)s * 0.5f;
			e->angle = p->faceAngle + (float)s * 0.5f;
			/* +0x74 = s*0.01 (DAT_008178dc / DAT_00813ec8): only read by ver < 0x58b; skipped */
		}
		aos_se(w, 0x2f);
	}
	if (p->timer < 20 || p->D != 3) {
		if (p->timer > 30)
			p->state = 0;
	} else
		aos_dash(w, p, 1);
	return true;
}

static bool st_desp(aos_world *w, aos_player *p) /* 0x4d T2 FUN_006a6dc0, S+B+H */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		aos_barrier(w, p, 0x14);
		aos_se(w, 7);
		aos_screen_dark(w, 0x32, 0.75f);       /* DAT_00813f3c */
		aos_screen_blur(w, 0x32, 0.75f, 2);
	}
	if (p->timer == 15) {
		p->faceAngle = aos_aim(w, p);
		p->speed = 50.0f; /* DAT_008140ec */
		p->moveAngle = p->faceAngle;
		aos_se(w, 0x20);
	}
	if (p->timer >= 15 && p->timer <= 40 && aos_dist(w, p) < 70.0f) { /* DAT_008145ec */
		aos_player *o = aos_opponent(w, p);
		if (grabbable(w, o)) {
			o->state = AOS_ST_HITSTUN;
			o->timer = 0;
			o->x = aos_cos(p->faceAngle) * 40.0f + p->x; /* DAT_008140e0 */
			o->y = aos_sin(p->faceAngle) * 40.0f + p->y;
			o->speed = 0;
			o->faceAngle = p->faceAngle + AOS_PI;
			aos_angle_normalize(&o->faceAngle);
			p->state = 0x4e;
			p->timer = 0;
			p->hitFlag = 0;
			p->speed *= 0.25f;
			face_opp_x(w, p);
			p->dashAngle = p->faceAngle;
			aos_tab.T2[0x4e](w, p);
			aos_camera_focus(w, 0x78, p->objId, &p->x, 1.25f); /* DAT_00815dbc */
			aos_screen_dark(w, 0x78, 0.9f);                     /* DAT_00814178 */
			aos_screen_blur(w, 0x78, 0.6f, 2);                  /* DAT_00813f34 */
			aos_speedline(w, 100, &p->x, 0.0f, 0.0f);
			fx0(w, 0xfa, 0, &p->x, 0, p->objId, 0, p->side);
			return true;
		}
	}
	if (p->timer < 60 || p->D != 3) {
		if (p->timer > 80)
			p->state = 0;
	} else
		aos_dash(w, p, 1);
	return true;
}

static bool st_desp_grab(aos_world *w, aos_player *p) /* 0x4e T2 FUN_006a71d0 */
{
	if (p->timer == 0) {
		int i = aos_spawn_enemy(w, ID, 400, p->objId, p->side, p->color);
		if (i >= 0) {
			aos_enemy *e = &w->obj.enemy[i];
			e->pos[0] = p->x;
			e->pos[1] = p->y;
			e->pos[2] = p->z;
		}
		aos_barrier(w, p, 0x14);
	}
	if (p->timer >= 0 && p->timer < 71)
		pin(w, p, 20.0f); /* DAT_008140fc */
	if (p->timer < 81)
		p->hittable = 0;
	if (p->timer == 80)
		hbox(w, 500, &p->x, 0, p->objId, p->side);
	if (p->timer < 90 || p->D != 3) {
		if (p->timer > 100)
			p->state = 0;
	} else
		aos_dash(w, p, 1);
	return true;
}

/* ------------------------------------------------------------------ */
/* player draw (T1)                                                    */

static aos_dcmd *sprite(aos_world *w, aos_player *p, const char *fmt, int f)
{
	aos_dcmd *c = aos_draw(aos_texf(fmt, f), aos_pidx(w, p), p->x, p->y, 0);
	c->halfW = c->halfH = 48.0f; /* S = 96 (DAT_00814118) */
	return c;
}

/* AIM transform: |a| >= π/2: RotZ(a), RotX(180), RotY(180); else RotZ(a), RotY(180). */
static void aimt(aos_dcmd *c, float a)
{
	c->rotZ = aos_rad2bam(a);
	c->flipX = 1;
	if (aos_fabs(a) >= HALF_PI)
		c->sy = -1.0f; /* RotX(180) ≈ y-scale × cos(π) */
}

static void dr_aim(aos_world *w, aos_player *p, const char *fmt, int f)
{
	aimt(sprite(w, p, fmt, f), p->faceAngle);
}

/* dr_aim + FUN_004d7e00 afterimage with the AIM rotation {RotX, RotY, RotZ}. */
static void dr_aim_ai(aos_world *w, aos_player *p, const char *fmt, int f)
{
	aos_dcmd *c = sprite(w, p, fmt, f);
	aimt(c, p->faceAngle);
	int32_t rot[3] = {HALF_PI <= aos_fabs(p->faceAngle) ? aos_deg2bam(0xb4) : 0, aos_deg2bam(0xb4),
	                  aos_rad2bam(p->faceAngle)};
	aos_afterimage(w, c, 1, p, rot);
}

static int pp(int t, int n)
{
	int f = (t / n) & 3;
	return f == 3 ? 1 : f;
}

static void dr_neutral(aos_world *w, aos_player *p) /* 0x32 T1 FUN_006a8f10 */
{
	int panel = find_panel(w, p->side);
	if (p->S < 2 || panel < 0 || w->frame - eint(&w->obj.enemy[panel], 2) < 0x65) {
		const char *fmt = "at_std%02d";
		if (p->up > 1) fmt = "at_fwd%02d";
		if (p->down > 1) fmt = "at_bck%02d";
		if (p->facingRight == 0 && p->left > 1) fmt = "at_fwd%02d";
		if (p->facingRight == 1 && p->left > 1) fmt = "at_bck%02d";
		if (p->facingRight == 0 && p->right > 1) fmt = "at_bck%02d";
		if (p->facingRight == 1 && p->right > 1) fmt = "at_fwd%02d";
		aos_dcmd *c = sprite(w, p, fmt, pp(p->timer, 5));
		c->flipX = p->facingRight != 0;
		int32_t rot[3] = {0, p->facingRight != 0 ? aos_deg2bam(0xb4) : 0, 0};
		aos_afterimage(w, c, 1, p, rot); /* FUN_004d7e00 */
	} else {
		int f = p->timer / 5;
		if (f > 3) f = 3;
		dr_aim(w, p, "at_sld%02d", f); /* Searchlight Shield pose */
	}
}

static void dr_dash(aos_world *w, aos_player *p) /* 0x33 T1 FUN_006a94c0 */
{
	int f = pp(p->timer, 3);
	if ((p->facingRight == 1 && aos_fabs(p->faceAngle) > HALF_PI) ||
	    (p->facingRight == 0 && aos_fabs(p->faceAngle) < HALF_PI))
		f += 3;
	dr_aim_ai(w, p, "at_dsh%02d", f);
}

static void dr_ko(aos_world *w, aos_player *p) /* 0x34 T1 FUN_006a9e20 */
{
	int f = pp(p->timer, 3);
	if (w->frame % 2 == 0)
		f += 3;
	if (p->timer < 100)
		dr_aim(w, p, "at_los%02d", f);
}

static void dr_hit(aos_world *w, aos_player *p) { dr_aim(w, p, "at_los%02d", 0); } /* 0x35 FUN_006a98c0 */

static void dr_shield(aos_world *w, aos_player *p) /* 0x36 FUN_006a9b60, 0x4c FUN_006ab540 */
{
	int f = p->timer / 4;
	dr_aim(w, p, "at_sld%02d", f > 3 ? 3 : f);
}

static void dr_smg(aos_world *w, aos_player *p) /* 0x3c/0x3d FUN_006aa120 / 006aa430 */
{
	int t = p->timer, f = t < 3 ? 0 : t < 6 ? 1 : (t - 6) % 3 + 2;
	dr_aim(w, p, "at_smg%02d", f);
}

static void dr_spin(aos_world *w, aos_player *p) /* 0x3e/0x3f/0x4b */
{
	int f = p->timer / 4;
	dr_aim(w, p, "at_spin%02d", f > 4 ? 4 : f);
}

static void dr_salvo(aos_world *w, aos_player *p) /* 0x40 FUN_006aaa00 */
{
	int f = p->timer / 5;
	if (f > 4) f -= 5;
	if (f > 4) f = 4;
	dr_aim(w, p, "at_spin%02d", f);
}

static void dr_rod(aos_world *w, aos_player *p) /* 0x41 FUN_006ab800 */
{
	int t = p->timer, f = t < 12 ? 0 : t < 15 ? 1 : ((t - 15) / 3) % 3 + 2;
	if (f == 5) f = 3;
	if (2.0f < p->speed) /* DAT_00813a4c */
		dr_aim_ai(w, p, "at_rod%02d", f);
	else
		dr_aim(w, p, "at_rod%02d", f);
}

static void dr_rod_loop(aos_world *w, aos_player *p) /* 0x42/0x47 FUN_006abbf0 / 006acb00 */
{
	int f = (p->timer / 3) % 3 + 2;
	if (f == 5) f = 3;
	if (2.0f < p->speed) /* DAT_00813a4c */
		dr_aim_ai(w, p, "at_rod%02d", f);
	else
		dr_aim(w, p, "at_rod%02d", f);
}

static void dr_punisher(aos_world *w, aos_player *p) /* 0x44..0x46 */
{
	int t = p->timer, f = t < 12 ? 0 : t < 20 ? 1 : ((t - 20) / 3) % 3 + 2;
	if (f == 5) f = 3;
	if (2.0f < p->speed) /* DAT_00813a4c: FUN_004d7e00 only while moving (FUN_006abf90) */
		dr_aim_ai(w, p, "at_rod%02d", f);
	else
		dr_aim(w, p, "at_rod%02d", f);
}

static void dr_roll(aos_world *w, aos_player *p) /* 0x48 FUN_006acea0 */
{
	int t = p->timer, f = t < 10 ? 0 : ((t - 10) / 3) % 3 + 1;
	dr_aim(w, p, "at_roll%02d", f);
	float a = ((float)((t / 3) % 6) / 6.0f) * AOS_2PI + p->faceAngle; /* DAT_00813f44 */
	if (t > 32)
		a = p->faceAngle;
	aos_angle_normalize(&a);
	aos_dcmd *c = sprite(w, p, "at_roll%02d", 4);
	aimt(c, p->faceAngle);
	c->rotZ = aos_rad2bam(a); /* flip choice follows |aim|, rotation the local angle */
}

static void dr_panel(aos_world *w, aos_player *p) /* 0x49 FUN_006aaf90 */
{
	int f = 0;
	if (p->timer >= 10)
		f = ((p->timer - 10) / 4) % 4 + 1;
	if (f == 4) f = 2;
	dr_aim(w, p, "at_sld%02d", f);
}

static void dr_desp(aos_world *w, aos_player *p) /* 0x4d FUN_006ad470 */
{
	if (2.0f < p->speed)
		dr_aim_ai(w, p, "at_dsh%02d", pp(p->timer, 3));
	else
		dr_aim(w, p, "at_dsh%02d", pp(p->timer, 3));
}
static void dr_hold(aos_world *w, aos_player *p) { dr_aim(w, p, "at_hld%02d", 0); }               /* 0x4e */

/* ------------------------------------------------------------------ */
/* Beam family (type 4): FUN_004aa870 / 004ab7b0 / 004ab0e0 / 004ab6c0 */

static int beam_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t age0,
                      int32_t team)
{
	if (id != 100 && id != 0x6e)
		return -1;
	int i = aos_beam_new(w, ID, id, pos, angle, owner, age0, team);
	if (i < 0)
		return -1;
	aos_beam *b = &w->obj.beam[i];
	if (id == 100) {
		b->speed = 16.0f; /* DAT_008140d8 */
		b->f6c = 0;
		b->damage = 100;
		b->shape.halfW = 24.0f; /* DAT_00853b8c */
		b->shape.halfH = 8.0f;  /* DAT_008140d4 */
		b->knockback = 10.0f;   /* DAT_00813f4c */
	} else {
		b->active = 0;
	}
	return i;
}

static bool beam_update(aos_world *w, aos_beam *b)
{
	if (b->id == 100) {
		if (0.0f < b->f6c && b->age < 50) {
			aos_player *o = opp_of_team(w, b->team);
			float a = aos_atan2(o->y - b->pos[1], o->x - b->pos[0]);
			aos_angle_toward(&b->moveAngle, a, (50.0f - (float)b->age) * b->f6c * 0.5f);
			b->angle = b->moveAngle;
		}
		b->pos[0] = aos_cos(b->moveAngle) * b->speed + b->pos[0];
		b->pos[1] = aos_sin(b->moveAngle) * b->speed + b->pos[1];
		if (b->age > 10 && !aos_in_arena(b->pos, 30.0f))
			return false;
		float pos[3] = {b->pos[0], b->pos[1], b->pos[2]};
		beam_spawn(w, 0x6e, pos, b->moveAngle, b->ownerId, 0, b->team);
	} else if (b->id == 0x6e) {
		if (b->age > 3)
			b->alpha -= 0.2f;
		if (b->alpha <= 0.0f)
			return false;
	}
	return true;
}

static void beam_draw(aos_world *w, aos_beam *b)
{
	if (b->id != 0x6e)
		return;
	int c = team_col(w, b->team, 1);
	float x0 = (float)((c >> 3) * 0xf0), y0 = (float)((c & 7) * 0x40);
	int a1 = (int)(16.0f * b->alpha) & 0xff; /* DAT_008140d8 (literal) */
	aos_dcmd *d = aos_draw(aos_tex("beamemitb"), AOS_BANK_COMMON, b->pos[0], b->pos[1], 0);
	d->hasSrc = 1;
	d->src[0] = x0; d->src[1] = y0; d->src[2] = x0 + 0x77; d->src[3] = y0 + 0x3f;
	d->halfW = 60.0f; /* DAT_00813a58 */
	d->halfH = 32.0f; /* DAT_008140dc */
	d->rotZ = aos_rad2bam(b->angle);
	d->rgba = aos_rgba(1, 1, 1, (float)a1 / 255.0f);
	d->blend = AOS_BLEND_ALPHA;
	int a2 = (int)(255.0f * b->alpha) & 0xff; /* DAT_00853b94 */
	if (b->age < 2) {
		d = aos_draw(aos_tex("beamemit"), AOS_BANK_COMMON, b->pos[0], b->pos[1], 0);
		x0 = (float)((c >> 3) << 8);
		d->hasSrc = 1;
		d->src[0] = x0; d->src[1] = y0; d->src[2] = x0 + 0x7f; d->src[3] = y0 + 0x3f;
		d->halfW = 48.0f; /* DAT_008140e4 */
		d->halfH = 16.0f; /* DAT_008140d8 */
		d->rotZ = aos_rad2bam(b->angle);
		d->blend = AOS_BLEND_ADD_ALPHA;
		return;
	}
	d = aos_draw(aos_tex("beamemitb"), AOS_BANK_COMMON, b->pos[0], b->pos[1], 0);
	d->hasSrc = 1;
	d->src[0] = x0 + 0x78; d->src[1] = y0; d->src[2] = x0 + 0xef; d->src[3] = y0 + 0x3f;
	d->halfW = 40.0f; /* DAT_008140e0 */
	d->halfH = 8.0f;  /* DAT_008140d4 */
	d->rotZ = aos_rad2bam(b->angle);
	d->blend = AOS_BLEND_ADD_ALPHA;
	float al = (float)a2 / 255.0f;
	if (b->age < 5)
		d->rgba = aos_rgba(1, 1, 1, al);
	else if (b->team < 1 || b->team > 2 || w->p[b->team - 1].color == 0)
		d->rgba = aos_rgba(1, 0, 1, al);
	else
		d->rgba = aos_rgba(0, 1, 1, al);
}

static void beam_on_hit(aos_world *w, aos_beam *b)
{
	if (b->id != 100)
		return;
	int32_t c = team_col(w, b->team, 1);
	fx0(w, 1000, c, b->pos, b->angle + AOS_PI, b->ownerId, 0, b->team);
	aos_spawn_particle(w, 1000, c, b->pos, b->angle, b->ownerId, 0, 10);
	aos_spawn_spark(w, 1000, c, 10, 10, b->angle, b->pos, b->ownerId);
	aos_se(w, 0x13);
}

/* ------------------------------------------------------------------ */
/* Missile family: FUN_0065e420 / 00661c50 / 0065ecf0 / 00661450 / 00661870 */

static int missile_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t age0,
                         int32_t team, int32_t p18)
{
	if (id != 100 && id != 200 && id != 300)
		return -1; /* 400 / 500 are never spawned (alte.md §0.10) */
	int i = aos_missile_new(w, ID, id, pos, angle, owner, age0, team, p18);
	if (i < 0)
		return -1;
	aos_missile *m = &w->obj.missile[i];
	if (id == 300) {
		m->speed = 8.0f;     /* DAT_008140d4 */
		m->f74 = 0.0025f;    /* DAT_008145e0 */
		m->hp = 0x78;
		m->damage = 200;
		m->shapeBody.radius = 10.0f; /* DAT_00813f4c */
		m->shapeProj.radius = 10.0f;
		m->knockback = 10.0f;
	} else if (id == 100) {
		m->speed = 17.0f; /* _DAT_00813f5c (ver > 0x58a) */
		m->angle = aos_rand_real(w, -AOS_PI, AOS_PI);
		m->hp = 0x78;     /* ver > 0x5a0 */
		m->damage = 0x1e;
		m->shapeBody.radius = 8.0f; /* DAT_008140d4 */
		m->shapeProj.radius = 8.0f;
		m->knockback = 5.0f; /* DAT_00813f40 */
		m->stun = 0.4f;      /* DAT_00813f30 */
	} else { /* 200 beam panel */
		m->speed = 0;
		m->growScale = 0;
		m->hp = 9999;
		m->damage = 0;
		m->shapeBody.radius = 0;
		m->shapeProj.radius = 0; /* ver > 0x5a0 */
		m->knockback = 10.0f;
		m->active = 0;
	}
	return i;
}

static bool missile_update(aos_world *w, aos_missile *m)
{
	if (m->id == 300) {
		if (m->age < 50) {
			if (0.0f < m->speed)
				m->speed -= 0.25f;
		} else {
			m->active = 1;
			if (m->speed < 10.0f)
				m->speed += 0.1f;
			aos_player *o = opp_of_team(w, m->team);
			if (m->age < 200) {
				float a = aos_atan2(o->y - m->pos[1], o->x - m->pos[0]);
				aos_angle_toward(&m->moveAngle, a, m->speed * m->f74);
				m->angle = m->moveAngle;
			}
		}
		m->pos[0] = aos_cos(m->moveAngle) * m->speed + m->pos[0];
		m->pos[1] = aos_sin(m->moveAngle) * m->speed + m->pos[1];
		if (m->age % 2 == 0 || 12.0f < m->speed) { /* DAT_00813f54 */
			float pt[3] = {-aos_cos(m->angle) * 20.0f + m->pos[0], -aos_sin(m->angle) * 20.0f + m->pos[1],
			               m->pos[2]};
			fx0(w, 0x802, 0, pt, m->moveAngle, m->ownerId, 0, m->team);
		}
		if (!aos_in_arena(m->pos, 10.0f)) {
			if (m->f59 == 1 || m->age > 299) {
				aos_otab.missileOnHit[m->type](w, m);
				return false;
			}
		} else if (m->age > 100) {
			m->f59 = 1;
		}
	} else if (m->id == 100) {
		m->angle += 0.25f;
		if (AOS_PI < m->angle)
			m->angle -= AOS_2PI;
		m->pos[0] = aos_cos(m->moveAngle) * m->speed + m->pos[0];
		m->pos[1] = aos_sin(m->moveAngle) * m->speed + m->pos[1];
		if (!aos_in_arena(m->pos, 10.0f)) {
			aos_otab.missileOnHit[m->type](w, m);
			return false;
		}
	} else if (m->id == 200) {
		if (m->age < 100) {
			if (m->growScale <= 1.0f && m->growScale != 1.0f)
				m->growScale += 0.1f;
			if (1.0f < m->growScale) {
				m->active = 1;
				m->growScale = 1.0f;
			}
			int a = m->age;
			if (a == 15 || a == 20 || a == 25 || a == 30 || a == 35) {
				int panel = find_panel(w, m->team);
				int32_t col = team_col(w, m->team, 1);
				float off = 0.0f;
				if (panel == -1) {
					off = a == 15 ? -0.125f : a == 20 ? -0.06f : a == 30 ? 0.06f : a == 35 ? 0.125f : 0.0f;
				} else {
					off = a == 15 ? -0.2f : a == 20 ? -0.15f : a == 30 ? 0.15f : a == 35 ? 0.2f : 0.0f;
				}
				float pos[3] = {m->pos[0], m->pos[1], m->pos[2]};
				int bi = aos_spawn_beam(w, ID, 100, pos, m->angle + off, m->ownerId, 0, m->team);
				fx0(w, 0x548, col, pos, m->angle, m->ownerId, 0, m->team);
				if (panel != -1 && bi >= 0)
					w->obj.beam[bi].f6c = 0.001f; /* DAT_00814bd8 */
				aos_se(w, 0x10);
			}
		} else {
			m->active = 0;
			if (0.0f < m->growScale)
				m->growScale -= 0.1f;
			if (m->growScale <= 0.0f)
				return false;
		}
	}
	return true;
}

static float pulse(int age, float amp, float base)
{
	return aos_sin(((float)(age % 50) / 50.0f) * AOS_2PI) * amp + base;
}

static void missile_draw(aos_world *w, aos_missile *m)
{
	aos_player *o = aos_player_by_objid(w, m->ownerId);
	int bank = o ? aos_pidx(w, o) : (m->team == 2 ? 1 : 0);
	aos_dcmd *c;
	if (m->id == 300) {
		float v[2] = {42.0f, 0.0f}; /* DAT_00824488 */
		aos_vec_rotate(v, m->angle + AOS_PI);
		v[0] += m->pos[0];
		v[1] += m->pos[1];
		c = aos_draw(aos_tex("at_bfireb"), bank, v[0], v[1], 0);
		c->halfW = c->halfH = 24.0f;
		c->blend = AOS_BLEND_ADD_ALPHA;
		c->rotZ = aos_rad2bam(m->angle);
		c = aos_draw(aos_tex("at_bfire"), bank, v[0], v[1], 0);
		c->halfW = c->halfH = 24.0f;
		c->blend = AOS_BLEND_ADD_ALPHA;
		c->rotZ = aos_rad2bam(m->angle);
		if ((m->age / 2) % 2 == 0)
			c->sy = 0.3f; /* DAT_00813f28 */
		int f = ((m->age / 3) % 3) * 3;
		for (int k = 0; k < 3; k++) {
			c = aos_draw(aos_texf("at_msl%d", f + k), bank, m->pos[0], m->pos[1], 0);
			c->halfW = c->halfH = 32.0f;
			c->rotZ = aos_rad2bam(m->angle);
			c->blend = k ? AOS_BLEND_ADD_ALPHA : AOS_BLEND_ALPHA;
			if (k)
				c->rgba = aos_rgba(1, 1, 1, k == 1 ? pulse(m->age, 0.25f, 0.75f) : pulse(m->age, 0.35f, 0.65f));
		}
	} else if (m->id == 100) {
		c = aos_draw(aos_tex("at_almond"), bank, m->pos[0], m->pos[1], 0);
		c->halfW = c->halfH = 27.0f;
		c->rotZ = aos_rad2bam(m->angle);
	} else if (m->id == 200) {
		float g = m->growScale;
		for (int k = 0; k < 3; k++) {
			c = aos_draw(aos_texf("at_panel%d", k), bank, m->pos[0], m->pos[1], 0);
			c->halfW = c->halfH = 27.0f;
			c->sx = g;
			c->sy = 4.0f - g * 3.0f;
			c->rotZ = aos_rad2bam(m->angle);
			c->blend = k ? AOS_BLEND_ADD_ALPHA : AOS_BLEND_ALPHA;
			c->rgba = k == 0 ? aos_rgba(g, g, g, 1)
			                 : aos_rgba(1, 1, 1, k == 1 ? pulse(m->age, 0.25f, 0.75f) : pulse(m->age, 0.35f, 0.65f));
		}
	}
}

static void debris(aos_world *w, const float pos[3], int32_t owner, int32_t team, int32_t lastId, int sound)
{
	(void)lastId;
	fx0(w, 0x262, 0, pos, 0, owner, 0, team);
	fx0(w, 0x514, 1, pos, 0, owner, 0, team);
	aos_se(w, sound);
}

static bool missile_on_hit(aos_world *w, aos_missile *m) /* FUN_00661450 */
{
	if (m->id == 300 || m->id == 200) {
		debris(w, m->pos, m->ownerId, m->team, 0, 0x14);
	} else if (m->id == 100) {
		int32_t c = team_col(w, m->team, 1);
		fx0(w, 1000, c, m->pos, m->moveAngle + AOS_PI, m->ownerId, 0, m->team);
		aos_spawn_spark(w, 1000, c, 4, 2, m->angle, m->pos, m->ownerId);
		aos_se(w, 0x13);
	}
	return false;
}

static bool missile_on_killed(aos_world *w, aos_missile *m) /* FUN_00661870 */
{
	if (m->id == 300 || m->id == 200)
		debris(w, m->pos, m->ownerId, m->team, 0, 0x1d);
	else if (m->id == 100)
		fx0(w, 0x516, team_col(w, m->team, 0), m->pos, 0, m->ownerId, 0, m->team);
	return false;
}

/* ------------------------------------------------------------------ */
/* Hitbox family: FUN_005e0180 / FUN_005e0740                          */

static int hitbox_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t zero,
                        int32_t team)
{
	int32_t dmg;
	float r, kb = 0;
	switch (id) {
	case 0x15e: dmg = 0; r = 80.0f; break;
	case 100: dmg = 0x1e; r = 80.0f; break;
	case 200: dmg = 0x5a; r = 140.0f; break; /* DAT_00817004 */
	case 300: dmg = 0; r = 60.0f; break;
	case 0x136: dmg = 0x50; r = 60.0f; break;
	case 0x140: dmg = 0x50; r = 60.0f; kb = 25.0f; break; /* DAT_008140f0 */
	case 0x168: dmg = 0x3c; r = 90.0f; break;
	case 0x172: dmg = 0x50; r = 90.0f; kb = 25.0f; break;
	case 400: dmg = 0x2d; r = 60.0f; break;
	case 0x19a: dmg = 0; r = 60.0f; break;
	case 500: dmg = 0x5dc; r = 120.0f; break; /* ver > 0x5a0 */
	default: return -1;
	}
	int i = aos_hitbox_new(w, ID, id, pos, angle, owner, zero, team);
	if (i < 0)
		return -1;
	aos_hitbox *h = &w->obj.hitbox[i];
	h->hitType = 3;
	h->damage = dmg;
	h->shape.radius = r;
	h->knockback = kb;
	return i;
}

static void streaks(aos_world *w, aos_hitbox *hb, aos_player *p, int n, float div)
{
	int32_t c = team_col(w, hb->team, 0);
	for (int k = -n; k < n; k++) {
		float a = ((float)k / div) * AOS_PI;
		float r = aos_rand_real(w, -0.25f, 0.25f); /* DAT_0081411c, DAT_00813f24 */
		fx0(w, 0x456, c, &p->x, a + r, hb->ownerId, 0, hb->team);
	}
}

static void hitbox_on_hit(aos_world *w, aos_hitbox *hb, aos_player *p)
{
	switch (hb->id) {
	case 0x15e:
	case 300: {
		aos_player *o = player_of_team(w, hb->team);
		if (o)
			o->hitFlag = 1;
		aos_se_cd(w, 0x15, 2);
		break;
	}
	case 100:
		if (hb->damage < 100) {
			aos_se_cd(w, 0x13, 4);
			if (0 < hb->damage) {
				streaks(w, hb, p, 3, 3.0f);
				fx0(w, 0x546, team_col(w, hb->team, 0), &p->x, hb->angle, hb->ownerId, 0, hb->team);
			}
		} else if (p->state != 1 || p->speed <= 2.0f) {
			streaks(w, hb, p, 3, 3.0f);
			fx0(w, 0x546, team_col(w, hb->team, 0), &p->x, hb->angle, hb->ownerId, 0, hb->team);
			aos_se(w, 0x15);
		} else {
			hb->dealsDamage = 0;
		}
		break;
	case 200:
		if (0 < hb->damage) {
			float r = aos_rand_real(w, -AOS_PI, AOS_PI);
			fx4(w, 400, team_col(w, hb->team, 0), &p->x, r, hb->ownerId, 0, hb->team);
			aos_se(w, 0x13);
		}
		if (99 < hb->damage)
			aos_se(w, 0x15);
		if (0x95 < hb->damage)
			aos_screen_blur(w, 10, 0.75f, 1); /* DAT_00813f3c */
		break;
	case 0x136:
	case 0x168:
		aos_se_cd(w, 0x15, 2);
		break;
	case 0x140:
	case 0x172:
		fx0(w, 0xa8d, 0, &p->x, 0, hb->ownerId, 0, hb->team);
		aos_screen_blur(w, 10, 0.75f, 1); /* DAT_00813f3c */
		aos_se(w, 0x19);
		break;
	case 400:
		if (p->state != 1 || (p->speed <= 2.0f && 4 < p->timer)) {
			streaks(w, hb, p, 4, 4.0f);
			fx0(w, 0x515, team_col(w, hb->team, 0), &p->x, 0, hb->ownerId, 0, hb->team);
			aos_se_cd(w, 0x15, 4);
		} else {
			hb->dealsDamage = 0;
		}
		break;
	case 0x19a:
		if (p->state == 1 && 2.0f < p->speed)
			hb->dealsDamage = 0;
		break;
	case 500:
		hb->angle = aos_atan2(p->y - hb->pos[1], p->x - hb->pos[0]);
		break;
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* Enemy family: FUN_0051a570 / 0051b1d0 / 0051d000 / 0051cbb0         */

static int enemy_spawn(aos_world *w, int32_t id, int32_t owner, int32_t team, int32_t p18)
{
	if (id != 100 && id != 200 && id != 300 && id != 400)
		return -1;
	int32_t grp = aos_enemy_group_next(w); /* FUN_0055efc0, once per spawn (before FUN_0055ee70) */
	int i = aos_enemy_new(w, ID, owner, team, p18);
	if (i < 0)
		return -1;
	aos_enemy *e = &w->obj.enemy[i];
	e->serial = grp;
	e->f10 = id;
	e->f7c = id;
	e->f84 = 0;
	e->motionId = 0;
	e->rehitTimer = 0;
	e->active = 0;
	switch (id) {
	case 100:
		e->hp = e->maxHp = 2000; /* ver > 0x5a0 */
		e->shape.radius = 30.0f; /* DAT_008140e8 */
		e->scale[0] = 0;
		e->scale[1] = 3.0f;      /* DAT_00813a50 */
		e->scale[2] = 0;
		e->mirrorChildren = 0;
		break;
	case 200:
		e->hp = e->maxHp = 400;
		e->shape.radius = 30.0f;
		e->scale[0] = 0;
		e->scale[1] = 3.0f;
		e->scale[2] = 0;
		e->mirrorChildren = 0;
		break;
	case 300:
		e->hp = e->maxHp = 999;
		e->scale[0] = e->scale[1] = e->scale[2] = 0;
		e->mirrorChildren = 0;
		break;
	case 400:
		e->hp = e->maxHp = 1000;
		e->scale[0] = e->scale[1] = 1.0f;
		e->scale[2] = 0;
		e->mirrorChildren = 0;
		break;
	}
	return i;
}

static bool upd_shock_ball(aos_world *w, aos_enemy *e)
{
	if (e->f84 == 0) {
		e->angle = aos_rand_real(w, -AOS_PI, AOS_PI);
		e->scale[0] += 0.1f;
		e->scale[2] += 0.1f;
		if (1.0f < e->scale[0])
			e->scale[0] = 1.0f;
		if (1.0f < e->scale[2]) {
			e->scale[2] = 1.0f;
			e->f84 = 100;
			e->timer2 = 0;
		}
	} else if (e->f84 == 100) {
		if (e->timer2 > 20) {
			e->f70 = 0;
			e->f84 = 200;
			e->timer2 = 0;
			aos_player *o = opp_of_team(w, e->team);
			float a = aos_atan2(o->y - e->pos[1], o->x - e->pos[0]);
			float r = aos_rand_real(w, -0.25f, 0.25f);
			e->angle2 = a + r;
		}
	} else if (e->f84 == 200) {
		e->f70 += 0.1f;
		if (7.5f < e->f70) /* DAT_00814150 */
			e->f70 = 7.5f;
		e->pos[0] = aos_cos(e->angle2) * e->f70 + e->pos[0];
		e->pos[1] = aos_sin(e->angle2) * e->f70 + e->pos[1];
		aos_player *o;
		if (find_panel(w, e->team) != -1 && (o = opp_of_team(w, e->team)) != NULL) {
			float a = aos_atan2(o->y - e->pos[1], o->x - e->pos[0]);
			aos_angle_toward(&e->angle2, a, e->f70 * 0.0015f); /* DAT_008153dc */
		}
		if (e->timer2 > 50 && !aos_in_arena(e->pos, 20.0f)) {
			e->f84 = 900;
			fx0(w, 0x515, team_col(w, e->team, 0), e->pos, 0, e->ownerId, 0, e->team);
		}
	} else {
		e->scale[0] -= 0.05f;
		if (e->scale[0] <= 0.0f)
			return false;
	}
	if (e->f84 != 900 && e->age % 15 < 3) {
		float a = (float)(e->age % 15) * TWO_PI_3;
		float r = aos_rand_real(w, -0.5f, 0.5f); /* DAT_00853bb0, DAT_00853b1c */
		fx4(w, 400, 0, e->pos, a + r, e->ownerId, 0, e->team);
	}
	if (e->f84 != 0 && e->f84 != 900 && e->timer2 % 4 == 0)
		hbox(w, 400, e->pos, e->angle2, e->ownerId, e->team);
	return true;
}

static bool upd_panel(aos_world *w, aos_enemy *e)
{
	if (e->f84 == 0) {
		e->scale[0] += 0.1f;
		e->scale[1] -= 0.2f;
		e->scale[2] += 0.1f;
		if (1.0f <= e->scale[0]) {
			e->scale[2] = e->scale[0] = e->scale[1] = 1.0f;
			e->f84 = 10;
			fx4(w, 100, 1, e->pos, 0, e->ownerId, 0, e->team);
		}
	} else if (e->f84 != 10) {
		if (e->f84 == 20) {
			if (e->age % 5 == 0)
				fx4(w, 200, 1, e->pos, e->angle, e->ownerId, 0, e->team);
			if (e->age % 6 == 0)
				fx4(w, 0xfa, 1, e->pos, e->angle, e->ownerId, 0, e->team);
		} else if (e->f84 == 30) {
			if (e->timer2 == 10) {
				aos_screen_dark(w, 100, 0.5f);       /* DAT_00853b1c */
				aos_screen_blur(w, 0x32, 0.5f, 2);
				fx4(w, 0x15e, 0, e->pos, e->angle, e->ownerId, 0, e->team);
				aos_se(w, 0x1e);
			}
			if (e->timer2 > 9 && e->timer2 < 51) {
				int h = hbox(w, 200, e->pos, e->angle2, e->ownerId, e->team);
				if (h >= 0) {
					if (e->timer2 == 50) {
						w->obj.hitbox[h].damage = 0x96;
						w->obj.hitbox[h].knockback = 30.0f; /* DAT_008140e8 */
					}
					if (e->age % 5 != 0)
						w->obj.hitbox[h].damage = 0;
				}
			}
			if (e->timer2 > 10 && e->timer2 < 60 && e->timer2 % 6 == 0)
				fx4(w, 0x104, 1, e->pos, e->angle, e->ownerId, 0, e->team);
			if (e->timer2 > 100)
				e->f84 = 10;
		} else {
			e->active = 0;
			e->scale[0] -= 0.1f;
			e->scale[1] += 0.1f;
			if (e->scale[0] <= 0.0f)
				return false;
		}
	}
	aos_player *opp = opp_of_team(w, e->team);
	aos_player *own = player_of_team(w, e->team);
	if (!opp || !own)
		return true;
	if (e->f84 == 20 || e->f84 == 30)
		e->angle = aos_atan2(opp->y - own->y, opp->x - own->x);
	else
		e->angle = aos_atan2(opp->y - e->pos[1], opp->x - e->pos[0]);
	if (own->barrierTime < 1)
		e->invulnerable = 0;
	if (own->S < 2 && own->state == 0)
		eset(e, 2, 0);
	if (own->S < 2 || own->state != 0 || w->frame - eint(e, 2) < 0x65) {
		if (e->f84 == 30) {
			aos_angle_toward(&e->angle2, e->angle, 0.2f); /* DAT_00853b14 */
			if (100.0f <= e->f70)
				e->f70 = 100.0f;
			else
				e->f70 += 10.0f;
		} else {
			if (e->f70 < 100.0f)
				e->f70 += 5.0f; /* DAT_00813f40 */
			if (e->f84 == 20)
				e->f84 = 10;
		}
	} else {
		aos_angle_toward(&e->angle2, e->angle, 0.2f);
		if (50.0f < e->f70) /* DAT_008140ec */
			e->f70 -= 10.0f;
		if (e->f84 == 10) {
			e->f84 = 20;
			fx4(w, 300, 1, e->pos, 0, own->objId, 0, e->team);
		}
	}
	e->pos[0] = aos_cos(e->angle2) * e->f70 + own->x;
	e->pos[1] = aos_sin(e->angle2) * e->f70 + own->y;
	if (e->age == 100)
		e->active = 1;
	return true;
}

static bool upd_claymore(aos_world *w, aos_enemy *e)
{
	if (e->f84 == 0) {
		e->scale[0] += 0.1f;
		e->scale[1] -= 0.2f;
		e->scale[2] += 0.1f;
		if (1.0f <= e->scale[0]) {
			e->scale[2] = e->scale[0] = e->scale[1] = 1.0f;
			e->f84 = 10;
			e->f70 = 10.0f;
			e->age = 0;
			e->timer2 = 0;
			fx4(w, 0x96, 1, e->pos, 0, e->id, 0, e->team);
		}
		return true;
	}
	if (e->f84 == 10 || e->f84 == 11) {
		if (e->age > 30) {
			if (2.0f < e->f70)
				e->f70 *= 0.975f; /* DAT_00814128 */
			if (2.0f < e->f70 && e->age % 3 == 0) {
				float r = aos_rand_real(w, -0.2f, 0.2f);
				int n = aos_rand_int(w, 0, 100) % 8;
				fx0(w, 0xa28 + n, 0xd, e->pos, r + e->angle, e->ownerId, 0, e->team);
			}
			aos_player *o = opp_of_team(w, e->team);
			aos_angle_toward(&e->angle, aos_atan2(o->y - e->pos[1], o->x - e->pos[0]), 0.012f); /* DAT_00813ed0 */
			if (AOS_PI < e->angle)
				e->angle -= AOS_2PI;
			if (e->angle <= -AOS_PI && e->angle != -AOS_PI)
				e->angle += AOS_2PI;
			e->pos[0] = aos_cos(e->angle2) * e->f70 + e->pos[0];
			e->pos[1] = aos_sin(e->angle2) * e->f70 + e->pos[1];
		}
		if (e->age > 29)
			e->active = 1;
		if (!aos_in_arena(e->pos, 0.0f)) {
			float a = aos_atan2(e->pos[1], e->pos[0]);
			float r = aos_rand_real(w, -0.1f, 0.1f);
			e->angle2 = (a - e->angle2) + (a + AOS_PI) + r; /* FUN_005f0ce0 result discarded */
			e->pos[0] = aos_cos(a) * 540.0f;
			e->pos[1] = aos_sin(a) * 540.0f;
			e->age = 1000;
			aos_se(w, 0x25);
			int32_t c = team_col(w, e->team, 0);
			float b = aos_atan2(e->pos[1], e->pos[0]);
			fx0(w, 0x547, c, e->pos, b, e->ownerId, 0, e->team);
			fx0(w, 0x546, c, e->pos, b, e->ownerId, 0, e->team);
		}
		if (e->age > 30 && e->age % 3 == 0 && e->f84 == 10) {
			float a0[2] = {e->pos[0], e->pos[1]};
			float b0[2] = {aos_cos(e->angle - 0.15f) * 300.0f + e->pos[0], aos_sin(e->angle - 0.15f) * 300.0f + e->pos[1]};
			float c0[2] = {aos_cos(e->angle + 0.15f) * 300.0f + e->pos[0], aos_sin(e->angle + 0.15f) * 300.0f + e->pos[1]};
			aos_player *o = opp_of_team(w, e->team);
			if (aos_point_in_tri(a0, b0, c0, &o->x)) {
				float pt[3] = {aos_cos(e->angle) * 20.0f + e->pos[0], aos_sin(e->angle) * 20.0f + e->pos[1], e->pos[2]};
				fx4(w, 500, 0, pt, e->angle, e->ownerId, 0, e->team);
				float r = aos_rand_real(w, -0.1f, 0.1f);
				float pos[3] = {e->pos[0], e->pos[1], e->pos[2]};
				aos_spawn_missile(w, ID, 100, pos, r + e->angle, e->id, 0, e->team, e->p18);
				aos_se_cd(w, 0x29, 6);
				eset(e, 0, eint(e, 0) + 1);
				eset(e, 1, e->age);
				if (eint(e, 0) > 14)
					e->timer2 = 0x3e9;
			}
			return true;
		}
		if (eint(e, 0) > 0 && e->f84 == 10 && e->age % 3 == 0 && 3 < e->age - eint(e, 1))
			eset(e, 0, eint(e, 0) - 1);
		if (e->f84 == 11 && e->age > 179) {
			e->f84 = 10;
			e->age = 0;
			eset(e, 0, 0);
			eset(e, 1, 0);
		}
		if (e->timer2 > 1000) {
			e->active = 0;
			e->f84 = 900;
			e->scale[2] = 0;
		}
	} else if (e->f84 != 20) {
		e->scale[0] -= 0.1f;
		e->scale[1] += 0.2f;
		if (e->scale[0] < 0.0f)
			return false;
	}
	return true;
}

static bool upd_desp(aos_world *w, aos_enemy *e)
{
	aos_player *o = player_of_team(w, e->team);
	if (!o)
		return true;
	e->pos[0] = o->x;
	float t = (float)e->timer2;
	if (e->timer2 < 40)
		e->pos[1] = (o->y - aos_sin((t / 80.0f) * AOS_PI) * 300.0f) + 100.0f;
	else
		e->pos[1] = o->y - aos_sin((t / 80.0f) * AOS_PI) * 200.0f;
	e->scale[2] = 1.0f; /* t/30 capped at 1, then overwritten with 1 (literal) */
	e->scale[0] = t / 80.0f;
	if (e->timer2 > 0x4e) {
		fx4(w, 700, 0, e->pos, 0, e->ownerId, 0, e->team);
		for (int k = 0; k < 10; k++) {
			float pt[3] = {e->pos[0], e->pos[1], e->pos[2]};
			pt[0] += aos_rand_real(w, -150.0f, 150.0f);
			pt[1] += aos_rand_real(w, -150.0f, 150.0f);
			fx0(w, 0x834, 0, pt, 0, e->ownerId, 0, e->team);
		}
		for (int k = 0; k < 10; k++) {
			float pt[3] = {e->pos[0], e->pos[1], e->pos[2]};
			pt[0] += aos_rand_real(w, -120.0f, 120.0f);
			pt[1] += aos_rand_real(w, -120.0f, 120.0f);
			fx0(w, 0x1f5, 0, pt, 0, e->ownerId, k * -3 - 5, e->team);
		}
		aos_se(w, 0x1b);
		return false;
	}
	return true;
}

static bool enemy_update(aos_world *w, aos_enemy *e)
{
	switch (e->f7c) {
	case 300: return upd_shock_ball(w, e);
	case 100: return upd_panel(w, e);
	case 200: return upd_claymore(w, e);
	case 400: return upd_desp(w, e);
	default: return true;
	}
}

static void enemy_draw(aos_world *w, aos_enemy *e)
{
	aos_player *o = aos_player_by_objid(w, e->ownerId);
	int bank = o ? aos_pidx(w, o) : (e->team == 2 ? 1 : 0);
	float p1 = pulse(e->age, 0.25f, 0.75f), p2 = pulse(e->age, 0.35f, 0.65f);
	float s2 = e->scale[2];
	aos_dcmd *c;
	if (e->f10 == 300) {
		for (int k = 0; k < 3; k++) {
			c = aos_draw(aos_texf("at_impact%d", k), bank, e->pos[0], e->pos[1], 0);
			c->halfW = c->halfH = 64.0f;
			c->sx = c->sy = e->scale[0] * (k == 2 ? 2.0f : 1.0f);
			c->rotZ = aos_rad2bam(e->angle);
			c->blend = k ? AOS_BLEND_ADD_ALPHA : AOS_BLEND_ALPHA;
			c->rgba = k == 0 ? aos_rgba(p1, p1, p1, s2) : k == 1 ? aos_rgba(s2, s2, s2, p2) : aos_rgba(s2, s2, s2, 1);
		}
	} else if (e->f10 == 100 || e->f10 == 200) {
		int f;
		const char *fmt;
		if (e->f10 == 100) {
			f = (e->f84 == 20 || e->f84 == 30) ? ((e->age / 4) % 3 + 1) * 3 : 0;
			fmt = "at_slight%02d";
		} else {
			f = ((e->age / 5) % 3) * 3;
			fmt = "at_clmr%02d";
		}
		for (int k = 0; k < 3; k++) {
			c = aos_draw(aos_texf(fmt, f + k), bank, e->pos[0], e->pos[1], 0);
			c->halfW = c->halfH = 36.0f;
			c->sx = e->scale[0];
			c->sy = e->scale[1];
			c->rotZ = aos_rad2bam(e->angle);
			c->blend = k ? AOS_BLEND_ADD_ALPHA : AOS_BLEND_ALPHA;
			c->rgba = k == 0 ? aos_rgba(s2, s2, s2, 1) : aos_rgba(1, 1, 1, (k == 1 ? p1 : p2) * s2);
		}
	} else if (e->f10 == 400) {
		c = aos_draw(aos_tex("at_mboss"), bank, e->pos[0], e->pos[1], 0);
		c->halfW = c->halfH = 192.0f;
		c->sx = c->sy = e->scale[0];
		c->rotZ = aos_rad2bam(e->angle);
		c->rgba = aos_rgba(s2, s2, s2, s2);
	}
}

static bool enemy_on_killed(aos_world *w, aos_enemy *e) /* FUN_0051cbb0 */
{
	if (e->f10 != 100 && e->f10 != 200)
		return false;
	fx0(w, 600, 0, e->pos, 0, e->ownerId, 0, e->team);
	for (int k = 0; k < 5; k++) {
		float pt[3] = {e->pos[0], e->pos[1], e->pos[2]};
		pt[0] += aos_rand_real(w, -50.0f, 50.0f); /* DAT_008149e4, DAT_008140ec */
		pt[1] += aos_rand_real(w, -50.0f, 50.0f);
		fx0(w, 0x834, 0, pt, 0, e->ownerId, 0, e->team);
	}
	for (int k = 0; k < 5; k++) {
		float pt[3] = {e->pos[0], e->pos[1], e->pos[2]};
		pt[0] += aos_rand_real(w, -30.0f, 30.0f); /* DAT_00817c08, DAT_008140e8 */
		pt[1] += aos_rand_real(w, -30.0f, 30.0f);
		fx0(w, 500, 0, pt, 0, e->ownerId, k * -3 - 10, e->team);
	}
	aos_se(w, 0x1a);
	return false;
}

/* ------------------------------------------------------------------ */
/* Effect family: FUN_004eab40 / 004edf10 / 004eb540                   */

static int effect_spawn(aos_world *w, int32_t id, int32_t variant, const float pos[3], float angle, int32_t owner,
                        int32_t age0, int32_t team)
{
	float scale = 0, alpha = 0, aux = 0, aux2 = 0, mdir = angle, rot = angle;
	int32_t layer = 1;
	switch (id) {
	case 0x168:
		aux2 = 1.0f;
		alpha = 1.0f;
		aux = 0.175f; /* DAT_00817888 */
		mdir = aos_rand_real(w, 0.25f, 0.75f); /* DAT_00813f24, DAT_00813f3c */
		break;
	case 100:
	case 0x96:
		layer = 0;
		break;
	case 200:
	case 600:
	case 400:
	case 500:
		alpha = 1.0f;
		scale = 1.0f;
		break;
	case 0xfa:
	case 0x104:
	case 300:
		alpha = 1.0f;
		break;
	case 0x15e:
		alpha = 1.0f;
		layer = 0;
		aux = 0.175f;
		break;
	case 0x172: {
		alpha = 1.0f;
		float r = aos_rand_real(w, -AOS_PI, AOS_PI);
		float cx = aos_cos(r) * 0.1f, sy = aos_sin(r);
		aux = aos_sqrt(cx * cx + sy * sy) * 500.0f; /* DAT_00814140 */
		mdir = rot = angle + r;
		break;
	}
	case 0x262:
		alpha = 1.0f;
		aux = 8.0f; /* DAT_008140d4 */
		break;
	case 0x263:
		alpha = 1.0f;
		aux = 6.0f; /* DAT_00813f44 */
		break;
	case 700:
		alpha = 1.0f;
		scale = 1.0f;
		break;
	default:
		return -1;
	}
	int i = aos_effect_new(w, ID, id, variant, pos, angle, owner, age0, team);
	if (i < 0)
		return -1;
	aos_effect *e = &w->obj.effect[i];
	e->drawLayer = layer;
	e->scale = scale;
	e->alpha = alpha;
	e->moveDir = mdir;
	e->rotation = rot;
	e->aux = aux;
	e->aux2 = aux2;
	return i;
}

static void grow01(float *v)
{
	if (*v <= 1.0f && *v != 1.0f)
		*v += 0.025f; /* DAT_00813edc */
	if (1.0f < *v)
		*v = 1.0f;
}

static bool fx_sparkle(aos_world *w, aos_effect *e, float dist) /* 0xfa / 0x104 */
{
	int pi = find_panel(w, e->team);
	aos_player *o = player_of_team(w, e->team);
	if (pi > -1 && o) {
		aos_enemy *pn = &w->obj.enemy[pi];
		e->moveDir = pn->angle;
		e->rotation = aos_rand_real(w, -AOS_PI, AOS_PI);
		e->pos[0] = pn->pos[0];
		e->pos[1] = pn->pos[1];
		e->pos[2] = pn->pos[2];
		e->pos[0] -= aos_cos(e->moveDir) * e->scale * dist;
		e->pos[1] -= aos_sin(e->moveDir) * e->scale * dist;
	}
	e->scale = e->alpha * e->alpha * 0.1f + e->scale;
	if (e->age > 5)
		e->alpha -= 0.05f;
	return e->alpha > 0.0f;
}

static bool fx_shield(aos_world *w, aos_effect *e) /* id 300: Searchlight Shield (gameplay) */
{
	int pi = find_panel(w, e->team);
	aos_player *o = player_of_team(w, e->team);
	if (pi < 0 || !o) {
		e->alpha -= 0.1f;
		e->scale -= 0.1f;
		return e->alpha > 0.0f;
	}
	aos_enemy *pn = &w->obj.enemy[pi];
	e->pos[0] = pn->pos[0];
	e->pos[1] = pn->pos[1];
	e->pos[2] = pn->pos[2];
	e->moveDir = pn->angle;
	e->rotation = aos_rand_real(w, -AOS_PI, AOS_PI);
	if (pn->f84 == 20 && aos_use_gauge(o, 0x4a)) {
		if (e->scale <= 1.0f && e->scale != 1.0f)
			e->scale += 0.2f;
		if (1.0f < e->scale)
			e->scale = 1.0f;
		if (1.0f <= e->scale) {
			int h = hbox(w, 100, e->pos, e->moveDir, e->ownerId, e->team);
			if (h >= 0) {
				if (e->age % 5 != 0)
					w->obj.hitbox[h].damage = 0;
				if (9 < o->comboUi[4]) { /* +0x128 attacker combo hits */
					w->obj.hitbox[h].damage = 100;
					w->obj.hitbox[h].knockback = 15.0f; /* DAT_00853b88 */
				}
			}
		}
		if (e->age % 15 < 3) {
			float a = (float)(e->age % 15) * TWO_PI_3;
			float r = aos_rand_real(w, -0.5f, 0.5f);
			fx4(w, 400, 0, e->pos, a + r, e->ownerId, 0, e->team);
			return true; /* literal: panel invulnerability not set on these frames */
		}
		w->obj.enemy[pi].invulnerable = 1;
	} else {
		if (pn->f84 == 20)
			eset(pn, 2, w->frame); /* shield lockout stamp (ver > 0x58a) */
		e->alpha -= 0.1f;
		e->scale -= 0.1f;
		if (e->alpha <= 0.0f)
			return false;
	}
	return true;
}

static bool fx_spark_flash(aos_world *w, aos_effect *e) /* id 0x15e */
{
	int pi = find_panel(w, e->team);
	if (pi > -1) {
		e->pos[0] = w->obj.enemy[pi].pos[0];
		e->pos[1] = w->obj.enemy[pi].pos[1];
		e->pos[2] = w->obj.enemy[pi].pos[2];
	}
	if (e->age < 50) {
		e->scale += e->aux;
		e->aux *= 0.8f; /* DAT_008145e8 */
	} else {
		e->alpha -= 0.01f;
		if (e->alpha <= 0.0f)
			return false;
	}
	if (e->age < 11) {
		for (int k = 0; k < 2; k++) {
			float a = aos_rand_real(w, -AOS_PI, AOS_PI);
			float d = aos_rand_real(w, 0.0f, 100.0f);
			float pt[3] = {aos_cos(a) * d + e->pos[0], aos_sin(a) * d + e->pos[1], 0};
			aos_spawn_particle(w, 1000, e->variant, pt, a, e->ownerId, 0, 1);
		}
	}
	if (e->age < 41 && e->age % 5 == 0) {
		float pt[3] = {e->pos[0], e->pos[1], e->pos[2]};
		float r = aos_rand_real(w, -AOS_PI, AOS_PI);
		fx4(w, 0x168, 0, pt, r, e->ownerId, 0, e->team);
		return true;
	}
	if (e->age < 50) {
		float pt[3] = {e->pos[0], e->pos[1], e->pos[2]};
		fx4(w, 0x172, 0, pt, e->moveDir, e->ownerId, 0, e->team);
		return true;
	}
	return true;
}

static bool effect_update(aos_world *w, aos_effect *e)
{
	switch (e->id) {
	case 0x168:
		if (0.0f < e->aux2)
			e->aux2 -= 0.2f;
		e->aux2 = 0; /* literal */
		if (e->age < 20) {
			e->scale += e->aux;
			e->aux *= 0.8f;
			if (e->aux <= 0.02f && e->aux != 0.02f)
				e->aux = 0.02f; /* DAT_00813ed8 */
		} else {
			e->alpha -= 0.05f;
			if (e->alpha <= 0.0f)
				return false;
		}
		return true;
	case 100: {
		grow01(&e->scale);
		grow01(&e->alpha);
		int pi = find_panel(w, e->team);
		if (pi < 0)
			return false;
		aos_enemy *pn = &w->obj.enemy[pi];
		e->pos[0] = pn->pos[0];
		e->pos[1] = pn->pos[1];
		e->pos[2] = pn->pos[2];
		e->rotation = pn->angle;
		e->aux2 = (pn->f84 == 20 || pn->f84 == 30) ? 0.0f : 1.0f;
		return true;
	}
	case 0x96: {
		grow01(&e->scale);
		grow01(&e->alpha);
		aos_enemy *c = aos_enemy_by_id(w, e->ownerId); /* FUN_0055f0c0 */
		if (!c)
			return false;
		e->pos[0] = c->pos[0];
		e->pos[1] = c->pos[1];
		e->pos[2] = c->pos[2];
		e->rotation = c->angle;
		return true;
	}
	case 200:
		e->alpha -= 0.05f;
		e->scale += 0.25f;
		e->pos[0] -= aos_cos(e->rotation) * e->alpha * 2.0f;
		e->pos[1] -= aos_sin(e->rotation) * e->alpha * 2.0f;
		return e->alpha > 0.0f;
	case 0xfa: return fx_sparkle(w, e, 20.0f);   /* DAT_008140fc */
	case 0x104: return fx_sparkle(w, e, 40.0f);  /* DAT_008140e0 */
	case 300: return fx_shield(w, e);
	case 0x15e: return fx_spark_flash(w, e);
	case 600: return e->age <= 1;
	case 0x172:
		e->scale += 0.03f;
		e->alpha -= 0.03f;
		e->pos[0] = (aos_cos(e->rotation) * e->aux) / 250.0f + e->pos[0]; /* DAT_008149ac */
		e->pos[1] = (aos_sin(e->rotation) * e->aux) / 250.0f + e->pos[1];
		return e->alpha > 0.0f;
	case 400:
		e->alpha -= 0.05f;
		e->scale += 0.1f;
		e->pos[0] -= aos_cos(e->rotation) * (1.0f - e->alpha) * 7.0f; /* DAT_0081789c */
		e->pos[1] -= aos_sin(e->rotation) * (1.0f - e->alpha) * 7.0f;
		return e->alpha > 0.0f;
	case 500:
		if (e->age == 2)
			e->scale = 0.5f;
		return e->age != 3;
	case 0x262:
	case 0x263: {
		float k = e->id == 0x262 ? 0.03f : 0.05f;
		e->scale += k;
		e->alpha -= k;
		e->pos[0] = aos_cos(e->rotation) * e->aux + e->pos[0];
		e->pos[1] = aos_sin(e->rotation) * e->aux + e->pos[1];
		return e->alpha > 0.0f;
	}
	case 700:
		e->scale += 0.001f;
		e->alpha -= 0.02f;
		return e->alpha > 0.0f;
	default:
		return true;
	}
}

static uint8_t a8(float v) { return (uint8_t)((uint32_t)(int32_t)v & 0xff); }

static aos_dcmd *fxq(aos_effect *e, const char *name, int bank, float hw, float hh, float rot, int blend)
{
	aos_dcmd *c = aos_draw(aos_tex(name), bank, e->pos[0], e->pos[1], 0);
	c->halfW = hw;
	c->halfH = hh;
	c->rotZ = aos_rad2bam(rot);
	c->blend = (uint8_t)blend;
	return c;
}

static uint32_t argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
{
	return (uint32_t)r << 24 | (uint32_t)g << 16 | (uint32_t)b << 8 | a;
}

static void effect_draw(aos_world *w, aos_effect *e)
{
	int bank = e->team == 2 ? 1 : 0; /* DAT_00885a00 + team*0x10 = that player's palette bank */
	aos_dcmd *c;
	(void)w;
	switch (e->id) {
	case 0x168: {
		uint8_t a = a8(128.0f * e->alpha), g = a8(255.0f * e->aux2);
		c = fxq(e, "at_exp02", bank, 360.0f * e->scale, 360.0f * e->scale * e->moveDir, e->rotation, AOS_BLEND_ALPHA);
		c->rgba = argb(a, g, g, g);
		break;
	}
	case 200:
		c = fxq(e, "at_psld", bank, 36.0f * e->scale, 36.0f * e->scale, e->rotation, AOS_BLEND_ADD_ALPHA);
		c->rgba = argb(a8(128.0f * e->alpha), 0xff, 0xff, 0xff);
		break;
	case 0xfa:
	case 0x104: {
		float base = e->id == 0xfa ? 100.0f : 200.0f;
		float q1 = aos_sin(((float)(w->frame % 50) / 50.0f) * AOS_2PI) * 0.15f + 0.85f;
		float q2 = aos_sin(((float)(w->frame % 50) / 50.0f) * AOS_2PI) * 0.25f + 0.75f;
		float h = base * e->scale;
		c = fxq(e, "at_impact0", bank, h * 0.5f, h * 1.5f, e->moveDir, AOS_BLEND_ALPHA);
		c->rgba = argb(a8(192.0f * e->alpha * q1), 0xff, 0xff, 0xff);
		uint8_t a = a8(192.0f * e->alpha * q2);
		c = fxq(e, "at_impact1", bank, h * 0.5f, h * 1.5f, e->moveDir, AOS_BLEND_ADD_ALPHA);
		c->rgba = argb(a, 0xff, a, 0xff);
		break;
	}
	case 300:
		c = fxq(e, "at_impact2", bank, 100.0f * e->scale, 100.0f * e->scale, e->rotation, AOS_BLEND_ADD_ALPHA);
		c->rgba = argb(a8(255.0f * e->alpha), 0xff, 0xff, 0xff);
		break;
	case 0x15e: {
		c = fxq(e, "at_exp00", bank, 360.0f * e->scale, 360.0f * e->scale, -e->rotation, AOS_BLEND_ADD_ALPHA);
		c->rgba = argb(a8(128.0f * e->alpha), 0xff, 0xff, 0xff);
		float h = 480.0f * e->scale;
		uint8_t a;
		if (e->age % 2 == 0) {
			h *= 0.85f;
			a = a8(225.0f * e->alpha);
		} else {
			a = a8(192.0f * e->alpha);
		}
		c = fxq(e, "at_exp01", bank, h, h, -e->rotation, AOS_BLEND_ADD_ALPHA);
		c->rgba = argb(a, 0xff, a, 0xff);
		break;
	}
	case 600:
		c = fxq(e, "at_impact0", bank, 64.0f * e->scale, 64.0f * e->scale, e->rotation, AOS_BLEND_ALPHA);
		c->rgba = argb(a8(64.0f * e->alpha), 0xff, 0xff, 0xff);
		c = fxq(e, "at_impact2", bank, 240.0f * e->scale, 240.0f * e->scale, e->rotation, AOS_BLEND_ADD_ALPHA);
		c->rgba = argb(a8(255.0f * e->alpha), 0xff, 0xff, 0xff);
		break;
	case 0x172: {
		uint8_t a = a8(255.0f * e->alpha);
		c = fxq(e, "hyper", AOS_BANK_COMMON, e->aux * e->scale, 12.0f, e->rotation, AOS_BLEND_ADD_ALPHA);
		c->hasSrc = 1;
		c->src[0] = 0; c->src[1] = 0; c->src[2] = 0xff; c->src[3] = 0x1d;
		c->rgba = argb(a, a, a, a);
		break;
	}
	case 400: {
		uint32_t a = e->alpha < 0.75f ? (uint32_t)(int32_t)((e->alpha / 0.25f) * 255.0f) : 0xff;
		static const char *const elect[4] = {"at_elect00", "at_elect01", "at_elect02", "at_elect03"}; /* no snprintf per draw */
		c = fxq(e, elect[e->age % 4 < 0 ? 0 : e->age % 4], bank, 24.0f * e->scale, 24.0f * e->scale, e->rotation, AOS_BLEND_ADD_ALPHA);
		c->rgba = argb((uint8_t)(a & 0xff), 0xff, 0xff, 0xff);
		break;
	}
	case 500:
		c = fxq(e, "at_gunfire00", bank, 48.0f * e->scale, 48.0f * e->scale, e->rotation, AOS_BLEND_ALPHA);
		c = fxq(e, "at_gunfire01", bank, 48.0f * e->scale, 48.0f * e->scale, e->rotation, AOS_BLEND_ADD_ALPHA);
		break;
	case 0x262:
	case 0x263: {
		c = fxq(e, "hyper", AOS_BANK_COMMON, e->aux * e->scale * 12.0f, e->id == 0x262 ? 6.0f : 4.0f, e->rotation,
		        AOS_BLEND_ADD_ALPHA);
		c->hasSrc = 1;
		c->src[0] = 0; c->src[1] = 0; c->src[2] = 0xff; c->src[3] = 0x1d;
		c->rgba = aos_colour_rgba(e->variant, e->alpha);
		break;
	}
	case 700: {
		uint8_t a = e->age % 2 == 0 ? a8(255.0f * e->alpha) : a8(192.0f * e->alpha);
		c = fxq(e, "at_exp00", bank, 1600.0f * e->scale, 1600.0f * e->scale, e->rotation, AOS_BLEND_ADD_ALPHA);
		c->rgba = argb(a, 0xff, (uint8_t)(0xff - a / 2), a);
		break;
	}
	case 100:
	case 0x96: {
		/* Light cone: 3-vertex gradient fan through DAT_00893e28 (+0x18 begin, +0x2c tri, +0x1c end),
		 * blend 1. Vertices are WORLD positions (no FUN_00619d90): apex at the effect, far corners at
		 * dist along rotation(+0x38) ± spread. Apex ARGB = alpha | colTab[variant], far corners alpha 0. */
		bool panel = e->id == 100;
		float dist = panel ? 300.0f : 250.0f;                          /* DAT_008145f0 / DAT_008149ac */
		float spread = (panel ? 0.2f : 0.15f) * e->scale;            /* DAT_00853b14 / DAT_00813f20 */
		float pul = aos_sin(((float)(e->age % 50) / 50.0f) * AOS_2PI) * 0.35f + 0.65f; /* DAT_00813f2c, DAT_0081788c */
		float av = 128.0f * e->scale * pul;                           /* DAT_00814134 */
		if (panel)
			av *= e->aux2;
		uint32_t rgb = aos_colour_rgba(e->variant, 0.0f) & 0xffffff00u; /* DAT_0087e138 + variant*0xc */
		float xy[6] = {e->pos[0], e->pos[1],
		               aos_cos(e->rotation + spread) * dist + e->pos[0], aos_sin(e->rotation + spread) * dist + e->pos[1],
		               aos_cos(e->rotation - spread) * dist + e->pos[0], aos_sin(e->rotation - spread) * dist + e->pos[1]};
		uint32_t rgba[3] = {rgb | a8(av), rgb, rgb};
		aos_draw_tri(xy, rgba, AOS_BLEND_ALPHA);
		break;
	}
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */

/* Engine hook (FUN_006b1b40 tail, all.c:356703; player update all.c:380823): a new barrier
 * makes the Searchlight Panel invulnerable; it becomes vulnerable again once the
 * barrier is in its last 20 frames. */
static void barrier_hook(aos_world *w, aos_player *p, int phase)
{
	int i = find_panel(w, p->side);
	if (i >= 0)
		w->obj.enemy[i].invulnerable = phase == 0 ? 1 : 0;
}

void aos_register_alte(void)
{
	aos_tab.barrierHook[4] = barrier_hook;
	aos_tab.inputA[ID] = input_a;
	aos_tab.inputB[ID] = input_b;
	aos_tab.cpu[ID][0] = aos_tab.cpu[ID][1] = aos_tab.cpu[ID][2] = NULL; /* FUN_006a86a0 / 006a7dd0 not ported */

	aos_tab.T2[0x32] = st_neutral;     aos_tab.T1[0x32] = dr_neutral;
	aos_tab.T2[0x33] = st_stub;        aos_tab.T1[0x33] = dr_dash;
	aos_tab.T2[0x34] = st_ko_hook;     aos_tab.T1[0x34] = dr_ko;
	aos_tab.T2[0x35] = st_stub;        aos_tab.T1[0x35] = dr_hit;
	aos_tab.T2[0x36] = st_stub;        aos_tab.T1[0x36] = dr_shield;
	aos_tab.T2[0x3c] = st_smg;         aos_tab.T1[0x3c] = dr_smg;
	aos_tab.T2[0x3d] = st_smg_dash;    aos_tab.T1[0x3d] = dr_smg;
	aos_tab.T2[0x3e] = st_beam_panels; aos_tab.T1[0x3e] = dr_spin;
	aos_tab.T2[0x3f] = st_missile;     aos_tab.T1[0x3f] = dr_spin;
	aos_tab.T2[0x40] = st_salvo;       aos_tab.T1[0x40] = dr_salvo;
	aos_tab.T2[0x41] = st_rod;         aos_tab.T1[0x41] = dr_rod;
	aos_tab.T2[0x42] = st_rod_hold;    aos_tab.T1[0x42] = dr_rod_loop;
	aos_tab.T2[0x44] = st_punisher;    aos_tab.T1[0x44] = dr_punisher;
	aos_tab.T2[0x45] = st_punisher2;   aos_tab.T1[0x45] = dr_punisher;
	aos_tab.T2[0x46] = st_punisher3;   aos_tab.T1[0x46] = dr_punisher;
	aos_tab.T2[0x47] = st_punisher_grab; aos_tab.T1[0x47] = dr_rod_loop;
	aos_tab.T2[0x48] = st_shock_ball;  aos_tab.T1[0x48] = dr_roll;
	aos_tab.T2[0x49] = st_panel;       aos_tab.T1[0x49] = dr_panel;
	aos_tab.T2[0x4b] = st_spark;       aos_tab.T1[0x4b] = dr_spin;
	aos_tab.T2[0x4c] = st_claymore;    aos_tab.T1[0x4c] = dr_shield;
	aos_tab.T2[0x4d] = st_desp;        aos_tab.T1[0x4d] = dr_desp;
	aos_tab.T2[0x4e] = st_desp_grab;   aos_tab.T1[0x4e] = dr_hold;

	aos_otab.beamSpawn[ID] = beam_spawn;
	aos_otab.beamUpdate[ID] = beam_update;
	aos_otab.beamDraw[ID] = beam_draw;
	aos_otab.beamOnHit[ID] = beam_on_hit;
	aos_otab.missileSpawn[ID] = missile_spawn;
	aos_otab.missileUpdate[ID] = missile_update;
	aos_otab.missileDraw[ID] = missile_draw;
	aos_otab.missileOnHit[ID] = missile_on_hit;
	aos_otab.missileOnKilled[ID] = missile_on_killed;
	aos_otab.missileOnAbsorb[ID] = NULL;
	aos_otab.hitboxSpawn[ID] = hitbox_spawn;
	aos_otab.hitboxOnHit[ID] = hitbox_on_hit;
	aos_otab.enemySpawn[ID] = enemy_spawn;
	aos_otab.enemyUpdate[ID] = enemy_update;
	aos_otab.enemyDraw[ID] = enemy_draw;
	aos_otab.enemyOnKilled[ID] = enemy_on_killed;
	aos_otab.effectSpawn[ID] = effect_spawn;
	aos_otab.effectUpdate[ID] = effect_update;
	aos_otab.effectDraw[ID] = effect_draw;
}
