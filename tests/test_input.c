/* Host-side tests for aos_input. Build: see tests/run_tests.sh */
#include "../src/input/aos_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...)                                              \
	do {                                                              \
		checks++;                                                     \
		if (!(cond)) {                                                \
			failures++;                                               \
			printf("FAIL %s:%d: ", __FILE__, __LINE__);               \
			printf(__VA_ARGS__);                                      \
			printf("\n");                                             \
		}                                                             \
	} while (0)

#define L AOS_RAW_L
#define R AOS_RAW_R

/* Feed a sequence of raw frames, return OR of all events and the per-frame
 * event log in ev[] (may be NULL). */
static uint32_t run(aos_input *s, const uint32_t *frames, int n, uint32_t *ev, uint32_t *held)
{
	uint32_t all = 0;
	for (int i = 0; i < n; i++) {
		aos_input_out o = aos_input_update(s, frames[i]);
		if (ev) ev[i] = o.events;
		if (held) held[i] = o.held;
		all |= o.events;
	}
	return all;
}

static aos_input fresh(int w, int d)
{
	aos_input s;
	aos_input_cfg c = {(uint8_t)w, (uint8_t)d};
	aos_input_init(&s, &c);
	return s;
}

static void test_dash_resolves_after_window(void)
{
	aos_input s = fresh(3, 15);
	uint32_t f[] = {L, L, L, L, L, 0}, ev[6], held[6];
	run(&s, f, 6, ev, held);
	CHECK(ev[0] == 0 && ev[1] == 0 && ev[2] == 0, "dash must wait for chord window");
	CHECK(ev[3] == AOS_EV_DASH, "dash on frame W (got %x)", ev[3]);
	CHECK(ev[4] == 0 && ev[5] == 0, "dash only once");
	CHECK(!(held[2] & AOS_HELD_DASH) && (held[3] & AOS_HELD_DASH) && (held[4] & AOS_HELD_DASH), "dash held after resolve");
	CHECK(!(held[5] & AOS_HELD_DASH), "dash held cleared on release");
}

static void test_quick_tap_resolves_on_release(void)
{
	aos_input s = fresh(3, 15);
	uint32_t f[] = {L, 0, 0, 0, 0}, ev[5];
	run(&s, f, 5, ev, NULL);
	CHECK(ev[0] == 0 && ev[1] == AOS_EV_DASH, "tap resolves on release frame (got %x %x)", ev[0], ev[1]);
	CHECK(ev[2] == 0 && ev[3] == 0, "no duplicate");
}

/* R alone = Shield, on the press frame (hypers are on the touch screen since 2026-11-10). */
static void test_r_shield(void)
{
	aos_input s = fresh(3, 15);
	uint32_t f[] = {R, R, R, R, R, 0, R}, ev[7], held[7];
	run(&s, f, 7, ev, held);
	CHECK(ev[0] == AOS_EV_SHIELD && (ev[1] | ev[2] | ev[3] | ev[4] | ev[5]) == 0, "R: shield once, at once (got %x)", ev[0]);
	for (int i = 0; i < 5; i++)
		CHECK(held[i] == AOS_HELD_SHIELD, "R held -> shield held (frame %d got %x)", i, held[i]);
	CHECK(held[5] == 0, "R released");
	CHECK(ev[6] == AOS_EV_SHIELD, "R again -> shield again");
}

static void test_shield_same_frame(void)
{
	aos_input s = fresh(3, 15);
	uint32_t f[] = {L | R, L | R, L | R, L | R, L | R, 0}, ev[6], held[6];
	uint32_t all = run(&s, f, 6, ev, held);
	CHECK(ev[0] == AOS_EV_SHIELD, "same-frame shield immediate (got %x)", ev[0]);
	CHECK(all == AOS_EV_SHIELD, "no dash/hyper with shield (got %x)", all);
	CHECK(held[4] == AOS_HELD_SHIELD, "shield held only (got %x)", held[4]);
	CHECK(held[5] == 0, "shield released");
}

static void test_shield_staggered_within_window(void)
{
	for (int lead = 0; lead < 2; lead++)
		for (int gap = 0; gap <= 3; gap++) {
			aos_input s = fresh(3, 15);
			uint32_t first = lead ? R : L, f[10], ev[10];
			for (int i = 0; i < 10; i++) f[i] = i < gap ? first : (L | R);
			uint32_t held[10];
			uint32_t all = run(&s, f, 10, ev, held);
			CHECK(all == AOS_EV_SHIELD, "lead=%d gap=%d: expected only shield, got %x", lead, gap, all);
			/* L first: the shield comes with R; R first: at once */
			CHECK(ev[lead ? 0 : gap] == AOS_EV_SHIELD, "lead=%d gap=%d: shield on the R press frame", lead, gap);
			CHECK(held[9] == AOS_HELD_SHIELD, "lead=%d gap=%d: shield held, no dash (got %x)", lead, gap, held[9]);
		}
}

