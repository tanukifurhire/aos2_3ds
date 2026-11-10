/* Hime (engine id 16 = AOS_CH_HIME) — 1:1 port of her code in aos2.exe.
 * Spec: docs/re/hime.md. Every function names its original address; the
 * decompiled code (tools/re/dec/all.c) is the source of truth. Only the
 * version-0x5a2 branches of DAT_0089426c are implemented.
 * Layout: 1 helpers, 2 input, 3 T2, 4 T1, 5 objects, 6 registration. */
#include <stddef.h>

#include "../draw.h"
#include "../fx.h"
#include "../sim.h"

#define ID AOS_CH_HIME
#define HALF_PI 1.5707964f /* DAT_00814104 */

/* Globals shared by both players (w->charGlobal[ID]). */
typedef struct {
	uint8_t paradisePaid; /* DAT_0086c23c (FUN_006c6450, ver >= 0x5a1) */
} hime_global;

static hime_global *G(aos_world *w) { return (hime_global *)w->charGlobal[ID]; }

/* ================================================================== */
/* 1. helpers                                                          */

static aos_player *team_player(aos_world *w, int32_t team) { return &w->p[team == 2 ? 1 : 0]; } /* FUN_00780450 */
static int team_bank(int32_t team) { return team == 2 ? AOS_BANK_P1 : AOS_BANK_P0; }
static int32_t tcol(aos_world *w, int32_t team, int k) { return aos_team_colour(w, team, k); } /* DAT_008859f4+team*0x10+4k */

static int fx0(aos_world *w, int32_t id, int32_t variant, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team)
{
	return aos_spawn_effect(w, 0, id, variant, pos, angle, owner, age0, team);
}
static int fxh(aos_world *w, int32_t id, int32_t variant, const float pos[3], float angle, int32_t owner, int32_t team)
{
	return aos_spawn_effect(w, ID, id, variant, pos, angle, owner, 0, team); /* DAT_008816d8 */
}

static void walk_mul(aos_player *p, float mul)
{
	float s = aos_tab.stats[p->id].walk * mul;
	if (p->up > 1) p->y += s;
	if (p->down > 1) p->y -= s;
	if (p->left > 1) p->x -= s;
	if (p->right > 1) p->x += s;
}

static void face_by_x(aos_world *w, aos_player *p)
{
	aos_player *o = aos_opponent(w, p);
	p->faceAngle = (o->x < p->x || o->x == p->x) ? AOS_PI : 0.0f;
	p->dashAngle = p->faceAngle;
}

/* pos + rot(v, ang) */
static void offset_pt(const aos_player *p, float vx, float vy, float ang, float out[3])
{
	float v[2] = {vx, vy};
	aos_vec_rotate(v, ang);
	out[0] = v[0] + p->x;
	out[1] = v[1] + p->y;
	out[2] = p->z;
}

static float dist2d(const float *a, const float *b) /* FUN_005f1c30 */
{
	float dx = a[0] - b[0], dy = a[1] - b[1];
	return aos_sqrt(dx * dx + dy * dy);
}

static float players_dist(aos_world *w) { return dist2d(&w->p[0].x, &w->p[1].x); } /* FUN_00619e20(0) */

/* FUN_006b1d70(p, range, speed): proximity knock. The trailing FUN_004a9f10 on the
 * camera-focus vector 0x895144 = aos_camera_focus_clear (checked in the disassembly). */
static void prox_knock(aos_world *w, aos_player *p, float range, float speed)
{
	aos_player *o = aos_opponent(w, p);
	if (o->state != AOS_ST_KO && o->hittable == 1 && o->statusOn[AOS_STATUS_STUNIMMUNE] == 0 &&
	    dist2d(&p->x, &o->x) < range) {
		o->state = AOS_ST_HITSTUN;
		o->timer = 0;
		o->faceAngle = aos_atan2(p->y - o->y, p->x - o->x);
		o->moveAngle = o->faceAngle + AOS_PI;
		o->speed = speed;
		aos_se(w, 0x19);
		aos_camera_focus_clear(w); /* FUN_004a9f10 on the camera focus vector 0x895144 (mov ecx,0x895144 @0x6b1e7d) */
	}
}

/* FUN_00780a50 */
static bool can_affect(aos_world *w, aos_player *o)
{
	return o->barrierTime == 0 && o->state != AOS_ST_KO && o->hittable != 0 &&
	       o->statusOn[AOS_STATUS_STUNIMMUNE] != 1 && !w->roundOver;
}

/* ================================================================== */
/* 2. input dispatch                                                   */

static bool st(aos_world *w, aos_player *p, int32_t s)
{
	aos_set_state(w, p, s);
	return true;
}

static bool normal_moves(aos_world *w, aos_player *p) /* FUN_006c85d0 second half */
{
	if (p->state == AOS_ST_NEUTRAL || p->state == AOS_ST_DASH) {
		if (p->D == 3) {
			aos_dash(w, p, 0);
		} else if (p->Shield == 3 && aos_spend_hyper(p, 0.5f) && aos_may_shield(w, p)) {
			aos_shield(w, p);
		} else {
			if (p->S > 1 && p->B == 3) return st(w, p, 0x201);
			if (p->S > 1 && p->A == 3) return st(w, p, 0x1ff);
			if (p->B == 3) return st(w, p, 0x1fe);
			if (p->D > 1 && p->A == 3) return st(w, p, 0x1fc);
			if (p->A == 3) return st(w, p, 0x1fb);
		}
	}
	return aos_tab.T2[p->state] ? aos_tab.T2[p->state](w, p) : true;
}

static bool lifelong(aos_world *w, aos_player *p)
{
	return st(w, p, 240.0f <= aos_dist(w, p) ? 0x205 : 0x204); /* DAT_00815584 */
}

static bool input_a(aos_world *w, aos_player *p) /* FUN_006c85d0 */
{
	if (aos_may_hyper(w, p)) {
		if (p->S > 1 && p->B > 1 && p->H == 3 && aos_spend_hyper(p, 3.0f)) return st(w, p, 0x207);
		if (p->S > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f)) return st(w, p, 0x206);
		if (p->B > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f)) return lifelong(w, p);
		if (p->D > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f)) return st(w, p, 0x203);
		if (p->H == 3 && aos_spend_hyper(p, 1.0f)) return st(w, p, 0x202);
	}
	return normal_moves(w, p);
}

static bool input_b(aos_world *w, aos_player *p) /* FUN_006c8ad0 (scheme B) */
{
	if (aos_may_hyper(w, p)) {
		if (p->H > 1 && p->Shield == 3 && aos_spend_hyper(p, 3.0f)) return st(w, p, 0x207);
		if (p->H > 1 && p->S == 3 && aos_spend_hyper(p, 1.0f)) return st(w, p, 0x206);
		if (p->H > 1 && p->B == 3 && aos_spend_hyper(p, 1.0f)) return lifelong(w, p);
		if (p->H > 1 && p->D == 3 && aos_spend_hyper(p, 1.0f)) return st(w, p, 0x203);
		if (p->H > 1 && p->A == 3 && aos_spend_hyper(p, 1.0f)) return st(w, p, 0x202);
	}
	return normal_moves(w, p);
}

/* ================================================================== */
/* 3. T2 state handlers                                                */

/* dash-cancel from `from`, neutral after `end`; true = normal branch ran */
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

static bool st_neutral(aos_world *w, aos_player *p) /* 0x1f0 T2 FUN_006c48d0 */
{
	(void)w;
	walk_mul(p, 1.0f);
	p->faceAngle = p->facingRight == 1 ? 0.0f : AOS_PI;
	return true;
}

static bool st_stub(aos_world *w, aos_player *p) /* 0x1f1..0x1f4 FUN_006c49c0/49f0/49d0/49e0 */
{
	(void)w, (void)p;
	return true;
}

static void missile(aos_world *w, aos_player *p, int32_t id, const float pos[3], float ang)
{
	aos_spawn_missile(w, ID, id, pos, ang, p->objId, 0, p->side, p->color);
}

static bool st_faint_breeze(aos_world *w, aos_player *p) /* 0x1fa T2 FUN_006c4a00 */
{
	float pt[3];
	if (p->timer == 12 && aos_use_gauge(p, 0x1fa)) {
		float y = aos_rand_real(w, 60.0f, 120.0f); /* DAT_00813a58, DAT_00814174 */
		offset_pt(p, -10.0f, y, p->faceAngle, pt); /* DAT_00817c10 */
		missile(w, p, 100, pt, p->faceAngle);
		aos_se(w, 0x10);
	}
	if (p->timer == 12 && aos_use_gauge(p, 0x1fa)) {
		float y = -aos_rand_real(w, 60.0f, 120.0f);
		offset_pt(p, -10.0f, y, p->faceAngle, pt);
		missile(w, p, 0x96, pt, p->faceAngle);
		aos_se(w, 0x10);
	}
	end_move(w, p, 15, 30);
	return true;
}

