/* Behaviour checks for the Sham port (src/sim/chars/sham.c) against docs/re/sham.md.
 * Every CHECK cites the spec section and the original address it comes from.
 * Build: zig cc -std=gnu11 -Wall -Wextra -Werror -O1 -fsanitize=undefined src/sim/{,chars/}<all .c>
 *        tests/test_sham.c -lm -o build/tests/test_sham.exe */
#include "../src/sim/draw.h"
#include "../src/sim/sim.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static aos_world W, W2;
static const uint16_t NONE[2] = {0, 0};

static void setup(aos_world *w, int scheme, int opp)
{
	int32_t ids[2] = {AOS_CH_SHAM, opp}, sch[2] = {scheme, 0}, col[2] = {0, 1};
	aos_match_init(w, ids, sch, col, 999, 2);
	for (int i = 0; i < 210; i++) /* past the input lock (frame < 200, core.md §2.3) */
		aos_tick(w, NONE);
	w->p[0].x = -200, w->p[0].y = 0;
	w->p[1].x = 200, w->p[1].y = 0;
	w->p[0].gaugeA = w->p[0].gaugeB = 1.0f;
	w->p[0].hyper = 3.0f;
}

static void tick1(aos_world *w, uint16_t m0)
{
	uint16_t m[2] = {m0, 0};
	aos_tick(w, m);
}

static int count_enemy(aos_world *w, int32_t f10)
{
	int n = 0;
	for (int i = 0; i < w->obj.nenemy; i++)
		if (w->obj.enemy[i].type == AOS_CH_SHAM && w->obj.enemy[i].f10 == f10)
			n++;
	return n;
}

/* press a combination: hold `hold` for 3 frames (so it reads as held, value 2), then add `press` */
static void press(aos_world *w, uint16_t hold, uint16_t press)
{
	if (hold) {
		tick1(w, hold);
		tick1(w, hold);
	}
	tick1(w, hold | press);
}

/* §2 input dispatch, FUN_0072e300 (scheme A) / FUN_0072e890 (scheme B) */
static void test_dispatch(void)
{
	static const struct { int scheme; uint16_t hold, press; int32_t state; const char *what; } t[] = {
		{0, 0, AOS_IN_A, 0xb1, "A -> Sprite Call (§2 B7)"},
		{0, AOS_IN_D, AOS_IN_A, 0xb9, "D+A, no sprites -> Beam Ribbon (§2 B6)"},
		{0, AOS_IN_S, AOS_IN_A, 0xba, "S+A -> Resistant Sphere (§2 B4)"},
		{0, 0, AOS_IN_B, 0xbc, "B -> Mysterious Cube hold state (§2 B5)"},
		{0, AOS_IN_S, AOS_IN_B, 0xb6, "S+B, no dwarfs -> Dwarf Call (§2 B3)"},
		{0, 0, AOS_IN_H, 0xb4, "H -> Poly Sprite (§2 A5)"},
		{0, AOS_IN_D, AOS_IN_H, 0xb5, "D+H -> Delta Sprite (§2 A4)"},
		{0, AOS_IN_B, AOS_IN_H, 0xbd, "B+H -> Bounding Cube (§2 A3)"},
		{0, AOS_IN_S, AOS_IN_H, 0xb8, "S+H -> Dwarf Rush (§2 A2)"},
		{0, AOS_IN_S | AOS_IN_B, AOS_IN_H, 0xbe, "S+B+H -> Arachne Call (§2 A1)"},
		{1, AOS_IN_H, AOS_IN_A, 0xb4, "scheme B: H held + A -> Poly Sprite"},
		{1, AOS_IN_H, AOS_IN_D, 0xb5, "scheme B: H held + D -> Delta Sprite"},
		{1, AOS_IN_H, AOS_IN_B, 0xbd, "scheme B: H held + B -> Bounding Cube"},
		{1, AOS_IN_H, AOS_IN_S, 0xb8, "scheme B: H held + S -> Dwarf Rush"},
		{1, AOS_IN_H, AOS_IN_SHIELD, 0xbe, "scheme B: H held + Shield -> Arachne Call"},
		{1, 0, AOS_IN_A, 0xb1, "scheme B: A -> Sprite Call"},
	};
	for (size_t k = 0; k < sizeof t / sizeof t[0]; k++) {
		aos_world *w = &W;
		setup(w, t[k].scheme, AOS_CH_SUGURI);
		float h0 = w->p[0].hyper;
		press(w, t[k].hold, t[k].press);
		CHECK(w->p[0].state == t[k].state, "%s: got state %#x", t[k].what, (unsigned)w->p[0].state);
		if (t[k].state == 0xbe)
			CHECK(fabsf(h0 - w->p[0].hyper - 3.0f) < 1e-4f, "Arachne costs 3.0 hyper (DAT_00813a50), spent %f", (double)(h0 - w->p[0].hyper));
		else if (t[k].state >= 0xb4 && t[k].state != 0xb6 && t[k].state != 0xb9 && t[k].state != 0xba && t[k].state != 0xbc && t[k].state != 0xb1)
			CHECK(fabsf(h0 - w->p[0].hyper - 1.0f) < 1e-4f, "%s costs 1.0 hyper, spent %f", t[k].what, (double)(h0 - w->p[0].hyper));
	}
}

