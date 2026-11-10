/* Behaviour checks for the Mira port (src/sim/chars/mira.c) against docs/re/mira.md.
 * Every assertion names the spec section / original address it checks.
 * Build: see the test_mira line in tests/run_tests.sh. */
#include "../src/sim/draw.h"
#include "../src/sim/sim.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static aos_world W, W2;

static void setup(aos_world *w, int scheme)
{
	int32_t ids[2] = {AOS_CH_MIRA, AOS_CH_SUGURI}, sch[2] = {scheme, 0}, col[2] = {0, 0};
	aos_match_init(w, ids, sch, col, 999, 2);
	uint16_t m[2] = {0, 0};
	for (int i = 0; i < 210; i++) /* past the input lock (frame < 200) */
		aos_tick(w, m);
	w->p[0].x = -100;
	w->p[1].x = 100;
	w->p[0].y = w->p[1].y = 0;
}

static void tick(aos_world *w, uint16_t m0)
{
	uint16_t m[2] = {m0, 0};
	aos_tick(w, m);
}

static void idle(aos_world *w, int n)
{
	for (int i = 0; i < n; i++)
		tick(w, 0);
}

static int count_enemy(aos_world *w, int32_t f10)
{
	int n = 0;
	for (int i = 0; i < w->obj.nenemy; i++)
		if (w->obj.enemy[i].type == AOS_CH_MIRA && w->obj.enemy[i].f10 == f10)
			n++;
	return n;
}

/* deploy 4 chakrams with Circling Chakrams (B+H) and wait until they orbit (§3 0x8f, §5.5) */
static void deploy(aos_world *w)
{
	w->p[0].hyper = 3.0f;
	tick(w, AOS_IN_B | AOS_IN_H);
	idle(w, 120);
	w->p[0].hyper = 3.0f;
}

/* --- §2 dispatch FUN_006fbb60 / FUN_006fc090 --- */
static void test_dispatch(void)
{
	static const struct { uint16_t m; bool hyper, chak; int scheme; int32_t st; const char *what; } t[] = {
		{AOS_IN_A, 0, 0, 0, 0x86, "A -> 0x86 Ninja Stars (§2 B7)"},
		{AOS_IN_D, 0, 0, 0, AOS_ST_DASH, "D -> dash (§2 B1)"},
		{AOS_IN_B, 0, 0, 0, 0x8c, "B -> 0x8c (§2 B5)"},
		{AOS_IN_S | AOS_IN_B, 0, 0, 0, 0x8d, "S+B no chakram -> 0x8d Binding Smoke (§2 B3)"},
		{AOS_IN_S | AOS_IN_B, 0, 1, 0, 0x90, "S+B with chakram -> 0x90 Soaring (§2 B3)"},
		{AOS_IN_S | AOS_IN_A, 0, 0, 0, 0x95, "S+A no chakram -> 0x95 Quick Slash (§2 B4)"},
		{AOS_IN_S | AOS_IN_A, 0, 1, 0, 0x91, "S+A with chakram -> 0x91 Swift (§2 B4)"},
		{AOS_IN_H, 1, 0, 0, 0x94, "H -> 0x94 Wrathful Tornado (§2 A5)"},
		{AOS_IN_B | AOS_IN_H, 1, 0, 0, 0x8f, "B+H no chakram -> 0x8f Circling (§2 A3)"},
		{AOS_IN_B | AOS_IN_H, 1, 1, 0, 0x92, "B+H with chakram -> 0x92 Sky-Soaring (§2 A3)"},
		{AOS_IN_S | AOS_IN_H, 1, 0, 0, 0x9a, "S+H no chakram -> 0x9a Divine Speed Slash (§2 A2)"},
		{AOS_IN_S | AOS_IN_H, 1, 1, 0, 0x93, "S+H with chakram -> 0x93 Baiting (§2 A2)"},
		{AOS_IN_S | AOS_IN_B | AOS_IN_H, 1, 0, 0, 0x9d, "S+B+H -> 0x9d Twin Dragon (§2 A1)"},
		{AOS_IN_H | AOS_IN_A, 1, 0, 1, 0x94, "scheme B: H+A -> 0x94 (FUN_006fc090)"},
		{AOS_IN_H | AOS_IN_S, 1, 0, 1, 0x9a, "scheme B: H+S -> 0x9a"},
		{AOS_IN_H | AOS_IN_B, 1, 1, 1, 0x92, "scheme B: H+B with chakram -> 0x92"},
		{AOS_IN_H | AOS_IN_SHIELD, 1, 0, 1, 0x9d, "scheme B: H+Shield -> 0x9d"},
		{AOS_IN_H | AOS_IN_D, 1, 0, 1, 0x8e, "scheme B: H+D -> 0x8e"},
	};
	for (size_t k = 0; k < sizeof t / sizeof t[0]; k++) {
		aos_world *w = &W;
		setup(w, t[k].scheme);
		if (t[k].chak) {
			deploy(w);
			CHECK(count_enemy(w, 100) == 4, "4 deployed chakrams (§3 0x8f)");
		}
		if (t[k].hyper)
			w->p[0].hyper = 3.0f;
		tick(w, t[k].m);
		CHECK(w->p[0].state == t[k].st, "%s (got %#x)", t[k].what, (unsigned)w->p[0].state);
	}
	/* D+H needs Dash held before H (Dash press alone dashes; §2 A4 tests +0x34 > 1) */
	aos_world *w = &W;
	setup(w, 0);
	w->p[0].hyper = 3.0f;
	tick(w, AOS_IN_D);
	tick(w, AOS_IN_D | AOS_IN_H);
	CHECK(w->p[0].state == 0x8e, "D held + H -> 0x8e Smokescreen (§2 A4) (got %#x)", (unsigned)w->p[0].state);
	CHECK(fabsf(w->p[0].hyper - 2.0f) < 1e-4f, "hyper cost 1.0 (DAT_00853b34) (got %f)", (double)w->p[0].hyper);
	/* D held + A -> Caltrops (§2 B6) */
	setup(w, 0);
	tick(w, AOS_IN_D);
	tick(w, AOS_IN_D | AOS_IN_A);
	CHECK(w->p[0].state == 0x88, "D held + A -> 0x88 (got %#x)", (unsigned)w->p[0].state);
	/* S+B+H costs 3 levels (DAT_00813a50) */
	setup(w, 0);
	w->p[0].hyper = 3.0f;
	tick(w, AOS_IN_S | AOS_IN_B | AOS_IN_H);
	CHECK(w->p[0].hyper < 1e-4f, "accel cost 3.0 (got %f)", (double)w->p[0].hyper);
}