static bool st_needle_fruit(aos_world *w, aos_player *p) /* 0x1fb T2 FUN_006c4c90 */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer < 12 && p->A < 1) {
		p->state = 0x1fa;
		return aos_tab.T2[0x1fa](w, p);
	}
	if (p->timer == 15 && aos_use_gauge(p, 0x1fb)) {
		p->faceAngle = aos_aim(w, p);
		for (int i = -12; i < 12; i++) {
			float a = ((float)i / 12.0f) * AOS_PI; /* DAT_00813f54 */
			float pt[3] = {aos_cos(a) * 120.0f + p->x, aos_sin(a) * 120.0f + p->y, p->z};
			missile(w, p, (i % 2 == 0) ? 0x6e : 0xa0, pt, a);
		}
		aos_se(w, 0x10);
	}
	end_move(w, p, 30, 50);
	return true;
}

static bool st_plenty(aos_world *w, aos_player *p) /* 0x1fc T2 FUN_006c5020 Faint Breeze (Plenty) */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer > 9 && p->timer < 0x22 && (p->timer - 10) % 4 == 0 && aos_use_gauge(p, 0x1fc)) {
		float v[2] = {aos_rand_real(w, 60.0f, 160.0f), 0.0f}; /* DAT_00813a58, DAT_00814168 */
		aos_vec_rotate(v, aos_rand_real(w, -AOS_PI, AOS_PI));
		float pt[3] = {v[0] + p->x, v[1] + p->y, p->z};
		p->faceAngle = aos_aim(w, p);
		missile(w, p, ((p->timer - 10) / 4) % 2 == 0 ? 0x69 : 0x9b, pt, p->faceAngle);
		aos_se(w, 0x10);
	}
	if (end_move(w, p, 0x23, 0x32))
		p->friction *= 0.5f;
	return true;
}

static bool st_paradise(aos_world *w, aos_player *p) /* 0x1fd T2 FUN_006c6450 Paradise of Faint Pain (ver >= 0x5a1) */
{
	static const float X[8] = {-18.75f, -37.5f, -56.25f, -75.0f, -93.75f, -112.5f, -131.25f, -150.0f}; /* DAT_00825488.. */
	static const float Y[8] = {157.5f, 22.5f, 135.0f, 45.0f, 112.5f, 67.5f, 90.0f, 85.0f};
	/* missile per t-12 (jump table @0x6c6af0): 0 = 0xfa, 1 = none, 2 = 200 */
	static const uint8_t KIND[17] = {0, 2, 2, 0, 0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1};
	int t = p->timer;
	if (t == 12)
		G(w)->paradisePaid = aos_use_gauge(p, 0x1fd) ? 1 : 0;
	if (t > 11 && t < 0x1d && G(w)->paradisePaid == 1) {
		float x = 0, y = 0;
		if (t >= 13) {
			int k = (t - 13) / 2;
			x = X[k];
			y = Y[k];
			if (t % 2 != 0)
				y *= -1.0f; /* DAT_00853bb4 */
		}
		float pt[3];
		offset_pt(p, x, y, p->faceAngle, pt);
		int kind = KIND[t - 12];
		if (kind == 0)
			missile(w, p, 0xfa, pt, p->faceAngle);
		else if (kind == 2)
			missile(w, p, 200, pt, p->faceAngle);
		aos_se_cd(w, 0xe, 4);
	}
	end_move(w, p, 0x1e, 0x32);
	return true;
}

static bool st_fever(aos_world *w, aos_player *p) /* 0x1fe T2 FUN_006c6080 Fever */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer < 0xb && p->B < 1) {
		p->state = 0x1fd;
		return aos_tab.T2[0x1fd](w, p);
	}
	if (p->timer == 10 && aos_use_gauge(p, 0x1fe)) {
		aos_spawn_laser(w, ID, 100, &p->x, p->faceAngle, p->objId, p->side, p->color);
		aos_se(w, 0x12);
	}
	end_move(w, p, 0x32, 0x3c);
	return true;
}

/* FUN_006c3e20(team): true when no own chain missile 600 / 0x259 exists */
static bool no_chain(aos_world *w, int32_t team)
{
	for (int i = 0; i < w->obj.nmissile; i++) {
		aos_missile *m = &w->obj.missile[i];
		if (m->team == team && m->type == ID && (m->id == 600 || m->id == 0x259))
			return false;
	}
	return true;
}

static bool st_binding(aos_world *w, aos_player *p) /* 0x1ff T2 FUN_006c52b0 Delicate Binding */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	bool fired = false;
	if (p->timer == 5 && no_chain(w, p->side) && aos_use_gauge(p, 0x1ff)) {
		p->speed = 0;
		float pt[3] = {aos_cos(p->faceAngle) * 30.0f + p->x, aos_sin(p->faceAngle) * 30.0f + p->y, p->z}; /* DAT_008140e8 */
		fxh(w, 0x1fe, 0, pt, p->faceAngle, p->objId, p->side);
		missile(w, p, 600, pt, p->faceAngle);
		fired = true;
	}
	if (!fired && p->timer == 5)
		p->timer = 0x14;
	if (p->hitFlag == 1 && p->timer > 5 && p->S > 1 && p->B == 3)
		return st(w, p, 0x201);
	if (p->hitFlag == 1 && p->timer > 5 && p->A == 3)
		return st(w, p, 0x200);
	if (end_move(w, p, 0x28, 0x50))
		p->friction *= 1.5f; /* DAT_00814110 */
	return true;
}

static bool st_binding2(aos_world *w, aos_player *p) /* 0x200 T2 FUN_006c55e0 */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer > 4 && p->timer < 0x1f && p->timer % 5 == 0 && aos_use_gauge(p, 0x200)) {
		p->speed = 0;
		float x = aos_rand_real(w, -80.0f, -40.0f); /* DAT_00817e00, DAT_00817dc0 */
		float y = aos_rand_real(w, 20.0f, 160.0f);  /* DAT_008140fc, DAT_00814168 */
		if ((p->timer / 5) % 2 == 0)
			y *= -1.0f;
		float pt[3];
		offset_pt(p, x, y, p->faceAngle, pt);
		aos_player *o = aos_opponent(w, p);
		float a = aos_atan2(o->y - pt[1], o->x - pt[0]);
		fxh(w, 0x1fe, 0, pt, p->faceAngle, p->objId, p->side);
		missile(w, p, 0x262, pt, a);
	}
	if (p->hitFlag == 1 && p->timer > 5 && p->S > 1 && p->B == 3)
		return st(w, p, 0x201);
	if (p->hitFlag == 1 && p->timer > 5 && p->B == 3)
		return st(w, p, 0x1fe);
	end_move(w, p, 0x46, 100);
	return true;
}

static bool st_sadism(aos_world *w, aos_player *p) /* 0x201 T2 FUN_006c72b0 Sadism */
{
	const int last = 0x18; /* ver > 0x5a0 */
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	int t = p->timer;
	if (t > 0xb && t <= last && t % 3 == 0 && aos_use_gauge(p, 0x201)) {
		p->speed = 0;
		float x = aos_rand_real(w, 70.0f, 100.0f); /* DAT_008145ec, DAT_00853b90 (drawn, overridden) */
		float y = aos_rand_real(w, -60.0f, 60.0f);
		x = 85.0f;                                 /* DAT_00824f98 */
		switch (t) {
		case 0xc: y = 0.0f; break;
		case 0xf: y = 20.0f; break;    /* DAT_008140fc */
		case 0x12: y = 40.0f; break;   /* DAT_008140e0 */
		case 0x15: y = -20.0f; break;  /* DAT_00814108 */
		default: y = -40.0f; break;    /* DAT_00817dc0 */
		}
		float pt[3];
		offset_pt(p, x, y, p->faceAngle, pt);
		if (t == last) {
			aos_player *o = aos_opponent(w, p);
			float tg[3] = {aos_cos(p->faceAngle) * -100.0f + o->x, aos_sin(p->faceAngle) * -100.0f + o->y, p->z};
			if (dist2d(tg, &p->x) <= 200.0f) { /* DAT_008147cc */
				pt[0] = tg[0];
				pt[1] = tg[1];
			}
		}
		bool red = (t - 0xc) % 6 == 0;
		fxh(w, red ? 200 : 0xfa, 0, pt, p->faceAngle, p->objId, p->side);
		fx0(w, 100, 0, pt, p->faceAngle, p->objId, 0, p->side); /* graze zone owned by Hime (literal) */
		int h = aos_spawn_hitbox(w, ID, red ? 200 : 0xfa, pt, p->faceAngle, p->objId, 0, p->side);
		if (t == last && h >= 0)
			w->obj.hitbox[h].knockback = 20.0f; /* DAT_008140fc */
		aos_se_cd(w, 0xb, 3);
	}
	end_move(w, p, 0x23, 0x2d);
	return true;
}

static bool st_amber(aos_world *w, aos_player *p) /* 0x202 T2 FUN_006c61c0 Amber */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		aos_barrier(w, p, 10);
		aos_hyper_flourish(w, p); /* SE 6, dark(100,.75), speedline(aim, π/4), blur(20,.5,2), 0xfa */
	}
	if (p->timer == 10) {
		aos_spawn_laser(w, ID, 0x96, &p->x, p->faceAngle, p->objId, p->side, p->color);
		aos_se(w, 0x12);
	}
	if (p->timer > 10 && p->timer < 0x3c)
		walk_mul(p, 0.5f);
	end_move(w, p, 0x3c, 0x50);
	return true;
}

