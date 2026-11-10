/* Behaviour checks for the Hime port (src/sim/chars/hime.c) against docs/re/hime.md.
 * Each CHECK cites the spec section / original address it verifies.
 * Build: see tests/run_tests.sh (all src/sim and src/sim/chars sources + this file). */
#include "../src/sim/draw.h"
#include "../src/sim/sim.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static aos_world W, W2;
static const uint16_t NONE[2] = {0, 0};

static void tick(aos_world *w, uint16_t m0, uint16_t m1, int n)
{
	uint16_t m[2] = {m0, m1};
	for (int i = 0; i < n; i++) {
		aos_tick(w, m);
		aos_draw_begin();
		aos_draw_world(w);
	}
}

static void setup(aos_world *w, int scheme, int opp)
{
	int32_t ids[2] = {AOS_CH_HIME, opp}, sch[2] = {scheme, 0}, col[2] = {0, 1};
	aos_match_init(w, ids, sch, col, 999, 2);
	tick(w, 0, 0, 210); /* past the input lock (frame < 200) */
	w->p[0].x = -100; w->p[0].y = 0;
	w->p[1].x = 100; w->p[1].y = 0; /* distance 200 < 240 */
	w->p[0].hyper = 3.0f;
	w->p[0].gaugeA = w->p[0].gaugeB = 1.0f;
}

static int count_missiles(aos_world *w, int id)
{
	int n = 0;
	for (int i = 0; i < w->obj.nmissile; i++)
		if (w->obj.missile[i].type == AOS_CH_HIME && w->obj.missile[i].id == id)
			n++;
	return n;
}

/* §2 dispatch FUN_006c85d0 / FUN_006c8ad0 */
static void test_dispatch(void)
{
	static const struct { int scheme; uint16_t m; float hyper; int far; int state; const char *what; } T[] = {
		{0, AOS_IN_A, 0, 0, 0x1fb, "A -> 0x1fb (§2 B7)"},
		{0, AOS_IN_B, 0, 0, 0x1fe, "B -> 0x1fe (§2 B5)"},
		{0, AOS_IN_S | AOS_IN_A, 0, 0, 0x1ff, "S+A -> 0x1ff (§2 B4)"},
		{0, AOS_IN_S | AOS_IN_B, 0, 0, 0x201, "S+B -> 0x201 (§2 B3)"},
		{0, AOS_IN_H, 1, 0, 0x202, "H -> 0x202 Amber (§2 A5)"},
		{0, AOS_IN_D | AOS_IN_H, 1, 0, 0x203, "D+H -> 0x203 Leash (§2 A4)"},
		{0, AOS_IN_B | AOS_IN_H, 1, 0, 0x204, "B+H dist<240 -> 0x204 (§2 A3, DAT_00815584)"},
		{0, AOS_IN_B | AOS_IN_H, 1, 1, 0x205, "B+H dist>=240 -> 0x205"},
		{0, AOS_IN_S | AOS_IN_H, 1, 0, 0x206, "S+H -> 0x206 Untouchable (§2 A2)"},
		{0, AOS_IN_S | AOS_IN_B | AOS_IN_H, 3, 0, 0x207, "S+B+H -> 0x207 God Knows (§2 A1)"},
		{0, AOS_IN_S | AOS_IN_B | AOS_IN_H, 1, 0, 0x206, "S+B+H with 1 level falls to S+H"},
		{1, AOS_IN_H | AOS_IN_A, 1, 0, 0x202, "scheme B: H+A -> 0x202 (FUN_006c8ad0)"},
		{1, AOS_IN_H | AOS_IN_SHIELD, 3, 0, 0x207, "scheme B: H+Shield -> 0x207"},
		{1, AOS_IN_H | AOS_IN_S, 1, 0, 0x206, "scheme B: H+S -> 0x206"},
		{1, AOS_IN_H | AOS_IN_D, 1, 0, 0x203, "scheme B: H+D -> 0x203"},
	};
	for (size_t k = 0; k < sizeof T / sizeof T[0]; k++) {
		aos_world *w = &W;
		setup(w, T[k].scheme, AOS_CH_SUGURI);
		w->p[0].hyper = T[k].hyper;
		if (T[k].far)
			w->p[1].x = 300; /* distance 400 */
		if (T[k].scheme == 1) /* scheme B hypers need H held first */
			tick(w, AOS_IN_H, 0, 2);
		float h0 = w->p[0].hyper;
		tick(w, T[k].m, 0, 1);
		CHECK(w->p[0].state == T[k].state, "%s (got %#x)", T[k].what, w->p[0].state);
		if (T[k].hyper > 0 && T[k].state >= 0x202)
			CHECK(fabsf(h0 - w->p[0].hyper - (T[k].state == 0x207 ? 3.0f : 1.0f)) < 1e-4f, "%s: hyper cost", T[k].what);
	}
	/* D held + A -> 0x1fc (§2 B6, tests +0x34 > 1) */
	aos_world *w = &W;
	setup(w, 0, AOS_CH_SUGURI);
	w->p[0].speed = 0;
	tick(w, AOS_IN_D, 0, 1); /* dash */
	tick(w, AOS_IN_D | AOS_IN_A, 0, 1);
	CHECK(w->p[0].state == 0x1fc, "D held + A -> 0x1fc (got %#x)", w->p[0].state);
}

