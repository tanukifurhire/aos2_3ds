/* Behaviour checks for the Sora port (src/sim/chars/sora.c) against docs/re/sora.md.
 * Every CHECK cites the spec section and the original address.
 * Build: see tests/run_tests.sh (all sim sources + this file). */
#include "../src/sim/draw.h"
#include "../src/sim/sim.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static aos_world W, W2;

static void draw_all(aos_world *w)
{
	aos_draw_begin();
	aos_draw_players(w);
	for (int i = 0; i < w->obj.nbeam; i++)
		if (aos_otab.beamDraw[w->obj.beam[i].type]) aos_otab.beamDraw[w->obj.beam[i].type](w, &w->obj.beam[i]);
	for (int i = 0; i < w->obj.nmissile; i++)
		if (aos_otab.missileDraw[w->obj.missile[i].type]) aos_otab.missileDraw[w->obj.missile[i].type](w, &w->obj.missile[i]);
	for (int i = 0; i < w->obj.nlaser; i++)
		if (aos_otab.laserDraw[w->obj.laser[i].type]) aos_otab.laserDraw[w->obj.laser[i].type](w, &w->obj.laser[i]);
	for (int i = 0; i < w->obj.neffect; i++)
		if (aos_otab.effectDraw[w->obj.effect[i].type]) aos_otab.effectDraw[w->obj.effect[i].type](w, &w->obj.effect[i]);
}

/* Sora vs Sora, scheme s0 for P1, both past the input lock, P1 at x0, P2 at x1. */
static void setup(aos_world *w, int s0, float x0, float x1)
{
	int32_t ids[2] = {AOS_CH_SORA, AOS_CH_SORA}, sch[2] = {s0, 0}, col[2] = {0, 1};
	aos_match_init(w, ids, sch, col, 999, 2);
	uint16_t m[2] = {0, 0};
	for (int i = 0; i < 210; i++)
		aos_tick(w, m);
	w->p[0].x = x0, w->p[0].y = 0;
	w->p[1].x = x1, w->p[1].y = 0;
	aos_tick(w, m);
}

static void tick1(aos_world *w, uint16_t m0)
{
	uint16_t m[2] = {m0, 0};
	aos_tick(w, m);
	draw_all(w);
}

/* Press a chord: hold `hold` for 2 frames, then add `press` for one frame. */
static void chord(aos_world *w, uint16_t hold, uint16_t press)
{
	if (hold) {
		tick1(w, hold);
		tick1(w, hold);
	}
	tick1(w, hold | press);
}

/* ---- §2 input dispatch FUN_0073b470 / FUN_0073b930 ---- */
static void test_dispatch(void)
{
	static const struct { int scheme; uint16_t hold, press; float hyper; int state; const char *why; } T[] = {
		{0, 0, AOS_IN_A, 0, 0x14, "§2 B7 A -> 0x14"},
		{0, AOS_IN_D, AOS_IN_A, 0, 0x13, "§2 B6 D held + A -> 0x13"},
		{0, 0, AOS_IN_B, 0, 0x1f, "§2 B5 B -> 0x1f"},
		{0, AOS_IN_S, AOS_IN_A, 0, 0x18, "§2 B4 S+A -> 0x18 (no distance test)"},
		{0, AOS_IN_S, AOS_IN_B, 0, 0x1b, "§2 B3 S+B -> 0x1b"},
		{0, 0, AOS_IN_H, 1, 0x16, "§2 A5 H -> 0x16"},
		{0, AOS_IN_D, AOS_IN_H, 1, 0x17, "§2 A4 D+H -> 0x17"},
		{0, AOS_IN_B, AOS_IN_H, 1, 0x20, "§2 A3 B+H -> 0x20"},
		{0, AOS_IN_S, AOS_IN_H, 1, 0x1c, "§2 A2 S+H -> 0x1c"},
		{0, AOS_IN_S | AOS_IN_B, AOS_IN_H, 3, 0x21, "§2 A1 S+B+H (3.0) -> 0x21"},
		{0, AOS_IN_S | AOS_IN_B, AOS_IN_H, 2, 0x1c, "§2 A1 fails at 2 levels -> next row 0x1c"},
		{1, AOS_IN_H, AOS_IN_A, 1, 0x16, "§2 alt: H held + A -> 0x16"},
		{1, AOS_IN_H, AOS_IN_D, 1, 0x17, "§2 alt: H held + Dash -> 0x17"},
		{1, AOS_IN_H, AOS_IN_B, 1, 0x20, "§2 alt: H held + B -> 0x20"},
		{1, AOS_IN_H, AOS_IN_S, 1, 0x1c, "§2 alt: H held + S -> 0x1c"},
		{1, AOS_IN_H, AOS_IN_SHIELD, 3, 0x21, "§2 alt: H held + Shield (3.0) -> 0x21"},
	};
	for (size_t k = 0; k < sizeof T / sizeof T[0]; k++) {
		aos_world *w = &W;
		setup(w, T[k].scheme, -100, 100);
		w->p[0].hyper = T[k].hyper;
		chord(w, T[k].hold, T[k].press);
		CHECK(w->p[0].state == T[k].state, "%s (got %#x)", T[k].why, w->p[0].state);
		if (T[k].hyper >= 1 && T[k].state >= 0x16 && T[k].state != 0x18 && T[k].state != 0x1b)
			CHECK(fabsf(w->p[0].hyper - (T[k].hyper - (T[k].state == 0x21 ? 3.0f : 1.0f))) < 1e-4f,
			      "%s: hyper cost (left %f)", T[k].why, w->p[0].hyper);
	}
}

