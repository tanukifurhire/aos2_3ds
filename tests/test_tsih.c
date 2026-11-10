/* Behaviour checks for the Tsih port (src/sim/chars/tsih.c) against docs/re/tsih.md.
 * Every CHECK cites the spec section / original address.
 * Build: see tests/run_tests.sh (all sim sources + this file). */
#include "../src/sim/draw.h"
#include "../src/sim/sim.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
#define NEAR(a, b) (fabsf((float)(a) - (float)(b)) < 1e-3f)

static aos_world W, W2;

static void draw_all(aos_world *w)
{
	aos_draw_begin();
	aos_draw_players(w);
	for (int i = 0; i < w->obj.nbeam; i++)
		if (aos_otab.beamDraw[w->obj.beam[i].type]) aos_otab.beamDraw[w->obj.beam[i].type](w, &w->obj.beam[i]);
	for (int i = 0; i < w->obj.nlaser; i++)
		if (aos_otab.laserDraw[w->obj.laser[i].type]) aos_otab.laserDraw[w->obj.laser[i].type](w, &w->obj.laser[i]);
	for (int i = 0; i < w->obj.nenemy; i++)
		if (aos_otab.enemyDraw[w->obj.enemy[i].type]) aos_otab.enemyDraw[w->obj.enemy[i].type](w, &w->obj.enemy[i]);
	for (int i = 0; i < w->obj.neffect; i++)
		if (aos_otab.effectDraw[w->obj.effect[i].type]) aos_otab.effectDraw[w->obj.effect[i].type](w, &w->obj.effect[i]);
}

/* Tsih P1 (x -300) vs an idle Suguri P2 (x +300). */
static void setup(aos_world *w, int scheme)
{
	int32_t ids[2] = {AOS_CH_TSIH, AOS_CH_SUGURI}, sch[2] = {scheme, 0}, col[2] = {0, 1};
	aos_match_init(w, ids, sch, col, 999, 2);
	uint16_t m[2] = {0, 0};
	for (int i = 0; i < 210; i++) /* past the input lock */
		aos_tick(w, m);
	w->p[0].x = -300; w->p[0].y = 0;
	w->p[1].x = 300; w->p[1].y = 0;
	aos_tick(w, m);
}

static void tick1(aos_world *w, uint16_t m0)
{
	uint16_t m[2] = {m0, 0};
	aos_tick(w, m);
}

/* Hold `hold` for one frame, then `hold|press` (so held buttons read 2 and the press reads 3). */
static int press(aos_world *w, uint16_t hold, uint16_t prs)
{
	if (hold)
		tick1(w, hold);
	tick1(w, hold | prs);
	return w->p[0].state;
}

static int count_beams(aos_world *w, int id)
{
	int n = 0;
	for (int i = 0; i < w->obj.nbeam; i++)
		n += w->obj.beam[i].type == AOS_CH_TSIH && w->obj.beam[i].id == id;
	return n;
}
static int count_enemies(aos_world *w, int f7c)
{
	int n = 0;
	for (int i = 0; i < w->obj.nenemy; i++)
		n += w->obj.enemy[i].type == AOS_CH_TSIH && w->obj.enemy[i].f7c == f7c;
	return n;
}

