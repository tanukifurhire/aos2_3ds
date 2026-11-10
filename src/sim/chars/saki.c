/* Saki (engine id 11 = AOS_CH_SAKI) — 1:1 port of her code in aos2.exe.
 * Spec: docs/re/saki.md. Every function names its original address; the
 * decompiled code (tools/re/dec/all.c) is the source of truth. Only the
 * version-0x5a2 branches of DAT_0089426c are implemented.
 * Layout follows suguri.c: 1. helpers 2. input 3. T2 states 4. T1 draws
 * 5. objects (beam, missile, hitbox, effect) 6. registration.
 * Saki keeps no per-player or global extra state (p->ext unused). */
#include <stddef.h>

#include "../draw.h"
#include "../fx.h"
#include "../sim.h"

#define ID AOS_CH_SAKI
#define HALF_PI 1.5707964f /* DAT_00814104 */

void aos_se_cd(aos_world *w, int id, int cd); /* core.c, FUN_007bcaf0 */

/* ================================================================== */
/* 1. helpers                                                          */

/* FUN_00780450(team): the player that owns a team. */
static aos_player *team_player(aos_world *w, int32_t team) { return &w->p[team == 2 ? 1 : 0]; }
/* FUN_00780480(team): the opponent of a team. */
static aos_player *team_opp(aos_world *w, int32_t team) { return &w->p[team == 2 ? 0 : 1]; }
/* Character art bank of a team (DAT_00885a00[team*0x10]); port bank = player index. */
static int team_bank(int32_t team) { return team == 2 ? AOS_BANK_P1 : AOS_BANK_P0; }

/* Common effect (*DAT_00881698)(id, variant, pos, angle, owner, age0, team). */
static void fx0(aos_world *w, int32_t id, int32_t variant, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team)
{
	aos_spawn_effect(w, 0, id, variant, pos, angle, owner, age0, team);
}

/* Saki effect (*DAT_008816c4)(...): returns the record (original returns a pointer). */
static aos_effect *fx11(aos_world *w, int32_t id, int32_t variant, const float pos[3], float angle, int32_t owner, int32_t team)
{
	int i = aos_spawn_effect(w, ID, id, variant, pos, angle, owner, 0, team);
	return i >= 0 ? &w->obj.effect[i] : NULL;
}

/* Dash sparks: r = rand(lo,hi); k = randint(0,100)%8; common effect base+k, colour 0xd,
 * at pos, angle dir + r (disassembly 0x7216c1..0x721752). */
static void sparks(aos_world *w, aos_player *p, const float pos[3], int base, float lo, float hi, float dir)
{
	float r = aos_rand_real(w, lo, hi);
	int32_t k = aos_rand_int(w, 0, 100) % 8;
	fx0(w, base + k, 0xd, pos, dir + r, p->objId, 0, p->side);
}

/* "Face": +0x84 = +0x88 = π if opponent.x <= x, else 0. */
static void face(aos_world *w, aos_player *p)
{
	aos_player *o = aos_opponent(w, p);
	p->faceAngle = (o->x <= p->x) ? AOS_PI : 0.0f;
	p->dashAngle = p->faceAngle;
}

/* Tail shared by every state: dash-cancel from `cancel`, end after `end`; `fric` halves friction. */
static void tail(aos_world *w, aos_player *p, int cancel, int end, bool fric)
{
	if (p->timer < cancel || p->D != 3) {
		if (p->timer > end)
			p->state = AOS_ST_NEUTRAL;
		if (fric)
			p->friction *= 0.5f;
	} else {
		aos_dash(w, p, 1);
	}
}

/* FUN_006b1d70(src, range, speed): push src's opponent into hit state if close. */
static void knock_near(aos_world *w, aos_player *src, float range, float speed)
{
	aos_player *o = aos_opponent(w, src);
	if (o->state == AOS_ST_KO || o->hittable != 1 || o->statusOn[AOS_STATUS_STUNIMMUNE] != 0)
		return;
	float dx = src->x - o->x, dy = src->y - o->y;
	if (!(aos_sqrt(dx * dx + dy * dy) < range)) /* FUN_005f1c30 */
		return;
	o->state = AOS_ST_HITSTUN;
	o->timer = 0;
	o->faceAngle = aos_atan2(src->y - o->y, src->x - o->x);
	o->moveAngle = o->faceAngle + AOS_PI;
	o->speed = speed;
	aos_se(w, 0x19);
	aos_camera_focus_clear(w); /* FUN_004a9f10 on the camera focus list */
}

/* FUN_00780a50: may the opponent be pulled/affected. */
static bool affectable(aos_world *w, const aos_player *o)
{
	return o->barrierTime == 0 && o->state != AOS_ST_KO && o->hittable != 0 &&
	       o->statusOn[AOS_STATUS_STUNIMMUNE] != 1 && !w->roundOver;
}

static void normal_hyper_start(aos_world *w, aos_player *p) /* t=0 of 0x165..0x168 */
{
	face(w, p);
	aos_barrier(w, p, 10);
	/* SE 6, FUN_004e9280(100,.75), FUN_007bec20(100,pos,0,0), FUN_004d7ac0(20,.5,2), effect 0xfa */
	aos_hyper_flourish_ex(w, p, 6, 100, 0.75f /* DAT_00813f3c */, 100, 0.0f, 0.0f, 0x14, 0.5f /* DAT_00853b1c */, 2, 1);
}

/* Sparkle used by 0x167/0x169 (dead offset computation still draws RNG). */
static void sparkle(aos_world *w, aos_player *p, int32_t id)
{
	float v[2];
	v[0] = aos_rand_real(w, 300.0f, 1000.0f); /* DAT_008145f0, DAT_00814408 */
	v[1] = 0.0f;
	aos_vec_rotate(v, aos_rand_real(w, -AOS_PI, AOS_PI));
	float ang = aos_rand_real(w, -AOS_PI, AOS_PI);
	aos_effect *e = fx11(w, id, 0, &p->x, ang, p->objId, p->side);
	float aux = id == 0xfa ? aos_rand_real(w, 15.0f, 45.0f)  /* DAT_00853b88, DAT_0081ce48 */
	                       : aos_rand_real(w, 10.0f, 30.0f); /* DAT_00813f4c, DAT_008140e8 */
	if (e)
		e->aux = aux;
}

/* ================================================================== */
/* 2. input dispatch                                                   */

static bool st(aos_world *w, aos_player *p, int32_t s)
{
	aos_set_state(w, p, s);
	return true;
}

static bool normal_moves(aos_world *w, aos_player *p)
{
	if (p->state == AOS_ST_NEUTRAL || p->state == AOS_ST_DASH) {
		if (p->D == 3) {
			aos_dash(w, p, 0);
		} else if (p->Shield == 3 && aos_spend_hyper(p, 0.5f) && aos_may_shield(w, p)) {
			aos_shield(w, p);
		} else {
			if (p->S > 1 && p->B == 3) return st(w, p, 0x161);
			if (p->S > 1 && p->D > 1 && p->A == 3) return st(w, p, 0x164);
			if (p->S > 1 && p->A == 3) return st(w, p, 0x162);
			if (p->B == 3) return st(w, p, 0x15f);
			if (p->D > 1 && p->A == 3) return st(w, p, 0x15e);
			if (p->A == 3) return st(w, p, 0x15d);
		}
	}
	return aos_tab.T2[p->state] ? aos_tab.T2[p->state](w, p) : true;
}

static bool input_a(aos_world *w, aos_player *p) /* FUN_00722ce0 */
{
	if (aos_may_hyper(w, p)) {
		if (p->S > 1 && p->B > 1 && p->H == 3 && aos_spend_hyper(p, 3.0f)) return st(w, p, 0x169);
		if (p->S > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f)) return st(w, p, 0x168);
		if (p->B > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f)) return st(w, p, 0x167);
		if (p->D > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f)) return st(w, p, 0x166);
		if (p->H == 3 && aos_spend_hyper(p, 1.0f)) return st(w, p, 0x165);
	}
	return normal_moves(w, p);
}

