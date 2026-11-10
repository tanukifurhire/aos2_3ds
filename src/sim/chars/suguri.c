/* Suguri (engine id 10 = AOS_CH_SUGURI) — 1:1 port of her code in aos2.exe.
 * Spec: docs/re/suguri.md. Every function names its original address; the
 * decompiled code (tools/re/dec/all.c) is the source of truth. Only the
 * version-0x5a2 branches of DAT_0089426c are implemented.
 *
 * Reference character: the other ports follow this layout —
 *   1. helpers  2. input dispatch  3. T2 states  4. T1 draws
 *   5. object families (beam, hitbox, missile, laser, enemy, effect)  6. registration */
#include <stddef.h>

#include "../draw.h"
#include "../fx.h"
#include "../sim.h"

#define ID AOS_CH_SUGURI
#define HALF_PI 1.5707964f /* DAT_00814104 */

void aos_se_cd(aos_world *w, int id, int cd);                              /* core.c, FUN_007bcaf0 */

/* ================================================================== */
/* 1. helpers                                                          */

/* FUN_00780450(team): the player that owns a team. */
static aos_player *team_player(aos_world *w, int32_t team) { return &w->p[team == 2 ? 1 : 0]; }

/* Character art bank for objects: DAT_00885a00[team*0x10] = palette of the team's player.
 * In the port the bank is the player index (PORTING.md "Drawing"). */
static int team_bank(int32_t team) { return team == 2 ? AOS_BANK_P1 : AOS_BANK_P0; }

/* Common (type 0) effect, original (*DAT_00881698)(id, variant, pos, angle, owner, 0, team). */
static void fx0(aos_world *w, int32_t id, int32_t variant, const float pos[3], float angle, int32_t owner, int32_t team)
{
	aos_spawn_effect(w, 0, id, variant, pos, angle, owner, 0, team);
}

/* Suguri effect (type 10), original (*DAT_008816c0)(...). */
static void fx10(aos_world *w, int32_t id, int32_t variant, const float pos[3], float angle, int32_t owner, int32_t team)
{
	aos_spawn_effect(w, ID, id, variant, pos, angle, owner, 0, team);
}

/* Dash sparks shared by several states:
 * r = rand(-0.3,0.3); k = randint(0,100)%8; effect 0xa28+k (colour 13) at pos, angle r+dir. */
static void dash_sparks(aos_world *w, aos_player *p, float dir)
{
	float r = aos_rand_real(w, -0.3f, 0.3f);     /* DAT_0081fa88, DAT_00813f28 */
	int32_t k = aos_rand_int(w, 0, 100) % 8;
	fx0(w, 0xa28 + k, 0xd, &p->x, r + dir, p->objId, p->side);
}

/* Free movement at walk speed (FUN_00756f80 body; also used by 0x133). */
static void walk(aos_player *p)
{
	float s = aos_tab.stats[p->id].walk;
	if (p->up > 1) p->y += s;
	if (p->down > 1) p->y -= s;
	if (p->left > 1) p->x -= s;
	if (p->right > 1) p->x += s;
}

static void point_ahead(const aos_player *p, float ang, float dist, float out[3])
{
	out[0] = aos_cos(ang) * dist + p->x;
	out[1] = aos_sin(ang) * dist + p->y;
	out[2] = p->z;
}

/* ================================================================== */
/* 2. input dispatch                                                   */

static bool st(aos_world *w, aos_player *p, int32_t s)
{
	aos_set_state(w, p, s);
	return true;
}

/* Normal moves, shared by both schemes (FUN_0075a030 second half). */
static bool normal_moves(aos_world *w, aos_player *p)
{
	if (p->state == AOS_ST_NEUTRAL || p->state == AOS_ST_DASH) {
		if (p->D == 3) {
			aos_dash(w, p, 0);
		} else if (p->Shield == 3 && aos_spend_hyper(p, 0.5f) && aos_may_shield(w, p)) {
			/* literal: 0.5 is spent even when may_shield then refuses */
			aos_shield(w, p);
		} else {
			if (p->S > 1 && p->B == 3)
				return st(w, p, 0x140);
			if (p->S > 1 && p->A == 3)
				return st(w, p, aos_dist(w, p) >= 300.0f ? 0x13c : 0x139); /* DAT_008145f0 */
			if (p->B == 3)
				return st(w, p, 0x138);
			if (p->D > 0 && p->A == 3) /* cmp [+0x34],1 / jl @0x75a3e3: D >= 1 */
				return st(w, p, 0x135);
			if (p->A == 3)
				return st(w, p, 0x133);
		}
	}
	return aos_tab.T2[p->state] ? aos_tab.T2[p->state](w, p) : true;
}

static bool input_a(aos_world *w, aos_player *p) /* FUN_0075a030 (0x881310+4*10) */
{
	if (aos_may_hyper(w, p)) {
		if (p->S > 1 && p->B > 1 && p->H == 3 && aos_spend_hyper(p, 3.0f))
			return st(w, p, 0x143);
		if (p->S > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f))
			return st(w, p, 0x141);
		if (p->B > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f))
			return st(w, p, 0x13f);
		if (p->H == 3 && aos_spend_hyper(p, 1.0f))
			return st(w, p, 0x136);
	}
	return normal_moves(w, p);
}

static bool input_b(aos_world *w, aos_player *p) /* FUN_0075a4b0 (0x881360+4*10), control scheme B */
{
	if (aos_may_hyper(w, p)) {
		if (p->H > 1 && p->Shield == 3 && aos_spend_hyper(p, 3.0f))
			return st(w, p, 0x143);
		if (p->H > 1 && p->S == 3 && aos_spend_hyper(p, 1.0f))
			return st(w, p, 0x141);
		if (p->H > 1 && p->B == 3 && aos_spend_hyper(p, 1.0f))
			return st(w, p, 0x13f);
		if (p->H > 1 && p->A == 3 && aos_spend_hyper(p, 1.0f))
			return st(w, p, 0x136);
	}
	return normal_moves(w, p);
}

/* ================================================================== */
/* 3. T2 state handlers                                                */

/* Shared end-of-move: dash cancel from frame `from`, back to neutral after `end`.
 * Returns true when the normal (non-cancel) branch ran (callers apply friction there). */
static bool end_move(aos_world *w, aos_player *p, int32_t from, int32_t end)
{
	if (p->timer < from || p->D != 3) {
		if (p->timer > end)
			p->state = AOS_ST_NEUTRAL; /* literal: timer not reset */
		return true;
	}
	aos_dash(w, p, 1);
	return false;
}

static bool st_neutral(aos_world *w, aos_player *p) /* 0x128 T2 FUN_00756f80 */
{
	(void)w;
	walk(p);
	p->faceAngle = p->facingRight == 1 ? 0.0f : AOS_PI;
	return true;
}

static bool st_stub(aos_world *w, aos_player *p) /* 0x129..0x12c T2 FUN_00757070/80/90/a0 */
{
	(void)w;
	(void)p;
	return true;
}

static bool st_rifle(aos_world *w, aos_player *p) /* 0x132 T2 FUN_007570b0 Beam Rifle */
{
	if (p->timer == 12 && aos_use_gauge(p, 0x132)) {
		p->faceAngle = aos_aim(w, p);
		aos_spawn_beam(w, ID, 100, &p->x, p->faceAngle, p->objId, 0, p->side);
		aos_se(w, 0xd);
	}
	end_move(w, p, 20, 30);
	return true;
}

static bool st_rifle_hold(aos_world *w, aos_player *p) /* 0x133 T2 FUN_007571b0 Beam Rifle / Charged Rifle */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer < 11 && p->A < 1) {
		p->state = 0x132; /* timer kept */
		aos_tab.T2[0x132](w, p);
		return true;
	}
	if (p->timer > 12 && p->timer < 40) {
		if (p->D < 2)
			walk(p);
		p->faceAngle = aos_aim(w, p);
	}
	if (p->timer > 14 && p->timer < 25 && p->A < 2) {
		if (aos_use_gauge(p, 0x132)) {
			aos_spawn_beam(w, ID, 100, &p->x, p->faceAngle, p->objId, 0, p->side);
			aos_se(w, 0xd);
		}
		p->timer = 100;
	}
	if (p->timer == 29) {
		aos_popup(w, 2, &p->x, p->objId);
		aos_se(w, 0x2a);
	}
	if (p->timer > 29 && p->timer < 40 && p->A < 2 && aos_use_gauge(p, 0x133)) {
		aos_spawn_beam(w, ID, 200, &p->x, p->faceAngle, p->objId, 0, p->side);
		int32_t col = aos_team_colour(w, p->side, 0);
		fx0(w, 0x456, col, &p->x, p->faceAngle + HALF_PI, p->objId, p->side);
		fx0(w, 0x456, col, &p->x, p->faceAngle - HALF_PI, p->objId, p->side);
		fx0(w, 0x515, col, &p->x, p->faceAngle, p->objId, p->side);
		aos_se(w, 0xe);
		p->timer = 100;
	}
	if (p->timer == 40)
		p->timer = 30;
	if (p->timer > 24 && p->timer < 40 && p->timer % 3 == 0)
		fx0(w, 0x514, aos_team_colour(w, p->side, 0), &p->x, 0.0f, p->objId, p->side);
	if (p->timer < 115 || p->D != 3) {
		if (p->timer > 150)
			p->state = AOS_ST_NEUTRAL;
		if (p->timer > 99 && p->hitFlag == 1 && p->B == 3)
			aos_set_state(w, p, 0x140);
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_rifle_finisher(aos_world *w, aos_player *p) /* 0x134 T2 FUN_007576f0 (Sword 2 -> A) */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer == 5 && aos_use_gauge(p, 0x134)) {
		p->faceAngle = aos_aim(w, p);
		aos_spawn_beam(w, ID, 200, &p->x, p->faceAngle, p->objId, 0, p->side);
		aos_se(w, 0xe);
	}
	if (p->hitFlag == 1 && p->B == 3)
		aos_set_state(w, p, 0x140);
	else
		end_move(w, p, 30, 40);
	return true;
}

static bool st_rapid(aos_world *w, aos_player *p) /* 0x135 T2 FUN_00757860 Rapid-fire (D+A) */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer == 5 || p->timer == 12 || p->timer == 19) {
		p->faceAngle = aos_aim(w, p);
		if (aos_use_gauge(p, 0x135)) {
			float r = aos_rand_real(w, -0.1f, 0.1f); /* DAT_00814130, DAT_00853b10 */
			aos_spawn_beam(w, ID, 0x96, &p->x, r + p->faceAngle, p->objId, 0, p->side);
			aos_se(w, 0xd);
		}
	}
	float a = aos_dir_angle(p);
	if (a < AOS_2PI) {
		float m = p->moveAngle;
		aos_angle_toward(&m, a, 0.08f); /* DAT_00813f00 */
		p->moveAngle = m;
	}
	if (end_move(w, p, 35, 45))
		p->friction *= 0.5f;
	return true;
}

