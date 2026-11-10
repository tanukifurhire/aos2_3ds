/* Iru (engine id 12, states 0x16e..0x184) — 1:1 port of her code in aos2.exe.
 * Spec: docs/re/iru.md. Every function names its original address.
 * Version: only the DAT_0089426c == 0x5a2 branches are ported. */
#include <string.h>

#include "../draw.h"
#include "../fx.h"
#include "../sim.h"

#define ID AOS_CH_IRU /* 12 */

/* ------------------------------------------------------------------ */
/* constants (address of the original float)                          */

#define K_PI 3.1415927f       /* DAT_00853b58 */
#define K_NPI -3.1415927f     /* DAT_00814100 */
#define K_HPI 1.5707964f      /* DAT_00814104 */
#define K_NHPI -1.5707964f    /* DAT_0081416c */
#define K_2PI 6.2831855f      /* DAT_00853b78 */

/* ------------------------------------------------------------------ */
/* small helpers                                                       */

/* FUN_00780450(team): player of a team (NULL for team 3 / invalid). */
static aos_player *player_of_team(aos_world *w, int32_t team)
{
	if (team == 1)
		return &w->p[0];
	if (team == 2)
		return &w->p[1];
	return NULL;
}

/* DAT_008859f4 + team*0x10 + 4k: per-team effect colour index. */
static int32_t team_colour(aos_world *w, int32_t team, int k) { return aos_team_colour(w, team, k); }

/* FUN_0068b8a0 particles / FUN_007bd5e0 sparks (original argument order). */
static void particle(aos_world *w, int kind, int32_t colour, const float pos[3], float angle,
                     int32_t owner, int32_t age0, int n)
{
	aos_spawn_particle(w, kind, colour, pos, angle, owner, age0, n);
}
static void spark(aos_world *w, int kind, int32_t colour, int a, int n, float angle,
                  const float pos[3], int32_t owner)
{
	aos_spawn_spark(w, kind, colour, a, n, angle, pos, owner);
}

static void fx0(aos_world *w, int32_t id, int32_t variant, const float pos[3], float angle,
                int32_t owner, int32_t age0, int32_t team)
{
	aos_spawn_effect(w, 0, id, variant, pos, angle, owner, age0, team);
}

static int fx12(aos_world *w, int32_t id, int32_t variant, const float pos[3], float angle,
                int32_t owner, int32_t age0, int32_t team);
static int hb_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner,
                    int32_t zero, int32_t team);
static bool missile_on_hit(aos_world *w, aos_missile *m);
static int whip_spawn(aos_world *w, int32_t id, int32_t ownerId, int32_t p3);

static void face_opponent_x(aos_world *w, aos_player *p) /* 0x17c / 0x17e t==0 */
{
	aos_player *o = aos_opponent(w, p);
	if (o->x < p->x || o->x == p->x)
		p->faceAngle = K_PI;
	else
		p->faceAngle = 0.0f;
	p->dashAngle = p->faceAngle;
}

/* common tail: "if (t < N || D != 3) { if (t > end) state = 0 } else dash-cancel" */
static void tail(aos_world *w, aos_player *p, int32_t cancelFrom, int32_t end)
{
	if (p->timer < cancelFrom || p->D != AOS_BTN_PRESSED) {
		if (p->timer > end)
			p->state = AOS_ST_NEUTRAL;
	} else {
		aos_dash(w, p, 1);
	}
}

/* ------------------------------------------------------------------ */
/* states (T2)                                                         */

static bool st_neutral(aos_world *w, aos_player *p) /* 0x16e T2 FUN_006d0f50 */
{
	(void)w;
	float v = aos_tab.stats[p->id].walk;
	if (p->up > 1)
		p->y += v;
	if (p->down > 1)
		p->y -= v;
	if (p->left > 1)
		p->x -= v;
	if (p->right > 1)
		p->x += v;
	p->faceAngle = p->facingRight == 1 ? 0.0f : K_PI;
	return true;
}

static bool st_stub(aos_world *w, aos_player *p) /* 0x16f..0x172 T2 FUN_006d1040/1050/1060/1070 */
{
	(void)w; (void)p;
	return true;
}

static bool st_rifle(aos_world *w, aos_player *p) /* 0x178 T2 FUN_006d1080 — High Speed Photon Rifle */
{
	if (p->timer == 14 && aos_use_gauge(p, 0x178)) {
		p->faceAngle = aos_aim(w, p);
		aos_spawn_beam(w, ID, 100, &p->x, p->faceAngle, p->objId, 0, p->side);
		aos_se(w, 0xd);
	}
	tail(w, p, 22, 40);
	return true;
}

static bool st_rifle_press(aos_world *w, aos_player *p) /* 0x179 T2 FUN_006d1180 — Scatter-shot */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer < 10 && p->A < 1) {
		p->state = 0x178; /* timer kept */
		aos_tab.T2[p->state](w, p);
		return true;
	}
	if (p->timer > 9 && p->timer < 20 && (p->timer - 10) % 3 == 0 && aos_use_gauge(p, 0x179)) {
		p->faceAngle = aos_aim(w, p);
		float r = aos_rand_real(w, -0.15f, 0.15f); /* DAT_00815dc4, DAT_00813f20 */
		aos_spawn_beam(w, ID, 0x96, &p->x, r + p->faceAngle, p->objId, 0, p->side);
		aos_se(w, 0xd);
	}
	tail(w, p, 20, 30);
	return true;
}

static bool st_rocket_tap(aos_world *w, aos_player *p) /* 0x17a T2 FUN_006d1340 */
{
	int32_t t = p->timer;
	if (t > 9 && t < 27 && t % 2 == 0 && aos_use_gauge(p, 0x17a)) {
		float o = 0.0f;
		if (t > 11 && t < 15)
			o = 0.3f; /* DAT_00813f28 */
		if (t > 15 && t < 19)
			o = 0.6f; /* DAT_00813f34 */
		if (t > 19 && t < 23)
			o = 0.9f; /* DAT_00814178 */
		if (t > 23 && t < 27)
			o = 1.2f; /* DAT_00817d78 */
		float a = (t / 2) % 2 == 0 ? p->faceAngle + o : p->faceAngle - o;
		aos_spawn_missile(w, ID, 100, &p->x, a, p->objId, 0, p->side, p->color);
		aos_se_cd(w, 0x22, 4);
	}
	tail(w, p, 30, 50);
	return true;
}

static bool st_rocket_press(aos_world *w, aos_player *p) /* 0x17b T2 FUN_006d1680 — Straight Rockets */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer < 11 && p->B < 1) {
		p->state = 0x17a;
		aos_tab.T2[p->state](w, p);
		return true;
	}
	int32_t t = p->timer;
	if (t > 11 && t <= 50 && t % 2 == 0 && aos_use_gauge(p, 0x17b)) { /* 0x32 for ver > 0x5a0 */
		float o = 0.2f; /* DAT_00853b14 */
		if (t > 15 && t < 19) o = 0.4f;  /* DAT_00813f30 */
		if (t > 19 && t < 23) o = 0.6f;  /* DAT_00813f34 */
		if (t > 23 && t < 27) o = 0.8f;  /* DAT_008145e8 */
		if (t > 27 && t < 31) o = 1.0f;  /* DAT_00853b34 */
		if (t > 31 && t < 35) o = 1.2f;  /* DAT_00817d78 */
		if (t > 35 && t < 39) o = 1.4f;  /* DAT_00817dd0 */
		if (t > 39 && t < 43) o = 1.6f;  /* DAT_0082533c */
		if (t > 43 && t < 47) o = 1.8f;  /* DAT_0081413c */
		if (t > 47 && t < 51) o = 2.0f;  /* DAT_00813a4c */
		float a = (t / 2) % 2 == 0 ? p->faceAngle + o : p->faceAngle - o;
		aos_spawn_missile(w, ID, 100, &p->x, a, p->objId, 0, p->side, p->color);
		aos_se_cd(w, 0x22, 4);
	}
	tail(w, p, 30, 60);
	return true;
}

static aos_enemy *field_new(aos_world *w, aos_player *p)
{
	int i = aos_spawn_enemy(w, ID, 100, p->objId, p->side, p->color);
	if (i < 0)
		return NULL;
	aos_enemy *e = &w->obj.enemy[i];
	e->pos[0] = p->x;
	e->pos[1] = p->y;
	e->pos[2] = p->z;
	return e;
}

static bool st_field(aos_world *w, aos_player *p) /* 0x17c T2 FUN_006d1b00 — Electromagnetic Field */
{
	if (p->timer == 0)
		face_opponent_x(w, p);
	if (p->timer == 10 && aos_use_gauge(p, 0x17c)) {
		field_new(w, p);
		aos_se(w, 0x2f);
	}
	tail(w, p, 15, 30);
	return true;
}

