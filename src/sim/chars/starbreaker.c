/* Star Breaker (engine id 9, states 0x101..0x11d) — 1:1 port of aos2.exe.
 * Spec: docs/re/starbreaker.md. Every function names its original address.
 * Version 0x5a2 branches only.
 *
 * Not ported (unreachable in the original, starbreaker.md §5.8): state 0x10b
 * (FUN_0074b1c0) and its twin FUN_0074b3c0, enemy ids 500/600, the smoke
 * family (FUN_007bb450..), the type-9 beam family (FUN_004b5130), missile 200,
 * SB effect 800 and laser 200. */
#include <stdio.h>
#include <string.h>

#include "../draw.h"
#include "../fx.h"
#include "../sim.h"

#define ID AOS_CH_STARBREAKER
#define HALF_PI 1.5707964f /* DAT_00814104 */


/* Per-player extended state (original player +0x90 float). */
typedef struct {
	float charge; /* +0x90: Sea of Flame charge speed (FUN_0074a900) */
} sb_ext;
static sb_ext *X(aos_player *p) { return (sb_ext *)p->ext.raw; }

/* ------------------------------------------------------------------ */
/* small helpers                                                       */

static aos_player *own_player(aos_world *w, int32_t team) /* FUN_00780450(team) */
{
	if (team == 1)
		return &w->p[0];
	if (team == 2)
		return &w->p[1];
	return NULL;
}

/* FUN_00780a50 — "victim can be grabbed". ENGINE REQUEST: promote to core. */
static bool can_grab(aos_world *w, const aos_player *v)
{
	return v->barrierTime == 0 && v->state != AOS_ST_KO && v->hittable &&
	       v->statusOn[AOS_STATUS_STUNIMMUNE] != 1 && !w->roundOver;
}

/* FUN_006b1d70(src, range, speed) — proximity knock of the opponent.
 * ENGINE REQUEST: promote to core (core.c inlines the same code in st_hitstun). */
static bool knock_near(aos_world *w, aos_player *src, float range, float speed)
{
	aos_player *o = aos_opponent(w, src);
	float dx = src->x - o->x, dy = src->y - o->y;
	if (o->state != AOS_ST_KO && o->hittable == 1 && o->statusOn[AOS_STATUS_STUNIMMUNE] == 0 &&
	    aos_sqrt(dx * dx + dy * dy) < range) {
		o->state = AOS_ST_HITSTUN;
		o->timer = 0;
		o->faceAngle = aos_atan2(src->y - o->y, src->x - o->x);
		o->moveAngle = o->faceAngle + AOS_PI;
		o->speed = speed;
		aos_se(w, 0x19);
		aos_camera_focus_clear(w); /* FUN_004a9f10 with ECX = 0x895144 (focus vector) */
		return true;
	}
	return false;
}

/* DAT_008859f4/f8/fc + team*0x10 */
static int team_col(aos_world *w, int32_t team, int k) { return aos_team_colour(w, team, k); }
/* DAT_00885a00 + team*0x10 = that player's +0x18 (all.c:284493, round init). */
static int team_pal(aos_world *w, int32_t team) /* DAT_00885a00 + team*0x10 */
{
	aos_player *p = own_player(w, team);
	return p ? p->color : 0;
}
static int team_bank(int32_t team) { return team == 2 ? AOS_BANK_P1 : AOS_BANK_P0; }

static int sbfx(aos_world *w, int id, int var, const float pos[3], float ang, int32_t owner, int32_t age0, int32_t team)
{
	return aos_spawn_effect(w, ID, id, var, pos, ang, owner, age0, team);
}
static int gfx(aos_world *w, int id, int var, const float pos[3], float ang, int32_t owner, int32_t age0, int32_t team)
{
	return aos_spawn_effect(w, 0, id, var, pos, ang, owner, age0, team);
}

static aos_enemy *new_enemy(aos_world *w, aos_player *p, int id)
{
	int i = aos_spawn_enemy(w, ID, id, p->objId, p->side, p->color);
	return i < 0 ? NULL : &w->obj.enemy[i];
}

static void face_opp_x(aos_world *w, aos_player *p)
{
	aos_player *o = aos_opponent(w, p);
	p->faceAngle = (o->x < p->x || o->x == p->x) ? AOS_PI : 0.0f;
	p->dashAngle = p->faceAngle;
}

static void dash_sparks(aos_world *w, aos_player *p, float dir) /* dash sparks, suguri.md §1.3 */
{
	float r = aos_rand_real(w, -0.3f, 0.3f);
	int n = aos_rand_int(w, 0, 100) % 8;
	gfx(w, 0xa28 + n, 0xc, &p->x, r + dir, p->objId, 0, p->side);
}

/* ------------------------------------------------------------------ */
/* input dispatch                                                      */

static bool normal_moves(aos_world *w, aos_player *p)
{
	if (p->state != AOS_ST_NEUTRAL && p->state != AOS_ST_DASH)
		return false;
	if (p->D == 3) {
		aos_dash(w, p, 0);
	} else if (p->Shield == 3 && aos_spend_hyper(p, 0.5f) && aos_may_shield(w, p)) {
		aos_shield(w, p);
	} else {
		if (p->S > 1 && p->B == 3) { aos_set_state(w, p, 0x110); return true; }
		if (p->S > 1 && p->A == 3) { aos_set_state(w, p, 0x116); return true; }
		if (p->B == 3) { aos_set_state(w, p, 0x10f); return true; }
		if (p->D > 1 && p->A == 3) { aos_set_state(w, p, 0x10c); return true; }
		if (p->A == 3) { aos_set_state(w, p, 0x113); return true; }
	}
	return false;
}

static bool input_a(aos_world *w, aos_player *p) /* FUN_0074b900 */
{
	if (aos_may_hyper(w, p)) {
		if (p->S > 1 && p->B > 1 && p->H == 3 && aos_spend_hyper(p, 3.0f)) { aos_set_state(w, p, 0x11d); return true; }
		if (p->S > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f)) { aos_set_state(w, p, 0x119); return true; }
		if (p->B > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f)) { aos_set_state(w, p, 0x10e); return true; }
		if (p->D > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f)) { aos_set_state(w, p, 0x111); return true; }
		if (p->H == 3 && aos_spend_hyper(p, 1.0f)) { aos_set_state(w, p, 0x11b); return true; }
	}
	if (normal_moves(w, p))
		return true;
	return aos_tab.T2[p->state] ? aos_tab.T2[p->state](w, p) : true;
}

static bool input_b(aos_world *w, aos_player *p) /* FUN_0074bdc0 */
{
	if (aos_may_hyper(w, p)) {
		if (p->H > 1 && p->Shield == 3 && aos_spend_hyper(p, 3.0f)) { aos_set_state(w, p, 0x11d); return true; }
		if (p->H > 1 && p->S == 3 && aos_spend_hyper(p, 1.0f)) { aos_set_state(w, p, 0x119); return true; }
		if (p->H > 1 && p->B == 3 && aos_spend_hyper(p, 1.0f)) { aos_set_state(w, p, 0x10e); return true; }
		if (p->H > 1 && p->D == 3 && aos_spend_hyper(p, 1.0f)) { aos_set_state(w, p, 0x111); return true; }
		if (p->H > 1 && p->A == 3 && aos_spend_hyper(p, 1.0f)) { aos_set_state(w, p, 0x11b); return true; }
	}
	if (normal_moves(w, p))
		return true;
	return aos_tab.T2[p->state] ? aos_tab.T2[p->state](w, p) : true;
}

/* ------------------------------------------------------------------ */
/* states (T2)                                                         */

static bool st_neutral(aos_world *w, aos_player *p) /* 0x101 T2 FUN_00747710 */
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

static bool st_stub(aos_world *w, aos_player *p) /* 0x102..0x105 T2 FUN_00747800/830/810/820 */
{
	(void)w;
	(void)p;
	return true;
}

/* standard "dash-cancel from dc, end after end" tail */
static void tail(aos_world *w, aos_player *p, int dc, int end)
{
	if (p->timer >= dc && p->D == 3)
		aos_dash(w, p, 1);
	else if (p->timer > end)
		p->state = AOS_ST_NEUTRAL;
}

static bool st_da(aos_world *w, aos_player *p) /* 0x10c T2 FUN_00748870 — D+A */
{
	int t = p->timer;
	if (t == 0)
		p->faceAngle = aos_aim(w, p);
	if (t == 10)
		aos_se(w, 0x26);
	if (t > 9 && t <= 0x16 && t % 2 == 0 && aos_use_gauge(p, 0x10c)) {
		p->faceAngle = aos_aim(w, p);
		float r = aos_rand_real(w, 10.0f, 70.0f);   /* drawn, then overridden (ver > 0x5a0) */
		float a = aos_rand_real(w, -0.25f, 0.25f);
		switch (t) {
		case 10: r = 10.0f; a = -0.25f; break;
		case 0xc: r = 20.0f; a = -0.15f; break;
		case 0xe: r = 30.0f; a = -0.05f; break;
		case 0x10: r = 40.0f; a = 0.05f; break;
		case 0x12: r = 50.0f; a = 0.15f; break;
		case 0x14: r = 60.0f; a = 0.25f; break;
		default: r = 0.0f; a = 0.0f; break;
		}
		float pt[3] = {0, 0, 0};
		if (t == 0x16) {
			a = a + p->faceAngle;
			pt[0] = aos_cos(a) * r + p->x;
			pt[1] = aos_sin(a) * r + p->y;
			aos_spawn_missile(w, ID, 0x12e, pt, p->faceAngle, p->objId, 0, p->side, p->color);
		} else if ((t - 10) % 4 == 0) {
			a = p->faceAngle + HALF_PI + a;
			pt[0] = aos_cos(a) * r + p->x;
			pt[1] = aos_sin(a) * r + p->y;
			aos_spawn_missile(w, ID, 300, pt, p->faceAngle + 0.3f, p->objId, 0, p->side, p->color);
		} else {
			a = (p->faceAngle - HALF_PI) + a;
			pt[0] = aos_cos(a) * r + p->x;
			pt[1] = aos_sin(a) * r + p->y;
			aos_spawn_missile(w, ID, 0x12d, pt, p->faceAngle - 0.3f, p->objId, 0, p->side, p->color);
		}
	}
	if (p->timer >= 0x14 && p->D == 3) {
		aos_dash(w, p, 1);
	} else {
		if (p->timer > 0x23)
			p->state = AOS_ST_NEUTRAL;
		p->friction *= 0.5f;
	}
	return true;
}

static bool st_btap(aos_world *w, aos_player *p) /* 0x10d T2 FUN_00747840 — B tap */
{
	if (p->timer == 0xe && aos_use_gauge(p, 0x10d)) {
		for (int k = 0; k < 2; k++) {
			aos_enemy *e = new_enemy(w, p, 200);
			if (!e)
				continue;
			e->pos[0] = p->x;
			e->pos[1] = p->y;
			e->pos[2] = p->z;
			e->angle = k == 0 ? p->faceAngle - 2.0f : p->faceAngle + 2.0f;
			e->angle2 = e->angle;
			e->f70 = 16.0f;
		}
		aos_se(w, 0x22);
	}
	tail(w, p, 0x14, 0x23);
	return true;
}

static bool st_bh(aos_world *w, aos_player *p) /* 0x10e T2 FUN_00747a80 — B+H */
{
	int t = p->timer;
	if (t == 0) {
		p->faceAngle = aos_aim(w, p);
		aos_barrier(w, p, 10);
		aos_hyper_flourish_ex(w, p, 6, 100, 0.75f, 100, 0.0f, 0.0f, 0x14, 0.5f, 2, 1);
	}
	if (t > 9 && t < 0x29 && (t - 10) % 3 == 0) {
		aos_enemy *e = new_enemy(w, p, 200);
		if (e) {
			e->f7c = 0xfa;
			e->pos[0] = p->x;
			e->pos[1] = p->y;
			e->pos[2] = p->z;
			e->f70 = 20.0f;
			if ((t - 10) % 6 == 0)
				e->angle = (p->faceAngle - AOS_PI) + (float)(t - 10) * 0.05f;
			else
				e->angle = (p->faceAngle + AOS_PI) - (float)(t - 10) * 0.05f;
			aos_angle_normalize(&e->angle);
			e->angle2 = e->angle;
		}
		aos_se_cd(w, 0x22, 6);
	}
	tail(w, p, 0x32, 0x50);
	return true;
}

