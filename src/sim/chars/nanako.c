/* Nanako (engine id 13, states 0x18d..0x1a3) — port of docs/re/nanako.md.
 * Every function names its original address. Version 0x5a2 branches only.
 *
 * Nanako's player states only broadcast commands (FUN_00704e30) to her seven
 * Bits — Enemy-family objects type 13, spawn ids 100..106, created at t=50 of
 * the round-start state 0x197. The Bits (FUN_00528e20) read her S button,
 * pay her gauges per shot and spawn every projectile.
 * Object families (type 13): beam 4af380/4b0220/4af560/4aff60, missile
 * 670310/671310/6704b0/6710b0/6711e0, hitbox 5e6730/5e6a90, laser
 * 620e90/622af0/620ff0/622ad0, enemy 528ca0/528e20/52bbf0/52b980,
 * effect 4fef30/5004c0/4ff3e0, whip 7c6a80/7c6f00/7c6b70. */
#include <string.h>

#include "../draw.h"
#include "../fx.h"
#include "../objects.h"
#include "../sim.h"

#define ID AOS_CH_NANAKO
#define HALF_PI 1.5707964f /* DAT_00814104 */

void aos_se_cd(aos_world *w, int id, int cd);                              /* FUN_007bcaf0 */

/* ------------------------------------------------------------------ */
/* helpers                                                             */

static aos_player *player_of_team(aos_world *w, int32_t team) /* FUN_00780450 */
{
	if (team == 1)
		return &w->p[0];
	if (team == 2)
		return &w->p[1];
	return NULL;
}

static aos_player *opponent_of_team(aos_world *w, int32_t team) /* FUN_00780480 */
{
	if (team == 1)
		return &w->p[1];
	if (team == 2)
		return &w->p[0];
	return NULL;
}

static int bank_of_team(int32_t team) { return team == 2 ? AOS_BANK_P1 : AOS_BANK_P0; }

/* DAT_008859f4 + team*0x10: per-team colour slot 0. */
static int32_t colour_of_team(aos_world *w, int32_t team) { return aos_team_colour(w, team, 0); }

/* FUN_0068b8a0(kind, col, pos, ang, owner, 0, n) */
static void particles(aos_world *w, int kind, int32_t col, const float pos[3], float ang, int32_t owner, int n)
{
	aos_spawn_particle(w, kind, col, pos, ang, owner, 0, n);
}

/* FUN_007bd5e0(1000, col, maxLen, n, ang, pos, owner) */
static void sparks(aos_world *w, int32_t col, int maxLen, int n, float ang, const float pos[3], int32_t owner)
{
	aos_spawn_spark(w, 1000, col, maxLen, n, ang, pos, owner);
}

static int eff0(aos_world *w, int32_t id, int32_t var, const float pos[3], float ang, int32_t owner, int32_t age0, int32_t team)
{
	return aos_spawn_effect(w, 0, id, var, pos, ang, owner, age0, team);
}

static int effN(aos_world *w, int32_t id, int32_t var, const float pos[3], float ang, int32_t owner, int32_t team)
{
	return aos_spawn_effect(w, ID, id, var, pos, ang, owner, 0, team);
}

static void aim(aos_world *w, aos_player *p) { p->faceAngle = aos_aim(w, p); }

/* "if (t < N || D != 3) {...} else dash cancel" */
static bool cancel_at(aos_world *w, aos_player *p, int32_t n)
{
	if (p->timer >= n && p->D == 3) {
		aos_dash(w, p, 1);
		return true;
	}
	return false;
}

/* hyper states 0x1a1/0x1a2/0x1a3: face the opponent's side */
static void face_side(aos_world *w, aos_player *p)
{
	aos_player *o = aos_opponent(w, p);
	p->faceAngle = (o->x <= p->x) ? AOS_PI : 0.0f;
	p->dashAngle = p->faceAngle;
}

/* normal-hyper start: FUN_006b1b40(p,10), SE 6, dark(100,.75),
 * speedline(100,pos,slAngle,slSpread), blur(20,.5,2). No effect 0xfa. */
static void hyper_start(aos_world *w, aos_player *p, float slAngle, float slSpread)
{
	aos_barrier(w, p, 10);
	aos_hyper_flourish_ex(w, p, 6, 100, 0.75f, 100, slAngle, slSpread, 0x14, 0.5f, 2, 0);
}

/* Bit-local float view of enemy +0x74 (decel / radius velocity). */
static float e74(const aos_enemy *e)
{
	float f;
	memcpy(&f, &e->f74, sizeof f);
	return f;
}
static void set_e74(aos_enemy *e, float f) { memcpy(&e->f74, &f, sizeof f); }

/* ------------------------------------------------------------------ */
/* command broadcast FUN_00704e30                                      */

static void cmd_aim_tackle(aos_world *w, aos_enemy *e, int32_t delay)
{
	aos_player *o = opponent_of_team(w, e->team);
	e->angle = aos_atan2(o->y - e->pos[1], o->x - e->pos[0]);
	e->angle2 = e->angle;
	e->f70 = 2.0f; /* DAT_00813a4c */
	e->timer2 = delay;
	e->f84 = 500;
}

static int bit_cmd(aos_world *w, int32_t cmd, int32_t owner, int32_t team) /* FUN_00704e30 */
{
	int n = 0;
	for (int i = 0; i < w->obj.nenemy; i++) {
		aos_enemy *e = &w->obj.enemy[i];
		if (e->ownerId != owner || e->team != team || e->f10 < 100 || e->f10 >= 0x6b)
			continue;
		int32_t k = e->f10 - 100;
		switch (cmd) {
		case 0x96:
		case 0xa0:
			e->f84 = cmd;
			e->timer2 = k * -5 - 10;
			break;
		case 300:
			e->f84 = cmd;
			e->timer2 = k * -10 - 20;
			break;
		case 0x136:
			e->f84 = 0x136;
			e->timer2 = -10;
			break;
		case 0x15e:
			e->f84 = 0x15e;
			e->timer2 = k * -3;
			e->f70 = 20.0f;   /* DAT_008140fc */
			set_e74(e, 0.2f); /* DAT_00853b14 */
			break;
		case 400:
			e->f84 = 400;
			e->timer2 = k * -2;
			break;
		case 900:
			e->f84 = 900;
			e->timer2 = k * 10;
			break;
		case 500:
			if (e->f10 == 0x66 && e->f84 != 500 && e->f84 != 0x1f5) {
				cmd_aim_tackle(w, e, -5);
				n++;
			}
			if (e->f10 == 0x68 && e->f84 != 500 && e->f84 != 0x1f5) {
				cmd_aim_tackle(w, e, -10);
				n++;
			}
			break;
		case 0x226: /* v >= 0x58d: delay 0 */
			if (e->f10 == 0x65 && e->f84 != 500 && e->f84 != 0x1f5) {
				cmd_aim_tackle(w, e, 0);
				n++;
			}
			if (e->f10 == 0x69 && e->f84 != 500 && e->f84 != 0x1f5) {
				cmd_aim_tackle(w, e, 0);
				n++;
			}
			break;
		default: /* 100, 200, 0xfa, 600, ... */
			e->f84 = cmd;
			e->timer2 = 0;
			break;
		}
	}
	return n;
}

static int cmd(aos_world *w, aos_player *p, int32_t c) { return bit_cmd(w, c, p->objId, p->side); }

/* ------------------------------------------------------------------ */
/* input dispatch                                                      */

static bool enter(aos_world *w, aos_player *p, int32_t s)
{
	aos_set_state(w, p, s);
	return true;
}

static bool finish(aos_world *w, aos_player *p)
{
	return aos_tab.T2[p->state] ? aos_tab.T2[p->state](w, p) : true;
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
		if (p->S > 1 && p->B == 3) return enter(w, p, 0x19d);
		if (p->B == 3) return enter(w, p, 0x19c);
		if (p->D > 1 && p->A == 3) return enter(w, p, 0x19a);
		if (p->A == 3) return enter(w, p, 0x199);
	}
	return false;
}

static bool input_a(aos_world *w, aos_player *p) /* FUN_00707380 */
{
	if (aos_may_hyper(w, p)) {
		if (p->S > 1 && p->B > 1 && p->H == 3 && aos_spend_hyper(p, 3.0f)) return enter(w, p, 0x1a3);
		if (p->S > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f)) return enter(w, p, 0x1a1);
		if (p->B > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f)) return enter(w, p, 0x1a2);
		if (p->D > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f)) return enter(w, p, 0x1a0);
		if (p->H == 3 && aos_spend_hyper(p, 1.0f)) return enter(w, p, 0x19f);
	}
	if (normal_moves(w, p))
		return true;
	return finish(w, p);
}

static bool input_b(aos_world *w, aos_player *p) /* FUN_007077f0 */
{
	if (aos_may_hyper(w, p)) {
		if (p->H > 1 && p->Shield == 3 && aos_spend_hyper(p, 3.0f)) return enter(w, p, 0x1a3);
		if (p->H > 1 && p->S == 3 && aos_spend_hyper(p, 1.0f)) return enter(w, p, 0x1a1);
		if (p->H > 1 && p->B == 3 && aos_spend_hyper(p, 1.0f)) return enter(w, p, 0x1a2);
		if (p->H > 1 && p->D == 3 && aos_spend_hyper(p, 1.0f)) return enter(w, p, 0x1a0);
		if (p->H > 1 && p->A == 3 && aos_spend_hyper(p, 1.0f)) return enter(w, p, 0x19f);
	}
	if (normal_moves(w, p))
		return true;
	return finish(w, p);
}

/* ------------------------------------------------------------------ */
/* player states (T2)                                                  */

static bool st_neutral(aos_world *w, aos_player *p) /* 0x18d FUN_00706250 */
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