static bool st_field_dash(aos_world *w, aos_player *p) /* 0x17d T2 FUN_006d1c80 — (D)+S+A */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer == 10 && aos_use_gauge(p, 0x17c)) {
		aos_enemy *e = field_new(w, p);
		if (e) {
			e->f7c = 0x6e;
			e->f70 = 10.0f; /* DAT_00813f4c */
			e->angle2 = p->faceAngle;
		}
		aos_se(w, 0x2f);
	}
	if (p->timer < 15 || p->D != AOS_BTN_PRESSED) {
		if (p->timer > 30)
			p->state = AOS_ST_NEUTRAL;
		p->friction *= 0.5f; /* DAT_00853b1c */
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_wide_field(aos_world *w, aos_player *p) /* 0x17e T2 FUN_006d1e10 — Wide-Area Electric Field */
{
	if (p->timer == 0) {
		face_opponent_x(w, p);
		aos_barrier(w, p, 10);
		aos_hyper_flourish_ex(w, p, 6, 100, 0.75f, 100, 0.0f, 0.0f, 0x14, 0.5f, 2, 1); /* speedline (100,pos,0,0) */
	}
	if (p->timer == 10) {
		static const struct { float base; int32_t sub; } k[4] = {
			{0.0f, 0x96}, {K_PI, 0x96}, {K_HPI, 0x97}, {K_NHPI, 0x97},
		};
		for (int j = 0; j < 4; j++) {
			aos_enemy *e = field_new(w, p);
			if (!e)
				continue;
			e->offset[0] = p->x; /* +0xc8 centre */
			e->offset[1] = p->y; /* +0xcc */
			e->fd0 = p->z;       /* +0xd0 */
			e->f70 = k[j].base;
			e->f7c = k[j].sub;
			whip_spawn(w, 6, p->color, e->id);
		}
		aos_se(w, 0x2f);
	}
	tail(w, p, 30, 60);
	return true;
}

static bool st_wide_field_dash(aos_world *w, aos_player *p) /* 0x17f T2 FUN_006d2330 — (D)+S+H */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		aos_barrier(w, p, 10);
		aos_hyper_flourish(w, p);
	}
	if (p->timer == 10) {
		const float off[4] = {-0.35f, 0.35f, -0.75f, 0.75f}; /* DAT_00813f2c, DAT_00813f3c */
		for (int j = 0; j < 4; j++) {
			aos_enemy *e = field_new(w, p);
			if (!e)
				continue;
			e->f7c = 0x6e;
			e->f70 = 12.0f; /* DAT_00813f54 */
			e->angle2 = p->faceAngle + off[j];
		}
		aos_se(w, 0x2f);
	}
	tail(w, p, 30, 60);
	return true;
}

static bool st_pod(aos_world *w, aos_player *p) /* 0x180 T2 FUN_006d2770 — Homing Missile Pod */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	int32_t t = p->timer;
	if (t > 9 && t < 21 && t % 2 == 0 && aos_use_gauge(p, 0x180)) { /* ver > 0x58b: own row */
		float o;
		switch (t) {
		case 10: case 12: o = 1.4f; break; /* DAT_00817dd0 */
		case 18: case 20: o = 1.0f; break; /* DAT_00853b34 */
		default: o = 1.2f; break;          /* DAT_00817d78 */
		}
		float a = (t / 2) % 2 == 0 ? p->faceAngle + o : p->faceAngle - o;
		aos_spawn_missile(w, ID, 200, &p->x, a, p->objId, 0, p->side, p->color);
		aos_se_cd(w, 0x22, 4);
	}
	tail(w, p, 32, 60);
	return true;
}

static bool st_curtain(aos_world *w, aos_player *p) /* 0x181 T2 FUN_006d2b00 — Full-Fire Bullet Curtain */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		aos_barrier(w, p, 10);
		aos_hyper_flourish(w, p);
	}
	int32_t t = p->timer;
	if (t > 9 && t < 37 && t % 2 == 0) {
		float o;
		switch (t) {
		case 14: case 16: o = 1.3f; break; /* DAT_00817d7c */
		case 18: case 20: o = 1.2f; break; /* DAT_00817d78 */
		case 22: case 24: o = 1.1f; break; /* DAT_00853b38 */
		case 26: case 28: o = 1.0f; break; /* DAT_00853b34 */
		case 30: case 32: o = 0.9f; break; /* DAT_00814178 */
		case 34: case 36: o = 0.8f; break; /* DAT_008145e8 */
		default: o = 1.4f; break;          /* DAT_00817dd0 */
		}
		float a = (t / 2) % 2 == 0 ? p->faceAngle + o : p->faceAngle - o;
		aos_spawn_missile(w, ID, 200, &p->x, a, p->objId, 0, p->side, p->color);
		aos_se_cd(w, 0x22, 4);
	}
	tail(w, p, 38, 60);
	return true;
}

/* FUN_00630880(id, owner, team): every laser with that id/owner/team stops following. */
static void lasers_detach(aos_world *w, int32_t id, int32_t owner, int32_t team)
{
	for (int i = 0; i < w->obj.nlaser; i++) {
		aos_laser *l = &w->obj.laser[i];
		if (l->id == id && l->ownerId == owner && l->team == team)
			l->f4d = 1;
	}
}

static bool st_linear_ray(aos_world *w, aos_player *p) /* 0x182 T2 FUN_006d2f50 — High Output Linear Ray */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		aos_barrier(w, p, 10);
		aos_hyper_flourish(w, p);
		lasers_detach(w, 100, p->objId, p->side); /* ver > 0x589 */
	}
	if (p->timer == 6) {
		p->speed = 0;
		aos_spawn_laser(w, ID, 100, &p->x, p->faceAngle, p->objId, p->side, p->color);
		aos_se(w, 0x12);
	}
	if (p->timer > 5 && p->timer < 65) {
		float a = aos_dir_angle(p);
		if (a <= K_2PI)
			aos_angle_toward(&p->faceAngle, a, 0.01f); /* DAT_00813ec8 */
	}
	tail(w, p, 64, 84);
	return true;
}

static bool st_genocide(aos_world *w, aos_player *p) /* 0x183 T2 FUN_006d32b0 — Genocide Bomb */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		aos_barrier(w, p, 10);
		p->barrierTime = p->barrierTime / 2;
		p->barrierBreakT = 25; /* +0x108, ver > 0x58a */
		aos_hyper_flourish_ex(w, p, 6, 100, 0.75f, 0, 0.0f, 0.0f, 0x14, 0.5f, 2, 1); /* no speedline */
	}
	if (p->timer == 15) {
		aos_spawn_missile(w, ID, 400, &p->x, p->faceAngle, p->objId, 0, p->side, p->color);
		aos_se_cd(w, 0x22, 1);
	}
	tail(w, p, 25, 35);
	return true;
}

static bool st_destruction_ray(aos_world *w, aos_player *p) /* 0x184 T2 FUN_006d3540 — Accel */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		aos_barrier(w, p, 0x14);
		/* SE 7, dark(120,.85), speedline(100,pos,aim,π/4), blur(120,.75,2), effect 0xfa */
		aos_hyper_flourish_ex(w, p, 7, 0x78, 0.85f, 100, p->faceAngle, 0.7853982f, 0x78, 0.75f, 2, 1);
	}
	if (p->timer == 5) { /* ver > 0x5a0 */
		p->speed = 0;
		aos_spawn_laser(w, ID, 200, &p->x, p->faceAngle, p->objId, p->side, p->color);
		aos_se(w, 0x12);
	}
	if (p->timer > 9 && p->timer < 61 && p->timer % 2 == 0) {
		float pos[3] = {aos_cos(p->faceAngle) * 50.0f + p->x, aos_sin(p->faceAngle) * 50.0f + p->y, 0};
		float r = aos_rand_real(w, -0.25f, 0.25f); /* DAT_0081411c, DAT_00813f24 */
		int32_t n = aos_rand_int(w, 0, 100) % 8;
		fx0(w, n + 0xa82, team_colour(w, p->side, 0), pos, r + p->faceAngle, p->objId, 0, p->side);
		pos[0] = aos_cos(p->faceAngle) * 100.0f + p->x;
		pos[1] = aos_sin(p->faceAngle) * 100.0f + p->y;
		spark(w, 1000, team_colour(w, p->side, 0), 10, 1, p->faceAngle + K_PI, pos, p->objId);
	}
	tail(w, p, 80, 100);
	return true;
}

/* ------------------------------------------------------------------ */
/* input dispatch                                                      */

static bool go(aos_world *w, aos_player *p, int32_t s)
{
	aos_set_state(w, p, s);
	return true;
}

static bool normals(aos_world *w, aos_player *p) /* shared tail of both schemes */
{
	if (p->state == AOS_ST_NEUTRAL || p->state == AOS_ST_DASH) {
		if (p->D == 3) {
			aos_dash(w, p, 0);
		} else if (p->Shield == 3 && aos_spend_hyper(p, 0.5f) && aos_may_shield(w, p)) {
			aos_shield(w, p);
		} else {
			if (p->D > 1 && p->S > 1 && p->A == 3)
				return go(w, p, 0x17d);
			if (p->S > 1 && p->A == 3)
				return go(w, p, 0x17c);
			if (p->S > 1 && p->B == 3)
				return go(w, p, 0x180);
			if (p->B == 3)
				return go(w, p, 0x17b);
			if (p->A == 3)
				return go(w, p, 0x179);
		}
	}
	return aos_tab.T2[p->state](w, p);
}

static bool input_a(aos_world *w, aos_player *p) /* FUN_006d3910 */
{
	if (aos_may_hyper(w, p)) {
		if (p->S > 1 && p->B > 1 && p->H == 3 && aos_spend_hyper(p, 3.0f))
			return go(w, p, 0x184);
		if (p->D > 1 && p->S > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0x17f);
		if (p->S > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0x17e);
		if (p->B > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0x183);
		if (p->D > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0x181);
		if (p->H == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0x182);
	}
	return normals(w, p);
}

static bool input_b(aos_world *w, aos_player *p) /* FUN_006d3e60 (control scheme B) */
{
	if (aos_may_hyper(w, p)) {
		if (p->H > 1 && p->Shield == 3 && aos_spend_hyper(p, 3.0f))
			return go(w, p, 0x184);
		if (p->D > 1 && p->H > 1 && p->S == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0x17f);
		/* original quirk: S held && H held, no press edge (0x6d3f75/0x6d3f7e) */
		if (p->S > 1 && p->H > 1 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0x17e);
		if (p->H > 1 && p->B == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0x183);
		if (p->H > 1 && p->D == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0x181);
		if (p->H > 1 && p->A == 3 && aos_spend_hyper(p, 1.0f))
			return go(w, p, 0x182);
	}
	return normals(w, p);
}

