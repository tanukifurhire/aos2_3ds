/* Sumika (engine id 17 = AOS_CH_SUMIKA) and her second form (engine id 18 =
 * AOS_CH_SUMIKA2, story-mode final boss) — 1:1 port of their code in aos2.exe.
 * Spec: docs/re/sumika.md (Part A = Sumika, Part B = second form). The two
 * characters have separate but almost identical function sets (type 0x11 vs
 * 0x12); they are ported once here, parametrised by the object/player type,
 * with every difference branched on `T == SB` and the original addresses of
 * both versions named. Only the version-0x5a2 branches are implemented.
 * Layout: 1 helpers, 2 input, 3 T2, 4 T1, 5 objects, 6 registration. */
#include <stddef.h>
#include <string.h>

#include "../draw.h"
#include "../fx.h"
#include "../sim.h"

#define SA AOS_CH_SUMIKA  /* 17 */
#define SB AOS_CH_SUMIKA2 /* 18 */
#define HALF_PI 1.5707964f

/* Global shared by both players and by both forms (DAT_0087faa4); the second
 * form's core pattern index DAT_009b0984 lives in its own slot. */
typedef struct {
	int32_t featherAlt; /* DAT_0087faa4 (Heaven's Feather alternation) */
} sa_global;
typedef struct {
	int32_t patternIdx; /* DAT_009b0984 */
} sb_global;
static sa_global *GA(aos_world *w) { return (sa_global *)w->charGlobal[SA]; }
static sb_global *GB(aos_world *w) { return (sb_global *)w->charGlobal[SB]; }

/* ================================================================== */
/* 1. helpers                                                          */

static aos_player *team_player(aos_world *w, int32_t team) { return &w->p[team == 2 ? 1 : 0]; } /* FUN_00780450 */
static int team_bank(int32_t team) { return team == 2 ? AOS_BANK_P1 : AOS_BANK_P0; }
static int32_t tcol(aos_world *w, int32_t team, int k) { return aos_team_colour(w, team, k); }

static int fx0(aos_world *w, int32_t id, int32_t variant, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team)
{
	return aos_spawn_effect(w, 0, id, variant, pos, angle, owner, age0, team);
}

static float dist2d(const float *a, const float *b) /* FUN_005f1c30 */
{
	float dx = a[0] - b[0], dy = a[1] - b[1];
	return aos_sqrt(dx * dx + dy * dy);
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

static bool can_affect(aos_world *w, aos_player *o) /* FUN_00780a50 */
{
	return o->barrierTime == 0 && o->state != AOS_ST_KO && o->hittable != 0 &&
	       o->statusOn[AOS_STATUS_STUNIMMUNE] != 1 && !w->roundOver;
}

/* FUN_006b1d70 proximity knock (trailing FUN_004a9f10 = clear the camera focus list) */
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

static void flourish00(aos_world *w, aos_player *p) /* SE 6, dark(100,.75), speedline(100,pos,0,0), blur(20,.5,2), 0xfa */
{
	aos_hyper_flourish_ex(w, p, 6, 100, 0.75f, 100, 0.0f, 0.0f, 0x14, 0.5f, 2, 1);
}

/* ================================================================== */
/* 2. input dispatch                                                   */

static bool st(aos_world *w, aos_player *p, int32_t s)
{
	aos_set_state(w, p, s);
	return true;
}

static int feather_count(aos_world *w, int32_t T, int32_t team); /* FUN_00768530 / FUN_0076d070 */
static int barrel_captured(aos_world *w, int32_t T, int32_t team); /* FUN_00768670 / FUN_0076d1b0 */

static bool normal_moves(aos_world *w, aos_player *p)
{
	bool b = p->id == SB;
	if (p->state == AOS_ST_NEUTRAL || p->state == AOS_ST_DASH) {
		if (p->D == 3) {
			aos_dash(w, p, 0);
		} else if (p->Shield == 3 && aos_spend_hyper(p, 0.5f) && aos_may_shield(w, p)) {
			aos_shield(w, p);
		} else {
			if (p->S > 1 && p->B == 3) return st(w, p, b ? 0x24a : 0x226);
			if (p->S > 1 && p->A == 3) return st(w, p, b ? 0x243 : 0x21f);
			if (p->B == 3) return st(w, p, b ? 0x248 : 0x224);
			if (p->D > 1 && p->A == 3) return st(w, p, b ? 0x242 : 0x21e);
			if (p->A == 3) return st(w, p, b ? 0x23f : 0x21b);
		}
	}
	return aos_tab.T2[p->state] ? aos_tab.T2[p->state](w, p) : true;
}

static bool coquettish_or_tropical(aos_world *w, aos_player *p)
{
	aos_player *o = aos_opponent(w, p);
	bool b = p->id == SB;
	if (o->statusOn[AOS_STATUS_MARK] == 1) {
		p->state = b ? 0x240 : 0x21c; /* original sets the state before touching the barrel */
		int e = barrel_captured(w, p->id, p->side);
		if (e != -1)
			w->obj.enemy[e].timer2 = 200;
		return st(w, p, p->state);
	}
	return st(w, p, b ? 0x249 : 0x225);
}

static bool input_a(aos_world *w, aos_player *p) /* FUN_0077a950 */
{
	if (aos_may_hyper(w, p)) {
		if (p->S > 1 && p->B > 1 && p->H == 3 && aos_spend_hyper(p, 3.0f))
			return st(w, p, feather_count(w, SA, p->side) < 9 ? 0x22a : 0x229);
		if (p->S > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f)) return st(w, p, 0x228);
		if (p->B > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f)) return coquettish_or_tropical(w, p);
		if (p->D > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f))
			return st(w, p, feather_count(w, SA, p->side) < 9 ? 0x220 : 0x221);
		if (p->H == 3 && aos_spend_hyper(p, 1.0f)) return st(w, p, 0x227);
	}
	return normal_moves(w, p);
}

static bool input_b(aos_world *w, aos_player *p) /* FUN_0077aec0 (scheme B) */
{
	if (aos_may_hyper(w, p)) {
		if (p->H > 1 && p->Shield == 3 && aos_spend_hyper(p, 3.0f))
			return st(w, p, feather_count(w, SA, p->side) < 9 ? 0x22a : 0x229);
		if (p->H > 1 && p->S == 3 && aos_spend_hyper(p, 1.0f)) return st(w, p, 0x228);
		if (p->H > 1 && p->B == 3 && aos_spend_hyper(p, 1.0f)) return coquettish_or_tropical(w, p);
		if (p->H > 1 && p->D == 3 && aos_spend_hyper(p, 1.0f))
			return st(w, p, feather_count(w, SA, p->side) < 9 ? 0x220 : 0x221);
		if (p->H > 1 && p->A == 3 && aos_spend_hyper(p, 1.0f)) return st(w, p, 0x227);
	}
	return normal_moves(w, p);
}

/* FUN_00771d10 — second form, both schemes (0x881358 and 0x8813a8).
 * Original: `if (DAT_0087faac == 3) return true;` (mode 3, probably training)
 * — the engine has no game-mode field; not reproduced (UNVERIFIED). */
static bool input_sb(aos_world *w, aos_player *p)
{
	if (aos_may_hyper(w, p)) {
		if (p->S > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f)) return st(w, p, 0x24c);
		if (p->B > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f)) return coquettish_or_tropical(w, p);
		if (p->D > 1 && p->H == 3 && aos_spend_hyper(p, 1.0f))
			return st(w, p, feather_count(w, SB, p->side) < 9 ? 0x244 : 0x245);
		if (p->H == 3 && aos_spend_hyper(p, 1.0f)) return st(w, p, 0x24b);
	}
	return normal_moves(w, p);
}

/* ================================================================== */
/* 3. T2 state handlers                                                */

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

static bool st_stub(aos_world *w, aos_player *p) /* 0x212..0x215, 0x237, 0x239 */
{
	(void)w, (void)p;
	return true;
}

/* second form: HP ≤ max/3 → final phase (0x235/0x236/0x238 tails) */
static bool sb_phase_check(aos_world *w, aos_player *p)
{
	if (p->maxHp / 3 < p->hp)
		return true;
	p->state = 0x24d; /* FUN_0076d240 writes only +0x150/+0x154 (hit flag kept) */
	p->timer = 0;
	return aos_tab.T2[0x24d](w, p);
}

static bool st_neutral(aos_world *w, aos_player *p) /* 0x211 FUN_00776c10 / 0x235 FUN_0076d240 */
{
	walk_mul(p, 1.0f);
	p->faceAngle = p->facingRight == 1 ? 0.0f : AOS_PI;
	if (p->id == SB)
		return sb_phase_check(w, p);
	return true;
}

static bool st_sb_hook(aos_world *w, aos_player *p) /* 0x236 FUN_0076d380 / 0x238 FUN_0076d3e0 */
{
	return sb_phase_check(w, p);
}

static int spawn_enemy(aos_world *w, aos_player *p, int32_t id)
{
	return aos_spawn_enemy(w, p->id, id, p->objId, p->side, p->color);
}

/* intro 0x21d FUN_00776d40 / 0x241 FUN_0076d460, Over Limit pair spawn (0x220/0x244) */
static void feather_pair(aos_world *w, aos_player *p, int k, int32_t slotBase, int32_t hp)
{
	for (int j = 0; j < 2; j++) {
		int e = spawn_enemy(w, p, k % 2 == 0 ? 100 : 0x6e);
		if (e < 0)
			continue;
		aos_enemy *en = &w->obj.enemy[e];
		en->pos[0] = p->x;
		en->pos[1] = p->y;
		en->pos[2] = p->z;
		en->angle = p->faceAngle;
		en->f84 = 0;
		en->timer2 = 0;
		en->scratch[0] = 10.0f; /* +0x74 (DAT_00813f4c) */
		en->maxHp = k * 2 + slotBase + j;
		en->hp = hp;
	}
	aos_se(w, 0x2f);
}

static bool st_intro(aos_world *w, aos_player *p) /* 0x21d / 0x241 */
{
	for (int k = 0; k < 4; k++)
		if (p->timer == k * 5 + 0x32)
			feather_pair(w, p, k, 1000, 9999);
	if (100 < p->timer)
		p->state = AOS_ST_NEUTRAL;
	p->hurtShape[0] = p->hurtShape[1] = p->hurtShape[2] = p->hurtShape[3] = 0; /* +0x5c..+0x68 */
	return true;
}

static void missile(aos_world *w, aos_player *p, int32_t id, const float pos[3], float ang)
{
	aos_spawn_missile(w, p->id, id, pos, ang, p->objId, 0, p->side, p->color);
}

static void whip_new(aos_world *w, int32_t T, int32_t kind, int32_t slot, int32_t target);

/* Pretty Sword 0x21b FUN_007781a0 / 0x23f FUN_0076e160 */
static bool sword_pair(aos_world *w, aos_player *p, float off, int32_t age)
{
	bool b = p->id == SB;
	if (!aos_use_gauge(p, b ? 0x23f : 0x21b))
		return false;
	p->faceAngle = aos_aim(w, p);
	for (int j = 0; j < 2; j++) {
		int m = aos_spawn_missile(w, p->id, j == 0 ? 100 : 0x96, &p->x, j == 0 ? p->faceAngle + off : p->faceAngle - off,
		                          p->objId, 0, p->side, p->color);
		if (m < 0)
			continue;
		w->obj.missile[m].age = age;
		whip_new(w, p->id, (b ? 0xd : 9) + j, p->color, w->obj.missile[m].serial);
	}
	aos_se(w, 0x10);
	return true;
}

static bool st_pretty_sword(aos_world *w, aos_player *p)
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer == 0xc && !sword_pair(w, p, 0.75f, 0x1e))
		p->timer = 100;
	if (p->timer == 0xc && p->A < 2)
		p->timer = 100;
	if (p->timer == 0x11 && !sword_pair(w, p, -1.25f, 0xf)) /* 100 at aim−1.25, 0x96 at aim+1.25 */
		p->timer = 100;
	if (p->timer == 0x11 && p->A < 2)
		p->timer = 100;
	if (p->timer == 0x16 && !sword_pair(w, p, 1.75f, 0))
		p->timer = 100;
	if (p->timer == 0x16 && p->A < 2)
		p->timer = 100;
	if (p->timer == 0x19)
		p->timer = 100;
	end_move(w, p, 0x69, 0x7d);
	return true;
}

/* ---- feather launchers ---- */
static int launch_fantastic(aos_world *w, int32_t T, int32_t team, bool hyper); /* FUN_00767630 / FUN_0076c780 */
static int launch_shiny(aos_world *w, int32_t T, int32_t team, bool hyper);     /* FUN_00767a70 / FUN_0076cbc0 */
static int launch_eternal(aos_world *w, int32_t T, int32_t team, bool hyper);   /* FUN_007680a0 */

static bool st_fantastic(aos_world *w, aos_player *p) /* 0x21e FUN_00777750 / 0x242 FUN_0076de30 */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer > 9 && p->timer < 0x16)
		walk_mul(p, 0.5f);
	if (p->timer == 0xf) {
		if (launch_fantastic(w, p->id, p->side, false) == -1) {
			p->timer = 0x16;
		} else {
			aos_use_gauge(p, p->id == SB ? 0x242 : 0x21e);
			p->faceAngle = aos_aim(w, p);
			aos_se_cd(w, 0x2f, 4);
		}
	}
	if (p->timer == 0x15 && p->A > 1)
		p->timer = 0xc;
	else if (end_move(w, p, 0x19, 0x23))
		p->friction *= 0.25f;
	return true;
}

static bool st_shiny(aos_world *w, aos_player *p) /* 0x21f FUN_00777980 / 0x243 FUN_0076e050 */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer == 10) {
		if (launch_shiny(w, p->id, p->side, false) == -1) {
			p->timer = 0x14;
		} else {
			aos_use_gauge(p, p->id == SB ? 0x243 : 0x21f);
			p->faceAngle = aos_aim(w, p);
			aos_se_cd(w, 0x2f, 4);
		}
	}
	if (p->timer == 0x12 && p->A > 1)
		p->timer = 8;
	else
		end_move(w, p, 0x16, 0x1e);
	return true;
}

static void hyper_start(aos_world *w, aos_player *p)
{
	face_by_x(w, p);
	aos_barrier(w, p, 10);
	flourish00(w, p);
}

static bool st_over_limit(aos_world *w, aos_player *p) /* 0x220 FUN_00777040 / 0x244 FUN_0076d750 */
{
	if (p->timer == 0)
		hyper_start(w, p);
	for (int k = 0; k < 4; k++)
		if (p->timer == k * 8 + 10)
			feather_pair(w, p, k, 0x44c, 0x69);
	end_move(w, p, 0x3c, 0x50);
	return true;
}

/* delete own feather whips (literal: compares the whip's target field with the player id) */
static void remove_feather_whips(aos_world *w, aos_player *p)
{
	int32_t k0 = p->id == SB ? 0xb : 7;
	for (int i = 0; i < w->obj.nwhip; i++) {
		aos_whip *h = &w->obj.whip[i];
		if (h->f10 == p->objId && (h->id == k0 || h->id == k0 + 1)) {
			memmove(&w->obj.whip[i], &w->obj.whip[i + 1], sizeof w->obj.whip[0] * (size_t)(w->obj.nwhip - i - 1));
			AOS_CLEAR_DEAD(&w->obj.whip[w->obj.nwhip - 1], sizeof w->obj.whip[0]);
			w->obj.nwhip--;
			i--;
		}
	}
}

static bool st_heavens(aos_world *w, aos_player *p) /* 0x221 FUN_00777470 / 0x245 FUN_0076db70 */
{
	if (p->timer == 0) {
		hyper_start(w, p);
		remove_feather_whips(w, p);
		GA(w)->featherAlt = 0;
	}
	if (p->timer > 0xb && p->timer < 0x3d && p->timer % 2 == 0) {
		if (GA(w)->featherAlt % 2 == 0)
			launch_fantastic(w, p->id, p->side, true);
		else
			launch_shiny(w, p->id, p->side, true);
		aos_se_cd(w, 0x2f, 4);
		GA(w)->featherAlt++;
	}
	end_move(w, p, 0x3c, 0x50);
	return true;
}

static bool st_heartful(aos_world *w, aos_player *p) /* 0x222 FUN_007794f0 / 0x246 FUN_0076f500 */
{
	bool b = p->id == SB;
	int last = b ? 0x1c : 0x1d;
	if (p->timer > 0xb && p->timer <= last && aos_use_gauge(p, b ? 0x246 : 0x222)) {
		float x = -aos_rand_real(w, 0.0f, 150.0f);   /* DAT_0081414c */
		float y = aos_rand_real(w, -180.0f, 180.0f); /* DAT_008254a4, DAT_00815258 */
		float v[2] = {x, y};
		aos_vec_rotate(v, p->faceAngle);
		float pt[3] = {v[0] + p->x, v[1] + p->y, p->z}; /* z: uninitialised local_c in the original; z is never read by 2D collision/movement */
		int32_t id = p->timer % 2 == 0 ? 200 : 0xfa;
		missile(w, p, id, pt, p->faceAngle);
		int e = spawn_enemy(w, p, id);
		if (e >= 0) {
			w->obj.enemy[e].pos[0] = pt[0];
			w->obj.enemy[e].pos[1] = pt[1];
			w->obj.enemy[e].pos[2] = pt[2];
			w->obj.enemy[e].angle = p->faceAngle;
		}
		aos_se_cd(w, 0xe, 4);
	}
	end_move(w, p, 0x1e, 0x32);
	return true;
}

static bool st_marble(aos_world *w, aos_player *p) /* 0x224 FUN_00778b60 / 0x248 FUN_0076ead0 */
{
	bool b = p->id == SB;
	if (p->timer == 0 || p->timer == 0xe)
		face_by_x(w, p);
	if (p->timer < 0xd && p->B < 2) {
		p->faceAngle = aos_aim(w, p);
		p->state = b ? 0x246 : 0x222;
		return aos_tab.T2[p->state](w, p);
	}
	if (p->timer > 0xd && p->timer < 0x16 && aos_use_gauge(p, b ? 0x248 : 0x224)) {
		float r = aos_rand_real(w, -0.62831855f, 0.62831855f); /* DAT_00826428, DAT_00826418 */
		missile(w, p, 300, &p->x, r + HALF_PI);
		aos_se_cd(w, 0x2f, 3);
	}
	end_move(w, p, 0x19, 0x32);
	return true;
}