static bool st_stub(aos_world *w, aos_player *p) /* 0x18e FUN_00706340, 0x190 FUN_00706350, 0x191 FUN_00706360 */
{
	(void)w, (void)p;
	return true;
}

static bool st_ko_hook(aos_world *w, aos_player *p) /* 0x18f FUN_00706370 (called from KO) */
{
	if (p->timer == 0)
		cmd(w, p, 900);
	return true;
}

static bool st_intro(aos_world *w, aos_player *p) /* 0x197 FUN_007063a0 */
{
	if (p->timer == 0x32) {
		for (int k = 0; k < 7; k++) {
			int i = aos_spawn_enemy(w, ID, 100 + k, p->objId, p->side, p->color);
			if (i < 0)
				continue;
			aos_enemy *e = &w->obj.enemy[i];
			e->pos[0] = p->x;
			e->pos[1] = p->y;
			e->pos[2] = p->z;
			e->angle = p->faceAngle;
			e->f84 = 0;
			e->timer2 = (k - 3 < 0 ? 3 - k : k - 3) * -10; /* FUN_007e1bee = abs */
			set_e74(e, 10.0f);                             /* DAT_00813f4c */
			if (aos_otab.whipSpawn[ID])
				aos_otab.whipSpawn[ID](w, 5, p->color, e->id);
		}
	}
	if (p->timer > 100)
		p->state = 0;
	p->hurtShape[0] = p->hurtShape[1] = p->hurtShape[2] = p->hurtShape[3] = 0;
	return true;
}

static bool st_tone(aos_world *w, aos_player *p) /* 0x198 FUN_00706540 */
{
	if (p->timer == 0)
		aim(w, p);
	if (p->timer == 0xb)
		cmd(w, p, 0x96);
	if (!cancel_at(w, p, 0x14) && p->timer > 0x28)
		p->state = 0;
	return true;
}

static bool st_needle(aos_world *w, aos_player *p) /* 0x199 FUN_00706600 */
{
	if (p->timer == 0)
		aim(w, p);
	if (p->timer < 0xf && p->A < 2) {
		p->timer = 10;
		p->state = 0x198;
		aos_tab.T2[0x198](w, p);
		return true;
	}
	if (p->timer == 0xf)
		cmd(w, p, 100);
	if (!cancel_at(w, p, 0x19) && p->timer > 0x28)
		p->state = 0;
	return true;
}

static bool st_fruit(aos_world *w, aos_player *p) /* 0x19a FUN_00706710 */
{
	if (p->timer == 0)
		aim(w, p);
	if (p->timer == 10)
		cmd(w, p, 0xa0);
	if (!cancel_at(w, p, 0x1e) && p->timer > 0x32)
		p->state = 0;
	return true;
}

static bool st_leaf(aos_world *w, aos_player *p) /* 0x19b FUN_007067d0 */
{
	if (p->timer == 0)
		aim(w, p);
	if (p->timer == 0xf)
		cmd(w, p, 200);
	if (!cancel_at(w, p, 0x14) && p->timer > 0x28)
		p->state = 0;
	return true;
}

static bool st_bug(aos_world *w, aos_player *p) /* 0x19c FUN_00706890 */
{
	if (p->timer == 0)
		aim(w, p);
	if (p->timer < 0xc && p->B < 2) {
		p->state = 0x19b; /* timer kept */
		aos_tab.T2[0x19b](w, p);
		return true;
	}
	if (p->timer == 0xc)
		cmd(w, p, 0xfa);
	if (!cancel_at(w, p, 0x23) && p->timer > 0x32) /* v > 0x5a0: 35 / 50 */
		p->state = 0;
	return true;
}

static bool st_shell(aos_world *w, aos_player *p) /* 0x19d FUN_00706f40 */
{
	const int32_t l8 = 0x17, lc = 0x14, l10 = 0x1e; /* v >= 0x5a1 */
	if (p->timer == 0)
		aim(w, p);
	if (p->timer == 5 && cmd(w, p, 500) > 0) {
		aos_use_gauge(p, 0x19d);
		aos_se(w, 0x2a);
	}
	if (p->timer == l8 && p->B > 1) {
		aos_set_state(w, p, 0x19e);
		return true;
	}
	if (!cancel_at(w, p, lc) && p->timer > l10)
		p->state = 0;
	return true;
}

static bool st_shell2(aos_world *w, aos_player *p) /* 0x19e FUN_007070e0 */
{
	if (p->timer == 0)
		aim(w, p);
	if (p->timer == 3 && aos_use_gauge(p, 0x19e))
		cmd(w, p, 0x226);
	if (!cancel_at(w, p, 0xc) && p->timer > 0x14)
		p->state = 0;
	return true;
}

static bool st_breath1(aos_world *w, aos_player *p) /* 0x19f FUN_007069d0 */
{
	if (p->timer == 0) {
		aim(w, p);
		hyper_start(w, p, 0.0f, 0.0f);
	}
	if (p->timer == 10)
		cmd(w, p, 300);
	if (!cancel_at(w, p, 0x3c) && p->timer > 0x46)
		p->state = 0;
	return true;
}

static bool st_breath_all(aos_world *w, aos_player *p) /* 0x1a0 FUN_00706b00 */
{
	if (p->timer == 0) {
		aim(w, p);
		hyper_start(w, p, p->faceAngle, 0.7853982f); /* DAT_0081fa90 */
	}
	if (p->timer == 10)
		cmd(w, p, 0x136);
	if (!cancel_at(w, p, 0x32) && p->timer > 0x46)
		p->state = 0;
	return true;
}

static bool st_sting(aos_world *w, aos_player *p) /* 0x1a1 FUN_00706c40 */
{
	if (p->timer == 0) {
		face_side(w, p);
		hyper_start(w, p, 0.0f, 0.0f);
	}
	if (p->timer == 1)
		cmd(w, p, 0x15e);
	if (!cancel_at(w, p, 0x46) && p->timer > 0x50)
		p->state = 0;
	return true;
}

static bool st_crown(aos_world *w, aos_player *p) /* 0x1a2 FUN_00706dc0 */
{
	if (p->timer == 0) {
		face_side(w, p);
		hyper_start(w, p, 0.0f, 0.0f);
	}
	if (p->timer == 10)
		cmd(w, p, 400);
	if (!cancel_at(w, p, 0x14) && p->timer > 0x3c)
		p->state = 0;
	return true;
}

static bool st_storm(aos_world *w, aos_player *p) /* 0x1a3 FUN_007071c0 */
{
	if (p->timer == 0) {
		face_side(w, p);
		aos_barrier(w, p, 0x14);
		aos_hyper_flourish_ex(w, p, 7, 100, 0.85f, 100, 0.0f, 0.0f, 100, 0.75f, 2, 1);
	}
	if (p->timer == 1)
		cmd(w, p, 600);
	if (!cancel_at(w, p, 0x50) && p->timer > 100)
		p->state = 0;
	return true;
}

/* ------------------------------------------------------------------ */
/* player draw (T1)                                                    */

static aos_dcmd *spr(aos_world *w, aos_player *p, const char *fmt, int frame)
{
	aos_dcmd *c = aos_draw(aos_texf(fmt, frame), aos_pidx(w, p), p->x, p->y, 0);
	c->halfW = c->halfH = 48.0f; /* 96 px quad (DAT_00814118) */
	return c;
}

/* AIM transform: RotZ(aim) [, RotX(180°) if |aim| >= π/2], RotY(180°). */
static aos_dcmd *spr_aim(aos_world *w, aos_player *p, const char *fmt, int frame)
{
	aos_dcmd *c = spr(w, p, fmt, frame);
	c->rotZ = aos_rad2bam(p->faceAngle);
	c->flipX = 1;
	if (HALF_PI <= aos_fabs(p->faceAngle))
		c->sy = -1.0f; /* RotX(180°) approximated as y-scale cos(π) */
	return c;
}

static int pp(int32_t t, int n)
{
	int f = (t / n) & 3;
	return f == 3 ? 1 : f;
}

static int L5(int32_t t)
{
	int n = t / 5;
	if (n > 1)
		n = ((n - 2) & 3) + 2;
	if (n == 5)
		n = 3;
	return n;
}

static int C5(int32_t t)
{
	int n = t / 5;
	return n > 4 ? 4 : n;
}

static void dr_neutral(aos_world *w, aos_player *p) /* 0x18d FUN_00708a90 */
{
	const char *s = "nk_std%02d";
	if (p->up > 1) s = "nk_fwd%02d";
	if (p->down > 1) s = "nk_bck%02d";
	if (!p->facingRight && p->left > 1) s = "nk_fwd%02d";
	if (p->facingRight == 1 && p->left > 1) s = "nk_bck%02d";
	if (!p->facingRight && p->right > 1) s = "nk_bck%02d";
	if (p->facingRight == 1 && p->right > 1) s = "nk_fwd%02d";
	aos_dcmd *c = spr(w, p, s, pp(p->timer, 5));
	c->flipX = p->facingRight != 0;
	int32_t rot[3] = {0, p->facingRight != 0 ? aos_rad2bam(AOS_PI) : 0, 0};
	aos_afterimage(w, c, 1, p, rot); /* FUN_004d7e00 */
}

static void dr_dash(aos_world *w, aos_player *p) /* 0x18e FUN_00708d50 */
{
	int f = pp(p->timer, 3);
	float a = aos_fabs(p->faceAngle);
	if ((p->facingRight == 1 && a > HALF_PI) || (p->facingRight == 0 && a < HALF_PI))
		f += 3;
	aos_dcmd *c = spr_aim(w, p, "nk_dsh%02d", f);
	int32_t rot[3] = {HALF_PI <= aos_fabs(p->faceAngle) ? aos_deg2bam(0xb4) : 0, aos_deg2bam(0xb4),
	                  aos_rad2bam(p->faceAngle)};
	aos_afterimage(w, c, 1, p, rot); /* FUN_004d7e00 */
}

