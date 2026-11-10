/* Behaviour checks for the Kae port (src/sim/chars/kae.c) against docs/re/kae.md.
 * Every check cites the spec section / original address.
 * Build: see tests/run_tests.sh (all src/sim and src/sim/chars sources + this file). */
#include "../src/sim/draw.h"
#include "../src/sim/sim.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
#define NEAR(a, b) (fabsf((float)(a) - (float)(b)) < 1e-4f)

static aos_world W, W2;
static const uint16_t NONE[2] = {0, 0};

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

/* Kae (P1) vs Kyoko (P2, idle), past the round-start input lock. dx = P2 x - P1 x. */
static void setup(aos_world *w, int scheme, float dx)
{
	int32_t ids[2] = {AOS_CH_KAE, AOS_CH_KYOKO}, sch[2] = {scheme, 0}, col[2] = {0, 1};
	aos_match_init(w, ids, sch, col, 999, 2);
	for (int i = 0; i < 210; i++)
		aos_tick(w, NONE);
	w->p[0].x = -dx * 0.5f; w->p[0].y = 0;
	w->p[1].x = dx * 0.5f;  w->p[1].y = 0;
	w->p[0].speed = w->p[1].speed = 0;
	aos_tick(w, NONE); /* settle facing */
}

static void tick1(aos_world *w, uint16_t m1)
{
	uint16_t m[2] = {m1, 0};
	aos_tick(w, m);
}

/* Press `keys` for one frame from neutral; return the state entered. */
static int32_t press(aos_world *w, uint16_t keys)
{
	tick1(w, keys);
	return w->p[0].state;
}

/* ---- §2 input dispatch (FUN_006e0490 / FUN_006e09c0) ---- */
static void test_dispatch(void)
{
	static const struct { uint16_t keys; float hyper; int blaze; int32_t want; const char *what; } a[] = {
		{AOS_IN_A, 0, 0, 0x1b4, "A -> Slash! 0x1b4 (§2 B7)"},
		{AOS_IN_B, 0, 0, 0x1b7, "B -> Pow! 0x1b7 (§2 B5)"},
		{AOS_IN_B, 0, 1, 0x1bb, "B while Blazing -> Fooom! 0x1bb (§2 B5, +0xaa)"},
		{AOS_IN_S | AOS_IN_A, 0, 0, 0x1bc, "S+A -> Zooooom! 0x1bc (§2 B4)"},
		{AOS_IN_S | AOS_IN_B, 0, 0, 0x1c0, "S+B -> Baaang! 0x1c0 (§2 B3)"},
		{AOS_IN_H, 1, 0, 0x1c3, "H -> ZOOOOOM! 0x1c3 (§2 A5)"},
		{AOS_IN_B | AOS_IN_H, 1, 0, 0x1c1, "B+H -> BA-BA-BAANG! 0x1c1 (§2 A3)"},
		{AOS_IN_S | AOS_IN_H, 1, 0, 0x1b9, "S+H -> Blazing! 0x1b9 (§2 A2)"},
		{AOS_IN_S | AOS_IN_H, 1, 1, 0x1c4, "S+H while Blazing -> Whaaam! 0x1c4 (§2 A2)"},
		{AOS_IN_S | AOS_IN_B | AOS_IN_H, 3, 0, 0x1c6, "S+B+H (3 lv) -> WHAAAAM! 0x1c6 (§2 A1)"},
		{AOS_IN_S | AOS_IN_B | AOS_IN_H, 1, 0, 0x1b9, "S+B+H with 1 lv falls to row 2 (§2 A)"},
		{AOS_IN_H, 0, 0, 0, "H with no gauge does nothing (FUN_00780910)"},
	};
	for (size_t k = 0; k < sizeof a / sizeof a[0]; k++) {
		aos_world *w = &W;
		setup(w, 0, 400);
		w->p[0].hyper = a[k].hyper;
		if (a[k].blaze)
			aos_status(w, &w->p[0], AOS_STATUS_OVERHEAT, 1000);
		int32_t s = press(w, a[k].keys);
		CHECK(s == a[k].want, "%s (got %#x)", a[k].what, (unsigned)s);
		if (a[k].want >= 0x1b8 && a[k].want != 0x1bb && a[k].want != 0x1bc && a[k].want != 0x1c0)
			CHECK(NEAR(w->p[0].hyper, a[k].hyper - (a[k].want == 0x1c6 ? 3.0f : 1.0f)),
			      "%s: hyper spent (%.3f left)", a[k].what, w->p[0].hyper);
	}
	/* D+A / D+H: Dash must be held (> 1) on the press frame (§2 B6, A4) */
	{
		aos_world *w = &W;
		setup(w, 0, 400);
		tick1(w, AOS_IN_D);              /* dash starts (state 2) */
		int32_t s = press(w, AOS_IN_D | AOS_IN_A);
		CHECK(s == 0x1b5, "D held + A -> Sla-slaaaash! 0x1b5 (§2 B6) (got %#x)", (unsigned)s);
		setup(w, 0, 400);
		w->p[0].hyper = 1;
		tick1(w, AOS_IN_D);
		s = press(w, AOS_IN_D | AOS_IN_H);
		CHECK(s == 0x1b8, "D held + H -> POW! 0x1b8 (§2 A4) (got %#x)", (unsigned)s);
	}
	/* scheme B: H held + X pressed (FUN_006e09c0) */
	static const struct { uint16_t x; int32_t want; } b[] = {
		{AOS_IN_A, 0x1c3}, {AOS_IN_B, 0x1c1}, {AOS_IN_S, 0x1b9}, {AOS_IN_SHIELD, 0x1c6}, {AOS_IN_D, 0x1b8},
	};
	for (size_t k = 0; k < sizeof b / sizeof b[0]; k++) {
		aos_world *w = &W;
		setup(w, 1, 400);
		w->p[0].hyper = 3;
		tick1(w, AOS_IN_H);                 /* H pressed (no gauge use: row needs H held + X) */
		CHECK(w->p[0].state == 0x1c3 || w->p[0].state == 0, "scheme B: lone H press");
		setup(w, 1, 400);
		w->p[0].hyper = 3;
		w->p[0].H = 2;                      /* H already held */
		uint16_t m[2] = {(uint16_t)(AOS_IN_H | b[k].x), 0};
		aos_tick(w, m);
		CHECK(w->p[0].state == b[k].want, "scheme B: H held + %#x -> %#x (§2 alt) (got %#x)",
		      b[k].x, (unsigned)b[k].want, (unsigned)w->p[0].state);
	}
}

