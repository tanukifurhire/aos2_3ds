/* Core match logic, ported from docs/re/core.md (function addresses on each
 * routine). Character code lives in src/sim/chars/ and registers itself into
 * aos_tab / aos_otab from aos_register_all(). */
#include <string.h>

#include "sim.h"
#include "fx.h"

aos_tables aos_tab;

void aos_register_costs(void);   /* costs.c */
void aos_register_fx(void);      /* fx.c: common effect type 0 */
void aos_register_chars(void);   /* chars/registry.c */
void aos_fx_tick(aos_world *w);  /* fx.c: particles, sparks, popups (in object step) */



/* ------------------------------------------------------------------ */
/* small helpers                                                       */

aos_player *aos_opponent(aos_world *w, const aos_player *p)
{
	return p->side == 1 ? &w->p[1] : &w->p[0];
}

float aos_rand_real(aos_world *w, float a, float b) { return aos_rand_f(&w->rng, a, b); }
int32_t aos_rand_int(aos_world *w, int32_t lo, int32_t hi) { return aos_rand_i(&w->rng, lo, hi); }

static void se_push(aos_world *w, int id, int cd)
{
	if (id < 0 || w->se_count >= sizeof w->se)
		return;
	for (int i = 0; i < w->se_count; i++)
		if (w->se[i] == id)
			return; /* each id at most once per frame (FUN_007bc9c0/FUN_007bcaf0); first request wins */
	w->se_cd[w->se_count] = (uint8_t)cd;
	w->se[w->se_count++] = (uint8_t)id;
}

void aos_se(aos_world *w, int id) { se_push(w, id, AOS_SE_CD_DEFAULT); }

void aos_se_cd(aos_world *w, int id, int cd)
{
	/* the retrigger lockout itself is applied by the platform mixer (audio_se) */
	se_push(w, id, cd < 0 ? 0 : cd > 0xfe ? 0xfe : cd);
}

void aos_set_state(aos_world *w, aos_player *p, int32_t s)
{
	p->state = s;
	p->timer = 0;
	p->hitFlag = 0;
	if (aos_tab.T2[s])
		aos_tab.T2[s](w, p);
}

float aos_dir_angle(const aos_player *p)
{
	aos_dirs d = {p->right, p->left, p->up, p->down};
	return aos_input_angle(&d);
}

bool aos_spend_hyper(aos_player *p, float cost) /* FUN_00780910 */
{
	if (p->hyper >= cost) {
		p->hyper -= cost;
		return true;
	}
	return false;
}

bool aos_may_hyper(aos_world *w, aos_player *p) /* FUN_00780950 */
{
	aos_player *o = aos_opponent(w, p);
	if (p->state == AOS_ST_KO || p->state == AOS_ST_HITSTUN)
		return false;
	if (p->barrierType == 3 && p->barrierTime > 80)
		return false;
	if (p->barrierType == 4 && p->barrierTime != 0)
		return false;
	if (o->barrierType == 4 && o->barrierTime >= 1)
		return false;
	return true;
}

bool aos_may_shield(aos_world *w, aos_player *p) /* FUN_007809e0 */
{
	aos_player *o = aos_opponent(w, p);
	if (o->barrierType == 4 && o->barrierTime >= 1)
		return false;
	if (w->roundOver)
		return false;
	if (p->scheme == 1 && p->H > 1)
		return false;
	return true;
}

bool aos_use_gauge(aos_player *p, int32_t s) /* FUN_007807f0 */
{
	const aos_cost *c = &aos_tab.cost[s];
	if (p->gaugeA >= c->a && p->gaugeB >= c->b) {
		p->gaugeA -= c->a;
		p->gaugeB -= c->b;
		p->hyper += c->hyperGain * p->hyperMul;
		return true;
	}
	return false;
}

float aos_aim(aos_world *w, aos_player *p) /* FUN_007804b0 */
{
	aos_player *o = aos_opponent(w, p);
	if (o->id == AOS_CH_TSIH && o->scratch[0x164 - 0x15c] == 1) {
		/* stealthed Tsih: aim follows input, or toward the opponent's side */
		float a = aos_dir_angle(p);
		if (a < AOS_2PI)
			return a;
		return o->x <= p->x ? AOS_PI : 0.0f;
	}
	return aos_atan2(o->y - p->y, o->x - p->x);
}

float aos_dist(aos_world *w, aos_player *p) /* FUN_00780570 */
{
	aos_player *o = aos_opponent(w, p);
	float dx = o->x - p->x, dy = o->y - p->y;
	return aos_sqrt(dx * dx + dy * dy);
}

void aos_heal(aos_player *p, float amount) /* FUN_007808a0 */
{
	p->hp += (int32_t)(amount * 0.1f);
	if (p->hp > p->recovHp)
		p->hp = p->recovHp;
	p->redHp = p->hp;
}