static void dr_ko(aos_world *w, aos_player *p) /* 0x18f FUN_00709700 */
{
	int f = pp(p->timer, 3);
	if (w->frame % 2 == 0)
		f += 3;
	if (p->timer < 100)
		spr_aim(w, p, "nk_los%02d", f);
}

static void dr_hit(aos_world *w, aos_player *p) /* 0x190 FUN_00709150 */
{
	spr_aim(w, p, "nk_los%02d", pp(p->timer, 3));
}

static void dr_sld_L5(aos_world *w, aos_player *p) /* 0x191 709420, 0x199 709fa0, 0x19a 70a280, 0x19c 70a820, 0x19d 70b660, 0x19e 70b940, 0x1a0 70adc0 */
{
	spr_aim(w, p, "nk_sld%02d", L5(p->timer));
}

static void dr_atkb_L5(aos_world *w, aos_player *p) /* 0x197 709a00, 0x1a1 70b0a0, 0x1a2 70b380, 0x1a3 70bc20 */
{
	spr_aim(w, p, "nk_atkb%02d", L5(p->timer));
}

static void dr_atka_C5(aos_world *w, aos_player *p) /* 0x198 709ce0, 0x19b 70a560, 0x19f 70ab00 */
{
	spr_aim(w, p, "nk_atka%02d", C5(p->timer));
}

/* ------------------------------------------------------------------ */
/* Bits: enemy family (type 13)                                        */

static int bit_spawn(aos_world *w, int32_t id, int32_t owner, int32_t team, int32_t p18) /* FUN_00528ca0 */
{
	if (id < 100 || id > 0x6a)
		return -1;
	int32_t grp = aos_enemy_group_next(w); /* FUN_0055efc0 */
	int i = aos_enemy_new(w, ID, owner, team, p18);
	if (i < 0)
		return -1;
	aos_enemy *e = &w->obj.enemy[i];
	e->serial = grp;
	e->f10 = id;
	e->f7c = 100;
	e->f84 = 0;
	e->hp = e->maxHp = 9999;
	e->active = 0;
	e->shape = (aos_shape){0, 0, 20.0f}; /* DAT_008140fc */
	e->scale[0] = 3.0f;                  /* DAT_00813a50 */
	e->scale[1] = 0;
	e->scale[2] = 0;
	e->mirrorChildren = 0;
	e->motionId = 0;
	e->rehitTimer = 0;
	return i;
}

static bool bit_killed(aos_world *w, aos_enemy *e) /* FUN_0052b980 */
{
	if (e->f10 < 100 || e->f10 > 0x6a)
		return false;
	eff0(w, 0x26c, 0, e->pos, 0, e->ownerId, 0, e->team);
	for (int j = 0; j < 3; j++) {
		float pt[3] = {e->pos[0], e->pos[1], e->pos[2]};
		pt[0] = aos_rand_real(w, -50.0f, 50.0f) + pt[0]; /* DAT_008149e4, DAT_008140ec */
		pt[1] = aos_rand_real(w, -50.0f, 50.0f) + pt[1];
		eff0(w, 0x834, 0, pt, 0, e->ownerId, 0, e->team);
	}
	for (int j = 0; j < 3; j++) {
		float pt[3] = {e->pos[0], e->pos[1], e->pos[2]};
		pt[0] = aos_rand_real(w, -20.0f, 20.0f) + pt[0]; /* DAT_00814108, DAT_008140fc */
		pt[1] = aos_rand_real(w, -20.0f, 20.0f) + pt[1];
		eff0(w, 500, 0, pt, 0, e->ownerId, j * -3 - 10, e->team);
	}
	aos_se(w, 0x1d);
	return false;
}

/* FUN_0055f110: angle from the Bit to the opponent (stealthed-Tsih case as FUN_007804b0) */
static float bit_aim(aos_world *w, aos_enemy *e)
{
	aos_player *o = opponent_of_team(w, e->team);
	if (o->id == AOS_CH_TSIH && o->scratch[0x164 - 0x15c] == 1) {
		float a = aos_dir_angle(player_of_team(w, e->team));
		if (AOS_PI < a)
			a = (o->x <= e->pos[0]) ? AOS_PI : 0.0f;
		return a;
	}
	return aos_atan2(o->y - e->pos[1], o->x - e->pos[0]);
}

static void bit_idle(aos_enemy *e) /* "->idle": T=0, mode 10, speed 16, decel 0.5 */
{
	e->timer2 = 0;
	e->f84 = 10;
	e->f70 = 16.0f; /* DAT_008140d8 */
	set_e74(e, 0.5f);
}

static void wrap_both(float *a)
{
	if (AOS_PI < *a)
		*a -= AOS_2PI;
	if (*a < -AOS_PI)
		*a += AOS_2PI;
}

static void spin(aos_enemy *e, float *a)
{
	if (e->f10 % 2 == 0)
		*a += 0.1f; /* DAT_00853b10 */
	else
		*a -= 0.1f;
}

static void move_heading(aos_enemy *e, float s)
{
	e->pos[0] = aos_cos(e->angle2) * s + e->pos[0];
	e->pos[1] = aos_sin(e->angle2) * s + e->pos[1];
}