static bool st_hyper_rifle(aos_world *w, aos_player *p) /* 0x136 T2 FUN_00757a50 Hyper Rifle */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		aos_barrier(w, p, 10);
		aos_hyper_flourish(w, p); /* SE 6, dim(100,.75), speedline(100,pos,aim,π/4), blur(20,.5,2), effect 0xfa */
	}
	if (p->timer == 10) {
		p->faceAngle = aos_aim(w, p);
		p->speed = 0;
		aos_spawn_laser(w, ID, 100, &p->x, p->faceAngle, p->objId, p->side, p->color);
	}
	end_move(w, p, 30, 80);
	return true;
}

static bool st_homing(aos_world *w, aos_player *p) /* 0x137 T2 FUN_00757c00 Missile (Homing) */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer > 14 && p->timer < 25 && p->timer % 3 == 0 && aos_use_gauge(p, 0x137)) {
		p->faceAngle = aos_aim(w, p);
		float r = aos_rand_real(w, 0.6f, 1.2f); /* DAT_00813f34, DAT_00817d78 (drawn, then unused on 0x5a2) */
		(void)r;
		int32_t sign = ((p->timer / 3) % 2) * 2 - 1;
		/* ver > 0x5a0: double math with 0.9 (_DAT_00826138) */
		float ang = (float)((double)p->faceAngle + (double)sign * 0.9);
		aos_spawn_missile(w, ID, 100, &p->x, ang, p->objId, 0, p->side, p->color);
		aos_se_cd(w, 0x22, 3);
	}
	if (end_move(w, p, 30, 45))
		p->friction *= 0.5f;
	return true;
}

static bool st_sidewinder(aos_world *w, aos_player *p) /* 0x138 T2 FUN_00757e30 Missile (Sidewinder) */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer < 11 && p->B < 1) {
		p->state = 0x137; /* timer kept */
		aos_tab.T2[0x137](w, p);
		return true;
	}
	if (p->timer > 11 && p->timer < 31 && p->timer % 3 == 0 && aos_use_gauge(p, 0x138)) {
		float v[2];
		v[0] = aos_rand_real(w, -60.0f, 60.0f);   /* DAT_008149e8, DAT_00813a58 */
		v[1] = aos_rand_real(w, -120.0f, 120.0f); /* DAT_0081525c, DAT_00814174 */
		aos_vec_rotate(v, p->faceAngle);
		float pt[3] = {v[0] + p->x, v[1] + p->y, p->z};
		aos_spawn_missile(w, ID, 200, pt, p->faceAngle, p->objId, 0, p->side, p->color);
		float r = aos_rand_real(w, -AOS_PI, AOS_PI);
		fx10(w, 100, 0, pt, r, p->objId, p->side);
		aos_se_cd(w, 0x22, 3);
	}
	if (end_move(w, p, 35, 45))
		p->friction *= 0.5f;
	return true;
}

/* Sword lunge set-up shared by 0x139..0x13b: speed = min(dist/div, cap) toward the aim. */
static void lunge(aos_world *w, aos_player *p, float div, float cap)
{
	p->faceAngle = aos_aim(w, p);
	float s = aos_dist(w, p) / div;
	if (cap <= s)
		s = cap;
	p->dashAngle = p->faceAngle;
	p->moveAngle = p->faceAngle;
	p->speed = s;
}

static bool st_sword1(aos_world *w, aos_player *p) /* 0x139 T2 FUN_00758790 Sword (close) */
{
	if (p->timer == 0)
		lunge(w, p, 14.0f, 14.0f); /* DAT_00813a54 */
	if (3.0f <= p->speed)
		dash_sparks(w, p, p->faceAngle);
	if (p->timer == 5)
		aos_se(w, 0x1f);
	if (p->timer == 10 && aos_use_gauge(p, 0x139)) {
		float pt[3];
		point_ahead(p, p->faceAngle, 30.0f, pt); /* DAT_008140e8 */
		aos_spawn_hitbox(w, ID, 100, pt, p->faceAngle, p->objId, 0, p->side);
	}
	if (p->timer >= 10 && p->hitFlag == 1 && p->S >= 2 && p->A == 3)
		aos_set_state(w, p, 0x13a);
	else
		end_move(w, p, 30, 40);
	return true;
}

static bool st_sword2(aos_world *w, aos_player *p) /* 0x13a T2 FUN_00758ac0 Sword 2nd */
{
	if (p->timer == 0)
		lunge(w, p, 15.0f, 10.0f); /* DAT_00853b88, DAT_00813f4c */
	if (p->timer == 5)
		aos_se(w, 0x1f);
	if (p->timer == 10 && aos_use_gauge(p, 0x13a)) {
		float pt[3];
		point_ahead(p, p->faceAngle, 30.0f, pt);
		aos_spawn_hitbox(w, ID, 0x6e, pt, p->faceAngle, p->objId, 0, p->side);
	}
	bool conf = p->timer >= 10 && p->hitFlag == 1;
	if (conf && p->S >= 2 && p->A == 3)
		aos_set_state(w, p, 0x13b);
	else if (conf && p->A == 3)
		aos_set_state(w, p, 0x134);
	else
		end_move(w, p, 30, 40);
	return true;
}

static bool st_sword3(aos_world *w, aos_player *p) /* 0x13b T2 FUN_00758db0 Sword 3rd */
{
	if (p->timer == 0)
		lunge(w, p, 15.0f, 10.0f);
	if (p->timer == 8 && aos_use_gauge(p, 0x13b))
		aos_spawn_hitbox(w, ID, 0x78, &p->x, p->faceAngle, p->objId, 0, p->side);
	end_move(w, p, 30, 40);
	return true;
}

static bool st_arrow1(aos_world *w, aos_player *p) /* 0x13c T2 FUN_007580d0 Sword Arrow */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer == 10 && aos_use_gauge(p, 0x13c)) { /* ver > 0x5a0: frame 10 */
		aos_spawn_beam(w, ID, 300, &p->x, p->faceAngle, p->objId, 0, p->side);
		aos_se(w, 0x1f);
	}
	if (p->timer >= 16 && p->timer <= 26 && p->A == 3 && aos_use_gauge(p, 0x13d))
		aos_set_state(w, p, 0x13d);
	else
		end_move(w, p, 32, 40);
	return true;
}

/* Entry of Sword Arrow 2/3: optional short dash in the held direction. */
static void arrow_entry(aos_world *w, aos_player *p)
{
	float a = aos_dir_angle(p);
	if (a < AOS_2PI) {
		p->moveAngle = a;
		p->speed = 16.0f;  /* DAT_008140d8 */
		p->heat += 0.3f;   /* DAT_00813f28 */
		aos_se(w, 8);
	}
	p->faceAngle = aos_aim(w, p);
	p->dashAngle = p->faceAngle;
}

static bool st_arrow2(aos_world *w, aos_player *p) /* 0x13d T2 FUN_007582c0 */
{
	if (p->timer == 0)
		arrow_entry(w, p);
	if (3.0f <= p->speed)
		dash_sparks(w, p, p->moveAngle);
	if (p->timer == 8) {
		aos_spawn_beam(w, ID, 300, &p->x, p->faceAngle, p->objId, 0, p->side);
		aos_se(w, 0x1f);
	}
	if (p->timer >= 14 && p->timer <= 24 && p->A == 3 && aos_use_gauge(p, 0x13e))
		aos_set_state(w, p, 0x13e);
	else
		end_move(w, p, 25, 40);
	return true;
}

static bool st_arrow3(aos_world *w, aos_player *p) /* 0x13e T2 FUN_00758580 */
{
	if (p->timer == 0)
		arrow_entry(w, p);
	if (3.0f <= p->speed)
		dash_sparks(w, p, p->moveAngle);
	if (p->timer == 6) {
		aos_spawn_beam(w, ID, 300, &p->x, p->faceAngle, p->objId, 0, p->side);
		aos_se(w, 0x1f);
	}
	end_move(w, p, 25, 40);
	return true;
}

static bool st_hyper_arrow(aos_world *w, aos_player *p) /* 0x13f T2 FUN_007596d0 Hyper Arrow */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		p->dashAngle = p->moveAngle = p->faceAngle;
		p->speed = 0;
		aos_barrier(w, p, 10);
		aos_hyper_flourish(w, p);
	}
	if (p->timer == 10) {
		aos_spawn_beam(w, ID, 0x15e, &p->x, p->faceAngle, p->objId, 0, p->side);
		aos_se(w, 0x20);
	}
	end_move(w, p, 50, 70);
	return true;
}

static bool st_sword_break(aos_world *w, aos_player *p) /* 0x140 T2 FUN_00758f20 Sword Break */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		p->dashAngle = p->moveAngle = p->faceAngle;
		p->speed = 0;
	}
	if (p->timer == 10 && aos_use_gauge(p, 0x140)) { /* ver > 0x5a0 branch: no heat cost */
		p->faceAngle = aos_aim(w, p);
		p->dashAngle = p->moveAngle = p->faceAngle;
		p->speed = 20.0f; /* DAT_008140fc */
		aos_se(w, 0x20);
	}
	if (p->timer > 9 && p->timer < 23 && aos_dist(w, p) < 80.0f) /* DAT_00814164 */
		p->speed = 4.0f;                                          /* DAT_00853b68 */
	if (p->timer > 11 && p->timer < 25 && p->timer % 3 == 0) {
		float pt[3];
		point_ahead(p, p->faceAngle, 30.0f, pt);
		int i = aos_spawn_hitbox(w, ID, 0x82, pt, p->faceAngle, p->objId, 0, p->side);
		if (p->timer == 24 && i >= 0)
			w->obj.hitbox[i].knockback = 15.0f; /* DAT_00853b88 via FUN_004b2eb0 */
	}
	if (6.0f <= p->speed) /* DAT_00813f44 */
		dash_sparks(w, p, p->faceAngle);
	if (end_move(w, p, 40, 50))
		p->friction *= 0.5f;
	return true;
}