/* run P1 with mask until the state leaves `st` (or max frames); returns ticks run */
static int run_state(aos_world *w, uint16_t m0, int st, int max, int *spawnT, int *nBeam)
{
	int n = 0;
	*spawnT = -1;
	*nBeam = 0;
	while (n < max && w->p[0].state == st) {
		int t = w->p[0].timer, nb = w->obj.nbeam;
		tick1(w, m0);
		if (w->obj.nbeam > nb) {
			if (*spawnT < 0)
				*spawnT = t;
			*nBeam += w->obj.nbeam - nb;
		}
		n++;
	}
	return n;
}

/* ---- §3 0x12 / 0x14 / 0x13, §5.1 beam params, §1.1 costs ---- */
static void test_rifle(void)
{
	aos_world *w = &W;
	setup(w, 0, -100, 100);
	int hp0 = w->p[1].hp;
	tick1(w, AOS_IN_A); /* press; A reads 1 (released) next frame, 0 the frame after -> "A < 1" */
	tick1(w, 0);
	tick1(w, 0);
	CHECK(w->p[0].state == 0x12, "§3 0x14 early release -> 0x12 timer kept (got %#x)", w->p[0].state);
	int sp, nb;
	int last = -1;
	for (int f = 0; f < 60 && w->p[0].state == 0x12; f++) {
		int t = w->p[0].timer, n0 = w->obj.nbeam;
		float a0 = w->p[0].gaugeA, h0 = w->p[0].hyper;
		tick1(w, 0);
		if (w->obj.nbeam > n0) {
			sp = t;
			CHECK(t == 12, "§3 0x12 beam on t=12 (FUN_00737930) got %d", t);
			CHECK(w->obj.beam[n0].id == 100 && w->obj.beam[n0].damage == 180 && w->obj.beam[n0].speed == 17.0f,
			      "§5.1 beam 100: dmg 180 speed 17 (FUN_004b3fe0)");
			CHECK(fabsf((a0 - w->p[0].gaugeA) - 0.25f) < 0.011f, "§1.1 0x12 costs A 0.25 (got %f)", a0 - w->p[0].gaugeA);
			CHECK(fabsf((w->p[0].hyper - h0) - 0.04f) < 1e-4f, "§1.1 0x12 hyper +0.04");
		}
		last = t;
	}
	(void)sp;
	CHECK(last == 31, "§3 0x12 ends when t > 30 (last t %d)", last);
	for (int f = 0; f < 30; f++)
		tick1(w, 0);
	CHECK(hp0 - w->p[1].hp == 180, "§5.1 beam 100 deals 180 (got %d)", hp0 - w->p[1].hp);

	/* Rifle Spread: 6 beams 200 on t=10,12,..,20 (FUN_00737f30) */
	setup(w, 0, -400, 400); /* far: the inherited dash would carry Sora onto P2 */
	chord(w, AOS_IN_D, AOS_IN_A);
	CHECK(w->p[0].state == 0x13, "0x13 entered");
	run_state(w, AOS_IN_D, 0x13, 60, &sp, &nb);
	CHECK(sp == 10 && nb == 6, "§3 0x13 first beam t=10, 6 beams (got %d, %d)", sp, nb);

	/* Charged: hold A >= 30 frames, release -> beam 300 dmg 130 (FUN_00737a50, FUN_004b3fe0) */
	setup(w, 0, -100, 300);
	tick1(w, AOS_IN_A);
	int fired = -1;
	for (int f = 0; f < 60; f++) {
		int t = w->p[0].timer, n0 = w->obj.nbeam;
		tick1(w, f < 35 ? AOS_IN_A : 0);
		if (w->obj.nbeam > n0 && fired < 0) {
			fired = t;
			CHECK(w->obj.beam[n0].id == 300 && w->obj.beam[n0].damage == 130 && w->obj.beam[n0].shape.radius == 30.0f,
			      "§5.1 beam 300: dmg 130 r 30");
			CHECK(w->p[0].timer == 101, "§3 0x14 charged shot sets t := 100");
		}
	}
	CHECK(fired >= 30 && fired <= 39, "§3 0x14 charged release window 30..39 (fired at %d)", fired);
}

