/* Behaviour checks for the Iru port (src/sim/chars/iru.c) against docs/re/iru.md.
 * Every assertion cites the spec section and the original address.
 * Build: zig cc -std=gnu11 -Wall -Wextra -Werror -O1 -fsanitize=undefined src/sim/{*}.c
 *        src/sim/chars/{*}.c tests/test_iru.c -lm -o build/tests/test_iru.exe */
#include "../src/sim/draw.h"
#include "../src/sim/sim.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static aos_world W, W2;
static uint16_t M[2];

static void setup(aos_world *w, int scheme, float oppDx)
{
	int32_t ids[2] = {AOS_CH_IRU, AOS_CH_SUGURI}, sch[2] = {scheme, 0}, col[2] = {0, 1};
	aos_match_init(w, ids, sch, col, 999, 2);
	M[0] = M[1] = 0;
	for (int i = 0; i < 210; i++) /* past the round-start input lock */
		aos_tick(w, M);
	w->p[0].x = -200; w->p[0].y = 0;
	w->p[1].x = -200 + oppDx; w->p[1].y = 0;
	w->p[0].hyper = 3.0f;
	w->p[0].gaugeA = w->p[0].gaugeB = 1.0f;
}

static void tick(aos_world *w, uint16_t mask, int n)
{
	for (int i = 0; i < n; i++) {
		M[0] = mask;
		aos_tick(w, M);
		aos_draw_begin();
		aos_draw_world(w);
	}
}

/* Press `press` (on top of `held`, which must already be held) and return the new state. */
static int32_t enter(aos_world *w, uint16_t held, uint16_t press)
{
	if (held)
		tick(w, held, 3);
	tick(w, held | press, 1);
	return w->p[0].state;
}

static int count_type(const aos_world *w, char fam, int32_t id)
{
	int n = 0;
	if (fam == 'b') for (int i = 0; i < w->obj.nbeam; i++) n += w->obj.beam[i].type == AOS_CH_IRU && w->obj.beam[i].id == id;
	if (fam == 'm') for (int i = 0; i < w->obj.nmissile; i++) n += w->obj.missile[i].type == AOS_CH_IRU && w->obj.missile[i].id == id;
	if (fam == 'l') for (int i = 0; i < w->obj.nlaser; i++) n += w->obj.laser[i].type == AOS_CH_IRU && w->obj.laser[i].id == id;
	if (fam == 'e') for (int i = 0; i < w->obj.nenemy; i++) n += w->obj.enemy[i].type == AOS_CH_IRU;
	return n;
}

