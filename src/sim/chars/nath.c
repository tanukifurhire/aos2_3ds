/* Nath (engine id 8, states 0xc8..0xf1) — port of docs/re/nath.md.
 * Every function names its original address. Version 0x5a2 branches only.
 * Object families (all type 8, outside Nath's code block):
 *   beam    spawn 0x4b0750 / update 0x4b0ea0 / draw 0x4b0960 / onHit 0x4b0cc0
 *   missile spawn 0x671750 / update 0x673870 / draw 0x671b90 / onHit 0x6733d0 /
 *           onKilled 0x6735e0 / onAbsorb 0x673790
 *   hitbox  spawn 0x5e6f50 / onHit 0x5e75b0
 *   laser   spawn 0x623170 / update 0x6249a0 / draw 0x623440 / onHit 0x624850
 *   enemy   spawn 0x52d0f0 / update 0x52eaf0 / draw 0x5336b0 / onKilled 0x5335b0
 *   effect  spawn 0x500ae0 / update 0x502f80 / draw 0x500fb0
 * Phase (+0x164) = p->scratch[8] (0 none, 1..3 weapon forms). */
#include <stddef.h>
#include <string.h>

#include "../draw.h"
#include "../fx.h"
#include "../motion.h"
#include "../sim.h"

#define ID AOS_CH_NATH
#define PI 3.1415927f      /* DAT_00853b58 */
#define TWO_PI 6.2831855f  /* DAT_00853b78 */
#define HALF_PI 1.5707964f /* DAT_00814104 */
#define QUARTER_PI 0.7853982f /* DAT_0081fa90 */
#define SIXTH_PI 0.5235988f   /* DAT_00817ddc */

void aos_se_cd(aos_world *w, int id, int cd); /* FUN_007bcaf0 */

/* ------------------------------------------------------------------ */
/* helpers                                                             */

static int32_t phase(const aos_player *p) { return p->scratch[8]; }          /* +0x164 */
static void set_phase(aos_player *p, int32_t v) { p->scratch[8] = (uint8_t)v; }

static aos_player *player_of_team(aos_world *w, int32_t team) /* FUN_00780450 */
{
	return team == 1 ? &w->p[0] : &w->p[1];
}
static aos_player *opp_of_team(aos_world *w, int32_t team) /* FUN_00780480 */
{
	return team == 1 ? &w->p[1] : &w->p[0];
}

static int col(aos_world *w, int32_t team, int k) { return aos_team_colour(w, team, k); }

static int eff0(aos_world *w, int32_t id, int32_t var, const float pos[3], float ang, int32_t owner, int32_t age0, int32_t team)
{
	return aos_spawn_effect(w, 0, id, var, pos, ang, owner, age0, team);
}
static int eff8(aos_world *w, int32_t id, int32_t var, const float pos[3], float ang, int32_t owner, int32_t age0, int32_t team)
{
	return aos_spawn_effect(w, ID, id, var, pos, ang, owner, age0, team);
}

/* enemy +0x74 is a float for Nath (laser-bit spin sign); the engine field is int32. */
static float ef74(const aos_enemy *e) { float f; memcpy(&f, &e->f74, sizeof f); return f; }
static void set_ef74(aos_enemy *e, float f) { memcpy(&e->f74, &f, sizeof f); }

static void aim(aos_world *w, aos_player *p) { p->faceAngle = aos_aim(w, p); }

static void face_x(aos_world *w, aos_player *p) /* "face-x" block */
{
	aos_player *o = aos_opponent(w, p);
	if (o->x < p->x || o->x == p->x)
		p->faceAngle = PI;
	else
		p->faceAngle = 0;
	p->dashAngle = p->faceAngle;
}

/* end-of-state block: dash cancel from C (FUN_00612900 at C omitted), state 0 after E.
 * Returns true when NOT cancelled (the branch where the original applies friction). */
static bool tail(aos_world *w, aos_player *p, int32_t C, int32_t E)
{
	if (p->timer < C || p->D != 3) {
		if (E < p->timer)
			p->state = AOS_ST_NEUTRAL;
		return true;
	}
	aos_dash(w, p, 1);
	return false;
}

static void zero_hurt(aos_player *p)
{
	p->hurtShape[0] = p->hurtShape[1] = p->hurtShape[2] = p->hurtShape[3] = 0;
}

static int find_root(aos_world *w, int32_t team) /* FUN_0070f3e0 */
{
	for (int i = 0; i < w->obj.nenemy; i++) {
		aos_enemy *e = &w->obj.enemy[i];
		if (e->team == team && e->type == 8 && e->f10 == 100)
			return i;
	}
	return -1;
}

static int find_1000(aos_world *w, int32_t team) /* FUN_0070f460 */
{
	for (int i = 0; i < w->obj.nenemy; i++) {
		aos_enemy *e = &w->obj.enemy[i];
		if (e->team == team && e->type == 8 && e->f10 == 1000)
			return i;
	}
	return -1;
}

static int count_bits(aos_world *w, int32_t team) /* FUN_0052ea60 */
{
	int n = 0;
	for (int i = 0; i < w->obj.nenemy; i++)
		if (w->obj.enemy[i].team == team && w->obj.enemy[i].f10 == 5000)
			n++;
	return n;
}

/* root command: root+0x84 = cmd, root+0x80 = 0. The original has no -1 check here
 * (FUN_004ef290(-1) would be out of bounds); the port skips the write instead. */
static void root_cmd(aos_world *w, aos_player *p, int32_t cmd)
{
	int r = find_root(w, p->side);
	if (r < 0)
		return; /* deviation: the original indexes element -1 (only reachable before the weapon exists,
		         * e.g. a hyper during the 0xd2 boot) */
	w->obj.enemy[r].f84 = cmd;
	w->obj.enemy[r].timer2 = 0;
}

static aos_enemy *spawn_enemy(aos_world *w, aos_player *p, int32_t id)
{
	int i = aos_spawn_enemy(w, ID, id, p->objId, p->side, p->color);
	return i >= 0 ? &w->obj.enemy[i] : NULL;
}

/* standard Nath hyper start: FUN_006b1b40(p,10), SE 6, FUN_004e9280(100,.75),
 * FUN_007bec20(100,pos,+0x84,sl), FUN_004d7ac0(20,.5,2), effect 0xfa (fa) */
static void flourish(aos_world *w, aos_player *p, float sl, int fa)
{
	aos_barrier(w, p, 10);
	aos_hyper_flourish_ex(w, p, 6, 100, 0.75f /* DAT_00813f3c */, 100, p->faceAngle, sl, 0x14,
	                      0.5f /* DAT_00853b1c */, 2, fa);
}

static void spawn_flash(aos_world *w, aos_player *p) /* enemy 9999 at pos + SE 0x33 */
{
	aos_enemy *e = spawn_enemy(w, p, 9999);
	aos_se(w, 0x33);
	if (e) {
		e->pos[0] = p->x;
		e->pos[1] = p->y;
		e->pos[2] = p->z;
	}
}

/* grow reset of a hidden extension part */
static void reveal(aos_enemy *e)
{
	e->f84 = 0;
	e->scale[2] = 0;
	e->scale[0] = 3.0f; /* DAT_00813a50 */
	e->scale[1] = 0;
}

/* ------------------------------------------------------------------ */
/* input dispatch                                                      */

static bool go(aos_world *w, aos_player *p, int32_t s)
{
	p->state = s;
	p->timer = 0;
	p->hitFlag = 0;
	aos_tab.T2[p->state](w, p);
	return true;
}

/* phase-dependent targets; a phase without an entry keeps the current state
 * (literal: t and flag are reset and T2 runs again) */
static bool go_ph(aos_world *w, aos_player *p, int32_t s1, int32_t s2, int32_t s3, bool elseIs3)
{
	int32_t ph = phase(p);
	if (ph == 1)
		p->state = s1;
	else if (ph == 2)
		p->state = s2;
	else if (elseIs3 || ph == 3)
		p->state = s3;
	p->timer = 0;
	p->hitFlag = 0;
	aos_tab.T2[p->state](w, p);
	return true;
}

static bool normals(aos_world *w, aos_player *p) /* shared tail of FUN_00715cb0 / FUN_00716350 */
{
	if (p->state == 0 || p->state == 2) {
		if (p->D == 3) {
			aos_dash(w, p, 0);
		} else if (p->Shield == 3 && aos_spend_hyper(p, 0.5f) && aos_may_shield(w, p)) {
			aos_shield(w, p);
		} else {
			if (p->S > 1 && p->B == 3)
				return go(w, p, 0xdf);
			if (p->S > 1 && p->A == 3)
				return go_ph(w, p, 0xe7, 0xe9, 0xea, false);
			if (p->B == 3)
				return go(w, p, phase(p) == 1 ? 0xd8 : 0xda);
			if (p->D > 1 && p->A == 3)
				return go_ph(w, p, 0xdd, 0xe3, 0xe3, false);
			if (p->A == 3)
				return go(w, p, 0xd5);
		}
	}
	return aos_tab.T2[p->state](w, p);
}

static bool input_a(aos_world *w, aos_player *p) /* 0x881330 FUN_00715cb0 */
{
	if (aos_may_hyper(w, p) && p->state != 0xd3 && p->state != 0xd4) { /* ver >= 0x58a */
		if (p->S > 1 && p->B > 1 && p->H == 3 && aos_spend_hyper(p, 3.0f))
			return go(w, p, 0xef);
		if (p->S > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f))
			return go_ph(w, p, 0xd3, 0xd4, 0xec, true);
		if (p->B > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f))
			return go_ph(w, p, 0xd9, 0xee, 0xdc, true);
		if (p->D > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f))
			return go_ph(w, p, 0xe4, 0xe5, 0xe6, true);
		if (p->H == 3 && aos_spend_hyper(p, 1.0f))
			return go_ph(w, p, 0xe8, 0xeb, 0xeb, true);
	}
	return normals(w, p);
}

static bool input_b(aos_world *w, aos_player *p) /* 0x881380 FUN_00716350 */
{
	if (aos_may_hyper(w, p) && p->state != 0xd3 && p->state != 0xd4) { /* ver >= 0x58b */
		if (p->H > 1 && p->Shield == 3 && aos_spend_hyper(p, 3.0f))
			return go(w, p, 0xef);
		if (p->H > 1 && p->S == 3 && aos_spend_hyper(p, 1.0f))
			return go_ph(w, p, 0xd3, 0xd4, 0xec, true);
		if (p->H > 1 && p->B == 3 && aos_spend_hyper(p, 1.0f))
			return go_ph(w, p, 0xd9, 0xee, 0xdc, true);
		if (p->H > 1 && p->D == 3 && aos_spend_hyper(p, 1.0f))
			return go_ph(w, p, 0xe4, 0xe5, 0xe6, true);
		if (p->H > 1 && p->A == 3 && aos_spend_hyper(p, 1.0f))
			return go_ph(w, p, 0xe8, 0xeb, 0xeb, true);
	}
	return normals(w, p);
}

/* ------------------------------------------------------------------ */
/* states (T2)                                                         */

static bool st_neutral(aos_world *w, aos_player *p) /* 0xc8 FUN_0070fdd0 */
{
	(void)w;
	float v = aos_tab.stats[p->id].walk;
	if (p->up > 1) p->y += v;
	if (p->down > 1) p->y -= v;
	if (p->left > 1) p->x -= v;
	if (p->right > 1) p->x += v;
	p->faceAngle = p->facingRight == 1 ? 0.0f : PI;
	p->friction *= 0.5f;
	return true;
}

static bool st_stub(aos_world *w, aos_player *p) /* 0xc9/0xcb/0xcc FUN_0070fee0/0070fef0/0070ff00 */
{
	(void)w;
	(void)p;
	return true;
}

static bool st_ko_hook(aos_world *w, aos_player *p) /* 0xca FUN_0070ff10 (called by common KO) */
{
	if (p->timer == 0) {
		for (int i = 0; i < w->obj.nenemy; i++) {
			aos_enemy *e = &w->obj.enemy[i];
			if (e->ownerId != p->objId)
				continue;
			switch (e->f7c) {
			case 200: case 300: case 0x15e: case 400: case 500: case 600:
				e->f84 = 999;
				break;
			case 1000: case 700: case 800: case 900:
				e->f84 = 900;
				e->scale[2] = 0;
				break;
			default:
				break;
			}
		}
		set_phase(p, 0);
	}
	return true;
}

static void invuln(aos_player *p)
{
	zero_hurt(p);
	p->hittable = 0;
}

static bool st_boot(aos_world *w, aos_player *p) /* 0xd2 FUN_00710290 (initial state) */
{
	if (p->timer == 0x28)
		spawn_flash(w, p);
	if (p->timer == 0x32)
		spawn_enemy(w, p, 100);
	if (0x46 < p->timer) {
		set_phase(p, 1);
		p->state = 0;
	}
	invuln(p);
	return true;
}

static void ext_start(aos_world *w, aos_player *p)
{
	face_x(w, p);
	p->speed = 0;
	aos_barrier(w, p, 10);
	aos_hyper_flourish_ex(w, p, 6, 100, 0.75f, 100, p->faceAngle, 0, 0x14, 0.5f, 2, 0);
}

static bool st_ext1(aos_world *w, aos_player *p) /* 0xd3 FUN_007103a0 Extension I */
{
	if (p->timer == 0) {
		ext_start(w, p);
		for (int i = 0; i < w->obj.nenemy; i++) {
			aos_enemy *e = &w->obj.enemy[i];
			if (e->ownerId == p->objId && e->f7c == 1000 && e->f84 == 1000)
				e->f84 = 900;
		}
		spawn_flash(w, p);
	}
	if (p->timer == 10) {
		for (int i = 0; i < w->obj.nenemy; i++) {
			aos_enemy *e = &w->obj.enemy[i];
			if (e->ownerId == p->objId && e->f10 == 1000)
				reveal(e);
		}
	}
	if (0x46 < p->timer) {
		set_phase(p, 2);
		p->state = 0;
	}
	invuln(p);
	return true;
}

static bool st_ext2(aos_world *w, aos_player *p) /* 0xd4 FUN_007106c0 Extension II */
{
	if (p->timer == 0) {
		ext_start(w, p);
		spawn_flash(w, p);
	}
	if (p->timer == 10) {
		for (int i = 0; i < w->obj.nenemy; i++) {
			aos_enemy *e = &w->obj.enemy[i];
			if (e->ownerId == p->objId && e->f7c == 700 && e->f84 == 1000)
				reveal(e);
			if (e->ownerId == p->objId && e->f7c == 800)
				reveal(e);
		}
	}
	if (0x46 < p->timer) {
		set_phase(p, 3);
		p->state = 0;
	}
	invuln(p);
	return true;
}

static bool st_spread(aos_world *w, aos_player *p) /* 0xd5 FUN_00710a00 Spread Beam */
{
	if (p->timer == 0)
		aim(w, p);
	if (9 < p->timer && p->timer < 0x1f && p->timer % 2 == 0 && aos_use_gauge(p, 0xd5)) {
		float r = aos_rand_real(w, -0.5f, 0.5f);
		aos_spawn_beam(w, ID, 100, &p->x, r + p->faceAngle, p->objId, 0, p->side);
		aos_se_cd(w, 0xe, 4);
	}
	if (p->timer == 5)
		root_cmd(w, p, 100);
	if (tail(w, p, 0x19, 0x23))
		p->friction *= 0.5f;
	return true;
}

