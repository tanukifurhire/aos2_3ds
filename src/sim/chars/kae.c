/* Kae (engine id 14, states 0x1aa..0x1c7, sprites ke_*). 1:1 port of the
 * decompiled functions named on each routine; spec docs/re/kae.md.
 *
 * Blazing mode = status 7 (p->statusOn[AOS_STATUS_OVERHEAT], +0xaa); its
 * per-frame tick (heat >= 2.01, heat = 1.0 on end) is core.c status_tick.
 *
 * Engine APIs used: particles/sparks, screen effects, camera focus and
 * afterimages (fx.h), team colour (aos_team_colour), summon group counter
 * (aos_enemy_group_next). Global tint FUN_004e8f50 and the camera-focus
 * clear FUN_004a9f10 have no engine API (cosmetic, omitted). */
#include <stddef.h>

#include "../draw.h"
#include "../fx.h"
#include "../objects.h"
#include "../sim.h"

#define ID AOS_CH_KAE
#define HALF_PI 1.5707964f   /* DAT_00814104 */
#define QUARTER_PI 0.7853982f /* DAT_0081fa90 */
#define EIGHTH_PI 0.3926991f  /* _DAT_008256c0 */

void aos_se_cd(aos_world *w, int id, int cd); /* FUN_007bcaf0 (core.c) */

/* ------------------------------------------------------------------ */
/* helpers                                                             */

static bool blazing(const aos_player *p) { return p->statusOn[AOS_STATUS_OVERHEAT] == 1; } /* +0xaa */

static aos_player *team_player(aos_world *w, int32_t team) /* FUN_00780450(team) */
{
	return team == 1 ? &w->p[0] : &w->p[1];
}
static aos_player *team_opponent(aos_world *w, int32_t team) /* FUN_00780480(team) */
{
	return team == 1 ? &w->p[1] : &w->p[0];
}

/* DAT_008859f4 + team*0x10: per-team colour slot 0. */
static int32_t team_col(aos_world *w, int32_t team) { return aos_team_colour(w, team, 0); }

static int bank_of_team(int32_t team) { return team == 1 ? AOS_BANK_P0 : AOS_BANK_P1; } /* DAT_00885a00[team] */

/* FUN_0068b8a0 / FUN_007bd5e0 (original argument order). */
static void particle(aos_world *w, int kind, int32_t col, const float pos[3], float ang, int32_t owner,
                     int32_t age0, int n)
{
	aos_spawn_particle(w, kind, col, pos, ang, owner, age0, n);
}
static void spark(aos_world *w, int kind, int32_t col, int len, int n, float ang, const float pos[3], int32_t owner)
{
	aos_spawn_spark(w, kind, col, len, n, ang, pos, owner);
}

static int fx0(aos_world *w, int32_t id, int32_t variant, const float pos[3], float ang, int32_t owner,
               int32_t age0, int32_t team)
{
	return aos_spawn_effect(w, 0, id, variant, pos, ang, owner, age0, team);
}
static int fxk(aos_world *w, int32_t id, int32_t variant, const float pos[3], float ang, int32_t owner,
               int32_t age0, int32_t team)
{
	return aos_spawn_effect(w, ID, id, variant, pos, ang, owner, age0, team);
}

static void do_aim(aos_world *w, aos_player *p) { p->faceAngle = aos_aim(w, p); } /* FUN_007804b0 */

static void face_horiz(aos_world *w, aos_player *p) /* opp.x <= x ? π : 0, +0x88 = +0x84 */
{
	aos_player *o = aos_opponent(w, p);
	if (o->x < p->x || o->x == p->x)
		p->faceAngle = AOS_PI;
	else
		p->faceAngle = 0;
	p->dashAngle = p->faceAngle;
}

static void steer(aos_player *p, float step) /* FUN_0061b230 + FUN_005f0e70 */
{
	float a = aos_dir_angle(p);
	if (a <= AOS_2PI)
		aos_angle_toward(&p->faceAngle, a, step);
}

/* lunge: +0x98 = min(dist/d, cap); +0x88 = +0x94 = +0x84 */
static float lunge(aos_world *w, aos_player *p, float d, float cap)
{
	float s = aos_dist(w, p) / d;
	if (cap <= s)
		s = cap;
	p->dashAngle = p->faceAngle;
	p->moveAngle = p->faceAngle;
	p->speed = s;
	return s;
}

/* Dash sparks (suguri.md §1.3): r, n, effect 0xa28+n variant 0xc. */
static void sparks(aos_world *w, aos_player *p)
{
	if (3.0f <= p->speed) { /* DAT_00813a50 */
		float r = aos_rand_real(w, -0.3f, 0.3f);
		int32_t n = aos_rand_int(w, 0, 100) % 8;
		fx0(w, 0xa28 + n, 0xc, &p->x, r + p->faceAngle, p->objId, 0, p->side);
	}
}

static void ahead(const aos_player *p, float a, float r, float out[3])
{
	out[0] = aos_cos(a) * r + p->x;
	out[1] = aos_sin(a) * r + p->y;
	out[2] = 0;
}

static int hitbox(aos_world *w, aos_player *p, int32_t id, const float pos[3], float ang)
{
	return aos_spawn_hitbox(w, ID, id, pos, ang, p->objId, 0, p->side);
}

/* FUN_00780a50: opponent can be grabbed */
static bool can_grab(aos_world *w, const aos_player *o)
{
	if (o->barrierTime == 0 && o->state != AOS_ST_KO && o->hittable && o->statusOn[AOS_STATUS_STUNIMMUNE] != 1)
		return !w->roundOver;
	return false;
}

/* Grab pin (0x1c4..0x1c7): opponent forced into hitstun 40 px in front. */
static void pin(aos_player *p, aos_player *o)
{
	o->state = AOS_ST_HITSTUN;
	o->timer = 0;
	o->x = aos_cos(p->faceAngle) * 40.0f + p->x; /* DAT_008140e0 */
	o->y = aos_sin(p->faceAngle) * 40.0f + p->y;
	o->speed = 0;
	o->faceAngle = p->faceAngle + AOS_PI;
	aos_angle_normalize(&o->faceAngle);
}

/* FUN_006b1d70(src, range, speed): proximity knock (core.md §4.1). */
static bool knock_near(aos_world *w, aos_player *p, float range, float speed)
{
	aos_player *o = aos_opponent(w, p);
	if (o->state == AOS_ST_KO || o->hittable != 1 || o->statusOn[AOS_STATUS_STUNIMMUNE] != 0)
		return false;
	float dx = p->x - o->x, dy = p->y - o->y;
	if (!(aos_sqrt(dx * dx + dy * dy) < range))
		return false;
	o->state = AOS_ST_HITSTUN;
	o->timer = 0;
	o->faceAngle = aos_atan2(p->y - o->y, p->x - o->x);
	o->moveAngle = o->faceAngle + AOS_PI;
	o->speed = speed;
	aos_se(w, 0x19);
	aos_camera_focus_clear(w); /* FUN_004a9f10 on 0x895144 (@0x6b1e7d) */
	return true;
}

/* hyper start: SE 6, dark(100,.75), speedline(100,pos,0,0), blur(20,.5,2) [+ effect 0xfa angle 0] */
static void flourish(aos_world *w, aos_player *p, int fa)
{
	aos_hyper_flourish_ex(w, p, 6, 100, 0.75f, 100, 0.0f, 0.0f, 0x14, 0.5f, 2, fa);
}

/* ------------------------------------------------------------------ */
/* input dispatch                                                      */

static bool normals(aos_world *w, aos_player *p)
{
	if (p->D == 3) {
		aos_dash(w, p, 0);
	} else if (p->Shield == 3 && aos_spend_hyper(p, 0.5f) && aos_may_shield(w, p)) {
		aos_shield(w, p);
	} else {
		if (p->S > 1 && p->B == 3) {
			aos_set_state(w, p, 0x1c0);
			return true;
		}
		if (p->S > 1 && p->A == 3) {
			aos_set_state(w, p, 0x1bc);
			return true;
		}
		if (p->B == 3) {
			aos_set_state(w, p, blazing(p) ? 0x1bb : 0x1b7);
			return true;
		}
		if (p->D > 1 && p->A == 3) {
			aos_set_state(w, p, 0x1b5); /* (if (0) 0x1ba — compiled out @0x6e08f1) */
			return true;
		}
		if (p->A == 3) {
			aos_set_state(w, p, 0x1b4);
			return true;
		}
	}
	return false;
}

static bool input_a(aos_world *w, aos_player *p) /* FUN_006e0490 */
{
	if (aos_may_hyper(w, p)) {
		if (p->S > 1 && p->B > 1 && p->H == 3 && aos_spend_hyper(p, 3.0f)) {
			aos_set_state(w, p, 0x1c6);
			return true;
		}
		if (p->S > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f)) {
			aos_set_state(w, p, !blazing(p) ? 0x1b9 : 0x1c4);
			return true;
		}
		if (p->B > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f)) {
			aos_set_state(w, p, 0x1c1);
			return true;
		}
		if (p->D > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f)) {
			aos_set_state(w, p, 0x1b8);
			return true;
		}
		if (p->H == 3 && aos_spend_hyper(p, 1.0f)) {
			aos_set_state(w, p, 0x1c3);
			return true;
		}
	}
	if ((p->state == 0 || p->state == 2) && normals(w, p))
		return true;
	return aos_tab.T2[p->state](w, p);
}