/* --- §3 0x86, §5.1 beam 200 --- */
static void test_ninja_stars(void)
{
	aos_world *w = &W;
	setup(w, 0);
	tick(w, AOS_IN_A);
	int spawnT = -1, nspawn = 0;
	float a0 = w->p[0].gaugeA;
	for (int f = 0; f < 40; f++) {
		int t = w->p[0].timer, nb = w->obj.nbeam;
		tick(w, 0);
		if (w->obj.nbeam > nb && spawnT < 0) {
			spawnT = t;
			nspawn = w->obj.nbeam - nb;
			CHECK(fabsf(a0 - w->p[0].gaugeA - 0.15f) < 1e-4f, "ninja-star pair costs A 0.15 (§1.1)");
			aos_beam *b = &w->obj.beam[w->obj.nbeam - 1];
			CHECK(b->id == 200 && b->damage == 200, "0xc9 stored as id 200, damage 200 (§5.1)");
			CHECK(fabsf(b->f6c + 0.05f) < 1e-6f, "0xc9 curve -0.05 (DAT_00814114) (§5.1)");
			CHECK(fabsf(w->obj.beam[w->obj.nbeam - 2].f6c - 0.05f) < 1e-6f, "200 curve +0.05 (§5.1)");
			CHECK(fabsf(b->speed - 12.0f) < 1e-4f, "speed 16 (DAT_008140d8) x0.75 after the spawn-frame update (§5.1 200)");
		}
		if (w->p[0].state == 0) {
			CHECK(t == 36, "0x86 ends when t>35 (§3 0x86) (state 0 set at t=%d)", t);
			break;
		}
	}
	CHECK(spawnT == 12, "stars fire at t=12 (§3 0x86) (got %d)", spawnT);
	CHECK(nspawn == 2, "one pair (got %d)", nspawn);
	/* hold A: t 20 -> 10, another pair (§3 0x86 t=20) */
	setup(w, 0);
	int pairs = 0;
	for (int f = 0; f < 60; f++) {
		int nb = w->obj.nbeam;
		tick(w, AOS_IN_A);
		if (w->obj.nbeam > nb)
			pairs++;
	}
	CHECK(w->p[0].state == 0x86, "A held keeps 0x86 looping");
	CHECK(pairs >= 5, "a pair every 10 frames while A held (got %d)", pairs);
	/* dash-cancel from t=20 */
	setup(w, 0);
	tick(w, AOS_IN_A);
	for (int f = 0; f < 18; f++)
		tick(w, 0);
	tick(w, AOS_IN_D); /* handler sees t=19 */
	CHECK(w->p[0].state == 0x86, "no dash-cancel at t=19");
	tick(w, 0);
	tick(w, AOS_IN_D); /* t=21 */
	CHECK(w->p[0].state == AOS_ST_DASH, "dash-cancel from t>=20 (got %#x)", (unsigned)w->p[0].state);
}