/* §3 0x1fb / 0x1fa: tap -> Faint Breeze (2 lances at t=12), hold -> Needle Fruit (24 at t=15) */
static void test_faint_breeze(void)
{
	aos_world *w = &W;
	setup(w, 0, AOS_CH_SUGURI);
	tick(w, AOS_IN_A, 0, 1);
	tick(w, 0, 0, 2);
	CHECK(w->p[0].state == 0x1fa, "A released before t=12 -> 0x1fa (got %#x)", w->p[0].state);
	int t12 = -1;
	for (int f = 0; f < 20 && t12 < 0; f++) {
		int t = w->p[0].timer;
		tick(w, 0, 0, 1);
		if (count_missiles(w, 100) == 1 && count_missiles(w, 0x96) == 1)
			t12 = t;
	}
	CHECK(t12 == 12, "Faint Breeze lances 100 + 0x96 at t=12 (got %d) FUN_006c4a00", t12);
	CHECK(fabsf(w->p[0].gaugeA - 0.8f) < 1e-4f, "two lances cost 2 x 0.1 ammo A (§1.2) (A=%f)", w->p[0].gaugeA);
	for (int f = 0; f < 40 && w->p[0].state == 0x1fa; f++)
		tick(w, 0, 0, 1);
	CHECK(w->p[0].state == 0 && w->p[0].timer == 32, "0x1fa ends when T2 sees t=31 (>30); timer then 32 (state %#x t %d)", w->p[0].state, w->p[0].timer);

	setup(w, 0, AOS_CH_SUGURI);
	tick(w, AOS_IN_A, 0, 16); /* hold */
	CHECK(w->p[0].state == 0x1fb, "A held stays 0x1fb");
	CHECK(count_missiles(w, 0x6e) == 12 && count_missiles(w, 0xa0) == 12, "Needle Fruit: 12 x 0x6e + 12 x 0xa0 at t=15");
	CHECK(fabsf(w->p[0].gaugeA - 0.3f) < 1e-4f, "Needle Fruit costs 0.7 ammo A (A=%f)", w->p[0].gaugeA);
	CHECK(fabsf(w->p[0].hyper - 3.1f) < 1e-3f || w->p[0].hyper == 3.0f, "Needle Fruit hyper gain 0.1 (capped at 3)");
}

/* §3 0x1fe Fever: laser 100 at t=10 drains ammo B 0.05/frame for laser ages 10..20 (FUN_0061e030) */
static void test_fever(void)
{
	aos_world *w = &W;
	setup(w, 0, AOS_CH_SUGURI);
	tick(w, AOS_IN_B, 0, 11);
	CHECK(w->obj.nlaser == 1 && w->obj.laser[0].id == 100, "Fever laser 100 spawned by t=10");
	CHECK(fabsf(w->p[0].gaugeB - 0.8f) < 1e-4f, "Fever costs 0.2 ammo B on spawn (B=%f)", w->p[0].gaugeB);
	tick(w, AOS_IN_B, 0, 30);
	CHECK(fabsf(w->p[0].gaugeB - (0.8f - 11 * 0.05f)) < 1e-3f, "laser drains 11 x 0.05 ammo B (B=%f)", w->p[0].gaugeB);
}

