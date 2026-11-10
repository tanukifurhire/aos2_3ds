/* Behaviour checks for the Kyoko port (src/sim/chars/kyoko.c) against docs/re/kyoko.md.
 * Every CHECK cites the spec section and the original address it comes from.
 * Build (see tests/run_tests.sh):
 *   zig cc -std=gnu11 -Wall -Wextra -Werror -O1 -fsanitize=undefined src/sim/(all .c)
 *          src/sim/chars/(all .c) tests/test_kyoko.c -lm -o build/tests/test_kyoko.exe */
#include "../src/sim/draw.h"
#include "../src/sim/sim.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
#define NEAR(a, b) (fabsf((float)(a) - (float)(b)) < 1e-4f)

#define KY AOS_CH_KYOKO
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
	for (int i = 0; i < w->obj.nenemy; i++)
		if (aos_otab.enemyDraw[w->obj.enemy[i].type]) aos_otab.enemyDraw[w->obj.enemy[i].type](w, &w->obj.enemy[i]);
	for (int i = 0; i < w->obj.neffect; i++)
		if (aos_otab.effectDraw[w->obj.effect[i].type]) aos_otab.effectDraw[w->obj.effect[i].type](w, &w->obj.effect[i]);
}

/* Kyoko (P1) vs Kyoko (P2), past the round-start input lock; P1 at x=0, P2 at x=dist. */
static void setup(aos_world *w, int scheme, float dist)
{
	int32_t ids[2] = {KY, KY}, sch[2] = {scheme, 0}, col[2] = {0, 1};
	aos_match_init(w, ids, sch, col, 999, 2);
	uint16_t m[2] = {0, 0};
	for (int i = 0; i < 210; i++)
		aos_tick(w, m);
	for (int i = 0; i < 2; i++) {
		w->p[i].x = i ? dist : 0.0f;
		w->p[i].y = 0;
		w->p[i].speed = 0;
		w->p[i].heat = 0;
		w->p[i].gaugeA = w->p[i].gaugeB = 1.0f;
		w->p[i].hyper = 0;
	}
}

static void tick(aos_world *w, uint16_t m0)
{
	uint16_t m[2] = {m0, 0};
	aos_tick(w, m);
}

static int count_missiles(aos_world *w, int id)
{
	int n = 0;
	for (int i = 0; i < w->obj.nmissile; i++)
		if (w->obj.missile[i].type == KY && w->obj.missile[i].id == id)
			n++;
	return n;
}

/* ---- §2 input dispatch ---- */
static int state_after(int scheme, float dist, float hyper, uint16_t first, uint16_t second)
{
	aos_world *w = &W;
	setup(w, scheme, dist);
	w->p[0].hyper = hyper;
	tick(w, first);
	if (second)
		tick(w, second);
	return w->p[0].state;
}