/* missile search by serial (FUN_00685ce0) */
static aos_missile *missile_by_serial(aos_world *w, int32_t serial)
{
	for (int i = 0; i < w->obj.nmissile; i++)
		if (w->obj.missile[i].serial == serial)
			return &w->obj.missile[i];
	return NULL;
}

/* FUN_007c81e0: original +0x08 = palette (texture bank) -> ownerId slot,
 * +0x10 = missile serial -> p3. */
static int whip_spawn(aos_world *w, int32_t id, int32_t p18, int32_t serial)
{
	if (id != 4)
		return -1;
	return aos_whip_new(w, ID, id, p18, serial);
}

static bool st_bhold(aos_world *w, aos_player *p) /* 0x10f T2 FUN_00747d80 — B press/hold */
{
	if (p->timer == 0)
		face_opp_x(w, p);
	if (p->timer < 0xc && p->B < 1) {
		p->state = 0x10d;
		p->faceAngle = aos_aim(w, p);
		aos_tab.T2[0x10d](w, p);
		return true;
	}
	if (p->timer == 0xc && aos_use_gauge(p, 0x10f)) {
		int mi = aos_spawn_missile(w, ID, 100, &p->x, 0.0f, p->objId, 0, p->side, p->color);
		aos_player *o = aos_opponent(w, p);
		float tx = aos_rand_real(w, -160.0f, 160.0f) + o->x;
		float ty = aos_rand_real(w, -160.0f, 160.0f) + o->y;
		if (p->up > 1) ty += 240.0f;
		if (p->down > 1) ty -= 240.0f;
		if (p->left > 1) tx -= 240.0f;
		if (p->right > 1) tx += 240.0f;
		float a = aos_rand_real(w, -AOS_PI, AOS_PI);
		float s = aos_rand_real(w, 0.0f, 10.0f) + 10.0f;
		if (mi >= 0) {
			aos_missile *m = &w->obj.missile[mi];
			m->moveAngle = a;
			m->speed = s;
			m->f34[6] = tx; /* +0x4c target */
			m->f34[7] = ty;
			m->f34[8] = 0;  /* +0x54 uninitialised local in the original; unused */
			whip_spawn(w, 4, p->color, m->serial);
		}
		aos_se_cd(w, 0x2f, 3);
	}
	if (p->timer == 0x10 && p->B > 1)
		p->timer = 10;
	else
		tail(w, p, 0x19, 0x23);
	return true;
}

static bool st_sb(aos_world *w, aos_player *p) /* 0x110 T2 FUN_00748140 — S+B */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer == 10 && aos_use_gauge(p, 0x110)) {
		for (int k = 0; k < 2; k++) {
			aos_enemy *e = new_enemy(w, p, 100);
			if (!e)
				continue;
			float side = k == 0 ? HALF_PI : -HALF_PI;
			e->pos[0] = p->x;
			e->pos[1] = p->y;
			e->pos[2] = p->z;
			e->angle = k == 0 ? p->faceAngle + 0.25f : p->faceAngle - 0.25f;
			e->f70 = k == 0 ? -0.009f : 0.009f; /* DAT_00825fc8 / DAT_00825fc0 */
			e->pos[0] = aos_cos(p->faceAngle + side) * 100.0f + e->pos[0];
			e->pos[1] = aos_sin(p->faceAngle + side) * 100.0f + e->pos[1];
		}
		aos_se(w, 0x2f);
	}
	tail(w, p, 0x19, 0x28);
	return true;
}

static bool st_dh(aos_world *w, aos_player *p) /* 0x111 T2 FUN_007484a0 — D+H */
{
	if (p->timer == 0) {
		face_opp_x(w, p);
		aos_barrier(w, p, 10);
		aos_hyper_flourish_ex(w, p, 6, 100, 0.75f, 100, 0.0f, 0.0f, 0x14, 0.5f, 2, 1);
	}
	if (p->timer == 5) {
		for (int i = -4; i < 4; i++) {
			aos_enemy *e = new_enemy(w, p, 100);
			if (!e)
				continue;
			e->f7c = 0x6e;
			if (i % 2 == 0) {
				e->f70 = 0.0065f;                               /* DAT_00825fbc */
				e->angle = ((float)i / 4.0f) * AOS_PI - 0.2f;
			} else {
				e->f70 = -0.0065f;                              /* DAT_00825fc4 */
				e->angle = ((float)i / 4.0f) * AOS_PI + 0.2f;
			}
			e->pos[0] = p->x;
			e->pos[1] = p->y;
			e->pos[2] = p->z;
			e->pos[0] = aos_cos(e->angle) * 100.0f + e->pos[0];
			e->pos[1] = aos_sin(e->angle) * 100.0f + e->pos[1];
			e->pivot[0] = p->x; /* +0xbc..+0xc4 orbit centre */
			e->pivot[1] = p->y;
			e->fc4 = p->z;
		}
		aos_se(w, 0x2f);
	}
	tail(w, p, 0x28, 0x3c);
	return true;
}

/* chain hits: 0x113/0x116 (stage 1), 0x114/0x117 (2), 0x115/0x118 (3) */
static void lunge(aos_world *w, aos_player *p, float div, float cap)
{
	p->faceAngle = aos_aim(w, p);
	float d = aos_dist(w, p) / div;
	if (cap <= d)
		d = cap;
	p->dashAngle = p->faceAngle;
	p->moveAngle = p->faceAngle;
	p->speed = d;
}
static void flame(aos_world *w, aos_player *p, int id, float a2)
{
	aos_enemy *e = new_enemy(w, p, id);
	if (e)
		e->angle2 = a2;
}
static void hit(aos_world *w, aos_player *p, int id)
{
	aos_spawn_hitbox(w, ID, id, &p->x, p->faceAngle, p->objId, 0, p->side);
}
static bool chain_tail(aos_world *w, aos_player *p, int next, int dc)
{
	if (p->timer >= 10 && p->hitFlag == 1 && p->A == 3)
		aos_set_state(w, p, next);
	else
		tail(w, p, dc, 0x32);
	return true;
}

static bool chain1(aos_world *w, aos_player *p, int va, int vb, int h1, int h2, int next)
{
	if (p->timer == 0) {
		lunge(w, p, 12.0f, 14.0f);
		flame(w, p, va, p->faceAngle + 0.5f);
		flame(w, p, vb, p->faceAngle - 0.5f);
		aos_se(w, 0x26);
		aos_afterimage_clear(w, p->objId);
	}
	if ((p->timer == 10 || p->timer == 0xd) && aos_use_gauge(p, 0x113))
		hit(w, p, h1);
	if (p->timer == 0x10 && aos_use_gauge(p, 0x113))
		hit(w, p, h2);
	return chain_tail(w, p, next, 0x19);
}
static bool chain2(aos_world *w, aos_player *p, int va, int vb, int h1, int h2, int next)
{
	if (p->timer == 0) {
		lunge(w, p, 15.0f, 8.0f);
		flame(w, p, va, p->faceAngle);
		aos_se(w, 0x26);
	}
	if (p->timer == 5)
		flame(w, p, vb, p->faceAngle);
	if (p->timer == 10 && aos_use_gauge(p, 0x114))
		hit(w, p, h1);
	if (p->timer == 0xf && aos_use_gauge(p, 0x114))
		hit(w, p, h2);
	return chain_tail(w, p, next, 0x19);
}
static bool chain3(aos_world *w, aos_player *p, int va, int vb, int h1, int h2)
{
	if (p->timer == 0) {
		lunge(w, p, 15.0f, 8.0f);
		flame(w, p, va, p->faceAngle + 0.75f);
		aos_se(w, 0x26);
	}
	if (p->timer == 5)
		flame(w, p, vb, p->faceAngle - 0.75f);
	if (p->timer == 10 && aos_use_gauge(p, 0x115))
		hit(w, p, h1);
	if (p->timer == 0xf && aos_use_gauge(p, 0x115))
		hit(w, p, h2);
	tail(w, p, 0x1e, 0x32);
	return true;
}
static bool st_a1(aos_world *w, aos_player *p) { return chain1(w, p, 700, 0x2bd, 200, 0xc9, 0x114); }   /* 0x113 FUN_00748ed0 */
static bool st_a2(aos_world *w, aos_player *p) { return chain2(w, p, 0x2c6, 0x2c7, 0xd2, 0xd3, 0x115); } /* 0x114 FUN_00749210 */
static bool st_a3(aos_world *w, aos_player *p) { return chain3(w, p, 0x2d0, 0x2d1, 0xdc, 0xdd); }        /* 0x115 FUN_00749510 */
static bool st_sa1(aos_world *w, aos_player *p) { return chain1(w, p, 800, 0x321, 300, 0x12d, 0x117); }  /* 0x116 FUN_007497d0 */
static bool st_sa2(aos_world *w, aos_player *p) { return chain2(w, p, 0x32a, 0x32b, 0x136, 0x137, 0x118); } /* 0x117 FUN_00749b10 */
static bool st_sa3(aos_world *w, aos_player *p) { return chain3(w, p, 0x334, 0x335, 0x140, 0x141); }     /* 0x118 FUN_00749e10 */

static void pin(aos_player *o, const aos_player *p, float dist) /* grab placement */
{
	o->state = AOS_ST_HITSTUN;
	o->timer = 0;
	o->x = aos_cos(p->faceAngle) * dist + p->x;
	o->y = aos_sin(p->faceAngle) * dist + p->y;
	o->speed = 0;
	o->faceAngle = p->faceAngle + AOS_PI;
	aos_angle_normalize(&o->faceAngle);
}

static bool st_sh(aos_world *w, aos_player *p) /* 0x119 T2 FUN_0074a0d0 — S+H lunge */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		aos_barrier(w, p, 10);
		p->barrierTime = 200;
		aos_hyper_flourish_ex(w, p, 6, 100, 0.75f, 0, 0.0f, 0.0f, 0x14, 0.5f, 2, 1); /* no speedline */
	}
	if (p->timer == 10) {
		p->faceAngle = aos_aim(w, p);
		p->speed = 15.0f;
		p->moveAngle = p->faceAngle;
		aos_se(w, 0x20);
	}
	if (p->timer > 9 && p->timer < 0x15 && aos_dist(w, p) < 70.0f) {
		aos_player *o = aos_opponent(w, p);
		if (can_grab(w, o)) {
			pin(o, p, 60.0f);
			aos_afterimage_clear(w, o->objId);
			p->state = 0x11a;
			p->timer = 0;
			p->hitFlag = 0;
			p->speed *= 0.25f;
			p->faceAngle = aos_atan2(-p->y, -p->x);
			aos_tab.T2[0x11a](w, p);
			aos_camera_focus(w, 0x50, p->objId, &p->x, 0.4f);
			aos_screen_dark(w, 100, 0.75f);
			aos_screen_blur(w, 0x14, 0.5f, 2);
			aos_speedline(w, 100, &p->x, 0.0f, 0.0f);
			gfx(w, 0xfa, 0, &p->x, 0, p->objId, 0, p->side);
			return true;
		}
	}
	if (3.0f <= p->speed)
		dash_sparks(w, p, p->faceAngle);
	if (p->timer >= 0x28 && p->D == 3) {
		p->barrierTime /= 2;
		aos_dash(w, p, 1);
	} else if (p->timer > 0x3c) {
		p->barrierTime /= 2;
		p->state = AOS_ST_NEUTRAL;
	}
	return true;
}

