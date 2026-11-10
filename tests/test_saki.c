/* Behaviour checks for the Saki port (src/sim/chars/saki.c), asserted against
 * docs/re/saki.md (section numbers / original addresses cited per check).
 * Build: tests/run_tests.sh (zig cc, all src/sim and src/sim/chars sources + this file). */
#include "../src/sim/draw.h"
#include "../src/sim/sim.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

enum { SAKI = AOS_CH_SAKI };
static aos_world W, W2;

static void draw_all(aos_world *w)
{
	aos_draw_begin();
	aos_draw_players(w);
	for (int i = 0; i < w->obj.nbeam; i++)
		if (aos_otab.beamDraw[w->obj.beam[i].type]) aos_otab.beamDraw[w->obj.beam[i].type](w, &w->obj.beam[i]);
	for (int i = 0; i < w->obj.nmissile; i++)
		if (aos_otab.missileDraw[w->obj.missile[i].type]) aos_otab.missileDraw[w->obj.missile[i].type](w, &w->obj.missile[i]);
	for (int i = 0; i < w->obj.neffect; i++)
		if (aos_otab.effectDraw[w->obj.effect[i].type]) aos_otab.effectDraw[w->obj.effect[i].type](w, &w->obj.effect[i]);
}

/* Saki (P1, scheme `sch`) vs Saki (P2) at distance `dx`, past the 200-frame input lock. */
static void setup(aos_world *w, int sch, float dx)
{
	int32_t ids[2] = {SAKI, SAKI}, schs[2] = {sch, 0}, col[2] = {0, 1};
	aos_match_init(w, ids, schs, col, 999, 2);
	uint16_t m[2] = {0, 0};
	for (int i = 0; i < 210; i++)
		aos_tick(w, m);
	w->p[0].x = -dx * 0.5f;
	w->p[1].x = dx * 0.5f;
	w->p[0].y = w->p[1].y = 0;
	w->p[0].heat = w->p[1].heat = 0;
}

static void tick(aos_world *w, uint16_t m0)
{
	uint16_t m[2] = {m0, 0};
	aos_tick(w, m);
}

/* Hold `hold` one frame, then hold it and press `press`. */
static void enter(aos_world *w, uint16_t hold, uint16_t press)
{
	if (hold)
		tick(w, hold);
	tick(w, hold | press);
}

/* ---- §2 input dispatch: call the dispatch function on hand-set button values ---- */
static int dispatch(int sch, int D, int A, int B, int S, int H, int Sh, float hyper)
{
	aos_world *w = &W;
	setup(w, sch, 400);
	aos_player *p = &w->p[0];
	p->state = 0;
	p->timer = 0;
	p->hyper = hyper;
	p->D = D; p->A = A; p->B = B; p->S = S; p->H = H; p->Shield = Sh;
	(sch ? aos_tab.inputB : aos_tab.inputA)[SAKI](w, p);
	return p->state;
}

static void test_dispatch(void)
{
	/* standard scheme FUN_00722ce0 (saki.md §2) */
	CHECK(dispatch(0, 0, 3, 0, 0, 0, 0, 0) == 0x15d, "A -> Tornado Cymbal 0x15d");
	CHECK(dispatch(0, 2, 3, 0, 0, 0, 0, 0) == 0x15e, "D held + A -> Stardust Bell 0x15e");
	CHECK(dispatch(0, 0, 0, 3, 0, 0, 0, 0) == 0x15f, "B -> 0x15f");
	CHECK(dispatch(0, 0, 0, 3, 2, 0, 0, 0) == 0x161, "S+B -> Tambourine Blade 0x161");
	CHECK(dispatch(0, 0, 3, 0, 2, 0, 0, 0) == 0x162, "S+A -> Maracas Hammer 0x162");
	CHECK(dispatch(0, 2, 3, 0, 2, 0, 0, 0) == 0x164, "D+S+A -> Maracas Swing 0x164 (row 4 before S+A)");
	CHECK(dispatch(0, 1, 3, 0, 0, 0, 0, 0) == 0x15d, "Dash value 1 is not 'held' (>1) -> 0x15d");
	CHECK(dispatch(0, 0, 0, 0, 0, 3, 0, 1.0f) == 0x165, "H -> Cyclone Cymbal 0x165");
	CHECK(dispatch(0, 2, 0, 0, 0, 3, 0, 1.0f) == 0x166, "D+H -> Galaxy Bell 0x166");
	CHECK(dispatch(0, 0, 0, 2, 0, 3, 0, 1.0f) == 0x167, "B+H -> Sunshine Bell 0x167");
	CHECK(dispatch(0, 0, 0, 0, 2, 3, 0, 1.0f) == 0x168, "S+H -> Rolling Tambourine 0x168");
	CHECK(dispatch(0, 0, 0, 2, 2, 3, 0, 3.0f) == 0x169, "S+B+H (3.0) -> Big Bang Bell 0x169");
	CHECK(dispatch(0, 0, 0, 2, 2, 3, 0, 2.0f) == 0x168, "S+B+H with 2.0 falls through to 0x168");
	CHECK(dispatch(0, 0, 0, 0, 0, 3, 0, 0.5f) == 0, "H without a level does nothing");
	/* alternate scheme FUN_00723200: H held + X pressed */
	CHECK(dispatch(1, 0, 0, 0, 0, 3, 0, 1.0f) == 0, "scheme B: plain H does nothing");
	CHECK(dispatch(1, 0, 3, 0, 0, 2, 0, 1.0f) == 0x165, "scheme B: H+A -> 0x165");
	CHECK(dispatch(1, 3, 0, 0, 0, 2, 0, 1.0f) == 0x166, "scheme B: H+D -> 0x166");
	CHECK(dispatch(1, 0, 0, 3, 0, 2, 0, 1.0f) == 0x167, "scheme B: H+B -> 0x167");
	CHECK(dispatch(1, 0, 0, 0, 3, 2, 0, 1.0f) == 0x168, "scheme B: H+S -> 0x168");
	CHECK(dispatch(1, 0, 0, 0, 0, 2, 3, 3.0f) == 0x169, "scheme B: H+Shield (3.0) -> 0x169");
	CHECK(dispatch(1, 0, 3, 0, 0, 0, 0, 1.0f) == 0x15d, "scheme B: normal moves unchanged");
}