static bool st_tropical(aos_world *w, aos_player *p) /* 0x225 FUN_00778d70 / 0x249 FUN_0076ecd0 */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		aos_barrier(w, p, 10);
		flourish00(w, p);
	}
	if (p->id == SA) {
		static const float O[6] = {-0.75f, -0.5f, -0.25f, 0.75f, 0.5f, 0.25f}; /* ver > 0x5a0 */
		for (int k = 0; k < 6; k++)
			if (p->timer == k * 5 + 10) {
				missile(w, p, 0x15e, &p->x, ((float)(k % 4) / 3.0f) * AOS_2PI + O[k]);
				aos_se_cd(w, 0x2f, 3);
			}
	} else {
		int n, step;
		float div;
		if (w->difficulty == 2) n = 8, step = 4, div = 4.0f;
		else if (w->difficulty == 1) n = 6, step = 5, div = 3.0f;
		else if (w->difficulty == 0) n = 5, step = 6, div = 5.0f;
		else n = 0, step = 1, div = 1.0f;
		for (int k = 0; k < n; k++)
			if (p->timer == k * step + 10) {
				float base = ((float)(k % 4) / div) * AOS_2PI;
				float r = aos_rand_real(w, -0.75f, 0.75f);
				missile(w, p, 0x15e, &p->x, base + r);
				aos_se_cd(w, 0x2f, 3);
			}
	}
	end_move(w, p, 0x32, 0x46);
	return true;
}

static int barrel_idle(aos_world *w, int32_t T, int32_t team); /* FUN_007685e0 / FUN_0076d120 */

static bool st_barrel(aos_world *w, aos_player *p) /* 0x226 FUN_007790a0 / 0x24a FUN_0076f0c0 */
{
	bool b = p->id == SB;
	if (p->timer == 0) {
		face_by_x(w, p);
		p->speed = 0;
	}
	if (p->timer == 5 && aos_use_gauge(p, b ? 0x24a : 0x226)) {
		int e = spawn_enemy(w, p, 300);
		if (e >= 0) {
			w->obj.enemy[e].pos[0] = p->x;
			w->obj.enemy[e].pos[1] = p->y + 100.0f;
			w->obj.enemy[e].angle = p->faceAngle;
		}
		aos_se(w, 0x39);
	}
	if (p->timer == 0xf) {
		int e = barrel_idle(w, p->id, p->side);
		if (e != -1) {
			float a;
			if (p->faceAngle <= HALF_PI) {
				a = 0.7853982f;                       /* DAT_0081fa90 */
				if (p->up > 1) a = 1.1780972f;        /* DAT_0082641c */
				if (p->down > 1) a = 0.5235988f;      /* DAT_00817ddc */
			} else {
				a = 2.3561945f;                       /* DAT_0081fa94 */
				if (p->up > 1) a = 1.9634955f;        /* DAT_00826424 */
				if (p->down > 1) a = 2.6179938f;      /* DAT_00825e78 */
			}
			aos_enemy *en = &w->obj.enemy[e];
			en->angle2 = a;
			en->angle = a;
			en->f70 = aos_sin(a) * 10.0f;      /* vy */
			en->scratch[0] = aos_cos(a) * 10.0f; /* vx (+0x74) */
			en->f84 = 200;
			en->timer2 = 0;
			aos_se(w, 0x23);
		}
	}
	end_move(w, p, 0x19, 0x28);
	return true;
}

static bool st_mystic(aos_world *w, aos_player *p) /* 0x227 FUN_00779820 / 0x24b FUN_0076f800 */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		aos_barrier(w, p, 10);
		flourish00(w, p);
	}
	if (p->timer == 10) {
		aos_spawn_laser(w, p->id, 100, &p->x, p->faceAngle, p->objId, p->side, p->color);
		aos_spawn_laser(w, p->id, 0x96, &p->x, p->faceAngle, p->objId, p->side, p->color);
		aos_se(w, 0x10);
	}
	if (p->timer > 10 && p->timer < 0x3c)
		walk_mul(p, 0.5f);
	end_move(w, p, 0x3c, 0x50);
	return true;
}

static bool st_coquettish(aos_world *w, aos_player *p) /* 0x21c FUN_00778790 / 0x240 FUN_0076e710 */
{
	if (p->timer == 0)
		hyper_start(w, p);
	if (p->timer > 5 && p->timer < 0x2f && p->timer % 2 == 0) {
		aos_player *o = aos_opponent(w, p);
		float a = aos_rand_real(w, -AOS_PI, AOS_PI);
		float v[2] = {aos_rand_real(w, 100.0f, 240.0f), 0.0f};
		aos_vec_rotate(v, a);
		float pt[3] = {v[0] + o->x, v[1] + o->y, p->z};
		missile(w, p, (p->timer / 2) % 2 == 0 ? 0x66 : 0x98, pt, a + AOS_PI);
		aos_se_cd(w, 0x10, 4);
	}
	end_move(w, p, 0x50, 0x3c); /* literal: dash cancel never reached */
	return true;
}

static void spark_at(aos_world *w, aos_player *p, float y, float aux2)
{
	float pt[3] = {p->x, p->y - y, p->z};
	int32_t col = tcol(w, p->side, p->timer % 6 == 0 ? 0 : 1);
	float r = aos_rand_real(w, -0.25f, 0.25f);
	int32_t k = aos_rand_int(w, 0, 100) % 8;
	int e = fx0(w, 0xa82 + k, col, pt, r + HALF_PI, p->objId, 0, p->side);
	if (e >= 0)
		w->obj.effect[e].aux2 = aux2;
}

static void ring_particle(aos_world *w, aos_player *p, int32_t kind, int k, float lo, float hi, float yoff)
{
	float a = aos_rand_real(w, -AOS_PI, AOS_PI);
	float d = aos_rand_real(w, lo, hi);
	float pt[3] = {aos_cos(a) * d + p->x, (aos_sin(a) * d * 0.25f + p->y) - yoff, p->z};
	aos_spawn_particle(w, kind, tcol(w, p->side, k), pt, HALF_PI, p->objId, 0, 1);
}

static bool st_angel_wing(aos_world *w, aos_player *p) /* 0x228 FUN_00779f00 / 0x24c FUN_0076fac0 */
{
	int t = p->timer;
	if (t == 0) {
		face_by_x(w, p);
		p->speed = 0;
		aos_barrier(w, p, 10);
		flourish00(w, p);
	}
	if (t > 5 && t < 0x33 && t % 2 == 0) {
		bool q = t % 4 == 0;
		aos_spawn_effect(w, p->id, q ? 700 : 0x2bd, 0, &p->x, -HALF_PI, p->objId, 0, p->side);
		aos_spawn_effect(w, p->id, q ? 0x2ef : 0x2ee, 0, &p->x, -HALF_PI, p->objId, 0, p->side);
	}
	int lo = p->id == SB ? 10 : 0xf;
	float l[3] = {p->x + 130.0f, p->y - 20.0f, p->z}, r[3] = {p->x - 130.0f, p->y - 20.0f, p->z}; /* DAT_0081ce58, DAT_008140fc */
	if (t < lo || t > 0x28 || t % 5 != 0) {
		if (t >= lo && t < 0x29) {
			aos_spawn_hitbox(w, p->id, 0x137, l, 0.0f, p->objId, 0, p->side);
			aos_spawn_hitbox(w, p->id, 0x137, r, 0.0f, p->objId, 0, p->side);
		}
	} else {
		int id = t % 10 == 0 ? 0x136 : 0x168;
		aos_spawn_hitbox(w, p->id, id, l, 0.0f, p->objId, 0, p->side);
		aos_spawn_hitbox(w, p->id, id, r, 0.0f, p->objId, 0, p->side);
	}
	if (t > 3 && t < 0x41 && t % 3 == 0)
		spark_at(w, p, 100.0f, 0.35f); /* DAT_00813f2c */
	if (t > 8 && t < 0x41 && t % 2 == 0)
		ring_particle(w, p, 1000, 1, 30.0f, 150.0f, 100.0f);
	if (t > 9 && t < 0x47)
		ring_particle(w, p, 0x44c, 0, 50.0f, 200.0f, 100.0f);
	if (t == 0x2d) {
		prox_knock(w, p, 240.0f, 20.0f);
		fx0(w, 0xa8c, 0, &p->x, 0.0f, p->objId, 0, p->side);
	}
	end_move(w, p, 0x37, 0x50);
	return true;
}

static bool st_eternal(aos_world *w, aos_player *p) /* 0x229 FUN_00777aa0 ☆ Eternal Feather */
{
	int t = p->timer;
	if (t == 0) {
		face_by_x(w, p);
		p->speed = 0;
		aos_barrier(w, p, 0x14);
		aos_hyper_flourish_ex(w, p, 7, 0x96, 0.85f, 100, p->faceAngle, 0.0f, 0x96, 0.6f, 2, 1);
		remove_feather_whips(w, p);
	}
	if (t > 0xb && t < 0x3d && t % 2 == 0) {
		launch_eternal(w, p->id, p->side, true);
		aos_se_cd(w, 0x2f, 4);
	}
	if (t > 3 && t < 0x41 && t % 3 == 0)
		spark_at(w, p, 50.0f, 0.25f); /* DAT_008140ec, DAT_00813f24 */
	if (t > 8 && t < 0x41 && t % 2 == 0)
		ring_particle(w, p, 1000, 1, 20.0f, 100.0f, 50.0f);
	if (t > 9 && t < 0x47)
		ring_particle(w, p, 0x44c, 0, 20.0f, 140.0f, 50.0f);
	end_move(w, p, 0x50, 0x78);
	return true;
}

static bool st_pretty_justice(aos_world *w, aos_player *p) /* 0x22a FUN_00779af0 */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		p->speed = 0;
		aos_barrier(w, p, 0x14);
		aos_hyper_flourish_ex(w, p, 7, 0x96, 0.85f, 100, p->faceAngle, 0.7853982f, 0x96, 0.6f, 2, 1);
	}
	if (p->timer == 0x14) {
		static const int32_t ids[6] = {200, 0xc9, 0xca, 0xfa, 0xfb, 0xfc};
		for (int k = 0; k < 6; k++)
			aos_spawn_laser(w, SA, ids[k], &p->x, p->faceAngle, p->objId, p->side, p->color);
		aos_se(w, 0x10);
	}
	if (p->timer > 10 && p->timer < 100)
		walk_mul(p, 0.5f);
	end_move(w, p, 0x78, 0x96);
	return true;
}

/* ---- second form, final phase ---- */

static bool st_sb_transition(aos_world *w, aos_player *p) /* 0x24d FUN_007704e0 */
{
	if (p->timer == 0) {
		p->dashAngle = aos_atan2(-p->y, -p->x);
		p->f90 = 0;
		float d = aos_sqrt(p->x * p->x + p->y * p->y) / 100.0f; /* +0x90 (float in the original) */
		memcpy(&p->f90, &d, sizeof d);
		p->speed = 0;
		p->faceAngle = 0;
		aos_barrier(w, p, 0x14);
		p->barrierTime = 400;
		aos_hyper_flourish_ex(w, p, 7, 19999, 0.75f, 100, p->faceAngle, 0.0f, 100, 0.75f, 2, 1);
		aos_status(w, p, AOS_STATUS_STUNIMMUNE, 99999);
	}
	float d;
	memcpy(&d, &p->f90, sizeof d);
	p->x = aos_cos(p->dashAngle) * d + p->x;
	p->y = aos_sin(p->dashAngle) * d + p->y;
	if (p->timer < 100) {
		p->hurtShape[0] = p->hurtShape[1] = p->hurtShape[2] = 0;
		return true;
	}
	p->state = 0x24e; /* state/timer only, then T2 */
	p->timer = 0;
	return aos_tab.T2[0x24e](w, p);
}

static void sb_laser300(aos_world *w, aos_player *p, float a)
{
	float pt[3] = {aos_cos(a) * 100.0f + p->x, aos_sin(a) * 100.0f + p->y, p->z};
	aos_spawn_laser(w, SB, 300, pt, a, p->objId, p->side, p->color);
	fx0(w, 0xaf0, 0xd, pt, 0.0f, p->objId, 0, p->side);
}

static void sb_tentacle(aos_world *w, aos_player *p, float a, float turn)
{
	float pt[3] = {aos_cos(a) * 50.0f + p->x, aos_sin(a) * 50.0f + p->y, p->z};
	int m = aos_spawn_missile(w, SB, 600, pt, a, p->objId, 0, p->side, p->color);
	if (m >= 0)
		w->obj.missile[m].f74 = turn;
	aos_se(w, 0x22);
}

static bool st_sb_final(aos_world *w, aos_player *p) /* 0x24e FUN_00770750 */
{
	int t = p->timer, d = w->difficulty;
	if (t == 0) {
		p->faceAngle = 0;
		int e = spawn_enemy(w, p, 500);
		if (e >= 0) {
			aos_enemy *en = &w->obj.enemy[e];
			en->pos[0] = p->x;
			en->pos[1] = p->y;
			en->angle2 = p->faceAngle;
			en->angle = p->faceAngle;
		}
		aos_se(w, 0x39);
		aos_se(w, 0x33);
	}
	if (d == 2) {
		int m = t % 300;
		if (299 < t && (m == 1 || m == 0x1f || m == 0x3d || m == 0x5b)) {
			bool plus = m == 1 || m == 0x3d;
			float a = aos_aim(w, p);
			float r = aos_rand_real(w, 0.0f, 0.75f);
			sb_tentacle(w, p, plus ? a + r : a - r, plus ? -1.0f : 1.0f);
		}
	} else if (299 < t && (t % 300 == 1 || t % 300 == 0x33)) {
		bool plus = t % 300 == 1;
		float a = aos_aim(w, p);
		float r = aos_rand_real(w, 0.0f, 0.75f);
		sb_tentacle(w, p, plus ? a + r : a - r, plus ? -1.0f : 1.0f);
	}
	if (d != 0) {
		if ((d == 1 && 999 < t && t % 0xfa == 1) || (d != 1 && 999 < t && t % 200 == 1)) {
			float r = aos_rand_real(w, -AOS_PI, AOS_PI);
			missile(w, p, 0x15e, &p->x, r);
			aos_se_cd(w, 0x2f, 3);
		}
	}
	if (p->maxHp / 6 < p->hp) {
		bool fire;
		if (d == 0) fire = t % 0x32 == 0;
		else if (d == 1) fire = 0x45 < t % 100 && t % 10 == 0;
		else fire = 0x45 < t % 100 && t % 6 == 0;
		if (fire)
			sb_laser300(w, p, aos_rand_real(w, -AOS_PI, AOS_PI));
	} else {
		bool fire, plus;
		float spread;
		if (d == 0) fire = 199 < t % 0xfa && t % 10 == 0, plus = t % 0x14 == 0, spread = 0.5f;
		else if (d == 1) fire = 0x95 < t % 200 && t % 10 == 0, plus = t % 0x14 == 0, spread = 0.5f;
		else fire = 0x95 < t % 200 && t % 6 == 0, plus = t % 0xc == 0, spread = 0.75f;
		if (fire) {
			float a = aos_aim(w, p);
			float r = aos_rand_real(w, 0.0f, spread);
			sb_laser300(w, p, plus ? a + r : a - r);
		}
	}
	if (t % 1000 == 900) {
		int n = d == 0 ? 4 : d == 1 ? 6 : 8;
		for (int k = 0; k < n; k++) {
			int e = spawn_enemy(w, p, k % 2 == 0 ? 700 : 0x2c6);
			if (e >= 0)
				w->obj.enemy[e].angle2 = ((float)k / (float)n) * AOS_2PI; /* DAT_00853b68 / 00813f44 / 008140d4 */
		}
	}
	return true;
}

/* ================================================================== */
/* 4. T1 draw handlers                                                 */

static int imin(int a, int b) { return a < b ? a : b; }
static int pingpong(int t, int n) { int f = (t / n) & 3; return f == 3 ? 1 : f; }
static const char *pfx(const aos_player *p) { return p->id == SB ? "sab_" : "sa_"; }

static aos_dcmd *body(aos_world *w, aos_player *p, const char *name, int frame)
{
	char pat[32];
	int n = 0;
	for (const char *s = pfx(p); *s; s++) pat[n++] = *s;
	for (const char *s = name; *s && n < 26; s++) pat[n++] = *s;
	pat[n++] = '%', pat[n++] = '0', pat[n++] = '2', pat[n++] = 'd', pat[n] = 0;
	aos_dcmd *c = aos_draw(aos_texf(pat, frame), aos_pidx(w, p), p->x, p->y, 0);
	c->halfW = c->halfH = 48.0f; /* S = 96 */
	c->blend = AOS_BLEND_ALPHA;
	return c;
}

static aos_dcmd *aim_body(aos_world *w, aos_player *p, const char *name, int frame, int32_t rot[3])
{
	aos_dcmd *c = body(w, p, name, frame);
	c->flipX = 1;
	c->rotZ = aos_rad2bam(p->faceAngle);
	bool tilt = aos_fabs(p->faceAngle) >= HALF_PI;
	if (tilt)
		c->sy = -1.0f;
	if (rot) {
		rot[0] = tilt ? aos_deg2bam(0xb4) : 0;
		rot[1] = aos_deg2bam(0xb4);
		rot[2] = aos_rad2bam(p->faceAngle);
	}
	return c;
}

static void dr_neutral(aos_world *w, aos_player *p) /* 0x211 FUN_0077c540 / 0x235 FUN_00773300 */
{
	const char *n = "std";
	if (p->up > 1) n = "fwd";
	if (p->down > 1) n = "bck";
	if (!p->facingRight && p->left > 1) n = "fwd";
	if (p->facingRight && p->left > 1) n = "bck";
	if (!p->facingRight && p->right > 1) n = "bck";
	if (p->facingRight && p->right > 1) n = "fwd";
	aos_dcmd *c = body(w, p, n, pingpong(p->timer, 5));
	c->flipX = p->facingRight != 0;
	int32_t rot[3] = {0, p->facingRight ? aos_deg2bam(0xb4) : 0, 0};
	aos_afterimage(w, c, 1, p, rot);
}

