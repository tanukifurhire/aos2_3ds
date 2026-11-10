/* Kyoko (engine id 15, states 0x1d1..0x1e7) — 1:1 port of aos2.exe.
 * Spec: docs/re/kyoko.md. Every handler names its original function.
 * Version branches: only the 0x5a2 (live) branch is implemented. */
#include <string.h>

#include "../draw.h"
#include "../fx.h"
#include "../objects.h"
#include "../sim.h"

#define KY AOS_CH_KYOKO
#define HALF_PI 1.5707964f /* DAT_00814104 */
#define QUARTER_PI 0.7853982f /* DAT_0081fa90 */

void aos_se_cd(aos_world *w, int id, int cd); /* FUN_007bcaf0 (core.c) */

/* ------------------------------------------------------------------ */
/* helpers                                                             */

/* FUN_00780480(team): the opponent of `team`. */
static aos_player *opp_of_team(aos_world *w, int32_t team) { return team == 1 ? &w->p[1] : &w->p[0]; }
/* FUN_00780450(team): the player of `team`. */
static aos_player *player_of_team(aos_world *w, int32_t team) { return team == 1 ? &w->p[0] : &w->p[1]; }


static int fx0(aos_world *w, int32_t id, int32_t variant, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team)
{
	return aos_spawn_effect(w, 0, id, variant, pos, angle, owner, age0, team);
}
static int fx15(aos_world *w, int32_t id, int32_t variant, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team)
{
	return aos_spawn_effect(w, KY, id, variant, pos, angle, owner, age0, team);
}

static float set_aim(aos_world *w, aos_player *p) { return p->faceAngle = aos_aim(w, p); }

/* "Face by x": +0x84 = +0x88 = (opp.x <= x) ? π : 0. */
static void face_by_x(aos_world *w, aos_player *p)
{
	aos_player *o = aos_opponent(w, p);
	p->faceAngle = (o->x <= p->x) ? AOS_PI : 0.0f;
	p->dashAngle = p->faceAngle;
}

/* Standard tail: dash-cancel from `cancel`, state 0 after `end`. */
static void tail(aos_world *w, aos_player *p, int32_t cancel, int32_t end)
{
	if (p->timer < cancel || p->D != 3) {
		if (p->timer > end)
			p->state = AOS_ST_NEUTRAL;
	} else {
		aos_dash(w, p, 1);
	}
}

/* Hyper start: FUN_006b1b40(p,10), SE 6, FUN_004e9280(100,.75), FUN_007bec20(100,pos,slA,slS),
 * FUN_004d7ac0(20,.5,2), optional effect 0xfa (angle 0). */
static void hyper_start(aos_world *w, aos_player *p, float slA, float slS, bool flash)
{
	aos_barrier(w, p, 10);
	aos_hyper_flourish_ex(w, p, 6, 100, 0.75f /* DAT_00813f3c */, 100, slA, slS, 0x14, 0.5f /* DAT_00853b1c */, 2, flash);
}

static void vec(float out[3], const aos_player *p, float a, float d)
{
	out[0] = aos_cos(a) * d + p->x;
	out[1] = aos_sin(a) * d + p->y;
	out[2] = p->z;
}

/* ------------------------------------------------------------------ */
/* input dispatch                                                      */

static bool go(aos_world *w, aos_player *p, int32_t s)
{
	aos_set_state(w, p, s);
	return true;
}

static bool normal_moves(aos_world *w, aos_player *p, bool *done)
{
	*done = false;
	if (p->state != AOS_ST_NEUTRAL && p->state != AOS_ST_DASH)
		return false;
	if (p->D == 3) {
		aos_dash(w, p, 0);
	} else if (p->Shield == 3 && aos_spend_hyper(p, 0.5f) && aos_may_shield(w, p)) {
		aos_shield(w, p);
	} else {
		*done = true;
		if (p->S > 1 && p->A == 3)
			return go(w, p, aos_dist(w, p) >= 200.0f ? 0x1e1 : 0x1e3); /* DAT_008147cc */
		if (p->D > 1 && p->A == 3)
			return go(w, p, 0x1dc);
		if (p->A == 3)
			return go(w, p, 0x1dd);
		if (p->S > 1 && p->B == 3)
			return go(w, p, 0x1e5);
		if (p->B == 3)
			return go(w, p, 0x1df);
		*done = false;
	}
	return false;
}

static bool input_a(aos_world *w, aos_player *p) /* FUN_006ec830 */
{
	if (aos_may_hyper(w, p)) {
		if (p->S > 1 && p->B > 1 && p->H == 3 && aos_spend_hyper(p, 3.0f))
			return go(w, p, 0x1e7);
		if (p->S > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, aos_dist(w, p) >= 360.0f ? 0x1e2 : 0x1e4); /* DAT_00817894 */
		if (p->B > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0x1e0);
		if (p->D > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0x1de);
		if (p->H == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0x1e6);
	}
	bool done;
	bool r = normal_moves(w, p, &done);
	if (done)
		return r;
	return aos_tab.T2[p->state] ? aos_tab.T2[p->state](w, p) : false;
}

static bool input_b(aos_world *w, aos_player *p) /* FUN_006ecdc0 */
{
	if (aos_may_hyper(w, p)) {
		if (p->H > 1 && p->Shield == 3 && aos_spend_hyper(p, 3.0f))
			return go(w, p, 0x1e7);
		if (p->H > 1 && p->S == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, aos_dist(w, p) >= 360.0f ? 0x1e2 : 0x1e4);
		if (p->H > 1 && p->B == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0x1e0);
		if (p->H > 1 && p->D == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0x1de);
		if (p->H > 1 && p->A == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0x1e6);
	}
	bool done;
	bool r = normal_moves(w, p, &done);
	if (done)
		return r;
	return aos_tab.T2[p->state] ? aos_tab.T2[p->state](w, p) : false;
}

/* ------------------------------------------------------------------ */
/* states (T2)                                                         */

static bool st_neutral(aos_world *w, aos_player *p) /* 0x1d1 FUN_006e9bf0 */
{
	(void)w;
	float s = aos_tab.stats[p->id].walk;
	if (p->up > 1) p->y += s;
	if (p->down > 1) p->y -= s;
	if (p->left > 1) p->x -= s;
	if (p->right > 1) p->x += s;
	p->faceAngle = p->facingRight == 1 ? 0.0f : AOS_PI;
	return true;
}

static bool st_stub(aos_world *w, aos_player *p) /* 0x1d2..0x1d5 FUN_006e9ce0/9d10/9cf0/9d00 */
{
	(void)w;
	(void)p;
	return true;
}

static bool st_drizzle(aos_world *w, aos_player *p) /* 0x1db FUN_006e9d20 */
{
	if (p->timer == 0)
		set_aim(w, p);
	if (p->timer > 14 && p->timer < 31 && aos_use_gauge(p, 0x1db)) { /* `t % 1` = every frame */
		float r1 = aos_rand_real(w, -AOS_PI, AOS_PI);
		float a = aos_rand_real(w, -0.3f, 0.3f) + p->faceAngle; /* DAT_0081fa88, DAT_00813f28 */
		float c = aos_cos(r1);
		float d1 = aos_rand_real(w, 30.0f, 150.0f); /* DAT_008140e8, DAT_0081414c */
		float pos[3];
		pos[0] = c * d1 + p->x;
		float s = aos_sin(r1);
		float d2 = aos_rand_real(w, 30.0f, 150.0f);
		pos[1] = s * d2 + p->y;
		pos[2] = p->z; /* original leaves local z unset; z is never read by beam collision/draw (2D), so any value is equivalent */
		aos_spawn_beam(w, KY, 100, pos, a, p->objId, 0, p->side);
		aos_se_cd(w, 0x10, 5);
	}
	tail(w, p, 0x16, 0x39);
	return true;
}

static bool st_hard_rain(aos_world *w, aos_player *p) /* 0x1dd FUN_006ea480 */
{
	if (p->timer == 0)
		set_aim(w, p);
	if (p->timer < 14 && p->A < 2) {
		p->state = 0x1db; /* timer kept */
		aos_tab.T2[0x1db](w, p);
		return true;
	}
	if (p->timer > 10 && p->timer < 25) {
		float a = aos_dir_angle(p);
		if (a <= AOS_2PI)
			aos_angle_toward(&p->faceAngle, a, 0.1f);
	}
	if (p->timer == 15 && aos_use_gauge(p, 0x1dc)) {
		aos_spawn_missile(w, KY, 400, &p->x, p->faceAngle, p->objId, 0, p->side, p->color);
		aos_se_cd(w, 0x10, 5);
	}
	if (p->timer == 20 && p->A > 1 && aos_use_gauge(p, 0x1dd)) {
		aos_spawn_missile(w, KY, 400, &p->x, p->faceAngle, p->objId, 0, p->side, p->color);
		aos_se_cd(w, 0x10, 5);
	}
	if (p->timer == 25 && p->A > 1 && aos_use_gauge(p, 0x1dd)) {
		aos_spawn_missile(w, KY, 400, &p->x, p->faceAngle, p->objId, 0, p->side, p->color);
		aos_se_cd(w, 0x10, 5);
	}
	tail(w, p, 0x11, 0x25);
	return true;
}

static bool st_clean_stream(aos_world *w, aos_player *p) /* 0x1dc FUN_006ea9a0 */
{
	if (p->timer == 0)
		set_aim(w, p);
	if (p->timer == 15 && aos_use_gauge(p, 0x1dc)) {
		set_aim(w, p);
		aos_spawn_beam(w, KY, 200, &p->x, p->faceAngle - 0.8f, p->objId, 0, p->side); /* DAT_008145e8 */
		aos_spawn_beam(w, KY, 200, &p->x, p->faceAngle + 0.8f, p->objId, 0, p->side);
		aos_se(w, 0xe);
	}
	if (p->timer == 17 && aos_use_gauge(p, 0x1dc)) {
		set_aim(w, p);
		aos_spawn_beam(w, KY, 200, &p->x, p->faceAngle - 1.2f, p->objId, 0, p->side); /* DAT_00817d78 */
		aos_spawn_beam(w, KY, 200, &p->x, p->faceAngle + 1.2f, p->objId, 0, p->side);
		aos_se(w, 0xe);
	}
	tail(w, p, 0x14, 0x1e);
	return true;
}