static bool st_shhold(aos_world *w, aos_player *p) /* 0x11a T2 FUN_0074a5b0 — S+H hold */
{
	int t = p->timer;
	if (t >= 0 && t < 0x51) {
		aos_player *o = aos_opponent(w, p);
		if (can_grab(w, o))
			pin(o, p, 60.0f);
	}
	if (t >= 0 && t < 0x1f && t % 5 == 0) {
		float a = aos_rand_real(w, -AOS_PI, AOS_PI);
		float d = aos_rand_real(w, 40.0f, 60.0f);
		aos_player *o = aos_opponent(w, p);
		float pt[3] = {aos_cos(a) * d + o->x, aos_sin(a) * d + o->y, 0};
		aos_spawn_missile(w, ID, 0x96, pt, 0.0f, p->objId, 0, p->side, p->color);
		aos_se_cd(w, 0x2f, 3);
	}
	if (t == 0x50) {
		gfx(w, 0xa8c, 0, &p->x, 0, p->objId, 0, p->side);
		knock_near(w, p, 120.0f, 18.0f);
	}
	tail(w, p, 0x6e, 0x82);
	return true;
}

static bool st_h(aos_world *w, aos_player *p) /* 0x11b T2 FUN_0074a900 — H charge */
{
	int t = p->timer;
	if (t == 0) {
		p->faceAngle = aos_aim(w, p);
		aos_barrier(w, p, 10);
		p->barrierTime = 200;
		aos_se(w, 6);
		aos_screen_dark(w, 100, 0.75f);
		aos_screen_blur(w, 0x14, 0.5f, 2);
		aos_se(w, 0x26);
		gfx(w, 0xfa, 0, &p->x, 0, p->objId, 0, p->side);
	}
	if (t == 10) {
		p->faceAngle = aos_aim(w, p);
		X(p)->charge = 0;
		aos_screen_blur(w, 100, 0.5f, 2);
	}
	if (t > 9) {
		X(p)->charge += 0.5f;
		p->x = aos_cos(p->faceAngle) * X(p)->charge + p->x;
		p->y = aos_sin(p->faceAngle) * X(p)->charge + p->y;
	}
	if (t % 3 == 0)
		sbfx(w, 600, 0, &p->x, aos_rand_real(w, -AOS_PI, AOS_PI), p->objId, 0, p->side);
	if (t % 10 == 1) {
		aos_enemy *e = new_enemy(w, p, p->color == 0 ? 700 : 800);
		float r = aos_rand_real(w, -AOS_PI, AOS_PI);
		if (e)
			e->angle2 = r;
	} else if (t % 10 == 6) {
		aos_enemy *e = new_enemy(w, p, p->color == 0 ? 0x2bd : 0x321);
		float r = aos_rand_real(w, -AOS_PI, AOS_PI);
		if (e)
			e->angle2 = r;
	}
	if (t > 9 && aos_dist(w, p) < 120.0f) {
		aos_player *o = aos_opponent(w, p);
		if (can_grab(w, o)) {
			pin(o, p, 40.0f);
			aos_afterimage_clear(w, o->objId);
			if (aos_camera_focus_count(w) == 0) /* FUN_004e93b0 on the focus vector 0x895144 */
				aos_camera_focus(w, 0x50, p->objId, &p->x, 0.4f); /* DAT_00813f30 */
		}
	}
	if (t < 0x14 || aos_in_arena(&p->x, 0.0f)) {
		tail(w, p, 0x6e, 0x82);
		return true;
	}
	/* wall crash */
	gfx(w, 0xa8c, 0xd, &p->x, 0, p->objId, 0, p->side);
	aos_se(w, 0x1b);
	aos_screen_blur(w, 0x14, 0.75f, 1);
	float b = p->faceAngle - AOS_PI;
	for (int i = -8; i < 9; i++) {
		float ang = ((float)i / 12.0f) * AOS_PI;
		float v[3] = {-aos_cos(ang) * 60.0f, aos_sin(ang) * 160.0f, 0};
		aos_vec_rotate(v, b);
		v[0] = aos_cos(b) * 30.0f + p->x + v[0];
		v[1] = aos_sin(b) * 30.0f + p->y + v[1];
		int k = aos_rand_int(w, -2, 2);
		gfx(w, 500, 0xd, v, 0, p->objId, k - 2, p->side);
	}
	int h = aos_spawn_hitbox(w, ID, 500, &p->x, p->faceAngle, p->objId, 0, p->side);
	if (h >= 0)
		w->obj.hitbox[h].damage = (t - 0x14) * 0x14 + 400;
	aos_set_state(w, p, 0x11c);
	return true;
}

static bool st_hrec(aos_world *w, aos_player *p) /* 0x11c T2 FUN_0074b160 */
{
	tail(w, p, 0x1e, 0x32);
	return true;
}

static bool st_accel(aos_world *w, aos_player *p) /* 0x11d T2 FUN_0074b5c0 — Accel */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		p->dashAngle = p->faceAngle;
		p->moveAngle = p->faceAngle;
		p->speed = 0;
		aos_barrier(w, p, 0x14);
		aos_hyper_flourish_ex(w, p, 7, 0x96, 0.85f, 0x78, p->faceAngle, 0.7853982f, 0x96, 0.75f, 2, 1);
		aos_enemy *e = new_enemy(w, p, 900);
		if (e) {
			e->pos[0] = p->x - aos_cos(p->faceAngle) * 200.0f;
			e->pos[1] = p->y - aos_sin(p->faceAngle) * 200.0f;
			e->angle = p->faceAngle;
			e->angle2 = e->angle;
		}
		aos_se(w, 0x33);
	}
	if (p->timer > 0x13 && p->timer < 0x47) {
		float a = aos_dir_angle(p);
		if (a <= AOS_2PI)
			aos_angle_toward(&p->faceAngle, a, 0.01f);
	}
	if (p->timer >= 0x50 && p->D == 3) {
		aos_dash(w, p, 1);
	} else {
		if (p->timer > 0x78)
			p->state = AOS_ST_NEUTRAL;
		if (p->timer < 0x50) {
			p->hurtShape[0] = p->hurtShape[1] = p->hurtShape[2] = p->hurtShape[3] = 0;
			p->hittable = 0;
		}
	}
	return true;
}

/* ------------------------------------------------------------------ */
/* player draws (T1)                                                   */

static int pp(int t, int n) /* (t/n)&3, 3 -> 1 */
{
	int f = (t / n) % 4;
	return f == 3 ? 1 : f;
}
static int cyc4(int t)
{
	int f = t / 4;
	if (f > 1)
		f = (f - 2) % 4 + 2;
	return f == 5 ? 3 : f;
}

static aos_dcmd *body(aos_world *w, aos_player *p, const char *fmt, int frame)
{
	aos_dcmd *c = aos_draw(aos_texf(fmt, frame), aos_pidx(w, p), p->x, p->y, 0);
	c->halfW = c->halfH = 48.0f; /* 96-px quad (DAT_00814118) */
	return c;
}
/* AIM transform: RotZ(aim); |aim| >= π/2: RotX(180°); RotY(180°). RotX(180°) is
 * approximated as y-scale × cos(180°) = −1 (PORTING.md). */
static aos_dcmd *aim_body(aos_world *w, aos_player *p, const char *fmt, int frame)
{
	aos_dcmd *c = body(w, p, fmt, frame);
	c->rotZ = aos_rad2bam(p->faceAngle);
	c->flipX = 1;
	if (1.5707964f <= aos_fabs(p->faceAngle))
		c->sy = -1.0f;
	return c;
}

static void dr_neutral(aos_world *w, aos_player *p) /* 0x101 T1 FUN_0074d450 */
{
	const char *s = "sb_std%02d";
	if (p->up > 1) s = "sb_fwd%02d";
	if (p->down > 1) s = "sb_bck%02d";
	if (p->facingRight == 0 && p->left > 1) s = "sb_fwd%02d";
	if (p->facingRight == 1 && p->left > 1) s = "sb_bck%02d";
	if (p->facingRight == 0 && p->right > 1) s = "sb_bck%02d";
	if (p->facingRight == 1 && p->right > 1) s = "sb_fwd%02d";
	aos_dcmd *c = body(w, p, s, pp(p->timer, 5));
	c->flipX = p->facingRight != 0;
	int32_t rot[3] = {0, p->facingRight != 0 ? aos_deg2bam(0xb4) : 0, 0};
	aos_afterimage(w, c, 1, p, rot);
}
static void dr_dash(aos_world *w, aos_player *p) /* 0x102 T1 FUN_0074d700 */
{
	int f = pp(p->timer, 3);
	float a = aos_fabs(p->faceAngle);
	if ((p->facingRight == 1 && a > HALF_PI) || (p->facingRight == 0 && a < HALF_PI))
		f += 3;
	aos_dcmd *c = aim_body(w, p, "sb_dsh%02d", f);
	int32_t rot[3] = {aos_fabs(p->faceAngle) >= HALF_PI ? aos_deg2bam(0xb4) : 0, aos_deg2bam(0xb4),
	                  aos_rad2bam(p->faceAngle)};
	aos_afterimage(w, c, 1, p, rot);
}
static void dr_ko(aos_world *w, aos_player *p) /* 0x103 T1 FUN_0074e060 */
{
	int f = pp(p->timer, 3);
	if (w->frame % 2 == 0)
		f += 3;
	if (p->timer < 100)
		aim_body(w, p, "sb_los%02d", f);
}
static void dr_hit(aos_world *w, aos_player *p) { aim_body(w, p, "sb_los%02d", 0); }  /* 0x104 FUN_0074db00 */
static void dr_sld5(aos_world *w, aos_player *p)                                        /* 0x105/0x10d/0x10e */
{
	int f = p->timer / 5;
	aim_body(w, p, "sb_sld%02d", f > 3 ? 3 : f);
}
static void dr_atka(aos_world *w, aos_player *p) { aim_body(w, p, "sb_atka%02d", cyc4(p->timer)); } /* 0x10c..0x111 */
static void dr_atkb(aos_world *w, aos_player *p)                                         /* 0x113..0x118 */
{
	int f = p->timer / 4;
	aim_body(w, p, "sb_atkb%02d", f > 7 ? 7 : f);
}
static void dr_catch(aos_world *w, aos_player *p) { aim_body(w, p, "sb_catch%02d", 0); } /* 0x119/0x11a/0x11c */
static void dr_h(aos_world *w, aos_player *p) /* 0x11b T1 FUN_00750a20 */
{
	if (p->timer < 10) {
		int f = p->timer / 3;
		aim_body(w, p, "sb_atkb%02d", f > 7 ? 7 : f);
	} else {
		aim_body(w, p, "sb_catch%02d", 0);
	}
}
static void dr_accel(aos_world *w, aos_player *p) /* 0x11d T1 FUN_00751240 */
{
	int t = p->timer;
	float sx, sy;
	if (t < 0x14) {
		sx = 1.0f - (float)t / 20.0f;
		sy = (float)t / 10.0f;
	} else if (t < 0x65) {
		if (t < 0x51)
			return;
		sx = (float)(t - 100) / 20.0f;
		sy = 2.0f - (float)(t - 100) / 20.0f;
	} else {
		sx = sy = 1.0f;
	}
	aos_dcmd *c = aim_body(w, p, "sb_atka%02d", cyc4(t));
	c->rgba = aos_rgba(0, 0, 0, 1); /* CModel+0xdc(a=1, r=g=b=0) */
	c->sx = sx;
	c->sy *= sy;
}

/* ------------------------------------------------------------------ */
/* enemy family (FUN_0054afe0 / 0054b870 / 0054e4c0 / 0054e170)        */