static void effect0(aos_world *w, int id, const float pos[3], float angle, const aos_player *p)
{
	aos_spawn_effect(w, 0, id, 0, pos, angle, p->objId, 0, p->side);
}

void aos_dash(aos_world *w, aos_player *p, int cancel) /* FUN_006b17d0 */
{
	if (p->statusOn[AOS_STATUS_NODASH])
		return;
	p->heat += cancel ? 0.25f : 0.3f;
	if (cancel)
		aos_popup(w, 0, &p->x, p->objId);
	aos_se(w, 8);
	float a = aos_dir_angle(p);
	p->dashAngle = a;
	if (a > AOS_2PI)
		p->dashAngle = p->faceAngle = p->facingRight ? 0.0f : AOS_PI;
	p->state = AOS_ST_DASH;
	p->timer = 0;
	p->speed = aos_tab.stats[p->id].dashSpeed;
	p->moveAngle = p->dashAngle;
	aos_afterimage_clear(w, p->objId); /* FUN_004d7ff0 */
}

void aos_barrier(aos_world *w, aos_player *p, int kind) /* FUN_006b1b40 */
{
	(void)w;
	switch (kind) {
	case 0: p->barrierType = 1; p->barrierTime = 60; p->barrierR = 120.0f; break;
	case 1: p->barrierType = 2; p->barrierTime = 120; p->barrierR = 200.0f; break;
	case 2: p->barrierType = 5; p->barrierTime = 60; p->barrierR = 160.0f; break;
	case 10: p->barrierType = 3; p->barrierTime = 100; p->barrierR = 160.0f; break;
	case 0x14: p->barrierType = 4; p->barrierTime = 100; p->barrierR = 160.0f; break;
	default: break; /* the original always runs the tail below (FUN_006b1b40) */
	}
	p->barrierScale = 0;
	p->barrierRot = 0;
	p->barrierBreakT = 50;
	if (aos_tab.barrierHook[p->id]) /* Alte: hyper barrier makes her panel invulnerable (FUN_006b1b40 tail) */
		aos_tab.barrierHook[p->id](w, p, 0);
}

void aos_shield(aos_world *w, aos_player *p) /* FUN_006b1930(p,0) */
{
	p->state = AOS_ST_SHIELD;
	p->timer = 0;
	p->speed = 0;
	aos_barrier(w, p, 0);
	aos_se(w, 0xb);
}

void aos_hitstun(aos_world *w, aos_player *p, float angle, float speed) /* FUN_006b19a0 */
{
	(void)w;
	if (p->statusOn[AOS_STATUS_STUNIMMUNE])
		return;
	p->state = AOS_ST_HITSTUN;
	p->timer = 0;
	float f = angle + AOS_PI;
	aos_angle_normalize(&f);
	p->faceAngle = f;
	p->dashAngle = angle;
	p->moveAngle = angle;
	p->speed = speed;
}

bool aos_damage(aos_world *w, aos_player *t, int32_t dmg, int32_t type, float stunAdd) /* FUN_00780ab0 */
{
	int32_t d = dmg;
	if ((type == 1 || type == 3) && t->heat > 1.0f)
		d = (int32_t)((float)dmg * (t->heat - 1.0f)) + dmg;
	t->hp -= d;
	t->recovHp -= (int32_t)((float)dmg * 0.8f);
	if (t->recovHp < t->redHp)
		t->redHp = t->recovHp;
	t->comboTimer = 30;
	t->comboDamage += d;
	t->comboHits += 1;
	t->stun += stunAdd;
	bool ret = t->stun >= 1.0f;
	t->hyper += (float)d * 0.0001f * t->hyperMul;
	if (t->hp < 1) {
		t->hp = 0;
		t->state = AOS_ST_KO;
		t->timer = 0;
		w->roundOver = 1;
		ret = false;
	}
	if (d < 1)
		return ret; /* original returns before the attacker counters */
	aos_player *a = aos_opponent(w, t); /* attacker combo counters (FUN_00780ab0 tail) */
	if (a->comboShowTimer < 1) {
		a->comboShowDmg = d;
		a->comboShowHits = 1;
		a->comboFadeA = 0;
		a->comboFadeB = 0;
	} else {
		a->comboShowDmg += d;
		a->comboShowHits += 1;
		if (w->frame % 5 == 0 || a->comboFadeA == 1.0f)
			a->comboFadeA = 0;
	}
	a->comboShowTimer = 0x78;
	if (a->comboShowHits > 99)
		a->comboShowHits = 99;
	if (a->comboShowDmg > 99999)
		a->comboShowDmg = 99999;
	return ret;
}

void aos_status(aos_world *w, aos_player *p, int k, int32_t frames) /* FUN_006b1a40 */
{
	if (p->state == AOS_ST_KO || !p->hittable || k < 0 || k >= AOS_STATUS_COUNT)
		return;
	if (p->statusOn[k]) {
		if (frames > p->statusTime[k])
			p->statusTime[k] = frames;
		return;
	}
	p->statusOn[k] = 1;
	p->statusTime[k] = frames;
	if (k == 1 || k == 2 || k == 7)
		aos_se(w, 0x26);
	if (k == 3)
		aos_se(w, 0x15);
}

