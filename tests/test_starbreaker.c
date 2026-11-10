/* Behaviour tests for the Star Breaker port (src/sim/chars/starbreaker.c).
 * Every assertion cites docs/re/starbreaker.md (§) and/or the original address.
 * Build: see tests/run_tests.sh. */
#include "../src/sim/draw.h"
#include "../src/sim/sim.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static aos_world W, W2;

static void draw_all(aos_world *w)
{
	aos_draw_begin();
	aos_draw_players(w);
	for (int i = 0; i < w->obj.nmissile; i++)
		if (aos_otab.missileDraw[w->obj.missile[i].type]) aos_otab.missileDraw[w->obj.missile[i].type](w, &w->obj.missile[i]);
	for (int i = 0; i < w->obj.nlaser; i++)
		if (aos_otab.laserDraw[w->obj.laser[i].type]) aos_otab.laserDraw[w->obj.laser[i].type](w, &w->obj.laser[i]);
	for (int i = 0; i < w->obj.nenemy; i++)
		if (aos_otab.enemyDraw[w->obj.enemy[i].type]) aos_otab.enemyDraw[w->obj.enemy[i].type](w, &w->obj.enemy[i]);
	for (int i = 0; i < w->obj.neffect; i++)
		if (aos_otab.effectDraw[w->obj.effect[i].type]) aos_otab.effectDraw[w->obj.effect[i].type](w, &w->obj.effect[i]);
	for (int i = 0; i < w->obj.nwhip; i++)
		if (aos_otab.whipDraw[w->obj.whip[i].type]) aos_otab.whipDraw[w->obj.whip[i].type](w, &w->obj.whip[i]);
}

/* P1 = Star Breaker at (x1,0), P2 = opp at (x2,0), past the input lock (frame >= 200). */
static void setup(aos_world *w, int opp, int scheme, float x1, float x2)
{
	int32_t ids[2] = {AOS_CH_STARBREAKER, opp}, sch[2] = {scheme, 0}, col[2] = {0, 1};
	aos_match_init(w, ids, sch, col, 999, 2);
	uint16_t m[2] = {0, 0};
	for (int i = 0; i < 210; i++)
		aos_tick(w, m);
	w->p[0].x = x1; w->p[0].y = 0;
	w->p[1].x = x2; w->p[1].y = 0;
	w->p[0].gaugeA = w->p[0].gaugeB = 1.0f;
	w->p[0].heat = w->p[1].heat = 0;
	w->p[0].hyper = 0;
}

static void tick1(aos_world *w, uint16_t m0, uint16_t m1)
{
	uint16_t m[2] = {m0, m1};
	aos_tick(w, m);
	draw_all(w);
}
static void idle(aos_world *w, int n) { for (int i = 0; i < n; i++) tick1(w, 0, 0); }

/* ---------------------------------------------------------------- input dispatch (§2) */

typedef struct { const char *name; uint16_t pre, press; int hyper; int state; } mapcase;