static void dr_dash(aos_world *w, aos_player *p) /* 0x212 FUN_0077c7f0 / 0x236 FUN_007735b0 */
{
	bool against = (p->facingRight == 1 && aos_fabs(p->faceAngle) > HALF_PI) ||
	               (p->facingRight == 0 && aos_fabs(p->faceAngle) < HALF_PI);
	int f = pingpong(p->timer, 3) + (against ? 3 : 0);
	int32_t rot[3];
	aos_dcmd *c = aim_body(w, p, "dsh", f, rot);
	aos_afterimage(w, c, 1, p, rot);
}

static void dr_ko(aos_world *w, aos_player *p) /* 0x213 FUN_0077d150 / 0x237 FUN_00773f10 */
{
	if (p->timer < 100)
		aim_body(w, p, "los", pingpong(p->timer, 3) + (w->frame % 2 == 0 ? 3 : 0), NULL);
}

static void dr_hit(aos_world *w, aos_player *p) /* 0x214 FUN_0077cbf0 / 0x238 FUN_007739b0 */
{
	aim_body(w, p, "los", 0, NULL);
}

static void dr_sld(aos_world *w, aos_player *p) /* 0x215 … (sprite 0x215 / 0x239) */
{
	aim_body(w, p, "sld", imin(p->timer / 4, 4), NULL);
}

static void dr_acta(aos_world *w, aos_player *p) /* 0x21c 0x220 0x221 0x228 0x229 / 0x240 0x244 0x245 0x24c 0x24d 0x24e */
{
	aim_body(w, p, "acta", pingpong(p->timer, 4), NULL);
}

static void dr_ball15(aos_world *w, aos_player *p) /* 0x226 FUN_0077f010 / 0x24a FUN_00775b00 */
{
	int f = p->timer < 0xf ? 0 : imin((p->timer - 0xf) / 4 + 1, 5);
	aim_body(w, p, "ball", f, NULL);
}

static void dr_ball10(aos_world *w, aos_player *p) /* 0x227/0x22a FUN_0077f5b0/0077f8b0, 0x24b FUN_007760a0 */
{
	int f = 0;
	if (p->timer >= 10) {
		int q = (p->timer - 10) / 4;
		f = q + 1;
		if (f > 2)
			f = ((q - 2) & 3) + 3;
		if (f == 6)
			f = 4;
	}
	aim_body(w, p, "ball", f, NULL);
}

/* ================================================================== */
/* 5. objects                                                          */

static aos_dcmd *blit(int tex, int bank, const float pos[3], float hw, float hh, float angle,
                      int alpha, uint32_t rgb, int blend)
{
	aos_dcmd *c = aos_draw(tex, bank, pos[0], pos[1], 0);
	c->halfW = hw < 0 ? -hw : hw;
	c->halfH = hh;
	c->flipX = hw < 0;
	c->rotZ = aos_rad2bam(angle);
	c->blend = (uint8_t)blend;
	c->rgba = (rgb & 0xffffffu) << 8 | (uint32_t)(alpha < 0 ? 0 : alpha > 255 ? 255 : alpha);
	return c;
}

static int texT(int32_t T, const char *name, int frame) /* "sa_"/"sab_" + name pattern */
{
	char pat[40];
	int n = 0;
	for (const char *s = T == SB ? "sab_" : "sa_"; *s; s++) pat[n++] = *s;
	for (const char *s = name; *s && n < 38; s++) pat[n++] = *s;
	pat[n] = 0;
	return aos_texf(pat, frame);
}

/* ---- whips (visual ribbons): spawn FUN_007c96f0 / FUN_007c8920, update FUN_007c9f60 / 7c9190,
 *      draw FUN_007c9880 / 7c8ab0. Kinds 7/8 (0xb/0xc) follow a feather, 9/10 (0xd/0xe) a sword. ---- */

static void whip_new(aos_world *w, int32_t T, int32_t kind, int32_t slot, int32_t target)
{
	int i = aos_whip_new(w, T, kind, slot, 0);
	if (i >= 0)
		w->obj.whip[i].f10 = target; /* +0x10 */
}

static int whip_spawn_a(aos_world *w, int32_t id, int32_t ownerId, int32_t p3)
{
	int i = aos_whip_new(w, SA, id, ownerId, 0);
	if (i >= 0) w->obj.whip[i].f10 = p3;
	return i;
}
static int whip_spawn_b(aos_world *w, int32_t id, int32_t ownerId, int32_t p3)
{
	int i = aos_whip_new(w, SB, id, ownerId, 0);
	if (i >= 0) w->obj.whip[i].f10 = p3;
	return i;
}

static void whip_push(aos_whip *h, const float a[2], const float b[2])
{
	for (int k = AOS_WHIP_NODES - 1; k >= 0; k--)
		if (h->node[k][6] == 0.0f) {
			h->node[k][3] = a[0], h->node[k][4] = a[1];
			h->node[k][0] = b[0], h->node[k][1] = b[1];
			h->node[k][6] = 1.0f;
			return;
		}
	for (int k = AOS_WHIP_NODES - 1; k > 0; k--)
		for (int j = 0; j < 6; j++)
			h->node[k][j] = h->node[k - 1][j];
	h->node[0][3] = a[0], h->node[0][4] = a[1];
	h->node[0][0] = b[0], h->node[0][1] = b[1];
}

static aos_missile *missile_by_serial(aos_world *w, int32_t serial) /* FUN_00685ce0 */
{
	for (int i = 0; i < w->obj.nmissile; i++)
		if (w->obj.missile[i].serial == serial)
			return &w->obj.missile[i];
	return NULL;
}

static bool whip_update(aos_world *w, aos_whip *h)
{
	int kind = h->type == SB ? h->id - 4 : h->id; /* 0xb..0xe -> 7..10 */
	float a[2], b[2];
	if (kind == 7 || kind == 8) {
		aos_enemy *e = aos_enemy_by_id(w, h->f10);
		if (!e || e->f84 == 100)
			return false;
		float ang = aos_atan2(e->pos[1] - e->prevPos[1], e->pos[0] - e->prevPos[0]);
		a[0] = -aos_cos(ang + HALF_PI) * 4.0f + e->pos[0]; /* DAT_00853b68 */
		a[1] = -aos_sin(ang + HALF_PI) * 4.0f + e->pos[1];
		b[0] = aos_cos(ang + HALF_PI) * 4.0f + e->pos[0];
		b[1] = aos_sin(ang + HALF_PI) * 4.0f + e->pos[1];
	} else if (kind == 9 || kind == 10) {
		aos_missile *m = missile_by_serial(w, h->f10);
		if (!m)
			return false;
		a[0] = -aos_cos(m->angle + HALF_PI) * 7.5f + m->pos[0]; /* DAT_00814150 */
		a[1] = -aos_sin(m->angle + HALF_PI) * 7.5f + m->pos[1];
		b[0] = aos_cos(m->angle + HALF_PI) * 7.5f + m->pos[0];
		b[1] = aos_sin(m->angle + HALF_PI) * 7.5f + m->pos[1];
	} else {
		return true;
	}
	whip_push(h, a, b);
	return true;
}

static void whip_draw(aos_world *w, aos_whip *h) /* FUN_007c9880 / FUN_007c8ab0: textured strip */
{
	/* Each segment k..k+1 is a quad (B_k, B_k+1, A_k+1, A_k) of the whip texture (DAT_0087fe48[kind][slot]:
	 * sa_whipr / sa_whipb, 256x256), u 0..256 across, v = k*12.8 (_DAT_00828324) along, colour 0x80ffffff.
	 * Port: one rotated rect per segment (exact when the strip is straight; quad skew not representable). */
	int kind = h->type == SB ? h->id - 4 : h->id;
	bool red = kind == 7 || kind == 9;
	int tex = aos_tex(h->type == SB ? (red ? "sab_whipr" : "sab_whipb") : (red ? "sa_whipr" : "sa_whipb"));
	aos_player *own = NULL;
	for (int i = 0; i < 2; i++)
		if (w->p[i].color == h->ownerId && w->p[i].id == h->type)
			own = &w->p[i];
	int bank = own ? aos_pidx(w, own) : AOS_BANK_P0;
	for (int k = 0; k < AOS_WHIP_NODES - 1; k++) {
		if (h->node[k][6] == 0.0f || h->node[k + 1][6] == 0.0f)
			continue;
		float x0 = (h->node[k][0] + h->node[k][3]) * 0.5f, y0 = (h->node[k][1] + h->node[k][4]) * 0.5f;
		float x1 = (h->node[k + 1][0] + h->node[k + 1][3]) * 0.5f, y1 = (h->node[k + 1][1] + h->node[k + 1][4]) * 0.5f;
		float wx = h->node[k][0] - h->node[k][3], wy = h->node[k][1] - h->node[k][4];
		float dx = x1 - x0, dy = y1 - y0, len = aos_sqrt(dx * dx + dy * dy);
		if (len <= 0.0f)
			continue;
		aos_dcmd *c = aos_draw(tex, bank, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 0);
		c->halfW = aos_sqrt(wx * wx + wy * wy) * 0.5f;
		c->halfH = len * 0.5f;
		c->rotZ = aos_rad2bam(aos_atan2(dy, dx) + 1.5707964f);
		c->rgba = 0xffffff80u;
		c->blend = AOS_BLEND_ALPHA; /* the original sets no blend mode here (inherits) */
		c->hasSrc = 1;
		c->src[0] = 0;
		c->src[1] = (float)k * 12.8f;
		c->src[2] = 255.0f;
		c->src[3] = (float)(k + 1) * 12.8f;
	}
}

/* ---- enemies: spawn FUN_00551160 / FUN_00551f70, update FUN_0055add0 / FUN_00553760,
 *      draw FUN_0055d070 / FUN_00556040, killed FUN_0055cbf0 / FUN_00555bc0 ---- */

static int enemy_spawn_t(aos_world *w, int32_t T, int32_t id, int32_t owner, int32_t team, int32_t p18)
{
	switch (id) {
	case 100: case 0x6e: case 200: case 0xfa: case 300: case 500: case 600: case 0x28a: case 700: case 0x2c6: break;
	default: return -1;
	}
	int32_t grp = aos_enemy_group_next(w); /* FUN_0055efc0 */
	int i = aos_enemy_new(w, T, owner, team, p18);
	if (i < 0)
		return -1;
	aos_enemy *e = &w->obj.enemy[i];
	e->serial = grp;
	e->f10 = id;
	e->f7c = id;
	e->scale[0] = 3.0f;
	e->scale[1] = 0;
	e->scale[2] = 0;
	e->mirrorChildren = 0;
	e->motionId = 0;
	e->rehitTimer = 0;
	e->active = 0;
	e->shape = (aos_shape){0, 0, 0};
	switch (id) {
	case 100: case 0x6e:
		e->f84 = 100;
		e->hp = e->maxHp = 1000;
		break;
	case 200: case 0xfa:
		e->f84 = 0;
		e->hp = e->maxHp = 1000;
		break;
	case 300:
		e->f84 = 100;
		e->hp = e->maxHp = 0x4b0;
		e->shape.radius = 80.0f; /* DAT_00814164 */
		break;
	case 500: {
		e->f84 = 0;
		e->solid = 1;
		e->hp = e->maxHp = 9999;
		e->active = 1;
		e->shape.radius = 100.0f;
		/* inserted at the FRONT of the enemy list (vector::insert(begin)) */
		aos_enemy tmp = *e;
		memmove(&w->obj.enemy[1], &w->obj.enemy[0], sizeof tmp * (size_t)i);
		w->obj.enemy[0] = tmp;
		return 0;
	}
	case 600: case 0x28a:
		e->drawLayer = 0;
		e->f84 = 100;
		e->hp = e->maxHp = 9999;
		e->scale[0] = e->scale[1] = e->scale[2] = 1.0f;
		if (id == 0x28a)
			e->motionId = 2;
		break;
	default: { /* 700 / 0x2c6 + solid child 0x2ee */
		e->f84 = 100;
		e->hp = e->maxHp = 500;
		e->shape.radius = 40.0f; /* DAT_008140e0 */
		e->scale[0] = 0;
		e->scale[1] = 0;
		e->scale[2] = 1.0f;
		e->parentId = -1;
		int c = aos_enemy_new(w, T, owner, team, p18);
		if (c < 0)
			return i;
		aos_enemy *ch = &w->obj.enemy[c];
		e = &w->obj.enemy[i];
		ch->serial = grp;
		ch->f10 = ch->f7c = 0x2ee;
		ch->f84 = 100;
		ch->solid = 1;
		ch->hp = ch->maxHp = 9999;
		ch->active = 1;
		ch->scale[0] = 0;
		ch->scale[1] = 0;
		ch->scale[2] = 1.0f;
		ch->mirrorChildren = 0;
		ch->motionId = 3;
		ch->parentId = e->id;
		e->childSlots[0] = ch->id;
		break;
	}
	}
	return i;
}
static int enemy_spawn_a(aos_world *w, int32_t id, int32_t o, int32_t t, int32_t p) { return enemy_spawn_t(w, SA, id, o, t, p); }
static int enemy_spawn_b(aos_world *w, int32_t id, int32_t o, int32_t t, int32_t p) { return enemy_spawn_t(w, SB, id, o, t, p); }

static int feather_count(aos_world *w, int32_t T, int32_t team)
{
	int n = 0;
	for (int i = w->obj.nenemy - 1; i >= 0; i--) {
		aos_enemy *e = &w->obj.enemy[i];
		if (e->team == team && e->type == T && (e->f10 == 100 || e->f10 == 0x6e) && e->f84 != 900)
			n++;
	}
	return n;
}

static int barrel_find(aos_world *w, int32_t T, int32_t team, int32_t phase)
{
	for (int i = 0; i < w->obj.nenemy; i++) {
		aos_enemy *e = &w->obj.enemy[i];
		if (e->team == team && e->type == T && e->f10 == 300 && e->f84 == phase)
			return i;
	}
	return -1;
}
static int barrel_captured(aos_world *w, int32_t T, int32_t team) { return barrel_find(w, T, team, 300); }
static int barrel_idle(aos_world *w, int32_t T, int32_t team) { return barrel_find(w, T, team, 100); }

/* FUN_0055f110: angle from the enemy to the opponent (Tsih stealth special case) */
static float enemy_aim(aos_world *w, aos_enemy *e)
{
	aos_player *o = aos_opponent(w, team_player(w, e->team));
	if (o->id == AOS_CH_TSIH && o->scratch[8] == 1) {
		float a = aos_dir_angle(team_player(w, e->team));
		if (AOS_PI < a)
			a = (o->x < e->pos[0] || o->x == e->pos[0]) ? AOS_PI : 0.0f;
		return a;
	}
	return aos_atan2(o->y - e->pos[1], o->x - e->pos[0]);
}

/* FUN_0055a0a0 / FUN_005529f0: formation slot around the owner */
static void formation(aos_world *w, aos_enemy *e, float s, float pos[3], float *ang)
{
	int32_t q = e->maxHp;
	float n0 = (float)((q - 1000) / 2), n1 = (float)((q - 0x44c) / 2);
	float r = 20.0f * s + n0 * 5.0f; /* DAT_008140fc, DAT_00813f40 */
	if (1099 < q)
		r = 25.0f * s + n1 * 6.0f; /* DAT_008140f0, DAT_00813f44 */
	aos_player *O = team_player(w, e->team);
	float A = O->faceAngle;
	float wob = aos_sin(((float)(w->frame % 300) / 300.0f) * AOS_2PI);
	bool L = HALF_PI <= aos_fabs(A);
	bool odd = (q % 2) != 0;
	bool front = (O->facingRight == 1 && !odd) || (O->facingRight == 0 && odd);
	bool big = q >= 0x44c;
	float n = big ? n1 : n0;
	float a;
	if (O->state != AOS_ST_DASH) {
		if (front) {
			if (!big)
				a = L ? (-n * 0.15f + A + 0.1f) - wob * 0.2f : ((n * 0.15f + A) - 0.1f) + wob * 0.2f;
			else
				a = L ? (-n * 0.15f + A + 0.1f + 0.15f) - wob * 0.2f : (((n * 0.15f + A) - 0.1f) - 0.15f) + wob * 0.2f;
		} else {
			if (!big)
				a = L ? ((n * 0.15f + A + AOS_PI) - 0.1f) + wob * 0.2f : (-n * 0.15f + A + AOS_PI + 0.1f) - wob * 0.2f;
			else
				a = L ? (((n * 0.15f + A + AOS_PI) - 0.1f) - 0.15f) + wob * 0.2f
				      : (-n * 0.15f + A + AOS_PI + 0.1f + 0.15f) - wob * 0.2f;
		}
	} else {
		float k1 = front ? 2.5f : 3.0f; /* DAT_008153e0 / DAT_00813a50 */
		if (!big)
			a = L ? ((n * 0.15f + A) - k1) - wob * 0.2f : (-n * 0.15f + A + k1) + wob * 0.2f;
		else
			a = L ? (((n * 0.15f + A) - k1) - 0.075f) - wob * 0.2f : (-n * 0.15f + A + k1 + 0.075f) + wob * 0.2f;
	}
	*ang = a;
	pos[0] = aos_cos(a) * r + O->x;
	pos[1] = aos_sin(a) * r + O->y;
}

static void wave(aos_enemy *e, float period)
{
	e->pos[0] = aos_cos(e->angle2) * e->f70 + e->pos[0];
	e->pos[1] = aos_sin(e->angle2) * e->f70 + e->pos[1];
	float v[2] = {0.0f, aos_cos(((float)e->timer2 / period) * AOS_PI) * 10.0f};
	if (e->angle2 < 0.0f && e->mirrorChildren == 0)
		v[1] = -v[1];
	if (0.0f < e->angle2 && e->mirrorChildren == 1)
		v[1] = -v[1];
	aos_vec_rotate(v, e->angle2);
	e->pos[0] += v[0];
	e->pos[1] += v[1];
}

static void feather_sparks(aos_world *w, aos_enemy *e, const float pos[3], float lo, float hi, float base)
{
	for (int k = 0; k < 5; k++) {
		float r = aos_rand_real(w, lo, hi);
		int32_t j = aos_rand_int(w, 0, 100) % 4;
		fx0(w, 0xa32 + j, 0xd, pos, r + base, e->ownerId, k * -3, e->team);
	}
}

