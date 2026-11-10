/* Behaviour checks for the Nanako port (src/sim/chars/nanako.c) against docs/re/nanako.md.
 * Every assertion cites the spec section / original address.
 * Build: like tests/run_tests.sh (src/sim/{*}.c src/sim/chars/{*}.c tests/test_nanako.c). */
#include "../src/sim/draw.h"
#include "../src/sim/sim.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static aos_world W, W2;
static uint16_t M[2];

static void tick(aos_world *w, uint16_t mask, int n)
{
	for (int i = 0; i < n; i++) {
		M[0] = mask;
		aos_tick(w, M);
		aos_draw_begin();
		aos_draw_world(w);
	}
}

static void init(aos_world *w, int scheme)
{
	int32_t ids[2] = {AOS_CH_NANAKO, AOS_CH_SUGURI}, sch[2] = {scheme, 0}, col[2] = {0, 1};
	aos_match_init(w, ids, sch, col, 999, 2);
	M[0] = M[1] = 0;
}

static void setup(aos_world *w, int scheme)
{
	init(w, scheme);
	tick(w, 0, 230); /* intro 0x197 done (t>100), Bits deployed (mode 10), input lock over */
	w->p[0].x = -200; w->p[0].y = 0;
	w->p[1].x = 300; w->p[1].y = 0;
	w->p[0].hyper = 3.0f;
	w->p[0].gaugeA = w->p[0].gaugeB = 1.0f;
}

static int nbits(const aos_world *w)
{
	int n = 0;
	for (int i = 0; i < w->obj.nenemy; i++)
		n += w->obj.enemy[i].type == AOS_CH_NANAKO && w->obj.enemy[i].f10 >= 100 && w->obj.enemy[i].f10 < 107;
	return n;
}

static aos_enemy *bit(aos_world *w, int idx)
{
	for (int i = 0; i < w->obj.nenemy; i++)
		if (w->obj.enemy[i].type == AOS_CH_NANAKO && w->obj.enemy[i].f10 == idx)
			return &w->obj.enemy[i];
	return NULL;
}

static int count(const aos_world *w, char fam, int32_t id)
{
	int n = 0;
	if (fam == 'b') for (int i = 0; i < w->obj.nbeam; i++) n += w->obj.beam[i].type == AOS_CH_NANAKO && w->obj.beam[i].id == id;
	if (fam == 'm') for (int i = 0; i < w->obj.nmissile; i++) n += w->obj.missile[i].type == AOS_CH_NANAKO && w->obj.missile[i].id == id;
	if (fam == 'l') for (int i = 0; i < w->obj.nlaser; i++) n += w->obj.laser[i].type == AOS_CH_NANAKO && w->obj.laser[i].id == id;
	return n;
}

/* Total spawned of a family/id over n frames (objects may die meanwhile: count increments). */
/* ammo actually paid (sum of per-frame drops; neutral-state regen +0.01/frame ignored) */
static float spentA, spentB;

static int spawned(aos_world *w, char fam, int32_t id, uint16_t mask, int n)
{
	int tot = 0;
	spentA = spentB = 0;
	for (int f = 0; f < n; f++) {
		int s0 = fam == 'b' ? w->obj.beamSerial : w->obj.missileSerial;
		int before = count(w, fam, id);
		float a = w->p[0].gaugeA, b = w->p[0].gaugeB;
		/* main loop regen +0.01 (cap 1) in states 0/1/2 happens after the Bit update in the same tick */
		tick(w, mask, 1);
		bool regen = w->p[0].state <= 2; /* state after the T2 (main-loop regen check) */
		int after = count(w, fam, id);
		int news = (fam == 'b' ? w->obj.beamSerial : w->obj.missileSerial) - s0;
		tot += news; /* serial delta: conversions (300->0x136) or removals in the same frame do not hide spawns */
		(void)after; (void)before;
		float ra = regen && w->p[0].gaugeA >= 1.0f ? 0.0f : regen ? 0.01f : 0.0f;
		float rb = regen && w->p[0].gaugeB >= 1.0f ? 0.0f : regen ? 0.01f : 0.0f;
		if (w->p[0].gaugeA - ra < a - 1e-4f) spentA += a - (w->p[0].gaugeA - ra);
		if (w->p[0].gaugeB - rb < b - 1e-4f) spentB += b - (w->p[0].gaugeB - rb);
	}
	return tot;
}