/* ---- §3 swords, chains, §5.4 hitboxes ---- */
static void test_swords(void)
{
	aos_world *w = &W;
	setup(w, 0, -100, 100);
	int hp0 = w->p[1].hp;
	chord(w, AOS_IN_S, AOS_IN_A);
	CHECK(w->p[0].state == 0x18, "0x18 entered");
	while (w->p[0].timer < 12)
		tick1(w, 0);
	CHECK(hp0 - w->p[1].hp == 180, "§5.4 hitbox 200 (Sword 1, t=10) deals 180 (got %d)", hp0 - w->p[1].hp);
	CHECK(w->p[0].hitFlag == 1, "§5.4 hitbox 200 sets hit-confirm (FUN_005ea560)");
	tick1(w, AOS_IN_B);
	CHECK(w->p[0].state == 0x1b, "§3 0x18 hit + B -> 0x1b Slash Through (got %#x)", w->p[0].state);
	hp0 = w->p[1].hp;
	for (int f = 0; f < 12; f++)
		tick1(w, 0);
	CHECK(hp0 - w->p[1].hp == 240, "§5.4 hitbox 0xe6 deals 240 (got %d)", hp0 - w->p[1].hp);
	tick1(w, AOS_IN_A);
	CHECK(w->p[0].state == 0x15, "§3 0x1b hit + A -> 0x15 (got %#x)", w->p[0].state);

	/* Sword 1 -> 2 -> 3 on S+A after hit-confirm */
	setup(w, 0, -100, 100);
	chord(w, AOS_IN_S, AOS_IN_A);
	while (w->p[0].timer < 12)
		tick1(w, AOS_IN_S);
	tick1(w, AOS_IN_S | AOS_IN_A);
	CHECK(w->p[0].state == 0x19, "§3 0x18 hit + S+A -> 0x19 (got %#x)", w->p[0].state);

	/* whiff: no chain, ends at t>40 */
	setup(w, 0, -500, 500);
	chord(w, AOS_IN_S, AOS_IN_A);
	int sp, nb, n = run_state(w, AOS_IN_B, 0x18, 80, &sp, &nb);
	CHECK(n == 41 && w->p[0].state == 0 && w->p[0].hitFlag == 0, "§3 0x18 whiff: no chain, ends at t=41 (ran %d, state %#x)", n, w->p[0].state);
}

/* ---- §3 0x1c Boisterous Dance: whiff path / hit path to 0x1d ---- */
static void test_dance(void)
{
	aos_world *w = &W;
	setup(w, 0, -400, 400);
	w->p[0].hyper = 1;
	chord(w, AOS_IN_S, AOS_IN_H);
	CHECK(w->p[0].state == 0x1c, "0x1c entered");
	CHECK(w->p[0].barrierType == 3, "§1.2 hyper start kind 10 -> barrierType 3 (FUN_006b1b40)");
	while (w->p[0].state == 0x1c && w->p[0].timer < 11)
		tick1(w, 0);
	tick1(w, 0); /* runs t=11: whiff detected */
	CHECK(w->p[0].timer == 67, "§3 0x1c whiff: t := 66 (got t %d)", w->p[0].timer);
	CHECK(w->p[0].barrierTime == 49 && w->p[0].barrierBreakT == 25,
	      "§3 0x1c whiff: +0xf8 := 50, +0x108 := 25 (got %d, %d)", w->p[0].barrierTime, w->p[0].barrierBreakT);

	setup(w, 0, -60, 60);
	w->p[0].hyper = 1;
	int hp0 = w->p[1].hp;
	chord(w, AOS_IN_S, AOS_IN_H);
	int saw1d = 0;
	for (int f = 0; f < 90; f++) {
		tick1(w, 0);
		if (w->p[0].state == 0x1d)
			saw1d = 1;
	}
	CHECK(saw1d, "§3 0x1c hit path -> 0x1d at t=65 (opponent in hitstun)");
	/* opener 100 + 2x0xfb 50 + 3x0xfc 50 + finisher 200 = 550 (heat-scaled if heat > 1) */
	CHECK(hp0 - w->p[1].hp >= 550, "§5.4 Boisterous total >= 550 (got %d)", hp0 - w->p[1].hp);
}