static bool input_b(aos_world *w, aos_player *p) /* FUN_00723200 */
{
	if (aos_may_hyper(w, p)) {
		if (p->H > 1 && p->Shield == 3 && aos_spend_hyper(p, 3.0f)) return st(w, p, 0x169);
		if (p->H > 1 && p->S == 3 && aos_spend_hyper(p, 1.0f)) return st(w, p, 0x168);
		if (p->H > 1 && p->B == 3 && aos_spend_hyper(p, 1.0f)) return st(w, p, 0x167);
		if (p->H > 1 && p->D == 3 && aos_spend_hyper(p, 1.0f)) return st(w, p, 0x166);
		if (p->H > 1 && p->A == 3 && aos_spend_hyper(p, 1.0f)) return st(w, p, 0x165);
	}
	return normal_moves(w, p);
}

/* ================================================================== */
/* 3. T2 states                                                        */

static bool st_neutral(aos_world *w, aos_player *p) /* 0x153 T2 FUN_0071fd20 */
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

static bool st_stub(aos_world *w, aos_player *p) /* 0x154..0x157 FUN_0071fe10/40/20/30 */
{
	(void)w;
	(void)p;
	return true;
}

/* 36 cymbals, i = −18..17 (FUN_0071fe50 / FUN_00720890). */
static void cymbal_ring(aos_world *w, aos_player *p, float base, float sub, int32_t age0)
{
	for (int i = -18; i < 18; i++) {
		float a = ((float)i / 18.0f) * AOS_PI + base - sub; /* DAT_00814144 */
		aos_spawn_beam(w, ID, i % 2 == 0 ? 300 : 0x136, &p->x, a, p->objId, age0, p->side);
	}
}

static bool st_tornado(aos_world *w, aos_player *p) /* 0x15d T2 FUN_0071fe50 */
{
	if (p->timer == 0)
		face(w, p);
	if (p->timer == 14 && aos_use_gauge(p, 0x15d)) {
		p->dashAngle = aos_aim(w, p);
		cymbal_ring(w, p, p->dashAngle, 0.0f, 0);
		aos_se(w, 0x10);
	}
	tail(w, p, 20, 30, true);
	return true;
}

static bool st_stardust(aos_world *w, aos_player *p) /* 0x15e T2 FUN_00720090 */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer > 9 && p->timer < 14 && aos_use_gauge(p, 0x15e)) {
		float a = aos_rand_real(w, -AOS_PI, AOS_PI);
		aos_spawn_beam(w, ID, p->timer % 2 == 0 ? 200 : 0xd2, &p->x, a, p->objId, 0, p->side);
		aos_se_cd(w, 0xf, 2);
	}
	tail(w, p, 16, 30, true);
	return true;
}

static bool st_grenade(aos_world *w, aos_player *p) /* 0x15f T2 FUN_00720270 */
{
	if (p->timer == 0 || p->timer == 14) {
		face(w, p);
		if (p->up > 1)
			aos_angle_toward(&p->dashAngle, HALF_PI, 0.4f);   /* DAT_00813f30 */
		if (p->down > 1)
			aos_angle_toward(&p->dashAngle, -HALF_PI, 1.2f);  /* DAT_00817d78 (asymmetric, literal) */
	}
	if (p->timer < 13 && p->B < 1) {
		p->faceAngle = aos_aim(w, p);
		p->state = 0x160; /* timer kept */
		aos_tab.T2[0x160](w, p);
		return true;
	}
	if (p->timer > 13 && p->timer < 22 && aos_use_gauge(p, 0x15f)) {
		aos_player *o = aos_opponent(w, p);
		float r = aos_rand_real(w, 0.5235988f, 1.0471976f); /* DAT_00817ddc, DAT_008203bc */
		float a = (o->x <= p->x) ? -r + p->dashAngle : r + p->dashAngle;
		aos_spawn_missile(w, ID, 100, &p->x, a, p->objId, 0, p->side, p->color);
		aos_se_cd(w, 0x2f, 3);
	}
	tail(w, p, 25, 50, false);
	return true;
}

static bool st_boomerang(aos_world *w, aos_player *p) /* 0x160 T2 FUN_00720590 */
{
	if (p->timer == 12 && aos_use_gauge(p, 0x160)) {
		aos_spawn_missile(w, ID, 200, &p->x, p->faceAngle, p->objId, 0, p->side, p->color);
		aos_se(w, 0x31);
	}
	tail(w, p, 25, 40, false);
	return true;
}

static bool st_blade(aos_world *w, aos_player *p) /* 0x161 T2 FUN_00720680 */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer == 4) {
		if (aos_fabs(p->faceAngle) <= HALF_PI)
			aos_spawn_missile(w, ID, 0x136, &p->x, p->faceAngle - HALF_PI, p->objId, 0, p->side, p->color);
		else
			aos_spawn_missile(w, ID, 300, &p->x, p->faceAngle + HALF_PI, p->objId, 0, p->side, p->color);
		aos_se(w, 0x31);
	}
	if (p->timer >= 10 && p->hitFlag == 1 && p->A == 3)
		return st(w, p, 0x162);
	tail(w, p, 25, 40, true);
	return true;
}

/* Lunge of 0x162/0x163: speed = min(dist/14, 10). */
static void lunge(aos_world *w, aos_player *p)
{
	p->faceAngle = aos_aim(w, p);
	float s = aos_dist(w, p) / 14.0f; /* DAT_00813a54 */
	if (10.0f <= s)                   /* DAT_00813f4c */
		s = 10.0f;
	p->dashAngle = p->faceAngle;
	p->moveAngle = p->faceAngle;
	p->speed = s;
}

static void ahead(const aos_player *p, float ang, float d, float out[3])
{
	out[0] = aos_cos(ang) * d + p->x;
	out[1] = aos_sin(ang) * d + p->y;
	out[2] = p->z;
}

static bool st_hammer(aos_world *w, aos_player *p) /* 0x162 T2 FUN_00721a80 */
{
	if (p->timer == 0)
		lunge(w, p);
	if (p->timer == 8)
		aos_se(w, 0x1f);
	if (p->timer == 10 && aos_use_gauge(p, 0x162)) {
		float pt[3];
		ahead(p, p->faceAngle, 30.0f, pt); /* DAT_008140e8 */
		aos_spawn_hitbox(w, ID, 300, pt, p->faceAngle, p->objId, 0, p->side);
	}
	tail(w, p, 35, 50, false);
	return true;
}

static bool st_unused_lunge(aos_world *w, aos_player *p) /* 0x163 T2 FUN_00721c90 (unreachable) */
{
	if (p->timer == 0)
		lunge(w, p);
	if (p->timer == 8)
		aos_se(w, 0x1f);
	tail(w, p, 35, 50, true);
	return true;
}

static bool st_swing(aos_world *w, aos_player *p) /* 0x164 T2 FUN_00721dc0 */
{
	int t = p->timer;
	float pt[3];
	if (t == 0) {
		p->faceAngle = aos_aim(w, p);
		p->moveAngle = p->faceAngle;
		p->speed = 0;
	}
	if (t == 10 && aos_use_gauge(p, 0x163)) {
		p->faceAngle = aos_aim(w, p);
		p->dashAngle = p->faceAngle;
		p->moveAngle = p->faceAngle;
		p->speed = 12.0f; /* DAT_00813f54 */
		aos_se(w, 0x20);
	}
	if (5.0f <= p->speed) /* DAT_00813f40 */
		sparks(w, p, &p->x, 0xa28, -0.15f, 0.15f, p->faceAngle + HALF_PI);
	if (t == 12 || t == 18 || t == 24) {
		ahead(p, p->faceAngle, 30.0f, pt);
		aos_spawn_hitbox(w, ID, 400, pt, p->faceAngle, p->objId, 0, p->side);
	}
	if (t == 15 || t == 21) {
		ahead(p, p->faceAngle, -30.0f, pt); /* −cos·30 + x */
		aos_spawn_hitbox(w, ID, 400, pt, p->faceAngle, p->objId, 0, p->side);
	}
	if (t == 27) {
		ahead(p, p->faceAngle, -30.0f, pt);
		aos_spawn_hitbox(w, ID, 0x19a, pt, p->faceAngle + AOS_PI, p->objId, 0, p->side);
	}
	tail(w, p, 40, 50, true);
	return true;
}