/* §3 0xb1 Sprite Call FUN_0072b740: spawn at t==9, A -0.2, hyper +0.02, hold loop t13->7, end t>30 */
static void test_sprite_call(void)
{
	aos_world *w = &W;
	setup(w, 0, AOS_CH_SUGURI);
	w->p[0].hyper = 0;
	tick1(w, AOS_IN_A);
	int spawnT = -1;
	float a0 = w->p[0].gaugeA;
	for (int f = 0; f < 12 && spawnT < 0; f++) {
		int t = w->p[0].timer, n = count_enemy(w, 100);
		tick1(w, 0);
		if (count_enemy(w, 100) > n)
			spawnT = t;
	}
	CHECK(spawnT == 9, "sprite spawn at t==9 (ver>0x5a0, FUN_0072b740), got %d", spawnT);
	CHECK(fabsf(a0 - w->p[0].gaugeA - 0.2f) < 1e-3f, "Sprite Call costs A 0.2 (DAT_00853b14) — paid %f", (double)(a0 - w->p[0].gaugeA));
	CHECK(fabsf(w->p[0].hyper - 0.02f) < 1e-4f, "hyper gain 0.02 (DAT_00813ed8), got %f", (double)w->p[0].hyper);
	int endT = -1;
	for (int f = 0; f < 40 && endT < 0; f++) {
		int t = w->p[0].timer;
		tick1(w, 0);
		if (w->p[0].state == 0)
			endT = t;
	}
	CHECK(endT == 31, "Sprite Call ends when t > 30 (FUN_0072b740), ended at t=%d", endT);

	/* hold A: one sprite every 6 frames (t==13 && A held -> t=7) */
	setup(w, 0, AOS_CH_SUGURI);
	for (int f = 0; f < 40; f++)
		tick1(w, AOS_IN_A);
	/* t=9, then every 6 frames (t 13 -> 7); the 6th would need 1.2 ammo A (no regen outside states 0-2) */
	CHECK(count_enemy(w, 100) == 5, "held A: 5 sprites (A 1.0 / 0.2 each), got %d", count_enemy(w, 100));

	/* sprite volley: 3 beams 100 at t2 == 1, middle one hits the opponent for 120 (§5.12, FUN_004b2ed0) */
	setup(w, 0, AOS_CH_SUGURI);
	w->p[1].x = 100;
	int hp0 = w->p[1].hp;
	tick1(w, AOS_IN_A);
	int beams = 0;
	for (int f = 0; f < 60; f++) {
		int nb = w->obj.nbeam;
		tick1(w, 0);
		if (w->obj.nbeam > nb)
			beams += w->obj.nbeam - nb;
	}
	CHECK(beams >= 3, "first volley spawns 3 beams (FUN_0053e810 case 100), saw %d", beams);
	CHECK(hp0 - w->p[1].hp == 120, "sprite beam deals 120 (beam 100 dmg 0x78), dealt %d", hp0 - w->p[1].hp);
}