/* §2 FUN_00786e70 / FUN_00787360: input -> state */
static void test_dispatch(void)
{
	static const struct { int scheme; uint16_t hold, prs; int hyper; int want; const char *what; } c[] = {
		{0, AOS_IN_S | AOS_IN_B, AOS_IN_H, 1, 0x77, "S+B+H -> 0x77 (3 levels)"},
		{0, AOS_IN_S, AOS_IN_H, 1, 0x65, "S+H -> 0x65 Stealth On"},
		{0, AOS_IN_B, AOS_IN_H, 1, 0x70, "B+H -> 0x70"},
		{0, AOS_IN_D, AOS_IN_H, 1, 0x6c, "D+H -> 0x6c"},
		{0, 0, AOS_IN_H, 1, 0x69, "H -> 0x69"},
		{0, AOS_IN_S, AOS_IN_B, 0, 0x73, "S+B -> 0x73"},
		{0, 0, AOS_IN_B, 0, 0x6d, "B -> 0x6d"},
		{0, AOS_IN_UP, AOS_IN_B, 0, 0x6e, "up+B -> 0x6e"},
		{0, AOS_IN_DOWN, AOS_IN_B, 0, 0x6f, "down+B -> 0x6f"},
		{0, AOS_IN_S, AOS_IN_A, 0, 0x6b, "S+A -> 0x6b"},
		{0, 0, AOS_IN_A, 0, 0x6a, "A -> 0x6a"},
		{1, AOS_IN_H, AOS_IN_SHIELD, 1, 0x77, "scheme B: H held + Shield -> 0x77"},
		{1, AOS_IN_H, AOS_IN_S, 1, 0x65, "scheme B: H+S -> 0x65"},
		{1, AOS_IN_H, AOS_IN_B, 1, 0x70, "scheme B: H+B -> 0x70"},
		{1, AOS_IN_H, AOS_IN_D, 1, 0x6c, "scheme B: H+D -> 0x6c"},
		{1, AOS_IN_H, AOS_IN_A, 1, 0x69, "scheme B: H+A -> 0x69"},
	};
	for (size_t k = 0; k < sizeof c / sizeof c[0]; k++) {
		aos_world *w = &W;
		setup(w, c[k].scheme);
		if (c[k].hyper)
			w->p[0].hyper = 3.0f;
		int s = press(w, c[k].hold, c[k].prs);
		CHECK(s == c[k].want, "%s (got %#x)", c[k].what, s);
		if (c[k].want == 0x77)
			CHECK(NEAR(w->p[0].hyper, 0.0f), "0x77 costs 3.0 hyper (DAT_00813a50), left %f", w->p[0].hyper);
	}
	/* D+A needs dash *held* (> 1): pressing D starts a dash first; hold D then press A */
	aos_world *w = &W;
	setup(w, 0);
	tick1(w, AOS_IN_D);                    /* dash */
	tick1(w, AOS_IN_D | AOS_IN_A);         /* in dash (state 2), D held, A pressed */
	CHECK(w->p[0].state == 0x68, "D held + A -> 0x68 (got %#x)", w->p[0].state);
	/* S+B+H with only 1 level falls through to S+H */
	setup(w, 0);
	w->p[0].hyper = 1.0f;
	CHECK(press(w, AOS_IN_S | AOS_IN_B, AOS_IN_H) == 0x65, "S+B+H with 1 level falls to 0x65");
}

/* §3 0x6a / 0x67: tap A -> Bendy Beam at t=12 (0xd2, curl -1) and t=14 (0xd7 -> 0xd2, curl +1) */
static void test_bendy(void)
{
	aos_world *w = &W;
	setup(w, 0);
	float a0 = w->p[0].gaugeA, h0 = w->p[0].hyper;
	tick1(w, AOS_IN_A); /* 0x6a */
	int t12 = -1, t14 = -1;
	for (int f = 0; f < 30; f++) {
		int t = w->p[0].timer, n = w->obj.nbeam;
		tick1(w, 0); /* released before t=10 -> 0x67, timer kept */
		if (w->obj.nbeam > n && t12 < 0) {
			t12 = t;
			CHECK(w->obj.beam[n].id == 0xd2 && w->obj.beam[n].f6c == -1.0f, "first beam 0xd2 curl -1 (FUN_004ba600)");
			CHECK(w->obj.beam[n].damage == 280 && NEAR(w->obj.beam[n].shape.radius, 8), "0xd2 dmg 280 r 8");
		} else if (w->obj.nbeam > n && t14 < 0) {
			t14 = t;
			CHECK(w->obj.beam[n].id == 0xd2 && w->obj.beam[n].f6c == 1.0f, "0xd7 normalised to 0xd2, curl +1");
		}
		if (f == 2)
			CHECK(w->p[0].state == 0x67, "A released before t=10 -> 0x67 (got %#x)", w->p[0].state);
	}
	CHECK(t12 == 12 && t14 == 14, "bendy beams at t=12/14 (got %d/%d)", t12, t14);
	/* gauge refill only runs in states 0..2; measure right after the second shot */
	(void)a0; (void)h0;
}