static bool bit_update(aos_world *w, aos_enemy *e) /* FUN_00528e20 */
{
	if (e->f7c - 100 < 0 || e->f7c - 100 > 6)
		return true;
	aos_player *own = player_of_team(w, e->team);
	aos_player *opp = opponent_of_team(w, e->team);
	if (!own || !opp)
		return true;
	int32_t idx = e->f10, T = e->timer2;
	int32_t col = colour_of_team(w, e->team);
	float slot = ((float)(idx - 0x67) / 7.0f) * AOS_2PI; /* DAT_0081789c, DAT_00853b78 */

	if (e->f84 == 0) { /* deploy */
		if (T == 0)
			aos_se(w, 0x2f);
		if (-1 < T && T < 0x14) {
			e->scale[0] -= 0.1f;  /* DAT_00853b10 */
			e->scale[1] += 0.05f; /* DAT_00813ef4 */
			e->f70 += e74(e);
			set_e74(e, e74(e) * 0.95f); /* _DAT_00814124 */
			e->angle = slot + HALF_PI;
			e->angle2 = e->angle;
			e->pos[0] = aos_cos(e->angle2) * e->f70 + own->x;
			e->pos[1] = aos_sin(e->angle2) * e->f70 + own->y;
		}
		if (0x13 < T)
			e->scale[2] += 0.05f;
		if (0x28 < T) {
			e->scale[2] = e->scale[0] = e->scale[1] = 1.0f;
			e->timer2 = 0;
			e->f84 = 10;
			e->f70 = 20.0f;
			set_e74(e, 0.5f);
		}
		T = e->timer2;
	}

	if (e->f84 == 10) { /* idle / follow (Deploy Bits on S hold) */
		float s = e->f70;
		if (own->state == 2)
			s *= 2.5f; /* DAT_008153e0 */
		move_heading(e, s);
		float a = slot + aos_atan2(opp->y - own->y, opp->x - own->x);
		float r = own->S < 2 ? 100.0f : 240.0f; /* DAT_00853b90 / DAT_00815584 */
		float tx = aos_cos(a) * r + own->x, ty = aos_sin(a) * r + own->y;
		aos_angle_toward(&e->angle2, aos_atan2(ty - e->pos[1], tx - e->pos[0]), e->f70 * 0.015f);
		e->angle = bit_aim(w, e);
		e->f70 -= e74(e);
		if (e->f70 < 8.0f)
			e->f70 = 8.0f; /* DAT_008140d4 */
		if (0x15d < e->timer2) {
			e->f84 = 10;
			e->timer2 = 0;
		}
		if (own->S == 3) {
			e->f70 = 16.0f;
			set_e74(e, 0.5f);
		}
	} else if (e->f84 == 100) { /* Tentacle Needle */
		if (T == 0)
			e->angle = bit_aim(w, e);
		if (T == 5 && aos_use_gauge(own, 0x199)) {
			aos_spawn_beam(w, ID, 100, e->pos, e->angle, e->ownerId, 0, e->team);
			eff0(w, 0x548, col, e->pos, e->angle, e->ownerId, 0, e->team);
			aos_se_cd(w, 0xd, 3);
		}
		if (0x32 < T)
			bit_idle(e);
	} else if (e->f84 == 0x96) { /* Tentacle Tone */
		if (T == 0)
			e->angle = bit_aim(w, e);
		if (T == 5 && aos_use_gauge(own, 0x198)) {
			aos_spawn_beam(w, ID, 200, e->pos, e->angle, e->ownerId, 0, e->team);
			eff0(w, 0x548, col, e->pos, e->angle, e->ownerId, 0, e->team);
			aos_se_cd(w, 0x10, 5);
		}
		if (0x1e < T)
			bit_idle(e);
	} else if (e->f84 == 0xa0) { /* Tentacle Fruit */
		spin(e, &e->angle);
		wrap_both(&e->angle);
		if (9 < T && 0x13 < T && T % 5 == 0 && aos_use_gauge(own, 0x19a)) {
			aos_spawn_beam(w, ID, 300, e->pos, e->angle, e->ownerId, 0, e->team);
			eff0(w, 0x516, col, e->pos, 0, e->ownerId, 0, e->team);
			aos_se_cd(w, 0xf, 3);
			return true;
		}
		if (0x32 < T)
			bit_idle(e);
	} else if (e->f84 == 200) { /* Ether Leaf */
		float sg = (float)((idx % 2) * 2 - 1);
		if (T == 0)
			e->angle = bit_aim(w, e) + sg * 0.6f; /* DAT_00813f34 */
		if (T == 5 && aos_use_gauge(own, 0x19b)) {
			int m = aos_spawn_missile(w, ID, 200, e->pos, e->angle, e->id, 0, e->team, e->p18);
			if (m >= 0)
				w->obj.missile[m].f74 = -sg * 0.025f; /* DAT_00813edc */
			aos_se_cd(w, 0x10, 3);
			return true;
		}
		if (0x1e < T)
			bit_idle(e);
	} else if (e->f84 == 0xfa) { /* Ether Bug */
		if (T == 0)
			e->angle += AOS_PI;
		spin(e, &e->angle);
		wrap_both(&e->angle);
		if (9 < T && 0x13 < T && T % 5 == 0 && aos_use_gauge(own, 0x19c)) {
			aos_spawn_missile(w, ID, 300, e->pos, e->angle, e->id, 0, e->team, e->p18);
			aos_se_cd(w, 0x31, 5);
			return true;
		}
		if (0x32 < T)
			bit_idle(e);
	} else if (e->f84 == 300) { /* Tentacle Breath, one by one */
		if (T == 0)
			e->angle = bit_aim(w, e);
		if (T == 10) {
			aos_spawn_laser(w, ID, 100, e->pos, e->angle, e->id, e->team, e->p18);
			aos_se_cd(w, 0x12, 5);
		}
		if (0x32 < T)
			bit_idle(e);
	} else if (e->f84 == 0x136) { /* Tentacle Breath, all at once */
		if (T == 0)
			e->angle = aos_aim(w, own);
		if (T == 10) {
			aos_spawn_laser(w, ID, 0x96, e->pos, e->angle, e->id, e->team, e->p18);
			aos_se_cd(w, 0x12, 5);
		}
		if (0x32 < T)
			bit_idle(e);
	} else if (e->f84 == 0x15e || e->f84 == 0x160) { /* Tentacle Sting, approach */
		effN(w, e->age % 3 + 300, 0, e->pos, e->angle, e->ownerId, e->team);
		float s = e->f70;
		if (own->state == 2)
			s *= 2.5f;
		move_heading(e, s);
		float tx, ty;
		if (e->f84 == 0x15e) {
			float b = ((float)(idx - 100) / 7.0f) * AOS_2PI;
			tx = aos_cos(b) * 180.0f + opp->x; /* DAT_00815258 */
			ty = aos_sin(b) * 180.0f + opp->y;
		} else {
			float b = ((float)((idx - 0x62) % 7) / 7.0f) * AOS_2PI;
			tx = aos_cos(b) * 240.0f + opp->x;
			ty = aos_sin(b) * 240.0f + opp->y;
		}
		aos_angle_toward(&e->angle2, aos_atan2(ty - e->pos[1], tx - e->pos[0]), e->f70 * 0.02f);
		e->angle = aos_atan2(opp->y - e->pos[1], opp->x - e->pos[0]);
		e->f70 -= e74(e);
		if (e->f70 < 8.0f)
			e->f70 = 8.0f;
		if (0x19 < e->timer2) {
			e->timer2 = 0;
			e->f70 = 20.0f;
			set_e74(e, 1.0f);
			e->angle2 = AOS_PI; /* v >= 0x5a1 */
			e->f84 += 1;
		}
	} else if (e->f84 == 0x15f || e->f84 == 0x161) { /* Tentacle Sting, fire */
		if (T == 0)
			e->angle = aos_atan2(opp->y - e->pos[1], opp->x - e->pos[0]);
		if (T == 5) {
			aos_spawn_beam(w, ID, 100, e->pos, e->angle, e->ownerId, 0, e->team);
			eff0(w, 0x548, col, e->pos, e->angle, e->ownerId, 0, e->team);
			aos_se_cd(w, 0xd, 3);
		}
		if (10 < T) {
			e->timer2 = 0;
			e->f70 = 16.0f;
			set_e74(e, 0.5f);
			e->f84 += 1;
			if (e->f84 == 0x162)
				e->f84 = 10;
		}
	} else if (e->f84 == 400) { /* Flower Crown */
		spin(e, &e->angle);
		wrap_both(&e->angle);
		e->angle2 = e->angle;
		if (9 < T && T < 0x15 && T % 5 == 0) {
			aos_spawn_beam(w, ID, 300, e->pos, e->angle, e->ownerId, 0, e->team);
			aos_spawn_beam(w, ID, 300, e->pos, e->angle + AOS_PI, e->ownerId, 0, e->team);
			aos_spawn_beam(w, ID, 300, e->pos, e->angle + HALF_PI, e->ownerId, 0, e->team);
			aos_spawn_beam(w, ID, 300, e->pos, e->angle - HALF_PI, e->ownerId, 0, e->team);
		}
		if (9 < T && T < 0x15 && T % 7 == 0) {
			aos_spawn_missile(w, ID, 300, e->pos, e->angle, e->id, 0, e->team, e->p18);
			aos_spawn_missile(w, ID, 300, e->pos, e->angle + AOS_PI, e->id, 0, e->team, e->p18);
			aos_spawn_missile(w, ID, 300, e->pos, e->angle + HALF_PI, e->id, 0, e->team, e->p18);
			aos_spawn_missile(w, ID, 300, e->pos, e->angle - HALF_PI, e->id, 0, e->team, e->p18);
		}
		if (T == 1)
			for (int j = -2; j < 2; j++)
				effN(w, 400, 0, e->pos, ((float)j / 2.0f) * AOS_PI + e->angle, e->id, e->team);
		if (0x32 < T)
			bit_idle(e);
	} else if (e->f84 == 500) { /* Shell Shot tackle (v >= 0x58c) */
		const int32_t A = 10, End = 0x1e;
		if (A < T) {
			if (T < A + 0xf)
				effN(w, e->age % 3 + 300, 0, e->pos, e->angle, e->ownerId, e->team);
			if (T < A + 0xf)
				effN(w, 500, 0, e->pos, e->angle, e->ownerId, e->team);
			move_heading(e, e->f70);
			if (T < A + 0xf && ((T + A) & 3) == 0)
				aos_spawn_hitbox(w, ID, 100, e->pos, e->angle2, e->ownerId, 0, e->team);
			if (T == A + 1) {
				for (int j = 0; j < 5; j++) {
					float r = aos_rand_real(w, -0.15f, 0.15f); /* DAT_00815dc4, DAT_00813f20 */
					int32_t n = aos_rand_int(w, 0, 100);
					eff0(w, 0xa32 + n % 4, 0xd, e->pos, r + e->angle2, e->ownerId, j * -3, e->team);
				}
			}
		} else if (8 < T) {
			effN(w, e->age % 3 + 300, 0, e->pos, e->angle, e->ownerId, e->team);
			move_heading(e, e->f70);
			aos_angle_toward(&e->angle2, aos_atan2(opp->y - e->pos[1], opp->x - e->pos[0]), 0.02f);
			e->angle = e->angle2;
			e->f70 += 0.25f; /* DAT_00813f24 */
			if (T == A) {
				e->f70 = 20.0f;
				aos_se(w, 0x1f);
			}
		} else {
			move_heading(e, e->f70);
			aos_angle_toward(&e->angle2, aos_atan2(opp->y - e->pos[1], opp->x - e->pos[0]), 0.02f);
			e->angle = e->angle2;
		}
		if (End < e->timer2) {
			e->timer2 = 0;
			e->f84 = 0x1f5;
			e->f70 = 20.0f;
			set_e74(e, 0.5f);
		}
	} else if (e->f84 == 0x1f5) { /* return to slot */
		float s = e->f70;
		if (own->state == 2)
			s *= 2.5f;
		move_heading(e, s);
		float a = slot + aos_atan2(opp->y - own->y, opp->x - own->x);
		float tx = aos_cos(a) * 100.0f + own->x, ty = aos_sin(a) * 100.0f + own->y;
		aos_angle_toward(&e->angle2, aos_atan2(ty - e->pos[1], tx - e->pos[0]), e->f70 * 0.015f);
		e->angle = aos_atan2(opp->y - e->pos[1], opp->x - e->pos[0]);
		e->f70 -= e74(e);
		if (e->f70 < 8.0f)
			e->f70 = 8.0f;
		if (0x1e <= e->timer2) {
			e->f84 = 10;
			e->timer2 = 0;
		}
	} else if (e->f84 == 600) { /* Spring Storm, gather */
		float phi = ((float)idx / 7.0f) * AOS_2PI + HALF_PI;
		aos_angle_normalize(&phi);
		float v[2] = {140.0f, 0.0f}; /* DAT_00817004 */
		aos_vec_rotate(v, phi);
		v[0] += own->x;
		v[1] += own->y;
		aos_angle_toward(&e->angle2, aos_atan2(v[1] - e->pos[1], v[0] - e->pos[0]), 0.4f); /* DAT_00813f30 */
		e->angle = e->angle2;
		e->f70 = T < 10 ? 16.0f : 8.0f;
		move_heading(e, e->f70);
		if (0x14 < T) {
			e->f84 = 0x262;
			e->timer2 = 0;
			e->pos[0] = v[0];
			e->pos[1] = v[1];
			e->angle2 = phi;
			e->angle = e->angle2;
		}
	} else if (e->f84 == 0x262) { /* Spring Storm, fire */
		if (T == 1)
			for (int j = -2; j < 2; j++)
				effN(w, 0x19a, 0, e->pos, ((float)j / 2.0f) * AOS_PI + e->angle, e->id, e->team);
		if (T == 10) {
			aos_spawn_laser(w, ID, 200, e->pos, e->angle, e->id, e->team, e->p18);
			aos_se_cd(w, 0x12, 5);
		}
		spin(e, &e->angle2);
		if (AOS_PI < e->angle2)
			e->angle2 -= AOS_2PI; /* literal: no lower wrap */
		if (9 < T && T < 0x15 && T % 5 == 0) {
			/* literal: the first two beams both go to heading+π */
			aos_spawn_beam(w, ID, 300, e->pos, e->angle2 + AOS_PI, e->ownerId, 0, e->team);
			aos_spawn_beam(w, ID, 300, e->pos, e->angle2 + AOS_PI, e->ownerId, 0, e->team);
			aos_spawn_beam(w, ID, 300, e->pos, e->angle2 - HALF_PI, e->ownerId, 0, e->team);
			aos_spawn_beam(w, ID, 300, e->pos, e->angle2 + HALF_PI, e->ownerId, 0, e->team);
			aos_se_cd(w, 0xf, 3);
		}
		if (0x31 < T) {
			e->f84 = 10;
			e->timer2 = 0;
		}
	} else if (e->f84 == 900 && 99 < T) { /* death */
		bit_killed(w, e);
		return false;
	}
	return true;
}