static bool st_hyper_sword(aos_world *w, aos_player *p) /* 0x141 T2 FUN_00759360 Hyper Sword */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		p->dashAngle = p->moveAngle = p->faceAngle;
		p->speed = 0;
		aos_barrier(w, p, 10);
		aos_hyper_flourish(w, p);
	}
	if (p->timer == 10) {
		p->faceAngle = aos_aim(w, p);
		p->dashAngle = p->moveAngle = p->faceAngle;
		p->speed = 20.0f;
		aos_se(w, 0x20);
	}
	if (5.0f <= p->speed) /* DAT_00813f40 */
		dash_sparks(w, p, p->moveAngle);
	if (p->timer == 15) {
		float a = p->faceAngle + AOS_PI;
		aos_angle_normalize(&a);
		float pt[3];
		point_ahead(p, p->faceAngle, 30.0f, pt);
		aos_spawn_hitbox(w, ID, 0x96, pt, a, p->objId, 0, p->side);
	}
	end_move(w, p, 50, 70);
	return true;
}

static bool st_accel(aos_world *w, aos_player *p) /* 0x143 T2 FUN_007598a0 Accel Burst */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		p->speed = 0;
		aos_barrier(w, p, 0x14);
		/* SE 7, FUN_004e9280(150,.85), FUN_007bec20(100,pos,aim,0), FUN_004d7ac0(150,.65,2), effect 0xfa */
		aos_hyper_flourish_ex(w, p, 7, 0x96, 0.85f /* DAT_00814120 */, 100, p->faceAngle, 0.0f, 0x96,
		                      0.65f /* DAT_0081788c */, 2, 1);
	}
	if (p->timer == 10) {
		p->faceAngle = aos_aim(w, p);
		p->speed = 0;
		static const float offs[4] = {-1.2f, -0.6f, 0.6f, 1.2f}; /* DAT_00824e14, 00826140, 00813f34, 00817d78 */
		for (int k = 0; k < 4; k++) {
			int i = aos_spawn_enemy(w, ID, 100, p->objId, p->side, p->color);
			float off = offs[k];
			float v[2] = {aos_cos(off) * 50.0f, aos_sin(off) * 100.0f}; /* DAT_008140ec, DAT_00853b90 */
			aos_vec_rotate(v, p->faceAngle);
			if (i < 0)
				continue;
			aos_enemy *e = &w->obj.enemy[i];
			e->pos[0] = p->x + v[0];
			e->pos[1] = p->y + v[1];
			e->f70 = off;
			e->angle2 = off * 0.5f + p->faceAngle;
			e->angle = e->angle2;
		}
		aos_spawn_laser(w, ID, 200, &p->x, p->faceAngle, p->objId, p->side, p->color);
	}
	end_move(w, p, 80, 100);
	return true;
}

/* ================================================================== */
/* 4. T1 draw handlers (sprite table suguri.md §4)                     */

static int imin(int a, int b) { return a < b ? a : b; }
static int pingpong(int t, int n) { int f = (t / n) & 3; return f == 3 ? 1 : f; }

/* Standard AIM transform (e.g. FUN_0075cbc0): |aim| >= π/2 → RotZ(aim), RotX(180°), RotY(180°);
 * else RotZ(aim), RotY(180°). Exact in 2D: RotX(π)·RotY(π) = RotZ(π) and Z rotations commute,
 * so the first case is an unmirrored RotZ(aim+π); the second is mirror (art faces −x) + RotZ(aim). */
static aos_dcmd *aim_quad(aos_world *w, aos_player *p, const char *pat, int frame, float S)
{
	aos_dcmd *c = aos_draw(aos_texf(pat, frame), aos_pidx(w, p), p->x, p->y, 0);
	c->halfW = c->halfH = S * 0.5f;
	if (aos_fabs(p->faceAngle) >= HALF_PI) {
		c->flipX = 0;
		c->rotZ = (uint16_t)(aos_rad2bam(p->faceAngle) + 0x8000u);
	} else {
		c->flipX = 1;
		c->rotZ = aos_rad2bam(p->faceAngle);
	}
	c->blend = AOS_BLEND_ALPHA;
	return c;
}

/* Rotation vector the AIM draws hand to FUN_004d7e00: {rotX, rotY, rotZ} in bam. */
static void aim_rot(const aos_player *p, int32_t rot[3])
{
	rot[0] = aos_fabs(p->faceAngle) >= HALF_PI ? aos_deg2bam(0xb4) : 0;
	rot[1] = aos_deg2bam(0xb4);
	rot[2] = aos_rad2bam(p->faceAngle);
}

static void dr_neutral(aos_world *w, aos_player *p) /* 0x128 T1 FUN_0075bc90 */
{
	const char *pat = "sg_std%02d";
	if (p->up > 1) pat = "sg_fwd%02d";
	if (p->down > 1) pat = "sg_bck%02d";
	if (!p->facingRight && p->left > 1) pat = "sg_fwd%02d";
	if (p->facingRight && p->left > 1) pat = "sg_bck%02d";
	if (!p->facingRight && p->right > 1) pat = "sg_bck%02d";
	if (p->facingRight && p->right > 1) pat = "sg_fwd%02d";
	aos_dcmd *c = aos_draw(aos_texf(pat, pingpong(p->timer, 5)), aos_pidx(w, p), p->x, p->y, 0);
	c->halfW = c->halfH = 48.0f;
	c->flipX = p->facingRight != 0; /* RotY(π) when +0x8c set: art faces left */
	int32_t rot[3] = {0, p->facingRight ? aos_deg2bam(0xb4) : 0, 0};
	aos_afterimage(w, c, 1, p, rot); /* FUN_004d7e00 */
}

static void dr_dash(aos_world *w, aos_player *p) /* 0x129 T1 FUN_0075bf40 */
{
	bool against = (p->facingRight == 1 && aos_fabs(p->faceAngle) > HALF_PI) ||
	               (p->facingRight == 0 && aos_fabs(p->faceAngle) < HALF_PI);
	int f = against ? pingpong(p->timer, 3) + 6 : (p->timer / 2) % 6;
	aos_dcmd *c = aim_quad(w, p, "sg_dsh%02d", f, 96.0f);
	int32_t rot[3];
	aim_rot(p, rot);
	aos_afterimage(w, c, 1, p, rot); /* FUN_004d7e00 */
}

static void dr_ko(aos_world *w, aos_player *p) /* 0x12a T1 FUN_0075c8c0 */
{
	aim_quad(w, p, "sg_los%02d", pingpong(p->timer, 3) + (w->frame % 2 == 0 ? 3 : 0), 96.0f);
}

static void dr_hit(aos_world *w, aos_player *p) /* 0x12b T1 FUN_0075c360 */
{
	aim_quad(w, p, "sg_los%02d", 0, 96.0f);
}

static void dr_slide(aos_world *w, aos_player *p) /* 0x12c / 0x137 / 0x138 T1 FUN_0075c600 / 0075da70 / 0075dd30 */
{
	aim_quad(w, p, "sg_sld%02d", imin(p->timer / 4, 5), 96.0f);
}

static int rifle_frame(int t, int start, int step) { return t < start ? 0 : imin((t - start) / step + 1, 4); }

static void dr_rifle(aos_world *w, aos_player *p) /* 0x132 T1 FUN_0075cbc0 */
{
	aim_quad(w, p, "sg_rfl%02d", rifle_frame(p->timer, 15, 3), 96.0f);
}
static void dr_rifle_hold(aos_world *w, aos_player *p) /* 0x133 T1 FUN_0075cea0 */
{
	aim_quad(w, p, "sg_rfl%02d", rifle_frame(p->timer, 100, 3), 96.0f);
}
static void dr_rifle_fin(aos_world *w, aos_player *p) /* 0x134 T1 FUN_0075d180 */
{
	aim_quad(w, p, "sg_rfl%02d", rifle_frame(p->timer, 5, 3), 96.0f);
}
static void dr_rapid(aos_world *w, aos_player *p) /* 0x135 T1 FUN_0075d460 */
{
	int t = p->timer, f;
	if (t < 5)
		f = 0;
	else if (t < 12)
		f = (t - 5) / 2 > 2 ? 0 : (t - 5) / 2;
	else if (t < 19)
		f = (t - 12) / 2 > 2 ? 0 : (t - 12) / 2;
	else
		f = (t - 19) / 2;
	aim_quad(w, p, "sg_rfl%02d", imin(f, 4), 96.0f);
}
static void dr_hyper_rifle(aos_world *w, aos_player *p) /* 0x136 T1 FUN_0075d790 (literal 0->4 jump at 60) */
{
	int t = p->timer;
	aim_quad(w, p, "sg_rfl%02d", t < 60 ? 0 : imin((t - 15) / 3 + 1, 4), 96.0f);
}
static void dr_accel(aos_world *w, aos_player *p) /* 0x143 T1 FUN_00764650 */
{
	aim_quad(w, p, "sg_rfl%02d", rifle_frame(p->timer, 80, 4), 96.0f);
}

/* Sword family: back blade, body, slash, front blade (anim spec §4.4). */
enum { BK_NONE, BK_IF_B, BK_IF_NOT_B };
enum { FR_IF_B, FR_IF_B_TWICE, FR_ALWAYS };
typedef struct {
	const char *body, *blade, *slash;
	float bladeS;
	int start, cap, back, front, slashA;
} sword_look;

static void dr_sword_layers(aos_world *w, aos_player *p, const sword_look *L, int f, bool ghost)
{
	int b = f == 0 ? 0 : 1;
	if ((L->back == BK_IF_B && b) || (L->back == BK_IF_NOT_B && !b))
		aim_quad(w, p, L->blade, b, L->bladeS);
	aos_dcmd *body = aim_quad(w, p, L->body, f, 96.0f);
	if (ghost) { /* FUN_004d7e00 after the body (0x140 / 0x141 only) */
		int32_t rot[3];
		aim_rot(p, rot);
		aos_afterimage(w, body, 1, p, rot);
	}
	int s = -1;
	if (L->slashA)
		s = (f == 1 || f == 2) ? 0 : (f == 3 || f == 4) ? 1 : -1;
	else
		s = f == 1 ? 0 : f == 2 ? 1 : -1;
	if (s >= 0)
		aim_quad(w, p, L->slash, s, 192.0f); /* slash layer: no global tint */
	if (L->front == FR_ALWAYS || b) {
		aim_quad(w, p, L->blade, b, L->bladeS);
		if (L->front == FR_IF_B_TWICE)
			aim_quad(w, p, L->blade, b, L->bladeS); /* literal: drawn twice in FUN_0075dff0 */
	}
}