/* ---- §3 0x197 / §5.1 spawn: 7 Bits per round, no hurtbox during the intro ---- */
static void test_intro(void)
{
	aos_world *w = &W;
	init(w, 0);
	CHECK(w->p[0].state == 0x197, "round starts in 0x197 (initialState) (got %#x)", w->p[0].state);
	int spawnT = -1;
	for (int f = 0; f < 120; f++) {
		int32_t t = w->p[0].timer;
		int n0 = nbits(w);
		tick(w, 0, 1);
		if (w->p[0].state == 0x197)
			CHECK(w->p[0].hurtShape[2] == 0.0f, "no hurtbox during 0x197 (FUN_007063a0)");
		if (nbits(w) > n0 && spawnT < 0) {
			spawnT = t;
			CHECK(nbits(w) == 7, "7 Bits at once (got %d)", nbits(w));
			for (int k = 0; k < 7; k++) {
				aos_enemy *e = bit(w, 100 + k);
				CHECK(e && e->hp == 9999 && e->active == 0, "Bit %d template hp 9999 inactive (§5.1)", k);
				if (e)
					CHECK(e->timer2 == -10 * (k > 3 ? k - 3 : 3 - k) + 1, "Bit %d start delay -10|k-3| (got %d)", k, e->timer2 - 1);
			}
			CHECK(w->obj.nwhip == 7, "one trail whip per Bit (§5.7) (got %d)", w->obj.nwhip);
		}
	}
	CHECK(spawnT == 50, "Bits spawn at 0x197 t=50 (got %d)", spawnT);
	CHECK(w->p[0].state == AOS_ST_NEUTRAL && w->p[0].timer > 100, "0x197 ends after t>100");
	tick(w, 0, 100);
	for (int k = 0; k < 7; k++) {
		aos_enemy *e = bit(w, 100 + k);
		CHECK(e && e->f84 == 10 && e->scale[2] == 1.0f, "Bit %d idle (mode 10) after deploy (§5.1 mode 0)", k);
	}
	/* next round: Bits are recreated (cmd 900 at KO removes them) */
}

/* ---- §2.1 Deploy Bits: orbit radius 100 / 240 with S held ---- */
static float mean_radius(aos_world *w, uint16_t mask, int frames)
{
	double sum = 0;
	int n = 0;
	for (int f = 0; f < frames; f++) {
		tick(w, mask, 1);
		if (f < frames / 2)
			continue;
		for (int k = 0; k < 7; k++) {
			aos_enemy *e = bit(w, 100 + k);
			if (!e)
				continue;
			float dx = e->pos[0] - w->p[0].x, dy = e->pos[1] - w->p[0].y;
			sum += sqrtf(dx * dx + dy * dy);
			n++;
		}
	}
	return n ? (float)(sum / n) : 0.0f;
}

static void test_deploy(void)
{
	aos_world *w = &W;
	setup(w, 0);
	float r0 = mean_radius(w, 0, 200);
	CHECK(r0 > 70.0f && r0 < 135.0f, "idle orbit ~100 px (DAT_00853b90) (got %f)", (double)r0);
	float r1 = mean_radius(w, AOS_IN_S, 200);
	CHECK(r1 > 200.0f && r1 < 280.0f, "S held: orbit ~240 px (DAT_00815584) (got %f)", (double)r1);
	CHECK(w->p[0].state == AOS_ST_NEUTRAL, "S alone has no state (§2.1)");
	/* S press: speed burst 16 on idle Bits */
	tick(w, 0, 5);
	tick(w, AOS_IN_S, 1);
	aos_enemy *e = bit(w, 100);
	CHECK(e && e->f70 > 15.0f, "S press -> Bit speed 16 (got %f)", e ? (double)e->f70 : -1.0);
}