static void test_dispatch(void)
{
	/* §2 standard scheme FUN_0074b900; "pre" is held one frame before "press" (makes it value 2) */
	static const mapcase a[] = {
		{"A", 0, AOS_IN_A, 0, 0x113},
		{"S+A", AOS_IN_S, AOS_IN_S | AOS_IN_A, 0, 0x116},
		{"B", 0, AOS_IN_B, 0, 0x10f},
		{"S+B", AOS_IN_S, AOS_IN_S | AOS_IN_B, 0, 0x110},
		{"D+A", AOS_IN_D, AOS_IN_D | AOS_IN_A, 0, 0x10c},
		{"H", 0, AOS_IN_H, 1, 0x11b},
		{"S+H", AOS_IN_S, AOS_IN_S | AOS_IN_H, 1, 0x119},
		{"B+H", AOS_IN_B, AOS_IN_B | AOS_IN_H, 1, 0x10e},
		{"D+H", AOS_IN_D, AOS_IN_D | AOS_IN_H, 1, 0x111},
		{"S+B+H", AOS_IN_S | AOS_IN_B, AOS_IN_S | AOS_IN_B | AOS_IN_H, 3, 0x11d},
		{"S+B+H with 1 level -> S+H", AOS_IN_S | AOS_IN_B, AOS_IN_S | AOS_IN_B | AOS_IN_H, 1, 0x119},
	};
	/* §2 alternate scheme FUN_0074bdc0: H held + X pressed */
	static const mapcase b[] = {
		{"H+A", AOS_IN_H, AOS_IN_H | AOS_IN_A, 1, 0x11b},
		{"H+S", AOS_IN_H, AOS_IN_H | AOS_IN_S, 1, 0x119},
		{"H+B", AOS_IN_H, AOS_IN_H | AOS_IN_B, 1, 0x10e},
		{"H+D", AOS_IN_H, AOS_IN_H | AOS_IN_D, 1, 0x111},
		{"H+Shield", AOS_IN_H, AOS_IN_H | AOS_IN_SHIELD, 3, 0x11d},
		{"A (no hyper)", 0, AOS_IN_A, 0, 0x113},
	};
	for (int sch = 0; sch < 2; sch++) {
		const mapcase *t = sch ? b : a;
		int n = sch ? (int)(sizeof b / sizeof b[0]) : (int)(sizeof a / sizeof a[0]);
		for (int k = 0; k < n; k++) {
			aos_world *w = &W;
			setup(w, AOS_CH_SUGURI, sch, -200, 200);
			w->p[0].hyper = (float)t[k].hyper;
			if (t[k].pre)
				tick1(w, t[k].pre, 0);
			tick1(w, t[k].press, 0);
			CHECK(w->p[0].state == t[k].state, "scheme %d %s -> %#x (got %#x)", sch, t[k].name, t[k].state, w->p[0].state);
			if (t[k].hyper)
				CHECK(fabsf(w->p[0].hyper - (float)(t[k].hyper - (t[k].state == 0x11d ? 3 : 1))) < 1e-4f,
				      "scheme %d %s hyper cost (left %.3f)", sch, t[k].name, w->p[0].hyper);
		}
	}
}

/* ---------------------------------------------------------------- A chains (§3 0x113..0x118, §5.3) */

static int count_hitboxes_spawned_at(aos_world *w, int *frames, int maxn, int nframes)
{
	int n = 0;
	for (int f = 0; f < nframes; f++) {
		int t = w->p[0].timer;
		tick1(w, 0, 0);
		(void)t;
		if (w->obj.nhitbox > 0 && n < maxn) /* hitboxes are cleared at end of tick: count via damage instead */
			frames[n++] = t;
	}
	return n;
}

static void test_chain(bool sa)
{
	aos_world *w = &W;
	setup(w, AOS_CH_SUGURI, 0, -60, 60);
	int hp0 = w->p[1].hp;
	float ga0 = w->p[0].gaugeA;
	uint16_t pre = sa ? AOS_IN_S : 0, press = (uint16_t)(sa ? (AOS_IN_S | AOS_IN_A) : AOS_IN_A);
	if (pre)
		tick1(w, pre, 0);
	tick1(w, press, 0);
	CHECK(w->p[0].state == (sa ? 0x116 : 0x113), "chain start state %#x", w->p[0].state);
	/* run to t=17: hits at t=10, 13, 16 (§3 0x113), each 60 dmg (§5.3 200/0xc9) */
	int dmgAt[3] = {0}, k = 0;
	while (w->p[0].timer <= 16) {
		int t = w->p[0].timer, hp = w->p[1].hp;
		tick1(w, 0, 0);
		if (w->p[1].hp != hp && k < 3) {
			CHECK(t == 10 || t == 13 || t == 16, "chain1 hit at t=%d", t);
			dmgAt[k++] = hp - w->p[1].hp;
		}
	}
	CHECK(k == 3, "chain1: 3 hits (got %d)", k);
	for (int i = 0; i < k; i++)
		CHECK(dmgAt[i] == 60, "chain1 hit %d damage 60 (got %d)", i, dmgAt[i]);
	CHECK(fabsf((ga0 - w->p[0].gaugeA) - 3 * 0.075f) < 1e-4f, "chain1 costs 3 x 0.075 A (cost 0x113, §1.1) got %.4f", ga0 - w->p[0].gaugeA);
	CHECK(w->p[0].hitFlag == 1, "hit-confirm set by hitbox 200/0xc9 (FUN_005eb6c0)");
	/* A press after confirm -> chain 2 (FUN_00748ed0) */
	tick1(w, AOS_IN_A, 0);
	CHECK(w->p[0].state == (sa ? 0x117 : 0x114), "chain2 state (got %#x)", w->p[0].state);
	hp0 = w->p[1].hp;
	while (w->p[0].timer <= 15)
		tick1(w, 0, 0);
	CHECK(hp0 - w->p[1].hp == 120, "chain2 two hits x60 (got %d)", hp0 - w->p[1].hp);
	tick1(w, AOS_IN_A, 0);
	CHECK(w->p[0].state == (sa ? 0x118 : 0x115), "chain3 state (got %#x)", w->p[0].state);
	w->p[1].x = w->p[0].x + 60; w->p[1].y = w->p[0].y; /* 0xd3 knocked the victim back (kb 10) */
	hp0 = w->p[1].hp;
	while (w->p[0].timer <= 15)
		tick1(w, 0, 0);
	CHECK(hp0 - w->p[1].hp == 140, "chain3 hits 60 + 80 (got %d)", hp0 - w->p[1].hp);
	int k2 = sa ? AOS_STATUS_DRAIN : AOS_STATUS_HEAT;
	CHECK(w->p[1].statusOn[k2] == 1, "finisher %s applies status %d (FUN_006b1a40)", sa ? "0x141" : "0xdd", k2);
	CHECK(w->p[1].statusTime[k2] >= 298 && w->p[1].statusTime[k2] <= 300, "status 300 frames (got %d)", w->p[1].statusTime[k2]);
	/* chain 3 ends at t>50 (FUN_00749510) */
	while (w->p[0].state == (sa ? 0x118 : 0x115) && w->p[0].timer < 80)
		tick1(w, 0, 0);
	CHECK(w->p[0].state == 0 && w->p[0].timer == 52, "chain3 ends when t>50 (state %#x t %d)", w->p[0].state, w->p[0].timer);
	(void)count_hitboxes_spawned_at;
}