static bool st_cyclone(aos_world *w, aos_player *p) /* 0x165 T2 FUN_00720890 */
{
	if (p->timer == 0)
		normal_hyper_start(w, p);
	for (int k = 0; k < 5; k++) {
		if (p->timer == k * 2 + 16) {
			p->dashAngle = aos_aim(w, p);
			cymbal_ring(w, p, p->dashAngle, (float)k * 0.1f, k * -4); /* DAT_00853b10 */
			if (k % 2 == 0)
				aos_se(w, 0x10);
		}
	}
	tail(w, p, 30, 40, false);
	return true;
}

static bool st_galaxy(aos_world *w, aos_player *p) /* 0x166 T2 FUN_00720ba0 */
{
	if (p->timer == 0)
		normal_hyper_start(w, p);
	if (p->timer > 7 && p->timer < 41 && p->timer % 2 == 0) {
		float a = aos_rand_real(w, -AOS_PI, AOS_PI);
		int i = aos_spawn_beam(w, ID, p->timer % 4 == 0 ? 200 : 0xd2, &p->x, a, p->objId, 0, p->side);
		int32_t age = aos_rand_int(w, 0, 0x96);
		if (i >= 0) {
			w->obj.beam[i].age = age;
			w->obj.beam[i].damage = 0x28;
		}
		aos_se_cd(w, 0xf, 4);
	}
	tail(w, p, 40, 60, false);
	return true;
}

static bool st_sunshine(aos_world *w, aos_player *p) /* 0x167 T2 FUN_00720ea0 */
{
	int t = p->timer;
	if (t == 0)
		normal_hyper_start(w, p);
	if (t == 1)
		aos_se(w, 0x30);
	if (t > 9 && t < 81) {
		float b[3] = {p->x, (float)t * 2.5f + p->y, p->z}; /* DAT_008153e0; z UNVERIFIED (stack garbage in original) */
		if (p->H > 1 || t < 30) {
			aos_effect *e = fx11(w, 200, 0, b, (float)t * 0.1f, p->objId, p->side);
			float sc = t < 30 ? (float)(t - 10) / 20.0f : (float)t / 30.0f; /* DAT_008140fc, DAT_008140e8 */
			if (e)
				e->scale = sc;
			if (t % 10 == 0 || t % 10 == 5) {
				int h = aos_spawn_hitbox(w, ID, t % 10 == 0 ? 200 : 0xd2, b, p->faceAngle, p->objId, 0, p->side);
				if (h >= 0)
					w->obj.hitbox[h].shape.radius = 64.0f * sc; /* DAT_008140f8 */
			}
		}
		if ((t > 29 && p->H < 2) || (t > 29 && p->hyper <= 0.0f) || t == 80) {
			int i = aos_spawn_beam(w, ID, 100, b, p->faceAngle, p->objId, 0, p->side);
			if (i >= 0) {
				aos_beam *bm = &w->obj.beam[i];
				bm->angle = (float)t * 0.1f;
				bm->scale = (float)t / 30.0f;
				bm->f6c = (float)t / 30.0f;
				bm->speed = 6.0f - (float)(t - 30) / 10.0f; /* DAT_00813f44, DAT_00813f4c */
				aos_player *o = aos_opponent(w, p);
				bm->moveAngle = aos_atan2(o->y - bm->pos[1], o->x - bm->pos[0]);
			}
			p->timer = t = 80;
			aos_se(w, 0x20);
		}
		if (t % 4 == 0)
			sparkle(w, p, 0xfa);
		else if (t % 2 == 0)
			sparkle(w, p, 0x104);
		if (t % 2 == 0) {
			float sp[3] = {p->x, p->y - 30.0f, p->z};
			sparks(w, p, sp, 0xa32, -0.15f, 0.15f, p->faceAngle + HALF_PI);
		}
	}
	tail(w, p, 85, 100, false);
	return true;
}

static bool st_rolling(aos_world *w, aos_player *p) /* 0x168 T2 FUN_007217c0 */
{
	if (p->timer == 0) {
		normal_hyper_start(w, p);
		p->faceAngle = aos_aim(w, p);
	}
	if (p->timer == 10) {
		for (int i = -5; i < 5; i++) {
			float f = ((float)i / 5.0f) * AOS_PI; /* DAT_00813f40 */
			if (i % 2 == 0)
				aos_spawn_missile(w, ID, 0x140, &p->x, f + p->faceAngle, p->objId, 0, p->side, p->color);
			else
				aos_spawn_missile(w, ID, 0x14a, &p->x, p->faceAngle - f, p->objId, 0, p->side, p->color);
		}
		aos_se(w, 0x31);
	}
	tail(w, p, 40, 60, false);
	return true;
}

static bool st_bigbang(aos_world *w, aos_player *p) /* 0x169 T2 FUN_00722270 */
{
	int t = p->timer;
	if (t == 0) {
		face(w, p);
		aos_barrier(w, p, 0x14);
		/* SE 7, FUN_004e9280(100,.85), FUN_007bec20(100,pos,0,0), FUN_004d7ac0(100,.6,2), effect 0xfa */
		aos_hyper_flourish_ex(w, p, 7, 100, 0.85f /* DAT_00814120 */, 100, 0.0f, 0.0f, 100, 0.6f /* DAT_00813f34 */, 2, 1);
		aos_se(w, 0x30);
	}
	if (t >= 0 && t <= 60) {
		float b[3] = {p->x, (float)t * 3.5f + p->y, p->z}; /* DAT_008178a0 */
		if (t == 60) {
			for (int k = 0; k < 6; k++) {
				float a = ((float)k / 3.0f) * AOS_PI; /* DAT_00813a50, DAT_00853b58 */
				aos_spawn_particle(w, 1000, aos_team_colour(w, p->side, 0), b, a, p->objId, 0, 4);
				aos_spawn_particle(w, 1000, aos_team_colour(w, p->side, 1), b, a, p->objId, 0, 4);
			}
			aos_player *o = aos_opponent(w, p);
			float a = aos_atan2(o->y - b[1], o->x - b[0]);
			fx11(w, 300, 0, b, 0.0f, p->objId, p->side);
			fx0(w, 0xa8c, 0, b, 0.0f, p->objId, 0, p->side);
			aos_spawn_hitbox(w, ID, 0x122, b, a, p->objId, 0, p->side);
			aos_se(w, 0x27);
		}
		aos_effect *e = fx11(w, 200, 0, b, (float)t * 0.1f, p->objId, p->side);
		if (e)
			e->scale = (float)(t - 10) / 20.0f;
		for (int k = 0; k < 4; k++) {
			float base = ((float)k / 4.0f) * AOS_2PI;
			if (aos_rand_int(w, 0, 100) < 0x32)
				fx11(w, 0x172, aos_team_colour(w, p->side, 0), b, aos_rand_real(w, 0.0f, HALF_PI) + base, p->objId, p->side);
			else
				fx11(w, 0x173, aos_team_colour(w, p->side, 1), b, aos_rand_real(w, 0.0f, HALF_PI) + base, p->objId, p->side);
		}
		aos_player *o = aos_opponent(w, p);
		if (affectable(w, o)) {
			float a = aos_atan2(o->y - b[1], o->x - b[0]);
			o->x -= aos_cos(a) * 6.0f; /* DAT_00813f44 */
			o->y -= aos_sin(a) * 6.0f;
		}
		if (t < 0x29) {
			if (t % 4 == 0)
				sparkle(w, p, 0xfa);
			else if (t % 2 == 0)
				sparkle(w, p, 0x104);
		}
		if (t % 2 == 0) {
			float sp[3] = {p->x, p->y - 30.0f, p->z};
			if (t < 0x2e)
				sparks(w, p, sp, 0xa32, -0.15f, 0.15f, p->faceAngle + HALF_PI);
			else
				sparks(w, p, sp, 0xa82, -0.25f, 0.25f, p->faceAngle + HALF_PI);
		}
	}
	tail(w, p, 85, 100, false);
	return true;
}

/* ================================================================== */
/* 4. T1 draw handlers (sprite table saki.md §4.1)                     */