/* root command at t==5 + standard 25/35 tail with friction */
static bool cmd_state(aos_world *w, aos_player *p, int32_t cmd, int32_t C, int32_t E, bool fric)
{
	if (p->timer == 0)
		aim(w, p);
	if (p->timer == 5)
		root_cmd(w, p, cmd);
	if (tail(w, p, C, E) && fric)
		p->friction *= 0.5f;
	return true;
}

static bool st_homing_bombing(aos_world *w, aos_player *p) /* 0xd8 FUN_00710be0 */
{
	return cmd_state(w, p, 200, 0x19, 0x23, true);
}

static bool st_carpet(aos_world *w, aos_player *p) /* 0xd9 FUN_00710cd0 Carpet Bombing */
{
	if (p->timer == 0) {
		aim(w, p);
		flourish(w, p, 0, 1);
	}
	return cmd_state(w, p, 0xd2, 0x32, 0x50, false);
}

static bool st_b23(aos_world *w, aos_player *p) /* 0xda FUN_00710e50 */
{
	int32_t C = 0x19, E = 0x23;
	if (phase(p) == 2) { C = 0x1e; E = 0x28; }
	else if (phase(p) == 3) { C = 0x21; E = 0x29; }
	return cmd_state(w, p, 0xfa, C, E, true);
}

static bool st_b_h3(aos_world *w, aos_player *p) /* 0xdc FUN_00710fc0 */
{
	if (p->timer == 0) {
		aim(w, p);
		flourish(w, p, QUARTER_PI, 1);
	}
	return cmd_state(w, p, 0x104, 0x19, 0x23, true);
}

static bool st_reflecting(aos_world *w, aos_player *p) /* 0xdd FUN_00711170 Reflecting Beam */
{
	if (p->timer == 0)
		aim(w, p);
	if (p->timer == 5)
		root_cmd(w, p, 300);
	if (p->timer == 0xf && aos_use_gauge(p, 0xdd)) {
		aos_spawn_beam(w, ID, 200, &p->x, p->faceAngle - QUARTER_PI, p->objId, 0, p->side);
		aos_spawn_beam(w, ID, 200, &p->x, p->faceAngle + QUARTER_PI, p->objId, 0, p->side);
		aos_se(w, 0x11);
	}
	if (tail(w, p, 0x19, 0x23))
		p->friction *= 0.5f;
	return true;
}

static bool st_reflector(aos_world *w, aos_player *p) /* 0xdf FUN_00711330 Beam Reflector */
{
	return cmd_state(w, p, 400, 0x19, 0x23, true);
}

/* laser bit (enemy 5000) setup shared by 0xe3/0xe4/0xe5/0xe6 */
static void bit_spawn(aos_world *w, aos_player *p, float tx, float ty, float a)
{
	aos_angle_normalize(&a);
	aos_enemy *e = spawn_enemy(w, p, 5000);
	if (e) {
		e->f84 = 100;
		e->f7c = 5000;
		e->pos[0] = aos_cos(a) * 100.0f + p->x; /* DAT_00853b90 */
		e->pos[1] = aos_sin(a) * 100.0f + p->y;
		e->angle2 = aos_atan2(ty - e->pos[1], tx - e->pos[0]);
		e->angle = a;
		float tp[3] = {tx, ty, 0};
		float d[2] = {tp[0] - e->pos[0], tp[1] - e->pos[1]};
		e->f70 = aos_sqrt(d[0] * d[0] + d[1] * d[1]) / 50.0f; /* FUN_005f1c30 / DAT_008140ec */
		set_ef74(e, e->angle <= 0.0f ? -1.0f : 1.0f);
	}
	aos_se_cd(w, 0x2f, 4);
}

static bool st_bits(aos_world *w, aos_player *p) /* 0xe3 FUN_00712370 (D+A ph2/3) */
{
	if (p->timer == 0)
		aim(w, p);
	if (p->timer == 5)
		root_cmd(w, p, 500);
	for (int k = 0; k < 4; k++) {
		if (p->timer != k * 4 + 0xc)
			continue;
		if (3 < count_bits(w, p->side))
			break;
		(void)aos_use_gauge(p, 0xe3); /* result ignored */
		aos_player *o = opp_of_team(w, p->side);
		float r, th, tx, ty;
		if (phase(p) == 3) {
			r = aos_rand_real(w, 80.0f, 320.0f);
			float base = ((float)k / 4.0f) * TWO_PI + p->faceAngle;
			th = base + aos_rand_real(w, -0.5f, 0.5f);
			tx = aos_cos(th) * r + o->x;
			ty = aos_sin(th) * r + o->y;
		} else {
			r = aos_rand_real(w, 80.0f, 240.0f);
			float base = ((float)k / 4.0f) * TWO_PI + p->faceAngle;
			th = base + aos_rand_real(w, -0.5f, 0.5f);
			tx = aos_cos(th) * r + p->x;
			ty = aos_sin(th) * r + p->y;
		}
		bit_spawn(w, p, tx, ty, aos_atan2(o->y - ty, o->x - tx) + PI);
	}
	if (tail(w, p, 0x1e, 0x28))
		p->friction *= 0.5f;
	return true;
}

static bool st_bits_h1(aos_world *w, aos_player *p) /* 0xe4 FUN_00712930 Laser Bit Deployment */
{
	if (p->timer == 0) {
		aim(w, p);
		flourish(w, p, QUARTER_PI, 1);
	}
	for (int k = 0; k < 8; k++) {
		if (p->timer != k * 3 + 10)
			continue;
		float v[2];
		v[0] = -aos_rand_real(w, 50.0f, 100.0f);
		if (k % 2 == 0)
			v[1] = -aos_rand_real(w, 10.0f, 200.0f);
		else
			v[1] = aos_rand_real(w, 10.0f, 200.0f);
		aos_vec_rotate(v, p->faceAngle);
		v[0] += p->x;
		v[1] += p->y;
		bit_spawn(w, p, v[0], v[1], p->faceAngle + PI);
	}
	tail(w, p, 0x28, 0x46);
	return true;
}

static bool st_bits_h2(aos_world *w, aos_player *p) /* 0xe5 FUN_00712e80 */
{
	if (p->timer == 0) {
		aim(w, p);
		flourish(w, p, QUARTER_PI, 1);
	}
	if (p->timer == 5)
		root_cmd(w, p, 500);
	for (int k = 0; k < 8; k++) {
		if (p->timer != k * 3 + 5)
			continue;
		aos_player *o = opp_of_team(w, p->side);
		float r = aos_rand_real(w, 120.0f, 320.0f);
		float base = ((float)k / 8.0f) * TWO_PI + p->faceAngle;
		float th = base + aos_rand_real(w, -0.5f, 0.5f);
		float tx = aos_cos(th) * r + p->x, ty = aos_sin(th) * r + p->y;
		float a = aos_atan2(o->y - ty, o->x - tx) + PI;
		a = a + aos_rand_real(w, -0.25f, 0.25f);
		bit_spawn(w, p, tx, ty, a);
	}
	tail(w, p, 0x28, 0x46);
	return true;
}

static bool st_bits_h3(aos_world *w, aos_player *p) /* 0xe6 FUN_007133f0 */
{
	if (p->timer == 0) {
		aim(w, p);
		flourish(w, p, 0, 1);
	}
	if (p->timer == 5)
		root_cmd(w, p, 500);
	for (int k = 0; k < 8; k++) {
		if (p->timer != k * 4 + 5)
			continue;
		aos_player *o = opp_of_team(w, p->side);
		float r = aos_rand_real(w, 120.0f, 360.0f);
		float base = ((float)k / 8.0f) * TWO_PI + p->faceAngle;
		float th = base + aos_rand_real(w, -0.75f, 0.75f);
		float tx = aos_cos(th) * r + o->x, ty = aos_sin(th) * r + o->y;
		bit_spawn(w, p, tx, ty, aos_atan2(o->y - ty, o->x - tx) + PI);
	}
	tail(w, p, 0x28, 0x46);
	return true;
}

static void fist_spawn(aos_world *w, aos_player *p, int32_t id, bool sideA)
{
	aos_player *o = opp_of_team(w, p->side);
	aos_enemy *e = spawn_enemy(w, p, id);
	if (!e)
		return;
	float s;
	if (aos_fabs(p->faceAngle) <= HALF_PI)
		s = sideA ? p->faceAngle + HALF_PI : p->faceAngle - HALF_PI;
	else
		s = sideA ? p->faceAngle - HALF_PI : p->faceAngle + HALF_PI;
	e->pos[0] = aos_cos(s) * 50.0f + p->x; /* DAT_008140ec */
	e->pos[1] = aos_sin(s) * 50.0f + p->y;
	e->angle = aos_atan2(o->y - e->pos[1], o->x - e->pos[0]);
	e->angle2 = e->angle;
}

static bool st_metal_fist(aos_world *w, aos_player *p) /* 0xe7 FUN_00711420 Metal Fist */
{
	if (p->timer == 0)
		aim(w, p);
	if (p->timer == 5)
		root_cmd(w, p, 600);
	if (p->timer == 5 && aos_use_gauge(p, 0xe7))
		fist_spawn(w, p, 0x13ed, true);  /* |aim| <= π/2 → aim+π/2 */
	if (p->timer == 10 && aos_use_gauge(p, 0xe7))
		fist_spawn(w, p, 0x13ec, false); /* other side */
	tail(w, p, 0x19, 0x23);
	return true;
}

static bool st_pounding(aos_world *w, aos_player *p) /* 0xe8 FUN_00711a20 Metal Pounding */
{
	if (p->timer == 0) {
		aim(w, p);
		flourish(w, p, QUARTER_PI, 1);
	}
	if (p->timer == 1)
		root_cmd(w, p, 0x262);
	if (4 < p->timer && p->timer < 0x2e && p->timer % 5 == 0) {
		int32_t id = 0x13f6 + ((p->timer - 5) / 5) % 2;
		aos_enemy *e = spawn_enemy(w, p, id);
		float d = aos_rand_real(w, 20.0f, 100.0f);
		float s;
		if (HALF_PI <= aos_fabs(p->faceAngle))
			s = p->timer % 10 == 0 ? p->faceAngle - HALF_PI : p->faceAngle + HALF_PI;
		else
			s = p->timer % 10 == 0 ? p->faceAngle + HALF_PI : p->faceAngle - HALF_PI;
		if (e) {
			e->pos[0] = aos_cos(s) * d + p->x;
			e->pos[1] = aos_sin(s) * d + p->y;
			e->angle = p->faceAngle;
			e->angle2 = e->angle;
		}
	}
	if (p->timer == 0x32) {
		aos_player *o = opp_of_team(w, p->side);
		float P[3];
		P[0] = aos_cos(p->faceAngle) * -100.0f + o->x;
		P[1] = aos_sin(p->faceAngle) * -100.0f + o->y;
		P[2] = 0; /* local_c (uninitialised in the original) */
		float dx = P[0] - p->x, dy = P[1] - p->y;
		if (aos_sqrt(dx * dx + dy * dy) <= 160.0f) {
			aos_enemy *e = spawn_enemy(w, p, 0x1400);
			if (e) {
				e->pos[0] = P[0];
				e->pos[1] = P[1];
				e->pos[2] = P[2];
				e->angle = p->faceAngle;
				e->angle2 = e->angle;
			}
		}
	}
	tail(w, p, 0x30, 0x44);
	return true;
}

static void lunge(aos_world *w, aos_player *p)
{
	float s = aos_dist(w, p) / 16.0f;
	if (12.0f <= s)
		s = 12.0f;
	p->dashAngle = p->faceAngle;
	p->moveAngle = p->faceAngle;
	p->speed = s;
}

static void dash_sparks(aos_world *w, aos_player *p) /* speed >= 10: spark effect 0xa28+n */
{
	if (10.0f <= p->speed) {
		float r = aos_rand_real(w, -0.25f, 0.25f);
		int32_t n = aos_rand_int(w, 0, 100) % 8;
		eff0(w, 0xa28 + n, 0xd, &p->x, r + p->faceAngle, p->objId, 0, p->side);
	}
}

static bool st_punch2(aos_world *w, aos_player *p) /* 0xe9 FUN_00712150 */
{
	if (p->timer == 0) {
		aim(w, p);
		lunge(w, p);
	}
	dash_sparks(w, p);
	if (p->timer == 5)
		aos_se(w, 0x20);
	if (p->timer == 5 && aos_use_gauge(p, 0xe9))
		root_cmd(w, p, 0x28a);
	tail(w, p, 0x2d, 0x46);
	return true;
}

static bool st_hand_laser(aos_world *w, aos_player *p) /* 0xea FUN_00713920 */
{
	if (p->timer == 0)
		aim(w, p);
	if (p->timer == 1)
		root_cmd(w, p, 700);
	tail(w, p, 0x28, 0x3c);
	return true;
}

static bool st_hyper_laser(aos_world *w, aos_player *p) /* 0xeb FUN_00713a00 */
{
	if (p->timer == 0) {
		aim(w, p);
		p->speed = 0;
		flourish(w, p, QUARTER_PI, 1);
	}
	if (p->timer == 1)
		root_cmd(w, p, 0x2c6);
	tail(w, p, 0x3c, 0x50);
	return true;
}

static bool st_grab(aos_world *w, aos_player *p) /* 0xec FUN_00713ba0 */
{
	if (p->timer == 0) {
		aim(w, p);
		lunge(w, p);
		flourish(w, p, QUARTER_PI, 1);
	}
	dash_sparks(w, p);
	if (p->timer == 1)
		aos_se(w, 0x20);
	if (p->timer == 1)
		root_cmd(w, p, 0x2ee);
	if (p->timer < 10 || 0x1e < p->timer || p->hitFlag != 1) {
		tail(w, p, 0x3c, 0x50);
	} else {
		aos_player *o = opp_of_team(w, p->side);
		aos_camera_focus(w, 0x32, o->objId, &o->x, 0.75f);
		aos_screen_dark(w, 100, 0.85f);  /* DAT_00814120 */
		aos_screen_blur(w, 100, 0.6f, 2); /* DAT_00813f34 */
		aos_speedline(w, 100, &p->x, 0, 0);
		go(w, p, 0xed);
	}
	return true;
}

static bool st_grab_hold(aos_world *w, aos_player *p) /* 0xed FUN_00713f60 */
{
	tail(w, p, 0x3c, 0x50);
	return true;
}

static bool st_mines(aos_world *w, aos_player *p) /* 0xee FUN_00713fc0 */
{
	if (p->timer == 0) {
		aim(w, p);
		p->dashAngle = p->faceAngle;
		p->moveAngle = p->faceAngle;
		p->speed = 0;
		flourish(w, p, QUARTER_PI, 1);
	}
	if (p->timer == 1)
		root_cmd(w, p, 800);
	if (p->timer == 0x14) {
		int h = find_1000(w, p->side);
		if (h != -1) {
			const float da[6] = {SIXTH_PI, -SIXTH_PI, -HALF_PI, 0, 0, 0};
			for (int k = 0; k < 6; k++) {
				aos_enemy *e = spawn_enemy(w, p, 0x1450);
				float a;
				switch (k) {
				case 0: case 1: case 2: a = p->faceAngle + da[k]; break;
				case 3: a = (p->faceAngle - PI) + SIXTH_PI; break;
				case 4: a = (p->faceAngle + PI) - SIXTH_PI; break;
				default: a = (p->faceAngle + PI) - HALF_PI; break;
				}
				aos_angle_normalize(&a);
				aos_enemy *H = &w->obj.enemy[h]; /* index is stable: spawns append */
				if (e) {
					e->pos[0] = aos_cos(a) * 50.0f + H->pos[0];
					e->pos[1] = aos_sin(a) * 50.0f + H->pos[1];
					e->angle2 = a;
					e->angle = a;
				}
			}
		}
	}
	tail(w, p, 0x1e, 0x3c);
	return true;
}