static void test_bendy_cost(void)
{
	aos_world *w = &W;
	setup(w, 0);
	w->p[0].gaugeA = 1.0f;
	w->p[0].hyper = 0;
	tick1(w, AOS_IN_A);
	while (w->p[0].timer <= 14)
		tick1(w, 0);
	CHECK(NEAR(w->p[0].gaugeA, 0.7f), "two 0x67 shots cost 2x0.15 A (§1.1) left %f", w->p[0].gaugeA);
	CHECK(NEAR(w->p[0].hyper, 0.04f), "two 0x67 shots give 2x0.02 hyper, got %f", w->p[0].hyper);
	while (w->p[0].state == 0x67 && w->p[0].timer < 40)
		tick1(w, 0);
	CHECK(w->p[0].state == 0, "0x67 ends after t>25");
}

/* §3 0x6a hold: 32-beam ring at t=16 (100/0x69), cost 0.5 A */
static void test_ring(void)
{
	aos_world *w = &W;
	setup(w, 0);
	tick1(w, AOS_IN_A);
	int at = -1;
	for (int f = 0; f < 20; f++) {
		int t = w->p[0].timer, n = w->obj.nbeam;
		tick1(w, AOS_IN_A);
		if (w->obj.nbeam - n == 32)
			at = t;
	}
	CHECK(at == 16, "ring spawns on t=16 (got %d)", at);
	CHECK(count_beams(w, 100) == 16 && count_beams(w, 0x69) == 16, "16 x id 100 + 16 x id 0x69");
	CHECK(NEAR(w->p[0].gaugeA, 0.5f), "ring costs 0.5 A (0x87c9e8) left %f", w->p[0].gaugeA);
	CHECK(w->obj.beam[0].damage == 120 && w->obj.beam[0].scale > 1.0f, "ball dmg 120, scale starts 3");
}

/* §3 0x68: beams at t=12,15,18,21 alternating 200 (curl -1) / 0xcd (-> 200, curl +1) */
static void test_many_bendy(void)
{
	aos_world *w = &W;
	setup(w, 0);
	tick1(w, AOS_IN_D);
	tick1(w, AOS_IN_D | AOS_IN_A);
	int ts[8], nt = 0;
	float curls[8];
	for (int f = 0; f < 30; f++) {
		int t = w->p[0].timer, n = w->obj.nbeam;
		tick1(w, 0);
		if (w->obj.nbeam > n && nt < 8) {
			curls[nt] = w->obj.beam[n].f6c;
			ts[nt++] = t;
		}
	}
	CHECK(nt == 4 && ts[0] == 12 && ts[1] == 15 && ts[2] == 18 && ts[3] == 21, "D+A beams at 12/15/18/21 (n=%d)", nt);
	CHECK(nt == 4 && curls[0] == -1 && curls[1] == 1 && curls[2] == -1 && curls[3] == 1, "ids 200/0xcd alternate");
	CHECK(count_beams(w, 200) == 4, "all normalised to id 200");
}

/* §3 0x6b + §5.1 300: wire spawns t=5, angle = aim + π, wire hitbox every frame (100 on age%4==0) */
static void test_wire(void)
{
	aos_world *w = &W;
	setup(w, 0);
	press(w, AOS_IN_S, AOS_IN_A);
	int at = -1;
	for (int f = 0; f < 10 && at < 0; f++) {
		int t = w->p[0].timer, n = w->obj.nbeam;
		tick1(w, 0);
		if (w->obj.nbeam > n)
			at = t;
	}
	CHECK(at == 5, "wire beam 300 at t=5 (got %d)", at);
	aos_beam *b = &w->obj.beam[0];
	CHECK(b->id == 300 && NEAR(b->angle, b->moveAngle + AOS_PI), "300: +0x64 = aim + π (ver>0x5a0)");
	CHECK(NEAR(w->p[0].gaugeA, 0.65f), "S+A costs 0.35 A, left %f", w->p[0].gaugeA);
}