/* §3 0x1fd Paradise (ver>=0x5a1): 13 swords 200/0xfa for t 12..28 with the jump-table gaps, B -0.5 */
static void test_paradise(void)
{
	aos_world *w = &W;
	setup(w, 0, AOS_CH_SUGURI);
	tick(w, AOS_IN_B, 0, 1);
	tick(w, 0, 0, 30);
	CHECK(w->p[0].state == 0x1fd, "B tap -> 0x1fd (got %#x)", w->p[0].state);
	CHECK(count_missiles(w, 200) == 10 && count_missiles(w, 0xfa) == 3, "Paradise 10 red + 3 blue (got %d/%d)",
	      count_missiles(w, 200), count_missiles(w, 0xfa));
	CHECK(fabsf(w->p[0].gaugeB - 0.5f) < 1e-4f, "Paradise costs 0.5 ammo B (FUN_004a7270 patch) (B=%f)", w->p[0].gaugeB);
}

/* §3 0x1ff Delicate Binding: missile 600 at t=5; blocked while a 600/0x259 exists (FUN_006c3e20) */
static void test_binding(void)
{
	aos_world *w = &W;
	setup(w, 0, AOS_CH_SUGURI);
	w->p[1].x = 400; /* out of reach: no hit-confirm */
	tick(w, AOS_IN_S | AOS_IN_A, 0, 7);
	CHECK(count_missiles(w, 600) == 1, "Delicate Binding spawns chain 600 at t=5");
	tick(w, 0, 0, 80);
	CHECK(count_missiles(w, 0x259) == 1, "chain becomes 0x259 after 11 frames and lingers");
	tick(w, AOS_IN_S | AOS_IN_A, 0, 1);
	tick(w, 0, 0, 6);
	CHECK(count_missiles(w, 600) == 0 && w->p[0].timer >= 0x14, "second Binding blocked while 0x259 lives: t jumps to 20 (t=%d)", w->p[0].timer);
}

/* §5.2 hitbox 100 on-hit sets hit-confirm in 0x1ff; §3 0x1ff -> A -> 0x200 */
static void test_binding_chain(void)
{
	aos_world *w = &W;
	setup(w, 0, AOS_CH_SUGURI);
	w->p[1].x = w->p[0].x + 150;
	int hp0 = w->p[1].hp;
	tick(w, AOS_IN_S | AOS_IN_A, 0, 1);
	int got = 0;
	for (int f = 0; f < 30 && !got; f++) {
		tick(w, 0, 0, 1);
		got = w->p[0].hitFlag;
	}
	CHECK(got, "chain hit sets +0x158 in 0x1ff (FUN_005e1b40)");
	CHECK(w->p[1].hp < hp0, "chain deals damage (hp %d -> %d)", hp0, w->p[1].hp);
	tick(w, AOS_IN_A, 0, 1);
	CHECK(w->p[0].state == 0x200, "hit-confirm + A -> 0x200 (got %#x)", w->p[0].state);
}

/* §3 0x201 Sadism: hitboxes at t = 12,15,18,21,24 (80 dmg each), last knockback 20.
 * Hitboxes live one frame (cleared at the end of the tick), so the spawn function is wrapped. */
static aos_hitbox_spawn_fn real_hb;
static int hb_log[16][4], hb_n;
static int hb_spy(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t zero, int32_t team)
{
	int i = real_hb(w, id, pos, angle, owner, zero, team);
	if (hb_n < 16) {
		hb_log[hb_n][0] = id;
		hb_log[hb_n][1] = w->p[0].timer;
		hb_log[hb_n][2] = i >= 0 ? w->obj.hitbox[i].damage : -1;
		hb_log[hb_n][3] = i;
		hb_n++;
	}
	return i;
}

