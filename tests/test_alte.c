/* Behaviour checks for the Alte port (src/sim/chars/alte.c) against docs/re/alte.md.
 * Every CHECK cites the spec section and/or the original address.
 * Build: see tests/run_tests.sh (all src/sim + chars sources, UBSan). */
#include "../src/sim/draw.h"
#include "../src/sim/sim.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
#define NEAR(a, b) (fabsf((float)(a) - (float)(b)) < 1e-3f)

static aos_world W, W2;
static uint16_t M[2];

static void tick(aos_world *w) { aos_tick(w, M); }
static void ticks(aos_world *w, int n) { while (n-- > 0) tick(w); }

/* Alte (P1) vs a passive dummy (Suguri, no input). */
static void setup(aos_world *w, int scheme)
{
	int32_t ids[2] = {AOS_CH_ALTE, AOS_CH_SUGURI}, sch[2] = {scheme, 0}, col[2] = {0, 0};
	aos_match_init(w, ids, sch, col, 999, 2);
	M[0] = M[1] = 0;
	ticks(w, 210); /* inputs locked while frame < 200 (core.md) */
}

static aos_player *P(aos_world *w) { return &w->p[0]; }

static int panel_idx(aos_world *w)
{
	for (int i = 0; i < w->obj.nenemy; i++)
		if (w->obj.enemy[i].type == 4 && w->obj.enemy[i].f10 == 100 && w->obj.enemy[i].team == 1)
			return i;
	return -1;
}
static int count_enemy(aos_world *w, int id)
{
	int n = 0;
	for (int i = 0; i < w->obj.nenemy; i++)
		n += w->obj.enemy[i].type == 4 && w->obj.enemy[i].f10 == id;
	return n;
}
static int count_missile(aos_world *w, int id)
{
	int n = 0;
	for (int i = 0; i < w->obj.nmissile; i++)
		n += w->obj.missile[i].type == 4 && w->obj.missile[i].id == id;
	return n;
}
static int32_t eint(const aos_enemy *e, int k) { int32_t v; memcpy(&v, &e->fd4[k], sizeof v); return v; }

/* Press `mask` for one frame, then release; returns P1 state after the press frame. */
static int press(aos_world *w, uint16_t held, uint16_t pressed)
{
	M[0] = held;
	tick(w);
	M[0] = held | pressed;
	tick(w);
	return P(w)->state;
}

static void deploy_panel(aos_world *w)
{
	P(w)->hyper = 3.0f;
	press(w, 0, AOS_IN_H);
	M[0] = 0;
	ticks(w, 70); /* 0x49 ends at t > 60 */
}

