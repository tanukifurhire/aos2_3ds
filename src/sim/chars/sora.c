/* Sora (engine id 3, states 0x08..0x21) — port of docs/re/sora.md.
 * Every function names its original address. Version 0x5a2 branches only.
 * Object families: all type-3 functions (beam 0x4b3fe0.., missile 0x677970..,
 * hitbox 0x5e9f20/0x5ea560, laser 0x625910.., effect 0x5070d0..). */
#include <stddef.h>

#include "../draw.h"
#include "../fx.h"
#include "../sim.h"

#define ID AOS_CH_SORA
#define HALF_PI 1.5707964f /* DAT_00814104 */

void aos_se_cd(aos_world *w, int id, int cd);                              /* FUN_007bcaf0 */

/* ------------------------------------------------------------------ */
/* helpers                                                             */

/* Per-team colour slots DAT_008859f4 + team*0x10 + 4k: aos_team_colour (k = 0: "f4", 1: "f8"). */
enum { COL_F4 = 0, COL_F8 = 1 };
static aos_player *player_of_team(aos_world *w, int32_t team);

static int eff0(aos_world *w, int32_t id, int32_t var, const float pos[3], float ang, int32_t owner, int32_t team)
{
	return aos_spawn_effect(w, 0, id, var, pos, ang, owner, 0, team);
}
static aos_effect *effS(aos_world *w, int32_t id, int32_t var, const float pos[3], float ang, int32_t owner, int32_t team)
{
	int i = aos_spawn_effect(w, ID, id, var, pos, ang, owner, 0, team);
	return i >= 0 ? &w->obj.effect[i] : NULL;
}

static aos_player *player_of_team(aos_world *w, int32_t team) /* FUN_00780450 */
{
	return team == 1 ? &w->p[0] : &w->p[1];
}

static void aim(aos_world *w, aos_player *p) { p->faceAngle = aos_aim(w, p); }

static void walk(aos_player *p) /* neutral movement block (FUN_00737800) */
{
	float s = aos_tab.stats[p->id].walk;
	if (p->up > 1) p->y += s;
	if (p->down > 1) p->y -= s;
	if (p->left > 1) p->x -= s;
	if (p->right > 1) p->x += s;
}

/* dash sparks: rand angle, rand int % 8 -> common 0xa28+n at pos, angle r + aim */
static void dash_sparks(aos_world *w, aos_player *p, float lo, float hi)
{
	float r = aos_rand_real(w, lo, hi);
	int32_t n = aos_rand_int(w, 0, 100);
	eff0(w, 0xa28 + n % 8, 0xd, &p->x, r + p->faceAngle, p->objId, p->side);
}

static void ahead(const aos_player *p, float d, float out[3])
{
	out[0] = aos_cos(p->faceAngle) * d + p->x;
	out[1] = aos_sin(p->faceAngle) * d + p->y;
	out[2] = 0;
}

static int hitbox(aos_world *w, aos_player *p, int32_t id, const float pos[3], float ang)
{
	return aos_spawn_hitbox(w, ID, id, pos, ang, p->objId, 0, p->side);
}

static void lunge(aos_player *p, float v)
{
	p->dashAngle = p->faceAngle;
	p->moveAngle = p->faceAngle;
	p->speed = v;
}

/* "if (t < N || D != 3) { ...end... } else dash cancel" */
static bool cancel_at(aos_world *w, aos_player *p, int32_t n)
{
	if (p->timer >= n && p->D == 3) {
		aos_dash(w, p, 1);
		return true;
	}
	return false;
}

/* ------------------------------------------------------------------ */
/* input dispatch                                                      */

static bool enter(aos_world *w, aos_player *p, int32_t s)
{
	aos_set_state(w, p, s);
	return true;
}

static bool normal_moves(aos_world *w, aos_player *p)
{
	if (p->state != 0 && p->state != 2)
		return false;
	if (p->D == 3) {
		aos_dash(w, p, 0);
	} else if (p->Shield == 3 && aos_spend_hyper(p, 0.5f) && aos_may_shield(w, p)) {
		aos_shield(w, p);
	} else {
		if (p->S > 1 && p->B == 3) return enter(w, p, 0x1b);
		if (p->S > 1 && p->A == 3) return enter(w, p, 0x18);
		if (p->B == 3) return enter(w, p, 0x1f);
		if (p->D > 1 && p->A == 3) return enter(w, p, 0x13);
		if (p->A == 3) return enter(w, p, 0x14);
	}
	return false;
}

static bool input_a(aos_world *w, aos_player *p) /* FUN_0073b470 */
{
	if (aos_may_hyper(w, p)) {
		if (p->S > 1 && p->B > 1 && p->H == 3 && aos_spend_hyper(p, 3.0f)) return enter(w, p, 0x21);
		if (p->S > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f)) return enter(w, p, 0x1c);
		if (p->B > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f)) return enter(w, p, 0x20);
		if (p->D > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f)) return enter(w, p, 0x17);
		if (p->H == 3 && aos_spend_hyper(p, 1.0f)) return enter(w, p, 0x16);
	}
	if (normal_moves(w, p))
		return true;
	return aos_tab.T2[p->state] ? aos_tab.T2[p->state](w, p) : true;
}

static bool input_b(aos_world *w, aos_player *p) /* FUN_0073b930 */
{
	if (aos_may_hyper(w, p)) {
		if (p->H > 1 && p->Shield == 3 && aos_spend_hyper(p, 3.0f)) return enter(w, p, 0x21);
		if (p->H > 1 && p->S == 3 && aos_spend_hyper(p, 1.0f)) return enter(w, p, 0x1c);
		if (p->H > 1 && p->B == 3 && aos_spend_hyper(p, 1.0f)) return enter(w, p, 0x20);
		if (p->H > 1 && p->D == 3 && aos_spend_hyper(p, 1.0f)) return enter(w, p, 0x17);
		if (p->H > 1 && p->A == 3 && aos_spend_hyper(p, 1.0f)) return enter(w, p, 0x16);
	}
	if (normal_moves(w, p))
		return true;
	return aos_tab.T2[p->state] ? aos_tab.T2[p->state](w, p) : true;
}

/* ------------------------------------------------------------------ */
/* states (T2)                                                         */

static bool st_neutral(aos_world *w, aos_player *p) /* 0x08 FUN_00737800 */
{
	(void)w;
	walk(p);
	p->faceAngle = p->facingRight == 1 ? 0.0f : AOS_PI;
	return true;
}

static bool st_stub(aos_world *w, aos_player *p) /* 0x09..0x0c FUN_007378f0/737920/737900/737910 */
{
	(void)w, (void)p;
	return true;
}

static void beam(aos_world *w, aos_player *p, int32_t id, float ang)
{
	aos_spawn_beam(w, ID, id, &p->x, ang, p->objId, 0, p->side);
}

static bool st_rifle_quick(aos_world *w, aos_player *p) /* 0x12 FUN_00737930 */
{
	if (p->timer == 0)
		aim(w, p);
	if (p->timer == 12 && aos_use_gauge(p, 0x12)) {
		aim(w, p);
		beam(w, p, 100, p->faceAngle);
		aos_se(w, 0xd);
	}
	if (!cancel_at(w, p, 20) && p->timer > 30)
		p->state = 0;
	return true;
}

static bool st_rifle_spread(aos_world *w, aos_player *p) /* 0x13 FUN_00737f30 */
{
	if (p->timer == 0)
		aim(w, p);
	if (p->timer > 9 && p->timer < 21 && p->timer % 2 == 0 && aos_use_gauge(p, 0x13)) {
		aim(w, p);
		float r = aos_rand_real(w, -0.4f, 0.4f); /* DAT_008147f0, DAT_00813f30 */
		beam(w, p, 200, r + p->faceAngle);
		aos_se(w, 0xd);
	}
	if (!cancel_at(w, p, 20)) {
		if (p->timer > 30)
			p->state = 0;
		p->friction *= 0.5f;
	}
	return true;
}

static bool st_rifle_press(aos_world *w, aos_player *p) /* 0x14 FUN_00737a50 */
{
	if (p->timer == 0)
		aim(w, p);
	if (p->timer < 11 && p->A < 1) {
		p->state = 0x12; /* timer kept */
		aos_tab.T2[0x12](w, p);
		return true;
	}
	if (p->timer > 12 && p->timer < 40) {
		if (p->D < 2)
			walk(p);
		aim(w, p);
	}
	if (p->timer > 14 && p->timer < 29 && p->A < 2) {
		if (aos_use_gauge(p, 0x12)) {
			beam(w, p, 100, p->faceAngle);
			aos_se(w, 0xd);
		}
		p->timer = 100;
	}
	if (p->timer == 29) {
		aos_popup(w, 2, &p->x, p->objId);
		aos_se(w, 0x2a);
	}
	if (p->timer > 29 && p->timer < 40 && p->A < 2) {
		aos_use_gauge(p, 0x14);
		beam(w, p, 300, p->faceAngle);
		int32_t c = aos_team_colour(w, p->side, COL_F4);
		eff0(w, 0x456, c, &p->x, p->faceAngle + HALF_PI, p->objId, p->side);
		eff0(w, 0x456, c, &p->x, p->faceAngle - HALF_PI, p->objId, p->side);
		eff0(w, 0x515, c, &p->x, p->faceAngle, p->objId, p->side);
		aos_se(w, 0xe);
		p->timer = 100;
	}
	if (p->timer == 40)
		p->timer = 30;
	if (p->timer > 24 && p->timer < 40 && p->timer % 3 == 0)
		eff0(w, 0x514, aos_team_colour(w, p->side, COL_F4), &p->x, 0, p->objId, p->side);
	if (!cancel_at(w, p, 115) && p->timer > 150)
		p->state = 0;
	return true;
}