/* re-placement for a Bird Form pass (0xef t=81 / 0xf0 between passes) */
static void bird_place(aos_world *w, aos_player *p)
{
	aos_player *o = opp_of_team(w, p->side);
	if (HALF_PI <= aos_fabs(p->faceAngle)) { /* 0xef: >= ; 0xf0 uses <= (see caller) */
		p->faceAngle = PI;
		p->dashAngle = PI;
		p->y = o->y;
		p->x = 1500.0f; /* DAT_008149d4 */
	} else {
		p->faceAngle = 0;
		p->dashAngle = 0;
		p->y = o->y;
		p->x = -1500.0f; /* DAT_00814a24 */
	}
}

static void bird_updown(aos_world *w, aos_player *p)
{
	aos_player *o = opp_of_team(w, p->side);
	if (p->up > 1) {
		p->y = 400.0f; /* DAT_008145f4 */
		p->faceAngle = aos_atan2(o->y - p->y, o->x - p->x);
		p->dashAngle = p->faceAngle;
	}
	if (p->down > 1) {
		p->y = -400.0f; /* DAT_00815bc8 */
		p->faceAngle = aos_atan2(o->y - p->y, o->x - p->x);
		p->dashAngle = p->faceAngle;
	}
}

static bool st_bird(aos_world *w, aos_player *p) /* 0xef FUN_007148f0 Bird Form */
{
	if (p->timer == 0) {
		face_x(w, p);
		p->speed = 0;
		aos_barrier(w, p, 0x14);
		aos_hyper_flourish_ex(w, p, 7, 700, 0.85f /* DAT_00814120 */, 100, p->faceAngle, 0, 0x14, 0.5f, 2, 1);
		spawn_flash(w, p);
	}
	if (p->timer == 0)
		root_cmd(w, p, 900);
	if (p->timer == 10) {
		for (int i = 0; i < w->obj.nenemy; i++) {
			/* FUN_00612830 per iteration: training only */
			aos_enemy *e = &w->obj.enemy[i];
			if (e->ownerId != p->objId)
				continue;
			if (e->f7c == 1000 && e->f84 == 1000)
				reveal(e);
			if (e->f7c == 700 && e->f84 == 1000)
				reveal(e);
			if (e->f7c == 800 && e->f84 == 1000)
				reveal(e);
			if (e->f7c == 900 && e->f84 == 1000) {
				reveal(e);
				e->motionFrame = 0;
			}
		}
	}
	if (p->timer == 0x1e) {
		aos_player *o = opp_of_team(w, p->side);
		aos_camera_focus(w, 0x2a8, o->objId, &o->x, 1.0f);
		aos_se(w, 0x32);
	}
	if (0x1e < p->timer) {
		p->y = (float)(p->timer - 0x1e) + p->y;
		if (aos_fabs(p->faceAngle) <= HALF_PI)
			p->x = p->x - (float)(p->timer - 0x1e) * 0.5f;
		else
			p->x = (float)(p->timer - 0x1e) * 0.5f + p->x;
	}
	if (p->timer < 0x51) {
		zero_hurt(p);
		p->clampArena = 0;
	} else {
		p->state = 0xf0;
		p->timer = 0;
		aos_barrier(w, p, 0x14);
		bird_place(w, p);
		bird_updown(w, p);
		root_cmd(w, p, 0x38e);
	}
	return true;
}

static bool st_bird_pass(aos_world *w, aos_player *p) /* 0xf0 FUN_00715190 */
{
	int32_t u = p->timer % 1000;
	p->x = aos_cos(p->faceAngle) * (float)u * 0.25f + p->x;
	p->y = aos_sin(p->faceAngle) * (float)u * 0.25f + p->y;
	if (u < 0x97) {
		for (int k = 0; k < 2; k++) {
			float a = aos_rand_real(w, -HALF_PI, HALF_PI);
			float v[2] = {aos_cos(a) * 60.0f - 20.0f, aos_sin(a) * 160.0f};
			aos_vec_rotate(v, p->faceAngle);
			float pos[3] = {v[0] + p->x, v[1] + p->y, 0};
			aos_spawn_spark(w, 0x7da, aos_team_colour(w, p->side, 1), 10, 1, p->faceAngle + PI, pos, p->objId);
		}
	}
	if (0x3b < u && u < 0x79 && u % 3 == 0) {
		int m = aos_spawn_missile(w, ID, 0x6e, &p->x, p->faceAngle, p->objId, 0, p->side, p->color);
		float r = aos_rand_real(w, 1.0f, 10.0f);
		if ((p->timer / 3) % 2 == 0) {
			if (m >= 0)
				w->obj.missile[m].speed = r;
		} else if (m >= 0) {
			w->obj.missile[m].speed = -r;
		}
		aos_se_cd(w, 0x22, 6);
	}
	if (p->timer % 3 == 0) {
		float r = aos_rand_real(w, -0.25f, 0.25f);
		aos_spawn_hitbox(w, ID, 600, &p->x, r + p->faceAngle, p->objId, 0, p->side);
	}
	if (0xb3 < p->timer % 1000) {
		aos_player *o = opp_of_team(w, p->side);
		p->timer = (p->timer / 1000) * 1000 + 1000;
		if (aos_fabs(p->faceAngle) <= HALF_PI) {
			p->faceAngle = PI;
			p->dashAngle = PI;
			p->y = o->y;
			p->x = 1500.0f;
		} else {
			p->faceAngle = 0;
			p->dashAngle = 0;
			p->y = o->y;
			p->x = -1500.0f;
		}
		bird_updown(w, p);
	}
	if (p->timer < 3000) {
		zero_hurt(p);
		p->hittable = 0;
		aos_barrier(w, p, 0x14);
		p->clampArena = 0;
	} else {
		aos_player *o = opp_of_team(w, p->side);
		p->state = 0xf1;
		p->timer = 0;
		aos_se(w, 0x32);
		if (o->x < p->x || o->x == p->x) {
			p->faceAngle = PI;
			p->dashAngle = p->faceAngle;
			p->y = 1500.0f;
			p->x = 300.0f; /* DAT_008145f0 */
		} else {
			p->faceAngle = 0;
			p->dashAngle = p->faceAngle;
			p->y = 1500.0f;
			p->x = -300.0f; /* DAT_00815f44 */
		}
		root_cmd(w, p, 0x38e);
	}
	return true;
}

static bool st_bird_land(aos_world *w, aos_player *p) /* 0xf1 FUN_00715920 */
{
	if (p->timer < 0x65)
		p->y = p->y * 0.95f; /* _DAT_00814124 */
	if (0x17 < p->timer && p->timer < 0x49 && p->timer % 3 == 0) {
		float pos[3] = {p->x, -120.0f /* DAT_0081525c */, 0};
		float r = aos_rand_real(w, -0.15f, 0.15f);
		int32_t n = aos_rand_int(w, 0, 100) % 8;
		int e = eff0(w, 0xa82 + n, 0xd, pos, r + HALF_PI, p->objId, 0, p->side);
		if (e >= 0)
			w->obj.effect[e].aux2 = (float)(p->timer - 0x19) / 100.0f + 0.25f;
	}
	if (p->timer == 100) {
		for (int i = 0; i < w->obj.nenemy; i++) {
			aos_enemy *e = &w->obj.enemy[i];
			if (e->ownerId != p->objId)
				continue;
			if (e->f7c == 1000 || e->f7c == 700 || e->f7c == 800 || e->f7c == 900) {
				e->f84 = 900;
				e->scale[2] = 0;
			}
		}
		p->y = 0;
		set_phase(p, 1);
		aos_barrier(w, p, 0x14);
	}
	if (0x82 < p->timer)
		p->state = 0;
	if (p->timer < 100) {
		zero_hurt(p);
		p->hittable = 0;
	}
	p->clampArena = 0;
	return true;
}

/* ------------------------------------------------------------------ */
/* enemy family (type 8)                                               */

typedef struct {
	int32_t f10, kind, cmd, motion, parent; /* parent: index into init order, -1 root */
	float scale, ox, oy, px;                 /* offset (+0xc8,+0xcc), pivot x (+0xbc) */
} part_def;

/* weapon parts in INIT order (FUN_0055ee70 calls → id order) */
static const part_def k_parts[15] = {
	/* 0 f4  */ {100, 100, 0, 0, -1, 1, 0, 0, 0},
	/* 1 cf0 */ {200, 200, 0, 3, 0, 1, 0, 0, 0},
	/* 2 67c */ {300, 300, 0, 0xe, 0, 1, 10.0f, -36.0f, -90.0f},
	/* 3 4a4 */ {0x15e, 300, 0, 0xf, 0, 1, 0, -36.0f, -82.0f},
	/* 4 590 */ {400, 400, 0, 0xd, 0, 1, 10.0f, -40.0f, -60.0f},
	/* 5 1e0 */ {500, 500, 0, 0x12, 1, 1, 0, 0, 30.0f},
	/* 6 2cc */ {600, 600, 0, 0x13, 5, 1, -30.0f, 0, 64.0f},
	/* 7 c04 */ {1000, 1000, 1000, 3, 6, 0, 0, 0, 0},
	/* 8 940 */ {700, 700, 1000, 0x1a, 0, 0, 30.0f, 20.0f, -60.0f},
	/* 9 3b8 */ {0x2c6, 700, 1000, 0x1b, 0, 0, 20.0f, 36.0f, -32.0f},
	/*10 b18 */ {0x2d0, 700, 1000, 0x1c, 0, 0, 30.0f, -20.0f, -60.0f},
	/*11 1450*/ {0x2da, 700, 1000, 3, -1, 0, 0, 0, 0}, /* initialised, never pushed */
	/*12 a2c */ {800, 800, 1000, 3, 0, 0, 30.0f, 0, 0},
	/*13 768 */ {900, 900, 1000, 0x1e, 0, 0, 0, 50.0f, 82.0f},
	/*14 854 */ {0x38e, 900, 1000, 0x1f, 0, 0, 0, -50.0f, 82.0f},
};
static const int k_push_order[14] = {0, 9, 3, 1, 4, 2, 13, 14, 5, 6, 8, 10, 12, 7};
static const int k_root_children[9] = {1, 2, 3, 4, 8, 9, 12, 13, 14};

static int spawn_weapon(aos_world *w, int32_t owner, int32_t team, int32_t p18)
{
	int32_t grp = aos_enemy_group_next(w);
	int32_t base = w->obj.enemyId;
	w->obj.enemyId += 15; /* 15 FUN_0055ee70 calls (one record is never pushed) */
	int ret = -1;
	for (int n = 0; n < 14; n++) {
		int k = k_push_order[n];
		const part_def *d = &k_parts[k];
		if (w->obj.nenemy >= AOS_MAX_ENEMY) {
			w->obj.overflow++;
			continue;
		}
		int i = w->obj.nenemy++;
		aos_enemy *e = &w->obj.enemy[i];
		memset(e, 0, sizeof *e);
		e->id = base + k;
		e->serial = grp;
		e->ownerId = owner;
		e->type = ID;
		e->team = team;
		e->p18 = p18;
		e->drawLayer = 1;
		e->f10 = d->f10;
		e->f7c = d->kind;
		e->f84 = d->cmd;
		e->hp = e->maxHp = 9999;
		e->active = 0;
		e->scale[0] = e->scale[1] = e->scale[2] = d->scale;
		e->mirrorChildren = 1;
		e->motionId = d->motion;
		e->rehitTimer = 0;
		e->parentId = d->parent < 0 ? -1 : base + d->parent;
		for (int c = 0; c < 10; c++)
			e->childSlots[c] = -1;
		e->offset[0] = d->ox;
		e->offset[1] = d->oy;
		e->pivot[0] = d->px;
		if (k == 0) {
			for (int c = 0; c < 9; c++)
				e->childSlots[c] = base + k_root_children[c];
			ret = i;
		}
		if (k == 5)
			e->childSlots[0] = base + 6;
	}
	return ret;
}

static aos_enemy *enew(aos_world *w, int32_t owner, int32_t team, int32_t p18, int *idx)
{
	int32_t grp = aos_enemy_group_next(w);
	int i = aos_enemy_new(w, ID, owner, team, p18);
	if (i < 0)
		return NULL;
	aos_enemy *e = &w->obj.enemy[i];
	e->serial = grp;
	*idx = i;
	return e;
}

static int enemy_spawn(aos_world *w, int32_t id, int32_t owner, int32_t team, int32_t p18) /* FUN_0052d0f0 */
{
	int idx = -1;
	aos_enemy *e;
	switch (id) {
	case 100:
		return spawn_weapon(w, owner, team, p18);
	case 0x1400:
	case 0x13f6:
	case 0x13f7:
	case 0x13ec:
	case 0x13ed:
		if (!(e = enew(w, owner, team, p18, &idx)))
			return -1;
		e->f10 = id;
		e->f7c = id == 0x1400 ? 0x1400 : (id >= 0x13f6 ? 0x13f6 : 0x13ec);
		e->f84 = 0;
		e->solid = id == 0x13ec || id == 0x13ed ? 0 : 1;
		e->hp = e->maxHp = 999;
		e->active = 0;
		e->shape = (aos_shape){0, 0, 0};
		e->scale[0] = 3.0f;
		e->scale[1] = 0;
		e->scale[2] = 0;
		e->mirrorChildren = 0;
		e->motionId = 0;
		e->rehitTimer = 0;
		return idx;
	case 0x1450:
		if (!(e = enew(w, owner, team, p18, &idx)))
			return -1;
		e->f10 = 0x1450;
		e->f7c = 0x1450;
		e->f84 = 0;
		e->hp = e->maxHp = 999;
		e->active = 0;
		e->shape = (aos_shape){0, 0, 0};
		e->scale[0] = 3.0f;
		e->scale[1] = 0;
		e->scale[2] = 0;
		e->mirrorChildren = 0;
		e->motionId = 0;
		e->rehitTimer = 0;
		e->drawLayer = 2;
		return idx;
	case 0x1451:
	case 0x1452:
		if (!(e = enew(w, owner, team, p18, &idx)))
			return -1;
		e->f10 = id;
		e->f7c = id;
		e->f84 = 0;
		e->hp = e->maxHp = 999;
		e->active = 0;
		e->shape = (aos_shape){0, 0, 0};
		e->scale[0] = e->scale[1] = e->scale[2] = 1.0f;
		e->mirrorChildren = 0;
		e->motionId = 0;
		e->rehitTimer = 0;
		return idx;
	case 9999:
		if (!(e = enew(w, owner, team, p18, &idx)))
			return -1;
		e->f10 = 9999;
		e->f7c = 9999;
		e->f84 = 0;
		e->hp = e->maxHp = 9999;
		e->active = 0;
		e->shape = (aos_shape){0, 0, 0};
		e->scale[0] = e->scale[1] = e->scale[2] = 0;
		e->mirrorChildren = 0;
		e->motionId = 0;
		e->rehitTimer = 0;
		return idx;
	case 5000:
		if (!(e = enew(w, owner, team, p18, &idx)))
			return -1;
		e->f10 = 5000;
		e->f7c = 5000;
		e->f84 = 100;
		e->hp = e->maxHp = 200;
		e->active = 0;
		e->shape = (aos_shape){0, 0, 10.0f};
		e->scale[0] = 3.0f;
		e->scale[1] = 0;
		e->scale[2] = 1.0f;
		e->mirrorChildren = 0;
		e->motionId = 0;
		e->rehitTimer = 0;
		return idx;
	default:
		return -1;
	}
}