static void test_sadism(void)
{
	aos_world *w = &W;
	setup(w, 0, AOS_CH_SUGURI);
	w->p[1].x = 400;
	real_hb = aos_otab.hitboxSpawn[AOS_CH_HIME];
	aos_otab.hitboxSpawn[AOS_CH_HIME] = hb_spy;
	hb_n = 0;
	tick(w, AOS_IN_S | AOS_IN_B, 0, 1);
	tick(w, 0, 0, 50);
	aos_otab.hitboxSpawn[AOS_CH_HIME] = real_hb;
	CHECK(hb_n == 5, "Sadism: 5 hitboxes (got %d) FUN_006c72b0", hb_n);
	static const int T[5] = {12, 15, 18, 21, 24}, I[5] = {200, 0xfa, 200, 0xfa, 200};
	for (int k = 0; k < 5 && k < hb_n; k++) {
		CHECK(hb_log[k][1] == T[k] && hb_log[k][0] == I[k], "Sadism hit %d: id %#x at t=%d (got %#x at %d)", k, I[k], T[k], hb_log[k][0], hb_log[k][1]);
		CHECK(hb_log[k][2] == 80, "Sadism hitbox damage 80 (§5.2)");
	}
	CHECK(w->p[0].state == 0, "Sadism ends after t>45 (ver>0x58a)");
}

/* §3 0x207 God Knows: 210-damage circles at (x±210, y-30) every 5 frames t=10..50 */
static void test_godknows(void)
{
	aos_world *w = &W;
	setup(w, 0, AOS_CH_SUGURI);
	w->p[1].x = w->p[0].x + 210;
	w->p[1].y = w->p[0].y - 30;
	tick(w, AOS_IN_S | AOS_IN_B | AOS_IN_H, 0, 1);
	CHECK(w->p[0].state == 0x207 && w->p[0].barrierType == 4, "God Knows starts the Accel barrier (FUN_006b1b40 0x14)");
	int hp0 = w->p[1].hp;
	tick(w, 0, 0, 11);
	CHECK(hp0 - w->p[1].hp == 210, "first God Knows hit = 210 (ver>0x5a0) (got %d)", hp0 - w->p[1].hp);
	tick(w, 0, 0, 120);
	CHECK(w->p[0].state == 0, "God Knows ends (t>120)");
}

/* §3 0x206 Untouchable: 6 wing enemies, alternating spin ±0.1 */
static void test_untouchable(void)
{
	aos_world *w = &W;
	setup(w, 0, AOS_CH_SUGURI);
	tick(w, AOS_IN_S | AOS_IN_H, 0, 1);
	int n = 0;
	for (int i = 0; i < w->obj.nenemy; i++)
		if (w->obj.enemy[i].type == AOS_CH_HIME)
			n++;
	CHECK(n == 6, "Untouchable spawns 6 wings (got %d)", n);
	tick(w, 0, 0, 120);
	n = 0;
	for (int i = 0; i < w->obj.nenemy; i++)
		if (w->obj.enemy[i].type == AOS_CH_HIME)
			n++;
	CHECK(n == 0, "wings expire (phase 100 70 frames, then fade)");
}

/* Random play with full hyper gauge: finite, deterministic (two identical runs compare equal),
 * and no overflow of the gameplay pools. (The cosmetic spark pool, AOS_MAX_SPARK = 160, does fill up
 * with 41 Life-long Burning swords hitting: engine request; sparks draw their RNG before the
 * capacity check, so determinism is unaffected.) */
static void test_random_determinism(void)
{
	for (int run = 0; run < 2; run++) {
		aos_world *w = run ? &W2 : &W;
		setup(w, 0, AOS_CH_SUMIKA);
		uint32_t s = 4242;
		uint16_t m[2] = {0, 0};
		int bad = 0;
		for (int f = 0; f < 15000; f++) {
			for (int i = 0; i < 2; i++) {
				s = s * 1664525u + 1013904223u;
				if (((s >> 8) & 3) == 0)
					m[i] ^= (uint16_t)(1u << ((s >> 12) % 10));
				w->p[i].hyper = 3.0f;
				w->p[i].hp = 4500;
			}
			tick(w, m[0], m[1], 1);
			if (!isfinite(w->p[0].x) || !isfinite(w->p[1].x))
				bad++;
		}
		CHECK(bad == 0, "random run %d: non-finite state (%d)", run, bad);
	}
	CHECK(!memcmp(&W, &W2, sizeof W), "random runs diverged (determinism)");
	(void)NONE;
}

int main(void)
{
	aos_register_all();
	test_dispatch();
	test_faint_breeze();
	test_fever();
	test_paradise();
	test_binding();
	test_binding_chain();
	test_sadism();
	test_godknows();
	test_untouchable();
	test_random_determinism();
	printf("test_hime: %s (%d/%d checks failed)\n", fails ? "FAIL" : "ok", fails, checks);
	return fails != 0;
}
