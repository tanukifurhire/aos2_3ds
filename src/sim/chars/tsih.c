/* Tsih (engine id 5, states 0x5b..0x77) — 1:1 port of aos2.exe.
 * Spec: docs/re/tsih.md. Original addresses on every function.
 * Only the version-0x5a2 branches are ported. */
#include <string.h>

#include "../draw.h"
#include "../fx.h"
#include "../sim.h"

#define ID AOS_CH_TSIH
#define HALF_PI 1.5707964f /* DAT_00814104 */
#define THIRD_PI 1.0471976f /* DAT_008203bc */

/* ------------------------------------------------------------------ */
/* small helpers                                                       */

/* +0x164 stealth flag (byte), +0x168 stealth timer (int) — player scratch. */
#define STEALTH(p) ((p)->scratch[0x164 - 0x15c])
static int32_t stime(const aos_player *p)
{
	int32_t v;
	memcpy(&v, &p->scratch[0x168 - 0x15c], 4);
	return v;
}
static void stime_set(aos_player *p, int32_t v) { memcpy(&p->scratch[0x168 - 0x15c], &v, 4); }

/* Enemy +0x74 is a float for Tsih (spin); the engine declares it int32. */
static float spin(const aos_enemy *e)
{
	float v;
	memcpy(&v, &e->f74, 4);
	return v;
}
static void spin_set(aos_enemy *e, float v) { memcpy(&e->f74, &v, 4); }

static aos_player *self_of_team(aos_world *w, int32_t team) /* FUN_00780450 */
{
	return team == 1 ? &w->p[0] : team == 2 ? &w->p[1] : NULL;
}
static aos_player *opp_of_team(aos_world *w, int32_t team) /* FUN_00780480 */
{
	return team == 1 ? &w->p[1] : team == 2 ? &w->p[0] : NULL;
}

/* Per-team colour slot k (DAT_008859f4 + team*0x10 + 4k). */
static int32_t tcol(aos_world *w, int32_t team, int k) { return aos_team_colour(w, team, k); }

static int fx0(aos_world *w, int32_t id, int32_t colour, const float pos[3], float angle,
               int32_t owner, int32_t team)
{
	return aos_spawn_effect(w, 0, id, colour, pos, angle, owner, 0, team);
}

static int beam(aos_world *w, aos_player *p, int32_t id, const float pos[3], float angle)
{
	return aos_spawn_beam(w, ID, id, pos, angle, p->objId, 0, p->side);
}

/* "face-x": aim = π if the opponent is left of (or level with) Tsih, else 0. */
static void face_x(aos_world *w, aos_player *p)
{
	aos_player *o = aos_opponent(w, p);
	if (o->x < p->x || o->x == p->x)
		p->faceAngle = AOS_PI;
	else
		p->faceAngle = 0.0f;
	p->dashAngle = p->faceAngle;
}

static void set_state_keep(aos_world *w, aos_player *p, int32_t s, bool resetTimer)
{
	p->state = s;
	if (resetTimer) {
		p->timer = 0;
		p->hitFlag = 0;
	}
	if (aos_tab.T2[s])
		aos_tab.T2[s](w, p);
}

/* Hyper start used by 0x65/0x69/0x6c/0x70 (e.g. FUN_00784130 t==0). */
static void hyper_start(aos_world *w, aos_player *p)
{
	face_x(w, p);
	aos_barrier(w, p, 10);
	/* SE 6, dark(100,.75), speedline(100,pos,0,0), blur(20,.5,2), effect 0xfa */
	aos_hyper_flourish_ex(w, p, 6, 100, 0.75f, 100, 0.0f, 0.0f, 0x14, 0.5f, 2, 1);
}

/* Dash sparks of 0x73..0x75 (FUN_00785ed0 etc.). */
static void sparks(aos_world *w, aos_player *p)
{
	if (5.0f <= p->speed) {
		float base = p->faceAngle + HALF_PI;
		float r = aos_rand_real(w, -0.15f, 0.15f);
		int32_t n = aos_rand_int(w, 0, 100) % 8;
		fx0(w, 0xa28 + n, 0xd, &p->x, base + r, p->objId, p->side);
	}
}

/* FUN_006b1d70(src, range, speed): proximity knock. */
static bool prox_knock(aos_world *w, aos_player *src, float range, float speed)
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
		aos_camera_focus_clear(w); /* FUN_004a9f10 on 0x895144 (call at 0x6b1e7d) */
		return true;
	}
	return false;
}

/* FUN_00782900: throw every idle rock of this player at the opponent. */
static void throw_rocks(aos_world *w, aos_player *p)
{
	aos_player *o = aos_opponent(w, p);
	for (int i = 0; i < w->obj.nenemy; i++) {
		aos_enemy *e = &w->obj.enemy[i];
		if (e->f10 == 100 && e->f7c == 100 && e->f84 == 100 && e->type == ID && e->ownerId == p->objId) {
			e->f84 = 200;
			e->timer2 = 0;
			e->angle2 = aos_atan2(o->y - e->pos[1], o->x - e->pos[0]);
			aos_se(w, 0x20);
		}
	}
}

/* ------------------------------------------------------------------ */
/* input dispatch                                                      */

static bool dispatch(aos_world *w, aos_player *p, bool schemeB) /* FUN_00786e70 / FUN_00787360 */
{
	if (aos_may_hyper(w, p)) {
		bool r1 = schemeB ? (p->H > 1 && p->Shield == 3) : (p->S > 1 && p->B > 1 && p->H == 3);
		if (r1 && aos_spend_hyper(p, 3.0f)) {
			aos_set_state(w, p, 0x77);
			return true;
		}
		bool r2 = schemeB ? (p->H > 1 && p->S == 3) : (p->S > 1 && p->H == 3);
		if (r2 && aos_spend_hyper(p, 1.0f)) {
			aos_set_state(w, p, 0x65);
			return true;
		}
		bool r3 = schemeB ? (p->H > 1 && p->B == 3) : (p->B > 1 && p->H == 3);
		if (r3 && aos_spend_hyper(p, 1.0f)) {
			aos_set_state(w, p, 0x70);
			return true;
		}
		bool r4 = schemeB ? (p->H > 1 && p->D == 3) : (p->D > 1 && p->H == 3);
		if (r4 && aos_spend_hyper(p, 1.0f)) {
			aos_set_state(w, p, 0x6c);
			return true;
		}
		bool r5 = schemeB ? (p->H > 1 && p->A == 3) : (p->H == 3);
		if (r5 && aos_spend_hyper(p, 1.0f)) {
			aos_set_state(w, p, 0x69);
			return true;
		}
	}
	if (p->state == AOS_ST_NEUTRAL || p->state == AOS_ST_DASH) {
		if (p->D == 3) {
			aos_dash(w, p, 0);
		} else if (p->Shield == 3 && aos_spend_hyper(p, 0.5f) && aos_may_shield(w, p)) {
			aos_shield(w, p); /* literal: the 0.5 is spent even when the shield is refused */
		} else {
			if (p->S > 1 && p->B == 3) {
				aos_set_state(w, p, 0x73);
				return true;
			}
			if (p->B == 3) {
				aos_set_state(w, p, p->up > 1 ? 0x6e : p->down > 1 ? 0x6f : 0x6d);
				return true;
			}
			if (p->S > 1 && p->A == 3) {
				aos_set_state(w, p, 0x6b);
				return true;
			}
			if (p->D > 1 && p->A == 3) {
				aos_set_state(w, p, 0x68);
				return true;
			}
			if (p->A == 3) {
				aos_set_state(w, p, 0x6a);
				return true;
			}
		}
	}
	return aos_tab.T2[p->state] ? aos_tab.T2[p->state](w, p) : true;
}

static bool input_a(aos_world *w, aos_player *p) { return dispatch(w, p, false); }
static bool input_b(aos_world *w, aos_player *p) { return dispatch(w, p, true); }

/* ------------------------------------------------------------------ */
/* T2 states                                                           */

static void stealth_tick(aos_world *w, aos_player *p)
{
	if (STEALTH(p) == 1) {
		stime_set(p, stime(p) - 1);
		if (stime(p) < 1)
			aos_set_state(w, p, 0x66);
	}
}

static bool st_neutral(aos_world *w, aos_player *p) /* 0x5b T2 FUN_00783470 */
{
	float spd = aos_tab.stats[p->id].walk;
	if (p->up > 1) p->y += spd;
	if (p->down > 1) p->y -= spd;
	if (p->left > 1) p->x -= spd;
	if (p->right > 1) p->x += spd;
	p->faceAngle = p->facingRight == 1 ? 0.0f : AOS_PI;
	stealth_tick(w, p);
	return true;
}

static bool st_dash_hook(aos_world *w, aos_player *p) /* 0x5c T2 FUN_007835d0 (from common dash) */
{
	stealth_tick(w, p);
	return true;
}

static bool st_ko_hook(aos_world *w, aos_player *p) /* 0x5d T2 FUN_007836a0 (from common KO) */
{
	if (p->timer == 0) {
		for (int i = 0; i < w->obj.nenemy; i++) {
			aos_enemy *e = &w->obj.enemy[i];
			if (e->ownerId == p->objId && (e->f10 == 100 || e->f10 == 200 || e->f10 == 0xd2)) {
				e->f84 = 900;
				e->timer2 = 0;
				e->active = 0;
			}
		}
	}
	return true;
}