static void flourish00(aos_world *w, aos_player *p) /* FUN_007bec20(100, pos, 0, 0) variant */
{
	aos_hyper_flourish_ex(w, p, 6, 100, 0.75f, 100, 0.0f, 0.0f, 0x14, 0.5f, 2, 1);
}

static bool st_leash(aos_world *w, aos_player *p) /* 0x203 T2 FUN_006c5970 Leash */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		aos_barrier(w, p, 10);
		flourish00(w, p);
	}
	if (p->timer == 0x19) {
		p->faceAngle = aos_aim(w, p);
		static const float D[3] = {0.0f, 2.0943952f, -2.0943952f}; /* DAT_00817898 */
		for (int j = 0; j < 3; j++) {
			float base = p->faceAngle + D[j];
			float c[3] = {aos_cos(base) * 120.0f + p->x, aos_sin(base) * 120.0f + p->y, p->z};
			for (int k = 0; k < 5; k++) {
				float a = ((float)k / 5.0f) * AOS_2PI + p->faceAngle + D[j];
				fxh(w, 500, 0, c, a, p->objId, p->side);
				missile(w, p, 500, c, a);
			}
		}
	}
	if (p->timer > 9 && p->timer < 0x29 && p->timer % 10 == 0) {
		float r = aos_rand_real(w, -AOS_PI, AOS_PI);
		fxh(w, 0x230, 0, &p->x, r, p->objId, p->side);
	}
	if (p->timer > 0x18 && p->timer < 0x29 && aos_dist(w, p) < 320.0f) { /* DAT_008178c0 */
		aos_player *o = aos_opponent(w, p);
		if (can_affect(w, o)) {
			o->heat += 0.1f;
			aos_se_cd(w, 0x26, 4);
		}
	}
	if (p->timer == 0x3c) {
		prox_knock(w, p, 240.0f, 20.0f);
		fx0(w, 0xa8c, 0, &p->x, 0.0f, p->objId, 0, p->side);
	}
	end_move(w, p, 0x3c, 0x50);
	return true;
}

static bool st_burning_near(aos_world *w, aos_player *p) /* 0x204 T2 FUN_006c6ee0 Life-long Burning (close) */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		aos_barrier(w, p, 10);
		flourish00(w, p);
	}
	if (p->timer > 5 && p->timer < 0x2f && p->timer % 2 == 0) {
		aos_player *o = aos_opponent(w, p);
		float a = aos_rand_real(w, -AOS_PI, AOS_PI);
		float v[2] = {aos_rand_real(w, 100.0f, 240.0f), 0.0f}; /* DAT_00853b90, DAT_00815584 */
		aos_vec_rotate(v, a);
		float pt[3] = {v[0] + o->x, v[1] + o->y, p->z};
		missile(w, p, (p->timer / 2) % 2 == 0 ? 0xca : 0xfc, pt, a + AOS_PI);
		aos_se_cd(w, 0xe, 4);
	}
	if (p->timer % 5 == 0 && p->timer < 0x3c) {
		float pt[3] = {aos_cos(p->faceAngle) * -100.0f + p->x, aos_sin(p->faceAngle) * -100.0f + p->y, p->z};
		fxh(w, 300, 0, pt, p->faceAngle, p->objId, p->side);
	}
	end_move(w, p, 0x50, 0x3c); /* literal: dash-cancel frame is never reached */
	return true;
}

static bool st_burning_far(aos_world *w, aos_player *p) /* 0x205 T2 FUN_006c6b10 Life-long Burning (far) */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		aos_barrier(w, p, 10);
		aos_hyper_flourish(w, p);
	}
	if (p->timer > 4 && p->timer < 0x2e) { /* `t & 0x80000000` == 0: every frame */
		float x = -aos_rand_real(w, -50.0f, 100.0f); /* DAT_008149e4, DAT_00853b90 */
		float y = aos_rand_real(w, -200.0f, 200.0f); /* DAT_00814600, DAT_008147cc */
		float pt[3];
		offset_pt(p, x, y, p->faceAngle, pt);
		missile(w, p, p->timer % 2 == 0 ? 0xc9 : 0xfb, pt, p->faceAngle);
		aos_se_cd(w, 0xe, 4);
	}
	if (p->timer % 5 == 0 && p->timer < 0x3c) {
		float pt[3] = {aos_cos(p->faceAngle) * -150.0f + p->x, aos_sin(p->faceAngle) * -150.0f + p->y, p->z}; /* DAT_008149ec */
		fxh(w, 300, 0, pt, p->faceAngle, p->objId, p->side);
	}
	end_move(w, p, 0x3c, 0x50);
	return true;
}

static bool st_untouchable(aos_world *w, aos_player *p) /* 0x206 T2 FUN_006c77c0 Untouchable */
{
	if (p->timer == 0) {
		face_by_x(w, p);
		aos_barrier(w, p, 10);
		aos_hyper_flourish_ex(w, p, 6, 100, 0.75f, 100, p->faceAngle, 0.0f, 0x14, 0.5f, 2, 1);
		for (int i = -3; i < 3; i++) {
			int e = aos_spawn_enemy(w, ID, (i % 2 == 0) ? 100 : 0x96, p->objId, p->side, p->color);
			if (e < 0)
				continue;
			aos_enemy *en = &w->obj.enemy[e];
			en->scratch[0] = (i % 2 == 0) ? 0.1f : -0.1f; /* +0x74 spin (DAT_00853b10 / DAT_00814130) */
			en->angle2 = ((float)i / 3.0f) * AOS_PI;      /* +0x5c */
		}
	}
	if (p->timer > 10 && p->timer < 0x50)
		walk_mul(p, 0.5f);
	end_move(w, p, 0x5a, 0x6e);
	return true;
}

static bool st_godknows(aos_world *w, aos_player *p) /* 0x207 T2 FUN_006c7b80 God Knows (Accel) */
{
	int t = p->timer;
	if (t == 0) {
		face_by_x(w, p);
		p->speed = 0;
		aos_barrier(w, p, 0x14);
		aos_hyper_flourish_ex(w, p, 7, 0x96, 0.85f, 100, p->faceAngle, 0.0f, 0x96, 0.6f, 2, 1);
	}
	if (t > 5 && t < 0x3d && t % 2 == 0) {
		bool q = t % 4 == 0;
		fxh(w, q ? 700 : 0x2bd, 0, &p->x, -HALF_PI, p->objId, p->side);
		fxh(w, q ? 0x2ef : 0x2ee, 0, &p->x, -HALF_PI, p->objId, p->side);
	}
	float l[3] = {p->x + 210.0f, p->y - 30.0f, p->z}, r[3] = {p->x - 210.0f, p->y - 30.0f, p->z}; /* DAT_008178d8, DAT_008140e8 */
	if (t < 10 || t > 0x32 || t % 5 != 0) {
		if (t > 9 && t < 0x33) {
			aos_spawn_hitbox(w, ID, 0x137, l, 0.0f, p->objId, 0, p->side);
			aos_spawn_hitbox(w, ID, 0x137, r, 0.0f, p->objId, 0, p->side);
		}
	} else {
		int id = t % 10 == 0 ? 0x136 : 0x168;
		aos_spawn_hitbox(w, ID, id, l, 0.0f, p->objId, 0, p->side);
		aos_spawn_hitbox(w, ID, id, r, 0.0f, p->objId, 0, p->side);
	}
	if (t > 3 && t < 0x49 && t % 3 == 0) {
		float pt[3] = {p->x, p->y - 150.0f, p->z}; /* DAT_0081414c */
		int32_t col = tcol(w, p->side, t % 6 == 0 ? 0 : 1);
		float rr = aos_rand_real(w, -0.25f, 0.25f);
		int32_t k = aos_rand_int(w, 0, 100) % 8;
		int e = fx0(w, 0xa82 + k, col, pt, rr + HALF_PI, p->objId, 0, p->side);
		if (e >= 0)
			w->obj.effect[e].aux2 = 0.5f;
	}
	if (t > 8 && t < 0x4c && t % 2 == 0) {
		float a = aos_rand_real(w, -AOS_PI, AOS_PI);
		float d = aos_rand_real(w, 50.0f, 200.0f); /* DAT_008140ec, DAT_008147cc */
		float pt[3] = {aos_cos(a) * d + p->x, (aos_sin(a) * d * 0.25f + p->y) - 100.0f, p->z};
		aos_spawn_particle(w, 1000, tcol(w, p->side, 1), pt, HALF_PI, p->objId, 0, 1);
	}
	if (t > 9 && t < 0x47) {
		float a = aos_rand_real(w, -AOS_PI, AOS_PI);
		float d = aos_rand_real(w, 50.0f, 200.0f);
		float pt[3] = {aos_cos(a) * d + p->x, (aos_sin(a) * d * 0.25f + p->y) - 100.0f, p->z};
		aos_spawn_particle(w, 0x44c, tcol(w, p->side, 0), pt, HALF_PI, p->objId, 0, 1);
	}
	if (t == 0x41) {
		prox_knock(w, p, 360.0f, 20.0f); /* DAT_00817894 */
		fx0(w, 0xa8c, 0, &p->x, 0.0f, p->objId, 0, p->side);
	}
	end_move(w, p, 100, 0x78);
	return true;
}

/* ================================================================== */
/* 4. T1 draw handlers                                                 */