/* ---- §2 dispatch ---- */
static void test_dispatch(void)
{
	aos_world *w = &W;
	struct { uint16_t held, press; int32_t st; const char *what; } a[] = {
		{0, AOS_IN_A, 0x199, "A -> 0x199 (§2 B6)"},
		{0, AOS_IN_B, 0x19c, "B -> 0x19c (§2 B4)"},
		{AOS_IN_S, AOS_IN_B, 0x19d, "S+B -> 0x19d (§2 B3)"},
		{AOS_IN_S, AOS_IN_A, 0x199, "S+A -> 0x199 (no special meaning, §2)"},
		{AOS_IN_S | AOS_IN_B, AOS_IN_H, 0x1a3, "S+B+H -> 0x1a3 (§2 A1)"},
		{AOS_IN_S, AOS_IN_H, 0x1a1, "S+H -> 0x1a1 (§2 A2)"},
		{AOS_IN_B, AOS_IN_H, 0x1a2, "B+H -> 0x1a2 (§2 A3)"},
		{0, AOS_IN_H, 0x19f, "H -> 0x19f (§2 A5)"},
	};
	for (size_t k = 0; k < sizeof a / sizeof a[0]; k++) {
		setup(w, 0);
		if (a[k].held)
			tick(w, a[k].held, 3);
		tick(w, a[k].held | a[k].press, 1);
		CHECK(w->p[0].state == a[k].st, "%s (got %#x)", a[k].what, w->p[0].state);
	}
	setup(w, 0);
	tick(w, AOS_IN_D, 1);
	tick(w, AOS_IN_D | AOS_IN_A, 1);
	CHECK(w->p[0].state == 0x19a, "D held + A -> 0x19a (§2 B5) (got %#x)", w->p[0].state);
	setup(w, 0);
	tick(w, AOS_IN_D, 1);
	tick(w, AOS_IN_D | AOS_IN_H, 1);
	CHECK(w->p[0].state == 0x1a0, "D held + H -> 0x1a0 (§2 A4) (got %#x)", w->p[0].state);
	/* S+B+H with 1 level falls through to S+H (FUN_00780910 fails) */
	setup(w, 0);
	w->p[0].hyper = 1.2f;
	tick(w, AOS_IN_S | AOS_IN_B, 3);
	tick(w, AOS_IN_S | AOS_IN_B | AOS_IN_H, 1);
	CHECK(w->p[0].state == 0x1a1, "S+B+H with 1.2 gauge -> 0x1a1 (got %#x)", w->p[0].state);

	/* scheme B (FUN_007077f0): H held + X pressed */
	struct { uint16_t press; int32_t st; } b[] = {
		{AOS_IN_SHIELD, 0x1a3}, {AOS_IN_S, 0x1a1}, {AOS_IN_B, 0x1a2}, {AOS_IN_D, 0x1a0}, {AOS_IN_A, 0x19f},
	};
	for (size_t k = 0; k < sizeof b / sizeof b[0]; k++) {
		setup(w, 1);
		tick(w, AOS_IN_H, 3);
		CHECK(w->p[0].state == AOS_ST_NEUTRAL, "scheme B: H alone does nothing");
		tick(w, AOS_IN_H | b[k].press, 1);
		CHECK(w->p[0].state == b[k].st, "scheme B: H held + %#x -> %#x (got %#x)", b[k].press, b[k].st, w->p[0].state);
	}
}

/* ---- §3 states: command frames, per-Bit ammo, ends, dash-cancel ---- */
static int mode_count(aos_world *w, int32_t mode)
{
	int n = 0;
	for (int k = 0; k < 7; k++) {
		aos_enemy *e = bit(w, 100 + k);
		n += e && e->f84 == mode;
	}
	return n;
}

static int cmd_frame(aos_world *w, int32_t mode, uint16_t mask, int maxF)
{
	for (int f = 0; f < maxF; f++) {
		int32_t t = w->p[0].timer;
		tick(w, mask, 1);
		if (mode_count(w, mode) == 7)
			return t;
	}
	return -1;
}