static void test_chain_cancel(void)
{
	aos_world *w = &W;
	setup(w, AOS_CH_SUGURI, 0, -300, 300); /* out of range: no hit */
	tick1(w, AOS_IN_A, 0);
	while (w->p[0].timer < 24)
		tick1(w, 0, 0);
	tick1(w, AOS_IN_D, 0); /* t=24: no cancel yet */
	CHECK(w->p[0].state == 0x113, "no dash-cancel at t=24");
	tick1(w, 0, 0);
	tick1(w, AOS_IN_D, 0); /* t=26 >= 25 */
	CHECK(w->p[0].state == AOS_ST_DASH, "dash-cancel from t=25 (FUN_00748ed0) got %#x", w->p[0].state);
	/* no hit -> A press does not chain */
	setup(w, AOS_CH_SUGURI, 0, -300, 300);
	tick1(w, AOS_IN_A, 0);
	while (w->p[0].timer < 12)
		tick1(w, 0, 0);
	tick1(w, AOS_IN_A, 0);
	CHECK(w->p[0].state == 0x113, "A without hit-confirm does not chain");
}

/* ---------------------------------------------------------------- B tap / hold (§3 0x10d, 0x10f) */

static void test_b(void)
{
	aos_world *w = &W;
	setup(w, AOS_CH_SUGURI, 0, -200, 200);
	tick1(w, AOS_IN_B, 0);
	tick1(w, 0, 0); /* B = 1 (released) */
	tick1(w, 0, 0); /* B = 0 (< 1) at t=2 < 12 -> 0x10d (FUN_00747d80) */
	CHECK(w->p[0].state == 0x10d, "B tap -> 0x10d (got %#x)", w->p[0].state);
	int spawnT = -1;
	while (w->p[0].state == 0x10d && w->p[0].timer < 40) {
		int t = w->p[0].timer, ne = w->obj.nenemy;
		tick1(w, 0, 0);
		if (spawnT < 0 && w->obj.nenemy >= ne + 2)
			spawnT = t;
	}
	CHECK(spawnT == 14, "B tap bombs at t=14 (got %d)", spawnT);
	CHECK(fabsf(w->p[0].gaugeB - 0.6f) < 0.02f, "B tap costs 0.4 B (got %.3f)", w->p[0].gaugeB);
	/* bomb attaches and explodes 100 frames later for 300 (§5.1, §5.3 0xb4) */
	/* the two bombs fan out at aim±2 rad and home slowly (0.025 rad/frame): against a static
	 * target 400 px away they pass ~±70 px wide and go to 0xcb at the wall (checked with a
	 * trace). So walk the opponent into the first bomb's path instead. */
	int hp0 = w->p[1].hp, attachF = -1, f;
	for (f = 0; f < 400 && hp0 == w->p[1].hp; f++) {
		if (attachF < 0 && f == 40)
			for (int i = 0; i < w->obj.nenemy; i++)
				if (w->obj.enemy[i].f7c == 200) {
					w->p[1].x = w->obj.enemy[i].pos[0] + 20;
					w->p[1].y = w->obj.enemy[i].pos[1];
					break;
				}
		tick1(w, 0, 0);
		for (int i = 0; i < w->obj.nenemy; i++)
			if (w->obj.enemy[i].type == AOS_CH_STARBREAKER && w->obj.enemy[i].f7c == 0xc9 && attachF < 0)
				attachF = f;
	}
	CHECK(attachF >= 0, "bomb attached to the opponent (variant 0xc9)");
	CHECK(hp0 - w->p[1].hp == 300, "bomb explosion 300 damage (got %d)", hp0 - w->p[1].hp);
	CHECK(attachF < 0 || f - attachF == 101, "explodes ~100 frames after attaching (got %d)", f - attachF);

	/* hold: mine at t=12, then every 6 frames while held (FUN_00747d80) */
	setup(w, AOS_CH_SUGURI, 0, -200, 200);
	int spawns[8], ns = 0;
	tick1(w, AOS_IN_B, 0);
	for (int i = 0; i < 40; i++) {
		int t = w->p[0].timer, nm = w->obj.nmissile;
		tick1(w, AOS_IN_B, 0);
		if (w->obj.nmissile > nm && ns < 8)
			spawns[ns++] = t;
	}
	CHECK(ns >= 3 && spawns[0] == 12 && spawns[1] == 12 && spawns[2] == 12,
	      "B hold mines every loop at t=12 (n %d, t %d)", ns, ns ? spawns[0] : -1);
	CHECK(w->obj.nwhip == ns, "one whip ribbon per mine (%d vs %d)", w->obj.nwhip, ns);
	CHECK(w->obj.missile[0].id == 100 && w->obj.missile[0].hp == 180 && w->obj.missile[0].active == 0,
	      "mine template (id 100, hp 180, inactive) §5.2");
}