static int imin(int a, int b) { return a < b ? a : b; }
static int pingpong(int t, int n) { int f = (t / n) & 3; return f == 3 ? 1 : f; }

static aos_dcmd *aim_quad(aos_world *w, aos_player *p, const char *pat, int frame, int32_t rot[3])
{
	aos_dcmd *c = aos_draw(aos_texf(pat, frame), aos_pidx(w, p), p->x, p->y, 0);
	c->halfW = c->halfH = 48.0f; /* S = 96 */
	c->flipX = 1;
	c->rotZ = aos_rad2bam(p->faceAngle);
	c->blend = AOS_BLEND_ALPHA;
	bool tilt = aos_fabs(p->faceAngle) >= HALF_PI;
	if (tilt)
		c->sy = -1.0f; /* RotX(180°) */
	if (rot) {
		rot[0] = tilt ? aos_deg2bam(0xb4) : 0;
		rot[1] = aos_deg2bam(0xb4);
		rot[2] = aos_rad2bam(p->faceAngle);
	}
	return c;
}

static void dr_neutral(aos_world *w, aos_player *p) /* 0x1f0 T1 FUN_006ca1e0 */
{
	const char *pat = "hm_std%02d";
	if (p->up > 1) pat = "hm_fwd%02d";
	if (p->down > 1) pat = "hm_bck%02d";
	if (!p->facingRight && p->left > 1) pat = "hm_fwd%02d";
	if (p->facingRight && p->left > 1) pat = "hm_bck%02d";
	if (!p->facingRight && p->right > 1) pat = "hm_bck%02d";
	if (p->facingRight && p->right > 1) pat = "hm_fwd%02d";
	aos_dcmd *c = aos_draw(aos_texf(pat, pingpong(p->timer, 5)), aos_pidx(w, p), p->x, p->y, 0);
	c->halfW = c->halfH = 48.0f;
	c->flipX = p->facingRight != 0;
	c->blend = AOS_BLEND_ALPHA;
	int32_t rot[3] = {0, p->facingRight ? aos_deg2bam(0xb4) : 0, 0};
	aos_afterimage(w, c, 1, p, rot);
}

static void dr_dash(aos_world *w, aos_player *p) /* 0x1f1 T1 FUN_006ca480 */
{
	bool against = (p->facingRight == 1 && aos_fabs(p->faceAngle) > HALF_PI) ||
	               (p->facingRight == 0 && aos_fabs(p->faceAngle) < HALF_PI);
	int f = against ? pingpong(p->timer, 3) + 6 : (p->timer / 2) % 6;
	int32_t rot[3];
	aos_dcmd *c = aim_quad(w, p, "hm_dsh%02d", f, rot);
	aos_afterimage(w, c, 1, p, rot);
}

static void dr_ko(aos_world *w, aos_player *p) /* 0x1f2 T1 FUN_006cae30 */
{
	int f = pingpong(p->timer, 3) + (w->frame % 2 == 0 ? 3 : 0);
	if (p->timer < 100)
		aim_quad(w, p, "hm_los%02d", f, NULL);
}

static void dr_hit(aos_world *w, aos_player *p) /* 0x1f3 T1 FUN_006ca8a0 */
{
	aim_quad(w, p, "hm_los%02d", pingpong(p->timer, 3), NULL);
}

static void dr_sld(aos_world *w, aos_player *p) /* 0x1f4/0x1fa/0x1fb/0x200/0x204 T1 */
{
	aim_quad(w, p, "hm_sld%02d", imin(p->timer / 4, 4), NULL);
}

static void dr_plenty(aos_world *w, aos_player *p) /* 0x1fc T1 FUN_006cb6b0 (+afterimage) */
{
	int32_t rot[3];
	aos_dcmd *c = aim_quad(w, p, "hm_sld%02d", imin(p->timer / 4, 4), rot);
	aos_afterimage(w, c, 1, p, rot);
}

static void dr_atka3(aos_world *w, aos_player *p) /* 0x1fd/0x1fe/0x1ff T1 FUN_006cc7f0/cc270/cba30 */
{
	aim_quad(w, p, "hm_atka%02d", imin(p->timer / 3, 6), NULL);
}

static void dr_atka4(aos_world *w, aos_player *p) /* 0x202/0x203/0x205 T1 FUN_006cc530/cbfb0/ccab0 */
{
	aim_quad(w, p, "hm_atka%02d", imin(p->timer / 4, 6), NULL);
}

static void dr_atkb(aos_world *w, aos_player *p) /* 0x201 T1 FUN_006cd030 */
{
	aim_quad(w, p, "hm_atkb%02d", imin(p->timer / 4, 5), NULL);
}

static void dr_atkc(aos_world *w, aos_player *p) /* 0x206/0x207 T1 FUN_006cd2f0/cd5d0 */
{
	int f = p->timer / 4;
	if (f > 1)
		f = ((f - 2) & 3) + 2;
	if (f == 5)
		f = 3;
	aim_quad(w, p, "hm_atkc%02d", f, NULL);
}

/* ================================================================== */
/* 5. objects                                                          */

/* 2D sprite blit (vtable +0x68, dst rect around a point; packed (bam(−angle), ARGB, blend)). */
static aos_dcmd *blit(int tex, int bank, const float pos[3], float hw, float hh, float angle,
                      int alpha, uint32_t rgb, int blend, int x0, int y0, int x1, int y1)
{
	aos_dcmd *c = aos_draw(tex, bank, pos[0], pos[1], 0);
	c->halfW = hw < 0 ? -hw : hw;
	c->halfH = hh;
	c->flipX = hw < 0;
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

/* ---- missiles: spawn FUN_00662fb0, update FUN_006647a0, draw FUN_00663610,
 *      on-hit FUN_006643d0, on-killed FUN_00664780 ---- */

static int missile_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team, int32_t p18)
{
	switch (id) {
	case 100: case 0x69: case 0x6e: case 0x6f: case 0x70: case 0x96: case 0x9b: case 0xa0: case 0xa1: case 0xa2:
	case 200: case 0xc9: case 0xca: case 0xfa: case 0xfb: case 0xfc:
	case 500: case 0x1f5: case 0x1fe: case 0x1ff: case 600: case 0x259: case 0x262: case 0x263:
		break;
	default:
		return -1;
	}
	int i = aos_missile_new(w, ID, id, pos, angle, owner, age0, team, p18);
	if (i < 0)
		return -1;
	aos_missile *m = &w->obj.missile[i];
	switch (id) {
	case 100: case 0x69: case 0x96: case 0x9b:
		m->speed = 0;
		m->growScale = 0;
		m->hitType = (id == 100 || id == 0x69) ? 1 : 2;
		m->active = 0;
		m->shapeBody.radius = 16.0f; /* DAT_008140d8 */
		m->hp = 0x3c;
		m->damage = 0x78;
		m->knockback = 10.0f;
		break;
	case 0x6e: case 0x6f: case 0x70: case 0xa0: case 0xa1: case 0xa2:
		m->speed = 1.0f;
		m->f74 = id < 0xa0 ? 0.075f : -0.075f; /* DAT_00813efc / DAT_00824a88 */
		m->growScale = 0;
		m->hitType = id < 0xa0 ? 1 : 2;
		m->active = 0;
		m->shapeBody.radius = 16.0f;
		m->hp = 0x3c;
		m->damage = 0x3c;
		m->knockback = 10.0f;
		break;
	case 200: case 0xc9: case 0xca: case 0xfa: case 0xfb: case 0xfc: {
		bool red = id < 0xfa;
		int low = red ? id - 200 : id - 0xfa;
		m->speed = -8.0f; /* DAT_0081410c */
		m->growScale = 0;
		m->hitType = red ? 1 : 2;
		m->active = 0;
		m->shapeBody.halfW = 30.0f; /* DAT_008140e8 */
		m->shapeBody.halfH = 16.0f;
		m->hp = 0x3c;
		m->damage = low == 2 ? 0x3c : 0x4b; /* ver > 0x5a0 */
		m->knockback = low == 0 ? 10.0f : 5.0f;
		m->id = red ? 200 : 0xfa;
		break;
	}
	case 500: case 0x1fe: case 600: case 0x262:
		m->speed = 32.0f; /* DAT_008140dc */
		m->active = 0;
		m->hp = 0x3c;
		break;
	default: /* 0x1f5 0x1ff 0x259 0x263 */
		m->speed = 0;
		m->active = 0;
		m->hp = 0x3c;
		break;
	}
	return i;
}

static void m_move(aos_missile *m)
{
	m->pos[0] = aos_cos(m->moveAngle) * m->speed + m->pos[0];
	m->pos[1] = aos_sin(m->moveAngle) * m->speed + m->pos[1];
}

static void dash_spark(aos_world *w, aos_missile *m, float lo, float hi, int32_t age0)
{
	float r = aos_rand_real(w, lo, hi);
	int32_t k = aos_rand_int(w, 0, 100) % 8;
	fx0(w, 0xa28 + k, 0xc, m->pos, r + m->angle, m->ownerId, age0, m->team);
}