static bool st_rifle_finish(aos_world *w, aos_player *p) /* 0x15 FUN_007380d0 */
{
	if (p->timer == 0)
		aim(w, p);
	if (p->timer == 5 && aos_use_gauge(p, 0x15)) {
		aim(w, p);
		beam(w, p, 300, p->faceAngle);
		aos_se(w, 0xe);
	}
	if (!cancel_at(w, p, 30) && p->timer > 40)
		p->state = 0;
	return true;
}

static void hyper_start(aos_world *w, aos_player *p)
{
	aim(w, p);
	aos_barrier(w, p, 10);
	aos_hyper_flourish(w, p); /* SE 6, dark(100,.75), speedline(100,pos,aim,π/4), blur(20,.5,2), 0xfa */
}

static bool st_hyper_spread(aos_world *w, aos_player *p) /* 0x16 FUN_007382e0 */
{
	int32_t t = p->timer;
	if (t == 0)
		hyper_start(w, p);
	if (t == 9)
		aim(w, p);
	if (t > 8 && t < 37 && t % 3 == 0) {
		float a = aos_rand_real(w, 0.0f, 0.35f);  /* DAT_00813f2c */
		float d = aos_rand_real(w, 50.0f, 90.0f); /* DAT_008140ec, DAT_00814170 */
		switch (t) { /* ver > 0x5a0 */
		case 0xc: a = 0.04f; break;                 /* DAT_00813eec */
		case 0xf: case 0x12: a = 0.08f; break;      /* DAT_00813f00 */
		case 0x15: case 0x18: a = 0.12f; break;     /* DAT_00824a8c */
		case 0x1b: case 0x1e: a = 0.16f; break;     /* DAT_0081cc64 */
		default: a = 0.0f; break;
		}
		d = (float)(t - 9) + 50.0f;
		if ((t - 9) % 9 == 0)
			a *= -1.0f;
		float v[2] = {aos_cos(a) * d, aos_sin(a) * d * 3.0f};
		aos_vec_rotate(v, p->faceAngle);
		float pt[3] = {v[0] + p->x, v[1] + p->y, 0};
		aos_spawn_laser(w, ID, 100, pt, p->faceAngle, p->objId, p->side, p->color);
		aos_se_cd(w, 0x12, 6);
	}
	if (!cancel_at(w, p, 40) && p->timer > 50)
		p->state = 0;
	return true;
}

static bool st_hyper_spread_d(aos_world *w, aos_player *p) /* 0x17 FUN_007386c0 */
{
	int32_t t = p->timer;
	if (t == 0)
		hyper_start(w, p);
	if (t > 8 && t < 37 && t % 3 == 0) {
		float a = aos_rand_real(w, 0.0f, 0.35f);
		float d = aos_rand_real(w, 50.0f, 100.0f); /* DAT_00853b90 */
		if ((t - 9) % 9 == 0)
			a *= -1.0f;
		float v[2] = {aos_cos(a) * d, aos_sin(a) * d * 3.0f};
		aos_vec_rotate(v, p->faceAngle);
		float pt[3] = {v[0] + p->x, v[1] + p->y, 0};
		aos_spawn_laser(w, ID, 100, pt, a + p->faceAngle, p->objId, p->side, p->color);
		aos_se_cd(w, 0x12, 6);
	}
	if (!cancel_at(w, p, 40) && p->timer > 50)
		p->state = 0;
	return true;
}

static bool st_sword1(aos_world *w, aos_player *p) /* 0x18 FUN_00739670 */
{
	if (p->timer == 0) {
		aim(w, p);
		float v = aos_dist(w, p) / 14.0f; /* DAT_00813a54 */
		if (14.0f <= v)
			v = 14.0f;
		lunge(p, v);
	}
	if (3.0f <= p->speed)
		dash_sparks(w, p, -0.3f, 0.3f);
	if (p->timer == 5)
		aos_se(w, 0x1f);
	if (p->timer == 10 && aos_use_gauge(p, 0x18)) {
		float pt[3];
		ahead(p, 60.0f, pt);
		hitbox(w, p, 200, pt, p->faceAngle);
	}
	if (p->timer >= 10 && p->hitFlag == 1 && p->B == 3)
		return enter(w, p, 0x1b);
	if (p->timer >= 10 && p->hitFlag == 1 && p->S > 1 && p->A == 3)
		return enter(w, p, 0x19);
	if (!cancel_at(w, p, 30) && p->timer > 40)
		p->state = 0;
	return true;
}

static bool st_sword2(aos_world *w, aos_player *p) /* 0x19 FUN_00739a70 */
{
	if (p->timer == 0) {
		aim(w, p);
		float v = aos_dist(w, p) / 15.0f;
		if (10.0f <= v)
			v = 10.0f;
		lunge(p, v);
	}
	if (p->timer == 5)
		aos_se(w, 0x1f);
	if (p->timer == 10 && aos_use_gauge(p, 0x19)) {
		float pt[3];
		ahead(p, 40.0f, pt);
		hitbox(w, p, 0xd2, pt, p->faceAngle);
	}
	if (p->timer >= 10 && p->hitFlag == 1 && p->S > 1 && p->A == 3)
		return enter(w, p, 0x1a);
	if (!cancel_at(w, p, 30) && p->timer > 40)
		p->state = 0;
	return true;
}

static bool st_sword3(aos_world *w, aos_player *p) /* 0x1a FUN_00739d60 */
{
	if (p->timer == 0) {
		aim(w, p);
		p->moveAngle = p->faceAngle;
		p->speed = 0;
	}
	if (p->timer == 8) {
		aim(w, p);
		lunge(p, 18.0f); /* DAT_00814144 */
		aos_se(w, 0x20);
	}
	if (p->timer == 9 && aos_use_gauge(p, 0x1a))
		hitbox(w, p, 0xdc, &p->x, p->faceAngle);
	if (3.0f <= p->speed)
		dash_sparks(w, p, -0.3f, 0.3f);
	if (!cancel_at(w, p, 30)) {
		if (p->timer > 40)
			p->state = 0;
		p->friction *= 0.5f;
	}
	return true;
}

static bool st_slash_through(aos_world *w, aos_player *p) /* 0x1b FUN_0073a010 */
{
	if (p->timer == 0) {
		aim(w, p);
		p->moveAngle = p->faceAngle;
		p->speed = 0;
	}
	if (p->timer == 8) {
		aim(w, p);
		lunge(p, 16.0f); /* DAT_008140d8 */
		aos_se(w, 0x21);
	}
	if (p->timer == 9 && aos_use_gauge(p, 0x1b))
		hitbox(w, p, 0xe6, &p->x, p->faceAngle);
	if (3.0f <= p->speed)
		dash_sparks(w, p, -0.3f, 0.3f);
	if (p->timer >= 8 && p->hitFlag == 1 && p->A == 3)
		return enter(w, p, 0x15);
	if (!cancel_at(w, p, 30)) {
		if (p->timer > 40)
			p->state = 0;
		p->friction *= 0.5f;
	}
	return true;
}

static bool st_dance(aos_world *w, aos_player *p) /* 0x1c FUN_0073a360 */
{
	if (p->timer == 0)
		hyper_start(w, p);
	if (p->timer == 5) {
		aim(w, p);
		float v = aos_dist(w, p) / 15.0f;
		if (12.0f <= v)
			v = 12.0f;
		lunge(p, v);
	}
	if (3.0f <= p->speed)
		dash_sparks(w, p, -0.5f, 0.5f);
	if (p->timer == 8)
		aos_se(w, 0x1f);
	float pt[3];
	if (p->timer == 10) {
		ahead(p, 60.0f, pt);
		hitbox(w, p, 0xfa, pt, p->faceAngle);
	}
	if (p->timer > 10 && p->timer < 60 && p->hitFlag == 0) { /* whiff */
		p->timer = 0x42;
		p->barrierTime = 0x32;
		p->barrierBreakT = 0x19;
	}
	int32_t t = p->timer;
	bool right = aos_fabs(p->faceAngle) <= HALF_PI;
	if (t < 11 || t > 60 || (t - 10) % 20 != 0) {
		if (t > 10 && t < 61 && (t - 10) % 10 == 0) {
			ahead(p, 60.0f, pt);
			hitbox(w, p, 0xfc, pt, right ? p->faceAngle + 0.5f : p->faceAngle - 0.5f);
			aos_se(w, 0x1f);
		}
	} else {
		ahead(p, 60.0f, pt);
		hitbox(w, p, 0xfb, pt, right ? p->faceAngle - 0.5f : p->faceAngle + 0.5f);
		aos_se(w, 0x1f);
	}
	aos_player *o = aos_opponent(w, p);
	if (p->timer == 0x41 && o->state == AOS_ST_HITSTUN)
		return enter(w, p, 0x1d);
	if (!cancel_at(w, p, 70)) {
		if (p->timer > 80)
			p->state = 0;
		p->friction *= 0.5f;
	}
	return true;
}