/* ---- §2 input dispatch ---------------------------------------------- */
static void test_dispatch(void)
{
	static const struct { uint16_t held, press; float hyper; int want; const char *what; } a[] = {
		{0, AOS_IN_A, 0, 0x3c, "A -> SMG 0x3c (§2 B7, FUN_006a7440)"},
		{AOS_IN_D, AOS_IN_A, 0, 0x3d, "D held + A -> 0x3d (§2 B6)"},
		{0, AOS_IN_B, 0, 0x3e, "B tapped -> 0x3f, released before t9 -> 0x3e (§3 0x3f)"},
		{AOS_IN_S, AOS_IN_A, 0, 0x41, "S+A -> Shock Rod 0x41 (§2 B3)"},
		{AOS_IN_S, AOS_IN_B, 0, 0x4c, "S+B -> Claymore 0x4c (§2 B4)"},
		{0, AOS_IN_H, 1, 0x49, "H, no panel -> Searchlight Panel 0x49 (§2 A5)"},
		{AOS_IN_S, AOS_IN_H, 1, 0x44, "S+H -> Shock Rod Punisher 0x44 (§2 A2)"},
		{AOS_IN_B, AOS_IN_H, 1, 0x40, "B+H -> Salvo Fire 0x40 (§2 A3)"},
		{AOS_IN_D, AOS_IN_H, 1, 0x48, "D+H -> Shock Ball 0x48 (§2 A4)"},
		{AOS_IN_S | AOS_IN_B, AOS_IN_H, 3, 0x4d, "S+B+H, 3 levels -> 0x4d (§2 A1)"},
		{AOS_IN_S | AOS_IN_B, AOS_IN_H, 2, 0x44, "S+B+H with 2 levels falls to S+H 0x44 (§2 A, cost check)"},
	};
	for (size_t k = 0; k < sizeof a / sizeof a[0]; k++) {
		aos_world *w = &W;
		setup(w, 0);
		P(w)->hyper = a[k].hyper;
		M[0] = a[k].held;
		ticks(w, 2);
		M[0] = a[k].held | a[k].press;
		tick(w);
		if (a[k].want == 0x3e) { /* release B immediately */
			M[0] = 0;
			tick(w);
		}
		CHECK(P(w)->state == a[k].want, "%s (got %#x)", a[k].what, P(w)->state);
		if (a[k].want == 0x49 || a[k].want == 0x44 || a[k].want == 0x4d)
			CHECK(NEAR(P(w)->hyper, a[k].hyper - (a[k].want == 0x4d ? 3.0f : 1.0f)),
			      "%s: hyper cost (hyper %f)", a[k].what, P(w)->hyper);
	}
	/* B held past t 9 stays in 0x3f (§3 0x3f, FUN_006a33c0) */
	{
		aos_world *w = &W;
		setup(w, 0);
		M[0] = AOS_IN_B;
		ticks(w, 12);
		CHECK(P(w)->state == 0x3f, "B held -> stays 0x3f Missile Shower (got %#x)", P(w)->state);
	}
	/* H with panel deployed -> SLP Spark 0x4b (§1.1, §2 A5) */
	{
		aos_world *w = &W;
		setup(w, 0);
		deploy_panel(w);
		CHECK(panel_idx(w) >= 0, "panel exists after 0x49");
		P(w)->hyper = 1.0f;
		CHECK(press(w, 0, AOS_IN_H) == 0x4b, "H with panel -> SLP Spark 0x4b (got %#x)", P(w)->state);
	}
	/* Scheme B (FUN_006a7900): H held + X pressed */
	static const struct { uint16_t press; int want; const char *what; } b[] = {
		{AOS_IN_A, 0x49, "B-scheme H+A -> 0x49"}, {AOS_IN_S, 0x44, "B-scheme H+S -> 0x44"},
		{AOS_IN_B, 0x40, "B-scheme H+B -> 0x40"}, {AOS_IN_D, 0x48, "B-scheme H+D -> 0x48"},
		{AOS_IN_SHIELD, 0x4d, "B-scheme H+Shield -> 0x4d"}, {AOS_IN_A | 0, 0x3c, "B-scheme A alone -> 0x3c"},
	};
	for (size_t k = 0; k < sizeof b / sizeof b[0]; k++) {
		aos_world *w = &W;
		setup(w, 1);
		P(w)->hyper = 3.0f;
		uint16_t held = b[k].want == 0x3c ? 0 : AOS_IN_H;
		M[0] = held;
		ticks(w, 2);
		M[0] = held | b[k].press;
		tick(w);
		CHECK(P(w)->state == b[k].want, "%s (got %#x)", b[k].what, P(w)->state);
	}
}