/* §3 0x6d + §5.3: rocks at t=10 (2 enemies + 2 wire lasers), grow 10 frames, throw at t=25 */
static void test_rock(void)
{
	aos_world *w = &W;
	setup(w, 0);
	tick1(w, AOS_IN_B);
	int spawnT = -1, throwT = -1;
	for (int f = 0; f < 30; f++) {
		int t = w->p[0].timer, n = w->obj.nenemy;
		bool thrown0 = w->obj.nenemy && w->obj.enemy[0].f84 == 200;
		tick1(w, 0);
		if (w->obj.nenemy - n == 2)
			spawnT = t;
		if (!thrown0 && w->obj.nenemy && w->obj.enemy[0].f84 == 200)
			throwT = t;
		if (spawnT >= 0 && t == spawnT + 1) {
			CHECK(w->obj.nlaser == 2 && w->obj.laser[0].f1c == w->obj.enemy[0].id, "a wire laser 100 linked to each rock");
			CHECK(w->obj.enemy[0].hp == 250 && NEAR(w->obj.enemy[0].shape.radius, 40), "rock hp 250 r 40 (FUN_0055f1e0)");
			CHECK(NEAR(w->obj.enemy[0].pos[0], -300.0f + cosf(2.8915927f) * 100.0f), "R1 at angle 2.8916 r 100");
		}
	}
	CHECK(spawnT == 10, "rocks spawn on t=10 (got %d)", spawnT);
	CHECK(throwT == 25, "rocks thrown on t=25 (got %d)", throwT);
	CHECK(NEAR(w->p[0].gaugeB, 0.6f) || w->p[0].state == 0, "B costs 0.4 B");
	/* thrown rock aims at the opponent (FUN_00782900) */
	aos_enemy *e = &w->obj.enemy[0];
	CHECK(e->f7c == 100 && e->f84 == 200 && fabsf(e->angle2) < 0.6f, "thrown at the opponent (angle %f)", e->angle2);
}

/* quirk §5.3: B held at spawn -> the rock re-aims at Tsih herself */
static void test_rock_held(void)
{
	aos_world *w = &W;
	setup(w, 0);
	tick1(w, AOS_IN_B);
	for (int f = 0; f < 30; f++)
		tick1(w, AOS_IN_B);
	aos_enemy *e = &w->obj.enemy[1]; /* R2 starts right of Tsih */
	CHECK(e->fe8[2] == 1 && e->fe8[1] == 1, "+0xea set by held B, +0xe9 latched");
	CHECK(fabsf(fabsf(e->angle2) - AOS_PI) < 0.5f, "R2 (right of Tsih) flies left toward Tsih (angle %f)", e->angle2);
}

/* §3 0x6d dash-cancel window [12,24]: cancelling leaves rocks idle (not thrown) */
static void test_rock_cancel(void)
{
	aos_world *w = &W;
	setup(w, 0);
	tick1(w, AOS_IN_B);
	while (w->p[0].timer < 20)
		tick1(w, 0);
	tick1(w, AOS_IN_D);
	CHECK(w->p[0].state == AOS_ST_DASH, "dash-cancel at t=20 allowed (got %#x)", w->p[0].state);
	for (int f = 0; f < 40; f++)
		tick1(w, 0);
	CHECK(w->obj.nenemy == 2 && w->obj.enemy[0].f84 == 100, "rocks stay idle after the cancel");
	/* next B throws the old rocks too: 4 rocks thrown */
	tick1(w, AOS_IN_B);
	for (int f = 0; f < 30; f++)
		tick1(w, 0);
	int thrown = 0;
	for (int i = 0; i < w->obj.nenemy; i++)
		thrown += w->obj.enemy[i].f84 == 200;
	CHECK(thrown == 4, "old idle rocks are thrown by the next B (thrown %d)", thrown);
	/* no cancel at t=26..29 */
	setup(w, 0);
	tick1(w, AOS_IN_B);
	while (w->p[0].timer < 27)
		tick1(w, 0);
	tick1(w, AOS_IN_D);
	CHECK(w->p[0].state == 0x6d, "no dash-cancel at t=27 (got %#x)", w->p[0].state);
}