static bool st_dance_finish(aos_world *w, aos_player *p) /* 0x1d FUN_0073aaa0 */
{
	if (p->timer == 0) {
		aim(w, p);
		p->speed = 0;
	}
	if (p->timer == 5) {
		aim(w, p);
		lunge(p, 20.0f); /* DAT_008140fc */
		float pt[3];
		ahead(p, 60.0f, pt);
		hitbox(w, p, 0x104, pt, p->faceAngle);
		eff0(w, 0xa8c, 0, pt, 0, p->objId, p->side);
		aos_se(w, 0x20);
	}
	if (p->timer > 4 && p->timer < 21 && p->timer % 3 == 0) /* common 100 = graze zone */
		eff0(w, 100, 0, &p->x, p->faceAngle, p->objId, p->side);
	if (!cancel_at(w, p, 40) && p->timer > 50)
		p->state = 0;
	return true;
}

static bool st_launcher_homing(aos_world *w, aos_player *p) /* 0x1e FUN_007389c0 */
{
	if (p->timer > 11 && p->timer < 31 && p->timer % 3 == 0 && aos_use_gauge(p, 0x1e)) {
		aim(w, p);
		float a = aos_rand_real(w, -HALF_PI, HALF_PI);
		float d = aos_rand_real(w, 40.0f, 80.0f);
		float v[2] = {d, 0.0f};
		aos_vec_rotate(v, a + p->faceAngle);
		float pt[3] = {aos_cos(p->faceAngle) * -100.0f + p->x + v[0],
		               aos_sin(p->faceAngle) * -100.0f + p->y + v[1], 0};
		aos_spawn_missile(w, ID, 100, pt, a / 3.0f + p->faceAngle, p->objId, 0, p->side, p->color);
		aos_se(w, 0x22);
	}
	if (!cancel_at(w, p, 35) && p->timer > 45)
		p->state = 0;
	return true;
}

static void rocket(aos_world *w, aos_player *p, float ang, float d, int32_t dmg)
{
	float v[2] = {d, 0.0f};
	aos_vec_rotate(v, ang);
	float pt[3] = {aos_cos(p->faceAngle) * -50.0f + p->x + v[0],
	               aos_sin(p->faceAngle) * -50.0f + p->y + v[1], 0};
	int i = aos_spawn_missile(w, ID, 0x96, pt, ang, p->objId, 0, p->side, p->color);
	if (i >= 0) {
		w->obj.missile[i].angle = p->faceAngle;
		if (dmg)
			w->obj.missile[i].damage = dmg;
	}
}

static bool st_launcher_press(aos_world *w, aos_player *p) /* 0x1f FUN_00738c00 */
{
	if (p->timer == 0)
		aim(w, p);
	if (p->timer < 11 && p->B < 2) {
		p->state = 0x1e; /* timer kept */
		aos_tab.T2[0x1e](w, p);
		return true;
	}
	int32_t t = p->timer;
	if (t > 20 && t < 31 && t % 3 == 0 && aos_use_gauge(p, 0x1f)) {
		float r = aos_rand_real(w, 1.0471976f, 2.3561945f); /* DAT_008203bc, DAT_0081fa94 */
		r = (t == 0x15 || t == 0x18) ? 1.0471976f : HALF_PI;
		float ang = (t / 3) % 2 == 0 ? p->faceAngle + r : p->faceAngle - r;
		float d = aos_rand_real(w, 40.0f, 80.0f);
		d = (t == 0x15 || t == 0x18) ? 40.0f : 50.0f;
		rocket(w, p, ang, d, 0);
		aos_se(w, 0x22);
	}
	if (!cancel_at(w, p, 31) && p->timer > 56)
		p->state = 0;
	return true;
}

static bool st_hyper_launcher(aos_world *w, aos_player *p) /* 0x20 FUN_007391c0 */
{
	if (p->timer == 0)
		hyper_start(w, p);
	int32_t t = p->timer;
	if (t > 11 && t < 31 && t % 2 == 0) {
		float r = aos_rand_real(w, 1.0471976f, 2.3561945f);
		switch (t) {
		case 0x10: case 0x12: r = HALF_PI; break;
		case 0x14: case 0x16: r = AOS_PI; break;
		case 0x18: case 0x1a: r = 2.6179938f; break; /* DAT_00825e78 */
		case 0x1c: case 0x1e: r = 2.3561945f; break;
		default: r = 1.0471976f; break;
		}
		float ang = (t / 2) % 2 == 0 ? p->faceAngle + r : p->faceAngle - r;
		float d = aos_rand_real(w, 20.0f, 80.0f); /* DAT_008140fc, DAT_00814164 */
		switch (t) {
		case 0x10: case 0x12: d = 50.0f; break;
		case 0x14: case 0x16: d = 60.0f; break;
		case 0x18: case 0x1a: d = 70.0f; break; /* DAT_008145ec */
		default: d = 40.0f; break;
		}
		rocket(w, p, ang, d, 200);
		aos_se_cd(w, 0x22, 4);
	}
	if (!cancel_at(w, p, 30) && p->timer > 60)
		p->state = 0;
	return true;
}

static bool st_unlimited(aos_world *w, aos_player *p) /* 0x21 FUN_0073ad10 */
{
	if (p->timer == 0) {
		aim(w, p);
		p->speed = 0;
		aos_barrier(w, p, 0x14);
		/* SE 7, FUN_004e9280(150,.85), FUN_007bec20(100,pos,aim,π/4), FUN_004d7ac0(150,.65,2), 0xfa */
		aos_hyper_flourish_ex(w, p, 7, 0x96, 0.85f /* DAT_00814120 */, 100, p->faceAngle,
		                      0.7853982f /* DAT_0081fa90 */, 0x96, 0.65f /* DAT_0081788c */, 2, 1);
	}
	int32_t t = p->timer;
	if (t < 0x33 && t % 2 == 0) {
		for (int k = -2; k < 2; k++) {
			int32_t n = aos_rand_int(w, 0, 100);
			float base = ((float)k / 2.0f) * AOS_PI;
			float r = aos_rand_real(w, -0.75f, 0.75f);
			eff0(w, n < 0x32 ? 200 : 0xd2, 0, &p->x, base + r, p->objId, p->side);
		}
	}
	if (t > 9 && t < 0x3d) {
		float pt[3] = {aos_cos(p->faceAngle) * (float)t * 4.0f + p->x,
		               aos_sin(p->faceAngle) * (float)t * 4.0f + p->y, 0};
		aos_effect *e = effS(w, 300, 0, pt, p->faceAngle, p->objId, p->side);
		if (e) {
			e->aux = (float)(t - 10) * 20.0f;
			if (t % 2 == 0)
				e->aux2 = 0.85f;
		}
		int h = hitbox(w, p, t % 5 == 0 ? 300 : 0x12d, pt, p->faceAngle);
		if (h >= 0)
			w->obj.hitbox[h].shape.radius = ((float)(t - 10) / 50.0f) * 280.0f; /* DAT_00814f14 */
		if (t == 0x3c && (e = effS(w, 0x15e, 0, pt, p->faceAngle, p->objId, p->side)) != NULL)
			e->aux = (float)(t - 10) * 20.0f;
	}
	if (t % 3 == 0 && t < 0x37) {
		float q[3];
		ahead(p, 50.0f, q);
		float r = aos_rand_real(w, -0.25f, 0.25f);
		int32_t n = aos_rand_int(w, 0, 100);
		int e = eff0(w, 0xa82 + n % 8, t % 6 == 0 ? 8 : 0xd, q, r + p->faceAngle, p->objId, p->side);
		if (e >= 0)
			w->obj.effect[e].aux2 = (float)t / 60.0f;
	}
	if (!cancel_at(w, p, 80) && p->timer > 110)
		p->state = 0;
	return true;
}

/* ------------------------------------------------------------------ */
/* draw (T1)                                                           */

static aos_dcmd *spr(aos_world *w, aos_player *p, const char *fmt, int frame, float size)
{
	aos_dcmd *c = aos_draw(aos_texf(fmt, frame), aos_pidx(w, p), p->x, p->y, 0);
	c->halfW = c->halfH = size * 0.5f;
	return c;
}

/* AIM transform (sora.md §4.2): RotZ(aim) [, RotX(180°)], RotY(180°). The
 * global tint FUN_004e8f50 is white in matches (no engine tint yet). */
static aos_dcmd *spr_aim(aos_world *w, aos_player *p, const char *fmt, int frame, float size)
{
	aos_dcmd *c = spr(w, p, fmt, frame, size);
	c->rotZ = aos_rad2bam(p->faceAngle);
	c->flipX = 1;
	if (HALF_PI <= aos_fabs(p->faceAngle))
		c->sy = -1.0f; /* RotX(180°): exact for a flat quad (flip applied in local space before rotZ) */
	return c;
}

/* Rotation handed to FUN_004d7e00 by the AIM draws: {RotX, RotY, RotZ} in bam. */
static void ghost_aim(aos_world *w, aos_player *p, const aos_dcmd *c)
{
	int32_t rot[3] = {HALF_PI <= aos_fabs(p->faceAngle) ? aos_deg2bam(0xb4) : 0, aos_deg2bam(0xb4),
	                  aos_rad2bam(p->faceAngle)};
	aos_afterimage(w, c, 1, p, rot); /* FUN_004d7e00 */
}

static int pp(int32_t t, int n)
{
	int f = (t / n) & 3;
	return f == 3 ? 1 : f;
}