static const sword_look LK_A1 = {"sg_swda%02d", "sg_swdab%02d", "sg_swdas%02d", 96.0f, 6, 5, BK_IF_B, FR_IF_B_TWICE, 1};
static const sword_look LK_B = {"sg_swdb%02d", "sg_swdbb%02d", "sg_swdbs%02d", 192.0f, 6, 4, BK_NONE, FR_ALWAYS, 0};
static const sword_look LK_C = {"sg_swdc%02d", "sg_swdcb%02d", "sg_swdcs%02d", 192.0f, 6, 5, BK_IF_NOT_B, FR_IF_B, 1};
static const sword_look LK_A2 = {"sg_swda%02d", "sg_swdab%02d", "sg_swdas%02d", 96.0f, 6, 5, BK_IF_B, FR_IF_B, 0};
static const sword_look LK_D = {"sg_swdd%02d", "sg_swddb%02d", "sg_swdds%02d", 192.0f, 0, 0, BK_IF_NOT_B, FR_IF_B, 0};
static const sword_look LK_HS = {"sg_swdc%02d", "sg_swdcb%02d", "sg_swdcs%02d", 192.0f, 12, 5, BK_IF_NOT_B, FR_IF_B, 1};

static void dr_sword(aos_world *w, aos_player *p, const sword_look *L)
{
	int t = p->timer;
	dr_sword_layers(w, p, L, t < L->start ? 0 : imin((t - L->start) / 3 + 1, L->cap), L == &LK_HS);
}
static void dr_sword1(aos_world *w, aos_player *p) { dr_sword(w, p, &LK_A1); }  /* 0x139 FUN_0075dff0 */
static void dr_sword2(aos_world *w, aos_player *p) { dr_sword(w, p, &LK_B); }   /* 0x13a FUN_0075ebe0 / 0x13d FUN_00760c40 */
static void dr_sword3(aos_world *w, aos_player *p) { dr_sword(w, p, &LK_C); }   /* 0x13b FUN_0075f480 / 0x13e FUN_007614e0 */
static void dr_arrow1(aos_world *w, aos_player *p) { dr_sword(w, p, &LK_A2); }  /* 0x13c FUN_00760070 / 0x13f FUN_00763a80 */
static void dr_hyper_sword(aos_world *w, aos_player *p) { dr_sword(w, p, &LK_HS); } /* 0x141 FUN_00762db0 (+ghost) */
static void dr_sword_break(aos_world *w, aos_player *p) /* 0x140 FUN_007620d0 (+ghost) */
{
	int t = p->timer;
	int f = t < 10 ? 0 : t < 13 ? 1 : t < 16 ? 2 : ((t - 16) / 3) % 3 + 3;
	dr_sword_layers(w, p, &LK_D, f, true);
}

/* ================================================================== */
/* 5. objects                                                          */

/* 2D sprite blit (vtable +0x68: dst rect around a screen point, src rect, packed
 * (bam(−angle), ARGB, blend)). Screen y is down, so −angle on screen = +angle in world. */
static aos_dcmd *blit(int tex, int bank, const float pos[3], float hw, float hh, float angle,
                      int alpha, uint32_t rgb, int blend, int x0, int y0, int x1, int y1)
{
	aos_dcmd *c = aos_draw(tex, bank, pos[0], pos[1], 0);
	c->halfW = hw < 0 ? -hw : hw;
	c->halfH = hh;
	c->flipX = hw < 0; /* mirrored destination rect */
	c->rotZ = aos_rad2bam(angle);
	c->blend = (uint8_t)blend;
	alpha &= 0xff;
	c->rgba = (rgb & 0xffffffu) << 8 | (uint32_t)alpha;
	c->hasSrc = 1;
	c->src[0] = (float)x0;
	c->src[1] = (float)y0;
	c->src[2] = (float)x1;
	c->src[3] = (float)y1;
	return c;
}

/* ---- beam family: spawn FUN_004b52a0, update FUN_004b67a0, draw FUN_004b56e0, hit FUN_004b6570 ---- */

static int beam_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team)
{
	if (id != 100 && id != 0x96 && id != 200 && id != 0xd2 && id != 300 && id != 0x15e)
		return -1;
	int i = aos_beam_new(w, ID, id, pos, angle, owner, age0, team);
	if (i < 0)
		return -1;
	aos_beam *b = &w->obj.beam[i];
	switch (id) {
	case 100:
		b->speed = 21.0f; /* DAT_00814154 (ver >= 0x58a) */
		b->damage = 0xd8;
		b->shape = (aos_shape){24.0f, 8.0f, 0.0f}; /* DAT_00853b8c, DAT_008140d4 */
		b->knockback = 10.0f;                      /* DAT_00813f4c */
		break;
	case 0x96:
		b->speed = 20.0f; /* DAT_008140fc */
		b->damage = 0x90;
		b->stun = 0.7f;   /* DAT_00813f38 */
		b->shape = (aos_shape){24.0f, 8.0f, 0.0f};
		b->knockback = 10.0f;
		break;
	case 200:
		b->speed = 17.0f; /* _DAT_00813f5c */
		b->damage = 0x78;
		b->shape = (aos_shape){0.0f, 0.0f, 30.0f}; /* DAT_008140e8 */
		b->knockback = 5.0f;                       /* ver > 0x5a0: DAT_00813f40 */
		break;
	case 0xd2:
		b->speed = 7.5f; /* DAT_00814150 */
		break;
	default: /* 300, 0x15e */
		b->speed = 16.0f; /* DAT_008140d8 */
		b->destroyOnHit = 0;
		b->active = 0;
		b->knockback = 10.0f;
		break;
	}
	return i;
}

static void beam_move(aos_beam *b, float mul)
{
	b->pos[0] = aos_cos(b->moveAngle) * b->speed * mul + b->pos[0];
	b->pos[1] = aos_sin(b->moveAngle) * b->speed * mul + b->pos[1];
}

/* Particle-trail of the arrows: rand angle in [−π/2, π/2], offset (cos·80 − 20, sin·ry) rotated. */
static void arrow_trail(aos_world *w, aos_beam *b, float ry)
{
	float a = aos_rand_real(w, -HALF_PI, HALF_PI); /* DAT_0081416c, DAT_00814104 */
	float v[2] = {aos_cos(a) * 80.0f - 20.0f, aos_sin(a) * ry};
	aos_vec_rotate(v, b->angle);
	float pt[3] = {v[0] + b->pos[0], v[1] + b->pos[1], 0.0f};
	aos_spawn_spark(w, 0x7da, aos_team_colour(w, b->team, 1), 10, 1, b->angle + AOS_PI, pt, b->ownerId);
}

static bool beam_update(aos_world *w, aos_beam *b)
{
	int32_t col;
	switch (b->id) {
	case 100:
	case 0x96:
		beam_move(b, 1.0f);
		if (!aos_in_arena(b->pos, 10.0f))
			return false;
		col = aos_team_colour(w, b->team, 0);
		fx10(w, b->id == 100 ? 300 : 0x136, col, b->pos, b->angle, b->ownerId, b->team);
		if (b->age == 1) {
			fx0(w, 0x547, col, b->pos, b->angle, b->ownerId, b->team);
			fx0(w, 0x548, col, b->pos, b->angle, b->ownerId, b->team);
		}
		if (b->age < 10)
			aos_spawn_particle(w, b->id == 100 ? 1000 : 0x44c, col, b->pos, b->angle + AOS_PI, b->ownerId, 0, 1);
		break;
	case 200:
		beam_move(b, 1.0f);
		if (!aos_in_arena(b->pos, 30.0f)) /* ver > 0x58a: DAT_008140e8 */
			return false;
		col = aos_team_colour(w, b->team, 0);
		fx10(w, 0x140, col, b->pos, b->angle, b->ownerId, b->team);
		if (b->age % 2 == 0)
			fx0(w, 0x548, col, b->pos, b->angle, b->ownerId, b->team);
		aos_spawn_particle(w, 0x456, col, b->pos, b->angle + AOS_PI, b->ownerId, 0, 1);
		break;
	case 0xd2:
		beam_move(b, aos_in_arena(b->pos, 5.0f) ? 1.0f : 0.1f); /* DAT_00813f40, DAT_00853b10 */
		if (!aos_in_arena(b->pos, 30.0f))
			return false;
		if (b->age % 4 == 0 && b->age < 0x11)
			aos_spawn_hitbox(w, ID, 200, b->pos, b->angle, b->ownerId, 0, b->team);
		col = aos_team_colour(w, b->team, 0);
		fx10(w, 0x140, col, b->pos, b->angle, b->ownerId, b->team);
		if (b->age % 5 == 0)
			fx0(w, 0x547, col, b->pos, b->angle, b->ownerId, b->team);
		aos_spawn_particle(w, 0x456, col, b->pos, b->angle + AOS_PI, b->ownerId, 0, 1);
		if (b->age > 0x14)
			return false;
		break;
	case 300:
		beam_move(b, 1.0f);
		if (b->age > 10)
			b->speed *= 0.95f; /* _DAT_00814124 */
		if (b->age > 0x14)
			b->alpha -= 0.05f; /* DAT_00813ef4 */
		if (b->alpha <= 0.0f)
			return false;
		if (b->age % 2 == 0)
			arrow_trail(w, b, 100.0f); /* DAT_00853b90 */
		if (b->age > 5 && b->age < 0x15 && b->age % 3 == 0)
			aos_spawn_hitbox(w, ID, 300, b->pos, b->angle, b->ownerId, 0, b->team);
		fx10(w, 200, 0, b->pos, b->angle, b->ownerId, b->team); /* literal: every frame */
		break;
	case 0x15e:
		beam_move(b, 1.0f);
		if (b->age > 0x1e)
			b->speed *= 0.95f;
		if (b->age > 0x28)
			b->alpha -= 0.05f;
		if (b->alpha <= 0.0f)
			return false;
		arrow_trail(w, b, 160.0f); /* DAT_00814168 */
		if (b->age > 2 && b->age < 0x29 && b->age % 3 == 0)
			aos_spawn_hitbox(w, ID, 0x136, b->pos, b->angle, b->ownerId, 0, b->team);
		fx10(w, 0xd2, 0, b->pos, b->angle, b->ownerId, b->team);
		break;
	default:
		break;
	}
	return true;
}

static void beam_on_hit(aos_world *w, aos_beam *b)
{
	int32_t col = aos_team_colour(w, b->team, 0);
	switch (b->id) {
	case 100:
		fx0(w, 1000, col, b->pos, b->angle + AOS_PI, b->ownerId, b->team);
		aos_spawn_particle(w, 1000, 0, b->pos, b->angle, b->ownerId, 0, 10);
		aos_spawn_spark(w, 1000, col, 10, 10, b->angle, b->pos, b->ownerId);
		aos_se(w, 0x13);
		break;
	case 0x96:
		fx0(w, 0x3f2, col, b->pos, b->angle + AOS_PI, b->ownerId, b->team);
		aos_spawn_particle(w, 1000, col, b->pos, b->angle, b->ownerId, 0, 5);
		aos_spawn_spark(w, 1000, col, 6, 6, b->angle, b->pos, b->ownerId);
		aos_se(w, 0x13);
		break;
	case 200:
		aos_spawn_beam(w, ID, 0xd2, b->pos, b->angle, b->ownerId, 0, b->team);
		aos_se(w, 0x13);
		break;
	default:
		break;
	}
}