/* Hitbox along the chain: mid-point / back-offset variants (FUN_006647a0) */
static int chain_hitbox(aos_world *w, aos_missile *m, int32_t id, const float pos[3], float halfW)
{
	float a = m->moveAngle + AOS_PI;
	aos_angle_normalize(&a);
	int h = aos_spawn_hitbox(w, ID, id, pos, a, m->ownerId, 0, m->team);
	if (h >= 0) {
		w->obj.hitbox[h].shape.halfW = halfW;
		w->obj.hitbox[h].shape.halfH = 15.0f; /* DAT_00853b88 */
	}
	return h;
}

static void chain_close(aos_world *w, aos_player *own, int h)
{
	if (h >= 0 && aos_dist(w, own) <= 120.0f) /* ver > 0x5a0: DAT_00814174 */
		w->obj.hitbox[h].knockback = 0;
}

static bool chain_end(aos_world *w, aos_missile *m)
{
	aos_spawn_particle(w, 0x44c, tcol(w, m->team, 2), m->pos, m->angle, m->ownerId, 0, 2);
	if (aos_otab.missileOnHit[m->type])
		aos_otab.missileOnHit[m->type](w, m);
	return false;
}

static bool missile_update(aos_world *w, aos_missile *m)
{
	aos_player *own = team_player(w, m->team);
	switch (m->id) {
	case 100: case 0x69: case 0x96: case 0x9b: /* lances: wait 20 frames, then fire at the opponent */
		if (1.0f <= m->growScale)
			m->growScale = 1.0f;
		else
			m->growScale += 0.1f;
		if (m->age == 0x14) {
			aos_player *o = aos_opponent(w, own);
			m->moveAngle = aos_atan2(o->y - m->pos[1], o->x - m->pos[0]);
			if (m->id % 10 == 5)
				m->moveAngle = aos_rand_real(w, -0.2f, 0.2f) + m->moveAngle; /* DAT_00817c04 */
			m->angle = m->moveAngle;
			m->speed = 17.0f; /* _DAT_00813f5c */
			m->active = 1;
		}
		if (m->age == 10)
			fx0(w, 100, 0, m->pos, m->angle, m->serial, 0, m->team);
		if (m->age > 0x13 && m->age < 0x19)
			dash_spark(w, m, -0.2f, 0.2f, 0);
		m_move(m);
		if (m->age > 0x14 && m->age % 2 == 0)
			fxh(w, m->id - m->id % 10, own->color, m->pos, m->angle, m->ownerId, m->team);
		if (m->age < 0x1f)
			return true;
		if (!aos_in_arena(m->pos, 10.0f)) {
			aos_otab.missileOnKilled[m->type](w, m);
			return false;
		}
		return true;
	case 0x6e: case 0x6f: case 0x70: case 0xa0: case 0xa1: case 0xa2: /* Needle Fruit ring */
		if (1.0f <= m->growScale)
			m->growScale = 1.0f;
		else
			m->growScale += 0.1f;
		if (m->speed < 18.0f)
			m->speed += 0.2f;
		if (m->speed < 14.0f) {
			m->moveAngle += m->f74;
			if (AOS_PI < m->moveAngle)
				m->moveAngle -= AOS_2PI;
			m->angle = m->moveAngle;
		}
		if (m->age == 10)
			m->active = 1;
		if (m->age == 0x3c)
			m->damage = 0x78;
		if (m->age % 2 == 0)
			fxh(w, m->id - 10, 0, m->pos, m->angle, m->ownerId, m->team);
		if (m->age == 0x46) {
			for (int k = 0; k < 4; k++) {
				float r = aos_rand_real(w, -0.1f, 0.1f);
				int32_t j = aos_rand_int(w, 0, 100) % 8;
				fx0(w, 0xa28 + j, 0xc, m->pos, r + m->angle, m->ownerId, k * -3, m->team);
			}
			aos_se_cd(w, 0xe, 5);
		}
		m_move(m);
		if (m->age < 0x65)
			return true;
		if (!aos_in_arena(m->pos, 10.0f)) {
			aos_otab.missileOnKilled[m->type](w, m);
			return false;
		}
		return true;
	case 200: case 0xfa: /* swords */
		if (m->age < 0x28) {
			m->growScale += 0.2f;
			if (1.0f < m->growScale) {
				m->growScale = 1.0f;
				m->active = 1;
			}
		}
		if (m->speed < 20.0f)
			m->speed += 1.0f;
		if (m->age == 5)
			fx0(w, 100, 0, m->pos, m->angle, m->serial, 0, m->team);
		m_move(m);
		if (m->age == 0x28)
			m->active = 0;
		if (m->age > 0x27)
			m->growScale -= 0.1f;
		return m->age != 0x32;
	case 500: case 600: { /* extending chains */
		if (m->id == 500)
			fxh(w, m->age == 0 ? 0x259 : 600, 0, m->pos, m->angle, m->serial, m->team);
		else
			fxh(w, m->age == 0 ? 0x263 : 0x262, 0, m->pos, m->angle, m->serial, m->team);
		m_move(m);
		fxh(w, 0x23a, (m->age / 2) % 3, m->pos, m->angle, m->serial, m->team);
		if (m->age > 10) {
			m->id = m->id == 600 ? 0x259 : 0x1f5;
			m->age = 0;
			return true;
		}
		float mid[3] = {(m->pos[0] + own->x) / 2.0f, (m->pos[1] + own->y) / 2.0f, 0};
		int h = chain_hitbox(w, m, 100, mid, (float)m->age * (m->speed / 2.0f) + 40.0f); /* DAT_008140e0 */
		if (h >= 0) {
			if (own->state == 0x203)
				w->obj.hitbox[h].damage <<= 1;
			if (m->age % 5 != 0)
				w->obj.hitbox[h].damage = 0;
		}
		chain_close(w, own, h);
		aos_se_cd(w, 0x28, 3);
		return true;
	}
	case 0x1f5: case 0x259: { /* holding chains */
		if (m->age % 3 == 0 && (m->id == 0x1f5 || m->age < 0x15)) {
			float mid[3] = {(m->pos[0] + own->x) / 2.0f, (m->pos[1] + own->y) / 2.0f, 0};
			int h = chain_hitbox(w, m, 100, mid, 200.0f); /* DAT_008147cc */
			if (h >= 0 && own->state == 0x203)
				w->obj.hitbox[h].damage <<= 1;
			chain_close(w, own, h);
		}
		if ((m->id == 0x1f5 && m->age == 0x14) || (m->id == 0x259 && m->age == 0x78))
			return chain_end(w, m);
		return true;
	}
	case 0x1fe: case 0x262: {
		if (m->id == 0x1fe)
			fxh(w, m->age == 0 ? 0x259 : 600, 0, m->pos, m->angle, m->serial, m->team);
		else
			fxh(w, m->age == 0 ? 0x263 : 0x262, 0, m->pos, m->angle, m->serial, m->team);
		m_move(m);
		fxh(w, 0x23a, (m->age / 2) % 3, m->pos, m->angle, m->serial, m->team);
		if (m->age == 6 || m->age == 9) {
			float bp[3] = {m->pos[0] - aos_cos(m->moveAngle) * 100.0f, m->pos[1] - aos_sin(m->moveAngle) * 100.0f, 0};
			int h = chain_hitbox(w, m, 0x6e, bp, 200.0f);
			if (h >= 0)
				w->obj.hitbox[h].knockback = 0;
		}
		if (m->age > 10) {
			m->id = m->id == 0x1fe ? 0x1ff : 0x263;
			m->age = 0;
			return true;
		}
		aos_se_cd(w, 0x28, 3);
		return true;
	}
	case 0x1ff: case 0x263:
		if (m->age < 10 && m->age % 3 == 0) {
			float bp[3] = {m->pos[0] - aos_cos(m->moveAngle) * 100.0f, m->pos[1] - aos_sin(m->moveAngle) * 100.0f, 0};
			int h = chain_hitbox(w, m, 100, bp, 200.0f);
			if (h >= 0) {
				w->obj.hitbox[h].damage = 0;
				w->obj.hitbox[h].knockback = 0;
			}
		}
		if ((m->id == 0x1ff && m->age == 0x14) || (m->id == 0x263 && m->age == 0x78))
			return chain_end(w, m);
		return true;
	default:
		return true;
	}
}

static bool missile_on_hit(aos_world *w, aos_missile *m) /* FUN_006643d0 */
{
	int32_t c;
	switch (m->id) {
	case 100: case 0x69: case 0x6e: case 0x6f: case 0x70:
	case 0x96: case 0x9b: case 0xa0: case 0xa1: case 0xa2:
		c = tcol(w, m->team, m->id < 0x96 ? 1 : 0);
		fx0(w, 0x9c4, c, m->pos, m->angle, m->ownerId, 0, m->team);
		aos_spawn_spark(w, 1000, c, 8, 8, m->angle, m->pos, m->ownerId);
		aos_se(w, 0x15);
		break;
	case 200: case 0xfa: {
		c = tcol(w, m->team, m->id == 200 ? 1 : 0);
		float r = aos_rand_real(w, -0.5f, 0.5f); /* DAT_00853bb0, DAT_00853b1c */
		fx0(w, 0x9c4, c, m->pos, r + m->angle, m->ownerId, 0, m->team);
		aos_spawn_spark(w, 1000, c, 8, 8, m->angle, m->pos, m->ownerId);
		aos_se(w, 0x15);
		break;
	}
	default:
		break;
	}
	return false;
}