static void test_no_shield_after_window(void)
{
	aos_input s = fresh(3, 15);
	uint32_t f[] = {L, L, L, L, L | R, L | R, L | R, L | R, L | R}, ev[9], held[9];
	uint32_t all = run(&s, f, 9, ev, held);
	CHECK(all == (AOS_EV_DASH | AOS_EV_SHIELD), "late R: dash, then shield (got %x)", all);
	CHECK(ev[3] == AOS_EV_DASH, "dash resolved");
	CHECK(ev[4] == AOS_EV_SHIELD, "late R shields at once (got %x)", ev[4]);
	CHECK(held[8] == (AOS_HELD_DASH | AOS_HELD_SHIELD), "both held separately (got %x)", held[8]);
}

static void test_released_before_partner_no_shield(void)
{
	/* L tapped and released the same frame R goes down: they never overlapped. */
	aos_input s = fresh(3, 15);
	uint32_t f[] = {L, R, R, R, R, 0}, ev[6];
	uint32_t all = run(&s, f, 6, ev, NULL);
	CHECK(all == (AOS_EV_DASH | AOS_EV_SHIELD), "no overlap -> L dashes, R shields (got %x)", all);
	CHECK(ev[1] == (AOS_EV_DASH | AOS_EV_SHIELD), "dash and shield on the R frame (%x)", ev[1]);
}

static void test_shield_release_one_side(void)
{
	aos_input s = fresh(3, 15);
	uint32_t f[] = {L | R, L | R, L, L, L, L, 0}, ev[7], held[7];
	uint32_t all = run(&s, f, 7, ev, held);
	CHECK(all == AOS_EV_SHIELD, "releasing R must not trigger dash (got %x)", all);
	CHECK(held[3] == 0, "nothing held after shield broken (got %x)", held[3]);
}

static void test_hyper_dash(void)
{
	aos_input s = fresh(3, 15);
	uint32_t f[] = {L, 0, 0, L, L, L, L, 0}, ev[8], held[8];
	run(&s, f, 8, ev, held);
	CHECK(ev[1] == AOS_EV_DASH, "first tap dash");
	CHECK(ev[6] == AOS_EV_HYPER_DASH, "second press hyper dash (got %x)", ev[6]);
	CHECK((held[6] & AOS_HELD_HYPER_DASH) && (held[6] & AOS_HELD_DASH), "hyper dash held");
}

static void test_hyper_dash_window_edges(void)
{
	for (int gap = 13; gap <= 17; gap++) {
		aos_input s = fresh(3, 15);
		uint32_t f[40] = {0}, ev[40];
		f[0] = L;
		f[gap] = L;
		uint32_t all = run(&s, f, 40, ev, NULL);
		int want = gap <= 15;
		CHECK(!!(all & AOS_EV_HYPER_DASH) == want, "gap=%d hyper dash=%d expected %d", gap, !!(all & AOS_EV_HYPER_DASH), want);
		CHECK(!!(all & AOS_EV_DASH), "gap=%d first dash", gap);
	}
}

static void test_triple_tap(void)
{
	aos_input s = fresh(3, 15);
	uint32_t f[] = {L, 0, L, 0, L, 0, 0}, ev[7];
	run(&s, f, 7, ev, NULL);
	CHECK(ev[1] == AOS_EV_DASH && ev[3] == AOS_EV_HYPER_DASH && ev[5] == AOS_EV_DASH,
	      "triple tap = dash, hyper dash, dash (%x %x %x)", ev[1], ev[3], ev[5]);
}

static void test_shield_cancels_double_tap(void)
{
	aos_input s = fresh(3, 15);
	uint32_t f[] = {L, 0, L | R, 0, L, 0}, ev[6];
	run(&s, f, 6, ev, NULL);
	CHECK(ev[1] == AOS_EV_DASH && ev[2] == AOS_EV_SHIELD, "dash then shield");
	CHECK(ev[5] == AOS_EV_DASH, "dash after shield is plain dash (got %x)", ev[5]);
}

static void test_zero_window(void)
{
	aos_input s = fresh(0, 15);
	uint32_t f[] = {L, L | R, 0, L | R}, ev[4];
	run(&s, f, 4, ev, NULL);
	CHECK(ev[0] == AOS_EV_DASH, "W=0 dash immediate");
	CHECK(ev[1] == AOS_EV_SHIELD, "W=0 staggered: R shields (got %x)", ev[1]);
	CHECK(ev[3] == AOS_EV_SHIELD, "W=0 same-frame shield (got %x)", ev[3]);
}

static void test_face_buttons(void)
{
	aos_input s = fresh(3, 15);
	uint32_t f[] = {AOS_RAW_A | AOS_RAW_X, AOS_RAW_A | AOS_RAW_X | AOS_RAW_B | AOS_RAW_Y, 0}, ev[3], held[3];
	run(&s, f, 3, ev, held);
	CHECK(ev[0] == (AOS_EV_WEAPON_A | AOS_EV_SPECIAL_A), "A+X immediate");
	CHECK(ev[1] == (AOS_EV_WEAPON_B | AOS_EV_SPECIAL_B), "B+Y edge only");
	CHECK(held[1] == (AOS_HELD_WEAPON_A | AOS_HELD_WEAPON_B | AOS_HELD_SPECIAL_A | AOS_HELD_SPECIAL_B), "all held");
	CHECK(held[2] == 0, "released");
}