static void to_return(aos_enemy *e)
{
	e->f84 = 0x122;
	e->timer2 = 0;
	e->f70 = 20.0f;
}

static bool hits_opponent(aos_world *w, aos_enemy *e, float r)
{
	aos_player *o = aos_opponent(w, team_player(w, e->team));
	if (o->barrierTime != 0 || o->hittable != 1)
		return false;
	aos_shape s = {0, 0, r};
	return aos_shape_hit(e->pos, &o->x, &s, (const aos_shape *)o->hurtShape, e->angle, o->faceAngle);
}

static bool feather_update(aos_world *w, aos_enemy *e)
{
	int32_t T = e->type;
	switch (e->f84) {
	case 0:
		e->scale[0] -= 0.2f;
		e->scale[1] += 0.1f;
		e->scale[2] += 0.1f;
		if (1.0f <= e->scale[1]) {
			e->scale[0] = e->scale[1] = e->scale[2] = 1.0f;
			e->f84 = 100;
		}
		break;
	case 100:
		if (e->hp < 0x65 || team_player(w, e->team)->state == AOS_ST_KO)
			e->f84 = 900;
		break;
	case 200: case 0xc9:
		wave(e, 50.0f);
		e->angle = 0.06283186f * e->scratch[0] + e->angle; /* DAT_00817dcc */
		if (0x31 < e->timer2) {
			e->f84 = e->f84 % 10 + 0xd2;
			e->timer2 = 0;
			e->f70 = 0;
		}
		break;
	case 0xd2: case 0xd3:
		e->pos[0] -= aos_cos(e->angle) * e->f70;
		e->pos[1] -= aos_sin(e->angle) * e->f70;
		e->f70 *= 0.95f;
		if (e->timer2 == 5) {
			aos_angle_toward(&e->angle, enemy_aim(w, e), 0.75f);
			e->angle2 = e->angle;
			if (e->f10 == 100)
				aos_spawn_beam(w, T, 500, e->pos, e->angle, e->ownerId, 0, e->team);
			else
				aos_spawn_missile(w, T, 500, e->pos, e->angle, e->id, 0, e->team, e->p18); /* literal owner = enemy id */
			aos_se_cd(w, 0xd, 3);
		}
		if (99 < e->timer2)
			to_return(e);
		break;
	case 0x122: {
		float P[3] = {0, 0, 0}, a;
		formation(w, e, (float)(0x32 - e->timer2) / 5.0f + 1.0f, P, &a);
		aos_angle_toward(&e->angle, a, 0.05f);
		e->angle2 = e->angle;
		e->pos[0] = aos_cos(e->angle2) * e->f70 + e->pos[0];
		e->pos[1] = aos_sin(e->angle2) * e->f70 + e->pos[1];
		float k = (float)e->timer2 / 50.0f;
		e->pos[0] = (P[0] - e->pos[0]) * k * k + e->pos[0];
		e->pos[1] = (P[1] - e->pos[1]) * k * k + e->pos[1];
		if (0x31 < e->timer2) {
			e->f84 = 100;
			e->timer2 = 0;
		}
		break;
	}
	case 300: case 0x12d: case 400: {
		bool et = e->f84 == 400;
		wave(e, 25.0f);
		e->angle = 0.12566371f * e->scratch[0] + e->angle; /* DAT_00817e04 */
		if (0x18 < e->timer2) {
			e->f84 = et ? 0x19a : e->f84 % 10 + 0x136;
			e->timer2 = 0;
			if (et)
				aos_se_cd(w, 0x21, 4);
			aos_angle_toward(&e->angle, enemy_aim(w, e), 0.75f);
			e->angle2 = e->angle;
			e->f70 = 20.0f;
			aos_se_cd(w, et ? 0x39 : 0x25, 3);
			feather_sparks(w, e, e->pos, -0.15f, 0.15f, e->angle2); /* DAT_00815dc4, DAT_00813f20 */
		}
		break;
	}
	case 0x136: case 0x137:
		e->pos[0] = aos_cos(e->angle) * e->f70 + e->pos[0];
		e->pos[1] = aos_sin(e->angle) * e->f70 + e->pos[1];
		if (10 < e->timer2)
			e->f70 *= 0.95f;
		if (hits_opponent(w, e, 30.0f)) {
			aos_player *o = aos_opponent(w, team_player(w, e->team));
			e->pivot[0] = e->pivot[1] = 0;
			e->angle2 = aos_atan2(o->y - e->pos[1], o->x - e->pos[0]);
			e->angle = e->angle2;
			e->ownerId = o->objId; /* literal: the stuck feather now names the victim as owner */
			e->f84 = e->f84 % 10 + 0x15e;
			e->timer2 = 0;
			return true;
		}
		if (0x4f < e->timer2)
			to_return(e);
		break;
	case 0x15e: case 0x15f: {
		if (e->timer2 == 1)
			aos_spawn_hitbox(w, T, e->f10 == 100 ? 100 : 0x96, e->pos, e->angle2, e->ownerId, 0, e->team);
		aos_player *v = aos_player_by_objid(w, e->ownerId);
		if (!v || v->state == AOS_ST_KO || v->barrierTime != 0)
			e->timer2 = 200;
		if (!v) { /* the original dereferences NULL here */
			to_return(e);
			break;
		}
		e->pos[0] = -aos_cos(e->angle2) * 30.0f + v->x;
		e->pos[1] = -aos_sin(e->angle2) * 30.0f + v->y;
		if (e->timer2 % 2 == 0) {
			if (e->f10 == 100) {
				aos_spawn_particle(w, 0x44c, tcol(w, e->team, 1), e->pos, e->angle + AOS_PI, e->ownerId, 0, 1);
				v->heat += 0.003f; /* DAT_00813ea0 */
			} else {
				aos_spawn_particle(w, 0x44c, tcol(w, e->team, 0), e->pos, e->angle + AOS_PI, e->ownerId, 0, 1);
				v->hyper -= 0.00075f; /* _DAT_00814bd4 */
				if (v->hyper < 0.0f)
					v->hyper = 0;
			}
			v->recovHp -= 1;
			if (v->recovHp < v->hp)
				v->recovHp = v->hp;
		}
		if (e->timer2 == 200)
			to_return(e);
		break;
	}
	case 0x19a: case 0x1a4:
		e->pos[0] = aos_cos(e->angle) * e->f70 + e->pos[0];
		e->pos[1] = aos_sin(e->angle) * e->f70 + e->pos[1];
		if (10 < e->timer2)
			e->f70 *= 0.95f;
		if (e->f84 == 0x19a && hits_opponent(w, e, 50.0f)) {
			aos_spawn_hitbox(w, T, e->f10 == 100 ? 600 : 0x262, e->pos, e->angle, e->ownerId, 0, e->team);
			e->f84 = 0x1a4;
			return true;
		}
		if (0x4f < e->timer2)
			to_return(e);
		break;
	default:
		e->scale[0] += 0.2f;
		e->scale[1] -= 0.1f;
		e->scale[2] += 0.1f;
		if (e->scale[1] <= 0.0f)
			return false;
		break;
	}
	if (e->f84 == 0 || e->f84 == 100 || e->f84 == 900) {
		float P[3] = {0, 0, 0}, a;
		formation(w, e, e->scale[0], P, &a);
		e->pos[0] = P[0];
		e->pos[1] = P[1];
		e->pos[2] = 0; /* FUN_0055a0a0 writes only out[0..1]; z keeps the old value (always 0) */
		e->angle = a;
		e->angle2 = a;
	}
	return true;
}

static bool barrel_update(aos_world *w, aos_enemy *e)
{
	int32_t T = e->type;
	if (e->f84 == 100) {
		e->scale[0] -= 0.2f;
		e->scale[1] += 0.1f;
		if (1.0f <= e->scale[1]) {
			e->scale[0] = e->scale[1] = e->scale[2] = 1.0f;
			e->active = 1;
		}
		aos_player *o = aos_player_by_objid(w, e->ownerId);
		if (!o || o->state != (T == SB ? 0x24a : 0x226)) {
			e->f84 = 900;
			e->scale[2] = 0;
			e->active = 0;
		}
	} else if (e->f84 == 200) {
		if (e->timer2 == 2) {
			float pt[3] = {aos_cos(e->angle2) * -50.0f + e->pos[0], aos_sin(e->angle2) * -50.0f + e->pos[1], 0}; /* DAT_008149e4 */
			feather_sparks(w, e, pt, -0.1f, 0.1f, e->angle2);
		}
		e->f70 -= 0.2f;
		e->pos[0] += e->scratch[0];
		e->pos[1] += e->f70;
		if (e->scratch[0] <= 0.0f) {
			e->angle -= 0.1f;
			if (e->angle <= -AOS_PI && -AOS_PI != e->angle)
				e->angle += AOS_2PI;
		} else {
			e->angle += 0.1f;
			if (AOS_PI < e->angle)
				e->angle -= AOS_2PI;
		}
		if (0.0f < e->f70 && (540.0f < e->pos[0] || e->pos[0] < -540.0f)) {
			aos_otab.enemyOnKilled[T](w, e);
			return false;
		}
		if (e->f70 < 0.0f && e->pos[1] < 0.0f && !aos_in_arena(e->pos, 50.0f)) {
			aos_otab.enemyOnKilled[T](w, e);
			return false;
		}
		aos_player *o = aos_opponent(w, team_player(w, e->team));
		if (dist2d(&o->x, e->pos) < 80.0f && can_affect(w, o)) {
			e->f84 = 300;
			e->timer2 = 0xd2;
			e->angle = HALF_PI;
			e->drawLayer = 2;
			fx0(w, 0xaf0, 0xd, e->pos, 0.0f, e->ownerId, 0, e->team);
			aos_se(w, 0x39);
		}
	} else if (e->f84 == 300) {
		aos_player *o = aos_opponent(w, team_player(w, e->team));
		bool hold = can_affect(w, o);
		if (hold && T == SA)
			hold = e->fe0 == 0.0f || dist2d(&o->x, e->pos) < 80.0f; /* ver > 0x58b */
		if (hold) {
			o->x = e->pos[0];
			o->y = e->pos[1];
			o->z = e->pos[2];
			o->faceAngle = 0;
			o->state = AOS_ST_HITSTUN;
			o->timer = 0;
			o->speed = 0;
			aos_status(w, o, AOS_STATUS_MARK, 5);
			if (T == SA)
				e->fe0 = 1.0f; /* latch byte +0xe0 */
		}
		if (299 < e->timer2 || (T == SA && o->state == AOS_ST_SHIELD)) {
			aos_spawn_hitbox(w, T, 400, e->pos, HALF_PI, e->ownerId, 0, e->team);
			aos_otab.enemyOnKilled[T](w, e);
			return false;
		}
	} else {
		e->scale[0] += 0.2f;
		e->scale[1] -= 0.1f;
		if (e->scale[1] <= 0.0f)
			return false;
	}
	return true;
}

static bool core_update(aos_world *w, aos_enemy *e) /* second form, enemy 500 */
{
	aos_player *o = aos_player_by_objid(w, e->ownerId);
	if (o && o->state == AOS_ST_KO && e->f84 != 900) {
		e->f84 = 900;
		e->timer2 = 0;
		e->active = 0;
		aos_se(w, 0x1b);
		/* FUN_004e8fd0: screen effect, not in fx.h (cosmetic) */
		aos_screen_dark(w, 0x1e, 0.75f);
		aos_screen_blur(w, 0x1e, 0.5f, 2);
	}
	if (e->f84 == 0) {
		e->scale[0] -= 0.1f;
		e->scale[1] += 0.05f;
		e->scale[2] += 0.1f;
		e->shape.radius = 100.0f * e->scale[1];
		if (1.0f <= e->scale[1]) {
			e->scale[0] = e->scale[1] = e->scale[2] = 1.0f;
			e->f84 = 100;
			e->active = 1;
			e->shape.radius = 100.0f;
		}
	} else if (e->f84 < 100 || 899 < e->f84) {
		e->scale[2] -= 0.005f; /* DAT_00813eac */
		e->scale[0] -= 0.001f;
		e->scale[1] -= 0.001f;
		e->pos[1] -= 1.0f;
		if (e->scale[2] < 0.0f)
			e->scale[2] = 0;
		if (e->timer2 < 0xc9 && e->timer2 % 5 == 0) {
			for (int k = 0; k < 2; k++) {
				float pt[3] = {e->pos[0], e->pos[1], e->pos[2]};
				float a = aos_rand_real(w, -AOS_PI, AOS_PI);
				float d = aos_rand_real(w, 20.0f * e->scale[0], 180.0f * e->scale[0]);
				pt[0] = aos_cos(a) * d + pt[0];
				pt[1] = aos_sin(a) * d + pt[1];
				int f = fx0(w, 500, 0, pt, 0.0f, e->ownerId, -10 - k, e->team);
				fx0(w, 0x515, 1, pt, 0.0f, e->ownerId, 0, e->team);
				if (f >= 0)
					w->obj.effect[f].drawLayer = 1;
			}
			aos_se_cd(w, 0x1d, 0x14);
		} else if (200 < e->timer2) {
			e->scale[1] += 0.2f;
			e->scale[0] -= 0.05f;
			if (e->scale[0] <= 0.0f)
				return false;
		}
		if (e->timer2 == 200) {
			fx0(w, 0xaf0, 0xd, e->pos, 0.0f, e->ownerId, 0, e->team);
			aos_se(w, 0x39);
		}
	} else if (e->f84 == 100 && 999 < e->timer2) { /* FUN_00553720 == 0 */
		static const int32_t table[10] = {100, 100, 100, 100, 100, 100, 100, 100, 100, -1}; /* 0x86c030 */
		GB(w)->patternIdx++;
		if (GB(w)->patternIdx < 0 || GB(w)->patternIdx > 9 || table[GB(w)->patternIdx] < 0)
			GB(w)->patternIdx = 0;
		e->f84 = table[GB(w)->patternIdx];
		e->timer2 = 0;
	}
	return true;
}

static bool satellite_update(aos_world *w, aos_enemy *e) /* second form, enemies 700 / 0x2c6 */
{
	if (e->f84 == 100) {
		e->scale[0] += 0.02f;
		e->scale[1] += 0.02f;
		e->scale[2] += 0.02f;
		if (1.0f <= e->scale[1]) {
			e->scale[0] = e->scale[1] = e->scale[2] = 1.0f;
			e->f84 = 200;
			e->active = 1;
			e->timer2 = 0;
		}
	} else if (e->f84 == 200) {
		if (e->timer2 == 0x17c || e->timer2 == 400 || e->timer2 == 0x1a4) {
			aos_spawn_beam(w, SB, e->f10 == 700 ? 0x2c6 : 700, e->pos, e->angle, e->ownerId, 0, e->team);
			aos_se_cd(w, 0xe, 3);
		}
		aos_player *o = aos_player_by_objid(w, e->ownerId);
		if (600 < e->timer2 || (o && o->state == AOS_ST_KO)) {
			e->f84 = 900;
			aos_enemy *c = aos_enemy_by_id(w, e->childSlots[0]); /* FUN_0055f0c0 */
			if (c)
				c->f84 = 900;
		}
	} else {
		e->scale[0] -= 0.02f;
		e->scale[1] -= 0.02f;
		e->scale[2] -= 0.02f;
		if (e->scale[1] <= 0.0f)
			return false;
	}
	e->angle2 = aos_sin(((float)(e->age % 800) / 800.0f) * AOS_2PI) * 0.025f + e->angle2; /* DAT_008149c8, DAT_00813edc */
	e->pos[0] = aos_cos(e->angle2) * 200.0f * e->scale[0];
	e->pos[1] = aos_sin(e->angle2) * 200.0f * e->scale[0];
	e->angle = e->angle2;
	return true;
}

static bool enemy_update(aos_world *w, aos_enemy *e)
{
	switch (e->f7c) {
	case 100: case 0x6e:
		return feather_update(w, e);
	case 200: case 0xfa:
		if (e->f84 == 0) {
			e->scale[0] += 0.2f;
			e->scale[1] -= 0.4f; /* DAT_00813f30 */
			if (1.0f <= e->scale[0]) {
				e->scale[0] = e->scale[1] = e->scale[2] = 1.0f;
				e->f84 = 100;
			}
		} else if (e->f84 == 100) {
			if (0x32 < e->age) {
				e->scale[2] = 0;
				e->f84 = 900;
			}
		} else {
			e->scale[0] -= 0.2f;
			e->scale[1] += 0.4f;
			if (e->scale[0] <= 0.0f)
				return false;
		}
		return true;
	case 300:
		return barrel_update(w, e);
	case 500:
		return e->type == SB ? core_update(w, e) : true;
	case 600: case 0x28a: {
		if (e->type != SB)
			return true;
		if (e->f7c == 0x28a) {
			if (e->f84 != 100)
				return false;
			int d = w->difficulty;
			if ((d == 0 && e->timer2 == 0x32) || (d == 1 && (e->timer2 == 0x28 || e->timer2 == 0x50)) ||
			    (d == 2 && (e->timer2 == 0x28 || e->timer2 == 0x50))) {
				float r = aos_rand_real(w, -AOS_PI, AOS_PI);
				aos_spawn_beam(w, SB, 600, e->pos, r, e->ownerId, 0, e->team);
				aos_se_cd(w, 0xf, 5);
			}
		}
		aos_player *o = aos_player_by_objid(w, e->ownerId);
		if (999 < e->age || (o && o->state == AOS_ST_KO))
			return false;
		return true;
	}
	case 700: case 0x2c6:
		return e->type == SB ? satellite_update(w, e) : true;
	case 0x2ee:
		if (e->f84 == 100) {
			e->scale[0] += 0.02f;
			e->scale[1] += 0.02f;
			e->scale[2] += 0.02f;
			e->shape.radius = e->scale[0] * 40.0f;
			if (1.0f <= e->scale[1]) {
				e->scale[0] = e->scale[1] = e->scale[2] = 1.0f;
				e->shape.radius = e->scale[0] * 40.0f;
				e->f84 = 200;
				e->timer2 = 0;
			}
		} else if (e->f84 == 900) {
			return false;
		}
		return true;
	default:
		return true;
	}
}