static bool missile_on_killed(aos_world *w, aos_missile *m) /* FUN_00664780 */
{
	(void)w, (void)m;
	return false;
}

static void missile_draw(aos_world *w, aos_missile *m) /* FUN_00663610 */
{
	(void)w;
	int bank = team_bank(m->team);
	const char *pat;
	int f;
	float S;
	bool sized = true;
	switch (m->id) {
	case 500: case 0x1f5: case 0x1fe: case 0x1ff: case 600: case 0x259: case 0x262: case 0x263:
		pat = "hm_chain%02d", f = (m->age / 2) % 3 + 2, S = 72.0f, sized = false; /* 0x20e */
		break;
	case 100: case 0x69: case 0x6e: case 0x6f: case 0x70:
		pat = "hm_lncer%d", f = (m->age / 2) % 3, S = 128.0f; /* 0x208 */
		break;
	case 0x96: case 0x9b: case 0xa0: case 0xa1: case 0xa2:
		pat = "hm_lnceb%d", f = (m->age / 2) % 3, S = 128.0f; /* 0x209 */
		break;
	case 200:
		pat = "hm_swordr%d", f = m->growScale < 1.0f ? 0 : (m->age / 2) % 6, S = 160.0f; /* 0x20a */
		break;
	case 0xfa:
		pat = "hm_swordb%d", f = m->growScale < 1.0f ? 0 : (m->age / 2) % 6, S = 160.0f; /* 0x20b */
		break;
	default:
		return;
	}
	aos_dcmd *c = aos_draw(aos_texf(pat, f), bank, m->pos[0], m->pos[1], 0);
	c->halfW = c->halfH = S * 0.5f;
	c->rotZ = aos_rad2bam(m->angle);
	c->blend = AOS_BLEND_ALPHA;
	if (sized) {
		float k = 1.0f <= m->growScale ? 1.0f : 0.0f; /* +0xdc(a=1, r=g=b=k) */
		c->rgba = aos_rgba(k, k, k, 1.0f);
		c->sx = 3.0f - m->growScale * 2.0f;
		c->sy = m->growScale;
	}
}

/* ---- hitboxes: spawn FUN_005e1690, on-hit FUN_005e1b40 ---- */

static int hitbox_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t zero, int32_t team)
{
	int32_t dmg;
	aos_shape sh = {0, 0, 0};
	float kb;
	switch (id) {
	case 0x96: dmg = 0x28; kb = 0; break;
	case 100: dmg = 0x19; kb = 10.0f; break;
	case 0x6e: dmg = 0x14; kb = 10.0f; break;
	case 200: case 0xfa: dmg = 0x50; sh.halfW = 150.0f; sh.halfH = 36.0f; kb = 5.0f; break; /* DAT_0081414c, DAT_008140f4 */
	case 300: case 0x15e: dmg = 0x14; sh.halfW = 140.0f; sh.halfH = 36.0f; kb = 2.5f; break; /* DAT_00817004, DAT_008153e0 */
	case 0x136: case 0x168: dmg = 0xd2; sh.radius = 200.0f; kb = 0; break; /* ver > 0x5a0 */
	case 0x137: dmg = 0; sh.radius = 200.0f; kb = 0; break;
	case 400: case 0x191: dmg = 0x32; sh.halfW = 580.0f; sh.halfH = 40.0f; kb = 10.0f; break; /* DAT_0081fa6c */
	case 0x1c2: case 0x1c3: dmg = 0x28; sh.halfW = 580.0f; sh.halfH = 75.0f; kb = 10.0f; break; /* DAT_00815254 */
	default: return -1;
	}
	int i = aos_hitbox_new(w, ID, id, pos, angle, owner, zero, team);
	if (i < 0)
		return -1;
	aos_hitbox *h = &w->obj.hitbox[i];
	h->hitType = 3;
	h->damage = dmg;
	h->shape = sh;
	h->knockback = kb;
	return i;
}

static void fan3(aos_world *w, aos_hitbox *h, aos_player *t, int32_t c, int sparkCount)
{
	float r0 = aos_rand_real(w, -AOS_PI, AOS_PI);
	for (int k = 0; k < 3; k++) {
		float a = ((float)k / 3.0f) * AOS_2PI;
		float r = aos_rand_real(w, -0.25f, 0.25f); /* DAT_0081411c, DAT_00813f24 */
		fx0(w, 0x456, c, &t->x, a + r + r0, h->ownerId, 0, h->team);
	}
	aos_spawn_spark(w, 1000, c, 10, sparkCount, h->angle, &t->x, h->ownerId);
}

static void godknows_fx(aos_world *w, aos_hitbox *h, aos_player *t, int k)
{
	float r = aos_rand_real(w, -AOS_PI, AOS_PI);
	int32_t c = tcol(w, h->team, k);
	float pt[3] = {aos_rand_real(w, -50.0f, 50.0f) + t->x, 0, 0};
	pt[1] = aos_rand_real(w, -50.0f, 50.0f) + t->y;
	fx0(w, 0x9c4, c, pt, r, h->ownerId, 0, h->team);
	fx0(w, 0x514, c, pt, 0.0f, h->ownerId, 0, h->team);
	aos_se_cd(w, 0x16, 3);
}

static void hitbox_on_hit(aos_world *w, aos_hitbox *h, aos_player *t) /* FUN_005e1b40 */
{
	int32_t c;
	float r;
	switch (h->id) {
	case 200: case 0xfa:
		c = tcol(w, h->team, h->id == 200 ? 1 : 0);
		r = aos_rand_real(w, -AOS_PI, AOS_PI);
		fx0(w, 0x9c4, c, &t->x, r, h->ownerId, 0, h->team);
		fx0(w, 0x514, c, &t->x, 0.0f, h->ownerId, 0, h->team);
		aos_se_cd(w, 0x16, 3);
		break;
	case 100:
		if (h->damage == 0)
			break;
		if (t->state == AOS_ST_HITSTUN && t->timer < 2) {
			h->dealsDamage = 0;
		} else {
			c = tcol(w, h->team, 2);
			r = aos_rand_real(w, -AOS_PI, AOS_PI);
			fx0(w, 0x9c4, c, &t->x, r, h->ownerId, 0, h->team);
			fx0(w, 0x515, c, &t->x, 0.0f, h->ownerId, 0, h->team);
			aos_player *o = team_player(w, h->team);
			if (o->state == 0x1ff)
				o->hitFlag = 1;
			aos_se_cd(w, 0x15, 3);
		}
		break;
	case 0x6e:
		if (h->damage == 0)
			break;
		c = tcol(w, h->team, 2);
		r = aos_rand_real(w, -AOS_PI, AOS_PI);
		fx0(w, 0x9c4, c, &t->x, r, h->ownerId, 0, h->team);
		fx0(w, 0x515, c, &t->x, 0.0f, h->ownerId, 0, h->team);
		if (team_player(w, h->team)->state == 0x200)
			team_player(w, h->team)->hitFlag = 1;
		aos_se_cd(w, 0x15, 3);
		break;
	case 300: case 0x15e:
		if (h->damage == 0)
			break;
		fan3(w, h, t, tcol(w, h->team, h->id == 300 ? 1 : 0), 2);
		aos_se_cd(w, 0x16, 3);
		break;
	case 0x136:
		godknows_fx(w, h, t, 1);
		godknows_fx(w, h, t, 0); /* literal fall-through into 0x168 */
		break;
	case 0x168:
		godknows_fx(w, h, t, 0);
		break;
	case 400: case 0x191: case 0x1c2: case 0x1c3:
		if (h->damage == 0)
			break;
		fan3(w, h, t, tcol(w, h->team, (h->id == 400 || h->id == 0x1c2) ? 1 : 0), 3);
		aos_se_cd(w, 0x13, 3);
		break;
	default:
		break;
	}
}

/* ---- lasers: spawn FUN_0061d050, update FUN_0061e030, draw FUN_0061d8d0, on-hit FUN_0061e010 (no-op) ---- */

static int laser_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t team, int32_t p18)
{
	if (id != 100 && id != 0x96)
		return -1;
	int i = aos_laser_new(w, ID, id, pos, angle, owner, team, p18);
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
	aos_player *o = team_player(w, l->team);
	l->nseg = 0;
	l->origin[0] = o->x;
	l->origin[1] = o->y;
	l->origin[2] = o->z;
	int last = l->id == 100 ? 0x27 : 0x31;
	if (l->id == 100 && l->age > 9 && l->age < 0x15) {
		o->gaugeB -= 0.05f; /* DAT_00813ef4 */
		if (o->gaugeB <= 0.0f)
			l->age = 0x15;
	}
	if (l->age > last)
		return false;
	int hi = l->id == 100 ? 0x15 : 0x29;
	if (l->age > 9 && l->age < hi) {
		float pt[3] = {aos_cos(l->angle) * 600.0f + l->origin[0], aos_sin(l->angle) * 600.0f + l->origin[1], 0}; /* DAT_008149b8 */
		int id = ((l->age / 2) % 2) + (l->id == 100 ? 400 : 0x1c2);
		int h = aos_spawn_hitbox(w, ID, id, pt, l->angle, l->ownerId, 0, l->team);
		if (l->age % 2 == 1 && h >= 0)
			w->obj.hitbox[h].damage = 0;
	}
	return true;
}