static bool st_hit_hook(aos_world *w, aos_player *p) /* 0x5e T2 FUN_00783650 (from common hitstun) */
{
	if (STEALTH(p) == 1 && stime(p) < 1) {
		STEALTH(p) = 0;
		aos_se(w, 0x2c);
	}
	return true;
}

static bool st_stub(aos_world *w, aos_player *p) /* 0x5f T2 FUN_00783690 */
{
	(void)w;
	(void)p;
	return true;
}

static bool st_stealth_on(aos_world *w, aos_player *p) /* 0x65 T2 FUN_00783790 */
{
	int32_t t = p->timer;
	if (t == 0)
		hyper_start(w, p);
	if (t == 10) {
		STEALTH(p) = 1;
		stime_set(p, 180);
		aos_se(w, 0x2b);
	}
	if (4 < t && t < 26 && t % 5 == 0)
		fx0(w, 0xaf0, tcol(w, p->side, 2), &p->x, 0, p->objId, p->side);
	if (30 < t)
		p->state = 0;
	return true;
}

static bool st_stealth_off(aos_world *w, aos_player *p) /* 0x66 T2 FUN_007839a0 */
{
	int32_t t = p->timer;
	if (t == 0)
		face_x(w, p);
	if (t == 10) {
		STEALTH(p) = 0;
		aos_se(w, 0x2c);
	}
	if (4 < t && t < 21 && t % 5 == 0)
		fx0(w, 0xaf0, tcol(w, p->side, 2), &p->x, 0, p->objId, p->side);
	if (t < 25 || p->D != 3) {
		if (40 < t)
			p->state = 0;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_bendy(aos_world *w, aos_player *p) /* 0x67 T2 FUN_00783d80 */
{
	int32_t t = p->timer;
	if (t == 12 && aos_use_gauge(p, 0x67)) {
		p->faceAngle = aos_aim(w, p);
		beam(w, p, 0xd2, &p->x, p->faceAngle + THIRD_PI);
		aos_se(w, 0xf);
	}
	if (p->timer == 14 && aos_use_gauge(p, 0x67)) {
		p->faceAngle = aos_aim(w, p);
		beam(w, p, 0xd7, &p->x, p->faceAngle - THIRD_PI);
		aos_se(w, 0xf);
	}
	if (p->timer < 15 || p->D != 3) {
		if (25 < p->timer)
			p->state = 0;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_bendy_many(aos_world *w, aos_player *p) /* 0x68 T2 FUN_00783f20 */
{
	int32_t t = p->timer;
	if (t == 0)
		p->faceAngle = aos_aim(w, p);
	if ((t == 12 || t == 15 || t == 18 || t == 21) && aos_use_gauge(p, 0x68)) {
		p->faceAngle = aos_aim(w, p);
		int32_t id = (t / 3) % 2 == 0 ? 200 : 0xcd;
		float a = aos_rand_real(w, -AOS_PI, AOS_PI);
		beam(w, p, id, &p->px, a); /* spawned at +0x78 (end-of-last-update pos) */
		aos_se(w, 0xf);
	}
	if (p->timer < 25 || p->D != 3) {
		if (30 < p->timer)
			p->state = 0;
		p->friction *= 0.5f;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_hyper_bendy(aos_world *w, aos_player *p) /* 0x69 T2 FUN_00784130 */
{
	if (p->timer == 0)
		hyper_start(w, p);
	if (p->timer == 10) {
		for (int32_t k = -8; k < 8; k++) {
			beam(w, p, k % 2 == 0 ? 0xdc : 0xe1, &p->x, ((float)k / 8.0f) * AOS_PI);
			aos_se(w, 0xf);
		}
	}
	if (p->timer < 30 || p->D != 3) {
		if (50 < p->timer)
			p->state = 0;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_a_press(aos_world *w, aos_player *p) /* 0x6a T2 FUN_00783b40 */
{
	if (p->timer == 0)
		face_x(w, p);
	if (p->timer < 10 && p->A < 1) {
		set_state_keep(w, p, 0x67, false); /* timer kept */
	} else {
		if (p->timer == 16 && aos_use_gauge(p, 0x6a)) {
			for (int32_t k = -16; k < 16; k++)
				beam(w, p, k % 2 == 0 ? 100 : 0x69, &p->px, ((float)k / 16.0f) * AOS_PI);
			aos_se(w, 0x10);
		}
		if (p->timer < 25 || p->D != 3) {
			if (30 < p->timer)
				p->state = 0;
		} else {
			aos_dash(w, p, 1);
		}
	}
	return true;
}

static bool st_wire(aos_world *w, aos_player *p) /* 0x6b T2 FUN_00785b40 */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer == 5 && aos_use_gauge(p, 0x6b)) {
		beam(w, p, 300, &p->x, p->faceAngle);
		aos_se(w, 0xf);
	}
	if (p->timer < 20 || p->D != 3) {
		if (40 < p->timer)
			p->state = 0;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_rounding_wire(aos_world *w, aos_player *p) /* 0x6c T2 FUN_00785c40 */
{
	if (p->timer == 0)
		hyper_start(w, p);
	for (int i = 0; i < 10; i++) {
		if (p->timer == i * 4 + 10) {
			float f = ((float)(i % 5) / 5.0f) * AOS_2PI;
			float r = aos_rand_real(w, -0.5f, 0.5f);
			float a = f + r + p->faceAngle;
			aos_angle_normalize(&a);
			beam(w, p, 0x136, &p->x, a);
			aos_se(w, 0xf);
		}
	}
	if (p->timer < 60 || p->D != 3) {
		if (80 < p->timer)
			p->state = 0;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

/* rock + wire laser pair used by 0x6d and 0x70 */
static int rock_wire(aos_world *w, aos_player *p, int e)
{
	if (e < 0)
		return -1;
	int l = aos_spawn_laser(w, ID, 100, &p->x, p->faceAngle, p->objId, p->side, p->color);
	if (l >= 0)
		w->obj.laser[l].f1c = w->obj.enemy[e].id;
	return l;
}

static bool st_rock(aos_world *w, aos_player *p) /* 0x6d/0x6e/0x6f T2 FUN_007843b0 */
{
	int32_t t = p->timer;
	if (t == 0)
		face_x(w, p);
	if (t == 10 && aos_use_gauge(p, 0x6d)) {
		int e4 = aos_spawn_enemy(w, ID, 100, p->objId, p->side, p->color);
		int e5 = aos_spawn_enemy(w, ID, 100, p->objId, p->side, p->color);
		int32_t v = aos_rand_int(w, 0, 100) % 4;
		if (e4 >= 0) w->obj.enemy[e4].motionId = v;
		v = aos_rand_int(w, 0, 100) % 4;
		if (e5 >= 0) w->obj.enemy[e5].motionId = v;
		float dx = 0, dy = 0;
		if (p->up > 1) dy = 70.0f;
		if (p->down > 1) dy = -70.0f;
		if (p->right > 1) dx = 70.0f;
		if (p->left > 1) dx = -70.0f;
		if (p->B > 1) {
			if (e4 >= 0) w->obj.enemy[e4].fe8[2] = 1; /* +0xea */
			if (e5 >= 0) w->obj.enemy[e5].fe8[2] = 1;
		}
		if (e4 >= 0) {
			aos_enemy *e = &w->obj.enemy[e4];
			e->pos[0] = p->x + dx + aos_cos(2.8915927f) * 100.0f; /* DAT_008264c4 */
			e->pos[1] = p->y + dy + aos_sin(2.8915927f) * 100.0f;
		}
		if (e5 >= 0) {
			aos_enemy *e = &w->obj.enemy[e5];
			e->pos[0] = p->x + dx + aos_cos(0.25f) * 100.0f;
			e->pos[1] = p->y + dy + aos_sin(0.25f) * 100.0f;
		}
		rock_wire(w, p, e4);
		rock_wire(w, p, e5);
		if (e4 >= 0) fx0(w, 0x514, 0, w->obj.enemy[e4].pos, 0, p->objId, p->side);
		if (e5 >= 0) fx0(w, 0x514, 0, w->obj.enemy[e5].pos, 0, p->objId, p->side);
		aos_se(w, 0x2f);
	}
	if (p->timer == 25)
		throw_rocks(w, p);
	t = p->timer;
	if (((t < 12 || 24 < t) && t < 30) || p->D != 3) {
		if (40 < t)
			p->state = 0;
	} else {
		aos_dash(w, p, 1); /* cancelling before t=25 leaves the rocks idle (literal) */
	}
	return true;
}

static bool st_many_rocks(aos_world *w, aos_player *p) /* 0x70 T2 FUN_00785030 */
{
	if (p->timer == 0)
		hyper_start(w, p);
	if (p->timer == 4) {
		for (int i = 0; i < 8; i++) {
			aos_rand_real(w, 120.0f, 240.0f); /* drawn and discarded (literal) */
			float off, r;
			switch (i) {
			case 0: case 1: off = -0.6f; r = 140.0f; break;
			case 2: case 3: off = 0.6f; r = 140.0f; break;
			case 4: case 5: off = 0.2f; r = 200.0f; break;
			default: off = -0.2f; r = 200.0f; break;
			}
			int e = aos_spawn_enemy(w, ID, 100, p->objId, p->side, p->color);
			p->faceAngle = aos_aim(w, p);
			float a = i % 2 == 0 ? off + p->faceAngle : off + p->faceAngle + AOS_PI;
			aos_angle_normalize(&a);
			if (e >= 0) {
				w->obj.enemy[e].pos[0] = aos_cos(a) * r + p->x;
				w->obj.enemy[e].pos[1] = aos_sin(a) * r + p->y;
			}
			int32_t v = aos_rand_int(w, 0, 100) % 4;
			if (e >= 0)
				w->obj.enemy[e].motionId = v;
			rock_wire(w, p, e);
			if (e >= 0)
				fx0(w, 0x514, 0, w->obj.enemy[e].pos, 0, p->objId, p->side);
			aos_se(w, 0x2f);
		}
	}
	if (p->timer == 16)
		throw_rocks(w, p);
	if (p->timer < 19 || p->D != 3) {
		if (40 <= p->timer)
			p->state = 0;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

/* 0x71 T2 FUN_00785810 — unreachable in the original (no code enters 0x71); ported for completeness. */
static bool st_dead71(aos_world *w, aos_player *p)
{
	if (p->timer == 0) {
		for (int i = 0; i < w->obj.nmissile; i++) {
			aos_missile *m = &w->obj.missile[i];
			if (m->ownerId == p->objId && m->id == 100 && m->f64 == 0x14) {
				float dx = m->pos[0] - p->x, dy = m->pos[1] - p->y;
				if (aos_sqrt(dx * dx + dy * dy) < 250.0f)
					p->faceAngle = aos_atan2(m->pos[1] - p->y, m->pos[0] - p->x);
			}
		}
		float a = aos_dir_angle(p);
		if (a < AOS_2PI)
			p->faceAngle = a;
	}
	if (p->timer == 5)
		aos_spawn_laser(w, ID, 200, &p->x, p->faceAngle, p->objId, p->side, p->color);
	if (p->timer == 15) {
		for (int i = 0; i < w->obj.nmissile; i++) {
			aos_missile *m = &w->obj.missile[i];
			if (m->ownerId == p->objId && m->id == 100 && m->f64 == 10) {
				aos_player *o = opp_of_team(w, p->side);
				m->f64 = 0x5a;
				m->moveAngle = aos_atan2(o->y - m->pos[1], o->x - m->pos[0]);
				aos_se_cd(w, 0x24, 2);
			}
		}
	}
	if (p->timer < 20 || p->D != 3) {
		if (40 < p->timer)
			p->state = 0;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_swirl1(aos_world *w, aos_player *p) /* 0x73 T2 FUN_00785ed0 */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		p->moveAngle = p->faceAngle;
		p->speed = 0;
		aos_spawn_enemy(w, ID, 200, p->objId, p->side, p->color);
		aos_se(w, 0x2f);
	}
	if (p->timer == 10 && aos_use_gauge(p, 0x73)) {
		p->faceAngle = aos_aim(w, p);
		p->dashAngle = p->moveAngle = p->faceAngle;
		p->speed = 12.0f;
		aos_se(w, 0x20);
	}
	sparks(w, p);
	if (19 < p->timer && p->B == 3 && aos_use_gauge(p, 0x74)) {
		p->state = 0x74;
		p->timer = 0;
		p->hitFlag = 0;
		p->speed = 12.0f;
		aos_angle_toward(&p->faceAngle, aos_aim(w, p), THIRD_PI);
		p->moveAngle = p->faceAngle;
		aos_se(w, 0x20);
		aos_tab.T2[p->state](w, p);
		return true;
	}
	if (p->timer == 16 && p->B < 2) {
		p->state = 0x76;
		p->timer = 0;
		p->hitFlag = 0;
		aos_se(w, 0x1f);
		aos_tab.T2[p->state](w, p);
	} else if (p->timer < 30 || p->D != 3) {
		if (45 < p->timer)
			p->state = 0;
		p->friction *= 0.5f;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_swirl2(aos_world *w, aos_player *p) /* 0x74 T2 FUN_007862a0 */
{
	sparks(w, p);
	if (11 < p->timer && p->B == 3 && aos_use_gauge(p, 0x75)) {
		p->state = 0x75;
		p->timer = 0;
		p->hitFlag = 0;
		p->speed = 12.0f;
		aos_angle_toward(&p->faceAngle, aos_aim(w, p), THIRD_PI);
		p->moveAngle = p->faceAngle;
		aos_se(w, 0x20);
		aos_tab.T2[p->state](w, p);
		return true;
	}
	if (p->timer == 10 && p->B < 2) {
		p->state = 0x76;
		p->timer = 0;
		p->hitFlag = 0;
		aos_se(w, 0x1f);
		aos_tab.T2[p->state](w, p);
	} else if (p->timer < 20 || p->D != 3) {
		if (35 < p->timer)
			p->state = 0;
		p->friction *= 0.4f;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_swirl3(aos_world *w, aos_player *p) /* 0x75 T2 FUN_00786560 */
{
	sparks(w, p);
	if (p->timer == 12) {
		p->state = 0x76;
		p->timer = 0;
		p->hitFlag = 0;
		aos_se(w, 0x1f);
		aos_tab.T2[p->state](w, p);
	} else if (p->timer < 20 || p->D != 3) {
		if (35 < p->timer)
			p->state = 0;
		p->friction *= 0.3f;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_swirl_release(aos_world *w, aos_player *p) /* 0x76 T2 FUN_00786720 */
{
	if (p->timer < 15 || p->D != 3) {
		if (35 < p->timer)
			p->state = 0;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_accel(aos_world *w, aos_player *p) /* 0x77 T2 FUN_007867c0 */
{
	if (p->timer == 0) {
		face_x(w, p);
		aos_barrier(w, p, 0x14);
		/* SE 7, dark(120,.85), speedline(100,pos,aim,0), blur(120,.5,2), effect 0xfa */
		aos_hyper_flourish_ex(w, p, 7, 0x78, 0.85f, 100, p->faceAngle, 0.0f, 0x78, 0.5f, 2, 1);
	}
	if (p->timer == 0) {
		float d = 60.0f;
		for (int i = 0; i < 6; i++) {
			int e = aos_spawn_enemy(w, ID, i % 2 == 0 ? 300 : 0x12d, p->objId, p->side, p->color);
			if (e < 0) {
				if (i % 2 != 0)
					d += 120.0f;
				continue;
			}
			aos_enemy *en = &w->obj.enemy[e];
			en->pos[1] = p->y + 300.0f;
			en->motionId = (p->timer / 5) % 4;
			if (i % 2 == 0) {
				en->pos[0] = p->x + d;
			} else {
				en->pos[0] = p->x - d;
				d += 120.0f;
			}
			en->angle2 = -HALF_PI;
			int l = aos_spawn_laser(w, ID, 300, en->pos, -HALF_PI, p->objId, p->side, p->color);
			if (l >= 0)
				w->obj.laser[l].f1c = w->obj.enemy[e].id;
		}
	}
	if (p->timer == 75) {
		prox_knock(w, p, 360.0f, 20.0f);
		fx0(w, 0xa8c, 0, &p->x, 0, p->objId, p->side);
	}
	if (p->timer < 80 || p->D != 3) {
		if (100 < p->timer)
			p->state = 0;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

/* ------------------------------------------------------------------ */
/* T1 draw                                                             */

static int pp(int32_t t, int n)
{
	int f = (t / n) & 3;
	return f == 3 ? 1 : f;
}

/* Body draw with Tsih's stealth look (tsih.md §4.2). mode 0 = neutral facing flip,
 * 1 = AIM transform. The display options DAT_0089437f/DAT_00894382 (training state
 * colours) are not in the engine: the stealth tint is always applied. */
static aos_dcmd *body(aos_world *w, aos_player *p, const char *fmt, int frame, int mode)
{
	aos_dcmd *c = NULL;
	int tex = aos_texf(fmt, frame);
	bool st = STEALTH(p) == 1;
	float s = 1.0f;
	if (st)
		s = aos_sin(((float)(w->frame % 50) / 50.0f) * AOS_PI) * 0.2f + 1.0f;
	for (int pass = 0; pass < (st ? 2 : 1); pass++) {
		c = aos_draw(tex, aos_pidx(w, p), p->x, p->y, 0);
		c->halfW = c->halfH = 48.0f * (pass ? s : 1.0f);
		if (mode == 0) {
			c->flipX = p->facingRight != 0;
		} else {
			c->rotZ = aos_rad2bam(p->faceAngle);
			c->flipX = 1;
			if (HALF_PI <= aos_fabs(p->faceAngle))
				c->sy = -1.0f; /* RotX(180°) approximated as y-scale cos(π) */
		}
		c->rgba = st ? aos_rgba(0.5f, 1.0f, 1.0f, 0.75f) : 0xffffffffu; /* 0xdc(a .75, r .5, g 1, b 1) */
		c->blend = pass ? AOS_BLEND_ADD_ALPHA : AOS_BLEND_ALPHA;
	}
	return c;
}

/* body() + FUN_004d7e00 afterimage of the last pass (0x5b, 0x5c, 0x73..0x75), with the
 * rotation the original passes: mode 0 {0, RotY π if facing right, 0}; mode 1 AIM. */
static void body_ai(aos_world *w, aos_player *p, const char *fmt, int frame, int mode)
{
	aos_dcmd *c = body(w, p, fmt, frame, mode);
	int32_t rot[3] = {0, p->facingRight != 0 ? aos_deg2bam(0xb4) : 0, 0};
	if (mode != 0) {
		rot[0] = HALF_PI <= aos_fabs(p->faceAngle) ? aos_deg2bam(0xb4) : 0;
		rot[1] = aos_deg2bam(0xb4);
		rot[2] = aos_rad2bam(p->faceAngle);
	}
	aos_afterimage(w, c, 1, p, rot);
}

static void dr_neutral(aos_world *w, aos_player *p) /* 0x5b T1 FUN_007886a0 */
{
	int spr = 0x5b, f = (p->timer / 5) % 8;
	if (p->up > 1) spr = 0x60;
	if (p->down > 1) spr = 0x61;
	if (p->facingRight == 0 && p->left > 1) spr = 0x60;
	if (p->facingRight == 1 && p->left > 1) spr = 0x61;
	if (p->facingRight == 0 && p->right > 1) spr = 0x61;
	if (p->facingRight == 1 && p->right > 1) spr = 0x60;
	if (spr != 0x5b)
		f = pp(p->timer, 5);
	body_ai(w, p, spr == 0x5b ? "ts_std0%d" : spr == 0x60 ? "ts_fwd0%d" : "ts_bck0%d", f, 0);
}

static void dr_dash(aos_world *w, aos_player *p) /* 0x5c T1 FUN_00788d80 */
{
	int f = pp(p->timer, 3);
	float a = aos_fabs(p->faceAngle);
	if ((p->facingRight == 1 && a > HALF_PI) || (p->facingRight == 0 && a < HALF_PI))
		f += 3;
	body_ai(w, p, "ts_dsh0%d", f, 1);
}

static void dr_ko(aos_world *w, aos_player *p) /* 0x5d T1 FUN_0078a6a0 */
{
	int f = pp(p->timer, 3);
	if (w->frame % 2 == 0)
		f += 3;
	if (p->timer < 100)
		body(w, p, "ts_los0%d", f, 1);
}

static void dr_hit(aos_world *w, aos_player *p) /* 0x5e T1 FUN_00789710 */
{
	body(w, p, "ts_los0%d", pp(p->timer, 3), 1);
}

static int mini(int a, int b) { return a < b ? a : b; }

static void dr_sld5(aos_world *w, aos_player *p) /* 0x5f/0x65/0x66/0x6a/0x6b/0x6c T1 (FUN_00789ee0 …) */
{
	body(w, p, "ts_sld0%d", mini(p->timer / 5, 3), 1);
}

static void dr_acta(aos_world *w, aos_player *p) /* 0x67/0x68/0x71 T1 FUN_0078c560 / 0078cd00 / 0078f770 */
{
	body(w, p, "ts_acta0%d", mini(p->timer / 3, 5), 1);
}

static void dr_two_part(aos_world *w, aos_player *p, int split, int div)
{
	if (p->timer < split)
		body(w, p, "ts_sld0%d", mini(p->timer / div, 3), 1);
	else
		body(w, p, "ts_actb0%d", mini((p->timer - split) / div, 4), 1);
}
static void dr_hyper_bendy(aos_world *w, aos_player *p) { dr_two_part(w, p, 20, 5); } /* 0x69 FUN_0078d4a0 */
static void dr_rock(aos_world *w, aos_player *p) { dr_two_part(w, p, 20, 4); }        /* 0x6d-0x6f FUN_0078dc80 */
static void dr_many_rocks(aos_world *w, aos_player *p) { dr_two_part(w, p, 15, 4); }  /* 0x70 FUN_0078e760 */

static void dr_accel(aos_world *w, aos_player *p) /* 0x77 T1 FUN_0078ef60 */
{
	int32_t t = p->timer;
	if (t < 20)
		body(w, p, "ts_sld0%d", mini(t / 4, 3), 1);
	else if (t < 70)
		body(w, p, "ts_actb0%d", mini((t - 20) / 4, 4), 1);
	else
		body(w, p, "ts_sld0%d", mini((t - 70) / 5, 3), 1);
}

static void dr_swirl1(aos_world *w, aos_player *p) /* 0x73 T1 FUN_00790830 */
{
	body_ai(w, p, "ts_swing0%d", (p->timer >= 13 && p->timer < 22) ? 1 : 0, 1);
}
static void dr_swirl23(aos_world *w, aos_player *p) /* 0x74/0x75 T1 FUN_00791100 / 007919d0 */
{
	body_ai(w, p, "ts_swing0%d", (p->timer >= 6 && p->timer < 15) ? 1 : 0, 1);
}
static void dr_swirl_release(aos_world *w, aos_player *p) /* 0x76 T1 FUN_007922a0 */
{
	body(w, p, "ts_swing0%d", 1, 1);
}

/* ------------------------------------------------------------------ */
/* Beam family (type 5)                                                */

static int beam_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner,
                      int32_t age0, int32_t team) /* FUN_004ba600 */
{
	switch (id) {
	case 100: case 0x69: case 200: case 0xcd: case 0xd2: case 0xd7: case 0xdc: case 0xe1:
	case 300: case 0x136:
		break;
	default:
		return -1;
	}
	int i = aos_beam_new(w, ID, id, pos, angle, owner, age0, team);
	if (i < 0)
		return -1;
	aos_beam *b = &w->obj.beam[i];
	switch (id) {
	case 100: case 0x69:
		b->speed = 12.0f;
		b->scale = 3.0f;
		b->damage = 120;
		b->shape.radius = 8.0f;
		b->knockback = 5.0f;
		break;
	case 200: case 0xcd:
		b->speed = 20.0f;
		b->f6c = id == 200 ? -1.0f : 1.0f;
		b->damage = 240;
		b->shape.radius = 8.0f;
		b->knockback = 10.0f;
		b->id = 200;
		break;
	case 0xd2: case 0xd7:
		b->speed = 20.0f;
		b->f6c = id == 0xd2 ? -1.0f : 1.0f;
		b->damage = 280;
		b->shape.radius = 8.0f;
		b->knockback = 10.0f;
		b->id = 0xd2;
		break;
	case 0xdc: case 0xe1:
		b->speed = 24.0f;
		b->f6c = id == 0xdc ? -1.0f : 1.0f;
		b->damage = 300;
		b->shape.radius = 8.0f;
		b->knockback = 10.0f;
		b->id = 0xdc;
		break;
	case 300:
		b->speed = 16.0f;
		b->alpha = 0;
		b->angle = b->angle + AOS_PI; /* ver > 0x5a0 */
		break;
	case 0x136:
		b->speed = aos_rand_real(w, 0.0f, 12.0f) + 10.0f;
		b->alpha = 0;
		break;
	}
	return i;
}

static void beam_move(aos_beam *b)
{
	b->pos[0] = aos_cos(b->moveAngle) * b->speed + b->pos[0];
	b->pos[1] = aos_sin(b->moveAngle) * b->speed + b->pos[1];
}

/* Wire laser (300 / 0x136) per-frame hitbox; returns the touch test result of 300. */
static bool wire_box(aos_world *w, aos_beam *b, float halfH, bool touchTest)
{
	int h = aos_spawn_hitbox(w, ID, b->age % 4 == 0 ? 100 : 0x6e, b->pos, b->angle, b->ownerId, 0, b->team);
	if (h < 0)
		return false;
	aos_hitbox *hb = &w->obj.hitbox[h];
	hb->shape.halfH = halfH;
	if (!touchTest)
		return false;
	aos_player *o = opp_of_team(w, b->team);
	return o && aos_shape_hit(hb->pos, &o->x, &hb->shape, (const aos_shape *)o->hurtShape, hb->angle, o->faceAngle);
}

static void wire_ends(aos_world *w, aos_beam *b, float dist)
{
	int32_t c = tcol(w, b->team, 2);
	float q[3] = {aos_cos(b->angle - HALF_PI) * dist * b->alpha + b->pos[0],
	              aos_sin(b->angle - HALF_PI) * dist * b->alpha + b->pos[1], 0};
	fx0(w, 0x516, c, q, b->angle, b->ownerId, b->team);
	q[0] = aos_cos(b->angle + HALF_PI) * dist * b->alpha + b->pos[0];
	q[1] = aos_sin(b->angle + HALF_PI) * dist * b->alpha + b->pos[1];
	fx0(w, 0x516, c, q, b->angle, b->ownerId, b->team);
}

static bool beam_update(aos_world *w, aos_beam *b) /* FUN_004bc950 */
{
	switch (b->id) {
	case 100:
	case 0x69:
		if (b->id == 100) {
			if (999 < b->age || b->speed <= 1.0f) {
				b->moveAngle += 0.005f;
				if (AOS_PI < b->moveAngle)
					b->moveAngle -= AOS_2PI;
				if (b->speed < 5.0f)
					b->speed += 0.1f;
			} else {
				b->speed *= 0.925f;
				if (b->speed <= 1.0f) {
					float r = aos_rand_real(w, -HALF_PI, HALF_PI);
					b->moveAngle = r + AOS_PI + b->moveAngle;
					b->age = 1000;
				}
			}
		} else if (999 < b->age || b->speed <= 1.0f) {
			b->moveAngle -= 0.005f;
			if (b->moveAngle < -AOS_PI)
				b->moveAngle += AOS_2PI;
			if (b->speed < 5.0f)
				b->speed += 0.1f;
		} else {
			b->speed *= 0.925f;
			if (b->speed <= 1.0f) {
				float r = aos_rand_real(w, -HALF_PI, HALF_PI);
				b->moveAngle = r + b->moveAngle;
				b->age = 1000;
			}
		}
		beam_move(b);
		if (1.0f < b->scale)
			b->scale -= 0.05f;
		if (!aos_in_arena(b->pos, 20.0f)) {
			b->id = 0x6e;
			b->active = 0;
			b->scale = 3.0f;
			fx0(w, 0x516, tcol(w, b->team, 0), b->pos, 0, b->ownerId, b->team);
		}
		break;
	case 0x6e:
		b->scale -= 0.5f;
		if (b->scale <= 0.0f)
			return false;
		break;
	case 200:
	case 0xd2:
		if (b->age < 1000) {
			if (b->age < 10)
				b->moveAngle = (b->speed * 0.01f * b->f6c * (float)b->age) / 10.0f + b->moveAngle;
			else
				b->moveAngle = b->speed * 0.01f * b->f6c + b->moveAngle;
			b->angle = b->moveAngle;
			if (8.0f < b->speed)
				b->speed -= 0.5f;
			if (150 < b->age)
				b->age = 1000;
		} else {
			b->speed += 0.1f;
			if (b->id == 0xd2 && b->age < 0x41a) {
				aos_player *o = opp_of_team(w, b->team);
				if (o)
					aos_angle_toward(&b->moveAngle, aos_atan2(o->y - b->pos[1], o->x - b->pos[0]),
					                 ((1100.0f - (float)b->age) / 100.0f) * 0.05f);
			}
			b->moveAngle = aos_sin(((float)(b->age % 20) / 20.0f) * AOS_2PI) * 0.1f * b->f6c + b->moveAngle;
			b->angle = b->moveAngle;
		}
		beam_move(b);
		if (0x406 < b->age && !aos_in_arena(b->pos, 20.0f))
			return false;
		aos_spawn_effect(w, ID, 100, tcol(w, b->team, 1), b->pos, b->angle, b->ownerId, 0, b->team);
		break;
	case 0xdc:
		if (b->age < 1000) {
			if (b->age < 10)
				b->moveAngle = (b->speed * 0.0075f * b->f6c * (float)b->age) / 10.0f + b->moveAngle;
			else
				b->moveAngle = b->speed * 0.0075f * b->f6c + b->moveAngle;
			b->angle = b->moveAngle;
			if (10.0f < b->speed)
				b->speed -= 0.5f;
			if (150 < b->age)
				b->age = 1000;
		} else {
			if (16.0f <= b->speed)
				b->speed -= 0.1f;
			b->moveAngle = aos_sin(((float)(b->age % 40) / 40.0f) * AOS_2PI) * 0.15f * b->f6c + b->moveAngle;
			b->angle = b->moveAngle;
		}
		beam_move(b);
		if (0x406 < b->age && !aos_in_arena(b->pos, 20.0f))
			return false;
		aos_spawn_effect(w, ID, 100, tcol(w, b->team, 2), b->pos, b->angle, b->ownerId, 0, b->team);
		break;
	case 300: {
		bool touch = false;
		if (b->age < 200) {
			b->alpha += 0.04f;
			if (1.0f < b->alpha)
				b->alpha = 1.0f;
			if (b->age % 10 == 0)
				wire_ends(w, b, 80.0f);
			touch = wire_box(w, b, 80.0f * b->alpha, true);
		} else {
			b->alpha -= 0.02f;
			if (b->alpha <= 0.0f)
				return false;
		}
		if (touch && 29 < b->age)
			b->speed *= 0.0f; /* PTR_00853af4: stops for good */
		else
			b->speed *= 0.9f;
		beam_move(b);
		break;
	}
	case 0x136:
		if (b->age < 300) {
			b->alpha += 0.04f;
			if (1.0f < b->alpha)
				b->alpha = 1.0f;
			if (b->age % 10 == 0)
				wire_ends(w, b, 120.0f);
			if (10 < b->age)
				wire_box(w, b, 120.0f * b->alpha, false);
		} else {
			b->alpha -= 0.02f;
			if (b->alpha <= 0.0f)
				return false;
		}
		b->speed *= 0.9f;
		beam_move(b);
		break;
	}
	return true;
}

static void beam_on_hit(aos_world *w, aos_beam *b) /* FUN_004bc6d0 */
{
	int k = (b->id == 100 || b->id == 0x69) ? 0 : 1;
	if (b->id != 100 && b->id != 0x69 && b->id != 200 && b->id != 0xcd && b->id != 0xd2 &&
	    b->id != 0xd7 && b->id != 0xdc && b->id != 0xe1)
		return;
	int32_t c = tcol(w, b->team, k);
	fx0(w, 1000, c, b->pos, b->angle + AOS_PI, b->ownerId, b->team);
	aos_spawn_particle(w, 1000, c, b->pos, b->angle, b->ownerId, 0, 3);
	aos_spawn_spark(w, 1000, c, 10, 5, b->angle, b->pos, b->ownerId);
	aos_se_cd(w, 0x13, 3);
}

/* beamsphere sheet cells (FUN_004baaa0): glow 100×100 / core 50×50 by colour index c */
static void sphere(float x, float y, int32_t c, bool glow, float hw, float hh, float rot, float a, int blend)
{
	aos_dcmd *d = aos_draw(aos_tex("beamsphere"), AOS_BANK_COMMON, x, y, 0);
	d->hasSrc = 1;
	if (glow) {
		float x0 = (float)(((c % 10) & 1) * 100 + 50 + (c / 10) * 250), y0 = (float)(((c % 10) / 2) * 100);
		d->src[0] = x0; d->src[1] = y0; d->src[2] = x0 + 99.0f; d->src[3] = y0 + 99.0f;
	} else {
		float x0 = (float)((c / 10) * 250), y0 = (float)((c % 10) * 50);
		d->src[0] = x0; d->src[1] = y0; d->src[2] = x0 + 49.0f; d->src[3] = y0 + 49.0f;
	}
	d->halfW = hw;
	d->halfH = hh;
	d->rotZ = aos_rad2bam(rot);
	d->rgba = aos_rgba(1, 1, 1, a);
	d->blend = (uint8_t)blend;
}

static void beam_draw(aos_world *w, aos_beam *b) /* FUN_004baaa0 */
{
	switch (b->id) {
	case 100: case 0x69: case 0x6e: {
		int32_t c = tcol(w, b->team, 0);
		sphere(b->pos[0], b->pos[1], c, true, 30.0f * b->scale, 30.0f * b->scale, 0,
		       (float)(int)(96.0f * b->alpha) / 255.0f, AOS_BLEND_ALPHA);
		sphere(b->pos[0], b->pos[1], c, false, 15.0f * b->scale, 15.0f * b->scale, 0,
		       (float)(int)(255.0f * b->alpha) / 255.0f, AOS_BLEND_ADD_ALPHA);
		break;
	}
	case 300: case 0x136: {
		int32_t c = tcol(w, b->team, 2);
		float len = b->id == 300 ? 90.0f : 120.0f, core = b->id == 300 ? 100.0f : 120.0f;
		float end = b->id == 300 ? 80.0f : 120.0f;
		sphere(b->pos[0], b->pos[1], c, true, 10.0f, len * b->alpha, b->angle,
		       (float)(int)(64.0f * b->alpha) / 255.0f, AOS_BLEND_ALPHA);
		sphere(b->pos[0], b->pos[1], c, false, 2.0f, core * b->alpha, b->angle,
		       (float)(int)(255.0f * b->alpha) / 255.0f, AOS_BLEND_ADD_ALPHA);
		for (int s = -1; s <= 1; s += 2) {
			float x = aos_cos(b->moveAngle + (float)s * HALF_PI) * end * b->alpha + b->pos[0];
			float y = aos_sin(b->moveAngle + (float)s * HALF_PI) * end * b->alpha + b->pos[1];
			sphere(x, y, c, true, 20.0f * b->scale, 20.0f * b->scale, 0,
			       (float)(int)(96.0f * b->alpha) / 255.0f, AOS_BLEND_ALPHA);
			sphere(x, y, c, false, 6.0f * b->scale, 6.0f * b->scale, 0,
			       (float)(int)(255.0f * b->alpha) / 255.0f, AOS_BLEND_ADD_ALPHA);
		}
		/* 0x136: bar (10, 120a) / core (2, 120a), orbs at 120a (DAT_00814174) — FUN_004baaa0 case 0x136 */
		break;
	}
	default:
		break; /* 200/0xd2/0xdc: drawn by effect 100 */
	}
}

/* ------------------------------------------------------------------ */
/* Hitbox family (type 5)                                              */

static int hitbox_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner,
                        int32_t zero, int32_t team) /* FUN_005efd20 */
{
	int32_t dmg;
	float hw = 0, r = 0, kb = 0;
	uint8_t once = 0, rehit = 0;
	switch (id) {
	case 100: dmg = 0x28; hw = 10.0f; once = 1; break;
	case 0x6e: dmg = 0; hw = 10.0f; once = 1; break;
	case 200: dmg = 0x118; r = 50.0f; kb = 10.0f; rehit = 1; break;
	case 300: dmg = 0xf0; r = 50.0f; kb = 7.5f; rehit = 1; break;
	case 0x12d: dmg = 0x168; r = 50.0f; kb = 9.0f; rehit = 1; break; /* ver > 0x5a0 */
	case 0x12e: dmg = 0x1e0; r = 50.0f; kb = 22.5f; rehit = 1; break;
	case 0x136: dmg = 0x1a4; r = 50.0f; kb = 22.5f; rehit = 1; break; /* ver > 0x5a0 */
	case 400: dmg = 0xfa; r = 90.0f; kb = 5.0f; break;
	default: return -1;
	}
	int i = aos_hitbox_new(w, ID, id, pos, angle, owner, zero, team);
	if (i < 0)
		return -1;
	aos_hitbox *h = &w->obj.hitbox[i];
	h->hitType = 3;
	h->oncePerFrame = once;
	h->damage = dmg;
	h->shape.halfW = hw;
	h->shape.radius = r;
	h->knockback = kb;
	h->rehitFlag = rehit;
	return i;
}

static void streaks(aos_world *w, aos_hitbox *hb, aos_player *v, int lo, int hi, float div)
{
	int32_t c = tcol(w, hb->team, 2);
	for (int k = lo; k < hi; k++) {
		float base = ((float)k / div) * AOS_PI;
		float r = aos_rand_real(w, -0.25f, 0.25f);
		fx0(w, 0x456, c, &v->x, base + r, hb->ownerId, hb->team);
	}
}

static void hitbox_on_hit(aos_world *w, aos_hitbox *hb, aos_player *v) /* FUN_005f01b0 */
{
	aos_player *me = self_of_team(w, hb->team);
	switch (hb->id) {
	case 100:
		if (v->state == AOS_ST_HITSTUN && v->timer < 11) {
			hb->dealsDamage = 0;
		} else {
			streaks(w, hb, v, -3, 3, 3.0f);
			fx0(w, 0x515, tcol(w, hb->team, 2), &v->x, 0, hb->ownerId, hb->team);
			/* +0x128 = attacker's combo hit counter */
			if (me && 7 < me->comboShowHits)
				hb->knockback = 10.0f;
			aos_se_cd(w, 0x15, 4);
		}
		break;
	case 0x6e:
		if (v->state == AOS_ST_HITSTUN && v->timer < 11)
			hb->dealsDamage = 0;
		else if (me && 7 < me->comboShowHits)
			hb->knockback = 10.0f;
		break;
	case 200:
		if (v->state != AOS_ST_HITSTUN || v->speed <= 2.0f) {
			streaks(w, hb, v, -4, 4, 4.0f);
			aos_screen_blur(w, 10, 0.5f, 1); /* DAT_00853b1c */
			aos_se_cd(w, 0x2d, 4);
		} else {
			hb->dealsDamage = 0;
		}
		break;
	case 300:
	case 0x12d:
		if (v->state != AOS_ST_HITSTUN || v->speed <= 2.0f) {
			if (me)
				hb->angle = aos_atan2(v->y - me->y, v->x - me->x);
			streaks(w, hb, v, -4, 4, 4.0f);
			aos_screen_blur(w, 10, 0.5f, 1); /* DAT_00853b1c */
			aos_se(w, 0x18);
		} else {
			hb->dealsDamage = 0;
		}
		break;
	case 0x12e:
		if (v->state != AOS_ST_HITSTUN || v->speed <= 2.0f) {
			if (me)
				hb->angle = aos_atan2(v->y - me->y, v->x - me->x);
			streaks(w, hb, v, -4, 4, 4.0f);
			fx0(w, 0xa8d, 0, &v->x, 0, hb->ownerId, hb->team);
			aos_screen_blur(w, 10, 0.75f, 1); /* DAT_00813f3c */
			aos_se(w, 0x19);
		} else {
			hb->dealsDamage = 0;
		}
		break;
	case 0x136:
		if (v->state != AOS_ST_HITSTUN || v->speed <= 2.0f) {
			hb->angle = aos_atan2(v->y - hb->pos[1], v->x - hb->pos[0]);
			fx0(w, 0xa8d, 0, &v->x, 0, hb->ownerId, hb->team);
			aos_screen_blur(w, 10, 0.75f, 1); /* DAT_00813f3c */
			aos_se(w, 0x19);
		} else {
			hb->dealsDamage = 0;
		}
		break;
	case 400:
		if (v->state != AOS_ST_HITSTUN || v->speed <= 3.6f) {
			streaks(w, hb, v, -4, 4, 4.0f);
			fx0(w, 0xaf0, 0, &v->x, 0, hb->ownerId, hb->team);
			aos_se_cd(w, 0x2d, 4);
		} else {
			hb->dealsDamage = 0;
		}
		break;
	}
}

/* ------------------------------------------------------------------ */
/* Enemy family (type 5): rocks, swirl, huge rocks                     */

static int enemy_spawn(aos_world *w, int32_t id, int32_t owner, int32_t team, int32_t p18) /* FUN_0055f1e0 */
{
	if (id != 100 && id != 0x65 && id != 200 && id != 300 && id != 0x12d)
		return -1;
	int32_t grp = aos_enemy_group_next(w); /* FUN_0055efc0, once per spawn (before FUN_0055ee70) */
	int i = aos_enemy_new(w, ID, owner, team, p18);
	if (i < 0)
		return -1;
	aos_enemy *e = &w->obj.enemy[i];
	e->serial = grp;
	e->f84 = 0;
	e->active = 0;
	e->scale[0] = 0;
	e->scale[1] = 3.0f;
	e->scale[2] = 1.0f;
	e->mirrorChildren = 0;
	e->motionId = 0;
	e->rehitTimer = 0;
	switch (id) {
	case 100: case 0x65:
		e->f10 = e->f7c = 100;
		e->hp = e->maxHp = 0xfa;
		e->shape.radius = 40.0f;
		spin_set(e, id == 100 ? 0.05f : -0.05f);
		break;
	case 200:
		e->f10 = e->f7c = 200;
		e->hp = e->maxHp = 400;
		e->shape.radius = 40.0f;
		break;
	default: /* 300, 0x12d */
		e->f10 = e->f7c = 300;
		e->hp = e->maxHp = 999;
		e->invulnerable = 1;
		e->shape.radius = 80.0f;
		spin_set(e, id == 300 ? 0.05f : -0.05f);
		break;
	}
	return i;
}

static void grow(aos_enemy *e)
{
	e->scale[1] -= 0.2f;
	e->scale[0] += 0.1f;
	if (1.0f <= e->scale[0]) {
		e->scale[0] = 1.0f;
		e->scale[1] = 1.0f;
		e->f84 = 100;
		e->timer2 = 0;
		e->active = 1;
	}
}

static bool shrink(aos_enemy *e)
{
	e->scale[1] += 0.2f;
	e->scale[0] -= 0.1f;
	return !(e->scale[0] <= 0.0f);
}

static void enemy_move(aos_enemy *e)
{
	e->pos[0] = aos_cos(e->angle2) * e->f70 + e->pos[0];
	e->pos[1] = aos_sin(e->angle2) * e->f70 + e->pos[1];
}

static int ehit(aos_world *w, aos_enemy *e, int32_t id, float angle)
{
	return aos_spawn_hitbox(w, ID, id, e->pos, angle, e->ownerId, 0, e->team);
}

static bool enemy_update(aos_world *w, aos_enemy *e) /* FUN_0055f6b0 */
{
	switch (e->f7c) {
	case 100: /* rock */
		if (e->f84 == 0) {
			grow(e);
		} else if (e->f84 == 100) {
			if (500 < e->timer2) {
				e->f84 = 900;
				e->timer2 = 0;
				e->active = 0;
			}
		} else if (e->f84 == 200) {
			e->f70 += 0.35f;
			if (e->fe8[2] == 1) { /* +0xea: B held at spawn */
				aos_player *me = self_of_team(w, e->team);
				if (me) {
					if (e->fe8[1] == 0) /* +0xe9 */
						e->angle2 = aos_atan2(me->y - e->pos[1], me->x - e->pos[0]); /* literal: aims at Tsih */
					float dy = me->y - e->pos[1], dx = me->x - e->pos[0];
					if (dy < 3000.0f && -3000.0f < dy && dx < 3000.0f && -3000.0f < dx)
						e->fe8[1] = 1;
				}
			}
			enemy_move(e);
			ehit(w, e, 200, e->angle2);
			if (200 < e->timer2) {
				e->f84 = 900;
				e->timer2 = 0;
				e->active = 0;
			}
		} else {
			if (!shrink(e))
				return false;
		}
		break;
	case 200: { /* swirl */
		if (e->f84 == 0) {
			grow(e);
		} else if (e->f84 == 900) {
			if (!shrink(e))
				return false;
		}
		if (e->f84 != 900) {
			aos_player *o = self_of_team(w, e->team); /* owner (reads its timer after the ++) */
			if (!o)
				return true;
			float r, a;
			if (o->state == 0x73) {
				r = -80.0f;
				a = o->timer < 10 ? 0.0f : (((float)(o->timer - 10) / 3.0f) * AOS_PI) / 3.0f;
			} else if (o->state == 0x74 || o->state == 0x75) {
				r = o->state == 0x74 ? -120.0f : -160.0f;
				a = (((float)o->timer / 3.0f) * AOS_PI) / 3.0f;
			} else if (o->state == 0x76) {
				e->f7c = 0xd2;
				e->timer2 = 0;
				e->f70 = 20.0f;
				e->angle2 = o->faceAngle;
				e->active = 1;
				return true;
			} else {
				e->active = 0;
				e->f84 = 900;
				return true;
			}
			if (AOS_2PI < a)
				a = AOS_2PI;
			e->pos[0] = aos_cos(a) * r;
			e->pos[1] = aos_sin(a) * r * 0.1f;
			aos_vec_rotate(e->pos, o->faceAngle);
			e->pos[0] += o->x;
			e->pos[1] += o->y;
			e->angle = o->faceAngle;
			if (e->active == 1) {
				if (o->state == 0x73)
					ehit(w, e, 300, e->angle);
				else if (o->state == 0x74)
					ehit(w, e, 0x12d, e->angle);
				else
					ehit(w, e, 0x12e, e->angle);
			}
		}
		break;
	}
	case 0xd2: /* launched swirl */
		if (e->f84 == 100) {
			if (30 < e->timer2)
				e->f70 *= 0.95f;
			enemy_move(e);
			e->angle += 0.1f;
			if (AOS_PI < e->angle)
				e->angle -= AOS_2PI;
			if (e->timer2 < 50)
				ehit(w, e, 0x136, e->angle2);
			if (e->timer2 == 100) {
				e->active = 0;
				e->angle = 0;
				e->f84 = 900;
			}
		} else if (!shrink(e)) {
			return false;
		}
		break;
	case 300: /* huge rock */
		if (e->f84 == 0) {
			grow(e);
		} else if (e->f84 == 100) {
			e->f70 += 1.0f;
			enemy_move(e);
			e->angle += spin(e);
			if (AOS_PI < e->angle)
				e->angle -= AOS_2PI;
			if (e->angle < -AOS_PI)
				e->angle += AOS_2PI;
			ehit(w, e, 400, -HALF_PI);
			if (29 < e->timer2) {
				e->f70 = 0;
				e->f84 = 900;
				e->timer2 = 0;
				aos_se(w, 0x19);
			}
		} else {
			if (e->timer2 < 21 && e->timer2 % 2 == 0) {
				float q[3] = {e->pos[0], e->pos[1] - 50.0f, 0};
				float r = aos_rand_real(w, -0.25f, 0.25f);
				int32_t n = aos_rand_int(w, 0, 100) % 8;
				int fe = fx0(w, 0xa82 + n, 0xd, q, r + HALF_PI, e->ownerId, e->team);
				if (fe >= 0)
					w->obj.effect[fe].aux2 = 0.5f;
			}
			if (e->timer2 < 20) {
				e->pos[0] = aos_rand_real(w, -2.0f, 2.0f) + e->pos[0];
				e->pos[1] = aos_rand_real(w, -2.0f, 2.0f) + e->pos[1];
			}
			if (20 < e->timer2) {
				if (e->timer2 < 40)
					ehit(w, e, 400, HALF_PI);
				e->f70 -= 0.25f;
				enemy_move(e);
			}
			if (60 < e->timer2) {
				e->angle = 0;
				e->scale[1] += 0.2f;
				e->scale[0] -= 0.1f;
				e->active = 0;
			}
			if (e->scale[0] <= 0.0f)
				return false;
		}
		break;
	}
	return true;
}

static bool enemy_on_killed(aos_world *w, aos_enemy *e) /* FUN_005605f0 */
{
	if (e->f10 == 100 || e->f10 == 200) {
		fx0(w, 0x514, tcol(w, e->team, 0), e->pos, 0, e->ownerId, e->team);
		aos_spawn_effect(w, ID, 200, e->f10 == 100 ? e->motionId : 0, e->pos, 0, e->ownerId, 0, e->team);
		aos_se(w, 0x1d);
	}
	return false;
}

static uint32_t glow_last[8] = {0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
                                0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};

static void enemy_draw(aos_world *w, aos_enemy *e) /* FUN_00560760 */
{
	aos_player *o = aos_player_by_objid(w, e->ownerId);
	int bank = o ? aos_pidx(w, o) : 0;
	float g = aos_sin(((float)(e->age % 50) / 50.0f) * AOS_2PI) * 0.25f + 0.75f;
	int v = e->f10 == 200 ? 0 : e->motionId * 2;
	float k = e->f10 == 300 ? 2.0f : 1.0f;
	aos_dcmd *c = aos_draw(aos_texf("ts_rock0%d", v), bank, e->pos[0], e->pos[1], 0);
	c->halfW = c->halfH = 48.0f;
	c->sx = e->scale[0] * k;
	c->sy = e->scale[1] * k;
	c->rotZ = aos_rad2bam(e->angle);
	if (e->f10 == 300 || 1.0f <= e->scale[0])
		c->rgba = 0xffffffffu;
	else
		c->rgba = aos_rgba(0, 0, 0, 1); /* (a1, r0, g0, b0): black while growing */
	c->blend = AOS_BLEND_ALPHA;
	c = aos_draw(aos_texf("ts_rock0%d", v + 1), bank, e->pos[0], e->pos[1], 0);
	c->halfW = c->halfH = 48.0f;
	c->sx = e->scale[0] * k;
	c->sy = e->scale[1] * k;
	c->rotZ = aos_rad2bam(e->angle);
	/* Below full size the original makes no +0xdc call: the glow model keeps the material it
	 * was last given (draw-only state, kept per rock frame; models start white). */
	if (1.0f <= e->scale[0])
		glow_last[(v + 1) & 7] = aos_rgba(1, 1, 1, g);
	c->rgba = glow_last[(v + 1) & 7];
	c->blend = AOS_BLEND_ADD_ALPHA;
}

/* ------------------------------------------------------------------ */
/* Laser family (type 5): visual wires (no collision: zero shapes)     */

static int laser_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner,
                       int32_t team, int32_t p18) /* FUN_006308f0 */
{
	if (id != 100 && id != 200 && id != 300)
		return -1;
	int i = aos_laser_new(w, ID, id, pos, angle, owner, team, p18);
	if (i < 0)
		return -1;
	aos_laser *l = &w->obj.laser[i];
	l->blockMode = 0;
	l->hitType = 0;
	l->damage = 0;
	l->knockback = 0;
	l->stun = 0;
	return i;
}

static void strand(aos_laser *l, float ang, float xoff, bool vertical)
{
	static const aos_shape zero = {0, 0, 0};
	for (int k = 0; k < 20; k++) {
		float q[3];
		q[0] = vertical ? l->origin[0] + xoff : aos_cos(ang) * (l->knockback / 20.0f) * (float)k + l->origin[0];
		q[1] = aos_sin(ang) * (l->knockback / 20.0f) * (float)k + l->origin[1];
		q[2] = 0;
		if (aos_laser_push_seg(l, &zero, q))
			l->seg[l->nseg - 1].pad = k < 5 ? 0.0f : k < 15 ? (float)(k - 5) / 10.0f : 1.0f;
	}
}

static bool laser_update(aos_world *w, aos_laser *l) /* FUN_00630f70 */
{
	if (l->id == 100) {
		l->nseg = 0;
		aos_player *o = aos_player_by_objid(w, l->ownerId);
		aos_enemy *r = aos_enemy_by_id(w, l->f1c);
		if (!o || !r)
			return false;
		if ((r->f84 == 200 && 30 < r->timer2) || r->f84 == 900)
			return false;
		l->origin[0] = o->x;
		l->origin[1] = o->y;
		l->origin[2] = o->z;
		l->angle = aos_atan2(r->pos[1] - o->y, r->pos[0] - o->x);
		float dx = r->pos[0] - o->x, dy = r->pos[1] - o->y;
		l->knockback = aos_sqrt(dx * dx + dy * dy); /* +0x44 holds the wire length */
		strand(l, l->angle, 0, false);
		strand(l, l->angle - 0.05f, 0, false);
		strand(l, l->angle + 0.05f, 0, false);
	} else if (l->id == 300) {
		l->nseg = 0;
		aos_enemy *r = aos_enemy_by_id(w, l->f1c);
		if (!r)
			return false;
		if (r->f84 == 900 && 60 < r->timer2)
			return false;
		l->angle = aos_atan2(r->pos[1] - l->origin[1], r->pos[0] - l->origin[0]);
		float dx = r->pos[0] - l->origin[0], dy = r->pos[1] - l->origin[1];
		l->knockback = aos_sqrt(dx * dx + dy * dy);
		if (l->age % 5 == 0)
			fx0(w, 0x546, tcol(w, l->team, 0), l->origin, -HALF_PI, l->ownerId, l->team);
		strand(l, l->angle, 10.0f, true);
		strand(l, l->angle - 0.05f, 0.0f, true);
		strand(l, l->angle + 0.05f, -10.0f, true);
	} else {
		/* id 200: only spawned by the unreachable state 0x71 (no code enters it) — deliberately not ported */
		return false;
	}
	return true;
}

static void laser_draw(aos_world *w, aos_laser *l) /* FUN_00630a80 */
{
	for (int k = 0; k < l->nseg; k++) {
		aos_seg *s = &l->seg[k];
		int x = (int)(aos_sin(((float)((l->age + k) % 10) / 10.0f) * AOS_2PI) * 50.0f + 50.0f);
		/* FUN_00457ae0: line from seg + R(len/40,0) to seg - R(len/40,0), 1 screen px wide */
		float hx = l->knockback / 40.0f, h[2] = {hx, 0}, q[2] = {-hx, 0};
		aos_vec_rotate(h, l->angle);
		aos_vec_rotate(q, l->angle);
		uint32_t rgba = (uint32_t)((0xe6 - x / 2) & 0xff) << 24 | (uint32_t)((0xeb - x / 3) & 0xff) << 16 |
		                0xffu << 8 | (uint32_t)((int)(255.0f * s->pad) & 0xff);
		aos_draw_line(h[0] + s->pos[0], h[1] + s->pos[1], q[0] + s->pos[0], q[1] + s->pos[1],
		              w->obj.zoom, rgba, AOS_BLEND_ALPHA);
	}
}

static void laser_on_hit(aos_world *w, aos_laser *l, int32_t seg, aos_player *p) /* FUN_00630f50 */
{
	(void)w; (void)l; (void)seg; (void)p;
}

/* ------------------------------------------------------------------ */
/* Effect family (type 5)                                              */

static int effect_spawn(aos_world *w, int32_t id, int32_t variant, const float pos[3], float angle,
                        int32_t owner, int32_t age0, int32_t team) /* FUN_005142a0 */
{
	if (id != 100 && id != 200)
		return -1;
	int i = aos_effect_new(w, ID, id, variant, pos, angle, owner, age0, team);
	if (i < 0)
		return -1;
	w->obj.effect[i].scale = 1.0f;
	w->obj.effect[i].alpha = 1.0f;
	w->obj.effect[i].aux = 0;
	return i;
}

static bool effect_update(aos_world *w, aos_effect *e) /* FUN_00514b70 */
{
	(void)w;
	if (e->id == 100) {
		if (5 < e->age)
			e->alpha -= 0.05f;
		if (e->alpha <= 0.0f)
			return false;
	} else if (e->id == 200) {
		e->alpha -= 0.1f;
		e->scale += 0.1f;
		if (e->alpha <= 0.0f)
			return false;
	}
	return true;
}

static void effect_draw(aos_world *w, aos_effect *e) /* FUN_005143e0 */
{
	if (e->id == 100) {
		int32_t c = e->variant;
		aos_player *tp = self_of_team(w, e->team);
		int32_t bank = tp ? tp->color : 0; /* DAT_00885a00[team] */
		aos_dcmd *d = aos_draw(aos_tex("beamemitb"), AOS_BANK_COMMON, e->pos[0], e->pos[1], 0);
		d->hasSrc = 1;
		d->src[0] = (float)((c / 8) * 240); d->src[1] = (float)((c % 8) * 64);
		d->src[2] = d->src[0] + 119.0f; d->src[3] = d->src[1] + 63.0f;
		d->halfW = 60.0f;
		d->halfH = 26.0f * e->scale;
		d->rotZ = aos_rad2bam(e->rotation);
		d->rgba = aos_rgba(1, 1, 1, (float)(int)(32.0f * e->alpha) / 255.0f);
		d->blend = AOS_BLEND_ALPHA;
		if (e->age < 2) {
			/* cell index DAT_00895c34: only ever read (refs 0x51461d..0x514661), BSS = 0 */
			d = aos_draw(aos_tex("beamemit"), AOS_BANK_COMMON, e->pos[0], e->pos[1], 0);
			d->hasSrc = 1;
			d->src[0] = 0; d->src[1] = 0; d->src[2] = 127.0f; d->src[3] = 63.0f;
			d->halfW = 48.0f;
			d->halfH = 12.0f * e->scale;
			d->rotZ = aos_rad2bam(e->rotation);
			d->blend = AOS_BLEND_ADD_ALPHA;
		} else {
			d = aos_draw(aos_tex("beamemitb"), AOS_BANK_COMMON, e->pos[0], e->pos[1], 0);
			d->hasSrc = 1;
			d->src[0] = (float)((c / 8) * 240 + 120); d->src[1] = (float)((c % 8) * 64);
			d->src[2] = (float)((c / 8) * 240 + 0xef); d->src[3] = d->src[1] + 63.0f;
			d->halfW = 40.0f;
			d->halfH = 8.0f * e->scale;
			d->rotZ = aos_rad2bam(e->rotation);
			float a = (float)(int)(255.0f * e->alpha * e->alpha) / 255.0f;
			if (e->age < 6)
				d->rgba = aos_rgba(1, 1, 1, a);
			else if (bank == 0)
				d->rgba = aos_rgba(0, 128.0f / 255.0f, 1, a);
			else
				d->rgba = aos_rgba(1, 128.0f / 255.0f, 0, a);
			d->blend = AOS_BLEND_ADD_ALPHA;
		}
	} else if (e->id == 200) {
		aos_player *tp = self_of_team(w, e->team);
		aos_dcmd *d = aos_draw(aos_texf("ts_rock0%d", e->variant * 2), tp ? aos_pidx(w, tp) : 0,
		                       e->pos[0], e->pos[1], 0);
		d->hasSrc = 1;
		d->src[0] = 0; d->src[1] = 0; d->src[2] = 255.0f; d->src[3] = 255.0f;
		d->halfW = d->halfH = 48.0f * e->scale;
		d->rotZ = aos_rad2bam(e->rotation);
		d->rgba = aos_rgba(1, 1, 1, (float)(int)(255.0f * e->alpha) / 255.0f);
		d->blend = AOS_BLEND_ALPHA;
	}
}

/* ------------------------------------------------------------------ */

void aos_register_tsih(void)
{
	aos_tab.inputA[ID] = input_a; /* 0x881324 FUN_00786e70 */
	aos_tab.inputB[ID] = input_b; /* 0x881374 FUN_00787360 */
	/* CPU AI FUN_00787fe0 / FUN_00787850: not reversed (NULL) */

	aos_tab.T2[0x5b] = st_neutral;       aos_tab.T1[0x5b] = dr_neutral;
	aos_tab.T2[0x5c] = st_dash_hook;     aos_tab.T1[0x5c] = dr_dash;
	aos_tab.T2[0x5d] = st_ko_hook;       aos_tab.T1[0x5d] = dr_ko;
	aos_tab.T2[0x5e] = st_hit_hook;      aos_tab.T1[0x5e] = dr_hit;
	aos_tab.T2[0x5f] = st_stub;          aos_tab.T1[0x5f] = dr_sld5;
	aos_tab.T2[0x65] = st_stealth_on;    aos_tab.T1[0x65] = dr_sld5;
	aos_tab.T2[0x66] = st_stealth_off;   aos_tab.T1[0x66] = dr_sld5;
	aos_tab.T2[0x67] = st_bendy;         aos_tab.T1[0x67] = dr_acta;
	aos_tab.T2[0x68] = st_bendy_many;    aos_tab.T1[0x68] = dr_acta;
	aos_tab.T2[0x69] = st_hyper_bendy;   aos_tab.T1[0x69] = dr_hyper_bendy;
	aos_tab.T2[0x6a] = st_a_press;       aos_tab.T1[0x6a] = dr_sld5;
	aos_tab.T2[0x6b] = st_wire;          aos_tab.T1[0x6b] = dr_sld5;
	aos_tab.T2[0x6c] = st_rounding_wire; aos_tab.T1[0x6c] = dr_sld5;
	aos_tab.T2[0x6d] = st_rock;          aos_tab.T1[0x6d] = dr_rock;
	aos_tab.T2[0x6e] = st_rock;          aos_tab.T1[0x6e] = dr_rock;
	aos_tab.T2[0x6f] = st_rock;          aos_tab.T1[0x6f] = dr_rock;
	aos_tab.T2[0x70] = st_many_rocks;    aos_tab.T1[0x70] = dr_many_rocks;
	aos_tab.T2[0x71] = st_dead71;        aos_tab.T1[0x71] = dr_acta;
	aos_tab.T2[0x73] = st_swirl1;        aos_tab.T1[0x73] = dr_swirl1;
	aos_tab.T2[0x74] = st_swirl2;        aos_tab.T1[0x74] = dr_swirl23;
	aos_tab.T2[0x75] = st_swirl3;        aos_tab.T1[0x75] = dr_swirl23;
	aos_tab.T2[0x76] = st_swirl_release; aos_tab.T1[0x76] = dr_swirl_release;
	aos_tab.T2[0x77] = st_accel;         aos_tab.T1[0x77] = dr_accel;

	aos_otab.beamSpawn[ID] = beam_spawn;       /* 0x88188c */
	aos_otab.beamUpdate[ID] = beam_update;     /* 0x8818dc */
	aos_otab.beamDraw[ID] = beam_draw;         /* 0x88192c */
	aos_otab.beamOnHit[ID] = beam_on_hit;      /* 0x88197c */
	/* Missile family (FUN_00685d40 …) is registered in the original but never spawned. */
	aos_otab.hitboxSpawn[ID] = hitbox_spawn;   /* 0x881bac */
	aos_otab.hitboxOnHit[ID] = hitbox_on_hit;  /* 0x881bfc */
	aos_otab.laserSpawn[ID] = laser_spawn;     /* 0x881c4c */
	aos_otab.laserUpdate[ID] = laser_update;   /* 0x881c9c */
	aos_otab.laserDraw[ID] = laser_draw;       /* 0x881cec */
	aos_otab.laserOnHit[ID] = laser_on_hit;    /* 0x881d3c */
	aos_otab.enemySpawn[ID] = enemy_spawn;     /* 0x88156c */
	aos_otab.enemyUpdate[ID] = enemy_update;   /* 0x88160c */
	aos_otab.enemyDraw[ID] = enemy_draw;       /* 0x8815bc */
	aos_otab.enemyOnKilled[ID] = enemy_on_killed; /* 0x88165c */
	aos_otab.effectSpawn[ID] = effect_spawn;   /* 0x8816ac */
	aos_otab.effectUpdate[ID] = effect_update; /* 0x8816fc */
	aos_otab.effectDraw[ID] = effect_draw;     /* 0x88174c */
}