/* Run the current state to its end; returns the state timer at which beams/missiles/hitboxes
 * first appeared and the timer value at which the state ended. */
typedef struct { int firstBeam, firstMissile, beams, missiles, endT, maxHit; } run_info;
static run_info run_state(aos_world *w, uint16_t m0, int frames)
{
	run_info r = {-1, -1, 0, 0, -1, 0};
	int s = w->p[0].state;
	for (int f = 0; f < frames; f++) {
		int t = w->p[0].timer, nb = w->obj.nbeam, nm = w->obj.nmissile;
		tick(w, m0);
		draw_all(w);
		if (w->obj.nbeam > nb) { if (r.firstBeam < 0) r.firstBeam = t; r.beams += w->obj.nbeam - nb; }
		if (w->obj.nmissile > nm) { if (r.firstMissile < 0) r.firstMissile = t; r.missiles += w->obj.nmissile - nm; }
		if (r.endT < 0 && w->p[0].state != s) { r.endT = t; break; }
	}
	return r;
}

static void test_tornado(void) /* §3 0x15d FUN_0071fe50; §1.2 cost row 0x15d */
{
	aos_world *w = &W;
	setup(w, 0, 400);
	enter(w, 0, AOS_IN_A);
	CHECK(w->p[0].state == 0x15d, "A -> 0x15d");
	run_info r = run_state(w, 0, 60);
	CHECK(r.firstBeam == 14 && r.beams == 36, "36 cymbals at t=14 (t=%d n=%d)", r.firstBeam, r.beams);
	CHECK(fabsf(w->p[0].gaugeA - (1.0f - 0.4f)) < 0.02f, "gauge A -0.4 (now %f)", w->p[0].gaugeA);
	CHECK(r.endT == 31, "state ends when t>30 (ended at %d)", r.endT);
	int n300 = 0, n136 = 0;
	for (int i = 0; i < w->obj.nbeam; i++) {
		aos_beam *b = &w->obj.beam[i];
		if (b->type != SAKI) continue;
		n300 += b->id == 300;
		n136 += b->id == 0x136;
		CHECK(b->damage == 120 && b->shape.radius == 10.0f, "cymbal dmg 120 r10 (§5.1)");
	}
	CHECK(n300 == 18 && n136 == 18, "18 each of 300/0x136 (%d/%d)", n300, n136);
	/* dash-cancel from t=20 (saki.md §3 0x15d) */
	setup(w, 0, 400);
	enter(w, 0, AOS_IN_A);
	while (w->p[0].timer < 19) tick(w, 0);
	tick(w, AOS_IN_D); /* T2 sees t=19: no cancel */
	CHECK(w->p[0].state == 0x15d, "no dash-cancel at t=19");
	tick(w, 0);
	tick(w, AOS_IN_D); /* t=21 */
	CHECK(w->p[0].state == AOS_ST_DASH, "dash-cancel at t>=20 (state %#x)", w->p[0].state);
}