static bool st_freezing(aos_world *w, aos_player *p) /* 0x1de FUN_006ea7c0 */
{
	if (p->timer == 0) {
		set_aim(w, p);
		hyper_start(w, p, 0.0f, 0.0f, true);
	}
	if (p->timer == 20) {
		for (int k = -3; k < 3; k++)
			aos_spawn_missile(w, KY, 400, &p->x, ((float)k / 3.0f) * AOS_PI + p->faceAngle, p->objId, 0, p->side, p->color);
		aos_se_cd(w, 0x10, 5);
	}
	tail(w, p, 0x1e, 0x32);
	return true;
}

static bool st_icy_crystal(aos_world *w, aos_player *p) /* 0x1df FUN_006eac00 */
{
	if (p->timer == 0)
		face_by_x(w, p);
	if (p->timer > 4 && p->timer < 0x2a && (p->timer - 5) % 6 == 0 && aos_use_gauge(p, 0x1df)) {
		float ra = aos_rand_real(w, -AOS_PI, AOS_PI);
		float rd = aos_rand_real(w, 50.0f, 150.0f); /* DAT_008140ec, DAT_0081414c */
		(void)ra; (void)rd;                         /* old-version point (overwritten for 0x5a2) */
		set_aim(w, p);
		float off, dist;
		switch (p->timer) {
		case 5: off = 0.5f; dist = 150.0f; break;
		case 0xb: off = -0.5f; dist = 129.0f; break;
		case 0x11: off = 0.5f; dist = 108.0f; break;
		case 0x17: off = -0.5f; dist = 87.0f; break;
		case 0x1d: off = 0.5f; dist = 66.0f; break;
		case 0x23: off = -0.5f; dist = 45.0f; break;
		default: off = 0.5f; dist = 24.0f; break;
		}
		float a = off + p->faceAngle;
		aos_angle_normalize(&a);
		float pt[3];
		vec(pt, p, a, dist);
		aos_spawn_missile(w, KY, 100, pt, 0.0f, p->objId, 0, p->side, p->color);
		fx0(w, 0x514, aos_team_colour(w, p->side, 0), pt, 0.0f, p->objId, 0, p->side);
		aos_se(w, 0xb);
	}
	if (p->timer > 12 && p->timer < 60 && p->B < 2)
		p->timer = 60;
	tail(w, p, 0x41, 0x50);
	return true;
}

static bool st_ice_over(aos_world *w, aos_player *p) /* 0x1e0 FUN_006eb0b0 */
{
	if (p->timer == 0) {
		face_by_x(w, p);
		hyper_start(w, p, 0.0f, 0.0f, true);
	}
	if (p->timer > 11 && p->timer < 0x33 && p->timer % 3 == 0) {
		float ra = aos_rand_real(w, -AOS_PI, AOS_PI);
		float rd = aos_rand_real(w, 80.0f, 200.0f); /* DAT_00814164, DAT_008147cc */
		float pt[3];
		vec(pt, p, ra, rd);
		aos_spawn_missile(w, KY, 100, pt, 0.0f, p->objId, 0, p->side, p->color);
		fx0(w, 0x514, aos_team_colour(w, p->side, 0), pt, 0.0f, p->objId, 0, p->side);
		aos_se_cd(w, 0xb, 6);
	}
	tail(w, p, 0x46, 0x50);
	return true;
}

static bool st_ripple(aos_world *w, aos_player *p) /* 0x1e1 FUN_006eb3d0 */
{
	if (p->timer == 0)
		set_aim(w, p);
	if (p->timer == 10 && aos_use_gauge(p, 0x1e1)) {
		static const float off[3][2] = {{40.0f, 50.0f}, {40.0f, -50.0f}, {50.0f, 0.0f}};
		for (int i = 0; i < 3; i++) {
			float v[2] = {off[i][0], off[i][1]};
			aos_vec_rotate(v, p->faceAngle);
			float pt[3] = {v[0] + p->x, v[1] + p->y, p->z};
			aos_spawn_missile(w, KY, 300, pt, p->faceAngle, p->objId, 0, p->side, p->color);
		}
		aos_se_cd(w, 0xb, 6);
	}
	tail(w, p, 0x14, 0x1e);
	return true;
}

static bool st_water_mirror(aos_world *w, aos_player *p) /* 0x1e2 FUN_006eb6b0 */
{
	if (p->timer == 0) {
		face_by_x(w, p);
		hyper_start(w, p, 0.0f, 0.0f, true);
	}
	if (p->timer == 20) {
		for (int k = -6; k < 6; k++)
			aos_spawn_missile(w, KY, 0x136, &p->x, ((float)k / 6.0f) * AOS_PI, p->objId, 0, p->side, p->color);
		aos_se(w, 0xb);
	}
	tail(w, p, 0x1e, 0x28);
	return true;
}

static bool st_icicle(aos_world *w, aos_player *p) /* 0x1e3 FUN_006ebbc0 */
{
	if (p->timer == 0)
		set_aim(w, p);
	if (p->timer > 7 && p->timer < 0x19 && p->timer % 4 == 0) {
		float a = aos_rand_real(w, -0.5f, 0.5f) + p->faceAngle; /* overwritten for 0x5a2 */
		set_aim(w, p);
		switch (p->timer) {
		case 8: a = p->faceAngle - 0.5f; break;
		case 0xc: a = p->faceAngle + 0.5f; break;
		case 0x10: a = p->faceAngle - 0.25f; break;
		case 0x14: a = p->faceAngle + 0.25f; break;
		default: a = p->faceAngle; break;
		}
		if (p->timer == 0x18)
			a = p->faceAngle;
		aos_spawn_missile(w, KY, 500, &p->x, a, p->objId, 0, p->side, p->color);
		aos_se(w, 0xb);
		float pt[3];
		vec(pt, p, a, 80.0f); /* DAT_00814164 */
		int h = aos_spawn_hitbox(w, KY, 200, pt, a, p->objId, 0, p->side);
		if (p->timer == 0x18 && h >= 0)
			w->obj.hitbox[h].knockback = 15.0f; /* DAT_00853b88 */
	}
	tail(w, p, 0x27, 0x31);
	return true;
}

static bool st_tree_of_ice(aos_world *w, aos_player *p) /* 0x1e4 FUN_006ebf50 */
{
	if (p->timer == 0) {
		set_aim(w, p);
		hyper_start(w, p, p->faceAngle, QUARTER_PI, true);
	}
	if (p->timer > 7 && p->timer < 0x29 && p->timer % 4 == 0) {
		float a;
		if (p->timer % 8 == 0)
			a = aos_rand_real(w, -0.1f, 0.35f) + p->faceAngle; /* DAT_00814130, DAT_00813f2c */
		else
			a = p->faceAngle - aos_rand_real(w, -0.1f, 0.35f);
		float pt[3];
		vec(pt, p, a, 40.0f); /* DAT_008140e0 */
		aos_spawn_missile(w, KY, 0x226, pt, a, p->objId, 0, p->side, p->color);
		aos_se(w, 0xb);
		vec(pt, p, a, 140.0f); /* DAT_00817004 */
		int h = aos_spawn_hitbox(w, KY, 0xfa, pt, a, p->objId, 0, p->side);
		if (p->timer == 0x28 && h >= 0)
			w->obj.hitbox[h].knockback = 15.0f;
	}
	tail(w, p, 0x32, 0x3c);
	return true;
}

static bool st_crystal(aos_world *w, aos_player *p) /* 0x1e5 FUN_006eb8d0 */
{
	if (p->timer == 0)
		face_by_x(w, p);
	if (p->timer == 10 && aos_use_gauge(p, 0x1e5)) {
		aos_rand_real(w, -AOS_PI, AOS_PI); /* old-version offset angle (overwritten for 0x5a2) */
		int i = aos_spawn_enemy(w, KY, 100, p->objId, p->side, p->color);
		if (i >= 0) {
			float dx = 0.0f, dy = 0.0f;
			if (p->up > 1) dy = 70.0f;     /* DAT_008145ec */
			if (p->down > 1) dy = -70.0f;  /* DAT_008256c8 */
			if (p->right > 1) dx = 70.0f;
			if (p->left > 1) dx = -70.0f;
			aos_enemy *e = &w->obj.enemy[i];
			e->pos[0] = p->x + dx;
			e->pos[1] = p->y + dy;
			e->angle = p->faceAngle;
		}
		aos_se(w, 0xb);
	}
	tail(w, p, 0xf, 0x1e);
	return true;
}