static void test_states(void)
{
	aos_world *w = &W;
	/* Needle: cmd(100) at t15; each Bit fires beam 100 at T5, 0.1 ammo A each (§3 0x199, §1.1 row 0x199) */
	setup(w, 0);
	tick(w, AOS_IN_A, 1);
	int t = cmd_frame(w, 100, AOS_IN_A, 20);
	CHECK(t == 15, "Needle cmd(100) at t=15 (got %d)", t);
	int nb = spawned(w, 'b', 100, AOS_IN_A, 10);
	CHECK(nb == 7, "7 Bits x beam 100 (got %d)", nb);
	CHECK(fabsf(w->p[0].gaugeA - 0.3f) < 1e-3f, "7 x 0.1 ammo A per Bit (A=%f)", (double)w->p[0].gaugeA);
	/* per-Bit ammo: only 3 Bits can pay 0.1 from 0.35 */
	setup(w, 0);
	w->p[0].gaugeA = 0.35f;
	tick(w, AOS_IN_A, 1);
	nb = spawned(w, 'b', 100, AOS_IN_A, 30);
	CHECK(nb == 3, "A=0.35 -> only 3 Bits fire (FUN_007807f0 per Bit) (got %d)", nb);
	/* end: state 0 after t>40; dash-cancel from 25 */
	setup(w, 0);
	tick(w, AOS_IN_A, 1);
	while (w->p[0].state == 0x199 && w->p[0].timer < 60)
		tick(w, AOS_IN_A, 1);
	CHECK(w->p[0].timer == 42, "0x199 ends at t=41 (got timer %d)", w->p[0].timer);
	setup(w, 0);
	tick(w, AOS_IN_A, 1);
	while (w->p[0].timer < 24)
		tick(w, AOS_IN_A, 1);
	tick(w, AOS_IN_A | AOS_IN_D, 1);
	CHECK(w->p[0].state == 0x199, "no dash-cancel at t=24");
	tick(w, AOS_IN_A, 1);
	tick(w, AOS_IN_A | AOS_IN_D, 1);
	CHECK(w->p[0].state == AOS_ST_DASH, "dash-cancel at t=26 (>=25) (got %#x)", w->p[0].state);

	/* Tone: release -> 0x198 with t := 10; cmd(0x96) at t=11; 7 x beam 200 (§3 0x198) */
	setup(w, 0);
	tick(w, AOS_IN_A, 1);
	tick(w, 0, 1); /* "A < 2": the release frame itself (value 1) switches */
	CHECK(w->p[0].state == 0x198 && w->p[0].timer == 11, "A release -> 0x198 with t:=10 (now %#x t=%d)", w->p[0].state, w->p[0].timer);
	nb = spawned(w, 'b', 200, 0, 60);
	CHECK(nb == 7, "Tone: 7 x beam 200 (got %d)", nb);
	CHECK(fabsf(spentA - 7 * 0.075f) < 1e-3f, "Tone 7 x 0.075 A per Bit (row 0x198) (spent %f)", (double)spentA);

	/* Fruit: each Bit up to 7 shots of beam 300, 0.03 A each (§5.1 mode 0xa0) */
	setup(w, 0);
	tick(w, AOS_IN_D, 1);
	tick(w, AOS_IN_D | AOS_IN_A, 1);
	nb = spawned(w, 'b', 300, 0, 120);
	CHECK(nb > 7 && nb <= 49, "Fruit: many beams 300, <= 49 (got %d)", nb);
	CHECK(fabsf(spentA - nb * 0.03f) < 1e-3f, "Fruit: 0.03 A per shot (row 0x19a) (%d shots, spent %f)", nb, (double)spentA);

	/* Ether Leaf / Bug: missiles 200 / 300, owner = Bit id (§0.3) */
	setup(w, 0);
	tick(w, AOS_IN_B, 1);
	tick(w, 0, 1);
	CHECK(w->p[0].state == 0x19b, "B release -> 0x19b (got %#x)", w->p[0].state);
	int nm = spawned(w, 'm', 200, 0, 30);
	CHECK(nm == 7, "Leaf: 7 x missile 200 (got %d)", nm);
	CHECK(fabsf(spentB - 7 * 0.075f) < 1e-3f, "Leaf 7 x 0.075 B (row 0x19b)");
	for (int i = 0; i < w->obj.nmissile; i++)
		if (w->obj.missile[i].type == AOS_CH_NANAKO)
			CHECK(aos_enemy_by_id(w, w->obj.missile[i].ownerId) != NULL, "Bit missile ownerId = the Bit enemy id (§0.3)");
	setup(w, 0);
	tick(w, AOS_IN_B, 20);
	nm = spawned(w, 'm', 300, AOS_IN_B, 60);
	CHECK(nm >= 25 && fabsf(spentB - nm * 0.04f) < 1e-3f, "Bug: 0.04 B per shot (row 0x19c) (%d shots, spent %f)", nm, (double)spentB);

	/* Shell Shot: cmd(500) at t5 -> Bits 0x66, 0x68 tackle; +0.05 hyper; B held at t23 -> 0x19e */
	setup(w, 0);
	w->p[0].hyper = 0.0f;
	tick(w, AOS_IN_S, 3);
	tick(w, AOS_IN_S | AOS_IN_B, 1);
	tick(w, AOS_IN_B, 5);
	CHECK(bit(w, 0x66)->f84 == 500 && bit(w, 0x68)->f84 == 500 && mode_count(w, 500) == 2, "cmd(500): Bits 0x66/0x68 tackle (§5.2)");
	CHECK(fabsf(w->p[0].hyper - 0.05f) < 1e-4f, "Shell Shot +0.05 hyper (row 0x19d) (got %f)", (double)w->p[0].hyper);
	while (w->p[0].state == 0x19d)
		tick(w, AOS_IN_B, 1);
	CHECK(w->p[0].state == 0x19e, "B held at t23 -> 0x19e follow-up (got %#x)", w->p[0].state);
	tick(w, AOS_IN_B, 4);
	CHECK(bit(w, 0x65)->f84 == 500 && bit(w, 0x69)->f84 == 500, "0x19e: cmd(0x226) sends Bits 0x65/0x69");
	CHECK(fabsf(w->p[0].hyper - 0.10f) < 1e-4f, "follow-up +0.05 hyper (row 0x19e)");

	/* hypers: command frames (§3) */
	struct { uint16_t held, press; int32_t mode, t; } h[] = {
		{0, AOS_IN_H, 300, 10}, {AOS_IN_B, AOS_IN_H, 400, 10}, {AOS_IN_S, AOS_IN_H, 0x15e, 1}, {AOS_IN_S | AOS_IN_B, AOS_IN_H, 600, 1},
	};
	for (size_t k = 0; k < sizeof h / sizeof h[0]; k++) {
		setup(w, 0);
		if (h[k].held)
			tick(w, h[k].held, 3);
		tick(w, h[k].held | h[k].press, 1);
		int tc = cmd_frame(w, h[k].mode, 0, 20);
		CHECK(tc == h[k].t, "hyper cmd(%#x) at t=%d (got %d)", h[k].mode, h[k].t, tc);
	}
}