static void test_stardust(void) /* §3 0x15e FUN_00720090 */
{
	aos_world *w = &W;
	setup(w, 0, 400);
	tick(w, AOS_IN_D); /* dash */
	tick(w, AOS_IN_D | AOS_IN_A);
	CHECK(w->p[0].state == 0x15e, "D(held, dashing)+A -> 0x15e (got %#x)", w->p[0].state);
	run_info r = run_state(w, 0, 60);
	CHECK(r.firstBeam == 10 && r.beams == 4, "4 bells from t=10 (t=%d n=%d)", r.firstBeam, r.beams);
	int nb = 0;
	for (int i = 0; i < w->obj.nbeam; i++) {
		aos_beam *b = &w->obj.beam[i];
		if (b->type == SAKI && (b->id == 200 || b->id == 0xd2)) {
			nb++;
			CHECK(b->active == 0 && b->damage == 100 && fabsf(b->stun - 0.4f) < 1e-6f, "bell inactive, 100 dmg, stun .4 (§5.1)");
		}
	}
	CHECK(nb == 4, "4 bells alive (%d)", nb);
}

static void test_grenade_boomerang(void) /* §3 0x15f FUN_00720270 / 0x160 FUN_00720590 */
{
	aos_world *w = &W;
	setup(w, 0, 400);
	enter(w, 0, AOS_IN_B);
	run_info r = run_state(w, AOS_IN_B, 80);
	CHECK(r.firstMissile == 14 && r.missiles == 8, "8 maracas t=14..21 (t=%d n=%d)", r.firstMissile, r.missiles);
	CHECK(fabsf(w->p[0].gaugeB - 0.6f) < 0.03f, "gauge B -8*0.05 (now %f)", w->p[0].gaugeB);
	CHECK(r.endT == 51, "0x15f ends t>50 (%d)", r.endT);

	setup(w, 0, 400);
	enter(w, 0, AOS_IN_B);
	tick(w, 0); /* release: B = 1 ("released") is not < 1 yet */
	CHECK(w->p[0].state == 0x15f, "B value 1 keeps 0x15f");
	tick(w, 0); /* B = 0 at t=2 -> 0x160 (timer kept) */
	CHECK(w->p[0].state == 0x160, "B released before t=13 -> 0x160 (got %#x)", w->p[0].state);
	r = run_state(w, 0, 80);
	CHECK(r.firstMissile == 12 && r.missiles == 1, "boomerang at t=12 (t=%d n=%d)", r.firstMissile, r.missiles);
	CHECK(r.endT == 41, "0x160 ends t>40 (%d)", r.endT);
	aos_missile *m = &w->obj.missile[0];
	CHECK(m->id == 200 && m->active == 0, "boomerang missile 200, inactive (§5.3)");
}

static void test_hammer_damage(void) /* §3 0x162, §5.2 hitbox 300 (dmg 250, crit 450) */
{
	int hits = 0, crits = 0;
	for (int k = 0; k < 40; k++) {
		aos_world *w = &W;
		setup(w, 0, 100);
		for (int j = 0; j < k; j++) aos_rand_real(w, 0, 1); /* vary the RNG stream */
		int hp0 = w->p[1].hp;
		enter(w, AOS_IN_S, AOS_IN_A);
		CHECK(w->p[0].state == 0x162, "S+A -> 0x162");
		for (int f = 0; f < 12; f++) tick(w, 0);
		int d = hp0 - w->p[1].hp;
		CHECK(d == 250 || d == 450, "hammer damage 250 or crit 450 (got %d)", d);
		if (d > 0) hits++;
		if (d == 450) {
			crits++;
			CHECK(w->p[0].state == AOS_ST_HITSTUN && w->p[0].speed > 10.0f,
			      "crit knocks Saki herself back (FUN_006b1d70), state %#x", w->p[0].state);
		}
	}
	CHECK(hits == 40, "every hammer hit (%d)", hits);
	CHECK(crits > 0 && crits < 15, "crit rate ~10/101 (%d/40)", crits);
}

static void test_blade_confirm(void) /* §3 0x161: hit-confirm (hitbox 0x82) + A -> 0x162 */
{
	aos_world *w = &W;
	setup(w, 0, 60);
	enter(w, AOS_IN_S, AOS_IN_B);
	CHECK(w->p[0].state == 0x161, "S+B -> 0x161 (got %#x)", w->p[0].state);
	int hp0 = w->p[1].hp;
	while (w->p[0].timer < 10) tick(w, 0);
	CHECK(w->p[0].hitFlag == 1, "tambourine 0x82 sets hit-confirm");
	CHECK(hp0 - w->p[1].hp >= 25, "tambourine hits for 25 (§5.2) (dealt %d)", hp0 - w->p[1].hp);
	tick(w, AOS_IN_A);
	CHECK(w->p[0].state == 0x162, "confirm + A -> Maracas Hammer (got %#x)", w->p[0].state);
}