static void dr_neutral(aos_world *w, aos_player *p) /* 0x08 FUN_0073cf10 */
{
	const char *s = "sr_std0%d";
	if (p->up > 1) s = "sr_fwd0%d";
	if (p->down > 1) s = "sr_bck0%d";
	if (p->facingRight == 0 && p->left > 1) s = "sr_fwd0%d";
	if (p->facingRight == 1 && p->left > 1) s = "sr_bck0%d";
	if (p->facingRight == 0 && p->right > 1) s = "sr_bck0%d";
	if (p->facingRight == 1 && p->right > 1) s = "sr_fwd0%d";
	aos_dcmd *c = spr(w, p, s, pp(p->timer, 5), 96.0f);
	c->flipX = p->facingRight != 0; /* RotY(π) when +0x8c set */
	int32_t rot[3] = {0, p->facingRight ? aos_deg2bam(0xb4) : 0, 0};
	aos_afterimage(w, c, 1, p, rot); /* FUN_004d7e00 */
}

static void dr_dash(aos_world *w, aos_player *p) /* 0x09 FUN_0073d1b0 */
{
	int f = (p->timer / 2) % 6;
	float a = aos_fabs(p->faceAngle);
	if ((p->facingRight == 1 && a > HALF_PI) || (p->facingRight == 0 && a < HALF_PI))
		f = pp(p->timer, 3) + 6;
	ghost_aim(w, p, spr_aim(w, p, "sr_dsh0%d", f, 96.0f));
}

static void dr_ko(aos_world *w, aos_player *p) /* 0x0a FUN_0073db60 */
{
	int f = pp(p->timer, 3);
	if (w->frame % 2 == 0)
		f += 3;
	if (p->timer < 100)
		spr_aim(w, p, "sr_los0%d", f, 96.0f);
}

static void dr_hit(aos_world *w, aos_player *p) /* 0x0b FUN_0073d5d0 */
{
	spr_aim(w, p, "sr_los0%d", pp(p->timer, 3), 96.0f);
}

static void dr_sld(aos_world *w, aos_player *p) /* 0x0c/0x1e/0x1f/0x20 FUN_0073d8a0 etc. */
{
	int f = p->timer / 4;
	spr_aim(w, p, "sr_sld0%d", f > 5 ? 5 : f, 96.0f);
}

static void rfl(aos_world *w, aos_player *p, int32_t start)
{
	int f = p->timer < start ? 0 : (p->timer - start) / 3 + 1;
	spr_aim(w, p, "sr_rfl0%d", f > 4 ? 4 : f, 96.0f);
}
static void dr_12(aos_world *w, aos_player *p) { rfl(w, p, 12); }  /* FUN_0073de60 */
static void dr_13(aos_world *w, aos_player *p) { rfl(w, p, 20); }  /* FUN_0073e700 */
static void dr_14(aos_world *w, aos_player *p) { rfl(w, p, 100); } /* FUN_0073e140 */
static void dr_15(aos_world *w, aos_player *p) { rfl(w, p, 5); }   /* FUN_0073e420 */
static void dr_16(aos_world *w, aos_player *p) { rfl(w, p, 36); }  /* FUN_0073e9e0 / 0073ecc0 */

static void dr_21(aos_world *w, aos_player *p) /* 0x21 FUN_00743850 (no tint) */
{
	int f = p->timer < 50 ? 0 : (p->timer - 50) / 4 + 1;
	if (f > 1)
		f = ((f - 2) & 3) + 2;
	if (f == 5)
		f = 3;
	spr_aim(w, p, "sr_rfl0%d", f, 96.0f);
}

/* sword layers (sora.md §4.4) */
static void blade_a(aos_world *w, aos_player *p, int f) { spr_aim(w, p, "sr_swdab0%d", f, 192.0f); }
static void blade_b(aos_world *w, aos_player *p, int f) { spr_aim(w, p, "sr_swdbb0%d", f, 96.0f); }
static void blade_c(aos_world *w, aos_player *p, int f) { spr_aim(w, p, "sr_swdcb0%d", f, 192.0f); }

static void swda(aos_world *w, aos_player *p, int f, bool ghost) /* 0x18 / 0x1d layout */
{
	int b = f == 0 ? 0 : f == 1 ? 1 : 2;
	if (b == 0)
		blade_a(w, p, 0);
	aos_dcmd *body = spr_aim(w, p, "sr_swda0%d", f, 96.0f);
	if (ghost) /* 0x1d only */
		ghost_aim(w, p, body);
	if (f >= 1 && f <= 3)
		spr_aim(w, p, "sr_swdas0%d", f - 1, 192.0f)->blend = AOS_BLEND_ALPHA;
	if (b != 0)
		blade_a(w, p, b);
}
static void dr_18(aos_world *w, aos_player *p) /* FUN_0073f7e0 */
{
	int f = p->timer / 3;
	swda(w, p, f > 6 ? 6 : f, false);
}
static void dr_1d(aos_world *w, aos_player *p) /* FUN_00742c20 */
{
	int f = p->timer / 3;
	swda(w, p, f > 6 ? 6 : f, true);
}

static void dr_19(aos_world *w, aos_player *p) /* FUN_00740370 */
{
	int f = p->timer / 3;
	if (f > 3)
		f = 3;
	int b = f == 0 ? 0 : f == 1 ? 99 : 1;
	spr_aim(w, p, "sr_swdb0%d", f, 96.0f);
	int s = f == 0 ? 0 : f - 1;
	if (p->timer <= 15)
		spr_aim(w, p, "sr_swdbs0%d", s, 96.0f);
	if (b != 99)
		blade_b(w, p, b);
}

static void swdc(aos_world *w, aos_player *p) /* 0x1a FUN_00740be0 / 0x1b FUN_00741520 */
{
	int f = p->timer < 8 ? 0 : (p->timer - 8) / 3 + 1;
	if (f > 5)
		f = 5;
	ghost_aim(w, p, spr_aim(w, p, "sr_swdc0%d", f, 96.0f));
	if (f == 1 || f == 2)
		spr_aim(w, p, "sr_swdcs0%d", f - 1, 192.0f);
	blade_c(w, p, f == 0 ? 0 : 1);
}

static void dr_1c(aos_world *w, aos_player *p) /* FUN_00741e50 */
{
	int32_t t = p->timer;
	int body, f, b;
	if (t < 0x10) {
		body = 0x18;
		f = t / 3;
		if (f > 6) f = 6;
		b = f == 0 ? 0 : f == 1 ? 1 : 2;
	} else if (t < 0x42) {
		f = ((t - 0x10) % 10) / 2;
		if (f > 3) f = 3;
		if (((t - 0x10) / 10) % 2 == 0) {
			body = 0x1a;
			b = f == 0 ? 0 : 1;
		} else {
			body = 0x19;
			b = f == 0 ? 0 : f == 1 ? 99 : 1;
		}
	} else {
		body = 0x1a;
		f = (t - 0x42) / 3 + 4;
		if (f > 5) f = 5;
		b = 1;
	}
	bool backBlade = body == 0x18 && b == 0;
	if (backBlade)
		blade_a(w, p, 0);
	ghost_aim(w, p, spr_aim(w, p, body == 0x18 ? "sr_swda0%d" : body == 0x19 ? "sr_swdb0%d" : "sr_swdc0%d", f, 96.0f));
	int s = 99;
	if (body == 0x18)
		s = (f >= 1 && f <= 3) ? f - 1 : 99;
	else if (body == 0x19)
		s = f == 0 ? 0 : f - 1;
	else
		s = (f == 1 || f == 2) ? f - 1 : 99;
	if (s != 99) {
		if (body == 0x18) spr_aim(w, p, "sr_swdas0%d", s, 192.0f);
		else if (body == 0x19) spr_aim(w, p, "sr_swdbs0%d", s, 96.0f);
		else spr_aim(w, p, "sr_swdcs0%d", s, 192.0f);
	}
	if (!backBlade && b != 99) {
		if (body == 0x18) blade_a(w, p, b);
		else if (body == 0x19) blade_b(w, p, b);
		else blade_c(w, p, b);
	}
}

/* ------------------------------------------------------------------ */
/* beam family (type 3)                                                */

static int beam_spawn(aos_world *w, int32_t id, const float pos[3], float ang, int32_t owner, int32_t age0, int32_t team) /* FUN_004b3fe0 */
{
	if (id != 100 && id != 200 && id != 300 && id != 0x136)
		return -1;
	int i = aos_beam_new(w, ID, id, pos, ang, owner, age0, team);
	if (i < 0)
		return -1;
	aos_beam *b = &w->obj.beam[i];
	switch (id) {
	case 100:
	case 200:
		b->speed = 17.0f; /* _DAT_00813f5c */
		b->damage = id == 100 ? 0xb4 : 0x3c;
		b->shape.halfW = 24.0f; /* DAT_00853b8c */
		b->shape.halfH = 8.0f;  /* DAT_008140d4 */
		b->knockback = 10.0f;
		break;
	case 300:
		b->speed = 15.0f;
		b->damage = 0x82; /* ver >= 0x58c */
		b->shape.radius = 30.0f;
		b->knockback = 10.0f;
		break;
	case 0x136:
		b->speed = 5.0f;
		b->damage = 0;
		b->knockback = 0;
		break;
	}
	return i;
}

static void beam_move(aos_beam *b, float k)
{
	b->pos[0] = aos_cos(b->moveAngle) * b->speed * k + b->pos[0];
	b->pos[1] = aos_sin(b->moveAngle) * b->speed * k + b->pos[1];
}