/* §3 0xb2 Sprite Release: with a deployed sprite, D+A -> 0xb2; sprites become kind 0xbe homing (FUN_0072bf10) */
static void test_release(void)
{
	aos_world *w = &W;
	setup(w, 0, AOS_CH_SUGURI);
	tick1(w, AOS_IN_A);
	for (int f = 0; f < 40; f++)
		tick1(w, 0); /* sprite deployed (phase 10 after 10 frames of growth) */
	CHECK(count_enemy(w, 100) == 1, "one deployed sprite");
	press(w, AOS_IN_D, AOS_IN_A);
	CHECK(w->p[0].state == 0xb2, "D+A with a deployed sprite -> 0xb2 Sprite Release (§1.3), got %#x", (unsigned)w->p[0].state);
	for (int f = 0; f < 12; f++)
		tick1(w, 0);
	CHECK(count_enemy(w, 0xbe) == 1 && count_enemy(w, 100) == 0, "released sprite has kind 0xbe (FUN_0072bf10 t==10)");
	/* phase 0x14 orbits a point at opp + speed*30 (DAT_008140e8) with speed floored at 5 for
	 * ver>0x5a0, i.e. 150 px from a standing opponent, so it only explodes on the timeout:
	 * t2 starts at c*10 = 0 and the hitbox 0x96 fires once t2 > 249 (FUN_0053e810 case 100). */
	int life = -1;
	for (int f = 0; f < 270 && life < 0; f++) {
		tick1(w, 0);
		if (count_enemy(w, 0xbe) == 0)
			life = f;
	}
	CHECK(life == 247, "released sprite (t2 = 2 here) explodes when t2 reaches 250, i.e. 248 updates later (index 247), got %d", life);
}

/* §3 0xbc / 0xbb Mysterious Cube: hold -> 12 cubes at t==15, B -0.5; tap -> 7 cubes at t==12, B -0.3 */
static void test_cubes(void)
{
	aos_world *w = &W;
	setup(w, 0, AOS_CH_SUGURI);
	for (int f = 0; f < 16; f++)
		tick1(w, AOS_IN_B);
	CHECK(w->p[0].state == 0xbc, "held B stays in 0xbc");
	CHECK(count_enemy(w, 300) == 12, "hold: 12 cubes (k=-6..5, FUN_0072d490), got %d", count_enemy(w, 300));
	CHECK(fabsf(1.0f - w->p[0].gaugeB - 0.5f) < 1e-3f, "hold costs B 0.5, paid %f", (double)(1.0f - w->p[0].gaugeB));

	setup(w, 0, AOS_CH_SUGURI);
	tick1(w, AOS_IN_B);
	tick1(w, 0); /* B released (1) */
	tick1(w, 0); /* B up (0) -> 0xbb */
	CHECK(w->p[0].state == 0xbb, "tap B -> 0xbb (B < 1 before t 12), got %#x", (unsigned)w->p[0].state);
	for (int f = 0; f < 12; f++)
		tick1(w, 0);
	CHECK(count_enemy(w, 300) == 7, "tap: 7 cubes (k=-3..3, FUN_0072d210), got %d", count_enemy(w, 300));
	CHECK(fabsf(1.0f - w->p[0].gaugeB - 0.3f) < 1e-3f, "tap costs B 0.3, paid %f", (double)(1.0f - w->p[0].gaugeB));
	aos_enemy *c = NULL;
	for (int i = 0; i < w->obj.nenemy; i++)
		if (w->obj.enemy[i].f10 == 300)
			c = &w->obj.enemy[i];
	CHECK(c && c->solid == 1 && c->shape.halfW == 30.0f, "cube 300 is a solid 30x30 rect (§5.0)");
}