/* ---------------------------------------------------------------- D+A (§3 0x10c) */

static void test_da(void)
{
	aos_world *w = &W;
	setup(w, AOS_CH_SUGURI, 0, -200, 200);
	tick1(w, AOS_IN_D, 0);
	tick1(w, AOS_IN_D | AOS_IN_A, 0);
	CHECK(w->p[0].state == 0x10c, "D+A -> 0x10c");
	int ts[8], n = 0;
	while (w->p[0].state == 0x10c) {
		int t = w->p[0].timer, nm = w->obj.nmissile;
		tick1(w, 0, 0);
		if (w->obj.nmissile > nm && n < 8)
			ts[n++] = t;
	}
	CHECK(n == 7, "7 missiles (got %d)", n);
	for (int i = 0; i < n; i++)
		CHECK(ts[i] == 10 + 2 * i, "missile %d at t=%d (got %d)", i, 10 + 2 * i, ts[i]);
	CHECK(fabsf((1.0f - w->p[0].gaugeA) - 7 * 0.09f) < 0.02f, "7 x 0.09 A (ver>0x5a0 patch) got %.3f", 1.0f - w->p[0].gaugeA);
}

/* ---------------------------------------------------------------- hypers */

static void test_grab(void)
{
	aos_world *w = &W;
	setup(w, AOS_CH_SUGURI, 0, -100, -40);
	w->p[0].hyper = 1;
	tick1(w, AOS_IN_S, 0);
	tick1(w, AOS_IN_S | AOS_IN_H, 0);
	CHECK(w->p[0].state == 0x119 && w->p[0].barrierTime == 199, "S+H -> 0x119, +0xf8 = 200 (got %#x %d)", w->p[0].state, w->p[0].barrierTime);
	int hp0 = w->p[1].hp, nm = 0, t;
	for (t = 0; t < 30 && w->p[0].state == 0x119; t++)
		tick1(w, 0, 0);
	CHECK(w->p[0].state == 0x11a, "grab -> 0x11a within t 10..20 (got %#x)", w->p[0].state);
	int serial0 = w->obj.missileSerial - 1; /* first missile spawned on the grab frame (t=0) */
	for (int f = 0; f < 140 && w->p[0].state == 0x11a; f++) {
		tick1(w, 0, 0);
		nm = w->obj.missileSerial - serial0;
		if (w->p[0].timer < 80)
			CHECK(w->p[1].state == AOS_ST_HITSTUN, "victim pinned in state 1");
	}
	CHECK(nm == 7, "7 missiles 0x96 (got %d)", nm);
	CHECK(hp0 - w->p[1].hp == 7 * 120, "7 x 120 explosion damage (got %d)", hp0 - w->p[1].hp);
}