static void beam_draw(aos_world *w, aos_beam *b)
{
	(void)w;
	int bank = team_bank(b->team);
	switch (b->id) {
	case 200:
	case 0xd2: /* 2D sprite 0x14a (sg_rflc) frames 0 and 3 */
		blit(aos_tex("sg_rflc00"), bank, b->pos, 60.0f, 28.0f, b->angle, (int)(128.0f * b->alpha), 0xffffff,
		     AOS_BLEND_ADD_ALPHA, 0, 0, 127, 127);
		blit(aos_tex("sg_rflc03"), bank, b->pos, 60.0f, 28.0f, b->angle, (int)(255.0f * b->alpha), 0xffffff,
		     AOS_BLEND_ALPHA, 0, 0, 127, 127);
		break;
	case 300:
	case 0x15e: { /* 2D sprite 0x147 (sg_moon) frame 1 (B1) then frame 0 (B2) */
		float hw = 192.0f * b->scale, hh = (b->id == 300 ? 192.0f : 256.0f) * b->scale;
		int a = (int)(255.0f * b->alpha);
		blit(aos_tex("sg_moon01"), bank, b->pos, hw, hh, b->angle, a, 0xffffff, AOS_BLEND_ALPHA, 0, 0, 255, 255);
		blit(aos_tex("sg_moon00"), bank, b->pos, hw, hh, b->angle, a, 0xffffff, AOS_BLEND_ADD_ALPHA, 0, 0, 255, 255);
		break;
	}
	default: /* 100 / 0x96: drawn by the per-frame effect 300 / 0x136 */
		break;
	}
}

/* ---- hitbox family: spawn FUN_005eca70, on-hit FUN_005ecfd0 ---- */

static int hitbox_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t zero, int32_t team)
{
	int32_t ht = 3, dmg;
	aos_shape sh = {0, 0, 0};
	float kb = 5.0f; /* DAT_00813f40 */
	switch (id) {
	case 200: ht = 1; dmg = 0x38; sh.radius = 40.0f; break; /* ver > 0x5a0: 56, kb 5 */
	case 100: dmg = 0xb4; sh.halfW = 100.0f; sh.halfH = 80.0f; break;
	case 0x6e: dmg = 0x3c; sh.halfW = 100.0f; sh.halfH = 80.0f; break;
	case 0x78: dmg = 0x78; sh.halfW = 100.0f; sh.halfH = 80.0f; kb = 22.5f; break; /* DAT_0081fa84 */
	case 0x82: dmg = 0x5a; sh.halfW = 80.0f; sh.halfH = 40.0f; break;
	case 0x96: dmg = 0x2d0; sh.halfW = 120.0f; sh.halfH = 60.0f; kb = 15.0f; break;
	case 300: dmg = 0x50; sh.radius = 100.0f; break;
	case 0x136: dmg = 0x78; sh.halfW = 60.0f; sh.halfH = 120.0f; kb = 10.0f; break;
	case 400: dmg = 0x46; kb = 2.5f; break; /* radius set by the laser (DAT_008153e0) */
	case 0x1c2: dmg = 0x32; sh.halfW = 800.0f; sh.halfH = 40.0f; break; /* DAT_008149c8, DAT_008140e0 */
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

/* 0x456 fan: n effects at (k/div)*mul + rand(lo,hi) [+ base] */
static void hitbox_on_hit(aos_world *w, aos_hitbox *h, aos_player *t)
{
	int32_t c0 = aos_team_colour(w, h->team, 0), c1 = aos_team_colour(w, h->team, 1);
	float r, r0;
	switch (h->id) {
	case 200:
		for (int k = -3; k < 3; k++) {
			float base = ((float)k / 3.0f) * AOS_PI;
			r = aos_rand_real(w, -0.25f, 0.25f); /* DAT_0081411c, DAT_00813f24 */
			fx0(w, 0x456, c0, &t->x, base + r, h->ownerId, h->team);
		}
		fx0(w, 0x516, c0, &t->x, 0.0f, h->ownerId, h->team);
		aos_se(w, 0x13);
		team_player(w, h->team)->hitFlag = 1;
		break;
	case 100:
		r = aos_rand_real(w, -AOS_PI, AOS_PI);
		fx0(w, 0x9c4, c1, &t->x, r, h->ownerId, h->team);
		fx0(w, 0xaf0, c1, &t->x, 0.0f, h->ownerId, h->team);
		team_player(w, h->team)->hitFlag = 1;
		aos_se_cd(w, 0x16, 3);
		break;
	case 0x6e:
		r = aos_rand_real(w, -AOS_PI, AOS_PI);
		fx0(w, 0x9c4, c1, &t->x, r, h->ownerId, h->team);
		team_player(w, h->team)->hitFlag = 1;
		aos_se_cd(w, 0x16, 3);
		break;
	case 0x78:
	case 0x96: {
		int sl = h->id == 0x78 ? 0x9c4 : 0x9c5, n = h->id == 0x78 ? 4 : 10;
		r = aos_rand_real(w, -AOS_PI, AOS_PI);
		fx0(w, sl, c1, &t->x, r, h->ownerId, h->team);
		fx0(w, sl, c1, &t->x, r + HALF_PI, h->ownerId, h->team);
		fx0(w, 0xaf0, c1, &t->x, 0.0f, h->ownerId, h->team);
		aos_spawn_particle(w, 1000, c1, &t->x, h->angle, h->ownerId, 0, n);
		aos_spawn_spark(w, 1000, c1, 10, n, h->angle, &t->x, h->ownerId);
		aos_se(w, 0x17);
		aos_screen_blur(w, 10, 0.75f /* DAT_00813f3c */, 1);
		break;
	}
	case 0x82:
		r = aos_rand_real(w, -AOS_PI, AOS_PI);
		fx0(w, 0x9c4, c1, &t->x, r, h->ownerId, h->team);
		fx0(w, 0x547, c1, h->pos, h->angle, h->ownerId, h->team);
		for (int k = -2; k < 3; k++) {
			float a = ((float)k / 2.0f) * 0.75f; /* DAT_00813a4c, DAT_00813f3c */
			float ang = h->angle;
			r = aos_rand_real(w, -0.3f, 0.3f);  /* DAT_0081fa88, DAT_00813f28 */
			fx0(w, 0x456, c1, h->pos, a + ang + r + AOS_PI, h->ownerId, h->team);
		}
		aos_se_cd(w, 0x16, 3);
		break;
	case 300:
	case 0x136:
		r0 = aos_rand_real(w, -AOS_PI, AOS_PI);
		r = aos_rand_real(w, -AOS_PI, AOS_PI);
		fx0(w, h->id == 300 ? 0x9c4 : 0x9c5, c1, &t->x, r, h->ownerId, h->team);
		for (int k = -2; k < 2; k++) {
			float a = ((float)k / 2.0f) * AOS_PI;
			r = aos_rand_real(w, -0.25f, 0.25f);
			fx0(w, 0x456, c1, &t->x, a + r + r0, h->ownerId, h->team);
		}
		aos_se_cd(w, 0x13, 5);
		break;
	case 400:
		aos_se_cd(w, 0x13, 5);
		break;
	case 0x1c2:
		for (int k = -3; k < 3; k++) {
			float a = ((float)k / 3.0f) * AOS_PI;
			r = aos_rand_real(w, -0.25f, 0.25f);
			fx0(w, 0x456, c1, &t->x, a + r, h->ownerId, h->team);
		}
		fx0(w, 0x516, c1, &t->x, 0.0f, h->ownerId, h->team);
		aos_se_cd(w, 0x13, 4);
		break;
	default:
		break;
	}
}

/* ---- missile family: spawn FUN_0067c2b0, update FUN_0067d2a0, draw FUN_0067c440,
 *      on-hit FUN_0067d040, on-killed FUN_0067d170 ---- */

static int missile_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team, int32_t p18)
{
	if (id != 100 && id != 200)
		return -1;
	int i = aos_missile_new(w, ID, id, pos, angle, owner, age0, team, p18);
	if (i < 0)
		return -1;
	aos_missile *m = &w->obj.missile[i];
	float r = id == 100 ? 8.0f : 12.0f; /* DAT_008140d4 / DAT_00813f54 */
	m->growScale = 0;
	m->speed = id == 100 ? 14.0f : -4.0f; /* DAT_00813a54 / DAT_00824aa0 */
	m->hp = 0x3c;
	m->damage = id == 100 ? 0xb4 : 0xfa;
	m->shapeBody.radius = r;
	m->shapeProj.radius = r;
	m->knockback = 10.0f; /* DAT_00813f4c */
	return i;
}

static void missile_smoke(aos_world *w, aos_missile *m)
{
	float pt[3] = {-aos_cos(m->angle) * 20.0f + m->pos[0], -aos_sin(m->angle) * 20.0f + m->pos[1], 0};
	float r = aos_rand_real(w, -AOS_PI, AOS_PI);
	fx0(w, 2000, 0, pt, r, m->ownerId, m->team);
}

static void missile_thruster(aos_world *w, aos_missile *m)
{
	float r = aos_rand_real(w, -0.2f, 0.2f); /* DAT_00817c04, DAT_00853b14 */
	int32_t k = aos_rand_int(w, 0, 100) % 8;
	fx0(w, 0xa28 + k, 0xd, m->pos, r + m->angle, m->ownerId, m->team);
}

static bool missile_update(aos_world *w, aos_missile *m)
{
	if (m->id != 100 && m->id != 200)
		return true;
	if (1.0f <= m->growScale)
		m->growScale = 1.0f;
	else
		m->growScale += 0.2f; /* DAT_00853b14 */
	if (m->id == 100) {
		aos_player *o = aos_opponent(w, team_player(w, m->team)); /* FUN_00780480(team) */
		if (m->age < 0x32) {
			if (2.0f < m->speed)
				m->speed -= 0.5f;
			aos_angle_toward(&m->moveAngle, aos_atan2(o->y - m->pos[1], o->x - m->pos[0]), 0.02f);
		} else if (m->age < 100) {
			if (m->speed < 10.0f)
				m->speed += 0.5f;
			aos_angle_toward(&m->moveAngle, aos_atan2(o->y - m->pos[1], o->x - m->pos[0]), 0.015f);
		}
		m->angle = m->moveAngle;
		if (m->age % 2 == 0)
			missile_smoke(w, m);
		if (m->age < 6 && m->age % 2 == 0)
			missile_thruster(w, m);
		m->pos[0] = aos_cos(m->moveAngle) * m->speed + m->pos[0];
		m->pos[1] = aos_sin(m->moveAngle) * m->speed + m->pos[1];
	} else {
		if (m->age % 10 == 0) {
			float lim = (float)m->age / 50.0f; /* DAT_008140ec */
			if (1.0f < lim)
				lim = 1.0f;
			if ((m->age / 10) % 2 == 0)
				m->moveAngle = m->moveAngle - aos_rand_real(w, 0.0f, lim);
			else
				m->moveAngle = aos_rand_real(w, 0.0f, lim) + m->moveAngle;
			m->angle = m->moveAngle;
		}
		if (m->age % 2 == 0)
			missile_smoke(w, m);
		if (m->speed < 12.0f)
			m->speed += 0.5f;
		m->pos[0] = aos_cos(m->moveAngle) * m->speed + m->pos[0];
		m->pos[1] = aos_sin(m->moveAngle) * m->speed + m->pos[1];
		if (m->age < 10 && m->age % 3 == 0)
			missile_thruster(w, m);
	}
	if (m->age > 0x32 && !aos_in_arena(m->pos, 10.0f)) {
		aos_otab.missileOnHit[m->type](w, m);
		return false;
	}
	return true;
}

static bool missile_explode(aos_world *w, aos_missile *m) /* FUN_0067d040 == FUN_0067d170 */
{
	if (m->id == 100 || m->id == 200) {
		fx0(w, 0x262, 0, m->pos, 0.0f, m->ownerId, m->team);
		fx0(w, 0x514, 1, m->pos, 0.0f, m->ownerId, m->team);
		aos_se(w, 0x14);
	}
	return false;
}

static aos_dcmd *model(int tex, int bank, float x, float y, float S, float sx, float sy, float ang,
                       float a, float r, float g, float b, int blend)
{
	aos_dcmd *c = aos_draw(tex, bank, x, y, 0);
	c->halfW = S * 0.5f * sx;
	c->halfH = S * 0.5f * sy;
	c->rotZ = aos_rad2bam(ang);
	c->rgba = aos_rgba(r, g, b, a);
	c->blend = (uint8_t)blend;
	return c;
}

static void missile_draw(aos_world *w, aos_missile *m)
{
	(void)w;
	if (m->id != 100 && m->id != 200)
		return;
	int bank = team_bank(m->team);
	float ph = ((float)(m->age % 0x32) / 50.0f) * AOS_2PI;
	float g3 = aos_sin(ph) * 0.15f + 0.85f, g4 = aos_sin(ph) * 0.25f + 0.75f;
	float s = m->growScale;
	if (s == 1.0f) { /* exhaust 0x144 (S 48), 42 px behind */
		float v[2] = {42.0f, 0.0f}; /* DAT_00824488 */
		aos_vec_rotate(v, m->angle + AOS_PI);
		float x = v[0] + m->pos[0], y = v[1] + m->pos[1];
		model(aos_tex("sg_bfireb"), bank, x, y, 48.0f, 1, 1, m->angle, 1, 1, 1, 1, AOS_BLEND_ADD_ALPHA);
		float sy = ((m->age / 2) % 2 == 0) ? 0.3f : 1.0f;
		model(aos_tex("sg_bfire"), bank, x, y, 48.0f, 1, sy, m->angle, 1, 1, 1, 1, AOS_BLEND_ADD_ALPHA);
	}
	float c = 1.0f <= s ? 1.0f : 0.0f; /* body is black until fully grown (literal) */
	float sx = 3.0f - s * 2.0f;
	model(aos_texf("sg_msl%02d", (m->age / 3) % 3), bank, m->pos[0], m->pos[1], 64.0f, sx, s, m->angle, 1, c, c, c, AOS_BLEND_ALPHA);
	model(aos_tex("sg_msl03"), bank, m->pos[0], m->pos[1], 64.0f, sx, s, m->angle, g3, 1, 1, 1, AOS_BLEND_ADD_ALPHA);
	model(aos_tex("sg_msl04"), bank, m->pos[0], m->pos[1], 64.0f, sx, s, m->angle, g4, 1, 1, 1, AOS_BLEND_ADD_ALPHA);
}

/* ---- laser family: spawn FUN_006289c0, update FUN_0062a540, draw FUN_00628c00, on-hit FUN_0062a2c0 ---- */

static int laser_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t team, int32_t p18)
{
	if (id != 100 && id != 0x6e && id != 200 && id != 0xd2)
		return -1;
	int i = aos_laser_new(w, ID, id, pos, angle, owner, team, p18);
	if (i < 0)
		return -1;
	aos_laser *l = &w->obj.laser[i];
	l->damage = 0;
	l->hitType = 3;
	l->stun = 1;
	if (id == 100 || id == 0x6e) {
		l->f48 = 200;
		l->blockMode = 2;
		l->knockback = 5.0f; /* DAT_00813f40 */
	} else {
		l->f48 = id == 200 ? 1000 : 0;
		l->blockMode = 1;
		l->knockback = 10.0f; /* DAT_00813f4c */
	}
	return i;
}