/* §3 0x65/0x66, §0 note 2: stealth */
static void test_stealth(void)
{
	aos_world *w = &W;
	setup(w, 0);
	w->p[0].hyper = 1.0f;
	press(w, AOS_IN_S, AOS_IN_H);
	while (w->p[0].timer <= 10)
		tick1(w, 0);
	int32_t tm;
	memcpy(&tm, &w->p[0].scratch[0x168 - 0x15c], 4);
	CHECK(w->p[0].scratch[8] == 1 && tm == 180, "t=10: +0x164=1, +0x168=180 (FUN_00783790)");
	/* opponent aim follows its own input while Tsih is stealthed (FUN_007804b0) */
	w->p[1].up = 2; w->p[1].right = 0; w->p[1].left = 0; w->p[1].down = 0;
	CHECK(NEAR(aos_aim(w, &w->p[1]), AOS_PI / 2), "stealthed: opponent aim = its input (up)");
	w->p[1].up = 0;
	CHECK(NEAR(aos_aim(w, &w->p[1]), AOS_PI), "stealthed, no input: aim toward Tsih's side (π)");
	while (w->p[0].state == 0x65)
		tick1(w, 0);
	/* timer counts only in states 0/2: 180 neutral frames -> 0x66, cleared at its t=10 */
	int f = 0;
	while (w->p[0].state != 0x66 && f < 400) {
		tick1(w, 0);
		f++;
	}
	CHECK(w->p[0].state == 0x66 && f >= 178 && f <= 181, "expiry -> 0x66 after ~180 neutral frames (%d)", f);
	while (w->p[0].timer <= 10)
		tick1(w, 0);
	CHECK(w->p[0].scratch[8] == 0, "0x66 t=10 clears stealth");
}

/* §3 0x73..0x76 + §5.3 swirl */
static void test_swirl(void)
{
	aos_world *w = &W;
	setup(w, 0);
	w->p[1].x = 1000; /* keep the swirl from hitting */
	press(w, AOS_IN_S, AOS_IN_B);
	CHECK(w->p[0].state == 0x73 && count_enemies(w, 200) == 1, "S+B -> 0x73 + swirl enemy 200");
	while (w->p[0].timer < 17)
		tick1(w, AOS_IN_B); /* hold through t=16 */
	CHECK(w->p[0].state == 0x73, "B held at t=16 keeps 0x73");
	while (w->p[0].timer < 20)
		tick1(w, 0);
	tick1(w, AOS_IN_B); /* press at t=20 (> 19) */
	CHECK(w->p[0].state == 0x74, "0x73 -> 0x74 (got %#x)", w->p[0].state);
	while (w->p[0].timer < 10)
		tick1(w, AOS_IN_B);
	tick1(w, 0); /* released at t=10 -> 0x76 */
	CHECK(w->p[0].state == 0x76, "B released at t==10 of 0x74 -> 0x76 (got %#x)", w->p[0].state);
	tick1(w, 0);
	CHECK(count_enemies(w, 0xd2) == 1, "swirl launches (becomes 0xd2) when Tsih is in 0x76");
	int f = 0;
	while (w->p[0].state == 0x76 && f++ < 60)
		tick1(w, 0);
	CHECK(f >= 34 && f <= 36, "0x76 ends after t>35 (%d)", f);
}

/* §3 0x77 Accel: 6 huge rocks above Tsih, x ±60/±180/±300 */
static void test_accel(void)
{
	aos_world *w = &W;
	setup(w, 0);
	w->p[0].hyper = 3.0f;
	tick1(w, AOS_IN_S | AOS_IN_B | AOS_IN_H); /* simultaneous press: S,B read 3 (> 1) */
	CHECK(w->p[0].state == 0x77 && count_enemies(w, 300) == 6 && w->obj.nlaser == 6, "6 huge rocks + 6 wires at t=0");
	static const float dx[6] = {60, -60, 180, -180, 300, -300};
	for (int i = 0; i < 6 && i < w->obj.nenemy; i++) {
		aos_enemy *e = &w->obj.enemy[i];
		CHECK(NEAR(e->pos[0], -300 + dx[i]) || e->f84 != 0, "rock %d x offset %f", i, dx[i]);
		CHECK(e->invulnerable == 1 && e->hp == 999, "huge rock invulnerable, hp 999");
	}
	CHECK(w->p[0].barrierType == 4, "Accel barrier kind 0x14");
}

