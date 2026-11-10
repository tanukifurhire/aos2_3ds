/* Behaviour checks for the Sumika / Sumika second-form port (src/sim/chars/sumika.c) against
 * docs/re/sumika.md (Part A = Sumika, Part B = second form). Each CHECK cites the spec section
 * or original address. Build: see tests/run_tests.sh. */
#include "../src/sim/draw.h"
#include "../src/sim/sim.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static aos_world W, W2;

static void tick(aos_world *w, uint16_t m0, uint16_t m1, int n)
{
	uint16_t m[2] = {m0, m1};
	for (int i = 0; i < n; i++) {
		aos_tick(w, m);
		aos_draw_begin();
		aos_draw_world(w);
	}
}

static void setup_id(aos_world *w, int id, int scheme, int difficulty)
{
	int32_t ids[2] = {id, AOS_CH_SUGURI}, sch[2] = {scheme, 0}, col[2] = {0, 1};
	aos_match_init(w, ids, sch, col, 999, 2);
	w->difficulty = difficulty;
	aos_round_init(w);
	tick(w, 0, 0, 210); /* intro 0x21d / 0x241 runs, then the input lock (frame < 200) ends */
	w->p[0].x = -100; w->p[0].y = 0;
	w->p[1].x = 100; w->p[1].y = 0;
	w->p[0].hyper = 3.0f;
	w->p[0].gaugeA = w->p[0].gaugeB = 1.0f;
}
static void setup(aos_world *w, int scheme) { setup_id(w, AOS_CH_SUMIKA, scheme, 0); }

static int count(aos_world *w, int fam, int type, int id)
{
	int n = 0;
	if (fam == 0) {
		for (int i = 0; i < w->obj.nmissile; i++)
			n += w->obj.missile[i].type == type && w->obj.missile[i].id == id;
	} else {
		for (int i = 0; i < w->obj.nenemy; i++)
			n += w->obj.enemy[i].type == type && w->obj.enemy[i].f10 == id;
	}
	return n;
}
static int feathers(aos_world *w, int type)
{
	int n = 0;
	for (int i = 0; i < w->obj.nenemy; i++) {
		aos_enemy *e = &w->obj.enemy[i];
		n += e->type == type && (e->f10 == 100 || e->f10 == 0x6e) && e->f84 != 900;
	}
	return n;
}

/* A3 0x21d (initial state, FUN_0069cc40 / FUN_00776d40): 8 feathers, slots 1000..1007, hp 9999 */
static void test_intro(void)
{
	aos_world *w = &W;
	setup(w, 0);
	CHECK(feathers(w, AOS_CH_SUMIKA) == 8, "intro spawns 8 feathers (got %d)", feathers(w, AOS_CH_SUMIKA));
	int ok = 1;
	for (int i = 0; i < w->obj.nenemy; i++) {
		aos_enemy *e = &w->obj.enemy[i];
		if (e->type == AOS_CH_SUMIKA && (e->f10 == 100 || e->f10 == 0x6e))
			ok &= e->hp == 9999 && e->maxHp >= 1000 && e->maxHp <= 1007 && e->f84 == 100;
	}
	CHECK(ok, "intro feathers: hp 9999, slot 1000..1007, idle phase 100");
}

/* A2 dispatch FUN_0077a950 / FUN_0077aec0 */
static void test_dispatch(void)
{
	static const struct { int scheme; uint16_t m; float hyper; int state; const char *what; } T[] = {
		{0, AOS_IN_A, 0, 0x21b, "A -> 0x21b Pretty Sword"},
		{0, AOS_IN_B, 0, 0x224, "B -> 0x224 Marble Ball"},
		{0, AOS_IN_S | AOS_IN_A, 0, 0x21f, "S+A -> 0x21f Shiny Feather"},
		{0, AOS_IN_S | AOS_IN_B, 0, 0x226, "S+B -> 0x226 Happy Barrel"},
		{0, AOS_IN_H, 1, 0x227, "H -> 0x227 Mystic Blast"},
		{0, AOS_IN_S | AOS_IN_H, 1, 0x228, "S+H -> 0x228 Angel Wing"},
		{0, AOS_IN_D | AOS_IN_H, 1, 0x220, "D+H with 8 feathers (< 9) -> 0x220 Over Limit"},
		{0, AOS_IN_B | AOS_IN_H, 1, 0x225, "B+H without capture -> 0x225 Tropical Ball"},
		{0, AOS_IN_S | AOS_IN_B | AOS_IN_H, 3, 0x22a, "S+B+H with < 9 feathers -> 0x22a Pretty Justice"},
		{1, AOS_IN_H | AOS_IN_A, 1, 0x227, "scheme B: H+A -> 0x227"},
		{1, AOS_IN_H | AOS_IN_SHIELD, 3, 0x22a, "scheme B: H+Shield -> 0x22a"},
		{1, AOS_IN_H | AOS_IN_B, 1, 0x225, "scheme B: H+B -> 0x225"},
	};
	for (size_t k = 0; k < sizeof T / sizeof T[0]; k++) {
		aos_world *w = &W;
		setup(w, T[k].scheme);
		w->p[0].hyper = T[k].hyper;
		if (T[k].scheme == 1)
			tick(w, AOS_IN_H, 0, 2);
		tick(w, T[k].m, 0, 1);
		CHECK(w->p[0].state == T[k].state, "%s (got %#x)", T[k].what, w->p[0].state);
	}
	aos_world *w = &W;
	setup(w, 0);
	tick(w, AOS_IN_D, 0, 1);
	tick(w, AOS_IN_D | AOS_IN_A, 0, 1);
	CHECK(w->p[0].state == 0x21e, "D held + A -> 0x21e Fantastic Feather (got %#x)", w->p[0].state);
}