static void test_l_after_r_joins_shield(void)
{
	/* R first, L within the window: one shield, L never dashes, even after R is let go */
	aos_input s = fresh(3, 15);
	uint32_t f[] = {R, R, L | R, L | R, L, L, L, 0}, ev[8], held[8];
	uint32_t all = run(&s, f, 8, ev, held);
	CHECK(all == AOS_EV_SHIELD && ev[0] == AOS_EV_SHIELD, "one shield (got %x)", all);
	CHECK(held[3] == AOS_HELD_SHIELD && held[5] == 0, "shield while R held, nothing after (got %x %x)", held[3], held[5]);
	/* L long after R: a dash while shielding */
	s = fresh(3, 15);
	uint32_t g[] = {R, R, R, R, R, L | R, L | R, L | R, L | R, L | R}, ev2[10], held2[10];
	all = run(&s, g, 10, ev2, held2);
	CHECK(all == (AOS_EV_SHIELD | AOS_EV_DASH) && ev2[8] == AOS_EV_DASH, "late L dashes (got %x %x)", all, ev2[8]);
	CHECK(held2[9] == (AOS_HELD_SHIELD | AOS_HELD_DASH), "both held (got %x)", held2[9]);
}

static void test_long_hold_no_overflow(void)
{
	aos_input s = fresh(3, 15);
	aos_input_out o;
	int dashes = 0;
	for (int i = 0; i < 100000; i++) {
		o = aos_input_update(&s, L);
		if (o.events & AOS_EV_DASH) dashes++;
	}
	CHECK(dashes == 1 && (o.held & AOS_HELD_DASH), "long hold: one dash, still held");
}

/* Fuzz: random bumper input. Every R press shields on its press frame; shield is held exactly while R is;
 * every L press resolves (Dash / Hyper Dash event, or silently joins an L+R shield) within chord_window
 * frames; deterministic replay. */
static void test_fuzz(void)
{
	srand(12345);
	for (int w = 0; w <= 6; w++) {
		aos_input s = fresh(w, 15), s2 = fresh(w, 15);
		uint32_t prev = 0;
		int l_open = -1; /* frame of the current L press while unresolved */
		for (int i = 0; i < 2000000; i++) {
			uint32_t raw = prev;
			int p = rand() % 100;
			if (p < 12) raw ^= L;
			else if (p < 24) raw ^= R;
			else if (p < 26) raw ^= (L | R);
			aos_input_out o = aos_input_update(&s, raw);
			aos_input_out o2 = aos_input_update(&s2, raw);
			if (o.events != o2.events || o.held != o2.held) {
				CHECK(0, "nondeterministic at %d", i);
				return;
			}
			uint32_t rise = raw & ~prev;
			if (rise & L) { if (l_open >= 0 && s.l.pending) { CHECK(0, "w=%d L press %d never resolved", w, l_open); return; } l_open = i; }
			int sh = !!(o.events & AOS_EV_SHIELD);
			if (sh != !!(rise & R)) { CHECK(0, "w=%d shield event %d without/with R press at %d", w, sh, i); return; }
			if (!!(o.held & AOS_HELD_SHIELD) != !!(raw & R)) { CHECK(0, "w=%d shield held != R at %d", w, i); return; }
			int lres = !!(o.events & (AOS_EV_DASH | AOS_EV_HYPER_DASH));
			if ((o.events & AOS_EV_DASH) && (o.events & AOS_EV_HYPER_DASH)) { CHECK(0, "dash+hyperdash at %d", i); return; }
			if (lres) { if (l_open < 0) { CHECK(0, "w=%d spurious L event at %d", w, i); return; } l_open = -1; }
			if (l_open >= 0 && !s.l.pending && !lres) l_open = -1; /* joined a shield */
			if (l_open >= 0 && i - l_open >= w) { CHECK(0, "w=%d L latency exceeded at %d", w, i); return; }
			prev = raw;
		}
		CHECK(1, "fuzz w=%d", w);
	}
}

int main(void)
{
	test_dash_resolves_after_window();
	test_quick_tap_resolves_on_release();
	test_r_shield();
	test_shield_same_frame();
	test_shield_staggered_within_window();
	test_no_shield_after_window();
	test_released_before_partner_no_shield();
	test_shield_release_one_side();
	test_hyper_dash();
	test_hyper_dash_window_edges();
	test_triple_tap();
	test_shield_cancels_double_tap();
	test_zero_window();
	test_face_buttons();
	test_l_after_r_joins_shield();
	test_long_hold_no_overflow();
	test_fuzz();
	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