static bool enemy_killed(aos_world *w, aos_enemy *e) /* FUN_0055cbf0 / FUN_00555bc0 */
{
	if (e->f10 == 300) {
		fx0(w, 0x28a, 0, e->pos, 0.0f, e->ownerId, 0, e->team);
		for (int k = 0; k < 10; k++) {
			float pt[3] = {e->pos[0], e->pos[1], e->pos[2]};
			pt[0] = aos_rand_real(w, -120.0f, 120.0f) + pt[0];
			pt[1] = aos_rand_real(w, -120.0f, 120.0f) + pt[1];
			fx0(w, 0x834, 0, pt, 0.0f, e->ownerId, 0, e->team);
		}
		for (int k = 0; k < 10; k++) {
			float pt[3] = {e->pos[0], e->pos[1], e->pos[2]};
			pt[0] = aos_rand_real(w, -100.0f, 100.0f) + pt[0];
			pt[1] = aos_rand_real(w, -100.0f, 100.0f) + pt[1];
			fx0(w, 500, 0, pt, 0.0f, e->ownerId, -10 - k, e->team);
		}
		aos_se(w, 0x1a);
	} else if (e->f10 == 700 || e->f10 == 0x2c6) {
		fx0(w, 600, 0, e->pos, 0.0f, e->ownerId, 0, e->team);
		for (int k = 0; k < 5; k++) {
			float pt[3] = {e->pos[0], e->pos[1], e->pos[2]};
			pt[0] = aos_rand_real(w, -80.0f, 80.0f) + pt[0];
			pt[1] = aos_rand_real(w, -80.0f, 80.0f) + pt[1];
			fx0(w, 0x834, 0, pt, 0.0f, e->ownerId, 0, e->team);
		}
		for (int k = 0; k < 5; k++) {
			float pt[3] = {e->pos[0], e->pos[1], e->pos[2]};
			pt[0] = aos_rand_real(w, -50.0f, 50.0f) + pt[0];
			pt[1] = aos_rand_real(w, -50.0f, 50.0f) + pt[1];
			fx0(w, 500, 0, pt, 0.0f, e->ownerId, k * -2 - 10, e->team);
		}
		aos_se(w, 0x1a);
	}
	return false;
}

static void enemy_draw(aos_world *w, aos_enemy *e) /* FUN_0055d070 / FUN_00556040 (sprite ids from the disassembly immediates) */
{
	(void)w;
	int32_t T = e->type;
	int bank = team_bank(e->team);
	float ph = ((float)(e->age % 0x32) / 50.0f) * AOS_2PI;
	float g1 = aos_sin(ph) * 0.25f + 0.75f, g2 = aos_sin(ph) * 0.35f + 0.65f;
	float br = e->scale[2];
	uint32_t base = aos_rgba(br, br, br, 1.0f);
	switch (e->f10) {
	case 100: case 0x6e: {
		const char *n = e->f10 == 100 ? "fthrr%02d" : "fthrb%02d";
		int f0 = 0;
		float k = 1.0f;
		bool moving = !(e->f84 == 0 || e->f84 == 100 || e->f84 == 900);
		if (moving) {
			f0 = ((e->timer2 / 3) % 6) * 3;
			if (e->f84 == 400) {
				k = (float)e->timer2 / 20.0f + 1.0f;
				if (2.0f < k) k = 2.0f;
			}
			if (e->f84 == 0x19a)
				k = 2.0f;
		}
		bool flip = !moving || e->f84 == 0x122;
		float A[3] = {1.0f, g1 * br, g2 * br};
		for (int j = 0; j < 3; j++) {
			aos_dcmd *c = aos_draw(texT(T, n, f0 + j), bank, e->pos[0], e->pos[1], 0);
			c->halfW = c->halfH = 32.0f; /* S = 64 */
			c->sx = e->scale[0] * k;
			c->sy = e->scale[1] * k;
			c->rotZ = aos_rad2bam(e->angle);
			c->flipX = flip;
			c->blend = j == 0 ? AOS_BLEND_ALPHA : AOS_BLEND_ADD_ALPHA;
			c->rgba = j == 0 ? base : aos_rgba(1, 1, 1, A[j]);
		}
		break;
	}
	case 200: case 0xfa: {
		int f = e->f84 == 0 ? 0 : e->f84 == 100 ? imin(e->age - 10 < 0 ? 0 : (e->age - 10) / 2, 3) : 3;
		if (f < 0) f = 0;
		aos_dcmd *c = aos_draw(texT(T, e->f10 == 200 ? "arrowr%02d" : "arrowb%02d", f), bank, e->pos[0], e->pos[1], 0);
		c->halfW = c->halfH = 64.0f;
		c->sx = e->scale[0];
		c->sy = e->scale[1];
		c->rotZ = aos_rad2bam(e->angle);
		c->rgba = base;
		break;
	}
	case 300: {
		float s1 = aos_sin(((float)(e->age % 0x1e) / 30.0f) * AOS_2PI);
		float sx = e->scale[0] * (s1 * 0.1f + 1.0f), sy = e->scale[1] * (1.0f - s1 * 0.1f);
		int f = (e->age / 4) % 5;
		const char *n = T == SB ? "barrel%02d" : "barrel%02d";
		float A[3] = {1.0f, g1 * br, g2 * br};
		int F[3] = {f, 5, 6}; /* glow frames 5, 6 (FUN_004a9ab0(5/6)) */
		for (int j = 0; j < 3; j++) {
			aos_dcmd *c = aos_draw(texT(T, n, F[j]), bank, e->pos[0], e->pos[1], 0);
			c->halfW = c->halfH = 96.0f; /* S = 192 */
			c->sx = sx;
			c->sy = sy;
			c->rotZ = aos_rad2bam(e->angle);
			c->blend = j == 0 ? AOS_BLEND_ALPHA : AOS_BLEND_ADD_ALPHA;
			c->rgba = j == 0 ? base : aos_rgba(1, 1, 1, A[j]);
		}
		break;
	}
	case 500: { /* second-form core: sprite 0x24e sab_bomb (S = 420), wobbling, frames 0 / 1 / 2 */
		float s100 = aos_sin(((float)(e->age % 100) / 100.0f) * AOS_2PI);
		float s20 = aos_sin(((float)(e->age % 0x14) / 20.0f) * AOS_2PI);
		float fx = (s100 * 0.05f + 1.0f) * (s20 * 0.2f * (1.0f - br) + 1.0f);
		float fy = (1.0f - s100 * 0.05f) * (1.0f - s20 * 0.2f * (1.0f - br));
		float A[3] = {1.0f, g1 * br, g2 * br};
		for (int j = 0; j < 3; j++) {
			aos_dcmd *c = aos_draw(texT(T, "bomb%02d", j), bank, e->pos[0], e->pos[1], 0);
			c->halfW = c->halfH = 210.0f; /* DAT_0081ce8c = 420 */
			c->sx = e->scale[0] * fx;
			c->sy = e->scale[1] * fy;
			c->rotZ = aos_rad2bam(e->angle);
			c->blend = j == 0 ? AOS_BLEND_ALPHA : AOS_BLEND_ADD_ALPHA;
			c->rgba = j == 0 ? base : aos_rgba(1, 1, 1, A[j]);
		}
		break;
	}
	case 700: case 0x2c6: { /* satellites: sprite 0x25a sab_shield (S = 128), frames k*3 (+9 for 0x2c6) .. +2 */
		int f0 = ((e->age / 3) % 3) * 3 + (e->f10 == 0x2c6 ? 9 : 0);
		float A[3] = {1.0f, g1 * br, g2 * br};
		for (int j = 0; j < 3; j++) {
			aos_dcmd *c = aos_draw(texT(T, "shield%02d", f0 + j), bank, e->pos[0], e->pos[1], 0);
			c->halfW = c->halfH = 64.0f;
			c->sx = e->scale[0];
			c->sy = e->scale[1];
			c->rotZ = aos_rad2bam(e->angle);
			c->blend = j < 2 ? AOS_BLEND_ALPHA : AOS_BLEND_ADD_ALPHA; /* literal: blend 2 set only before the 3rd layer */
			c->rgba = j == 0 ? base : aos_rgba(1, 1, 1, A[j]);
		}
		break;
	}
	case 600: case 0x28a: { /* tentacle: sprite 0x259 sab_tenta (S = 96); glow frame +3 / 9 */
		int f = (e->age / 4) % 3;
		if (e->f10 == 600 || 999 <= e->age)
			f += 6;
		int g = e->f10 == 600 ? 9 : (f < 6 ? f + 3 : 9);
		float pulse = aos_sin(((float)(e->age % 0x32) / 50.0f) * AOS_2PI) * 0.35f + 0.65f;
		aos_dcmd *c = aos_draw(texT(T, "tenta%02d", f), bank, e->pos[0], e->pos[1], 0);
		c->halfW = c->halfH = 48.0f;
		c->sx = e->scale[0];
		c->sy = e->scale[1];
		c->rotZ = aos_rad2bam(e->angle);
		c->rgba = base;
		c = aos_draw(texT(T, "tenta%02d", g), bank, e->pos[0], e->pos[1], 0);
		c->halfW = c->halfH = 48.0f;
		c->sx = e->scale[0];
		c->sy = e->scale[1];
		c->rotZ = aos_rad2bam(e->angle);
		c->blend = AOS_BLEND_ADD_ALPHA;
		c->rgba = aos_rgba(1, 1, 1, pulse * br);
		break;
	}
	default:
		break;
	}
}

/* ---- missiles: spawn FUN_00681e00 / FUN_0067da60, update FUN_00684d70 / FUN_00680ca0,
 *      draw FUN_00682540 / FUN_0067e1a0, on-hit FUN_00684620 / FUN_00680550,
 *      on-killed FUN_00684c90 / FUN_00680bc0 ---- */

static int missile_spawn_t(aos_world *w, int32_t T, int32_t id, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team, int32_t p18)
{
	switch (id) {
	case 100: case 0x66: case 0x96: case 0x98: case 200: case 0xfa: case 500: case 300: case 0x15e: case 600: break;
	default: return -1;
	}
	float r = 0;
	if (id == 300 || id == 0x15e)
		r = aos_rand_real(w, -AOS_PI, AOS_PI); /* draw angle, drawn before the push */
	int i = aos_missile_new(w, T, id, pos, angle, owner, age0, team, p18);
	if (i < 0)
		return -1;
	aos_missile *m = &w->obj.missile[i];
	m->hp = 0x3c;
	switch (id) {
	case 100: case 0x96:
		m->speed = 16.0f;
		m->growScale = 0;
		m->hitType = id == 100 ? 1 : 2;
		m->active = 0;
		m->shapeBody.radius = 16.0f;
		m->damage = 0x96;
		m->knockback = 10.0f;
		break;
	case 0x66: case 0x98:
		m->speed = -5.0f; /* DAT_00824aa4 */
		m->growScale = 0;
		m->hitType = id == 0x66 ? 1 : 2;
		m->active = 0;
		m->shapeBody.halfW = 30.0f;
		m->shapeBody.halfH = 16.0f;
		m->damage = 0x3c;
		m->knockback = 5.0f;
		break;
	case 200: case 0xfa:
		m->speed = 0;
		m->growScale = 0;
		m->hitType = id == 200 ? 1 : 2;
		m->active = 0;
		m->shapeBody.halfW = 20.0f;
		m->shapeBody.halfH = 16.0f;
		m->stun = 0.35f; /* DAT_00813f2c */
		m->damage = 0x78;
		m->knockback = 10.0f;
		break;
	case 500:
		m->speed = 18.0f;
		m->growScale = 0;
		m->hitType = 2;
		m->active = 1;
		m->shapeBody.halfW = 20.0f;
		m->shapeBody.halfH = 6.0f;
		m->damage = 0x96;
		m->stun = 0.75f;
		m->knockback = 5.0f;
		break;
	case 300:
		m->angle = r;
		m->f74 = aos_cos(angle) * 12.0f; /* vx */
		m->speed = aos_sin(angle) * 12.0f; /* vy */
		m->growScale = 0;
		m->damage = 0x32;
		m->shapeBody.radius = 20.0f;
		m->shapeProj.radius = 20.0f;
		m->knockback = 10.0f;
		break;
	case 0x15e:
		m->angle = r;
		m->speed = 16.0f;
		m->f60 = 0;
		m->growScale = 0;
		m->hp = 0x78;
		m->damage = 0x32;
		m->shapeBody.radius = 40.0f;
		m->shapeProj.radius = 50.0f;
		m->knockback = 10.0f;
		break;
	default: /* 600 */
		m->speed = 10.0f;
		m->growScale = 1.0f;
		m->hitType = 2;
		m->active = 0;
		m->damage = 0;
		m->knockback = 0;
		break;
	}
	return i;
}
static int missile_spawn_a(aos_world *w, int32_t id, const float pos[3], float a, int32_t o, int32_t g, int32_t t, int32_t p)
{
	return missile_spawn_t(w, SA, id, pos, a, o, g, t, p);
}
static int missile_spawn_b(aos_world *w, int32_t id, const float pos[3], float a, int32_t o, int32_t g, int32_t t, int32_t p)
{
	return missile_spawn_t(w, SB, id, pos, a, o, g, t, p);
}

static void m_move(aos_missile *m)
{
	m->pos[0] = aos_cos(m->moveAngle) * m->speed + m->pos[0];
	m->pos[1] = aos_sin(m->moveAngle) * m->speed + m->pos[1];
}

static void spawn_tentacle_part(aos_world *w, aos_missile *m, int32_t id)
{
	int e = aos_spawn_enemy(w, SB, id, m->ownerId, m->team, m->p18);
	if (e < 0)
		return;
	aos_enemy *en = &w->obj.enemy[e];
	en->pos[0] = m->pos[0], en->pos[1] = m->pos[1], en->pos[2] = m->pos[2];
	en->angle2 = m->moveAngle;
	en->angle = m->moveAngle;
	en->f84 = 100;
	en->hp = m->age / 3 + 1000;
	en->age = m->age + 800 + (m->age / 3) * 2;
}

static bool missile_update(aos_world *w, aos_missile *m)
{
	int32_t T = m->type;
	switch (m->id) {
	case 100: case 0x96:
		if (1.0f <= m->growScale) {
			m->growScale = 1.0f;
			m->active = 1;
		} else {
			m->growScale += 0.1f;
		}
		if (m->age < 0x32)
			m->speed *= 0.95f;
		if (m->age == 0x32) {
			aos_player *o = aos_opponent(w, team_player(w, m->team));
			m->moveAngle = aos_atan2(o->y - m->pos[1], o->x - m->pos[0]);
			m->angle = m->moveAngle;
			m->speed = 16.0f;
		}
		m_move(m);
		if (m->age < 0x51)
			return true;
		if (!aos_in_arena(m->pos, 10.0f)) {
			aos_otab.missileOnKilled[T](w, m);
			return false;
		}
		return true;
	case 0x66: case 0x98:
		if (m->age < 0x28) {
			m->growScale += 0.1f;
			if (1.0f < m->growScale) {
				m->growScale = 1.0f;
				m->active = 1;
			}
		}
		if (m->speed < 16.0f)
			m->speed += 0.5f;
		if (m->age == 5)
			fx0(w, 100, 0, m->pos, m->angle, m->serial, 0, m->team);
		m_move(m);
		if (m->age == 0x28)
			m->active = 0;
		if (0x27 < m->age)
			m->growScale -= 0.1f;
		return m->age != 0x32;
	case 200: case 0xfa:
		if (m->age < 0x28) {
			if (1.0f <= m->growScale) {
				m->growScale = 1.0f;
				m->active = 1;
			} else {
				m->growScale += 0.2f;
			}
		}
		if (9 < m->age && m->speed < 20.0f)
			m->speed += 2.0f;
		if (m->age == 10)
			fx0(w, 100, 0, m->pos, m->angle, m->serial, 0, m->team);
		m_move(m);
		if (m->age == 0x28)
			m->active = 0;
		if (0x27 < m->age)
			m->growScale -= 0.1f;
		return m->age != 0x32;
	case 300: {
		m->growScale += 0.1f;
		if (1.0f < m->growScale)
			m->growScale = 1.0f;
		m->shapeBody.radius = 20.0f * m->growScale;
		m->shapeProj = m->shapeBody;
		m->f9c = m->f8c;
		m->speed -= 0.2f;
		if (m->speed < -10.0f)
			m->speed = -10.0f;
		m->pos[0] += m->f74;
		m->pos[1] += m->speed;
		m->moveAngle = aos_atan2(m->speed, m->f74);
		m->angle += 0.1f;
		if (AOS_PI < m->angle)
			m->angle -= AOS_2PI;
		if (m->damage < 200)
			m->damage += 5;
		if (0.0f <= m->speed && (540.0f < m->pos[0] || m->pos[0] < -540.0f)) {
			m->f74 *= -1.0f;
			if (540.0f < m->pos[0]) m->pos[0] = 540.0f;
			if (m->pos[0] < -540.0f) m->pos[0] = -540.0f;
		}
		float margin = T == SB ? 50.0f : 500.0f; /* ver > 0x5a0 (Sumika only): DAT_00814140 */
		if (!aos_in_arena(m->pos, margin) && m->speed < 0.0f) {
			aos_otab.missileOnHit[T](w, m);
			return false;
		}
		return true;
	}
	case 0x15e: {
		m->growScale += 0.1f;
		if (1.0f < m->growScale)
			m->growScale = 1.0f;
		m->shapeBody.radius = 50.0f * m->growScale;
		m->shapeProj = m->shapeBody;
		m->f9c = m->f8c;
		if (m->f60 <= 0.0f)
			m->f60 = 0;
		else
			m->f60 -= 0.025f;
		m->speed -= 0.2f;
		if (m->speed < 4.0f)
			m->speed = 4.0f;
		m_move(m);
		m->angle += 0.1f;
		if (AOS_PI < m->angle)
			m->angle -= AOS_2PI;
		if (m->damage < 300)
			m->damage += 5;
		if (aos_in_arena(m->pos, 50.0f))
			return true;
		if (m->age < 0x9c4) {
			aos_player *o = aos_opponent(w, team_player(w, m->team));
			m->moveAngle = aos_atan2(o->y - m->pos[1], o->x - m->pos[0]);
			m->speed = 14.0f;
			m->age += 500;
			m->f60 = 1.0f;
			float pt[3] = {m->pos[0] - aos_cos(m->moveAngle) * 50.0f, m->pos[1] - aos_sin(m->moveAngle) * 50.0f, 0};
			int32_t c = tcol(w, m->team, 2);
			fx0(w, 0x547, c, pt, m->moveAngle, m->ownerId, 0, m->team);
			fx0(w, 0x546, c, pt, m->moveAngle, m->ownerId, 0, m->team);
			aos_se_cd(w, 0x39, 4);
			return true;
		}
		if (m->speed < 6.0f) {
			aos_otab.missileOnHit[T](w, m);
			return false;
		}
		return true;
	}
	case 500: {
		m_move(m);
		if (0x14 < m->age && !aos_in_arena(m->pos, 10.0f))
			return false;
		int32_t c = tcol(w, m->team, 0);
		aos_spawn_effect(w, T, 0x1fe, c, m->pos, m->angle, m->ownerId, 0, m->team);
		if (m->age == 1) {
			fx0(w, 0x547, c, m->pos, m->angle, m->ownerId, 0, m->team);
			fx0(w, 0x548, c, m->pos, m->angle, m->ownerId, 0, m->team);
		}
		return true;
	}
	case 600:
		if (T != SB)
			return true; /* Sumika: no update case (would live forever; never spawned) */
		if (m->age % 3 == 0)
			spawn_tentacle_part(w, m, 0x28a);
		m->moveAngle = aos_cos(((float)(m->age % 0x1e) / 30.0f) * AOS_2PI) * 0.1f * m->f74 + m->moveAngle;
		m->angle = m->moveAngle;
		m_move(m);
		if (m->age % 3 != 2)
			return true;
		if (!aos_in_arena(m->pos, 10.0f)) {
			spawn_tentacle_part(w, m, 600);
			int32_t c = tcol(w, m->team, 2);
			fx0(w, 0x9c6, c, m->pos, m->angle, m->ownerId, 0, m->team);
			fx0(w, 0x514, c, m->pos, m->angle, m->ownerId, 0, m->team);
			fx0(w, 0x515, c, m->pos, m->angle, m->ownerId, 0, m->team);
			aos_spawn_hitbox(w, SB, 500, m->pos, m->angle, m->ownerId, 0, m->team);
			aos_se(w, 0x15);
			return false;
		}
		return true;
	default:
		return true;
	}
}