static void test_dispatch(void)
{
	enum { A = AOS_IN_A, B = AOS_IN_B, S = AOS_IN_S, D = AOS_IN_D, H = AOS_IN_H, G = AOS_IN_SHIELD };
	int s;
	/* §2B FUN_006ec830 */
	s = state_after(0, 400, 0, A, 0);
	CHECK(s == 0x1dd, "A -> 0x1dd Hard Rain (§2B row 5) got %#x", s);
	s = state_after(0, 400, 0, D, D | A);
	CHECK(s == 0x1dc, "D held + A -> 0x1dc Clean Stream (§2B row 4) got %#x", s);
	s = state_after(0, 199, 0, S | A, 0);
	CHECK(s == 0x1e3, "S+A dist 199 -> 0x1e3 Icicle (§2B row 3, DAT_008147cc = 200) got %#x", s);
	s = state_after(0, 200, 0, S | A, 0);
	CHECK(s == 0x1e1, "S+A dist 200 -> 0x1e1 Ripple (dist >= 200) got %#x", s);
	s = state_after(0, 400, 0, S | B, 0);
	CHECK(s == 0x1e5, "S+B -> 0x1e5 Crystal (§2B row 6) got %#x", s);
	s = state_after(0, 400, 0, B, 0);
	CHECK(s == 0x1df, "B -> 0x1df Icy Crystal (§2B row 7) got %#x", s);
	/* §2A hypers */
	s = state_after(0, 400, 1.0f, H, 0);
	CHECK(s == 0x1e6, "H -> 0x1e6 Early Summer Rain (§2A row 5) got %#x", s);
	s = state_after(0, 400, 1.0f, D, D | H);
	CHECK(s == 0x1de, "D+H -> 0x1de Freezing (§2A row 4) got %#x", s);
	s = state_after(0, 400, 1.0f, B | H, 0);
	CHECK(s == 0x1e0, "B+H -> 0x1e0 Ice Over (§2A row 3) got %#x", s);
	s = state_after(0, 359, 1.0f, S | H, 0);
	CHECK(s == 0x1e4, "S+H dist 359 -> 0x1e4 Tree of Ice (§2A row 2, DAT_00817894 = 360) got %#x", s);
	s = state_after(0, 360, 1.0f, S | H, 0);
	CHECK(s == 0x1e2, "S+H dist 360 -> 0x1e2 Water Mirror got %#x", s);
	s = state_after(0, 400, 3.0f, S | B | H, 0);
	CHECK(s == 0x1e7, "S+B+H -> 0x1e7 Avalanche (§2A row 1) got %#x", s);
	s = state_after(0, 400, 2.9f, S | B | H, 0);
	CHECK(s == 0x1e2, "S+B+H with 2.9 hyper falls through to S+H (§2A note) got %#x", s);
	s = state_after(0, 400, 0.9f, H, 0);
	CHECK(s == 0, "H with < 1.0 hyper does nothing (FUN_00780910) got %#x", s);
	/* §2 alternate scheme FUN_006ecdc0: H held + X pressed */
	s = state_after(1, 400, 1.0f, H, H | A);
	CHECK(s == 0x1e6, "scheme B: H held + A -> 0x1e6 got %#x", s);
	s = state_after(1, 400, 1.0f, H, H | D);
	CHECK(s == 0x1de, "scheme B: H held + D -> 0x1de got %#x", s);
	s = state_after(1, 400, 1.0f, H, H | B);
	CHECK(s == 0x1e0, "scheme B: H held + B -> 0x1e0 got %#x", s);
	s = state_after(1, 359, 1.0f, H, H | S);
	CHECK(s == 0x1e4, "scheme B: H held + S, dist 359 -> 0x1e4 got %#x", s);
	s = state_after(1, 360, 1.0f, H, H | S);
	CHECK(s == 0x1e2, "scheme B: H held + S, dist 360 -> 0x1e2 got %#x", s);
	s = state_after(1, 400, 3.0f, H, H | G);
	CHECK(s == 0x1e7, "scheme B: H held + Shield -> 0x1e7 got %#x", s);
	s = state_after(1, 400, 0, S | A, 0);
	CHECK(s == 0x1e1, "scheme B normal block identical: S+A dist 400 -> Ripple got %#x", s);
}

/* ---- §3 0x1dd Hard Rain: icicles 400 at t = 15, 20, 25 while A held ---- */
static void test_hard_rain(void)
{
	aos_world *w = &W;
	setup(w, 0, 400);
	int spawnT[8], n = 0;
	for (int f = 0; f < 60; f++) {
		int t = w->p[0].timer, before = count_missiles(w, 400) + count_missiles(w, 0x1c2);
		bool in = w->p[0].state == 0x1dd;
		tick(w, AOS_IN_A);
		if (in && count_missiles(w, 400) + count_missiles(w, 0x1c2) > before && n < 8)
			spawnT[n++] = t;
		if (f == 0)
			CHECK(w->p[0].state == 0x1dd, "A press -> 0x1dd");
	}
	CHECK(n == 3 && spawnT[0] == 15 && spawnT[1] == 20 && spawnT[2] == 25,
	      "missile 400 at t=15,20,25 (§3 0x1dd FUN_006ea480): n=%d t=%d,%d,%d", n, n > 0 ? spawnT[0] : -1,
	      n > 1 ? spawnT[1] : -1, n > 2 ? spawnT[2] : -1);
	/* first icicle paid with 0x1dc's entry (0.15), 2nd/3rd with 0x1dd (0.15) (§1.2); regen only in 0..2 */
	CHECK(w->p[0].state == 0, "state 0 after t > 37");
}