static int en_spawn(aos_world *w, int32_t id, int32_t owner, int32_t team, int32_t p18) /* FUN_0054afe0 */
{
	int kind;
	switch (id) {
	case 100: case 200: case 900: kind = id; break;
	case 700: case 0x2bd: case 0x2c6: case 0x2c7: case 0x2d0: case 0x2d1: kind = 700; break;
	case 800: case 0x321: case 0x32a: case 0x32b: case 0x334: case 0x335: kind = 800; break;
	default: return -1; /* 500/600: only reachable from dead state 0x10b */
	}
	int32_t group = aos_enemy_group_next(w); /* FUN_0055efc0, every case */
	int i = aos_enemy_new(w, ID, owner, team, p18);
	if (i < 0)
		return -1;
	aos_enemy *e = &w->obj.enemy[i];
	e->serial = group;
	e->f10 = kind;
	e->f7c = id;
	e->f84 = 0;
	e->solid = 0;
	e->active = 0;
	e->shape = (aos_shape){0, 0, 0};
	e->mirrorChildren = 0;
	e->motionId = 0;
	e->rehitTimer = 0;
	if (id == 100) {
		e->hp = e->maxHp = 300;
		e->shape.radius = 30.0f;
		e->scale[0] = 0; e->scale[1] = 4.0f; e->scale[2] = 1.0f;
	} else if (id == 200) {
		e->hp = e->maxHp = 200;
		e->shape.radius = 30.0f;
		e->scale[0] = 3.0f; e->scale[1] = 0; e->scale[2] = 1.0f;
	} else {
		e->hp = e->maxHp = 999;
		e->scale[0] = e->scale[1] = e->scale[2] = 0;
	}
	return i;
}

static bool en_killed(aos_world *w, aos_enemy *e) /* FUN_0054e170 */
{
	if (e->f10 == 100) {
		gfx(w, 600, 0, e->pos, 0, e->ownerId, 0, e->team);
		for (int k = 0; k < 5; k++) {
			float pt[3] = {e->pos[0], e->pos[1], e->pos[2]};
			pt[0] = aos_rand_real(w, -50.0f, 50.0f) + pt[0];
			pt[1] = aos_rand_real(w, -50.0f, 50.0f) + pt[1];
			gfx(w, 0x834, 0, pt, 0, e->ownerId, 0, e->team);
		}
		for (int k = 0; k < 5; k++) {
			float pt[3] = {e->pos[0], e->pos[1], e->pos[2]};
			pt[0] = aos_rand_real(w, -30.0f, 30.0f) + pt[0];
			pt[1] = aos_rand_real(w, -30.0f, 30.0f) + pt[1];
			gfx(w, 500, 0, pt, 0, e->ownerId, k * -3 - 10, e->team);
		}
		aos_se(w, 0x1a);
	} else if (e->f10 == 200 || e->f10 == 0xc9) {
		int col = team_col(w, e->team, 2);
		sbfx(w, 300, col, e->pos, e->angle + AOS_PI, e->ownerId, 0, e->team);
		gfx(w, 0xaf0, col, e->pos, 0, e->ownerId, 0, e->team);
		aos_player *own = own_player(w, e->team);
		aos_spawn_hitbox(w, ID, 0xb4, e->pos, 0, own ? own->objId : e->ownerId, 0, e->team);
		aos_se(w, 0x14);
	}
	return false;
}

static void bomb_attach(aos_enemy *e, float a2, int32_t owner, int variant)
{
	e->pivot[0] = 0;
	e->pivot[1] = 0;
	e->angle2 = a2;
	e->ownerId = owner;
	e->f10 = 0xc9;
	e->f7c = variant;
	e->timer2 = 0;
	e->scale[0] = 1.0f;
	e->scale[1] = 1.0f;
}

/* variants 200 (B tap bomb) and 0xfa (B+H boomerang bomb) */
static bool en_bomb_fly(aos_world *w, aos_enemy *e, bool boom)
{
	if (1.0f < e->scale[1] || e->scale[1] == 1.0f) {
		e->scale[0] = 1.0f;
		e->scale[1] = 1.0f;
	} else {
		e->scale[0] -= 0.2f;
		e->scale[1] += 0.1f;
	}
	aos_player *opp = own_player(w, e->team == 1 ? 2 : 1); /* FUN_00780480(team) */
	if (e->age < 0x14) {
		aos_angle_toward(&e->angle2, aos_atan2(opp->y - e->pos[1], opp->x - e->pos[0]), e->f70 * 0.01f);
		e->f70 *= 0.9f;
	} else if (!boom && e->age < 0x96) {
		aos_angle_toward(&e->angle2, aos_atan2(opp->y - e->pos[1], opp->x - e->pos[0]), 0.025f);
		if (e->f70 <= 16.0f && e->f70 != 16.0f)
			e->f70 += 0.35f;
		if (e->age > 0x13 && e->age < 0x19) {
			float r = aos_rand_real(w, -0.2f, 0.2f);
			int n = aos_rand_int(w, 0, 100) % 8;
			gfx(w, 0xa28 + n, 0xc, e->pos, r + e->angle, e->ownerId, 0, e->team);
		}
	} else if (boom && e->age < 100) {
		aos_player *own = own_player(w, e->team);
		aos_angle_toward(&e->angle2, aos_atan2(own->y - e->pos[1], own->x - e->pos[0]), 0.025f);
	} else if (boom && e->age < 0xfa) {
		aos_angle_toward(&e->angle2, aos_atan2(opp->y - e->pos[1], opp->x - e->pos[0]), 0.01f);
		if (e->f70 <= 16.0f && e->f70 != 16.0f)
			e->f70 += 0.15f;
		/* spark block for ages 20..24 is unreachable in this branch (age >= 100) */
	}
	e->angle = e->angle2;
	if (e->age % 2 == 0 || 12.0f < e->f70)
		gfx(w, 0x802, 0, e->pos, e->angle2, e->ownerId, 0, e->team);
	e->pos[0] = aos_cos(e->angle2) * e->f70 + e->pos[0];
	e->pos[1] = aos_sin(e->angle2) * e->f70 + e->pos[1];
	for (int i = 0; i < 2; i++) {
		aos_player *pl = &w->p[i];
		if (e->ownerId != pl->objId && e->team != pl->side &&
		    aos_shape_hit(e->pos, &pl->x, &e->shape, (const aos_shape *)pl->hurtShape, e->angle, pl->faceAngle)) {
			bomb_attach(e, aos_atan2(pl->y - e->pos[1], pl->x - e->pos[0]), pl->objId, 0xc9);
			break; /* literal: the enemy loop below still runs */
		}
	}
	for (int k = 0; k < w->obj.nenemy; k++) {
		aos_enemy *o = &w->obj.enemy[k];
		if (e->id != o->id && e->team != o->team &&
		    aos_shape_hit(e->pos, o->pos, &e->shape, &o->shape, e->angle, o->angle)) {
			bomb_attach(e, aos_atan2(o->pos[1] - e->pos[1], o->pos[0] - e->pos[0]), o->id, 0xca);
			break;
		}
	}
	if (e->age < (boom ? 0x97 : 0x15))
		return true;
	if (!aos_in_arena(e->pos, 0.0f)) {
		e->f10 = 0xc9;
		e->f7c = 0xcb;
		e->timer2 = 0;
		e->scale[0] = 1.0f;
		e->scale[1] = 1.0f;
	}
	return true;
}

static bool en_bit(aos_world *w, aos_enemy *e, bool orbit)
{
	if (e->f84 == 0) {
		e->scale[0] += 0.1f;
		e->scale[1] -= orbit ? 0.2f : 0.3f;
		e->scale[2] += 0.1f;
		if (e->scale[0] < 1.0f)
			return true;
		e->active = 1;
		e->scale[2] = e->scale[0] = e->scale[1] = 1.0f;
		e->f84 = 10;
		e->timer2 = 0;
		aos_spawn_laser(w, ID, 100, e->pos, e->angle, e->id, e->team, e->p18);
		return true;
	}
	if (e->f84 != 10) {
		e->scale[0] -= 0.1f;
		e->scale[1] += 0.3f;
		return 0.0f < e->scale[0];
	}
	e->angle += e->f70;
	if (orbit) {
		e->pos[0] = aos_cos(e->angle) * 100.0f + e->pivot[0];
		e->pos[1] = aos_sin(e->angle) * 100.0f + e->pivot[1];
	}
	if (e->timer2 == 0x13) {
		aos_spawn_laser(w, ID, orbit ? 0x78 : 0x6e, e->pos, e->angle, e->id, e->team, e->p18);
		aos_se(w, 0x11);
	}
	if (e->timer2 < (orbit ? 0xb5 : 0x65))
		return true;
	e->active = 0;
	e->f84 = 0x14;
	e->timer2 = 0;
	return true;
}

static bool en_flame(aos_world *w, aos_enemy *e) /* 700/800 groups */
{
	aos_player *v = aos_player_by_objid(w, e->ownerId);
	if (!v)
		return false; /* original dereferences NULL; cannot happen (owner is the SB player) */
	float a = ((float)e->timer2 / 20.0f) * AOS_2PI;
	bool left = HALF_PI < aos_fabs(v->faceAngle);
	if (left)
		a = AOS_2PI - a;
	int id = e->f7c, g = e->f10 == 800 ? id - 100 : id; /* map blue ids onto red ones */
	if (e->f10 == 800)
		g = id == 800 ? 700 : id == 0x321 ? 0x2bd : id == 0x32a ? 0x2c6 : id == 0x32b ? 0x2c7 : id == 0x334 ? 0x2d0 : 0x2d1;
	float c = aos_cos(a), s = aos_sin(a);
	float rx = (float)(e->timer2 * 2 + 0x50), ry = (float)(e->timer2 * 2 + 0x3c);
	switch (g) {
	case 700: e->pos[0] = c * -80.0f; e->pos[1] = s * 36.0f; break;
	case 0x2bd: e->pos[0] = c * 80.0f; e->pos[1] = s * -36.0f; break;
	case 0x2c6: e->pos[0] = c * -100.0f; e->pos[1] = s * -20.0f; break;
	case 0x2c7: e->pos[0] = c * 60.0f; e->pos[1] = s * 90.0f; break;
	case 0x2d0:
		if (!left) { e->pos[0] = c * -rx; e->pos[1] = s * ry; }
		else { e->pos[0] = c * rx; e->pos[1] = s * ry; }
		break;
	default: /* 0x2d1 */
		if (!left) { e->pos[0] = c * rx; e->pos[1] = s * -ry; }
		else { e->pos[0] = c * -rx; e->pos[1] = s * -ry; }
		break;
	}
	aos_vec_rotate(e->pos, e->angle2);
	e->pos[0] = e->pos[0] + v->x;
	e->pos[1] = e->pos[1] + v->y;
	if (1 < e->timer2) {
		int i1 = e->f10 == 800 ? 0x1fe : 500, i2 = e->f10 == 800 ? 0x1ff : 0x1f5;
		e->angle = aos_atan2(e->pos[1] - e->prevPos[1], e->pos[0] - e->prevPos[0]);
		sbfx(w, i1, 0, e->pos, e->angle, e->ownerId, 0, e->team);
		sbfx(w, i2, 0, e->pos, e->angle, e->ownerId, 0, e->team);
		float m[3] = {(e->pos[0] + e->prevPos[0]) / 2.0f, (e->pos[1] + e->prevPos[1]) / 2.0f, 0};
		sbfx(w, i1, 0, m, e->angle, e->ownerId, 0, e->team);
		sbfx(w, i2, 0, m, e->angle, e->ownerId, 0, e->team);
	}
	return e->timer2 <= 0x14;
}