static void bit_quad(aos_world *w, aos_enemy *e, int frame, bool scaled)
{
	aos_dcmd *c = aos_draw(aos_texf("nk_bit%02d", frame), bank_of_team(e->team), e->pos[0], e->pos[1], 0);
	c->halfW = c->halfH = 27.0f; /* 54 px quad (DAT_008178a8) */
	c->rgba = aos_rgba(e->scale[2], e->scale[2], e->scale[2], 1.0f); /* (a=1, rgb=+0x68) */
	if (scaled) {
		c->sx = e->scale[0];
		c->sy = e->scale[1];
	}
	c->rotZ = aos_rad2bam(e->angle);
	(void)w;
}

static int iabs(int v) { return v < 0 ? -v : v; }

static void bit_draw(aos_world *w, aos_enemy *e) /* FUN_0052bbf0 */
{
	if (e->f10 < 100 || e->f10 > 0x6a)
		return;
	if (e->f10 == 0x66 || e->f10 == 0x68) { /* v > 0x58a: melee glow, additive */
		/* a pulse 0.75+0.25·sin((age%50)/50·2π) is computed but never read: stored to [ebp-0x100]/[ebp-0x104] at 0x52bc76/0x52bccc, no later load (dead code in the original) */
		for (int k = 0; k < 2; k++) {
			aos_dcmd *c = aos_draw(aos_texf("nk_bitmelee%02d", k), bank_of_team(e->team), e->pos[0], e->pos[1], 0);
			c->halfW = c->halfH = 31.0f; /* 62 px (DAT_00825a00) */
			float v = e->scale[2] * (k ? 0.75f : 0.25f);
			c->rgba = aos_rgba(v, v, v, k ? 1.0f : 0.5f);
			c->sx = e->scale[0];
			c->sy = e->scale[1];
			c->rotZ = aos_rad2bam(e->angle);
			c->blend = AOS_BLEND_ADD_ALPHA;
		}
	}
	int32_t T = e->timer2, m = e->f84, f;
	if (m == 0) {
		bit_quad(w, e, (iabs(T) / 5) % 3, true);
		return;
	}
	if (m == 10) {
		f = (T / 5) % 3;
	} else if (m == 100 || m == 0x96 || m == 0xa0 || m == 200 || m == 0xfa || m == 300 || m == 0x136) {
		if (T < 0x1e) {
			f = T < 0 ? 0 : T;
			f /= 2;
			if (f > 3)
				f = 3;
		} else {
			f = (T - 0x1e) / 4;
			if (f > 3)
				f = 3;
			f = 3 - f;
		}
		f = f * 3 + 3;
	} else if (m == 0x15f || m == 0x161) {
		if (T < 6) {
			f = T > 3 ? 3 : T;
		} else {
			f = 10 - T;
			if (f > 3) f = 3;
			if (f < 0) f = 0;
		}
		f = f * 3 + 3;
	} else if (m == 400) {
		if (T < 0x2e) {
			if (T < 0) {
				f = (iabs(T) / 5) % 3;
			} else {
				f = T > 3 ? 3 : T;
				f = f * 3 + 3;
			}
		} else {
			f = 10 - T;
			if (f > 3) f = 3;
			if (f < 0) f = 0;
			f = f * 3 + 3;
		}
	} else if (m == 0x262) {
		if (T < 0x2e) {
			f = T > 3 ? 3 : T;
		} else {
			f = 10 - T;
			if (f > 3) f = 3;
			if (f < 0) f = 0;
		}
		f = f * 3 + 3;
	} else {
		f = (iabs(T) / 5) % 3;
	}
	if (f < 0) /* unreachable: modes 0x15f/0x161/0x262 are always entered with T = 0 (mode += 1 / 600 -> 0x262 set T=0), so T is never negative there */
		f = 0;
	bit_quad(w, e, f, false);
}

/* ------------------------------------------------------------------ */
/* beam family (type 13)                                               */

static int beam_spawn(aos_world *w, int32_t id, const float pos[3], float ang, int32_t owner, int32_t age0, int32_t team) /* FUN_004af380 */
{
	if (id != 100 && id != 200 && id != 300)
		return -1;
	int i = aos_beam_new(w, ID, id, pos, ang, owner, age0, team);
	if (i < 0)
		return -1;
	aos_beam *b = &w->obj.beam[i];
	if (id == 100) {
		b->speed = 20.0f;                      /* DAT_008140fc */
		b->damage = 100;
		b->shape = (aos_shape){24.0f, 8.0f, 0}; /* DAT_00853b8c, DAT_008140d4 */
		b->knockback = 10.0f;                  /* DAT_00813f4c */
	} else if (id == 200) {
		b->speed = 15.0f; /* DAT_00853b88 */
		b->damage = 0x78;
		b->shape = (aos_shape){20.0f, 10.0f, 0};
		b->knockback = 10.0f;
	} else {
		b->speed = 14.0f; /* DAT_00813a54 */
		b->scale = 2.0f;  /* DAT_00813a4c */
		b->damage = 0x32;
		b->shape = (aos_shape){0, 0, 10.0f};
		b->knockback = 10.0f;
	}
	return i;
}

static bool beam_update(aos_world *w, aos_beam *b) /* FUN_004b0220 */
{
	int32_t col = colour_of_team(w, b->team);
	switch (b->id) {
	case 100:
		b->pos[0] = aos_cos(b->moveAngle) * b->speed + b->pos[0];
		b->pos[1] = aos_sin(b->moveAngle) * b->speed + b->pos[1];
		if (0x14 < b->age && !aos_in_arena(b->pos, 20.0f))
			return false;
		effN(w, 200, col, b->pos, b->angle, b->ownerId, b->team);
		if (b->age == 1)
			eff0(w, 0x548, col, b->pos, b->angle, b->ownerId, 0, b->team);
		break;
	case 200:
		b->pos[0] = aos_cos(b->moveAngle) * b->speed + b->pos[0];
		b->pos[1] = aos_sin(b->moveAngle) * b->speed + b->pos[1];
		if (0x14 < b->age && !aos_in_arena(b->pos, 20.0f))
			return false;
		effN(w, 0xd2, col, b->pos, b->angle, b->ownerId, b->team);
		particles(w, 0x4b0, col, b->pos, b->angle + AOS_PI, b->ownerId, 1);
		break;
	case 300:
		if (b->scale <= 1.0f)
			b->scale = 1.0f;
		else
			b->scale -= 0.1f;
		if (2.0f < b->speed)
			b->speed -= 0.5f;
		b->pos[0] = aos_cos(b->moveAngle) * b->speed + b->pos[0];
		b->pos[1] = aos_sin(b->moveAngle) * b->speed + b->pos[1];
		if (b->age == 0x32) {
			b->id = 0x136;
			b->active = 0;
			b->scale = 2.0f;
			eff0(w, 0x516, col, b->pos, 0, b->ownerId, 0, b->team);
		}
		break;
	case 0x136:
		b->scale -= 0.25f;
		if (b->scale <= 0.0f)
			return false;
		break;
	}
	return true;
}

static void beam_on_hit(aos_world *w, aos_beam *b) /* FUN_004aff60 */
{
	if (b->id != 100 && b->id != 200 && b->id != 300)
		return;
	int32_t col = colour_of_team(w, b->team);
	int n = b->id == 100 ? 6 : b->id == 200 ? 5 : 3;
	int s = b->id == 100 ? 6 : 5;
	eff0(w, 1000, col, b->pos, b->angle + AOS_PI, b->ownerId, 0, b->team);
	particles(w, 1000, col, b->pos, b->angle, b->ownerId, n);
	sparks(w, col, s, s, b->angle, b->pos, b->ownerId);
	aos_se_cd(w, 0x13, 3);
}

/* bullet-atlas cells (beamsphere): glow / core source rects of colour c */
static void cell_glow(int32_t c, float s[4])
{
	s[0] = (float)((c % 10 & 1) * 100 + 0x32 + (c / 10) * 0xfa);
	s[1] = (float)((c % 10 / 2) * 100);
	s[2] = (float)((c % 10 & 1) * 100 + 0x95 + (c / 10) * 0xfa);
	s[3] = s[1] + 99.0f;
}
static void cell_core(int32_t c, float s[4])
{
	s[0] = (float)((c / 10) * 0xfa);
	s[1] = (float)((c % 10) * 0x32);
	s[2] = s[0] + 49.0f;
	s[3] = s[1] + 49.0f;
}

