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

static void test_hyper(void)
{
	aos_input s = fresh(3, 15);
	uint32_t f[] = {R, R, R, R, R}, ev[5], held[5];
	run(&s, f, 5, ev, held);
	CHECK(ev[3] == AOS_EV_HYPER && (ev[0] | ev[1] | ev[2] | ev[4]) == 0, "hyper once on frame W");
	CHECK(held[4] & AOS_HELD_HYPER, "hyper held");
}

static void test_shield_same_frame(void)
{
	aos_input s = fresh(3, 15);
	uint32_t f[] = {L | R, L | R, L | R, L | R, L | R, 0}, ev[6], held[6];
	uint32_t all = run(&s, f, 6, ev, held);
	CHECK(ev[0] == AOS_EV_SHIELD, "same-frame shield immediate (got %x)", ev[0]);
	CHECK(all == AOS_EV_SHIELD, "no dash/hyper with shield (got %x)", all);
	CHECK((held[4] & AOS_HELD_SHIELD) && !(held[4] & (AOS_HELD_DASH | AOS_HELD_HYPER)), "shield held only");
	CHECK(held[5] == 0, "shield released");
}

static void test_shield_staggered_within_window(void)
{
	for (int lead = 0; lead < 2; lead++)
		for (int gap = 0; gap <= 3; gap++) {
			aos_input s = fresh(3, 15);
			uint32_t first = lead ? R : L, f[10], ev[10];
			for (int i = 0; i < 10; i++) f[i] = i < gap ? first : (L | R);
			uint32_t all = run(&s, f, 10, ev, NULL);
			CHECK(all == AOS_EV_SHIELD, "lead=%d gap=%d: expected only shield, got %x", lead, gap, all);
			CHECK(ev[gap] == AOS_EV_SHIELD, "lead=%d gap=%d: shield on partner frame", lead, gap);
		}
}

static void test_no_shield_after_window(void)
{
	aos_input s = fresh(3, 15);
	uint32_t f[] = {L, L, L, L, L | R, L | R, L | R, L | R, L | R}, ev[9], held[9];
	uint32_t all = run(&s, f, 9, ev, held);
	CHECK(!(all & AOS_EV_SHIELD), "late partner must not shield (got %x)", all);
	CHECK(ev[3] == AOS_EV_DASH, "dash resolved");
	CHECK(ev[7] == AOS_EV_HYPER, "late R becomes hyper (got %x)", ev[7]);
	CHECK((held[8] & AOS_HELD_DASH) && (held[8] & AOS_HELD_HYPER) && !(held[8] & AOS_HELD_SHIELD), "both held separately");
}

static void test_released_before_partner_no_shield(void)
{
	/* L tapped and released the same frame R goes down: they never overlapped. */
	aos_input s = fresh(3, 15);
	uint32_t f[] = {L, R, R, R, R, 0}, ev[6];
	uint32_t all = run(&s, f, 6, ev, NULL);
	CHECK(!(all & AOS_EV_SHIELD), "no overlap -> no shield (got %x)", all);
	CHECK(ev[1] == AOS_EV_DASH && ev[4] == AOS_EV_HYPER, "dash then hyper (%x %x)", ev[1], ev[4]);
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
	CHECK(ev[1] == AOS_EV_HYPER, "W=0 staggered is hyper (got %x)", ev[1]);
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

/* Fuzz: random bumper input; every L/R press must resolve exactly once into
 * exactly one outcome within chord_window frames; shield only while both are
 * held; deterministic replay. */
static void test_fuzz(void)
{
	srand(12345);
	for (int w = 0; w <= 6; w++) {
		aos_input s = fresh(w, 15), s2 = fresh(w, 15);
		uint32_t prev = 0;
		int l_open = -1, r_open = -1; /* frame of unresolved press */
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
			if (rise & L) { if (l_open >= 0) { CHECK(0, "w=%d L press %d never resolved", w, l_open); return; } l_open = i; }
			if (rise & R) { if (r_open >= 0) { CHECK(0, "w=%d R press %d never resolved", w, r_open); return; } r_open = i; }
			int lres = !!(o.events & (AOS_EV_DASH | AOS_EV_HYPER_DASH));
			int rres = !!(o.events & AOS_EV_HYPER);
			int sh = !!(o.events & AOS_EV_SHIELD);
			if (sh) {
				if (!((raw & L) && (raw & R) && l_open >= 0 && r_open >= 0)) { CHECK(0, "w=%d bad shield at %d", w, i); return; }
				int d = l_open > r_open ? l_open - r_open : r_open - l_open;
				if (d > w) { CHECK(0, "w=%d shield with gap %d", w, d); return; }
				if (lres || rres) { CHECK(0, "w=%d shield+single same frame at %d", w, i); return; }
				l_open = r_open = -1;
			}
			if ((o.events & AOS_EV_DASH) && (o.events & AOS_EV_HYPER_DASH)) { CHECK(0, "dash+hyperdash at %d", i); return; }
			if (lres) { if (l_open < 0) { CHECK(0, "w=%d spurious L event at %d", w, i); return; } l_open = -1; }
			if (rres) { if (r_open < 0) { CHECK(0, "w=%d spurious R event at %d", w, i); return; } r_open = -1; }
			if (l_open >= 0 && i - l_open >= w) { CHECK(0, "w=%d L latency exceeded at %d", w, i); return; }
			if (r_open >= 0 && i - r_open >= w) { CHECK(0, "w=%d R latency exceeded at %d", w, i); return; }
			if ((o.held & AOS_HELD_SHIELD) && (o.held & (AOS_HELD_DASH | AOS_HELD_HYPER))) { CHECK(0, "shield+dash held at %d", i); return; }
			prev = raw;
		}
		CHECK(1, "fuzz w=%d", w);
	}
}

int main(void)
{
	test_dash_resolves_after_window();
	test_quick_tap_resolves_on_release();
	test_hyper();
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
	test_long_hold_no_overflow();
	test_fuzz();
	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