/* ---- §2 input dispatch (FUN_006d3910 / FUN_006d3e60) ---- */
static void test_dispatch(void)
{
	aos_world *w = &W;
	struct { uint16_t held, press; int32_t st; const char *what; } a[] = {
		{0, AOS_IN_A, 0x179, "A -> 0x179 (§2 B7)"},
		{0, AOS_IN_B, 0x17b, "B -> 0x17b (§2 B6)"},
		{AOS_IN_S, AOS_IN_A, 0x17c, "S+A -> 0x17c (§2 B4)"},
		{AOS_IN_S, AOS_IN_B, 0x180, "S+B -> 0x180 (§2 B5)"},
		{AOS_IN_S | AOS_IN_B, AOS_IN_H, 0x184, "S+B+H -> 0x184 (§2 A1)"},
		{AOS_IN_S, AOS_IN_H, 0x17e, "S+H -> 0x17e (§2 A3)"},
		{AOS_IN_B, AOS_IN_H, 0x183, "B+H -> 0x183 (§2 A4)"},
		{0, AOS_IN_H, 0x182, "H -> 0x182 (§2 A6)"},
	};
	for (size_t k = 0; k < sizeof a / sizeof a[0]; k++) {
		setup(w, 0, 400);
		int32_t s = enter(w, a[k].held, a[k].press);
		CHECK(s == a[k].st, "%s (got %#x)", a[k].what, s);
	}
	/* Dash-held variants: D held while dashing (state 2 also dispatches, §2 B) */
	setup(w, 0, 400);
	tick(w, AOS_IN_D, 1);
	CHECK(w->p[0].state == AOS_ST_DASH, "D press -> dash");
	tick(w, AOS_IN_D | AOS_IN_S, 1);
	tick(w, AOS_IN_D | AOS_IN_S | AOS_IN_A, 1);
	CHECK(w->p[0].state == 0x17d, "D held + S held + A -> 0x17d (§2 B3) (got %#x)", w->p[0].state);
	setup(w, 0, 400);
	tick(w, AOS_IN_D, 1);
	tick(w, AOS_IN_D | AOS_IN_S, 1);
	tick(w, AOS_IN_D | AOS_IN_S | AOS_IN_H, 1);
	CHECK(w->p[0].state == 0x17f, "D+S+H -> 0x17f (§2 A2) (got %#x)", w->p[0].state);
	setup(w, 0, 400);
	tick(w, AOS_IN_D, 1);
	tick(w, AOS_IN_D | AOS_IN_H, 1);
	CHECK(w->p[0].state == 0x181, "D+H -> 0x181 (§2 A5) (got %#x)", w->p[0].state);
	/* hyper rows fall through when the gauge is short (FUN_00780910): S+B+H with 1 level -> S+H */
	setup(w, 0, 400);
	w->p[0].hyper = 1.5f;
	CHECK(enter(w, AOS_IN_S | AOS_IN_B, AOS_IN_H) == 0x17e, "S+B+H with 1.5 gauge -> 0x17e (§2 A)");
	CHECK(fabsf(w->p[0].hyper - 0.5f) < 1e-4f, "hyper -1.0 (got %f)", (double)w->p[0].hyper);

	/* scheme B (FUN_006d3e60) */
	struct { uint16_t held, press; int32_t st; const char *what; } b[] = {
		{AOS_IN_H, AOS_IN_SHIELD, 0x184, "B: H held + Shield -> 0x184"},
		{AOS_IN_H, AOS_IN_B, 0x183, "B: H held + B -> 0x183"},
		{AOS_IN_H, AOS_IN_A, 0x182, "B: H held + A -> 0x182"},
		{AOS_IN_H, AOS_IN_D, 0x181, "B: H held + D -> 0x181"},
	};
	for (size_t k = 0; k < sizeof b / sizeof b[0]; k++) {
		setup(w, 1, 400);
		w->p[0].hyper = 3.0f;
		int32_t s = enter(w, b[k].held, b[k].press);
		CHECK(s == b[k].st, "%s (got %#x)", b[k].what, s);
	}
	/* D held first (a D press with H held would be row 5 -> 0x181), then H, then press S */
	setup(w, 1, 400);
	tick(w, AOS_IN_D, 3);
	tick(w, AOS_IN_D | AOS_IN_H, 1);
	tick(w, AOS_IN_D | AOS_IN_H | AOS_IN_S, 1);
	CHECK(w->p[0].state == 0x17f, "B: D,H held + S -> 0x17f (got %#x)", w->p[0].state);
	/* scheme-B quirk (§0.6, 0x6d3f75): S held && H held, no press edge, re-fires after 20 frames */
	setup(w, 1, 400);
	w->p[0].hyper = 3.0f;
	int entries = 0;
	int32_t prevT = 99;
	for (int f = 0; f < 60; f++) {
		tick(w, AOS_IN_S | AOS_IN_H, 1);
		if (w->p[0].state == 0x17e && w->p[0].timer < prevT)
			entries++;
		prevT = w->p[0].state == 0x17e ? w->p[0].timer : 99;
	}
	CHECK(entries >= 2, "scheme B S+H held re-triggers 0x17e (entries %d)", entries);
}

/* ---- §3 states: spawn frames, parameters, damage, end and dash-cancel frames ---- */

static int spawn_timer(aos_world *w, char fam, int32_t id, int maxF, uint16_t mask, int *count)
{
	int first = -1, n = 0;
	for (int f = 0; f < maxF; f++) {
		int32_t t = w->p[0].timer, st = w->p[0].state;
		int before = count_type(w, fam, id);
		tick(w, mask, 1);
		int after = count_type(w, fam, id);
		if (after > before && st >= 0x178) {
			if (first < 0)
				first = t; /* timer before the tick = the value the T2 sees (§1.1) */
			n += after - before;
		}
	}
	if (count)
		*count = n;
	return first;
}