static void atlas(const float pos[3], int32_t c, bool glow, float hw, float hh, float ang, float alpha255, int blend)
{
	aos_dcmd *d = aos_draw(aos_tex("beamsphere"), AOS_BANK_COMMON, pos[0], pos[1], 0);
	d->halfW = hw;
	d->halfH = hh;
	d->hasSrc = 1;
	if (glow)
		cell_glow(c, d->src);
	else
		cell_core(c, d->src);
	d->rotZ = aos_rad2bam(ang);
	d->blend = (uint8_t)blend;
	d->rgba = aos_rgba(1, 1, 1, (float)(int)alpha255 / 255.0f);
}

static void beam_draw(aos_world *w, aos_beam *b) /* FUN_004af560 */
{
	int32_t c = colour_of_team(w, b->team);
	if (b->id == 200) {
		atlas(b->pos, c, true, 50.0f, 20.0f, b->angle, 96.0f * b->alpha, AOS_BLEND_ALPHA);
		atlas(b->pos, c, false, 30.0f, 10.0f, b->angle, 255.0f * b->alpha, AOS_BLEND_ADD_ALPHA);
	} else if (b->id == 300 || b->id == 0x136) {
		atlas(b->pos, c, true, 24.0f * b->scale, 24.0f * b->scale, 0, 96.0f * b->alpha, AOS_BLEND_ALPHA);
		atlas(b->pos, c, false, 15.0f * b->scale, 15.0f * b->scale, 0, 255.0f * b->alpha, AOS_BLEND_ADD_ALPHA);
	}
}

/* ------------------------------------------------------------------ */
/* missile family (type 13)                                            */

static int missile_spawn(aos_world *w, int32_t id, const float pos[3], float ang, int32_t owner, int32_t age0, int32_t team, int32_t p18) /* FUN_00670310 */
{
	if (id != 200 && id != 300)
		return -1;
	int i = aos_missile_new(w, ID, id, pos, ang, owner, age0, team, p18);
	if (i < 0)
		return -1;
	aos_missile *m = &w->obj.missile[i];
	m->speed = id == 200 ? 15.0f : 14.0f; /* DAT_00853b88 / DAT_00813a54 */
	m->hp = 0x3c;
	m->damage = id == 200 ? 0x78 : 0x96; /* 200: v > 0x58a */
	m->shapeBody = (aos_shape){0, 0, 10.0f};
	m->shapeProj = (aos_shape){0, 0, 10.0f};
	m->knockback = 10.0f;
	return i;
}

static bool missile_update(aos_world *w, aos_missile *m) /* FUN_00671310 */
{
	if (m->id == 200) {
		if (m->age < 100) {
			m->moveAngle += m->f74;
			m->angle = m->moveAngle;
		}
		if (6.0f < m->speed) /* DAT_00813f44 */
			m->speed -= 0.2f;
		if (m->age % 2 == 0)
			eff0(w, 0x802, 0, m->pos, m->moveAngle, m->ownerId, 0, m->team);
	} else if (m->id == 300) {
		if (3.0f < m->speed)
			m->speed -= 0.5f;
		m->angle += 0.08f; /* DAT_00813f00 */
		if (AOS_PI < m->angle)
			m->angle -= AOS_2PI;
		if (w->frame % 2 == 0)
			effN(w, 100, 0, m->pos, m->angle, m->ownerId, m->team);
	} else {
		return true;
	}
	m->pos[0] = aos_cos(m->moveAngle) * m->speed + m->pos[0];
	m->pos[1] = aos_sin(m->moveAngle) * m->speed + m->pos[1];
	if (!aos_in_arena(m->pos, 200.0f)) { /* DAT_008147cc, v >= 0x5a1 */
		eff0(w, 0x26c, 0, m->pos, 0, m->ownerId, 0, m->team);
		return false;
	}
	return true;
}

static bool missile_on_hit(aos_world *w, aos_missile *m) /* FUN_006710b0 */
{
	if (m->id == 200 || m->id == 300) {
		eff0(w, 0x262, 0, m->pos, 0, m->ownerId, 0, m->team);
		eff0(w, 0x514, 1, m->pos, 0, m->ownerId, 0, m->team);
		aos_se(w, 0x14);
	}
	return false;
}

static bool missile_on_killed(aos_world *w, aos_missile *m) /* FUN_006711e0 */
{
	if (m->id == 200 || m->id == 300) {
		eff0(w, 0x26c, 0, m->pos, 0, m->ownerId, 0, m->team);
		eff0(w, 0x515, 1, m->pos, 0, m->ownerId, 0, m->team);
		aos_se(w, 0x1d);
	}
	return false;
}

static void missile_draw(aos_world *w, aos_missile *m) /* FUN_006704b0 */
{
	(void)w;
	if (m->id != 200 && m->id != 300)
		return;
	const char *fmt = m->id == 200 ? "nk_leaf%02d" : "nk_bug%02d";
	float ph = aos_sin(((float)(m->age % 0x32) / 50.0f) * AOS_2PI);
	float a1 = ph * 0.25f + 0.75f, a2 = ph * 0.35f + 0.65f; /* DAT_00813f24/3c, DAT_00813f2c/0081788c */
	for (int k = 0; k < 3; k++) {
		aos_dcmd *c = aos_draw(aos_texf(fmt, k), bank_of_team(m->team), m->pos[0], m->pos[1], 0);
		c->halfW = c->halfH = 27.0f; /* 54 px */
		c->rotZ = aos_rad2bam(m->angle);
		if (k > 0) {
			c->blend = AOS_BLEND_ADD_ALPHA;
			c->rgba = aos_rgba(1, 1, 1, k == 1 ? a1 : a2);
		}
	}
}

/* ------------------------------------------------------------------ */
/* hitbox family (type 13)                                             */

static int hitbox_spawn(aos_world *w, int32_t id, const float pos[3], float ang, int32_t owner, int32_t zero, int32_t team) /* FUN_005e6730 */
{
	int32_t dmg;
	aos_shape s;
	switch (id) {
	case 100: dmg = 0x1e; s = (aos_shape){0, 0, 80.0f}; break;          /* DAT_00814164 */
	case 200: dmg = 0x32; s = (aos_shape){800.0f, 40.0f, 0}; break;     /* DAT_008149c8, DAT_008140e0 */
	case 0xd2: dmg = 0x50; s = (aos_shape){800.0f, 60.0f, 0}; break;    /* DAT_00813a58 */
	case 300: dmg = 0x78; s = (aos_shape){800.0f, 90.0f, 0}; break;     /* DAT_00814170 */
	case 0x136: dmg = 0; s = (aos_shape){800.0f, 90.0f, 0}; break;
	default: return -1;
	}
	int i = aos_hitbox_new(w, ID, id, pos, ang, owner, zero, team);
	if (i < 0)
		return -1;
	aos_hitbox *h = &w->obj.hitbox[i];
	h->hitType = 3;
	h->damage = dmg;
	h->shape = s;
	h->knockback = 10.0f; /* DAT_00813f4c */
	h->stun = 1.0f;
	return i;
}