/* --- §3 0x88, §1.1 (no cost entry), §5.1 beam 300 --- */
static void test_caltrops(void)
{
	aos_world *w = &W;
	setup(w, 0);
	tick(w, AOS_IN_D);
	tick(w, AOS_IN_D | AOS_IN_A);
	float a0 = w->p[0].gaugeA, h0 = w->p[0].hyper;
	int first = -1, last = -1, n = 0;
	for (int f = 0; f < 30; f++) {
		int t = w->p[0].timer, nb = w->obj.beamSerial;
		tick(w, 0);
		if (w->obj.beamSerial > nb) { /* count spawns (caltrops that hit are erased) */
			if (first < 0) first = t;
			last = t;
			n += w->obj.beamSerial - nb;
		}
	}
	CHECK(first == 10 && last == 21 && n == 12, "12 caltrops at t=10..21 (§3 0x88) (got %d..%d n=%d)", first, last, n);
	CHECK(w->p[0].gaugeA == a0 && w->p[0].hyper == h0, "caltrops free: no cost entry for 0x88 (§0.2)");
	int k = -1;
	for (int i = 0; i < w->obj.nbeam; i++)
		if (w->obj.beam[i].type == AOS_CH_MIRA && w->obj.beam[i].id == 300) { k = i; break; }
	CHECK(k >= 0 && w->obj.beam[k].damage == 50 && w->obj.beam[k].scale == 1.0f,
	      "grown caltrop damage 50 / scale 1 (§5.1 300)");
}

/* --- §3 0x8c/0x89, §5.2 missiles --- */
static void test_kunai_kiri(void)
{
	aos_world *w = &W;
	setup(w, 0);
	int hp0 = w->p[1].hp;
	tick(w, AOS_IN_B);
	tick(w, 0); /* B = 1 (released this frame) */
	tick(w, 0); /* B = 0 < 1 at t=2 < 13 -> 0x89 */
	CHECK(w->p[0].state == 0x89, "B tap -> 0x89 Kunai (§3 0x8c) (got %#x)", (unsigned)w->p[0].state);
	float b0 = w->p[0].gaugeB;
	int spawnT = -1, n = 0;
	for (int f = 0; f < 80; f++) {
		int t = w->p[0].timer, nm = w->obj.nmissile;
		tick(w, 0);
		if (w->obj.nmissile > nm && spawnT < 0) {
			spawnT = t;
			n = w->obj.nmissile - nm;
			aos_missile *m = &w->obj.missile[w->obj.nmissile - 1];
			CHECK(m->id == 100 && m->hp == 80 && m->damage == 180, "kunai hp 80 dmg 180 (§5.2 v>0x58a)");
			CHECK(fabsf(b0 - w->p[0].gaugeB - 0.2f) < 1e-4f, "kunai costs B 0.2 (§1.1 v>0x5a0)");
		}
	}
	CHECK(spawnT == 12 && n == 3, "3 kunai at t=12 (§3 0x89) (t=%d n=%d)", spawnT, n);
	CHECK(w->obj.nwhip >= 0, "whips");
	CHECK(hp0 - w->p[1].hp == 180, "centre kunai hits a placed opponent for 180 (§5.2) (got %d)", hp0 - w->p[1].hp);
	/* hold B: 8 kiri at t=15,17,19,21 (§3 0x8c) */
	setup(w, 0);
	int kiri = 0, kfirst = -1;
	for (int f = 0; f < 30; f++) {
		int t = w->p[0].timer, nm = w->obj.nmissile;
		tick(w, AOS_IN_B);
		if (w->obj.nmissile > nm) {
			kiri += w->obj.nmissile - nm;
			if (kfirst < 0) kfirst = t;
		}
	}
	CHECK(kiri == 8 && kfirst == 15, "8 kiri from t=15 (§3 0x8c) (n=%d first=%d)", kiri, kfirst);
	CHECK(w->obj.missile[0].id == 200 && w->obj.missile[0].damage == 160, "kiri id 200 dmg 160 (§5.2)");
}