static void laser_segments(aos_laser *l, float r, bool arenaCut, aos_world *w)
{
	for (int k = 1; k < 0x1e; k++) {
		float pt[3] = {aos_cos(l->angle) * (float)k * 60.0f + l->origin[0],
		               aos_sin(l->angle) * (float)k * 60.0f + l->origin[1], l->origin[2]};
		aos_shape s = {0, 0, r};
		if (!arenaCut || k < 10 || aos_in_arena(pt, 30.0f)) {
			aos_laser_push_seg(l, &s, pt);
		} else if (l->age > 3 && l->age % 2 == 0) {
			float rr = aos_rand_real(w, -0.15f, 0.15f); /* DAT_00815dc4, DAT_00813f20 */
			int32_t kk = aos_rand_int(w, 0, 100) % 8;
			fx0(w, 0xa82 + kk, 0xd, pt, rr + l->angle, l->ownerId, l->team);
			int h = aos_spawn_hitbox(w, ID, 400, pt, l->angle, l->ownerId, 0, l->team);
			if (h >= 0) {
				w->obj.hitbox[h].shape.radius = (float)((l->age + 5) * 12) * 0.35f; /* DAT_00813f2c */
				if (l->age % 4 == 0)
					w->obj.hitbox[h].damage = 0;
			}
			if (l->age == 0x50)
				fx10(w, 600, 0, pt, 0.0f, l->ownerId, l->team);
			break;
		}
	}
}

static bool laser_update(aos_world *w, aos_laser *l)
{
	aos_player *own = team_player(w, l->team);
	float pt[3];
	switch (l->id) {
	case 100:
	case 0x6e:
		l->nseg = 0;
		if (l->age == 0x10)
			aos_se(w, 0x12);
		if (l->age > 99)
			return false;
		if (own->state == 0x136 && l->age % 3 == 0) {
			pt[0] = aos_cos(l->angle) * 40.0f + l->origin[0]; /* DAT_008140e0 */
			pt[1] = aos_sin(l->angle) * 40.0f + l->origin[1];
			pt[2] = l->origin[2];
			int32_t col = aos_team_colour(w, l->team, 0);
			fx0(w, 0x547, col, pt, l->angle, l->ownerId, l->team);
			fx0(w, 0x548, col, pt, l->angle, l->ownerId, l->team);
		}
		laser_segments(l, (l->age < 0x10 || l->age > 0x3c) ? 0.0f : 50.0f, false, w); /* DAT_008140ec */
		if (l->age % 4 == 0)
			l->damage = own->state == 0x136 ? 0x3c : 0x1e;
		else
			l->damage = 0;
		break;
	case 200:
		l->nseg = 0;
		if (l->age == 0x10)
			aos_se(w, 0x12);
		if (l->age == 0x50)
			aos_se(w, 0x1b);
		if (l->age > 99)
			return false;
		if (own->state == 0x143 && l->age < 0x50 && l->age % 3 == 0) {
			int32_t col = aos_team_colour(w, l->team, 0);
			pt[0] = aos_cos(l->angle) * 40.0f + l->origin[0];
			pt[1] = aos_sin(l->angle) * 40.0f + l->origin[1];
			pt[2] = l->origin[2];
			for (int k = -2; k < 3; k++) {
				float a = l->angle + AOS_PI, b = (float)k * 0.75f; /* DAT_00813f3c */
				float r = aos_rand_real(w, -0.35f, 0.35f);         /* DAT_00814d78, DAT_00813f2c */
				fx10(w, 500, col, pt, a + b + r, l->ownerId, l->team);
			}
		}
		laser_segments(l, (l->age < 0x10 || l->age > 0x3c) ? 0.0f : 50.0f, true, w);
		l->damage = 0;
		break;
	case 0xd2: {
		l->nseg = 0;
		aos_enemy *u = aos_enemy_by_id(w, l->ownerId);
		if (u)
			l->angle = u->angle;
		if (l->age > 0x59)
			return false;
		if (l->age % 5 == 2 && l->age < 0x3d) {
			pt[0] = aos_cos(l->angle) * 30.0f + l->origin[0];
			pt[1] = aos_sin(l->angle) * 30.0f + l->origin[1];
			pt[2] = l->origin[2];
			fx0(w, 0x546, aos_team_colour(w, l->team, 1), pt, l->angle, l->ownerId, l->team);
		}
		if (l->age > 4 && l->age < 0x3d && l->age % 5 == 0) {
			pt[0] = aos_cos(l->angle) * 800.0f + l->origin[0]; /* DAT_008149c8 */
			pt[1] = aos_sin(l->angle) * 800.0f + l->origin[1];
			pt[2] = l->origin[2];
			aos_spawn_hitbox(w, ID, 0x1c2, pt, l->angle, l->ownerId, 0, l->team);
		}
		break;
	}
	default:
		break;
	}
	return true;
}

static void laser_on_hit(aos_world *w, aos_laser *l, int32_t seg, aos_player *t)
{
	(void)t;
	if (l->id == 100 || l->id == 0x6e) {
		/* FUN_00612830 frame meter: omitted */
		int32_t col = aos_team_colour(w, l->team, 0);
		const float *sp = l->seg[seg].pos;
		aos_spawn_particle(w, 1000, 0, sp, l->angle, l->ownerId, 0, 1);
		aos_spawn_spark(w, 1000, col, 10, 1, l->angle, sp, l->ownerId);
		aos_se_cd(w, 0x13, 5);
		for (int k = -5; k < 5; k++) {
			float a = (float)k * 0.35f, ang = l->angle; /* DAT_00813f2c */
			float r = aos_rand_real(w, -0.25f, 0.25f);
			fx10(w, 500, col, sp, a + ang + r, l->ownerId, l->team);
		}
		aos_se_cd(w, 0x13, 4);
		if (l->age % 3 == 0) {
			float r = aos_rand_real(w, -AOS_PI, AOS_PI);
			fx10(w, 0x6e, 0, sp, r, l->ownerId, l->team);
		}
	} else if (l->id == 200) {
		aos_se_cd(w, 0x13, 5);
	}
}