static bool missile_on_hit(aos_world *w, aos_missile *m)
{
	int32_t c;
	switch (m->id) {
	case 100: case 0x66: case 200: case 0x96: case 0x98: case 0xfa:
		c = tcol(w, m->team, (m->id == 100 || m->id == 0x66 || m->id == 200) ? 1 : 0);
		fx0(w, 0x9c4, c, m->pos, m->angle, m->ownerId, 0, m->team);
		aos_spawn_spark(w, 1000, c, 8, 8, m->angle, m->pos, m->ownerId);
		aos_se(w, 0x15);
		break;
	case 300: case 0x15e:
		c = tcol(w, m->team, 2);
		aos_spawn_effect(w, m->type, m->id == 300 ? 0x96 : 0xa0, c, m->pos, m->angle + AOS_PI, m->ownerId, 0, m->team);
		fx0(w, m->id == 300 ? 0x514 : 0xaf0, c, m->pos, 0.0f, m->ownerId, 0, m->team);
		aos_se(w, 0x14);
		break;
	case 500:
		c = tcol(w, m->team, 0);
		fx0(w, 0x3f2, c, m->pos, m->angle + AOS_PI, m->ownerId, 0, m->team);
		aos_spawn_particle(w, 1000, c, m->pos, m->angle, m->ownerId, 0, 5);
		aos_spawn_spark(w, 1000, c, 6, 6, m->angle, m->pos, m->ownerId);
		aos_se_cd(w, 0x13, 4);
		break;
	default:
		break;
	}
	return false;
}

static bool missile_on_killed(aos_world *w, aos_missile *m)
{
	if (m->id == 300 || m->id == 0x15e) {
		int32_t c = tcol(w, m->team, 2);
		aos_spawn_effect(w, m->type, 0x96, c, m->pos, m->angle + AOS_PI, m->ownerId, 0, m->team);
		fx0(w, 0x514, c, m->pos, 0.0f, m->ownerId, 0, m->team);
		aos_se(w, 0x14);
	}
	return false;
}

static aos_dcmd *m_quad(int tex, int bank, const aos_missile *m, float half, float sx, float sy, uint32_t rgba, int blend)
{
	aos_dcmd *c = aos_draw(tex, bank, m->pos[0], m->pos[1], 0);
	c->halfW = c->halfH = half;
	c->sx = sx;
	c->sy = sy;
	c->rotZ = aos_rad2bam(m->angle);
	c->rgba = rgba;
	c->blend = (uint8_t)blend;
	return c;
}

static void missile_draw(aos_world *w, aos_missile *m) /* FUN_00682540 / FUN_0067e1a0 */
{
	(void)w;
	int32_t T = m->type;
	int bank = team_bank(m->team);
	float g = m->growScale;
	float k = 1.0f <= g ? 1.0f : 0.0f; /* DAT_009b3680 */
	switch (m->id) {
	case 100: case 0x66: case 0x96: case 0x98: /* sprite 0x22d / 0x22e (S 96), +0xdc(1, k, k, k), scale (3-2g, g) */
		m_quad(texT(T, (m->id == 100 || m->id == 0x66) ? "swordr%d" : "swordb%d", (m->age / 2) % 6), bank, m, 48.0f,
		       3.0f - g * 2.0f, g, aos_rgba(k, k, k, 1.0f), AOS_BLEND_ALPHA);
		break;
	case 200: case 0xfa: { /* arrow 0x230 / 0x231 (S 128) + sword frame 6 glow (blend 2) once grown */
		bool red = m->id == 200;
		m_quad(texT(T, red ? "arrowr%02d" : "arrowb%02d", (m->age / 2) % 3 + 4), bank, m, 64.0f, 3.0f - g * 2.0f, g,
		       aos_rgba(k, k, k, 1.0f), AOS_BLEND_ALPHA);
		if (k == 1.0f) {
			bool even = (m->age / 2) % 2 == 0, palA = m->p18 == 0;
			float a = even ? 0.75f : 0.5f, sy = even ? g : g * 1.5f; /* DAT_00813f3c / DAT_00853b1c, DAT_00814110 */
			uint32_t rgba = red ? (palA ? aos_rgba(1.0f, 0.25f, 0.5f, a) : aos_rgba(1.0f, 0.5f, 0.25f, a))
			                    : (palA ? aos_rgba(0.25f, 0.5f, 1.0f, a) : aos_rgba(0.5f, 1.0f, 0.25f, a));
			m_quad(texT(T, red ? "swordr%d" : "swordb%d", 6), bank, m, 48.0f, 3.0f - g * 2.0f, sy, rgba, AOS_BLEND_ADD_ALPHA);
		}
		break;
	}
	case 300: case 0x15e: { /* ball 0x22f (second form 0x253 = mr_smkball), S 96: odd frame + additive even frame */
		float s = g;
		if (m->id == 0x15e)
			s = g * 2.0f * (aos_sin(((float)(m->age % 0x14) / 20.0f) * AOS_2PI) * 0.25f * m->f60 + 1.0f);
		int f = ((m->age / 2) % 6) * 2 + 1;
		float pulse = aos_sin(((float)(m->age % 0x32) / 50.0f) * AOS_2PI) * 0.35f + 0.65f; /* DAT_00813f2c, DAT_0081788c */
		int t1 = T == SB ? aos_texf("mr_smkball%02d", f) : texT(T, "beachball%02d", f);
		int t0 = T == SB ? aos_texf("mr_smkball%02d", f - 1) : texT(T, "beachball%02d", f - 1);
		m_quad(t1, bank, m, 48.0f, s, s, aos_rgba(1, 1, 1, 1), AOS_BLEND_ALPHA);
		m_quad(t0, bank, m, 48.0f, s, s, aos_rgba(1, 1, 1, pulse), AOS_BLEND_ADD_ALPHA);
		break;
	}
	case 600: /* second-form tentacle head: sprite 0x259 (S 96) frames (age/4)%3+6, glow 9 (FUN_0067e1a0) */
		if (T == SB) {
			float pulse = aos_sin(((float)(m->age % 0x32) / 50.0f) * AOS_2PI) * 0.35f + 0.65f;
			m_quad(texT(T, "tenta%02d", (m->age / 4) % 3 + 6), bank, m, 48.0f, g, g, aos_rgba(m->f60, m->f60, m->f60, 1.0f),
			       AOS_BLEND_ALPHA);
			m_quad(texT(T, "tenta%02d", 9), bank, m, 48.0f, g, g, aos_rgba(1, 1, 1, pulse), AOS_BLEND_ADD_ALPHA);
		}
		break;
	default:
		break;
	}
}

/* ---- hitboxes: spawn FUN_005edcb0 / FUN_005eee50, on-hit FUN_005ee310 / FUN_005ef3c0 ---- */

static int hitbox_spawn_t(aos_world *w, int32_t T, int32_t id, const float pos[3], float angle, int32_t owner, int32_t zero, int32_t team)
{
	int32_t dmg;
	aos_shape sh = {0, 0, 0};
	float kb;
	uint8_t once = 0;
	switch (id) {
	case 400: dmg = 300; sh.radius = 120.0f; kb = 20.0f; break;
	case 500: dmg = 300; sh.halfW = 120.0f; sh.halfH = 60.0f; kb = 10.0f; break;
	case 0x226: dmg = 0x3c; sh.halfW = 300.0f; sh.halfH = 16.0f; kb = 0; break;
	case 0x230: dmg = 0x3c; sh.radius = 40.0f; kb = 0; break;
	case 0x23a: dmg = 0x3c; sh.radius = 70.0f; kb = 0; break;
	case 600: case 0x262:
		if (T == SB) return -1;
		dmg = 0xa0; sh.radius = 60.0f; kb = 5.0f; break;
	case 0x136: case 0x168: dmg = 0x82; sh.radius = 120.0f; kb = 0; break;
	case 100: case 0x96: dmg = 0x96; sh.halfW = 60.0f; sh.halfH = 20.0f; kb = 5.0f; break;
	case 200: case 0xc9: dmg = T == SB ? 0x32 : 0x50; sh.halfW = 580.0f; sh.halfH = 30.0f; kb = 10.0f; break;
	case 0xfa:
		if (T == SB) return -1;
		dmg = 0x5a; sh.halfW = 580.0f; sh.halfH = 40.0f; once = 1; kb = 10.0f; break;
	case 0x137: dmg = 0; sh.radius = 120.0f; kb = 0; break;
	default: return -1;
	}
	int i = aos_hitbox_new(w, T, id, pos, angle, owner, zero, team);
	if (i < 0)
		return -1;
	aos_hitbox *h = &w->obj.hitbox[i];
	h->hitType = 3;
	h->damage = dmg;
	h->shape = sh;
	h->knockback = kb;
	h->oncePerFrame = once;
	return i;
}
static int hitbox_spawn_a(aos_world *w, int32_t id, const float pos[3], float a, int32_t o, int32_t z, int32_t t) { return hitbox_spawn_t(w, SA, id, pos, a, o, z, t); }
static int hitbox_spawn_b(aos_world *w, int32_t id, const float pos[3], float a, int32_t o, int32_t z, int32_t t) { return hitbox_spawn_t(w, SB, id, pos, a, o, z, t); }

static void wing_fx(aos_world *w, aos_hitbox *h, aos_player *t, int k)
{
	float r = aos_rand_real(w, -AOS_PI, AOS_PI);
	int32_t c = tcol(w, h->team, k);
	float pt[3] = {aos_rand_real(w, -50.0f, 50.0f) + t->x, 0, 0};
	pt[1] = aos_rand_real(w, -50.0f, 50.0f) + t->y;
	fx0(w, 0x9c4, c, pt, r, h->ownerId, 0, h->team);
	fx0(w, 0x514, c, pt, 0.0f, h->ownerId, 0, h->team);
	aos_se_cd(w, 0x16, 3);
}

static void hitbox_on_hit(aos_world *w, aos_hitbox *h, aos_player *t)
{
	int32_t c;
	switch (h->id) {
	case 400:
		aos_screen_blur(w, 10, 0.75f, 1);
		fx0(w, 0xa8d, 0xd, h->pos, h->angle, h->ownerId, 0, h->team);
		break;
	case 100: case 0x96:
		c = tcol(w, h->team, h->id == 100 ? 1 : 0);
		fx0(w, 0x9c4, c, h->pos, h->angle, h->ownerId, 0, h->team);
		aos_spawn_spark(w, 1000, c, 8, 8, h->angle, h->pos, h->ownerId);
		aos_se(w, 0x15);
		break;
	case 200: case 0xc9:
		if (h->damage != 0) {
			float r0 = aos_rand_real(w, -AOS_PI, AOS_PI);
			c = tcol(w, h->team, h->id == 200 ? 1 : 0);
			for (int k = 0; k < 3; k++) {
				float a = ((float)k / 3.0f) * AOS_2PI;
				float r = aos_rand_real(w, -0.25f, 0.25f);
				fx0(w, 0x456, c, &t->x, a + r + r0, h->ownerId, 0, h->team);
			}
			aos_spawn_spark(w, 1000, c, 10, 3, h->angle, &t->x, h->ownerId);
			aos_se_cd(w, 0x13, 3);
		}
		break;
	case 0xfa:
		if (h->damage != 0) {
			float r0 = aos_rand_real(w, -AOS_PI, AOS_PI);
			c = 0;
			for (int k = 0; k < 6; k++) {
				c = tcol(w, h->team, k % 2 == 0 ? 1 : 0);
				float a = ((float)k / 6.0f) * AOS_2PI;
				float r = aos_rand_real(w, -0.25f, 0.25f);
				fx0(w, 0x456, c, &t->x, a + r + r0, h->ownerId, 0, h->team);
			}
			aos_spawn_spark(w, 1000, c, 10, 3, h->angle, &t->x, h->ownerId);
			aos_se_cd(w, 0x13, 3);
		}
		break;
	case 0x136:
		wing_fx(w, h, t, 1);
		wing_fx(w, h, t, 0); /* literal fall-through into 0x168 */
		break;
	case 0x168:
		wing_fx(w, h, t, 0);
		break;
	case 500:
		c = tcol(w, h->team, 2);
		aos_spawn_particle(w, 1000, c, &t->x, h->angle, h->ownerId, 0, 10);
		aos_spawn_spark(w, 1000, c, 10, 10, h->angle, &t->x, h->ownerId);
		aos_se(w, 0x17);
		aos_screen_blur(w, 10, 0.75f, 1);
		break;
	case 0x226: case 0x230: case 0x23a:
		if (h->damage != 0) {
			c = tcol(w, h->team, 2);
			float r0 = aos_rand_real(w, -AOS_PI, AOS_PI);
			for (int k = -2; k < 2; k++) {
				float a = ((float)k / 2.0f) * AOS_PI;
				float r = aos_rand_real(w, -0.25f, 0.25f);
				fx0(w, 0x456, c, &t->x, a + r + r0, h->ownerId, 0, h->team);
			}
			aos_se_cd(w, 0x13, 3);
		}
		break;
	case 600: case 0x262:
		c = tcol(w, h->team, h->id == 600 ? 0 : 1);
		aos_spawn_particle(w, 1000, c, &t->x, h->angle, h->ownerId, 0, 5);
		aos_spawn_spark(w, 1000, c, 5, 5, h->angle, &t->x, h->ownerId);
		fx0(w, 0x9c6, c, h->pos, h->angle, h->ownerId, 0, h->team);
		fx0(w, 0xaf0, c, h->pos, h->angle, h->ownerId, 0, h->team);
		aos_se(w, 0x17);
		aos_screen_blur(w, 0x14, 0.75f, 2);
		break;
	default:
		break;
	}
}

/* ---- lasers: spawn FUN_00630490 / FUN_0062af40, update FUN_0062f6f0 / FUN_0062c7e0,
 *      draw FUN_0062d040 / FUN_0062b110, on-hit no-op ---- */