/* ------------------------------------------------------------------ */
/* drawing helpers                                                     */

static aos_dcmd *spr(aos_world *w, int bank, const char *fmt, int frame, float S, float x, float y)
{
	(void)w;
	aos_dcmd *c = aos_draw(aos_texf(fmt, frame), bank, x, y, 0);
	c->halfW = c->halfH = S * 0.5f;
	return c;
}

/* AIM transform (iru.md §4.2): RotZ(aim) [+ RotX(180°) if |aim| >= π/2] + RotY(180°).
 * RotX(180°) -> y-scale × cos(π) = −1 (PORTING.md rule). Global tint FUN_004e8f50 = white. */
static aos_dcmd *draw_aim(aos_world *w, aos_player *p, const char *fmt, int frame)
{
	aos_dcmd *c = spr(w, aos_pidx(w, p), fmt, frame, 96.0f, p->x, p->y);
	c->rotZ = aos_rad2bam(p->faceAngle);
	c->flipX = 1;
	if (aos_fabs(p->faceAngle) >= K_HPI)
		c->sy = -1.0f;
	c->blend = AOS_BLEND_ALPHA;
	return c;
}

static int pp(int32_t t, int n) /* (t/n)&3 with 3 -> 1 */
{
	int f = (t / n) % 4;
	return f == 3 ? 1 : f;
}

/* ------------------------------------------------------------------ */
/* draw handlers (T1)                                                  */

static void dr_neutral(aos_world *w, aos_player *p) /* 0x16e T1 FUN_006d5240 */
{
	const char *s = "ir_std%02d";
	if (p->up > 1) s = "ir_fwd%02d";
	if (p->down > 1) s = "ir_bck%02d";
	if (p->facingRight == 0 && p->left > 1) s = "ir_fwd%02d";
	if (p->facingRight == 1 && p->left > 1) s = "ir_bck%02d";
	if (p->facingRight == 0 && p->right > 1) s = "ir_bck%02d";
	if (p->facingRight == 1 && p->right > 1) s = "ir_fwd%02d";
	aos_dcmd *c = spr(w, aos_pidx(w, p), s, pp(p->timer, 5), 96.0f, p->x, p->y);
	c->flipX = p->facingRight != 0; /* RotY(π) when +0x8c != 0 */
	int32_t rot[3] = {0, p->facingRight != 0 ? aos_rad2bam(K_PI) : 0, 0};
	aos_afterimage(w, c, 1, p, rot); /* FUN_004d7e00 */
}

static void dr_dash(aos_world *w, aos_player *p) /* 0x16f T1 FUN_006d5500 */
{
	int f = pp(p->timer, 3);
	float a = aos_fabs(p->faceAngle);
	if ((p->facingRight == 1 && a > K_HPI) || (p->facingRight == 0 && a < K_HPI))
		f += 3;
	aos_dcmd *c = draw_aim(w, p, "ir_dsh%02d", f);
	int32_t rot[3] = {K_HPI <= aos_fabs(p->faceAngle) ? aos_deg2bam(0xb4) : 0, aos_deg2bam(0xb4),
	                  aos_rad2bam(p->faceAngle)};
	aos_afterimage(w, c, 1, p, rot); /* FUN_004d7e00 */
}

static void dr_ko(aos_world *w, aos_player *p) /* 0x170 T1 FUN_006d5e90 */
{
	int f = pp(p->timer, 3);
	if (w->frame % 2 == 0)
		f += 3;
	if (p->timer < 100)
		draw_aim(w, p, "ir_los%02d", f);
}

static void dr_hit(aos_world *w, aos_player *p) /* 0x171 T1 FUN_006d5900 */
{
	draw_aim(w, p, "ir_los%02d", pp(p->timer, 3));
}

static void dr_sld(aos_world *w, aos_player *p) /* 0x172/0x17c/0x17d/0x17f T1 FUN_006d5bd0/6d6d20/6d6fe0/6d7580 */
{
	int f = p->timer / 3;
	if (f > 3)
		f = 3;
	draw_aim(w, p, "ir_sld%02d", f);
}

static void dr_wide_field(aos_world *w, aos_player *p) /* 0x17e T1 FUN_006d72a0 */
{
	int f = p->timer < 10 ? 0 : ((p->timer - 10) / 3) % 3 + 1;
	draw_aim(w, p, "ir_sld%02d", f);
}

static void dr_rifle(aos_world *w, aos_player *p) /* 0x178 T1 FUN_006d6190 */
{
	int f = p->timer < 15 ? 0 : (p->timer - 15) / 3 + 1;
	draw_aim(w, p, "ir_rifle%02d", f > 4 ? 4 : f);
}

static void dr_rifle_press(aos_world *w, aos_player *p) /* 0x179 T1 FUN_006d6470 */
{
	int f = p->timer < 20 ? 0 : (p->timer - 20) / 3 + 1;
	draw_aim(w, p, "ir_rifle%02d", f > 4 ? 4 : f);
}

static void dr_hand(aos_world *w, aos_player *p) /* 0x17a T1 FUN_006d6750 */
{
	int f = p->timer < 12 ? 0 : p->timer < 26 ? 1 : (p->timer - 26) / 3 + 2;
	draw_aim(w, p, "ir_hand%02d", f > 6 ? 6 : f);
}

static void dr_handa(aos_world *w, aos_player *p) /* 0x17b/0x180/0x181 T1 FUN_006d6a40/6d7840/6d7b20 */
{
	int f = p->timer < 12 ? 0 : ((p->timer - 12) / 3) % 3 + 1;
	draw_aim(w, p, "ir_handa%02d", f);
}

static void dr_linear_ray(aos_world *w, aos_player *p) /* 0x182 T1 FUN_006d7e00 */
{
	int f = p->timer < 50 ? 0 : (p->timer - 50) / 5 + 1;
	draw_aim(w, p, "ir_rifle%02d", f > 4 ? 4 : f);
}

static void dr_genocide(aos_world *w, aos_player *p) /* 0x183 T1 FUN_006d80e0 */
{
	int f = p->timer < 25 ? 0 : (p->timer - 25) / 5;
	draw_aim(w, p, "ir_bazz%02d", f > 5 ? 5 : f);
}

static void dr_destruction_ray(aos_world *w, aos_player *p) /* 0x184 T1 FUN_006d83c0 */
{
	int f = p->timer < 80 ? 0 : (p->timer - 80) / 5 + 1;
	draw_aim(w, p, "ir_rifle%02d", f > 4 ? 4 : f);
}

/* ------------------------------------------------------------------ */
/* beams (type 12): spawn FUN_004abbd0, update FUN_004abf90, draw FUN_004abd50, onHit FUN_004abd70 */

static int beam_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner,
                      int32_t age0, int32_t team)
{
	if (id != 100 && id != 0x96)
		return -1;
	int i = aos_beam_new(w, ID, id, pos, angle, owner, age0, team);
	if (i < 0)
		return -1;
	aos_beam *b = &w->obj.beam[i];
	if (id == 100) {
		b->speed = 25.0f;                       /* DAT_008140f0 */
		b->damage = 360;
		b->shape = (aos_shape){36.0f, 16.0f, 0}; /* DAT_008140f4, DAT_008140d8 */
		b->knockback = 10.0f;                   /* DAT_00813f4c */
	} else {
		b->speed = 16.0f;                       /* DAT_008140d8 */
		b->damage = 120;                        /* ver > 0x5a0 */
		b->shape = (aos_shape){24.0f, 12.0f, 0}; /* DAT_00853b8c, DAT_00813f54 */
		b->knockback = 10.0f;
	}
	return i;
}

static bool beam_update(aos_world *w, aos_beam *b)
{
	if (b->id != 100 && b->id != 0x96)
		return true;
	b->pos[0] = aos_cos(b->moveAngle) * b->speed + b->pos[0];
	b->pos[1] = aos_sin(b->moveAngle) * b->speed + b->pos[1];
	if (!aos_in_arena(b->pos, 10.0f))
		return false;
	int32_t col = team_colour(w, b->team, 0);
	if (b->id == 100) {
		if (b->age % 3 == 0)
			fx0(w, 0x548, col, b->pos, b->angle, b->ownerId, 0, b->team);
		fx12(w, 300, col, b->pos, b->angle, b->ownerId, 0, b->team);
	} else {
		if (b->age % 2 == 0)
			particle(w, 0x456, col, b->pos, b->angle + K_PI, b->ownerId, 0, 1);
		fx12(w, 0x15e, col, b->pos, b->angle, b->ownerId, 0, b->team);
	}
	return true;
}

static void beam_draw(aos_world *w, aos_beam *b) { (void)w; (void)b; } /* FUN_004abd50: empty */

static void beam_on_hit(aos_world *w, aos_beam *b)
{
	int32_t col = team_colour(w, b->team, 0);
	if (b->id == 100) {
		fx0(w, 1000, col, b->pos, b->angle + K_PI, b->ownerId, 0, b->team);
		fx0(w, 0x514, col, b->pos, b->angle, b->ownerId, 0, b->team);
		particle(w, 1000, col, b->pos, b->angle, b->ownerId, 0, 10);
		spark(w, 1000, col, 10, 10, b->angle, b->pos, b->ownerId);
		aos_se(w, 0x13);
		aos_se(w, 0x15);
	} else if (b->id == 0x96) {
		fx0(w, 1000, col, b->pos, b->angle + K_PI, b->ownerId, 0, b->team);
		particle(w, 1000, col, b->pos, b->angle, b->ownerId, 0, 10);
		spark(w, 1000, col, 6, 6, b->angle, b->pos, b->ownerId);
		aos_se(w, 0x13);
	}
}