/* Status callbacks (DAT_00881530[k], core.md §6.8). The particle visuals are
 * cosmetic but consume game RNG; draws are made in the same counts. */
static void status_end_check(aos_player *p, int k)
{
	p->statusTime[k]--;
	if (p->statusTime[k] < 1 || p->state == AOS_ST_KO)
		p->statusOn[k] = 0;
}

/* Shared part of status callbacks 1, 2, 7: effect at pos + rand(±30) offsets, angle π/2,
 * and spark 0x4b0 on even match frames (FUN_00751b20 / 751920 / 751750). */
static void status_fx(aos_world *w, aos_player *p, int effectId, int sparkColour)
{
	float pos[3] = {p->x, p->y, p->z};
	pos[0] = aos_rand_real(w, -30.0f, 30.0f) + pos[0];
	pos[1] = aos_rand_real(w, -30.0f, 30.0f) + pos[1];
	aos_spawn_effect(w, 0, effectId, 0, pos, 1.5707964f, p->objId, 0, p->side);
	if (w->frame % 2 == 0)
		aos_spawn_spark(w, 0x4b0, sparkColour, 3, 1, 1.5707964f, pos, p->objId);
}

static void status_tick(aos_world *w, aos_player *p)
{
	for (int k = 0; k < AOS_STATUS_COUNT; k++) {
		if (p->statusOn[k] != 1)
			continue;
		switch (k) {
		case 1: /* FUN_00751b20: burn */
			status_fx(w, p, 3000, 1);
			p->heat += 0.007f;
			status_end_check(p, k);
			break;
		case 2: /* FUN_00751920: hyper drain */
			status_fx(w, p, 0xbc2, 0);
			p->hyper -= 0.001f;
			if (p->hyper < 0.0f)
				p->hyper = 0;
			p->recovHp -= 1;
			if (p->recovHp < p->hp)
				p->recovHp = p->hp;
			status_end_check(p, k);
			break;
		case 3: /* FUN_00751ce0: no-dash */
			if (w->frame % 10 == 1) {
				float r = aos_rand_real(w, -3.1415927f, AOS_PI);
				aos_spawn_effect(w, 0, 0xc1c, 0, &p->x, r, p->objId, 0, p->side);
			}
			status_end_check(p, k);
			break;
		case 4:
			status_end_check(p, k);
			break;
		case 7: /* FUN_00751750: overheat (Kae Blazing) */
			status_fx(w, p, 3000, 1);
			if (p->heat < 2.01f)
				p->heat = 2.01f;
			p->statusTime[k]--;
			if (p->statusTime[k] < 1 || p->state == AOS_ST_KO) {
				p->statusOn[k] = 0;
				p->heat = 1.0f;
			}
			break;
		case 8:
			p->statusTime[k]--;
			if (p->statusTime[k] < 1 || p->state != AOS_ST_HITSTUN)
				p->statusOn[k] = 0;
			break;
		default:
			break; /* 0, 5: no callback; 6: no-op (never ends) */
		}
	}
}

/* ------------------------------------------------------------------ */
/* common states (core.md §4)                                          */

static bool st_neutral(aos_world *w, aos_player *p) /* FUN_006af390 */
{
	int32_t s = aos_tab.firstState[p->id];
	if (aos_tab.T2[s])
		aos_tab.T2[s](w, p);
	return true;
}

static bool st_hitstun(aos_world *w, aos_player *p) /* FUN_006af880 */
{
	if (p->Shield == 3 && aos_may_shield(w, p) && aos_spend_hyper(p, 1.0f)) {
		aos_barrier(w, p, 1);
		p->state = AOS_ST_BURST;
		p->timer = 0;
		aos_se(w, 0xb);
		/* FUN_004d7ac0(10,0.75,1) blur — cosmetic */
		aos_player *o = aos_opponent(w, p);
		float dx = o->x - p->x, dy = o->y - p->y;
		if (o->state != AOS_ST_KO && o->hittable && !o->statusOn[AOS_STATUS_STUNIMMUNE] &&
		    aos_sqrt(dx * dx + dy * dy) < 150.0f) { /* FUN_006b1d70(p, 150, 18) */
			o->state = AOS_ST_HITSTUN;
			o->timer = 0;
			o->faceAngle = aos_atan2(p->y - o->y, p->x - o->x);
			o->moveAngle = o->faceAngle + AOS_PI;
			o->speed = 18.0f;
			aos_se(w, 0x19);
			if (o->barrierTime > o->barrierBreakT) {
				o->barrierTime = 10;
				aos_popup(w, 4, &o->x, o->objId);
				aos_se(w, 0x19);
			}
		}
		return aos_tab.T2[AOS_ST_BURST](w, p);
	}
	if (p->timer >= 15 && p->D == 3) {
		aos_dash(w, p, 1);
	} else if (p->timer > 29) {
		p->state = AOS_ST_NEUTRAL;
		p->timer = 0;
	}
	if (p->id == AOS_CH_TSIH && aos_tab.T2[0x5e])
		aos_tab.T2[0x5e](w, p);
	if (p->id == AOS_CH_SUMIKA2 && aos_tab.T2[0x238])
		aos_tab.T2[0x238](w, p);
	return true;
}