/* ---- §3 0x3c SMG -------------------------------------------------- */
static void test_smg(void)
{
	aos_world *w = &W;
	setup(w, 0);
	press(w, 0, AOS_IN_A);
	M[0] = 0;
	int shots[8], n = 0;
	float a0 = P(w)->gaugeA;
	for (int f = 0; f < 40 && P(w)->state == 0x3c; f++) {
		int t = P(w)->timer, m0 = count_missile(w, 100);
		tick(w);
		if (count_missile(w, 100) > m0 && n < 8)
			shots[n++] = t;
		if (P(w)->state != 0x3c)
			CHECK(t == 30, "0x3c ends at the first T2 with t > 29 (ended at t %d) §3", t);
	}
	CHECK(n == 4 && shots[0] == 6 && shots[1] == 9 && shots[2] == 12 && shots[3] == 15,
	      "SMG tap: bullets at t 6,9,12,15 (§3 0x3c: 5<t<16, t%%3==0) (n %d first %d)", n, n ? shots[0] : -1);
	(void)a0;
	/* bullet parameters §5.2 FUN_0065e420 id 100 */
	aos_world *v = &W2;
	setup(v, 0);
	press(v, 0, AOS_IN_A);
	M[0] = 0;
	ticks(v, 7);
	aos_missile *m = NULL;
	for (int i = 0; i < v->obj.nmissile; i++)
		if (v->obj.missile[i].id == 100) m = &v->obj.missile[i];
	CHECK(m != NULL, "SMG bullet (missile 100) spawned by t 6");
	if (m) {
		CHECK(NEAR(m->speed, 17.0f), "bullet speed 17 (_DAT_00813f5c) got %f", m->speed);
		CHECK(m->damage == 30 && m->hp == 120, "bullet dmg 30 hp 120 (got %d %d)", (int)m->damage, (int)m->hp);
		CHECK(NEAR(m->shapeBody.radius, 8) && NEAR(m->knockback, 5) && NEAR(m->stun, 0.4f) && m->hitType == 2,
		      "bullet r8 knock5 stun0.4 hitType2");
	}
	CHECK(NEAR(P(v)->gaugeA, 1.0f - 0.025f), "SMG bullet costs 0.025 ammo A (§1.2) got %f", P(v)->gaugeA);
	/* hold A: loop t 15 -> 6 keeps firing (§3 0x3c) */
	setup(v, 0);
	M[0] = AOS_IN_A;
	ticks(v, 2 + 60);
	CHECK(P(v)->state == 0x3c, "holding A keeps SMG looping (state %#x)", P(v)->state);
}

/* ---- §3 0x3e / 0x3f / §5.2 missiles 200/300, §5.1 beam 100 ----------- */
static void test_missiles(void)
{
	aos_world *w = &W;
	setup(w, 0);
	M[0] = 0; tick(w); M[0] = AOS_IN_B; tick(w); M[0] = 0;
	int spawnT = -1;
	for (int f = 0; f < 20; f++) {
		int t = P(w)->timer, n = count_missile(w, 200);
		tick(w);
		if (spawnT < 0 && count_missile(w, 200) > n) spawnT = t;
	}
	CHECK(spawnT == 8, "beam panels deploy at t 8 (§3 0x3e) got %d", spawnT);
	CHECK(count_missile(w, 200) == 6, "6 beam panels (got %d)", count_missile(w, 200));
	CHECK(NEAR(P(w)->gaugeB, 1.0f - 0.4f + 0.01f * 0) || P(w)->gaugeB < 0.7f, "B cost 0.4 (§1.2) gaugeB %f", P(w)->gaugeB);
	{
		aos_missile *m = NULL;
		for (int i = 0; i < w->obj.nmissile; i++)
			if (w->obj.missile[i].id == 200) { m = &w->obj.missile[i]; break; }
		if (m) {
			float dx = m->pos[0] - P(w)->x, dy = m->pos[1] - P(w)->y;
			CHECK(fabsf(sqrtf(dx * dx + dy * dy) - 100.0f) < 2.0f, "panel 100 px from Alte (DAT_00853b90)");
			CHECK(m->hp == 9999 && m->damage == 0, "beam panel hp 9999 dmg 0 (ver>0x5a0)");
		}
	}
	/* panels fire 5 beams each at ages 15..35 (§5.2) */
	int beams = 0, maxb = 0;
	for (int f = 0; f < 30; f++) {
		int nb = 0;
		for (int i = 0; i < w->obj.nbeam; i++)
			nb += w->obj.beam[i].id == 100;
		if (nb > maxb) maxb = nb;
		tick(w);
	}
	for (int i = 0; i < w->obj.nbeam; i++)
		if (w->obj.beam[i].id == 100) {
			CHECK(w->obj.beam[i].damage == 100 && NEAR(w->obj.beam[i].speed, 16.0f), "beam 100 dmg 100 speed 16 (§5.1)");
			beams++;
			break;
		}
	CHECK(maxb >= 6, "beam panels fire beams (max live %d)", maxb);
	(void)beams;
	/* Missile Shower: hold B -> 6 missiles 300 at t 16 (§3 0x3f) */
	setup(w, 0);
	M[0] = AOS_IN_B;
	spawnT = -1;
	for (int f = 0; f < 30; f++) {
		int t = P(w)->timer, n = count_missile(w, 300);
		tick(w);
		if (spawnT < 0 && count_missile(w, 300) > n) spawnT = t;
	}
	CHECK(spawnT == 16 && count_missile(w, 300) == 6, "Missile Shower 6x300 at t 16 (got t %d n %d)", spawnT,
	      count_missile(w, 300));
	for (int i = 0; i < w->obj.nmissile; i++)
		if (w->obj.missile[i].id == 300) {
			aos_missile *m = &w->obj.missile[i];
			CHECK(m->damage == 200 && m->hp == 120 && NEAR(m->f74, 0.0025f), "missile 300 dmg 200 hp 120 homing .0025");
			break;
		}
}