/* ------------------------------------------------------------------ */
/* missiles (type 12): spawn FUN_00665eb0, update FUN_00668010, draw FUN_00666160,
 * onHit FUN_00667c90, onKilled FUN_00667ec0 */

static int missile_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner,
                         int32_t age0, int32_t team, int32_t p18)
{
	if (id != 100 && id != 200 && id != 400)
		return -1;
	int i = aos_missile_new(w, ID, id, pos, angle, owner, age0, team, p18);
	if (i < 0)
		return -1;
	aos_missile *m = &w->obj.missile[i];
	if (id == 100) { /* ver >= 0x5a1 */
		m->speed = 12.0f;  /* DAT_00813f54 */
		m->hp = 120;
		m->damage = 150;
		m->knockback = 10.0f;
		m->shapeProj.radius = 16.0f; /* DAT_008140d8 */
		m->shapeBody.radius = m->shapeProj.radius;
	} else if (id == 200) {
		m->speed = 25.0f;  /* DAT_008140f0 */
		m->hp = 180;
		m->damage = 170;   /* ver > 0x5a0 */
		m->knockback = 15.0f; /* DAT_00853b88 */
		m->shapeBody.radius = 16.0f;
		m->shapeProj.radius = 16.0f;
	} else {
		m->speed = 5.0f;   /* DAT_00813f40, ver > 0x5a0 */
		m->hp = 540;
		m->damage = 400;
		m->shapeProj.radius = 26.0f; /* DAT_008178d0, ver > 0x5a0 */
		m->knockback = 10.0f;
		m->shapeBody.radius = m->shapeProj.radius;
	}
	return i;
}

static bool missile_update(aos_world *w, aos_missile *m)
{
	if (m->id == 100) {
		if (m->age < 20) {
			if (2.0f < m->speed)
				m->speed = m->speed - 1.0f;
		} else if (m->speed <= 12.0f && 12.0f != m->speed) {
			m->speed = m->speed + 0.5f;
		}
		if (m->age % 2 == 0 || 12.0f < m->speed)
			fx0(w, 0x802, 0, m->pos, m->moveAngle, m->ownerId, 0, m->team);
		m->pos[0] = aos_cos(m->moveAngle) * m->speed + m->pos[0];
		m->pos[1] = aos_sin(m->moveAngle) * m->speed + m->pos[1];
		if (m->age > 10 && !aos_in_arena(m->pos, 10.0f)) {
			missile_on_hit(w, m);
			return false;
		}
	} else if (m->id == 200) {
		if (m->age < 20) {
			aos_player *o = player_of_team(w, m->team);
			o = o ? aos_opponent(w, o) : NULL; /* FUN_00780480(team) */
			if (o)
				aos_angle_toward(&m->moveAngle, aos_atan2(o->y - m->pos[1], o->x - m->pos[0]), 0.05f);
			if (2.0f < m->speed)
				m->speed = m->speed * 0.9f; /* DAT_00814178 */
		} else if (m->age < 40) {
			aos_player *o = player_of_team(w, m->team);
			o = o ? aos_opponent(w, o) : NULL;
			if (o)
				aos_angle_toward(&m->moveAngle, aos_atan2(o->y - m->pos[1], o->x - m->pos[0]), 0.025f);
			if (m->speed <= 24.0f && 24.0f != m->speed)
				m->speed = m->speed + 0.75f;
			if (m->age > 19 && m->age < 25) {
				float r = aos_rand_real(w, -0.2f, 0.2f); /* DAT_00817c04, DAT_00853b14 */
				int32_t n = aos_rand_int(w, 0, 100) % 8;
				fx0(w, n + 0xa28, 0xd, m->pos, r + m->angle, m->ownerId, 0, m->team);
			}
		}
		m->angle = m->moveAngle;
		if (m->age % 2 == 0 || 12.0f < m->speed) {
			float pt[3] = {-aos_cos(m->angle) * 20.0f + m->pos[0], -aos_sin(m->angle) * 20.0f + m->pos[1], 0};
			fx0(w, 0x802, 0, pt, m->moveAngle, m->ownerId, 0, m->team);
		}
		m->pos[0] = aos_cos(m->moveAngle) * m->speed + m->pos[0];
		m->pos[1] = aos_sin(m->moveAngle) * m->speed + m->pos[1];
		if (m->age > 50 && !aos_in_arena(m->pos, 10.0f)) {
			missile_on_hit(w, m);
			return false;
		}
	} else if (m->id == 400) {
		if (m->speed <= 16.0f && 16.0f != m->speed)
			m->speed = m->speed + 0.25f;
		float r = aos_rand_real(w, K_NPI, K_PI);
		fx0(w, 2000, 0, m->pos, r, m->ownerId, 0, m->team);
		m->pos[0] = aos_cos(m->moveAngle) * m->speed + m->pos[0];
		m->pos[1] = aos_sin(m->moveAngle) * m->speed + m->pos[1];
		if (!aos_in_arena(m->pos, 0.0f)) {
			missile_on_hit(w, m);
			return false;
		}
	}
	return true;
}

static bool missile_on_hit(aos_world *w, aos_missile *m)
{
	if (m->id == 100 || m->id == 200) {
		int32_t col = team_colour(w, m->team, 0);
		fx12(w, 100, col, m->pos, m->angle + K_PI, m->ownerId, 0, m->team);
		fx0(w, 0x514, col, m->pos, 0, m->ownerId, 0, m->team);
		aos_se(w, 0x14);
	} else if (m->id == 400) {
		aos_screen_dark(w, 100, 0.75f);                 /* DAT_00813f3c */
		aos_screen_blur(w, 0x32, 0.4f, 2);              /* DAT_00813f30 */
		aos_speedline(w, 100, m->pos, m->angle, 0.0f);
		fx12(w, 200, 0, m->pos, 0, m->ownerId, 0, m->team);
		aos_se(w, 0x1a);
	}
	return false;
}

static bool missile_on_killed(aos_world *w, aos_missile *m)
{
	if (m->id == 100 || m->id == 200) {
		fx0(w, 0x262, 0, m->pos, 0, m->ownerId, 0, m->team);
		fx0(w, 0x514, 1, m->pos, 0, m->ownerId, 0, m->team);
		aos_se(w, 0x1d);
	} else if (m->id == 400) {
		missile_on_hit(w, m);
	}
	return false;
}

static int owner_bank(aos_world *w, int32_t ownerId)
{
	aos_player *p = aos_player_by_objid(w, ownerId);
	return p ? aos_pidx(w, p) : AOS_BANK_P0;
}

static void missile_layer(aos_world *w, int bank, const char *fmt, int frame, float S, const float pos[3],
                          float angle, int blend, float alpha)
{
	aos_dcmd *c = spr(w, bank, fmt, frame, S, pos[0], pos[1]);
	c->rotZ = aos_rad2bam(angle);
	c->blend = (uint8_t)blend;
	c->rgba = aos_rgba(1, 1, 1, alpha);
}

static void missile_exhaust(aos_world *w, aos_missile *m, int bank)
{
	float v[2] = {42.0f, 0.0f}; /* DAT_00824488 */
	aos_vec_rotate(v, m->angle + K_PI);
	float pt[3] = {v[0] + m->pos[0], v[1] + m->pos[1], 0};
	aos_dcmd *c = spr(w, bank, "ir_bfireb", 0, 48.0f, pt[0], pt[1]);
	c->rotZ = aos_rad2bam(m->angle);
	c->blend = AOS_BLEND_ADD_ALPHA;
	c = spr(w, bank, "ir_bfire", 0, 48.0f, pt[0], pt[1]);
	c->rotZ = aos_rad2bam(m->angle);
	c->blend = AOS_BLEND_ADD_ALPHA;
	if ((m->age / 2) % 2 == 0)
		c->sy = 0.3f; /* DAT_00813f28 */
}

static void missile_draw(aos_world *w, aos_missile *m)
{
	int bank = owner_bank(w, m->ownerId);
	float s = aos_sin(((float)(m->age % 50) / 50.0f) * K_2PI);
	float p1 = s * 0.15f + 0.85f; /* DAT_00813f20, DAT_00814120 */
	float p2 = s * 0.25f + 0.75f; /* DAT_00813f24, DAT_00813f3c */
	if (m->id == 100) {
		int f = (m->age / 3) % 3;
		missile_layer(w, bank, "ir_hrckt%02d", f, 64.0f, m->pos, m->angle, AOS_BLEND_ALPHA, 1.0f);
		missile_layer(w, bank, "ir_hrckt%02d", 3, 64.0f, m->pos, m->angle, AOS_BLEND_ADD_ALPHA, p1);
		missile_layer(w, bank, "ir_hrckt%02d", 4, 64.0f, m->pos, m->angle, AOS_BLEND_ADD_ALPHA, p2);
	} else if (m->id == 200) {
		if (m->growScale == 1.0f)
			missile_exhaust(w, m, bank);
		int f = (m->age / 3) % 3;
		missile_layer(w, bank, "ir_hmis%02d", f, 72.0f, m->pos, m->angle, AOS_BLEND_ALPHA, 1.0f);
		missile_layer(w, bank, "ir_hmis%02d", 3, 72.0f, m->pos, m->angle, AOS_BLEND_ADD_ALPHA, p1);
		missile_layer(w, bank, "ir_hmis%02d", 4, 72.0f, m->pos, m->angle, AOS_BLEND_ADD_ALPHA, p2);
	} else if (m->id == 400) {
		missile_exhaust(w, m, bank);
		int f = ((m->age / 3) % 3) * 3;
		missile_layer(w, bank, "ir_geno%02d", f, 92.0f, m->pos, m->angle, AOS_BLEND_ALPHA, 1.0f);
		missile_layer(w, bank, "ir_geno%02d", f + 1, 92.0f, m->pos, m->angle, AOS_BLEND_ADD_ALPHA, p1);
		missile_layer(w, bank, "ir_geno%02d", f + 2, 92.0f, m->pos, m->angle, AOS_BLEND_ADD_ALPHA, p2);
	}
}