static bool st_dash(aos_world *w, aos_player *p) /* FUN_006af5e0 */
{
	const aos_stats *st = &aos_tab.stats[p->id];
	float hold = st->dashHold, walk = st->walk;
	if (p->statusOn[AOS_STATUS_SLOW]) {
		hold *= 0.25f;
		walk *= 0.25f;
	}
	if (p->D > 1 && p->speed < hold) {
		p->speed = hold;
		p->heat += 0.012f;
	}
	float a = aos_dir_angle(p);
	if (a < AOS_2PI)
		aos_angle_toward(&p->dashAngle, a, 0.08f);
	p->moveAngle = p->faceAngle = p->dashAngle;
	if (p->speed <= p->friction) {
		p->speed = 0;
		p->state = AOS_ST_NEUTRAL;
	}
	if (p->D < 2) {
		if (p->up > 1) p->y += walk;
		if (p->down > 1) p->y -= walk;
		if (p->left > 1) p->x -= walk;
		if (p->right > 1) p->x += walk;
	}
	if (p->timer % 5 == 0)
		aos_spawn_effect(w, 0, 100, 0, &p->x, p->faceAngle, p->objId, 0, p->side);
	if (p->id == AOS_CH_TSIH && aos_tab.T2[0x5c]) aos_tab.T2[0x5c](w, p);
	if (p->id == AOS_CH_KAE && aos_tab.T2[0x1ab]) aos_tab.T2[0x1ab](w, p);
	if (p->id == AOS_CH_SUMIKA2 && aos_tab.T2[0x236]) aos_tab.T2[0x236](w, p);
	return true;
}

static void face_opponent(aos_world *w, aos_player *p)
{
	aos_player *o = aos_opponent(w, p);
	p->faceAngle = p->dashAngle = (o->x <= p->x) ? AOS_PI : 0.0f;
}

static void shield_particles(aos_world *w, aos_player *p, float aux2)
{
	if (p->timer % 2 == 0 && p->timer >= 0 && p->timer < 13) {
		float r = aos_rand_real(w, -0.1f, 0.1f);
		int k = aos_rand_int(w, 0, 100) % 8;
		float pos[3] = {p->x, p->y - 48.0f, p->z};
		int e = aos_spawn_effect(w, 0, 0xa82 + k, 0, pos, r + 1.5707964f, p->objId, 0, p->side);
		if (e >= 0)
			w->obj.effect[e].aux2 = aux2;
	}
}

static bool st_shield(aos_world *w, aos_player *p) /* FUN_006afac0 */
{
	aos_player *o = aos_opponent(w, p);
	if (p->timer == 0)
		face_opponent(w, p);
	if (p->timer == 5 && o->barrierTime > o->barrierBreakT) {
		float dx = o->x - p->x, dy = o->y - p->y;
		if (aos_sqrt(dx * dx + dy * dy) <= p->barrierR * p->barrierScale + o->barrierR * o->barrierScale) {
			o->barrierTime = 10;
			aos_popup(w, 4, &o->x, o->objId);
			aos_se(w, 0xc);
		}
	}
	shield_particles(w, p, 0.2f);
	if (p->timer >= 5 && p->D == 3)
		aos_dash(w, p, 1);
	else if (p->timer > 19) {
		p->state = AOS_ST_NEUTRAL;
		p->timer = 0;
	}
	return true;
}

static bool st_burst(aos_world *w, aos_player *p) /* FUN_006afd90 */
{
	aos_player *o = aos_opponent(w, p);
	if (p->timer == 0)
		face_opponent(w, p);
	if (p->timer == 5 && o->state != AOS_ST_KO && o->hittable && !o->statusOn[AOS_STATUS_STUNIMMUNE]) {
		float dx = o->x - p->x, dy = o->y - p->y;
		if (aos_sqrt(dx * dx + dy * dy) <= p->barrierR * p->barrierScale) {
			o->state = AOS_ST_PUSHED;
			o->timer = 0;
			o->dashAngle = o->moveAngle = aos_atan2(o->y - p->y, o->x - p->x);
			o->speed = 20.0f;
			aos_se(w, 0xc);
		}
	}
	shield_particles(w, p, 0.25f);
	if (p->timer >= 20 && p->D == 3)
		aos_dash(w, p, 1);
	else if (p->timer > 29) {
		p->state = AOS_ST_NEUTRAL;
		p->timer = 0;
	}
	return true;
}