static void test_rifle(void)
{
	aos_world *w = &W;
	setup(w, 0, 400);
	tick(w, AOS_IN_A, 1); /* 0x179 t=0 */
	tick(w, 0, 2);        /* release (value 1, then 0: "A < 1" seen at t=2) -> 0x178, timer kept (§3 0x179) */
	CHECK(w->p[0].state == 0x178, "A tap -> 0x178 (got %#x)", w->p[0].state);
	w->p[0].hyper = 0.0f; /* gauge clamps at 3.0 */
	float ga = w->p[0].gaugeA, hy = w->p[0].hyper;
	int hp0 = w->p[1].hp, n;
	int t = spawn_timer(w, 'b', 100, 12, 0, &n);
	CHECK(t == 14, "Photon Rifle beam 100 at t=14 (§3 0x178, FUN_006d1080) (got %d)", t);
	CHECK(fabsf((ga - w->p[0].gaugeA) - 0.3f) < 1e-4f, "cost A 0.3 (§1.2)");
	CHECK(fabsf((w->p[0].hyper - hy) - 0.11f) < 1e-4f, "hyper gain 0.11 (§1.2, FUN_004a7270)");
	for (int i = 0; i < w->obj.nbeam; i++)
		if (w->obj.beam[i].id == 100) {
			CHECK(w->obj.beam[i].speed == 25.0f && w->obj.beam[i].damage == 360, "beam 100 speed 25 dmg 360 (§5.1)");
			CHECK(w->obj.beam[i].shape.halfW == 36.0f && w->obj.beam[i].shape.halfH == 16.0f, "beam 100 shape {36,16}");
		}
	tick(w, 0, 30);
	CHECK(hp0 - w->p[1].hp == 360, "Photon Rifle damage 360 (§5.1) (got %d)", hp0 - w->p[1].hp);

	/* end: state 0 when t > 40 (FUN_006d1080) */
	setup(w, 0, 400);
	tick(w, AOS_IN_A, 1);
	tick(w, 0, 2);
	while (w->p[0].state == 0x178 && w->p[0].timer < 60)
		tick(w, 0, 1);
	CHECK(w->p[0].timer == 42, "0x178 ends after t=41 (t>40) (timer now %d)", w->p[0].timer);

	/* dash-cancel from t=22 (FUN_006d1080) */
	setup(w, 0, 400);
	tick(w, AOS_IN_A, 1);
	tick(w, 0, 2);
	while (w->p[0].timer < 20)
		tick(w, 0, 1);
	tick(w, AOS_IN_D, 1); /* T2 runs with t=20 */
	CHECK(w->p[0].state == 0x178, "no dash-cancel at t=20");
	tick(w, 0, 1);
	tick(w, AOS_IN_D, 1); /* t=22 */
	CHECK(w->p[0].state == AOS_ST_DASH, "dash-cancel at t=22 (got %#x)", w->p[0].state);

	/* Scatter-shot: 4 beams 0x96 at t=10,13,16,19 (§3 0x179) */
	setup(w, 0, 400);
	hp0 = w->p[1].hp;
	ga = w->p[0].gaugeA;
	int t0 = spawn_timer(w, 'b', 0x96, 25, AOS_IN_A, &n);
	CHECK(t0 == 10 && n == 4, "Scatter-shot first beam t=10, 4 beams (got t=%d n=%d)", t0, n);
	CHECK(fabsf((ga - w->p[0].gaugeA) - 0.32f) < 1e-3f, "4 x 0.08 ammo A (§1.2)");
}