/* ---- Spring Storm: 4 x beam 300 per Bit per volley at heading+π twice, −π/2, +π/2 (bug kept) ---- */
static void test_storm(void)
{
	aos_world *w = &W;
	setup(w, 0);
	tick(w, AOS_IN_S | AOS_IN_B, 3);
	tick(w, AOS_IN_S | AOS_IN_B | AOS_IN_H, 1);
	CHECK(w->p[0].state == 0x1a3 && w->p[0].barrierType == 4, "S+B+H -> 0x1a3 Accel");
	int volleys = 0, dupOk = 0, zeroBad = 0;
	for (int f = 0; f < 80; f++) {
		int nb0 = w->obj.nbeam;
		tick(w, 0, 1);
		if (w->obj.nbeam - nb0 < 28)
			continue;
		volleys++;
		for (int k = 0; k < 7; k++) { /* beams spawned per Bit in order, 4 each */
			const aos_beam *b = &w->obj.beam[nb0 + 4 * k];
			if (b[0].id != 300)
				continue;
			float d01 = b[0].angle - b[1].angle;
			if (fabsf(d01) < 1e-5f)
				dupOk++;
			/* heading = b[0].angle − π: none of the 4 points along the heading */
			for (int j = 0; j < 4; j++) {
				float d = b[j].angle - (b[0].angle - 3.1415927f);
				while (d > 3.1415927f) d -= 6.2831855f;
				while (d < -3.1415927f) d += 6.2831855f;
				if (fabsf(d) < 1e-3f)
					zeroBad++;
			}
		}
	}
	CHECK(volleys == 3, "3 volleys (T=10,15,20) (got %d)", volleys);
	CHECK(dupOk == 21, "every Bit fires two beams at heading+π (§5.1 mode 0x262 bug) (got %d/21)", dupOk);
	CHECK(zeroBad == 0, "no beam along the heading (+0) (got %d)", zeroBad);
	CHECK(count(w, 'l', 200) == 7 || count(w, 'l', 200) == 0, "7 lasers 200");
}