static int laser_spawn_t(aos_world *w, int32_t T, int32_t id, const float pos[3], float angle, int32_t owner, int32_t team, int32_t p18)
{
	switch (id) {
	case 100: case 0x96: case 300: break;
	case 200: case 0xc9: case 0xca: case 0xfa: case 0xfb: case 0xfc:
		if (T == SB) return -1;
		break;
	default: return -1;
	}
	int i = aos_laser_new(w, T, id, pos, angle, owner, team, p18);
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
static int laser_spawn_a(aos_world *w, int32_t id, const float pos[3], float a, int32_t o, int32_t t, int32_t p) { return laser_spawn_t(w, SA, id, pos, a, o, t, p); }
static int laser_spawn_b(aos_world *w, int32_t id, const float pos[3], float a, int32_t o, int32_t t, int32_t p) { return laser_spawn_t(w, SB, id, pos, a, o, t, p); }

static void laser_hitbox(aos_world *w, aos_laser *l, int32_t id, float dist)
{
	float pt[3] = {aos_cos(l->angle) * dist + l->origin[0], aos_sin(l->angle) * dist + l->origin[1], 0};
	int h = aos_spawn_hitbox(w, l->type, id, pt, l->angle, l->ownerId, 0, l->team);
	if (l->age % 4 != 1 && h >= 0)
		w->obj.hitbox[h].damage = 0;
}

static bool laser_update(aos_world *w, aos_laser *l)
{
	aos_player *o = team_player(w, l->team);
	switch (l->id) {
	case 100: case 0x96: {
		float ph = ((float)(l->age % 0x28) / 40.0f) * AOS_2PI - (l->id == 0x96 ? AOS_PI : 0.0f);
		float v[2] = {aos_cos(ph) * (float)l->age * 2.0f, aos_sin(ph) * (float)l->age * 2.0f};
		aos_vec_rotate(v, l->angle);
		l->origin[0] = o->x + v[0];
		l->origin[1] = o->y + v[1];
		if (l->age < 0x32 && l->age % 4 == 0)
			fx0(w, 0x546, tcol(w, l->team, l->id == 100 ? 1 : 0), l->origin, l->angle, l->ownerId, 0, l->team);
		l->nseg = 0;
		if (0x45 < l->age)
			return false;
		if (l->age > 9 && l->age < 0x33)
			laser_hitbox(w, l, l->id == 100 ? 200 : 0xc9, 600.0f);
		return true;
	}
	case 300:
		if (l->age == 0x32)
			aos_se_cd(w, 0x12, 5);
		l->nseg = 0;
		if (0x4f < l->age)
			return false;
		if (l->age > 0x31 && l->age < 0x47) {
			laser_hitbox(w, l, 0x226, 300.0f);
			laser_hitbox(w, l, 0x230, 0.0f);
			laser_hitbox(w, l, 0x23a, 450.0f); /* _DAT_008203b8 */
		}
		return true;
	default: {
		float off = 0;
		switch (l->id) {
		case 0xfa: off = 1.0471976f; break; /* DAT_008203bc */
		case 0xc9: off = 2.0943952f; break; /* DAT_00817898 */
		case 0xfb: off = AOS_PI; break;
		case 0xca: off = 4.1887903f; break; /* DAT_008203c0 */
		case 0xfc: off = 5.2359877f; break; /* DAT_008203c4 */
		default: break;
		}
		float ph = ((float)(l->age % 0x28) / 40.0f) * AOS_2PI + off;
		float v[2] = {aos_cos(ph) * (float)l->age * 2.0f, aos_sin(ph) * (float)l->age * 2.0f};
		aos_vec_rotate(v, o->faceAngle);
		l->origin[0] = o->x + v[0];
		l->origin[1] = o->y + v[1];
		l->angle = aos_sin(ph) * (((float)l->age * 0.5f) / 90.0f) + o->faceAngle;
		if (l->age < 0x46 && l->age % 4 == 0)
			fx0(w, 0x546, tcol(w, l->team, l->id < 0xfa ? 1 : 0), l->origin, l->angle, l->ownerId, 0, l->team);
		l->nseg = 0;
		if (0x59 < l->age)
			return false;
		if (l->age > 9 && l->age < 0x47)
			laser_hitbox(w, l, 0xfa, 600.0f);
		return true;
	}
	}
}

static void laser_on_hit(aos_world *w, aos_laser *l, int32_t seg, aos_player *t)
{
	(void)w, (void)l, (void)seg, (void)t;
}

static aos_dcmd *blit_src(int tex, int bank, float x, float y, float hw, float hh, float angle, uint32_t argb, int blend,
                          int x0, int y0, int x1, int y1)
{
	float pos[3] = {x, y, 0};
	aos_dcmd *c = blit(tex, bank, pos, hw, hh, angle, (int)(argb >> 24), argb & 0xffffffu, blend);
	c->hasSrc = 1;
	c->src[0] = (float)x0;
	c->src[1] = (float)y0;
	c->src[2] = (float)x1;
	c->src[3] = (float)y1;
	return c;
}

/* beamsphere muzzle (DAT_0087fde4 = "beamsphere"), colour-table cell c */
static void laser_muzzle(aos_laser *l, int32_t c, int fadeFrom, int fadeTo)
{
	float x = aos_cos(l->angle) * 20.0f + l->origin[0], y = aos_sin(l->angle) * 20.0f + l->origin[1]; /* DAT_008140fc */
	int tex = aos_tex("beamsphere");
	float h = 64.0f; /* DAT_008140f8 */
	if (fadeFrom < l->age)
		h = ((float)(fadeTo - l->age) / 20.0f) * h;
	if (l->age % 2 == 0)
		h *= 1.25f;
	int odd = (c % 10) % 2;
	blit_src(tex, AOS_BANK_COMMON, x, y, h, h, 0.0f, 0x60ffffffu, AOS_BLEND_ALPHA, odd * 100 + 0x32 + (c / 10) * 0xfa,
	         ((c % 10) / 2) * 100, odd * 100 + 0x95 + (c / 10) * 0xfa, ((c % 10) / 2) * 100 + 99);
	h = 36.0f; /* DAT_008140f4 */
	if (fadeFrom < l->age)
		h = ((float)(fadeTo - l->age) / 20.0f) * h;
	blit_src(tex, AOS_BANK_COMMON, x, y, h, h, 0.0f, 0xffffffffu, AOS_BLEND_ADD_ALPHA, (c / 10) * 0xfa, (c % 10) * 0x32,
	         (c / 10) * 0xfa + 0x31, (c % 10) * 0x32 + 0x31);
}

static void laser_draw(aos_world *w, aos_laser *l) /* FUN_0062d040 / FUN_0062b110 */
{
	int bank = team_bank(l->team);
	int32_t T = l->type;
	if (l->id == 300) { /* frame 1 of sa_laser / sab_laser: warning dots, then the burst */
		int tex = texT(T, "laser%d", 1);
		float h = (l->age / 2) % 2 == 0 ? 120.0f : 100.0f; /* DAT_00814174 / DAT_00853b90 */
		if (l->age < 0x1e)
			h = ((float)l->age / 30.0f) * h;
		if (0x46 < l->age)
			h = ((float)(0x50 - l->age) / 10.0f) * h;
		blit_src(tex, bank, l->origin[0], l->origin[1], h, h, 0.0f, 0xffffffffu, AOS_BLEND_ADD_ALPHA, 0x1c0, 0x1c0, 0x1ff, 0x1ff);
		for (int k = 2; k < 0xe; k++) {
			float x = aos_cos(l->angle) * (float)k * 30.0f + l->origin[0], y = aos_sin(l->angle) * (float)k * 30.0f + l->origin[1];
			float hh = 120.0f;
			if (l->age < 0x32) hh = 10.0f;
			else if (l->age < 0x3c) hh = ((float)(l->age - 0x32) / 10.0f) * hh;
			else if (0x46 < l->age) hh = ((float)(0x50 - l->age) / 10.0f) * hh;
			blit_src(tex, bank, x, y, 60.0f, hh, l->angle, 0x40ffffffu, AOS_BLEND_ADD_ALPHA, 0x140, 0x1c0, 0x17f, 0x1ff);
			hh = 20.0f;
			if (l->age < 0x32) hh = 5.0f;
			else if (l->age < 0x3c) hh = ((float)(l->age - 0x32) / 10.0f) * hh;
			else if (0x46 < l->age) hh = ((float)(0x50 - l->age) / 10.0f) * hh;
			blit_src(tex, bank, x, y, 100.0f, hh, l->angle, 0x40ffffffu, AOS_BLEND_ADD_ALPHA, 0x100, 0x1c0, 0x13f, 0x1ff);
		}
		float x = aos_cos(l->angle) * 15.0f * 30.0f + l->origin[0], y = aos_sin(l->angle) * 15.0f * 30.0f + l->origin[1];
		h = 300.0f; /* DAT_008145f0 */
		if (l->age < 0x32) h = 120.0f;
		else if (0x46 < l->age) h = ((float)(0x50 - l->age) / 10.0f) * h;
		if ((l->age / 2) % 2 == 0)
			h *= 1.2f; /* DAT_00817d78 */
		blit_src(tex, bank, x, y, h, h, 0.0f, 0xffffffffu, AOS_BLEND_ADD_ALPHA, 0x1c0, 0x1c0, 0x1ff, 0x1ff);
		return;
	}
	bool mystic = l->id == 100 || l->id == 0x96;
	int hold = mystic ? 0x33 : 0x47, end = mystic ? 0x46 : 0x5a;
	int row = (l->id == 100 || (l->id >= 200 && l->id < 0xfa)) ? 0 : 1; /* DAT_009b1520 */
	float hh;
	if (l->age < 0xb)
		hh = ((float)l->age * 60.0f) / 10.0f;
	else if (l->age < hold)
		hh = l->age % 2 == 0 ? 60.0f * 1.25f : 60.0f;
	else
		hh = ((float)(end - l->age) * 60.0f) / 20.0f;
	float x = aos_cos(l->angle) * 960.0f + l->origin[0], y = aos_sin(l->angle) * 960.0f + l->origin[1]; /* DAT_008178c8 */
	int tex = texT(T, "laser%d", 0);
	blit_src(tex, bank, x, y, 1000.0f, hh, l->angle, 0xc0ffffffu, AOS_BLEND_ALPHA, 0, row * 100, 0x1ff, row * 100 + 99);
	blit_src(tex, bank, x, y, 1000.0f * 0.95f, hh * 0.5f, l->angle, 0xffffffffu, AOS_BLEND_ADD_ALPHA, 0, row * 100 + 200, 0x1ff,
	         row * 100 + 299);
	laser_muzzle(l, tcol(w, l->team, row == 0 ? 1 : 0), mystic ? 0x32 : 0x46, end);
}

/* ---- beams: spawn FUN_004b8d80 / FUN_004b7660, update FUN_004b9b70 / FUN_004b8450,
 *      draw FUN_004b9050 / FUN_004b7930, on-hit FUN_004b96e0 / FUN_004b7fc0 ---- */

static int beam_spawn_t(aos_world *w, int32_t T, int32_t id, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team)
{
	if (id != 600 && id != 100 && id != 500 && id != 700 && id != 0x2c6)
		return -1;
	int i = aos_beam_new(w, T, id, pos, angle, owner, age0, team);
	if (i < 0)
		return -1;
	aos_beam *b = &w->obj.beam[i];
	switch (id) {
	case 600:
		b->speed = 0;
		b->scale = 5.0f;
		b->active = 0;
		b->damage = 0x78;
		b->shape = (aos_shape){0, 0, 16.0f};
		b->stun = 0.75f;
		b->knockback = 5.0f;
		break;
	case 100:
		b->speed = 10.0f;
		b->alpha = 0;
		b->scale = 5.0f;
		b->damage = 0x78;
		b->shape = (aos_shape){20.0f, 8.0f, 0};
		b->knockback = 10.0f;
		break;
	case 500:
		b->speed = 18.0f;
		b->alpha = 0;
		b->scale = 1.0f;
		b->damage = 100;
		b->shape = (aos_shape){20.0f, 6.0f, 0};
		b->stun = 0.75f;
		b->knockback = 5.0f;
		break;
	default:
		b->speed = 12.0f;
		b->f6c = 0.004f; /* DAT_00813ea4 */
		b->damage = 0x96;
		b->shape = (aos_shape){24.0f, 8.0f, 0};
		b->knockback = 10.0f;
		break;
	}
	return i;
}
static int beam_spawn_a(aos_world *w, int32_t id, const float pos[3], float a, int32_t o, int32_t g, int32_t t) { return beam_spawn_t(w, SA, id, pos, a, o, g, t); }
static int beam_spawn_b(aos_world *w, int32_t id, const float pos[3], float a, int32_t o, int32_t g, int32_t t) { return beam_spawn_t(w, SB, id, pos, a, o, g, t); }

static void b_move(aos_beam *b)
{
	b->pos[0] = aos_cos(b->moveAngle) * b->speed + b->pos[0];
	b->pos[1] = aos_sin(b->moveAngle) * b->speed + b->pos[1];
}

static bool beam_update(aos_world *w, aos_beam *b)
{
	int32_t T = b->type;
	switch (b->id) {
	case 600:
		if (b->scale <= 1.0f) {
			if (b->speed < 5.0f)
				b->speed += 0.1f;
			b_move(b);
			if (!aos_in_arena(b->pos, 20.0f))
				return false;
		} else {
			b->scale -= 0.2f;
			if (b->scale <= 1.0f) {
				b->scale = 1.0f;
				b->speed = 0;
				b->active = 1;
			}
		}
		return true;
	case 100:
		if (b->age < 1000) {
			b->alpha += 0.1f;
			b->scale -= 0.4f;
			if (1.0f <= b->alpha) {
				b->alpha = b->scale = 1.0f;
				b->age = 1000;
				fx0(w, 0x516, tcol(w, b->team, 0), b->pos, 0.0f, b->ownerId, 0, b->team);
			}
			return true;
		}
		b_move(b);
		if (b->age < 2000 && !aos_in_arena(b->pos, 0.0f)) {
			float a = aos_atan2(b->pos[1], b->pos[0]);
			b->moveAngle = (a - b->moveAngle) + a + AOS_PI; /* FUN_005f0ce0 result discarded (literal) */
			b->pos[0] = aos_cos(a) * 540.0f;
			b->pos[1] = aos_sin(a) * 540.0f;
			b->angle = b->moveAngle;
			b->age += 1000;
			aos_se_cd(w, 0x25, 5);
			fx0(w, 0x516, tcol(w, b->team, 0), b->pos, 0.0f, b->ownerId, 0, b->team);
		}
		if (!aos_in_arena(b->pos, 10.0f))
			return false;
		if (0 <= b->age) /* `age & 0x80000000` */
			aos_spawn_effect(w, T, 100, tcol(w, b->team, 0), b->pos, b->angle, b->ownerId, 0, b->team);
		return true;
	case 500: {
		b_move(b);
		if (0x14 < b->age && !aos_in_arena(b->pos, 10.0f))
			return false;
		int32_t c = tcol(w, b->team, 1);
		aos_spawn_effect(w, T, 500, c, b->pos, b->angle, b->ownerId, 0, b->team);
		if (b->age == 1) {
			fx0(w, 0x547, c, b->pos, b->angle, b->ownerId, 0, b->team);
			fx0(w, 0x548, c, b->pos, b->angle, b->ownerId, 0, b->team);
		}
		return true;
	}
	default: { /* 700 / 0x2c6 */
		if (b->age < 0x32) {
			aos_player *o = aos_opponent(w, team_player(w, b->team));
			aos_angle_toward(&b->moveAngle, aos_atan2(o->y - b->pos[1], o->x - b->pos[0]),
			                 (50.0f - (float)b->age) * b->f6c * 0.5f);
			b->angle = b->moveAngle;
		}
		if (b->age < 0x1e)
			b->speed *= 0.95f;
		else if (b->speed < 20.0f)
			b->speed += 0.25f;
		b_move(b);
		if (10 < b->age && !aos_in_arena(b->pos, 30.0f))
			return false;
		int32_t c = tcol(w, b->team, b->id == 700 ? 0 : 1);
		aos_spawn_effect(w, T, 300, c, b->pos, b->angle, b->ownerId, 0, b->team);
		if (b->age == 1) {
			fx0(w, 0x546, c, b->pos, b->angle, b->ownerId, 0, b->team);
			fx0(w, 0x547, c, b->pos, b->angle, b->ownerId, 0, b->team);
		}
		return true;
	}
	}
}

static void beam_on_hit(aos_world *w, aos_beam *b)
{
	int k, cnt, sl;
	int32_t eid;
	switch (b->id) {
	case 600: k = 2, eid = 1000, cnt = 3, sl = 10; break;
	case 100: k = 0, eid = 0x3f2, cnt = 5, sl = 6; break;
	case 500: k = 1, eid = 0x3f2, cnt = 5, sl = 6; break;
	case 700: k = 0, eid = 1000, cnt = 6, sl = 6; break;
	case 0x2c6: k = 1, eid = 1000, cnt = 6, sl = 6; break;
	default: return;
	}
	int32_t c = tcol(w, b->team, k);
	fx0(w, eid, c, b->pos, b->angle + AOS_PI, b->ownerId, 0, b->team);
	aos_spawn_particle(w, 1000, c, b->pos, b->angle, b->ownerId, 0, cnt);
	aos_spawn_spark(w, 1000, c, sl, b->id == 600 ? 5 : 6, b->angle, b->pos, b->ownerId);
	aos_se_cd(w, 0x13, (b->id == 100 || b->id == 500) ? 4 : 3);
}

/* beamsphere ("DAT_0087fde4") glow + core for colour-table cell c (FUN_004b9050 pattern) */
static void sphere(float x, float y, int32_t c, float glowHalf, int glowA, int glowBlend, float coreHalf, int coreA)
{
	int tex = aos_tex("beamsphere");
	int odd = (c % 10) % 2;
	blit_src(tex, AOS_BANK_COMMON, x, y, glowHalf, glowHalf, 0.0f, (uint32_t)(glowA & 0xff) << 24 | 0xffffffu, glowBlend,
	         odd * 100 + 0x32 + (c / 10) * 0xfa, ((c % 10) / 2) * 100, odd * 100 + 0x95 + (c / 10) * 0xfa,
	         ((c % 10) / 2) * 100 + 99);
	blit_src(tex, AOS_BANK_COMMON, x, y, coreHalf, coreHalf, 0.0f, (uint32_t)(coreA & 0xff) << 24 | 0xffffffu, AOS_BLEND_ADD_ALPHA,
	         (c / 10) * 0xfa, (c % 10) * 0x32, (c / 10) * 0xfa + 0x31, (c % 10) * 0x32 + 0x31);
}

static void beam_draw(aos_world *w, aos_beam *b) /* FUN_004b9050 / FUN_004b7930 (500 / 700 draw nothing: their bolts are effects) */
{
	if (b->id == 100 && b->age < 1000)
		sphere(b->pos[0], b->pos[1], tcol(w, b->team, 0), 24.0f * b->scale, (int)(96.0f * b->alpha), AOS_BLEND_ALPHA,
		       10.0f * b->scale, (int)(255.0f * b->alpha));
	else if (b->id == 600)
		sphere(b->pos[0], b->pos[1], tcol(w, b->team, 2), 40.0f * b->scale, (int)(96.0f * b->alpha), AOS_BLEND_ADD_ALPHA,
		       15.0f * b->scale, (int)(255.0f * b->alpha));
}

/* ---- effects: spawn FUN_00511880 / FUN_0050ee70, update FUN_00513c60 / FUN_00511240,
 *      draw FUN_00511e00 / FUN_0050f3e0 ---- */

static int effect_spawn_t(aos_world *w, int32_t T, int32_t id, int32_t variant, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team)
{
	int nid = id, layer = 1;
	float alpha = 1.0f, scale = 1.0f, moveDir = angle, aux2 = 0;
	switch (id) {
	case 100: moveDir = 1.0f; break;
	case 0x96: case 0xa0: alpha = 0; scale = 2.0f; break;
	case 200: case 0xc9: case 0xca: case 0xcb: case 0xcc: case 0xcd:
	case 0xd2: case 0xd3: case 0xd4: case 0xd5: case 0xd6: case 0xd7: layer = 0; break;
	case 300: case 500: case 0x1fe: break;
	case 700: case 0x2bd: case 0x2ee: case 0x2ef:
		aux2 = id % 10 == 1 ? 0.1f : -0.1f;
		nid = id - id % 10;
		break;
	case 0x2be: case 0x2f0: layer = 0; break;
	default: return -1;
	}
	int i = aos_effect_new(w, T, nid, variant, pos, angle, owner, age0, team);
	if (i < 0)
		return -1;
	aos_effect *e = &w->obj.effect[i];
	e->drawLayer = layer;
	e->alpha = alpha;
	e->scale = scale;
	e->moveDir = moveDir;
	e->aux = 0;
	e->aux2 = aux2;
	return i;
}
static int effect_spawn_a(aos_world *w, int32_t id, int32_t v, const float pos[3], float a, int32_t o, int32_t g, int32_t t) { return effect_spawn_t(w, SA, id, v, pos, a, o, g, t); }
static int effect_spawn_b(aos_world *w, int32_t id, int32_t v, const float pos[3], float a, int32_t o, int32_t g, int32_t t) { return effect_spawn_t(w, SB, id, v, pos, a, o, g, t); }

static bool effect_update(aos_world *w, aos_effect *e)
{
	switch (e->id) {
	case 100:
		if (2 < e->age) {
			e->alpha -= 0.1f;
			e->scale -= 0.075f;
		}
		return 0.0f < e->alpha;
	case 0x96: case 0xa0:
		if (e->age < 100) {
			e->alpha += 0.1f;
			e->scale -= 0.1f;
			if (1.0f < e->scale)
				return true;
			e->alpha = e->scale = 1.0f;
			e->age = 100;
			e->drawLayer = 0;
			return true;
		}
		if (e->age < 0x78)
			return true;
		e->alpha -= 0.05f;
		e->scale -= 0.05f;
		return 0.0f < e->scale;
	case 300:
		if (2 < e->age)
			e->alpha -= 0.1f;
		return 0.0f < e->alpha;
	case 500: case 0x1fe:
		if (3 < e->age)
			e->alpha -= 0.1f;
		return 0.0f < e->alpha;
	case 700: case 750: {
		aos_player *o = team_player(w, e->team);
		e->moveDir += e->aux2;
		e->pos[0] = aos_cos(e->moveDir) * 70.0f * ((float)e->age / 8.0f) + o->x; /* DAT_008145ec */
		e->pos[1] = (aos_sin(e->moveDir) * 70.0f * ((float)e->age / 8.0f) + o->y) - 10.0f;
		e->scale = ((float)e->age * 140.0f) / 8.0f;
		e->aux = ((float)e->age * 100.0f) / 15.0f;
		e->alpha = e->age < 0x10 ? 1.0f : (float)(0x19 - e->age) / 10.0f;
		e->rotation = e->moveDir;
		if (0x17 < e->age)
			return false;
		aos_effect cp = *e;
		int n = aos_spawn_effect(w, cp.type, cp.id + 2, 0, cp.pos, cp.rotation, cp.ownerId, 0, cp.team);
		if (n >= 0) {
			w->obj.effect[n].scale = cp.scale;
			w->obj.effect[n].aux = cp.aux;
			w->obj.effect[n].aux2 = cp.alpha;
		}
		return true;
	}
	case 0x2be: case 0x2f0:
		e->alpha -= 0.25f;
		return 0.0f < e->alpha;
	default:
		e->alpha -= 0.1f; /* 200..0xd7 */
		return 0.0f < e->alpha;
	}
}

static int a8(float v) { return v < 0 ? 0 : v > 255 ? 255 : (int)v; }

/* bolt flash 300 / 500 / 0x1fe: beamemitb (DAT_0087fdf4) glow + core, beamemit (DAT_0087fdf0) for the first 2 frames */
static void bolt_draw(aos_effect *e, float glowH, float coreH, float flashH, int tintFrom, uint32_t tint)
{
	int32_t v = e->variant, col = v / 8, row = v % 8;
	int tb = aos_tex("beamemitb");
	float x = e->pos[0], y = e->pos[1];
	blit_src(tb, AOS_BANK_COMMON, x, y, 60.0f, glowH, e->rotation, (uint32_t)a8(32.0f * e->alpha) << 24 | 0xffffff,
	         AOS_BLEND_ALPHA, col * 0xf0, row * 0x40, col * 0xf0 + 0x77, row * 0x40 + 0x3f); /* DAT_00813a58, DAT_008140dc */
	uint32_t a = (uint32_t)a8(255.0f * e->alpha) << 24; /* DAT_00853b94 */
	if (e->age < 2)
		blit_src(aos_tex("beamemit"), AOS_BANK_COMMON, x, y, 48.0f, flashH, e->rotation, 0xffffffffu, AOS_BLEND_ADD_ALPHA,
		         col * 0x100, row * 0x40, col * 0x100 + 0x7f, row * 0x40 + 0x3f); /* DAT_008140e4 */
	else if (e->age < tintFrom || tint)
		blit_src(tb, AOS_BANK_COMMON, x, y, 40.0f, coreH, e->rotation, a | (e->age < tintFrom ? 0xffffffu : tint & 0xffffffu),
		         AOS_BLEND_ADD_ALPHA, col * 0xf0 + 0x78, row * 0x40, col * 0xf0 + 0xef, row * 0x40 + 0x3f); /* DAT_008140e0 */
}

static void effect_draw(aos_world *w, aos_effect *e) /* FUN_00511e00 / FUN_0050f3e0 (identical but for sprite ids) */
{
	int bank = team_bank(e->team);
	int32_t T = e->type;
	switch (e->id) {
	case 500: /* core tint 0xff80e1 after age 6 */
		bolt_draw(e, 40.0f, 8.0f, 15.0f, 6, 0x01ff80e1u); /* DAT_008140e0, DAT_008140d4, DAT_00853b88 */
		break;
	case 0x1fe: /* core tint 0x0080ff after age 6 */
		bolt_draw(e, 40.0f, 8.0f, 15.0f, 6, 0x010080ffu);
		break;
	case 300: /* taller (80/16/32); tint after age 5 by colour 0 / 1 (other colours: core not drawn) */
		bolt_draw(e, 80.0f, 16.0f, 32.0f, 5,
		          e->variant == 0 ? 0x018080ffu : e->variant == 1 ? 0x01ff8080u : 0); /* DAT_00814164, DAT_008140d8 */
		break;
	case 0x96: case 0xa0: { /* explosion atlas DAT_0087fccc[v/8], cell (v%8)&3 row, (v%8)/4 column; rotation un-negated */
		int32_t v = e->variant, r = v % 8;
		int tex = aos_tex((v / 8) % 2 ? "explosion1" : "explosion0");
		int x0 = (r / 4) * 0x100, y0 = (r & 3) * 0x80;
		bool big = e->id == 0xa0;
		float h = (big ? 270.0f : 180.0f) * e->scale; /* DAT_008178ec / DAT_00815258 */
		blit_src(tex, AOS_BANK_COMMON, e->pos[0], e->pos[1], h, h, -e->rotation,
		         (uint32_t)a8(128.0f * e->alpha) << 24 | 0xffffff, AOS_BLEND_ADD_ALPHA, x0, y0, x0 + 0x7f, y0 + 0x7f);
		h = (big ? 300.0f : 200.0f) * e->scale; /* DAT_008145f0 / DAT_008147cc */
		if (e->age % 2 == 0)
			h *= 0.85f; /* DAT_00814120 */
		blit_src(tex, AOS_BANK_COMMON, e->pos[0], e->pos[1], h, h, -e->rotation,
		         (uint32_t)a8(255.0f * e->alpha) << 24 | 0xffffff, AOS_BLEND_ADD_ALPHA, x0 + 0x80, y0, x0 + 0xff, y0 + 0x7f);
		break;
	}
	case 200: case 0xc9: case 0xca: case 0xcb: case 0xcc: case 0xcd:
	case 0xd2: case 0xd3: case 0xd4: case 0xd5: case 0xd6: case 0xd7: { /* sword 0x22d/0x22e (SB 0x251/0x252), frame id%10 */
		float h = 48.0f * e->scale; /* DAT_008140e4 */
		blit(texT(T, e->id < 0xd2 ? "swordr%d" : "swordb%d", e->id % 10), bank, e->pos, h, h, e->rotation,
		     a8(128.0f * e->alpha), 0xffffff, AOS_BLEND_ADD_ALPHA);
		break;
	}
	case 700: case 750: case 0x2be: case 0x2f0: { /* wing 0x234 (SB 600), frame 0 for 700/0x2be else 1; half (scale/2, aux) */
		bool trail = e->id == 0x2be || e->id == 0x2f0;
		int a = trail ? a8(128.0f * e->alpha * e->aux2) : a8(255.0f * e->alpha); /* DAT_00814134 / DAT_00853b94 */
		blit(texT(T, "wing%d", (e->id == 700 || e->id == 0x2be) ? 0 : 1), bank, e->pos, e->scale * 0.5f, e->aux, e->rotation,
		     a, 0xffffff, AOS_BLEND_ADD_ALPHA); /* DAT_00853b1c, DAT_00853b34 */
		break;
	}
	default: /* 100: no draw case in the original */
		(void)w;
		break;
	}
}

/* ================================================================== */
/* 6. registration                                                     */

static void reg_objects(int32_t T)
{
	bool b = T == SB;
	aos_otab.beamSpawn[T] = b ? beam_spawn_b : beam_spawn_a;
	aos_otab.beamUpdate[T] = beam_update;
	aos_otab.beamDraw[T] = beam_draw;
	aos_otab.beamOnHit[T] = beam_on_hit;
	aos_otab.missileSpawn[T] = b ? missile_spawn_b : missile_spawn_a;
	aos_otab.missileUpdate[T] = missile_update;
	aos_otab.missileDraw[T] = missile_draw;
	aos_otab.missileOnHit[T] = missile_on_hit;
	aos_otab.missileOnKilled[T] = missile_on_killed;
	aos_otab.hitboxSpawn[T] = b ? hitbox_spawn_b : hitbox_spawn_a;
	aos_otab.hitboxOnHit[T] = hitbox_on_hit;
	aos_otab.laserSpawn[T] = b ? laser_spawn_b : laser_spawn_a;
	aos_otab.laserUpdate[T] = laser_update;
	aos_otab.laserDraw[T] = laser_draw;
	aos_otab.laserOnHit[T] = laser_on_hit;
	aos_otab.enemySpawn[T] = b ? enemy_spawn_b : enemy_spawn_a;
	aos_otab.enemyUpdate[T] = enemy_update;
	aos_otab.enemyDraw[T] = enemy_draw;
	aos_otab.enemyOnKilled[T] = enemy_killed;
	aos_otab.effectSpawn[T] = b ? effect_spawn_b : effect_spawn_a;
	aos_otab.effectUpdate[T] = effect_update;
	aos_otab.effectDraw[T] = effect_draw;
	aos_otab.whipSpawn[T] = b ? whip_spawn_b : whip_spawn_a;
	aos_otab.whipUpdate[T] = whip_update;
	aos_otab.whipDraw[T] = whip_draw;
}

void aos_register_sumika(void)
{
	static const struct { int s; aos_update_fn t2; aos_draw_fn t1; } tab[] = {
		/* Sumika (FUN_0049ae40 registry, T1 in the Tsih block 0x77c540..) */
		{0x211, st_neutral, dr_neutral},        {0x212, st_stub, dr_dash},
		{0x213, st_stub, dr_ko},                {0x214, st_stub, dr_hit},
		{0x215, st_stub, dr_sld},               {0x21b, st_pretty_sword, dr_sld},
		{0x21c, st_coquettish, dr_acta},        {0x21d, st_intro, dr_sld},
		{0x21e, st_fantastic, dr_sld},          {0x21f, st_shiny, dr_sld},
		{0x220, st_over_limit, dr_acta},        {0x221, st_heavens, dr_acta},
		{0x222, st_heartful, dr_sld},           {0x224, st_marble, dr_sld},
		{0x225, st_tropical, dr_sld},           {0x226, st_barrel, dr_ball15},
		{0x227, st_mystic, dr_ball10},          {0x228, st_angel_wing, dr_acta},
		{0x229, st_eternal, dr_acta},           {0x22a, st_pretty_justice, dr_ball10},
		/* second form (T1 in the Sumika block 0x773300..) */
		{0x235, st_neutral, dr_neutral},        {0x236, st_sb_hook, dr_dash},
		{0x237, st_stub, dr_ko},                {0x238, st_sb_hook, dr_hit},
		{0x239, st_stub, dr_sld},               {0x23f, st_pretty_sword, dr_sld},
		{0x240, st_coquettish, dr_acta},        {0x241, st_intro, dr_sld},
		{0x242, st_fantastic, dr_sld},          {0x243, st_shiny, dr_sld},
		{0x244, st_over_limit, dr_acta},        {0x245, st_heavens, dr_acta},
		{0x246, st_heartful, dr_sld},           {0x248, st_marble, dr_sld},
		{0x249, st_tropical, dr_sld},           {0x24a, st_barrel, dr_ball15},
		{0x24b, st_mystic, dr_ball10},          {0x24c, st_angel_wing, dr_acta},
		{0x24d, st_sb_transition, dr_acta},     {0x24e, st_sb_final, dr_acta},
	};
	for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++) {
		aos_tab.T2[tab[i].s] = tab[i].t2;
		aos_tab.T1[tab[i].s] = tab[i].t1;
	}
	aos_tab.inputA[SA] = input_a;
	aos_tab.inputB[SA] = input_b;
	aos_tab.inputA[SB] = input_sb;
	aos_tab.inputB[SB] = input_sb;
	/* CPU AI FUN_0077bd50 / 0077b420 and FUN_00772ae0 / 007721f0: not ported */
	reg_objects(SA);
	reg_objects(SB);
}