static bool st_early_summer_rain(aos_world *w, aos_player *p) /* 0x1e6 FUN_006e9fc0 */
{
	if (p->timer == 0) {
		set_aim(w, p);
		hyper_start(w, p, p->faceAngle, QUARTER_PI, false); /* no effect 0xfa */
	}
	if (p->timer > 3 && p->timer < 0x19 && p->timer % 2 == 0) {
		float v[2];
		v[0] = aos_rand_real(w, 30.0f, 90.0f);    /* DAT_008140e8, DAT_00814170 */
		v[1] = aos_rand_real(w, -120.0f, 120.0f); /* DAT_0081525c, DAT_00814174 */
		aos_vec_rotate(v, p->faceAngle);
		float pt[3] = {v[0] + p->x, v[1] + p->y, p->z};
		aos_spawn_missile(w, KY, 300, pt, p->faceAngle, p->objId, 0, p->side, p->color);
		aos_se_cd(w, 0xb, 4);
	}
	if (p->timer >= 0x14 && p->timer <= 0x32 && p->timer % 2 == 0) {
		float la = aos_rand_real(w, -0.4f, 0.4f) + p->faceAngle; /* DAT_008147f0, DAT_00813f30 */
		float a = aos_aim(w, p);                                   /* not stored */
		float q = aos_rand_real(w, -HALF_PI, HALF_PI);
		float v[2] = {aos_rand_real(w, 60.0f, 100.0f), 0.0f}; /* DAT_00813a58, DAT_00853b90 */
		aos_vec_rotate(v, q + a);
		float o[3];
		o[0] = aos_cos(a) * 10.0f + p->x + v[0]; /* DAT_00813f4c */
		o[1] = aos_sin(a) * 10.0f + p->y + v[1];
		o[2] = p->z;
		aos_spawn_laser(w, KY, 100, o, la, p->objId, p->side, p->color);
		aos_se_cd(w, 0x12, 4);
	}
	tail(w, p, 0x1e, 0x32);
	return true;
}

static bool st_avalanche(aos_world *w, aos_player *p) /* 0x1e7 FUN_006ec2f0 */
{
	if (p->timer == 0) {
		face_by_x(w, p);
		aos_speedline(w, 100, &p->x, p->faceAngle != 0.0f ? -0.75f * AOS_PI /* DAT_0081fa9c */ : -QUARTER_PI /* DAT_0081fa98 */,
		              QUARTER_PI);
		aos_barrier(w, p, 0x14);
		aos_se(w, 7);
		aos_screen_dark(w, 0x96, 0.85f);     /* DAT_00814120 */
		aos_screen_blur(w, 0x96, 0.6f, 2);   /* DAT_00813f34 */
		fx0(w, 0xfa, 0, &p->x, 0.0f, p->objId, 0, p->side);
	}
	if (p->timer >= 0 && p->timer < 0x47 && p->timer % 2 == 0) {
		aos_player *o = opp_of_team(w, p->side);
		bool left = p->faceAngle >= HALF_PI;
		float s = left ? -QUARTER_PI : QUARTER_PI; /* DAT_0081fa98 / DAT_0081fa90 */
		float base[2] = {600.0f, 0.0f};            /* DAT_008149b8 */
		aos_vec_rotate(base, HALF_PI + s);
		base[0] += o->x;
		base[1] += o->y;
		float v[2];
		v[0] = aos_rand_real(w, -300.0f, 300.0f); /* DAT_00815f44, DAT_008145f0 */
		v[1] = aos_rand_real(w, -200.0f, 100.0f); /* DAT_00814600, DAT_00853b90 */
		aos_vec_rotate(v, s);
		float pt[3] = {v[0] + base[0], v[1] + base[1], 0.0f};
		float ang;
		if (left) {
			s = -HALF_PI + s;
			ang = s + aos_rand_real(w, -0.2f, 0.2f); /* DAT_00817c04, DAT_00853b14 */
		} else {
			s = -s;
			ang = s + aos_rand_real(w, -0.2f, 0.2f);
		}
		int k = (p->timer / 2) % 3;
		aos_spawn_missile(w, KY, k == 0 ? 0x96 : k == 1 ? 0x97 : 0x98, pt, ang, p->objId, 0, p->side, p->color);
		fx0(w, 0x514, aos_team_colour(w, p->side, 0), pt, 0.0f, p->objId, 0, p->side);
		aos_se_cd(w, 0xb, 6);
	}
	tail(w, p, 0x5a, 100);
	return true;
}

/* ------------------------------------------------------------------ */
/* player draw (T1)                                                    */

static int pp(int32_t t, int n)
{
	int f = (t / n) & 3;
	return f == 3 ? 1 : f;
}
static int imin(int a, int b) { return a < b ? a : b; }

/* AIM transform (kyoko.md §4.2): rotZ(aim) [, rotX(180°) if |aim| ≥ π/2], rotY(180°).
 * rotX(180°) is approximated as y-scale × cos(180°) = −1 (PORTING.md). */
static aos_dcmd *body(aos_world *w, aos_player *p, const char *fmt, int frame)
{
	aos_dcmd *c = aos_draw(aos_texf(fmt, frame), aos_pidx(w, p), p->x, p->y, 0);
	c->halfW = c->halfH = 48.0f; /* 96×96 quad (DAT_00814118) */
	c->rotZ = aos_rad2bam(p->faceAngle);
	c->flipX = 1; /* RotY(180°) always applied (art faces −x) */
	if (aos_fabs(p->faceAngle) >= HALF_PI)
		c->sy = -1.0f;
	c->blend = AOS_BLEND_ALPHA;
	return c;
}

static void dr_neutral(aos_world *w, aos_player *p) /* 0x1d1 FUN_006ee3b0 */
{
	const char *fmt = "ky_std0%d";
	if (p->up > 1) fmt = "ky_fwd0%d";
	if (p->down > 1) fmt = "ky_bck0%d";
	if (p->facingRight == 0 && p->left > 1) fmt = "ky_fwd0%d";
	if (p->facingRight == 1 && p->left > 1) fmt = "ky_bck0%d";
	if (p->facingRight == 0 && p->right > 1) fmt = "ky_bck0%d";
	if (p->facingRight == 1 && p->right > 1) fmt = "ky_fwd0%d";
	aos_dcmd *c = aos_draw(aos_texf(fmt, pp(p->timer, 5)), aos_pidx(w, p), p->x, p->y, 0);
	c->halfW = c->halfH = 48.0f;
	c->flipX = p->facingRight != 0; /* RotY(π) when +0x8c != 0 */
	int32_t rot[3] = {0, p->facingRight ? aos_deg2bam(0xb4) : 0, 0};
	aos_afterimage(w, c, 1, p, rot); /* FUN_004d7e00 */
}

static void dr_dash(aos_world *w, aos_player *p) /* 0x1d2 FUN_006ee670 */
{
	int f = pp(p->timer, 3);
	float fa = aos_fabs(p->faceAngle);
	if ((p->facingRight == 1 && fa > HALF_PI) || (p->facingRight == 0 && fa < HALF_PI))
		f += 3;
	aos_dcmd *c = body(w, p, "ky_dsh0%d", f);
	int32_t rot[3] = {aos_fabs(p->faceAngle) >= HALF_PI ? aos_deg2bam(0xb4) : 0, aos_deg2bam(0xb4),
	                  aos_rad2bam(p->faceAngle)};
	aos_afterimage(w, c, 1, p, rot); /* FUN_004d7e00 */
}

static void dr_ko(aos_world *w, aos_player *p) /* 0x1d3 FUN_006ef020 */
{
	if (p->timer >= 100)
		return;
	int f = pp(p->timer, 3);
	if (w->frame % 2 == 0)
		f += 3;
	body(w, p, "ky_los0%d", f);
}

static void dr_hit(aos_world *w, aos_player *p) /* 0x1d4 FUN_006eea70 */
{
	body(w, p, "ky_los0%d", pp(p->timer, 3));
}

static void dr_shield(aos_world *w, aos_player *p) /* 0x1d5 FUN_006eed40 */
{
	/* literal: min(.., 6) — frame 6 does not exist; unreachable since shield states end by t=29 */
	body(w, p, "ky_sld0%d", p->timer < 10 ? 0 : imin((p->timer - 10) / 4, 6));
}

static void dr_atka(aos_world *w, aos_player *p) /* 0x1db/1dc/1dd/1e1/1e3/1e6 */
{
	body(w, p, "ky_atka0%d", imin(p->timer / 4, 4));
}
static void dr_atkb(aos_world *w, aos_player *p) /* 0x1df/0x1e0 */
{
	body(w, p, "ky_atkb0%d", imin(p->timer / 4, 4));
}
static void dr_sld10(aos_world *w, aos_player *p) /* 0x1de/0x1e2 */
{
	body(w, p, "ky_sld0%d", p->timer < 10 ? 0 : imin((p->timer - 10) / 4, 5));
}
static void dr_sld4(aos_world *w, aos_player *p) /* 0x1e4/0x1e7 */
{
	body(w, p, "ky_sld0%d", imin(p->timer / 4, 5));
}
static void dr_sld3(aos_world *w, aos_player *p) /* 0x1e5 FUN_006f0c20 */
{
	body(w, p, "ky_sld0%d", imin(p->timer / 3, 5));
}

/* ------------------------------------------------------------------ */
/* beams (FUN_004ad0f0 / 4ad5d0 / 4ad2f0 / 4ad310)                     */

static int beam_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team)
{
	if (id != 100 && id != 200 && id != 300)
		return -1;
	int i = aos_beam_new(w, KY, id, pos, angle, owner, age0, team);
	if (i < 0)
		return -1;
	aos_beam *b = &w->obj.beam[i];
	b->knockback = 10.0f; /* DAT_00813f4c */
	if (id == 100) {
		b->speed = -8.0f; /* DAT_0081410c */
		b->damage = 0x78;
		b->shape.halfW = 30.0f; /* DAT_008140e8 */
		b->shape.halfH = 8.0f;  /* DAT_008140d4 */
	} else if (id == 200) {
		b->speed = 30.0f;
		b->f6c = 0.0f; /* "redirected" flag */
		b->damage = 0xb4;
		b->shape.halfW = 24.0f; /* DAT_00853b8c */
		b->shape.halfH = 10.0f;
	} else {
		b->speed = 16.0f; /* DAT_008140d8 */
		b->damage = 100;
		b->shape.halfW = 20.0f; /* DAT_008140fc */
		b->shape.halfH = 6.0f;  /* DAT_00813f44 */
	}
	return i;
}