static bool en_update(aos_world *w, aos_enemy *e) /* FUN_0054b870 (switch on +0x7c) */
{
	switch (e->f7c) {
	case 0xcb:
		if (e->timer2 == 100) {
			en_killed(w, e);
			return false;
		}
		return true;
	case 0xc9: {
		aos_player *v = aos_player_by_objid(w, e->ownerId);
		if (!v || v->state == AOS_ST_KO)
			return false;
		if (v->barrierTime != 0)
			e->timer2 = 100;
		e->pos[0] = -aos_cos(e->angle2) * 30.0f + v->x;
		e->pos[1] = -aos_sin(e->angle2) * 30.0f + v->y;
		if (e->timer2 == 100) {
			en_killed(w, e);
			return false;
		}
		return true;
	}
	case 0xca: {
		aos_enemy *o = aos_enemy_by_id(w, e->ownerId);
		if (!o)
			return false;
		e->pos[0] = -aos_cos(e->angle2) * 30.0f + o->pos[0];
		e->pos[1] = -aos_sin(e->angle2) * 30.0f + o->pos[1];
		if (e->timer2 == 100) {
			en_killed(w, e);
			return false;
		}
		return true;
	}
	case 0xfa: return en_bomb_fly(w, e, true);
	case 200: return en_bomb_fly(w, e, false);
	case 100: return en_bit(w, e, false);
	case 0x6e: return en_bit(w, e, true);
	case 700: case 0x2bd: case 0x2c6: case 0x2c7: case 0x2d0: case 0x2d1:
	case 800: case 0x321: case 0x32a: case 0x32b: case 0x334: case 0x335:
		return en_flame(w, e);
	case 900: {
		if (e->f84 == 0) {
			e->scale[0] += 0.05f;
			e->scale[1] += 0.05f;
			e->scale[2] += 0.05f;
			if (1.0f <= e->scale[0]) {
				e->scale[2] = e->scale[0] = e->scale[1] = 1.0f;
				e->f84 = 100;
				e->timer2 = 0;
				aos_spawn_laser(w, ID, 300, e->pos, e->angle2, e->id, e->team, e->p18);
				aos_se(w, 0x12);
			}
		} else if (e->f84 == 100) {
			if (0x78 < e->timer2)
				e->f84 = 900;
		} else {
			e->scale[0] += 0.1f;
			e->scale[1] += 0.1f;
			e->scale[2] -= 0.1f;
			if (e->scale[2] <= 0.0f)
				return false;
		}
		aos_player *v = aos_player_by_objid(w, e->ownerId);
		if (v && v->state == 0x11d) {
			e->pos[0] = v->x - aos_cos(v->faceAngle) * 200.0f;
			e->pos[1] = v->y - aos_sin(v->faceAngle) * 200.0f;
			e->angle = v->faceAngle;
			e->angle2 = e->angle;
		}
		return true;
	}
	default:
		return true;
	}
}

static float pulse(int age, float amp, float base)
{
	return aos_sin(((float)(age % 50) / 50.0f) * AOS_2PI) * amp + base;
}

static aos_dcmd *en_quad(const aos_enemy *e, const char *fmt, int frame, float half, float x, float y)
{
	aos_dcmd *c = aos_draw(aos_texf(fmt, frame), team_bank(e->team), x, y, 0);
	c->halfW = c->halfH = half;
	return c;
}

static void en_draw(aos_world *w, aos_enemy *e) /* FUN_0054e4c0 (switch on +0x10). Visual only. */
{
	float f7 = pulse(e->age, 0.25f, 0.75f), f8 = pulse(e->age, 0.35f, 0.65f);
	if (e->f10 == 100 || e->f10 == 200 || e->f10 == 0xc9) {
		const char *fmt = e->f10 == 100 ? "sb_bit%02d" : "sb_missile%02d";
		float half = e->f10 == 100 ? 36.0f : 40.0f;
		int base;
		float rot, sx = e->scale[0], sy = e->scale[1];
		uint32_t col;
		if (e->f10 == 100) {
			base = e->f84 == 10 ? 3 : 0;
			rot = e->f84 == 10 ? e->angle : 0.0f;
			col = aos_rgba(e->scale[2], e->scale[2], e->scale[2], 1);
		} else {
			base = e->f10 == 200 ? ((e->age / 3) % 3) * 3 : 9;
			rot = e->angle;
			col = e->scale[1] == 1.0f ? aos_rgba(1, 1, 1, 1) : aos_rgba(0, 0, 0, 1);
			if (e->f10 == 0xc9) {
				float f9 = pulse(e->age, 0.2f, 1.0f);
				sx *= f9;
				sy *= f9;
			}
		}
		for (int k = 0; k < 3; k++) {
			aos_dcmd *c = en_quad(e, fmt, base + k, half, e->pos[0], e->pos[1]);
			c->sx = sx;
			c->sy = sy;
			c->rotZ = aos_rad2bam(rot);
			if (k == 0) {
				c->rgba = col;
			} else {
				c->blend = AOS_BLEND_ADD_ALPHA;
				c->rgba = aos_rgba(1, 1, 1, (k == 1 ? f7 : f8) * e->scale[2]);
			}
		}
	} else if (e->f10 == 900) {
		float g7 = aos_sin(((float)(w->frame % 500) / 500.0f) * AOS_2PI) * 0.25f + 0.25f;
		float g8 = aos_sin(((float)((w->frame + 0xfa) % 500) / 500.0f) * AOS_2PI) * 0.25f + 0.25f;
		float f9 = pulse(e->age, 0.05f, 1.0f);
		aos_dcmd *c = en_quad(e, "sb_starb03", 0, 128.0f, e->pos[0], e->pos[1]);
		c->rgba = aos_rgba(g8, 0, g7, e->scale[2]); /* CModel+0xdc(a=sz, r, g=0, b) */
		c->sx = e->scale[0] * f9;
		c->sy = f9 * 0.70710677f; /* RotX(45°) approximated as y × cos 45° */
		c->rotZ = aos_rad2bam(e->angle + HALF_PI);
		/* vtable +0xc4 = FUN_0046e470: forwards (frame%200)/200 to every mesh's +0x68 (texture
		 * offset; UNVERIFIED which axis) — not reproduced, the draw API has no UV scroll. */
		float bx = aos_cos(e->angle) * 180.0f * e->scale[0] + e->pos[0];
		float by = aos_sin(e->angle) * 180.0f * e->scale[0] + e->pos[1];
		c = en_quad(e, "sb_starb%02d", 0, 160.0f, bx, by);
		c->rgba = e->scale[1] == 1.0f ? aos_rgba(1, 1, 1, e->scale[2]) : aos_rgba(0, 0, 0, e->scale[2]);
		c->sx = c->sy = e->scale[0];
		c->rotZ = aos_rad2bam(e->angle);
		c = en_quad(e, "sb_starb%02d", 1, 160.0f, bx, by);
		c->blend = AOS_BLEND_ADD_ALPHA;
		c->rgba = aos_rgba(1, 1, 1, f7 * e->scale[2]);
		c->sx = c->sy = e->scale[0];
		c->rotZ = aos_rad2bam(e->angle);
	}
}

/* ------------------------------------------------------------------ */
/* missile family (FUN_00679f00 / 0067b9b0 / 0067a2e0 / 0067b5e0 / 0067b970) */

static int mi_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner,
                    int32_t age0, int32_t team, int32_t p18) /* FUN_00679f00 */
{
	if (id != 100 && id != 0x96 && id != 300 && id != 0x12d && id != 0x12e)
		return -1; /* 200: no spawner (dead) */
	int i = aos_missile_new(w, ID, id, pos, angle, owner, age0, team, p18);
	if (i < 0)
		return -1;
	aos_missile *m = &w->obj.missile[i];
	m->damage = 0;
	m->absorbs = 0;
	m->knockback = 10.0f;
	if (id == 100 || id == 0x96) {
		m->growScale = id == 100 ? 0.0f : 1.5f;
		m->f60 = id == 100 ? 1.0f : 0.0f;
		m->speed = 0;
		m->hp = 0xb4;
		m->active = 0;
		m->shapeBody.radius = 40.0f;
		m->shapeProj.radius = 40.0f;
	} else {
		m->id = 300; /* 0x12d / 0x12e are stored as 300 */
		m->growScale = 0;
		m->speed = 10.0f;
		m->f60 = 1.0f;
		m->f74 = id == 0x12e ? 0.0f : id == 300 ? -0.01f : 0.01f;
		m->hp = 0x3c;
		m->damage = 200;
		m->active = 1;
		m->shapeBody.radius = 16.0f;
		m->shapeProj.radius = 0;
	}
	return i;
}

static bool mi_hit(aos_world *w, aos_missile *m) /* FUN_0067b5e0 */
{
	int col = team_col(w, m->team, 2);
	switch (m->id) {
	case 100:
	case 0x96:
		sbfx(w, m->id, col, m->pos, 0, m->ownerId, 0, m->team);
		aos_se_cd(w, 0x1b, 6);
		if (m->id == 0x96)
			aos_screen_blur(w, 10, 0.75f, 1);
		aos_screen_dark(w, 0x28, 0.75f);
		break;
	case 300:
		gfx(w, 0x515, col, m->pos, m->angle, m->ownerId, 0, m->team);
		aos_se_cd(w, 0x1d, 3);
		sbfx(w, 700, col, m->pos, m->angle, m->ownerId, 0, m->team);
		break;
	default:
		break;
	}
	return false;
}

static bool mi_killed(aos_world *w, aos_missile *m) /* FUN_0067b970 */
{
	if (m->id == 100)
		mi_hit(w, m);
	return false;
}

static bool mi_update(aos_world *w, aos_missile *m) /* FUN_0067b9b0 */
{
	switch (m->id) {
	case 100: {
		m->growScale = aos_sin((((float)m->age / 300.0f) * AOS_PI) / 2.0f);
		m->pos[0] = aos_cos(m->moveAngle) * m->speed + m->pos[0];
		m->pos[1] = aos_sin(m->moveAngle) * m->speed + m->pos[1];
		m->angle += 0.1f;
		if (AOS_PI < m->angle)
			m->angle -= AOS_2PI;
		if (m->age == 100)
			m->active = 1;
		float f = 1.0f - m->growScale * m->growScale;
		m->pos[0] = m->f34[6] + f * (m->pos[0] - m->f34[6]);
		m->pos[1] = m->f34[7] + f * (m->pos[1] - m->f34[7]);
		if (0xf9 < m->age) {
			mi_hit(w, m);
			return false;
		}
		return true;
	}
	case 0x96:
		m->angle += 0.1f;
		if (AOS_PI < m->angle)
			m->angle -= AOS_2PI;
		m->f60 += 0.1f;
		if (1.0f <= m->f60)
			m->f60 = 1.0f;
		if (5 <= m->age) {
			mi_hit(w, m);
			return false;
		}
		return true;
	case 300: {
		m->pos[0] = aos_cos(m->moveAngle) * m->speed + m->pos[0];
		m->pos[1] = aos_sin(m->moveAngle) * m->speed + m->pos[1];
		m->moveAngle += m->f74;
		if (0x14 < m->age && !aos_in_arena(m->pos, 0.0f)) {
			mi_hit(w, m);
			return false;
		}
		bool red = team_pal(w, m->team) == 0;
		sbfx(w, red ? 500 : 0x1fe, 0, m->pos, m->moveAngle, m->ownerId, 0, m->team);
		sbfx(w, red ? 0x1f5 : 0x1ff, 0, m->pos, m->moveAngle, m->ownerId, 0, m->team);
		return true;
	}
	default:
		return true;
	}
}

static void mi_draw(aos_world *w, aos_missile *m) /* FUN_0067a2e0 (exact; missile 300 draws nothing) */
{
	(void)w;
	int bank = team_bank(m->team); /* original: sprite bank of slot +0x1c */
	if (m->id != 100 && m->id != 0x96)
		return; /* 200 (unused) is the only other case */
	float a1 = aos_sin(((float)(m->age % 50) / 50.0f) * AOS_2PI) * 0.15f + 0.85f; /* DAT_009b3628 */
	float a2 = aos_sin(((float)(m->age % 50) / 50.0f) * AOS_2PI) * 0.25f + 0.75f; /* DAT_009b362c */
	float s;
	if (m->id == 100) {
		s = m->growScale * 4.0f;
		if (2.0f < s) s = 2.0f;
		if (s < 0.5f) s = 0.5f;
		if (0x96 < m->age)
			s = aos_sin(((float)(m->age % 10) / 10.0f) * AOS_2PI) * 0.2f + s;
	} else {
		s = m->growScale;
	}
	int f = (m->age / 3) % 6;
	for (int k = 0; k < 3; k++) {
		aos_dcmd *c = aos_draw(aos_texf("sb_bomb%02d", f + 6 * k), bank, m->pos[0], m->pos[1], 0);
		c->halfW = c->halfH = 32.0f; /* 64-px quad (DAT_008140f8) */
		c->sx = c->sy = s;
		c->rotZ = aos_rad2bam(m->angle);
		if (k == 0) {
			c->rgba = m->id == 0x96 ? aos_rgba(1, 1, 1, m->f60) : 0xffffffffu; /* 100: no colour call */
		} else {
			c->blend = AOS_BLEND_ADD_ALPHA;
			c->rgba = aos_rgba(1, 1, 1, k == 1 ? a1 : a2);
		}
	}
}