static void test_rockets(void)
{
	aos_world *w = &W;
	int n;
	setup(w, 0, 400);
	tick(w, AOS_IN_B, 1);
	int t = spawn_timer(w, 'm', 100, 52, AOS_IN_B, &n); /* still inside 0x17b: no regen */
	CHECK(t == 12 && n == 20, "Straight Rockets: first t=12, 20 rockets (§3 0x17b) (got t=%d n=%d)", t, n);
	CHECK(fabsf(w->p[0].gaugeB - (1.0f - 20 * 0.03f)) < 1e-3f, "20 x 0.03 ammo B (§1.2) (B=%f)", (double)w->p[0].gaugeB);

	setup(w, 0, 400);
	tick(w, AOS_IN_B, 1);
	tick(w, 0, 2); /* released -> 0x17a */
	CHECK(w->p[0].state == 0x17a, "B tap -> 0x17a (got %#x)", w->p[0].state);
	t = spawn_timer(w, 'm', 100, 60, 0, &n);
	CHECK(t == 10 && n == 9, "rocket fan: first t=10, 9 rockets (§3 0x17a) (got t=%d n=%d)", t, n);

	/* missile 100 template (§5.2) */
	setup(w, 0, 400);
	tick(w, AOS_IN_B, 1);
	tick(w, AOS_IN_B, 12);
	for (int i = 0; i < w->obj.nmissile; i++) {
		aos_missile *m = &w->obj.missile[i];
		CHECK(m->hp == 120 && m->damage == 150 && m->shapeBody.radius == 16.0f && m->knockback == 10.0f,
		      "rocket 100 hp120 dmg150 r16 knock10 (§5.2)");
	}

	/* Homing Missile Pod: 6 x missile 200 t=10..20 (§3 0x180) */
	setup(w, 0, 400);
	tick(w, AOS_IN_S, 3);
	tick(w, AOS_IN_S | AOS_IN_B, 1);
	CHECK(w->p[0].state == 0x180, "S+B -> 0x180");
	t = spawn_timer(w, 'm', 200, 40, 0, &n);
	CHECK(t == 10 && n == 6, "pod: first t=10, 6 missiles (got t=%d n=%d)", t, n);

	/* Full-Fire Bullet Curtain: 14 x missile 200, no ammo (§3 0x181) */
	setup(w, 0, 400);
	float gb = w->p[0].gaugeB;
	tick(w, AOS_IN_D, 1);
	tick(w, AOS_IN_D | AOS_IN_H, 1);
	t = spawn_timer(w, 'm', 200, 50, 0, &n);
	CHECK(t == 10 && n == 14, "curtain: first t=10, 14 missiles (got t=%d n=%d)", t, n);
	CHECK(w->p[0].gaugeB >= gb, "curtain costs no ammo B");
}