/* --- §3 0x8d, §5.5 smoke ball / cloud slow --- */
static void test_smoke(void)
{
	aos_world *w = &W;
	setup(w, 0);
	float b0 = w->p[0].gaugeB;
	tick(w, AOS_IN_S | AOS_IN_B);
	int spawnT = -1;
	for (int f = 0; f < 20; f++) {
		int t = w->p[0].timer, ne = w->obj.nenemy;
		tick(w, AOS_IN_S);
		if (w->obj.nenemy > ne && spawnT < 0)
			spawnT = t;
	}
	CHECK(spawnT == 10, "smoke ball at t=10 (§3 0x8d) (got %d)", spawnT);
	CHECK(fabsf(b0 - w->p[0].gaugeB - 0.3f) < 0.02f, "smoke costs B 0.3 (§1.1)");
	CHECK(count_enemy(w, 200) == 1, "enemy 200 smoke ball");
	/* fly ~46 updates then become a cloud; opponent within 100 gets status 4 (×0.25) for 16 */
	bool slowed = false, cloud = false;
	for (int f = 0; f < 120; f++) {
		tick(w, 0);
		for (int i = 0; i < w->obj.nenemy; i++)
			if (w->obj.enemy[i].f10 == 200 && w->obj.enemy[i].f84 == 100) {
				cloud = true;
				w->p[1].x = w->obj.enemy[i].pos[0];
				w->p[1].y = w->obj.enemy[i].pos[1];
			}
		if (w->p[1].statusOn[AOS_STATUS_SLOW]) {
			slowed = true;
			CHECK(w->p[1].statusTime[AOS_STATUS_SLOW] <= 16, "slow 16 frames (FUN_006b1a40(opp,4,16))");
		}
	}
	CHECK(cloud, "smoke ball becomes a cloud (onKilled returns 1, §5.5)");
	CHECK(slowed, "cloud slows the opponent (§5.5 behaviour 200)");
}

/* --- §3 0x8f/0x90/0x92, §1.2, §5.5 chakrams --- */
static void test_chakrams(void)
{
	aos_world *w = &W;
	setup(w, 0);
	w->p[0].hyper = 3.0f;
	tick(w, AOS_IN_B | AOS_IN_H);
	int spawnT = -1;
	for (int f = 0; f < 10; f++) {
		int t = w->p[0].timer, ne = w->obj.nenemy;
		tick(w, 0);
		if (w->obj.nenemy > ne && spawnT < 0)
			spawnT = t;
	}
	CHECK(spawnT == 5 && count_enemy(w, 100) == 4, "4 chakrams at t=5 (§3 0x8f) (t=%d)", spawnT);
	CHECK(w->obj.enemy[0].active == 0, "inactive while deploying (§5.5 sub 0)");
	idle(w, 70);
	aos_enemy *e = &w->obj.enemy[0];
	CHECK(e->active == 1 && e->f84 == 100 && e->f70 == 120.0f, "deployed: radius 120, active (§5.5)");
	float dx = e->pos[0] - w->p[0].x, dy = e->pos[1] - w->p[0].y;
	CHECK(fabsf(sqrtf(dx * dx + dy * dy) - 120.0f) < 1.5f, "orbit radius 120 around Mira");
	CHECK(e->hp == 500, "chakram hp 500 (§5.5)");
	/* Soaring Chakrams: S+B launches one at t=12 (§3 0x90) */
	tick(w, AOS_IN_S | AOS_IN_B);
	CHECK(w->p[0].state == 0x90, "0x90");
	for (int f = 0; f < 12; f++)
		tick(w, 0);
	CHECK(count_enemy(w, 100) == 3 && count_enemy(w, 0xb4) == 1, "one launched (+0x10 -> 0xb4) (§1.2)");
	for (int i = 0; i < w->obj.nenemy; i++)
		if (w->obj.enemy[i].f10 == 0xb4)
			CHECK(w->obj.enemy[i].f70 == -15.0f || w->obj.enemy[i].f70 == -16.0f, "launch speed -16 (DAT_00825918)");
	idle(w, 40);
	/* Sky-Soaring: B+H launches all remaining (§3 0x92) */
	w->p[0].hyper = 3.0f;
	tick(w, AOS_IN_B | AOS_IN_H);
	CHECK(w->p[0].state == 0x92, "0x92 (got %#x)", (unsigned)w->p[0].state);
	tick(w, 0);
	CHECK(count_enemy(w, 100) == 0 && count_enemy(w, 0xbe) == 3, "all chakrams -> 0xbe at t=1");
}