/* Count spawns in a family by serial counter and record the state timers. */
typedef struct { int n; int t[32]; } spawns;

static void run_state(aos_world *w, int frames, spawns *beams, spawns *missiles, spawns *enemies, int *endT)
{
	*endT = -1;
	int32_t s0 = w->p[0].state;
	for (int f = 0; f < frames; f++) {
		int t = w->p[0].timer;
		int32_t bs = w->obj.beamSerial, ms = w->obj.missileSerial, es = w->obj.enemySerial;
		aos_tick(w, NONE);
		draw_all(w);
		if (beams) for (int32_t k = bs; k < w->obj.beamSerial && beams->n < 32; k++) beams->t[beams->n++] = t;
		if (missiles) for (int32_t k = ms; k < w->obj.missileSerial && missiles->n < 32; k++) missiles->t[missiles->n++] = t;
		if (enemies) for (int32_t k = es; k < w->obj.enemySerial && enemies->n < 32; k++) enemies->t[enemies->n++] = t;
		if (*endT < 0 && w->p[0].state != s0)
			*endT = t;
	}
}

/* ---- §3 states: spawn frames, costs, end frames ---- */
static void test_slash(void)
{
	aos_world *w = &W;
	setup(w, 0, 300);
	press(w, AOS_IN_A);
	spawns b = {0};
	int end, hp0 = w->p[1].hp;
	run_state(w, 60, &b, NULL, NULL, &end);
	CHECK(b.n == 5 && b.t[0] == 12 && b.t[4] == 20, "Slash!: 5 beams at t=12..20 even (§3 0x1b4) (n=%d first=%d last=%d)", b.n, b.t[0], b.t[b.n ? b.n - 1 : 0]);
	CHECK(end == 36, "Slash!: state 0 once t>35, i.e. at t=36 (§3 0x1b4) (ended at t=%d)", end);
	CHECK(hp0 - w->p[1].hp == 500, "Slash!: 5 hits x 100 when not Blazing (§5.1 beam 100, ver>0x58b) (got %d)", hp0 - w->p[1].hp);
	CHECK(NEAR(aos_tab.cost[0x1b4].a, 0.1f) && NEAR(aos_tab.cost[0x1b4].hyperGain, 0.011f), "cost 0x1b4 = {0.1,0,0.011} (§3 table, FUN_004a7270)");

	/* Blazing: damage stays 120 (spawn default) — §1.1 */
	setup(w, 0, 300);
	aos_status(w, &w->p[0], AOS_STATUS_OVERHEAT, 1000);
	press(w, AOS_IN_A);
	run_state(w, 30, &b, NULL, NULL, &end);
	for (int i = 0; i < w->obj.nbeam; i++)
		CHECK(w->obj.beam[i].damage == 120, "Blazing Slash! beam damage 120 (§5.1)");

	/* dash-cancel from t=28 (§3 0x1b4) */
	setup(w, 0, 300);
	press(w, AOS_IN_A);
	for (int i = 0; i < 26; i++) tick1(w, 0);
	tick1(w, AOS_IN_D); /* t=27: refused */
	CHECK(w->p[0].state == 0x1b4, "no dash-cancel at t=27 (§3 0x1b4)");
	tick1(w, 0);
	tick1(w, AOS_IN_D); /* t=29 (>= 28) */
	CHECK(w->p[0].state == AOS_ST_DASH, "dash-cancel at t>=28 (§3 0x1b4) (state %#x)", (unsigned)w->p[0].state);
}