/* ---- §3 launchers, §5.2 missiles ---- */
static void test_launchers(void)
{
	aos_world *w = &W;
	setup(w, 0, -100, 300);
	tick1(w, AOS_IN_B); /* tap: released before t 11 -> 0x1e */
	tick1(w, 0);
	CHECK(w->p[0].state == 0x1e, "§3 0x1f early release -> 0x1e (got %#x)", w->p[0].state);
	int first = -1, n = 0;
	float b0 = w->p[0].gaugeB;
	for (int f = 0; f < 60 && w->p[0].state == 0x1e; f++) {
		int t = w->p[0].timer, m0 = w->obj.nmissile;
		tick1(w, 0);
		if (w->obj.nmissile > m0) {
			if (first < 0) {
				first = t;
				aos_missile *m = &w->obj.missile[m0];
				CHECK(m->id == 100 && m->damage == 150 && m->speed == 4.75f && m->hp == 60,
				      "§5.2 missile 100: dmg 150 speed 5 (-0.25 in its first update) hp 60 (FUN_00677970)");
			}
			n += w->obj.nmissile - m0;
		}
	}
	CHECK(first == 12 && n == 7, "§3 0x1e: 7 missiles from t=12 (got %d from %d)", n, first);
	CHECK(b0 - w->p[0].gaugeB > 0.5f, "§1.1 0x1e costs B 0.075 each");

	setup(w, 0, -100, 300);
	first = -1, n = 0;
	for (int f = 0; f < 70; f++) {
		int t = w->p[0].timer, m0 = w->obj.nmissile, st = w->p[0].state;
		tick1(w, AOS_IN_B);
		if (st == 0x1f && w->obj.nmissile > m0) {
			if (first < 0) {
				first = t;
				aos_missile *m = &w->obj.missile[m0];
				CHECK(m->id == 0x96 && m->damage == 250 && m->hp == 120 && m->shapeBody.halfW == 30.0f,
				      "§5.2 rocket 0x96: dmg 250 hp 120 rect 30x20");
				CHECK(m->angle == w->p[0].faceAngle, "§3 0x1f rocket +0x6c := aim");
			}
			n += w->obj.nmissile - m0;
		}
	}
	CHECK(first == 21 && n == 4, "§3 0x1f ver>=0x5a1: 4 rockets from t=21 (got %d from %d)", n, first);

	setup(w, 0, -100, 300);
	w->p[0].hyper = 1;
	chord(w, AOS_IN_B, AOS_IN_H);
	n = 0;
	int dmgOk = 1;
	for (int f = 0; f < 70; f++) {
		int m0 = w->obj.nmissile;
		tick1(w, 0);
		for (int i = m0; i < w->obj.nmissile; i++)
			dmgOk &= w->obj.missile[i].damage == 200;
		if (w->obj.nmissile > m0)
			n += w->obj.nmissile - m0;
	}
	CHECK(n == 10 && dmgOk, "§3 0x20: 10 rockets with damage 200 (got %d)", n);
}

/* ---- §3 0x16/0x17 lasers, §5.3 ---- */
static void test_lasers(void)
{
	aos_world *w = &W;
	setup(w, 0, -100, 100);
	w->p[0].hyper = 1;
	tick1(w, AOS_IN_H);
	int n = 0, first = -1;
	for (int f = 0; f < 60; f++) {
		int t = w->p[0].timer, l0 = w->obj.nlaser;
		tick1(w, 0);
		if (w->obj.nlaser > l0) {
			if (first < 0) {
				first = t;
				aos_laser *l = &w->obj.laser[l0];
				CHECK(l->damage == 10 && l->blockMode == 2 && l->knockback == 5.0f, "§5.3 laser 100 params (FUN_00625910)");
				CHECK(fabsf(l->angle - w->p[0].faceAngle) < 1e-6f, "§3 0x16 lasers parallel to aim");
			}
			n++;
		}
	}
	CHECK(first == 9 && n == 10, "§3 0x16: 10 lasers from t=9 (got %d from %d)", n, first);
}