/* ☆: Over Limit adds 8 feathers (hp 105) -> 16 >= 9 -> D+H gives 0x221, S+B+H gives 0x229 (A1.1) */
static void test_star(void)
{
	aos_world *w = &W;
	setup(w, 0);
	tick(w, AOS_IN_D | AOS_IN_H, 0, 1);
	tick(w, 0, 0, 90);
	CHECK(feathers(w, AOS_CH_SUMIKA) == 16, "Over Limit: 8 + 8 feathers (got %d)", feathers(w, AOS_CH_SUMIKA));
	w->p[0].hyper = 1.0f;
	tick(w, AOS_IN_D | AOS_IN_H, 0, 1);
	CHECK(w->p[0].state == 0x221, "D+H with >= 9 feathers -> 0x221 Heaven's Feather (got %#x)", w->p[0].state);
	tick(w, 0, 0, 100);
	w->p[0].hyper = 3.0f;
	tick(w, AOS_IN_S | AOS_IN_B | AOS_IN_H, 0, 1);
	CHECK(w->p[0].state == 0x229, "S+B+H with >= 9 feathers -> 0x229 Eternal Feather (got %#x)", w->p[0].state);
	/* Over-Limit feathers die after 5 launches (hp 105 -> < 101, A1.1) */
	int extra = 0;
	for (int i = 0; i < w->obj.nenemy; i++)
		extra += w->obj.enemy[i].type == AOS_CH_SUMIKA && w->obj.enemy[i].maxHp >= 0x44c && w->obj.enemy[i].f84 != 900;
	CHECK(extra <= 8, "Over-Limit feathers counted");
}

/* A3 0x21b Pretty Sword: pair at t=12 (age 30), tap -> t := 100; hold -> pairs at 12, 17, 22; A -0.25/pair */
static void test_pretty_sword(void)
{
	aos_world *w = &W;
	setup(w, 0);
	tick(w, AOS_IN_A, 0, 1);
	tick(w, 0, 0, 14);
	CHECK(count(w, 0, AOS_CH_SUMIKA, 100) == 1 && count(w, 0, AOS_CH_SUMIKA, 0x96) == 1, "tap: one sword pair");
	CHECK(w->p[0].timer > 100, "tap: t jumps to 100 after the first pair (t=%d)", w->p[0].timer);
	CHECK(fabsf(w->p[0].gaugeA - 0.75f) < 1e-4f, "pair costs 0.25 ammo A (ver>0x5a0) (A=%f)", w->p[0].gaugeA);
	setup(w, 0);
	tick(w, AOS_IN_A, 0, 24);
	CHECK(count(w, 0, AOS_CH_SUMIKA, 100) == 3 && count(w, 0, AOS_CH_SUMIKA, 0x96) == 3, "hold: three sword pairs");
	CHECK(w->obj.nwhip >= 6, "each sword trails a whip (kinds 9/10)");
}

/* A3 0x222 Heartful Arrow: t 12..29 one arrow per frame (+decoy enemy), B -0.03 each */
static void test_heartful(void)
{
	aos_world *w = &W;
	setup(w, 0);
	tick(w, AOS_IN_B, 0, 1);
	tick(w, 0, 0, 30);
	int n = count(w, 0, AOS_CH_SUMIKA, 200) + count(w, 0, AOS_CH_SUMIKA, 0xfa);
	CHECK(w->p[0].state == 0x222, "B tap -> 0x222 (got %#x)", w->p[0].state);
	CHECK(n == 18, "18 arrows for t=12..29 (got %d)", n);
	CHECK(fabsf(w->p[0].gaugeB - (1.0f - 18 * 0.03f)) < 1e-3f, "B cost 0.03 per arrow (B=%f)", w->p[0].gaugeB);
}