static void test_slaslash(void)
{
	aos_world *w = &W;
	setup(w, 0, 300);
	w->p[0].state = 0;
	aos_set_state(w, &w->p[0], 0x1b5);
	tick1(w, 0);
	spawns b = {0};
	int end;
	b.n = 0;
	run_state(w, 20, &b, NULL, NULL, &end);
	CHECK(NEAR(w->p[0].gaugeA, 1.0f - 9 * 0.05f), "9 x 0.05 A, no regen inside the state (§3 table) (A=%.3f)", w->p[0].gaugeA);
	run_state(w, 40, &b, NULL, NULL, &end);
	CHECK(b.n == 9 && b.t[0] == 10 && b.t[8] == 18, "Sla-slaaaash!: 9 beams t=10..18 (§3 0x1b5) (n=%d)", b.n);
	CHECK(end == 41, "Sla-slaaaash!: ends once t>40 (ver>=0x5a1) (got %d)", end);
}

static void test_missiles(void)
{
	aos_world *w = &W;
	spawns m = {0};
	int end;
	/* B tap: 0x1b7 -> 0x1b6 on release before t=11, 6 missiles t=12..22 (§3 0x1b6) */
	setup(w, 0, 400);
	press(w, AOS_IN_B);
	run_state(w, 24, NULL, &m, NULL, &end);
	CHECK(NEAR(w->p[0].gaugeB, 1.0f - 0.6f), "B tap: 6 x 0.1 B (B=%.3f)", w->p[0].gaugeB);
	run_state(w, 16, NULL, &m, NULL, &end);
	CHECK(m.n == 6 && m.t[0] == 12 && m.t[5] == 22, "B tap: 6 missiles t=12..22 (§3 0x1b6) (n=%d)", m.n);
	/* B hold: pairs at t=18..28 (§3 0x1b7) */
	setup(w, 0, 400);
	m.n = 0;
	tick1(w, AOS_IN_B);
	for (int f = 0; f < 50; f++) {
		int t = w->p[0].timer, ms = w->obj.missileSerial;
		tick1(w, AOS_IN_B);
		for (int k = ms; k < w->obj.missileSerial && m.n < 32; k++) m.t[m.n++] = t;
	}
	CHECK(w->p[0].state == 0x1b7 || w->p[0].state == 0, "B hold stays in 0x1b7");
	CHECK(m.n == 12 && m.t[0] == 18 && m.t[11] == 28, "Pow!: 6 pairs t=18..28 (§3 0x1b7) (n=%d)", m.n);
	/* missile parameters (§5.2) */
	setup(w, 0, 400);
	aos_spawn_missile(w, AOS_CH_KAE, 100, &w->p[0].x, 0, w->p[0].objId, 0, 1, 0);
	aos_missile *q = &w->obj.missile[w->obj.nmissile - 1];
	CHECK(q->damage == 40 && q->hp == 60 && NEAR(q->speed, 12) && NEAR(q->shapeBody.radius, 10) && q->growScale == 0,
	      "missile 100: dmg 40, hp 60, speed 12, r 10, grow 0 (§5.2)");
}