static int imin(int a, int b) { return a < b ? a : b; }
static int pingpong(int t, int n) { int f = (t / n) & 3; return f == 3 ? 1 : f; }
static int ramp(int t, int s, int cap) { return t < s ? 0 : imin((t - s) / 4 + 1, cap); }

/* AIM transform: rotZ(aim) [+ rotX(180°) when |aim| ≥ π/2] + rotY(180°); as suguri.c. */
static aos_dcmd *aim_quad(aos_world *w, aos_player *p, const char *pat, int frame)
{
	aos_dcmd *c = aos_draw(aos_texf(pat, frame), aos_pidx(w, p), p->x, p->y, 0);
	c->halfW = c->halfH = 48.0f; /* 96×96 quad */
	c->flipX = 1;
	c->rotZ = aos_rad2bam(p->faceAngle);
	if (aos_fabs(p->faceAngle) >= HALF_PI)
		c->sy = -1.0f; /* rotX(180°): v' = Rz·Rx·Ry·v — the only order that points the art along the aim */
	c->blend = AOS_BLEND_ALPHA;
	/* FUN_004e8f50 global tint: not modelled */
	return c;
}

/* FUN_004d7e00 after an AIM body draw: rot {RotX, RotY, RotZ} in bam. */
static void ghost_aim(aos_world *w, aos_player *p, const aos_dcmd *c)
{
	int32_t rot[3] = {aos_fabs(p->faceAngle) >= HALF_PI ? aos_deg2bam(0xb4) : 0, aos_deg2bam(0xb4),
	                  aos_rad2bam(p->faceAngle)};
	aos_afterimage(w, c, 1, p, rot);
}

static void dr_neutral(aos_world *w, aos_player *p) /* 0x153 T1 FUN_00724710 */
{
	const char *pat = "sk_std%02d";
	if (p->up > 1) pat = "sk_fwd%02d";
	if (p->down > 1) pat = "sk_bck%02d";
	if (!p->facingRight && p->left > 1) pat = "sk_fwd%02d";
	if (p->facingRight && p->left > 1) pat = "sk_bck%02d";
	if (!p->facingRight && p->right > 1) pat = "sk_bck%02d";
	if (p->facingRight && p->right > 1) pat = "sk_fwd%02d";
	aos_dcmd *c = aos_draw(aos_texf(pat, pingpong(p->timer, 5)), aos_pidx(w, p), p->x, p->y, 0);
	c->halfW = c->halfH = 48.0f;
	c->flipX = p->facingRight != 0; /* RotY(π) when +0x8c set (as suguri.c) */
	int32_t rot[3] = {0, p->facingRight ? aos_deg2bam(0xb4) : 0, 0};
	aos_afterimage(w, c, 1, p, rot); /* FUN_004d7e00 */
}

static void dr_dash(aos_world *w, aos_player *p) /* 0x154 T1 FUN_007249d0 (+afterimage) */
{
	bool against = (p->facingRight == 1 && aos_fabs(p->faceAngle) > HALF_PI) ||
	               (p->facingRight == 0 && aos_fabs(p->faceAngle) < HALF_PI);
	ghost_aim(w, p, aim_quad(w, p, "sk_dsh%02d", pingpong(p->timer, 3) + (against ? 3 : 0)));
}

static void dr_ko(aos_world *w, aos_player *p) /* 0x155 T1 FUN_00725360 */
{
	if (p->timer < 100)
		aim_quad(w, p, "sk_los%02d", pingpong(p->timer, 3) + (w->frame % 2 == 0 ? 3 : 0));
}

static void dr_hit(aos_world *w, aos_player *p) /* 0x156 T1 FUN_00724dd0 */
{
	aim_quad(w, p, "sk_los%02d", pingpong(p->timer, 3));
}

static void dr_shield(aos_world *w, aos_player *p) /* 0x157 T1 FUN_007250a0 */
{
	aim_quad(w, p, "sk_sld%02d", imin(p->timer / 5, 3));
}

static void dr_tornado(aos_world *w, aos_player *p) { aim_quad(w, p, "sk_sld%02d", imin(p->timer / 4, 3)); } /* 0x15d FUN_00725660 */
static void dr_stardust(aos_world *w, aos_player *p) { aim_quad(w, p, "sk_thrw%02d", ramp(p->timer, 8, 5)); } /* 0x15e FUN_00725920, 0x160 FUN_00725ee0 */
static void dr_grenade(aos_world *w, aos_player *p) { aim_quad(w, p, "sk_thrw%02d", ramp(p->timer, 10, 5)); } /* 0x15f FUN_00725c00 */
static void dr_sld8(aos_world *w, aos_player *p) { aim_quad(w, p, "sk_sld%02d", ramp(p->timer, 8, 3)); } /* 0x161 FUN_007261c0, 0x166 FUN_00726780 */
static void dr_sld5(aos_world *w, aos_player *p) { aim_quad(w, p, "sk_sld%02d", ramp(p->timer, 5, 3)); } /* 0x165 FUN_007264a0, 0x168 FUN_00726d60 */
static void dr_hammer(aos_world *w, aos_player *p) { ghost_aim(w, p, aim_quad(w, p, "sk_hammer%02d", ramp(p->timer, 8, 3))); } /* 0x162 FUN_00727040 (+afterimage) */
static void dr_lunge(aos_world *w, aos_player *p) { ghost_aim(w, p, aim_quad(w, p, "sk_swing%02d", ramp(p->timer, 5, 3))); } /* 0x163 FUN_007273e0 (+afterimage) */

static void dr_swing(aos_world *w, aos_player *p) /* 0x164 T1 FUN_00727780 (+afterimage) */
{
	int f = 0;
	if (p->timer >= 10) {
		int n = (p->timer - 10) / 3;
		f = n + 1;
		if (3 < f) f = n - 2;
		if (3 < f) f -= 3;
	}
	ghost_aim(w, p, aim_quad(w, p, "sk_swing%02d", imin(f, 3)));
}

static void dr_ball(aos_world *w, aos_player *p) /* 0x167 FUN_00726a60, 0x169 FUN_00727b40 */
{
	int f = p->timer < 0x50 ? pingpong(p->timer, 3) : imin((p->timer - 0x50) / 3 + 3, 7);
	aim_quad(w, p, "sk_ball%02d", f);
}

/* ================================================================== */
/* 5. objects                                                          */

/* 2D sprite blit (vtable +0x68 / +0x60). `angle` is the WORLD angle (screen call passes −angle). */
static aos_dcmd *blit(int tex, int bank, const float pos[3], float hw, float hh, float angle,
                      int alpha, uint32_t rgb, int blend, int x0, int y0, int x1, int y1)
{
	aos_dcmd *c = aos_draw(tex, bank, pos[0], pos[1], 0);
	c->halfW = hw;
	c->halfH = hh;
	c->rotZ = aos_rad2bam(angle);
	c->blend = (uint8_t)blend;
	c->rgba = (rgb & 0xffffffu) << 8 | (uint32_t)(alpha & 0xff);
	c->hasSrc = 1;
	c->src[0] = (float)x0;
	c->src[1] = (float)y0;
	c->src[2] = (float)x1;
	c->src[3] = (float)y1;
	return c;
}

/* CModel quad of an S×S sprite. */
static aos_dcmd *model(int tex, int bank, const float pos[3], float S, float scale, float ang, float a, int blend)
{
	aos_dcmd *c = aos_draw(tex, bank, pos[0], pos[1], 0);
	c->halfW = c->halfH = S * 0.5f * scale;
	c->rotZ = aos_rad2bam(ang);
	c->rgba = aos_rgba(1, 1, 1, a); /* +0xdc(a, r, g, b) */
	c->blend = (uint8_t)blend;
	return c;
}

static void move(float pos[3], float ang, float spd)
{
	pos[0] = aos_cos(ang) * spd + pos[0];
	pos[1] = aos_sin(ang) * spd + pos[1];
}

/* ---- beam family: spawn FUN_004b14c0, update FUN_004b25b0, draw FUN_004b17d0, hit FUN_004b2260 ---- */