/* ------------------------------------------------------------------ */
/* hitboxes (type 12): spawn FUN_005e2870, onHit FUN_005e2c40          */

static int hb_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner,
                    int32_t zero, int32_t team)
{
	aos_shape sh;
	int32_t dmg;
	float knock;
	switch (id) {
	case 100: case 0x6e:
		sh = (aos_shape){0, 0, 100.0f}; /* DAT_00853b90 */
		dmg = id == 0x6e ? 0 : 0x1e;
		knock = 0;
		break;
	case 200: case 0xd2:
		sh = (aos_shape){0, 0, 280.0f}; /* DAT_00814f14 */
		dmg = id == 0xd2 ? 0 : 0x3c;    /* ver > 0x5a0 */
		knock = id == 0xd2 ? 5.0f : 0;  /* DAT_00813f40 */
		break;
	case 300: case 0x136:
		sh = (aos_shape){800.0f, 60.0f, 0}; /* DAT_008149c8, DAT_00813a58 */
		dmg = id == 300 ? 0x5a : 0;         /* ver > 0x5a0 */
		knock = 5.0f;
		break;
	case 400: case 0x19a:
		sh = (aos_shape){800.0f, 120.0f, 0}; /* DAT_00814174 */
		dmg = id == 400 ? 0x8c : 0;          /* ver > 0x5a0 */
		knock = 5.0f;
		break;
	default:
		return -1;
	}
	int i = aos_hitbox_new(w, ID, id, pos, angle, owner, zero, team);
	if (i < 0)
		return -1;
	aos_hitbox *h = &w->obj.hitbox[i];
	h->hitType = 3;
	h->shape = sh;
	h->damage = dmg;
	h->knockback = knock;
	return i;
}

static void streaks(aos_world *w, aos_hitbox *hb, aos_player *t, int32_t col)
{
	for (int k = -3; k < 3; k++) {
		float a = ((float)k / 3.0f) * K_PI; /* DAT_00813a50, DAT_00853b58 */
		float r = aos_rand_real(w, -0.25f, 0.25f);
		fx0(w, 0x456, col, &t->x, a + r, hb->ownerId, 0, hb->team);
	}
}