static void test_swing(void) /* §3 0x164 */
{
	aos_world *w = &W;
	setup(w, 0, 400);
	tick(w, AOS_IN_D);
	tick(w, AOS_IN_D | AOS_IN_S);
	tick(w, AOS_IN_D | AOS_IN_S | AOS_IN_A);
	CHECK(w->p[0].state == 0x164, "D+S+A -> 0x164 (got %#x)", w->p[0].state);
	for (int f = 0; f < 60 && w->p[0].state == 0x164; f++) {
		int t = w->p[0].timer;
		tick(w, 0);
		if (t == 10) CHECK(w->p[0].speed > 11.0f, "speed 12 at t=10 (%f)", w->p[0].speed);
		if (t == 50) CHECK(w->p[0].state == 0x164, "still in state at t=50");
	}
	CHECK(w->p[0].state == 0, "swing ended (state %#x)", w->p[0].state);
}

static int count_ids(aos_world *w, int id)
{
	int n = 0;
	for (int i = 0; i < w->obj.nbeam; i++) n += w->obj.beam[i].type == SAKI && w->obj.beam[i].id == id;
	return n;
}

static void test_hypers(void)
{
	aos_world *w = &W;
	/* Cyclone Cymbal §3 0x165: 5 waves x 36 at t=16..24, ages 0,-4,..-16 */
	setup(w, 0, 400);
	w->p[0].hyper = 1.0f;
	enter(w, 0, AOS_IN_H);
	CHECK(w->p[0].state == 0x165 && w->p[0].hyper < 0.01f, "H -> 0x165, -1.0 hyper");
	run_info r = run_state(w, 0, 80);
	CHECK(r.firstBeam == 16 && r.beams == 180, "180 cymbals from t=16 (t=%d n=%d)", r.firstBeam, r.beams);
	CHECK(r.endT == 41, "0x165 ends t>40 (%d)", r.endT);

	/* Galaxy Bell §3 0x166: 17 bells, damage 40 */
	setup(w, 0, 400);
	w->p[0].hyper = 1.0f;
	tick(w, AOS_IN_D);
	tick(w, AOS_IN_D | AOS_IN_H);
	CHECK(w->p[0].state == 0x166, "D+H -> 0x166 (got %#x)", w->p[0].state);
	r = run_state(w, 0, 80);
	CHECK(r.firstBeam == 8 && r.beams == 17, "17 bells from t=8 (t=%d n=%d)", r.firstBeam, r.beams);
	for (int i = 0; i < w->obj.nbeam; i++)
		if (w->obj.beam[i].id == 200 || w->obj.beam[i].id == 0xd2)
			CHECK(w->obj.beam[i].damage == 40, "galaxy bell damage 40");

	/* Rolling Tambourine §3 0x168: 10 orbiting tambourines at t=10 */
	setup(w, 0, 400);
	w->p[0].hyper = 1.0f;
	enter(w, AOS_IN_S, AOS_IN_H);
	CHECK(w->p[0].state == 0x168, "S+H -> 0x168 (got %#x)", w->p[0].state);
	r = run_state(w, 0, 80);
	CHECK(r.firstMissile == 10 && r.missiles == 10, "10 tambourines at t=10 (t=%d n=%d)", r.firstMissile, r.missiles);
	CHECK(r.endT == 61, "0x168 ends t>60 (%d)", r.endT);

	/* Sunshine Bell §3 0x167: tap -> released at t=30, ball speed 6, scale 1 */
	setup(w, 0, 400);
	w->p[0].hyper = 1.5f;
	enter(w, AOS_IN_B, AOS_IN_H);
	CHECK(w->p[0].state == 0x167, "B+H -> 0x167 (got %#x)", w->p[0].state);
	int relT = -1;
	for (int f = 0; f < 120 && w->p[0].state == 0x167; f++) {
		int t = w->p[0].timer, nb = count_ids(w, 100);
		tick(w, 0);
		if (count_ids(w, 100) > nb && relT < 0) {
			relT = t;
			for (int i = 0; i < w->obj.nbeam; i++)
				if (w->obj.beam[i].id == 100) {
					aos_beam *b = &w->obj.beam[i];
					CHECK(fabsf(b->scale - 1.0f) < 1e-5f && b->speed > 5.9f && b->speed < 6.2f,
					      "ball scale 1, speed ~6 (%f %f)", b->scale, b->speed);
				}
			CHECK(w->p[0].timer == 81, "timer forced to 80 on release (now %d)", w->p[0].timer);
		}
	}
	CHECK(relT == 30, "tap releases at t=30 (got %d)", relT);
	/* held H: released at t=80 with scale 80/30 */
	setup(w, 0, 400);
	w->p[0].hyper = 1.5f;
	enter(w, AOS_IN_B, AOS_IN_H);
	relT = -1;
	for (int f = 0; f < 120 && relT < 0; f++) {
		int t = w->p[0].timer, nb = count_ids(w, 100);
		tick(w, AOS_IN_H);
		if (count_ids(w, 100) > nb) relT = t;
	}
	CHECK(relT == 80, "hold H releases at t=80 (got %d)", relT);

	/* Big Bang Bell §3 0x169: pull 6 px/frame, blast 0x122 = 1333 at t=60 */
	setup(w, 0, 400);
	w->p[0].hyper = 3.0f;
	enter(w, AOS_IN_S | AOS_IN_B, AOS_IN_H);
	CHECK(w->p[0].state == 0x169, "S+B+H -> 0x169 (got %#x)", w->p[0].state);
	float x0 = w->p[1].x;
	tick(w, 0);
	CHECK(w->p[1].x < x0 - 3.0f, "opponent pulled toward the bell (%f -> %f)", x0, w->p[1].x);
	int hp0 = w->p[1].hp, blastT = -1;
	for (int f = 0; f < 80; f++) {
		int t = w->p[0].timer, hp = w->p[1].hp;
		tick(w, 0);
		if (hp - w->p[1].hp == 1333) blastT = t;
	}
	CHECK(blastT == 60, "1333 blast at t=60 (t=%d, total dealt %d)", blastT, hp0 - w->p[1].hp);
}