/* launchers (defined after the enemy helpers they use) */

static int launch_pick(aos_world *w, int32_t T, int32_t team, bool hyper, bool eternal)
{
	for (int i = w->obj.nenemy - 1; i >= 0; i--) {
		aos_enemy *e = &w->obj.enemy[i];
		if (e->team != team || e->type != T || (e->f10 != 100 && e->f10 != 0x6e))
			continue;
		if (eternal) {
			if (e->f84 == 400 || e->f84 == 0x19a || e->f84 == 0x1a4)
				continue;
		} else if (e->f84 != 100 && (!hyper || e->f84 % 10 != 0)) {
			continue;
		}
		return i;
	}
	return -1;
}

static void launch_aim(aos_world *w, aos_enemy *e, const float P[2], float div)
{
	aos_player *opp = aos_opponent(w, team_player(w, e->team));
	float a = aos_atan2(opp->y - P[1], opp->x - P[0]) + AOS_PI;
	aos_angle_normalize(&a);
	e->angle2 = aos_atan2(P[1] - e->pos[1], P[0] - e->pos[0]);
	e->angle = a;
	float dx = P[0] - e->pos[0], dy = P[1] - e->pos[1];
	e->f70 = aos_sqrt(dx * dx + dy * dy) / div;
	e->scratch[0] = e->angle <= 0.0f ? -1.0f : 1.0f;
	int32_t T = e->type;
	aos_player *own = team_player(w, e->team);
	whip_new(w, T, (T == SB ? 0xb : 7) + (e->f10 == 100 ? 0 : 1), own->color, e->id);
}

static int launch_fantastic(aos_world *w, int32_t T, int32_t team, bool hyper)
{
	int i = launch_pick(w, T, team, hyper, false);
	if (i < 0)
		return -1;
	aos_enemy *e = &w->obj.enemy[i];
	e->f84 = 200 + (hyper ? 1 : 0);
	e->timer2 = 0;
	if (e->hp < 1000)
		e->hp--;
	aos_player *opp = aos_opponent(w, team_player(w, team));
	float d = aos_rand_real(w, 80.0f, 320.0f); /* DAT_00814164, DAT_008178c0 */
	float a = aos_rand_real(w, -AOS_PI, AOS_PI);
	float P[2] = {aos_cos(a) * d + opp->x, aos_sin(a) * d + opp->y};
	launch_aim(w, e, P, 50.0f);
	return i;
}

static int launch_shiny(aos_world *w, int32_t T, int32_t team, bool hyper)
{
	int i = launch_pick(w, T, team, hyper, false);
	if (i < 0)
		return -1;
	aos_enemy *e = &w->obj.enemy[i];
	e->f84 = 300 + (hyper ? 1 : 0);
	e->timer2 = 0;
	if (e->hp < 1000)
		e->hp--;
	aos_player *opp = aos_opponent(w, team_player(w, team)), *own = team_player(w, team);
	float d = aos_rand_real(w, 160.0f, 240.0f); /* DAT_00814168, DAT_00815584 */
	float a = aos_rand_real(w, -AOS_PI, AOS_PI);
	if (T == SA && !hyper) { /* ver > 0x5a0 */
		d = 0;
		a = AOS_PI;
		bool u = own->up > 1, dn = own->down > 1, r = own->right > 1, l = own->left > 1;
		if (u && r) d = -200.0f, a = 3.926991f;        /* DAT_00826284 */
		else if (u && l) d = 200.0f, a = 2.3561945f;   /* DAT_0081fa94 */
		else if (dn && r) d = -200.0f, a = 2.3561945f;
		else if (dn && l) d = 200.0f, a = 3.926991f;
		else if (u) d = 200.0f, a = HALF_PI;
		else if (dn) d = -200.0f, a = HALF_PI;
		else if (r) d = -200.0f;
		else if (l) d = 200.0f;
	}
	const aos_player *c = hyper ? opp : own;
	float P[2] = {aos_cos(a) * d + c->x, aos_sin(a) * d + c->y};
	launch_aim(w, e, P, 25.0f);
	return i;
}

static int launch_eternal(aos_world *w, int32_t T, int32_t team, bool hyper)
{
	int i = launch_pick(w, T, team, hyper, true);
	if (i < 0)
		return -1;
	aos_enemy *e = &w->obj.enemy[i];
	e->f84 = 400;
	e->timer2 = 0;
	if (e->hp < 1000)
		e->hp--;
	aos_player *opp = aos_opponent(w, team_player(w, team)), *own = team_player(w, team);
	float d = aos_rand_real(w, 160.0f, 240.0f);
	float a = aos_rand_real(w, -AOS_PI, AOS_PI);
	const aos_player *c = hyper ? opp : own;
	float P[2] = {aos_cos(a) * d + c->x, aos_sin(a) * d + c->y};
	launch_aim(w, e, P, 25.0f);
	return i;
}