static int beam_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team)
{
	if (id != 100 && id != 200 && id != 0xd2 && id != 300 && id != 0x136)
		return -1; /* original pushes nothing for other ids */
	int i = aos_beam_new(w, ID, id, pos, angle, owner, age0, team);
	if (i < 0)
		return -1;
	aos_beam *b = &w->obj.beam[i];
	switch (id) {
	case 100:
		b->speed = 0;
		b->destroyOnHit = 0;
		b->active = 0;
		b->damage = 0;
		b->knockback = 10.0f;
		break;
	case 200:
	case 0xd2:
		b->speed = 5.0f; /* DAT_00813f40 */
		b->destroyOnHit = 1;
		b->active = 0;
		b->stun = 0.4f;  /* DAT_00813f30 */
		b->damage = 100;
		b->shape.radius = 15.0f; /* DAT_00853b88 */
		b->knockback = 10.0f;
		break;
	default: /* 300 / 0x136 */
		b->speed = 5.0f;
		b->f6c = -0.1f;  /* DAT_00814130 */
		b->destroyOnHit = 1;
		b->active = 0;
		b->damage = 0x78;
		b->shape.radius = 10.0f; /* DAT_00813f4c */
		b->knockback = 10.0f;
		break;
	}
	return i;
}

static void hb_radius(aos_world *w, int h, float r)
{
	if (h >= 0)
		w->obj.hitbox[h].shape.radius = r;
}

static bool beam_update(aos_world *w, aos_beam *b)
{
	switch (b->id) {
	case 100: {
		b->angle += 0.1f;
		if (AOS_PI < b->angle)
			b->angle -= AOS_2PI;
		if (b->age < 300) {
			aos_player *o = team_opp(w, b->team);
			float cap = 8.0f - b->f6c * 1.8f; /* DAT_008140d4, DAT_0081413c */
			if (b->speed < cap)
				b->speed += 0.05f;
			aos_angle_toward(&b->moveAngle, aos_atan2(o->y - b->pos[1], o->x - b->pos[0]), 0.03f);
		}
		move(b->pos, b->moveAngle, b->speed);
		if (300 < b->age && !aos_in_arena(b->pos, 500.0f))
			return false;
		int32_t hid = b->age % 0x12 == 0 ? 200 : b->age % 0x12 == 9 ? 0xd2 : 0xdc;
		float r = 64.0f * b->scale;
		hb_radius(w, aos_spawn_hitbox(w, ID, hid, b->pos, b->angle, b->ownerId, 0, b->team), r);
		/* DAT_0087a3a8: colour A/B by age parity (age >= 0: every frame) */
		aos_spawn_particle(w, 1000, aos_team_colour(w, b->team, b->age % 2 == 0 ? 0 : 1), b->pos, b->angle + AOS_PI,
		                   b->ownerId, 0, 1);
		break;
	}
	case 200:
	case 0xd2: {
		int32_t col = aos_team_colour(w, b->team, b->id == 200 ? 0 : 1);
		if (b->age < 200) {
			aos_player *o = team_player(w, b->team);
			aos_angle_toward(&b->moveAngle, aos_atan2(o->y - b->pos[1], o->x - b->pos[0]), 0.08f);
			b->angle = b->moveAngle;
		} else if (b->age < 300) {
			aos_player *o = team_opp(w, b->team);
			aos_angle_toward(&b->moveAngle, aos_atan2(o->y - b->pos[1], o->x - b->pos[0]), 0.04f);
			b->angle = b->moveAngle;
			if (b->speed < 16.0f)
				b->speed += 0.1f;
			b->active = 1;
		}
		if (b->age == 200)
			fx0(w, 0x515, col, b->pos, 0.0f, b->ownerId, 0, b->team);
		move(b->pos, b->moveAngle, b->speed);
		if (b->age % 3 == 0)
			aos_spawn_particle(w, 0x456, col, b->pos, b->angle + AOS_PI, b->ownerId, 0, 1);
		if (300 < b->age && !aos_in_arena(b->pos, 10.0f))
			return false;
		break;
	}
	case 300:
	case 0x136:
		if (b->age == 10)
			b->active = 1;
		if (b->age == 0x32)
			b->f6c = 0.2f; /* DAT_00853b14 */
		if (0x31 < b->age && b->age < 0x12d) {
			if (b->id == 300)
				b->moveAngle += 0.01f;
			else
				b->moveAngle -= 0.01f;
			b->angle = b->moveAngle;
		}
		b->speed += b->f6c;
		move(b->pos, b->moveAngle, b->speed);
		if (0x32 < b->age && !aos_in_arena(b->pos, 10.0f))
			return false;
		break;
	}
	return true;
}

static void beam_on_hit(aos_world *w, aos_beam *b) /* FUN_004b2260 */
{
	if (b->id == 100)
		return;
	int32_t col = aos_team_colour(w, b->team, (b->id == 200 || b->id == 300) ? 0 : 1);
	fx0(w, 1000, col, b->pos, b->angle + AOS_PI, b->ownerId, 0, b->team);
	aos_spawn_spark(w, 1000, col, 5, 5, b->angle, b->pos, b->ownerId);
	aos_se(w, 0x13);
}

static void beam_draw(aos_world *w, aos_beam *b) /* FUN_004b17d0 */
{
	int bank = team_bank(b->team);
	switch (b->id) {
	case 100: {
		int tex = aos_tex("sk_ssbell00");
		/* screen call passes bam(+angle) → world −angle */
		blit(tex, bank, b->pos, 144.0f * b->scale, 144.0f * b->scale, -b->angle, (int)(128.0f * b->alpha), 0xffffff, AOS_BLEND_ALPHA, 0, 0, 0xff, 0xff);
		blit(tex, bank, b->pos, 128.0f * b->scale, 128.0f * b->scale, -b->angle, (int)(255.0f * b->alpha), 0xffffff, AOS_BLEND_ADD_ALPHA, 0, 0, 0xff, 0xff);
		break;
	}
	case 200:
	case 0xd2: {
		int tex = aos_texf("sk_wisp%02d", b->id != 200);
		float h = (b->active == 1 ? 48.0f : 24.0f) * b->scale; /* DAT_008140e4 / DAT_00853b8c */
		blit(tex, bank, b->pos, h, h, -b->angle, (int)(128.0f * b->alpha), 0xffffff, AOS_BLEND_ALPHA, 0, 0, 0x3f, 0x3f);
		h *= 0.75f;
		int a = (int)((b->active == 1 ? 255.0f : 128.0f) * b->alpha);
		blit(tex, bank, b->pos, h, h, -b->angle, a, 0xffffff, AOS_BLEND_ADD_ALPHA, 0, 0, 0x3f, 0x3f);
		break;
	}
	default: { /* 300 / 0x136: beamsphere atlas, cell = team colour */
		int c = aos_team_colour(w, b->team, b->id == 300 ? 0 : 1);
		int tex = aos_tex("beamsphere");
		int x0 = ((c % 10) % 2) * 100 + 0x32 + (c / 10) * 0xfa, y0 = ((c % 10) / 2) * 100;
		blit(tex, AOS_BANK_COMMON, b->pos, 40.0f, 20.0f, b->angle, (int)(96.0f * b->alpha), 0xffffff, AOS_BLEND_ALPHA,
		     x0, y0, ((c % 10) % 2) * 100 + 0x95 + (c / 10) * 0xfa, y0 + 99);
		x0 = (c / 10) * 0xfa;
		y0 = (c % 10) * 0x32;
		blit(tex, AOS_BANK_COMMON, b->pos, 20.0f, 10.0f, b->angle, (int)(255.0f * b->alpha), 0xffffff, AOS_BLEND_ADD_ALPHA,
		     x0, y0, x0 + 0x31, y0 + 0x31);
		break;
	}
	}
}

/* ---- missile family: spawn FUN_00674ca0, update FUN_00676290, draw FUN_00675060,
 *      onHit FUN_00675df0, onKilled FUN_00676050 ---- */