static bool st_pushed(aos_world *w, aos_player *p) /* FUN_006b00a0 */
{
	if (p->timer >= 30 && p->D == 3)
		aos_dash(w, p, 1);
	else if (p->timer > 39) {
		p->state = AOS_ST_NEUTRAL;
		p->timer = 0;
	}
	return true;
}

static bool st_ko(aos_world *w, aos_player *p) /* FUN_006b0110 */
{
	static const int16_t hook[AOS_NUM_CHARS] = {
		[AOS_CH_ALTE] = 0x34, [AOS_CH_TSIH] = 0x5d, [AOS_CH_MIRA] = 0x7e, [AOS_CH_SHAM] = 0xa9,
		[AOS_CH_NATH] = 0xca, [AOS_CH_NANAKO] = 0x18f, [AOS_CH_KYOKO] = 0x1d3,
	};
	if (hook[p->id] && aos_tab.T2[hook[p->id]])
		aos_tab.T2[hook[p->id]](w, p);
	if (p->timer == 0) {
		p->speed = 0;
		aos_opponent(w, p)->roundsWon++;
		aos_round_end_banner(w, 0); /* FUN_0060b310(0) */
		effect0(w, 900, &p->x, 0, p);
	}
	if (p->timer == 100) {
		effect0(w, 0x385, &p->x, 0, p);
		for (int i = 0; i < 16; i++) {
			float base = (float)i / 16.0f * AOS_2PI;
			effect0(w, 0x38e, &p->x, base + aos_rand_real(w, -0.2f, 0.2f), p);
			effect0(w, 0x38f, &p->x, base + aos_rand_real(w, -0.2f, 0.2f), p);
		}
		aos_se(w, 0xc);
	}
	p->hittable = 0;
	return true;
}

/* ------------------------------------------------------------------ */
/* input decode (FUN_0061bc70) and locks (end of FUN_006116c0)         */

static void btn(int32_t *v, bool down)
{
	if (down)
		*v = (*v == 0 || *v == 1) ? 3 : 2;
	else
		*v = (*v == 2 || *v == 3) ? 1 : 0;
}

static void decode_input(aos_world *w, aos_player *p, uint16_t m)
{
	btn(&p->up, m & AOS_IN_UP);
	btn(&p->down, m & AOS_IN_DOWN);
	btn(&p->left, m & AOS_IN_LEFT);
	btn(&p->right, m & AOS_IN_RIGHT);
	btn(&p->A, m & AOS_IN_A);
	btn(&p->B, m & AOS_IN_B);
	btn(&p->S, m & AOS_IN_S);
	btn(&p->D, m & AOS_IN_D);
	btn(&p->H, m & AOS_IN_H);
	btn(&p->Shield, m & AOS_IN_SHIELD);
	btn(&p->Pause, m & AOS_IN_PAUSE);
	if (w->frame < 200 || w->matchFinished) {
		p->A = p->B = p->H = p->Shield = 0;
	}
	if (w->frame < 100 || w->matchFinished) {
		p->right = p->left = p->up = p->down = 0;
		p->D = 0;
	}
}

/* ------------------------------------------------------------------ */
/* player update FUN_006f1720                                          */

static void combo_tick(aos_player *a, const aos_player *v) /* FUN_00780650(attacker, victim) */
{
	a->comboShowTimer--;
	if (v->state != AOS_ST_HITSTUN && v->hittable == 1)
		a->comboShowTimer = 0;
	if (a->comboShowTimer == 0) {
		if (a->comboBestDmg < a->comboShowDmg)
			a->comboBestDmg = a->comboShowDmg;
		if (a->comboBestHits < a->comboShowHits)
			a->comboBestHits = a->comboShowHits;
	}
	if (a->comboShowTimer < 1) {
		a->comboFadeB -= 0.05f;
		if (a->comboFadeB < 0.0f)
			a->comboFadeB = 0;
	} else {
		if (a->comboFadeA < 1.0f)
			a->comboFadeA += 0.1f;
		if (a->comboFadeA > 1.0f)
			a->comboFadeA = 1.0f;
		a->comboFadeB += 0.1f;
		if (a->comboFadeB > 1.0f)
			a->comboFadeB = 1.0f;
	}
}