static bool beam_update(aos_world *w, aos_beam *b) /* FUN_004b47c0 */
{
	int32_t c = aos_team_colour(w, b->team, COL_F4);
	aos_effect *e;
	switch (b->id) {
	case 100:
		beam_move(b, 1.0f);
		if (!aos_in_arena(b->pos, 10.0f))
			return false;
		effS(w, 200, c, b->pos, b->angle, b->ownerId, b->team);
		if (b->age == 1) {
			eff0(w, 0x547, c, b->pos, b->angle, b->ownerId, b->team);
			eff0(w, 0x548, c, b->pos, b->angle, b->ownerId, b->team);
		}
		if (b->age < 10)
			aos_spawn_particle(w, 1000, c, b->pos, b->angle + AOS_PI, b->ownerId, 0, 1);
		break;
	case 200:
		beam_move(b, 1.0f);
		if (b->age > 0x1d || !aos_in_arena(b->pos, 10.0f))
			return false;
		if (b->age == 0x14)
			b->active = 0;
		e = effS(w, 0xd2, c, b->pos, b->angle, b->ownerId, b->team);
		if (e && b->age > 0x14)
			e->moveDir = 1.0f - (float)(0x1e - b->age) / 10.0f;
		if (b->age == 1)
			eff0(w, 0x548, c, b->pos, b->angle, b->ownerId, b->team);
		if (b->age < 5)
			aos_spawn_particle(w, 0x44c, c, b->pos, b->angle + AOS_PI, b->ownerId, 0, 1);
		break;
	case 300:
		beam_move(b, 1.0f);
		if (!aos_in_arena(b->pos, 30.0f))
			return false;
		effS(w, 0xdc, c, b->pos, b->angle, b->ownerId, b->team);
		if (b->age % 8 == 0) {
			eff0(w, 0x546, c, b->pos, b->angle, b->ownerId, b->team);
			eff0(w, 0x548, c, b->pos, b->angle, b->ownerId, b->team);
		}
		aos_spawn_particle(w, 0x456, c, b->pos, b->angle + AOS_PI, b->ownerId, 0, 1);
		break;
	case 0x136:
		beam_move(b, aos_in_arena(b->pos, 5.0f) ? 1.0f : 0.1f);
		if (!aos_in_arena(b->pos, 30.0f))
			return false;
		if (b->age % 4 == 0 && b->age < 0x11)
			aos_spawn_hitbox(w, ID, 100, b->pos, b->angle, b->ownerId, 0, b->team);
		effS(w, 0xdc, c, b->pos, b->angle, b->ownerId, b->team);
		if (b->age % 5 == 0)
			eff0(w, 0x547, c, b->pos, b->angle, b->ownerId, b->team);
		aos_spawn_particle(w, 0x456, c, b->pos, b->angle + AOS_PI, b->ownerId, 0, 1);
		if (b->age > 0x14)
			return false;
		break;
	}
	return true;
}

static void beam_onhit(aos_world *w, aos_beam *b) /* FUN_004b4590 */
{
	int32_t c = aos_team_colour(w, b->team, COL_F4);
	if (b->id == 100) {
		eff0(w, 1000, c, b->pos, b->angle + AOS_PI, b->ownerId, b->team);
		aos_spawn_particle(w, 1000, c, b->pos, b->angle, b->ownerId, 0, 10);
		aos_spawn_spark(w, 1000, c, 10, 10, b->angle, b->pos, b->ownerId);
		aos_se(w, 0x13);
	} else if (b->id == 200) {
		eff0(w, 0x3f2, c, b->pos, b->angle + AOS_PI, b->ownerId, b->team);
		aos_spawn_particle(w, 1000, c, b->pos, b->angle, b->ownerId, 0, 5);
		aos_spawn_spark(w, 1000, c, 6, 6, b->angle, b->pos, b->ownerId);
		aos_se(w, 0x13);
	} else if (b->id == 300) {
		aos_spawn_beam(w, ID, 0x136, b->pos, b->angle, b->ownerId, 0, b->team);
		aos_se(w, 0x13);
	}
}

static int bank_of(aos_world *w, int32_t owner)
{
	aos_player *o = aos_player_by_objid(w, owner);
	return o ? aos_pidx(w, o) : AOS_BANK_P0;
}

/* 2D sprite blit, CSprite vtable +0x68 (dst rect around the screen point, scale 1,
 * src rect, bam angle, ARGB, blend). The original passes bam(−angle) because screen y
 * is down, i.e. +angle in world space; callers pass the WORLD angle. */
static aos_dcmd *blit(int tex, int bank, const float pos[3], float hw, float hh, float worldAngle,
                      int alpha, uint32_t rgb, int blend, int x0, int y0, int x1, int y1)
{
	aos_dcmd *c = aos_draw(tex, bank, pos[0], pos[1], 0);
	c->halfW = hw;
	c->halfH = hh;
	c->rotZ = aos_rad2bam(worldAngle);
	c->blend = (uint8_t)blend;
	c->rgba = (rgb & 0xffffffu) << 8 | (uint32_t)(alpha & 0xff);
	c->hasSrc = 1;
	c->src[0] = (float)x0;
	c->src[1] = (float)y0;
	c->src[2] = (float)x1;
	c->src[3] = (float)y1;
	return c;
}

/* DAT_00885a00 + team*0x10 = the team's sprite slot (player index). */
static int team_bank(int32_t team) { return team == 2 ? AOS_BANK_P1 : AOS_BANK_P0; }

static void beam_draw(aos_world *w, aos_beam *b) /* FUN_004b4310 */
{
	(void)w;
	if (b->id != 300 && b->id != 0x136)
		return;
	/* 2D sprite 0x27 (sr_rflc, `imul eax, edx, 0x27` @0x4b445c) frames 0 then 3, src
	 * (0,0)-(127,127), half (60,40) (DAT_00813a58, DAT_008140e0), bam(−angle). */
	int bank = team_bank(b->team);
	blit(aos_tex("sr_rflc00"), bank, b->pos, 60.0f, 40.0f, b->angle, (int)(128.0f * b->alpha), 0xffffff,
	     AOS_BLEND_ADD_ALPHA, 0, 0, 0x7f, 0x7f);
	blit(aos_tex("sr_rflc03"), bank, b->pos, 60.0f, 40.0f, b->angle, (int)(255.0f * b->alpha), 0xffffff,
	     AOS_BLEND_ALPHA, 0, 0, 0x7f, 0x7f);
}

/* ------------------------------------------------------------------ */
/* missile family (type 3)                                             */

static int missile_spawn(aos_world *w, int32_t id, const float pos[3], float ang, int32_t owner, int32_t age0, int32_t team, int32_t p18) /* FUN_00677970 */
{
	if (id != 100 && id != 0x96)
		return -1;
	int i = aos_missile_new(w, ID, id, pos, ang, owner, age0, team, p18);
	if (i < 0)
		return -1;
	aos_missile *m = &w->obj.missile[i];
	m->growScale = 0;
	m->knockback = 10.0f;
	if (id == 100) {
		m->speed = 5.0f;
		m->hp = 0x3c;
		m->damage = 0x96;
		m->shapeBody.radius = 10.0f;
		m->shapeProj.radius = 10.0f;
	} else {
		m->speed = 6.0f; /* DAT_00813f44 */
		m->hp = 0x78;
		m->damage = 0xfa;
		m->shapeBody.halfW = m->shapeProj.halfW = 30.0f;
		m->shapeBody.halfH = m->shapeProj.halfH = 20.0f;
	}
	return i;
}

static float angle_to_opp(aos_world *w, const aos_missile *m)
{
	aos_player *o = player_of_team(w, m->team == 1 ? 2 : 1); /* FUN_00780480(team) */
	return aos_atan2(o->y - m->pos[1], o->x - m->pos[0]);
}

static void missile_smoke(aos_world *w, aos_missile *m)
{
	float q[3] = {-aos_cos(m->angle) * 20.0f + m->pos[0], -aos_sin(m->angle) * 20.0f + m->pos[1], 0};
	float r = aos_rand_real(w, -AOS_PI, AOS_PI);
	eff0(w, 2000, 0, q, r, m->ownerId, m->team);
}

static void missile_spark(aos_world *w, aos_missile *m)
{
	float r = aos_rand_real(w, -0.2f, 0.2f);
	int32_t n = aos_rand_int(w, 0, 100);
	eff0(w, 0xa28 + n % 8, 0xd, m->pos, r + m->angle, m->ownerId, m->team);
}

static bool missile_onkilled(aos_world *w, aos_missile *m);