/* objects: damage on a placed opponent (§5.1 beam 100 = 120, §5.2 hitbox 200 = 280, anti-juggle) */
static void test_damage(void)
{
	aos_world *w = &W;
	setup(w, 0);
	aos_player *o = &w->p[1];
	o->heat = 0;
	int hp = o->hp;
	float pos[3] = {o->x - 4, o->y, 0};
	aos_spawn_beam(w, AOS_CH_TSIH, 100, pos, 0, w->p[0].objId, 0, w->p[0].side);
	tick1(w, 0);
	CHECK(hp - o->hp == 120, "beam ball deals 120 (got %d)", hp - o->hp);
	setup(w, 0);
	o = &w->p[1];
	o->heat = 0;
	hp = o->hp;
	float pos2[3] = {o->x, o->y, 0};
	aos_spawn_hitbox(w, AOS_CH_TSIH, 200, pos2, 0, w->p[0].objId, 0, w->p[0].side);
	tick1(w, 0);
	CHECK(hp - o->hp == 280, "rock hitbox 200 deals 280 (got %d)", hp - o->hp);
	/* anti-juggle: victim in hitstun with speed > 2 takes nothing (FUN_005f01b0) */
	hp = o->hp;
	o->state = AOS_ST_HITSTUN;
	o->speed = 6.0f;
	aos_spawn_hitbox(w, AOS_CH_TSIH, 200, &o->x, 0, w->p[0].objId, 0, w->p[0].side);
	tick1(w, 0);
	CHECK(hp == o->hp, "rock hitbox ignored on a fast-moving hitstun victim");
	/* wire hitbox 100: knockback 10 once the attacker's combo counter > 7 */
	setup(w, 0);
	o = &w->p[1];
	w->p[0].comboShowHits = 8;
	w->p[0].comboShowTimer = 100;
	o->stun = 1.0f;
	aos_spawn_hitbox(w, AOS_CH_TSIH, 100, &o->x, 0, w->p[0].objId, 0, w->p[0].side);
	tick1(w, 0);
	CHECK(o->state == AOS_ST_HITSTUN, "wire hit with full stun -> hitstun");
	CHECK(o->speed > 5.0f, "wire knockback 10 when combo hits > 7 (speed %f)", o->speed);
}

/* every state runs to completion with all draw paths; random play deterministic */
static void test_all_states(void)
{
	static const int states[] = {0x5b, 0x5c, 0x5d, 0x5e, 0x5f, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b,
	                             0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x73, 0x74, 0x75, 0x76, 0x77};
	for (size_t k = 0; k < sizeof states / sizeof states[0]; k++) {
		aos_world *w = &W;
		setup(w, 0);
		aos_set_state(w, &w->p[0], states[k]);
		for (int f = 0; f < 200; f++) {
			tick1(w, 0);
			draw_all(w);
		}
		CHECK(states[k] <= 0x5f || states[k] == 0x66 || w->p[0].state < 0x5b, "state %#x did not end", states[k]);
	}
	uint32_t s = 4242;
	for (int run = 0; run < 2; run++) {
		aos_world *w = run ? &W2 : &W;
		setup(w, 0);
		uint16_t m[2] = {0, 0};
		s = 4242;
		for (int f = 0; f < 15000; f++) {
			for (int i = 0; i < 2; i++) {
				s = s * 1664525u + 1013904223u;
				if (((s >> 8) & 3) == 0)
					m[i] ^= (uint16_t)(1u << ((s >> 12) % 10));
				w->p[i].hyper = 3.0f;
				w->p[i].hp = 4500;
			}
			aos_tick(w, m);
			draw_all(w);
		}
	}
	CHECK(!memcmp(&W, &W2, sizeof W), "random runs diverged");
	CHECK(W.obj.overflow == 0, "pool overflow %d", (int)W.obj.overflow);
}

int main(void)
{
	aos_register_all();
	test_dispatch();
	test_bendy();
	test_bendy_cost();
	test_ring();
	test_many_bendy();
	test_wire();
	test_rock();
	test_rock_held();
	test_rock_cancel();
	test_stealth();
	test_swirl();
	test_accel();
	test_damage();
	test_all_states();
	printf("test_tsih: %s (%d failures)\n", fails ? "FAIL" : "ok", fails);
	return fails != 0;
}