static void test_all_states_random(void)
{
	static const int states[] = {0x153, 0x154, 0x155, 0x156, 0x157, 0x15d, 0x15e, 0x15f, 0x160, 0x161,
	                             0x162, 0x163, 0x164, 0x165, 0x166, 0x167, 0x168, 0x169};
	for (size_t k = 0; k < sizeof states / sizeof states[0]; k++) {
		aos_world *w = &W;
		setup(w, 0, 200);
		aos_set_state(w, &w->p[0], states[k]);
		for (int f = 0; f < 200; f++) {
			tick(w, f < 40 ? AOS_IN_H : 0);
			draw_all(w);
			CHECK(isfinite(w->p[0].x), "state %#x non-finite", states[k]);
		}
		CHECK(states[k] <= 0x157 || w->p[0].state < 0x153, "state %#x ended (now %#x)", states[k], w->p[0].state);
	}
	uint32_t s;
	int maxGame = 0;
	for (int run = 0; run < 2; run++) {
		aos_world *w = run ? &W2 : &W;
		setup(w, run, 300);
		w->p[0].scheme = 0; /* same scheme in both runs */
		uint16_t m[2] = {0, 0};
		s = 4242;
		for (int f = 0; f < 15000; f++) {
			for (int i = 0; i < 2; i++) {
				s = s * 1664525u + 1013904223u;
				if (((s >> 8) & 3) == 0) m[i] ^= (uint16_t)(1u << ((s >> 12) % 10));
				w->p[i].hyper = 3.0f;
				w->p[i].hp = 4500;
			}
			aos_tick(w, m);
			draw_all(w);
			if (w->obj.nbeam > maxGame) maxGame = w->obj.nbeam;
			CHECK(w->obj.nmissile < AOS_MAX_MISSILE && w->obj.nhitbox < AOS_MAX_HITBOX && w->obj.neffect < AOS_MAX_EFFECT, "pool full at frame %d", f);
		}
	}
	CHECK(!memcmp(&W, &W2, sizeof W), "random hyper runs diverged");
	if (W.obj.overflow) /* sparks (pool 160) overflow when a cymbal ring hits: ENGINE REQUEST in saki report */
		printf("note: %d cosmetic spawns dropped (spark pool)\n", W.obj.overflow);
	CHECK(maxGame < AOS_MAX_BEAM, "gameplay pools stayed below capacity (max beams %d)", maxGame);
}

int main(void)
{
	aos_register_all();
	test_dispatch();
	test_tornado();
	test_stardust();
	test_grenade_boomerang();
	test_hammer_damage();
	test_blade_confirm();
	test_swing();
	test_hypers();
	test_all_states_random();
	printf("test_saki: %s (%d failures)\n", fails ? "FAIL" : "ok", fails);
	return fails != 0;
}