/* ------------------------------------------------------------------ */
/* hitbox family (FUN_005eb120 / FUN_005eb6c0)                         */

static int hb_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner,
                    int32_t zero, int32_t team) /* FUN_005eb120 */
{
	int ht, dmg;
	aos_shape s = {0, 0, 0};
	float kb = 0;
	switch (id) {
	case 400: ht = 1; dmg = 0x3c; s.halfW = 800.0f; s.halfH = 16.0f; break;
	case 100: ht = 3; dmg = 0x96; s.radius = 100.0f; kb = 5.0f; break;
	case 0x96: ht = 3; dmg = 0x78; s.radius = 100.0f; break;
	case 0xb4: ht = 2; dmg = 300; s.radius = 80.0f; break;
	case 200: case 0xd2: case 0xdc: case 300: case 0x136: case 0x140:
		ht = 3; dmg = 0x3c; s.radius = 120.0f; break;
	case 0xc9: case 0xd3: case 0x12d: case 0x137:
		ht = 3; dmg = 0x3c; s.radius = 150.0f; kb = 10.0f; break;
	case 0xdd: case 0x141: ht = 3; dmg = 0x50; s.radius = 170.0f; kb = 20.0f; break;
	case 500: ht = 2; dmg = 900; s.radius = 160.0f; break;
	case 600: ht = 3; dmg = 200; s.halfW = 800.0f; s.halfH = 160.0f; kb = 2.5f; break; /* ver > 0x5a0 */
	default: return -1;
	}
	int i = aos_hitbox_new(w, ID, id, pos, angle, owner, zero, team);
	if (i < 0)
		return -1;
	aos_hitbox *h = &w->obj.hitbox[i];
	h->dealsDamage = 1;
	h->oncePerFrame = 0;
	h->hitType = ht;
	h->damage = dmg;
	h->shape = s;
	h->knockback = kb;
	h->stun = 1.0f;
	h->rehitFlag = 0;
	return i;
}

static void streaks(aos_world *w, aos_hitbox *hb, aos_player *v, int col, float base)
{
	for (int k = -2; k < 2; k++) {
		float a = ((float)k / 2.0f) * AOS_PI;
		float r = aos_rand_real(w, -0.25f, 0.25f);
		gfx(w, 0x456, col, &v->x, a + r + base, hb->ownerId, 0, hb->team);
	}
}