/* ---- §3 0x21 Unlimited Charge, §5.6 effect 0x15e wave ---- */
static void test_unlimited(void)
{
	aos_world *w = &W;
	setup(w, 0, -300, 300);
	w->p[0].hyper = 3;
	chord(w, AOS_IN_S | AOS_IN_B, AOS_IN_H);
	CHECK(w->p[0].state == 0x21 && w->p[0].barrierType == 4, "0x21 entered, Accel barrier (kind 0x14)");
	int hp0 = w->p[1].hp, wave = 0;
	for (int f = 0; f < 140; f++) {
		tick1(w, 0);
		for (int i = 0; i < w->obj.neffect; i++)
			if (w->obj.effect[i].type == AOS_CH_SORA && w->obj.effect[i].id == 0x15e)
				wave = 1;
	}
	CHECK(wave, "§5.6 effect 0x15e spawned at t=60");
	CHECK(hp0 - w->p[1].hp >= 110, "§5.4 UC hitboxes deal damage (got %d)", hp0 - w->p[1].hp);
	CHECK(w->p[0].state == 0, "§3 0x21 ends at t > 110");
}


/* ---- dash-cancel frames (suguri.md §1.1 pattern; Sora §3 per state) ---- */
static void test_cancel(void)
{
	static const struct { uint16_t hold, press; int st, n; const char *why; } T[] = {
		{0, AOS_IN_B, 0x1f, 31, "§3 0x1f dash-cancel from t=31 (FUN_00738c00)"},
		{AOS_IN_S, AOS_IN_A, 0x18, 30, "§3 0x18 dash-cancel from t=30 (FUN_00739670)"},
		{AOS_IN_S, AOS_IN_B, 0x1b, 30, "§3 0x1b dash-cancel from t=30 (FUN_0073a010)"},
	};
	for (size_t k = 0; k < sizeof T / sizeof T[0]; k++)
		for (int early = 1; early >= 0; early--) {
			aos_world *w = &W;
			setup(w, 0, -500, 500);
			chord(w, T[k].hold, T[k].press);
			uint16_t keep = T[k].st == 0x1f ? AOS_IN_B : 0;
			while (w->p[0].timer < T[k].n - early)
				tick1(w, keep);
			tick1(w, keep | AOS_IN_D);
			if (early)
				CHECK(w->p[0].state == T[k].st, "%s: no cancel at t=%d", T[k].why, T[k].n - 1);
			else
				CHECK(w->p[0].state == AOS_ST_DASH, "%s (state %#x)", T[k].why, w->p[0].state);
		}
}

/* ---- all states run + random hyper play deterministic ---- */
static void test_random(void)
{
	for (int run = 0; run < 2; run++) {
		aos_world *w = run ? &W2 : &W;
		setup(w, run & 0, -100, 100);
		uint32_t s = 4242;
		uint16_t m[2] = {0, 0};
		for (int f = 0; f < 20000; f++) {
			for (int i = 0; i < 2; i++) {
				s = s * 1664525u + 1013904223u;
				if (((s >> 8) & 3) == 0)
					m[i] ^= (uint16_t)(1u << ((s >> 12) % 10));
				w->p[i].hyper = 3.0f;
				w->p[i].hp = w->p[i].maxHp;
			}
			aos_tick(w, m);
			draw_all(w);
			if (!isfinite(w->p[0].x) || !isfinite(w->p[1].x)) {
				CHECK(0, "non-finite at %d", f);
				return;
			}
		}
	}
	CHECK(!memcmp(&W, &W2, sizeof W), "random hyper runs diverged");
	CHECK(W.obj.overflow == 0, "pool overflow %d", W.obj.overflow);
}

int main(void)
{
	aos_register_all();
	test_dispatch();
	test_rifle();
	test_swords();
	test_dance();
	test_launchers();
	test_lasers();
	test_unlimited();
	test_cancel();
	test_random();
	printf("test_sora: %s (%d failures)\n", fails ? "FAIL" : "ok", fails);
	return fails != 0;
}