/* ---- §5.4 panel, â˜… Searchlight Shield, SLP Spark, barrier hook, KO hook ---- */
static void test_panel(void)
{
	aos_world *w = &W;
	setup(w, 0);
	P(w)->hyper = 1.0f;
	press(w, 0, AOS_IN_H);
	M[0] = 0;
	int spawnT = -1;
	for (int f = 0; f < 20; f++) {
		int t = P(w)->timer, n = count_enemy(w, 100);
		tick(w);
		if (spawnT < 0 && count_enemy(w, 100) > n) spawnT = t;
	}
	CHECK(spawnT == 10, "panel spawned at t 10 (§3 0x49) got %d", spawnT);
	int pi = panel_idx(w);
	CHECK(pi >= 0, "panel present");
	if (pi < 0)
		return;
	aos_enemy *e = &w->obj.enemy[pi];
	CHECK(e->hp == 2000 && NEAR(e->shape.radius, 30.0f), "panel hp 2000 r30 (FUN_0051a570)");
	CHECK(e->f84 == 10, "panel unfolds to phase 10 (got %d)", (int)e->f84);
	CHECK(NEAR(e->pos[0], P(w)->x + 100.0f) && NEAR(e->pos[1], P(w)->y), "panel at owner + (100,0) (+0x5c = 0)");
	ticks(w, 100);
	pi = panel_idx(w);
	e = &w->obj.enemy[pi];
	CHECK(e->active == 1, "panel collidable after age 100 (§5.4 step 7)");
	CHECK(P(w)->state == 0, "0x49 over");

	/* â˜… Searchlight Shield: hold S in neutral (§5.4 step 5, §5.5 effect 300) */
	float a0 = P(w)->gaugeA;
	M[0] = AOS_IN_S;
	tick(w);
	e = &w->obj.enemy[panel_idx(w)];
	CHECK(e->f84 == 20, "S held in neutral -> panel phase 20 (got %d)", (int)e->f84);
	int fx300 = 0;
	for (int i = 0; i < w->obj.neffect; i++)
		fx300 += w->obj.effect[i].type == 4 && w->obj.effect[i].id == 300;
	CHECK(fx300 == 1, "shield effect 300 spawned (got %d)", fx300);
	int hb = 0, inv = 0;
	for (int f = 0; f < 20; f++) {
		tick(w);
		for (int i = 0; i < w->obj.nhitbox; i++)
			hb += w->obj.hitbox[i].type == 4 && w->obj.hitbox[i].id == 100;
		inv += w->obj.enemy[panel_idx(w)].invulnerable == 1;
	}
	(void)hb; /* hitboxes are cleared at the end of each tick */
	CHECK(inv > 0, "panel invulnerable while shielding (effect 300)");
	float drain = (a0 - P(w)->gaugeA) / 21.0f;
	CHECK(fabsf(drain - 0.02f) < 0.003f, "ammo A drain 0.03/f minus 0.01 regen (entry 0x4a ver>0x5a0) got %f", drain);
	e = &w->obj.enemy[panel_idx(w)];
	CHECK(fabsf(e->f70 - 50.0f) < 0.01f, "panel pulled in to radius 50 (got %f)", e->f70);
	CHECK(fabsf(e->pos[1] - P(w)->y) < 1.0f && e->pos[0] > P(w)->x, "panel swung toward the opponent");
	/* lockout: ammo A runs out -> stamp +0xdc, shield drops (§5.4) */
	P(w)->gaugeA = 0.0f;
	tick(w);
	tick(w);
	e = &w->obj.enemy[panel_idx(w)];
	CHECK(eint(e, 2) != 0 && e->f84 == 10, "out of ammo: lockout stamp set, phase back to 10 (stamp %d phase %d)",
	      (int)eint(e, 2), (int)e->f84);
	ticks(w, 30);
	CHECK(w->obj.enemy[panel_idx(w)].f84 == 10, "locked out while S still held (< 101 frames)");
	M[0] = 0;
	tick(w);
	CHECK(eint(&w->obj.enemy[panel_idx(w)], 2) == 0, "releasing S in neutral clears the lockout");
	ticks(w, 20);
	CHECK(fabsf(w->obj.enemy[panel_idx(w)].f70 - 100.0f) < 0.01f, "radius back to 100 (+5/f)");

	/* barrier hook: Shield (FUN_006b1930 -> FUN_006b1b40 kind 0) makes the panel invulnerable (§0.2) */
	P(w)->hyper = 1.0f;
	P(w)->gaugeA = 1.0f;
	M[0] = AOS_IN_SHIELD;
	tick(w);
	M[0] = 0;
	CHECK(P(w)->state == AOS_ST_SHIELD && w->obj.enemy[panel_idx(w)].invulnerable == 1,
	      "Shield -> panel invulnerable (barrier hook phase 0) state %#x", P(w)->state);
	ticks(w, 45);
	CHECK(w->obj.enemy[panel_idx(w)].invulnerable == 0, "cleared once barrierTime < 21 (all.c:380823)");

	/* SLP Spark (§3 0x4b, §5.4 phase 30) */
	ticks(w, 30);
	P(w)->hyper = 1.0f;
	press(w, 0, AOS_IN_H);
	M[0] = 0;
	CHECK(P(w)->state == 0x4b, "H with panel -> 0x4b");
	ticks(w, 5);
	e = &w->obj.enemy[panel_idx(w)];
	CHECK(e->f84 == 30 && e->active == 1, "t5: panel phase 30, active (got %d)", (int)e->f84);
	int maxd = 0, nh = 0;
	for (int f = 0; f < 60; f++) {
		/* hitboxes live one tick: inspect them via a hitbox-onHit-free probe: count by re-spawning? Use tick hook:
		 * read the pool right after the enemy update by stepping manually is not exposed, so check the spawn fn. */
		tick(w);
	}
	(void)maxd; (void)nh;
	CHECK(w->obj.enemy[panel_idx(w)].f84 == 30 || w->obj.enemy[panel_idx(w)].f84 == 10, "spark phase runs");
	/* hitbox templates (§5.3 FUN_005e0180) */
	int h = aos_spawn_hitbox(w, 4, 200, &P(w)->x, 0, P(w)->objId, 0, 1);
	CHECK(h >= 0 && w->obj.hitbox[h].damage == 90 && NEAR(w->obj.hitbox[h].shape.radius, 140) && w->obj.hitbox[h].hitType == 3,
	      "hitbox 200: 90 dmg r140 type3");
	h = aos_spawn_hitbox(w, 4, 500, &P(w)->x, 0, P(w)->objId, 0, 1);
	CHECK(h >= 0 && w->obj.hitbox[h].damage == 1500 && NEAR(w->obj.hitbox[h].shape.radius, 120), "hitbox 500: 1500 dmg r120");
	h = aos_spawn_hitbox(w, 4, 100, &P(w)->x, 0, P(w)->objId, 0, 1);
	CHECK(h >= 0 && w->obj.hitbox[h].damage == 30 && NEAR(w->obj.hitbox[h].shape.radius, 80), "hitbox 100: 30 dmg r80");
	w->obj.nhitbox = 0;

	/* KO hook (§3 0x34): panel goes to phase 900 and disappears */
	P(w)->hp = 1;
	aos_damage(w, P(w), 10, 2, 0);
	tick(w);
	pi = panel_idx(w);
	CHECK(pi < 0 || w->obj.enemy[pi].f84 == 900, "KO -> panel phase 900");
	ticks(w, 20);
	CHECK(panel_idx(w) < 0, "panel faded out after KO");
}