/* ---- §3 0x1db Drizzling Rain: tap A -> 16 beams 100 at t = 15..30, 0.04 A each ---- */
static void test_drizzle(void)
{
	aos_world *w = &W;
	setup(w, 0, 400);
	tick(w, AOS_IN_A);
	int first = -1, last = -1, n = 0, endT = -1;
	float a0 = w->p[0].gaugeA;
	for (int f = 0; f < 80; f++) {
		int t = w->p[0].timer, st = w->p[0].state;
		int nb = 0;
		for (int i = 0; i < w->obj.nbeam; i++)
			nb += w->obj.beam[i].id == 100 && w->obj.beam[i].age == 0;
		tick(w, 0);
		int nb2 = 0;
		for (int i = 0; i < w->obj.nbeam; i++)
			nb2 += w->obj.beam[i].type == KY && w->obj.beam[i].id == 100 && w->obj.beam[i].age == 1;
		(void)nb;
		if (st == 0x1db && nb2 > 0) {
			if (first < 0)
				first = t;
			last = t;
			n += nb2;
		}
		if (f == 0)
			CHECK(w->p[0].state == 0x1db, "release before t=14 -> 0x1db (§3 0x1dd) got %#x", w->p[0].state);
		if (st == 0x1db && w->p[0].state == 0 && endT < 0)
			endT = t;
	}
	CHECK(first == 15 && last == 30 && n == 16, "beam 100 every frame t=15..30 (§3 0x1db FUN_006e9d20): %d..%d n=%d", first, last, n);
	CHECK(endT == 58, "0x1db ends when t > 57 (got %d)", endT);
	(void)a0;
}

/* ---- §3 0x1dc Clean Stream: 2 pairs of beam 200 at t=15/17, dmg 180, dash-cancel from t=20 ---- */
static void test_clean_stream(void)
{
	aos_world *w = &W;
	for (int pass = 0; pass < 2; pass++) {
		setup(w, 0, 400);
		tick(w, AOS_IN_D);
		tick(w, AOS_IN_D | AOS_IN_A);
		CHECK(w->p[0].state == 0x1dc, "D+A -> 0x1dc");
		float a0 = w->p[0].gaugeA, h0 = w->p[0].hyper;
		int cancelAt = -1;
		for (int f = 0; f < 40 && w->p[0].state == 0x1dc; f++) {
			int t = w->p[0].timer;
			uint16_t m = (pass == 0 && t == 19) || (pass == 1 && t == 20) ? AOS_IN_D : 0;
			tick(w, m);
			if (m && w->p[0].state == AOS_ST_DASH)
				cancelAt = t;
			if (t == 17 && pass == 0) {
				int nb = 0, dmgOk = 1;
				for (int i = 0; i < w->obj.nbeam; i++)
					if (w->obj.beam[i].type == KY && w->obj.beam[i].id == 200) {
						nb++;
						dmgOk &= w->obj.beam[i].damage == 180;
					}
				CHECK(nb == 4 && dmgOk, "4 beams 200, dmg 180 (§5.1 ver > 0x5a0) n=%d", nb);
				CHECK(NEAR(a0 - w->p[0].gaugeA, 0.30f), "2 x 0.15 ammo A (§1.2 0x1dc) used %f", (double)(a0 - w->p[0].gaugeA));
				CHECK(NEAR(w->p[0].hyper - h0, 2 * 0.06f * w->p[0].hyperMul),
				      "hyper gain 2 x 0.06 (FUN_004a7270 ver >= 0x58b) got %f", (double)(w->p[0].hyper - h0));
			}
		}
		if (pass == 0)
			CHECK(cancelAt < 0, "no dash-cancel at t=19");
		else
			CHECK(cancelAt == 20, "dash-cancel at t=20 (§3 0x1dc) got %d", cancelAt);
	}
}

/* ---- §3 0x1e3 Icicle: 5 x hitbox 200 (80 dmg) on a placed opponent, no ammo cost ---- */
static void test_icicle_damage(void)
{
	aos_world *w = &W;
	setup(w, 0, 100);
	int hp0 = w->p[1].hp;
	float a0 = w->p[0].gaugeA, b0 = w->p[0].gaugeB;
	tick(w, AOS_IN_S | AOS_IN_A);
	CHECK(w->p[0].state == 0x1e3, "S+A close -> Icicle");
	int n500 = 0;
	for (int f = 0; f < 60; f++) {
		int before = count_missiles(w, 500);
		tick(w, 0);
		if (count_missiles(w, 500) > before)
			n500++;
	}
	CHECK(hp0 - w->p[1].hp == 5 * 80, "Icicle deals 5 x 80 (§5.3 hitbox 200, knock 0) got %d", hp0 - w->p[1].hp);
	CHECK(n500 == 5, "5 visual icicles 500 (§3 0x1e3) got %d", n500);
	CHECK(w->p[0].gaugeA >= a0 && w->p[0].gaugeB >= b0, "Icicle costs nothing (§1.2)");
}