static void hb_hit(aos_world *w, aos_hitbox *hb, aos_player *v) /* FUN_005eb6c0 */
{
	int id = hb->id;
	aos_player *own = own_player(w, hb->team);
	switch (id) {
	case 400:
		if (hb->damage != 0) {
			int col = team_col(w, hb->team, 2);
			float base = aos_rand_real(w, -AOS_PI, AOS_PI);
			streaks(w, hb, v, col, base);
			aos_se_cd(w, 0x13, 3);
		}
		break;
	case 100: {
		hb->angle = aos_atan2(v->y - hb->pos[1], v->x - hb->pos[0]);
		int col = team_col(w, hb->team, 2);
		streaks(w, hb, v, col, hb->angle);
		gfx(w, 0x515, col, &v->x, 0, hb->ownerId, 0, hb->team);
		break;
	}
	case 200: case 0xc9: case 0xd2: case 0xd3: case 0xdc:
	case 300: case 0x12d: case 0x136: case 0x137: case 0x140: {
		int col = team_col(w, hb->team, id >= 300 ? 1 : 0);
		float base = aos_rand_real(w, -AOS_PI, AOS_PI);
		streaks(w, hb, v, col, base);
		if (id == 0xc9 || id == 0xd3 || id == 0x12d || id == 0x137)
			gfx(w, 0xaf0, col, &v->x, 0, hb->ownerId, 0, hb->team);
		if (id != 0xdc && id != 0x140 && own)
			own->hitFlag = 1;
		aos_se_cd(w, 0x18, 3);
		break;
	}
	case 0xdd: case 0x141: {
		int col = team_col(w, hb->team, id == 0x141 ? 1 : 0);
		float r = aos_rand_real(w, -AOS_PI, AOS_PI);
		gfx(w, 0xa8c, col, &v->x, r, hb->ownerId, 0, hb->team);
		gfx(w, 0xaf0, col, &v->x, 0, hb->ownerId, 0, hb->team);
		aos_se_cd(w, 0x19, 3);
		aos_screen_blur(w, 10, 0.75f, 1);
		aos_status(w, v, id == 0xdd ? AOS_STATUS_HEAT : AOS_STATUS_DRAIN, 300);
		break;
	}
	case 500:
		aos_se(w, 0x19);
		break;
	case 600: {
		int col = team_col(w, hb->team, 2);
		for (int k = -2; k < 3; k++) {
			float a = ((float)k / 5.0f) * AOS_2PI;
			float r = aos_rand_real(w, -0.25f, 0.25f);
			gfx(w, 0x456, col, &v->x, a + r, hb->ownerId, 0, hb->team);
		}
		aos_se_cd(w, 0x13, 4);
		break;
	}
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* laser family (FUN_006261c0 / 00627a30 / 006264f0 / 00627a10)       */

static int la_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner,
                    int32_t team, int32_t p18) /* FUN_006261c0 */
{
	if (id != 100 && id != 0x6e && id != 0x78 && id != 300)
		return -1; /* 200: only spawned by dead effect 800 */
	int i = aos_laser_new(w, ID, id, pos, angle, owner, team, p18);
	if (i < 0)
		return -1;
	aos_laser *l = &w->obj.laser[i];
	l->damage = 0;
	if (id == 100) {
		l->blockMode = 0; l->hitType = 0; l->knockback = 0; l->stun = 0;
	} else if (id == 300) {
		l->blockMode = 1; l->hitType = 3; l->knockback = 5.0f; l->stun = 1;
	} else {
		l->blockMode = 1; l->hitType = 1; l->knockback = 0; l->stun = 1;
	}
	return i;
}

static void la_segs(aos_world *w, aos_laser *l, bool wallStop)
{
	static const aos_shape zero = {0, 0, 0};
	for (int i = 0; i < 0x28; i++) {
		float pt[3] = {aos_cos(l->angle) * (float)i * 50.0f + l->origin[0],
		               aos_sin(l->angle) * (float)i * 50.0f + l->origin[1], 0};
		if (wallStop && 2 < i && !aos_in_arena(pt, 10.0f)) {
			int col = team_col(w, l->team, 2);
			sbfx(w, 200, col, pt, 0, l->ownerId, 0, l->team);
			if (l->age % 3 == 0)
				gfx(w, 0x515, col, pt, 0, l->ownerId, 0, l->team);
			break;
		}
		aos_laser_push_seg(l, &zero, pt);
	}
}

static void la_tick_hitbox(aos_world *w, aos_laser *l)
{
	float pt[3] = {aos_cos(l->angle) * 800.0f + l->origin[0], aos_sin(l->angle) * 800.0f + l->origin[1], 0};
	int h = aos_spawn_hitbox(w, ID, 400, pt, l->angle, l->ownerId, 0, l->team);
	if (l->age % 5 != 0 && h >= 0)
		w->obj.hitbox[h].damage = 0;
}

static bool la_update(aos_world *w, aos_laser *l) /* FUN_00627a30 */
{
	switch (l->id) {
	case 100: {
		aos_enemy *e = aos_enemy_by_id(w, l->ownerId);
		l->nseg = 0;
		if (0x13 < l->age || !e)
			return false;
		l->angle = e->angle;
		l->origin[0] = aos_cos(l->angle) * 60.0f + e->pos[0];
		l->origin[1] = aos_sin(l->angle) * 60.0f + e->pos[1];
		la_segs(w, l, false);
		return true;
	}
	case 0x6e:
	case 0x78: {
		aos_enemy *e = aos_enemy_by_id(w, l->ownerId);
		l->nseg = 0;
		if ((l->id == 0x6e ? 0x3b : 0x95) < l->age || !e)
			return false;
		l->angle = e->angle;
		l->origin[0] = aos_cos(l->angle) * 30.0f + e->pos[0];
		l->origin[1] = aos_sin(l->angle) * 30.0f + e->pos[1];
		int col = team_col(w, l->team, 2);
		if (l->id == 0x6e) {
			if (l->age % 5 == 0)
				gfx(w, 0x516, col, l->origin, 0, l->ownerId, 0, l->team);
			if (l->age < 0x32)
				la_tick_hitbox(w, l);
		} else {
			sbfx(w, 200, col, l->origin, 0, l->ownerId, 0, l->team);
			if (l->age % 3 == 0)
				gfx(w, 0x515, col, l->origin, 0, l->ownerId, 0, l->team);
			if (l->age < 0x82)
				la_tick_hitbox(w, l);
		}
		la_segs(w, l, true);
		aos_se_cd(w, 9, 5);
		return true;
	}
	case 300: {
		aos_se_cd(w, 9, 5);
		aos_enemy *e = aos_enemy_by_id(w, l->ownerId);
		if (e) {
			l->origin[0] = aos_cos(e->angle) * 250.0f + e->pos[0];
			l->origin[1] = aos_sin(e->angle) * 250.0f + e->pos[1];
			l->angle = e->angle2;
		}
		if (l->age < 0x33 && l->age % 5 == 0)
			sbfx(w, 0x352, 0, l->origin, l->angle, l->ownerId, 0, l->team);
		if (0x4f < l->age)
			return false;
		if (9 < l->age && l->age < 0x3d && l->age % 5 == 0) {
			/* FUN_00612830 on the owner: training display only */
			float pt[3] = {aos_cos(l->angle) * 800.0f + l->origin[0], aos_sin(l->angle) * 800.0f + l->origin[1], 0};
			aos_spawn_hitbox(w, ID, 600, pt, l->angle, l->ownerId, 0, l->team);
		}
		if (10 < l->age && l->age < 0x28 && l->age % 3 == 0) {
			for (int k = 1; k < 0x10; k++) {
				float a = aos_rand_real(w, -AOS_PI, AOS_PI);
				float d = aos_rand_real(w, 10.0f, 160.0f);
				float pt[3] = {aos_cos(a) * d + l->origin[0] + aos_cos(l->angle) * (float)(k * 100),
				               aos_sin(a) * d + l->origin[1] + aos_sin(l->angle) * (float)(k * 100), 0};
				aos_spawn_spark(w, 0x7da, team_col(w, l->team, 2), 10, 1, l->angle + AOS_PI, pt, l->ownerId);
			}
		}
		return true;
	}
	default:
		return true;
	}
}

/* One FUN_0047f7b0(sprite 0x123 sb_laser)->vtable+0x68 blit: screen-space rect
 * of half size (hw, hh) world units, rotated by the laser angle, additive. */
static void la_blit(const aos_laser *l, float x, float y, float hw, float hh, int s0, int s1, int s2,
                    int s3, uint32_t argb)
{
	aos_dcmd *c = aos_draw(aos_tex("sb_laser"), team_bank(l->team), x, y, 0);
	c->halfW = hw;
	c->halfH = hh;
	c->hasSrc = 1;
	c->src[0] = (float)s0; c->src[1] = (float)s1; c->src[2] = (float)s2; c->src[3] = (float)s3;
	c->rotZ = aos_rad2bam(l->angle);
	c->blend = AOS_BLEND_ADD_ALPHA;
	c->rgba = (argb & 0x00ffffffu) << 8 | argb >> 24;
}

static void la_draw(aos_world *w, aos_laser *l) /* FUN_006264f0 (exact) */
{
	switch (l->id) {
	case 100:
		for (int i = 0; i < l->nseg; i++)
			la_blit(l, l->seg[i].pos[0], l->seg[i].pos[1], 100.0f, 10.0f, 0, 0x100, 0x3f, 0x13f, 0x20ffffffu);
		for (int i = 0; i < l->nseg; i++)
			if (l->age % 3 != i % 4)
				la_blit(l, l->seg[i].pos[0], l->seg[i].pos[1], 60.0f, 2.0f, 0x100, 0x100, 0x13f, 0x13f, 0xffffffffu);
		break;
	case 0x6e:
	case 0x78: {
		bool late = l->id == 0x6e ? 0x32 < l->age : 0x82 < l->age;
		float k = l->id == 0x6e ? (float)(0x3c - l->age) / 10.0f : (float)(0x96 - l->age) / 20.0f;
		for (int i = 0; i < l->nseg; i++) {
			if (i == 0) {
				float h = 48.0f; /* DAT_008140e4 */
				if (late) h *= k;
				la_blit(l, l->seg[i].pos[0], l->seg[i].pos[1], h, h, 0x1c0, 0x100, 0x1ff, 0x13f, 0xffffffffu);
			} else {
				float hh = 60.0f;
				if (late) hh *= k;
				la_blit(l, l->seg[i].pos[0], l->seg[i].pos[1], 100.0f, hh, 0, 0x100, 0x3f, 0x13f, 0x20ffffffu);
			}
		}
		for (int i = 1; i < l->nseg; i++) {
			float hh = 16.0f;
			if (late) hh *= k;
			la_blit(l, l->seg[i].pos[0], l->seg[i].pos[1], 60.0f, hh, 0x100, 0x100, 0x13f, 0x13f, 0xffffffffu);
		}
		break;
	}
	case 300:
		for (int i = 0; i < 0x14; i++) {
			float x = aos_cos(l->angle) * (float)(i * 100) + l->origin[0];
			float y = aos_sin(l->angle) * (float)(i * 100) + l->origin[1];
			int c = 3 - (l->age / 2 + i) % 4;
			float h1 = 300.0f, h2 = 200.0f; /* DAT_008145f0, DAT_008147cc */
			if (w->frame % 2 == 0)
				h2 *= 0.85f;
			if (l->age < 10) {
				h1 = ((float)l->age / 10.0f) * h1;
				h2 = ((float)l->age / 10.0f) * h2;
			}
			if (0x32 < l->age) {
				h1 = ((float)(0x50 - l->age) / 30.0f) * h1;
				h2 = ((float)(0x50 - l->age) / 30.0f) * h2;
			}
			la_blit(l, x, y, 400.0f, h1, c * 0x40, 0x100, c * 0x40 + 0x3f, 0x13f, 0x30ffffffu);
			la_blit(l, x, y, 400.0f, h2, c * 0x40, 0x100, c * 0x40 + 0x3f, 0x13f, 0x40ffffffu);
		}
		break;
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* effect family (FUN_005094a0 / 0050bb10 / 00509ac0)                 */

static int fx_spawn(aos_world *w, int32_t id, int32_t variant, const float pos[3], float angle,
                    int32_t owner, int32_t age0, int32_t team) /* FUN_005094a0 */
{
	float sc, al, aux = 0;
	int layer = 1;
	bool rnd = false;
	switch (id) {
	case 0x1fe: case 500: sc = 0.1f; al = 1.0f; layer = 0; rnd = true; break;
	case 300: sc = 2.0f; al = 0.0f; break;
	case 100: case 0x96: case 200: sc = 1.0f; al = 1.0f; break;
	case 0x1f5: case 0x1ff: sc = 0.3f; al = 1.0f; rnd = true; break;
	case 0x2c6: case 700: sc = 1.0f; al = 1.0f; layer = 0; break;
	case 600: sc = 1.0f; al = 1.0f; aux = 0.05f; break;
	case 0x352: sc = 0.0f; al = 1.0f; aux = 0.2f; break;
	default: return -1; /* 800: no spawner (dead) */
	}
	int i = aos_effect_new(w, ID, id, variant, pos, angle, owner, age0, team);
	if (i < 0) {
		if (rnd)
			aos_rand_real(w, -AOS_PI, AOS_PI); /* keep the RNG stream on pool overflow */
		return -1;
	}
	aos_effect *e = &w->obj.effect[i];
	e->drawLayer = layer;
	e->scale = sc;
	e->alpha = al;
	e->moveDir = angle;
	e->rotation = rnd ? aos_rand_real(w, -AOS_PI, AOS_PI) : angle;
	e->aux = aux;
	return i;
}

static bool fx_update(aos_world *w, aos_effect *e) /* FUN_0050bb10 */
{
	switch (e->id) {
	case 300:
		if (e->age < 100) {
			e->alpha += 0.1f;
			e->scale -= 0.1f;
			if (1.0f < e->scale)
				return true;
			e->alpha = 1.0f;
			e->scale = 1.0f;
			e->age = 100;
			e->drawLayer = 0;
			return true;
		}
		if (e->age < 0x78)
			return true;
		e->alpha -= 0.05f;
		e->scale -= 0.05f;
		return 0.0f < e->scale;
	case 100:
	case 0x96:
		e->scale += 0.02f;
		if (e->scale < 1.0f)
			e->scale += 0.15f;
		if (5 < e->age)
			e->alpha -= 0.02f;
		e->rotation = aos_rand_real(w, -AOS_PI, AOS_PI);
		if ((e->age < 0xf && e->age % 3 == 0) || (e->age < 0x1e && e->age % 5 == 0)) {
			float r = aos_rand_real(w, -AOS_PI, AOS_PI);
			aos_spawn_particle(w, 1000, e->variant, e->pos, r, e->ownerId, 0, 3);
		}
		if (e->alpha <= 0.0f)
			return false;
		if (e->id != 100) {
			if (e->age == 2)
				aos_spawn_hitbox(w, ID, 0x96, e->pos, e->moveDir, e->ownerId, 0, e->team);
			return true;
		}
		if (e->age < 2 || 6 < e->age)
			return true;
		if (e->age % 2 == 0)
			aos_spawn_hitbox(w, ID, 100, e->pos, e->moveDir, e->ownerId, 0, e->team);
		return true;
	case 200:
		return e->age != 2;
	case 500:
	case 0x1fe:
		if (8 < e->age) {
			e->alpha -= 0.03f;
			e->drawLayer = 0;
		}
		for (int k = 0; k < e->age / 10 + 1; k++)
			e->scale += 0.01f;
		if (5 < e->age)
			e->scale = aos_rand_real(w, -0.05f, 0.05f) + e->scale;
		if (0x14 < e->age)
			e->scale = e->scale - aos_rand_real(w, -0.01f, 0.075f);
		e->pos[0] = aos_cos(e->moveDir) * 2.0f + e->pos[0];
		e->pos[1] = aos_sin(e->moveDir) * 2.0f + e->pos[1];
		e->rotation = aos_rand_real(w, -AOS_PI, AOS_PI);
		return 0.0f < e->alpha;
	case 0x1f5:
	case 0x1ff:
		if (8 < e->age) {
			e->alpha -= 0.04f;
			e->drawLayer = 1;
		}
		for (int k = 0; k < e->age / 10 + 1; k++)
			e->scale += 0.005f;
		if (5 < e->age)
			e->scale = aos_rand_real(w, -0.05f, 0.05f) + e->scale;
		if (0x14 < e->age)
			e->scale = e->scale - aos_rand_real(w, -0.01f, 0.075f);
		if (10 < e->age)
			e->rotation = aos_rand_real(w, -AOS_PI, AOS_PI);
		e->pos[0] = aos_cos(e->moveDir) * 2.0f + e->pos[0];
		e->pos[1] = aos_sin(e->moveDir) * 2.0f + e->pos[1];
		e->pos[0] = aos_rand_real(w, -2.0f, 2.0f) + e->pos[0];
		e->pos[1] = aos_rand_real(w, -2.0f, 2.0f) + e->pos[1];
		return 0.0f < e->alpha;
	case 0x352:
		if (e->age < 10) {
			e->scale += e->aux;
			e->alpha -= 0.02f;
		} else {
			e->aux *= 0.95f;
			e->scale += e->aux;
			e->alpha -= 0.025f;
		}
		return 0.0f < e->alpha;
	case 0x2c6:
		e->alpha -= 0.025f;
		e->scale += 0.1f;
		return 0.0f < e->alpha;
	case 600:
		e->scale += e->aux;
		e->aux *= 0.95f;
		if (10 < e->age)
			e->alpha -= 0.025f;
		return 0.0f < e->alpha;
	case 700: {
		if (e->age < 0x51)
			e->alpha += 0.05f;
		else
			e->alpha -= 0.05f;
		if (1.0f < e->alpha)
			e->alpha = 1.0f;
		if (0x50 < e->age && e->alpha <= 0.0f)
			return false;
		float pos[3] = {e->pos[0], e->pos[1], e->pos[2]};
		aos_player *o = own_player(w, e->team == 1 ? 2 : 1); /* FUN_00780480(team) */
		if (o && o->statusOn[AOS_STATUS_OVERHEAT] == 0) {
			float dx = o->x - e->pos[0], dy = o->y - e->pos[1];
			if (aos_sqrt(dx * dx + dy * dy) < 50.0f)
				o->heat += 0.01f;
		}
		if (e->age % 10 == 0)
			sbfx(w, 0x2c6, 0, pos, aos_rand_real(w, -AOS_PI, AOS_PI), e->ownerId, 0, e->team);
		if (e->age % 3 != 0)
			return true;
		float a = aos_rand_real(w, -AOS_PI, AOS_PI);
		float d = aos_rand_real(w, 5.0f, 50.0f);
		float pt[3] = {aos_cos(a) * d + e->pos[0], aos_sin(a) * d + e->pos[1], 0};
		gfx(w, team_pal(w, e->team) == 0 ? 3000 : 0xbc2, 0, pt, HALF_PI, e->ownerId, 0, e->team);
		return true;
	}
	default:
		return true;
	}
}

static aos_dcmd *fx_quad(const aos_effect *e, const char *name, int bank, float half)
{
	aos_dcmd *c = aos_draw(aos_tex(name), bank, e->pos[0], e->pos[1], 0);
	c->halfW = c->halfH = half;
	return c;
}

static void fx_draw(aos_world *w, aos_effect *e) /* FUN_00509ac0 (visual only) */
{
	int bank = team_bank(e->team);
	char name[24];
	switch (e->id) {
	case 500: case 0x1fe: case 0x1f5: case 0x1ff: {
		bool big = e->id == 500 || e->id == 0x1fe;
		bool blue = e->id == 0x1fe || e->id == 0x1ff;
		int f = (e->age / 3) * 2;
		if (f > 4) f = 4;
		f += e->age % 2;
		static const char *const fl[2][6] = {{"sb_flamer0", "sb_flamer1", "sb_flamer2", "sb_flamer3", "sb_flamer4", "sb_flamer5"},
		                                     {"sb_flameb0", "sb_flameb1", "sb_flameb2", "sb_flameb3", "sb_flameb4", "sb_flameb5"}};
		const char *tn = name;
		if (f >= 0 && f < 6) /* literal names: no snprintf per draw (perf) */
			tn = fl[blue][f];
		else
			snprintf(name, sizeof name, blue ? "sb_flameb%d" : "sb_flamer%d", f);
		float half = (big ? 72.0f : 60.0f) * e->scale, rot = e->rotation;
		if (half < (big ? 30.0f : 25.0f)) {
			half = big ? 30.0f : 25.0f;
			rot = e->moveDir; /* the original writes +0x38 = +0x34 here; draw-local in the port */
		}
		aos_dcmd *c = fx_quad(e, tn, bank, half);
		c->rotZ = aos_rad2bam(rot);
		if (big) {
			float g = 128.0f * e->alpha / 255.0f;
			c->rgba = aos_rgba(g, g, g, g);
		} else {
			c->blend = AOS_BLEND_ADD_ALPHA;
			c->rgba = aos_rgba(1, 1, 1, 200.0f * e->alpha / 255.0f);
		}
		break;
	}
	case 600: case 0x2c6: case 700: {
		aos_dcmd *c = fx_quad(e, e->id == 700 ? "sb_field1" : "sb_field", bank,
		                      (e->id == 600 ? 96.0f : e->id == 700 ? 72.0f : 32.0f) * e->scale);
		c->rotZ = aos_rad2bam(e->rotation);
		if (e->id == 700) {
			float g = pulse(e->age, 0.25f, 0.75f) * 128.0f / 255.0f;
			c->rgba = aos_rgba(g, g, g, 64.0f * e->alpha / 255.0f);
		} else {
			c->blend = AOS_BLEND_ADD_ALPHA;
			c->rgba = aos_rgba(1, 1, 1, (e->id == 600 ? 200.0f : 128.0f) * e->alpha / 255.0f);
		}
		break;
	}
	case 0x352: {
		bool red = team_pal(w, e->team) == 0;
		for (int k = 0; k < 2; k++) {
			aos_dcmd *c = fx_quad(e, k ? "sb_impct01" : "sb_impct00", bank, 0);
			c->halfW = (24.0f - (float)e->age * 0.1f) * e->scale;
			c->halfH = 128.0f * e->scale;
			c->rotZ = aos_rad2bam(e->rotation);
			c->blend = AOS_BLEND_ADD_ALPHA;
			/* (uint)(K*alpha) << 24: 256·alpha wraps to 0 at alpha ~1 — literal */
			float a = (float)((uint32_t)(int32_t)((k ? 128.0f : 256.0f) * e->alpha) & 0xffu) / 255.0f;
			c->rgba = red ? aos_rgba(1, 0.25f, 1, a) : aos_rgba(0.25f, 1, 1, a);
		}
		break;
	}
	case 100: case 0x96: case 200: case 300: {
		/* shared atlases: 100/0x96 burst0..3 [variant/4], 200/300 explosion0.. [variant/8] */
		static const char *atl[6] = {"explosion0", "explosion1", "burst0", "burst1", "burst2", "burst3"};
		int v = e->variant < 0 ? 0 : e->variant, idx, col, row;
		float h1, h2;
		if (e->id == 100 || e->id == 0x96) {
			idx = 2 + v / 4;
			row = v & 3;
			col = 2;
			h1 = (120.0f + 30.0f) * e->scale;
			h2 = ((e->age / 3) % 2 == 1 ? 120.0f : 100.0f) * e->scale;
			if (e->age % 2 == 0) h2 *= 0.85f;
		} else {
			idx = v / 8;
			row = (v % 8) % 4;
			col = ((v % 8) / 4) * 2;
			h1 = (e->id == 300 ? 180.0f : 90.0f) * e->scale;
			h2 = (e->id == 300 ? 200.0f : 100.0f) * e->scale;
			if ((e->id == 300 ? e->age : w->frame) % 2 == 0) h2 *= 0.85f;
		}
		if (idx > 5) idx = 5;
		for (int k = 0; k < 2; k++) {
			aos_dcmd *c = fx_quad(e, atl[idx], AOS_BANK_COMMON, k ? h2 : h1);
			int cx = k ? (e->id == 100 || e->id == 0x96 ? ((e->age / 3) % 2) * 128 : col * 128 + 128) : col * 128;
			c->hasSrc = 1;
			c->src[0] = (float)cx; c->src[1] = (float)(row * 128);
			c->src[2] = (float)(cx + 127); c->src[3] = (float)(row * 128 + 127);
			c->rotZ = aos_rad2bam(-e->rotation); /* these blits pass +0x38 un-negated (screen y-down) */
			c->blend = (k == 0 && (e->id == 100 || e->id == 0x96)) ? AOS_BLEND_ALPHA : AOS_BLEND_ADD_ALPHA;
			c->rgba = aos_rgba(1, 1, 1, (k ? 255.0f : (e->id == 100 || e->id == 0x96 ? 200.0f : 128.0f)) * e->alpha / 255.0f);
		}
		break;
	}
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* whip ribbon (FUN_007c81e0 / 007c8660 / 007c82d0)                   */
/* node[k][0..2] = point B (+0x14+k*0x1c), [3..5] = point A, [6] = used flag */

static bool wh_update(aos_world *w, aos_whip *h) /* FUN_007c8660 */
{
	if (h->id != 4)
		return true;
	aos_missile *m = missile_by_serial(w, h->p3);
	if (!m)
		return false;
	float a = m->moveAngle + HALF_PI;
	float ax = -aos_cos(a) * 30.0f * m->growScale + m->pos[0];
	float ay = -aos_sin(a) * 30.0f * m->growScale + m->pos[1];
	float bx = aos_cos(a) * 30.0f * m->growScale + m->pos[0];
	float by = aos_sin(a) * 30.0f * m->growScale + m->pos[1];
	for (int k = AOS_WHIP_NODES - 1; k >= 0; k--) {
		if (h->node[k][6] == 0.0f) {
			h->node[k][3] = ax; h->node[k][4] = ay; h->node[k][5] = 0;
			h->node[k][0] = bx; h->node[k][1] = by; h->node[k][2] = 0;
			h->node[k][6] = 1.0f;
			return true;
		}
	}
	for (int k = AOS_WHIP_NODES - 1; k > 0; k--)
		for (int j = 0; j < 6; j++)
			h->node[k][j] = h->node[k - 1][j];
	h->node[0][3] = ax; h->node[0][4] = ay; h->node[0][5] = 0;
	h->node[0][0] = bx; h->node[0][1] = by; h->node[0][2] = 0;
	return true;
}

/* FUN_007c82d0 draws a textured triangle strip (UV u = k*12.8, v 0..256, vertex colour
 * 0x80ffffff); the draw API has no strips, so each strip cell is drawn as one rotated quad. */
static void wh_draw(aos_world *w, aos_whip *h)
{
	if (h->id != 4)
		return;
	aos_missile *m = missile_by_serial(w, h->p3);
	int bank = m ? team_bank(m->team) : AOS_BANK_P0; /* original: texture slot of +0x18 */
	for (int k = 0; k < AOS_WHIP_NODES - 1; k++) {
		if (h->node[k][6] == 0.0f || h->node[k + 1][6] == 0.0f)
			continue;
		float mx = (h->node[k][0] + h->node[k][3] + h->node[k + 1][0] + h->node[k + 1][3]) * 0.25f;
		float my = (h->node[k][1] + h->node[k][4] + h->node[k + 1][1] + h->node[k + 1][4]) * 0.25f;
		float cx0 = (h->node[k][0] + h->node[k][3]) * 0.5f, cy0 = (h->node[k][1] + h->node[k][4]) * 0.5f;
		float cx1 = (h->node[k + 1][0] + h->node[k + 1][3]) * 0.5f, cy1 = (h->node[k + 1][1] + h->node[k + 1][4]) * 0.5f;
		float dx = cx1 - cx0, dy = cy1 - cy0;
		float wx = h->node[k][0] - h->node[k][3], wy = h->node[k][1] - h->node[k][4];
		aos_dcmd *c = aos_draw(aos_tex("sb_whip0"), bank, mx, my, 0);
		c->halfW = aos_sqrt(dx * dx + dy * dy) * 0.5f;
		c->halfH = aos_sqrt(wx * wx + wy * wy) * 0.5f;
		c->rotZ = aos_rad2bam(aos_atan2(dy, dx));
		c->hasSrc = 1;
		c->src[0] = (float)k * 12.8f; c->src[1] = 0;
		c->src[2] = (float)(k + 1) * 12.8f; c->src[3] = 256;
		c->rgba = aos_rgba(1, 1, 1, 128.0f / 255.0f);
	}
}

/* ------------------------------------------------------------------ */

void aos_register_starbreaker(void)
{
	aos_tab.inputA[ID] = input_a;
	aos_tab.inputB[ID] = input_b;
	/* CPU AI FUN_0074cb90 / FUN_0074c280: not ported */

	aos_tab.T2[0x101] = st_neutral; aos_tab.T1[0x101] = dr_neutral;
	aos_tab.T2[0x102] = st_stub;    aos_tab.T1[0x102] = dr_dash;
	aos_tab.T2[0x103] = st_stub;    aos_tab.T1[0x103] = dr_ko;
	aos_tab.T2[0x104] = st_stub;    aos_tab.T1[0x104] = dr_hit;
	aos_tab.T2[0x105] = st_stub;    aos_tab.T1[0x105] = dr_sld5;
	aos_tab.T2[0x10c] = st_da;      aos_tab.T1[0x10c] = dr_atka;
	aos_tab.T2[0x10d] = st_btap;    aos_tab.T1[0x10d] = dr_sld5;
	aos_tab.T2[0x10e] = st_bh;      aos_tab.T1[0x10e] = dr_sld5;
	aos_tab.T2[0x10f] = st_bhold;   aos_tab.T1[0x10f] = dr_atka;
	aos_tab.T2[0x110] = st_sb;      aos_tab.T1[0x110] = dr_atka;
	aos_tab.T2[0x111] = st_dh;      aos_tab.T1[0x111] = dr_atka;
	aos_tab.T2[0x113] = st_a1;      aos_tab.T1[0x113] = dr_atkb;
	aos_tab.T2[0x114] = st_a2;      aos_tab.T1[0x114] = dr_atkb;
	aos_tab.T2[0x115] = st_a3;      aos_tab.T1[0x115] = dr_atkb;
	aos_tab.T2[0x116] = st_sa1;     aos_tab.T1[0x116] = dr_atkb;
	aos_tab.T2[0x117] = st_sa2;     aos_tab.T1[0x117] = dr_atkb;
	aos_tab.T2[0x118] = st_sa3;     aos_tab.T1[0x118] = dr_atkb;
	aos_tab.T2[0x119] = st_sh;      aos_tab.T1[0x119] = dr_catch;
	aos_tab.T2[0x11a] = st_shhold;  aos_tab.T1[0x11a] = dr_catch;
	aos_tab.T2[0x11b] = st_h;       aos_tab.T1[0x11b] = dr_h;
	aos_tab.T2[0x11c] = st_hrec;    aos_tab.T1[0x11c] = dr_catch;
	aos_tab.T2[0x11d] = st_accel;   aos_tab.T1[0x11d] = dr_accel;

	aos_otab.enemySpawn[ID] = en_spawn;
	aos_otab.enemyUpdate[ID] = en_update;
	aos_otab.enemyDraw[ID] = en_draw;
	aos_otab.enemyOnKilled[ID] = en_killed;
	aos_otab.missileSpawn[ID] = mi_spawn;
	aos_otab.missileUpdate[ID] = mi_update;
	aos_otab.missileDraw[ID] = mi_draw;
	aos_otab.missileOnHit[ID] = mi_hit;
	aos_otab.missileOnKilled[ID] = mi_killed;
	aos_otab.hitboxSpawn[ID] = hb_spawn;
	aos_otab.hitboxOnHit[ID] = hb_hit;
	aos_otab.laserSpawn[ID] = la_spawn;
	aos_otab.laserUpdate[ID] = la_update;
	aos_otab.laserDraw[ID] = la_draw;
	aos_otab.laserOnHit[ID] = NULL; /* FUN_00627a10 is empty */
	aos_otab.effectSpawn[ID] = fx_spawn;
	aos_otab.effectUpdate[ID] = fx_update;
	aos_otab.effectDraw[ID] = fx_draw;
	aos_otab.whipSpawn[ID] = whip_spawn;
	aos_otab.whipUpdate[ID] = wh_update;
	aos_otab.whipDraw[ID] = wh_draw;
}