static void beam_move(aos_beam *b)
{
	b->pos[0] = aos_cos(b->moveAngle) * b->speed + b->pos[0];
	b->pos[1] = aos_sin(b->moveAngle) * b->speed + b->pos[1];
}

static void beam_home(aos_world *w, aos_beam *b)
{
	aos_player *o = opp_of_team(w, b->team);
	float t = aos_atan2(o->y - b->pos[1], o->x - b->pos[0]);
	aos_angle_toward(&b->moveAngle, t, (50.0f - (float)b->age) * 0.003f); /* DAT_008140ec, DAT_00813ea0 */
	b->angle = b->moveAngle;
}

static bool beam_update(aos_world *w, aos_beam *b)
{
	int32_t col;
	if (b->id == 100) {
		if (b->speed < 20.0f)
			b->speed += 2.0f;
		beam_move(b);
		if (b->age > 0x13 && !aos_in_arena(b->pos, 10.0f))
			return false;
		col = aos_team_colour(w, b->team, 0);
		fx15(w, 0x136, col, b->pos, b->angle, b->ownerId, 0, b->team);
		if (b->age == 1)
			fx0(w, 0x547, col, b->pos, b->angle, b->ownerId, 0, b->team);
	} else if (b->id == 200) {
		if (b->age < 0x14) {
			if (b->speed > 2.0f)
				b->speed -= 2.0f;
		} else {
			if (b->speed < 25.0f) /* DAT_008140f0 */
				b->speed += 1.5f; /* DAT_00814110 */
			if (b->age < 0x28)
				beam_home(w, b);
		}
		beam_move(b);
		if (b->age > 0x31 && !aos_in_arena(b->pos, 20.0f))
			return false;
		col = aos_team_colour(w, b->team, 0);
		fx15(w, 300, col, b->pos, b->angle, b->ownerId, 0, b->team);
		aos_spawn_particle(w, 0x456, col, b->pos, b->angle + AOS_PI, b->ownerId, 0, 1);
		if (b->age == 3)
			fx0(w, 0x547, col, b->pos, b->angle, b->ownerId, 0, b->team);
	} else if (b->id == 300) {
		if (b->age < 0x14) {
			if (b->speed > 2.0f)
				b->speed -= 1.0f;
		} else {
			if (b->speed < 12.0f)
				b->speed += 2.0f;
			if (b->age < 0x32)
				beam_home(w, b);
		}
		beam_move(b);
		if (b->age > 0x31 && !aos_in_arena(b->pos, 10.0f))
			return false;
		col = aos_team_colour(w, b->team, 0);
		fx15(w, 0x140, col, b->pos, b->angle, b->ownerId, 0, b->team);
		if (b->age == 1)
			fx0(w, 0x547, col, b->pos, b->angle, b->ownerId, 0, b->team);
	}
	return true;
}

static void beam_draw(aos_world *w, aos_beam *b) /* FUN_004ad2f0: empty (visuals are effects) */
{
	(void)w;
	(void)b;
}

static void beam_on_hit(aos_world *w, aos_beam *b) /* FUN_004ad310 */
{
	int32_t col = aos_team_colour(w, b->team, 0);
	if (b->id == 100 || b->id == 300) {
		fx0(w, 0x3f2, col, b->pos, b->angle + AOS_PI, b->ownerId, 0, b->team);
		aos_spawn_particle(w, 1000, col, b->pos, b->angle, b->ownerId, 0, 3);
		aos_spawn_spark(w, 1000, col, 4, 4, b->angle, b->pos, b->ownerId);
		aos_se(w, 0x13);
	} else if (b->id == 200) {
		fx0(w, 1000, col, b->pos, b->angle + AOS_PI, b->ownerId, 0, b->team);
		aos_spawn_particle(w, 1000, col, b->pos, b->angle, b->ownerId, 0, 10);
		aos_spawn_spark(w, 1000, col, 10, 10, b->angle, b->pos, b->ownerId);
		aos_se(w, 0x13);
	}
}

/* ------------------------------------------------------------------ */
/* missiles (FUN_00669960 / 66d200 / 669f40 / 66caf0 / 66d070 / 66d190) */

static void mshape(aos_shape *s, float hw, float hh, float r)
{
	s->halfW = hw;
	s->halfH = hh;
	s->radius = r;
}

static int missile_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team, int32_t p18)
{
	switch (id) {
	case 100: case 0x96: case 0x97: case 0x98: case 200: case 300: case 0x136:
	case 400: case 0x1c2: case 500: case 0x226:
		break;
	default:
		return -1;
	}
	int i = aos_missile_new(w, KY, id, pos, angle, owner, age0, team, p18);
	if (i < 0)
		return -1;
	aos_missile *m = &w->obj.missile[i];
	switch (id) {
	case 100:
		m->f64 = 100;
		m->growScale = 0;
		m->hp = 300;
		m->active = 0;
		mshape(&m->shapeBody, 0, 0, 20.0f); /* DAT_008140fc */
		mshape(&m->shapeProj, 0, 0, 20.0f);
		m->damage = 200;
		m->knockback = 5.0f; /* DAT_00813f40 */
		break;
	case 0x96: case 0x97: case 0x98:
		m->angle = m->moveAngle + HALF_PI;
		m->f64 = 100;
		m->growScale = 0;
		m->f60 = 1.0f;
		m->hp = 0x168;
		m->active = 0;
		mshape(&m->shapeBody, 0, 0, 60.0f); /* DAT_00813a58 */
		mshape(&m->shapeProj, 0, 0, 60.0f);
		m->damage = 0xb4;
		m->knockback = 5.0f;
		break;
	case 200:
		m->growScale = 0;
		m->hp = 300;
		m->active = 0;
		m->shapeBody.radius = 0;
		m->shapeProj.radius = 30.0f; /* DAT_008140e8 */
		break;
	case 300: case 0x136: /* FUN_00669960: id 300 -> jz 0x669bab; jump table 0x669e28[0] (id 0x136) -> 0x669bab (shared init, verified) */
		m->growScale = 0;
		m->speed = 0;
		m->absorbs = 1;
		m->hp = 300;
		m->active = 0;
		mshape(&m->shapeBody, 20.0f, 40.0f, 0); /* DAT_008140fc, DAT_008140e0 */
		mshape(&m->shapeProj, 20.0f, 40.0f, 0);
		break;
	case 400:
		m->speed = 14.0f; /* DAT_00813a54 */
		m->hp = 10;
		m->damage = 100;
		m->active = 1;
		m->shapeBody.radius = 30.0f;
		m->shapeProj.radius = 30.0f;
		m->knockback = 10.0f;
		break;
	case 0x1c2:
		m->speed = 0;
		m->f60 = 1.0f;
		m->growScale = 0;
		m->hp = 300;
		m->active = 0;
		m->shapeBody.radius = 40.0f;
		m->shapeProj.radius = 40.0f;
		break;
	case 500:
		m->speed = 10.0f;
		m->growScale = 0;
		m->hp = 999;
		m->active = 0;
		break;
	case 0x226:
		m->speed = 16.0f;
		m->growScale = 0;
		m->hp = 999;
		m->active = 0;
		break;
	}
	return i;
}

static void m_move(aos_missile *m)
{
	m->pos[0] = aos_cos(m->moveAngle) * m->speed + m->pos[0];
	m->pos[1] = aos_sin(m->moveAngle) * m->speed + m->pos[1];
}

static bool missile_on_hit(aos_world *w, aos_missile *m);

/* Beam-200 redirect used by missile 200 and the Crystal enemy. */
static bool redirect_beams(aos_world *w, const float pos[3], int32_t owner, int32_t team, float r, bool fxse)
{
	bool any = false;
	aos_shape s = {0, 0, r};
	for (int i = 0; i < w->obj.nbeam; i++) {
		aos_beam *b = &w->obj.beam[i];
		if (b->ownerId != owner || b->type != KY || b->id != 200 || b->f6c != 0.0f)
			continue;
		if (!aos_shape_hit(pos, b->pos, &s, &s, 0, 0))
			continue;
		any = true;
		aos_player *o = opp_of_team(w, team);
		b->moveAngle = aos_atan2(o->y - b->pos[1], o->x - b->pos[0]);
		b->angle = b->moveAngle;
		b->f6c = 1.0f;
		b->age = 0x28;
		if (b->speed < 12.0f)
			b->speed = 12.0f;
		if (fxse) {
			fx0(w, 0x515, aos_team_colour(w, team, 0), b->pos, 0.0f, owner, 0, team);
			aos_se(w, 0x25);
		}
	}
	return any;
}