static void follow(aos_enemy *e, const aos_player *p) /* pos = Nath, angle = wrap(+0x84) */
{
	e->pos[0] = p->x;
	e->pos[1] = p->y;
	e->pos[2] = p->z;
	e->angle = p->faceAngle;
	aos_angle_normalize(&e->angle);
}

static void ext_anim(aos_enemy *e) /* kinds 700/800/900/1000 */
{
	if (e->f84 == 0) {
		e->scale[0] -= 0.1f;
		e->scale[1] += 0.05f;
		if (1.0f <= e->scale[1]) {
			e->scale[0] = 1.0f;
			e->scale[1] = 1.0f;
			e->f84 = 100;
			e->timer2 = 0;
		}
	} else if (e->f84 == 100) {
		e->scale[2] += 0.1f;
		if (1.0f < e->scale[2])
			e->scale[2] = 1.0f;
	} else if (e->f84 == 900) {
		e->scale[0] += 0.2f;
		e->scale[1] -= 0.1f;
		if (e->scale[1] <= 0.0f) {
			e->scale[0] = e->scale[1] = e->scale[2] = 0;
			e->f84 = 1000;
		}
	}
}

static void fade999(aos_enemy *e)
{
	if (e->f84 == 999) {
		e->scale[2] -= 0.02f;
		if (e->scale[2] < 0.0f)
			e->scale[2] = 0;
	}
}

/* pair of reflectors; rx/ry: random ranges of the old version (draws kept), fx/fy: 0x5a2 values */
static void reflector_pair(aos_world *w, aos_enemy *e, aos_player *p, float ax, float bx, float ay, float by,
                           bool negX, float fx, float fy1, float fy2)
{
	float v[2];
	v[0] = aos_rand_real(w, ax, bx);
	if (negX)
		v[0] = -v[0];
	v[1] = aos_rand_real(w, ay, by);
	v[0] = fx;
	v[1] = fy1;
	aos_vec_rotate(v, p->faceAngle);
	float pos[3] = {v[0] + e->pos[0], v[1] + e->pos[1], 0};
	aos_spawn_missile(w, ID, 200, pos, e->angle, e->id, 0, e->team, e->p18);
	v[0] = aos_rand_real(w, ax, bx);
	if (negX)
		v[0] = -v[0];
	v[1] = -aos_rand_real(w, ay, by);
	v[0] = fx;
	v[1] = fy2;
	aos_vec_rotate(v, p->faceAngle);
	float pos2[3] = {v[0] + e->pos[0], v[1] + e->pos[1], 0};
	aos_spawn_missile(w, ID, 0xc9, pos2, e->angle, e->id, 0, e->team, e->p18);
	aos_se(w, 0x2f);
}

static void homing_missile(aos_world *w, aos_enemy *e, int32_t id, int32_t div, float speed, float acc)
{
	int m = aos_spawn_missile(w, ID, id, e->pos, e->angle, e->id, 0, e->team, e->p18);
	float r = aos_rand_real(w, 1.4f, 2.6f); /* DAT_00817dd0 / DAT_00817dd4 */
	if (m < 0)
		return;
	aos_missile *ms = &w->obj.missile[m];
	if ((e->timer2 / div) % 2 == 0)
		ms->moveAngle = r + e->angle;
	else
		ms->moveAngle = e->angle - r;
	aos_angle_normalize(&ms->moveAngle);
	ms->angle = ms->moveAngle;
	ms->speed = speed;
	ms->f74 = acc;
	aos_se(w, 0x22);
}

static void bomb(aos_world *w, aos_enemy *e, int32_t id, float hi)
{
	int m = aos_spawn_missile(w, ID, id, e->pos, e->angle, e->id, 0, e->team, e->p18);
	float r = aos_rand_real(w, 1.0f, hi);
	if (m >= 0)
		w->obj.missile[m].speed = (e->timer2 / 3) % 2 == 0 ? r : -r;
	aos_se_cd(w, 0x22, 4);
}

static void root_update(aos_world *w, aos_enemy *e) /* kind 100 */
{
	aos_player *p = aos_player_by_objid(w, e->ownerId);
	if (!p)
		return;
	int32_t c = e->f84, t = e->timer2;
	static const struct { int32_t cmd, st; } endOn[] = {
		{500, 0xe3}, {600, 0xe7}, {0x262, 0xe8}, {0x28a, 0xe9}, {700, 0xea}, {0x2c6, 0xeb},
		{800, 0xee}, {900, 0xef}, {0x38e, 0xf0}, {0x398, 0xf1},
	};
	switch (c) {
	case 100:
		follow(e, p);
		if (t == 0x1e)
			e->f84 = 0;
		return;
	case 200:
		follow(e, p);
		if (0xb < t && t < 0x25 && t % 3 == 0 && aos_use_gauge(p, 0xd8))
			bomb(w, e, 100, 5.0f);
		if (p->state != 0xd8)
			e->f84 = 0;
		return;
	case 0xd2:
		follow(e, p);
		if (8 < t && t < 0x2e && t % 3 == 0)
			bomb(w, e, 0x6e, 10.0f);
		if (p->state != 0xd9)
			e->f84 = 0;
		return;
	case 0xfa:
		follow(e, p);
		if (phase(p) == 3) {
			if (7 < t && t <= 0x24 && t % 4 == 0 && aos_use_gauge(p, 0xdb))
				homing_missile(w, e, 0x96, 4, 7.0f, 0.1f);
		} else if (9 < t && t < 0x24 && t % 5 == 0 && aos_use_gauge(p, 0xda)) {
			homing_missile(w, e, 0x96, 5, 7.0f, 0.1f);
		}
		if (t == 0x32)
			e->f84 = 0;
		return;
	case 0x104:
		follow(e, p);
		if (2 < t && t < 0x25 && t % 3 == 0)
			homing_missile(w, e, 0xa0, 3, 12.0f, 0.2f);
		if (t == 0x32)
			e->f84 = 0;
		return;
	case 300:
		follow(e, p);
		if (t == 0x32)
			e->f84 = 0;
		return;
	case 400:
		follow(e, p);
		if (phase(p) == 1) {
			if (t == 10 && aos_use_gauge(p, 0xdf))
				reflector_pair(w, e, p, 20, 50, 10, 100, false, 40.0f, 30.0f, -30.0f);
		} else if (phase(p) == 2) {
			if (t == 10 && aos_use_gauge(p, 0xe0))
				reflector_pair(w, e, p, 20, 50, 10, 100, false, 40.0f, 30.0f, -30.0f);
			if (t == 0xe && aos_use_gauge(p, 0xe0))
				reflector_pair(w, e, p, 20, 50, 50, 150, true, 10.0f, 100.0f, -100.0f);
		} else if (phase(p) == 3) {
			if (t == 8 && aos_use_gauge(p, 0xe1))
				reflector_pair(w, e, p, 20, 50, 10, 100, false, 40.0f, 30.0f, -30.0f);
			if (t == 0xc && aos_use_gauge(p, 0xe1))
				reflector_pair(w, e, p, 20, 50, 50, 150, true, 10.0f, 100.0f, -100.0f);
			if (t == 0x10 && aos_use_gauge(p, 0xe1))
				reflector_pair(w, e, p, 50, 100, 80, 160, false, -10.0f, 150.0f, -150.0f);
		}
		if (t == 0x32)
			e->f84 = 0;
		return;
	case 0x2ee:
		follow(e, p);
		if (p->state != 0xec && p->state != 0xed)
			e->f84 = 0;
		return;
	default:
		for (size_t k = 0; k < sizeof endOn / sizeof endOn[0]; k++) {
			if (c == endOn[k].cmd) {
				follow(e, p);
				if (p->state != endOn[k].st)
					e->f84 = 0;
				return;
			}
		}
		e->pos[0] = p->x;
		e->pos[1] = p->y;
		e->pos[2] = p->z;
		e->angle = aos_fabs(p->faceAngle) <= HALF_PI ? 0.0f : PI;
		return;
	}
}

static void arm_motion(aos_enemy *e, const aos_player *p, int32_t m9, int32_t mOther, int32_t mDef)
{
	int32_t s = p->state, t = p->timer;
	if (s == 0xe9) {
		if (t == 5) { e->motionId = m9; e->motionFrame = 0; }
	} else if (s == 0xea || s == 0xeb || s == 0xec || s == 0xed || s == 0xee) {
		if (t == 1) { e->motionId = mOther; e->motionFrame = 0; }
	} else if (s == 0xef || s == 0xf0 || s == 0xf1) {
		if (s == 0xef && t == 0x14) { e->motionId = mOther; e->motionFrame = 0; }
	} else {
		e->motionId = mDef;
	}
}

static void kill_lasers(aos_world *w, int32_t id, int32_t owner, int32_t team) /* FUN_00630880 */
{
	for (int i = 0; i < w->obj.nlaser; i++) {
		aos_laser *l = &w->obj.laser[i];
		if (l->id == id && l->ownerId == owner && l->team == team)
			l->f4d = 1;
	}
}

static void hand_actions(aos_world *w, aos_enemy *e, aos_player *p) /* kind 600 per-state work */
{
	int32_t s = p->state, t = p->timer;
	if (s == 0xe9) {
		if (t == 5) { e->motionId = 0x17; e->motionFrame = 0; }
		if (5 < t && t < 0x19 && t % 2 == 0)
			eff8(w, 0x15e, 0, e->pos, e->angle2, e->ownerId, 0, e->team);
		if (5 < t && t < 0x19)
			aos_spawn_hitbox(w, ID, 0xfa, e->pos, e->angle2, e->ownerId, 0, e->team);
	} else if (s == 0xea) {
		if (t == 1) { e->motionId = 0x19; e->motionFrame = 0; }
		if (t == 0xf) {
			aos_spawn_laser(w, ID, 200, e->pos, e->angle2, e->id, e->team, e->p18);
			aos_se(w, 0x12);
		}
	} else if (s == 0xeb) {
		if (t == 1) {
			e->motionId = 0x19;
			e->motionFrame = 0;
			kill_lasers(w, 200, e->id, e->team); /* ver > 0x589 */
			kill_lasers(w, 0xfa, e->id, e->team);
			kill_lasers(w, 0x104, e->id, e->team);
		}
		if (t == 0xb) {
			aos_spawn_laser(w, ID, phase(p) == 2 ? 0xfa : 0x104, e->pos, e->angle2, e->id, e->team, e->p18);
			aos_se(w, 0x12);
		}
	} else if (s == 0xec) {
		if (t == 1) { e->motionId = 0x19; e->motionFrame = 0; }
		if (10 < t && t < 0x19)
			aos_spawn_hitbox(w, ID, 400, e->pos, e->angle2, e->ownerId, 0, e->team);
	} else if (s == 0xed) {
		if (t == 1) {
			e->motionId = 0x19;
			e->motionFrame = 0;
			aos_se(w, 0x30);
		}
		if (t < 0x3c) {
			aos_player *o = opp_of_team(w, p->side);
			/* FUN_00780a50 */
			if (o->barrierTime == 0 && o->state != AOS_ST_KO && o->hittable != 0 &&
			    o->statusOn[AOS_STATUS_STUNIMMUNE] != 1 && !w->roundOver) {
				o->state = AOS_ST_HITSTUN;
				o->timer = 0;
				o->x = aos_cos(e->angle2) * 150.0f + e->pos[0]; /* DAT_0081414c */
				o->y = aos_sin(e->angle2) * 150.0f + e->pos[1];
				o->speed = 0;
				o->faceAngle = e->angle2 + PI;
				aos_angle_normalize(&o->faceAngle);
			}
		}
		int c2 = col(w, p->side, 2); /* DAT_008859fc */
		float hp[3] = {aos_cos(e->angle2) * 150.0f + e->pos[0], aos_sin(e->angle2) * 150.0f + e->pos[1], 0};
		if (t == 0x3c) {
			aos_spawn_hitbox(w, ID, 0x19a, hp, e->angle2, e->ownerId, 0, e->team);
			aos_se(w, 0x1e);
		}
		if (9 < t && t < 0x3d && t % 10 == 0) {
			int f = eff8(w, 400, c2, hp, 0, e->ownerId, 0, e->team);
			if (f >= 0)
				w->obj.effect[f].scale = (float)(t - 10) * 0.1f + 0.75f;
		}
		if (-1 < t && t < 0x1f) {
			float v[2] = {aos_rand_real(w, 240.0f, 640.0f), 0.0f};
			aos_vec_rotate(v, aos_rand_real(w, -PI, PI));
			float q[3] = {aos_cos(e->angle2) * 150.0f + e->pos[0] + v[0],
			              aos_sin(e->angle2) * 150.0f + e->pos[1] + v[1], 0};
			float a = aos_rand_real(w, -PI, PI);
			int f = eff8(w, 0x19a, c2, q, a, e->id, 0, e->team);
			float sp = aos_rand_real(w, 10.0f, 30.0f);
			if (f >= 0)
				w->obj.effect[f].aux = sp;
		}
	} else if (s == 0xee) {
		if (t == 1) { e->motionId = 0x19; e->motionFrame = 0; }
	} else if (s == 0xef || s == 0xf0 || s == 0xf1) {
		if (s == 0xef && t == 0x14) { e->motionId = 0x19; e->motionFrame = 0; }
	} else {
		e->motionId = 0x13;
	}
}

static void spark_burst(aos_world *w, aos_enemy *e, int n, int32_t base, float auxMul)
{
	for (int k = 0; k < n; k++) {
		float r = aos_rand_real(w, -0.1f, 0.1f); /* DAT_00814130 */
		int32_t m = aos_rand_int(w, 0, 100) % 8;
		int f = eff0(w, base + m, 0xd, e->pos, r + e->angle, e->id, k * -2, e->team);
		if (f >= 0 && auxMul > 0)
			w->obj.effect[f].aux2 *= auxMul;
	}
}

static bool fist_update(aos_world *w, aos_enemy *e, float spd, int nspark, int32_t sbase, float auxMul,
                        bool everyFrame, int32_t hbId, int32_t trailId) /* 0x13ec / 0x13f6 / 0x1400 */
{
	if (e->f84 == 0) {
		e->scale[0] -= 0.2f;
		e->scale[1] += 0.1f;
		if (1.0f <= e->scale[1]) {
			e->scale[0] = e->scale[1] = e->scale[2] = 1.0f;
			e->f70 = spd;
			e->f84 = 100;
			e->timer2 = 0;
			aos_se(w, 0x20);
			spark_burst(w, e, nspark, sbase, auxMul);
		}
	} else if (e->f84 == 100) {
		if (everyFrame ? (2 < e->timer2 && e->timer2 < 0xb) : e->timer2 == 8)
			aos_spawn_hitbox(w, ID, hbId, e->pos, e->angle2, e->ownerId, 0, e->team);
		if (trailId && e->timer2 % 2 == 0)
			eff8(w, e->f10 == trailId ? 300 : 0x12d, 0, e->pos, e->angle2, e->ownerId, 0, e->team);
		if (9 < e->timer2) {
			e->f84 = 900;
			e->timer2 = 0;
		}
	} else {
		e->f70 *= 0.85f; /* DAT_00814120 */
		e->scale[2] -= 0.1f;
		if (e->scale[2] <= 0.0f)
			return false;
	}
	e->pos[0] = aos_cos(e->angle2) * e->f70 + e->pos[0];
	e->pos[1] = aos_sin(e->angle2) * e->f70 + e->pos[1];
	return true;
}