static bool missile_update(aos_world *w, aos_missile *m) /* FUN_00679630 */
{
	if (m->growScale >= 1.0f)
		m->growScale = 1.0f;
	else
		m->growScale += 0.2f;
	if (m->id == 100) {
		if (m->age < 0x14) {
			if (m->speed > 2.0f)
				m->speed -= 0.25f;
		} else if (m->age < 100) {
			aos_angle_toward(&m->moveAngle, angle_to_opp(w, m), 0.01f);
			m->angle = m->moveAngle;
			if (m->speed < 12.0f)
				m->speed += 0.8f;
		}
		if (m->age % 2 == 0)
			missile_smoke(w, m);
		if (m->age > 0x13 && m->age < 0x19)
			missile_spark(w, m);
		m->pos[0] = aos_cos(m->moveAngle) * m->speed + m->pos[0];
		m->pos[1] = aos_sin(m->moveAngle) * m->speed + m->pos[1];
		if (m->age > 0x1e && !aos_in_arena(m->pos, 10.0f)) {
			missile_onkilled(w, m);
			return false;
		}
	} else if (m->id == 0x96) {
		if (m->age < 100) {
			if ((double)m->speed > 0.5)
				m->speed -= 0.2f;
			aos_angle_toward(&m->moveAngle, angle_to_opp(w, m), 0.005f);
		} else if (m->age == 100) {
			aos_angle_toward(&m->angle, angle_to_opp(w, m), 0.1f);
			float r = aos_rand_real(w, -0.2f, 0.2f);
			float f = m->angle;
			m->angle = f + r;
			m->moveAngle = f + r;
		} else if (m->age < 0x96) {
			if (m->speed < 14.0f)
				m->speed += 1.0f;
			aos_angle_toward(&m->moveAngle, angle_to_opp(w, m), 0.03f);
			m->angle = m->moveAngle;
		}
		if (m->age > 100)
			missile_smoke(w, m);
		if (m->age > 99 && m->age < 0x6f)
			missile_spark(w, m);
		m->pos[0] = aos_cos(m->moveAngle) * m->speed + m->pos[0];
		m->pos[1] = aos_sin(m->moveAngle) * m->speed + m->pos[1];
		if (m->age > 0x78 && !aos_in_arena(m->pos, 10.0f)) {
			missile_onkilled(w, m);
			return false;
		}
	}
	return true;
}

static bool missile_onhit(aos_world *w, aos_missile *m) /* FUN_00679420 */
{
	if (m->id == 100) {
		eff0(w, 0x262, 0, m->pos, 0, m->ownerId, m->team);
		eff0(w, 0x514, 1, m->pos, 0, m->ownerId, m->team);
		aos_se(w, 0x14);
	} else if (m->id == 0x96) {
		int32_t c = aos_team_colour(w, m->team, COL_F4);
		effS(w, 100, c, m->pos, m->angle + AOS_PI, m->ownerId, m->team);
		eff0(w, 0x514, c, m->pos, 0, m->ownerId, m->team);
		aos_se(w, 0x14);
	}
	return false;
}

static bool missile_onkilled(aos_world *w, aos_missile *m) /* FUN_00679580 */
{
	if (m->id == 100 || m->id == 0x96) {
		eff0(w, 0x262, 0, m->pos, 0, m->ownerId, m->team);
		eff0(w, 0x514, 1, m->pos, 0, m->ownerId, m->team);
		aos_se(w, 0x1d);
	}
	return false;
}

static void missile_draw(aos_world *w, aos_missile *m) /* FUN_00677b20 */
{
	int bank = bank_of(w, m->ownerId);
	bool rocketId = m->id == 0x96;
	if (m->id != 100 && !rocketId)
		return;
	float ph = ((float)(m->age % 50) / 50.0f) * AOS_2PI;
	float g1 = aos_sin(ph) * 0.15f + 0.85f, g2 = aos_sin(ph) * 0.25f + 0.75f;
	uint16_t rz = aos_rad2bam(m->angle);
	if (m->growScale == 1.0f) { /* exhaust */
		float v[2] = {rocketId ? 64.0f : 42.0f, 0.0f};
		aos_vec_rotate(v, m->angle + AOS_PI);
		for (int k = 1; k >= 0; k--) {
			aos_dcmd *c = aos_draw(aos_tex(k ? "sr_bfireb" : "sr_bfire"), bank, v[0] + m->pos[0], v[1] + m->pos[1], 0);
			c->halfW = c->halfH = 24.0f;
			c->rotZ = rz;
			c->blend = AOS_BLEND_ADD_ALPHA;
			if (k == 0 && (m->age / 2) % 2 == 0)
				c->sy = 0.3f;
		}
	}
	float s = m->growScale, v = s >= 1.0f ? 1.0f : 0.0f;
	const char *fmt = rocketId ? "sr_rckt0%d" : "sr_msl0%d";
	float half = rocketId ? 48.0f : 32.0f;
	int base = rocketId ? ((m->age / 3) % 3) * 3 : (m->age / 3) % 3;
	int fr[3] = {base, rocketId ? base + 1 : 3, rocketId ? base + 2 : 4};
	for (int k = 0; k < 3; k++) {
		aos_dcmd *c = aos_draw(aos_texf(fmt, fr[k]), bank, m->pos[0], m->pos[1], 0);
		c->halfW = c->halfH = half;
		c->sx = 3.0f - s * 2.0f;
		c->sy = s;
		c->rotZ = rz;
		c->blend = k == 0 ? AOS_BLEND_ALPHA : AOS_BLEND_ADD_ALPHA;
		c->rgba = k == 0 ? aos_rgba(v, v, v, 1) : aos_rgba(1, 1, 1, k == 1 ? g1 : g2);
	}
}

/* ------------------------------------------------------------------ */
/* hitbox family (type 3)                                              */

static int hitbox_spawn(aos_world *w, int32_t id, const float pos[3], float ang, int32_t owner, int32_t zero, int32_t team) /* FUN_005e9f20 */
{
	static const struct { int32_t id, type, dmg; float hw, hh, r, knock; } T[] = {
		{100, 1, 0x46, 0, 0, 40.0f, 5.0f},          /* ver > 0x58a: 70 */
		{200, 3, 0xb4, 100.0f, 80.0f, 0, 5.0f},
		{0xd2, 3, 0x3c, 0, 0, 100.0f, 5.0f},
		{0xdc, 3, 0x8c, 120.0f, 80.0f, 0, 5.0f},    /* ver > 0x58a: 140 */
		{0xe6, 3, 0xf0, 160.0f, 80.0f, 0, 5.0f},
		{0xfa, 3, 100, 160.0f, 80.0f, 0, 5.0f},
		{0xfb, 3, 0x32, 0, 0, 100.0f, 2.5f},
		{0xfc, 3, 0x32, 0, 0, 100.0f, 2.5f},
		{0x104, 3, 200, 160.0f, 80.0f, 0, 5.0f},
		{300, 3, 0x6e, 0, 0, 320.0f, 5.0f},
		{0x12d, 3, 0, 0, 0, 320.0f, 5.0f},
		{0x15e, 3, 0xa0, 0, 0, 320.0f, 10.0f},
		{0x15f, 3, 0, 0, 0, 320.0f, 10.0f},
	};
	for (size_t k = 0; k < sizeof T / sizeof T[0]; k++) {
		if (T[k].id != id)
			continue;
		int i = aos_hitbox_new(w, ID, id, pos, ang, owner, zero, team);
		if (i < 0)
			return -1;
		aos_hitbox *h = &w->obj.hitbox[i];
		h->hitType = T[k].type;
		h->damage = T[k].dmg;
		h->shape.halfW = T[k].hw;
		h->shape.halfH = T[k].hh;
		h->shape.radius = T[k].r;
		h->knockback = T[k].knock;
		return i;
	}
	return -1;
}

static void streaks6(aos_world *w, aos_hitbox *h, aos_player *v)
{
	int32_t c = aos_team_colour(w, h->team, COL_F4);
	for (int k = -3; k < 3; k++) {
		float base = ((float)k / 3.0f) * AOS_PI;
		float r = aos_rand_real(w, -0.25f, 0.25f);
		eff0(w, 0x456, c, &v->x, base + r, h->ownerId, h->team);
	}
}

static void slash_spark(aos_world *w, aos_hitbox *h, aos_player *v)
{
	float r = aos_rand_real(w, -AOS_PI, AOS_PI);
	eff0(w, 0x9c4, aos_team_colour(w, h->team, COL_F8), &v->x, r, h->ownerId, h->team);
}

static void hitbox_onhit(aos_world *w, aos_hitbox *h, aos_player *v) /* FUN_005ea560 */
{
	int32_t c8 = aos_team_colour(w, h->team, COL_F8);
	aos_player *own = player_of_team(w, h->team);
	switch (h->id) {
	case 100:
		streaks6(w, h, v);
		eff0(w, 0x516, aos_team_colour(w, h->team, COL_F4), &v->x, 0, h->ownerId, h->team);
		aos_se(w, 0x13);
		break;
	case 200:
		slash_spark(w, h, v);
		eff0(w, 0xaf0, c8, &v->x, 0, h->ownerId, h->team);
		own->hitFlag = 1;
		aos_se_cd(w, 0x16, 3);
		break;
	case 0xd2:
		slash_spark(w, h, v);
		own->hitFlag = 1;
		aos_se_cd(w, 0x16, 3);
		break;
	case 0xdc:
		slash_spark(w, h, v);
		eff0(w, 0xaf0, c8, &v->x, 0, h->ownerId, h->team);
		aos_se(w, 0x17);
		break;
	case 0xe6:
		slash_spark(w, h, v);
		eff0(w, 0xaf0, c8, &v->x, 0, h->ownerId, h->team);
		for (int k = 0; k < 3; k++) {
			float r = aos_rand_real(w, -AOS_PI, AOS_PI);
			effS(w, 0x6e, 0, &v->x, r, h->ownerId, h->team);
		}
		own->hitFlag = 1;
		aos_se_cd(w, 0x16, 3);
		break;
	case 0xfa:
		eff0(w, 0xaf0, c8, &v->x, 0, h->ownerId, h->team);
		own->hitFlag = 1;
		aos_camera_focus(w, 0x46, own->objId, &own->x, 0.4f /* DAT_00813f30 */);
		aos_se_cd(w, 0x16, 3);
		break;
	case 0xfb:
	case 0xfc:
		eff0(w, 0x9c4, c8, &v->x, h->angle, h->ownerId, h->team);
		if (h->id == 0xfc)
			eff0(w, 0xaf0, c8, &v->x, 0, h->ownerId, h->team);
		aos_se_cd(w, 0x16, 3);
		own->speed *= 0.75f; /* DAT_00813f3c */
		break;
	case 0x104:
		slash_spark(w, h, v);
		eff0(w, 0xaf0, c8, &v->x, 0, h->ownerId, h->team);
		aos_se(w, 0x17);
		aos_screen_blur(w, 10, 0.75f /* DAT_00813f3c */, 1);
		break;
	case 300:
		h->angle = aos_atan2(h->pos[1] - v->y, h->pos[0] - v->x); /* pull toward the centre */
		streaks6(w, h, v);
		aos_se_cd(w, 0x13, 4);
		break;
	case 0x12d:
		h->angle = aos_atan2(h->pos[1] - v->y, h->pos[0] - v->x);
		aos_se_cd(w, 0x13, 4);
		break;
	case 0x15e:
		streaks6(w, h, v);
		aos_se_cd(w, 0x13, 4);
		break;
	case 0x15f:
		aos_se_cd(w, 0x13, 4);
		break;
	}
}