static bool missile_update(aos_world *w, aos_missile *m)
{
	switch (m->id) {
	case 300:
		if (m->age < 100) {
			m->growScale += 0.1f;
			if (m->growScale > 1.0f) {
				m->growScale = 1.0f;
				m->active = 1;
			}
		} else {
			m->growScale -= 0.05f;
			m->active = 0;
			if (m->growScale <= 0.0f)
				return false;
		}
		break;
	case 100:
		if (m->f64 == 100) {
			m->growScale += 0.1f;
			if (m->growScale > 1.0f) {
				m->growScale = 1.0f;
				m->active = 1;
			}
			if (m->age > 0x95) {
				m->f64 = 200;
				m->speed = 0;
				aos_player *o = opp_of_team(w, m->team);
				m->moveAngle = aos_atan2(o->y - m->pos[1], o->x - m->pos[0]);
			}
		} else if (m->f64 == 200) {
			if (m->speed < 12.0f)
				m->speed += 0.5f;
			m_move(m);
			if (m->speed >= 12.0f && !aos_in_arena(m->pos, 20.0f)) {
				missile_on_hit(w, m); /* (*0x881aa8[type])(m) */
				return false;
			}
		} else {
			m->growScale -= 0.05f;
			if (m->growScale <= 0.0f)
				return false;
		}
		break;
	case 0x96: case 0x97: case 0x98:
		if (m->f64 == 100) {
			m->growScale += 0.1f;
			if (m->growScale > 1.0f) {
				m->growScale = 1.0f;
				m->active = 1;
				m->f64 = 200;
			}
		} else if (m->f64 == 200) {
			if (m->speed < 15.0f)
				m->speed += 0.5f;
			if (m->age > 0x78 && m->speed >= 15.0f && !aos_in_arena(m->pos, 50.0f)) {
				m->speed = 0;
				m->active = 0;
				m->f64 = 900;
			}
			if (m->age % 5 == 0) {
				float a = aos_rand_real(w, -AOS_PI, AOS_PI);
				fx15(w, 0xc9, 0, m->pos, a, m->ownerId, 0, m->team);
			}
		} else {
			if (m->speed < 15.0f)
				m->speed += 0.5f;
			m->f60 -= 0.05f;
			if (m->f60 <= 0.0f)
				return false;
		}
		m_move(m);
		break;
	case 200:
		m->growScale += 0.1f;
		if (m->growScale > 1.0f) {
			m->growScale = 1.0f;
			m->active = 1;
		}
		if (m->f74 > 0.0f && m->age % 10 == 0)
			fx0(w, 0x515, aos_team_colour(w, m->team, 0), m->pos, 0.0f, m->ownerId, 0, m->team);
		if (m->active == 1 && redirect_beams(w, m->pos, m->ownerId, m->team, 20.0f, false))
			m->f74 = 1.0f;
		break;
	case 0x136: {
		if (m->age < 100) {
			m->growScale += 0.05f;
			if (m->growScale > 1.0f) {
				m->growScale = 1.0f;
				m->active = 1;
			}
		}
		if (m->speed < 100.0f)
			m->speed += 5.0f;
		m->moveAngle += 0.075f;
		if (m->moveAngle > AOS_PI)
			m->moveAngle -= AOS_2PI;
		m->angle = m->moveAngle;
		aos_player *ow = aos_player_by_objid(w, m->ownerId);
		if (ow) {
			m->pos[0] = aos_cos(m->moveAngle) * m->speed + ow->x;
			m->pos[1] = aos_sin(m->moveAngle) * m->speed + ow->y;
		}
		if (m->age > 0x207) {
			m->growScale -= 0.05f;
			m->active = 0;
			if (m->growScale <= 0.0f)
				return false;
		}
		break;
	}
	case 400:
		if (m->age % 2 == 0) {
			float a = aos_rand_real(w, -AOS_PI, AOS_PI);
			fx15(w, 200, 0, m->pos, a, m->ownerId, 0, m->team);
		}
		if (m->speed > 6.0f)
			m->speed -= 0.25f;
		m_move(m);
		if (!aos_in_arena(m->pos, 0.0f)) {
			missile_on_hit(w, m);
			return true;
		}
		break;
	case 0x1c2:
		if (m->f64 == 0) {
			m->growScale += 0.1f;
			m->speed += 2.5f;
			if (m->growScale >= 1.0f) {
				m->growScale = 1.0f;
				m->active = 1;
				m->f64 = 100;
			}
		} else if (m->f64 == 100) {
			if (m->age % 10 == 0) {
				float a = aos_rand_real(w, -AOS_PI, AOS_PI);
				fx15(w, 200, 0, m->pos, a, m->ownerId, 0, m->team);
			}
			if (m->age > 800) {
				m->active = 0;
				m->f64 = 900;
			}
		} else {
			m->growScale -= 0.1f;
			m->f60 -= 0.1f;
			m->speed += 2.5f;
			int32_t col = aos_team_colour(w, m->team, 0);
			for (int k = 0; k < 6; k++) {
				float a = ((float)k / 6.0f) * AOS_2PI - m->angle;
				float q[3] = {aos_cos(a) * m->speed + m->pos[0], aos_sin(a) * m->speed + m->pos[1],
				              0.0f}; /* z: never written in the original (local_1c) */
				aos_spawn_particle(w, 0x456, col, q, a, m->ownerId, 0, 1);
			}
			if (m->growScale <= 0.0f)
				return false;
		}
		if (m->age % 4 == 0)
			m->f74 = aos_rand_real(w, 0.5f, 0.75f);
		{
			aos_player *o = opp_of_team(w, m->team);
			m->moveAngle = aos_atan2(o->y - m->pos[1], o->x - m->pos[0]);
		}
		break;
	case 500:
		if (m->age < 10) {
			m->growScale += 0.2f;
			if (m->growScale > 1.0f)
				m->growScale = 1.0f;
		} else {
			aos_spawn_particle(w, 0x456, aos_team_colour(w, m->team, 0), m->pos, m->angle, m->ownerId, 0, 1);
			m->growScale -= 0.2f;
		}
		if (m->age == 3)
			fx0(w, 0x547, aos_team_colour(w, m->team, 0), m->pos, m->angle, m->ownerId, 0, m->team);
		m_move(m);
		if (m->age > 10 && m->growScale <= 0.0f)
			return false;
		break;
	case 0x226:
		if (m->age < 10) {
			m->growScale += 0.2f;
			if (m->growScale > 1.0f)
				m->growScale = 1.0f;
		} else {
			aos_spawn_particle(w, 0x456, aos_team_colour(w, m->team, 0), m->pos, m->angle, m->ownerId, 0, 1);
			m->growScale -= 0.1f;
			m->speed = 20.0f;
		}
		if (m->age == 1)
			fx0(w, 0x546, aos_team_colour(w, m->team, 0), m->pos, m->angle, m->ownerId, 0, m->team);
		m_move(m);
		if (m->age > 10 && m->growScale <= 0.0f)
			return false;
		break;
	default:
		break;
	}
	return true;
}

static void streaks6(aos_world *w, aos_missile *m)
{
	int32_t col = aos_team_colour(w, m->team, 0);
	for (int k = -3; k < 3; k++) {
		float a = ((float)k / 3.0f) * AOS_PI;
		a += aos_rand_real(w, -0.25f, 0.25f); /* DAT_0081411c, DAT_00813f24 */
		fx0(w, 0x456, col, m->pos, a, m->ownerId, 0, m->team);
	}
}

static bool missile_on_hit(aos_world *w, aos_missile *m) /* FUN_0066caf0 */
{
	int32_t col = aos_team_colour(w, m->team, 0);
	switch (m->id) {
	case 100:
		streaks6(w, m);
		fx0(w, 0x516, col, m->pos, 0.0f, m->ownerId, 0, m->team);
		aos_se(w, 0x13);
		return false;
	case 0x96: case 0x97: case 0x98:
		m->f64 = 900;
		m->speed = 0;
		m->active = 0;
		streaks6(w, m);
		aos_se(w, 0x13);
		return true;
	case 300: case 0x136:
		streaks6(w, m);
		fx0(w, 0x515, col, m->pos, 0.0f, m->ownerId, 0, m->team);
		aos_se(w, 0x13);
		return false;
	case 400:
		fx0(w, 0x514, col, m->pos, 0.0f, m->ownerId, 0, m->team);
		aos_se(w, 0x15);
		m->id = 0x1c2;
		m->f64 = 0;
		m->speed = 0;
		m->f60 = 1.0f;
		m->growScale = 0;
		m->hp = 200;
		m->damage = 0x96;
		m->active = 0;
		m->shapeBody.radius = 40.0f; /* DAT_008140e0 */
		m->shapeProj.radius = 40.0f;
		m->knockback = 10.0f;
		return true;
	case 0x1c2:
		fx0(w, 0x514, col, m->pos, 0.0f, m->ownerId, 0, m->team);
		aos_se(w, 0x15);
		m->id = 0x1c2;
		m->hp = 999;
		m->f64 = 900;
		m->active = 0;
		return true;
	default:
		return false;
	}
}

static bool missile_on_killed(aos_world *w, aos_missile *m) /* FUN_0066d070 */
{
	if (m->id == 400 || m->id == 0x1c2)
		return missile_on_hit(w, m);
	if (m->id == 100 || m->id == 200)
		fx0(w, 0x514, aos_team_colour(w, m->team, 0), m->pos, 0.0f, m->ownerId, 0, m->team);
	return false;
}

static void missile_on_absorb(aos_world *w, aos_missile *m, aos_beam *b) /* FUN_0066d190 */
{
	if (m->id == 300 || m->id == 0x136) {
		float pos[3] = {b->pos[0], b->pos[1], b->pos[2]};
		aos_spawn_beam(w, KY, 300, pos, m->angle, m->ownerId, 0, m->team);
		aos_se_cd(w, 0x25, 3);
	}
}

/* Draw: FUN_00669f40. DAT_009b3580 (pass-1 glow alpha) is a draw-only static
 * shared between missiles; missile 500 reads it without setting it. */
static float s_glow_alpha;

static int bank_of(aos_world *w, int32_t ownerId)
{
	aos_player *o = aos_player_by_objid(w, ownerId);
	return o ? aos_pidx(w, o) : AOS_BANK_P0;
}

static aos_dcmd *mdraw(aos_world *w, const aos_missile *m, const char *fmt, int frame, float half,
                       float sx, float sy, int blend, float a, float r, float g, float b, bool rot, float ang)
{
	aos_dcmd *c = aos_draw(aos_texf(fmt, frame), bank_of(w, m->ownerId), m->pos[0], m->pos[1], 0);
	c->halfW = half * sx;
	c->halfH = half * sy;
	c->blend = (uint8_t)blend;
	c->rgba = aos_rgba(r, g, b, a);
	if (rot)
		c->rotZ = aos_rad2bam(ang);
	return c;
}

static float pulse(int32_t age, int per, float amp, float base)
{
	return aos_sin(((float)(age % per) / (float)per) * AOS_2PI) * amp + base;
}