static bool input_b(aos_world *w, aos_player *p) /* FUN_006e09c0 */
{
	if (aos_may_hyper(w, p)) {
		if (p->H > 1 && p->Shield == 3 && aos_spend_hyper(p, 3.0f)) {
			aos_set_state(w, p, 0x1c6);
			return true;
		}
		if (p->H > 1 && p->S == 3 && aos_spend_hyper(p, 1.0f)) {
			aos_set_state(w, p, !blazing(p) ? 0x1b9 : 0x1c4);
			return true;
		}
		if (p->H > 1 && p->B == 3 && aos_spend_hyper(p, 1.0f)) {
			aos_set_state(w, p, 0x1c1);
			return true;
		}
		if (p->H > 1 && p->D == 3 && aos_spend_hyper(p, 1.0f)) {
			aos_set_state(w, p, 0x1b8);
			return true;
		}
		if (p->H > 1 && p->A == 3 && aos_spend_hyper(p, 1.0f)) {
			aos_set_state(w, p, 0x1c3);
			return true;
		}
	}
	if ((p->state == 0 || p->state == 2) && normals(w, p))
		return true;
	return aos_tab.T2[p->state](w, p);
}

/* ------------------------------------------------------------------ */
/* states (T2)                                                         */

static bool st_neutral(aos_world *w, aos_player *p) /* 0x1aa T2 FUN_006dbc30 */
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

static bool st_dash_hook(aos_world *w, aos_player *p) /* 0x1ab T2 FUN_006dbd20 (called by common dash) */
{
	if (2.0f <= p->heat && !blazing(p)) { /* DAT_00813a4c */
		aos_status(w, p, AOS_STATUS_OVERHEAT, 500);
		aos_se(w, 0x27);
		fx0(w, 0xa8c, 0, &p->x, 0, p->objId, 0, p->side);
	}
	return true;
}

static bool st_stub(aos_world *w, aos_player *p) /* 0x1ac/0x1ad/0x1ae T2 FUN_006dbdd0/6dbdb0/6dbdc0 */
{
	(void)w, (void)p;
	return true;
}