static void hb_on_hit(aos_world *w, aos_hitbox *hb, aos_player *t)
{
	switch (hb->id) {
	case 0xd2: /* pull toward the blast centre */
		hb->angle = aos_atan2(hb->pos[1] - t->y, hb->pos[0] - t->x);
		break;
	case 100: case 0x6e:
		if (t->state != AOS_ST_HITSTUN || t->speed <= 2.0f) { /* DAT_00813a4c */
			if (hb->id == 100) {
				int32_t col = team_colour(w, hb->team, 0);
				streaks(w, hb, t, col);
				fx0(w, 0x515, col, &t->x, 0, hb->ownerId, 0, hb->team);
			}
			hb->angle = aos_atan2(t->y - hb->pos[1], t->x - hb->pos[0]);
			aos_player *own = player_of_team(w, hb->team);
			if (own && own->comboUi[4] > 11) /* attacker +0x128 */
				hb->knockback = 10.0f; /* DAT_00813f4c */
			aos_se_cd(w, 0x15, 5);
		} else {
			hb->dealsDamage = 0;
		}
		break;
	case 200: {
		float pt[3] = {t->x, t->y, t->z};
		pt[0] = aos_rand_real(w, -30.0f, 30.0f) + pt[0]; /* DAT_00817c08, DAT_008140e8 */
		pt[1] = aos_rand_real(w, -30.0f, 30.0f) + pt[1];
		fx0(w, 500, 0, pt, 0, hb->ownerId, 0, hb->team);
		aos_se_cd(w, 0x13, 5);
		break;
	}
	case 300: case 400: {
		int32_t col = team_colour(w, hb->team, 0);
		streaks(w, hb, t, col);
		particle(w, 1000, col, &t->x, hb->angle, hb->ownerId, 0, 4);
		spark(w, 1000, col, 10, 5, hb->angle, &t->x, hb->ownerId);
		aos_se_cd(w, 0x13, 4);
		break;
	}
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* lasers (type 12): spawn FUN_0061e3a0, update FUN_0061eff0, draw FUN_0061e500,
 * onHit FUN_0061eec0 (unreachable: no segments are ever created) */

static int laser_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner,
                       int32_t team, int32_t p18)
{
	if (id != 100 && id != 200)
		return -1;
	int i = aos_laser_new(w, ID, id, pos, angle, owner, team, p18);
	if (i < 0)
		return -1;
	aos_laser *l = &w->obj.laser[i];
	l->damage = 10;
	l->f48 = 200;
	l->hitType = 3;
	l->blockMode = 1;
	l->knockback = 10.0f; /* DAT_00813f4c */
	l->stun = 1;
	return i;
}

static bool laser_update(aos_world *w, aos_laser *l)
{
	aos_player *own = player_of_team(w, l->team);
	if (l->id == 100) {
		l->nseg = 0; /* FUN_004a9f10 */
		bool casting = own && own->state == 0x182;
		if (!casting && l->age < 0x33)
			l->age = 0x32;
		if (casting) {
			if (l->f4d == 0) {
				l->origin[0] = own->x;
				l->origin[1] = own->y;
				l->origin[2] = own->z;
				l->angle = own->faceAngle;
			}
			if (l->age % 5 == 0 && l->age < 0x33) {
				float pt[3] = {aos_cos(l->angle) * 40.0f + l->origin[0], aos_sin(l->angle) * 40.0f + l->origin[1], 0};
				int32_t col = team_colour(w, l->team, 0);
				fx0(w, 0x546, col, pt, l->angle, l->ownerId, 0, l->team);
				fx0(w, 0x547, col, pt, l->angle, l->ownerId, 0, l->team);
			}
		}
		if (l->age > 0x45)
			return false;
		if (l->age > 9 && l->age < 0x33) {
			float pt[3] = {aos_cos(l->angle) * 800.0f + l->origin[0], aos_sin(l->angle) * 800.0f + l->origin[1], 0};
			hb_spawn(w, l->age % 5 == 0 ? 300 : 0x136, pt, l->angle, l->ownerId, 0, l->team);
		}
	} else if (l->id == 200) {
		l->nseg = 0;
		if (l->age > 9 && l->age < 0x47 && l->age % 5 == 0) {
			float d = (float)l->age * 20.0f; /* DAT_008140fc */
			float pt[3] = {aos_cos(l->angle) * d + l->origin[0], aos_sin(l->angle) * d + l->origin[1], 0};
			float r = aos_rand_real(w, 50.0f, 200.0f); /* DAT_008140ec, DAT_008147cc */
			float pa = l->age % 10 == 0 ? l->angle + K_HPI : l->angle - K_HPI;
			pt[0] = aos_cos(pa) * r + pt[0];
			pt[1] = aos_sin(pa) * r + pt[1];
			fx12(w, 400, 0, pt, l->angle, l->ownerId, 0, l->team);
			aos_se_cd(w, 0x34, 10);
		}
		if (l->age > 0x59)
			return false;
		if (l->age > 9 && l->age < 0x47) {
			float pt[3] = {aos_cos(l->angle) * 800.0f + l->origin[0], aos_sin(l->angle) * 800.0f + l->origin[1], 0};
			hb_spawn(w, l->age % 5 == 0 ? 400 : 0x19a, pt, l->angle, l->ownerId, 0, l->team);
		}
	}
	return true;
}

static void laser_on_hit(aos_world *w, aos_laser *l, int32_t seg, aos_player *t) /* FUN_0061eec0 */
{
	(void)t;
	if (l->id != 100 || seg < 0 || seg >= l->nseg)
		return;
	int32_t col = team_colour(w, l->team, 0);
	particle(w, 1000, 0, l->seg[seg].pos, l->angle, l->ownerId, 0, 1);
	spark(w, 1000, col, 10, 1, l->angle, l->seg[seg].pos, l->ownerId);
	aos_se_cd(w, 0x13, 5);
	float r = aos_rand_real(w, K_NPI, K_PI);
	fx0(w, 0x44c, col, l->seg[seg].pos, r, l->ownerId, 0, l->team);
}

static void laser_strip(aos_world *w, aos_laser *l, float cx, float cy, float L, float H, float y0,
                        float y1, uint32_t argb, int blend)
{
	if (L <= 0.0f || H <= 0.0f)
		return; /* empty dest rect: CTexture::Draw (FUN_0045c470) draws nothing visible */
	aos_dcmd *c = aos_draw(aos_tex("ir_laser"), owner_bank(w, l->ownerId), cx, cy, 0);
	c->halfW = L;
	c->halfH = H;
	c->rotZ = aos_rad2bam(l->angle); /* screen −angle == world +angle */
	c->hasSrc = 1;
	c->src[0] = 0;
	c->src[1] = y0;
	c->src[2] = 511;
	c->src[3] = y1;
	c->rgba = (argb & 0xffffffu) << 8 | argb >> 24;
	c->blend = (uint8_t)blend;
}

/* FUN_0061e500. Blits use CTexture::Draw (vtable 0x8464c0 +0x68 = FUN_0045c470):
 * (destRect screen px, z, srcRect texels, angle bam (screen, clockwise-positive; callers
 * pass −angle), D3DCOLOR ARGB, blend). Dest rect = world centre ± half-size / zoom. */
static void laser_draw(aos_world *w, aos_laser *l)
{
	int32_t a = l->age;
	float L, H, H3, len, fadeFrom, fadeLen;
	uint32_t c3;
	if (l->id == 100) {
		len = 1000.0f; L = 1000.0f; /* DAT_00814408 */
		H = (a / 2) % 2 == 0 ? 150.0f : 180.0f; /* DAT_0081414c / DAT_00815258 */
		H3 = 60.0f; fadeFrom = 50; fadeLen = 70; c3 = 0xc0ffffffu;
	} else if (l->id == 200) {
		len = 1150.0f; L = 1200.0f; /* _DAT_008203a0, DAT_008149d0 */
		H = (a / 2) % 2 == 0 ? 400.0f : 540.0f; /* DAT_008145f4 / DAT_0081412c */
		H3 = 240.0f; fadeFrom = 70; fadeLen = 90; c3 = 0xffffffffu;
	} else {
		return;
	}
	float cx = aos_cos(l->angle) * len + l->origin[0], cy = aos_sin(l->angle) * len + l->origin[1];
	if (a < 10)
		H = (float)(a * -3 + 40) * 0.1f * H;
	if (a > fadeFrom)
		H = ((float)(fadeLen - a) / 20.0f) * H;
	laser_strip(w, l, cx, cy, L, H, 100, 199, 0x40ffffffu, AOS_BLEND_ALPHA);
	laser_strip(w, l, cx, cy, L, H * 0.75f, 0, 99, 0x80ffffffu, AOS_BLEND_ADD_ALPHA);
	if (a < 10)
		H3 = (float)(a * -3 + 40) * 0.1f * H3;
	if (a > fadeFrom)
		H3 = ((float)(fadeLen - a) / 20.0f) * H3;
	float y0 = (float)(((a / 2) % 3) * 40 + 200);
	laser_strip(w, l, cx, cy, L, H3, y0, y0 + 39, c3, AOS_BLEND_ADD_ALPHA);
}

/* ------------------------------------------------------------------ */
/* enemies = Electromagnetic Fields (type 12): spawn FUN_0051f210, update FUN_0051f350,
 * draw FUN_0051fcf0, onKilled FUN_0051fab0 */

static int enemy_spawn(aos_world *w, int32_t id, int32_t owner, int32_t team, int32_t p18)
{
	if (id != 100)
		return -1;
	int32_t grp = aos_enemy_group_next(w); /* FUN_0055efc0, once per spawn (before FUN_0055ee70) */
	int i = aos_enemy_new(w, ID, owner, team, p18);
	if (i < 0)
		return -1;
	aos_enemy *e = &w->obj.enemy[i];
	e->serial = grp;
	e->f10 = 100;
	e->hp = 400;
	e->maxHp = 400;
	e->active = 0;
	e->shape = (aos_shape){0, 0, 36.0f}; /* DAT_008140f4 */
	e->scale[0] = 0;
	e->scale[1] = 3.0f; /* DAT_00813a50 */
	e->scale[2] = 0;
	e->mirrorChildren = 0;
	e->f7c = 100;
	e->f84 = 0;
	e->motionId = 0;
	e->rehitTimer = 0;
	return i;
}

static void field_tick(aos_world *w, aos_enemy *e, int32_t hbUntil, int32_t endAfter)
{
	e->angle = aos_rand_real(w, K_NPI, K_PI);
	if (e->timer2 > 0x13 && e->timer2 < hbUntil)
		hb_spawn(w, e->timer2 % 10 == 0 ? 100 : 0x6e, e->pos, 0, e->ownerId, 0, e->team);
	if (e->timer2 > endAfter) {
		e->scale[2] = 0;
		e->active = 0;
		e->f84 = 900;
	}
}

static bool enemy_update(aos_world *w, aos_enemy *e)
{
	switch (e->f7c) {
	case 100: case 0x6e:
		if (e->f84 == 0) {
			e->scale[0] = e->scale[0] + 0.1f; /* DAT_00853b10 */
			e->scale[1] = e->scale[1] - 0.2f; /* DAT_00853b14 */
			if (1.0f <= e->scale[0]) {
				e->active = 1;
				e->scale[2] = 1.0f;
				e->scale[0] = 1.0f;
				e->scale[1] = 1.0f;
				e->f84 = 100;
				e->timer2 = 0;
			}
		} else if (e->f84 == 100) {
			field_tick(w, e, 0xab, 0xdc); /* ver > 0x5a0 */
		} else {
			e->scale[0] = e->scale[0] - 0.1f;
			e->scale[1] = e->scale[1] + 0.2f;
			if (e->scale[0] <= 0.0f)
				return false;
		}
		e->pos[0] = aos_cos(e->angle2) * e->f70 + e->pos[0];
		e->pos[1] = aos_sin(e->angle2) * e->f70 + e->pos[1];
		e->f70 = e->f70 * 0.95f; /* _DAT_00814124 */
		break;
	case 0x96: case 0x97:
		if (e->f84 == 0) {
			e->scale[0] = e->scale[0] + 0.01f; /* DAT_00813ec8 */
			e->scale[1] = e->scale[1] - 0.02f; /* DAT_00813ed8 */
			if (1.0f <= e->scale[0]) {
				e->scale[0] = 1.0f;
				e->scale[1] = 1.0f;
			}
			float f = (float)(100 - e->age) / 100.0f; /* DAT_00853b90 */
			f = f * f;
			float r = 200.0f - 200.0f * f; /* DAT_008147cc */
			float k = 1.0f - f;
			float th = (e->f7c == 0x97 ? k : -k) * K_2PI + e->f70;
			e->pos[0] = aos_cos(th) * r + e->offset[0];
			e->pos[1] = aos_sin(th) * r + e->offset[1];
			if (e->timer2 == 100) {
				e->active = 1;
				e->scale[2] = 1.0f;
				e->f84 = 100;
				e->timer2 = 0;
			}
		} else if (e->f84 == 100) {
			field_tick(w, e, 0x15f, 400); /* no version branch here */
		} else {
			e->scale[0] = e->scale[0] - 0.1f;
			e->scale[1] = e->scale[1] + 0.2f;
			if (e->scale[0] <= 0.0f)
				return false;
		}
		break;
	default:
		break;
	}
	return true;
}

static bool enemy_on_killed(aos_world *w, aos_enemy *e)
{
	if (e->f10 == 100) {
		fx0(w, 600, 0, e->pos, 0, e->ownerId, 0, e->team);
		for (int k = 0; k < 5; k++) {
			float pt[3] = {e->pos[0], e->pos[1], e->pos[2]};
			pt[0] = aos_rand_real(w, -50.0f, 50.0f) + pt[0]; /* DAT_008149e4, DAT_008140ec */
			pt[1] = aos_rand_real(w, -50.0f, 50.0f) + pt[1];
			fx0(w, 0x834, 0, pt, 0, e->ownerId, 0, e->team);
		}
		for (int k = 0; k < 5; k++) {
			float pt[3] = {e->pos[0], e->pos[1], e->pos[2]};
			pt[0] = aos_rand_real(w, -30.0f, 30.0f) + pt[0];
			pt[1] = aos_rand_real(w, -30.0f, 30.0f) + pt[1];
			fx0(w, 500, 0, pt, 0, e->ownerId, k * -3 - 10, e->team);
		}
		aos_se(w, 0x1a);
	}
	return false;
}

static void enemy_draw(aos_world *w, aos_enemy *e)
{
	if (e->f10 != 100)
		return;
	int bank = owner_bank(w, e->ownerId);
	float s50 = aos_sin(((float)(e->age % 50) / 50.0f) * K_2PI);
	float pa = s50 * 0.25f + 0.75f;  /* DAT_00813f24, DAT_00813f3c */
	float pb = s50 * 0.35f + 0.65f;  /* DAT_00813f2c, DAT_0081788c */
	if (e->f84 == 100) {
		float s = aos_sin(((float)(e->age % 75) / 75.0f) * K_2PI) * 0.05f + 1.0f; /* DAT_00815254 */
		if (e->timer2 < 0x14)
			s = ((float)e->timer2 / 20.0f) * s;
		else if (e->timer2 < 10 || e->timer2 > 0x15d) {
			if (e->timer2 < 0x15e || e->timer2 > 0x171)
				s = 0.0f;
			else
				s = ((float)(0x172 - e->timer2) / 20.0f) * s;
		}
		aos_dcmd *c = spr(w, bank, "ir_hbitf%02d", 1, 256.0f, e->pos[0], e->pos[1]);
		c->blend = AOS_BLEND_ALPHA;
		c->rgba = aos_rgba(1, 1, 1, pb * 0.75f);
		c->sx = c->sy = s;
		c->rotZ = aos_rad2bam(e->angle);
		c = spr(w, bank, "ir_hbitf%02d", 0, 256.0f, e->pos[0], e->pos[1]);
		c->blend = AOS_BLEND_ADD_ALPHA;
		c->rgba = aos_rgba(1, 1, 1, pa * 0.75f);
		c->sx = c->sy = s;
		c->rotZ = aos_rad2bam(e->angle);
	}
	int f = (e->age / 3) % 6;
	float k = aos_sin(((float)(e->age % 100) / 100.0f) * K_2PI) * 0.15f + 1.0f; /* DAT_00813f20 */
	const int fr[3] = {f, f + 6, f + 12};
	for (int j = 0; j < 3; j++) {
		aos_dcmd *c = spr(w, bank, "ir_hbit%02d", fr[j], 64.0f, e->pos[0], e->pos[1]);
		c->sx = e->scale[0] * k;
		c->sy = e->scale[1] * k;
		c->rotZ = aos_rad2bam(e->angle2);
		if (j == 0) {
			c->blend = AOS_BLEND_ALPHA;
			c->rgba = aos_rgba(e->scale[2], e->scale[2], e->scale[2], 1.0f); /* +0xdc(1, z, z, z) */
		} else {
			c->blend = AOS_BLEND_ADD_ALPHA;
			c->rgba = aos_rgba(1, 1, 1, j == 1 ? pa : pb);
		}
	}
}

/* ------------------------------------------------------------------ */
/* effects (type 12): spawn FUN_004f7a00, update FUN_004f9540, draw FUN_004f7d20 */

static int fx12(aos_world *w, int32_t id, int32_t variant, const float pos[3], float angle,
                int32_t owner, int32_t age0, int32_t team)
{
	float scale, alpha, rate = 0, aux2 = 0, dir = angle;
	int32_t layer = 1;
	switch (id) {
	case 300: case 0x15e: case 400: scale = 1.0f; alpha = 1.0f; break;
	case 100: scale = 2.0f; alpha = 0.0f; break; /* DAT_00813a4c */
	case 200: scale = 0.0f; alpha = 1.0f; layer = 0; rate = 0.175f; break; /* DAT_00817888 */
	case 0xd2:
		aux2 = 1.0f;
		scale = 0.0f;
		alpha = 1.0f;
		rate = 0.175f;
		dir = aos_rand_real(w, 0.25f, 0.75f); /* DAT_00813f24, DAT_00813f3c -> +0x34 */
		break;
	default:
		return 0; /* original returns local_54 = 0 */
	}
	int i = aos_effect_new(w, ID, id, variant, pos, angle, owner, age0, team);
	if (i < 0)
		return -1;
	aos_effect *e = &w->obj.effect[i];
	e->drawLayer = layer;
	e->scale = scale;
	e->alpha = alpha;
	e->moveDir = dir;
	e->rotation = angle;
	e->aux = rate;
	e->aux2 = aux2;
	return i;
}

static bool fx12_update(aos_world *w, aos_effect *e)
{
	switch (e->id) {
	case 300:
		if (e->age > 5)
			e->alpha = e->alpha - 0.1f;
		return !(e->alpha <= 0.0f);
	case 100:
		if (e->age < 100) {
			e->alpha = e->alpha + 0.1f;
			e->scale = e->scale - 0.1f;
			if (e->scale <= 1.0f) {
				e->alpha = 1.0f;
				e->scale = 1.0f;
				e->age = 100;
				e->drawLayer = 0;
			}
		} else if (e->age > 0x77) {
			e->alpha = e->alpha - 0.05f;
			e->scale = e->scale - 0.05f;
			if (e->scale <= 0.0f)
				return false;
		}
		return true;
	case 200:
		if (e->age < 0x32) {
			e->scale = e->scale + e->aux;
			e->aux = e->aux * 0.8f; /* DAT_008145e8 */
		} else {
			e->alpha = e->alpha - 0.01f;
			if (e->alpha <= 0.0f)
				return false;
		}
		if (e->age > 1 && e->age < 0x29) /* team 3: hits both players (0x4f971b) */
			hb_spawn(w, (e->age - 2) % 3 == 0 ? 200 : 0xd2, e->pos, e->rotation, e->ownerId, 0, 3);
		if (e->age < 0xb) {
			for (int k = 0; k < 2; k++) {
				float a = aos_rand_real(w, K_NPI, K_PI);
				float d = aos_rand_real(w, 0.0f, 100.0f);
				float pt[3] = {aos_cos(a) * d + e->pos[0], aos_sin(a) * d + e->pos[1], 0};
				particle(w, 1000, e->variant, pt, a, e->ownerId, 0, 1);
			}
		}
		if (e->age < 0x29 && e->age % 5 == 0) {
			float pt[3] = {e->pos[0], e->pos[1], e->pos[2]};
			float a = aos_rand_real(w, K_NPI, K_PI);
			fx12(w, 0xd2, 0, pt, a, e->ownerId, 0, e->team);
			return true;
		}
		if (e->age < 0x32) {
			float pt[3] = {e->pos[0], e->pos[1], e->pos[2]};
			float a = aos_rand_real(w, K_NPI, K_PI);
			fx0(w, e->age % 2 == 0 ? 200 : 0xd2, 0, pt, a, e->ownerId, 0, e->team);
			return true;
		}
		return true;
	case 0xd2:
		e->aux2 = 0; /* +0x40: decremented then forced to 0 (literal) */
		if (e->age < 0x14) {
			e->scale = e->scale + e->aux;
			e->aux = e->aux * 0.8f;
			if (e->aux <= 0.02f && 0.02f != e->aux)
				e->aux = 0.02f; /* DAT_00813ed8 */
		} else {
			e->alpha = e->alpha - 0.05f;
			if (e->alpha <= 0.0f)
				return false;
		}
		return true;
	case 0x15e:
		if (e->age > 2)
			e->alpha = e->alpha - 0.1f;
		e->scale = e->scale + 0.25f;
		return !(e->alpha <= 0.0f);
	case 400:
		if (e->scale <= 1.0f && 1.0f != e->scale)
			e->scale = e->scale + 0.2f;
		if (e->age > 0x14)
			e->alpha = e->alpha - 0.05f;
		return !(e->alpha <= 0.0f);
	default:
		return true;
	}
}

static int beamemit_static; /* DAT_00895840: draw-only static left over between draws (literal) */

static void blit(int tex, int bank, float x, float y, float hw, float hh, const float src[4],
                 float angle, uint32_t argb, int blend)
{
	aos_dcmd *c = aos_draw(tex, bank, x, y, 0);
	c->halfW = hw;
	c->halfH = hh;
	c->rotZ = aos_rad2bam(angle);
	c->hasSrc = 1;
	memcpy(c->src, src, sizeof c->src);
	c->rgba = (argb & 0xffffffu) << 8 | argb >> 24;
	c->blend = (uint8_t)blend;
}

static uint32_t a8(float v) { int i = (int)v; return (uint32_t)(i & 0xff); }

static void fx12_draw(aos_world *w, aos_effect *e) /* FUN_004f7d20 (cosmetic, approximate where noted) */
{
	int bank = owner_bank(w, e->ownerId);
	int32_t v = e->variant;
	float sx = (float)((v / 8) * 0xf0), sy = (float)((v % 8) * 64);
	switch (e->id) {
	case 300: case 0x15e: {
		bool b300 = e->id == 300;
		float s1[4] = {sx, sy, sx + 0x77, sy + 0x3f}, s2[4] = {sx + 0x78, sy, sx + 0xef, sy + 0x3f};
		blit(aos_tex("beamemitb"), AOS_BANK_COMMON, e->pos[0], e->pos[1], b300 ? 72.0f : 60.0f,
		     b300 ? 40.0f : 36.0f * e->scale, s1, e->rotation, a8(32.0f * e->alpha) << 24 | 0xffffffu, AOS_BLEND_ALPHA);
		float al = b300 ? 255.0f * e->alpha : 255.0f * e->alpha * e->alpha;
		float hw = b300 ? 48.0f : 40.0f, hh = b300 ? 10.0f : 8.0f * e->scale;
		if (e->age < 2) {
			int32_t q = beamemit_static;
			float s3[4] = {(float)((q / 8) * 256), (float)((q % 8) * 64), (float)((q / 8) * 256 + 0x7f), (float)((q % 8) * 64 + 0x3f)};
			blit(aos_tex("beamemit"), AOS_BANK_COMMON, e->pos[0], e->pos[1], b300 ? 54.0f : 48.0f,
			     b300 ? 18.0f : 16.0f * e->scale, s3, e->rotation, 0xffffffffu, AOS_BLEND_ADD_ALPHA);
		} else if (e->age < 6) {
			blit(aos_tex("beamemitb"), AOS_BANK_COMMON, e->pos[0], e->pos[1], hw, hh, s2, e->rotation,
			     a8(al) << 24 | 0xffffffu, AOS_BLEND_ADD_ALPHA);
		} else {
			blit(aos_tex("beamemitb"), AOS_BANK_COMMON, e->pos[0], e->pos[1], hw, hh, s2, e->rotation,
			     a8(al) << 24 | 0x80ffu, AOS_BLEND_ADD_ALPHA);
		}
		break;
	}
	case 100: { /* atlas DAT_0087fccc[variant/8] (explosion0/1), cell from variant%8 (+0x08 = colour) */
		int idx = v / 8;
		if (idx < 0 || idx > 1)
			break; /* colours 0..11 only reach explosion0/1 */
		int c = (v % 8) % 4, r = (v % 8) / 4;
		float s1[4] = {(float)(r * 256), (float)(c * 128), (float)(r * 256 + 0x7f), (float)(c * 128 + 0x7f)};
		float s2[4] = {(float)(r * 256 + 0x80), (float)(c * 128), (float)(r * 256 + 0xff), (float)(c * 128 + 0x7f)};
		int tex = aos_texf("explosion%d", idx);
		float h = 180.0f * e->scale;
		blit(tex, AOS_BANK_COMMON, e->pos[0], e->pos[1], h, h, s1, -e->rotation, a8(128.0f * e->alpha) << 24 | 0xffffffu, AOS_BLEND_ADD_ALPHA);
		h = 200.0f * e->scale * (e->age % 2 == 0 ? 0.85f : 1.0f);
		blit(tex, AOS_BANK_COMMON, e->pos[0], e->pos[1], h, h, s2, -e->rotation, a8(255.0f * e->alpha) << 24 | 0xffffffu, AOS_BLEND_ADD_ALPHA);
		break;
	}
	case 200: case 400: {
		const float full[4] = {0, 0, 255, 255};
		float h0 = e->id == 200 ? 720.0f * e->scale : (e->age % 2 == 0 ? 320.0f : 280.0f) * e->scale;
		blit(aos_tex("ir_exp00"), bank, e->pos[0], e->pos[1], h0, h0, full, -e->rotation,
		     a8(128.0f * e->alpha) << 24 | 0xffffffu, AOS_BLEND_ADD_ALPHA);
		float h1 = e->id == 200 ? 960.0f * e->scale * (e->age % 2 == 0 ? 0.85f : 1.0f) : h0;
		uint32_t al = a8((e->age % 2 == 0 ? 225.0f : 192.0f) * e->alpha);
		const aos_player *tp = player_of_team(w, e->team);
		uint32_t argb = (tp ? tp->color : 0) == 0 /* DAT_00885a00 + team*0x10 = team player's palette */
		                    ? al << 24 | (al & 0xff) << 16 | 0xff00u | ((0xffu - al / 2) & 0xff)
		                    : al << 24 | (al & 0xff) << 16 | ((0xffu - al / 2) & 0xff) << 8 | 0xffu;
		blit(aos_tex("ir_exp01"), bank, e->pos[0], e->pos[1], h1, h1, full, -e->rotation, argb, AOS_BLEND_ADD_ALPHA);
		break;
	}
	case 0xd2: {
		const float full[4] = {0, 0, 255, 255};
		float hw = 720.0f * e->scale, hh = hw * e->moveDir;
		uint32_t g = a8(255.0f * e->aux2);
		beamemit_static = (int)g;
		blit(aos_tex("ir_exp02"), bank, e->pos[0], e->pos[1], hw, hh, full, e->rotation,
		     a8(128.0f * e->alpha) << 24 | g << 16 | g << 8 | g, AOS_BLEND_ALPHA);
		break;
	}
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* whip = field trail (type 12): spawn FUN_007c50e0, update FUN_007c5a80, draw FUN_007c56f0.
 * Port layout: node[k] = {B.xyz (+0x14), A.xyz (+0x20), used (+0x2c byte)}. */

static int whip_spawn(aos_world *w, int32_t id, int32_t ownerId, int32_t p3)
{
	if (id != 6)
		return -1;
	int i = aos_whip_new(w, ID, id, ownerId, p3); /* +0x08 = owner's palette (p+0x18), used for the texture */
	if (i < 0)
		return -1;
	w->obj.whip[i].f10 = p3; /* +0x10 enemy id */
	return i;
}

static bool whip_update(aos_world *w, aos_whip *h)
{
	if (h->id != 6)
		return true;
	aos_enemy *e = aos_enemy_by_id(w, h->f10);
	if (!e)
		return false;
	float d = aos_atan2(e->pos[1] - e->prevPos[1], e->pos[0] - e->prevPos[0]);
	float c = aos_cos(d + K_HPI), s = aos_sin(d + K_HPI);
	float ax = -c * 10.0f + e->pos[0], ay = -s * 10.0f + e->pos[1];
	float bx = c * 10.0f + e->pos[0], by = s * 10.0f + e->pos[1];
	int k;
	for (k = AOS_WHIP_NODES - 1; k >= 0; k--)
		if (h->node[k][6] == 0.0f)
			break;
	if (k < 0) {
		memmove(&h->node[1], &h->node[0], sizeof h->node[0] * (AOS_WHIP_NODES - 1));
		k = 0;
	}
	h->node[k][0] = bx; h->node[k][1] = by; h->node[k][2] = 0;
	h->node[k][3] = ax; h->node[k][4] = ay; h->node[k][5] = 0;
	h->node[k][6] = 1.0f;
	return true;
}

static void whip_draw(aos_world *w, aos_whip *h) /* quad strip approximated by rotated quads */
{
	aos_enemy *e = aos_enemy_by_id(w, h->f10);
	int bank = e ? owner_bank(w, e->ownerId) : AOS_BANK_P0;
	for (int k = 0; k < AOS_WHIP_NODES - 1; k++) {
		const float *n0 = h->node[k], *n1 = h->node[k + 1];
		if (n0[6] == 0.0f || n1[6] == 0.0f)
			continue;
		/* Original quad: A_k (tex 0, k·12.8), A_k+1 (0, (k+1)·12.8), B_k+1 (256, …), B_k (256, …):
		 * texture x runs across the ribbon (A→B, width 20), texture y along it. Exact when
		 * A_k−B_k ∥ A_k+1−B_k+1 (true unless the field turned between frames); a zero-length
		 * segment (stationary field) has zero area and draws nothing, as in the original. */
		float c0x = (n0[0] + n0[3]) * 0.5f, c0y = (n0[1] + n0[4]) * 0.5f;
		float c1x = (n1[0] + n1[3]) * 0.5f, c1y = (n1[1] + n1[4]) * 0.5f;
		float dx = c1x - c0x, dy = c1y - c0y;
		float len = aos_sqrt(dx * dx + dy * dy);
		if (len < 0.001f)
			continue;
		float ax = n0[0] - n0[3], ay = n0[1] - n0[4]; /* B - A: across */
		float src[4] = {0, (float)k * 12.8f, 256.0f, (float)(k + 1) * 12.8f}; /* _DAT_00828324, DAT_00814160 */
		blit(aos_tex("ir_whip0"), bank, (c0x + c1x) * 0.5f, (c0y + c1y) * 0.5f, 10.0f, len * 0.5f, src,
		     aos_atan2(ay, ax), 0x80ffffffu, AOS_BLEND_ALPHA);
	}
}

/* ------------------------------------------------------------------ */

void aos_register_iru(void)
{
	aos_tab.inputA[ID] = input_a;
	aos_tab.inputB[ID] = input_b;
	/* CPU AI FUN_006d4b60 (level 0) / FUN_006d43b0 (1, 2): not ported */

	aos_tab.T2[0x16e] = st_neutral;
	aos_tab.T2[0x16f] = st_stub;
	aos_tab.T2[0x170] = st_stub;
	aos_tab.T2[0x171] = st_stub;
	aos_tab.T2[0x172] = st_stub;
	aos_tab.T2[0x178] = st_rifle;
	aos_tab.T2[0x179] = st_rifle_press;
	aos_tab.T2[0x17a] = st_rocket_tap;
	aos_tab.T2[0x17b] = st_rocket_press;
	aos_tab.T2[0x17c] = st_field;
	aos_tab.T2[0x17d] = st_field_dash;
	aos_tab.T2[0x17e] = st_wide_field;
	aos_tab.T2[0x17f] = st_wide_field_dash;
	aos_tab.T2[0x180] = st_pod;
	aos_tab.T2[0x181] = st_curtain;
	aos_tab.T2[0x182] = st_linear_ray;
	aos_tab.T2[0x183] = st_genocide;
	aos_tab.T2[0x184] = st_destruction_ray;

	aos_tab.T1[0x16e] = dr_neutral;
	aos_tab.T1[0x16f] = dr_dash;
	aos_tab.T1[0x170] = dr_ko;
	aos_tab.T1[0x171] = dr_hit;
	aos_tab.T1[0x172] = dr_sld;
	aos_tab.T1[0x178] = dr_rifle;
	aos_tab.T1[0x179] = dr_rifle_press;
	aos_tab.T1[0x17a] = dr_hand;
	aos_tab.T1[0x17b] = dr_handa;
	aos_tab.T1[0x17c] = dr_sld;
	aos_tab.T1[0x17d] = dr_sld;
	aos_tab.T1[0x17e] = dr_wide_field;
	aos_tab.T1[0x17f] = dr_sld;
	aos_tab.T1[0x180] = dr_handa;
	aos_tab.T1[0x181] = dr_handa;
	aos_tab.T1[0x182] = dr_linear_ray;
	aos_tab.T1[0x183] = dr_genocide;
	aos_tab.T1[0x184] = dr_destruction_ray;

	aos_otab.beamSpawn[ID] = beam_spawn;
	aos_otab.beamUpdate[ID] = beam_update;
	aos_otab.beamDraw[ID] = beam_draw;
	aos_otab.beamOnHit[ID] = beam_on_hit;
	aos_otab.missileSpawn[ID] = missile_spawn;
	aos_otab.missileUpdate[ID] = missile_update;
	aos_otab.missileDraw[ID] = missile_draw;
	aos_otab.missileOnHit[ID] = missile_on_hit;
	aos_otab.missileOnKilled[ID] = missile_on_killed;
	aos_otab.missileOnAbsorb[ID] = NULL; /* 0x881b78 = 0 */
	aos_otab.hitboxSpawn[ID] = hb_spawn;
	aos_otab.hitboxOnHit[ID] = hb_on_hit;
	aos_otab.laserSpawn[ID] = laser_spawn;
	aos_otab.laserUpdate[ID] = laser_update;
	aos_otab.laserDraw[ID] = laser_draw;
	aos_otab.laserOnHit[ID] = laser_on_hit;
	aos_otab.enemySpawn[ID] = enemy_spawn;
	aos_otab.enemyUpdate[ID] = enemy_update;
	aos_otab.enemyDraw[ID] = enemy_draw;
	aos_otab.enemyOnKilled[ID] = enemy_on_killed;
	aos_otab.effectSpawn[ID] = fx12;
	aos_otab.effectUpdate[ID] = fx12_update;
	aos_otab.effectDraw[ID] = fx12_draw;
	aos_otab.whipSpawn[ID] = whip_spawn;
	aos_otab.whipUpdate[ID] = whip_update;
	aos_otab.whipDraw[ID] = whip_draw;
}