static void laser_draw(aos_world *w, aos_laser *l)
{
	(void)w;
	int bank = team_bank(l->team);
	int t = l->age;
	if (l->id == 100 || l->id == 0x6e || l->id == 200) {
		int n = l->nseg;
		float len = (float)n * 60.0f;
		int sx1 = (int)(((float)n / 30.0f) * 511.0f); /* DAT_008140e8, _DAT_008203b0 */
		float c[3] = {aos_cos(l->angle) * len * 0.5f + l->origin[0], aos_sin(l->angle) * len * 0.5f + l->origin[1], 0};
		float hh = ((t / 2) % 2 == 0) ? 120.0f : 160.0f; /* DAT_00814174 / DAT_00814168 */
		if (t < 0xf) hh *= 0.25f;
		if (t > 0x50) hh = ((float)(100 - t) / 20.0f) * hh;
		int tex = aos_tex("sg_lsr00");
		blit(tex, bank, c, len * 0.5f, hh, l->angle, 0x60, 0xffffff, AOS_BLEND_ALPHA, 0, 0x154, sx1, 499);
		hh *= (t % 2 == 0) ? 0.85f : 0.75f;
		blit(tex, bank, c, len * 0.5f, hh, l->angle, 0xa0, 0xffffff, AOS_BLEND_ADD_ALPHA, 0, 0, sx1, 0x9f);
		hh = 60.0f;
		if (t < 0xf) hh *= 0.25f;
		if (t > 0x50) hh = ((float)(100 - t) / 20.0f) * hh;
		int y0 = ((t / 2) % 3) * 0x3c + 0xa0;
		blit(tex, bank, c, len * 0.5f, hh, l->angle, 0xa0, 0xffffff, AOS_BLEND_ADD_ALPHA, 0, y0, sx1, y0 + 0x3b);
		if (l->id == 200 && n > 0) { /* wall flare at the last segment (literal reads seg[n-1]); 2D sprite
		                               * 0x141 sg_slash, not the beam texture (imul eax,edx,0x141 @0x629914) */
			float r = (float)((t + 5) * ((t / 2) % 2 == 0 ? 0xe : 0xc));
			if (t < 0xf) r *= 0.25f;
			if (t > 0x50) r = ((float)(100 - t) / 20.0f) * r;
			blit(aos_tex("sg_slash"), bank, l->seg[n - 1].pos, r, r, l->angle, 0xff, 0xffffff, AOS_BLEND_ADD_ALPHA, 0, 0x80, 0x7f, 0xff);
		}
	} else if (l->id == 0xd2) {
		float c[3] = {aos_cos(l->angle) * 1000.0f + l->origin[0], aos_sin(l->angle) * 1000.0f + l->origin[1], 0}; /* DAT_00814408 */
		float hh = ((t / 2) % 2 == 0) ? 80.0f : 100.0f; /* DAT_00814164 / DAT_00853b90 */
		if (t < 5) hh = ((float)t / 5.0f) * hh;
		if (t > 0x3c) hh = ((float)(0x5a - t) / 30.0f) * hh;
		int A = t < 0x3d ? 0xff : (int)(((float)(0x5a - t) / 30.0f) * 255.0f);
		int tex = aos_tex("sg_lsr01");
		blit(tex, bank, c, 1000.0f, hh, l->angle, A / 5, 0xffffff, AOS_BLEND_ALPHA, 0, 0x154, 0x1ff, 499);
		hh *= 0.75f;
		blit(tex, bank, c, 1000.0f, hh, l->angle, A / 3, 0xffffff, AOS_BLEND_ADD_ALPHA, 0, 0, 0x1ff, 0x9f);
		hh = 20.0f; /* DAT_008140fc */
		if (t < 5) hh = ((float)t / 5.0f) * hh;
		if (t > 0x3c) hh = ((float)(0x5a - t) / 30.0f) * hh;
		int y0 = ((t / 2) % 3) * 0x3c + 0xa0;
		blit(tex, bank, c, 1000.0f, hh, l->angle, 0xa0, 0xffffff, AOS_BLEND_ADD_ALPHA, 0, y0, 0x1ff, y0 + 0x3b);
		/* muzzle sphere: common beamsphere, cell by colour k1 */
		float r = 50.0f; /* DAT_008140ec */
		if (t < 5) r = ((float)t / 5.0f) * 50.0f;
		if (t > 0x3c) r = ((float)(0x5a - t) / 30.0f) * r;
		float m[3] = {aos_cos(l->angle) * 30.0f + l->origin[0], aos_sin(l->angle) * 30.0f + l->origin[1], 0};
		int32_t v = aos_team_colour(w, l->team, 1);
		int cx = (v % 10) % 2, x0 = cx * 100 + 0x32 + (v / 10) * 0xfa, y = ((v % 10) / 2) * 100;
		blit(aos_tex("beamsphere"), AOS_BANK_COMMON, m, r, r, 0, A / 2, 0xffffff, AOS_BLEND_ADD_ALPHA, x0, y, x0 + 99 - 0x32 + 0x32 - 1, y + 99);
		r += 0.5f;
		blit(aos_tex("beamsphere"), AOS_BANK_COMMON, m, r, r, 0, A, 0xffffff, AOS_BLEND_ADD_ALPHA,
		     (v / 10) * 0xfa, (v % 10) * 0x32, (v / 10) * 0xfa + 0x31, (v % 10) * 0x32 + 0x31);
	}
}

/* ---- enemy family (Accel units): spawn FUN_00550660, update FUN_005507d0,
 *      draw FUN_00550aa0, on-killed FUN_00550a80 ---- */

static int enemy_spawn(aos_world *w, int32_t id, int32_t owner, int32_t team, int32_t p18)
{
	if (id < 100 || id > 0x67)
		return -1;
	int32_t grp = aos_enemy_group_next(w); /* FUN_0055efc0 */
	int i = aos_enemy_new(w, ID, owner, team, p18);
	if (i < 0)
		return -1;
	aos_enemy *e = &w->obj.enemy[i];
	e->serial = grp;   /* +0x04 */
	e->f10 = 100;      /* +0x10 draw variant */
	e->f7c = id;       /* +0x7c update variant */
	e->f84 = 100;      /* +0x84 phase: opening */
	e->hp = e->maxHp = 1000;
	e->active = 0;     /* +0x2c: not hittable */
	e->shape = (aos_shape){0, 0, 0};
	e->scale[0] = 3.0f; /* DAT_00813a50 */
	e->scale[1] = 0.0f;
	e->scale[2] = 1.0f; /* +0x68 brightness/alpha */
	e->mirrorChildren = 0;
	e->motionId = 0;
	e->rehitTimer = 0;
	return i;
}

static bool enemy_update(aos_world *w, aos_enemy *e)
{
	if (e->f7c < 100 || e->f7c > 0x67)
		return true;
	if (e->f84 == 100) {
		e->scale[0] -= 0.2f; /* DAT_00853b14 */
		e->scale[1] += 0.1f; /* DAT_00853b10 */
		if (1.0f <= e->scale[1]) {
			e->scale[0] = e->scale[1] = 1.0f;
			e->f84 = 200;
			e->timer2 = 0;
			aos_spawn_laser(w, ID, 0xd2, e->pos, e->angle, e->id, e->team, e->p18);
			aos_se_cd(w, 0x12, 5);
		}
	} else if (e->f84 == 200) {
		if (e->timer2 > 0x50)
			e->f84 = 900;
		if (e->timer2 > 0x32) {
			aos_player *o = team_player(w, e->team);
			e->f70 *= 0.975f; /* _DAT_00814128 */
			float v[2] = {aos_cos(e->f70) * 50.0f, aos_sin(e->f70) * 100.0f};
			aos_vec_rotate(v, o->faceAngle);
			e->pos[0] = o->x + v[0];
			e->pos[1] = o->y + v[1];
			e->angle2 = e->f70 * 0.5f + o->faceAngle;
			e->angle = e->angle2;
		}
	} else {
		e->scale[0] += 0.2f;
		e->scale[1] -= 0.1f;
		if (e->scale[1] <= 0.0f)
			return false;
	}
	return true;
}

static void enemy_draw(aos_world *w, aos_enemy *e)
{
	(void)w;
	if (e->f10 != 100)
		return;
	float ph = ((float)(e->age % 0x32) / 50.0f) * AOS_2PI;
	float a1 = aos_sin(ph) * 0.25f + 0.75f, a2 = aos_sin(ph) * 0.35f + 0.65f; /* DAT_00813f24/3c, 2c/0081788c */
	int bank = team_bank(e->team);
	float br = e->scale[2];
	model(aos_tex("sg_artl00"), bank, e->pos[0], e->pos[1], 64.0f, e->scale[0], e->scale[1], e->angle, 1, br, br, br, AOS_BLEND_ALPHA);
	model(aos_tex("sg_artl01"), bank, e->pos[0], e->pos[1], 64.0f, e->scale[0], e->scale[1], e->angle, a1 * br, 1, 1, 1, AOS_BLEND_ADD_ALPHA);
	model(aos_tex("sg_artl02"), bank, e->pos[0], e->pos[1], 64.0f, e->scale[0], e->scale[1], e->angle, a2 * br, 1, 1, 1, AOS_BLEND_ADD_ALPHA);
}

static bool enemy_killed(aos_world *w, aos_enemy *e) /* FUN_00550a80 */
{
	(void)w;
	(void)e;
	return false;
}

/* ---- Suguri effect family (type 10): spawn FUN_0050c820, update FUN_0050e9e0, draw FUN_0050cd50 ---- */