/* ------------------------------------------------------------------ */
/* laser family (type 3)                                               */

static int laser_spawn(aos_world *w, int32_t id, const float pos[3], float ang, int32_t owner, int32_t team, int32_t p18) /* FUN_00625910 */
{
	if (id != 100)
		return -1;
	int i = aos_laser_new(w, ID, id, pos, ang, owner, team, p18);
	if (i < 0)
		return -1;
	aos_laser *l = &w->obj.laser[i];
	l->damage = 10;
	l->f48 = 200;
	l->hitType = 3;
	l->blockMode = 2;
	l->knockback = 5.0f;
	l->stun = 1;
	return i;
}

static bool laser_update(aos_world *w, aos_laser *l) /* FUN_00626040 */
{
	if (l->id != 100)
		return true;
	l->nseg = 0;
	if (l->age > 0x27)
		return false;
	if (l->age == 1) /* common 100 = graze zone owned by Sora (sora.md note 3) */
		eff0(w, 100, 0, l->origin, l->angle, l->ownerId, l->team);
	aos_shape s = {0, 0, 20.0f};
	if (l->age < 10 || l->age > 0x1e)
		s.radius = 0;
	for (int k = 0; k < 0x3c; k++) {
		float q[3] = {aos_cos(l->angle) * (float)k * 30.0f + l->origin[0],
		              aos_sin(l->angle) * (float)k * 30.0f + l->origin[1], 0};
		aos_laser_push_seg(l, &s, q);
	}
	return true;
}

static void laser_onhit(aos_world *w, aos_laser *l, int32_t seg, aos_player *p) /* FUN_00625ed0 */
{
	(void)p;
	if (l->id != 100 || l->age % 2 != 0 || seg < 0 || seg >= l->nseg)
		return;
	int32_t c = aos_team_colour(w, l->team, COL_F4);
	const float *q = l->seg[seg].pos;
	aos_spawn_spark(w, 1000, c, 10, 1, l->angle, q, l->ownerId);
	for (int k = -2; k < 3; k++) {
		float base = (float)k * 0.65f + l->angle; /* DAT_0081788c */
		float r = aos_rand_real(w, -0.45f, 0.45f);
		eff0(w, 0x456, c, q, base + r, l->ownerId, l->team);
	}
	aos_se_cd(w, 0x13, 4);
}

static void laser_draw(aos_world *w, aos_laser *l) /* FUN_00625a10 (sprite 0x26 sr_laser) */
{
	if (l->id != 100)
		return;
	int bank = bank_of(w, l->ownerId);
	int tex = aos_tex("sr_laser");
	uint16_t rz = aos_rad2bam(l->angle);
	bool pulse = (l->age / 3) % 2 == 0;
	for (int k = 0; k < l->nseg; k++) {
		const float *q = l->seg[k].pos;
		if (k == 0) {
			aos_dcmd *c = aos_draw(tex, bank, q[0], q[1], 0);
			c->halfW = c->halfH = 60.0f + (pulse ? 5.0f : 0.0f);
			c->hasSrc = 1;
			c->src[0] = 448, c->src[1] = 64, c->src[2] = 512, c->src[3] = 127;
			c->rotZ = rz;
			c->blend = AOS_BLEND_ADD_ALPHA;
			continue;
		}
		float hh = 50.0f + (pulse ? 10.0f : 0.0f);
		if (l->age > 0x1e)
			hh = ((float)(0x28 - l->age) / 10.0f) * hh;
		if (l->age < 10)
			hh = ((float)l->age / 10.0f) * hh;
		aos_dcmd *c = aos_draw(tex, bank, q[0], q[1], 0);
		c->halfW = 80.0f;
		c->halfH = hh;
		c->hasSrc = 1;
		c->src[0] = 320, c->src[1] = 64, c->src[2] = 383, c->src[3] = 127;
		c->rotZ = rz;
		c->blend = AOS_BLEND_ADD_ALPHA;
		c->rgba = 0xffffff20u;
		c = aos_draw(tex, bank, q[0], q[1], 0);
		c->halfW = 120.0f;
		c->halfH = 20.0f;
		c->hasSrc = 1;
		c->src[0] = 384, c->src[1] = 64, c->src[2] = 447, c->src[3] = 127;
		c->rotZ = rz;
		c->blend = AOS_BLEND_ADD_ALPHA;
		c->rgba = 0xffffff20u;
	}
}

/* ------------------------------------------------------------------ */
/* effect family (type 3)                                              */

static int effect_spawn(aos_world *w, int32_t id, int32_t var, const float pos[3], float ang, int32_t owner, int32_t age0, int32_t team) /* FUN_005070d0 */
{
	if (id != 100 && id != 0x6e && id != 200 && id != 0xd2 && id != 0xdc && id != 300 && id != 0x15e)
		return -1;
	float r = 0;
	if (id == 0x6e) /* the spawn itself draws RNG */
		r = aos_rand_real(w, 0.25f, 0.75f); /* DAT_00813f24, DAT_00813f3c */
	int i = aos_effect_new(w, ID, id, var, pos, ang, owner, age0, team);
	if (i < 0)
		return -1;
	aos_effect *e = &w->obj.effect[i];
	e->drawLayer = 1;
	e->rotation = ang;
	e->aux = 0;
	e->aux2 = 0;
	switch (id) {
	case 100:
		e->scale = 2.0f; e->alpha = 0; e->moveDir = ang;
		break;
	case 0x6e:
		e->aux2 = 1.0f; e->alpha = 1.0f; e->scale = 0; e->aux = 0.175f; e->moveDir = r;
		break;
	case 200:
		e->alpha = 1.0f; e->scale = 1.0f; e->moveDir = ang;
		break;
	case 0xd2:
	case 0xdc:
	case 300:
	case 0x15e:
		e->alpha = 1.0f; e->scale = 1.0f; e->moveDir = 1.0f;
		if (id == 0xdc || id == 300)
			e->drawLayer = 0;
		break;
	}
	return i;
}

static bool effect_update(aos_world *w, aos_effect *e) /* FUN_00508d60 */
{
	switch (e->id) {
	case 100:
		if (e->age < 100) {
			e->alpha += 0.1f;
			e->scale -= 0.1f;
			if (e->scale <= 1.0f) {
				e->alpha = 1.0f;
				e->scale = 1.0f;
				e->age = 100;
				e->drawLayer = 0;
			}
		} else if (e->age > 0x77) {
			e->alpha -= 0.05f;
			e->scale -= 0.05f;
			if (e->scale <= 0.0f)
				return false;
		}
		if (e->age < 0x65 && e->age % 5 == 0) {
			float q[3] = {e->pos[0], e->pos[1], e->pos[2]};
			float r = aos_rand_real(w, -AOS_PI, AOS_PI);
			effS(w, 0x6e, 0, q, r, e->ownerId, e->team);
			return true;
		}
		break;
	case 0x6e:
		if (0.0f < e->aux2)
			e->aux2 -= 0.2f;
		e->aux2 = 0; /* literal: overwritten */
		if (e->age < 0x14) {
			e->scale += e->aux;
			e->aux *= 0.8f;
			if (e->aux < 0.02f)
				e->aux = 0.02f;
		} else {
			e->alpha -= 0.1f;
			if (e->alpha <= 0.0f)
				return false;
		}
		break;
	case 200:
		if (e->age > 5)
			e->alpha -= 0.1f;
		if (e->alpha <= 0.0f)
			return false;
		break;
	case 0xd2:
		if (e->age > 2) {
			e->alpha -= 0.1f;
			e->scale -= 0.075f;
		}
		if (e->alpha <= 0.0f)
			return false;
		break;
	case 0xdc:
		if (e->age > 2)
			e->scale -= 0.05f;
		if (e->age > 5)
			e->alpha -= 0.1f;
		if (e->alpha <= 0.0f)
			return false;
		break;
	case 300:
		e->alpha -= 0.35f;
		if (e->alpha <= 0.0f)
			return false;
		break;
	case 0x15e: /* Unlimited Charge wave: gameplay */
		e->pos[0] = aos_cos(e->rotation) * 24.0f + e->pos[0];
		e->pos[1] = aos_sin(e->rotation) * 24.0f + e->pos[1];
		e->aux2 = e->age % 2 == 0 ? 0.85f : 1.0f;
		aos_spawn_hitbox(w, ID, e->age % 4 == 0 ? 0x15e : 0x15f, e->pos, e->rotation, e->ownerId, 0, e->team);
		if (e->age > 0x4f)
			return false;
		for (int k = 0; k < 2; k++) {
			float a = aos_rand_real(w, -HALF_PI, HALF_PI);
			float v[2] = {aos_cos(a) * 160.0f - 40.0f, aos_sin(a) * 300.0f};
			aos_vec_rotate(v, e->rotation);
			float q[3] = {v[0] + e->pos[0], v[1] + e->pos[1], 0};
			aos_spawn_spark(w, 0x7da, aos_team_colour(w, e->team, COL_F4), 10, 1, e->rotation + AOS_PI, q, e->ownerId);
		}
		break;
	}
	return true;
}