static int missile_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team, int32_t p18)
{
	if (id != 100 && id != 200 && id != 300 && id != 0x136 && id != 0x140 && id != 0x14a)
		return -1;
	float spin = aos_rand_real(w, -AOS_PI, AOS_PI); /* drawn before the record is pushed (no RNG in init) */
	int i = aos_missile_new(w, ID, id, pos, angle, owner, age0, team, p18);
	if (i < 0)
		return -1;
	aos_missile *m = &w->obj.missile[i];
	m->angle = spin;
	if (id == 100) {
		m->f74 = aos_cos(angle) * 10.0f;  /* vx */
		m->speed = aos_sin(angle) * 10.0f; /* vy */
		m->hp = 0x3c;
		m->damage = 0x32;
		m->shapeBody.radius = 20.0f; /* DAT_008140fc */
		m->shapeProj.radius = 10.0f; /* DAT_00813f4c */
		m->knockback = 10.0f;
	} else {
		m->speed = id == 200 ? 12.0f : 32.0f; /* DAT_00813f54 / DAT_008140dc */
		if (id == 200)
			m->f74 = 0.012f; /* DAT_00813ed0 (unused) */
		m->hp = 0xb4;
		m->damage = 0;
		m->knockback = 10.0f;
		m->active = 0;
		m->shapeBody.radius = 0;
		m->shapeProj.radius = 0;
	}
	return i;
}

static bool missile_on_hit(aos_world *w, aos_missile *m);

static float wrap_dec(float a, float d) { a -= d; if (a < -AOS_PI) a += AOS_2PI; return a; }
static float wrap_inc(float a, float d) { a += d; if (AOS_PI < a) a -= AOS_2PI; return a; }

static void orbit(aos_world *w, aos_missile *m)
{
	aos_player *o = team_player(w, m->team);
	m->pos[0] = aos_cos(m->moveAngle) * m->speed + o->x;
	m->pos[1] = aos_sin(m->moveAngle) * m->speed + o->y;
	aos_spawn_effect(w, ID, 0x6e, 0, m->pos, m->angle, m->ownerId, 0, m->team); /* frame >= 0: always */
}

static bool missile_update(aos_world *w, aos_missile *m)
{
	switch (m->id) {
	case 100:
		if (m->age % 2 == 0 || 11.0f < aos_fabs(m->speed)) { /* DAT_00813f50 */
			float r = aos_rand_real(w, -AOS_PI, AOS_PI);
			fx0(w, 2000, 0, m->pos, r, m->ownerId, 0, m->team);
		}
		m->speed -= 0.25f; /* gravity, DAT_00813f24 */
		m->pos[0] += m->f74;
		m->pos[1] += m->speed;
		m->angle = wrap_inc(m->angle, 0.1f);
		if (m->damage < 200)
			m->damage += 5;
		if (!aos_in_arena(m->pos, 30.0f)) {
			missile_on_hit(w, m);
			return false;
		}
		break;
	case 200:
		if (-12.0f < m->speed) /* _DAT_00824a90 */
			m->speed -= 0.2f;
		m->angle = wrap_dec(m->angle, 0.5f);
		move(m->pos, m->moveAngle, m->speed);
		if (w->frame % 2 == 0)
			aos_spawn_effect(w, ID, 100, 0, m->pos, m->angle, m->ownerId, 0, m->team);
		if (m->speed < 0.0f && !aos_in_arena(m->pos, 500.0f)) { /* ver >= 0x5a1 */
			missile_on_hit(w, m);
			return false;
		}
		aos_spawn_hitbox(w, ID, m->age % 16 == 0 ? 100 : m->age % 16 == 8 ? 0x6e : 0x78, m->pos, m->moveAngle, m->ownerId, 0, m->team);
		break;
	case 300:
	case 0x136:
		if (m->speed < 80.0f)
			m->speed += 4.0f;
		if (m->id == 300) {
			m->angle = wrap_dec(m->angle, 0.5f);
			m->moveAngle = wrap_dec(m->moveAngle, 0.12f); /* DAT_00824a8c */
		} else {
			m->angle = wrap_inc(m->angle, 0.5f);
			m->moveAngle = wrap_inc(m->moveAngle, 0.12f);
		}
		orbit(w, m);
		if (m->age == 0x1e)
			return false;
		if (m->age < 0x15 && m->age % 4 == 0) {
			aos_spawn_hitbox(w, ID, 100, m->pos, m->angle, m->ownerId, 0, m->team);
			return true;
		}
		if (m->age < 0x15 && m->age % 4 == 2) {
			aos_spawn_hitbox(w, ID, 0x6e, m->pos, m->angle, m->ownerId, 0, m->team);
			return true;
		}
		aos_spawn_hitbox(w, ID, 0x82, m->pos, m->moveAngle, m->ownerId, 0, m->team);
		break;
	case 0x140:
	case 0x14a:
		if (m->speed < 120.0f)
			m->speed += 6.0f;
		if (m->id == 0x140) {
			m->angle = wrap_dec(m->angle, 0.3f);
			m->moveAngle = wrap_dec(m->moveAngle, 0.08f);
		} else {
			m->angle = wrap_inc(m->angle, 0.3f);
			m->moveAngle = wrap_inc(m->moveAngle, 0.08f);
		}
		orbit(w, m);
		if (m->age == 0x46)
			return false;
		if (m->age < 0x33 && m->age % 8 == 0) {
			aos_spawn_hitbox(w, ID, 0x65, m->pos, m->angle, m->ownerId, 0, m->team);
			return true;
		}
		if (m->age < 0x33 && m->age % 8 == 4) {
			aos_spawn_hitbox(w, ID, 0x6f, m->pos, m->angle, m->ownerId, 0, m->team);
			return true;
		}
		aos_spawn_hitbox(w, ID, 0x82, m->pos, m->moveAngle, m->ownerId, 0, m->team);
		break;
	}
	return true;
}

static void scatter(aos_world *w, aos_missile *m, int32_t id, float r, int n, int32_t age0, int32_t step)
{
	for (int k = 0; k < n; k++) {
		float pt[3] = {m->pos[0], m->pos[1], m->pos[2]};
		pt[0] = aos_rand_real(w, -r, r) + pt[0];
		pt[1] = aos_rand_real(w, -r, r) + pt[1];
		fx0(w, id, 0, pt, 0.0f, m->ownerId, age0 + k * step, m->team);
	}
}

static bool missile_on_hit(aos_world *w, aos_missile *m) /* FUN_00675df0 */
{
	if (m->id == 100) {
		int32_t col = aos_team_colour(w, m->team, 0);
		scatter(w, m, 0x834, 30.0f, 3, 0, 0);   /* DAT_00817c08 */
		scatter(w, m, 500, 20.0f, 4, 0, -3);    /* DAT_00814108 */
		fx0(w, 0x515, col, m->pos, 0.0f, m->ownerId, 0, m->team);
		aos_se(w, 0x14);
	}
	return false;
}

static bool missile_on_killed(aos_world *w, aos_missile *m) /* FUN_00676050 */
{
	if (m->id == 100) {
		fx0(w, 600, 0, m->pos, 0.0f, m->ownerId, 0, m->team);
		scatter(w, m, 0x834, 30.0f, 3, 0, 0);
		scatter(w, m, 500, 20.0f, 3, -5, -3);
		aos_se(w, 0x1a);
	}
	return false;
}

static void missile_draw(aos_world *w, aos_missile *m) /* FUN_00675060 */
{
	(void)w;
	int bank = team_bank(m->team);
	float ph = ((float)(m->age % 0x32) / 50.0f) * AOS_2PI;
	float a1 = aos_sin(ph) * 0.15f + 0.85f; /* DAT_00813f20, DAT_00814120 */
	float a2 = aos_sin(ph) * 0.25f + 0.75f; /* DAT_00813f24, DAT_00813f3c */
	if (m->id == 100) {
		int f = (m->age / 5) % 6;
		model(aos_texf("sk_grnd%02d", f), bank, m->pos, 72.0f, 1.0f, m->angle, 1.0f, AOS_BLEND_ALPHA);
		model(aos_texf("sk_grnd%02d", f + 6), bank, m->pos, 72.0f, 1.0f, m->angle, a1, AOS_BLEND_ADD_ALPHA);
		model(aos_texf("sk_grnd%02d", f + 12), bank, m->pos, 72.0f, 1.0f, m->angle, a2, AOS_BLEND_ADD_ALPHA);
	} else {
		model(aos_tex("sk_tmbrn00"), bank, m->pos, 128.0f, 1.05f, m->angle, 1.0f, AOS_BLEND_ALPHA); /* DAT_00817de4 */
		model(aos_tex("sk_tmbrn01"), bank, m->pos, 128.0f, 1.0f, m->angle, a1, AOS_BLEND_ADD_ALPHA);
		model(aos_tex("sk_tmbrn02"), bank, m->pos, 128.0f, 1.0f, m->angle, a2, AOS_BLEND_ADD_ALPHA);
	}
}