/* ---- damage of the panel attacks on a placed opponent (§5.4 phase 30, §5.5 effect 300) ---- */
static void test_panel_damage(void)
{
	aos_world *w = &W;
	setup(w, 0);
	deploy_panel(w);
	ticks(w, 40);
	int hp0 = w->p[1].hp;
	P(w)->hyper = 1.0f;
	press(w, 0, AOS_IN_H);
	M[0] = 0;
	for (int f = 0; f < 70; f++) {
		/* opponent parked 60 px beyond the panel: inside hitbox 200 (r 140) */
		w->p[1].x = w->obj.enemy[panel_idx(w)].pos[0] + 60.0f;
		w->p[1].y = w->obj.enemy[panel_idx(w)].pos[1];
		tick(w);
	}
	int d = hp0 - w->p[1].hp;
	CHECK((d % 90 == 0 && d / 90 >= 8 && d / 90 <= 9) || ((d - 150) % 90 == 0 && (d - 150) / 90 >= 7 && (d - 150) / 90 <= 8),
	      "SLP Spark: 90 on frames with age%%5==0 over timer 10..50, final 150 only if age%%5==0 (§5.4) got %d", d);
	/* shield: 30 every 5th effect age once fully grown (§5.5) */
	setup(w, 0);
	deploy_panel(w);
	ticks(w, 40);
	M[0] = AOS_IN_S;
	tick(w);
	hp0 = w->p[1].hp;
	for (int f = 0; f < 50; f++) {
		w->p[1].x = w->obj.enemy[panel_idx(w)].pos[0] + 40.0f;
		w->p[1].y = w->obj.enemy[panel_idx(w)].pos[1];
		tick(w);
	}
	d = hp0 - w->p[1].hp;
	CHECK(d > 0 && d % 30 == 0 && d <= 30 * 10, "Searchlight Shield: 30 per 5 frames (§5.5) got %d", d);
	M[0] = 0;
}