/* §3 0xb6/0xb7 dwarfs: two dwarfs at t 15/20 for free; second S+B -> 0xb7; tackle hits for 60 */
static void test_dwarfs(void)
{
	aos_world *w = &W;
	setup(w, 0, AOS_CH_SUGURI);
	float a0 = w->p[0].gaugeA, b0 = w->p[0].gaugeB;
	press(w, AOS_IN_S, AOS_IN_B);
	int t15 = -1, t20 = -1;
	for (int f = 0; f < 30; f++) {
		int t = w->p[0].timer, n = count_enemy(w, 200);
		tick1(w, AOS_IN_S);
		if (count_enemy(w, 200) == n + 1 && t15 < 0)
			t15 = t;
		else if (count_enemy(w, 200) == n + 1)
			t20 = t;
	}
	CHECK(t15 == 15 && t20 == 20, "dwarfs at t 15 and 20 (FUN_0072c8c0), got %d %d", t15, t20);
	CHECK(w->p[0].gaugeA >= a0 && w->p[0].gaugeB >= b0, "Dwarf Call is free (no FUN_007807f0 call)");
	for (int f = 0; f < 30; f++)
		tick1(w, 0);
	w->p[1].x = w->p[0].x + 250;
	w->p[1].y = w->p[0].y;
	int hp0 = w->p[1].hp;
	press(w, AOS_IN_S, AOS_IN_B);
	CHECK(w->p[0].state == 0xb7, "S+B with dwarfs -> 0xb7 Dwarf Tackle (FUN_0072a8a0), got %#x", (unsigned)w->p[0].state);
	int tackling = 0;
	for (int f = 0; f < 6; f++)
		tick1(w, 0);
	for (int i = 0; i < w->obj.nenemy; i++)
		if (w->obj.enemy[i].f10 == 200 && w->obj.enemy[i].f84 == 0x14)
			tackling++;
	CHECK(tackling == 2, "both dwarfs tackle (phase 0x14), got %d", tackling);
	for (int f = 0; f < 120; f++)
		tick1(w, 0);
	int d = hp0 - w->p[1].hp;
	CHECK(d > 0 && d % 60 == 0, "tackle hitbox 200 deals multiples of 60 (FUN_005e92d0), dealt %d", d);
}

/* §3 0xba Resistant Sphere: hp(duration) = (int)(A*100)+50 after the 0.5 cost, A -> 0; pushes the opponent */
static void test_sphere(void)
{
	aos_world *w = &W;
	setup(w, 0, AOS_CH_SUGURI);
	w->p[0].gaugeA = 0.9f;
	w->p[1].x = w->p[0].x + 100;
	press(w, AOS_IN_S, AOS_IN_A);
	float aUse = w->p[0].gaugeA; /* no regen in 0xba (core.md §6.9: states 0/1/2 only) */
	for (int f = 0; f < 11; f++)
		tick1(w, 0);
	aos_enemy *s = NULL;
	for (int i = 0; i < w->obj.nenemy; i++)
		if (w->obj.enemy[i].f10 == 400)
			s = &w->obj.enemy[i];
	CHECK(s != NULL, "sphere spawned at t==10");
	if (s)
		CHECK(s->hp == (int)((aUse - 0.5f) * 100.0f) + 50, "sphere duration = (int)(A*100)+50 (FUN_0072dd40), got %d", (int)s->hp);
	CHECK(w->p[0].gaugeA == 0.0f, "Resistant Sphere zeroes ammo A");
	int lasers = 0;
	for (int i = 0; i < w->obj.nlaser; i++)
		if (w->obj.laser[i].type == AOS_CH_SHAM)
			lasers++;
	CHECK(lasers == 2, "two orbit rings (laser 100 at +-pi/6), got %d", lasers);
	for (int f = 0; f < 20; f++)
		tick1(w, 0);
	float dx = w->p[1].x - w->p[0].x, dy = w->p[1].y - w->p[0].y;
	CHECK(sqrtf(dx * dx + dy * dy) >= 180.0f, "opponent pushed out of the sphere (r = 1.1*185), dist %f", (double)sqrtf(dx * dx + dy * dy));
}

/* §5.3 Delta Sprite: opponent inside the triangle gets the no-dash status (FUN_006b1a40(opp,3,200)) */
static void test_delta(void)
{
	aos_world *w = &W;
	setup(w, 0, AOS_CH_SUGURI);
	w->p[1].x = w->p[0].x + 20; /* very close: inside the corner triangles that start around Sham */
	press(w, AOS_IN_D, AOS_IN_H);
	CHECK(w->p[0].state == 0xb5, "D+H -> 0xb5");
	CHECK(w->p[0].barrierType == 3, "Delta Sprite starts the normal hyper barrier (FUN_006b1b40 kind 10)");
	int got = 0;
	for (int f = 0; f < 60 && !got; f++) {
		tick1(w, 0);
		got = w->p[1].statusOn[AOS_STATUS_NODASH];
	}
	CHECK(got, "no-dash status applied inside the Delta triangle (§5.3)");
	CHECK(count_enemy(w, 0xa0) + count_enemy(w, 0xa1) + count_enemy(w, 0xa2) == 9, "3 groups x 3 corners");
}