/* ---- hitbox family: spawn FUN_005e7fd0, onHit FUN_005e8430 ---- */

static int hitbox_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t zero, int32_t team)
{
	int32_t ht = 3, dmg = 0;
	float kb = 0;
	aos_shape sh = {0, 0, 0};
	switch (id) {
	case 400: dmg = 100; sh.halfW = 100.0f; sh.halfH = 20.0f; kb = 5.0f; break;  /* DAT_00853b90, DAT_008140fc */
	case 0x19a: dmg = 100; sh.halfW = 100.0f; sh.halfH = 20.0f; kb = 15.0f; break;
	case 100: case 0x65: case 0x6e: case 0x6f: dmg = 0x19; sh.radius = 70.0f; break; /* DAT_008145ec */
	case 0x78: case 0x82: dmg = 0; sh.radius = 69.0f; break;  /* ver > 0x598: DAT_0081fa7c */
	case 200: case 0xd2: ht = 1; dmg = 0x19; sh.radius = 120.0f; break; /* DAT_00814174 */
	case 0xdc: dmg = 0; sh.radius = 120.0f; break;
	case 0x122: dmg = 0x535; sh.radius = 300.0f; kb = 30.0f; break; /* DAT_008145f0, DAT_008140e8 */
	case 300: dmg = 0xfa; sh.radius = 60.0f; kb = 15.0f; break;      /* DAT_00813a58, DAT_00853b88 */
	default: return -1;
	}
	int i = aos_hitbox_new(w, ID, id, pos, angle, owner, zero, team);
	if (i < 0)
		return -1;
	aos_hitbox *h = &w->obj.hitbox[i];
	h->hitType = ht;
	h->damage = dmg;
	h->shape = sh;
	h->knockback = kb;
	return i;
}

/* Critical block of FUN_005e8430 (ids 300 / 0x19a). */
static void crit(aos_world *w, aos_hitbox *hb, aos_player *v, int32_t dmg)
{
	fx0(w, 600, 0, &v->x, 0.0f, hb->ownerId, 0, hb->team);
	for (int k = 0; k < 5; k++) {
		float pt[3] = {v->x, v->y, v->z};
		pt[0] = aos_rand_real(w, -50.0f, 50.0f) + pt[0]; /* DAT_008149e4, DAT_008140ec */
		pt[1] = aos_rand_real(w, -50.0f, 50.0f) + pt[1];
		fx0(w, 0x834, 0, pt, 0.0f, hb->ownerId, 0, hb->team);
	}
	for (int k = 0; k < 5; k++) {
		float pt[3] = {v->x, v->y, v->z};
		pt[0] = aos_rand_real(w, -50.0f, 50.0f) + pt[0];
		pt[1] = aos_rand_real(w, -50.0f, 50.0f) + pt[1];
		fx0(w, 500, 0, pt, 0.0f, hb->ownerId, k * -3 - 5, hb->team);
	}
	aos_se(w, 0x1a);
	hb->damage = dmg;
	knock_near(w, v, 100.0f, 12.0f); /* FUN_006b1d70(victim,…) → knocks SAKI back */
}

static void hitbox_on_hit(aos_world *w, aos_hitbox *hb, aos_player *v) /* FUN_005e8430 */
{
	bool flying = v->state == AOS_ST_HITSTUN && 2.0f < v->speed; /* DAT_00813a4c */
	switch (hb->id) {
	case 200:
	case 0xd2: {
		float r0 = aos_rand_real(w, -AOS_PI, AOS_PI);
		int32_t col = aos_team_colour(w, hb->team, hb->id == 200 ? 0 : 1);
		for (int k = -3; k < 3; k++) {
			float f = ((float)k / 3.0f) * AOS_PI;
			float r = aos_rand_real(w, -0.25f, 0.25f);
			fx0(w, 0x456, col, &v->x, f + r + r0, hb->ownerId, 0, hb->team);
		}
		fx0(w, 0x515, col, &v->x, 0.0f, hb->ownerId, 0, hb->team);
		aos_se_cd(w, 0x13, 5);
		break;
	}
	case 300: {
		int32_t col = aos_team_colour(w, hb->team, 0);
		fx0(w, 0xaf0, col, &v->x, 0.0f, hb->ownerId, 0, hb->team);
		for (int k = -3; k < 3; k++) {
			float f = ((float)k / 3.0f) * AOS_PI;
			aos_spawn_particle(w, 1000, col, &v->x, f, hb->ownerId, 0, 1);
			aos_spawn_particle(w, 0x44c, col, &v->x, f, hb->ownerId, 0, 2);
		}
		for (int k = -4; k < 4; k++) {
			float f = ((float)k / 4.0f) * AOS_PI;
			float r = aos_rand_real(w, -0.25f, 0.25f);
			fx0(w, 0x456, col, &v->x, f + r, hb->ownerId, 0, hb->team);
		}
		aos_se(w, 0x19);
		if (aos_rand_int(w, 0, 100) <= 9)
			crit(w, hb, v, 0x1c2);
		break;
	}
	case 400: {
		int32_t col = aos_team_colour(w, hb->team, 0);
		for (int k = -2; k < 2; k++) {
			float f = ((float)k / 2.0f) * AOS_PI; /* DAT_00813a4c */
			aos_spawn_particle(w, 1000, col, &v->x, f, hb->ownerId, 0, 1);
			aos_spawn_particle(w, 0x44c, col, &v->x, f, hb->ownerId, 0, 2);
		}
		for (int k = -3; k < 3; k++) {
			float f = ((float)k / 3.0f) * AOS_PI;
			float r = aos_rand_real(w, -0.5f, 0.5f); /* DAT_00853bb0, DAT_00853b1c */
			fx0(w, 0x456, col, &v->x, f + r, hb->ownerId, 0, hb->team);
		}
		aos_se(w, 0x18);
		break;
	}
	case 0x19a: {
		int32_t col = aos_team_colour(w, hb->team, 0);
		fx0(w, 0xa8c, col, &v->x, 0.0f, hb->ownerId, 0, hb->team);
		for (int k = -3; k < 3; k++) {
			float f = ((float)k / 3.0f) * AOS_PI;
			aos_spawn_particle(w, 1000, col, &v->x, f, hb->ownerId, 0, 1);
			aos_spawn_particle(w, 0x44c, col, &v->x, f, hb->ownerId, 0, 2);
		}
		aos_se(w, 0x19);
		aos_screen_blur(w, 10, 0.75f /* DAT_00813f3c */, 1);
		if (aos_rand_int(w, 0, 100) <= 0x13)
			crit(w, hb, v, 0xfa);
		break;
	}
	case 0x78:
	case 0x82:
		if (!flying) {
			if (hb->id == 0x82)
				team_player(w, hb->team)->hitFlag = 1;
		} else {
			hb->dealsDamage = 0;
		}
		break;
	case 100:
	case 0x65:
	case 0x6e:
	case 0x6f:
		if (!flying) {
			float r0 = aos_rand_real(w, -AOS_PI, AOS_PI);
			int32_t col = aos_team_colour(w, hb->team, hb->id == 100 ? 0 : 1);
			for (int k = -2; k < 2; k++) {
				float f = ((float)k / 2.0f) * AOS_PI;
				float r = aos_rand_real(w, -0.25f, 0.25f);
				fx0(w, 0x456, col, hb->pos, f + r + r0, hb->ownerId, 0, hb->team);
			}
			fx0(w, 0x515, col, hb->pos, 0.0f, hb->ownerId, 0, hb->team);
			if (hb->id != 0x65 && hb->id != 0x6f) {
				fx0(w, 0x514, col, hb->pos, 0.0f, hb->ownerId, 0, hb->team);
				if (0xb < team_player(w, hb->team)->comboUi[4]) /* attacker +0x128 */
					hb->knockback = 10.0f;
				aos_se_cd(w, 0x15, 4);
			}
			aos_se_cd(w, 0x13, 4);
		} else {
			hb->dealsDamage = 0;
		}
		break;
	default:
		break;
	}
}