static void test_combo(void)
{
	aos_world *w = &W;
	setup(w, 0, 100);
	int hp0 = w->p[1].hp;
	press(w, AOS_IN_S | AOS_IN_A);
	CHECK(w->p[0].state == 0x1bc, "S+A near -> 0x1bc");
	for (int i = 0; i < 11; i++) tick1(w, 0); /* hit at t=10 */
	CHECK(w->p[0].hitFlag == 1, "hitbox 100 sets hit-confirm (§5.3, FUN_005e39d0)");
	CHECK(hp0 - w->p[1].hp == 100, "hit 1: 100 damage (§5.3 id 100, ver>=0x58c) (got %d)", hp0 - w->p[1].hp);
	tick1(w, AOS_IN_A);
	CHECK(w->p[0].state == 0x1bd, "hit-confirm + A at t>=12 -> 0x1bd (§3 0x1bc) (got %#x)", (unsigned)w->p[0].state);
	for (int i = 0; i < 11; i++) tick1(w, 0); /* entered at t=0 inside the tick; A at t=12 */
	tick1(w, AOS_IN_A);
	CHECK(w->p[0].state == 0x1be, "-> 0x1be (§3 0x1bd) (got %#x)", (unsigned)w->p[0].state);
	for (int i = 0; i < 11; i++) tick1(w, 0);
	tick1(w, AOS_IN_A);
	CHECK(w->p[0].state == 0x1bf, "-> 0x1bf (§3 0x1be) (got %#x)", (unsigned)w->p[0].state);
	int t0 = -1;
	for (int i = 0; i < 60 && t0 < 0; i++) {
		int t = w->p[0].timer;
		tick1(w, AOS_IN_D);
		if (w->p[0].state != 0x1bf) t0 = t;
	}
	CHECK(t0 == 0x23, "0x1bf: no dash-cancel, ends after t>34 (§3 0x1bf) (left at t=%d)", t0);
}

static void test_blazing(void)
{
	aos_world *w = &W;
	/* Blazing! (0x1b9): status 7 for 1000 at t=20, knock within 160 (§3 0x1b9) */
	setup(w, 0, 140);
	w->p[0].hyper = 1;
	press(w, AOS_IN_S | AOS_IN_H);
	for (int i = 0; i < 20; i++) tick1(w, 0);
	CHECK(w->p[0].statusOn[AOS_STATUS_OVERHEAT] == 1, "Blazing! sets status 7 at t=20 (§1.1)");
	CHECK(w->p[0].statusTime[AOS_STATUS_OVERHEAT] >= 998, "Blazing duration 1000 (got %d)", (int)w->p[0].statusTime[AOS_STATUS_OVERHEAT]);
	CHECK(w->p[0].heat >= 2.0f, "Blazing floors heat at 2.01 (FUN_00751750) then the -0.005 decay of non-neutral states applies (heat %.3f)", w->p[0].heat);
	CHECK(w->p[1].state == AOS_ST_HITSTUN, "Blazing! knocks an opponent within 160 (FUN_006b1d70)");
	/* Dash hook: heat >= 2.0 -> Blazing 500 (§1.1, FUN_006dbd20) */
	setup(w, 0, 400);
	w->p[0].heat = 2.0f;
	tick1(w, AOS_IN_D);
	CHECK(w->p[0].statusOn[AOS_STATUS_OVERHEAT] == 1 && w->p[0].statusTime[AOS_STATUS_OVERHEAT] <= 500,
	      "dash with heat >= 2.0 enters Blazing for 500 (§1.1)");
	setup(w, 0, 400);
	w->p[0].heat = 1.5f;
	tick1(w, AOS_IN_D);
	CHECK(w->p[0].statusOn[AOS_STATUS_OVERHEAT] == 0, "dash at heat < 2.0 (after +0.3 = 1.8) does not");
	/* Fooom! field: opponent within 100 gets status 7 (§5.5 id 500) */
	setup(w, 0, 80);
	aos_status(w, &w->p[0], AOS_STATUS_OVERHEAT, 1000);
	press(w, AOS_IN_B);
	CHECK(w->p[0].state == 0x1bb, "Blazing B -> 0x1bb");
	for (int i = 0; i < 25; i++) tick1(w, AOS_IN_B);
	CHECK(w->p[1].statusOn[AOS_STATUS_OVERHEAT] == 1, "fire field forces status 7 on the opponent within 100 (§5.5)");
	CHECK(NEAR(w->p[0].gaugeB, 0.5f), "Fooom! costs 0.5 B (§3 table) (B=%.3f)", w->p[0].gaugeB);
}