static bool st_slash(aos_world *w, aos_player *p) /* 0x1b4 T2 FUN_006dbde0 — Slash! (A) */
{
	int t = p->timer;
	if (t == 0)
		do_aim(w, p);
	if (t < 20)
		steer(p, 0.1f);
	if (t > 11 && t < 21 && t % 2 == 0 && aos_use_gauge(p, 0x1b4)) {
		int i = aos_spawn_beam(w, ID, 100, &p->x, p->faceAngle, p->objId, 0, p->side);
		if (!blazing(p) && i >= 0) /* ver > 0x58b */
			w->obj.beam[i].damage = 100;
		aos_se_cd(w, 0x10, 4);
	}
	if (t < 28 || p->D != 3) {
		if (t > 35)
			p->state = 0;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_slaslash(aos_world *w, aos_player *p) /* 0x1b5 T2 FUN_006dbfb0 — Sla-slaaaash! (D+A) */
{
	int t = p->timer;
	if (t == 0)
		do_aim(w, p);
	if (t > 9 && t < 19 && aos_use_gauge(p, 0x1b5)) { /* `t & 0x80000000` = every frame */
		do_aim(w, p);
		float d = (float)(t - 14) * 0.08f; /* DAT_00813f00 */
		float a = aos_fabs(p->faceAngle) <= HALF_PI ? p->faceAngle - d : d + p->faceAngle;
		int i = aos_spawn_beam(w, ID, 200, &p->x, a, p->objId, 0, p->side);
		if (!blazing(p) && i >= 0)
			w->obj.beam[i].damage = 100;
		aos_se_cd(w, 0x10, 3);
	}
	/* ver >= 0x5a1 */
	if (t > 32 && p->D == 3) {
		aos_dash(w, p, 1);
		return true;
	}
	if (t > 40)
		p->state = 0;
	p->friction *= 0.5f;
	return true;
}

static bool st_btap(aos_world *w, aos_player *p) /* 0x1b6 T2 FUN_006dc270 — B tap */
{
	int t = p->timer;
	if (t > 10 && t < 24)
		steer(p, 0.1f);
	if (t > 11 && t < 23 && t % 2 == 0 && aos_use_gauge(p, 0x1b6)) {
		aos_spawn_missile(w, ID, 100, &p->x, p->faceAngle, p->objId, 0, p->side, p->color);
		aos_se_cd(w, 0x1f, 4);
	}
	if (t < 20 || p->D != 3) {
		if (t > 30)
			p->state = 0;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_pow(aos_world *w, aos_player *p) /* 0x1b7 T2 FUN_006dc3f0 — Pow! (B hold) */
{
	int t = p->timer;
	if (t == 0)
		do_aim(w, p);
	if (t < 11 && p->B < 1) {
		p->state = 0x1b6; /* timer kept */
		aos_tab.T2[p->state](w, p);
		return true;
	}
	if (t > 17 && t < 29 && t % 2 == 0 && aos_use_gauge(p, 0x1b7)) {
		float d = (float)(t - 21) / 4.0f * 0.7f; /* DAT_00853b68, DAT_00813f38 */
		aos_spawn_missile(w, ID, 100, &p->x, (d + p->faceAngle) - 0.3f, p->objId, 0, p->side, p->color);
		aos_spawn_missile(w, ID, 100, &p->x, (p->faceAngle - d) - 0.3f, p->objId, 0, p->side, p->color);
		aos_se_cd(w, 0x1f, 4);
	}
	if (t < 40 || p->D != 3) {
		if (t > 50)
			p->state = 0;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_pow_hyper(aos_world *w, aos_player *p) /* 0x1b8 T2 FUN_006dc620 — POW! (D+H) */
{
	int t = p->timer;
	if (t == 0) {
		do_aim(w, p);
		aos_barrier(w, p, 10);
		flourish(w, p, 0);
	}
	if (t > 15 && t < 31 && t % 2 == 0) {
		float d = (float)(t - 21) / 4.0f * 0.5f; /* DAT_00853b1c */
		aos_spawn_missile(w, ID, 0x96, &p->x, (d + p->faceAngle) - 2.2f, p->objId, 0, p->side, p->color);
		aos_spawn_missile(w, ID, 0x96, &p->x, (p->faceAngle - d) + 2.2f, p->objId, 0, p->side, p->color);
		aos_se_cd(w, 0x1f, 4);
	}
	if (t < 40 || p->D != 3) {
		if (t > 60)
			p->state = 0;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_blazing(aos_world *w, aos_player *p) /* 0x1b9 T2 FUN_006dccb0 — Blazing! (S+H) */
{
	int t = p->timer;
	if (t == 0) {
		face_horiz(w, p);
		aos_barrier(w, p, 10);
		flourish(w, p, 1);
	}
	if (t == 20) {
		aos_status(w, p, AOS_STATUS_OVERHEAT, 1000);
		aos_se(w, 0x27);
		fx0(w, 0xa8c, 0, &p->x, 0, p->objId, 0, p->side);
		knock_near(w, p, 160.0f, 18.0f); /* DAT_00814168, DAT_00814144 */
	}
	if (t > 30)
		p->state = 0;
	return true;
}

static bool st_unused_1ba(aos_world *w, aos_player *p) /* 0x1ba T2 FUN_006dc860 — unreachable D+A variant */
{
	int t = p->timer;
	if (t == 0)
		do_aim(w, p);
	if (t > 9 && t < 19 && aos_use_gauge(p, 0x1ba)) {
		/* (an offset vector (60,0) rotated by the shot angle is computed and never used) */
		float a = aos_fabs(p->faceAngle) <= HALF_PI ? (float)(t - 14) * 0.1f + p->faceAngle
		                                            : (float)(t - 14) * -0.1f + p->faceAngle;
		aos_spawn_beam(w, ID, 300, &p->x, a, p->objId, 0, p->side);
	}
	if (t < 30 || p->D != 3) {
		if (t > 50)
			p->state = 0;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_fooom(aos_world *w, aos_player *p) /* 0x1bb T2 FUN_006dcb00 — Fooom!★ (B hold, Blazing) */
{
	int t = p->timer;
	if (t == 0)
		face_horiz(w, p);
	if (t < 11 && p->B < 1) {
		do_aim(w, p);
		p->state = 0x1b6;
		aos_tab.T2[p->state](w, p);
		return true;
	}
	if (t == 15 && aos_use_gauge(p, 0x1bb)) {
		fxk(w, 500, 0, &p->x, 0, p->objId, 0, p->side);
		aos_se(w, 0x26);
	}
	if (t < 25 || p->D != 3) {
		if (t > 40)
			p->state = 0;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_zoom1(aos_world *w, aos_player *p) /* 0x1bc T2 FUN_006dce90 — Zooooom! 1 (S+A) */
{
	int t = p->timer;
	float pos[3];
	if (t == 0) {
		do_aim(w, p);
		float s = lunge(w, p, 10.0f, 20.0f); /* DAT_00813f4c, DAT_008140fc */
		if (10.0f < s)
			aos_se(w, 0x24);
	}
	sparks(w, p);
	if (t == 5)
		aos_se(w, 0x1f);
	if (t == 10 && aos_use_gauge(p, 0x1bc)) {
		ahead(p, p->faceAngle, 60.0f, pos); /* DAT_00813a58 */
		hitbox(w, p, blazing(p) ? 0x65 : 100, pos, p->faceAngle);
	}
	if (t < 20 && 1.0f <= p->speed && aos_dist(w, p) < 50.0f) /* DAT_008140ec */
		p->speed = 0;
	if (t < 12 || p->hitFlag != 1 || p->A != 3) {
		if (t < 35 || p->D != 3) {
			if (t > 40)
				p->state = 0;
		} else {
			aos_dash(w, p, 1);
		}
	} else {
		aos_set_state(w, p, 0x1bd);
	}
	return true;
}

/* 0x1bd T2 FUN_006dd270 / 0x1be T2 FUN_006dd550 — chain hits 2 and 3 */
static bool chain_mid(aos_world *w, aos_player *p, int32_t costState, int32_t id, int32_t idStar, int32_t next)
{
	int t = p->timer;
	float pos[3];
	if (t == 0) {
		do_aim(w, p);
		lunge(w, p, 15.0f, 10.0f); /* DAT_00853b88, DAT_00813f4c */
	}
	if (t == 5)
		aos_se(w, 0x1f);
	if (t == 8 && aos_use_gauge(p, costState)) {
		ahead(p, p->faceAngle, 40.0f, pos); /* DAT_008140e0 */
		hitbox(w, p, blazing(p) ? idStar : id, pos, p->faceAngle);
	}
	if (t < 12 || p->hitFlag != 1 || p->A != 3) {
		if (t < 30 || p->D != 3) {
			if (t > 40)
				p->state = 0;
		} else {
			aos_dash(w, p, 1);
		}
	} else {
		aos_set_state(w, p, next);
	}
	return true;
}
static bool st_zoom2(aos_world *w, aos_player *p) { return chain_mid(w, p, 0x1bd, 0x6e, 0x6f, 0x1be); }
static bool st_zoom3(aos_world *w, aos_player *p) { return chain_mid(w, p, 0x1be, 0x78, 0x79, 0x1bf); }

static bool st_zoom4(aos_world *w, aos_player *p) /* 0x1bf T2 FUN_006dd830 — chain finisher */
{
	int t = p->timer;
	float pos[3];
	if (t == 0) {
		do_aim(w, p);
		lunge(w, p, 15.0f, 10.0f);
	}
	if (t == 5)
		aos_se(w, 0x20);
	if (t == 8 && aos_use_gauge(p, 0x1bf)) {
		ahead(p, p->faceAngle, 40.0f, pos);
		float a = aos_dir_angle(p);
		if (AOS_PI < a)
			a = p->faceAngle;
		hitbox(w, p, blazing(p) ? 0x83 : 0x82, pos, a);
		p->faceAngle = a;
	}
	if (t > 0x22) /* ver > 0x5a0 */
		p->state = 0;
	return true;
}

static bool st_baaang(aos_world *w, aos_player *p) /* 0x1c0 T2 FUN_006ddab0 — Baaang! (S+B) */
{
	int t = p->timer;
	if (t == 0)
		face_horiz(w, p);
	if (((t == 10 && blazing(p)) || t == 15) && aos_use_gauge(p, 0x1c0)) {
		int i = aos_spawn_enemy(w, ID, 100, p->objId, p->side, p->color);
		if (i >= 0) {
			aos_enemy *e = &w->obj.enemy[i];
			float lx = 0, ly = 0; /* ver >= 0x5a1 */
			if (p->up > 1) ly = 70.0f;     /* DAT_008145ec */
			if (p->down > 1) ly = -70.0f;  /* DAT_008256c8 */
			if (p->right > 1) lx = 70.0f;
			if (p->left > 1) lx = -70.0f;
			e->pos[0] = p->x + lx;
			e->pos[1] = p->y + ly;
			e->angle = p->faceAngle;
			aos_se_cd(w, 0x2f, 4);
			fx0(w, 0xaf0, team_col(w, p->side), w->obj.enemy[i].pos, p->faceAngle, p->objId, 0, p->side);
		} else {
			aos_se_cd(w, 0x2f, 4);
		}
	}
	if (t < 25 || p->D != 3) {
		if (t > 40)
			p->state = 0;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_babang(aos_world *w, aos_player *p) /* 0x1c1 T2 FUN_006ddee0 — BA-BA-BAANG! (B+H) */
{
	int t = p->timer;
	if (t == 0) {
		face_horiz(w, p);
		p->dashAngle = aos_aim(w, p);
		aos_barrier(w, p, 10);
		flourish(w, p, 0);
	}
	if (t > -1 && t < 25 && t % 3 == 0) {
		float a;
		if (t % 6 == 0)
			a = (float)(t / 6) * QUARTER_PI + p->dashAngle + EIGHTH_PI;
		else
			a = (p->dashAngle - (float)(t / 6) * QUARTER_PI) - EIGHTH_PI;
		int i = aos_spawn_enemy(w, ID, 0x96, p->objId, p->side, p->color);
		if (i >= 0) {
			aos_enemy *e = &w->obj.enemy[i];
			e->pos[0] = aos_cos(a) * 160.0f + p->x; /* DAT_00814168 */
			e->pos[1] = aos_sin(a) * 160.0f + p->y;
			e->angle = p->faceAngle;
		}
		aos_se_cd(w, 0x2f, 4);
		if (i >= 0)
			fx0(w, 0xaf0, team_col(w, p->side), w->obj.enemy[i].pos, p->faceAngle, p->objId, 0, p->side);
	}
	if (blazing(p) && t == 10) {
		for (int k = -2; k < 2; k++) {
			float a = (float)k / 2.0f * AOS_PI + QUARTER_PI;
			float pos[3] = {aos_cos(a) * 160.0f + p->x, aos_sin(a) * 160.0f + p->y, 0};
			fxk(w, 500, 0, pos, 0, p->objId, 0, p->side);
		}
		aos_se(w, 0x26);
	}
	if (t == 0x32)
		aos_set_state(w, p, 0x1c2);
	return true;
}

static bool st_babang_slash(aos_world *w, aos_player *p) /* 0x1c2 T2 FUN_006de3a0 */
{
	int t = p->timer;
	float pos[3];
	if (t == 0) {
		do_aim(w, p);
		float s = lunge(w, p, 5.0f, 24.0f); /* DAT_00813f40, DAT_00853b8c */
		if (10.0f < s)
			aos_se(w, 0x24);
	}
	sparks(w, p);
	if (t == 5 && aos_use_gauge(p, 0x1bc)) {
		ahead(p, p->faceAngle, 60.0f, pos);
		hitbox(w, p, 0x104, pos, p->faceAngle);
	}
	if (t < 6 && 1.0f <= p->speed && aos_dist(w, p) < 60.0f)
		p->speed *= 0.25f;
	if (t < 10 || p->D != 3) {
		if (t > 30)
			p->state = 0;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_zoooom(aos_world *w, aos_player *p) /* 0x1c3 T2 FUN_006de6b0 — ZOOOOOM! (H) */
{
	int t = p->timer;
	if (t == 0) {
		p->dashAngle = aos_aim(w, p);
		aos_barrier(w, p, 10);
		flourish(w, p, 1);
	}
	if (t == 10) {
		do_aim(w, p);
		p->speed = 24.0f; /* DAT_00853b8c */
		p->moveAngle = p->faceAngle;
		for (int i = 0; i < 5; i++) {
			float r = aos_rand_real(w, -0.15f, 0.15f); /* DAT_00815dc4, DAT_00813f20 */
			int32_t n = aos_rand_int(w, 0, 100) % 4;
			fx0(w, 0xa32 + n, 0xd, &p->x, r + p->dashAngle, p->objId, i * -3, p->side);
		}
	}
	if (t > 9 && t < 31 && aos_dist(w, p) < 60.0f)
		p->speed = 5.0f;
	if (t > 9 && t < 30) {
		int h = hitbox(w, p, blazing(p) ? 0x12d : 300, &p->x, p->faceAngle);
		if (t % 3 != 0 && h >= 0)
			w->obj.hitbox[h].damage = 0;
		if (t % 3 == 0)
			fxk(w, 600, 0, &p->x, p->faceAngle, p->objId, 0, p->side);
		if (5.0f < p->speed) { /* `t & 0x80000000` = always */
			float a = aos_rand_real(w, -HALF_PI, HALF_PI);
			float v[3] = {aos_cos(a) * 30.0f - 10.0f, aos_sin(a) * 50.0f, 0};
			aos_vec_rotate(v, p->faceAngle);
			v[0] += p->x;
			v[1] += p->y;
			spark(w, 0x7da, team_col(w, p->side), 6, 1, p->faceAngle + AOS_PI, v, p->objId);
		}
	}
	if (t == 0x1e) {
		hitbox(w, p, blazing(p) ? 0x137 : 0x136, &p->x, p->faceAngle + AOS_PI);
		if (p->speed < 10.0f)
			p->speed = 10.0f;
	}
	if (t < 0x32 || p->D != 3) {
		if (t > 0x50)
			p->state = 0;
		p->friction *= 0.5f;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_whaaam(aos_world *w, aos_player *p) /* 0x1c4 T2 FUN_006decf0 — Whaaam!★ (S+H, Blazing) */
{
	int t = p->timer;
	if (t == 0) {
		do_aim(w, p);
		aos_barrier(w, p, 10);
		aos_se(w, 6);
		aos_screen_dark(w, 100, 0.75f);   /* DAT_00813f3c */
		aos_screen_blur(w, 0x14, 0.5f, 2); /* DAT_00853b1c */
	}
	if (t == 10) {
		do_aim(w, p);
		p->speed = 18.0f; /* DAT_00814144 */
		p->moveAngle = p->faceAngle;
		aos_se(w, 0x20);
	}
	if (t > 9 && t < 21 && aos_dist(w, p) < 70.0f) { /* DAT_008145ec */
		aos_player *o = aos_opponent(w, p);
		if (can_grab(w, o)) {
			pin(p, o);
			p->state = 0x1c5;
			p->timer = 0;
			p->hitFlag = 0;
			p->speed *= 0.25f;
			aos_tab.T2[p->state](w, p);
			aos_camera_focus(w, 0x50, p->objId, &p->x, 0.4f); /* DAT_00813f30 */
			aos_screen_dark(w, 100, 0.75f);
			aos_screen_blur(w, 0x14, 0.5f, 2);
			aos_speedline(w, 100, &p->x, 0.0f, 0.0f);
			fx0(w, 0xfa, 0, &p->x, 0, p->objId, 0, p->side);
			return true;
		}
	}
	sparks(w, p);
	if (t < 0x28 || p->D != 3) {
		if (t > 0x3c)
			p->state = 0;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_whaaam_hold(aos_world *w, aos_player *p) /* 0x1c5 T2 FUN_006df110 */
{
	int t = p->timer;
	float pos[3];
	if (t > -1 && t < 0x51) {
		aos_player *o = aos_opponent(w, p);
		if (can_grab(w, o))
			pin(p, o);
	}
	if (t > 0x1b && t < 0x54 && (t - 0x14) % 8 == 0) {
		int32_t id = 100;
		float r = aos_rand_real(w, -0.1f, 0.6f); /* DAT_00814130, DAT_00813f34 */
		if ((t - 0x14) % 16 == 0) {
			r *= -1.0f; /* DAT_00853bb4 */
			id = 0x6e;
		}
		r += p->faceAngle;
		ahead(p, r, 50.0f, pos); /* DAT_008140ec */
		hitbox(w, p, id, pos, r);
	}
	if (t == 0x54) {
		ahead(p, p->faceAngle, 50.0f, pos);
		hitbox(w, p, 0xbe, pos, p->faceAngle);
		fx0(w, 0xa8c, 0, pos, 0, p->objId, 0, p->side);
	}
	if (t < 100 || p->D != 3) {
		if (t > 0x78)
			p->state = 0;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_whaaaam(aos_world *w, aos_player *p) /* 0x1c6 T2 FUN_006df4f0 — WHAAAAM! (S+B+H) */
{
	int t = p->timer;
	if (t == 0) {
		do_aim(w, p);
		aos_barrier(w, p, 0x14);
		aos_se(w, 7);
		aos_screen_dark(w, 0x32, 0.85f); /* DAT_00814120 */
		aos_speedline(w, 0x32, &p->x, 0.0f, 0.0f);
		aos_screen_blur(w, 0x32, 0.75f, 2); /* DAT_00813f3c */
	}
	if (t == 10) {
		do_aim(w, p);
		p->speed = 15.0f; /* DAT_00853b88 */
		p->moveAngle = p->faceAngle;
		aos_se(w, 0x20);
	}
	if (t > 9 && t < 21 && aos_dist(w, p) < 70.0f) {
		aos_player *o = aos_opponent(w, p);
		if (can_grab(w, o)) {
			pin(p, o);
			p->state = 0x1c7;
			p->timer = 0;
			p->hitFlag = 0;
			p->speed *= 0.25f;
			if (o->x < p->x || o->x == p->x)
				p->faceAngle = AOS_PI;
			else
				p->faceAngle = 0;
			p->dashAngle = p->faceAngle;
			aos_tab.T2[p->state](w, p);
			aos_camera_focus(w, 0x50, p->objId, &p->x, 0.4f);
			aos_screen_dark(w, 100, 0.85f);
			aos_screen_blur(w, 100, 0.6f, 2); /* DAT_00813f34 */
			aos_speedline(w, 100, &p->x, 0.0f, 0.0f);
			fx0(w, 0xfa, 0, &p->x, 0, p->objId, 0, p->side);
			if (blazing(p))
				aos_se(w, 0x26);
			return true;
		}
	}
	sparks(w, p);
	if (t < 100 || p->D != 3) {
		if (t > 0x78)
			p->state = 0;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_whaaaam_hold(aos_world *w, aos_player *p) /* 0x1c7 T2 FUN_006df9a0 */
{
	int t = p->timer;
	if (t > -1 && t < 0x47) {
		aos_player *o = aos_opponent(w, p);
		if (can_grab(w, o)) {
			pin(p, o);
			if (blazing(p))
				o->heat += 0.015f; /* DAT_00813ed4 */
		}
	}
	if (t == 0x4b) {
		aos_player *o = aos_opponent(w, p);
		float pos[3];
		ahead(p, p->faceAngle, 50.0f, pos);
		hitbox(w, p, !blazing(p) ? 500 : 0x1fe, pos, HALF_PI);
		fx0(w, 0xa8c, 0, pos, 0, p->objId, 0, p->side);
		fx0(w, 600, 0, pos, 0, p->objId, 0, p->side);
		for (int i = 0; i < 5; i++) {
			pos[0] = o->x;
			pos[1] = o->y;
			pos[2] = o->z;
			pos[0] = aos_rand_real(w, -50.0f, 50.0f) + pos[0]; /* DAT_008149e4, DAT_008140ec */
			pos[1] = aos_rand_real(w, -50.0f, 50.0f) + pos[1];
			fx0(w, 0x834, 0, pos, 0, p->objId, 0, p->side);
		}
		for (int i = 0; i < 6; i++) {
			pos[0] = o->x;
			pos[1] = o->y;
			pos[2] = o->z;
			pos[0] = aos_rand_real(w, -60.0f, 60.0f) + pos[0]; /* DAT_008149e8, DAT_00813a58 */
			pos[1] = aos_rand_real(w, -60.0f, 60.0f) + pos[1];
			fx0(w, 500, 0, pos, 0, p->objId, i * -5 - 5, p->side);
		}
		aos_se(w, 0x27);
	}
	if (t > 8 && t < 0x4c && t % 3 == 0) {
		float v[3] = {p->x, p->y - 30.0f, 0}; /* DAT_008140e8 */
		float r = aos_rand_real(w, -0.25f, 0.25f); /* DAT_0081411c, DAT_00813f24 */
		int32_t n = aos_rand_int(w, 0, 100) % 8;
		int e = fx0(w, 0xa82 + n, team_col(w, p->side), v, r + HALF_PI, p->objId, 0, p->side);
		if (e >= 0)
			w->obj.effect[e].aux2 = 0.5f; /* +0x40 */
	}
	if (t > 8 && t < 0x4c && t % 3 == 0) {
		float a = aos_rand_real(w, -AOS_PI, AOS_PI);
		float rr = aos_rand_real(w, 50.0f, 200.0f); /* DAT_008140ec, DAT_008147cc */
		float v[3] = {aos_cos(a) * rr + p->x, aos_sin(a) * rr * 0.25f + p->y, 0};
		particle(w, 1000, team_col(w, p->side), v, HALF_PI, p->objId, 0, 1);
	}
	if (t > 9 && t < 0x47 && t % 2 == 0) {
		float a = aos_rand_real(w, -AOS_PI, AOS_PI);
		float rr = aos_rand_real(w, 50.0f, 200.0f);
		float v[3] = {aos_cos(a) * rr + p->x, aos_sin(a) * rr * 0.25f + p->y, 0};
		particle(w, 0x44c, team_col(w, p->side), v, HALF_PI, p->objId, 0, 1);
	}
	if (blazing(p) && t > -1 && t < 0x3d && t % 5 == 0) {
		float v[3] = {p->x, p->y - 50.0f, 0};
		fxk(w, 0x208, 0, v, 0, p->objId, 0, p->side);
	}
	if (blazing(p) && t > -1 && t < 0x3d && t % 2 == 0) {
		float a = aos_rand_real(w, -AOS_PI, AOS_PI);
		float rr = aos_rand_real(w, 50.0f, 200.0f);
		float v[3] = {aos_cos(a) * rr + p->x, aos_sin(a) * rr * 0.25f + p->y, 0};
		fx0(w, 3000, 0, v, HALF_PI, p->objId, 0, p->side);
	}
	if (t < 0x5a || p->D != 3) {
		if (t > 100)
			p->state = 0;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

/* ------------------------------------------------------------------ */
/* draw (T1)                                                           */

static int pp(int t, int n) /* (t/n)&3, 3 -> 1 */
{
	int f = (t / n) & 3;
	return f == 3 ? 1 : f;
}

static aos_dcmd *body(aos_world *w, aos_player *p, const char *fmt, int frame)
{
	aos_dcmd *c = aos_draw(aos_texf(fmt, frame), aos_pidx(w, p), p->x, p->y, 0);
	c->halfW = c->halfH = 96.0f * 0.5f; /* DAT_00814118 */
	return c;
}

/* AIM transform (suguri.md §4.2): RotZ(a) [+RotX(π) if |a| >= π/2] + RotY(π).
 * 2D: art faces -x; RotY(π) mirrors it (flipX), then rotate by a and flip
 * vertically when pointing left. */
static aos_dcmd *aimx(aos_dcmd *c, float a)
{
	c->rotZ = aos_rad2bam(a);
	c->flipX = 1;
	if (1.5707964f <= aos_fabs(a))
		c->sy = -c->sy;
	return c;
}

static void dr_aim(aos_world *w, aos_player *p, const char *fmt, int frame)
{
	aimx(body(w, p, fmt, frame), p->faceAngle);
}

static void dr_neutral(aos_world *w, aos_player *p) /* 0x1aa T1 FUN_006e2310 */
{
	int t = p->timer;
	const char *fmt = "ke_std%02d";
	int f = (t / 5) % 8;
	if (4 < f)
		f = 8 - f;
	if (p->up > 1) fmt = "ke_fwd%02d";
	if (p->down > 1) fmt = "ke_bck%02d";
	if (p->facingRight == 0 && p->left > 1) fmt = "ke_fwd%02d";
	if (p->facingRight == 1 && p->left > 1) fmt = "ke_bck%02d";
	if (p->facingRight == 0 && p->right > 1) fmt = "ke_bck%02d";
	if (p->facingRight == 1 && p->right > 1) fmt = "ke_fwd%02d";
	if (fmt[3] != 's') /* not ke_std */
		f = pp(t, 5);
	aos_dcmd *c = body(w, p, fmt, f);
	c->flipX = p->facingRight != 0; /* RotY(π) when +0x8c != 0 */
	int32_t rot[3] = {0, p->facingRight != 0 ? aos_deg2bam(0xb4) : 0, 0};
	aos_afterimage(w, c, 1, p, rot);
}

static void dr_dash(aos_world *w, aos_player *p) /* 0x1ab T1 FUN_006e25f0 */
{
	int f = pp(p->timer, 3);
	float fa = aos_fabs(p->faceAngle);
	if ((p->facingRight == 1 && fa > HALF_PI) || (p->facingRight != 1 && p->facingRight == 0 && fa < HALF_PI))
		f += 3;
	aos_dcmd *c = aimx(body(w, p, "ke_dsh%02d", f), p->faceAngle);
	int32_t rot[3] = {HALF_PI <= aos_fabs(p->faceAngle) ? aos_deg2bam(0xb4) : 0, aos_deg2bam(0xb4),
	                  aos_rad2bam(p->faceAngle)};
	aos_afterimage(w, c, 1, p, rot); /* FUN_004d7e00 */
}

static void dr_ko(aos_world *w, aos_player *p) /* 0x1ac T1 FUN_006e2f80 */
{
	int f = pp(p->timer, 3);
	if (w->frame % 2 == 0) /* DAT_008858f8 */
		f += 3;
	if (p->timer < 100) /* drawn only before the KO explosion (t == 100) */
		dr_aim(w, p, "ke_los%02d", f);
}

static void dr_hit(aos_world *w, aos_player *p) { dr_aim(w, p, "ke_los%02d", pp(p->timer, 3)); } /* 0x1ad T1 FUN_006e29f0 */

static int min_i(int a, int b) { return a < b ? a : b; }

static void dr_sld(aos_world *w, aos_player *p) /* 0x1ae/0x1b9/0x1bb/0x1c0/0x1c1 T1 (6e2cc0/6e4640/6e4380/6e5400/6e56c0) */
{
	dr_aim(w, p, "ke_sld%02d", min_i(p->timer / 5, 3));
}
static void dr_sld0(aos_world *w, aos_player *p) { dr_aim(w, p, "ke_sld%02d", 0); } /* 0x1ba T1 FUN_006e40e0 */

static void dr_atka(aos_world *w, aos_player *p) /* 0x1b4/0x1b5 T1 FUN_006e3280/6e3560 */
{
	int t = p->timer;
	dr_aim(w, p, "ke_atka%02d", t < 8 ? 0 : min_i((t - 8) / 4 + 1, 5));
}
static void dr_atkb(aos_world *w, aos_player *p) /* 0x1b6..0x1b8 T1 FUN_006e3840/6e3b20/6e3e00 */
{
	int t = p->timer;
	dr_aim(w, p, "ke_atkb%02d", t < 10 ? 0 : min_i((t - 10) / 4 + 1, 5));
}
static void dr_infa(aos_world *w, aos_player *p) { dr_aim(w, p, "ke_infghta%02d", min_i(p->timer / 5, 3)); } /* 0x1bc/0x1c2 */
static void dr_infb(aos_world *w, aos_player *p) { dr_aim(w, p, "ke_infghtb%02d", min_i(p->timer / 4, 4)); } /* 0x1bd */
static void dr_infc(aos_world *w, aos_player *p) { dr_aim(w, p, "ke_infghtc%02d", min_i(p->timer / 5, 3)); } /* 0x1be */
static void dr_infd(aos_world *w, aos_player *p) { dr_aim(w, p, "ke_infghtd%02d", min_i(p->timer / 3, 6)); } /* 0x1bf */
static void dr_infb5(aos_world *w, aos_player *p) { dr_aim(w, p, "ke_infghtb%02d", min_i(p->timer / 5, 4)); } /* 0x1c4/0x1c6 */

static void dr_hyp(aos_world *w, aos_player *p) /* 0x1c3 T1 FUN_006e5c40 */
{
	int t = p->timer;
	int f = t < 0xf ? min_i(t / 3, 4) : pp(t - 0xf, 1) + 5;
	dr_aim(w, p, "ke_hyp%02d", f);
}

static void dr_whaaam_hold(aos_world *w, aos_player *p) /* 0x1c5 T1 FUN_006e6200 */
{
	int t = p->timer;
	if (t >= 0x14 && t <= 0x4c && (t - 0x14) % 16 < 8)
		dr_aim(w, p, "ke_infghta%02d", min_i((t - 0x14) % 8 / 3, 3));
	else if (t >= 0x14 && t <= 0x4c)
		dr_aim(w, p, "ke_infghtb%02d", min_i((t - 0x14) % 8 / 3, 4));
	else if (t > 0x4c)
		dr_aim(w, p, "ke_infghtd%02d", min_i((t - 0x4c) / 3, 6));
	else
		dr_aim(w, p, "ke_hyp%02d", min_i(t / 5, 3));
}

static void dr_whaaaam_hold(aos_world *w, aos_player *p) /* 0x1c7 T1 FUN_006e6880 */
{
	int t = p->timer;
	if (t < 0x46) {
		int f = t / 5;
		if (1 < f)
			f = ((f - 2) & 3) + 2;
		if (f == 5)
			f = 3;
		dr_aim(w, p, "ke_infghtb%02d", f);
	} else {
		dr_aim(w, p, "ke_hyp%02d", min_i((t - 0x46) / 4, 3));
	}
}

/* ------------------------------------------------------------------ */
/* beams (type 14): spawn FUN_004ac290, update FUN_004acc90, draw FUN_004ac450, onHit FUN_004aca30 */

static int beam_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team)
{
	if (id != 100 && id != 200 && id != 300)
		return -1;
	int i = aos_beam_new(w, ID, id, pos, angle, owner, age0, team);
	if (i < 0)
		return -1;
	aos_beam *b = &w->obj.beam[i];
	if (id == 100 || id == 200) {
		b->speed = 12.0f; /* DAT_00813f54 */
		b->destroyOnHit = 1;
		b->active = 1;
		b->damage = 0x78;
		b->shape.radius = id == 100 ? 40.0f : 30.0f; /* DAT_008140e0 / DAT_008140e8 */
		b->knockback = 10.0f;
	} else {
		b->speed = 6.0f; /* DAT_00813f44 */
		b->destroyOnHit = 0;
		b->active = 0;
		b->damage = 0;
		b->shape.radius = 0;
		b->knockback = 0;
	}
	return i;
}

static void beam_move(aos_beam *b)
{
	b->pos[0] = aos_cos(b->moveAngle) * b->speed + b->pos[0];
	b->pos[1] = aos_sin(b->moveAngle) * b->speed + b->pos[1];
}

static bool beam_update(aos_world *w, aos_beam *b)
{
	if (b->id == 100) {
		beam_move(b);
		if (!aos_in_arena(b->pos, 30.0f))
			return false;
		fxk(w, 200, team_col(w, b->team), b->pos, b->angle, b->ownerId, 0, b->team);
	} else if (b->id == 200) {
		if (10.0f < b->speed)
			b->speed -= 0.5f;
		if (b->age == 10) /* ver >= 0x58c */
			b->damage = b->damage == 0x78 ? 0x32 : 0x28;
		beam_move(b);
		if (!aos_in_arena(b->pos, 30.0f))
			return false;
		fxk(w, 0xfa, 0, b->pos, b->angle, b->ownerId, 0, b->team);
	} else if (b->id == 300) {
		beam_move(b);
		if (b->age == 0xc || b->age == 0x14)
			aos_spawn_hitbox(w, ID, 400, b->pos, b->moveAngle, b->ownerId, 0, b->team);
		if (0x28 < b->age)
			return false;
		float v[3] = {0, 0, 0};
		v[0] = aos_rand_real(w, -20.0f, 20.0f) + b->pos[0]; /* DAT_00814108, DAT_008140fc */
		v[1] = aos_rand_real(w, -20.0f, 20.0f) + b->pos[1];
		fx0(w, 3000, 0, v, HALF_PI, b->ownerId, 0, b->team);
	}
	return true;
}

static void beam_draw(aos_world *w, aos_beam *b)
{
	(void)w;
	if (b->id != 100 && b->id != 200)
		return;
	float hw = 64.0f * b->scale, hh = (b->id == 100 ? 64.0f : 32.0f) * b->scale; /* DAT_008140f8 / DAT_008140dc */
	for (int pass = 0; pass < 2; pass++) { /* frame 1 alpha-blend, then frame 0 additive */
		aos_dcmd *c = aos_draw(aos_tex(pass == 0 ? "ke_moon01" : "ke_moon00"), bank_of_team(b->team), b->pos[0], b->pos[1], 0);
		c->halfW = hw;
		c->halfH = hh;
		c->rotZ = aos_rad2bam(b->angle);
		c->rgba = aos_rgba(1, 1, 1, (float)(int)(255.0f * b->alpha) / 255.0f);
		c->blend = pass == 0 ? AOS_BLEND_ALPHA : AOS_BLEND_ADD_ALPHA;
	}
}

static void beam_on_hit(aos_world *w, aos_beam *b)
{
	int32_t col = team_col(w, b->team);
	if (b->id == 100) {
		float r = aos_rand_real(w, -AOS_PI, AOS_PI);
		fx0(w, 0x9c4, col, b->pos, r, b->ownerId, 0, b->team);
		r = aos_rand_real(w, -AOS_PI, AOS_PI);
		fxk(w, 300, col, b->pos, r, b->ownerId, 0, b->team);
		aos_se(w, 0x13);
	} else if (b->id == 200) {
		float v[3] = {aos_cos(b->angle) * 20.0f + b->pos[0], aos_sin(b->angle) * 20.0f + b->pos[1], 0};
		fx0(w, 0x3f2, col, v, b->angle + AOS_PI, b->ownerId, 0, b->team);
		particle(w, 1000, col, v, b->angle, b->ownerId, 0, 4);
		spark(w, 1000, col, 6, 4, b->angle, v, b->ownerId);
		aos_se(w, 0x13);
	}
}

/* ------------------------------------------------------------------ */
/* missiles (type 14): spawn FUN_00668720, update FUN_006692e0, draw FUN_006688c0,
 * onHit FUN_00669050, onKilled FUN_006691b0 */

static int missile_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t age0,
                         int32_t team, int32_t p18)
{
	if (id != 100 && id != 0x96)
		return -1;
	int i = aos_missile_new(w, ID, id, pos, angle, owner, age0, team, p18);
	if (i < 0)
		return -1;
	aos_missile *m = &w->obj.missile[i];
	m->growScale = 0;
	m->speed = 12.0f;
	m->hp = 0x3c;
	m->damage = 0x28;
	m->shapeBody.radius = 10.0f;
	m->shapeProj.radius = 10.0f;
	m->knockback = 10.0f;
	return i;
}

static bool missile_update(aos_world *w, aos_missile *m)
{
	if (m->id == 100 || m->id == 0x96) {
		bool homing = m->id == 0x96;
		m->growScale += 0.1f;
		if (1.0f < m->growScale)
			m->growScale = 1.0f;
		if (!homing) {
			if (m->age < 10)
				m->speed -= 1.0f;
			if (0x14 < m->age && m->speed < 10.0f)
				m->speed += 0.5f;
			if (0x14 < m->age)
				fxk(w, 100, team_col(w, m->team), m->pos, m->angle, m->ownerId, 0, m->team);
		} else {
			if (m->age < 0x14)
				m->speed -= 0.5f;
			if (0x28 < m->age && m->speed < 15.0f)
				m->speed += 0.5f;
		}
		m->pos[0] = aos_cos(m->moveAngle) * m->speed + m->pos[0];
		m->pos[1] = aos_sin(m->moveAngle) * m->speed + m->pos[1];
		if (m->age < 1000) {
			if (!aos_in_arena(m->pos, 0)) {
				float wa = aos_atan2(m->pos[1], m->pos[0]);
				if (!homing)
					m->moveAngle = (wa - m->moveAngle) + wa + AOS_PI; /* FUN_005f0ce0 result discarded */
				m->pos[0] = aos_cos(wa) * 540.0f; /* DAT_0081412c */
				m->pos[1] = aos_sin(wa) * 540.0f;
				if (homing) {
					aos_player *o = team_opponent(w, m->team);
					m->moveAngle = aos_atan2(o->y - m->pos[1], o->x - m->pos[0]);
				}
				m->angle = m->moveAngle;
				m->age = 1000;
				aos_se_cd(w, 0x25, 5);
				fx0(w, 0x516, team_col(w, m->team), m->pos, 0, m->ownerId, 0, m->team);
			}
		} else if (!aos_in_arena(m->pos, 30.0f)) {
			m->id = 0xbe;
			m->active = 0;
			return true;
		}
		if (homing && 0x14 < m->age)
			fxk(w, 0x96, team_col(w, m->team), m->pos, m->angle, m->ownerId, 0, m->team);
	} else if (m->id == 0xbe) {
		m->growScale -= 0.1f;
		if (m->growScale <= 0.0f)
			return false;
	}
	return true;
}

static void missile_draw(aos_world *w, aos_missile *m)
{
	(void)w;
	if (m->id != 100 && m->id != 0x96 && m->id != 0xbe)
		return;
	int bank = m->p18 == 0 || m->p18 == 1 ? bank_of_team(m->team) : bank_of_team(m->team);
	float ph = aos_sin((float)(m->age % 0x32) / 50.0f * AOS_2PI);
	float a1 = ph * 0.15f + 0.85f, a2 = ph * 0.25f + 0.75f;
	float s = m->growScale;
	aos_dcmd *c = aos_draw(aos_tex("ke_pile00"), bank, m->pos[0], m->pos[1], 0);
	c->halfW = c->halfH = 32.0f; /* 64 (DAT_008140f8) / 2 */
	c->sx = 3.0f - s * 2.0f;
	c->sy = s;
	c->rgba = s == 1.0f ? aos_rgba(1, 1, 1, 1) : aos_rgba(0, 0, 0, 1); /* +0xdc (a, r, g, b) */
	c->rotZ = aos_rad2bam(m->angle);
	c->blend = AOS_BLEND_ALPHA;
	if (s == 1.0f) {
		for (int k = 1; k <= 2; k++) {
			aos_dcmd *g = aos_draw(aos_texf("ke_pile%02d", k), bank, m->pos[0], m->pos[1], 0);
			g->halfW = g->halfH = 32.0f;
			g->rgba = aos_rgba(1, 1, 1, k == 1 ? a1 : a2);
			g->rotZ = aos_rad2bam(m->angle);
			g->blend = AOS_BLEND_ADD_ALPHA;
		}
	}
}

static bool missile_on_hit(aos_world *w, aos_missile *m)
{
	if (m->id == 100 || m->id == 0x96) {
		int32_t col = team_col(w, m->team);
		fx0(w, 0x9c4, col, m->pos, m->angle, m->ownerId, 0, m->team);
		spark(w, 1000, col, 6, 6, m->angle, m->pos, m->ownerId);
		aos_se(w, 0x15);
	}
	return false;
}

static bool missile_on_killed(aos_world *w, aos_missile *m)
{
	if (m->id == 100 || m->id == 0x96) {
		fx0(w, 0x262, 0, m->pos, 0, m->ownerId, 0, m->team);
		fx0(w, 0x514, 1, m->pos, 0, m->ownerId, 0, m->team);
		aos_se_cd(w, 0x1d, 4);
	}
	return false;
}

/* ------------------------------------------------------------------ */
/* hitboxes (type 14): spawn FUN_005e3290, onHit FUN_005e39d0 (all ver >= 0x58c / > 0x5a0) */

static int hitbox_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t zero, int32_t team)
{
	int32_t dmg;
	float hw = 0, hh = 0, r = 0, kb;
	switch (id) {
	case 400: dmg = 0x3c; r = 40.0f; kb = 5.0f; break;
	case 100: dmg = 100; hw = 100.0f; hh = 80.0f; kb = 2.5f; break;
	case 0x65: dmg = 0x78; hw = 100.0f; hh = 80.0f; kb = 2.5f; break;
	case 0x6e: dmg = 0x3c; hw = 100.0f; hh = 80.0f; kb = 2.5f; break;
	case 0x6f: dmg = 0x46; hw = 100.0f; hh = 80.0f; kb = 2.5f; break;
	case 0x78: dmg = 0x28; hw = 80.0f; hh = 80.0f; kb = 2.5f; break;
	case 0x79: dmg = 0x3c; hw = 80.0f; hh = 80.0f; kb = 2.5f; break;
	case 0x82: dmg = 0xb4; hw = 80.0f; hh = 80.0f; kb = 25.0f; break; /* DAT_008140f0 */
	case 0x83: dmg = 0x10e; hw = 80.0f; hh = 80.0f; kb = 25.0f; break;
	case 0xbe: dmg = 0x96; hw = 80.0f; hh = 80.0f; kb = 30.0f; break;
	case 200: dmg = 0xb4; r = 60.0f; kb = 10.0f; break;
	case 0xfa: dmg = 0x6e; r = 60.0f; kb = 0; break;
	case 0x104: dmg = 0xa0; r = 100.0f; kb = 30.0f; break;
	case 300: dmg = 0x50; r = 80.0f; kb = 5.0f; break;
	case 0x12d: dmg = 0x6e; r = 80.0f; kb = 5.0f; break;
	case 0x136: dmg = 0x78; r = 80.0f; kb = 15.0f; break;
	case 0x137: dmg = 0xa0; r = 80.0f; kb = 15.0f; break;
	case 500: dmg = 0x5dc; hw = 80.0f; hh = 80.0f; kb = 30.0f; break;   /* ver > 0x5a0 */
	case 0x1fe: dmg = 0x708; hw = 80.0f; hh = 80.0f; kb = 30.0f; break; /* ver > 0x5a0 */
	default: return -1;
	}
	int i = aos_hitbox_new(w, ID, id, pos, angle, owner, zero, team);
	if (i < 0)
		return -1;
	aos_hitbox *h = &w->obj.hitbox[i];
	h->dealsDamage = 1;
	h->oncePerFrame = 0;
	h->hitType = 3;
	h->damage = dmg;
	h->shape.halfW = hw;
	h->shape.halfH = hh;
	h->shape.radius = r;
	h->knockback = kb;
	h->stun = 1.0f;
	return i;
}

/* ★ variants: 5 × (effect 3000 + spark 0x4b0) around the victim */
static void star_burst(aos_world *w, aos_hitbox *hb, aos_player *v)
{
	for (int i = 0; i < 5; i++) {
		float q[3] = {v->x, v->y, v->z};
		q[0] = aos_rand_real(w, -30.0f, 30.0f) + q[0]; /* DAT_008140e8 */
		q[1] = aos_rand_real(w, -30.0f, 30.0f) + q[1];
		fx0(w, 3000, 0, q, HALF_PI, hb->ownerId, 0, hb->team);
		spark(w, 0x4b0, 1, 3, 1, HALF_PI, q, hb->ownerId);
	}
}

static void hitbox_on_hit(aos_world *w, aos_hitbox *hb, aos_player *v)
{
	int32_t col = team_col(w, hb->team);
	float r;
	switch (hb->id) {
	case 500:
	case 0x1fe:
		fx0(w, 0x9c5, col, &v->x, HALF_PI, hb->ownerId, 0, hb->team);
		return;
	case 0x136:
	case 0x137:
		r = aos_rand_real(w, -AOS_PI, AOS_PI);
		fx0(w, 0xa8c, col, &v->x, r, hb->ownerId, 0, hb->team);
		fx0(w, 0xaf0, col, &v->x, 0, hb->ownerId, 0, hb->team);
		aos_se(w, 0x19);
		/* fall through */
	case 400: {
		float r0 = aos_rand_real(w, -AOS_PI, AOS_PI);
		for (int k = -2; k < 2; k++) {
			float a = (float)k / 2.0f * AOS_PI;
			float rr = aos_rand_real(w, -0.25f, 0.25f);
			fx0(w, 0x456, col, &v->x, a + rr + r0, hb->ownerId, 0, hb->team);
		}
		v->heat += 0.1f; /* DAT_00853b10 */
		aos_se_cd(w, 0x13, 4);
		return;
	}
	case 100:
	case 0x65:
	case 0x6e:
	case 0x6f:
	case 0x78:
	case 0x79:
		r = aos_rand_real(w, -AOS_PI, AOS_PI);
		fx0(w, 0x9c4, col, &v->x, r, hb->ownerId, 0, hb->team);
		if (hb->id == 100 || hb->id == 0x65)
			fx0(w, 0xaf0, col, &v->x, 0, hb->ownerId, 0, hb->team);
		particle(w, 1000, col, &v->x, hb->angle, hb->ownerId, 0, 4);
		spark(w, 1000, col, 10, 4, hb->angle, &v->x, hb->ownerId);
		team_player(w, hb->team)->hitFlag = 1;
		aos_se_cd(w, 0x18, 3);
		if (hb->id == 0x65 || hb->id == 0x6f || hb->id == 0x79)
			star_burst(w, hb, v);
		return;
	case 0x82:
	case 0x83:
	case 0xbe:
		r = aos_rand_real(w, -AOS_PI, AOS_PI);
		fx0(w, 0x9c4, col, &v->x, r, hb->ownerId, 0, hb->team);
		fx0(w, 0xaf0, col, &v->x, 0, hb->ownerId, 0, hb->team);
		particle(w, 1000, col, &v->x, hb->angle, hb->ownerId, 0, 4);
		spark(w, 1000, col, 10, 4, hb->angle, &v->x, hb->ownerId);
		aos_se(w, 0x19);
		aos_screen_blur(w, 10, 0.75f, 1); /* DAT_00813f3c */
		if (hb->id == 0x83)
			star_burst(w, hb, v);
		return;
	case 200:
	case 0xfa:
		r = aos_rand_real(w, -AOS_PI, AOS_PI);
		fx0(w, 0x9c4, col, &v->x, r, hb->ownerId, 0, hb->team);
		fx0(w, 0xaf0, col, hb->id == 200 ? &v->x : hb->pos, 0, hb->ownerId, 0, hb->team);
		particle(w, 1000, col, &v->x, hb->angle, hb->ownerId, 0, 4);
		spark(w, 1000, col, 10, 4, hb->angle, &v->x, hb->ownerId);
		aos_se_cd(w, 0x18, 3);
		return;
	case 0x104:
		r = aos_rand_real(w, -AOS_PI, AOS_PI);
		fx0(w, 0xa8c, col, &v->x, r, hb->ownerId, 0, hb->team);
		fx0(w, 0xaf0, col, &v->x, 0, hb->ownerId, 0, hb->team);
		aos_se(w, 0x19);
		aos_screen_blur(w, 10, 0.75f, 1);
		return;
	case 300:
	case 0x12d:
		if (hb->damage != 0) {
			r = aos_rand_real(w, -AOS_PI, AOS_PI);
			fx0(w, 0x9c4, col, &v->x, r, hb->ownerId, 0, hb->team);
			fx0(w, 0xaf0, col, &v->x, 0, hb->ownerId, 0, hb->team);
			aos_se_cd(w, 0x18, 3);
		}
		return;
	default:
		return;
	}
}

/* ------------------------------------------------------------------ */
/* clones (Enemy family, type 14): spawn FUN_00520990, update FUN_00520ae0,
 * draw FUN_005217a0, onKilled FUN_00521780. f7c = subtype, f84 = phase, f70 = speed. */

static int enemy_spawn(aos_world *w, int32_t id, int32_t owner, int32_t team, int32_t p18)
{
	if (id != 100 && id != 0x96)
		return -1;
	int32_t grp = aos_enemy_group_next(w); /* FUN_0055efc0 */
	int i = aos_enemy_new(w, ID, owner, team, p18);
	if (i < 0)
		return -1;
	aos_enemy *e = &w->obj.enemy[i];
	e->serial = grp;
	e->f10 = 100;
	e->f7c = id;
	e->f84 = 0;
	e->hp = e->maxHp = 9999;
	e->active = 0;
	e->shape = (aos_shape){0, 0, 0};
	e->scale[0] = 0;
	e->scale[1] = 3.0f; /* DAT_00813a50 */
	e->scale[2] = 0;
	e->mirrorChildren = 0;
	e->motionId = 0;
	e->rehitTimer = 0;
	return i;
}

static void clone_rush_start(aos_world *w, aos_enemy *e, float div, float cap, int32_t sparkVariant, bool seFirst)
{
	e->f84 = 200;
	e->timer2 = 0;
	aos_player *o = team_opponent(w, e->team);
	float dx = o->x - e->pos[0], dy = o->y - e->pos[1];
	e->angle = aos_atan2(dy, dx);
	e->angle2 = e->angle;
	float s = aos_sqrt(dx * dx + dy * dy) / div;
	if (cap <= s)
		s = cap;
	e->f70 = s;
	if (seFirst)
		aos_se(w, 0x1f);
	for (int i = 0; i < 5; i++) {
		float r = aos_rand_real(w, -0.15f, 0.15f);
		int32_t n = aos_rand_int(w, 0, 100) % 4;
		fx0(w, 0xa32 + n, sparkVariant, e->pos, r + e->angle2, e->ownerId, i * -3, e->team);
	}
	if (!seFirst)
		aos_se(w, 0x1f);
}

static void clone_move(aos_enemy *e, float decel)
{
	e->pos[0] = aos_cos(e->angle2) * e->f70 + e->pos[0];
	e->pos[1] = aos_sin(e->angle2) * e->f70 + e->pos[1];
	e->f70 -= decel;
	if (e->f70 <= 0.0f && e->f70 != 0.0f)
		e->f70 = 0;
}

static void clone_strike(aos_world *w, aos_enemy *e, int32_t id)
{
	float v[3] = {aos_cos(e->angle) * 40.0f + e->pos[0], aos_sin(e->angle) * 40.0f + e->pos[1], 0};
	aos_spawn_hitbox(w, ID, id, v, e->angle, e->ownerId, 0, e->team);
}

static bool enemy_update(aos_world *w, aos_enemy *e)
{
	bool big = e->f7c == 100;
	if (e->f7c != 100 && e->f7c != 0x96)
		return true;
	switch (e->f84) {
	case 0:
		e->scale[1] -= 0.2f; /* DAT_00853b14 */
		e->scale[0] += 0.1f;
		if (1.0f <= e->scale[0]) {
			e->scale[0] = 1.0f;
			e->scale[1] = 1.0f;
			e->f84 = 100;
			e->timer2 = 0;
		}
		break;
	case 100:
		if (big ? 100 < e->timer2 : 10 < e->timer2)
			clone_rush_start(w, e, 12.0f, big ? 15.0f : 18.0f, big ? 0xc : 0xd, !big);
		break;
	case 200:
		clone_move(e, big ? 0.45f : 0.5f); /* DAT_00817c0c (ver >= 0x58c) / DAT_00853b1c */
		if (e->timer2 == 10)
			clone_strike(w, e, big ? 200 : 0xfa);
		if (0x28 < e->timer2) { /* ver > 0x58b: no second rush */
			e->f84 = 900;
			e->timer2 = 0;
		}
		break;
	case 300: /* only reachable in ver 0x58a..0x58b */
		clone_move(e, 0.5f);
		if (e->timer2 == 10)
			clone_strike(w, e, 200);
		if (0x28 < e->timer2) {
			e->f84 = 900;
			e->timer2 = 0;
		}
		break;
	default:
		e->scale[1] += 0.2f;
		e->scale[0] -= 0.1f;
		if (e->scale[0] <= 0.0f)
			return false;
		break;
	}
	return true;
}

static void enemy_draw(aos_world *w, aos_enemy *e)
{
	(void)w;
	if (e->f10 != 100)
		return;
	const char *fmt;
	int f;
	if (e->f84 == 100) {
		fmt = "ke_std%02d";
		f = (e->timer2 / 10) % 8;
		if (4 < f)
			f = 8 - f;
	} else if (e->f84 == 200) {
		fmt = "ke_infghtd%02d";
		f = min_i(e->timer2 / 3, 6);
	} else {
		fmt = "ke_sld%02d";
		f = min_i(e->timer2 / 5, 3);
	}
	for (int pass = 0; pass < 2; pass++) {
		aos_dcmd *c = aos_draw(aos_texf(fmt, f), bank_of_team(e->team), e->pos[0], e->pos[1], 0);
		c->halfW = c->halfH = 48.0f;
		c->sx = e->scale[0];
		c->sy = e->scale[1];
		c->rgba = aos_rgba(1, 1, 1, 0.5f); /* +0xdc (a=0.5, 1, 1, 1) */
		aimx(c, e->angle);
		c->blend = pass == 0 ? AOS_BLEND_ALPHA : AOS_BLEND_ADD_ALPHA;
	}
}

static bool enemy_on_killed(aos_world *w, aos_enemy *e) /* FUN_00521780: returns 0 */
{
	(void)w, (void)e;
	return false;
}

/* ------------------------------------------------------------------ */
/* Kae effects (type 14): spawn FUN_004f9cb0, update FUN_004fb350, draw FUN_004fa1f0 */

static int effect_spawn(aos_world *w, int32_t id, int32_t variant, const float pos[3], float angle, int32_t owner,
                        int32_t age0, int32_t team)
{
	int layer;
	switch (id) {
	case 300: case 0x96: case 600: layer = 1; break;
	case 100: case 200: case 0xfa: case 500: case 0x1fe: case 0x208: layer = 0; break;
	default: return -1; /* original returns 0 without pushing */
	}
	int i = aos_effect_new(w, ID, id, variant, pos, angle, owner, age0, team);
	if (i < 0)
		return -1;
	aos_effect *e = &w->obj.effect[i];
	e->drawLayer = layer;
	e->f24 = 1;
	e->scale = 1.0f;
	e->alpha = 1.0f;
	e->moveDir = angle;
	e->rotation = angle;
	e->aux = 0;
	return i;
}

static bool effect_update(aos_world *w, aos_effect *e)
{
	switch (e->id) {
	case 300:
		e->scale += 0.075f; /* DAT_00813efc */
		e->alpha -= 0.05f;  /* DAT_00813ef4 */
		e->rotation += 0.125f; /* DAT_00813f1c */
		return 0.0f < e->alpha;
	case 100:
		e->alpha -= 0.075f;
		return 0.0f < e->alpha;
	case 0x96:
		e->alpha -= 0.05f;
		return 0.0f < e->alpha;
	case 200:
	case 0xfa:
		e->alpha -= 0.1f;
		return 0.0f < e->alpha;
	case 500: { /* fire field: forces Blazing (status 7) on the opponent within 100 */
		if (0x1e0 < e->age)
			return false;
		float pos[3] = {e->pos[0], e->pos[1], e->pos[2]};
		aos_player *o = team_opponent(w, e->team);
		if (o->statusOn[AOS_STATUS_OVERHEAT] == 0) {
			float dx = o->x - e->pos[0], dy = o->y - e->pos[1]; /* FUN_005f1c30 */
			if (aos_sqrt(dx * dx + dy * dy) < 100.0f)
				aos_status(w, o, AOS_STATUS_OVERHEAT, 500);
		}
		if (e->age % 10 == 0) {
			float r = aos_rand_real(w, -AOS_PI, AOS_PI);
			fxk(w, 0x1fe, 0, pos, r, e->ownerId, 0, e->team);
		}
		if (e->age % 2 == 0) {
			float a = aos_rand_real(w, -AOS_PI, AOS_PI);
			float rr = aos_rand_real(w, 10.0f, 80.0f); /* DAT_00813f4c, DAT_00814164 */
			float v[3] = {aos_cos(a) * rr + e->pos[0], aos_sin(a) * rr + e->pos[1], 0};
			fx0(w, 3000, 0, v, HALF_PI, e->ownerId, 0, e->team);
		}
		return true;
	}
	case 0x1fe:
	case 0x208:
		e->alpha -= 0.02f; /* DAT_00813ed8 */
		e->scale += 0.1f;
		return 0.0f < e->alpha;
	case 600:
		e->scale += 0.1f;
		e->alpha -= 0.1f;
		return 0.0f < e->alpha;
	default:
		return true;
	}
}

static aos_dcmd *blit(int tex, int bank, const aos_effect *e, float hw, float hh, float alpha, int blend)
{
	aos_dcmd *c = aos_draw(tex, bank, e->pos[0], e->pos[1], 0);
	c->halfW = hw;
	c->halfH = hh;
	c->rotZ = aos_rad2bam(e->rotation); /* -(+0x38) in screen space = +0x38 in world */
	c->rgba = aos_rgba(1, 1, 1, (float)(uint32_t)(alpha) / 255.0f);
	c->blend = (uint8_t)blend;
	return c;
}

static void effect_draw(aos_world *w, aos_effect *e)
{
	(void)w;
	int bank = bank_of_team(e->team);
	float s = e->scale;
	switch (e->id) {
	case 300: {
		int f = e->age / 4;
		if (3 < f)
			f = 3;
		blit(aos_texf("ke_blade%02d", f), bank, e, 72.0f * s, 72.0f * s, 255.0f * e->alpha, AOS_BLEND_ALPHA);
		break;
	}
	case 100:
	case 0x96: { /* common atlas "beamemitb", src cell from the variant (team colour) */
		int v = e->variant;
		float x0 = (float)((v / 8) * 0xf0), y0 = (float)((v % 8) << 6);
		aos_dcmd *c;
		if (e->id == 100) {
			c = blit(aos_tex("beamemitb"), AOS_BANK_COMMON, e, 32.0f * s, 20.0f * s, 64.0f * e->alpha, AOS_BLEND_ADD_ALPHA);
			c->hasSrc = 1;
			c->src[0] = x0, c->src[1] = y0, c->src[2] = x0 + 0x77, c->src[3] = y0 + 0x3f;
		} else {
			c = blit(aos_tex("beamemitb"), AOS_BANK_COMMON, e, 48.0f * s, 32.0f * s, 64.0f * e->alpha, AOS_BLEND_ADD_ALPHA);
			c->hasSrc = 1;
			c->src[0] = x0, c->src[1] = y0, c->src[2] = x0 + 0x77, c->src[3] = y0 + 0x3f;
			float g = (float)(uint32_t)(128.0f * e->alpha) / 255.0f; /* grey = alpha byte */
			c = blit(aos_tex("beamemitb"), AOS_BANK_COMMON, e, 32.0f * s, 20.0f * s * e->alpha, 128.0f * e->alpha, AOS_BLEND_ADD_ALPHA);
			c->rgba = aos_rgba(g, g, g, g);
			c->hasSrc = 1;
			c->src[0] = x0 + 0x78, c->src[1] = y0, c->src[2] = x0 + 0xef, c->src[3] = y0 + 0x3f;
		}
		break;
	}
	case 200:
		blit(aos_tex("ke_moon01"), bank, e, 72.0f * s, 72.0f * s, 144.0f * e->alpha, AOS_BLEND_ADD_ALPHA);
		break;
	case 0xfa:
		blit(aos_tex("ke_moon01"), bank, e, 72.0f * s, 36.0f * s, 144.0f * e->alpha, AOS_BLEND_ADD_ALPHA);
		break;
	case 0x1fe:
		blit(aos_tex("ke_field"), bank, e, 48.0f * s, 48.0f * s, 128.0f * e->alpha, AOS_BLEND_ALPHA);
		blit(aos_tex("ke_field"), bank, e, 48.0f * s, 48.0f * s, 128.0f * e->alpha, AOS_BLEND_ADD_ALPHA);
		break;
	case 0x208:
		blit(aos_tex("ke_field"), bank, e, 96.0f * s, 28.0f * s, 96.0f * e->alpha, AOS_BLEND_ALPHA);
		break;
	case 600:
		blit(aos_tex("ke_cone00"), bank, e, 96.0f * s, 96.0f * s, 255.0f * e->alpha, AOS_BLEND_ADD_ALPHA);
		break;
	default:
		break; /* 500: not drawn */
	}
}

/* ------------------------------------------------------------------ */

void aos_register_kae(void)
{
	aos_tab.inputA[ID] = input_a;
	aos_tab.inputB[ID] = input_b;
	/* CPU AI FUN_006e1970 (level 0) / FUN_006e0ef0 (1, 2): not ported */

	static const struct { int16_t s; aos_update_fn t2; aos_draw_fn t1; } st[] = {
		{0x1aa, st_neutral, dr_neutral},     {0x1ab, st_dash_hook, dr_dash},
		{0x1ac, st_stub, dr_ko},             {0x1ad, st_stub, dr_hit},
		{0x1ae, st_stub, dr_sld},            {0x1b4, st_slash, dr_atka},
		{0x1b5, st_slaslash, dr_atka},       {0x1b6, st_btap, dr_atkb},
		{0x1b7, st_pow, dr_atkb},            {0x1b8, st_pow_hyper, dr_atkb},
		{0x1b9, st_blazing, dr_sld},         {0x1ba, st_unused_1ba, dr_sld0},
		{0x1bb, st_fooom, dr_sld},           {0x1bc, st_zoom1, dr_infa},
		{0x1bd, st_zoom2, dr_infb},          {0x1be, st_zoom3, dr_infc},
		{0x1bf, st_zoom4, dr_infd},          {0x1c0, st_baaang, dr_sld},
		{0x1c1, st_babang, dr_sld},          {0x1c2, st_babang_slash, dr_infa},
		{0x1c3, st_zoooom, dr_hyp},          {0x1c4, st_whaaam, dr_infb5},
		{0x1c5, st_whaaam_hold, dr_whaaam_hold}, {0x1c6, st_whaaaam, dr_infb5},
		{0x1c7, st_whaaaam_hold, dr_whaaaam_hold},
	};
	for (size_t i = 0; i < sizeof st / sizeof st[0]; i++) {
		aos_tab.T2[st[i].s] = st[i].t2;
		aos_tab.T1[st[i].s] = st[i].t1;
	}

	aos_otab.beamSpawn[ID] = beam_spawn;
	aos_otab.beamUpdate[ID] = beam_update;
	aos_otab.beamDraw[ID] = beam_draw;
	aos_otab.beamOnHit[ID] = beam_on_hit;
	aos_otab.missileSpawn[ID] = missile_spawn;
	aos_otab.missileUpdate[ID] = missile_update;
	aos_otab.missileDraw[ID] = missile_draw;
	aos_otab.missileOnHit[ID] = missile_on_hit;
	aos_otab.missileOnKilled[ID] = missile_on_killed;
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