/* Rifle-bolt body (effects 200 / 0xd2 / 0xdc): beamemitb (DAT_0087fdf4) / beamemit
 * (DAT_0087fdf0), cell from the colour index (variant): x = (v/8)*0xf0, y = (v%8)*0x40. */
static void bolt_draw(aos_effect *e, float h1, float h2, float h3, float a1, float a2, int fresh)
{
	int v = e->variant < 0 ? 0 : e->variant, x0 = (v / 8) * 0xf0, y0 = (v % 8) * 0x40;
	blit(aos_tex("beamemitb"), AOS_BANK_COMMON, e->pos, 60.0f, h1, e->rotation, (int)a1, 0xffffff,
	     AOS_BLEND_ALPHA, x0, y0, x0 + 0x77, y0 + 0x3f);
	if (e->age < 2)
		blit(aos_tex("beamemit"), AOS_BANK_COMMON, e->pos, 48.0f, h3, e->rotation, 0xff, 0xffffff,
		     AOS_BLEND_ADD_ALPHA, (v / 8) * 0x100, y0, (v / 8) * 0x100 + 0x7f, y0 + 0x3f);
	else
		blit(aos_tex("beamemitb"), AOS_BANK_COMMON, e->pos, 40.0f, h2, e->rotation, (int)a2,
		     e->age < fresh ? 0xffffff : 0x0080ff, AOS_BLEND_ADD_ALPHA, x0 + 0x78, y0, x0 + 0xef, y0 + 0x3f);
}

static void effect_draw(aos_world *w, aos_effect *e) /* FUN_00507530 */
{
	(void)w;
	switch (e->id) {
	case 100: { /* sheet (&DAT_0087fccc)[v/8] = explosion0/1, cell from v%8 */
		int v = e->variant < 0 ? 0 : e->variant;
		int tex = aos_tex((v / 8) ? "explosion1" : "explosion0");
		int x = ((v % 8) / 4) << 8, y = ((v % 8) % 4) << 7;
		/* passes bam(+rotation) (no sign flip) -> world angle = −rotation */
		blit(tex, AOS_BANK_COMMON, e->pos, 210.0f * e->scale, 210.0f * e->scale, -e->rotation,
		     (int)(128.0f * e->alpha), 0xffffff, AOS_BLEND_ADD_ALPHA, x, y, x + 0x7f, y + 0x7f); /* DAT_008178d8 */
		float h = 240.0f * e->scale; /* DAT_00815584 */
		if (e->age % 2 == 0)
			h *= 0.85f; /* DAT_00814120 */
		blit(tex, AOS_BANK_COMMON, e->pos, h, h, -e->rotation, (int)(255.0f * e->alpha), 0xffffff,
		     AOS_BLEND_ADD_ALPHA, x + 0x80, y, x + 0xff, y + 0x7f);
		break;
	}
	case 0x6e: { /* sprite 0x25 frame 2 (sr_exp02), grey = 255·+0x40, alpha 64·a */
		float g = e->aux2;
		blit(aos_tex("sr_exp02"), team_bank(e->team), e->pos, 300.0f * e->scale, 300.0f * e->scale * e->moveDir,
		     e->rotation, (int)(64.0f * e->alpha), aos_rgba(g, g, g, 0) >> 8, AOS_BLEND_ALPHA, 0, 0, 0xff, 0xff);
		break;
	}
	case 200:
		bolt_draw(e, 60.0f, 10.0f, 18.0f, 32.0f * e->alpha, 255.0f * e->alpha, 6);
		break;
	case 0xd2:
		bolt_draw(e, 50.0f * e->scale, 8.0f * e->scale, 16.0f * e->scale, 32.0f * e->alpha * e->moveDir,
		          255.0f * e->alpha * e->alpha, 6);
		break;
	case 0xdc:
		bolt_draw(e, 60.0f * e->scale, 40.0f * e->scale, 64.0f * e->scale, 32.0f * e->alpha * e->moveDir,
		          255.0f * e->alpha * e->alpha, 4);
		break;
	case 300:
	case 0x15e: {
		/* +0x60 blit (no rotation) of frames 0 and 1 of the sprite id left in the draw
		 * global DAT_00895abc — only the 0x6e case writes it (0x25). UNVERIFIED stale
		 * value; 0x25 (sr_exp) assumed. Half size +0x3c·+0x40, alpha 64·a then 255·a. */
		float h = e->aux * e->aux2;
		int bank = team_bank(e->team);
		blit(aos_tex("sr_exp00"), bank, e->pos, h, h, 0, (int)(64.0f * e->alpha), 0xffffff, AOS_BLEND_ADD_ALPHA, 0, 0, 0xff, 0xff);
		blit(aos_tex("sr_exp01"), bank, e->pos, h, h, 0, (int)(255.0f * e->alpha), 0xffffff, AOS_BLEND_ADD_ALPHA, 0, 0, 0xff, 0xff);
		break;
	}
	}
}

/* ------------------------------------------------------------------ */

void aos_register_sora(void)
{
	aos_tab.inputA[ID] = input_a;
	aos_tab.inputB[ID] = input_b;
	/* CPU AI FUN_0073c750 / FUN_0073bdf0 not ported */

	aos_tab.T2[0x08] = st_neutral;
	for (int s = 0x09; s <= 0x0c; s++)
		aos_tab.T2[s] = st_stub;
	aos_tab.T2[0x12] = st_rifle_quick;
	aos_tab.T2[0x13] = st_rifle_spread;
	aos_tab.T2[0x14] = st_rifle_press;
	aos_tab.T2[0x15] = st_rifle_finish;
	aos_tab.T2[0x16] = st_hyper_spread;
	aos_tab.T2[0x17] = st_hyper_spread_d;
	aos_tab.T2[0x18] = st_sword1;
	aos_tab.T2[0x19] = st_sword2;
	aos_tab.T2[0x1a] = st_sword3;
	aos_tab.T2[0x1b] = st_slash_through;
	aos_tab.T2[0x1c] = st_dance;
	aos_tab.T2[0x1d] = st_dance_finish;
	aos_tab.T2[0x1e] = st_launcher_homing;
	aos_tab.T2[0x1f] = st_launcher_press;
	aos_tab.T2[0x20] = st_hyper_launcher;
	aos_tab.T2[0x21] = st_unlimited;

	aos_tab.T1[0x08] = dr_neutral;
	aos_tab.T1[0x09] = dr_dash;
	aos_tab.T1[0x0a] = dr_ko;
	aos_tab.T1[0x0b] = dr_hit;
	aos_tab.T1[0x0c] = dr_sld;
	aos_tab.T1[0x12] = dr_12;
	aos_tab.T1[0x13] = dr_13;
	aos_tab.T1[0x14] = dr_14;
	aos_tab.T1[0x15] = dr_15;
	aos_tab.T1[0x16] = dr_16;
	aos_tab.T1[0x17] = dr_16;
	aos_tab.T1[0x18] = dr_18;
	aos_tab.T1[0x19] = dr_19;
	aos_tab.T1[0x1a] = swdc;
	aos_tab.T1[0x1b] = swdc;
	aos_tab.T1[0x1c] = dr_1c;
	aos_tab.T1[0x1d] = dr_1d;
	aos_tab.T1[0x1e] = dr_sld;
	aos_tab.T1[0x1f] = dr_sld;
	aos_tab.T1[0x20] = dr_sld;
	aos_tab.T1[0x21] = dr_21;

	aos_otab.beamSpawn[ID] = beam_spawn;
	aos_otab.beamUpdate[ID] = beam_update;
	aos_otab.beamDraw[ID] = beam_draw;
	aos_otab.beamOnHit[ID] = beam_onhit;
	aos_otab.missileSpawn[ID] = missile_spawn;
	aos_otab.missileUpdate[ID] = missile_update;
	aos_otab.missileDraw[ID] = missile_draw;
	aos_otab.missileOnHit[ID] = missile_onhit;
	aos_otab.missileOnKilled[ID] = missile_onkilled;
	aos_otab.hitboxSpawn[ID] = hitbox_spawn;
	aos_otab.hitboxOnHit[ID] = hitbox_onhit;
	aos_otab.laserSpawn[ID] = laser_spawn;
	aos_otab.laserUpdate[ID] = laser_update;
	aos_otab.laserDraw[ID] = laser_draw;
	aos_otab.laserOnHit[ID] = laser_onhit;
	aos_otab.effectSpawn[ID] = effect_spawn;
	aos_otab.effectUpdate[ID] = effect_update;
	aos_otab.effectDraw[ID] = effect_draw;
}