static void missile_draw(aos_world *w, aos_missile *m)
{
	float s = m->growScale;
	float p1 = pulse(m->age, 20, 0.15f, 0.85f); /* DAT_00813f20, DAT_00814120 */
	float p2 = pulse(m->age, 50, 0.25f, 0.75f); /* DAT_00813f24, DAT_00813f3c */
	bool slot0 = m->p18 == 0;
	switch (m->id) {
	case 100: case 200: case 300: case 0x136: {
		const char *fmt = m->id == 100 ? "ky_ice0%d" : m->id == 200 ? "ky_crys0%d" : "ky_mirr0%d";
		float half = m->id == 100 ? 48.0f : 64.0f;
		bool rot = m->id == 300 || m->id == 0x136;
		float sx = s, sy = 4.0f - s * 3.0f;
		s_glow_alpha = p2;
		mdraw(w, m, fmt, 3, half, sx, sy, AOS_BLEND_ADD_ALPHA, p2, s, s, s, rot, m->angle);
		int f = (m->age / 2) % 3;
		if (slot0)
			mdraw(w, m, fmt, f, half, sx, sy, AOS_BLEND_ALPHA, 1, s * p1, s, s, rot, m->angle);
		else
			mdraw(w, m, fmt, f, half, sx, sy, AOS_BLEND_ALPHA, 1, s, s * p1, s, rot, m->angle);
		break;
	}
	case 0x96: case 0x97: case 0x98: {
		float sx = s, sy = 4.0f - s * 3.0f;
		float k = m->id == 0x96 ? 2.0f : m->id == 0x97 ? 3.0f : 2.5f;
		sx *= k;
		sy *= k;
		float q1 = p1 * m->f60, q2 = p2 * m->f60;
		s_glow_alpha = q2;
		mdraw(w, m, "ky_ice0%d", 3, 48.0f, sx, sy, AOS_BLEND_ADD_ALPHA, q2, s, s, s, true, m->angle);
		int f = (m->age / 2) % 3;
		if (slot0)
			mdraw(w, m, "ky_ice0%d", f, 48.0f, sx, sy, AOS_BLEND_ALPHA, m->f60, s * q1, s, s, true, m->angle);
		else
			mdraw(w, m, "ky_ice0%d", f, 48.0f, sx, sy, AOS_BLEND_ALPHA, m->f60, s, s * q1, s, true, m->angle);
		break;
	}
	case 0x1c2:
		for (int k = 0; k < 6; k++) {
			float a = ((float)k / 6.0f) * AOS_2PI - m->angle;
			float x = aos_cos(a) * m->speed + m->pos[0], y = aos_sin(a) * m->speed + m->pos[1];
			float v = m->f74;
			for (int pass = 0; pass < 2; pass++) {
				aos_dcmd *c = aos_draw(aos_tex("ky_iccl08"), bank_of(w, m->ownerId), x, y, 0);
				c->halfW = 160.0f * s * 0.25f; /* 320 quad (DAT_008178c0) */
				c->halfH = 160.0f * s * 0.35f;
				c->rotZ = aos_rad2bam(a);
				c->blend = pass == 0 ? AOS_BLEND_ALPHA : AOS_BLEND_ADD_ALPHA;
				float al = pass == 0 ? 1.0f : 0.5f;
				c->rgba = slot0 ? aos_rgba(v, v * 0.5f + 0.5f, 1, al) : aos_rgba(1, v, v * 0.5f + 0.5f, al);
			}
		}
		break;
	case 500: {
		float sy = m->age < 10 ? 1.0f : s;
		float sx = m->age < 10 ? s : 4.0f - s * 3.0f;
		float c3 = sy; /* DAT_009b3584 = DAT_009b3588 */
		mdraw(w, m, "ky_iccl0%d", m->age % 3 + 3, 64.0f, sx, sy, AOS_BLEND_ADD_ALPHA, s_glow_alpha, c3, c3, c3, true, m->angle);
		mdraw(w, m, "ky_iccl0%d", m->age % 3, 64.0f, sx, sy, AOS_BLEND_ALPHA, 1, c3, c3, c3, true, m->angle);
		break;
	}
	case 0x226: {
		float sy = m->age < 10 ? 1.0f : s;
		float sx = m->age < 10 ? s : 3.0f - s * 2.0f;
		float c3 = sy;
		if (slot0)
			mdraw(w, m, "ky_iccl0%d", m->age % 3 + 6, 160.0f, sx, sy, AOS_BLEND_ADD_ALPHA, 1, c3, c3 * 0.5f + 0.5f, 1, true, m->angle);
		else
			mdraw(w, m, "ky_iccl0%d", m->age % 3 + 6, 160.0f, sx, sy, AOS_BLEND_ADD_ALPHA, 1, 1, c3, c3 * 0.5f + 0.5f, true, m->angle);
		break;
	}
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* hitboxes (FUN_005e4d20 / FUN_005e5050)                              */

static int hitbox_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t zero, int32_t team)
{
	if (id != 100 && id != 200 && id != 0xfa && id != 300)
		return -1;
	int i = aos_hitbox_new(w, KY, id, pos, angle, owner, zero, team);
	if (i < 0)
		return -1;
	aos_hitbox *h = &w->obj.hitbox[i];
	h->hitType = 3;
	switch (id) {
	case 100:
		h->damage = 1;
		h->shape.radius = 100.0f; /* DAT_00853b90 */
		h->knockback = 10.0f;
		break;
	case 200:
		h->damage = 0x50;
		h->shape.halfW = 100.0f;
		h->shape.halfH = 50.0f; /* DAT_008140ec */
		h->knockback = 0;
		break;
	case 0xfa:
		h->damage = 0x46;
		h->shape.halfW = 200.0f; /* DAT_008147cc */
		h->shape.halfH = 100.0f;
		h->knockback = 0;
		break;
	case 300:
		h->damage = 0x50;
		h->shape.halfW = 760.0f; /* DAT_0081fa70 */
		h->shape.halfH = 20.0f;  /* DAT_008140fc */
		h->knockback = 10.0f;
		break;
	}
	return i;
}

static void hitbox_on_hit(aos_world *w, aos_hitbox *h, aos_player *t) /* FUN_005e5050 */
{
	int32_t col;
	switch (h->id) {
	case 100:
		h->angle = aos_atan2(t->y - h->pos[1], t->x - h->pos[0]);
		aos_se_cd(w, 0x15, 4);
		break;
	case 200: {
		col = aos_team_colour(w, h->team, 0);
		float a = aos_rand_real(w, -AOS_PI, AOS_PI);
		fx0(w, 0x9c4, col, &t->x, a, h->ownerId, 0, h->team);
		fx0(w, 0x515, col, &t->x, 0.0f, h->ownerId, 0, h->team);
		aos_se(w, 0x16);
		break;
	}
	case 0xfa: {
		col = aos_team_colour(w, h->team, 0);
		float a = h->angle + HALF_PI;
		a += aos_rand_real(w, -0.5f, 0.5f);
		fx0(w, 0x9c4, col, &t->x, a, h->ownerId, 0, h->team);
		fx0(w, 0x515, col, &t->x, 0.0f, h->ownerId, 0, h->team);
		aos_se(w, 0x16);
		break;
	}
	case 300:
		col = aos_team_colour(w, h->team, 0);
		for (int k = -3; k < 3; k++) {
			float a = ((float)k / 3.0f) * AOS_PI;
			a += aos_rand_real(w, -0.25f, 0.25f);
			fx0(w, 0x456, col, &t->x, a, h->ownerId, 0, h->team);
		}
		fx0(w, 0x516, col, &t->x, 0.0f, h->ownerId, 0, h->team);
		aos_se_cd(w, 0x13, 4);
		break;
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* lasers (FUN_0061f650 / 61faa0 / 61f750 / 61fa80)                    */

static int laser_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t team, int32_t p18)
{
	if (id != 100)
		return -1;
	int i = aos_laser_new(w, KY, id, pos, angle, owner, team, p18);
	if (i < 0)
		return -1;
	aos_laser *l = &w->obj.laser[i];
	l->damage = 0;
	l->f48 = 0;
	l->hitType = 3;
	l->blockMode = 1;
	l->knockback = 10.0f;
	l->stun = 1;
	return i;
}

static bool laser_update(aos_world *w, aos_laser *l)
{
	if (l->id == 100) {
		l->nseg = 0; /* FUN_004a9f10: segment list cleared every frame */
		if (l->age > 0x13)
			return false;
		if (l->age == 1 || l->age == 3) {
			float pt[3] = {aos_cos(l->angle) * 800.0f + l->origin[0], aos_sin(l->angle) * 800.0f + l->origin[1], l->origin[2]}; /* DAT_008149c8 */
			aos_spawn_hitbox(w, KY, 300, pt, l->angle, l->ownerId, 0, l->team);
		}
	}
	return true;
}

static void laser_draw(aos_world *w, aos_laser *l) /* FUN_0061f750 (screen blit of ky_lsr; verified) */
{
	if (l->id != 100)
		return;
	float h = l->age <= 10 ? 40.0f : 40.0f - 3.0f * (float)(l->age - 10);
	float cx = aos_cos(l->angle) * 1000.0f + l->origin[0], cy = aos_sin(l->angle) * 1000.0f + l->origin[1];
	int bank = bank_of(w, l->ownerId);
	aos_dcmd *c = aos_draw(aos_tex("ky_lsr"), bank, cx, cy, 0);
	c->halfW = 1000.0f;
	c->halfH = h;
	c->rotZ = aos_rad2bam(l->angle);
	c->blend = AOS_BLEND_ADD_ALPHA;
	c->rgba = aos_rgba(1, 1, 1, 128.0f / 255.0f);
	c->hasSrc = 1;
	c->src[0] = 0; c->src[1] = 0; c->src[2] = 511; c->src[3] = 159;
	int k = (l->age / 2) % 3;
	c = aos_draw(aos_tex("ky_lsr"), bank, cx, cy, 0);
	c->halfW = 1000.0f;
	c->halfH = h * 0.15f;
	c->rotZ = aos_rad2bam(l->angle);
	c->blend = AOS_BLEND_ADD_ALPHA;
	c->rgba = aos_rgba(1, 1, 1, 64.0f / 255.0f);
	c->hasSrc = 1;
	c->src[0] = 0; c->src[1] = (float)(160 + 60 * k); c->src[2] = 511; c->src[3] = (float)(219 + 60 * k);
}

static void laser_on_hit(aos_world *w, aos_laser *l, int32_t seg, aos_player *p) /* FUN_0061fa80: empty */
{
	(void)w; (void)l; (void)seg; (void)p;
}

/* ------------------------------------------------------------------ */
/* Crystal enemy (FUN_00521ee0 / 522050 / 522630 / 522580)             */
/* Port note: enemy +0x74 (float glow) is kept in scratch[0] because the engine
 * struct types +0x74 as int; +0x6c "triggered" = mirrorChildren. */

static int enemy_spawn(aos_world *w, int32_t id, int32_t owner, int32_t team, int32_t p18)
{
	if (id != 100)
		return -1;
	int32_t group = aos_enemy_group_next(w); /* FUN_0055efc0 */
	int i = aos_enemy_new(w, KY, owner, team, p18);
	if (i < 0)
		return -1;
	aos_enemy *e = &w->obj.enemy[i];
	e->serial = group;
	e->f10 = 100;
	e->f7c = 100;
	e->f84 = 0;
	e->hp = e->maxHp = 0x1c2;
	e->active = 0;
	e->shape.halfW = 0;
	e->shape.halfH = 0;
	e->shape.radius = 30.0f; /* DAT_008140e8 */
	e->scale[0] = 0;
	e->scale[1] = 3.0f;
	e->scale[2] = 0;
	e->mirrorChildren = 0;
	e->motionId = 0;
	e->rehitTimer = 0;
	return i;
}

static bool enemy_update(aos_world *w, aos_enemy *e)
{
	if (e->f7c != 100 && e->f7c != 0x6e)
		return true;
	if (e->f84 == 0) {
		e->scale[0] += 0.1f;
		e->scale[1] -= 0.2f;
		e->scale[2] += 0.1f;
		if (e->scale[0] >= 1.0f) {
			e->scale[0] = e->scale[1] = e->scale[2] = 1.0f;
			e->active = 1;
			e->f84 = 100;
			e->timer2 = 0;
		}
	} else if (e->f84 == 100) {
		if (redirect_beams(w, e->pos, e->ownerId, e->team, 30.0f, true))
			e->mirrorChildren = 1;
		if (e->mirrorChildren == 1) {
			e->angle = aos_rand_real(w, -AOS_PI, AOS_PI);
			e->scratch[0] += 0.1f;
			if (e->scratch[0] > 1.0f)
				e->scratch[0] = 1.0f;
			aos_player *ow = player_of_team(w, e->team);
			if (ow->comboShowHits < 0x15) /* owner +0x128 = shown combo-hit count (FUN_00780ab0: 1 on new combo, else ++, cap 99) */
				aos_spawn_hitbox(w, KY, 100, e->pos, 0.0f, e->ownerId, 0, e->team);
		}
		if (e->timer2 > 699)
			e->f84 = 900;
	} else {
		e->scale[0] -= 0.05f;
		e->scale[1] += 0.1f;
		e->scale[2] -= 0.05f;
		e->scratch[0] -= 0.1f;
		if (e->scratch[0] < 0.0f)
			e->scratch[0] = 0;
		if (e->scale[0] <= 0.0f)
			return false;
	}
	return true;
}

static bool enemy_on_killed(aos_world *w, aos_enemy *e) /* FUN_00522580 */
{
	if (e->f10 != 100)
		return false;
	fx0(w, 0x514, aos_team_colour(w, e->team, 0), e->pos, 0.0f, e->ownerId, 0, e->team);
	aos_se(w, 0xb);
	e->f84 = 900;
	e->active = 0;
	e->hp = 999;
	return true;
}

static void enemy_draw(aos_world *w, aos_enemy *e) /* FUN_00522630 */
{
	if (e->f10 != 100)
		return;
	int bank = bank_of(w, e->ownerId);
	float p1 = pulse(e->age, 20, 0.15f, 0.85f), p2 = pulse(e->age, 50, 0.25f, 0.75f);
	if (e->f84 == 100) {
		float k = e->scratch[0] * 2.0f * (pulse(e->age, 75, 0.05f, 1.0f));
		static const struct { int frame, blend; float km; } pass[3] = {{1, 1, 1}, {2, 2, 2}, {0, 2, 1}};
		for (int i = 0; i < 3; i++) {
			aos_dcmd *c = aos_draw(aos_texf("ky_impact%d", pass[i].frame), bank, e->pos[0], e->pos[1], 0);
			c->halfW = c->halfH = 64.0f * k * pass[i].km;
			c->rotZ = aos_rad2bam(e->angle);
			c->blend = (uint8_t)pass[i].blend;
			float a = i == 0 ? p2 * 0.75f : i == 1 ? p2 * 0.5f : p1 * 0.75f;
			c->rgba = aos_rgba(1, 1, 1, a);
		}
	}
	float sz = e->scale[2];
	aos_dcmd *c = aos_draw(aos_tex("ky_crys03"), bank, e->pos[0], e->pos[1], 0);
	c->halfW = 64.0f * e->scale[0];
	c->halfH = 64.0f * e->scale[1];
	c->blend = AOS_BLEND_ADD_ALPHA;
	c->rgba = aos_rgba(sz, sz, sz, p2);
	c = aos_draw(aos_texf("ky_crys0%d", (e->age / 2) % 3), bank, e->pos[0], e->pos[1], 0);
	c->halfW = 64.0f * e->scale[0];
	c->halfH = 64.0f * e->scale[1];
	c->blend = AOS_BLEND_ALPHA;
	c->rgba = e->p18 == 0 ? aos_rgba(sz * p1, sz, sz, 1) : aos_rgba(sz, sz * p1, sz, 1);
}

/* ------------------------------------------------------------------ */
/* Kyoko effects (FUN_004fb8d0 / 4fd940 / 4fbcf0) — cosmetic            */

static int effect_spawn(aos_world *w, int32_t id, int32_t variant, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team)
{
	float sc, al, aux;
	int layer = 1;
	switch (id) {
	case 100: case 0x65: case 0x66: sc = 0; al = 1; aux = 0; break;
	case 200: case 0xc9: case 0xfa: sc = 1; al = 1; aux = 1; layer = 0; break;
	case 0x104: sc = 0; al = 1; aux = 1; break;
	case 300: case 0x136: case 0x140: sc = 1; al = 1; aux = 0; break;
	default: return 0; /* FUN_004fb8d0 returns 0 without spawning */
	}
	int i = aos_effect_new(w, KY, id, variant, pos, angle, owner, age0, team);
	if (i < 0)
		return 0;
	aos_effect *e = &w->obj.effect[i];
	e->drawLayer = layer;
	e->scale = sc;
	e->alpha = al;
	e->aux = aux;
	return i;
}

static bool effect_update(aos_world *w, aos_effect *e)
{
	switch (e->id) {
	case 100: case 0x65: case 0x66:
		e->alpha -= 0.05f;
		if (e->scale < 3.0f)
			e->scale += 0.5f;
		e->pos[0] = aos_cos(e->moveDir) * 10.0f + e->pos[0];
		e->pos[1] = aos_sin(e->moveDir) * 10.0f + e->pos[1];
		return e->alpha > 0.0f;
	case 200: case 0xc9:
		e->alpha -= 0.025f;
		e->scale += 0.1f;
		return e->alpha > 0.0f;
	case 0xfa:
		if (e->age > 300)
			return false;
		if (e->age % 10 == 0) {
			float a = aos_rand_real(w, -AOS_PI, AOS_PI);
			fx15(w, 200, 0, e->pos, a, e->ownerId, 0, e->team);
		}
		return true;
	case 0x104:
		if (e->age < 300) {
			if (e->scale < 1.0f) {
				e->scale += 0.1f;
				if (e->scale > 1.0f)
					e->scale = 1.0f;
				e->pos[0] = aos_cos(e->moveDir) * 2.5f + e->pos[0];
				e->pos[1] = aos_sin(e->moveDir) * 2.5f + e->pos[1];
			}
		} else {
			e->scale -= 0.1f;
			e->alpha -= 0.1f;
			aos_spawn_particle(w, 0x456, e->variant, e->pos, e->moveDir, e->ownerId, 0, 1);
		}
		e->rotation = pulse(e->age, 50, 0.25f, 0.75f);
		return e->alpha > 0.0f;
	case 300:
		if (e->age > 5)
			e->alpha -= 0.1f;
		return e->alpha > 0.0f;
	case 0x136:
		if (e->age > 2)
			e->alpha -= 0.1f;
		return e->alpha > 0.0f;
	case 0x140:
		if (e->age > 1)
			e->alpha -= 0.1f;
		return e->alpha > 0.0f;
	default:
		return true;
	}
}

/* Bullet-sheet blit used by effects 300/0x136/0x140 (beamMdl beamemitb /
 * beamemit, src by colour index). Sizes per FUN_004fbcf0: 300 body 60x60 core 40x16
 * flash 54x26 (DAT_008178a8/d0), white core until age 6; 0x136 and 0x140 body 60x40
 * core 40x8 flash 48x16, white core until age 5 (verified, 0x136 == 0x140). */
static void bullet_fx(aos_world *w, aos_effect *e, float bw, float bh, float cw, float ch, float fw, float fh, int plainAge)
{
	int c = e->variant;
	int sx0 = (c / 8) * 240, sy0 = (c % 8) * 64;
	aos_player *o = player_of_team(w, e->team);
	aos_dcmd *d = aos_draw(aos_tex("beamemitb"), AOS_BANK_COMMON, e->pos[0], e->pos[1], 0);
	d->halfW = bw; d->halfH = bh;
	d->rotZ = aos_rad2bam(e->rotation);
	d->blend = AOS_BLEND_ALPHA;
	d->rgba = aos_rgba(1, 1, 1, (float)(int)(32.0f * e->alpha) / 255.0f);
	d->hasSrc = 1;
	d->src[0] = (float)sx0; d->src[1] = (float)sy0; d->src[2] = (float)(sx0 + 0x77); d->src[3] = (float)(sy0 + 0x3f);
	if (e->age < 2) {
		int fx0x = (c / 8) * 256;
		d = aos_draw(aos_tex("beamemit"), AOS_BANK_COMMON, e->pos[0], e->pos[1], 0);
		d->halfW = fw; d->halfH = fh;
		d->rgba = 0xffffffffu;
		d->src[0] = (float)fx0x; d->src[1] = (float)sy0; d->src[2] = (float)(fx0x + 0x7f); d->src[3] = (float)(sy0 + 0x3f);
	} else {
		d = aos_draw(aos_tex("beamemitb"), AOS_BANK_COMMON, e->pos[0], e->pos[1], 0);
		d->halfW = cw; d->halfH = ch;
		float a = (float)(int)(255.0f * e->alpha) / 255.0f; /* (int)(255*alpha) */
		if (e->age < plainAge)
			d->rgba = aos_rgba(1, 1, 1, a);
		else if (o->color == 0)
			d->rgba = aos_rgba(0, 0x80 / 255.0f, 1, a);
		else
			d->rgba = aos_rgba(1, 0, 0x80 / 255.0f, a);
		d->src[0] = (float)(sx0 + 0x78); d->src[1] = (float)sy0; d->src[2] = (float)(sx0 + 0xef); d->src[3] = (float)(sy0 + 0x3f);
	}
	d->rotZ = aos_rad2bam(e->rotation);
	d->blend = AOS_BLEND_ADD_ALPHA;
	d->hasSrc = 1;
}

static void effect_draw(aos_world *w, aos_effect *e)
{
	aos_player *o = player_of_team(w, e->team);
	int bank = aos_pidx(w, o);
	switch (e->id) {
	case 100: case 0x65: case 0x66: {
		float hw = 80.0f * e->scale, hh = 80.0f;
		if (e->age > 10) {
			hw *= (float)(e->age - 10) * 0.05f + 1.0f;
			hh = (float)(20 - e->age) * 8.0f;
		}
		hh *= 2.4f; /* DAT_008178cc */
		for (int pass = 0; pass < 2; pass++) {
			aos_dcmd *c = aos_draw(aos_texf("ky_iccl0%d", 1 - pass), bank, e->pos[0], e->pos[1], 0);
			c->halfW = hw; c->halfH = hh;
			c->rotZ = aos_rad2bam(e->rotation);
			c->blend = pass == 0 ? AOS_BLEND_ADD_ALPHA : AOS_BLEND_ALPHA;
			c->rgba = aos_rgba(1, 1, 1, pass == 0 ? e->alpha : 1.0f);
		}
		break;
	}
	case 200: case 0xc9: {
		int f = imin(e->age / 12, 3);
		aos_dcmd *c = aos_draw(aos_texf("ky_smk0%d", f), bank, e->pos[0], e->pos[1], 0);
		c->halfW = c->halfH = 32.0f * e->scale;
		c->rotZ = aos_rad2bam(e->rotation);
		c->blend = AOS_BLEND_ALPHA;
		if (e->id == 0xc9) {
			c->rgba = aos_rgba(1, 1, 1, (float)((int)(255.0f * e->alpha) / 2) / 255.0f);
			break;
		}
		c->rgba = aos_rgba(1, 1, 1, e->alpha);
		float g = (float)(int)(128.0f * e->alpha) / 255.0f;
		c = aos_draw(aos_texf("ky_smk0%d", f), bank, e->pos[0], e->pos[1], 0);
		c->halfW = c->halfH = 32.0f * e->scale;
		c->rotZ = aos_rad2bam(e->rotation);
		c->blend = AOS_BLEND_ADD_ALPHA;
		c->rgba = aos_rgba(g, g, g, e->alpha);
		break;
	}
	case 0x104: {
		aos_dcmd *c = aos_draw(aos_tex("ky_iccl08"), bank, e->pos[0], e->pos[1], 0);
		c->halfW = 32.0f * e->scale;
		c->halfH = 54.0f * e->scale;
		c->rotZ = aos_rad2bam(e->moveDir);
		float v = e->rotation;
		c->rgba = o->color == 0 ? aos_rgba(v, v * 0.5f + 0.5f, 1, 1) : aos_rgba(1, v, v * 0.5f + 0.5f, 1);
		break;
	}
	case 300: bullet_fx(w, e, 60, 60, 40, 16, 54, 26, 6); break;
	case 0x136: bullet_fx(w, e, 60, 40, 40, 8, 48, 16, 5); break;
	case 0x140: bullet_fx(w, e, 60, 40, 40, 8, 48, 16, 5); break;
	default: break;
	}
}

/* ------------------------------------------------------------------ */

void aos_register_kyoko(void)
{
	aos_tab.inputA[KY] = input_a;
	aos_tab.inputB[KY] = input_b;
	/* CPU AI FUN_006edbe0 / FUN_006ed350: not reversed (cpu[] left NULL) */

	aos_tab.T2[0x1d1] = st_neutral;     aos_tab.T1[0x1d1] = dr_neutral;
	aos_tab.T2[0x1d2] = st_stub;        aos_tab.T1[0x1d2] = dr_dash;
	aos_tab.T2[0x1d3] = st_stub;        aos_tab.T1[0x1d3] = dr_ko;
	aos_tab.T2[0x1d4] = st_stub;        aos_tab.T1[0x1d4] = dr_hit;
	aos_tab.T2[0x1d5] = st_stub;        aos_tab.T1[0x1d5] = dr_shield;
	aos_tab.T2[0x1db] = st_drizzle;     aos_tab.T1[0x1db] = dr_atka; /* FUN_006ef320 */
	aos_tab.T2[0x1dc] = st_clean_stream;aos_tab.T1[0x1dc] = dr_atka; /* FUN_006efe40 */
	aos_tab.T2[0x1dd] = st_hard_rain;   aos_tab.T1[0x1dd] = dr_atka; /* FUN_006ef8a0 */
	aos_tab.T2[0x1de] = st_freezing;    aos_tab.T1[0x1de] = dr_sld10;/* FUN_006efb60 */
	aos_tab.T2[0x1df] = st_icy_crystal; aos_tab.T1[0x1df] = dr_atkb; /* FUN_006f0100 */
	aos_tab.T2[0x1e0] = st_ice_over;    aos_tab.T1[0x1e0] = dr_atkb; /* FUN_006f03c0 */
	aos_tab.T2[0x1e1] = st_ripple;      aos_tab.T1[0x1e1] = dr_atka; /* FUN_006f0680 */
	aos_tab.T2[0x1e2] = st_water_mirror;aos_tab.T1[0x1e2] = dr_sld10;/* FUN_006f0940 */
	aos_tab.T2[0x1e3] = st_icicle;      aos_tab.T1[0x1e3] = dr_atka; /* FUN_006f0ee0 */
	aos_tab.T2[0x1e4] = st_tree_of_ice; aos_tab.T1[0x1e4] = dr_sld4; /* FUN_006f11a0 */
	aos_tab.T2[0x1e5] = st_crystal;     aos_tab.T1[0x1e5] = dr_sld3; /* FUN_006f0c20 */
	aos_tab.T2[0x1e6] = st_early_summer_rain; aos_tab.T1[0x1e6] = dr_atka; /* FUN_006ef5e0 */
	aos_tab.T2[0x1e7] = st_avalanche;   aos_tab.T1[0x1e7] = dr_sld4; /* FUN_006f1460 */

	aos_otab.beamSpawn[KY] = beam_spawn;
	aos_otab.beamUpdate[KY] = beam_update;
	aos_otab.beamDraw[KY] = beam_draw;
	aos_otab.beamOnHit[KY] = beam_on_hit;
	aos_otab.missileSpawn[KY] = missile_spawn;
	aos_otab.missileUpdate[KY] = missile_update;
	aos_otab.missileDraw[KY] = missile_draw;
	aos_otab.missileOnHit[KY] = missile_on_hit;
	aos_otab.missileOnKilled[KY] = missile_on_killed;
	aos_otab.missileOnAbsorb[KY] = missile_on_absorb;
	aos_otab.hitboxSpawn[KY] = hitbox_spawn;
	aos_otab.hitboxOnHit[KY] = hitbox_on_hit;
	aos_otab.laserSpawn[KY] = laser_spawn;
	aos_otab.laserUpdate[KY] = laser_update;
	aos_otab.laserDraw[KY] = laser_draw;
	aos_otab.laserOnHit[KY] = laser_on_hit;
	aos_otab.enemySpawn[KY] = enemy_spawn;
	aos_otab.enemyUpdate[KY] = enemy_update;
	aos_otab.enemyDraw[KY] = enemy_draw;
	aos_otab.enemyOnKilled[KY] = enemy_on_killed;
	aos_otab.effectSpawn[KY] = effect_spawn;
	aos_otab.effectUpdate[KY] = effect_update;
	aos_otab.effectDraw[KY] = effect_draw;
}