static void test_wall(void)
{
	aos_world *w = &W;
	setup(w, AOS_CH_SUGURI, 0, 200, 300); /* charge right into the wall at 540 */
	w->p[0].hyper = 1;
	tick1(w, AOS_IN_H, 0);
	CHECK(w->p[0].state == 0x11b, "H -> 0x11b");
	int hp0 = w->p[1].hp, crashT = -1;
	for (int f = 0; f < 140 && crashT < 0; f++) {
		int t = w->p[0].timer;
		tick1(w, 0, 0);
		if (w->p[0].state == 0x11c)
			crashT = t;
	}
	CHECK(crashT >= 20, "wall crash at t>=20 (got %d)", crashT);
	CHECK(hp0 - w->p[1].hp == (crashT - 20) * 20 + 400, "crash damage (t-20)*20+400 = %d (got %d)",
	      (crashT - 20) * 20 + 400, hp0 - w->p[1].hp);
	idle(w, 60);
	CHECK(w->p[0].state == 0, "0x11c ends after t>50");
}

static void test_accel(void)
{
	aos_world *w = &W;
	setup(w, AOS_CH_SUGURI, 0, -300, 100);
	w->p[0].hyper = 3;
	tick1(w, AOS_IN_S, 0);
	tick1(w, AOS_IN_S | AOS_IN_B, 0);
	tick1(w, AOS_IN_S | AOS_IN_B | AOS_IN_H, 0);
	CHECK(w->p[0].state == 0x11d, "S+B+H -> 0x11d");
	int hp0 = w->p[1].hp, ticks = 0;
	for (int f = 0; f < 130; f++) {
		int hp = w->p[1].hp;
		w->p[1].x = 100; w->p[1].y = 0; /* keep the target in the beam */
		tick1(w, 0, 0);
		if (w->p[0].timer < 80)
			CHECK(w->p[0].hittable == 0 || w->p[0].state != 0x11d, "untouchable while t<80");
		if (w->p[1].hp < hp) ticks++;
	}
	CHECK(ticks == 11, "laser 300: 11 hitbox-600 ticks (got %d)", ticks);
	CHECK(hp0 - w->p[1].hp == 11 * 200, "11 x 200 (got %d)", hp0 - w->p[1].hp);
}

static void test_bits(void)
{
	aos_world *w = &W;
	setup(w, AOS_CH_SUGURI, 0, -200, 200);
	tick1(w, AOS_IN_S, 0);
	tick1(w, AOS_IN_S | AOS_IN_B, 0);
	CHECK(w->p[0].state == 0x110, "S+B -> 0x110");
	idle(w, 12);
	int bits = 0;
	for (int i = 0; i < w->obj.nenemy; i++)
		if (w->obj.enemy[i].f10 == 100) bits++;
	CHECK(bits == 2, "two laser bits (got %d)", bits);
	CHECK(fabsf(w->p[0].gaugeB - 0.65f) < 0.01f, "S+B costs 0.35 B (got %.3f)", w->p[0].gaugeB);
	int hp0 = w->p[1].hp, maxl = 0;
	for (int f = 0; f < 200; f++) {
		w->p[1].state = 0; /* not dashing: hitbox 400 is hitType 1 */
		tick1(w, 0, 0);
		if (w->obj.nlaser > maxl) maxl = w->obj.nlaser;
	}
	CHECK(maxl >= 2, "bits fire lasers (max %d)", maxl);
	int d = hp0 - w->p[1].hp;
	CHECK(d % 60 == 0 && d > 0, "beam damage in 60-ticks (got %d)", d);
}

static void test_random_hyper(void)
{
	uint32_t s;
	for (int run = 0; run < 2; run++) {
		aos_world *w = run ? &W2 : &W;
		setup(w, AOS_CH_STARBREAKER, run & 0, -150, 150);
		w->p[1].scheme = 1;
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
	CHECK(W.obj.overflow == 0, "pool overflow %d", W.obj.overflow);
}

int main(void)
{
	aos_register_all();
	test_dispatch();
	test_chain(false);
	test_chain(true);
	test_chain_cancel();
	test_b();
	test_da();
	test_grab();
	test_wall();
	test_accel();
	test_bits();
	test_random_hyper();
	printf("test_starbreaker: %s (%d/%d checks failed)\n", fails ? "FAIL" : "ok", fails, checks);
	return fails != 0;
}