static void test_grabs(void)
{
	aos_world *w = &W;
	/* Whaaam! (0x1c4): grab within 70 between t=10..20 -> 0x1c5 pin (§3 0x1c4/0x1c5, §1.2) */
	setup(w, 0, 150);
	w->p[0].hyper = 1;
	aos_status(w, &w->p[0], AOS_STATUS_OVERHEAT, 1000);
	press(w, AOS_IN_S | AOS_IN_H);
	CHECK(w->p[0].state == 0x1c4, "Blazing S+H -> 0x1c4");
	int hp0 = w->p[1].hp;
	for (int i = 0; i < 25 && w->p[0].state == 0x1c4; i++) tick1(w, 0);
	CHECK(w->p[0].state == 0x1c5, "grab -> 0x1c5 (got %#x)", (unsigned)w->p[0].state);
	tick1(w, 0);
	float d = sqrtf((w->p[1].x - w->p[0].x) * (w->p[1].x - w->p[0].x) + (w->p[1].y - w->p[0].y) * (w->p[1].y - w->p[0].y));
	CHECK(w->p[1].state == AOS_ST_HITSTUN && d > 30.0f && d < 41.0f, "opponent pinned 40 px ahead in hitstun (§1.2; Kae then moves ~3 px by her own residual speed) (d=%.2f)", d);
	for (int i = 0; i < 130; i++) tick1(w, 0);
	/* 4x100 + 3x60 + 150 (§3 0x1c5); the victim's heat stays at 0 (no multiplier) */
	CHECK(hp0 - w->p[1].hp == 730, "Whaaam! total 730 (§3 0x1c5) (got %d)", hp0 - w->p[1].hp);

	/* WHAAAAM! (0x1c6 -> 0x1c7): 1500 blast at t=75 (§3 0x1c7, §5.3 id 500) */
	setup(w, 0, 150);
	w->p[0].hyper = 3;
	press(w, AOS_IN_S | AOS_IN_B | AOS_IN_H);
	for (int i = 0; i < 25 && w->p[0].state == 0x1c6; i++) tick1(w, 0);
	CHECK(w->p[0].state == 0x1c7, "Accel grab -> 0x1c7 (got %#x)", (unsigned)w->p[0].state);
	hp0 = w->p[1].hp;
	for (int i = 0; i < 110; i++) tick1(w, 0);
	CHECK(hp0 - w->p[1].hp == 1500, "WHAAAAM! blast 1500 (ver>0x5a0) (got %d)", hp0 - w->p[1].hp);
}