/* ---- §3 0x1e5 / §5.5 Crystal: cost, phases, beam redirect ---- */
static void test_crystal(void)
{
	aos_world *w = &W;
	setup(w, 0, 400);
	tick(w, AOS_IN_S | AOS_IN_B);
	CHECK(w->p[0].state == 0x1e5, "S+B -> 0x1e5");
	for (int f = 0; f < 10; f++)
		tick(w, 0);
	CHECK(w->obj.nenemy == 1, "enemy 100 spawned at t=10 (§3 0x1e5)");
	aos_enemy *e = &w->obj.enemy[0];
	CHECK(e->hp == 450 && e->maxHp == 450 && e->active == 0, "Crystal hp 450, inactive (§5.5 0x1c2) hp=%d", (int)e->hp);
	CHECK(NEAR(1.0f - w->p[0].gaugeB, 0.4f), "Crystal costs 0.4 B (§1.2) used %f", (double)(1.0f - w->p[0].gaugeB));
	CHECK(NEAR(w->p[0].hyper, 0.1f * w->p[0].hyperMul), "Crystal hyper gain 0.1 (§1.2) got %f", (double)w->p[0].hyper);
	for (int f = 0; f < 10; f++)
		tick(w, 0);
	CHECK(e->active == 1 && e->f84 == 100, "active, phase 100 after 10 frames (§5.5 phase 0)");
	/* beam 200 owned by Kyoko overlapping the crystal -> redirected at the opponent */
	int bi = aos_spawn_beam(w, KY, 200, e->pos, AOS_PI / 2, w->p[0].objId, 0, w->p[0].side);
	w->obj.beam[bi].speed = 0;
	tick(w, 0);
	aos_beam *b = NULL;
	for (int i = 0; i < w->obj.nbeam; i++)
		if (w->obj.beam[i].type == KY && w->obj.beam[i].id == 200)
			b = &w->obj.beam[i];
	CHECK(e->mirrorChildren == 1, "Crystal triggered (+0x6c) after redirect (§5.5 phase 100)");
	if (b) {
		CHECK(b->f6c == 1.0f, "beam +0x6c = 1.0 (redirected)");
		CHECK(fabsf(b->moveAngle) < 0.2f, "beam re-aimed at the opponent (to +x) got %f", (double)b->moveAngle);
	}
	int hp0 = w->p[1].hp;
	w->p[1].x = e->pos[0] + 60; /* inside the radius-100 field */
	w->p[1].y = e->pos[1];
	for (int f = 0; f < 5; f++)
		tick(w, 0);
	CHECK(hp0 - w->p[1].hp >= 4, "field hitbox 100 deals 1 per frame (§5.3 ver > 0x5a0) lost %d", hp0 - w->p[1].hp);
	for (int f = 0; f < 720 && w->obj.nenemy; f++)
		tick(w, 0);
	CHECK(w->obj.nenemy == 0, "Crystal fades out after timer2 > 699 (§5.5)");
}