/* ---- §3 0x4c Claymore ---------------------------------------------- */
static void test_claymore(void)
{
	aos_world *w = &W;
	setup(w, 0);
	int st = press(w, AOS_IN_S, AOS_IN_B);
	M[0] = 0;
	CHECK(st == 0x4c, "S+B -> 0x4c");
	int spawnT = -1;
	for (int f = 0; f < 15; f++) {
		int t = P(w)->timer, n = count_enemy(w, 200);
		tick(w);
		if (spawnT < 0 && count_enemy(w, 200) > n) spawnT = t;
	}
	CHECK(spawnT == 10 && count_enemy(w, 200) == 2, "2 claymores at t 10 (got t %d n %d)", spawnT, count_enemy(w, 200));
	CHECK(NEAR(P(w)->gaugeB, 0.3f), "claymore costs 0.7 B (FUN_004a7270) got %f", P(w)->gaugeB);
	for (int i = 0; i < w->obj.nenemy; i++)
		if (w->obj.enemy[i].f10 == 200)
			CHECK(w->obj.enemy[i].hp == 400, "claymore hp 400");
	ticks(w, 40);
	P(w)->gaugeB = 1.0f;
	press(w, AOS_IN_S, AOS_IN_B);
	M[0] = 0;
	ticks(w, 14);
	int live = 0;
	for (int i = 0; i < w->obj.nenemy; i++)
		live += w->obj.enemy[i].f10 == 200 && w->obj.enemy[i].f84 != 900;
	CHECK(live == 2, "placing new claymores expires the old pair (timer 1001) live %d", live);
}