/* ---- §5.6 laser hitbox damage on a placed opponent ---- */
static void test_lasers(void)
{
	struct { int32_t id; int dmg; } L[] = {{100, 150}, {0x96, 320}, {200, 840}};
	for (size_t k = 0; k < 3; k++) {
		aos_world *w = &W;
		setup(w, 0);
		w->p[1].x = w->p[0].x + 400;
		w->p[1].y = w->p[0].y;
		int hp0 = w->p[1].hp;
		aos_enemy *e = bit(w, 100);
		aos_spawn_laser(w, AOS_CH_NANAKO, L[k].id, &w->p[0].x, 0.0f, e->id, w->p[0].side, w->p[0].color);
		for (int f = 0; f < 90; f++) {
			w->p[1].x = w->p[0].x + 400; /* keep the target in the beam */
			w->p[1].y = w->p[0].y;
			tick(w, 0, 1);
		}
		CHECK(hp0 - w->p[1].hp == L[k].dmg, "laser %#x total damage %d (§5.6) (got %d)", L[k].id, L[k].dmg, hp0 - w->p[1].hp);
	}
}

/* ---- §3 0x18f / §5.1 mode 900: KO -> Bit k self-destructs after 100−10k ---- */
static void test_ko(void)
{
	aos_world *w = &W;
	setup(w, 0);
	w->p[0].hp = 1;
	aos_damage(w, &w->p[0], 100, 2, 1.0f);
	CHECK(w->p[0].state == AOS_ST_KO, "Nanako KO");
	tick(w, 0, 1);
	CHECK(mode_count(w, 900) == 7, "KO hook cmd(900)");
	tick(w, 0, 42);
	CHECK(nbits(w) == 6, "Bit 6 gone after ~40 frames (100-60) (got %d)", nbits(w));
	tick(w, 0, 60);
	CHECK(nbits(w) == 0, "all Bits gone after 100 frames (got %d)", nbits(w));
}

static void test_random(void)
{
	for (int run = 0; run < 2; run++) {
		aos_world *w = run ? &W2 : &W;
		setup(w, run);
		uint32_t s = 4242;
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
		CHECK(w->obj.overflow == 0, "pool overflow %d (scheme %d)", w->obj.overflow, run);
		CHECK(nbits(w) == 7, "still 7 Bits (got %d)", nbits(w));
	}
}

int main(void)
{
	aos_register_all();
	test_intro();
	test_deploy();
	test_dispatch();
	test_states();
	test_storm();
	test_lasers();
	test_ko();
	test_random();
	printf("test_nanako: %s (%d failures)\n", fails ? "FAIL" : "ok", fails);
	return fails != 0;
}