static void laser_on_hit(aos_world *w, aos_laser *l, int32_t seg, aos_player *t)
{
	(void)w, (void)l, (void)seg, (void)t;
}

static void laser_draw(aos_world *w, aos_laser *l) /* FUN_0061d8d0 */
{
	float H = l->id == 100 ? 100.0f : 160.0f, hold = l->id == 100 ? 0x1f : 0x29, tail = l->id == 100 ? 0x28 : 0x32;
	float hh;
	if (l->age < 0xb)
		hh = ((float)l->age * H) / 10.0f;
	else if (l->age < hold)
		hh = l->age % 2 == 0 ? H * 1.25f : H;
	else
		hh = ((tail - (float)l->age) * H) / 10.0f;
	float c[3] = {aos_cos(l->angle) * 960.0f + l->origin[0], aos_sin(l->angle) * 960.0f + l->origin[1], 0};
	int row = (l->age / 2) % 2;
	int tex = aos_tex("hm_laser");
	int bank = team_bank(l->team);
	(void)w;
	blit(tex, bank, c, 1000.0f, hh, l->angle, 0xc0, 0xffffff, AOS_BLEND_ALPHA, 0, row * 100, 0x1ff, row * 100 + 99);
	blit(tex, bank, c, 1000.0f * 0.95f, hh * 0.5f, l->angle, 0xff, 0xffffff, AOS_BLEND_ADD_ALPHA, 0, row * 100 + 200, 0x1ff, row * 100 + 299);
}

/* ---- enemies (Untouchable wings): spawn FUN_0051e8c0, update FUN_0051eb10, draw FUN_0051efd0 ---- */

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
	e->f10 = id;
	e->f7c = id;
	e->f84 = 0;
	e->hp = e->maxHp = 300;
	e->active = 0;
	e->shape = (aos_shape){0, 0, 0};
	e->scale[0] = 3.0f; /* DAT_00813a50 */
	e->scale[1] = 0;
	e->scale[2] = 0;
	e->mirrorChildren = 0;
	e->motionId = 0;
	e->rehitTimer = 0;
	return i;
}

static bool enemy_update(aos_world *w, aos_enemy *e)
{
	if (e->f7c != 100 && e->f7c != 0x96)
		return true;
	if (e->f84 == 0) {
		e->scale[0] -= 0.2f;
		e->scale[1] += 0.1f;
		if (1.0f <= e->scale[1]) {
			e->scale[0] = e->scale[1] = 1.0f;
			e->f84 = 100;
			e->timer2 = 0;
		}
	} else if (e->f84 == 100) {
		e->angle2 += e->scratch[0];
		if (AOS_PI < e->angle2) e->angle2 -= AOS_2PI;
		if (e->angle2 <= -AOS_PI) e->angle2 += AOS_2PI;
		if (e->timer2 > 0x45) {
			e->f84 = 900;
			e->timer2 = 0;
		}
	} else {
		e->scratch[0] *= 0.95f; /* _DAT_00814124 */
		e->angle2 += e->scratch[0];
		if (AOS_PI < e->angle2) e->angle2 -= AOS_2PI;
		if (e->angle2 <= -AOS_PI) e->angle2 += AOS_2PI;
		e->scale[1] -= 0.05f;
		if (e->scale[1] <= 0.0f)
			return false;
	}
	e->angle = e->angle2;
	e->f70 = e->scale[0] * 160.0f; /* DAT_00814168 */
	aos_player *o = team_player(w, e->team);
	e->pos[0] = aos_cos(e->angle2) * e->f70 + o->x;
	e->pos[1] = aos_sin(e->angle2) * e->f70 + o->y;
	int fe = fxh(w, e->f10 == 100 ? 0xca : 0xfc, 0, e->pos, e->angle, e->ownerId, e->team);
	if (fe >= 0) {
		w->obj.effect[fe].scale = e->scale[0];
		w->obj.effect[fe].aux = e->scale[1];
	}
	if (e->f84 == 100) {
		int h = aos_spawn_hitbox(w, ID, e->f10 == 100 ? 300 : 0x15e, e->pos, e->angle, e->ownerId, 0, e->team);
		if (h >= 0) {
			if (e->timer2 % 2 == 0)
				w->obj.hitbox[h].damage = 0;
			if (e->timer2 > 0x3f)
				w->obj.hitbox[h].knockback = 15.0f; /* DAT_00853b88 */
			if (players_dist(w) < 120.0f)
				w->obj.hitbox[h].knockback = 10.0f;
		}
	}
	return true;
}

static void enemy_draw(aos_world *w, aos_enemy *e) /* FUN_0051efd0: sprite 0x20c / 0x20d (S 320), frame 0 */
{
	(void)w;
	if (e->f10 != 100 && e->f10 != 0x96)
		return;
	aos_dcmd *c = aos_draw(aos_tex(e->f10 == 100 ? "hm_wingr" : "hm_wingb"), team_bank(e->team), e->pos[0], e->pos[1], 0);
	c->halfW = c->halfH = 160.0f; /* DAT_008178c0 */
	c->sx = e->scale[0];
	c->sy = e->scale[1];
	c->rotZ = aos_rad2bam(e->angle);
	c->rgba = aos_rgba(1, 1, 1, 1);
	c->blend = AOS_BLEND_ALPHA;
}

static bool enemy_killed(aos_world *w, aos_enemy *e) /* FUN_0051efb0 */
{
	(void)w, (void)e;
	return false;
}

/* ---- Hime effects: spawn FUN_004f4ea0, update FUN_004f70c0, draw FUN_004f55f0 ---- */

static int effect_spawn(aos_world *w, int32_t id, int32_t variant, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team)
{
	int layer = 1, nid = id;
	float scale = 1.0f, aux2 = 0;
	switch (id) {
	case 100: case 0x65: case 0x66: case 0x96: case 0x97: case 0x98:
	case 0xc9: case 0xfb: case 0xca: case 0xfc:
	case 600: case 0x259: case 0x262: case 0x263: case 0x2be: case 0x2f0:
		layer = 0;
		break;
	case 200: case 0xfa:
		scale = 0;
		break;
	case 300:
		layer = 0;
		scale = 1.5f; /* DAT_00814110 */
		break;
	case 500: case 0x1fe: case 0x226:
		break;
	case 0x230:
		scale = 0.5f;
		break;
	case 0x23a:
		variant = 0; /* literal: the caller's variant is overwritten */
		break;
	case 700: case 0x2bd: case 0x2ee: case 0x2ef:
		aux2 = id % 10 == 1 ? 0.1f : -0.1f; /* DAT_00853b10 / DAT_00814130 */
		nid = id - id % 10;
		break;
	default:
		return -1;
	}
	int i = aos_effect_new(w, ID, nid, variant, pos, angle, owner, age0, team);
	if (i < 0)
		return -1;
	aos_effect *e = &w->obj.effect[i];
	e->drawLayer = layer;
	e->alpha = 1.0f;
	e->scale = scale;
	e->aux = 0;
	e->aux2 = aux2;
	return i;
}

static bool effect_update(aos_world *w, aos_effect *e)
{
	switch (e->id) {
	case 500:
	case 0x1fe:
		if (e->age > (e->id == 500 ? 0x1e : 0x82))
			return false;
		if (e->age % 3 == 0)
			fxh(w, 0x226, 0, e->pos, e->rotation, e->ownerId, e->team);
		return true;
	case 100: case 0x65: case 0x66: case 0x96: case 0x97: case 0x98:
		e->alpha -= 0.15f; /* DAT_00813f20 */
		e->scale += 0.25f;
		return e->alpha > 0.0f;
	case 200: case 0xfa: {
		if (e->age > 4)
			e->alpha -= 0.1f;
		if (e->scale < 2.5f)
			e->scale += 0.5f;
		e->pos[0] = aos_cos(e->moveDir) * 12.0f + e->pos[0];
		e->pos[1] = aos_sin(e->moveDir) * 12.0f + e->pos[1];
		if (e->alpha <= 0.0f)
			return false;
		float sc = e->scale, al = e->alpha;
		int n = fxh(w, e->id + 1, 0, e->pos, e->rotation, e->ownerId, e->team);
		if (n >= 0) {
			w->obj.effect[n].scale = sc;
			w->obj.effect[n].aux = al;
		}
		return true;
	}
	case 0xc9: case 0xfb:
		e->alpha -= 0.15f;
		return e->alpha > 0.0f;
	case 0xca: case 0xfc:
		e->alpha -= 0.2f;
		return e->alpha > 0.0f;
	case 300: case 0x226:
		e->alpha -= 0.05f;
		e->scale += 0.25f;
		return e->alpha > 0.0f;
	case 0x230:
		e->alpha -= 0.02f;
		e->scale += 0.02f;
		return e->alpha > 0.0f;
	case 0x23a:
		e->alpha -= 0.1f;
		return e->alpha > 0.0f;
	case 600: case 0x259: case 0x262: case 0x263:
		if (e->age > ((e->id == 600 || e->id == 0x259) ? 0x14 : 0x78)) {
			aos_spawn_particle(w, 0x44c, tcol(w, e->team, 2), e->pos, e->rotation, e->ownerId, 0, 2);
			return false;
		}
		return true;
	case 700: case 750: {
		aos_player *o = team_player(w, e->team);
		e->moveDir += e->aux2;
		e->pos[0] = aos_cos(e->moveDir) * 140.0f * ((float)e->age / 8.0f) + o->x; /* DAT_00817004, DAT_008140d4 */
		e->pos[1] = (aos_sin(e->moveDir) * 140.0f * ((float)e->age / 8.0f) + o->y) - 30.0f;
		e->scale = ((float)e->age * 140.0f) / 8.0f;
		e->aux = ((float)e->age * 100.0f) / 15.0f;
		e->alpha = e->age < 0x10 ? 1.0f : (float)(0x19 - e->age) / 10.0f;
		e->rotation = e->moveDir;
		if (e->age > 0x17)
			return false;
		aos_effect cp = *e;
		int n = fxh(w, cp.id + 2, 0, cp.pos, cp.rotation, cp.ownerId, cp.team);
		if (n >= 0) {
			w->obj.effect[n].scale = cp.scale;
			w->obj.effect[n].aux = cp.aux;
			w->obj.effect[n].aux2 = cp.alpha;
		}
		return true;
	}
	case 0x2be: case 0x2f0:
		e->alpha -= 0.25f;
		return e->alpha > 0.0f;
	default:
		return true;
	}
}