static void test_clones(void)
{
	aos_world *w = &W;
	setup(w, 0, 150); /* clone 100 rush: speed min(dist/12,15), strike 40 ahead at t2==10, r 60 (§5.4) */
	press(w, AOS_IN_S | AOS_IN_B);
	spawns e = {0};
	int end;
	run_state(w, 16, NULL, NULL, &e, &end);
	CHECK(NEAR(w->p[0].gaugeB, 0.75f), "clone costs 0.25 B (§3 table) (B=%.3f)", w->p[0].gaugeB);
	run_state(w, 34, NULL, NULL, &e, &end);
	CHECK(e.n == 1 && e.t[0] == 15, "Baaang!: one clone at t=15 (§3 0x1c0) (n=%d t=%d)", e.n, e.n ? e.t[0] : -1);
	/* clone 100 strikes: hitbox 200 (180 dmg) ~121 frames after spawn (§5.4) */
	int hp0 = w->p[1].hp, hitAt = -1;
	for (int f = 0; f < 200 && hitAt < 0; f++) {
		tick1(w, 0);
		if (w->p[1].hp != hp0) hitAt = f;
	}
	CHECK(hp0 - w->p[1].hp == 180, "clone hit 180 (§5.3 id 200) (got %d)", hp0 - w->p[1].hp);
	/* Blazing: clones at t=10 and t=15 (§3 0x1c0) */
	setup(w, 0, 300);
	aos_status(w, &w->p[0], AOS_STATUS_OVERHEAT, 1000);
	press(w, AOS_IN_S | AOS_IN_B);
	e.n = 0;
	run_state(w, 50, NULL, NULL, &e, &end);
	CHECK(e.n == 2 && e.t[0] == 10 && e.t[1] == 15, "Blazing Baaang!: clones at t=10 and 15 (n=%d)", e.n);
	/* BA-BA-BAANG!: 9 clones, then 0x1c2 at t=50 (§3 0x1c1) */
	setup(w, 0, 300);
	w->p[0].hyper = 1;
	int es0 = w->obj.enemySerial;
	press(w, AOS_IN_B | AOS_IN_H);
	e.n = w->obj.enemySerial - es0; /* t=0 clone spawns in the press tick */
	int last = -1;
	for (int f = 0; f < 60; f++) {
		int t = w->p[0].timer, es = w->obj.enemySerial;
		tick1(w, 0);
		for (int k = es; k < w->obj.enemySerial; k++) e.n++;
		if (w->p[0].state == 0x1c2 && last < 0) last = t;
	}
	CHECK(e.n == 9, "BA-BA-BAANG!: 9 clones (got %d)", e.n);
	CHECK(last == 50, "BA-BA-BAANG! -> 0x1c2 at t=50 (got %d)", last);
}

static void test_zoooom(void)
{
	aos_world *w = &W;
	setup(w, 0, 150);
	w->p[0].hyper = 1;
	int hp0 = w->p[1].hp;
	press(w, AOS_IN_H);
	for (int i = 0; i < 90; i++) tick1(w, 0);
	/* 6 x 80 (t=12..27, t%3==0) + 120 at t=30 if everything connects (§3 0x1c3, §5.3) */
	int dmg = hp0 - w->p[1].hp;
	CHECK(dmg > 0 && dmg <= 6 * 80 + 120 && dmg % 40 == 0, "ZOOOOOM! damage in 80/120 units (got %d)", dmg);
	CHECK(w->p[1].heat >= 0.1f - 1e-4f || dmg < 600, "finisher 0x136 adds +0.1 heat to the victim (§5.3) (heat %.3f)", w->p[1].heat);
}

static void test_random_hyper(void)
{
	uint32_t s;
	for (int run = 0; run < 2; run++) {
		aos_world *w = run ? &W2 : &W;
		setup(w, 0, 300);
		uint16_t m[2] = {0, 0};
		s = 4242;
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
				CHECK(0, "non-finite at frame %d", f);
				return;
			}
		}
	}
	CHECK(!memcmp(&W, &W2, sizeof W), "random hyper runs diverged");
	CHECK(W.obj.overflow == 0, "pool overflow %d", (int)W.obj.overflow);
}

int main(void)
{
	aos_register_all();
	test_dispatch();
	test_slash();
	test_slaslash();
	test_missiles();
	test_combo();
	test_blazing();
	test_grabs();
	test_clones();
	test_zoooom();
	test_random_hyper();
	printf("test_kae: %s (%d/%d checks failed)\n", fails ? "FAIL" : "ok", fails, checks);
	return fails != 0;
}