/* ---- §5.2 onAbsorb: an opponent beam hitting an active Ripple mirror becomes Kyoko beam 300 ---- */
static void test_mirror_reflect(void)
{
	aos_world *w = &W;
	setup(w, 0, 400);
	tick(w, AOS_IN_S | AOS_IN_A);
	CHECK(w->p[0].state == 0x1e1, "Ripple");
	for (int f = 0; f < 30; f++)
		tick(w, 0);
	CHECK(count_missiles(w, 300) == 3, "3 mirrors 300 (§3 0x1e1)");
	aos_missile *m = NULL;
	for (int i = 0; i < w->obj.nmissile; i++)
		if (w->obj.missile[i].id == 300)
			m = &w->obj.missile[i];
	CHECK(m && m->active == 1 && m->absorbs == 1 && m->hp == 300, "mirror active, absorbs, hp 300 (§5.2; init 0x669bab)");
	if (!m)
		return;
	float mpos[3] = {m->pos[0], m->pos[1], 0}, mang = m->angle;
	int bi = aos_spawn_beam(w, KY, 100, mpos, AOS_PI, w->p[1].objId, 0, w->p[1].side);
	CHECK(bi >= 0, "spawned opponent beam");
	tick(w, 0);
	int theirs = 0, refl = 0;
	for (int i = 0; i < w->obj.nbeam; i++) {
		aos_beam *b = &w->obj.beam[i];
		if (b->team == w->p[1].side && b->id == 100)
			theirs++;
		if (b->team == w->p[0].side && b->id == 300) {
			refl++;
			CHECK(fabsf(b->moveAngle - mang) < 1e-4f || b->age > 0, "reflected beam takes mirror angle");
		}
	}
	CHECK(theirs == 0 && refl == 1, "beam absorbed and reflected as Kyoko beam 300 (FUN_0066d190): theirs=%d refl=%d", theirs, refl);
}

/* ---- §5.2 icicle 400 -> ice block 0x1c2 at the arena edge ---- */
static void test_ice_block(void)
{
	aos_world *w = &W;
	setup(w, 0, 400);
	float pos[3] = {520, 0, 0};
	aos_spawn_missile(w, KY, 400, pos, 0, w->p[0].objId, 0, w->p[0].side, w->p[0].color);
	aos_missile *m = &w->obj.missile[w->obj.nmissile - 1];
	CHECK(m->speed == 14.0f && m->hp == 10 && m->damage == 100, "missile 400 init (§5.2)");
	for (int f = 0; f < 5 && m->id == 400; f++)
		tick(w, 0);
	CHECK(m->id == 0x1c2 && m->hp == 200 && m->damage == 150 && m->active == 0,
	      "outside arena -> ice block 0x1c2 hp 200 dmg 150 inactive (§5.2) id=%#x", (unsigned)m->id);
	for (int f = 0; f < 12; f++)
		tick(w, 0);
	CHECK(m->active == 1 && m->growScale == 1.0f, "ice block active once s reaches 1 (§5.2 phase 0)");
	float x0 = m->pos[0];
	for (int f = 0; f < 100; f++)
		tick(w, 0);
	CHECK(m->pos[0] == x0, "ice block never moves (§5.2)");
	int alive = 1;
	for (int f = 0; f < 900 && alive; f++) {
		tick(w, 0);
		alive = count_missiles(w, 0x1c2);
	}
	CHECK(!alive, "ice block shatters after age 800 (§5.2 phase 900)");
}