/* ---- Saki effect family: spawn FUN_00503530, update FUN_00504630, draw FUN_00503900 ---- */

static int effect_spawn(aos_world *w, int32_t id, int32_t variant, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team)
{
	if (id != 100 && id != 0x6e && id != 200 && id != 0xfa && id != 0x104 && id != 300 && id != 0x172 && id != 0x173)
		return -1; /* original returns 0 without pushing */
	float aux2 = 0;
	if (id == 0xfa || id == 0x104)
		aux2 = aos_rand_real(w, 1.0f, 4.0f); /* drawn before the push (DAT_00853b34, DAT_00853b68) */
	int i = aos_effect_new(w, ID, id, variant, pos, angle, owner, age0, team);
	if (i < 0)
		return -1;
	aos_effect *e = &w->obj.effect[i];
	e->scale = 1.0f;
	e->alpha = 1.0f;
	e->aux = 0;
	switch (id) {
	case 100: case 0x6e: case 200: e->drawLayer = 0; break;
	case 0xfa: case 0x104: e->alpha = 0; e->aux2 = aux2; break;
	case 0x172: e->scale = 0; e->aux = 20.0f; break; /* DAT_008140fc */
	case 0x173: e->scale = 0; e->aux = 16.0f; break; /* DAT_008140d8 */
	default: break;
	}
	return i;
}

static bool effect_update(aos_world *w, aos_effect *e)
{
	switch (e->id) {
	case 300:
		e->scale += 0.05f;
		e->alpha -= 0.025f; /* DAT_00813edc */
		return 0.0f < e->alpha;
	case 0x172:
	case 0x173: {
		float d = e->id == 0x172 ? 0.03f : 0.05f;
		e->scale += d;
		e->alpha -= d;
		move(e->pos, e->rotation, e->aux);
		return 0.0f < e->alpha;
	}
	case 0xfa:
	case 0x104: {
		if (e->alpha < 1.0f && (e->alpha += 0.025f, 1.0f < e->alpha))
			e->alpha = 1.0f;
		move(e->pos, e->moveDir, e->aux);
		e->scale -= 0.02f; /* DAT_00813ed8 */
		aos_player *o = team_player(w, e->team);
		float f = 1.0f - (1.0f - e->scale) * (1.0f - e->scale);
		float cy = o->y + 50.0f; /* DAT_008140ec */
		e->pos[0] = o->x + f * (e->pos[0] - o->x);
		e->pos[1] = cy + f * (e->pos[1] - cy);
		return !(e->scale <= 0.0f);
	}
	case 100:
		e->alpha -= 0.05f;
		return !(e->alpha <= 0.0f);
	case 0x6e:
		e->alpha -= 0.1f;
		return !(e->alpha <= 0.0f);
	case 200:
		return !(1 < e->age);
	}
	return true;
}

/* 2D colour table 0x87e138 (objects.md §5). */
static const uint32_t col2d[14] = {0x8090ff, 0xff9080, 0x80ff80, 0xffff90, 0xe180ff, 0xff80e1, 0x90e1ff,
                                   0xffe180, 0x80ffe1, 0xe1ff80, 0x8080ff, 0xffa4a4, 0x202020, 0xffffff};

static void effect_draw(aos_world *w, aos_effect *e)
{
	(void)w;
	int bank = team_bank(e->team);
	switch (e->id) {
	case 300: { /* sprite id = stale DAT_00895a18 (usually 0x16c), frame 2 — UNVERIFIED */
		float h = 400.0f * e->scale; /* DAT_008145f4 */
		int a = (int)((e->age % 2 == 0 ? 255.0f : 192.0f) * e->alpha);
		blit(aos_tex("sk_ssbell02"), bank, e->pos, h, h, e->rotation, a, 0xffffff, AOS_BLEND_ADD_ALPHA, 0, 0, 0xff, 0xff);
		break;
	}
	case 0x172:
	case 0x173: {
		float hw = e->aux * e->scale * (e->id == 0x172 ? 20.0f : 24.0f);
		uint32_t rgb = (e->variant >= 0 && e->variant < 14) ? col2d[e->variant] : 0xffffff;
		blit(aos_tex("hyper"), AOS_BANK_COMMON, e->pos, hw, 12.0f, e->rotation, (int)(255.0f * e->alpha), rgb, AOS_BLEND_ADD_ALPHA, 0, 0, 0xff, 0x1d);
		break;
	}
	case 0xfa:
	case 0x104: { /* vtable +0x60: no rotation */
		int tex = aos_texf("sk_wisp%02d", e->id != 0xfa);
		float h = 48.0f * e->scale * e->aux2;
		blit(tex, bank, e->pos, h, h, 0.0f, (int)(128.0f * e->alpha * e->scale), 0xffffff, AOS_BLEND_ALPHA, 0, 0, 0x3f, 0x3f);
		h *= 0.75f;
		blit(tex, bank, e->pos, h, h, 0.0f, (int)(255.0f * e->alpha * e->scale), 0xffffff, AOS_BLEND_ADD_ALPHA, 0, 0, 0x3f, 0x3f);
		break;
	}
	case 100:
	case 0x6e: {
		float h = 64.0f * e->scale;
		blit(aos_tex("sk_tmbrn00"), bank, e->pos, h, h, -e->rotation, (int)(128.0f * e->alpha), 0xffffff, AOS_BLEND_ADD_ALPHA, 0, 0, 0x7f, 0x7f);
		break;
	}
	case 200: {
		int tex = aos_tex("sk_ssbell00");
		float h = 144.0f * e->scale;
		blit(tex, bank, e->pos, h, h, -e->rotation, (int)(128.0f * e->alpha), 0xffffff, AOS_BLEND_ALPHA, 0, 0, 0xff, 0xff);
		h = 128.0f * e->scale;
		blit(tex, bank, e->pos, h, h, -e->rotation, (int)(255.0f * e->alpha), 0xffffff, AOS_BLEND_ADD_ALPHA, 0, 0, 0xff, 0xff);
		break;
	}
	}
}

/* ================================================================== */
/* 6. registration                                                     */

void aos_register_saki(void)
{
	aos_tab.inputA[ID] = input_a;
	aos_tab.inputB[ID] = input_b;
	/* CPU AI (FUN_00724010 level 0, FUN_00723720 levels 1/2) not ported */

	static const struct { int s; aos_update_fn t2; aos_draw_fn t1; } tab[] = {
		{0x153, st_neutral, dr_neutral}, {0x154, st_stub, dr_dash},
		{0x155, st_stub, dr_ko},         {0x156, st_stub, dr_hit},
		{0x157, st_stub, dr_shield},     {0x15d, st_tornado, dr_tornado},
		{0x15e, st_stardust, dr_stardust}, {0x15f, st_grenade, dr_grenade},
		{0x160, st_boomerang, dr_stardust}, {0x161, st_blade, dr_sld8},
		{0x162, st_hammer, dr_hammer},   {0x163, st_unused_lunge, dr_lunge},
		{0x164, st_swing, dr_swing},     {0x165, st_cyclone, dr_sld5},
		{0x166, st_galaxy, dr_sld8},     {0x167, st_sunshine, dr_ball},
		{0x168, st_rolling, dr_sld5},    {0x169, st_bigbang, dr_ball},
	};
	for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++) {
		aos_tab.T2[tab[i].s] = tab[i].t2;
		aos_tab.T1[tab[i].s] = tab[i].t1;
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
	aos_otab.effectSpawn[ID] = effect_spawn;
	aos_otab.effectUpdate[ID] = effect_update;
	aos_otab.effectDraw[ID] = effect_draw;
}