static void player_update(aos_world *w, aos_player *p)
{
	aos_player *o = aos_opponent(w, p);
	p->hurtShape[0] = 0;
	p->hurtShape[1] = 0;
	p->hurtShape[2] = 16.0f;
	p->hurtShape[3] = 0;
	p->hittable = 1;
	p->friction = 0.5f;
	p->clampArena = 1;
	aos_input_fn in = p->scheme == 1 ? aos_tab.inputB[p->id] : aos_tab.inputA[p->id];
	if (in)
		in(w, p);
	else if (aos_tab.T2[p->state])
		aos_tab.T2[p->state](w, p); /* character not implemented yet: run its state only */
	/* physics */
	float spd = p->speed;
	if (p->statusOn[AOS_STATUS_SLOW])
		spd *= 0.25f;
	float ox = p->x, oy = p->y;
	p->x += aos_cos(p->moveAngle) * spd;
	p->y += aos_sin(p->moveAngle) * spd;
	if (p->id == AOS_CH_ALTE && ((p->state == 0x41 && p->timer >= 15 && p->timer <= 40) ||
	                             (p->state == 0x44 && p->timer >= 15 && p->timer <= 34))) {
		/* Alte-only rule (core.md §10): small steps are amplified on the larger axis */
		float sx = p->x - ox, sy = p->y - oy;
		if (aos_fabs(sx) < 8.0f && aos_fabs(sy) < 8.0f && spd > 0.1f) { /* all.c:380598 */
			if (aos_fabs(sx) <= aos_fabs(sy)) {
				p->x += sx;
				p->y += sy * 3.0f;
			} else {
				p->x += sx * 3.0f;
				p->y += sy;
			}
		}
	}
	if (p->speed <= 0.0f)
		p->speed = 0;
	else
		p->speed -= p->friction;
	status_tick(w, p);
	if (p->state != AOS_ST_KO && p->state != AOS_ST_HITSTUN) {
		bool fast = p->state >= 0 && p->state <= 3;
		p->heat -= fast ? 0.01f : 0.005f;
	}
	if (p->heat < 0)
		p->heat = 0;
	if (p->heat > 3.0f)
		p->heat = 3.0f;
	if (w->difficulty == 2 && p->inputSrc == 10 && w->frame % 5 == 0)
		p->hyper += 0.001f;
	if (p->hyper > 3.0f)
		p->hyper = 3.0f;
	if (p->gaugeA < 0) p->gaugeA = 0;
	if (p->gaugeB < 0) p->gaugeB = 0;
	if (p->state >= 0 && p->state <= 2) {
		p->gaugeA += 0.01f;
		p->gaugeB += 0.01f;
		if (p->gaugeA > 1.0f) p->gaugeA = 1.0f;
		if (p->gaugeB > 1.0f) p->gaugeB = 1.0f;
	}
	if (p->state != AOS_ST_HITSTUN) {
		if (p->comboTimer > 0)
			p->comboTimer--;
		if (p->comboTimer < 1) {
			p->comboDamage = 0;
			p->comboHits = 0;
			p->stun = 0;
		}
	}
	if (p->hp < p->redHp)
		p->redHp -= 10;
	if (p->redHp < p->hp)
		p->redHp = p->hp;
	int32_t third = p->maxHp / 3;
	if (third > 0) {
		int32_t cap = (p->hp / third + 1) * third;
		if (p->recovHp > cap)
			p->recovHp = cap;
	}
	if (p->hp < 1)
		p->recovHp = 0;
	combo_tick(p, o);
	if (p->state < 7)
		p->hitFlag = 0;
	if (p->clampArena && (double)p->x * p->x + (double)p->y * p->y > 291600.0) {
		float a = aos_atan2(p->y, p->x);
		p->x = aos_cos(a) * 540.0f;
		p->y = aos_sin(a) * 540.0f;
	}
	p->px = p->x;
	p->py = p->y;
	p->pz = p->z;
	if (p->x < o->x)
		p->facingRight = 1;
	if (p->x > o->x)
		p->facingRight = 0;
	if (p->barrierTime > 0) {
		p->hurtShape[0] = p->hurtShape[1] = p->hurtShape[2] = p->hurtShape[3] = 0;
		if (p->barrierTime < 21) {
			if (aos_tab.barrierHook[p->id]) /* all.c:380823 */
				aos_tab.barrierHook[p->id](w, p, 1);
			p->barrierScale = (float)p->barrierTime / 20.0f;
		} else {
			p->barrierScale = p->barrierScale >= 1.0f ? 1.0f : p->barrierScale + 0.1f;
			p->hurtShape[2] = p->barrierScale * p->barrierR * 0.8f;
		}
		p->barrierRot = aos_rand_real(w, -AOS_PI, AOS_PI);
		p->barrierTime--;
	}
	if (w->matchFinished == 1)
		p->hittable = 0;
	p->timer++;
}

/* ------------------------------------------------------------------ */
/* registration / match / round                                        */

/* Common-state draw handlers (FUN_006b07a0, 6b0c40, 6b09f0, 6b0e90, 6b10e0,
 * 6b1330, 6b1580) switch on the character and call one of the character's
 * five stub states: neutral +0, dash +1, KO +2, hit/pushed +3, shield/burst +4. */