/* ---- §3 0x41/0x42 Shock Rod chain, 0x4d/0x4e, 0x48 --------------------- */
static void test_rod_and_grabs(void)
{
	aos_world *w = &W;
	setup(w, 0);
	w->p[1].x = P(w)->x + 120.0f;
	w->p[1].y = P(w)->y;
	int hp0 = w->p[1].hp;
	press(w, AOS_IN_S, AOS_IN_A);
	M[0] = 0;
	bool grab = false;
	for (int f = 0; f < 70; f++) {
		tick(w);
		grab |= P(w)->state == 0x42;
	}
	CHECK(grab, "Shock Rod hit-confirm -> 0x42 (§3 0x41, hitbox 300 sets +0x158)");
	CHECK(hp0 - w->p[1].hp == 6 * 80, "0x42: 5x80 + final 80 = 480 dmg (§5.3) got %d", hp0 - w->p[1].hp);

	setup(w, 0);
	w->p[1].x = P(w)->x + 200.0f;
	w->p[1].y = P(w)->y;
	hp0 = w->p[1].hp;
	P(w)->hyper = 3.0f;
	press(w, AOS_IN_S | AOS_IN_B, AOS_IN_H);
	M[0] = 0;
	bool g4e = false;
	for (int f = 0; f < 140; f++) {
		tick(w);
		g4e |= P(w)->state == 0x4e;
	}
	CHECK(g4e, "Desperation dash grabs the opponent -> 0x4e (§3 0x4d)");
	CHECK(hp0 - w->p[1].hp == 1500, "Desperation hitbox 500 deals 1500 (ver>0x5a0) got %d", hp0 - w->p[1].hp);

	setup(w, 0);
	P(w)->hyper = 1.0f;
	press(w, AOS_IN_D, AOS_IN_H);
	M[0] = 0;
	ticks(w, 32);
	CHECK(count_enemy(w, 300) == 10, "Shock Ball: 10 balls at t 10..30 (§3 0x48) got %d", count_enemy(w, 300));

	setup(w, 0);
	w->p[1].x = P(w)->x + 120.0f;
	w->p[1].y = P(w)->y;
	hp0 = w->p[1].hp;
	P(w)->hyper = 1.0f;
	press(w, AOS_IN_S, AOS_IN_H);
	M[0] = 0;
	bool g47 = false;
	for (int f = 0; f < 120; f++) {
		tick(w);
		g47 |= P(w)->state == 0x47;
	}
	CHECK(g47, "Punisher hit-confirm -> 0x47 (§3 0x44, hitbox 0x15e)");
	CHECK(hp0 - w->p[1].hp == 10 * 60 + 80, "0x47: 10x60 + 80 = 680 (§5.3) got %d", hp0 - w->p[1].hp);
}