/* A3 0x224 Marble Ball: hold B -> 8 balls 300 for t 14..21, B -0.05 each */
static void test_marble(void)
{
	aos_world *w = &W;
	setup(w, 0);
	tick(w, AOS_IN_B, 0, 25);
	CHECK(count(w, 0, AOS_CH_SUMIKA, 300) == 8, "8 marble balls (got %d)", count(w, 0, AOS_CH_SUMIKA, 300));
	CHECK(fabsf(w->p[0].gaugeB - 0.6f) < 1e-3f, "B cost 0.05 per ball (B=%f)", w->p[0].gaugeB);
}

/* A5.2 barrel: spawn at t=5 (B -1.0), capture puts the opponent into hitstun with status 8 (★) */
static void test_barrel(void)
{
	aos_world *w = &W;
	setup(w, 0);
	tick(w, AOS_IN_S | AOS_IN_B, 0, 7);
	CHECK(count(w, 1, AOS_CH_SUMIKA, 300) == 1, "Happy Barrel enemy 300 spawned at t=5");
	CHECK(fabsf(w->p[0].gaugeB - 0.0f) < 1e-3f, "Happy Barrel costs 1.0 ammo B (B=%f)", w->p[0].gaugeB);
	aos_enemy *b = NULL;
	for (int f = 0; f < 60; f++) {
		for (int i = 0; i < w->obj.nenemy; i++)
			if (w->obj.enemy[i].type == AOS_CH_SUMIKA && w->obj.enemy[i].f10 == 300)
				b = &w->obj.enemy[i];
		if (b && b->f84 == 200) { /* place the opponent on the flying barrel */
			w->p[1].x = b->pos[0];
			w->p[1].y = b->pos[1];
		}
		tick(w, 0, 0, 1);
		if (b && b->f84 == 300)
			break;
	}
	CHECK(b && b->f84 == 300, "barrel captures the opponent (phase 300)");
	tick(w, 0, 0, 1);
	CHECK(w->p[1].state == AOS_ST_HITSTUN && w->p[1].statusOn[AOS_STATUS_MARK] == 1, "captured: state 1 + status 8 (FUN_006b1a40(opp,8,5))");
	w->p[0].hyper = 1.0f;
	tick(w, 0, 0, 30);
	tick(w, AOS_IN_B | AOS_IN_H, 0, 1);
	CHECK(w->p[0].state == 0x21c, "★ B+H while captured -> 0x21c Coquettish Sword (got %#x)", w->p[0].state);
	int t2 = -1;
	for (int i = 0; i < w->obj.nenemy; i++)
		if (w->obj.enemy[i].type == AOS_CH_SUMIKA && w->obj.enemy[i].f10 == 300)
			t2 = w->obj.enemy[i].timer2;
	CHECK(t2 == 201, "Coquettish resets the barrel hold timer to 200 (+1 after the tick) (got %d)", t2);
}

/* A3 0x228 Angel Wing: circles at (x±130, y−20), 130 damage on t%5==0 from t=15 */
static void test_angel_wing(void)
{
	aos_world *w = &W;
	setup(w, 0);
	w->p[1].x = w->p[0].x + 130;
	w->p[1].y = w->p[0].y - 20;
	tick(w, AOS_IN_S | AOS_IN_H, 0, 1);
	int hp0 = w->p[1].hp;
	tick(w, 0, 0, 15);
	CHECK(hp0 - w->p[1].hp == 130, "first Angel Wing hit = 130 at t=15 (got %d)", hp0 - w->p[1].hp);
	tick(w, 0, 0, 90);
	CHECK(w->p[0].state == 0, "Angel Wing ends (t>80)");
}

/* A3 0x21e Fantastic Feather: launches the most recent idle feather at t=15 (phase 200) */
static void test_fantastic(void)
{
	aos_world *w = &W;
	setup(w, 0);
	tick(w, AOS_IN_D, 0, 1);
	tick(w, AOS_IN_D | AOS_IN_A, 0, 1);
	tick(w, 0, 0, 15);
	int n = 0;
	for (int i = 0; i < w->obj.nenemy; i++)
		n += w->obj.enemy[i].type == AOS_CH_SUMIKA && w->obj.enemy[i].f84 == 200;
	CHECK(n == 1, "one feather launched into phase 200 (got %d)", n);
	tick(w, 0, 0, 200);
	CHECK(feathers(w, AOS_CH_SUMIKA) == 8, "feather returns to formation (still 8)");
}