static void hitbox_on_hit(aos_world *w, aos_hitbox *hb, aos_player *t) /* FUN_005e6a90 */
{
	int32_t col;
	switch (hb->id) {
	case 100: {
		float base = aos_rand_real(w, -AOS_PI, AOS_PI);
		col = colour_of_team(w, hb->team);
		for (int j = -3; j < 3; j++) {
			float a = ((float)j / 3.0f) * AOS_PI;
			float r = aos_rand_real(w, -0.25f, 0.25f); /* DAT_0081411c, DAT_00813f24 */
			eff0(w, 0x456, col, &t->x, a + r + base, hb->ownerId, 0, hb->team);
		}
		eff0(w, 0xaf0, col, &t->x, 0, hb->ownerId, 0, hb->team);
		sparks(w, col, 5, 6, hb->angle, &t->x, hb->ownerId);
		aos_se_cd(w, 0x18, 4);
		break;
	}
	case 200:
	case 0xd2:
		col = colour_of_team(w, hb->team);
		for (int j = -3; j < 3; j++) {
			float a = ((float)j / 3.0f) * AOS_PI;
			float r = aos_rand_real(w, -0.25f, 0.25f);
			eff0(w, 0x456, col, &t->x, a + r, hb->ownerId, 0, hb->team);
		}
		eff0(w, 0x516, col, &t->x, 0, hb->ownerId, 0, hb->team);
		aos_se_cd(w, 0x13, 4);
		break;
	case 300:
		col = colour_of_team(w, hb->team);
		for (int j = -2; j < 2; j++) {
			float a = ((float)j / 2.0f) * AOS_PI;
			float r = aos_rand_real(w, -0.75f, 0.75f); /* DAT_0081fa78, DAT_00813f3c */
			eff0(w, 0x456, col, &t->x, a + r, hb->ownerId, 0, hb->team);
		}
		eff0(w, 0x516, col, &t->x, 0, hb->ownerId, 0, hb->team);
		aos_se_cd(w, 0x13, 4);
		break;
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* laser family (type 13) — no segments; periodic rect hitboxes        */

static int laser_spawn(aos_world *w, int32_t id, const float pos[3], float ang, int32_t owner, int32_t team, int32_t p18) /* FUN_00620e90 */
{
	if (id != 100 && id != 0x96 && id != 200)
		return -1;
	int i = aos_laser_new(w, ID, id, pos, ang, owner, team, p18);
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

static void along(const aos_laser *l, float d, float out[3])
{
	out[0] = aos_cos(l->angle) * d + l->origin[0];
	out[1] = aos_sin(l->angle) * d + l->origin[1];
	out[2] = 0;
}

static bool laser_update(aos_world *w, aos_laser *l) /* FUN_00622af0 */
{
	float pt[3];
	int32_t a = l->age;
	l->nseg = 0; /* FUN_004a9f10: segment list cleared, never filled */
	if (l->id == 100 || l->id == 0x96) {
		if (a > (l->id == 100 ? 0x1d : 0x31))
			return false;
		if (a % 5 == 2 && a < 0x10) {
			along(l, 30.0f, pt); /* DAT_008140e8 */
			eff0(w, 0x546, colour_of_team(w, l->team), pt, l->angle, l->ownerId, 0, l->team);
		}
		bool hit = l->id == 100 ? (a == 1 || a == 6 || a == 0xb) : (a == 0xb || a == 0x10 || a == 0x15 || a == 0x1a);
		if (hit) {
			along(l, 800.0f, pt); /* DAT_008149c8 */
			aos_spawn_hitbox(w, ID, l->id == 100 ? 200 : 0xd2, pt, l->angle, l->ownerId, 0, l->team);
		}
	} else if (l->id == 200) {
		if (0x4f < a)
			return false;
		if (a % 5 == 2 && a < 0x29) {
			along(l, 30.0f, pt);
			eff0(w, 0x546, colour_of_team(w, l->team), pt, l->angle, l->ownerId, 0, l->team);
		}
		if (9 < a && a < 0x33 && a % 2 == 0) {
			along(l, 720.0f, pt); /* DAT_008178c4 */
			aos_spawn_hitbox(w, ID, a % 6 == 0 ? 300 : 0x136, pt, l->angle, l->ownerId, 0, l->team);
		}
	}
	return true;
}

static void laser_on_hit(aos_world *w, aos_laser *l, int32_t seg, aos_player *p) /* FUN_00622ad0: empty */
{
	(void)w, (void)l, (void)seg, (void)p;
}

static void lsr_quad(const aos_laser *l, const float c[3], float hh, float sy0, float sy1, uint32_t rgba, int blend)
{
	aos_dcmd *d = aos_draw(aos_tex("nk_lsr"), bank_of_team(l->team), c[0], c[1], 0);
	d->halfW = 1000.0f;
	d->halfH = hh;
	d->hasSrc = 1;
	d->src[0] = 0;
	d->src[1] = sy0;
	d->src[2] = 511.0f;
	d->src[3] = sy1;
	d->rotZ = aos_rad2bam(l->angle);
	d->blend = (uint8_t)blend;
	d->rgba = rgba;
}

static void laser_draw(aos_world *w, aos_laser *l) /* FUN_00620ff0 */
{
	int32_t t = l->age;
	float c[3], m[3], h, h3, mr, A;
	along(l, 1000.0f, c); /* DAT_00814408 */
	along(l, 30.0f, m);
	if (l->id == 100) {
		h = (t / 2) % 2 == 0 ? 80.0f : 100.0f;
		h3 = 20.0f;
		mr = 50.0f;
		if (t < 5) { h *= (float)t / 5.0f; h3 *= (float)t / 5.0f; mr *= (float)t / 5.0f; }
		if (0xf < t) { h *= (float)(0x1e - t) / 15.0f; h3 *= (float)(0x1e - t) / 15.0f; mr *= (float)(0x1e - t) / 15.0f; }
		A = t < 0x10 ? 255.0f : (float)(int)((float)(0x1e - t) / 15.0f * 255.0f);
	} else if (l->id == 0x96) {
		h = (t / 2) % 2 == 0 ? 90.0f : 120.0f;
		h3 = 30.0f;
		mr = 50.0f;
		if (t < 10) { h *= 0.1f; h3 *= 0.1f; }
		else if (t < 0xe) { h *= (float)(t - 9) / 5.0f; h3 *= (float)(t - 9) / 5.0f; }
		if (0x1e < t) h *= (float)(0x32 - t) / 30.0f;
		if (0x14 < t) h3 *= (float)(0x32 - t) / 30.0f;
		if (t < 5) mr *= (float)t / 5.0f;
		if (0x14 < t) mr *= (float)(0x32 - t) / 30.0f;
		A = t < 0x1f ? 255.0f : (float)(int)((float)(0x32 - t) / 30.0f * 255.0f);
	} else if (l->id == 200) {
		h = (t / 2) % 2 == 0 ? 180.0f : 240.0f;
		h3 = 30.0f;
		mr = 100.0f;
		if (t < 5) { h *= (float)t / 5.0f; h3 *= (float)t / 5.0f; mr *= (float)t / 5.0f; }
		if (0x32 < t) { h *= (float)(0x50 - t) / 30.0f; h3 *= (float)(0x50 - t) / 30.0f; mr *= (float)(0x50 - t) / 30.0f; }
		A = t < 0x33 ? 255.0f : (float)(int)((float)(0x50 - t) / 30.0f * 255.0f);
	} else {
		return;
	}
	int Ai = (int)A;
	lsr_quad(l, c, h, 340.0f, 499.0f, aos_rgba(1, 1, 1, (float)(Ai / 5) / 255.0f), AOS_BLEND_ALPHA);
	lsr_quad(l, c, h * 0.75f, 0.0f, 159.0f, aos_rgba(1, 1, 1, (float)(Ai / 3) / 255.0f), AOS_BLEND_ADD_ALPHA);
	float row = (float)(((t / 2) % 3) * 0x3c + 0xa0);
	lsr_quad(l, c, h3, row, row + 59.0f, aos_rgba(1, 1, 1, 160.0f / 255.0f), AOS_BLEND_ADD_ALPHA);
	int32_t col = colour_of_team(w, l->team);
	int ag = l->id == 200 ? Ai / 4 : Ai / 2, ac = l->id == 200 ? Ai / 2 : Ai;
	atlas(m, col, true, mr, mr, 0, (float)ag, AOS_BLEND_ADD_ALPHA);
	atlas(m, col, false, mr + 0.5f, mr + 0.5f, 0, (float)ac, AOS_BLEND_ADD_ALPHA);
}

/* ------------------------------------------------------------------ */
/* effect family (type 13)                                             */

static int effect_spawn(aos_world *w, int32_t id, int32_t var, const float pos[3], float ang, int32_t owner, int32_t age0, int32_t team) /* FUN_004fef30 */
{
	switch (id) {
	case 100: case 200: case 0xd2: case 300: case 0x12d: case 0x12e: case 400: case 0x19a: case 500:
		break;
	default:
		return -1;
	}
	int i = aos_effect_new(w, ID, id, var, pos, ang, owner, age0, team);
	if (i < 0)
		return -1;
	aos_effect *e = &w->obj.effect[i];
	e->drawLayer = id == 100 ? 0 : 1;
	e->scale = 1.0f;
	e->alpha = 1.0f;
	e->aux = 0;
	return i;
}

static bool effect_update(aos_world *w, aos_effect *e) /* FUN_005004c0 */
{
	switch (e->id) {
	case 400:
	case 0x19a: {
		aos_enemy *b = aos_enemy_by_id(w, e->ownerId);
		aos_player *o = player_of_team(w, e->team);
		if (!b || !o)
			return true; /* literal: kept, frozen */
		if (e->aux < 32.0f) /* DAT_008140dc */
			e->aux += 4.0f;
		e->scale += 0.1f;
		if (1.0f < e->scale)
			e->scale = 1.0f;
		e->moveDir += 0.1f;
		e->rotation = e->moveDir;
		float r = e->id == 400 ? e->aux : e->aux * 2.0f;
		e->pos[0] = aos_cos(e->moveDir) * r + b->pos[0];
		e->pos[1] = aos_sin(e->moveDir) * r + b->pos[1];
		if ((e->id == 400 ? 0x1e : 0x32) < e->age)
			e->alpha -= 0.05f;
		return 0.0f < e->alpha;
	}
	case 500:
		e->alpha -= 0.15f;
		e->scale += 0.25f;
		return 0.0f < e->alpha;
	case 300:
	case 0x12d:
	case 0x12e:
		e->alpha -= 0.1f;
		return !(e->alpha <= 0.0f);
	case 100:
		e->alpha -= 0.075f; /* DAT_00813efc */
		return !(e->alpha <= 0.0f);
	case 200:
		if (2 < e->age)
			e->alpha -= 0.1f;
		return !(e->alpha <= 0.0f);
	case 0xd2:
		if (2 < e->age)
			e->alpha -= 0.2f;
		return !(e->alpha <= 0.0f);
	default:
		return true;
	}
}

static aos_dcmd *eq(int tex, int bank, const aos_effect *e, float hw, float hh, int blend, float a255)
{
	aos_dcmd *c = aos_draw(tex, bank, e->pos[0], e->pos[1], 0);
	c->halfW = hw;
	c->halfH = hh;
	c->rotZ = aos_rad2bam(e->rotation);
	c->blend = (uint8_t)blend;
	c->rgba = aos_rgba(1, 1, 1, (float)(int)a255 / 255.0f);
	return c;
}

static void effect_draw(aos_world *w, aos_effect *e) /* FUN_004ff3e0 */
{
	(void)w;
	int bank = bank_of_team(e->team);
	switch (e->id) {
	case 400:
	case 0x19a: {
		float k = e->id == 0x19a ? 2.0f : 1.0f;
		eq(aos_tex("nk_petal01"), bank, e, 32.0f * k, 32.0f * e->scale * k, AOS_BLEND_ALPHA, 192.0f * e->alpha);
		eq(aos_tex("nk_petal00"), bank, e, 32.0f * k, 32.0f * e->scale * k, AOS_BLEND_ADD_ALPHA, 192.0f * e->alpha);
		break;
	}
	case 500: {
		int f = e->age / 3 + 3;
		if (2 < f)
			f = 2; /* literal */
		f = f * 3 + e->age % 3;
		eq(aos_texf("nk_cone%02d", f), bank, e, 48.0f * e->scale, 48.0f * e->scale, AOS_BLEND_ADD_ALPHA, 128.0f * e->alpha);
		break;
	}
	case 300:
	case 0x12d:
	case 0x12e:
		eq(aos_texf("nk_bit%02d", e->id - 300), bank, e, 27.0f * e->scale, 27.0f * e->scale, AOS_BLEND_ADD_ALPHA, 128.0f * e->alpha);
		break;
	case 100:
		eq(aos_tex("nk_bug00"), bank, e, 32.0f * e->scale, 32.0f * e->scale, AOS_BLEND_ADD_ALPHA, 128.0f * e->alpha);
		break;
	case 200: {
		int32_t v = e->variant;
		aos_dcmd *c = eq(aos_tex("beamemitb"), AOS_BANK_COMMON, e, 60.0f, 40.0f, AOS_BLEND_ALPHA, 32.0f * e->alpha);
		c->hasSrc = 1;
		c->src[0] = (float)((v / 8) * 0xf0);
		c->src[1] = (float)((v % 8) * 0x40);
		c->src[2] = c->src[0] + 0x77;
		c->src[3] = c->src[1] + 0x3f;
		if (e->age < 2) {
			c = eq(aos_tex("beamemit"), AOS_BANK_COMMON, e, 48.0f, 16.0f, AOS_BLEND_ADD_ALPHA, 255.0f);
			c->src[0] = (float)((v / 8) * 0x100);
			c->src[2] = c->src[0] + 0x7f;
		} else {
			c = eq(aos_tex("beamemitb"), AOS_BANK_COMMON, e, 40.0f, 8.0f, AOS_BLEND_ADD_ALPHA, 255.0f * e->alpha);
			c->src[0] = (float)((v / 8) * 0xf0 + 0x78);
			c->src[2] = (float)((v / 8) * 0xf0 + 0xef);
			if (e->age >= 5)
				c->rgba = aos_rgba(1.0f, 128.0f / 255.0f, 1.0f, (float)(int)(255.0f * e->alpha) / 255.0f);
		}
		c->hasSrc = 1;
		c->src[1] = (float)((v % 8) * 0x40);
		c->src[3] = c->src[1] + 0x3f;
		break;
	}
	case 0xd2: {
		float s[4];
		cell_glow(e->variant, s);
		aos_dcmd *c = eq(aos_tex("beamsphere"), AOS_BANK_COMMON, e, 32.0f, 18.0f, AOS_BLEND_ADD_ALPHA, 32.0f * e->alpha);
		c->hasSrc = 1;
		memcpy(c->src, s, sizeof s);
		break;
	}
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* whip family (type 13): Bit trail ribbon                             */

static int whip_spawn(aos_world *w, int32_t id, int32_t ownerId, int32_t p3) /* FUN_007c6a80 */
{
	if (id != 5)
		return -1;
	int i = aos_whip_new(w, ID, 5, ownerId, p3); /* +0x08 ownerId: p+0x18 (texture slot) */
	if (i < 0)
		return -1;
	w->obj.whip[i].f10 = p3; /* +0x10: Bit enemy id */
	return i;
}

/* node[i]: [0..2] point B (+0x14), [3..5] point A (+0x20), [6] used flag (+0x2c) */
static bool whip_update(aos_world *w, aos_whip *h) /* FUN_007c6f00 */
{
	if (h->id != 5)
		return true;
	aos_enemy *e = aos_enemy_by_id(w, h->f10);
	if (!e)
		return false;
	float ax = -aos_cos(e->angle + HALF_PI) * 5.0f + e->pos[0]; /* DAT_00813f40 */
	float ay = -aos_sin(e->angle + HALF_PI) * 5.0f + e->pos[1];
	float bx = aos_cos(e->angle + HALF_PI) * 5.0f + e->pos[0];
	float by = aos_sin(e->angle + HALF_PI) * 5.0f + e->pos[1];
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

static void whip_draw(aos_world *w, aos_whip *h) /* FUN_007c6b70 */
{
	/* Original: one textured quad per history segment through the sprite batch at
	 * 0x87fe5c + p18*0x3c (nk_whip0): vertices A_k (tex 0, 12.8k), A_k+1 (0, 12.8(k+1)),
	 * B_k+1 (256, ...), B_k (256, ...), colour 0x80ffffff (identical to Iru's FUN_007c56f0).
	 * Texture x runs ACROSS the ribbon (A->B, width 10), y ALONG it. draw.h has no
	 * textured-strip primitive, so each segment is one rotated quad between the pair
	 * midpoints: exact while A_k-B_k is parallel to A_k+1-B_k+1, approximate when the Bit
	 * turned between two frames. Zero-length segments (Bit at rest) have zero area in the
	 * original and draw nothing. */
	if (h->id != 5)
		return;
	aos_enemy *e = aos_enemy_by_id(w, h->f10);
	aos_player *op = e ? aos_player_by_objid(w, e->ownerId) : NULL;
	int bank = op ? aos_pidx(w, op) : AOS_BANK_P0;
	for (int k = 0; k < AOS_WHIP_NODES - 1; k++) {
		const float *n0 = h->node[k], *n1 = h->node[k + 1];
		if (n0[6] == 0.0f || n1[6] == 0.0f)
			continue;
		float c0x = (n0[0] + n0[3]) * 0.5f, c0y = (n0[1] + n0[4]) * 0.5f;
		float c1x = (n1[0] + n1[3]) * 0.5f, c1y = (n1[1] + n1[4]) * 0.5f;
		float dx = c1x - c0x, dy = c1y - c0y;
		float len = aos_sqrt(dx * dx + dy * dy);
		if (len < 0.001f)
			continue;
		aos_dcmd *c = aos_draw(aos_tex("nk_whip0"), bank, (c0x + c1x) * 0.5f, (c0y + c1y) * 0.5f, 0);
		c->halfW = 5.0f;        /* across: |B - A| / 2 (DAT_00813f40) */
		c->halfH = len * 0.5f;  /* along */
		c->rotZ = aos_rad2bam(aos_atan2(n0[1] - n0[4], n0[0] - n0[3])); /* local x = A->B */
		c->hasSrc = 1;
		c->src[0] = 0;
		c->src[1] = (float)k * 12.8f; /* _DAT_00828324 */
		c->src[2] = 256.0f;           /* DAT_00814160 */
		c->src[3] = (float)(k + 1) * 12.8f;
		c->rgba = aos_rgba(1, 1, 1, 128.0f / 255.0f);
	}
}

/* ------------------------------------------------------------------ */
/* registration                                                        */

void aos_register_nanako(void)
{
	aos_tab.inputA[ID] = input_a;
	aos_tab.inputB[ID] = input_b;
	/* CPU AI FUN_007083a0 (level 0) / FUN_00707c60 (1, 2): not ported */

	aos_tab.T2[0x18d] = st_neutral;   aos_tab.T1[0x18d] = dr_neutral;
	aos_tab.T2[0x18e] = st_stub;      aos_tab.T1[0x18e] = dr_dash;
	aos_tab.T2[0x18f] = st_ko_hook;   aos_tab.T1[0x18f] = dr_ko;
	aos_tab.T2[0x190] = st_stub;      aos_tab.T1[0x190] = dr_hit;
	aos_tab.T2[0x191] = st_stub;      aos_tab.T1[0x191] = dr_sld_L5;
	aos_tab.T2[0x197] = st_intro;     aos_tab.T1[0x197] = dr_atkb_L5;
	aos_tab.T2[0x198] = st_tone;      aos_tab.T1[0x198] = dr_atka_C5;
	aos_tab.T2[0x199] = st_needle;    aos_tab.T1[0x199] = dr_sld_L5;
	aos_tab.T2[0x19a] = st_fruit;     aos_tab.T1[0x19a] = dr_sld_L5;
	aos_tab.T2[0x19b] = st_leaf;      aos_tab.T1[0x19b] = dr_atka_C5;
	aos_tab.T2[0x19c] = st_bug;       aos_tab.T1[0x19c] = dr_sld_L5;
	aos_tab.T2[0x19d] = st_shell;     aos_tab.T1[0x19d] = dr_sld_L5;
	aos_tab.T2[0x19e] = st_shell2;    aos_tab.T1[0x19e] = dr_sld_L5;
	aos_tab.T2[0x19f] = st_breath1;   aos_tab.T1[0x19f] = dr_atka_C5;
	aos_tab.T2[0x1a0] = st_breath_all; aos_tab.T1[0x1a0] = dr_sld_L5;
	aos_tab.T2[0x1a1] = st_sting;     aos_tab.T1[0x1a1] = dr_atkb_L5;
	aos_tab.T2[0x1a2] = st_crown;     aos_tab.T1[0x1a2] = dr_atkb_L5;
	aos_tab.T2[0x1a3] = st_storm;     aos_tab.T1[0x1a3] = dr_atkb_L5;

	aos_otab.enemySpawn[ID] = bit_spawn;
	aos_otab.enemyUpdate[ID] = bit_update;
	aos_otab.enemyDraw[ID] = bit_draw;
	aos_otab.enemyOnKilled[ID] = bit_killed;
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
	aos_otab.laserSpawn[ID] = laser_spawn;
	aos_otab.laserUpdate[ID] = laser_update;
	aos_otab.laserDraw[ID] = laser_draw;
	aos_otab.laserOnHit[ID] = laser_on_hit;
	aos_otab.effectSpawn[ID] = effect_spawn;
	aos_otab.effectUpdate[ID] = effect_update;
	aos_otab.effectDraw[ID] = effect_draw;
	aos_otab.whipSpawn[ID] = whip_spawn;
	aos_otab.whipUpdate[ID] = whip_update;
	aos_otab.whipDraw[ID] = whip_draw;
}