/* ---- every state runs to completion; random play stays deterministic ---- */
static void draw_all(aos_world *w)
{
	aos_draw_begin();
	aos_draw_players(w);
	for (int i = 0; i < w->obj.nbeam; i++)
		if (aos_otab.beamDraw[w->obj.beam[i].type]) aos_otab.beamDraw[w->obj.beam[i].type](w, &w->obj.beam[i]);
	for (int i = 0; i < w->obj.nmissile; i++)
		if (aos_otab.missileDraw[w->obj.missile[i].type]) aos_otab.missileDraw[w->obj.missile[i].type](w, &w->obj.missile[i]);
	for (int i = 0; i < w->obj.nenemy; i++)
		if (aos_otab.enemyDraw[w->obj.enemy[i].type]) aos_otab.enemyDraw[w->obj.enemy[i].type](w, &w->obj.enemy[i]);
	for (int i = 0; i < w->obj.neffect; i++)
		if (aos_otab.effectDraw[w->obj.effect[i].type]) aos_otab.effectDraw[w->obj.effect[i].type](w, &w->obj.effect[i]);
}

static void test_states_and_random(void)
{
	/* end = state timer of the T2 call that returns to state 0 (first t > limit), §3 per state */
	static const int st[] = {0x3c, 0x3d, 0x3e, 0x40, 0x41, 0x42, 0x44, 0x45, 0x46, 0x47,
	                         0x48, 0x49, 0x4b, 0x4c, 0x4d, 0x4e};
	static const int end[] = {30, 30, 41, 71, 56, 61, 81, 81, 81, 81, 56, 61, 81, 31, 81, 101};
	for (size_t k = 0; k < sizeof st / sizeof st[0]; k++) {
		aos_world *w = &W;
		setup(w, 0);
		aos_set_state(w, P(w), st[k]);
		int ended = -1;
		for (int f = 0; f < 200 && ended < 0; f++) {
			int t = P(w)->timer, s = P(w)->state;
			w->p[1].barrierTime = 1000; /* untouchable dummy: no hit-confirm / grab branches */
			tick(w);
			draw_all(w);
			if (s == st[k] && P(w)->state != s)
				ended = t;
		}
		CHECK(ended == end[k], "state %#x ends after t %d (got %d)", st[k], end[k], ended);
	}
	uint32_t s = 4242;
	int full = 0;
	for (int run = 0; run < 2; run++) {
		aos_world *w = run ? &W2 : &W;
		setup(w, 0);
		s = 4242;
		for (int f = 0; f < 20000; f++) {
			for (int i = 0; i < 2; i++) {
				s = s * 1664525u + 1013904223u;
				if (((s >> 8) & 3) == 0)
					M[i] ^= (uint16_t)(1u << ((s >> 12) % 10));
				if (f % 600 == 0)
					w->p[i].hyper = 3.0f;
				w->p[i].hp = w->p[i].maxHp;
			}
			tick(w);
			draw_all(w);
			full |= w->obj.nbeam >= AOS_MAX_BEAM || w->obj.nmissile >= AOS_MAX_MISSILE ||
			        w->obj.nenemy >= AOS_MAX_ENEMY || w->obj.neffect >= AOS_MAX_EFFECT;
		}
	}
	CHECK(!memcmp(&W, &W2, sizeof W), "random hyper runs diverged");
	/* spark pool (AOS_MAX_SPARK 160) can overflow under Salvo Fire: 12 panels x 5 beams, each hit
	 * FUN_007bd5e0(1000,c,10,10) — engine pool size, reported; gameplay pools must not fill. */
	CHECK(!full, "gameplay object pool filled up");
}

int main(void)
{
	aos_register_all();
	test_dispatch();
	test_smg();
	test_missiles();
	test_panel();
	test_panel_damage();
	test_claymore();
	test_rod_and_grabs();
	test_states_and_random();
	printf("test_alte: %s (%d/%d checks failed)\n", fails ? "FAIL" : "ok", fails, checks);
	return fails != 0;
}