static void common_draw(aos_world *w, aos_player *p, int off)
{
	aos_draw_fn f = aos_tab.T1[aos_tab.firstState[p->id] + off];
	if (f)
		f(w, p);
}
static void dr_neutral(aos_world *w, aos_player *p) { common_draw(w, p, 0); }
static void dr_dash(aos_world *w, aos_player *p) { common_draw(w, p, 1); }
static void dr_ko(aos_world *w, aos_player *p) { common_draw(w, p, 2); }
static void dr_hit(aos_world *w, aos_player *p) { common_draw(w, p, 3); }
static void dr_shield(aos_world *w, aos_player *p) { common_draw(w, p, 4); }

void aos_draw_players(aos_world *w) /* FUN_006b1ea0: T1[state] for each player */
{
	for (int i = 0; i < 2; i++) {
		aos_draw_fn f = aos_tab.T1[w->p[i].state];
		if (f)
			f(w, &w->p[i]);
	}
}

void aos_register_all(void)
{
	memset(&aos_tab, 0, sizeof aos_tab);
	memset(&aos_otab, 0, sizeof aos_otab);
	static const struct { int id; float walk, dash, hold; int32_t first, init; } st[] = {
		{AOS_CH_SORA, 5, 15, 12, 0x08, 0}, {AOS_CH_ALTE, 5, 13, 10, 0x32, 0},
		{AOS_CH_TSIH, 6, 12, 11, 0x5b, 0}, {AOS_CH_MIRA, 5, 12, 10, 0x7c, 0},
		{AOS_CH_SHAM, 5, 12, 10, 0xa7, 0}, {AOS_CH_NATH, 4, 15, 9, 0xc8, 0xd2},
		{AOS_CH_STARBREAKER, 5, 13, 10, 0x101, 0}, {AOS_CH_SUGURI, 5, 15, 12, 0x128, 0},
		{AOS_CH_SAKI, 4, 14, 12, 0x153, 0}, {AOS_CH_IRU, 4, 14, 12, 0x16e, 0},
		{AOS_CH_NANAKO, 4, 14, 12, 0x18d, 0x197}, {AOS_CH_KAE, 4, 17, 11, 0x1aa, 0},
		{AOS_CH_KYOKO, 4, 12, 10, 0x1d1, 0}, {AOS_CH_HIME, 5, 14, 12, 0x1f0, 0},
		{AOS_CH_SUMIKA, 5, 14, 12, 0x211, 0x21d}, {AOS_CH_SUMIKA2, 5, 14, 12, 0x235, 0x241},
	};
	for (size_t i = 0; i < sizeof st / sizeof st[0]; i++) {
		aos_tab.stats[st[i].id] = (aos_stats){st[i].walk, st[i].dash, st[i].hold};
		aos_tab.firstState[st[i].id] = st[i].first;
		aos_tab.initialState[st[i].id] = st[i].init;
	}
	aos_register_costs();
	aos_tab.T2[AOS_ST_NEUTRAL] = st_neutral;
	aos_tab.T2[AOS_ST_HITSTUN] = st_hitstun;
	aos_tab.T2[AOS_ST_DASH] = st_dash;
	aos_tab.T2[AOS_ST_SHIELD] = st_shield;
	aos_tab.T2[AOS_ST_BURST] = st_burst;
	aos_tab.T2[AOS_ST_PUSHED] = st_pushed;
	aos_tab.T2[AOS_ST_KO] = st_ko;
	aos_tab.T1[AOS_ST_NEUTRAL] = dr_neutral;
	aos_tab.T1[AOS_ST_HITSTUN] = dr_hit;
	aos_tab.T1[AOS_ST_DASH] = dr_dash;
	aos_tab.T1[AOS_ST_SHIELD] = dr_shield;
	aos_tab.T1[AOS_ST_BURST] = dr_shield;
	aos_tab.T1[AOS_ST_PUSHED] = dr_hit;
	aos_tab.T1[AOS_ST_KO] = dr_ko;
	aos_register_fx();
	aos_register_chars();
}

void aos_round_init(aos_world *w) /* FUN_005f4650 + FUN_0069cc40 */
{
	aos_player keep[2] = {w->p[0], w->p[1]};
	memset(&w->obj, 0, sizeof w->obj);
	w->obj.zoom = 2.0f;
	for (int i = 0; i < 2; i++) {
		aos_player *p = &w->p[i];
		memset(p, 0, sizeof *p);
		p->objId = w->nextObjId++;
		p->inputSrc = keep[i].inputSrc;
		p->scheme = keep[i].scheme;
		p->id = keep[i].id;
		p->side = i + 1;
		p->color = keep[i].color;
		p->roundsWon = keep[i].roundsWon;
		p->x = i == 0 ? -150.0f : 150.0f;
		p->faceAngle = i == 0 ? 0.0f : AOS_PI;
		p->fa1 = 1;
		p->hittable = 1;
		p->maxHp = p->hp = p->redHp = p->recovHp = 4500;
		if (p->id == AOS_CH_SUMIKA2) {
			static const int32_t bossHp[3] = {12000, 15000, 18000};
			int d = w->difficulty < 0 ? 0 : w->difficulty > 2 ? 2 : w->difficulty;
			p->maxHp = p->hp = p->recovHp = bossHp[d];
		}
		p->gaugeA = p->gaugeB = 1.0f;
		p->hyperMul = 1.0f;
		p->f114 = -1;
		p->barrierBreakT = 50; /* FUN_0069cc40 */
		p->state = aos_tab.initialState[p->id];
	}
	for (int i = 0; i < 2; i++)
		w->p[i].facingRight = w->p[i].x < w->p[1 - i].x;
	aos_mt_seed(&w->rng, 0);
	w->frame = 0;
	w->roundOver = 0;
	aos_flow_round_init(w); /* banners, round timer, transition (flow.c) */
}