static int effect_spawn(aos_world *w, int32_t id, int32_t variant, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team)
{
	switch (id) {
	case 100: case 0x6e: case 200: case 0xd2: case 300: case 500: case 0x136: case 0x140: case 600: break;
	default: return -1;
	}
	float r = 0;
	if (id == 0x6e)
		r = aos_rand_real(w, 0.25f, 0.75f); /* DAT_00813f24, DAT_00813f3c — drawn before the push */
	int i = aos_effect_new(w, ID, id, variant, pos, angle, owner, age0, team);
	if (i < 0)
		return -1;
	aos_effect *e = &w->obj.effect[i];
	e->drawLayer = 1;
	e->scale = 1.0f;
	e->alpha = 1.0f;
	e->moveDir = angle;
	e->rotation = angle;
	e->aux = 0;
	switch (id) {
	case 0x6e:
		e->scale = 0;
		e->aux = 0.175f; /* DAT_00817888 */
		e->aux2 = 1.0f;
		e->moveDir = r;
		break;
	case 200:
	case 0xd2:
	case 0x140:
		e->drawLayer = 0;
		if (id == 0x140)
			e->moveDir = 1.0f;
		break;
	case 0x136:
		e->moveDir = 1.0f;
		break;
	case 500:
		e->pos[0] = aos_cos(angle) * 50.0f + e->pos[0]; /* DAT_008140ec */
		e->pos[1] = aos_sin(angle) * 50.0f + e->pos[1];
		break;
	default:
		break;
	}
	return i;
}

static bool effect_update(aos_world *w, aos_effect *e)
{
	(void)w;
	switch (e->id) {
	case 300:
		if (e->age > 5) e->alpha -= 0.1f;
		return e->alpha > 0.0f;
	case 100:
		e->scale += 0.075f;    /* DAT_00813efc */
		e->alpha -= 0.05f;     /* DAT_00813ef4 */
		e->rotation += 0.125f; /* DAT_00813f1c */
		return e->alpha > 0.0f;
	case 0x6e:
		if (0.0f < e->aux2) e->aux2 -= 0.2f;
		if (e->aux2 < 0.0f) e->aux2 = 0;
		e->aux2 = 0; /* literal */
		if (e->age < 0x14) {
			e->scale += e->aux;
			e->aux *= 0.8f; /* DAT_008145e8 */
			if (e->aux < 0.02f) e->aux = 0.02f;
			return true;
		}
		e->alpha -= 0.1f;
		return e->alpha > 0.0f;
	case 200:
		e->alpha -= 0.05f;
		return e->alpha > 0.0f;
	case 0xd2:
		e->alpha -= 0.025f; /* DAT_00813edc */
		return e->alpha > 0.0f;
	case 500:
		e->scale += 0.1f;
		e->alpha -= 0.1f;
		e->pos[0] = aos_cos(e->moveDir) * 30.0f + e->pos[0];
		e->pos[1] = aos_sin(e->moveDir) * 30.0f + e->pos[1];
		return e->alpha > 0.0f;
	case 0x136:
		if (e->age > 2) {
			e->alpha -= 0.1f;
			e->scale -= 0.075f;
		}
		return e->alpha > 0.0f;
	case 0x140:
		if (e->age > 2) e->scale += 0.05f;
		if (e->age > 5) e->alpha -= 0.1f;
		return e->alpha > 0.0f;
	case 600:
		e->scale += 0.001f;  /* DAT_00814bd8 */
		e->alpha -= 0.025f;
		return e->alpha > 0.0f;
	default:
		return true;
	}
}

/* Rifle-bolt look shared by 300 / 0x136 / 0x140 (beamemit / beamemitb, common art). */
static void bolt_draw(aos_effect *e, float h1, float h2, float h3, float a1, float a2, int fresh)
{
	int v = e->variant, x0 = (v / 8) * 240, y0 = (v % 8) * 64;
	int texb = aos_tex("beamemitb"), texe = aos_tex("beamemit");
	blit(texb, AOS_BANK_COMMON, e->pos, 60.0f, h1, e->rotation, (int)a1, 0xffffff, AOS_BLEND_ALPHA, x0, y0, x0 + 0x77, y0 + 0x3f);
	if (e->age < 2)
		blit(texe, AOS_BANK_COMMON, e->pos, 48.0f, h3, e->rotation, 0xff, 0xffffff, AOS_BLEND_ADD_ALPHA,
		     (v / 8) * 256, y0, (v / 8) * 256 + 0x7f, y0 + 0x3f);
	else
		blit(texb, AOS_BANK_COMMON, e->pos, 40.0f, h2, e->rotation, (int)a2, e->age < fresh ? 0xffffff : 0x0080ff,
		     AOS_BLEND_ADD_ALPHA, x0 + 0x78, y0, x0 + 0xef, y0 + 0x3f);
}

static void effect_draw(aos_world *w, aos_effect *e)
{
	(void)w;
	int bank = team_bank(e->team);
	switch (e->id) {
	case 300:
		bolt_draw(e, 60.0f, 10.0f, 18.0f, 32.0f * e->alpha, 255.0f * e->alpha, 6);
		break;
	case 0x136:
		bolt_draw(e, 50.0f * e->scale, 8.0f * e->scale, 16.0f * e->scale, 32.0f * e->alpha * e->moveDir,
		          255.0f * e->alpha * e->alpha, 6);
		break;
	case 0x140:
		bolt_draw(e, 50.0f * e->scale, 32.0f * e->scale, 40.0f * e->scale, 32.0f * e->alpha * e->moveDir,
		          255.0f * e->alpha * e->alpha, 4);
		break;
	case 100:
		blit(aos_texf("sg_blade%02d", imin(e->age / 4, 3)), bank, e->pos, 48.0f * e->scale, 48.0f * e->scale,
		     e->rotation, (int)(255.0f * e->alpha), 0xffffff, AOS_BLEND_ALPHA, 0, 0, 255, 255);
		break;
	case 0x6e:
		blit(aos_tex("sg_blade02"), bank, e->pos, 400.0f * e->scale, 400.0f * e->scale * e->moveDir, e->rotation,
		     (int)(64.0f * e->alpha), 0xffffff, AOS_BLEND_ALPHA, 0, 0, 255, 255);
		break;
	case 200:
		blit(aos_tex("sg_moon01"), bank, e->pos, 204.0f * e->scale, 204.0f * e->scale, e->rotation,
		     (int)(144.0f * e->alpha), 0xffffff, AOS_BLEND_ADD_ALPHA, 0, 0, 255, 255);
		break;
	case 0xd2:
		blit(aos_tex("sg_moon01"), bank, e->pos, 204.0f * e->scale, 276.0f * e->scale, e->rotation,
		     (int)(144.0f * e->alpha), 0xffffff, AOS_BLEND_ADD_ALPHA, 0, 0, 255, 255);
		break;
	case 500: {
		int v = e->variant, row = (v % 4) * 128;
		int tex = aos_texf("diffuse%d", v / 4);
		float hw = -(144.0f * e->scale), hh = (2.0f - e->scale) * 128.0f; /* mirrored dst rect */
		blit(tex, AOS_BANK_COMMON, e->pos, hw, hh, e->rotation, (int)(24.0f * e->alpha), 0xffffff, AOS_BLEND_ADD_ALPHA, 0, row, 0x7f, row + 0x7f);
		if (e->age >= 0) {
			int x0 = e->age < 4 ? 0x80 : 0x100;
			blit(tex, AOS_BANK_COMMON, e->pos, hw, hh, e->rotation, (int)(48.0f * e->alpha), 0xffffff, AOS_BLEND_ADD_ALPHA, x0, row, x0 + 0x7f, row + 0x7f);
		}
		break;
	}
	case 600: { /* 2D sprite 0x141 sg_slash frame 0 (imul eax,edx,0x141 @0x50e8c9) */
		float a = (e->age % 2 == 0 ? 255.0f : 192.0f) * e->alpha;
		blit(aos_tex("sg_slash"), bank, e->pos, 1600.0f * e->scale, 1600.0f * e->scale, e->rotation, (int)a,
		     0xffffff, AOS_BLEND_ADD_ALPHA, 0, 0x100, 0xff, 0x1ff);
		break;
	}
	default:
		break;
	}
}

/* ================================================================== */
/* 6. registration (Suguri's part of FUN_0049ae40)                     */

void aos_register_suguri(void)
{
	aos_tab.inputA[ID] = input_a;
	aos_tab.inputB[ID] = input_b;
	/* CPU AI (FUN_0075b420 / FUN_0075a930) not ported yet */

	static const struct { int s; aos_update_fn t2; aos_draw_fn t1; } tab[] = {
		{0x128, st_neutral, dr_neutral},          {0x129, st_stub, dr_dash},
		{0x12a, st_stub, dr_ko},                  {0x12b, st_stub, dr_hit},
		{0x12c, st_stub, dr_slide},               {0x132, st_rifle, dr_rifle},
		{0x133, st_rifle_hold, dr_rifle_hold},    {0x134, st_rifle_finisher, dr_rifle_fin},
		{0x135, st_rapid, dr_rapid},              {0x136, st_hyper_rifle, dr_hyper_rifle},
		{0x137, st_homing, dr_slide},             {0x138, st_sidewinder, dr_slide},
		{0x139, st_sword1, dr_sword1},            {0x13a, st_sword2, dr_sword2},
		{0x13b, st_sword3, dr_sword3},            {0x13c, st_arrow1, dr_arrow1},
		{0x13d, st_arrow2, dr_sword2},            {0x13e, st_arrow3, dr_sword3},
		{0x13f, st_hyper_arrow, dr_arrow1},       {0x140, st_sword_break, dr_sword_break},
		{0x141, st_hyper_sword, dr_hyper_sword},  {0x143, st_accel, dr_accel},
	};
	for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++) {
		aos_tab.T2[tab[i].s] = tab[i].t2;
		aos_tab.T1[tab[i].s] = tab[i].t1;
	}

	aos_otab.beamSpawn[ID] = beam_spawn;
	aos_otab.beamUpdate[ID] = beam_update;
	aos_otab.beamDraw[ID] = beam_draw;
	aos_otab.beamOnHit[ID] = beam_on_hit;
	aos_otab.hitboxSpawn[ID] = hitbox_spawn;
	aos_otab.hitboxOnHit[ID] = hitbox_on_hit;
	aos_otab.missileSpawn[ID] = missile_spawn;
	aos_otab.missileUpdate[ID] = missile_update;
	aos_otab.missileDraw[ID] = missile_draw;
	aos_otab.missileOnHit[ID] = missile_explode;
	aos_otab.missileOnKilled[ID] = missile_explode;
	aos_otab.laserSpawn[ID] = laser_spawn;
	aos_otab.laserUpdate[ID] = laser_update;
	aos_otab.laserDraw[ID] = laser_draw;
	aos_otab.laserOnHit[ID] = laser_on_hit;
	aos_otab.enemySpawn[ID] = enemy_spawn;
	aos_otab.enemyUpdate[ID] = enemy_update;
	aos_otab.enemyDraw[ID] = enemy_draw;
	aos_otab.enemyOnKilled[ID] = enemy_killed;
	aos_otab.effectSpawn[ID] = effect_spawn;
	aos_otab.effectUpdate[ID] = effect_update;
	aos_otab.effectDraw[ID] = effect_draw;
}