static void test_fields(void)
{
	aos_world *w = &W;
	setup(w, 0, 60);
	float ga = w->p[0].gaugeA;
	tick(w, AOS_IN_S, 3);
	tick(w, AOS_IN_S | AOS_IN_A, 1);
	CHECK(w->p[0].state == 0x17c, "S+A -> 0x17c");
	int n, t = spawn_timer(w, 'e', 0, 20, 0, &n);
	CHECK(t == 10 && n == 1, "field at t=10 (§3 0x17c) (got t=%d n=%d)", t, n);
	CHECK(fabsf((ga - w->p[0].gaugeA) - 0.6f) < 1e-4f, "field costs A 0.6 (§1.2, FUN_004a7270)");
	aos_enemy *e = &w->obj.enemy[0];
	CHECK(e->hp == 400 && e->shape.radius == 36.0f && e->f7c == 100 && e->maxHp == 400, "field template (§5.5)");
	int hp0 = w->p[1].hp, firstHit = -1, ticks = 0, pushedAt = -1;
	for (int f = 0; f < 260 && w->obj.nenemy; f++) {
		int hp = w->p[1].hp;
		if (pushedAt < 0 && w->p[1].state == AOS_ST_HITSTUN && w->p[1].speed >= 9.0f)
			pushedAt = ticks;
		int32_t t2 = w->obj.enemy[0].timer2;
		w->p[1].x = w->obj.enemy[0].pos[0] + 40; /* keep the victim inside r=100 */
		w->p[1].y = w->obj.enemy[0].pos[1];
		tick(w, 0, 1);
		if (w->p[1].hp < hp) {
			ticks++;
			if (firstHit < 0)
				firstHit = t2;
			CHECK(hp - w->p[1].hp == 30, "field tick 30 (§5.3 id 100) (got %d)", hp - w->p[1].hp);
			CHECK(t2 % 10 == 0, "damage only on timer2%%10==0 (got %d)", t2);
		}
	}
	CHECK(firstHit == 20, "first field damage at timer2 20 (§5.5) (got %d)", firstHit);
	/* knock 0 until the caster's combo counter (+0x128) exceeds 11, then knock 10 pushes the
	 * victim out; a victim in hitstun moving faster than 2 is released (dealsDamage=0) (§5.3) */
	CHECK(pushedAt == 12, "push-out (knock 10) after the 12th damaging tick (§5.3) (got %d)", pushedAt);
	CHECK(ticks >= 12 && ticks <= 16 && hp0 - w->p[1].hp == ticks * 30, "12..16 ticks x 30 (§5.5) (got %d, %d)", ticks,
	      hp0 - w->p[1].hp);
	CHECK(w->obj.nenemy == 0, "field removed after phase 900");

	/* not enough ammo A -> no field */
	setup(w, 0, 400);
	w->p[0].gaugeA = 0.5f;
	tick(w, AOS_IN_S, 3);
	tick(w, AOS_IN_S | AOS_IN_A, 1);
	tick(w, 0, 15);
	CHECK(w->obj.nenemy == 0, "0x17c with A < 0.6 spawns nothing");

	/* projected field (0x17d): speed 10 along aim, decays x0.95 (§3 0x17d) */
	setup(w, 0, 400);
	tick(w, AOS_IN_D, 1);
	tick(w, AOS_IN_D | AOS_IN_S, 1);
	tick(w, AOS_IN_D | AOS_IN_S | AOS_IN_A, 1);
	tick(w, AOS_IN_D | AOS_IN_S, 10);
	CHECK(w->obj.nenemy == 1 && w->obj.enemy[0].f7c == 0x6e, "0x17d spawns a 0x6e field");
	if (w->obj.nenemy) {
		float x0 = w->obj.enemy[0].pos[0];
		tick(w, 0, 100);
		float d = w->obj.enemy[0].pos[0] - x0;
		CHECK(d > 150.0f && d < 200.0f, "projected field slides toward the opponent (~200 px) (got %f)", (double)d);
	}

	/* wide-area (0x17e): 4 fields end on radius 200 around the cast point, 4 whips (§5.5) */
	setup(w, 0, 400);
	float cx = w->p[0].x, cy = w->p[0].y;
	tick(w, AOS_IN_S, 3);
	tick(w, AOS_IN_S | AOS_IN_H, 1);
	tick(w, 0, 10);
	CHECK(w->obj.nenemy == 4 && w->obj.nwhip == 4, "0x17e: 4 fields + 4 whips (got %d, %d)", w->obj.nenemy, w->obj.nwhip);
	tick(w, 0, 101);
	for (int i = 0; i < w->obj.nenemy; i++) {
		float dx = w->obj.enemy[i].pos[0] - cx, dy = w->obj.enemy[i].pos[1] - cy;
		CHECK(fabsf(sqrtf(dx * dx + dy * dy) - 200.0f) < 1.0f, "wide field at radius 200 (got %f)", (double)sqrtf(dx * dx + dy * dy));
		CHECK(w->obj.enemy[i].active == 1, "wide field active after the spiral");
	}

	/* D+S+H: 4 moving fields at aim ±0.35, ±0.75 (§3 0x17f) */
	setup(w, 0, 400);
	tick(w, AOS_IN_D, 1);
	tick(w, AOS_IN_D | AOS_IN_S, 1);
	tick(w, AOS_IN_D | AOS_IN_S | AOS_IN_H, 1);
	tick(w, 0, 10);
	CHECK(w->obj.nenemy == 4, "0x17f: 4 fields");
	for (int i = 0; i < w->obj.nenemy; i++)
		CHECK(w->obj.enemy[i].f7c == 0x6e && (fabsf(fabsf(w->obj.enemy[i].angle2) - 0.35f) < 1e-3f || fabsf(fabsf(w->obj.enemy[i].angle2) - 0.75f) < 1e-3f),
		      "0x17f field angle aim±0.35/0.75 (got %f)", (double)w->obj.enemy[i].angle2);
}

static void test_rays(void)
{
	aos_world *w = &W;
	/* Linear Ray: laser 100 at t=6, 9 ticks x 90 = 810 (§5.4) */
	setup(w, 0, 400);
	int hp0 = w->p[1].hp, n;
	tick(w, AOS_IN_H, 1);
	CHECK(w->p[0].state == 0x182, "H -> 0x182");
	int t = spawn_timer(w, 'l', 100, 10, 0, &n);
	CHECK(t == 6, "Linear Ray laser at t=6 (§3 0x182) (got %d)", t);
	CHECK(w->p[0].barrierType == 3, "hyper start FUN_006b1b40(10)");
	tick(w, 0, 90);
	CHECK(hp0 - w->p[1].hp == 810, "Linear Ray 9 x 90 = 810 (§5.4) (got %d)", hp0 - w->p[1].hp);
	CHECK(count_type(w, 'l', 100) == 0, "ray removed at age 70");

	/* Destruction Ray (Accel): laser 200 at t=5, 13 ticks x 140 = 1820 (§5.4) */
	setup(w, 0, 400);
	hp0 = w->p[1].hp;
	tick(w, AOS_IN_S | AOS_IN_B, 3);
	tick(w, AOS_IN_S | AOS_IN_B | AOS_IN_H, 1);
	CHECK(w->p[0].state == 0x184 && w->p[0].barrierType == 4, "accel 0x184, barrier kind 0x14");
	CHECK(fabsf(w->p[0].hyper) < 1e-4f, "accel costs 3.0 hyper");
	t = spawn_timer(w, 'l', 200, 10, 0, &n);
	CHECK(t == 5, "Destruction Ray laser at t=5 (got %d)", t);
	tick(w, 0, 100);
	CHECK(hp0 - w->p[1].hp == 1820, "Destruction Ray 13 x 140 = 1820 (§5.4) (got %d)", hp0 - w->p[1].hp);
	CHECK(w->p[0].state == AOS_ST_NEUTRAL, "0x184 ended (t>100)");
}