/* §3 end / dash-cancel frames per state (C, E table) */
static void test_end_frames(void)
{
	static const struct { int32_t s, C, E; } t[] = {
		{0xb2, 0x14, 0x1e}, {0xb4, 0x2d, 0x50}, {0xb5, 0x23, 0x32}, {0xb6, 0x19, 0x23}, {0xb7, 0x19, 0x23},
		{0xb8, 0x32, 0x50}, {0xb9, 0x19, 0x23}, {0xba, 0x19, 0x23}, {0xbb, 0x14, 0x1e}, {0xbd, 0x1e, 0x3c},
		{0xbe, 0x28, 100},
	};
	for (size_t k = 0; k < sizeof t / sizeof t[0]; k++) {
		aos_world *w = &W;
		setup(w, 0, AOS_CH_SUGURI);
		aos_set_state(w, &w->p[0], t[k].s);
		int endT = -1;
		for (int f = 0; f < 200 && endT < 0; f++) {
			int tt = w->p[0].timer;
			tick1(w, 0);
			if (w->p[0].state == 0)
				endT = tt;
		}
		CHECK(endT == t[k].E + 1, "state %#x ends at t > %d, got %d", (unsigned)t[k].s, t[k].E, endT);
		/* dash cancel: first frame it works is t == C */
		setup(w, 0, AOS_CH_SUGURI);
		aos_set_state(w, &w->p[0], t[k].s);
		while (w->p[0].timer < t[k].C - 1)
			tick1(w, 0);
		tick1(w, AOS_IN_D); /* Dash pressed while t == C-1: refused */
		CHECK(w->p[0].state == t[k].s, "state %#x: no dash cancel before t=%d", (unsigned)t[k].s, t[k].C);
		tick1(w, 0);
		tick1(w, AOS_IN_D); /* pressed at t == C+1 (>= C): accepted */
		CHECK(w->p[0].state == AOS_ST_DASH, "state %#x: dash cancel at t>=%d, state %#x", (unsigned)t[k].s, t[k].C, (unsigned)w->p[0].state);
	}
}

/* §3 0xa9: KO dismisses sprites/dwarfs/cubes (phase 0x5a) */
static void test_ko_hook(void)
{
	aos_world *w = &W;
	setup(w, 0, AOS_CH_SUGURI);
	tick1(w, AOS_IN_A);
	for (int f = 0; f < 30; f++)
		tick1(w, 0);
	w->p[0].hp = 0;
	aos_set_state(w, &w->p[0], AOS_ST_KO);
	int dismissed = 0;
	for (int i = 0; i < w->obj.nenemy; i++)
		if (w->obj.enemy[i].f10 == 100 && w->obj.enemy[i].f84 == 0x5a)
			dismissed++;
	CHECK(dismissed == 1, "KO hook (FUN_0072b4d0) puts the sprite in phase 0x5a, got %d", dismissed);
}

/* random play with full hyper: finite, deterministic, every draw path exercised */
static void test_random(void)
{
	for (int run = 0; run < 2; run++) {
		aos_world *w = run ? &W2 : &W;
		setup(w, 0, AOS_CH_SHAM);
		uint32_t s = 4242;
		uint16_t m[2] = {0, 0};
		for (int f = 0; f < 15000; f++) {
			for (int i = 0; i < 2; i++) {
				s = s * 1664525u + 1013904223u;
				if (((s >> 8) & 3) == 0)
					m[i] ^= (uint16_t)(1u << ((s >> 12) % 10));
				w->p[i].hyper = 3.0f;
				w->p[i].hp = w->p[i].maxHp;
			}
			aos_tick(w, m);
			aos_draw_begin();
			aos_draw_world(w);
			if (!isfinite(w->p[0].x) || !isfinite(w->p[1].x)) {
				CHECK(0, "non-finite at %d", f);
				return;
			}
		}
	}
	CHECK(!memcmp(&W, &W2, sizeof W), "random runs diverged");
}

int main(void)
{
	aos_register_all();
	test_dispatch();
	test_sprite_call();
	test_release();
	test_cubes();
	test_dwarfs();
	test_sphere();
	test_delta();
	test_end_frames();
	test_ko_hook();
	test_random();
	printf("test_sham: %s (%d failures)\n", fails ? "FAIL" : "ok", fails);
	return fails != 0;
}