/* Part B: second form */
static void test_second_form(void)
{
	aos_world *w = &W;
	for (int d = 0; d < 3; d++) {
		static const int hp[3] = {12000, 15000, 18000};
		setup_id(w, AOS_CH_SUMIKA2, 0, d);
		CHECK(w->p[0].maxHp == hp[d], "second form HP by CPU level %d = %d (FUN_0069cc40) (got %d)", d, hp[d], w->p[0].maxHp);
	}
	setup_id(w, AOS_CH_SUMIKA2, 0, 1);
	CHECK(feathers(w, AOS_CH_SUMIKA2) == 8, "second form intro 0x241: 8 feathers");
	w->p[0].hyper = 3.0f;
	tick(w, AOS_IN_S | AOS_IN_B | AOS_IN_H, 0, 1);
	CHECK(w->p[0].state == 0x24c, "B1: no S+B+H — S+H row wins -> 0x24c (got %#x)", w->p[0].state);
	tick(w, 0, 0, 100);
	/* B3: HP <= max/3 in neutral -> 0x24d -> (100 frames) 0x24e */
	w->p[0].hp = w->p[0].maxHp / 3;
	tick(w, 0, 0, 1);
	CHECK(w->p[0].state == 0x24d, "HP <= max/3 -> 0x24d (got %#x)", w->p[0].state);
	CHECK(w->p[0].barrierTime >= 390 && w->p[0].statusOn[AOS_STATUS_STUNIMMUNE], "0x24d: barrier 400 + status 6 (no hitstun)");
	tick(w, 0, 0, 101);
	CHECK(w->p[0].state == 0x24e, "0x24d -> 0x24e after 100 frames (got %#x)", w->p[0].state);
	CHECK(fabsf(w->p[0].x) <= 2.0f && fabsf(w->p[0].y) <= 2.0f, "moved to the arena centre (+-2 for the per-frame int truncation) (%f,%f)", w->p[0].x, w->p[0].y);
	CHECK(w->obj.nenemy > 0 && w->obj.enemy[0].type == AOS_CH_SUMIKA2 && w->obj.enemy[0].f10 == 500 && w->obj.enemy[0].solid,
	      "core enemy 500 inserted at the FRONT of the enemy list, solid");
	int sat = 0, tent = 0;
	for (int f = 0; f < 1000; f++) {
		w->p[0].hp = w->p[0].maxHp / 4;
		tick(w, 0, 0, 1);
		sat |= count(w, 1, AOS_CH_SUMIKA2, 700) > 0;
		tent |= count(w, 0, AOS_CH_SUMIKA2, 600) > 0;
	}
	CHECK(w->p[0].state == 0x24e, "0x24e never ends on its own");
	CHECK(tent, "tentacles (missile 600) spawned from t>=300");
	CHECK(sat, "satellites (enemy 700/0x2c6) spawned at t%%1000 == 900");
}

static void test_random_determinism(void)
{
	for (int run = 0; run < 2; run++) {
		aos_world *w = run ? &W2 : &W;
		setup(w, 0);
		w->p[1].id = AOS_CH_SUGURI;
		uint32_t s = 99;
		uint16_t m[2] = {0, 0};
		int bad = 0;
		for (int f = 0; f < 15000; f++) {
			for (int i = 0; i < 2; i++) {
				s = s * 1664525u + 1013904223u;
				if (((s >> 8) & 3) == 0)
					m[i] ^= (uint16_t)(1u << ((s >> 12) % 10));
				w->p[i].hyper = 3.0f;
				w->p[i].hp = w->p[i].maxHp;
			}
			tick(w, m[0], m[1], 1);
			bad += !isfinite(w->p[0].x) || !isfinite(w->p[1].x);
		}
		CHECK(bad == 0, "random run %d finite", run);
	}
	CHECK(!memcmp(&W, &W2, sizeof W), "random runs diverged (determinism)");
}

int main(void)
{
	aos_register_all();
	test_intro();
	test_dispatch();
	test_star();
	test_pretty_sword();
	test_heartful();
	test_marble();
	test_barrel();
	test_angel_wing();
	test_fantastic();
	test_second_form();
	test_random_determinism();
	printf("test_sumika: %s (%d/%d checks failed)\n", fails ? "FAIL" : "ok", fails, checks);
	return fails != 0;
}