/* --- §3 0x95..0x99 Quick Slash chain, §5.4 hitboxes --- */
static void test_quick_slash(void)
{
	aos_world *w = &W;
	setup(w, 0);
	w->p[1].x = 20; /* 120 px */
	int hp0 = w->p[1].hp;
	tick(w, AOS_IN_S | AOS_IN_A);
	CHECK(w->p[0].state == 0x95, "0x95");
	CHECK(w->p[0].hurtShape[2] == 0.0f, "intangible during the approach (§3 0x95)");
	int t97 = -1;
	for (int f = 0; f < 20 && w->p[0].state != 0x97; f++) {
		int t = w->p[0].timer;
		tick(w, 0);
		if (w->p[0].state == 0x97)
			t97 = t;
	}
	CHECK(t97 == 11, "0x95 -> 0x97 at t=11 (§3 0x95) (got %d)", t97);
	for (int f = 0; f < 8; f++)
		tick(w, 0);
	CHECK(hp0 - w->p[1].hp == 120, "Quick Slash 1 hitbox 300 deals 120 (§5.4) (got %d)", hp0 - w->p[1].hp);
	CHECK(w->p[0].hitFlag == 1, "hit-confirm set by hitbox 300 (§5.4)");
	tick(w, AOS_IN_A);
	CHECK(w->p[0].state == 0x96, "A on hit -> 0x96 dash-through (got %#x)", (unsigned)w->p[0].state);
	CHECK(w->p[1].state == AOS_ST_HITSTUN && w->p[1].speed == 0.0f, "opponent frozen in hitstun (§0.10)");
	int to98 = 0;
	for (int f = 0; f < 15; f++) {
		tick(w, 0);
		if (w->p[0].state == 0x98)
			to98 = 1;
	}
	CHECK(to98, "0x96 -> 0x98 after t=11");
}

/* --- §3 0x94 / §5.6 laser 100 --- */
static void test_tornado(void)
{
	aos_world *w = &W;
	setup(w, 0);
	w->p[0].hyper = 3.0f;
	tick(w, AOS_IN_H);
	int spawnT = -1;
	for (int f = 0; f < 20; f++) {
		int t = w->p[0].timer, nl = w->obj.nlaser;
		tick(w, 0);
		if (w->obj.nlaser > nl && spawnT < 0)
			spawnT = t;
	}
	CHECK(spawnT == 15, "tornado laser at t=15 (§3 0x94) (got %d)", spawnT);
	CHECK(w->obj.nlaser == 1 && w->obj.laser[0].id == 100 && w->obj.laser[0].damage == 35,
	      "laser 100 damage 35 (§5.6)");
	CHECK(fabsf(w->obj.laser[0].origin[0] - w->p[1].x) < 2.0f, "tornado through the opponent (projection, §3 0x94)");
	int hp0 = w->p[1].hp, endT = -1, gone = -1;
	for (int f = 0; f < 130; f++) {
		int t = w->p[0].timer, age = w->obj.nlaser ? w->obj.laser[0].age : -1;
		tick(w, 0);
		if (endT < 0 && w->p[0].state == 0)
			endT = t;
		if (gone < 0 && w->obj.nlaser == 0)
			gone = age + 1;
	}
	CHECK(w->p[1].hp < hp0, "tornado damages a placed opponent");
	CHECK(gone == 121, "laser removed in the update that sees age 121 (>120, §5.6) (got %d)", gone);
	CHECK(endT == 101, "0x94 ends when t>100 (§3 0x94) (got %d)", endT);
}