void aos_match_init(aos_world *w, const int32_t ids[2], const int32_t schemes[2],
                    const int32_t colors[2], int32_t roundSeconds, int32_t roundsToWin)
{
	memset(w, 0, sizeof *w);
	for (int i = 0; i < 2; i++) {
		w->p[i].id = ids[i];
		w->p[i].scheme = schemes[i];
		w->p[i].color = colors[i];
		w->p[i].inputSrc = 0;
	}
	w->roundsToWin = roundsToWin;
	w->flow.roundSeconds = roundSeconds;
	w->flow.firstInit = 1;
	w->nextObjId = 1;
	aos_mt_seed(&w->cpuRng, 5489); /* PC: random_device; any seed is faithful */
	aos_round_init(w);
}

/* ------------------------------------------------------------------ */
/* the gameplay tick FUN_006129b0                                      */

void aos_tick(aos_world *w, const uint16_t masks[2])
{
	aos_flow_frame_start(w); /* pending transition update / Game Init (FUN_006124b0 head) */
	w->se_count = 0;
	/* CPU input source (FUN_006116c0): inputSrc 10 -> (&DAT_00881400)[diff + id*4](p, &mask),
	 * both players' AI run before any decode; mask forced 0 while the round-end banner is up. */
	uint16_t cm[2] = {masks[0], masks[1]};
	for (int i = 0; i < 2; i++) {
		aos_player *p = &w->p[i];
		if (p->inputSrc != 10 || p->id < 0 || p->id >= AOS_NUM_CHARS)
			continue;
		int d = w->difficulty < 0 ? 0 : w->difficulty > 2 ? 2 : w->difficulty;
		cm[i] = 0;
		if (aos_tab.cpu[p->id][d])
			aos_tab.cpu[p->id][d](w, p, &cm[i]);
		if (w->roundEndBanner)
			cm[i] = 0;
	}
	for (int i = 0; i < 2; i++)
		decode_input(w, &w->p[i], cm[i]);
	float hyper0[2] = {w->p[0].hyper, w->p[1].hyper};
	for (int i = 0; i < 2; i++)
		player_update(w, &w->p[i]);
	aos_enemies_update(w);
	for (int i = 0; i < 2; i++) {
		aos_player *p = &w->p[i];
		p->x = (float)(int32_t)p->x;
		p->y = (float)(int32_t)p->y;
		p->z = (float)(int32_t)p->z;
	}
	aos_beams_update(w);
	aos_missiles_update(w);
	aos_lasers_update(w);
	aos_effects_update(w);
	aos_fx_tick(w); /* smoke / particle / spark / popup lists */
	aos_whips_update(w);
	aos_camera_update(w);
	aos_flow_tick(w); /* FUN_00617a60, FUN_00608a00 (flow.c) */
	aos_collisions(w);
	aos_enemies_cleanup(w);
	AOS_CLEAR_DEAD(w->obj.hitbox, sizeof w->obj.hitbox[0] * (size_t)w->obj.nhitbox); /* dead slots stay zero (objects.h) */
	w->obj.nhitbox = 0; /* hitboxes live one frame (FUN_005e0150, after drawing in the PC loop) */
	w->frame++;
	aos_rehit_timers(w);
	/* round timer (core.md §6.10) */
	if (w->frame % 60 == 0 && w->roundTimer != 999 && w->roundTimer > 0 && !w->roundStartBanner &&
	    !w->roundEndBanner)
		w->roundTimer--;
	if (w->roundTimer == 0 && !w->roundOver) {
		if (w->p[0].hp > w->p[1].hp)
			w->p[0].roundsWon++;
		else
			w->p[1].roundsWon++;
		w->roundOver = 1;
		if (aos_match_decided(w))
			w->matchFinished = 1; /* DAT_0088596c (+ DAT_00885970 = 1) */
		aos_round_end_banner(w, 1); /* FUN_0060b310(1) */
	}
	for (int i = 0; i < 2; i++) {
		float h = w->p[i].hyper;
		for (int lv = 1; lv <= 3; lv++)
			if (hyper0[i] < (float)lv && h >= (float)lv)
				aos_se(w, 0x39 + lv);
	}
	aos_flow_frame_end(w);
}