static bool enemy_killed(aos_world *w, aos_enemy *e) /* FUN_005335b0 */
{
	if (e->f10 == 0x1451 || e->f10 == 0x1452) {
		eff0(w, 0x26c, 1, e->pos, 0, e->ownerId, 0, e->team);
		eff0(w, 500, 1, e->pos, 0, e->ownerId, 0, e->team);
		eff0(w, 0x515, 1, e->pos, 0, e->ownerId, 0, e->team);
		aos_se_cd(w, 0x1a, 10);
	}
	return false;
}

static bool enemy_update(aos_world *w, aos_enemy *e) /* FUN_0052eaf0 */
{
	int32_t k = e->f7c;
	aos_player *p;
	switch (k) {
	case 1000: case 700: case 800: case 900:
		ext_anim(e);
		return true;
	case 500:
	case 600:
		p = aos_player_by_objid(w, e->ownerId);
		if (!p)
			return true;
		if (k == 500) {
			if (phase(p) == 1) {
				if (e->pivot[0] <= 20.0f) e->pivot[0] = 20.0f;
				else e->pivot[0] -= 0.5f;
			} else if (50.0f < e->pivot[0] || 50.0f == e->pivot[0]) {
				e->pivot[0] = 50.0f;
			} else {
				e->pivot[0] += 0.5f;
			}
		} else {
			if (phase(p) == 1) {
				if (-20.0f < e->offset[0] || -20.0f == e->offset[0]) e->offset[0] = -20.0f;
				else e->offset[0] += 0.5f;
			} else if (e->offset[0] <= -50.0f) {
				e->offset[0] = -50.0f;
			} else {
				e->offset[0] -= 0.5f;
			}
		}
		if (p->state == 0xe7 || p->state == 0xe8) {
			e->scale[0] = 0;
		} else {
			e->scale[0] = 1.0f;
			if (k == 500)
				arm_motion(e, p, 0x16, 0x18, 0x12);
			else
				hand_actions(w, e, p);
			fade999(e);
		}
		return true;
	case 300: {
		p = aos_player_by_objid(w, e->ownerId);
		if (!p)
			return true;
		bool mv = 1 < p->down;
		if (p->facingRight == 0 && 1 < p->left) mv = false;
		if (p->facingRight == 1 && 1 < p->left) mv = true;
		if (p->facingRight == 0 && 1 < p->right) mv = true;
		if (p->facingRight == 1 && 1 < p->right) mv = false;
		if (p->state != 0) mv = false;
		if (mv)
			e->motionId = e->f10 == 300 ? 0x10 : 0x11;
		else
			e->motionId = e->f10 == 300 ? 0xe : 0xf;
		fade999(e);
		return true;
	}
	case 100:
		root_update(w, e);
		return true;
	case 200:
	case 400:
		fade999(e);
		return true;
	case 0x1400:
		return fist_update(w, e, 12.0f, 8, 0xa82, 0.5f, false, 0xdc, 0);
	case 5000:
		if (e->f84 == 100) {
			e->scale[0] -= 0.2f;
			e->scale[1] += 0.1f;
			if (1.0f <= e->scale[1]) {
				e->scale[0] = e->scale[1] = 1.0f;
				e->f84 = 200;
				e->timer2 = 0;
				e->active = 1;
			}
		} else if (e->f84 == 200) {
			e->pos[0] = aos_cos(e->angle2) * e->f70 + e->pos[0];
			e->pos[1] = aos_sin(e->angle2) * e->f70 + e->pos[1];
			float v[2] = {0.0f, aos_cos(((float)e->timer2 / 50.0f) * PI) * 10.0f};
			if (e->angle2 < 0.0f && e->mirrorChildren == 0)
				v[1] = -v[1];
			if (0.0f < e->angle2 && e->mirrorChildren == 1)
				v[1] = -v[1];
			aos_vec_rotate(v, e->angle2);
			e->pos[0] += v[0];
			e->pos[1] += v[1];
			e->angle = 0.06283186f * ef74(e) + e->angle; /* DAT_00817dcc */
			if (0x31 < e->timer2) {
				e->f84 = 300;
				e->timer2 = 0;
				e->f70 = 3.0f;
			}
		} else if (e->f84 == 300) {
			e->pos[0] = e->pos[0] - aos_cos(e->angle) * e->f70;
			e->pos[1] = e->pos[1] - aos_sin(e->angle) * e->f70;
			e->f70 *= 0.95f; /* _DAT_00814124 */
			if (e->timer2 == 10) {
				aos_spawn_laser(w, ID, 100, e->pos, e->angle, e->id, e->team, e->p18);
				aos_se_cd(w, 0x12, 3);
			}
			if (99 < e->timer2) {
				e->f84 = 900;
				e->timer2 = 0;
				e->active = 0;
			}
		} else {
			e->scale[0] += 0.2f;
			e->scale[1] -= 0.1f;
			if (e->scale[1] <= 0.0f)
				return false;
		}
		return true;
	case 0x13ec:
		return fist_update(w, e, 10.0f, 5, 0xa28, 0, true, 200, 0x13ec);
	case 0x13f6:
		return fist_update(w, e, 12.0f, 8, 0xa82, 0.25f, false, 0xd2, 0x13f6);
	case 0x1450:
		if (e->f84 == 0) {
			e->scale[0] -= 0.2f;
			e->scale[1] += 0.1f;
			if (e->scale[1] < 1.0f)
				return true;
			e->scale[0] = e->scale[1] = e->scale[2] = 1.0f;
			e->f84 = 100;
			e->timer2 = 0;
			aos_spawn_missile(w, ID, 300, e->pos, e->angle, e->id, 0, e->team, e->p18);
			aos_se_cd(w, 0x22, 5);
			return true;
		}
		if (e->f84 != 100) {
			e->scale[0] += 0.2f;
			e->scale[1] -= 0.1f;
			return 0.0f < e->scale[1];
		}
		if (e->timer2 < 0xc9)
			return true;
		e->f84 = 900;
		e->scale[2] = 0;
		return true;
	case 0x1451:
	case 0x1452:
		if (0x95 < e->timer2) {
			aos_spawn_hitbox(w, ID, 500, e->pos, e->angle2, e->ownerId, 0, e->team);
			enemy_killed(w, e);
			return false;
		}
		return true;
	case 9999:
		p = aos_player_by_objid(w, e->ownerId);
		if (p) {
			e->pos[0] = p->x;
			e->pos[1] = p->y;
			e->pos[2] = p->z;
		}
		if (e->age < 0x33) {
			e->scale[0] += 0.1f;
			e->scale[2] += 0.05f;
			if (1.0f < e->scale[0]) e->scale[0] = 1.0f;
			if (1.0f < e->scale[2]) e->scale[2] = 1.0f;
		} else {
			e->scale[2] -= 0.025f;
			e->scale[0] += 0.02f;
			if (e->scale[2] <= 0.0f)
				return false;
		}
		return true;
	default:
		return true;
	}
}

/* ------------------------------------------------------------------ */
/* draw helpers                                                        */

/* sprite bank: the original uses the owner palette slot (+0x18 / DAT_00885a00[team]);
 * the engine resolves palettes through the player index = team - 1 */
static int team_bank(int32_t team) { return team == 2 ? AOS_BANK_P1 : AOS_BANK_P0; }

/* CModel +0xdc takes (a, r, g, b) */
static uint32_t argb(float a, float r, float g, float b) { return aos_rgba(r, g, b, a); }
/* 2D blit colour word 0xAARRGGBB */
static uint32_t word(uint32_t c)
{
	return aos_rgba((float)((c >> 16) & 0xff) / 255.0f, (float)((c >> 8) & 0xff) / 255.0f,
	                (float)(c & 0xff) / 255.0f, (float)(c >> 24) / 255.0f);
}

/* CModel sprite (registered SxS quad) at pos with scale (sx, sy) */
static aos_dcmd *model(const char *fmt, int frame, int bank, float x, float y, float size, float sx, float sy)
{
	aos_dcmd *c = aos_draw(strchr(fmt, '%') ? aos_texf(fmt, frame) : aos_tex(fmt), bank, x, y, 0);
	c->halfW = c->halfH = size * 0.5f;
	c->sx = sx;
	c->sy = sy;
	return c;
}

/* AIM transform: RotZ(a); RotX(180 deg) when |a| >= pi/2; RotY(180 deg) */
static void aim_t(aos_dcmd *c, float a)
{
	c->rotZ = aos_rad2bam(a);
	c->flipX = 1;
	if (HALF_PI <= aos_fabs(a))
		c->sy = -c->sy;
}

/* 2D blit (vtable +0x68 / +0x60): dst half-size in world units, src rect in texture pixels,
 * rotation in world radians (the original passes -angle in screen space) */
static aos_dcmd *blit(const char *tex, int bank, float x, float y, float hw, float hh, float s0, float s1,
                      float s2, float s3, float rot, uint32_t colour, int blend)
{
	aos_dcmd *c = aos_draw(aos_tex(tex), bank, x, y, 0);
	c->halfW = hw;
	c->halfH = hh;
	c->hasSrc = 1;
	c->src[0] = s0;
	c->src[1] = s1;
	c->src[2] = s2;
	c->src[3] = s3;
	c->rotZ = aos_rad2bam(rot);
	c->rgba = word(colour);
	c->blend = (uint8_t)blend;
	return c;
}

static uint32_t a8(float v) { return (uint32_t)(int32_t)v & 0xff; } /* (uint)(K * alpha) */

/* beamsphere (DAT_0087fde4) colour cells, FUN_004baaa0 layout */
static void sphere_glow(float x, float y, int32_t c, float hw, float hh, float rot, uint32_t col, int blend)
{
	float x0 = (float)(((c % 10) & 1) * 100 + 0x32 + (c / 10) * 0xfa), y0 = (float)(((c % 10) / 2) * 100);
	blit("beamsphere", AOS_BANK_COMMON, x, y, hw, hh, x0, y0, x0 + 99.0f, y0 + 99.0f, rot, col, blend);
}
static void sphere_core(float x, float y, int32_t c, float hw, float hh, float rot, uint32_t col, int blend)
{
	float x0 = (float)((c / 10) * 0xfa), y0 = (float)((c % 10) * 0x32);
	blit("beamsphere", AOS_BANK_COMMON, x, y, hw, hh, x0, y0, x0 + 49.0f, y0 + 49.0f, rot, col, blend);
}

/* ---- enemy draw FUN_005336b0 (sprite ids from the dispatch at 0x5336d5 / jump table 0x53d254) ---- */

static float glow_a(const aos_enemy *e) { return aos_sin(((float)(e->age % 0x32) / 50.0f) * TWO_PI) * 0.25f + 0.75f; } /* fVar9 */
static float glow_b(const aos_enemy *e) { return aos_sin(((float)(e->age % 0x32) / 50.0f) * TWO_PI) * 0.35f + 0.65f; } /* fVar10 */

/* base frame f0 + two additive glow frames f0+1, f0+2; base colour (1,z,z,z) or (z,z,z,z) */
static void tri(aos_enemy *e, const char *fmt, int f0, float size, bool zAlpha, bool aim)
{
	float z = e->scale[2];
	int bank = team_bank(e->team);
	aos_dcmd *c = model(fmt, f0, bank, e->pos[0], e->pos[1], size, e->scale[0], e->scale[1]);
	c->rgba = zAlpha ? argb(z, z, z, z) : argb(1.0f, z, z, z);
	if (aim)
		aim_t(c, e->angle);
	else
		c->rotZ = aos_rad2bam(e->angle);
	float g[2] = {glow_a(e), glow_b(e)};
	for (int k = 0; k < 2; k++) {
		c = model(fmt, f0 + 1 + k, bank, e->pos[0], e->pos[1], size, e->scale[0], e->scale[1]);
		c->rgba = argb(z, g[k], g[k], g[k]);
		c->blend = AOS_BLEND_ADD_ALPHA;
		if (aim)
			aim_t(c, e->angle);
		else
			c->rotZ = aos_rad2bam(e->angle);
	}
}

static aos_dcmd *single(aos_enemy *e, const char *fmt, int f, float size, uint32_t rgba, bool aim)
{
	aos_dcmd *c = model(fmt, f, team_bank(e->team), e->pos[0], e->pos[1], size, e->scale[0], e->scale[1]);
	c->rgba = rgba;
	if (aim)
		aim_t(c, e->angle);
	else
		c->rotZ = aos_rad2bam(e->angle);
	return c;
}

static void enemy_draw(aos_world *w, aos_enemy *e) /* FUN_005336b0 */
{
	float z = e->scale[2];
	switch (e->f10) {
	case 200: /* sprite 0xf8 atch_body, 3 layers */
		tri(e, "atch_body%02d", 0, 192.0f, true, true);
		break;
	case 300:
	case 0x15e: { /* 0xf9 frame 0 legl / frame 1 legr */
		aos_dcmd *c = single(e, e->f10 == 300 ? "atch_legl00" : "atch_legr00", 0, 384.0f, argb(z, z, z, z), true);
		if (e->motionId == (e->f10 == 300 ? 0x10 : 0x11))
			c->sy = -c->sy; /* extra RotX(180 deg) */
		break;
	}
	case 400: /* 0xfa atch_ovip */
		tri(e, "atch_ovip%02d", 0, 192.0f, true, true);
		break;
	case 500: { /* 0xfb: 7 links from (hand - dir(hand)*60) to the parent; last link full size */
		aos_enemy *pa = aos_enemy_by_id(w, e->parentId), *ch = aos_enemy_by_id(w, e->childSlots[0]);
		if (!pa || !ch)
			break; /* the original dereferences both unconditionally */
		float ex = ch->pos[0] - aos_cos(ch->angle) * 60.0f, ey = ch->pos[1] - aos_sin(ch->angle) * 60.0f;
		for (int k = 0; k < 7; k++) {
			float f = (float)k / 6.0f;
			float sc = k == 6 ? 1.0f : 0.5f;
			aos_dcmd *c = model("atch_arm", 0, team_bank(e->team), (pa->pos[0] - ex) * f + ex,
			                    (pa->pos[1] - ey) * f + ey, 96.0f, e->scale[0] * sc, e->scale[1] * sc);
			c->rgba = argb(z, z, z, z);
			aim_t(c, e->angle);
		}
		break;
	}
	case 600: /* 0xfc frame 0 */
		single(e, "atch_hand%02d", 0, 144.0f, argb(z, z, z, z), true);
		break;
	case 700:
	case 0x2c6:
	case 0x2d0: /* 0xfe frames 0-2 / 3-5 / 6-8 */
		if (e->scale[1] == 0.0f)
			break;
		tri(e, "atch_wing%02d", e->f10 == 700 ? 0 : e->f10 == 0x2c6 ? 3 : 6, 192.0f, false, true);
		break;
	case 800: /* 0xff, frame from the match frame counter */
		if (e->scale[1] == 0.0f)
			break;
		single(e, "atch_gene%02d", (w->frame / 5) % 3, 96.0f, argb(1.0f, z, z, z), true);
		break;
	case 900:
	case 0x38e: /* 0x100 frames 0-2 / 3-5 */
		if (e->scale[1] == 0.0f)
			break;
		tri(e, "atch_cowl%02d", e->f10 == 900 ? 0 : 3, 192.0f, false, true);
		break;
	case 1000: /* 0xfd atch_cannon */
		if (e->scale[1] == 0.0f)
			break;
		tri(e, "atch_cannon%02d", 0, 384.0f, false, true);
		break;
	case 5000: /* 0xf5, RotZ only */
		single(e, "nt_fannel%02d", 0, 64.0f, argb(1.0f, z, z, z), false);
		break;
	case 0x1400:
	case 0x13ec:
	case 0x13f6:
	case 0x13ed:
	case 0x13f7: { /* 0xfc frame 0 / 1 / 2; black while growing */
		int f = e->f10 == 0x1400 ? 0 : (e->f10 == 0x13ec || e->f10 == 0x13f6) ? 1 : 2;
		single(e, "atch_hand%02d", f, 144.0f, e->f84 == 0 ? argb(1, 0, 0, 0) : argb(z, 1, 1, 1), true);
		break;
	}
	case 0x1450: /* 0xee frames 0-2, RotZ only */
		tri(e, "nt_fuse%02d", 0, 74.0f, false, false);
		break;
	case 0x1451:
		single(e, "nt_fuse%02d", 3, 74.0f, argb(1.0f, z, z, z), false);
		break;
	case 0x1452:
		tri(e, "nt_fuse%02d", 4, 74.0f, false, false);
		break;
	case 9999: { /* 0xf2, all blend 2 */
		/* frame 0 is a 3-D mesh (FUN_00468d80(256,256,256,30,30)) spun RotY(frame%360 deg);
		 * frames 1/2 are quads tilted RotX(10 deg) and spun RotY((frame%120)*3 deg).
		 * APPROXIMATION (the draw API has no 3-D meshes): the mesh is drawn as a quad;
		 * RotY -> x-scale by cos, RotX -> y-scale by cos (render.md). */
		int bank = team_bank(e->team);
		float s = e->scale[0];
		float ry0 = aos_cos((float)(w->frame % 360) * (PI / 180.0f));
		aos_dcmd *c = model("nt_extension%d", 0, bank, e->pos[0], e->pos[1], 256.0f, s * ry0, s);
		c->rgba = argb(z, 0.25f, 0.25f, 0.25f);
		c->rotZ = aos_rad2bam(e->angle);
		c->blend = AOS_BLEND_ADD_ALPHA;
		float ry = aos_cos((float)((w->frame % 0x78) * 3) * (PI / 180.0f)), rx = aos_cos(10.0f * (PI / 180.0f));
		for (int f = 1; f <= 2; f++)
			for (int k = 0; k < 2; k++) {
				float y = k == 0 ? 120.0f * s + e->pos[1] : e->pos[1] - 120.0f * s;
				c = model("nt_extension%d", f, bank, e->pos[0], y, 320.0f, s * ry, s * rx);
				float g = f == 1 ? 1.0f : 0.5f;
				c->rgba = argb(z, g, g, g);
				c->blend = AOS_BLEND_ADD_ALPHA;
			}
		break;
	}
	default: /* 100 (root) and the unpushed 0x2da draw nothing */
		break;
	}
}