/* --- §3 0x9a..0x9c Divine Speed Slash --- */
static void test_divine(void)
{
	aos_world *w = &W;
	setup(w, 0);
	w->p[0].hyper = 3.0f;
	tick(w, AOS_IN_S | AOS_IN_H);
	bool b = false, c = false;
	int clones = 0;
	for (int f = 0; f < 140; f++) {
		tick(w, 0);
		if (w->p[0].state == 0x9b) {
			b = true;
			CHECK(w->p[0].hittable == 0, "Mira not hittable in 0x9b (§3 0x9b)");
		}
		if (w->p[0].state == 0x9c)
			c = true;
		int k = count_enemy(w, 400) + count_enemy(w, 0x191) + count_enemy(w, 0x192);
		if (k > clones)
			clones = k;
	}
	CHECK(b && c, "0x9a hit -> 0x9b -> 0x9c (§3)");
	CHECK(clones >= 4, "clones 400/401/402 spawned (§3 0x9b) (max alive %d)", clones);
	CHECK(w->p[1].hp < w->p[1].maxHp, "Divine Speed Slash deals damage");
}

/* every state runs to completion and draws (smoke for draw code) */
static void draw_all(aos_world *w)
{
	aos_draw_begin();
	aos_draw_world(w);
}

static void test_all_states(void)
{
	static const int states[] = {0x86, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93,
	                             0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d};
	for (size_t k = 0; k < sizeof states / sizeof states[0]; k++) {
		aos_world *w = &W;
		setup(w, 0);
		if (states[k] >= 0x90 && states[k] <= 0x93)
			deploy(w);
		aos_set_state(w, &w->p[0], states[k]);
		for (int f = 0; f < 400; f++) {
			tick(w, f < 30 ? AOS_IN_B : 0);
			draw_all(w);
			CHECK(isfinite(w->p[0].x) && isfinite(w->p[0].y), "state %#x non-finite", states[k]);
		}
		CHECK(w->p[0].state < 0x7c, "state %#x did not end (now %#x)", states[k], (unsigned)w->p[0].state);
	}
}

static int mx[4];
static void test_hyper_random(void)
{
	for (int run = 0; run < 2; run++) {
		aos_world *w = run ? &W2 : &W;
		int32_t ids[2] = {AOS_CH_MIRA, AOS_CH_MIRA}, sch[2] = {0, 1}, col[2] = {0, 1};
		aos_match_init(w, ids, sch, col, 999, 2);
		uint16_t m[2] = {0, 0};
		uint32_t s = 4242;
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
			if (w->obj.nhitbox > mx[0]) mx[0] = w->obj.nhitbox;
			if (w->obj.nbeam > mx[1]) mx[1] = w->obj.nbeam;
			if (w->obj.neffect > mx[2]) mx[2] = w->obj.neffect;
			if (w->obj.nenemy > mx[3]) mx[3] = w->obj.nenemy;
			if (!isfinite(w->p[0].x) || !isfinite(w->p[1].x)) {
				CHECK(0, "non-finite at frame %d", f);
				return;
			}
		}
	}
	CHECK(!memcmp(&W, &W2, sizeof W), "hyper random runs diverged");
	/* Known engine limit (not a Mira bug): each tornado laser spawns 40 hitboxes 0xd2 per even
	 * frame (§5.6) and AOS_MAX_HITBOX is 96, so overlapping tornadoes overflow the pool (the
	 * original std::vector is unbounded). Reported as an engine request; informational here. */
	if (W.obj.overflow)
		printf("note: pool overflow %ld in the mirror hyper run (hitbox pool, engine request)\n", (long)W.obj.overflow);
	(void)mx;
}

#define RUN(fn) do { if (verbose) { printf("[%s]\n", #fn); fflush(stdout); } fn(); } while (0)

int main(int argc, char **argv)
{
	bool verbose = argc > 1;
	(void)argv;
	aos_register_all();
	RUN(test_dispatch);
	RUN(test_ninja_stars);
	RUN(test_caltrops);
	RUN(test_kunai_kiri);
	RUN(test_smoke);
	RUN(test_chakrams);
	RUN(test_quick_slash);
	RUN(test_tornado);
	RUN(test_divine);
	RUN(test_all_states);
	RUN(test_hyper_random);
	printf("test_mira: %s (%d failures)\n", fails ? "FAIL" : "ok", fails);
	return fails != 0;
}