static void effect_draw(aos_world *w, aos_effect *e) /* FUN_004f55f0 */
{
	int bank = team_bank(e->team);
	bool palA = team_player(w, e->team)->color == 0; /* DAT_00885a00[team] == 0 */
	int a;
	switch (e->id) {
	case 0x226:
		a = (int)(128.0f * e->alpha);
		blit(aos_tex("hm_aura1"), bank, e->pos, 36.0f * e->scale, 36.0f * e->scale, e->rotation, a, 0xffffff, AOS_BLEND_ADD_ALPHA, 0, 0, 0x7f, 0x7f);
		break;
	case 100: case 0x65: case 0x66: case 0x96: case 0x97: case 0x98: {
		bool red = e->id < 0x96;
		a = (int)(128.0f * e->alpha);
		uint32_t rgb = red ? (palA ? 0xff0080 : 0xffc040) : (palA ? 0x0080ff : 0x00ff80);
		blit(aos_texf("hm_lncer%d", e->age % 3 + 3), bank, e->pos, 68.0f * e->scale, 68.0f * e->scale, e->rotation, a, rgb,
		     AOS_BLEND_ADD_ALPHA, 0, 0, 0x7f, 0x7f); /* literal: blue ids also use sprite 0x208 (FUN_004f55f0) */
		break;
	}
	case 200: case 0xfa:
		blit(aos_tex(e->id == 200 ? "hm_wingr" : "hm_wingb"), bank, e->pos, 64.0f * e->scale, 64.0f * e->alpha, e->rotation,
		     0xff, 0xffffff, AOS_BLEND_ALPHA, 0, 0, 0xff, 0xff);
		break;
	case 0xc9: case 0xfb:
		a = (int)(128.0f * e->alpha);
		blit(aos_tex(e->id == 0xc9 ? "hm_wingr" : "hm_wingb"), bank, e->pos, 72.0f * e->scale, 72.0f * e->aux, e->rotation, a,
		     0xffffff, AOS_BLEND_ADD_ALPHA, 0, 0, 0xff, 0xff);
		break;
	case 0xca: case 0xfc:
		a = (int)(200.0f * e->alpha);
		blit(aos_tex(e->id == 0xca ? "hm_wingr" : "hm_wingb"), bank, e->pos, 160.0f * e->scale, 160.0f * e->aux, e->rotation, a,
		     0xffffff, AOS_BLEND_ADD_ALPHA, 0, 0, 0xff, 0xff);
		break;
	case 300:
		a = (int)(200.0f * e->alpha);
		blit(aos_tex("hm_aura1"), bank, e->pos, -(48.0f * e->scale), 96.0f * e->scale, e->rotation, a, 0xffffff,
		     AOS_BLEND_ADD_ALPHA, 0, 0, 0x7f, 0x7f);
		break;
	case 0x230:
		a = (int)(96.0f * e->alpha);
		blit(aos_tex("hm_aura0"), bank, e->pos, 600.0f * e->scale, 600.0f * e->scale, e->rotation, a, 0xffffff,
		     AOS_BLEND_ADD_ALPHA, 0, 0, 0x7f, 0x7f);
		break;
	case 0x23a:
		a = (int)(255.0f * e->alpha);
		blit(aos_texf("hm_chain%02d", e->variant + 2), bank, e->pos, 36.0f * e->scale, 36.0f * e->scale, e->rotation, a,
		     palA ? 0xff00ff : 0xffc000, AOS_BLEND_ADD_ALPHA, 0, 0, 0x7f, 0x7f);
		break;
	case 600: case 0x262: case 0x259: case 0x263:
		a = (int)(255.0f * e->alpha);
		blit(aos_texf("hm_chain%02d", (e->id == 600 || e->id == 0x262) ? 1 : 0), bank, e->pos, 36.0f * e->scale,
		     36.0f * e->scale, e->rotation, a, 0xffffff, AOS_BLEND_ALPHA, 0, 0, 0x7f, 0x7f);
		break;
	case 700: case 750:
		a = (int)(255.0f * e->alpha);
		blit(aos_tex(e->id == 700 ? "hm_wingr" : "hm_wingb"), bank, e->pos, e->scale, e->aux, e->rotation, a, 0xffffff,
		     AOS_BLEND_ADD_ALPHA, 0, 0, 0xff, 0xff);
		break;
	case 0x2be: case 0x2f0:
		a = (int)(128.0f * e->alpha * e->aux2);
		blit(aos_tex(e->id == 0x2be ? "hm_wingr" : "hm_wingb"), bank, e->pos, e->scale, e->aux, e->rotation, a, 0xffffff,
		     AOS_BLEND_ADD_ALPHA, 0, 0, 0xff, 0xff);
		break;
	default:
		break;
	}
}

/* ---- beam family (FUN_004ab9a0 id 100 only; never spawned by Hime) ---- */

static int beam_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team)
{
	if (id != 100)
		return -1;
	int i = aos_beam_new(w, ID, id, pos, angle, owner, age0, team);
	if (i < 0)
		return -1;
	aos_beam *b = &w->obj.beam[i];
	b->speed = 16.0f;
	b->damage = 0x78;
	b->shape = (aos_shape){24.0f, 8.0f, 0};
	b->knockback = 10.0f;
	return i;
}

static bool beam_update(aos_world *w, aos_beam *b) /* FUN_004abbb0: returns 1, never moves */
{
	(void)w, (void)b;
	return true;
}

static void beam_on_hit(aos_world *w, aos_beam *b) /* FUN_004abac0 */
{
	if (b->id != 100)
		return;
	int32_t c = tcol(w, b->team, 0);
	fx0(w, 1000, c, b->pos, b->angle + AOS_PI, b->ownerId, 0, b->team);
	aos_spawn_particle(w, 1000, c, b->pos, b->angle, b->ownerId, 0, 6);
	aos_spawn_spark(w, 1000, c, 6, 6, b->angle, b->pos, b->ownerId);
	aos_se(w, 0x13);
}

/* ================================================================== */
/* 6. registration                                                     */

void aos_register_hime(void)
{
	aos_tab.inputA[ID] = input_a;
	aos_tab.inputB[ID] = input_b;
	/* CPU AI FUN_006c98f0 / FUN_006c8fd0: not ported */
	static const struct { int s; aos_update_fn t2; aos_draw_fn t1; } tab[] = {
		{0x1f0, st_neutral, dr_neutral},      {0x1f1, st_stub, dr_dash},
		{0x1f2, st_stub, dr_ko},              {0x1f3, st_stub, dr_hit},
		{0x1f4, st_stub, dr_sld},             {0x1fa, st_faint_breeze, dr_sld},
		{0x1fb, st_needle_fruit, dr_sld},     {0x1fc, st_plenty, dr_plenty},
		{0x1fd, st_paradise, dr_atka3},       {0x1fe, st_fever, dr_atka3},
		{0x1ff, st_binding, dr_atka3},        {0x200, st_binding2, dr_sld},
		{0x201, st_sadism, dr_atkb},          {0x202, st_amber, dr_atka4},
		{0x203, st_leash, dr_atka4},          {0x204, st_burning_near, dr_sld},
		{0x205, st_burning_far, dr_atka4},    {0x206, st_untouchable, dr_atkc},
		{0x207, st_godknows, dr_atkc},
	};
	for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++) {
		aos_tab.T2[tab[i].s] = tab[i].t2;
		aos_tab.T1[tab[i].s] = tab[i].t1;
	}
	aos_otab.beamSpawn[ID] = beam_spawn;
	aos_otab.beamUpdate[ID] = beam_update;
	aos_otab.beamOnHit[ID] = beam_on_hit;
	aos_otab.missileSpawn[ID] = missile_spawn;
	aos_otab.missileUpdate[ID] = missile_update;
	aos_otab.missileDraw[ID] = missile_draw;
	aos_otab.missileOnHit[ID] = missile_on_hit;
	aos_otab.missileOnKilled[ID] = missile_on_killed;
	aos_otab.hitboxSpawn[ID] = hitbox_spawn;
	aos_otab.hitboxOnHit[ID] = hitbox_on_hit;
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