/* ------------------------------------------------------------------ */
/* beam family (type 8)                                                */

static int beam_spawn(aos_world *w, int32_t id, const float pos[3], float ang, int32_t owner, int32_t age0, int32_t team) /* FUN_004b0750 */
{
	if (id != 100 && id != 200 && id != 300)
		return -1;
	int i = aos_beam_new(w, ID, id, pos, ang, owner, age0, team);
	if (i < 0)
		return -1;
	aos_beam *b = &w->obj.beam[i];
	if (id == 100) {
		b->speed = 12.0f;
		b->damage = 0x78;
		b->shape = (aos_shape){24.0f, 8.0f, 0};
		b->knockback = 10.0f;
	} else if (id == 200) {
		b->speed = 10.0f;
		b->destroyOnHit = 0;
		b->damage = 200;
		b->shape = (aos_shape){30.0f, 10.0f, 0}; /* ver >= 0x58a */
		b->knockback = 10.0f;
		b->rehitFlag = 1;
	} else {
		b->speed = 24.0f;
		b->alpha = 1.0f;
		b->damage = 0xa0;
		b->shape = (aos_shape){20.0f, 8.0f, 0};
		b->active = 0;
		b->knockback = 10.0f;
	}
	return i;
}

static void beam_move(aos_beam *b)
{
	b->pos[0] = aos_cos(b->moveAngle) * b->speed + b->pos[0];
	b->pos[1] = aos_sin(b->moveAngle) * b->speed + b->pos[1];
}

static bool beam_update(aos_world *w, aos_beam *b) /* FUN_004b0ea0 */
{
	if (b->id == 100) {
		beam_move(b);
		if (!aos_in_arena(b->pos, 10.0f))
			return false;
		eff8(w, 100, col(w, b->team, 1), b->pos, b->angle, b->ownerId, 0, b->team);
	} else if (b->id == 200) {
		beam_move(b);
		if (b->age < 1000 && !aos_in_arena(b->pos, 0)) {
			float n = aos_atan2(b->pos[1], b->pos[0]);
			b->moveAngle = (n - b->moveAngle) + n + PI;
			/* FUN_005f0ce0(moveAngle): result discarded (literal) */
			b->pos[0] = aos_cos(n) * 540.0f;
			b->pos[1] = aos_sin(n) * 540.0f;
			b->angle = b->moveAngle;
			b->age += 500;
			aos_se_cd(w, 0x25, 5);
			eff0(w, 0x516, col(w, b->team, 0), b->pos, 0, b->ownerId, 0, b->team);
		}
		if (!aos_in_arena(b->pos, 20.0f))
			return false;
		aos_spawn_hitbox(w, ID, 100, b->pos, b->moveAngle, b->serial, 0, b->team);
		eff8(w, 0x96, col(w, b->team, 0), b->pos, b->angle, b->ownerId, 0, b->team);
	} else if (b->id == 300) {
		if (b->age < 1000) {
			b->scale += 0.05f;
			if (1.0f < b->scale)
				b->scale = 1.0f;
			if (0x31 < b->age) {
				b->age = 1000;
				b->active = 1;
				aos_player *o = opp_of_team(w, b->team);
				b->moveAngle = aos_atan2(o->y - b->pos[1], o->x - b->pos[0]);
				b->angle = b->moveAngle;
				aos_se_cd(w, 0xf, 5);
				eff0(w, 0x515, col(w, b->team, 1), b->pos, 0, b->ownerId, 0, b->team);
			}
		} else {
			if (12.0f < b->speed)
				b->speed *= 0.975f; /* _DAT_00814128 */
			beam_move(b);
			if (!aos_in_arena(b->pos, 10.0f))
				return false;
			eff8(w, 0xaa, col(w, b->team, 1), b->pos, b->angle, b->ownerId, 0, b->team);
		}
	}
	return true;
}

static void beam_onhit(aos_world *w, aos_beam *b) /* FUN_004b0cc0 */
{
	if (b->id != 100 && b->id != 300)
		return;
	int c = col(w, b->team, 0);
	eff0(w, 0x3f2, c, b->pos, b->angle + PI, b->ownerId, 0, b->team);
	aos_spawn_particle(w, 1000, c, b->pos, b->angle, b->ownerId, 0, 5);
	aos_spawn_spark(w, 1000, c, 6, 6, b->angle, b->pos, b->ownerId);
	aos_se_cd(w, 0x13, 3);
}

static void beam_draw(aos_world *w, aos_beam *b) /* FUN_004b0960 */
{
	if (b->id != 300 || 999 < b->age)
		return;
	int32_t c = col(w, b->team, 0);
	sphere_glow(b->pos[0], b->pos[1], c, 30.0f * b->scale, 30.0f * b->scale, 0,
	            a8(96.0f * b->alpha) << 24 | 0xffffff, AOS_BLEND_ALPHA);
	sphere_core(b->pos[0], b->pos[1], c, 15.0f * b->scale, 15.0f * b->scale, 0,
	            a8(255.0f * b->alpha) << 24 | 0xffffff, AOS_BLEND_ADD_ALPHA);
}

/* ------------------------------------------------------------------ */
/* missile family (type 8)                                             */

static int missile_spawn(aos_world *w, int32_t id, const float pos[3], float ang, int32_t owner, int32_t age0, int32_t team, int32_t p18) /* FUN_00671750 */
{
	if (id != 100 && id != 0x6e && id != 0x96 && id != 0xa0 && id != 200 && id != 0xc9 && id != 300)
		return -1;
	int i = aos_missile_new(w, ID, id, pos, ang, owner, age0, team, p18);
	if (i < 0)
		return -1;
	aos_missile *m = &w->obj.missile[i];
	switch (id) {
	case 100:
	case 0x96:
		m->speed = 0;
		m->hp = 0x3c;
		m->damage = 0x96;
		m->shapeBody.radius = 10.0f;
		m->shapeProj.radius = 10.0f;
		m->knockback = 10.0f;
		break;
	case 0x6e:
	case 0xa0:
		m->speed = 0;
		m->hp = 0x78;
		m->damage = 200;
		m->shapeBody.radius = 16.0f;
		m->shapeProj.radius = 16.0f;
		m->knockback = 10.0f;
		break;
	case 200:
	case 0xc9:
		m->speed = 0;
		m->growScale = 0;
		m->f60 = 1.0f;
		m->f64 = 0;
		m->absorbs = 1;
		m->hp = 0x78;
		m->damage = 100;
		m->shapeBody.halfW = 10.0f;
		m->shapeBody.halfH = 40.0f;
		m->shapeProj.halfW = 10.0f;
		m->shapeProj.halfH = 40.0f;
		m->knockback = 10.0f;
		break;
	default: /* 300 */
		m->speed = 16.0f;
		m->growScale = 0;
		m->f60 = 0;
		m->hp = 0x78;
		m->active = 0;
		m->knockback = 10.0f;
		break;
	}
	return i;
}

static bool missile_onhit(aos_world *w, aos_missile *m) /* FUN_006733d0 */
{
	switch (m->id) {
	case 100: case 0x6e: case 0x96: case 0xa0: case 200: {
		int c = col(w, m->team, 2);
		eff8(w, 200, c, m->pos, m->angle + PI, m->ownerId, 0, m->team);
		eff0(w, 0x514, c, m->pos, 0, m->ownerId, 0, m->team);
		aos_se(w, 0x14);
		break;
	}
	default:
		break;
	}
	return false;
}

static bool missile_onkilled(aos_world *w, aos_missile *m) /* FUN_006735e0 */
{
	switch (m->id) {
	case 100: case 0x6e: case 0x96: case 0xa0:
		eff0(w, 0x262, 0, m->pos, 0, m->ownerId, 0, m->team);
		eff0(w, 0x514, 1, m->pos, 0, m->ownerId, 0, m->team);
		aos_se(w, 0x1d);
		break;
	case 200: case 0xc9:
		eff0(w, 0x262, 0, m->pos, 0, m->ownerId, 0, m->team);
		eff0(w, 0x514, 1, m->pos, 0, m->ownerId, 0, m->team);
		aos_se(w, 0x1a);
		break;
	default:
		break;
	}
	return false;
}

static void missile_onabsorb(aos_world *w, aos_missile *m, aos_beam *b) /* FUN_00673790 */
{
	if (m->id != 200 && m->id != 0xc9)
		return;
	eff0(w, 0x516, col(w, b->team, 0), b->pos, 0, b->ownerId, 0, b->team);
	float pos[3] = {b->pos[0], b->pos[1], b->pos[2]};
	aos_spawn_beam(w, ID, 300, pos, m->angle, m->ownerId, 0, m->team);
	aos_se_cd(w, 0x25, 3);
}

static void mmove(aos_missile *m, float ang)
{
	m->pos[0] = aos_cos(ang) * m->speed + m->pos[0];
	m->pos[1] = aos_sin(ang) * m->speed + m->pos[1];
}

static bool bomb_update(aos_world *w, aos_missile *m, float period, int32_t end1, float spd2,
                        float fuse, float fuseRand, bool jitter, float vmax, float acc, float margin, bool fx)
{
	if (m->age < 1000) {
		mmove(m, m->angle + HALF_PI);
		float c = aos_cos(((float)m->age / period) * PI);
		m->pos[0] = m->pos[0] - aos_cos(m->angle) * c * 8.0f;
		m->pos[1] = m->pos[1] - aos_sin(m->angle) * c * 8.0f;
		if (end1 < m->age) {
			m->speed = spd2;
			m->f74 = fuse - aos_rand_real(w, 0, fuseRand);
			if (jitter)
				m->moveAngle = aos_rand_real(w, -0.25f, 0.25f) + m->moveAngle;
			m->age = 1000;
		}
		return true;
	}
	m->f74 -= 1.0f;
	if (0.0f < m->f74 && m->speed < vmax)
		m->speed += acc;
	if (m->f74 <= 0.0f)
		m->speed *= 0.975f; /* _DAT_00814128 */
	mmove(m, m->moveAngle);
	if (m->age % 2 == 0) {
		int c = col(w, m->team, 0);
		aos_spawn_particle(w, 0x456, c, m->pos, m->angle + PI, m->ownerId, 0, 1);
		if (fx)
			eff0(w, 0x547, c, m->pos, m->angle, m->ownerId, 0, m->team);
	}
	if ((0x4b0 < m->age && !aos_in_arena(m->pos, margin)) || m->f74 < -50.0f) {
		missile_onhit(w, m);
		return false;
	}
	return true;
}

static bool homing_update(aos_world *w, aos_missile *m, int32_t steerT, float cap, float decay, int32_t die, bool fx)
{
	if (m->age < steerT) {
		aos_player *o = opp_of_team(w, m->team);
		float tgt = aos_atan2(o->y - m->pos[1], o->x - m->pos[0]);
		aos_angle_toward(&m->angle, tgt, m->speed * 0.0125f); /* _DAT_00815880 */
		float vx = aos_cos(m->moveAngle) * m->speed + aos_cos(m->angle) * m->f74;
		float vy = aos_sin(m->moveAngle) * m->speed + aos_sin(m->angle) * m->f74;
		m->moveAngle = aos_atan2(vy, vx);
		m->speed = aos_sqrt(vy * vy + vx * vx);
		if (0x14 < m->age && cap < aos_fabs(m->speed))
			m->speed = cap;
	} else {
		m->speed *= decay;
		if (die < m->age) {
			missile_onhit(w, m);
			return false;
		}
	}
	mmove(m, m->moveAngle);
	if (m->age % 2 == 0) {
		int c = col(w, m->team, 0);
		aos_spawn_particle(w, 0x456, c, m->pos, m->angle + PI, m->ownerId, 0, 1);
		if (fx)
			eff0(w, 0x547, c, m->pos, m->angle, m->ownerId, 0, m->team);
	}
	return true;
}

static void mine_spawn(aos_world *w, aos_missile *m, int32_t id)
{
	int i = aos_spawn_enemy(w, ID, id, m->ownerId, m->team, m->p18);
	if (i < 0)
		return;
	aos_enemy *e = &w->obj.enemy[i];
	e->pos[0] = m->pos[0];
	e->pos[1] = m->pos[1];
	e->pos[2] = m->pos[2];
	e->angle2 = m->moveAngle;
	e->angle = m->moveAngle;
}

static bool missile_update(aos_world *w, aos_missile *m) /* FUN_00673870 */
{
	switch (m->id) {
	case 100:
		return bomb_update(w, m, 50.0f, 0x31, 0, 120.0f, 50.0f, false, 10.0f, 0.1f, 10.0f, false);
	case 0x6e:
		return bomb_update(w, m, 30.0f, 0x1d, 5.0f, 50.0f, 30.0f, true, 15.0f, 0.2f, 20.0f, true);
	case 0x96:
		return homing_update(w, m, 300, 3.0f, 0.985f /* _DAT_00814148 */, 400, false);
	case 0xa0:
		return homing_update(w, m, 200, 7.5f, 0.9f /* DAT_00814178 */, 0xfa, true);
	case 200:
	case 0xc9:
		if (m->f64 == 0) {
			m->growScale += 0.1f;
			if (1.0f < m->growScale) {
				m->f64 = 100;
				m->age = 0;
				m->growScale = 1.0f;
				m->active = 1;
			}
		} else if (m->f64 == 100) {
			float a = m->id == 200 ? m->angle + HALF_PI : m->angle - HALF_PI;
			float s = aos_sin(((float)m->age / 30.0f) * TWO_PI);
			m->pos[0] = aos_cos(a) * s + m->pos[0];
			m->pos[1] = aos_sin(a) * s + m->pos[1];
			if (0x96 < m->age) {
				m->f64 = 900;
				m->active = 0;
			}
		} else {
			m->growScale -= 0.1f;
			if (m->growScale <= 0.0f)
				return false;
		}
		return true;
	case 300:
		mmove(m, m->moveAngle);
		if (9 < m->age && !aos_in_arena(m->pos, 20.0f)) {
			mine_spawn(w, m, 0x1452);
			return false;
		}
		if (m->age % 3 == 0)
			mine_spawn(w, m, 0x1451);
		return true;
	default:
		return true;
	}
}