static void test_genocide(void)
{
	aos_world *w = &W;
	setup(w, 0, 300);
	tick(w, AOS_IN_B, 3);
	tick(w, AOS_IN_B | AOS_IN_H, 1);
	CHECK(w->p[0].state == 0x183, "B+H -> 0x183");
	CHECK(w->p[0].barrierTime == 49 && w->p[0].barrierBreakT == 25, "barrier halved 100->50 (now 49), +0x108=25 (§3 0x183) (got %d %d)",
	      w->p[0].barrierTime, w->p[0].barrierBreakT);
	int n, t = spawn_timer(w, 'm', 400, 20, 0, &n);
	CHECK(t == 15 && n == 1, "bomb (missile 400) at t=15 (got %d)", t);
	for (int i = 0; i < w->obj.nmissile; i++)
		if (w->obj.missile[i].id == 400)
			CHECK(w->obj.missile[i].hp == 540 && w->obj.missile[i].damage == 400 && w->obj.missile[i].speed > 4.9f,
			      "bomb template hp540 dmg400 speed 5 (§5.2)");

	/* team-3 blast hits Iru herself once her bubble is gone (§0.2, §5.6) */
	setup(w, 0, 1000);
	w->p[1].x = 2000; /* opponent out of the way */
	w->p[0].barrierTime = 0;
	int hp0 = w->p[0].hp;
	aos_spawn_effect(w, AOS_CH_IRU, 200, 0, &w->p[0].x, 0, w->p[0].objId, 0, w->p[0].side);
	int ticks = 0;
	for (int f = 0; f < 60; f++) {
		int hp = w->p[0].hp;
		tick(w, 0, 1);
		if (w->p[0].hp < hp)
			ticks++;
	}
	CHECK(hp0 - w->p[0].hp > 0, "Genocide blast (team 3) damages its own caster");
	CHECK(ticks <= 13, "at most 13 damage ticks (ages 2,5,..,38) (got %d)", ticks);
}

static void test_random_hyper(void)
{
	uint32_t s;
	for (int run = 0; run < 2; run++) {
		aos_world *w = run ? &W2 : &W;
		setup(w, run, 300); /* both schemes */
		s = 99;
		uint16_t m = 0;
		for (int f = 0; f < 15000; f++) {
			s = s * 1664525u + 1013904223u;
			if (((s >> 8) & 3) == 0)
				m ^= (uint16_t)(1u << ((s >> 12) % 10));
			w->p[0].hyper = 3.0f;
			w->p[0].hp = w->p[0].maxHp, w->p[1].hp = w->p[1].maxHp;
			tick(w, m, 1);
			if (!isfinite(w->p[0].x)) {
				CHECK(0, "non-finite at %d", f);
				return;
			}
		}
		/* scheme B + forced full gauge re-fires Wide-Area every 20 frames (S+H held quirk):
		 * the original's std::vector grows without limit; only scheme A must stay in the pools */
		if (run == 0)
			CHECK(w->obj.overflow == 0, "pool overflow %d (scheme %d)", w->obj.overflow, run);
	}
}

int main(void)
{
	aos_register_all();
	test_dispatch();
	test_rifle();
	test_rockets();
	test_fields();
	test_rays();
	test_genocide();
	test_random_hyper();
	printf("test_iru: %s (%d failures)\n", fails ? "FAIL" : "ok", fails);
	return fails != 0;
}