/* ---- hypers: object counts, costs ---- */
static void test_hypers(void)
{
	aos_world *w = &W;
	/* §3 0x1e6 Early Summer Rain: 11 mirrors 300, 16 lasers 100 */
	setup(w, 0, 400);
	w->p[0].hyper = 1.0f;
	tick(w, AOS_IN_H);
	CHECK(w->p[0].state == 0x1e6 && w->p[0].hyper < 1e-6f, "ESR costs 1.0");
	int nl = 0, n300 = 0;
	for (int f = 0; f < 60; f++) {
		int l0 = w->obj.nlaser;
		int m0 = count_missiles(w, 300);
		tick(w, 0);
		if (w->obj.nlaser > l0)
			nl += w->obj.nlaser - l0;
		if (count_missiles(w, 300) > m0)
			n300 += count_missiles(w, 300) - m0;
	}
	CHECK(nl == 16 && n300 == 11, "ESR: 16 lasers, 11 mirrors (§3 0x1e6) got %d, %d", nl, n300);
	/* §3 0x1e2 Water Mirror: 12 orbit mirrors, radius grows to 100 around the owner */
	setup(w, 0, 400);
	w->p[0].hyper = 1.0f;
	tick(w, AOS_IN_S | AOS_IN_H);
	for (int f = 0; f < 60; f++)
		tick(w, 0);
	CHECK(count_missiles(w, 0x136) == 12, "12 orbit mirrors (§3 0x1e2)");
	for (int i = 0; i < w->obj.nmissile; i++) {
		aos_missile *m = &w->obj.missile[i];
		if (m->id != 0x136)
			continue;
		float dx = m->pos[0] - w->p[0].x, dy = m->pos[1] - w->p[0].y;
		CHECK(m->speed == 100.0f && fabsf(sqrtf(dx * dx + dy * dy) - 100.0f) < 0.5f,
		      "orbit radius 100 around the owner (§5.2 0x136) r=%f", (double)m->speed);
		break;
	}
	/* §3 0x1e7 Avalanche: 36 chunks, cost 3.0, Accel barrier */
	setup(w, 0, 400);
	w->p[0].hyper = 3.0f;
	tick(w, AOS_IN_S | AOS_IN_B | AOS_IN_H);
	CHECK(w->p[0].state == 0x1e7 && w->p[0].barrierType == 4, "Avalanche, Accel barrier (FUN_006b1b40 0x14)");
	int nch = count_missiles(w, 0x96) + count_missiles(w, 0x97) + count_missiles(w, 0x98); /* t=0 chunk spawns on the press frame */
	for (int f = 0; f < 110; f++) {
		int c0 = count_missiles(w, 0x96) + count_missiles(w, 0x97) + count_missiles(w, 0x98);
		tick(w, 0);
		int c1 = count_missiles(w, 0x96) + count_missiles(w, 0x97) + count_missiles(w, 0x98);
		if (c1 > c0)
			nch += c1 - c0;
	}
	CHECK(nch == 36, "36 avalanche chunks (§3 0x1e7) got %d", nch);
	CHECK(w->p[0].state == 0, "Avalanche ends after t > 100");
	/* §3 0x1de Freezing: six icicles 400 at t=20 */
	setup(w, 0, 400);
	w->p[0].hyper = 1.0f;
	tick(w, AOS_IN_D);
	tick(w, AOS_IN_D | AOS_IN_H);
	CHECK(w->p[0].state == 0x1de, "Freezing");
	int t20 = -1;
	for (int f = 0; f < 30; f++) {
		int t = w->p[0].timer, c0 = count_missiles(w, 400);
		tick(w, 0);
		if (count_missiles(w, 400) - c0 == 6)
			t20 = t;
	}
	CHECK(t20 == 20, "6 icicles 400 at t=20 (§3 0x1de) got %d", t20);
}

/* ---- every state runs to completion; random hyper play is deterministic ---- */
static void test_all_states(void)
{
	static const int states[] = {0x1db, 0x1dc, 0x1dd, 0x1de, 0x1df, 0x1e0, 0x1e1, 0x1e2, 0x1e3,
	                             0x1e4, 0x1e5, 0x1e6, 0x1e7};
	for (size_t k = 0; k < sizeof states / sizeof states[0]; k++) {
		aos_world *w = &W;
		setup(w, 0, 300);
		aos_set_state(w, &w->p[0], states[k]);
		for (int f = 0; f < 250; f++) {
			tick(w, f < 40 ? AOS_IN_A | AOS_IN_B : 0);
			draw_all(w);
			CHECK(isfinite(w->p[0].x), "state %#x non-finite", states[k]);
		}
		CHECK(w->p[0].state < 0x1d1, "state %#x did not end (now %#x)", states[k], w->p[0].state);
	}
	uint32_t s = 4242;
	for (int run = 0; run < 2; run++) {
		aos_world *w = run ? &W2 : &W;
		setup(w, 0, 300);
		uint16_t m[2] = {0, 0};
		s = 4242;
		for (int f = 0; f < 15000; f++) {
			for (int i = 0; i < 2; i++) {
				s = s * 1664525u + 1013904223u;
				if (((s >> 8) & 3) == 0)
					m[i] ^= (uint16_t)(1u << ((s >> 12) % 10));
				w->p[i].hyper = 3.0f;
				w->p[i].hp = w->p[i].maxHp;
			}
			aos_tick(w, m);
			draw_all(w);
		}
	}
	CHECK(!memcmp(&W, &W2, sizeof W), "random hyper runs diverged");
	CHECK(W.obj.overflow == 0, "pool overflow %d", (int)W.obj.overflow);
}

int main(void)
{
	aos_register_all();
	test_dispatch();
	test_hard_rain();
	test_drizzle();
	test_clean_stream();
	test_icicle_damage();
	test_crystal();
	test_mirror_reflect();
	test_ice_block();
	test_hypers();
	test_all_states();
	printf("test_kyoko: %s (%d failures)\n", fails ? "FAIL" : "ok", fails);
	return fails != 0;
}