static void missile_draw(aos_world *w, aos_missile *m) /* FUN_00671b90 */
{
	(void)w;
	int bank = team_bank(m->team); /* original: DAT_009b35c0 = +0x1c (p18) */
	switch (m->id) {
	case 100: case 0x96: case 0x6e: case 0xa0: { /* 0xf4 frame (age/3)%3, RotZ only */
		float s = (m->id == 0x6e || m->id == 0xa0) ? 1.25f : 1.0f; /* DAT_00815dbc */
		aos_dcmd *c = model("nt_mis%02d", (m->age / 3) % 3, bank, m->pos[0], m->pos[1], 64.0f, s, s);
		c->rotZ = aos_rad2bam(m->angle);
		break;
	}
	case 200: case 0xc9: { /* 0xf7 frames 0..2 */
		float sn = aos_sin(((float)(m->age % 0x32) / 50.0f) * TWO_PI);
		float a1 = sn * 0.15f + 0.85f, a2 = sn * 0.25f + 0.75f;
		float sy = 3.0f - m->growScale * 2.0f;
		for (int k = 0; k < 3; k++) {
			aos_dcmd *c = model("atch_ref%02d", k, bank, m->pos[0], m->pos[1], 128.0f, m->growScale, sy);
			c->rotZ = aos_rad2bam(m->angle);
			if (k == 0)
				c->rgba = m->growScale == 1.0f ? argb(1, 1, 1, 1) : argb(1, 0, 0, 0);
			else {
				c->rgba = argb(k == 1 ? a1 : a2, 1, 1, 1);
				c->blend = AOS_BLEND_ADD_ALPHA;
			}
		}
		break;
	}
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* hitbox family (type 8)                                              */

static int hitbox_spawn(aos_world *w, int32_t id, const float pos[3], float ang, int32_t owner, int32_t zero, int32_t team) /* FUN_005e6f50 */
{
	int32_t ht = 3, dmg;
	aos_shape s;
	float kb;
	uint8_t rh = 0;
	switch (id) {
	case 200: dmg = 200; s = (aos_shape){60.0f, 40.0f, 0}; kb = 15.0f; rh = 1; break;
	case 100: ht = 1; dmg = 200; s = (aos_shape){30.0f, 10.0f, 0}; kb = 10.0f; break;
	case 0x136: dmg = 0x46; s = (aos_shape){800.0f, 64.0f, 0}; kb = 10.0f; break;
	case 0xd2: dmg = 0x78; s = (aos_shape){100.0f, 80.0f, 0}; kb = 2.5f; break;   /* ver > 0x5a0 */
	case 0xdc: dmg = 200; s = (aos_shape){100.0f, 120.0f, 0}; kb = 20.0f; break;  /* ver > 0x5a0 */
	case 0xfa: dmg = 400; s = (aos_shape){120.0f, 40.0f, 0}; kb = 20.0f; rh = 1; break;
	case 300: dmg = 0x28; s = (aos_shape){800.0f, 48.0f, 0}; kb = 10.0f; break;
	case 0x12d: dmg = 0; s = (aos_shape){800.0f, 60.0f, 0}; kb = 5.0f; break;
	case 0x19a: dmg = 900; s = (aos_shape){0, 0, 320.0f}; kb = 30.0f; break;      /* ver > 0x58a */
	case 0x140: dmg = 0x5a; s = (aos_shape){800.0f, 82.0f, 0}; kb = 10.0f; break;
	case 400: dmg = 0; s = (aos_shape){140.0f, 50.0f, 0}; kb = 0; break;
	case 500: dmg = 300; s = (aos_shape){0, 0, 40.0f}; kb = 5.0f; break;
	case 600: dmg = 0x78; s = (aos_shape){160.0f, 140.0f, 0}; kb = 15.0f; break;
	default: return -1; /* includes 0x137 / 0x141 (laser off-ticks) */
	}
	int i = aos_hitbox_new(w, ID, id, pos, ang, owner, zero, team);
	if (i < 0)
		return -1;
	aos_hitbox *h = &w->obj.hitbox[i];
	h->hitType = ht;
	h->damage = dmg;
	h->shape = s;
	h->knockback = kb;
	h->rehitFlag = rh;
	return i;
}

static void streaks(aos_world *w, aos_hitbox *h, aos_player *v, int c)
{
	for (int k = -3; k < 3; k++) {
		float base = ((float)k / 3.0f) * PI;
		float a = base + aos_rand_real(w, -0.25f, 0.25f);
		eff0(w, 0x456, c, &v->x, a, h->ownerId, 0, h->team);
	}
}

static bool juggled(const aos_player *v)
{
	return v->state == AOS_ST_HITSTUN && (v->timer < 5 || 2.0f < v->speed);
}

static void hitbox_onhit(aos_world *w, aos_hitbox *h, aos_player *v) /* FUN_005e75b0 */
{
	switch (h->id) {
	case 100: {
		if (v->state == AOS_ST_HITSTUN && 2.0f < v->speed) {
			h->dealsDamage = 0;
			return;
		}
		int c = col(w, h->team, 0);
		eff0(w, 0x3f2, c, h->pos, h->angle + PI, h->ownerId, 0, h->team);
		aos_spawn_particle(w, 1000, c, h->pos, h->angle, h->ownerId, 0, 5);
		aos_spawn_spark(w, 1000, c, 6, 6, h->angle, h->pos, h->ownerId);
		aos_se(w, 0x13);
		return;
	}
	case 200:
		if (juggled(v)) {
			h->dealsDamage = 0;
			return;
		}
		streaks(w, h, v, col(w, h->team, 1));
		aos_screen_blur(w, 10, 0.75f, 1);
		aos_se(w, 0x18);
		return;
	case 0xd2:
		streaks(w, h, v, col(w, h->team, 1));
		aos_se_cd(w, 0x18, 3);
		return;
	case 0xdc:
		eff0(w, 0xa8d, 0xd, h->pos, 0, h->ownerId, 0, h->team);
		aos_se(w, 0x19);
		aos_screen_blur(w, 10, 0.75f, 1);
		return;
	case 0xfa:
		if (juggled(v) && w->frame - v->f174 < 0x28) {
			h->dealsDamage = 0;
			return;
		}
		eff0(w, 0xa8d, 0xd, h->pos, 0, h->ownerId, 0, h->team);
		aos_se(w, 0x19);
		aos_screen_blur(w, 10, 0.75f, 1);
		v->f174 = w->frame; /* ver >= 0x58b */
		return;
	case 500: {
		h->angle = aos_atan2(v->y - h->pos[1], v->x - h->pos[0]);
		for (int k = 0; k < 5; k++) {
			float q[3] = {v->x, v->y, v->z};
			q[0] = aos_rand_real(w, -30.0f, 30.0f) + q[0];
			q[1] = aos_rand_real(w, -30.0f, 30.0f) + q[1];
			eff0(w, 500, 0, q, 0, h->ownerId, k * -4, h->team);
		}
		aos_status(w, v, 1, 500);
		return;
	}
	case 600: {
		int c = col(w, h->team, 0);
		streaks(w, h, v, c);
		eff0(w, 0xaf0, c, h->pos, 0, h->ownerId, 0, h->team);
		aos_se(w, 0x18);
		aos_screen_blur(w, 10, 0.75f, 1);
		return;
	}
	case 0x19a:
		h->angle = aos_atan2(-v->y, -v->x);
		eff0(w, 0xa8d, 0, &v->x, 0, h->ownerId, 0, h->team);
		return;
	case 400:
		player_of_team(w, h->team)->hitFlag = 1;
		aos_se(w, 0x18);
		return;
	case 300:
	case 0x136:
	case 0x140: {
		int c = col(w, h->team, 0);
		streaks(w, h, v, c);
		aos_spawn_particle(w, 1000, c, &v->x, h->angle, h->ownerId, 0, 3);
		aos_spawn_spark(w, 1000, c, 10, 3, h->angle, &v->x, h->ownerId);
		aos_se_cd(w, 0x13, 4);
		return;
	}
	default:
		return;
	}
}

/* ------------------------------------------------------------------ */
/* laser family (type 8)                                               */

static int laser_spawn(aos_world *w, int32_t id, const float pos[3], float ang, int32_t owner, int32_t team, int32_t p18) /* FUN_00623170 */
{
	if (id != 100 && id != 200 && id != 0xfa && id != 0x104)
		return -1;
	int i = aos_laser_new(w, ID, id, pos, ang, owner, team, p18);
	if (i < 0)
		return -1;
	aos_laser *l = &w->obj.laser[i];
	l->damage = id == 100 ? 10 : 0;
	l->f48 = id == 100 ? 200 : 999;
	l->hitType = 3;
	l->blockMode = id == 100 ? 2 : 1;
	l->knockback = 5.0f;
	l->stun = 1;
	return i;
}

static void laser_follow(aos_world *w, aos_laser *l)
{
	if (l->f4d == 0) {
		aos_enemy *e = aos_enemy_by_id(w, l->ownerId);
		if (e) {
			l->origin[0] = e->pos[0];
			l->origin[1] = e->pos[1];
			l->origin[2] = e->pos[2];
			l->angle = e->angle2;
		}
	}
}

static bool laser_update(aos_world *w, aos_laser *l) /* FUN_006249a0 */
{
	if (l->id == 100) {
		l->nseg = 0;
		if (0x1d < l->age)
			return false;
		if (l->age == 1)
			eff0(w, 0x546, col(w, l->team, 1), l->origin, l->angle, l->ownerId, 0, l->team);
		aos_shape s = {0, 0, 20.0f};
		if (0x14 < l->age)
			s.radius = 0;
		for (int k = 0; k < 0x3c; k++) {
			float q[3] = {aos_cos(l->angle) * (float)k * 30.0f + l->origin[0],
			              aos_sin(l->angle) * (float)k * 30.0f + l->origin[1], 0};
			aos_laser_push_seg(l, &s, q);
		}
		l->damage = l->age % 4 == 0 ? 0x32 : 0;
		return true;
	}
	int32_t st = l->id == 200 ? 0xea : 0xeb;
	int32_t hb1 = l->id == 200 ? 300 : l->id == 0xfa ? 0x136 : 0x140;
	int32_t hb2 = l->id == 200 ? 0x12d : l->id == 0xfa ? 0x137 : 0x141;
	l->nseg = 0;
	aos_player *own = player_of_team(w, l->team);
	if (own->state != st && l->age < 0x33) {
		l->age = 0x32;
		if (l->id == 200)
			l->f4d = 1; /* ver > 0x58a */
	}
	if (l->id == 200 && 10 < l->age && l->age < 0x32 && l->age % 5 == 0 && !aos_use_gauge(own, 0xea))
		l->age = 0x32;
	laser_follow(w, l);
	if (0x45 < l->age)
		return false;
	if (9 < l->age && l->age < 0x33) {
		float q[3] = {aos_cos(l->angle) * 800.0f + l->origin[0], aos_sin(l->angle) * 800.0f + l->origin[1], 0};
		aos_spawn_hitbox(w, ID, l->age % 5 == 0 ? hb1 : hb2, q, l->angle, l->ownerId, 0, l->team);
	}
	return true;
}

static void laser_onhit(aos_world *w, aos_laser *l, int32_t seg, aos_player *p) /* FUN_00624850 */
{
	(void)p;
	if (l->id != 100 || l->age % 2 != 0)
		return;
	int c = col(w, l->team, 1);
	aos_spawn_spark(w, 1000, c, 10, 1, l->angle, l->seg[seg].pos, l->ownerId);
	for (int k = -2; k < 3; k++) {
		float a = (float)k * 0.65f + l->angle + aos_rand_real(w, -0.45f, 0.45f);
		eff0(w, 0x456, c, l->seg[seg].pos, a, l->ownerId, 0, l->team);
	}
}

static void laser_draw(aos_world *w, aos_laser *l) /* FUN_00623440 (sprite 0xf6 nt_laser, bank +0x14) */
{
	(void)w;
	int bank = team_bank(l->team);
	if (l->id == 100) {
		bool pulse = (l->age / 3) % 2 == 0;
		for (int k = 0; k < l->nseg - 1; k++) {
			const float *q = l->seg[k].pos;
			if (k == 0) {
				float h = 60.0f + (pulse ? 10.0f : 0.0f);
				blit("nt_laser00", bank, q[0], q[1], h, h, 0x1c0, 0xc0, 0x200, 0xff, l->angle, 0xffffffffu, AOS_BLEND_ADD_ALPHA);
			} else {
				float hh = 50.0f + (pulse ? 10.0f : 0.0f);
				if (0x14 < l->age)
					hh = ((float)(0x1e - l->age) / 10.0f) * hh;
				blit("nt_laser00", bank, q[0], q[1], 50.0f, hh, 0x140, 0xc0, 0x17f, 0xff, l->angle, 0x20ffffffu, AOS_BLEND_ADD_ALPHA);
				float h2 = 10.0f;
				if (0x14 < l->age)
					h2 = ((float)(0x1e - l->age) / 10.0f) * 10.0f;
				blit("nt_laser00", bank, q[0], q[1], 50.0f, h2, 0x100, 0xc0, 0x13f, 0xff, l->angle, 0xffffffffu, AOS_BLEND_ADD_ALPHA);
			}
		}
		return;
	}
	float hA, hB, hCore;
	if (l->id == 200) { hA = 180.0f; hB = 150.0f; hCore = 40.0f; }        /* DAT_00815258/0081414c/008140e0 */
	else if (l->id == 0xfa) { hA = 280.0f; hB = 240.0f; hCore = 60.0f; }  /* DAT_00814f14/00815584/00813a58 */
	else if (l->id == 0x104) { hA = 320.0f; hB = 280.0f; hCore = 70.0f; } /* DAT_008178c0/00814f14/008145ec */
	else return;
	float cx = aos_cos(l->angle) * 1000.0f + l->origin[0], cy = aos_sin(l->angle) * 1000.0f + l->origin[1];
	float h = (l->age / 2) % 2 == 0 ? hB : hA;
	if (l->age < 10)
		h = (float)(l->age * -3 + 0x28) * 0.1f * h;
	if (0x32 < l->age)
		h = ((float)(0x46 - l->age) / 20.0f) * h;
	blit("nt_laser01", bank, cx, cy, 1000.0f, h, 0, 100, 0x1ff, 199, l->angle, 0x40ffffffu, AOS_BLEND_ALPHA);
	h *= 0.75f;
	blit("nt_laser01", bank, cx, cy, 1000.0f, h, 0, 0, 0x1ff, 99, l->angle, 0x80ffffffu, AOS_BLEND_ADD_ALPHA);
	h = hCore;
	if (l->age < 10)
		h = (float)(l->age * -3 + 0x28) * 0.1f * h;
	if (0x32 < l->age)
		h = ((float)(0x46 - l->age) / 20.0f) * h;
	float r = (float)(((l->age / 2) % 3) * 0x28 + 200);
	blit("nt_laser01", bank, cx, cy, 1000.0f, h, 0, r, 0x1ff, r + 39.0f, l->angle, 0xc0ffffffu, AOS_BLEND_ADD_ALPHA);
}

/* ------------------------------------------------------------------ */
/* effect family (type 8, gameplay-free but simulated in order)        */

static int effect_spawn(aos_world *w, int32_t id, int32_t var, const float pos[3], float ang, int32_t owner, int32_t age0, int32_t team) /* FUN_00500ae0 */
{
	float sc = 1.0f, al = 1.0f, aux2 = 0;
	int layer = 1;
	switch (id) {
	case 100: case 0x96: case 0xaa: break;
	case 200: sc = 2.0f; al = 0; break;
	case 300: case 0x12d: case 0x15e: layer = 0; break;
	case 400: sc = 0; al = 0; break;
	case 0x19a: sc = 1.0f; al = 0; aux2 = aos_rand_real(w, 1.0f, 4.0f); break;
	default: return -1;
	}
	int i = aos_effect_new(w, ID, id, var, pos, ang, owner, age0, team);
	if (i < 0)
		return -1;
	aos_effect *e = &w->obj.effect[i];
	e->drawLayer = layer;
	e->scale = sc;
	e->alpha = al;
	e->aux = 0;
	e->aux2 = aux2;
	return i;
}

static bool effect_update(aos_world *w, aos_effect *e) /* FUN_00502f80 */
{
	switch (e->id) {
	case 100:
		if (3 < e->age) e->alpha -= 0.1f;
		return 0.0f < e->alpha;
	case 0x96:
		if (0xf < e->age) e->alpha -= 0.1f;
		return 0.0f < e->alpha;
	case 0xaa:
		if (1 < e->age) e->alpha -= 0.1f;
		return 0.0f < e->alpha;
	case 200:
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
	case 300: case 0x12d: case 0x15e:
		e->alpha -= 0.1f;
		return !(e->alpha <= 0.0f);
	case 400:
		if (e->age < 10) {
			e->alpha += 0.1f;
			e->scale -= 0.05f;
		} else {
			e->alpha -= 0.1f;
			if (e->alpha <= 0.0f)
				return false;
		}
		return true;
	case 0x19a: {
		e->alpha += 0.05f;
		if (1.0f < e->alpha)
			e->alpha = 1.0f;
		e->pos[0] = aos_cos(e->moveDir) * e->aux + e->pos[0];
		e->pos[1] = aos_sin(e->moveDir) * e->aux + e->pos[1];
		e->scale -= 0.01f;
		aos_enemy *h = aos_enemy_by_id(w, e->ownerId);
		if (!h)
			return false;
		float cx = aos_cos(h->angle2) * 150.0f + h->pos[0];
		float cy = aos_sin(h->angle2) * 150.0f + h->pos[1];
		e->pos[0] = cx + (e->pos[0] - cx) * e->scale;
		e->pos[1] = cy + (e->pos[1] - cy) * e->scale;
		return !(e->scale <= 0.0f);
	}
	default:
		return true;
	}
}

/* explosion sheets DAT_0087fccc[v/8] (explosion0/1), cell (v%8)&3 rows x (v%8)/4 columns of 256x128 */
static void explosion_cells(int32_t v, const char **tex, float *x0, float *y0)
{
	int32_t r = v % 8;
	*tex = (v / 8) % 2 ? "explosion1" : "explosion0";
	*y0 = (float)((r & 3) * 0x80);
	*x0 = (float)((r / 4) * 0x100);
}

static void effect_draw(aos_world *w, aos_effect *e) /* FUN_00500fb0 */
{
	float x = e->pos[0], y = e->pos[1];
	int32_t v = e->variant;
	switch (e->id) {
	case 100:
	case 0x96: { /* beamsphere glow (team tint by palette) + core */
		int pal = player_of_team(w, e->team)->color; /* DAT_00885a00[team] */
		uint32_t a = a8(64.0f * e->alpha);
		uint32_t tint = pal == 0 ? (a << 24 | (a & 0x7f) << 17 | 0xffff) : (a << 24 | 0xffff00 | (a & 0x7f) << 1);
		sphere_glow(x, y, v, 60.0f * e->scale, (e->id == 100 ? 12.0f : 16.0f) * e->scale, e->rotation, tint, AOS_BLEND_ALPHA);
		sphere_core(x, y, v, (e->id == 100 ? 30.0f : 40.0f) * e->scale, 5.0f * e->scale, e->rotation,
		            a8(255.0f * e->alpha) << 24 | 0xffffff, AOS_BLEND_ADD_ALPHA);
		break;
	}
	case 0xaa: { /* beamemitb trail + head */
		int pal = player_of_team(w, e->team)->color;
		float x0 = (float)((v / 8) * 0xf0), y0 = (float)((v % 8) * 0x40);
		blit("beamemitb", AOS_BANK_COMMON, x, y, 60.0f, 40.0f, x0, y0, x0 + 0x77, y0 + 0x3f, e->rotation,
		     a8(32.0f * e->alpha) << 24 | 0xffffff, AOS_BLEND_ALPHA);
		uint32_t a = a8(255.0f * e->alpha);
		if (e->age < 2) {
			float bx = (float)((v / 8) * 0x100);
			blit("beamemit", AOS_BANK_COMMON, x, y, 48.0f, 16.0f, bx, y0, bx + 0x7f, y0 + 0x3f, e->rotation, 0xffffffffu, AOS_BLEND_ADD_ALPHA);
		} else {
			uint32_t c = e->age < 5 ? (a << 24 | 0xffffff) : pal == 0 ? (a << 24 | 0x80ff) : (a << 24 | 0xffff00);
			blit("beamemitb", AOS_BANK_COMMON, x, y, 40.0f, 8.0f, x0 + 0x78, y0, x0 + 0xef, y0 + 0x3f, e->rotation, c, AOS_BLEND_ADD_ALPHA);
		}
		break;
	}
	case 200:
	case 400: { /* explosion sheet, two quads */
		const char *tex;
		float x0, y0;
		explosion_cells(v, &tex, &x0, &y0);
		float h = 180.0f * e->scale; /* DAT_00815258 */
		/* 200 uses the rotating blit with +0x38 un-negated (= world -rotation); 400 has no rotation */
		float rot = e->id == 200 ? -e->rotation : 0;
		blit(tex, AOS_BANK_COMMON, x, y, h, h, x0, y0, x0 + 0x7f, y0 + 0x7f, rot, a8(128.0f * e->alpha) << 24 | 0xffffff, AOS_BLEND_ADD_ALPHA);
		h = (e->id == 200 ? 200.0f : 180.0f) * e->scale; /* DAT_008147cc / DAT_00815258 */
		if (e->age % 2 == 0)
			h *= 0.85f;
		uint32_t a2 = e->id == 200 ? a8(255.0f * e->alpha) : a8(200.0f * e->alpha);
		blit(tex, AOS_BANK_COMMON, x, y, h, h, x0 + 0x80, y0, x0 + 0xff, y0 + 0x7f, rot, a2 << 24 | 0xffffff, AOS_BLEND_ADD_ALPHA);
		break;
	}
	case 300:
	case 0x12d:
	case 0x15e: { /* Nath sprites 0xfc frame 1/2 (fist trails) and 0xfd frame 0 (punch) as mirrored blits */
		bool punch = e->id == 0x15e;
		float h = (punch ? 192.0f : 72.0f) * e->scale; /* DAT_0081415c / DAT_008178ac */
		uint32_t a = a8(255.0f * e->alpha);
		char tex[16] = "atch_hand01";
		if (e->id == 0x12d) tex[10] = '2';
		aos_dcmd *c = blit(punch ? "atch_cannon00" : tex, team_bank(e->team), x, y, h, h, 0, 0,
		                   punch ? 0x1ff : 0xff, punch ? 0x1ff : 0xff, e->rotation, a << 24 | a << 16 | a << 8 | a,
		                   AOS_BLEND_ALPHA);
		c->flipX = 1; /* dst rect left > right */
		if (HALF_PI <= aos_fabs(e->rotation))
			c->sy = -1.0f; /* and top > bottom */
		break;
	}
	case 0x19a: { /* beamsphere glow + core, faded by scale */
		uint32_t a1 = a8(128.0f * e->alpha * e->scale), a2 = a8(255.0f * e->alpha * e->scale);
		sphere_glow(x, y, v, 120.0f * e->scale, 120.0f * e->scale, 0, a1 << 24 | 0xffffff, AOS_BLEND_ADD_ALPHA);
		sphere_core(x, y, v, 40.0f * e->scale, 40.0f * e->scale, 0, a2 << 24 | 0xffffff, AOS_BLEND_ADD_ALPHA);
		break;
	}
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* player draw (T1)                                                    */

static aos_dcmd *spr(aos_world *w, aos_player *p, const char *fmt, int frame)
{
	aos_dcmd *c = aos_draw(aos_texf(fmt, frame), aos_pidx(w, p), p->x, p->y, 0);
	c->halfW = c->halfH = 48.0f; /* 96 (DAT_00814118) */
	return c;
}

static aos_dcmd *spr_aim(aos_world *w, aos_player *p, const char *fmt, int frame)
{
	aos_dcmd *c = spr(w, p, fmt, frame);
	c->rotZ = aos_rad2bam(p->faceAngle);
	c->flipX = 1;
	if (HALF_PI <= aos_fabs(p->faceAngle))
		c->sy = -1.0f; /* RotX(180°) */
	return c;
}

static int pp(int32_t t, int n)
{
	int f = (t / n) & 3;
	return f == 3 ? 1 : f;
}

static void dr_neutral(aos_world *w, aos_player *p) /* 0xc8 FUN_00717b30 */
{
	const char *s = "nt_std%02d";
	if (p->up > 1) s = "nt_fwd%02d";
	if (p->down > 1) s = "nt_bck%02d";
	if (p->facingRight == 0 && p->left > 1) s = "nt_fwd%02d";
	if (p->facingRight == 1 && p->left > 1) s = "nt_bck%02d";
	if (p->facingRight == 0 && p->right > 1) s = "nt_bck%02d";
	if (p->facingRight == 1 && p->right > 1) s = "nt_fwd%02d";
	aos_dcmd *c = spr(w, p, s, pp(p->timer, 5));
	c->flipX = p->facingRight != 0;
	int32_t rot[3] = {0, p->facingRight ? aos_deg2bam(0xb4) : 0, 0};
	aos_afterimage(w, c, 1, p, rot);
}

static void dr_dash(aos_world *w, aos_player *p) /* 0xc9 FUN_00717de0 */
{
	int f = pp(p->timer, 3);
	float a = aos_fabs(p->faceAngle);
	if ((p->facingRight == 1 && a > HALF_PI) || (p->facingRight == 0 && a < HALF_PI))
		f += 3;
	aos_dcmd *c = spr_aim(w, p, "nt_dsh%02d", f);
	int32_t rot[3] = {aos_fabs(p->faceAngle) >= HALF_PI ? aos_deg2bam(0xb4) : 0, aos_deg2bam(0xb4),
	                  aos_rad2bam(p->faceAngle)};
	aos_afterimage(w, c, 1, p, rot);
}

static void dr_ko(aos_world *w, aos_player *p) /* 0xca FUN_00718750 */
{
	int f = pp(p->timer, 3);
	if (w->frame % 2 == 0)
		f += 3;
	if (p->timer < 100)
		spr_aim(w, p, "nt_los%02d", f);
}

static void dr_hit(aos_world *w, aos_player *p) { spr_aim(w, p, "nt_los%02d", pp(p->timer, 3)); } /* 0xcb FUN_007181e0 */
static void dr_sld(aos_world *w, aos_player *p) { spr_aim(w, p, "nt_sld%02d", 0); }                 /* 0xcc FUN_007184b0 */

static void dr_transform(aos_world *w, aos_player *p) /* 0xd2/0xd3/0xd4 FUN_00718a50/00718d30/00719010 */
{
	int f = p->timer / 4;
	if (0 < f)
		f = ((f - 1) & 3) + 1;
	if (f == 4)
		f = 2;
	spr_aim(w, p, "nt_sld%02d", f);
}

static void dr_attack(aos_world *w, aos_player *p) /* 0xd5..0xf1 (FUN_007192f0 … FUN_0071ccb0) */
{
	int f = p->timer / 4;
	spr_aim(w, p, "nt_sld%02d", f > 3 ? 3 : f);
}

/* ------------------------------------------------------------------ */

void aos_register_nath(void)
{
	aos_enemy_motion = aos_motion_step;

	aos_tab.inputA[ID] = input_a;
	aos_tab.inputB[ID] = input_b;
	/* CPU AI FUN_007172e0 / FUN_00716a00 not ported */

	aos_tab.T2[0xc8] = st_neutral;
	aos_tab.T2[0xc9] = st_stub;
	aos_tab.T2[0xca] = st_ko_hook;
	aos_tab.T2[0xcb] = st_stub;
	aos_tab.T2[0xcc] = st_stub;
	aos_tab.T2[0xd2] = st_boot;
	aos_tab.T2[0xd3] = st_ext1;
	aos_tab.T2[0xd4] = st_ext2;
	aos_tab.T2[0xd5] = st_spread;
	aos_tab.T2[0xd8] = st_homing_bombing;
	aos_tab.T2[0xd9] = st_carpet;
	aos_tab.T2[0xda] = st_b23;
	aos_tab.T2[0xdc] = st_b_h3;
	aos_tab.T2[0xdd] = st_reflecting;
	aos_tab.T2[0xdf] = st_reflector;
	aos_tab.T2[0xe3] = st_bits;
	aos_tab.T2[0xe4] = st_bits_h1;
	aos_tab.T2[0xe5] = st_bits_h2;
	aos_tab.T2[0xe6] = st_bits_h3;
	aos_tab.T2[0xe7] = st_metal_fist;
	aos_tab.T2[0xe8] = st_pounding;
	aos_tab.T2[0xe9] = st_punch2;
	aos_tab.T2[0xea] = st_hand_laser;
	aos_tab.T2[0xeb] = st_hyper_laser;
	aos_tab.T2[0xec] = st_grab;
	aos_tab.T2[0xed] = st_grab_hold;
	aos_tab.T2[0xee] = st_mines;
	aos_tab.T2[0xef] = st_bird;
	aos_tab.T2[0xf0] = st_bird_pass;
	aos_tab.T2[0xf1] = st_bird_land;

	aos_tab.T1[0xc8] = dr_neutral;
	aos_tab.T1[0xc9] = dr_dash;
	aos_tab.T1[0xca] = dr_ko;
	aos_tab.T1[0xcb] = dr_hit;
	aos_tab.T1[0xcc] = dr_sld;
	aos_tab.T1[0xd2] = dr_transform;
	aos_tab.T1[0xd3] = dr_transform;
	aos_tab.T1[0xd4] = dr_transform;
	static const int16_t atk[] = {0xd5, 0xd8, 0xd9, 0xda, 0xdc, 0xdd, 0xdf, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
	                              0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, 0xf0, 0xf1};
	for (size_t i = 0; i < sizeof atk / sizeof atk[0]; i++)
		aos_tab.T1[atk[i]] = dr_attack;

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
	aos_otab.missileOnHit[ID] = missile_onhit;
	aos_otab.missileOnKilled[ID] = missile_onkilled;
	aos_otab.missileOnAbsorb[ID] = missile_onabsorb;
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
